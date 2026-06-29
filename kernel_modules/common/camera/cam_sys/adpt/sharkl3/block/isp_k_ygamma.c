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
#define pr_fmt(fmt) "YGAMMA: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkygamma_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_ygamma_info *ygamma_info = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	ygamma_info = &isp_k_param->ygamma_info;
	log_ctx = &debug->block_param_log;

	if (!log_ctx)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "ygamma: ctx %llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, ygamma_info->update_flag, ygamma_info->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "ygamma_info: gain %d %d %d %d %d %d %d %d %d %d\n",
		ygamma_info->gain[0], ygamma_info->gain[15], ygamma_info->gain[31],
		ygamma_info->gain[47], ygamma_info->gain[63], ygamma_info->gain[79],
		ygamma_info->gain[95], ygamma_info->gain[111], ygamma_info->gain[127],
		ygamma_info->gain[128]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int isp_k_ygamma_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	uint32_t i, val;
	uint32_t buf_sel, ybuf_addr;
	struct isp_dev_ygamma_info *ygamma_info = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->ygamma_info.isupdate == 0)
		return ret;
	ygamma_info = &isp_k_param->ygamma_info;

	isp_k_param->ygamma_info.isupdate = 0;

	debug = &isp_k_param->debug;

	if (g_isp_bypass[idx] & (1 << _EISP_GAMY))
		ygamma_info->bypass = 1;

	if (g_isp_block_dump & (1 << _EISP_GAMY))
		ispkygamma_param_dump(isp_k_param, debug);

	ISP_REG_MWR(idx, ISP_YGAMMA_PARAM, BIT_0, ygamma_info->bypass);
	if (ygamma_info->bypass)
		return 0;

	buf_sel = 0;
	ybuf_addr = ISP_YGAMMA_BUF0;
	ISP_REG_MWR(idx, ISP_YGAMMA_PARAM, BIT_1, buf_sel << 1);

	for (i = 0; i < ISP_YUV_GAMMA_NUM - 1; i++) {
		val = ygamma_info->gain[i] | ((ygamma_info->gain[i+1] & 0xFF) << 8);
		ISP_REG_WR(idx, ybuf_addr + i * 4, val);
	}

	return ret;
}

int isp_k_cfg_ygamma(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct isp_dev_ygamma_info *ygamma_info = NULL;

	ygamma_info = &isp_k_param->ygamma_info;
	switch (param->property) {
	case ISP_PRO_YGAMMA_BLOCK:
		ret = copy_from_user((void *)ygamma_info, param->property_param, sizeof(struct isp_dev_ygamma_info));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return ret;
		}
		isp_k_param->ygamma_info.isupdate = 1;
		break;
	default:
		pr_err("fail to support cmd id = %d\n",
			param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_ygamma(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->ygamma_info.isupdate == 1) {
		memcpy(&param_block->ygamma_info, &isp_k_param->ygamma_info, sizeof(struct isp_dev_ygamma_info));
		isp_k_param->ygamma_info.isupdate = 0;
		param_block->ygamma_info.isupdate = 1;
	}

	return ret;
}

