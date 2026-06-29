/*
 * Copyright (C) 2021-2022 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/version.h>
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
#include <soc/unisoc/sprd_iommu.h>
#else
#include <linux/sprd_iommu.h>
#include <linux/sprd_ion.h>
#endif
#include "cam_queue.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_BUF: %d %d %s : " fmt, current->pid, __LINE__, __func__

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
MODULE_IMPORT_NS(DMA_BUF);
#endif

static struct cam_mem_dbg_info s_mem_dbg;
struct cam_mem_dbg_info *g_mem_dbg = &s_mem_dbg;
static atomic_t s_dev_cnt;
struct iommudev_info {
	enum cam_buf_iommudev_type type;
	int32_t iommu_en;
	struct device *dev;
};
static struct iommudev_info s_iommudevs[CAM_BUF_IOMMUDEV_MAX];

static struct iommudev_info *cambuf_iommudev_get(enum cam_buf_iommudev_type type, struct device *dev)
{
	uint32_t i = 0;
	struct iommudev_info *cur = NULL;

	if (dev == NULL) {
		if (type >= CAM_BUF_IOMMUDEV_MAX)
			return NULL;

		cur = &s_iommudevs[type];
		if ((cur->type == type) && (cur->dev != NULL))
			return cur;

	} else {
		for (i = 0; i < CAM_BUF_IOMMUDEV_MAX; i++) {
			cur = &s_iommudevs[i];
			if (cur->dev == dev)
				return cur;
		}
	}

	return NULL;
}

static void cambuf_adapt_ion_free(struct dma_buf *dmabuf)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	/*dma_buf_put encapsulation calls ion_free to release*/
	dma_buf_put(dmabuf);
#else
	ion_free(dmabuf);
#endif
}

static int cambuf_adapt_buffer_alloc(struct camera_buf *buf_info,
	size_t size, unsigned int iommu_enable, unsigned int flag)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	int ret = 0;
	const char *heap_name = NULL;
	struct dma_heap *dmaheap = NULL;

	if (buf_info->buf_sec)
		heap_name = "carveout_fd";
	else {
		if (iommu_enable)
			if(flag)
				heap_name = "system";
			else
				heap_name = "system-uncached";
		else
			heap_name = "carveout_mm";
	}

	dmaheap = dma_heap_find(heap_name);
	if (dmaheap == NULL) {
		pr_err("fail to get dmaheap %s\n", heap_name);
		ret = -ENOMEM;
		return ret;
	}
	buf_info->dmabuf_p = dma_heap_buffer_alloc(dmaheap, size, O_RDWR | O_CLOEXEC, 0);
	return ret;
#elif (KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE)
	int ret = 0;
	int heap_type = 0;

	if (buf_info->buf_sec)
		heap_type = ION_HEAP_ID_MASK_CAM;
	else
		heap_type = iommu_enable ?
				ION_HEAP_ID_MASK_SYSTEM :
				ION_HEAP_ID_MASK_MM;

	buf_info->dmabuf_p = ion_alloc(size, heap_type, flag);
	return ret;
#else
	int ret = 0;
	int heap_type = 0;

	if (buf_info->buf_sec)
		heap_type = ION_HEAP_ID_MASK_CAM;
	else
		heap_type = iommu_enable ?
				ION_HEAP_ID_MASK_SYSTEM :
				ION_HEAP_ID_MASK_MM;

	buf_info->dmabuf_p = ion_new_alloc(size, heap_type, flag);
	return ret;
#endif
}

static void cambuf_adapt_buffer_free(struct dma_buf *dmabuf)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	dma_heap_buffer_free(dmabuf);
#else
	cambuf_adapt_ion_free(dmabuf);
#endif
}

static int cambuf_adapt_ion_get_buffer(int fd, enum cam_en_status buf_sec, struct dma_buf *dmabuf,
		void **buf, size_t *size)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	int ret = 0;

	if (buf_sec)
		ret = sprd_dmabuf_get_carvebuffer(fd, dmabuf, buf, size);
	else
		ret = sprd_dmabuf_get_sysbuffer(fd, dmabuf, buf, size);

	return ret;
#else
	int ret = 0;

	ret = sprd_ion_get_buffer(fd, dmabuf, buf, size);
	return ret;
#endif
}

static int cambuf_adapt_phys_get_addr(int fd, struct dma_buf *dmabuf,
		unsigned long *phys_addr, size_t *size)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	int ret = 0;

	ret = sprd_dmabuf_get_phys_addr(fd, dmabuf, phys_addr, size);
	return ret;
#else
	int ret = 0;

	ret = sprd_ion_get_phys_addr(fd, dmabuf, phys_addr, size);
	return ret;
#endif
}

static int cambuf_adapt_buf_kmap(struct camera_buf *buf_info)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	int ret = 0;

	ret = sprd_dmabuf_map_kernel(buf_info->dmabuf_p, &buf_info->map);
	buf_info->addr_k = (unsigned long)buf_info->map.vaddr;

	return ret;
#else
	int ret = 0;

	buf_info->addr_k = (unsigned long)
		sprd_ion_map_kernel(buf_info->dmabuf_p, 0);
	return ret;
#endif
}

static int cambuf_adapt_buf_kunmap(struct camera_buf *buf_info)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	int ret = 0;

	ret = sprd_dmabuf_unmap_kernel(buf_info->dmabuf_p, &buf_info->map);
	return ret;
#else
	int ret = 0;

	ret = sprd_ion_unmap_kernel(buf_info->dmabuf_p, 0);
	return ret;
#endif
}

int cam_buf_mdbg_check(void)
{
	int val[9] = {0};

	val[0] = atomic_read(&g_mem_dbg->ion_alloc_cnt);
	val[1] = atomic_read(&g_mem_dbg->ion_kmap_cnt);
	val[2] = atomic_read(&g_mem_dbg->ion_dma_cnt);
	val[3] = atomic_read(&g_mem_dbg->empty_frm_cnt);
	val[4] = atomic_read(&g_mem_dbg->iommu_map_cnt[0]);
	val[5] = atomic_read(&g_mem_dbg->iommu_map_cnt[1]);
	val[6] = atomic_read(&g_mem_dbg->mem_kzalloc_cnt);
	val[7] = atomic_read(&g_mem_dbg->mem_vzalloc_cnt);
	val[8] = atomic_read(&g_mem_dbg->ion_alloc_size);

	pr_info("mdbg info1: k_alloc: %d, k_map: %d, u_ion: %d, frm: %d, isp_map: %d\n",
		val[0], val[1], val[2], val[3], val[4]);

	pr_info("mdbg info2: dcam_map: %d, kzalloc: %d, vzalloc: %d, k_alloc_size: %d\n",
		val[5], val[6], val[7], val[8]);

	return 0;
}

int cam_buf_alloc(struct camera_buf *buf_info, size_t size, unsigned int iommu_enable)
{
	int ret = 0;
	unsigned int flag = 0;

	if (!buf_info) {
		pr_err("fail to get buffer info ptr\n");
		return -EFAULT;
	}

	if (iommu_enable & CAM_BUF_CAHCED)
		flag = 1;
	iommu_enable &= ~CAM_BUF_CAHCED;

#ifdef TEST_ON_HAPS
	/* force reserved memory during bringup. */
	iommu_enable = 0;
#endif
	ret = cambuf_adapt_buffer_alloc(buf_info, size, iommu_enable, flag);
	if (IS_ERR_OR_NULL(buf_info->dmabuf_p)) {
		pr_err("fail to alloc ion buf size = 0x%x\n", (int)size);
		ret = -ENOMEM;
		return ret;
	}

	ret = cambuf_adapt_ion_get_buffer(-1, buf_info->buf_sec, buf_info->dmabuf_p, &buf_info->ionbuf, &buf_info->size);
	if (ret) {
		pr_err("fail to get ionbuf for kernel buffer %p\n", buf_info->dmabuf_p);
		ret = -EFAULT;
		goto failed;
	}

	pr_debug("dmabuf_p[%p], ionbuf[%p], size %d\n", buf_info->dmabuf_p, buf_info->ionbuf, (int)buf_info->size);

	buf_info->type = CAM_BUF_KERNEL;
	buf_info->status = CAM_BUF_ALLOC;
	pr_debug("alloc done. %p\n", buf_info);

	if (g_mem_dbg) {
		atomic_inc(&g_mem_dbg->ion_alloc_cnt);
		atomic_add((int)buf_info->size, &g_mem_dbg->ion_alloc_size);
	}

	return 0;

failed:
	cambuf_adapt_ion_free(buf_info->dmabuf_p);
	buf_info->dmabuf_p = NULL;
	buf_info->ionbuf = NULL;
	buf_info->size = 0;
	return ret;
}

int cam_buf_free(struct camera_buf *buf_info)
{
	int rtn = 0, size = 0;
	struct dma_buf *dmabuf = NULL;

	if (!buf_info) {
		pr_err("fail to get buffer info ptr\n");
		return -EFAULT;
	}

	if (buf_info->type != CAM_BUF_KERNEL) {
		pr_err("fail to get correct buffer type: %d\n", buf_info->type);
		return -EPERM;
	}

	if (buf_info->mapping_state != CAM_BUF_MAPPING_NULL) {
		pr_err("fail to get correct mapping state %x. maybe iova leak.\n", buf_info->mapping_state);
	}

	if (buf_info->status & CAM_BUF_WITH_K_ADDR)
		pr_err("fail to get correct status %x. maybe kaddr leak.\n", buf_info->status);

	dmabuf = buf_info->dmabuf_p;
	if (dmabuf) {
		cambuf_adapt_buffer_free(dmabuf);
		size = (int)buf_info->size;
		buf_info->dmabuf_p = NULL;
		buf_info->mfd = 0;
		buf_info->size = 0;
		buf_info->ionbuf = NULL;
		if (g_mem_dbg) {
			atomic_dec(&g_mem_dbg->ion_alloc_cnt);
			atomic_sub(size, &g_mem_dbg->ion_alloc_size);
		}
	}

	buf_info->status = CAM_BUF_EMPTY;
	pr_debug("free done: %p, dmabuf[%p]\n", buf_info, dmabuf);
	return rtn;
}

int cam_buf_kmap(struct camera_buf *buf_info)
{
	int ret = 0;

	if (!buf_info) {
		pr_err("fail to get buffer info ptr\n");
		return -EFAULT;
	}

	if (buf_info->status & CAM_BUF_WITH_K_ADDR) {
		pr_warn("warning: buf type %d status: 0x%x\n", buf_info->type, buf_info->status);
		return 0;
	}

	if ((buf_info->size <= 0) || (buf_info->dmabuf_p == NULL)) {
		pr_err("fail to kmap, size %zu, dmabuf %p\n", buf_info->size, buf_info->dmabuf_p);
		return -EFAULT;
	}

	ret = cambuf_adapt_buf_kmap(buf_info);
	if (IS_ERR_OR_NULL((void *)buf_info->addr_k)) {
		pr_err("fail to map k_addr %p for dmabuf[%p]\n", (void *)buf_info->addr_k, buf_info->dmabuf_p);
		buf_info->addr_k = 0;
		return -EINVAL;
	}
	buf_info->addr_k += buf_info->offset[0];
	pr_debug("buf_info %p, addr_k %p, dmabuf[%p]\n", buf_info, (void *)buf_info->addr_k, buf_info->dmabuf_p);

	buf_info->status &= 0xfc;
	buf_info->status |= CAM_BUF_WITH_K_ADDR;
	pr_debug("done: %p\n", (void *)buf_info->addr_k);

	if (g_mem_dbg)
		atomic_inc(&g_mem_dbg->ion_kmap_cnt);

	return 0;
}

int cam_buf_kunmap(struct camera_buf *buf_info)
{
	int ret = 0;

	if (!buf_info) {
		pr_err("fail to get buffer info ptr\n");
		return -EFAULT;
	}

	if (!(buf_info->status & CAM_BUF_WITH_K_ADDR)) {
		pr_warn("warning: buf type %d status: 0x%x\n", buf_info->type, buf_info->status);
		return 0;
	}

	if ((buf_info->size <= 0) || (buf_info->dmabuf_p == NULL)) {
		pr_err("fail to kunmap, size %zu, dmabuf %p\n", buf_info->size, buf_info->dmabuf_p);
		return -EFAULT;
	}

	pr_debug("buf_info %p, addr_k %p, dmabuf[%p]\n", buf_info, (void *)buf_info->addr_k, buf_info->dmabuf_p);

	ret = cambuf_adapt_buf_kunmap(buf_info);
	buf_info->addr_k = 0;
	buf_info->status &= (~CAM_BUF_WITH_K_ADDR);
	if (!buf_info->status)
		buf_info->status = CAM_BUF_WITH_ION;

	if (g_mem_dbg)
		atomic_dec(&g_mem_dbg->ion_kmap_cnt);

	return ret;
}

int cam_buf_ionbuf_get(struct camera_buf *buf_info)
{
	int ret = 0;
	enum cam_buf_iommudev_type type = CAM_BUF_IOMMUDEV_MAX;
	struct iommudev_info *dev_info = NULL;

	if (!buf_info) {
		pr_err("fail to get buffer info ptr\n");
		return -EFAULT;
	}
	if (buf_info->type != CAM_BUF_USER) {
		pr_err("fail to get correct buffer type: %d\n", buf_info->type);
		return -EFAULT;
	}

	if (buf_info->mfd <= 0) {
		pr_warn("Waning:get ionbuf, mfd %d\n", buf_info->mfd);
		return 0;
	}

	pr_debug("enter. user buf:  %d, sec:%d\n", buf_info->mfd, buf_info->buf_sec);
	ret = cambuf_adapt_ion_get_buffer(buf_info->mfd, buf_info->buf_sec, NULL, &buf_info->ionbuf, &buf_info->size);
	if (ret) {
		pr_err("fail to get ionbuf for user buffer %d\n", buf_info->mfd);
		goto failed;
	}
	pr_debug("size %d, ionbuf %p\n", (int)buf_info->size, buf_info->ionbuf);
	/*
	 * get dambuf to avoid ion buf is freed during
	 * hardware access it.
	 * In fact, this situation will happen when application exit
	 * exceptionally and for user buffer only.
	 * Just get dmabuf when hardware ready to access it
	 * for user buffer can avoid previous situation.
	 * We get dmabuf here because if the buffer is passed to kernel,
	 * there is potential hardware access.
	 * And it must be put before return to user space,
	 * or else it may cause ion memory leak.
	 */
	if (buf_info->dmabuf_p == NULL) {
		buf_info->dmabuf_p = dma_buf_get(buf_info->mfd);
		if (IS_ERR_OR_NULL(buf_info->dmabuf_p)) {
			pr_err("fail to get dma buf %p\n", buf_info->dmabuf_p);
			ret = -EINVAL;
			goto failed;
		}
		if (g_mem_dbg)
			atomic_inc(&g_mem_dbg->ion_dma_cnt);

		pr_debug("buf_info %p, dmabuf %p\n", buf_info, buf_info->dmabuf_p);
	}

	if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) {
		for (type = CAM_BUF_IOMMUDEV_ISP; type < CAM_BUF_IOMMUDEV_MAX; type++) {
			dev_info = cambuf_iommudev_get(type, NULL);
			if (!dev_info)
				continue;
			if (dma_set_mask(dev_info->dev, DMA_BIT_MASK(64))) {
				dev_warn(dev_info->dev, "mydev: No suitable DMA available\n");
				goto failed;
			}
			buf_info->attachment[type] = dma_buf_attach(buf_info->dmabuf_p, dev_info->dev);
			if (IS_ERR_OR_NULL(buf_info->attachment[type])) {
				pr_err("fail to attach dmabuf %p\n", (void *)buf_info->dmabuf_p);
				ret = -EINVAL;
				goto failed;
			}
			buf_info->table[type] = dma_buf_map_attachment(buf_info->attachment[type], DMA_BIDIRECTIONAL);
			if (IS_ERR_OR_NULL(buf_info->table[type])) {
				pr_err("fail to map attachment %p\n",
					(void *)buf_info->attachment[type]);
				ret = -EINVAL;
				goto map_attachment_failed;
			}
		}
	}

	buf_info->status = CAM_BUF_WITH_ION;
	return 0;

map_attachment_failed:
	for (type = CAM_BUF_IOMMUDEV_ISP; type < CAM_BUF_IOMMUDEV_MAX; type++) {
		if (!IS_ERR_OR_NULL(buf_info->attachment[type]))
			dma_buf_detach(buf_info->dmabuf_p, buf_info->attachment[type]);
	}
failed:
	if (!IS_ERR_OR_NULL(buf_info->dmabuf_p)) {
		dma_buf_put(buf_info->dmabuf_p);
		buf_info->dmabuf_p = NULL;
		if (g_mem_dbg)
			atomic_dec(&g_mem_dbg->ion_dma_cnt);
	}
	buf_info->ionbuf = NULL;
	return ret;
}

int cam_buf_ionbuf_put(struct camera_buf *buf_info)
{
	int ret = 0;
	enum cam_buf_iommudev_type type = CAM_BUF_IOMMUDEV_MAX;

	if (!buf_info) {
		pr_err("fail to get buffer info ptr\n");
		return -EFAULT;
	}
	if (buf_info->type != CAM_BUF_USER) {
		pr_debug("buffer type: %d is not user buf.\n", buf_info->type);
		return 0;
	}

	pr_debug("enter. buf_info %p\n", buf_info);
	if (buf_info->mfd <= 0) {
		pr_err("fail to put ionbuf, mfd %d\n", buf_info->mfd);
		return -EFAULT;
	}
	if (IS_ERR_OR_NULL(buf_info->dmabuf_p))
		goto exit;

	for (type = CAM_BUF_IOMMUDEV_ISP; type < CAM_BUF_IOMMUDEV_MAX; type++) {
		if (!IS_ERR_OR_NULL(buf_info->table[type]))
			dma_buf_unmap_attachment(buf_info->attachment[type], buf_info->table[type], DMA_BIDIRECTIONAL);
		if (!IS_ERR_OR_NULL(buf_info->attachment[type]))
			dma_buf_detach(buf_info->dmabuf_p, buf_info->attachment[type]);
	}
	if (!IS_ERR_OR_NULL(buf_info->dmabuf_p->file) &&
		virt_addr_valid(buf_info->dmabuf_p->file))
		dma_buf_put(buf_info->dmabuf_p);
	buf_info->dmabuf_p = NULL;
	if (g_mem_dbg)
		atomic_dec(&g_mem_dbg->ion_dma_cnt);
exit:
	buf_info->ionbuf = NULL;
	buf_info->status = CAM_BUF_ALLOC;
	return ret;
}

int cam_buf_iommu_map(struct camera_buf *buf_info, enum cam_buf_iommudev_type type)
{
	int ret = 0;
	struct iommudev_info *dev_info = NULL;
	struct sprd_iommu_map_data iommu_data = {0};

	if (type >= CAM_BUF_IOMMUDEV_MAX)
		return ret;

	dev_info = cambuf_iommudev_get(type, NULL);
	if (!buf_info || !dev_info) {
		pr_err("fail to get valid param %p %p\n", buf_info, dev_info);
		return -EFAULT;
	}

	if (buf_info->ionbuf == NULL) {
		pr_err("fail to map, ionbuf is NULL\n");
		return -EFAULT;
	}

	pr_debug("enter.\n");

	if (dev_info->iommu_en && (!buf_info->buf_sec || (buf_info->buf_sec && buf_info->reserved_iommu_map_support))) {
		memset(&iommu_data, 0, sizeof(struct sprd_iommu_map_data));
		iommu_data.buf = buf_info->ionbuf;
		iommu_data.iova_size = buf_info->size;
		iommu_data.ch_type = SPRD_IOMMU_FM_CH_RW;
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
		iommu_data.buf_type = SPRD_IOMMU_BUFTYPE_SYSTEM;
#endif
		pr_debug("start map buf: %p, size: %d iommu type:%d space type:%d\n", buf_info->ionbuf, (int)iommu_data.iova_size, type, buf_info->type);

		if (buf_info->buf_sec && buf_info->reserved_iommu_map_support)
			ret = sprd_iommu_map_v2(dev_info->dev, &iommu_data, SPRD_IOMMU_BUFTYPE_CARVEOUT);
		else
			ret = sprd_iommu_map(dev_info->dev, &iommu_data);
		if (ret) {
			pr_err("fail to get iommu kaddr\n");
			ret = -EFAULT;
			goto failed;
		}

		buf_info->iova[type] = iommu_data.iova_addr;
		buf_info->iova[type] += buf_info->offset[0];

		pr_debug("buf_info %p, mfd 0x%x, dmap %px, addr_k %px, iova: 0x%08x, off 0x%lx, size 0x%x\n",
			buf_info, buf_info->mfd, (void *)buf_info->dmabuf_p, (void *)buf_info->addr_k,
			(uint32_t)buf_info->iova[type], (uint32_t)buf_info->offset[0],
			(uint32_t)buf_info->size);

		if (g_mem_dbg)
			atomic_inc(&g_mem_dbg->iommu_map_cnt[type]);
	} else {
		ret = cambuf_adapt_phys_get_addr(-1, buf_info->dmabuf_p, &buf_info->iova[type], &buf_info->size);
		if (ret) {
			pr_err("fail to get iommu kaddr\n");
			ret = -EFAULT;
			goto failed;
		}
		buf_info->iova[type] += buf_info->offset[0];
		pr_debug("mfd %d, kaddr %p, iova: 0x%08x, off 0x%x, size 0x%x\n",
			buf_info->mfd, (void *)buf_info->addr_k,(uint32_t)buf_info->iova[type],
			(uint32_t)buf_info->offset[0], (uint32_t)buf_info->size);
	}
	buf_info->mapping_state |= 1 << type;

	buf_info->status &= 0xfc;
	buf_info->status |= CAM_BUF_WITH_IOVA;
	return 0;

failed:
	if (buf_info->size <= 0 || buf_info->iova[type] == 0) {
		buf_info->iova[type] = 0;
		return ret;
	}

	if (dev_info->iommu_en) {
		struct sprd_iommu_unmap_data unmap_data;

		unmap_data.iova_addr = buf_info->iova[type] - buf_info->offset[0];
		unmap_data.iova_size = buf_info->size;
		unmap_data.ch_type = SPRD_IOMMU_FM_CH_RW;
		unmap_data.table = NULL;
		unmap_data.buf = NULL;
		ret = sprd_iommu_unmap(dev_info->dev, &unmap_data);
		if (ret)
			pr_err("fail to free iommu\n");
	}
	buf_info->iova[type] = 0;
	return ret;
}

int cam_buf_iommu_unmap(struct camera_buf *buf_info, enum cam_buf_iommudev_type type)
{
	int ret = 0;
	struct iommudev_info *dev_info = NULL;
	struct sprd_iommu_unmap_data unmap_data = {0};

	if (!buf_info) {
		pr_err("fail to get buffer info ptr\n");
		return -EFAULT;
	}

	if ((buf_info->mapping_state & (1 << type)) == 0) {
		pr_info("buf may not be mapping %d\n", buf_info->mapping_state);
		return ret;
	}

	if (type >= CAM_BUF_IOMMUDEV_MAX)
		return ret;

	dev_info = cambuf_iommudev_get(type, NULL);
	if (!dev_info) {
		pr_err("fail to get matched iommu dev.\n");
		return -EFAULT;
	}

	if (buf_info->size <= 0 || buf_info->iova[type] == 0) {
		pr_err("fail to unmap, size %zu, iova 0x%08x\n", buf_info->size, (uint32_t)buf_info->iova[type]);
		return -EFAULT;
	}

	if (dev_info->iommu_en && (!buf_info->buf_sec || (buf_info->buf_sec && buf_info->reserved_iommu_map_support))) {
		unmap_data.iova_addr = buf_info->iova[type] - buf_info->offset[0];
		unmap_data.iova_size = buf_info->size;
		unmap_data.ch_type = SPRD_IOMMU_FM_CH_RW;
		unmap_data.table = NULL;
		unmap_data.buf = NULL;

		pr_debug("upmap buf %p, mfd 0x%x, addr: %lx, iova 0x%lx, iommu type:%d, size 0x%lx\n",
			buf_info->ionbuf, buf_info->mfd, unmap_data.iova_addr,
			buf_info->iova[type], type, buf_info->size);
		ret = sprd_iommu_unmap(dev_info->dev, &unmap_data);
		if (ret) {
			pr_err("fail to free iommu\n");
			goto exit;
		}
		if (g_mem_dbg && !ret)
			atomic_dec(&g_mem_dbg->iommu_map_cnt[dev_info->type]);
	}
	buf_info->iova[type] = 0;
exit:
	buf_info->mapping_state &= ~(1 << type);

	if (buf_info->mapping_state == CAM_BUF_MAPPING_NULL) {
		buf_info->status &= (~CAM_BUF_WITH_IOVA);
		buf_info->status &= (~CAM_BUF_WITH_SINGLE_PAGE_IOVA);
		if (!buf_info->status)
			buf_info->status = CAM_BUF_WITH_ION;
	}
	pr_debug("buf_info %p unmap done.\n", buf_info);
	return ret;
}

int cam_buf_iommu_single_page_map(struct camera_buf *buf_info, enum cam_buf_iommudev_type type)
{
	int ret = 0;
	struct iommudev_info *dev_info = NULL;
	struct sprd_iommu_map_data iommu_data = {0};

	if (type >= CAM_BUF_IOMMUDEV_MAX)
		return ret;

	dev_info = cambuf_iommudev_get(type, NULL);
	if (!buf_info || !dev_info) {
		pr_err("fail to get valid param %p %p\n", buf_info, dev_info);
		return -EFAULT;
	}

	if (buf_info->ionbuf == NULL) {
		pr_err("fail to map, ionbuf is NULL\n");
		return -EFAULT;
	}

	pr_debug("enter.\n");

	if (dev_info->iommu_en && !buf_info->buf_sec) {
		memset(&iommu_data, 0, sizeof(struct sprd_iommu_map_data));
		iommu_data.buf = buf_info->ionbuf;
		iommu_data.iova_size = buf_info->size;
		iommu_data.ch_type = SPRD_IOMMU_FM_CH_RW;
		pr_debug("start map buf: %p, size: %d\n", buf_info->ionbuf, (int)iommu_data.iova_size);
		ret = sprd_iommu_map_single_page(dev_info->dev, &iommu_data);
		if (ret) {
			pr_err("fail to get iommu kaddr\n");
			ret = -EFAULT;
			goto failed;
		}

		buf_info->iova[type] = iommu_data.iova_addr;
		pr_debug("buf_info %p, mfd %d, kaddr %p, iova: 0x%08x, size 0x%x\n",
			buf_info, buf_info->mfd, (void *)buf_info->addr_k, (uint32_t)buf_info->iova[type], (uint32_t)buf_info->size);

		if (g_mem_dbg)
			atomic_inc(&g_mem_dbg->iommu_map_cnt[type]);
	} else {
		ret = cambuf_adapt_phys_get_addr(-1, buf_info->dmabuf_p, &buf_info->iova[type], &buf_info->size);
		if (ret) {
			pr_err("fail to get iommu kaddr\n");
			ret = -EFAULT;
			goto failed;
		}
		pr_debug("mfd %d, kaddr %p, iova: 0x%08x, off 0x%x, size 0x%x\n",
				buf_info->mfd, (void *)buf_info->addr_k,(uint32_t)buf_info->iova[type],
				(uint32_t)buf_info->offset[0], (uint32_t)buf_info->size);
	}
	buf_info->mapping_state |= (1 << type);
	buf_info->status &= 0xfc;
	buf_info->status |= CAM_BUF_WITH_SINGLE_PAGE_IOVA;
	return 0;

failed:
	if (buf_info->size <= 0 || buf_info->iova[type] == 0)
		return ret;

	if (dev_info->iommu_en) {
		struct sprd_iommu_unmap_data unmap_data = {0};

		unmap_data.iova_addr = buf_info->iova[type];
		unmap_data.iova_size = buf_info->size;
		unmap_data.ch_type = SPRD_IOMMU_FM_CH_RW;
		unmap_data.table = NULL;
		unmap_data.buf = NULL;
		ret = sprd_iommu_unmap(dev_info->dev, &unmap_data);
		if (ret)
			pr_err("fail to free iommu\n");
	}
	buf_info->iova[type] = 0;

	return ret;
}

int cam_buf_iommu_restore(enum cam_buf_iommudev_type type)
{
	int ret = 0;
	struct iommudev_info *dev_info = NULL;

	dev_info = cambuf_iommudev_get(type, NULL);
	ret = sprd_iommu_restore(dev_info->dev);
	if (ret != 0)
		pr_info("fail to iommu enable\n");

	return ret;
}

int cam_buf_iommu_status_get(enum cam_buf_iommudev_type type)
{
	int ret = -ENODEV;
	struct iommudev_info *cur = NULL;

	if (type >= CAM_BUF_IOMMUDEV_MAX)
		return ret;

	cur = &s_iommudevs[type];
	if ((cur->type == type) && (cur->dev != NULL)) {
		int enable;

		if (g_dbg_iommu_mode == IOMMU_AUTO)
			ret = cur->iommu_en ? 0 : -1;
		else if (g_dbg_iommu_mode == IOMMU_ON)
			ret = 0;
		else
			ret = -1;
		enable = (ret == 0) ? 1 : 0;
		pr_debug("dev %d, iommu_mode %d en %d\n", type, g_dbg_iommu_mode, enable);
	}

	return ret;
}

int cam_buf_iommudev_reg(struct device *dev, enum cam_buf_iommudev_type type)
{
	if (type < CAM_BUF_IOMMUDEV_MAX) {

		s_iommudevs[type].type = type;
		s_iommudevs[type].dev = dev;
		s_iommudevs[type].iommu_en = 0;

		if (atomic_inc_return(&s_dev_cnt) == 1) {
			memset(g_mem_dbg, 0, sizeof(struct cam_mem_dbg_info));
			pr_info("reset to 0\n");
		}

		/* change mode when camera open */
		if (type == CAM_BUF_IOMMUDEV_DCAM)
			g_dbg_iommu_mode = g_dbg_set_iommu_mode;

		if (g_dbg_iommu_mode == IOMMU_AUTO) {
			if (sprd_iommu_attach_device(dev) == 0)
				s_iommudevs[type].iommu_en = 1;
		} else if (g_dbg_iommu_mode == IOMMU_OFF) {
			s_iommudevs[type].iommu_en = 0;
		} else {
			s_iommudevs[type].iommu_en = 1;
		}
		pr_info("dev %d, iommu_mode %d iommu_hw_en %d\n",
			type, g_dbg_iommu_mode, s_iommudevs[type].iommu_en);
	}

	return 0;
}

int cam_buf_iommudev_unreg(enum cam_buf_iommudev_type type)
{
	if (type < CAM_BUF_IOMMUDEV_MAX) {
		if (!s_iommudevs[type].dev)
			return 0;
		atomic_dec(&s_dev_cnt);
		s_iommudevs[type].type = CAM_BUF_IOMMUDEV_MAX;
		s_iommudevs[type].dev = NULL;
		s_iommudevs[type].iommu_en = 0;
	}
	return 0;
}

void *cam_buf_kernel_sys_kzalloc(unsigned long size, gfp_t flags)
{
	void *mem = NULL;

	mem = kzalloc(size, flags);
	if (mem && g_mem_dbg) {
		atomic_inc(&g_mem_dbg->mem_kzalloc_cnt);
		pr_debug("kzalloc done: %p, mem_count %d\n", mem, atomic_read(&g_mem_dbg->mem_kzalloc_cnt));
	} else
		pr_err("fail to kzalloc %ld memory\n", size);
	return mem;
}

void cam_buf_kernel_sys_kfree(const void *mem)
{
	kfree(mem);
	if (g_mem_dbg)
		atomic_dec(&g_mem_dbg->mem_kzalloc_cnt);
	pr_debug("kfree done. mem_count %d mem %p\n", atomic_read(&g_mem_dbg->mem_kzalloc_cnt), mem);
}

void *cam_buf_kernel_sys_vzalloc(unsigned long size)
{
	void *mem = NULL;

	mem = vzalloc(size);
	if (mem && g_mem_dbg) {
		atomic_inc(&g_mem_dbg->mem_vzalloc_cnt);
		pr_debug("vzalloc done: %p, mem_count %d\n", mem, atomic_read(&g_mem_dbg->mem_vzalloc_cnt));
	} else
		pr_err("fail to vzalloc %ld memory\n", size);
	return mem;
}

void cam_buf_kernel_sys_vfree(const void *mem)
{
	vfree(mem);
	if (g_mem_dbg)
		atomic_dec(&g_mem_dbg->mem_vzalloc_cnt);
	pr_debug("vfree done. mem_count %d mem %p\n", atomic_read(&g_mem_dbg->mem_vzalloc_cnt), mem);
}
