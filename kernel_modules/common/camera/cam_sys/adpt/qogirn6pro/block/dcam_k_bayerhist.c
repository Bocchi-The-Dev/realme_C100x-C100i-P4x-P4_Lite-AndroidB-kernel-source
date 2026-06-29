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
#define pr_fmt(fmt) "BAYER_HIST: %d %d %s : " fmt, current->pid, __LINE__, __func__

enum {
	_UPDATE_ROI = BIT(0),
};

static void dcam_k_bayerhist_param_dump(struct dcam_dev_hist_info *p,
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

	CAM_DEBUG_LOG_WRITE(log_ctx, "bayerhist: bypass %d update %d\n",
		p->hist_bypass, p->update_flag);
	CAM_DEBUG_LOG_WRITE(log_ctx, "skip_num %d mul_enable %d mode_sel %d\n",
		p->hist_skip_num, p->hist_mul_enable, p->hist_mode_sel);
	CAM_DEBUG_LOG_WRITE(log_ctx, "st_x %d st_y %d end_x %d end_y %d\n",
		p->bayer_hist_stx, p->bayer_hist_sty, p->bayer_hist_endx, p->bayer_hist_endy);
	CAM_DEBUG_LOG_WRITE(log_ctx, "initial_clear %d skip_num_clr %d sgl_start %d\n",
		p->hist_initial_clear, p->hist_skip_num_clr, p->hist_sgl_start);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_bayerhist_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	struct dcam_dev_hist_info *p = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (param == NULL)
		return -1;

	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;
	p = &(param->hist.bayerHist_info);
	dev = param->dev;
	if (dev == NULL) {
		pr_err("fail to get dev\n");
		return -1;
	}

	hw_ctx = &dev->hw_ctx[idx];
	if (hw_ctx == NULL) {
		pr_err("fail to get hw_ctx\n");
		return -1;
	}
	if (g_dcam_block_dump & (1 << _E_HIST)) {
		hw_ctx->debug.idx = idx;
		hw_ctx->debug.fid = hw_ctx->fid;
		hw_ctx->debug.block_param = 1;
	}
	dcam_k_bayerhist_param_dump(&param->hist.bayerHist_info, &hw_ctx->debug);

	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_CTRL0, BIT_0, p->hist_bypass);
	if (p->hist_bypass)
		return 0;

	if (hw_ctx->slowmotion_count) {
		pr_debug("DCAM%u HIST ignore skip_num %u, slowmotion_count %u\n",
			hw_ctx->hw_ctx_id, p->hist_skip_num, hw_ctx->slowmotion_count);
		p->hist_skip_num = hw_ctx->slowmotion_count - 1;
	}

	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_CTRL0, 0xff6,
			((p->hist_skip_num & 0xff) << 4) |
			(p->hist_mul_enable << 2) |
			(p->hist_mode_sel << 1));

	dcam_online_port_skip_num_set(param->dev, idx, DCAM_PATH_HIST, p->hist_skip_num);
	pr_debug("skip num %d\n", p->hist_skip_num);

	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_START, 0xffffffff,
			((p->bayer_hist_sty & 0x1fff) << 16) |
			(p->bayer_hist_stx & 0x1fff));

	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_END, 0xffffffff,
			((p->bayer_hist_endy & 0x1fff) << 16) |
			(p->bayer_hist_endx & 0x1fff));

	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_CTRL1, 0x3,
			(p->hist_skip_num_clr << 1) |
			(p->hist_sgl_start << 0));
	return ret;
}

int dcam_k_bayerhist_roi(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0;
	struct dcam_dev_hist_info *p = NULL;

	if (param == NULL)
		return -1;
	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	p = &(param->hist.bayerHist_info);
	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_CTRL0, BIT_0, p->hist_bypass);
	if (p->hist_bypass)
		return 0;

	pr_debug("dcam%d, roi (%d %d %d %d)\n", idx,
		p->bayer_hist_stx, p->bayer_hist_sty,
		p->bayer_hist_endx, p->bayer_hist_endy);

	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_START, 0xffffffff,
			((p->bayer_hist_sty & 0x1fff) << 16) |
			(p->bayer_hist_stx & 0x1fff));

	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_END, 0xffffffff,
			((p->bayer_hist_endy & 0x1fff) << 16) |
			(p->bayer_hist_endx & 0x1fff));

	return ret;
}

int dcam_k_cfg_bayerhist(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	unsigned long flags = 0;
	struct dcam_dev_hist_info hist = {0};

	ret = copy_from_user((void *)&hist,
		param->property_param, sizeof(struct dcam_dev_hist_info));
	if (ret) {
		pr_err("fail to copy from user, ret=0x%x\n", (unsigned int)ret);
		return -EPERM;
	}

	spin_lock_irqsave(&p->hist_update_lock, flags);
	memcpy(&p->hist.bayerHist_info, &hist, sizeof(struct dcam_dev_hist_info));
	spin_unlock_irqrestore(&p->hist_update_lock, flags);

	if (p->idx == DCAM_HW_CONTEXT_MAX)
		return 0;
	ret = dcam_k_bayerhist_block(p);

	pr_debug("dcam%d re_config hist %d, win (%d %d %d %d)\n",
			p->idx, p->hist.bayerHist_info.hist_bypass,
			p->hist.bayerHist_info.bayer_hist_stx,
			p->hist.bayerHist_info.bayer_hist_sty,
			p->hist.bayerHist_info.bayer_hist_endx,
			p->hist.bayerHist_info.bayer_hist_endy);

	return ret;
}
