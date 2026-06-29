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
#define pr_fmt(fmt) "CMC10: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkcmc_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_cmc10_info *cmc_param = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	cmc_param = &isp_k_param->cmc10_info;
	log_ctx = &debug->block_param_log;

	if (!log_ctx)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "CMC: ctx%llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, cmc_param->update_flag, cmc_param->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "CMC: matrix [%d %d %d %d %d %d %d %d %d]\n",
		cmc_param->matrix.val[0], cmc_param->matrix.val[1], cmc_param->matrix.val[2],
		cmc_param->matrix.val[3], cmc_param->matrix.val[4], cmc_param->matrix.val[5],
		cmc_param->matrix.val[6], cmc_param->matrix.val[7], cmc_param->matrix.val[8]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int isp_k_cmc10_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	uint32_t val = 0;
	struct isp_dev_cmc10_info *cmc10_info = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->cmc10_info.isupdate == 0)
		return ret;

	cmc10_info = &isp_k_param->cmc10_info;
	isp_k_param->cmc10_info.isupdate = 0;

	debug = &isp_k_param->debug;

	if (g_isp_block_dump & (1 << _EISP_CMC))
		ispkcmc_param_dump(isp_k_param, debug);

	if (g_isp_bypass[idx] & (1 << _EISP_CMC))
		cmc10_info->bypass = 1;

	ISP_REG_MWR(idx, ISP_CMC10_PARAM, BIT_0, cmc10_info->bypass);
	if (cmc10_info->bypass)
		return 0;

	val = ((cmc10_info->matrix.val[1] & 0x3FFF) << 14) |
		(cmc10_info->matrix.val[0] & 0x3FFF);
	ISP_REG_WR(idx, ISP_CMC10_MATRIX0, val);

	val = ((cmc10_info->matrix.val[3] & 0x3FFF) << 14) |
		(cmc10_info->matrix.val[2] & 0x3FFF);
	ISP_REG_WR(idx, (ISP_CMC10_MATRIX0 + 4), val);

	val = ((cmc10_info->matrix.val[5] & 0x3FFF) << 14) |
		(cmc10_info->matrix.val[4] & 0x3FFF);
	ISP_REG_WR(idx, (ISP_CMC10_MATRIX0 + 8), val);

	val = ((cmc10_info->matrix.val[7] & 0x3FFF) << 14) |
		(cmc10_info->matrix.val[6] & 0x3FFF);
	ISP_REG_WR(idx, (ISP_CMC10_MATRIX0 + 12), val);

	val = cmc10_info->matrix.val[8] & 0x3FFF;
	ISP_REG_WR(idx, (ISP_CMC10_MATRIX0 + 16), val);

	return ret;
}

int isp_k_cfg_cmc10(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct isp_dev_cmc10_info *cmc10_info = NULL;

	cmc10_info = &isp_k_param->cmc10_info;

	switch (param->property) {
	case ISP_PRO_CMC_BLOCK:
		ret = copy_from_user((void *)cmc10_info, param->property_param, sizeof(struct isp_dev_cmc10_info));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return ret;
		}
		isp_k_param->cmc10_info.isupdate = 1;
		break;
	default:
		pr_err("fail to support cmd id = %d\n",
			param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_cmc10(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->cmc10_info.isupdate == 1) {
		memcpy(&param_block->cmc10_info, &isp_k_param->cmc10_info, sizeof(struct isp_dev_cmc10_info));
		isp_k_param->cmc10_info.isupdate = 0;
		param_block->cmc10_info.isupdate = 1;
	}

	return ret;
}
