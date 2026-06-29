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
#include "dcam_core.h"
#include "dcam_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "LSCM: %d %d %s : " fmt, current->pid, __LINE__, __func__

int dcam_k_lscm_monitor(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = param->idx;
	uint32_t mode = 0;
	uint32_t val = 0;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	dev = param->dev;
	hw_ctx = &dev->hw_ctx[idx];

	DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL0, BIT_0, 0);

	mode = param->lscm.mode;
	DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL0, BIT_2, mode << 2);

	/* mode: 0 - single; 1 - multi mode */
	if (mode == 0)
		/* trigger lscm_sgl_start. */
		DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL1, BIT_0, 0x1);
	else
		/* trigger multi frame works after skip_num */
		DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL0, BIT_3, (0x1 << 3));

	if (hw_ctx->slowmotion_count)
		param->lscm.skip_num = hw_ctx->slowmotion_count - 1;

	val = (param->lscm.skip_num & 0xF) << 4;
	DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL0, 0xF0, val);

	/* It is better to set lscm_skip_num_clr when new skip_num is set. */
	DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL1, BIT_1, 1 << 1);
	dcam_online_port_skip_num_set(param->dev, idx, DCAM_PATH_LSCM, param->lscm.skip_num);

	val = ((param->lscm.offset_y & 0x1FFF) << 16) |
			(param->lscm.offset_x & 0x1FFF);
	DCAM_REG_WR(idx, DCAM_LSCM_OFFSET, val);

	val = ((param->lscm.blk_height & 0xFF) << 8) |
			(param->lscm.blk_width & 0xFF);
	DCAM_REG_WR(idx, DCAM_LSCM_BLK_SIZE, val);

	val = ((param->lscm.blk_num_y & 0xFF) << 8) |
			(param->lscm.blk_num_x & 0xFF);
	DCAM_REG_WR(idx, DCAM_LSCM_BLK_NUM, val);

	return ret;
}

int dcam_k_cfg_lscm(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	void *pcpy;

	pcpy = (void *)&(p->lscm);
	if (p->offline == 0) {
		ret = copy_from_user(pcpy, param->property_param,
			sizeof(struct dcam_dev_lscm_param));
		if (ret) {
			pr_err("fail to copy, ret=0x%x\n", (unsigned int)ret);
			return -EPERM;
		}
		if (p->idx >= DCAM_HW_CONTEXT_MAX)
			return 0;
		ret = dcam_k_lscm_monitor(p);
	} else {
		mutex_lock(&p->param_lock);
		ret = copy_from_user(pcpy, param->property_param,
			sizeof(struct dcam_dev_lscm_param));
		if (ret) {
			mutex_unlock(&p->param_lock);
			pr_err("fail to copy, ret=0x%x\n", (unsigned int)ret);
			return -EPERM;
		}
		mutex_unlock(&p->param_lock);
	}

	return ret;
}
