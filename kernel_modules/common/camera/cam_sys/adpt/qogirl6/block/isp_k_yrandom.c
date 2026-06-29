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
#define pr_fmt(fmt) "YRANDOM: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkyrandom_block_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_yrandom_info *yrandom_info = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	yrandom_info = &isp_k_param->yrandom_info;
	log_ctx = &debug->block_param_log;

	if (!log_ctx)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "yrandom: ctx %llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, yrandom_info->update_flag, yrandom_info->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "yrandom: mode %d, offset %d seed %d shift %d\n",
		yrandom_info->mode, yrandom_info->offset,
		yrandom_info->seed, yrandom_info->shift);
	CAM_DEBUG_LOG_WRITE(log_ctx, "yrandom: takeBit %d, %d, %d, %d, %d, %d, %d, %d\n",
		yrandom_info->takeBit[0], yrandom_info->takeBit[1], yrandom_info->takeBit[2],
		yrandom_info->takeBit[3], yrandom_info->takeBit[4], yrandom_info->takeBit[5],
		yrandom_info->takeBit[6], yrandom_info->takeBit[7]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int isp_k_yrandom_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	uint32_t val = 0;
	struct isp_dev_yrandom_info *yrandom_info = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->yrandom_info.isupdate == 0)
		return ret;

	yrandom_info = &isp_k_param->yrandom_info;
	isp_k_param->yrandom_info.isupdate = 0;

	debug = &isp_k_param->debug;

	if (g_isp_bypass[idx] & (1 << _EISP_YRAND))
		yrandom_info->bypass = 1;

	if (g_isp_block_dump & (1 << _EISP_YRAND))
		ispkyrandom_block_param_dump(isp_k_param, debug);

	ISP_REG_MWR(idx, ISP_YRANDOM_PARAM1, BIT_0, yrandom_info->bypass);
	if (yrandom_info->bypass)
		return 0;

	val = (yrandom_info->seed << 8) |
		((yrandom_info->mode & 1) << 1);
	ISP_REG_MWR(idx, ISP_YRANDOM_PARAM1, 0xFFFFFF02, val);

	if (yrandom_info->mode == 0)
		ISP_REG_WR(idx, ISP_YRANDOM_INIT, 1);

	val = (yrandom_info->shift & 0xF) |
		((yrandom_info->offset & 0x7FF) << 16);
	ISP_REG_MWR(idx, ISP_YRANDOM_PARAM2, 0x7FF000F, val);

	val = (yrandom_info->takeBit[0]  & 0xF) |
		((yrandom_info->takeBit[1] & 0xF) << 4) |
		((yrandom_info->takeBit[2] & 0xF) << 8) |
		((yrandom_info->takeBit[3] & 0xF) << 12) |
		((yrandom_info->takeBit[4] & 0xF) << 16) |
		((yrandom_info->takeBit[5] & 0xF) << 20) |
		((yrandom_info->takeBit[6] & 0xF) << 24) |
		((yrandom_info->takeBit[7] & 0xF) << 28);
	ISP_REG_WR(idx, ISP_YRANDOM_PARAM3, val);

	return ret;
}

int isp_k_cfg_yrandom(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct isp_dev_yrandom_info *yrandom_info = NULL;

	yrandom_info = &isp_k_param->yrandom_info;

	switch (param->property) {
	case ISP_PRO_YRANDOM_BLOCK:
		ret = copy_from_user((void *)yrandom_info, param->property_param, sizeof(struct isp_dev_yrandom_info));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return ret;
		}
		isp_k_param->yrandom_info.isupdate = 1;
		break;
	default:
		pr_err("fail to support cmd id:%d\n", param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_yrandom(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->yrandom_info.isupdate == 1) {
		memcpy(&param_block->yrandom_info, &isp_k_param->yrandom_info, sizeof(struct isp_dev_yrandom_info));
		isp_k_param->yrandom_info.isupdate = 0;
		param_block->yrandom_info.isupdate = 1;
	}
	return ret;
}
