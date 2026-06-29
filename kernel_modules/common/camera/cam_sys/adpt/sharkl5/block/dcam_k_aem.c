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

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "AEM: %d %d %s : " fmt, current->pid, __LINE__, __func__

enum {
	_UPDATE_WIN = BIT(2),
};

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
	dev = param->dev;
	mode = param->aem.mode;
	hw_ctx = &dev->hw_ctx[idx];
	p_thr = &(param->aem.aem_info);

	DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_0, param->aem.bypass & BIT_0);
	if (param->aem.bypass)
		return 0;

	DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_2, mode << 2);
	if (mode == AEM_MODE_SINGLE)
		DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL1, BIT_0, 0x1);
	else
		DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_3, (0x1 << 3));

	if (hw_ctx->slowmotion_count) {
		/* on L5/L5P/L6 when slowmotion,AEM & BAYER_HIST & BIN skip with a special
		 * register and skip_num no need set or set 0 now
		 */
		pr_info("DCAM%u AEM ignore skip_num %u, slowmotion_count %u\n",
			hw_ctx->hw_ctx_id, param->aem.skip_num, hw_ctx->slowmotion_count);
		return 0;
	}
	pr_debug("dcam%d skip_num %d", idx, param->aem.skip_num);
	if (hw_ctx->fid == 0) {
		val = (param->aem.skip_num & 0xF) << 4;
		DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, 0xF0, val);
		/* It is better to set aem_skip_num_clr when new skip_num is set. */
		DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL1, BIT_1, 1 << 1);
		dcam_online_port_skip_num_set(param->dev, idx, DCAM_PATH_AEM, param->aem.skip_num);
	}
	val = ((p_thr->aem_r_thr.low_thr & 0x3FF) << 16) | (p_thr->aem_r_thr.high_thr & 0x3FF);
	DCAM_REG_WR(idx, DCAM_AEM_RED_THR, val);
	val = ((p_thr->aem_b_thr.low_thr & 0x3FF) << 16) | (p_thr->aem_b_thr.high_thr & 0x3FF);
	DCAM_REG_WR(idx, DCAM_AEM_BLUE_THR, val);
	val = ((p_thr->aem_g_thr.low_thr & 0x3FF) << 16) | (p_thr->aem_g_thr.high_thr & 0x3FF);
	DCAM_REG_WR(idx, DCAM_AEM_GREEN_THR, val);

	if (p_thr->aem_hdr_en) {
		val = ((p_thr->aem_short_r_thr.low_thr & 0x3FF) << 16) |
			(p_thr->aem_short_r_thr.high_thr & 0x3FF);
		DCAM_REG_WR(idx, DCAM_AEM_SHORT_RED_THR, val);
		val = ((p_thr->aem_short_b_thr.low_thr & 0x3FF) << 16) |
			(p_thr->aem_short_b_thr.high_thr & 0x3FF);
		DCAM_REG_WR(idx, DCAM_AEM_SHORT_BLUE_THR, val);
		val = ((p_thr->aem_short_g_thr.low_thr & 0x3FF) << 16) |
			(p_thr->aem_short_g_thr.high_thr & 0x3FF);
		DCAM_REG_WR(idx, DCAM_AEM_SHORT_GREEN_THR, val);
	}

	return ret;
}

int dcam_k_aem_win(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t idx = 0, val = 0;
	struct dcam_dev_aem_win *p = NULL; /* win_info; */

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

	pr_debug("dcam%d, UPDATE win %d %d %d %d.\n", idx,
		p->blk_num_x, p->blk_num_y, p->blk_width, p->blk_height);

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
