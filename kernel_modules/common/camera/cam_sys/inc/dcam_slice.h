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

#ifndef _DCAM_SLICE_H_
#define _DCAM_SLICE_H_

#include "dcam_core.h"

uint32_t dcam_slice_needed_info_get(struct dcam_hw_context *pctx, uint32_t *dev_lbuf, uint32_t in_width);
int dcam_slice_hw_info_set(struct dcam_offline_slice_info *slice, struct cam_frame *pframe,
	uint32_t slice_wmax, struct dcam_isp_k_block *pm);
int dcam_slice_num_valid_cal(struct dcam_offline_node *node,
	uint32_t slice_wmax, struct cam_frame *pframe);
#endif
