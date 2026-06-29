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
#define pr_fmt(fmt) "GAMMA: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void dcamkgamma_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_gamma_info_v1 *gamma_param = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	gamma_param = &isp_k_param->gamma_info_v1.gamma_info;
	log_ctx = &debug->block_param_log;

	if (!debug->block_param)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "GAMMA: ctx_id %llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, gamma_param->update_flag, gamma_param->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "GAMMA: gain_r %d %d %d %d %d %d %d %d %d %d\n",
		gamma_param->gain_r[0], gamma_param->gain_r[15], gamma_param->gain_r[31],
		gamma_param->gain_r[47], gamma_param->gain_r[63], gamma_param->gain_r[79],
		gamma_param->gain_r[95], gamma_param->gain_r[111], gamma_param->gain_r[127],
		gamma_param->gain_r[128]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "GAMMA: gain_g %d %d %d %d %d %d %d %d %d %d\n",
		gamma_param->gain_g[0], gamma_param->gain_g[15], gamma_param->gain_g[31],
		gamma_param->gain_g[47], gamma_param->gain_g[63], gamma_param->gain_g[79],
		gamma_param->gain_g[95], gamma_param->gain_g[111], gamma_param->gain_g[127],
		gamma_param->gain_g[128]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "GAMMA: gain_b %d %d %d %d %d %d %d %d %d %d\n",
		gamma_param->gain_b[0], gamma_param->gain_b[15], gamma_param->gain_b[31],
		gamma_param->gain_b[47], gamma_param->gain_b[63], gamma_param->gain_b[79],
		gamma_param->gain_b[95], gamma_param->gain_b[111], gamma_param->gain_b[127],
		gamma_param->gain_b[128]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_gamma_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	uint32_t val = 0;
	uint32_t i = 0;
	struct isp_dev_gamma_info_v1 *p;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (param == NULL)
		return -EPERM;

	idx = param->idx;
	p = &(param->gamma_info_v1.gamma_info);

	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	dev = param->dev;

	if (dev && idx < DCAM_HW_CONTEXT_MAX) {
		hw_ctx = &dev->hw_ctx[idx];
		if (hw_ctx) {
			if (g_dcam_block_dump & (1 << _E_GAMMA)) {
				hw_ctx->debug.idx = idx;
				hw_ctx->debug.fid = hw_ctx->fid;
				hw_ctx->debug.block_param = 1;
			}
			dcamkgamma_param_dump(param, &hw_ctx->debug);
		}
	}

	if (g_dcam_bypass[idx] & (1 << _E_GAMMA))
		p->bypass = 1;

	/* only cfg mode and buf0 is selected. */
	DCAM_REG_MWR(idx, DCAM_BUF_CTRL, BIT_4, BIT_4);
	DCAM_REG_MWR(idx, DCAM_FGAMMA10_PARAM, BIT_0, p->bypass);
	if (p->bypass)
		return 0;

	DCAM_REG_MWR(idx, DCAM_BUF_CTRL, 0x300, 0 << 8);
	for (i = 0; i < ISP_FRGB_GAMMA_PT_NUM_V1 - 1; i++) {
		val = ((p->gain_r[i] & 0x3FF) << 10) | (p->gain_r[i + 1] & 0x3FF);
		DCAM_REG_BWR(idx, DCAM_FGAMMA10_TABLE + i * 4, val);
	}

	DCAM_REG_MWR(idx, DCAM_BUF_CTRL, 0x300, 1 << 8);
	for (i = 0; i < ISP_FRGB_GAMMA_PT_NUM_V1 - 1; i++) {
		val = ((p->gain_g[i] & 0x3FF) << 10) | (p->gain_g[i + 1] & 0x3FF);
		DCAM_REG_BWR(idx, DCAM_FGAMMA10_TABLE + i * 4, val);
	}

	DCAM_REG_MWR(idx, DCAM_BUF_CTRL, 0x300, 2 << 8);
	for (i = 0; i < ISP_FRGB_GAMMA_PT_NUM_V1 - 1; i++) {
		val = ((p->gain_b[i] & 0x3FF) << 10) | (p->gain_b[i + 1] & 0x3FF);
		DCAM_REG_BWR(idx, DCAM_FGAMMA10_TABLE + i * 4, val);
	}

	val = DCAM_REG_RD(idx, DCAM_BUF_CTRL);
	val = val & BIT_20;
	DCAM_REG_MWR(idx, DCAM_BUF_CTRL, BIT_20, ~val);

	return ret;
}

int dcam_k_cfg_gamma(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;

	switch (param->property) {
	case ISP_PRO_GAMMA_BLOCK:
		/* online mode not need mutex, response faster
		 * Offline need mutex to protect param
		 */
		if (p->offline == 0) {
			ret = copy_from_user((void *)&(p->gamma_info_v1.gamma_info),
				param->property_param,
				sizeof(p->gamma_info_v1.gamma_info));
			if (ret) {
				pr_err("fail to copy from user ret=0x%x\n", (unsigned int)ret);
				return -EPERM;
			}
			if (p->idx == DCAM_HW_CONTEXT_MAX || param->scene_id == PM_SCENE_CAP)
				return 0;
			if (g_dcam_bypass[p->idx] & (1 << _E_GAMMA))
				p->gamma_info_v1.gamma_info.bypass = 1;
			ret = dcam_k_gamma_block(p);
		} else {
			mutex_lock(&p->param_lock);
			ret = copy_from_user((void *)&(p->gamma_info_v1.gamma_info),
				param->property_param,
				sizeof(p->gamma_info_v1.gamma_info));
			if (ret) {
				mutex_unlock(&p->param_lock);
				pr_err("fail to copy from user ret=0x%x\n", (unsigned int)ret);
				return -EPERM;
			}
			mutex_unlock(&p->param_lock);
		}
		break;
	default:
		pr_err("fail to support cmd id = %d\n", param->property);
		break;
	}

	return ret;
}
