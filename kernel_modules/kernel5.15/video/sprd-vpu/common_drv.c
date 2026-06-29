/*
*SPDX-FileCopyrightText: 2019 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier: GPL-2.0-only
*/

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma-heap.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/sprd_iommu.h>
#include <linux/sprd_ion.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/notifier.h>
#include <linux/compat.h>
#include <linux/of.h>
#include "vpu_drv.h"
#include "vpu_sys.h"

#define LEN_MAX 100

struct vpu_iommu_map_entry {
	struct list_head list;

	int fd;
	unsigned long iova_addr;
	size_t iova_size;
	uint8_t  need_cache_sync;

	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	void *inst_ptr;
};

struct system_heap_buffer {
	struct dma_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table sg_table;
	int vmap_cnt;
	void *vaddr;

	bool uncached;
};

void vpu_qos_config(struct vpu_platform_data *data)
{
	unsigned int i = 0, vpu_qos_num = 0, dpu_vpu_qos_num = 0;
	int reg_val;
	struct vpu_qos_reg *vpu_mtx_qos = NULL;
	const struct vpu_ops *ops = data->p_data->ops;

	/*static volatile unsigned int *base_addr_virt;*/
	unsigned int *base_addr_virt;

	ops->clock_enable(data);

	if (data->version == QOGIRN6PRO) {
		vpu_mtx_qos = vpu_mtx_qos_qogirn6pro;
		vpu_qos_num = ARRAY_SIZE(vpu_mtx_qos_qogirn6pro);
	} else if (data->version == QOGIRN6L) {
		vpu_mtx_qos = vpu_mtx_qos_qogirn6lite;
		vpu_qos_num = ARRAY_SIZE(vpu_mtx_qos_qogirn6lite);
	} else {
		pr_info("No vpu qos config");
		ops->clock_disable(data);
		return;
	}

	dpu_vpu_qos_num = ARRAY_SIZE(dpu_vpu_mtx_qos);//? N6L

	for (i = 0; i < vpu_qos_num; i++) {
		base_addr_virt = ioremap(VPU_SOC_QOS_BASE + vpu_mtx_qos[i].offset, 4);
		reg_val = readl_relaxed((void __iomem *)base_addr_virt);
		writel_relaxed((((reg_val) & (~vpu_mtx_qos[i].mask))
		| (vpu_mtx_qos[i].value)), (void __iomem *)base_addr_virt);
		iounmap(base_addr_virt);
	}

	for (i = 0; i < dpu_vpu_qos_num; i++) {
		base_addr_virt = ioremap(DPU_VPU_SOC_QOS_BASE + dpu_vpu_mtx_qos[i].offset, 4);
		reg_val = readl_relaxed((void __iomem *)base_addr_virt);
		writel_relaxed((((reg_val) & (~dpu_vpu_mtx_qos[i].mask))
		| (dpu_vpu_mtx_qos[i].value)), (void __iomem *)base_addr_virt);
		iounmap(base_addr_virt);
	}

	ops->clock_disable(data);
}

static int handle_common_interrupt(struct vpu_platform_data *data, int *status)
{
	int i;
	int int_status;
	int mmu_status;
	struct device *dev = data->dev;
	struct mmu_reg *mmu_reg = data->p_data->mmu_reg;

	int_status = readl_relaxed(data->glb_reg_base + VPU_INT_RAW_OFF);

	if (mmu_reg->mmu_int_en_off) {
		mmu_status = readl_relaxed(data->vpu_base + mmu_reg->mmu_int_raw_off);
		*status |= int_status | (mmu_status << 16);
	} else {
		/* for sharkle and pike2 */
		*status = int_status;
		mmu_status = (int_status >> 10) & 0xff;
		int_status = *status & 0x3ff;
	}

	if (((int_status & 0xffff) == 0) &&
		((mmu_status & 0xff) == 0)) {
		dev_info(dev, "%s dec IRQ_NONE int_status 0x%x 0x%x",
			__func__, int_status, mmu_status);
		return IRQ_NONE;
	}

	if (int_status & DEC_BSM_OVF_ERR)
		dev_err(dev, "dec_bsm_overflow");

	if (int_status & DEC_VLD_ERR)
		dev_err(dev, "dec_vld_err");

	if (int_status & DEC_TIMEOUT_ERR)
		dev_err(dev, "dec_timeout");

	if (int_status & DEC_MMU_INT_ERR)
		dev_err(dev, "dec_mmu_int_err");

	if (int_status & DEC_AFBCD_HERR)
		dev_err(dev, "dec_afbcd_herr");

	if (int_status & DEC_AFBCD_PERR)
		dev_err(dev, "dec_afbcd_perr");

	if (mmu_status & MMU_RD_WR_ERR) {
		/* mmu ERR */
		dev_err(dev, "dec iommu addr: 0x%x\n",mmu_status);

		for (i = mmu_reg->mmu_vaor_addr_rd; i <= mmu_reg->mmu_uns_addr_wr; i += 4)
			dev_info(dev, "addr 0x%x is 0x%x\n", i,
				readl_relaxed(data->vpu_base + i));

		for (i = mmu_reg->mmu_vpn_paor_rd; i <= mmu_reg->mmu_ppn_paor_wr; i += 4)
			dev_info(dev, "addr 0x%x is 0x%x\n", i,
				readl_relaxed(data->vpu_base + i));
		WARN_ON_ONCE(1);
	}

	/* clear VSP accelerator interrupt bit */
	clr_vpu_interrupt_mask(data);

	return IRQ_HANDLED;
}

static int handle_vpu_enc_interrupt(struct vpu_platform_data *data, int *status)
{
	int i;
	int int_status;
	int mmu_status;
	struct device *dev = data->dev;
	struct mmu_reg *mmu_reg = data->p_data->mmu_reg;

	int_status = readl_relaxed(data->glb_reg_base + VPU_INT_RAW_OFF);
	mmu_status = readl_relaxed(data->vpu_base + mmu_reg->mmu_int_raw_off);
	*status |= int_status | (mmu_status << 16);

	if (((int_status & 0x7f) == 0) &&
		((mmu_status & 0xff) == 0)) {
		dev_info(dev, "%s enc IRQ_NONE int_status 0x%x 0x%x",
			__func__, int_status, mmu_status);
		return IRQ_NONE;
	}

	if (int_status & ENC_BSM_OVF_ERR)
		dev_err(dev, "enc_bsm_overflow");

	if (int_status & ENC_TIMEOUT_ERR)
		dev_err(dev, "enc_timeout");

	if (int_status & ENC_AFBCD_HERR)
		dev_err(dev, "enc_afbcd_herr");

	if (int_status & ENC_AFBCD_PERR)
		dev_err(dev, "enc_afbcd_perr");

	if (int_status & ENC_MMU_INT_ERR)
		dev_err(dev, "enc_mmu_int_err");

	if (mmu_status & MMU_RD_WR_ERR) {
		/* mmu ERR */
		dev_err(dev, "enc iommu addr: 0x%x\n",mmu_status);

		for (i = mmu_reg->mmu_vaor_addr_rd; i <= mmu_reg->mmu_uns_addr_wr; i += 4)
			dev_info(dev, "addr 0x%x is 0x%x\n", i,
				readl_relaxed(data->vpu_base + i));

		for (i = mmu_reg->mmu_vpn_paor_rd; i <= mmu_reg->mmu_ppn_paor_wr; i += 4)
			dev_info(dev, "addr 0x%x is 0x%x\n", i,
				readl_relaxed(data->vpu_base + i));
		WARN_ON_ONCE(1);
	}

	/* clear VSP accelerator interrupt bit */
	clr_vpu_interrupt_mask(data);

	return IRQ_HANDLED;
}

void clr_vpu_interrupt_mask(struct vpu_platform_data *data)
{
	int vpu_int_mask = 0;
	int mmu_int_mask = 0;
	int cmd = 0;
	struct mmu_reg *mmu_reg = data->p_data->mmu_reg;

	if (mmu_reg->mmu_int_en_off) {
		vpu_int_mask = 0x1fff;
		mmu_int_mask = 0xff;
	} else {
		/* use for PIKE2, SHARKLE or the chip before them */
		/*set the interrupt mask 0 */
		cmd = readl_relaxed(data->vpu_base + ARM_INT_MASK_OFF);
		cmd &= ~0x4;
		writel_relaxed(cmd, data->vpu_base + ARM_INT_MASK_OFF);
		writel_relaxed(BIT(2), data->vpu_base + ARM_INT_CLR_OFF);
		vpu_int_mask = 0x3ffff;
	}

	/* set the interrupt mask 0 */
	writel_relaxed(0, data->glb_reg_base + VPU_INT_MASK_OFF);
	if (mmu_reg->mmu_int_en_off) {
		writel_relaxed(0, data->vpu_base + mmu_reg->mmu_int_en_off);
	}

	/* clear vsp int */
	writel_relaxed(vpu_int_mask, data->glb_reg_base + VPU_INT_CLR_OFF);
	if (mmu_reg->mmu_int_en_off) {
		writel_relaxed(mmu_int_mask, data->vpu_base + mmu_reg->mmu_int_clr_off);
	}

}

static irqreturn_t common_isr_handler(struct vpu_platform_data *data)
{
	int ret, status = 0;
	struct vpu_fp *inst_ptr = NULL;

	if (data == NULL) {
		pr_err("%s error occurred, data == NULL\n", __func__);
		return IRQ_NONE;
	}
	inst_ptr = data->inst_ptr;

	if (inst_ptr == NULL) {
		dev_err(data->dev, "%s error occurred, inst_ptr == NULL\n", __func__);
		return IRQ_HANDLED;
	}

	if (inst_ptr->is_clock_enabled == false) {
		dev_err(data->dev, " vpu clk is disabled");
		return IRQ_HANDLED;
	}

	/* check which module occur interrupt and clear corresponding bit */
	ret = handle_common_interrupt(data, &status);
	if (ret == IRQ_NONE)
		return IRQ_NONE;

	data->vpu_int_status = status;
	data->condition_work = 1;
	wake_up_interruptible(&data->wait_queue_work);

	return IRQ_HANDLED;
}

static irqreturn_t vpu_enc_isr_handler(struct vpu_platform_data *data)
{
	int ret, status = 0;
	struct vpu_fp *inst_ptr = NULL;

	if (data == NULL) {
		pr_err("%s error occurred, data == NULL\n", __func__);
		return IRQ_NONE;
	}
	inst_ptr = data->inst_ptr;

	if (inst_ptr == NULL) {
		dev_err(data->dev, "%s error occurred, inst_ptr == NULL\n", __func__);
		return IRQ_HANDLED;
	}

	if (inst_ptr->is_clock_enabled == false) {
		dev_err(data->dev, " vpu clk is disabled");
		return IRQ_HANDLED;
	}

	/* check which module occur interrupt and clear corresponding bit */
	ret = handle_vpu_enc_interrupt(data, &status);
	if (ret == IRQ_NONE)
		return IRQ_NONE;

	data->vpu_int_status = status;
	data->condition_work = 1;
	wake_up_interruptible(&data->wait_queue_work);

	return IRQ_HANDLED;
}

irqreturn_t enc_core0_isr(int irq, void *data)
{
	struct vpu_platform_data *vpu_core = data;
	int ret = 0;

	dev_dbg(vpu_core->dev, "%s, isr", vpu_core->p_data->name);

	ret = vpu_enc_isr_handler(data);

	/*Do enc core0 specified work here, if needed.*/

	return ret;
}

irqreturn_t enc_core1_isr(int irq, void *data)
{
	struct vpu_platform_data *vpu_core = data;
	int ret = 0;
	dev_dbg(vpu_core->dev, "%s, isr", vpu_core->p_data->name);

	ret = vpu_enc_isr_handler(data);

	/*Do enc core1 specified work here, if needed.*/

	return ret;
}

irqreturn_t common_isr(int irq, void *data)
{
	struct vpu_platform_data *vpu_core = data;
	int ret = 0;

	dev_dbg(vpu_core->dev, "%s, isr", vpu_core->p_data->name);

	ret = common_isr_handler(data);

	/*Do dec core0 specified work here, if needed.*/

	return ret;
}

struct clk *get_clk_src_name(struct clock_name_map_t clock_name_map[],
				unsigned int freq_level,
				unsigned int max_freq_level)
{
	if (freq_level >= max_freq_level) {
		pr_info("set freq_level to max_freq_level\n");
		freq_level = max_freq_level - 1;
	}

	pr_debug("VPU_CONFIG_FREQ %d freq_level_name %s\n", freq_level,
		 clock_name_map[freq_level].name);
	return clock_name_map[freq_level].clk_parent;
}

int find_freq_level(struct clock_name_map_t clock_name_map[],
			unsigned long freq,
			unsigned int max_freq_level)
{
	int level = 0;
	int i;

	for (i = 0; i < max_freq_level; i++) {
		if (clock_name_map[i].freq == freq) {
			level = i;
			break;
		}
	}
	return level;
}

#ifdef CONFIG_COMPAT
long compat_vpu_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	if (!filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	return filp->f_op->unlocked_ioctl(filp, cmd, (unsigned long)
						  compat_ptr(arg));
}
#endif

void non_free_bufs_check(void *inst_ptr, struct vpu_platform_data *data)
{
	struct vpu_iommu_map_entry *entry = NULL;
	struct sprd_iommu_unmap_data unmapdata = {0};
	const struct vpu_ops *ops = data->p_data->ops;
	int ret = 0;
	int b_find = 0;

	ops->clock_enable(data);
	mutex_lock(&data->map_lock);

	do {
		b_find  = 0;
		list_for_each_entry(entry, &data->map_list, list) {
			if (entry->inst_ptr == inst_ptr) {
				unmapdata.iova_addr = entry->iova_addr;
				unmapdata.iova_size = entry->iova_size;
				unmapdata.ch_type = SPRD_IOMMU_FM_CH_RW;
				unmapdata.buf = NULL;
				b_find = 1;
				list_del(&entry->list);
				break;
			}
		}
		if (b_find) {
			dev_info(data->dev, "%s, inst %p, iova_addr=%#lx, size=%zu)\n", __func__,
				entry->inst_ptr, entry->iova_addr, entry->iova_size);

			ret = sprd_iommu_unmap(data->dev, &unmapdata);
			if (ret) {
				dev_err(data->dev, "sprd_iommu_unmap failed: ret=%d, iova_addr=%#lx, size=%zu\n",
					ret, unmapdata.iova_addr, unmapdata.iova_size);
			}

			if (entry->need_cache_sync) {
				dma_buf_unmap_attachment(entry->attachment, entry->table, DMA_BIDIRECTIONAL);
				dma_buf_detach(entry->dmabuf, entry->attachment);
			} else {
				dev_info(data->dev, "free_iova, bypass dma_buf_unmap_attachment and dma_buf_detach\n");
			}
			dma_buf_put(entry->dmabuf);

			kfree(entry);
		}
	} while (b_find);

	mutex_unlock(&data->map_lock);
	ops->clock_disable(data);
}

void vsp_check_pw_status(struct vpu_platform_data *data)
{
	int ret = 0;
	u32 dpu_vsp_apb_regs = 0;

	regmap_read(data->regs[RESET].gpr, 0x0, &dpu_vsp_apb_regs); /*dev_eb*/

	if ((dpu_vsp_apb_regs & data->p_data->dev_eb_mask) !=
			data->p_data->dev_eb_mask) {
		dev_err(data->dev, "dpu_vsp_apb_regs APB_EB dev_eb 0x%x\n", dpu_vsp_apb_regs);
		ret = regmap_update_bits(data->regs[RESET].gpr, 0x0,
					data->p_data->dev_eb_mask, data->p_data->dev_eb_mask);
	}

}

int vsp_get_dmabuf(int fd, struct dma_buf **dmabuf, void **buf, size_t *size)
{
	struct system_heap_buffer *buffer = NULL;

	if (fd < 0 && !dmabuf) {
		pr_err("%s, input fd: %d, dmabuf: %p error\n", __func__, fd, dmabuf);
		return -EINVAL;
	}

	if (fd >= 0) {
		*dmabuf = dma_buf_get(fd);
		if (IS_ERR_OR_NULL(*dmabuf)) {
			pr_err("%s, dmabuf error: %p !\n", __func__, *dmabuf);
			return PTR_ERR(*dmabuf);
		}
		buffer = (*dmabuf)->priv;
	} else {
		buffer = (*dmabuf)->priv;
	}

	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	*buf = (void *)buffer;
	*size = buffer->len;

	return 0;
}

int get_iova(void *inst_ptr, struct vpu_platform_data *data,
		 struct iommu_map_data *mapdata, void __user *arg)
{
	int ret = 0;
	struct sprd_iommu_map_data iommu_map_data = {0};
	struct sprd_iommu_unmap_data iommu_ummap_data = {0};
	struct device *dev = data->dev;
	struct dma_buf *dmabuf = NULL;
	struct dma_buf_attachment *attachment = NULL;
	struct sg_table *table = NULL;
	struct vpu_iommu_map_entry *entry = NULL;
	const struct vpu_ops *ops = data->p_data->ops;

	ops->clock_enable(data);
	ops->check_pw_status(data);
	ret = vsp_get_dmabuf(mapdata->fd, &dmabuf,
					&(iommu_map_data.buf),
					&iommu_map_data.iova_size);

	if (ret) {
		pr_err("vpu_get_dmabuf failed: ret=%d\n", ret);
		goto err_get_dmabuf;
	}

	if (mapdata->need_cache_sync) {
		attachment = dma_buf_attach(dmabuf, data->dev);
		if (IS_ERR_OR_NULL(attachment)) {
			pr_err("Failed to attach dmabuf=%p\n", dmabuf);
			ret = PTR_ERR(attachment);
			goto err_attach;
		}

		table = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);
		if (IS_ERR_OR_NULL(table)) {
			pr_err("Failed to map attachment=%p\n", attachment);
			ret = PTR_ERR(table);
			goto err_map_attachment;
		}
	} else
		dev_dbg(dev, "get_iova, bypass dma_buf_attach and dma_buf_map_attachment\n");

	iommu_map_data.ch_type = SPRD_IOMMU_FM_CH_RW;
	ret = sprd_iommu_map(data->dev, &iommu_map_data);
	if (!ret) {
		mutex_lock(&data->map_lock);
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			mutex_unlock(&data->map_lock);
			pr_err("fatal error! kzalloc fail!\n");
			iommu_ummap_data.iova_addr = iommu_map_data.iova_addr;
			iommu_ummap_data.iova_size = iommu_map_data.iova_size;
			iommu_ummap_data.ch_type = SPRD_IOMMU_FM_CH_RW;
			iommu_ummap_data.buf = NULL;
			ret = -ENOMEM;
			goto err_kzalloc;
		}
		entry->fd = mapdata->fd;
		entry->iova_addr = iommu_map_data.iova_addr;
		entry->iova_size = iommu_map_data.iova_size;
		entry->dmabuf = dmabuf;
		entry->need_cache_sync = mapdata->need_cache_sync;
		if (mapdata->need_cache_sync) {
			entry->attachment = attachment;
			entry->table = table;
		}
		entry->inst_ptr = inst_ptr;
		list_add(&entry->list, &data->map_list);
		mutex_unlock(&data->map_lock);

		mapdata->iova_addr = iommu_map_data.iova_addr;
		mapdata->size = iommu_map_data.iova_size;
		dev_dbg(dev, "vpu iommu map success iova addr 0x%llx size 0x%llx\n",
			mapdata->iova_addr, mapdata->size);
		ret = copy_to_user((void __user *)arg, (void *)mapdata,
					sizeof(struct iommu_map_data));
		if (ret) {
			dev_err(dev, "fatal error! copy_to_user failed, ret=%d\n", ret);
			goto err_copy_to_user;
		}
	} else {
		dev_err(dev, "vpu iommu map failed, ret=%d, map_size=%zu\n",
			ret, iommu_map_data.iova_size);
		goto err_iommu_map;
	}
	ops->clock_disable(data);
	return ret;

err_copy_to_user:
		mutex_lock(&data->map_lock);
		list_del(&entry->list);
		kfree(entry);
		mutex_unlock(&data->map_lock);
err_kzalloc:
		ret = sprd_iommu_unmap(data->dev, &iommu_ummap_data);
		if (ret) {
			pr_err("sprd_iommu_unmap failed, ret=%d, addr&size: 0x%lx 0x%zx\n",
				ret, iommu_ummap_data.iova_addr, iommu_ummap_data.iova_size);
		}
err_iommu_map:
		if (mapdata->need_cache_sync)
			dma_buf_unmap_attachment(attachment, table, DMA_BIDIRECTIONAL);
err_map_attachment:
		if (mapdata->need_cache_sync)
			dma_buf_detach(dmabuf, attachment);
err_attach:
		dma_buf_put(dmabuf);
err_get_dmabuf:
		ops->clock_disable(data);

		return ret;
}

int free_iova(void *inst_ptr, struct vpu_platform_data *data,
		  struct iommu_map_data *ummapdata)
{
	int ret = 0;
	struct vpu_iommu_map_entry *entry = NULL;
	struct sprd_iommu_unmap_data iommu_ummap_data = {0};
	const struct vpu_ops *ops = data->p_data->ops;
	int b_find = 0;

	ops->clock_enable(data);
	mutex_lock(&data->map_lock);
	list_for_each_entry(entry, &data->map_list, list) {
		if (entry->iova_addr == ummapdata->iova_addr &&
			entry->iova_size == ummapdata->size &&
			entry->inst_ptr == inst_ptr) {
			b_find = 1;
			break;
		}
	}

	if (b_find) {
		iommu_ummap_data.iova_addr = entry->iova_addr;
		iommu_ummap_data.iova_size = entry->iova_size;
		iommu_ummap_data.ch_type = SPRD_IOMMU_FM_CH_RW;
		iommu_ummap_data.buf = NULL;
		list_del(&entry->list);
		pr_debug("success to find node(inst %p, iova_addr=%#llx, size=%llu)\n",
			inst_ptr, ummapdata->iova_addr, ummapdata->size);
	} else {
		pr_err("fatal error! not find node(inst %p, iova_addr=%#llx, size=%llu)\n",
				inst_ptr, ummapdata->iova_addr, ummapdata->size);
		mutex_unlock(&data->map_lock);
		ops->clock_disable(data);
		return -EFAULT;
	}
	mutex_unlock(&data->map_lock);

	ret = sprd_iommu_unmap(data->dev, &iommu_ummap_data);
	if (ret) {
		pr_err("sprd_iommu_unmap failed: ret=%d, iova_addr=%#llx, size=%llu\n",
			ret, ummapdata->iova_addr, ummapdata->size);
		ops->clock_disable(data);
		return ret;
	}
	pr_debug("sprd_iommu_unmap success: iova_addr=%#llx size=%llu\n",
		ummapdata->iova_addr, ummapdata->size);

	if (ummapdata->need_cache_sync) {
		dma_buf_unmap_attachment(entry->attachment, entry->table, DMA_BIDIRECTIONAL);
		dma_buf_detach(entry->dmabuf, entry->attachment);
	} else
		dev_dbg(data->dev, "free_iova, bypass dma_buf_unmap_attachment and dma_buf_detach\n");

	dma_buf_put(entry->dmabuf);
	kfree(entry);

	ops->clock_disable(data);

	return ret;
}

u32 get_reset_mask(struct vpu_platform_data *data)
{
	return data->regs[RESET].mask;
}

void get_freq_clk(struct vpu_platform_data *data, struct device_node *np)
{
	int i, j = 0;
	struct clk *clk_parent;
	struct device *dev = data->dev;

	for (i = 0; i < ARRAY_SIZE(vpu_clk_src); i++) {
		//struct clk *clk_parent;
		unsigned long frequency;

		clk_parent = of_clk_get_by_name(np, vpu_clk_src[i]);
		if (IS_ERR_OR_NULL(clk_parent)) {
			dev_info(dev, "clk %s not found,continue to find next clock\n",
				vpu_clk_src[i]);
			continue;
		}
		frequency = clk_get_rate(clk_parent);

		data->clock_name_map[j].name = vpu_clk_src[i];
		data->clock_name_map[j].freq = frequency;
		data->clock_name_map[j].clk_parent = clk_parent;

		dev_info(dev, "vpu clk in dts file: clk[%d] = (%ld, %s)\n", j,
			frequency, data->clock_name_map[j].name);
		j++;
	}
	data->max_freq_level = j;

}

int get_eb_clk_lite(struct vpu_platform_data *data, struct device_node *np)
{
	int ret = 0;
	struct clk *core_clk;
	struct clk *clk_ahb_gate_vsp_eb;
	struct device *dev = data->dev;

	core_clk = devm_clk_get(data->dev, "clk_vsp");

	if (IS_ERR_OR_NULL(core_clk)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n", "core_clk",
		       core_clk);
		ret = -EINVAL;
		data->clk.core_clk = NULL;
		goto errout;
	} else
		data->clk.core_clk = core_clk;

	clk_ahb_gate_vsp_eb = devm_clk_get(data->dev, "clk_ahb_gate_vsp_eb");
	if (IS_ERR_OR_NULL(clk_ahb_gate_vsp_eb)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n", "clk_ahb_gate_vsp_eb",
			   clk_ahb_gate_vsp_eb);
		ret = -EINVAL;
		data->clk.clk_ahb_gate_vsp_eb = NULL;
		goto errout;
	} else
		data->clk.clk_ahb_gate_vsp_eb = clk_ahb_gate_vsp_eb;
	dev_err(dev, "for sharkle sharkl5 sharkl5pro sharkl6");
	/*for sharkle sharkl5 sharkl5pro sharkl6*/

errout:
	return ret;
}

int clock_enable_lite(struct vpu_platform_data *data)
{
	int ret = 0;
	struct vpu_clk *clk = &data->clk;
	struct device *dev = data->dev;

    if (clk->clk_ahb_gate_vsp_eb) {
		ret = clk_prepare_enable(clk->clk_ahb_gate_vsp_eb);
		if (ret) {
			dev_err(dev, "vsp clk_ahb_gate_vsp_eb: clk_enable failed!\n");
			goto error1;
		}
		dev_dbg(dev, "vsp clk_ahb_gate_vsp_eb: clk_prepare_enable ok.\n");
	}
	ret = clk_set_parent(clk->core_clk, clk->core_parent_clk);
		if (ret) {
			dev_err(dev, "clock[%s]: clk_set_parent() failed!", "clk_core");
			goto error2;
		}

	ret = clk_prepare_enable(clk->core_clk);
	if (ret) {
		dev_err(dev, "core_clk: clk_prepare_enable failed!\n");
		goto error2;
	}
	dev_dbg(dev, "vsp_clk: clk_prepare_enable ok.\n");

	dev_dbg(data->dev, "%s %d,OK\n", __func__, __LINE__);
	return ret;

error2:
	clk_disable_unprepare(clk->clk_ahb_gate_vsp_eb);
error1:
	return ret;

}

void clock_disable_lite(struct vpu_platform_data *data)
{
	struct vpu_clk *clk = &data->clk;

	clk_disable_unprepare(clk->core_clk);
	clk_disable_unprepare(clk->clk_ahb_gate_vsp_eb);

	dev_dbg(data->dev, "%s %d,OK\n", __func__, __LINE__);
}

