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
#define pr_fmt(fmt) "CMC10: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void dcamkcmc_param_dump(struct dcam_isp_k_block *isp_k_param,
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

	if (!debug->block_param)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "CMC: ctx%llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, cmc_param->update_flag, cmc_param->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "CMC: matrix [%d %d %d %d %d %d %d %d %d]\n",
		cmc_param->matrix.val[0], cmc_param->matrix.val[1], cmc_param->matrix.val[2],
		cmc_param->matrix.val[3], cmc_param->matrix.val[4], cmc_param->matrix.val[5],
		cmc_param->matrix.val[6], cmc_param->matrix.val[7], cmc_param->matrix.val[8]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_cmc10_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	uint32_t val = 0;
	struct isp_dev_cmc10_info *cmc10_info = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (param == NULL)
		return 0;

	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;
	cmc10_info = &param->cmc10_info;

	dev = param->dev;

	if (dev) {
		hw_ctx = &dev->hw_ctx[idx];
		if (hw_ctx) {
			if (g_dcam_block_dump & (1 << _E_CMC)) {
				hw_ctx->debug.idx = idx;
				hw_ctx->debug.fid = hw_ctx->fid;
				hw_ctx->debug.block_param = 1;
			}
			dcamkcmc_param_dump(param, &hw_ctx->debug);
		}
	}

	if (g_dcam_bypass[idx] & (1 << _E_CMC))
		cmc10_info->bypass = 1;

	DCAM_REG_MWR(idx, DCAM_CMC10_PARAM, BIT_0, cmc10_info->bypass);
	if (cmc10_info->bypass)
		return 0;

	val = ((cmc10_info->matrix.val[1] & 0x3FFF) << 14) |
		(cmc10_info->matrix.val[0] & 0x3FFF);
	DCAM_REG_WR(idx, DCAM_CMC10_MATRIX0, val);

	val = ((cmc10_info->matrix.val[3] & 0x3FFF) << 14) |
		(cmc10_info->matrix.val[2] & 0x3FFF);
	DCAM_REG_WR(idx, (DCAM_CMC10_MATRIX1), val);

	val = ((cmc10_info->matrix.val[5] & 0x3FFF) << 14) |
		(cmc10_info->matrix.val[4] & 0x3FFF);
	DCAM_REG_WR(idx, (DCAM_CMC10_MATRIX2), val);

	val = ((cmc10_info->matrix.val[7] & 0x3FFF) << 14) |
		(cmc10_info->matrix.val[6] & 0x3FFF);
	DCAM_REG_WR(idx, (DCAM_CMC10_MATRIX3), val);

	val = cmc10_info->matrix.val[8] & 0x3FFF;
	DCAM_REG_WR(idx, (DCAM_CMC10_MATRIX4), val);

	return ret;
}

int dcam_k_cfg_cmc10(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;

	switch (param->property) {
	case ISP_PRO_CMC_BLOCK:
		if (p->offline == 0) {
			ret = copy_from_user((void *)&(p->cmc10_info),
				param->property_param,
				sizeof(p->cmc10_info));
			if (ret) {
				pr_err("fail to copy from user ret=0x%x\n", (unsigned int)ret);
				return -EPERM;
			}
			if (p->idx == DCAM_HW_CONTEXT_MAX)
				return 0;
			if (g_dcam_bypass[p->idx] & (1 << _E_CMC))
				p->cmc10_info.bypass = 1;
			ret = dcam_k_cmc10_block(p);
		} else {
			mutex_lock(&p->param_lock);
			ret = copy_from_user((void *)&(p->cmc10_info),
				param->property_param, sizeof(p->cmc10_info));
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
