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
#include "dcam_core.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "FRGB_HIST: %d %d %s : " fmt, current->pid, __LINE__, __func__

enum {
	_UPDATE_ROI = BIT(0),
};

static void dcamkfrgbhist_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_hist2_info *p = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	p = &isp_k_param->hist_roi.hist_roi_info;
	log_ctx = &debug->block_param_log;

	if (!debug->block_param)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "FRGB: ctx%llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, p->update_flag, p->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "FRGB skip_num %d, frgb hist roi (%d %d %d %d)\n",
		p->skip_num, p->hist_roi.start_x, p->hist_roi.start_y,
		p->hist_roi.end_x, p->hist_roi.end_y);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_frgbhist_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0, val = 0;
	struct isp_dev_hist2_info *p = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (param == NULL)
		return -1;

	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;
	p = &(param->hist_roi.hist_roi_info);

	pr_debug("dcam%d, frgb hist roi bypass:%d (%d %d %d %d)\n", idx,
		p->bypass, p->hist_roi.start_x, p->hist_roi.start_y,
		p->hist_roi.end_x, p->hist_roi.end_y);

	DCAM_REG_MWR(idx, DCAM_HIST_ROI_CTRL0, BIT_0, p->bypass);
	if (p->bypass)
		return 0;

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
	if (hw_ctx && p->skip_num > 0 && hw_ctx->slowmotion_count) {
		pr_debug("DCAM%u frgb hist ignore skip_num %u, slowmotion_count %u\n",
			hw_ctx->hw_ctx_id, p->skip_num, hw_ctx->slowmotion_count);
		p->skip_num = 0;
	}

	if (g_dcam_block_dump & (1 << _E_HIST)) {
		hw_ctx->debug.idx = idx;
		hw_ctx->debug.fid = hw_ctx->fid;
		hw_ctx->debug.block_param = 1;
	}
	dcamkfrgbhist_param_dump(param, &hw_ctx->debug);

	val = (0 << 12) | ((p->skip_num & 0xff) << 4) | (1 << 2) | ((p->mode & 1) << 1);
	DCAM_REG_MWR(idx, DCAM_HIST_ROI_CTRL0, 0x1ffe, val);

	DCAM_REG_MWR(idx, DCAM_HIST_ROI_CTRL1, BIT_1, 1);

	dcam_online_port_skip_num_set(param->dev, idx, DCAM_PATH_FRGB_HIST, p->skip_num);
	pr_debug("skip num %d\n", p->skip_num);

	DCAM_REG_MWR(idx, DCAM_HIST_ROI_START, 0x1fff1fff,
			((p->hist_roi.start_y & 0x1fff) << 16) |
			(p->hist_roi.start_x & 0x1fff));

	p->hist_roi.end_y &= ~1;
	p->hist_roi.end_x &= ~1;

	DCAM_REG_MWR(idx, DCAM_HIST_ROI_END, 0x1fff1fff,
			((p->hist_roi.end_y & 0x1fff) << 16) |
			(p->hist_roi.end_x & 0x1fff));

	return ret;

}

int dcam_k_cfg_frgbhist(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	struct isp_dev_hist2_info cur;

	switch (param->property) {
	case ISP_PRO_HIST2_BLOCK:
		ret = copy_from_user((void *)&cur,
				param->property_param,
				sizeof(struct isp_dev_hist2_info));
		if (ret) {
			pr_err("fail to copy from user, ret=0x%x\n",
				(unsigned int)ret);
			return -EPERM;
		}

		p->hist_roi.hist_roi_info = cur;
		p->hist_roi.update |= _UPDATE_ROI;
		pr_debug("dcam%d, w:%d h:%d\n", p->idx, cur.hist_roi.end_x ,cur.hist_roi.end_y);

		if (p->idx == DCAM_HW_CONTEXT_MAX)
			return 0;

		ret = dcam_k_frgbhist_block(p);
		if (ret)
			pr_err("fail to config frgb hist reg\n");
		pr_debug("dcam%d config frgb hist bypass %d, win (%d %d %d %d)\n",
			p->idx, cur.bypass,
			cur.hist_roi.start_x, cur.hist_roi.start_y,
			cur.hist_roi.end_x, cur.hist_roi.end_y);
		break;
	default:
		pr_err("fail to support property %d\n",
			param->property);
		ret = -EINVAL;
		break;
	}

	return ret;
}

