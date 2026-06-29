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
#define pr_fmt(fmt) "AWBC: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void dcam_k_awbc_param_dump(struct dcam_dev_awbc_info *awb_info,
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

	CAM_DEBUG_LOG_WRITE(log_ctx, "awb: bypass %d update %d\n",
		awb_info->awbc_bypass, awb_info->update_flag);
	CAM_DEBUG_LOG_WRITE(log_ctx, "r_gain %d gr_gain %d gb_gain %d b_gain %d\n",
		awb_info->gain.r, awb_info->gain.gr, awb_info->gain.gb, awb_info->gain.b);
	CAM_DEBUG_LOG_WRITE(log_ctx, "r_offset %d gr_offset %d gb_offset %d b_offset %d\n",
		awb_info->gain_offset.r, awb_info->gain_offset.gr,
		awb_info->gain_offset.gb, awb_info->gain_offset.b);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_awbc_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	uint32_t val = 0;
	struct dcam_dev_awbc_info *p = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (param == NULL)
		return -EPERM;

	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;
	p = &(param->awbc.awbc_info);
	dev = param->dev;

	if (dev) {
		hw_ctx = &dev->hw_ctx[idx];
		if (hw_ctx) {
			if (g_dcam_block_dump & (1 << _E_AWBC)) {
				hw_ctx->debug.idx = idx;
				hw_ctx->debug.fid = hw_ctx->fid;
				hw_ctx->debug.block_param = 1;
			}
			dcam_k_awbc_param_dump(&param->awbc.awbc_info, &hw_ctx->debug);
		}
	}

	if (g_dcam_bypass[idx] & (1 << _E_AWBC))
		p->awbc_bypass = 1;

	DCAM_REG_MWR(idx, DCAM_AWBC_GAIN0, BIT_31,
			((p->awbc_bypass & 1) << 31));
	if (p->awbc_bypass)
		return 0;

	val = ((p->gain.b & 0x3FFF) << 16) |
		(p->gain.r & 0x3FFF);
	DCAM_REG_MWR(idx, DCAM_AWBC_GAIN0,
			(0x3FFF << 16 | 0x3FFF), val);

	val = ((p->gain.gb & 0x3FFF) << 16) |
		(p->gain.gr & 0x3FFF);
	DCAM_REG_WR(idx, DCAM_AWBC_GAIN1, val);

	p->thrd.b = 0x3FFF;
	p->thrd.r = 0x3FFF;
	val = ((p->thrd.b & 0x3FFF) << 16) |
		(p->thrd.r & 0x3FFF);
	DCAM_REG_WR(idx, DCAM_AWBC_THRD0, val);

	p->thrd.gr = 0x3FFF;
	val = (p->thrd.gr & 0x3FFF);
	DCAM_REG_WR(idx, DCAM_AWBC_THRD1, val);

	val = ((p->gain_offset.b & 0x7FFF) << 16) |
		(p->gain_offset.r & 0x7FFF);
	DCAM_REG_WR(idx, DCAM_AWBC_OFFSET0, val);

	val = ((p->gain_offset.gb & 0x7FFF) << 16) |
		(p->gain_offset.gr & 0x7FFF);
	DCAM_REG_WR(idx, DCAM_AWBC_OFFSET1, val);

	return ret;
}

int dcam_k_awbc_gain(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	uint32_t val = 0;
	struct img_rgb_info *p = NULL;

	if (param == NULL)
		return -EPERM;

	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;
	p = &(param->awbc.awbc_info.gain);
	val = ((p->b & 0x3FFF) << 16) |
		(p->r & 0x3FFF);
	DCAM_REG_MWR(idx, DCAM_AWBC_GAIN0,
			(0x3FFF << 16 | 0x3FFF), val);

	val = ((p->gb & 0x3FFF) << 16) |
		(p->gr & 0x3FFF);
	DCAM_REG_WR(idx, DCAM_AWBC_GAIN1, val);

	return ret;
}

int dcam_k_awbc_bypass(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	uint32_t bypass = 0;

	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;
	bypass = param->awbc.awbc_info.awbc_bypass;
	DCAM_REG_MWR(idx, DCAM_AWBC_GAIN0, BIT_31, bypass << 31);

	return ret;
}

int dcam_k_cfg_awbc(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	void *pcpy;
	int size;
	FUNC_DCAM_PARAM sub_func = NULL;

	switch (param->property) {
	case DCAM_PRO_AWBC_BLOCK:
		pcpy = (void *)&(p->awbc.awbc_info);
		size = sizeof(p->awbc.awbc_info);
		sub_func = dcam_k_awbc_block;
		break;
	case DCAM_PRO_AWBC_GAIN:
		pcpy = (void *)&(p->awbc.awbc_info.gain);
		size = sizeof(p->awbc.awbc_info.gain);
		sub_func = dcam_k_awbc_gain;
		break;
	case DCAM_PRO_AWBC_BYPASS:
		pcpy = (void *)&(p->awbc.awbc_info.awbc_bypass);
		size = sizeof(p->awbc.awbc_info.awbc_bypass);
		sub_func = dcam_k_awbc_bypass;
		break;
	default:
		pr_err("fail to support property %d\n",
			param->property);
		return -EINVAL;
	}

	if (p->offline == 0) {
		ret = copy_from_user(pcpy, param->property_param, size);
		if (ret) {
			pr_err("fail to copy from user ret=0x%x\n",
				(unsigned int)ret);
			return -EPERM;
		}
		if (p->idx == DCAM_HW_CONTEXT_MAX)
			return 0;
		if (g_dcam_bypass[p->idx] & (1 << _E_AWBC))
			p->awbc.awbc_info.awbc_bypass = 1;
		ret = sub_func(p);
		return ret;
	}

	/* offline just save parameters in structure */
	mutex_lock(&p->param_lock);
	ret = copy_from_user(pcpy, param->property_param, size);
	if (ret) {
		mutex_unlock(&p->param_lock);
		pr_err("fail to copy from user ret=0x%x\n", (unsigned int)ret);
		return -EPERM;
	}
	mutex_unlock(&p->param_lock);

	return ret;
}
