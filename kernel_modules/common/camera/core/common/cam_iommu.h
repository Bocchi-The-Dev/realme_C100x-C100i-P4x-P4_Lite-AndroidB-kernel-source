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

#ifndef _CAM_IOMMU_H_
#define _CAM_IOMMU_H_

#include <linux/version.h>
#include <linux/types.h>
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
#include <soc/unisoc/sprd_iommu.h>
#else
#include <linux/sprd_iommu.h>
#endif
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
#include <soc/unisoc/sprd_dmabuf.h>
#elif (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
#include <uapi/linux/sprd_dmabuf.h>
#endif

struct pfiommu_info {
	struct device *dev;
	unsigned int mfd[3];
	struct sg_table *table[3];
	void *buf[3];
	size_t size[3];
	unsigned long iova[3];
	struct dma_buf *dmabuf_p[3];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	struct dma_buf_map map;
#endif
	unsigned int offset[3];
	uint32_t is_secure;
};

int pfiommu_get_sg_table(struct pfiommu_info *pfinfo);
int  pfiommu_put_sg_table(void);
void dma_buffer_list_clear(void);
int pfiommu_get_addr(struct pfiommu_info *pfinfo);
int pfiommu_free_addr(struct pfiommu_info *pfinfo);
int pfiommu_free_addr_with_id(struct pfiommu_info *pfinfo,
	enum sprd_iommu_chtype ctype, unsigned int cid);

#endif /* _CAM_IOMMU_H_ */
