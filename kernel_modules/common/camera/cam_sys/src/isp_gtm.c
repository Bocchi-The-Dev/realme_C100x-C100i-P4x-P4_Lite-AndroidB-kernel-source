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
#include <linux/mutex.h>
#include <sprd_mm.h>

#include "isp_gtm.h"
#include "cam_buf_manager.h"

 #ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_GTM: %d %d %s : " fmt, current->pid, __LINE__, __func__

static int ispgtm_cfg_param(void *handle,
		enum isp_gtm_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct isp_gtm_ctx_desc *gtm_ctx = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	gtm_ctx = (struct isp_gtm_ctx_desc *)handle;

	switch (cmd) {
	case ISP_GTM_CFG_EB:
		gtm_ctx->enable = *(uint32_t *)param;
		pr_debug("GTM ctx_id %d, enable %d\n", gtm_ctx->ctx_id, gtm_ctx->enable);
		break;
	case ISP_GTM_CFG_MODE:
		gtm_ctx->mode = *(uint32_t *)param;
		pr_debug("GTM ctx_id %d, mode %d\n", gtm_ctx->ctx_id, gtm_ctx->mode);
		break;
	case ISP_GTM_CFG_FRAME_ID:
		gtm_ctx->fid = *(uint32_t *)param;
		pr_debug("GTM ctx_id %d, frame id %d\n", gtm_ctx->ctx_id, gtm_ctx->fid);
		break;
	case ISP_GTM_CFG_HIST_BYPASS:
		gtm_ctx->gtm_hist_stat_bypass = !(*(uint32_t *)param);
		pr_debug("GTM ctx_id %d, frame id %d, hist bypass %d\n", gtm_ctx->ctx_id, gtm_ctx->fid, gtm_ctx->gtm_hist_stat_bypass);
		break;
	case ISP_GTM_CFG_MAP_BYPASS:
		gtm_ctx->gtm_map_bypass = !(*(uint32_t *)param);
		pr_debug("GTM ctx_id %d, frame id %d, map bypass %d\n", gtm_ctx->ctx_id, gtm_ctx->fid, gtm_ctx->gtm_map_bypass);
		break;
	case ISP_GTM_CFG_MOD_EN:
		gtm_ctx->gtm_mode_en= *(uint32_t *)param;
		pr_debug("GTM ctx_id %d, frame id %d, mod_en %d\n", gtm_ctx->ctx_id, gtm_ctx->fid, gtm_ctx->gtm_mode_en);
		break;
	default:
		pr_debug("fail to get known cmd: %d\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static void ispgtm_histbuf_param_cfg(struct isp_gtm_ctx_desc *gtm_ctx, struct dcam_dev_raw_gtm_block_info *gtm_param)
{
	int ret = 0;
	struct cam_frame *frame = NULL;
	uint32_t mod_en = 0, hist_bypass = 0;

	hist_bypass = gtm_ctx->gtm_hist_stat_bypass || gtm_param->bypass_info.gtm_hist_stat_bypass;
	mod_en = gtm_ctx->gtm_mode_en || gtm_param->bypass_info.gtm_mod_en;

	if (mod_en && !hist_bypass) {
		frame = cam_buf_manager_buf_dequeue(gtm_ctx->outpool, NULL, gtm_ctx->buf_manager_handle);
		if (frame) {
			frame->common.fid = gtm_ctx->fid;
			ret = cam_buf_manager_buf_enqueue(gtm_ctx->resultpool, frame, NULL, gtm_ctx->buf_manager_handle);
			if (ret)
				pr_err("fail to enqueue gtm frame\n");
		} else
			pr_debug("There is no frame in gtm outpool:%d.\n",
				gtm_ctx->outpool->private_pool_id);
	} else
		pr_debug("gtm mod disable or hist bypass,needn't sethist buf, mod_en:%d, hist bypass:%d.\n",mod_en, hist_bypass);
}

static int ispgtm_pipe_proc(void *handle, void *param)
{
	int ret = 0;
	uint32_t idx = 0;
	struct isp_hw_gtm_func gtm_func = {0};
	struct isp_gtm_k_block gtm_k_block ={0};
	struct isp_gtm_ctx_desc *gtm_ctx = NULL;
	struct dcam_isp_k_block *isp_using_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input handle:%p, param:%p\n", handle, param);
		return -EFAULT;
	}

	gtm_ctx = (struct isp_gtm_ctx_desc *)handle;
	isp_using_param = (struct dcam_isp_k_block *)param;

	gtm_k_block.ctx = gtm_ctx;
	gtm_k_block.tuning = &isp_using_param->gtm_rgb_info;
	gtm_k_block.map = &isp_using_param->gtm_sw_map_info;
	switch (gtm_ctx->mode) {
	case MODE_GTM_PRE:
		gtm_func.index = ISP_K_GTM_BLOCK_SET;
		gtm_ctx->hw->isp_ioctl(gtm_ctx->hw, ISP_HW_CFG_GTM_FUNC_GET, &gtm_func);
		gtm_func.k_blk_func(&gtm_k_block);

		if (gtm_k_block.tuning->bypass_info.gtm_mod_en) {
			gtm_func.index = ISP_K_GTM_MAPPING_SET;
			gtm_ctx->hw->isp_ioctl(gtm_ctx->hw, ISP_HW_CFG_GTM_FUNC_GET, &gtm_func);
			gtm_func.k_blk_func(&gtm_k_block);
		}

		ispgtm_histbuf_param_cfg(gtm_ctx, &isp_using_param->gtm_rgb_info);
		gtm_func.index = ISP_K_GTM_BYPASS_SET;
		gtm_ctx->hw->isp_ioctl(gtm_ctx->hw, ISP_HW_CFG_GTM_FUNC_GET, &gtm_func);
		gtm_func.k_blk_func(&gtm_k_block);
		break;
	case MODE_GTM_CAP:
		gtm_k_block.tuning->bypass_info.gtm_hist_stat_bypass = 1;
		if (gtm_k_block.tuning->bypass_info.gtm_mod_en == 0) {
			gtm_func.index = ISP_K_GTM_BYPASS_SET;
			gtm_ctx->hw->isp_ioctl(gtm_ctx->hw, ISP_HW_CFG_GTM_FUNC_GET, &gtm_func);
			gtm_func.k_blk_func(&gtm_k_block);
			goto exit;
		}

		ispgtm_histbuf_param_cfg(gtm_ctx, &isp_using_param->gtm_rgb_info);
		gtm_func.index = ISP_K_GTM_BLOCK_SET;
		gtm_ctx->hw->isp_ioctl(gtm_ctx->hw, ISP_HW_CFG_GTM_FUNC_GET, &gtm_func);
		gtm_func.k_blk_func(&gtm_k_block);

		gtm_func.index = ISP_K_GTM_MAPPING_SET;
		gtm_ctx->hw->isp_ioctl(gtm_ctx->hw, ISP_HW_CFG_GTM_FUNC_GET, &gtm_func);
		gtm_func.k_blk_func(&gtm_k_block);

		gtm_func.index = ISP_K_GTM_BYPASS_SET;
		gtm_ctx->hw->isp_ioctl(gtm_ctx->hw, ISP_HW_CFG_GTM_FUNC_GET, &gtm_func);
		gtm_func.k_blk_func(&gtm_k_block);

		idx = gtm_ctx->ctx_id;
		gtm_func.index = ISP_K_GTM_STATUS_GET;
		gtm_ctx->hw->isp_ioctl(gtm_ctx->hw, ISP_HW_CFG_GTM_FUNC_GET, &gtm_func);
		gtm_ctx->gtm_mode_en = gtm_func.k_blk_func(&idx) & gtm_k_block.tuning->bypass_info.gtm_mod_en;
		break;
	case MODE_GTM_OFF:
		pr_debug("ctx_id %d, GTM off\n", gtm_ctx->ctx_id);
		break;
	default:
		pr_debug("waring , ctx_id %d, GTM need to check, mode %d\n", gtm_ctx->ctx_id, gtm_ctx->mode);
		break;
	 }
exit:
	pr_debug("ctx %d, fid %d, do preview mapping\n", gtm_ctx->ctx_id, gtm_ctx->fid);
	return ret;
}

static struct isp_gtm_ctx_desc *ispgtm_ctx_init(uint32_t idx, void *hw)
{
	struct isp_gtm_ctx_desc *gtm_ctx = NULL;

	gtm_ctx = cam_buf_kernel_sys_vzalloc(sizeof(struct isp_gtm_ctx_desc));
	if (!gtm_ctx) {
		pr_err("fail to alloc isp %d gtm ctx\n", idx);
		return NULL;
	}

	gtm_ctx->ctx_id = idx;
	gtm_ctx->hw = hw;
	atomic_set(&gtm_ctx->cnt, 0);
	gtm_ctx->gtm_ops.cfg_param = ispgtm_cfg_param;
	gtm_ctx->gtm_ops.pipe_proc = ispgtm_pipe_proc;

	return gtm_ctx;
}

void isp_gtm_rgb_ctx_put(void *gtm_handle)
{
	struct isp_gtm_ctx_desc *gtm_ctx = NULL;

	if (!gtm_handle) {
		pr_err("fail to get valid gtm_handle\n");
		return;
	}

	gtm_ctx = (struct isp_gtm_ctx_desc *)gtm_handle;

	if (gtm_ctx)
		cam_buf_kernel_sys_vfree(gtm_ctx);

	gtm_ctx = NULL;
}

void *ispgtm_rgb_ctx_get(uint32_t idx, void *hw)
{
	struct isp_gtm_ctx_desc *gtm_ctx = NULL;

	gtm_ctx = ispgtm_ctx_init(idx, hw);
	if (!gtm_ctx) {
		pr_err("fail to get invalid ltm_ctx\n");
		return NULL;
	}

	return gtm_ctx;
}
