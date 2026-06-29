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
#define pr_fmt(fmt) "AFL: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define V_COUNTER_INTERVAL 524288

static void dcam_k_afl_param_dump(struct isp_dev_anti_flicker_new_info *afl_info,
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

	CAM_DEBUG_LOG_WRITE(log_ctx, "afl: bypass %d update %d\n",
		afl_info->bypass, afl_info->update_flag);
	CAM_DEBUG_LOG_WRITE(log_ctx, "cfg afl mode %d %d %d\n", afl_info->mode,
		afl_info->bayer2y_chanel, afl_info->bayer2y_mode);
	CAM_DEBUG_LOG_WRITE(log_ctx,
		"skip_frame_num %d afl_stepx %d, afl_stepy %d\n", afl_info->skip_frame_num,
		afl_info->afl_stepx, afl_info->afl_stepy);
	CAM_DEBUG_LOG_WRITE(log_ctx,
		"frame_num %d start_col %d end_col %d x_region %d\n", afl_info->frame_num,
		afl_info->start_col, afl_info->end_col, afl_info->step_x_region);
	CAM_DEBUG_LOG_WRITE(log_ctx,
		"y_region %d x_start_region %d x_end_region %d\n", afl_info->step_y_region,
		afl_info->step_x_start_region, afl_info->step_x_end_region);
	CAM_DEBUG_LOG_WRITE(log_ctx,
		"glb_num %d region_num %d imgsize %d %d\n", afl_info->afl_glb_total_num,
		afl_info->afl_region_total_num,
		afl_info->img_size.width,
		afl_info->img_size.height);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

static uint32_t get_afl_data_num(uint32_t afl_stepy,
		uint32_t afl_frm_num, uint32_t ImgHeight)
{
	unsigned int data_long = 0;
	unsigned int v_counter = 0;
	unsigned int v_factor = afl_stepy;
	unsigned int row = 0;

	for (row = 0; row < ImgHeight; row++) {
		if (row == ((v_counter + V_COUNTER_INTERVAL) >> 20)) {
			data_long++;
			v_counter += v_factor;
		}
	}
	/*statistics on the 64bit data num for one frame*/
	if (data_long % 2 == 0)
		data_long = data_long / 2;
	else
		data_long = data_long / 2 + 1;
	/*statistics on the 64bit data num for one frame*/
	data_long = data_long * afl_frm_num;

	return data_long;

}

static int get_afl_region_data_num(uint32_t step_y_region,
		uint32_t afl_frm_num, uint32_t ImgHeight)
{
	unsigned int data_long = 0;
	unsigned int v_counter = 0;
	unsigned int v_factor = step_y_region;
	unsigned int row = 0;

	for (row = 0; row < ImgHeight; row++) {
		if (row == ((v_counter + V_COUNTER_INTERVAL) >> 20)) {
			data_long += 4;
			v_counter += v_factor;
		}
	}
	/*add 8,32bit flatness data for one frame*/
	data_long = data_long + 8;
	/*statistics on the 64bit data num for one frame*/
	data_long = data_long / 2;
	/*statistic on the 64bit data for every periods*/
	data_long = data_long * afl_frm_num;

	return data_long;
}

int dcam_k_afl_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	uint32_t val = 0;
	struct isp_dev_anti_flicker_new_info *p;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (param == NULL)
		return -1;

	idx = param->idx;
	p = &(param->afl.afl_info);
	dev = param->dev;

	if (dev && idx < DCAM_HW_CONTEXT_MAX) {
		hw_ctx = &dev->hw_ctx[idx];
		if (g_dcam_block_dump & (1 << _E_AFL)) {
			hw_ctx->debug.idx = idx;
			hw_ctx->debug.fid = hw_ctx->fid;
			hw_ctx->debug.block_param = 1;
		}
		dcam_k_afl_param_dump(&param->afl.afl_info, &hw_ctx->debug);
	}

	pr_debug("cfg afl bypass %d, mode %d %d %d\n",
		p->bypass, p->mode,
		p->bayer2y_chanel, p->bayer2y_mode);
	DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL, BIT_0, p->bypass);
	/* bayer2y, bypass should be same as afl bypass. */
	DCAM_REG_MWR(idx, ISP_AFL_PARAM0, BIT_0, p->bypass);
	if (p->bypass)
		return 0;

	val = ((p->bayer2y_chanel & 0x3) << 6) |
		((p->bayer2y_mode & 0x3) << 4);
	DCAM_REG_MWR(idx, ISP_AFL_PARAM0, 0xF0, val);

	DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL, BIT_2, p->mode << 2);
	if (p->mode == AFL_MODE_SINGLE)
		/* trigger next work frame for single mode */
		DCAM_REG_MWR(idx, ISP_AFL_CFG_READY, BIT_0, 1);
	else
		/* set afl_mul_enable for multi mode */
		DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL, BIT_3, 1 << 3);

	DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL, 0xF0,
		(p->skip_frame_num & 0xF) << 4);
	/* It is better to set afl_skip_num_clr when new skip_num is set. */
	DCAM_REG_MWR(idx, ISP_AFL_CFG_READY, BIT_1, 1 << 1);

	val = ((p->frame_num & 0xFF) << 16);
	DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL, 0xFF0000, val);

	DCAM_REG_WR(idx, ISP_AFL_PARAM1, (p->afl_stepx & 0xFFFFFF));
	DCAM_REG_WR(idx, ISP_AFL_PARAM2, (p->afl_stepy & 0xFFFFFF));

	val = (p->start_col & 0x1FFF) |
		((p->end_col & 0x1FFF) << 16);
	DCAM_REG_WR(idx, ISP_AFL_COL_POS, val);

	DCAM_REG_WR(idx, ISP_AFL_REGION0, p->step_x_region & 0xFFFFFF);
	DCAM_REG_WR(idx, ISP_AFL_REGION1, p->step_y_region & 0xFFFFFF);
	val = (p->step_x_start_region & 0x1FFF) |
		((p->step_x_end_region & 0x1FFF) << 16);
	DCAM_REG_WR(idx, ISP_AFL_REGION2, val);

	p->afl_glb_total_num =
		get_afl_data_num(p->afl_stepy,
			p->frame_num,
			p->img_size.height);
	p->afl_region_total_num =
		get_afl_region_data_num(p->afl_stepy,
			p->frame_num,
			p->img_size.height);

	DCAM_REG_WR(idx, ISP_AFL_SUM1, p->afl_glb_total_num & 0xFFFFF);
	DCAM_REG_WR(idx, ISP_AFL_SUM2, p->afl_region_total_num & 0xFFFFF);

	return ret;
}

int dcam_k_afl_bypass(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	uint32_t mode;

	if (param == NULL)
		return -1;

	idx = param->idx;

	mode = (DCAM_REG_RD(idx, ISP_AFL_FRM_CTRL) >> 2) & 1;
	pr_debug("afl bypass %d, mode %d\n", param->afl.afl_info.bypass, mode);

	DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL, BIT_0, param->afl.afl_info.bypass);
	/* bayer2y, bypass should be same as afl bypass. */
	DCAM_REG_MWR(idx, ISP_AFL_PARAM0, BIT_0, param->afl.afl_info.bypass);

	if (param->afl.afl_info.bypass == 0) {
		/* It is better to set afl_skip_num_clr
		 * when module is re-enable.
		 */
		DCAM_REG_MWR(idx, ISP_AFL_CFG_READY, BIT_1, 1 << 1);

		if (mode == AFL_MODE_SINGLE) {
			/* trigger next work frame for single mode */
			DCAM_REG_MWR(idx, ISP_AFL_CFG_READY, BIT_0, 1);
		}
	}
	return ret;
}

int dcam_k_cfg_afl(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	void *pcpy;
	int size;
	FUNC_DCAM_PARAM sub_func = NULL;

	switch (param->property) {
	case DCAM_PRO_AFL_BLOCK:
		pcpy = (void *)&(p->afl.afl_info);
		size = sizeof(p->afl.afl_info);
		sub_func = dcam_k_afl_block;
		break;
	case DCAM_PRO_AFL_BYPASS:
		pcpy = (void *)&(p->afl.afl_info.bypass);
		size = sizeof(p->afl.afl_info.bypass);
		sub_func = dcam_k_afl_bypass;
		break;
	default:
		pr_err("fail to support property %d\n",
			param->property);
		return -EINVAL;
	}

	if (p->offline == 0) {
		ret = copy_from_user(pcpy, param->property_param, size);
		if (ret) {
			pr_err("fail to copy, ret=0x%x\n", (unsigned int)ret);
			return -EPERM;
		}

		if (p->idx == DCAM_HW_CONTEXT_MAX)
			return 0;

		ret = sub_func(p);
	} else {
		mutex_lock(&p->param_lock);
		ret = copy_from_user(pcpy, param->property_param, size);
		if (ret) {
			mutex_unlock(&p->param_lock);
			pr_err("fail to copy, ret=0x%x\n", (unsigned int)ret);
			return -EPERM;
		}
		mutex_unlock(&p->param_lock);
	}

	return ret;
}
