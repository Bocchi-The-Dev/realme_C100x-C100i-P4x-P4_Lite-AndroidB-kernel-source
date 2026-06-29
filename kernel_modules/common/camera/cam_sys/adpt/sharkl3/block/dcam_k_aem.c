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

#include "dcam_core.h"
#include "dcam_reg.h"
#include "cam_debugger.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "AEM: %d %d %s : " fmt, current->pid, __LINE__, __func__

enum {
	_UPDATE_WIN = BIT(2),
};

static void dcam_k_aem_param_dump(struct dcam_dev_aem_param *aem_info,
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

	CAM_DEBUG_LOG_WRITE(log_ctx, "aem: bypass %d update %d\n",
		aem_info->bypass, aem_info->update_flag);
	CAM_DEBUG_LOG_WRITE(log_ctx, "mode %d, skip_num %d, win %d %d %d %d\n", aem_info->mode,
		aem_info->skip_num, aem_info->win_info.blk_num_x,
		aem_info->win_info.blk_num_y, aem_info->win_info.blk_width,
		aem_info->win_info.blk_height);
	CAM_DEBUG_LOG_WRITE(log_ctx,
		"r_thr %d %d g_thr %d %d b_thr %d %d\n", aem_info->aem_info.aem_r_thr.low_thr,
		aem_info->aem_info.aem_r_thr.high_thr, aem_info->aem_info.aem_g_thr.low_thr,
		aem_info->aem_info.aem_g_thr.high_thr, aem_info->aem_info.aem_b_thr.low_thr,
		aem_info->aem_info.aem_b_thr.high_thr);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_aem_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0, mode = 0, val = 0;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_dev_aem_thr *p_thr = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (param == NULL)
		return -EPERM;

	idx = param->idx;
	mode = param->aem.mode;
	dev = param->dev;
	p_thr = &(param->aem.aem_info);
	if (idx >= DCAM_HW_CONTEXT_MAX) {
		pr_err("warnning:illeagle dcam idx:%d.\n", idx);
		return 0;
	}

	hw_ctx = &dev->hw_ctx[idx];
	if (hw_ctx == NULL) {
		pr_err("fail to get hw_ctx\n");
		return -EPERM;
	}
	if (g_dcam_block_dump & (1 << _E_AEM)) {
		hw_ctx->debug.idx = idx;
		hw_ctx->debug.fid = hw_ctx->fid;
		hw_ctx->debug.block_param = 1;
	}
	dcam_k_aem_param_dump(&param->aem, &hw_ctx->debug);

	DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_0, param->aem.bypass & BIT_0);
	if (param->aem.bypass)
		return 0;

	DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_2, mode << 2);
	if (mode == AEM_MODE_SINGLE)
		DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL1, BIT_0, 0x1);
	else
		DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_3, (0x1 << 3));

	if (param->is_high_fps)
		param->aem.skip_num = hw_ctx->slowmotion_count - 1;
	if (hw_ctx->fid == 0) {
		val = (param->aem.skip_num & 0xF) << 4;
		DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, 0xF0, val);
		/* It is better to set aem_skip_num_clr when new skip_num is set. */
		DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL1, BIT_1, 1 << 1);
		dcam_online_port_skip_num_set(param->dev, idx, DCAM_PATH_AEM, param->aem.skip_num);
		pr_debug("DCAM%u AEM set skip_num %u\n", idx, param->aem.skip_num);
	}

	val = ((p_thr->aem_r_thr.low_thr & 0x3FF) << 16) | (p_thr->aem_r_thr.high_thr & 0x3FF);
	DCAM_REG_WR(idx, DCAM_AEM_RED_THR, val);
	val = ((p_thr->aem_b_thr.low_thr & 0x3FF) << 16) | (p_thr->aem_b_thr.high_thr & 0x3FF);
	DCAM_REG_WR(idx, DCAM_AEM_BLUE_THR, val);
	val = ((p_thr->aem_g_thr.low_thr & 0x3FF) << 16) | (p_thr->aem_g_thr.high_thr & 0x3FF);
	DCAM_REG_WR(idx, DCAM_AEM_GREEN_THR, val);

	return ret;
}

int dcam_k_aem_win(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0, val = 0;
	struct dcam_dev_aem_win *p = NULL;

	if (param == NULL)
		return -1;

	if (param->idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	idx = param->idx;
	p = &(param->aem.win_info);

	val = ((p->offset_y & 0x1FFF) << 16) | (p->offset_x & 0x1FFF);
	DCAM_REG_WR(idx, DCAM_AEM_OFFSET, val);
	val = ((p->blk_height & 0xFF) << 8) | (p->blk_width & 0xFF);
	DCAM_REG_WR(idx, DCAM_AEM_BLK_SIZE, val);
	val = ((p->blk_num_y & 0xFF) << 8) | (p->blk_num_x & 0xFF);
	DCAM_REG_WR(idx, DCAM_AEM_BLK_NUM, val);

	return ret;
}

int dcam_k_cfg_aem(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	void *pcpy;
	unsigned long flags = 0;
	struct dcam_dev_aem_param aem = {0};

	pcpy = (void *)&(p->aem);
	if (p->offline == 0) {
		ret = copy_from_user(&aem, param->property_param,
			sizeof(struct dcam_dev_aem_param));
		if (ret) {
			pr_err("fail to copy from user ret=0x%x\n", (unsigned int)ret);
			return -EPERM;
		}
		spin_lock_irqsave(&p->aem_win_lock, flags);
		memcpy(pcpy, &aem, sizeof(struct dcam_dev_aem_param));
		spin_unlock_irqrestore(&p->aem_win_lock, flags);

		if (p->idx == DCAM_HW_CONTEXT_MAX)
			return 0;
		ret = dcam_k_aem_block(p);
	} else {
		mutex_lock(&p->param_lock);
		ret = copy_from_user(pcpy, param->property_param,
			sizeof(struct dcam_dev_aem_param));
		if (ret) {
			mutex_unlock(&p->param_lock);
			pr_err("fail to copy, ret=0x%x\n", (unsigned int)ret);
			return -EPERM;
		}
		mutex_unlock(&p->param_lock);
	}

	return ret;
}
