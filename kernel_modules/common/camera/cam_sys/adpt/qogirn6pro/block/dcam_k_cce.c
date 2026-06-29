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
#define pr_fmt(fmt) "CCE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void dcamkcce_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_cce_info *cce_info = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	cce_info = &isp_k_param->cce_info;
	log_ctx = &debug->block_param_log;

	if (!debug->block_param)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "CCE: ctx_id %llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, cce_info->update_flag, cce_info->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "CCE: matrix %d %d %d, %d %d %d, %d %d %d, offset %d %d %d\n",
		cce_info->matrix[0], cce_info->matrix[1], cce_info->matrix[2], cce_info->matrix[3],
		cce_info->matrix[4], cce_info->matrix[5], cce_info->matrix[6], cce_info->matrix[7],
		cce_info->matrix[8], cce_info->y_offset, cce_info->u_offset, cce_info->v_offset);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_cce_block(struct dcam_isp_k_block *p)
{
	int ret = 0;
	uint32_t val = 0;
	uint32_t idx = 0;
	struct isp_dev_cce_info *cce_info = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (p == NULL)
		return 0;

	cce_info = &p->cce_info;
	idx = p->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	dev = p->dev;
	if (dev) {
		hw_ctx = &dev->hw_ctx[idx];
		if (hw_ctx) {
			if (g_dcam_block_dump & (1 << _E_CCE)) {
				hw_ctx->debug.idx = idx;
				hw_ctx->debug.fid = hw_ctx->fid;
				hw_ctx->debug.block_param = 1;
			}
			dcamkcce_param_dump(p, &hw_ctx->debug);
		}
	}

	if (g_dcam_bypass[idx] & (1 << _E_CCE))
		cce_info->bypass = 1;

	DCAM_REG_WR(idx, DCAM_CCE_PARAM, cce_info->bypass);
	if (cce_info->bypass)
		return 0;

	val = ((cce_info->matrix[1] & 0x7FF) << 11) |
		(cce_info->matrix[0] & 0x7FF);
	DCAM_REG_WR(idx, DCAM_CCE_MATRIX0, val);

	val = ((cce_info->matrix[3] & 0x7FF) << 11) |
		(cce_info->matrix[2] & 0x7FF);
	DCAM_REG_WR(idx, DCAM_CCE_MATRIX1, val);

	val = ((cce_info->matrix[5] & 0x7FF) << 11) |
		(cce_info->matrix[4] & 0x7FF);
	DCAM_REG_WR(idx, DCAM_CCE_MATRIX2, val);

	val = ((cce_info->matrix[7] & 0x7FF) << 11) |
		(cce_info->matrix[6] & 0x7FF);
	DCAM_REG_WR(idx, DCAM_CCE_MATRIX3, val);

	val = (cce_info->matrix[8] & 0x7FF)
		| ((cce_info->v_offset & 0x7FF) << 16);
	DCAM_REG_WR(idx, DCAM_CCE_MATRIX4, val);

	val = (cce_info->y_offset & 0x7FF)
		| ((cce_info->u_offset & 0x7FF) << 16);
	DCAM_REG_WR(idx, DCAM_CCE_SHIFT, (val & 0x7FF07FF));

	return ret;
}

int dcam_k_cfg_cce(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;

	switch (param->property) {
	case ISP_PRO_CCE_BLOCK:
		if (p->offline == 0) {
			ret = copy_from_user((void *)&(p->cce_info),
				param->property_param, sizeof(p->cce_info));
			if (ret) {
				pr_err("fail to copy from user ret=0x%x\n", (unsigned int)ret);
				return -EPERM;
			}
			if (p->idx == DCAM_HW_CONTEXT_MAX)
				return 0;
			ret = dcam_k_cce_block(p);
		} else {
			mutex_lock(&p->param_lock);
			ret = copy_from_user((void *)&(p->cce_info),
				param->property_param, sizeof(p->cce_info));
			if (ret) {
				mutex_unlock(&p->param_lock);
				pr_err("fail to copy from user ret=0x%x\n", (unsigned int)ret);
				return -EPERM;
			}
			mutex_unlock(&p->param_lock);
		}
		break;
	default:
		pr_err("fail to support cmd id = %d\n",
			param->property);
		break;
	}

	return ret;
}
