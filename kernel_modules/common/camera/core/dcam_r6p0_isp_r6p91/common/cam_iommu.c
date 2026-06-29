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

#include <linux/err.h>
#include <linux/version.h>
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
#include <soc/unisoc/sprd_iommu.h>
#else
#include <linux/sprd_iommu.h>
#include <linux/sprd_ion.h>
#endif
#include "cam_iommu.h"
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_IOMMU: %d %d %s : " \
	fmt, current->pid, __LINE__, __func__

#define DCAM_MAX_OUT_SIZE                              ((4160*3120*3)>>1)

struct fd_map_buf {
	struct list_head list;
	int fd;
	void *buf;
	void *dmabuf;
	size_t size;
};
static LIST_HEAD(dma_buffer_list);
static LIST_HEAD(iommu_buffer_list);
static DEFINE_MUTEX(buffer_lock);

int buffer_list_add(int fd, void *buf, size_t size, int buf_type)
{
	struct fd_map_buf *fd_buf = NULL;
	struct list_head *buffer_list;
	if (buf_type == IOMMU_BUF) {
		buffer_list = &iommu_buffer_list;
	} else {
		buffer_list = &dma_buffer_list;
	}

	list_for_each_entry(fd_buf, buffer_list, list) {
		if (fd == fd_buf->fd && buf == fd_buf->buf)
			return 0;
	}

	fd_buf = kzalloc(sizeof(struct fd_map_buf), GFP_KERNEL);
	if (!fd_buf)
		return -ENOMEM;

	fd_buf->fd = fd;
	fd_buf->buf = buf;
	fd_buf->size = size;
	fd_buf->dmabuf = dma_buf_get(fd_buf->fd);
	mutex_lock(&buffer_lock);
	list_add_tail(&fd_buf->list, buffer_list);
	mutex_unlock(&buffer_lock);
	DMABUF_TRACE("add type %d 0x%x 0x%x %x\n",buf_type,  fd_buf->fd,
		((struct dma_buf *)fd_buf->buf), fd_buf->dmabuf);

	return 0;
}

int buffer_list_del(int fd, void *buf, size_t size, int buf_type)
{
	struct fd_map_buf *fd_buf = NULL;
	struct list_head *buffer_list;
	if (buf_type == IOMMU_BUF) {
		buffer_list = &iommu_buffer_list;
	} else {
		buffer_list = &dma_buffer_list;
	}

	list_for_each_entry(fd_buf, buffer_list, list) {
		if (fd == fd_buf->fd && buf == fd_buf->buf){
			mutex_lock(&buffer_lock);
			list_del(&fd_buf->list);
			mutex_unlock(&buffer_lock);
			dma_buf_put(fd_buf->dmabuf);
			DMABUF_TRACE("type %d fd %x,buf %x,%x", buf_type, fd, buf, fd_buf->dmabuf);
			kfree(fd_buf);
			return 0;
		}
	}
	pr_err("fail type %d fd %x,buf %x", buf_type, fd, buf);
	return -1;
}

void buffer_list_clear(int buf_type, struct device *dev)
{
	struct fd_map_buf *fd_buf = NULL;
	struct fd_map_buf *fd_buf_next = NULL;
	struct list_head *buffer_list;
	struct sprd_iommu_unmap_data iommu_data = {0};
	int ret = 0;

	if (buf_type == IOMMU_BUF) {
		if (dev == NULL || sprd_iommu_attach_device(dev) != 0){
			pr_err("dev error");
			return;
		}
		buffer_list = &iommu_buffer_list;
	} else {
		buffer_list = &dma_buffer_list;
	}

	list_for_each_entry_safe(fd_buf, fd_buf_next, buffer_list, list) {
		mutex_lock(&buffer_lock);
		list_del(&fd_buf->list);
		mutex_unlock(&buffer_lock);
		if (buf_type == IOMMU_BUF) {
			iommu_data.buf = fd_buf->buf;
			iommu_data.iova_size = fd_buf->size;
			ret = sprd_iommu_unmap(dev, &iommu_data);
			if (ret)
				pr_err("unmap fail, ret %d, fd 0x%x iova 0x%x size 0x%zx sg %x\n",
					ret, fd_buf->fd, fd_buf->size,
					(unsigned int)iommu_data.iova_addr, fd_buf->buf);
			DMABUF_TRACE("put:type %d 0x%x 0x%x\n", buf_type, fd_buf->fd,
				fd_buf->dmabuf);
			dma_buf_put(fd_buf->dmabuf);
		} else {
			DMABUF_TRACE("put:type %d 0x%x 0x%x\n", buf_type, fd_buf->fd,
				fd_buf->dmabuf);
			dma_buf_put(fd_buf->dmabuf);
		}
		DMABUF_TRACE("del:type %d %x %x %x\n", buf_type, fd_buf->fd, fd_buf->buf,
			fd_buf->dmabuf);
		kfree(fd_buf);
	}
	pr_info("buf_type %d end",buf_type);
}

bool buffer_list_empty(int buf_type)
{
	struct list_head *buffer_list;
	if (buf_type == IOMMU_BUF) {
		buffer_list = &iommu_buffer_list;
	} else {
		buffer_list = &dma_buffer_list;
	}
	return list_empty(buffer_list);
}

int pfiommu_get_sg_table(struct pfiommu_info *pfinfo)
{
	int i;
	int ret = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	if(pfinfo->is_secure) {
		// only for pike in faceid unlock mode need bypass dcam iommu
		ret = sprd_iommu_set_cam_bypass(true);
		if (unlikely(ret)) {
			pr_err("fail to enable vaor bypass mode, ret %d\n", ret);
			return -EFAULT;
		}
	}
#endif
	for (i = 0; i < 2; i++) {
		if (pfinfo->mfd[i] > 0) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
			if (pfinfo->is_secure)
				ret = sprd_dmabuf_get_carvebuffer(pfinfo->mfd[i],
							NULL,
							&pfinfo->buf[i],
							&pfinfo->size[i]);
			else
				ret = sprd_dmabuf_get_sysbuffer(pfinfo->mfd[i],
							NULL,
							&pfinfo->buf[i],
							&pfinfo->size[i]);
#else
			ret = sprd_ion_get_buffer(pfinfo->mfd[i],
						    NULL,
						    &pfinfo->buf[i],
						    &pfinfo->size[i]);
#endif
			if (ret) {
				pr_err("failed to get sg table %d mfd 0x%x\n",
					i, pfinfo->mfd[i]);
				return -EFAULT;
			}

			pfinfo->dmabuf_p[i] = dma_buf_get(pfinfo->mfd[i]);
			DMABUF_TRACE("%d,dma_buf_get %x", i, pfinfo->dmabuf_p[i]);
			if (IS_ERR_OR_NULL(pfinfo->dmabuf_p[i])) {
				pr_err("failed to get dma buf %x\n",
				       pfinfo->dmabuf_p[i]);
				return -EFAULT;
			}
			dma_buf_put(pfinfo->dmabuf_p[i]);
			DMABUF_TRACE("%d,dma_buf_put %x", i, pfinfo->dmabuf_p[i]);
			buffer_list_add(pfinfo->mfd[i], pfinfo->dmabuf_p[i], 0, DMA_BUF);
		}
	}

	return 0;
}

int  pfiommu_put_sg_table(void)
{
	buffer_list_clear(DMA_BUF, NULL);
	return 0;
}

int pfiommu_get_single_page_addr(struct pfiommu_info *pfinfo)
{
	int i;
	int ret = 0;
	struct sprd_iommu_map_data iommu_data;
	pr_debug("%s, cb: %pS\n", __func__, __builtin_return_address(0));

	if (pfinfo->size[0] > 0)
		pfinfo->size[0] = (DCAM_MAX_OUT_SIZE + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	if (pfinfo->size[1] > 0)
		pfinfo->size[1] = (DCAM_MAX_OUT_SIZE + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	if (pfinfo->size[2] > 0)
		pfinfo->size[2] = (DCAM_MAX_OUT_SIZE + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	for (i = 0; i < 2; i++) {
		if (pfinfo->size[i] <= 0)
			continue;

		if (!pfinfo->is_secure && sprd_iommu_attach_device(pfinfo->dev) == 0) {
			memset(&iommu_data, 0x00, sizeof(iommu_data));
			iommu_data.buf = pfinfo->buf[i];
			iommu_data.iova_size = pfinfo->size[i];
			iommu_data.ch_type = SPRD_IOMMU_FM_CH_RW;
			iommu_data.sg_offset = pfinfo->offset[i];

			ret = sprd_iommu_map_single_page(pfinfo->dev, &iommu_data);
			if (ret) {
				pr_err("failed to get iommu kaddr %d\n", i);
				return -EFAULT;
			}

			pfinfo->iova[i] = iommu_data.iova_addr;
		} else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
			ret = sprd_dmabuf_get_phys_addr(-1, pfinfo->dmabuf_p[i],
					       &pfinfo->iova[i],
					       &pfinfo->size[i]);
#else
			ret = sprd_ion_get_phys_addr(-1, pfinfo->dmabuf_p[i],
					       &pfinfo->iova[i],
					       &pfinfo->size[i]);
#endif
			pfinfo->iova[i] += pfinfo->offset[i];
		}
	}

	return ret;
}

int pfiommu_get_addr(struct pfiommu_info *pfinfo)
{
	int i;
	int ret = 0;
	struct sprd_iommu_map_data iommu_data;
	pr_debug("%s, cb: %pS\n", __func__, __builtin_return_address(0));

	for (i = 0; i < 2; i++) {
		if (pfinfo->size[i] <= 0)
			continue;

		if (!pfinfo->is_secure && sprd_iommu_attach_device(pfinfo->dev) == 0) {
			memset(&iommu_data, 0x00, sizeof(iommu_data));
			iommu_data.buf = pfinfo->buf[i];
			iommu_data.iova_size = pfinfo->size[i];
			iommu_data.ch_type = SPRD_IOMMU_FM_CH_RW;
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
			iommu_data.buf_type = SPRD_IOMMU_BUFTYPE_SYSTEM;
#endif
			iommu_data.sg_offset = pfinfo->offset[i];

			ret = sprd_iommu_map(pfinfo->dev, &iommu_data);
			if (ret) {
				pr_err("failed to get iommu kaddr %d\n", i);
				return -EFAULT;
			}
			DMABUF_TRACE("%d,buf %x,dma buf %d,fd %x", i, pfinfo->buf[i],
				pfinfo->dmabuf_p[i], pfinfo->mfd[i]);

			pfinfo->iova[i] = iommu_data.iova_addr;
		} else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
			ret = sprd_dmabuf_get_phys_addr(-1, pfinfo->dmabuf_p[i],
					       &pfinfo->iova[i],
					       &pfinfo->size[i]);
#else
			ret = sprd_ion_get_phys_addr(-1, pfinfo->dmabuf_p[i],
					       &pfinfo->iova[i],
					       &pfinfo->size[i]);
#endif
			pfinfo->iova[i] += pfinfo->offset[i];
		}
	}

	return ret;
}

unsigned int pfiommu_get_kaddr(struct pfiommu_info *pfinfo)
{
	unsigned int kaddr = 0;

	if (pfinfo->size[0] <= 0)
		return 0;

	if (!pfinfo->is_secure && sprd_iommu_attach_device(pfinfo->dev) == 0) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		sprd_dmabuf_map_kernel(pfinfo->dmabuf_p[0], &pfinfo->map);
		kaddr = (unsigned int)pfinfo->map.vaddr;
		pr_info("dmabuf %x map kaddr 0x%x", pfinfo->dmabuf_p[0], kaddr);
#else
		kaddr = (unsigned int) sprd_ion_map_kernel(pfinfo->dmabuf_p[0], 0);
#endif
	} else {
		kaddr = pfinfo->iova[0];
	}

	return kaddr;
}

int pfiommu_check_addr(struct pfiommu_info *pfinfo)
{
#ifndef DMABUF_DEBUG
	return 0;
#else
	struct fd_map_buf *fd_dma = NULL;
	struct list_head *g_dma_buffer_list = &dma_buffer_list;

	list_for_each_entry(fd_dma, g_dma_buffer_list, list) {
		if (fd_dma->fd == pfinfo->mfd[0] &&
		    fd_dma->buf == pfinfo->dmabuf_p[0])
			break;
	}

	if (&fd_dma->list == g_dma_buffer_list) {
		pr_err("invalid mfd: 0x%x, dma_buf:0x%x!\n",
		       pfinfo->mfd[0],
		       pfinfo->dmabuf_p[0]);
		return -1;
	}
	return 0;//sprd_ion_check_phys_addr(pfinfo->dmabuf_p[0]);
#endif
}

int pfiommu_free_addr(struct pfiommu_info *pfinfo)
{
	int i, ret;
	struct sprd_iommu_unmap_data iommu_data;

	pr_debug("%s, cb: %pS, iova 0x%lx\n",
		 __func__, __builtin_return_address(0), pfinfo->iova[0]);

	for (i = 0; i < 2; i++) {
		if (pfinfo->size[i] <= 0 || pfinfo->iova[i] == 0)
			continue;

		if (!pfinfo->is_secure && sprd_iommu_attach_device(pfinfo->dev) == 0) {
			iommu_data.iova_addr = pfinfo->iova[i];
			iommu_data.table = pfinfo->table[i];
			iommu_data.iova_size = pfinfo->size[i];
			iommu_data.ch_type = SPRD_IOMMU_FM_CH_RW;
			iommu_data.buf = NULL;
			DMABUF_TRACE("%d,buf %x,dma buf %d,fd %x,iova %x,sg_table %x", i,
				pfinfo->buf[i], pfinfo->dmabuf_p[i], pfinfo->mfd[i],
				pfinfo->iova[i], pfinfo->table[i]);

			ret = sprd_iommu_unmap(pfinfo->dev, &iommu_data);
			if (ret) {
				pr_err("failed to free iommu %d\n iova %s", i, pfinfo->iova[i]);
				return -EFAULT;
			} else {
				pfinfo->iova[i] = 0;
				pfinfo->size[i] = 0;
			}
		}
	}

	return 0;
}

int pfiommu_free_addr_with_id(struct pfiommu_info *pfinfo,
	enum sprd_iommu_chtype ctype, unsigned int cid)
{
	int i, ret;
	struct sprd_iommu_unmap_data iommu_data;

	pr_debug("%s, cb: %pS, iova 0x%lx\n",
		 __func__, __builtin_return_address(0), pfinfo->iova[0]);

	for (i = 0; i < 2; i++) {
		if (pfinfo->size[i] <= 0 || pfinfo->iova[i] == 0)
			continue;

		if (!pfinfo->is_secure && sprd_iommu_attach_device(pfinfo->dev) == 0) {
			iommu_data.iova_addr = pfinfo->iova[i];
			iommu_data.iova_size = pfinfo->size[i];
			iommu_data.ch_type = ctype;
			iommu_data.buf = NULL;
			iommu_data.channel_id = cid;

			DMABUF_TRACE("%d,buf %x,dma buf %d,fd %x,iova %x,sg_table %x", i,
				pfinfo->buf[i], pfinfo->dmabuf_p[i], pfinfo->mfd[i],
				pfinfo->iova[i], pfinfo->table[i]);
			ret = sprd_iommu_unmap(pfinfo->dev,
					&iommu_data);
			if (ret) {
				pr_err("failed to free iommu %d iova %s cid %d", i,
					pfinfo->iova[i], cid);
				//dump_stack();
				return -EFAULT;
			} else {
				pfinfo->iova[i] = 0;
				pfinfo->size[i] = 0;
			}
		}
	}

	return 0;
}
