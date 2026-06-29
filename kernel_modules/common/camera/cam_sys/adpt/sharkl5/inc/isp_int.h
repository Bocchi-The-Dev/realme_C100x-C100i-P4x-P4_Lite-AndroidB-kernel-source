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

#ifndef _ISP_INT_H_
#define _ISP_INT_H_

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include "cam_types.h"
#include "isp_dev.h"
#include "isp_interface.h"

typedef void(*isp_int_isr)(enum isp_context_hw_id idx, void *param);

enum isp_irq_id {
	ISP_INT_ISP_ALL_DONE,
	ISP_INT_SHADOW_DONE,
	ISP_INT_DISPATCH_DONE,
	ISP_INT_STORE_DONE_OUT,

	ISP_INT_STORE_DONE_PRE,
	ISP_INT_STORE_DONE_VID,
	ISP_INT_STORE_DONE_VID_SKIP,
	ISP_INT_NR3_ALL_DONE,

	ISP_INT_NR3_SHADOW_DONE,
	ISP_INT_STORE_DONE_THUMBNAIL,
	ISP_INT_YUV_LTMHISTS_DONE,
	ISP_INT_FMCU_LOAD_DONE,

	ISP_INT_FMCU_CONFIG_DONE,
	ISP_INT_FMCU_SHADOW_DONE,
	ISP_INT_FMCU_CMD_X,
	ISP_INT_FMCU_TIMEOUT,

	ISP_INT_FMCU_CMD_ERROR,
	ISP_INT_FMCU_STOP_DONE,
	ISP_INT_HIST_CAL_DONE,
	ISP_INT_FBD_FETCH_ERR,

	ISP_INT_NR3_FBD_ERR,
	ISP_INT_NR3_FBC_ERR,
	ISP_INT_VID_FBC_ERR,
	ISP_INT_AXI_TIMEOUT,

	ISP_INT_MMU_VAOR_RD,
	ISP_INT_MMU_VAOR_WR,
	ISP_INT_MMU_INV_RD,
	ISP_INT_MMU_INV_WR,

	ISP_INT_MMU_UNS_RD,
	ISP_INT_MMU_UNS_WR,
	ISP_INT_MMU_PAOR_RD,
	ISP_INT_MMU_PAOR_WR,
};
#define ISP_INT_FMCU_STORE_DONE               ISP_INT_FMCU_CONFIG_DONE

#define ISP_INT_LINE_MASK_ERR                 \
	((1 << ISP_INT_FMCU_TIMEOUT) |        \
	(1 << ISP_INT_FMCU_CMD_ERROR) |       \
	(1 << ISP_INT_FBD_FETCH_ERR) |        \
	(1 << ISP_INT_NR3_FBD_ERR) |          \
	(1 << ISP_INT_NR3_FBC_ERR) |          \
	(1 << ISP_INT_VID_FBC_ERR) |          \
	(1 << ISP_INT_AXI_TIMEOUT))

#define ISP_INT_LINE_MASK_MMU                 \
	((1 << ISP_INT_MMU_VAOR_RD) |         \
	(1 << ISP_INT_MMU_VAOR_WR) |          \
	(1 << ISP_INT_MMU_INV_RD) |           \
	(1 << ISP_INT_MMU_INV_WR) |           \
	(1 << ISP_INT_MMU_UNS_RD) |           \
	(1 << ISP_INT_MMU_UNS_WR) |           \
	(1 << ISP_INT_MMU_PAOR_RD) |          \
	(1 << ISP_INT_MMU_PAOR_WR))

#define ISP_INT_FALAT_ERR_MASK                (1 << ISP_INT_AXI_TIMEOUT)

struct isp_int_ctxs_com {
	unsigned long irq_offset;
	uint32_t err_mask;
	uint32_t irq_numbers;
	const uint32_t *irq_vect;
	uint32_t irq_line;
	uint32_t irq_line1;
	isp_int_isr *isp_isr_handler;
	uint32_t mmu_irq_line;
};

void  ispint_iommu_regs_dump(void);
int ispint_err_pre_proc(enum isp_context_hw_id hw_idx, void *isp_handle, uint32_t irq_line);
struct isp_int_ctxs_com isp_int_reg_handle(int c_id);
uint32_t isp_int_get_mmu_irq_line(struct isp_int_ctxs_com ctxs_com);

#endif
