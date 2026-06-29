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

#ifndef _DCAM_HWCTX_H_
#define _DCAM_HWCTX_H_


int dcam_hwctx_offline_reset(struct dcam_hw_context *hw_ctx);
void dcam_hwctx_nr3_store_addr(struct dcam_hw_context *hw_ctx, struct cam_frame *frame);
void dcam_hwctx_binning_4in1_set(struct dcam_hw_context *hw_ctx);
void dcam_hwctx_block_set(struct dcam_hw_context *hw_ctx);
void dcam_hwctx_debug_init(struct dcam_hw_context *hw_ctx);
void dcam_hwctx_frame_param_set(struct dcam_hw_context *hw_ctx);
int dcam_hwctx_fetch_set(struct dcam_hw_context *hw_ctx);
void dcam_hwctx_slice_set(struct dcam_hw_context *hw_ctx, struct dcam_fetch_info *fetch, struct dcam_offline_slice_info *slice);
void dcam_hwctx_slice_force_copy(struct dcam_hw_context *hw_ctx, int x);
void dcam_hwctx_cfg_fetch_start(struct cam_hw_info *hw);
int dcam_hwctx_slw_fmcu_set(struct dcam_hw_context *hw_ctx, uint32_t hw_ctx_id, int j);
void dcam_hwctx_update_path_size(struct dcam_online_port *dcam_port, struct cam_frame *frame, struct dcam_hw_context *hw_ctx, uint32_t idx);
int dcam_hwctx_pyr_dec_cfg(struct dcam_online_port *dcam_port, struct cam_frame *frame, struct dcam_hw_context *hw_ctx, uint32_t idx);

#endif

