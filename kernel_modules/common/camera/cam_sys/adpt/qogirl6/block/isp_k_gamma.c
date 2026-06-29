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
#include "isp_reg.h"
#include "cam_debugger.h"
#include "isp_dev.h"
#include "isp_hwctx.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "GAMMA: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkgamma_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_gamma_info *gamma_param = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	gamma_param = &isp_k_param->gamma_info;
	log_ctx = &debug->block_param_log;

	if (!log_ctx)
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

int isp_k_gamma_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	uint32_t i = 0, buf_sel = 0, buf_addr = 0, val = 0;
	struct isp_dev_gamma_info *gamma_info = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->gamma_info.isupdate == 0)
		return ret;

	debug = &isp_k_param->debug;

	if (g_isp_block_dump & (1 << _EISP_GAMC))
		ispkgamma_param_dump(isp_k_param, debug);

	gamma_info = &isp_k_param->gamma_info;
	isp_k_param->gamma_info.isupdate = 0;

	if (g_isp_bypass[idx] & (1 << _EISP_GAMC))
		gamma_info->bypass = 1;

	ISP_REG_MWR(idx, ISP_GAMMA_PARAM, BIT_0,
			gamma_info->bypass);
	if (gamma_info->bypass)
		return 0;

	/* only cfg mode and buf0 is selected. */
	ISP_REG_MWR(idx, ISP_GAMMA_PARAM, BIT_1, buf_sel << 1);

	buf_addr = ISP_FGAMMA_R_BUF0;
	for (i = 0; i < ISP_FRGB_GAMMA_PT_NUM - 1; i++) {
		val = gamma_info->gain_r[i];
		val = ((val << 8) | gamma_info->gain_r[i + 1]);
		ISP_REG_WR(idx, buf_addr + i * 4, val);
	}

	buf_addr = ISP_FGAMMA_G_BUF0;
	for (i = 0; i < ISP_FRGB_GAMMA_PT_NUM - 1; i++) {
		val = gamma_info->gain_g[i];
		val = ((val << 8) | gamma_info->gain_g[i + 1]);
		ISP_REG_WR(idx, buf_addr + i * 4, val);
	}

	buf_addr = ISP_FGAMMA_B_BUF0;
	for (i = 0; i < ISP_FRGB_GAMMA_PT_NUM - 1; i++) {
		val = gamma_info->gain_b[i];
		val = ((val << 8) | gamma_info->gain_b[i + 1]);
		ISP_REG_WR(idx, buf_addr + i * 4, val);
	}

	return ret;
}

int isp_k_cfg_gamma(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct isp_dev_gamma_info *gamma_info = NULL;

	gamma_info = &isp_k_param->gamma_info;

	switch (param->property) {
	case ISP_PRO_GAMMA_BLOCK:
		ret = copy_from_user((void *)gamma_info, param->property_param, sizeof(struct isp_dev_gamma_info));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return  ret;
		}
		isp_k_param->gamma_info.isupdate = 1;
		break;
	default:
		pr_err("fail to support cmd id = %d\n",
			param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_gamma(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->gamma_info.isupdate == 1) {
		memcpy(&param_block->gamma_info, &isp_k_param->gamma_info, sizeof(struct isp_dev_gamma_info));
		isp_k_param->gamma_info.isupdate = 0;
		param_block->gamma_info.isupdate = 1;
	}

	return ret;
}
