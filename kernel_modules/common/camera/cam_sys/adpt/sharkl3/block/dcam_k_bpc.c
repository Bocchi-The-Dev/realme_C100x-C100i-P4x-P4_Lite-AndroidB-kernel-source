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

#include <linux/uaccess.h>
#include <sprd_mm.h>

#include "cam_block.h"
#include "dcam_reg.h"
#include "cam_debugger.h"
#include "dcam_core.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "BPC: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void dcam_k_bpc_block_param_dump(struct dcam_dev_bpc_info_l3 *p,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	if (!debug->block_param)
		return;

	log_ctx = &debug->block_param_log;

	CAM_DEBUG_LOG_WRITE(log_ctx, "bpc: bypass %d update %d\n",
		p->bpc_bypass, p->update_flag);
	CAM_DEBUG_LOG_WRITE(log_ctx, "bypass: double %d three %d four %d mod_en %d\n",
		p->bpc_double_bypass, p->bpc_three_bypass, p->bpc_four_bypass, p->bpc_mode);
	CAM_DEBUG_LOG_WRITE(log_ctx, "mode %d is_mono_sensor %d edge_hv_mode %d edge_rd_mode %d\n",
		p->bpc_mode, p->is_mono_sensor, p->edge_hv_mode, p->edge_rd_mode);
	CAM_DEBUG_LOG_WRITE(log_ctx, "double_badpixel_th: [0] %d [1] %d [2] %d [3] %d\n",
		p->double_badpixel_th[0], p->double_badpixel_th[1],
		p->double_badpixel_th[2], p->double_badpixel_th[3]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "three_badpixel_th: [0] %d [1] %d [2] %d [3] %d\n",
		p->three_badpixel_th[0], p->three_badpixel_th[1],
		p->three_badpixel_th[2], p->three_badpixel_th[3]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "four_badpixel_th: [0] %d [1] %d [2] %d [3] %d\n",
		p->four_badpixel_th[0], p->four_badpixel_th[1],
		p->four_badpixel_th[2], p->four_badpixel_th[3]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "shift: [0] %d [1] %d [2] %d flat_th %d texture_th %d\n",
		p->shift[0], p->shift[1], p->shift[2],
		p->flat_th, p->texture_th);
	CAM_DEBUG_LOG_WRITE(log_ctx, "edgeratio_rd %d edgeratio_hv %d lowcoeff %d highcoeff %d\n",
		p->edge_ratio_rd, p->edge_ratio_hv, p->low_coeff, p->high_coeff);
	CAM_DEBUG_LOG_WRITE(log_ctx, "lowoffset %d highoffset %d maxcoeff %d mincoeff %d\n",
		p->low_offset, p->high_offset, p->max_coeff, p->min_coeff);
	CAM_DEBUG_LOG_WRITE(log_ctx, "intercept_b: [0] %d [1] %d [2] %d [3] %d\n",
		p->intercept_b[0], p->intercept_b[1],
		p->intercept_b[2], p->intercept_b[3]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "intercept_b: [4] %d [5] %d [6] %d [7] %d\n",
		p->intercept_b[4], p->intercept_b[5],
		p->intercept_b[6], p->intercept_b[7]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "slope_k: [0] %d [1] %d [2] %d [3] %d\n",
		p->slope_k[0], p->slope_k[1],
		p->slope_k[2], p->slope_k[3]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "slope_k: [4] %d [5] %d [6] %d [7] %d\n",
		p->slope_k[4], p->slope_k[5],
		p->slope_k[6], p->slope_k[7]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "lut_level: [0] %d [1] %d [2] %d [3] %d\n",
		p->lut_level[0], p->lut_level[1],
		p->lut_level[2], p->lut_level[3]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "lut_level: [4] %d [5] %d [6] %d [7] %d\n",
		p->lut_level[4], p->lut_level[5],
		p->lut_level[6], p->lut_level[7]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_bpc_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = param->idx;
	int i = 0;
	uint32_t val = 0;
	struct dcam_dev_bpc_info_l3 *p;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	p = &(param->bpc.bpc_param.bpc_info_l3);
	/* debugfs bpc not bypass then write*/

	dev = param->dev;
	if (idx >= DCAM_HW_CONTEXT_MAX || dev == NULL) {
		pr_err("fail to get valid param dcam idx:%d, dev %p.\n", idx, dev);
		return 0;
	}

	hw_ctx = &dev->hw_ctx[idx];
	if (hw_ctx) {
		if (g_dcam_block_dump & (1 << _E_BPC)) {
			hw_ctx->debug.idx = idx;
			hw_ctx->debug.fid = hw_ctx->fid;
			hw_ctx->debug.block_param = 1;
		}
		dcam_k_bpc_block_param_dump(p, &hw_ctx->debug);
	}

	if (g_dcam_bypass[idx] & (1 << _E_BPC))
		p->bpc_bypass = 1;

	val = ((p->bpc_mode_en_gc & 0x1) << 2) |
		((p->bpc_mode_en & 0x1) << 1) |
		(p->bpc_gc_cg_dis & 0x1);
	DCAM_REG_MWR(idx, ISP_BPC_GC_CFG, 0x7, val);
	if (p->bpc_mode_en == 0 && p->bpc_mode_en_gc == 0)
		return 0;

	/* following bit can be 0 only if bpc_bypss is 0 */
	if (p->bpc_bypass == 1)
		p->bpc_double_bypass = 1;
	if (p->bpc_double_bypass == 1)
		p->bpc_three_bypass = 1;
	if (p->bpc_three_bypass == 1)
		p->bpc_four_bypass = 1;

	val = (p->bpc_bypass & 0x1) |
		((p->bpc_double_bypass & 0x1) << 1) |
		((p->bpc_three_bypass & 0x1) << 2) |
		((p->bpc_four_bypass & 0x1) << 3);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, 0xF, val);
	if (p->bpc_bypass)
		return 0;

	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_4 | BIT_5, p->bpc_mode << 4);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_6,
			p->pos_out_continue_mode << 6);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_7, p->is_mono_sensor << 7);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_9 |
			BIT_8, p->edge_hv_mode << 8);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_11 |
			BIT_10, p->edge_rd_mode << 10);

	DCAM_REG_MWR(idx, ISP_BPC_PARAM,
			0xF000, p->pos_out_skip_num << 12);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, 0xFF0000,
			p->rd_retain_num << 16);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_24, p->rd_max_len_sel << 24);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_25, p->wr_max_len_sel << 25);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_31, p->bpc_blk_mode << 31);

	for (i = 0; i < 4; i++) {
		val = ((p->double_badpixel_th[i] & 0x3FF) << 20) |
			((p->three_badpixel_th[i] & 0x3FF) << 10) |
			((p->four_badpixel_th[i] & 0x3FF));
		DCAM_REG_WR(idx, ISP_BPC_BAD_PIXEL_TH0 + i * 4, val);
	}

	val = ((p->shift[0] & 0xF) << 28) |
			((p->shift[1] & 0xF) << 24) |
			((p->shift[2] & 0xF) << 20) |
			((p->flat_th & 0x3FF) << 10) |
			((p->texture_th & 0x3FF));
	DCAM_REG_WR(idx, ISP_BPC_FLAT_TH, val);

	val = ((p->edge_ratio_rd & 0x1FF) << 16) |
			((p->edge_ratio_hv & 0x1FF));
	DCAM_REG_WR(idx, ISP_BPC_EDGE_RATIO, val);

	val = ((p->low_coeff & 0x7) << 24) |
			((p->high_coeff & 0x7) << 16) |
			((p->low_offset & 0xFF) << 8) |
			((p->high_offset & 0xFF));
	DCAM_REG_WR(idx, ISP_BPC_BAD_PIXEL_PARAM, val);

	val = ((p->max_coeff & 0x1F) << 16) |
			((p->min_coeff & 0x1F));

	DCAM_REG_WR(idx, ISP_BPC_BAD_PIXEL_COEFF, val);

	for (i = 0; i < 8; i++) {
		val = ((p->lut_level[i] & 0x3FF) << 20) |
				((p->slope_k[i] & 0x3FF) << 10) |
				(p->intercept_b[i] & 0x3FF);
		DCAM_REG_WR(idx, ISP_BPC_LUTWORD0 + i * 4, val);
	}

	DCAM_REG_MWR(idx, ISP_BPC_MAP_CTRL,
			BIT_0, p->bad_map_hw_fifo_clr_en);
	DCAM_REG_WR(idx, ISP_BPC_MAP_CTRL1, p->bad_pixel_num);

	return ret;
}

int dcam_k_cfg_bpc(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	void *dst_ptr;
	ssize_t dst_size;
	FUNC_DCAM_PARAM sub_func = NULL;

	switch (param->property) {
	case DCAM_PRO_BPC_BLOCK:
	{
		dst_ptr = (void *)&p->bpc.bpc_param.bpc_info_l3;
		dst_size = sizeof(struct dcam_dev_bpc_info_l3);
		sub_func = dcam_k_bpc_block;
		break;
	}
	default:
		pr_err("fail to support property %d\n", param->property);
		ret = -EINVAL;
		return ret;
	}

	if (p->offline == 0) {
		if (copy_from_user(dst_ptr, param->property_param, dst_size)) {
			pr_err("fail to copy from user\n");
			ret = -1;
			goto exit;
		}

		if (p->idx == DCAM_HW_CONTEXT_MAX)
			return 0;

		if (sub_func)
			ret = sub_func(p);
	} else {
		mutex_lock(&p->param_lock);
		if (copy_from_user(dst_ptr, param->property_param, dst_size)) {
			pr_err("fail to copy from user\n");
			ret = -1;
		}
		mutex_unlock(&p->param_lock);
	}
exit:
	return ret;
}
