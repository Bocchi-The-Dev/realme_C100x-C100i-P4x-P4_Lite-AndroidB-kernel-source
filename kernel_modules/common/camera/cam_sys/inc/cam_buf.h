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

#ifndef _CAM_BUF_H_
#define _CAM_BUF_H_

#include <linux/version.h>
#include <linux/types.h>
#include <linux/device.h>
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
#include <soc/unisoc/sprd_iommu.h>
#else
#include <linux/sprd_iommu.h>
#endif

#include "cam_types.h"

#define CAM_BUF_CAHCED                     (1 << 31)

enum cam_q_head_status {
	CAM_Q_FREE = 0,
	CAM_Q_USED,
	CAM_Q_MAX
};

struct cam_q_head {
	struct list_head list;
	atomic_t status;
};

enum cam_buf_type {
	CAM_BUF_NONE,
	CAM_BUF_USER,
	CAM_BUF_KERNEL,
};

enum cam_buf_iommudev_type {
	CAM_BUF_IOMMUDEV_ISP,
	CAM_BUF_IOMMUDEV_DCAM,
	CAM_BUF_IOMMUDEV_DCAM_LITE,
	CAM_BUF_IOMMUDEV_MAX,
};

enum cam_buf_map_status {
	CAM_BUF_MAPPING_NULL = 0,
	CAM_BUF_MAPPING_ISP = (1 << CAM_BUF_IOMMUDEV_ISP),
	CAM_BUF_MAPPING_DCAM = (1 << CAM_BUF_IOMMUDEV_DCAM),
	CAM_BUF_MAPPING_DCAM_LITE = (1 << CAM_BUF_IOMMUDEV_DCAM_LITE),
};

/* BIT 0: CAM_BUF_ALLOC
 * BIT 1: CAM_BUF_WITH_ION
 * BIT 2: CAM_BUF_WITH_IOVA
 * BIT 3: CAM_BUF_WITH_SINGLE_PAGE_IOVA
 * BIT 4: CAM_BUF_WITH_K_ADDR
 * =================buf status move step==============
 * CAM_BUF_EMPTY    ->CAM_BUF_ALLOC                      ->CAM_BUF_WITH_ION
 * CAM_BUF_WITH_ION ->CAM_BUF_WITH_IOVA                  -> CAM_BUF_WITH_IOVA_K_ADDR
 *                  ->CAM_BUF_WITH_SINGLE_PAGE_IOVA
 *                  ->CAM_BUF_WITH_K_ADDR                -> CAM_BUF_WITH_IOVA_K_ADDR
 *                  ->CAM_BUF_WITH_IOVA_K_ADDR
 * CAM_BUF_WITH_IOVA_K_ADDR = CAM_BUF_WITH_IOVA | CAM_BUF_WITH_K_ADDR
*/
enum cam_buf_status {
	CAM_BUF_EMPTY = 0,
	CAM_BUF_ALLOC = 0x1,
	CAM_BUF_WITH_ION = 0x2,
	CAM_BUF_WITH_IOVA = 0x4,
	CAM_BUF_WITH_SINGLE_PAGE_IOVA = 0x8,
	CAM_BUF_WITH_K_ADDR = 0x10,
	CAM_BUF_WITH_IOVA_K_ADDR = 0x14,
	CAM_BUF_STATUS_NUM,
};

struct camera_buf {
	enum cam_en_status buf_sec;
	/* user buffer info */
	int32_t mfd;
	struct dma_buf *dmabuf_p;
	void *ionbuf;/* for iommu map */
	unsigned long offset[3];
	size_t size;
	unsigned long addr_vir[3];
	unsigned long addr_k;
	unsigned long iova[CAM_BUF_IOMMUDEV_MAX];
	enum cam_buf_type type;
	enum cam_buf_map_status mapping_state;
	struct dma_buf_attachment *attachment[CAM_BUF_IOMMUDEV_MAX];
	struct sg_table *table[CAM_BUF_IOMMUDEV_MAX];
	enum cam_buf_status status;
	enum cam_en_status bypass_iova_ops;
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
	struct iosys_map map;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	struct dma_buf_map map;/* for k515 dambuf */
#endif
	enum cam_en_status reserved_iommu_map_support;
};

int cam_buf_mdbg_check(void);
int cam_buf_alloc(struct camera_buf *buf_info, size_t size, unsigned int iommu_enable);
int cam_buf_free(struct camera_buf *buf_info);
int cam_buf_kmap(struct camera_buf *buf_info);
int cam_buf_kunmap(struct camera_buf *buf_info);
int cam_buf_ionbuf_get(struct camera_buf *buf_info);
int cam_buf_ionbuf_put(struct camera_buf *buf_info);
int cam_buf_iommu_map(struct camera_buf *buf_info, enum cam_buf_iommudev_type type);
int cam_buf_iommu_unmap(struct camera_buf *buf_info, enum cam_buf_iommudev_type type);
int cam_buf_iommu_single_page_map(struct camera_buf *buf_info, enum cam_buf_iommudev_type type);
int cam_buf_iommu_restore(enum cam_buf_iommudev_type type);
int cam_buf_iommu_status_get(enum cam_buf_iommudev_type type);
int cam_buf_iommudev_reg(struct device *dev, enum cam_buf_iommudev_type type);
int cam_buf_iommudev_unreg(enum cam_buf_iommudev_type type);
void *cam_buf_kernel_sys_kzalloc(unsigned long size, gfp_t flags);
void cam_buf_kernel_sys_kfree(const void *mem);
void *cam_buf_kernel_sys_vzalloc(unsigned long size);
void cam_buf_kernel_sys_vfree(const void *mem);

#endif/* _CAM_BUF_H_ */
