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
#define pr_fmt(fmt) "GTM: %d %d %s : " fmt, current->pid, __LINE__, __func__

int dcam_k_raw_gtm_slice(uint32_t idx, struct dcam_dev_gtm_slice_info *gtm_slice)
{
	int ret = 0;
	unsigned int val = 0;

	/* for slice */
	val =((gtm_slice->gtm_slice_main & 0x1) << 6);
	DCAM_REG_MWR(idx, DCAM_GTM_GLB_CTRL, 0x40, val);

	val = (gtm_slice->gtm_slice_line_startpos & 0x1FFF) |
		((gtm_slice->gtm_slice_line_endpos & 0x1FFF) << 16);
	DCAM_REG_WR(idx, GTM_SLICE_LINE_POS, val);

	return ret;
}

static void dcamkgtm_param_dump(struct dcam_dev_rgb_gtm_block_info *p,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;

	if (!debug || !p) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	log_ctx = &debug->block_param_log;

	if (!debug->block_param)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "GTM: ctx %llu fid %llu, update %d, mode_en %d, map %d, hist %d\n",
		debug->idx, debug->fid, p->update_flag,
		p->bypass_info.gtm_mod_en, p->bypass_info.gtm_map_bypass,
		p->bypass_info.gtm_hist_stat_bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "GTM: gtm_pre_ymin_weight %d\n",
		p->gtm_pre_ymin_weight);
	CAM_DEBUG_LOG_WRITE(log_ctx, "GTM: cur_ymin_weight %d diff_thr(%d %d) coeff %d %d %d\n",
		p->gtm_cur_ymin_weight, p->gtm_ymax_diff_thr, p->gtm_yavg_diff_thr,
		p->tm_rgb2y_g_coeff, p->tm_rgb2y_r_coeff,
		p->tm_rgb2y_b_coeff);
	CAM_DEBUG_LOG_WRITE(log_ctx, "GTM: gtm_min_per %d gtm_max_per %d\n",
		p->gtm_min_per, p->gtm_max_per);
	CAM_DEBUG_LOG_WRITE(log_ctx, "GTM: tm_hist_xpts %d %d %d %d %d %d %d %d %d\n",
		p->tm_hist_xpts[0], p->tm_hist_xpts[16], p->tm_hist_xpts[32], p->tm_hist_xpts[48],
		p->tm_hist_xpts[64], p->tm_hist_xpts[80], p->tm_hist_xpts[96],
		p->tm_hist_xpts[112], p->tm_hist_xpts[127]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_raw_gtm_block(struct dcam_isp_k_block *param)
{
	int ret = 0;
	uint32_t i = 0, val = 0;
	uint32_t idx = param->idx;
	uint32_t gtm_imgkey_set_mode = 0;
	uint32_t gtm_cur_is_first_frame = 1;
	uint32_t gtm_tm_param_calc_by_sw = 1;
	uint32_t gtm_tm_param_calc_by_hw = 1;
	uint32_t gtm_target_norm_setting_mode = 0;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct dcam_dev_rgb_gtm_param *gtm = NULL;
	struct dcam_dev_rgb_gtm_block_info *p = NULL;

	gtm = &param->rgb_gtm;
	dev = (struct dcam_pipe_dev *)param->dev;
	hw_ctx = &dev->hw_ctx[idx];
	p = &(gtm->rgb_gtm_info);

	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	pr_debug("dcam%d map_bypass %d stat_bypass %d\n",
		idx, p->bypass_info.gtm_map_bypass, p->bypass_info.gtm_hist_stat_bypass);

	if (hw_ctx) {
		if (g_dcam_block_dump & (1 << _E_GTM)) {
			hw_ctx->debug.idx = idx;
			hw_ctx->debug.fid = hw_ctx->fid;
			hw_ctx->debug.block_param = 1;
		}
		dcamkgtm_param_dump(p, &hw_ctx->debug);
	}

	if (g_dcam_bypass[idx] & (1 << _E_GTM)) {
		p->bypass_info.gtm_map_bypass = 1;
		p->bypass_info.gtm_hist_stat_bypass = 1;
	}

	if (p->bypass_info.gtm_map_bypass && p->bypass_info.gtm_hist_stat_bypass) {
		DCAM_REG_MWR(idx, DCAM_GTM_GLB_CTRL, 0x3, 0x3);
		return 0;
	}

	pr_debug("ctx %d, gtm hw_ymin %d, target_norm %d, lr_int %d\n",
		idx, p->gtm_ymin, p->gtm_target_norm, p->gtm_lr_int);
	pr_debug("ctx %d, gtm log_min_int %d, log_diff_int %d, log_diff %d, imgkey_setting_value %d\n",
		idx, p->gtm_log_min_int, p->gtm_log_diff_int, p->gtm_log_diff, p->gtm_imgkey_setting_value);
	val = ((p->bypass_info.gtm_map_bypass & 0x1)) |
		((p->bypass_info.gtm_hist_stat_bypass & 0x1) << 1) |
		((gtm_tm_param_calc_by_sw & 0x1) << 2) |
		((gtm_tm_param_calc_by_hw & 0x1) << 3) |
		((gtm_cur_is_first_frame & 0x1) << 4) |
		((p->gtm_rgb2y_mode & 0x1) << 5);
	DCAM_REG_MWR(idx, DCAM_GTM_GLB_CTRL, 0x3F, val);

	val = (gtm_imgkey_set_mode & 0x1) | ((p->gtm_imgkey_setting_value & 0x7FFF) << 4);
	DCAM_REG_MWR(idx, GTM_HIST_CTRL0, 0x7FFF1, val);

	val = (gtm_target_norm_setting_mode & 0x1)
		| ((p->gtm_target_norm & 0xFFF) << 4)
		| ((p->gtm_target_norm_coeff & 0x3FFF) << 16);
	DCAM_REG_MWR(idx, GTM_HIST_CTRL1, 0x3FFFFFF1, val);

	val = p->gtm_ymin & 0x3FFF;
	DCAM_REG_MWR(idx, GTM_HIST_YMIN, 0x3FFF, val);

	val = p->gtm_yavg_diff_thr & 0x3FFF;
	DCAM_REG_WR(idx, GTM_HIST_CTRL2, val);

	val = (p->gtm_lr_int & 0xFFFF) | ((p->gtm_log_min_int & 0xFFFF) << 16);
	DCAM_REG_WR(idx, GTM_HIST_CTRL3, val);

	val = ((p->gtm_log_diff_int & 0xFFFF) << 16);
	DCAM_REG_MWR(idx, GTM_HIST_CTRL4, 0xFFFF0000, val);

	p->gtm_hist_total = hw_ctx->cap_info.cap_size.size_x * hw_ctx->cap_info.cap_size.size_y;
	val = p->gtm_hist_total & 0x3FFFFFF;
	DCAM_REG_WR(idx, GTM_HIST_CTRL5, val);

	val = ((p->gtm_min_per * p->gtm_hist_total) >> 16) & 0xFFFFF;
	DCAM_REG_WR(idx, GTM_HIST_CTRL6, val);

	val = ((p->gtm_max_per * p->gtm_hist_total) >> 16) & 0xFFFFF;
	DCAM_REG_WR(idx, GTM_HIST_CTRL7, val);

	val = p->gtm_log_diff & 0x1FFFFFFF;
	DCAM_REG_WR(idx, GTM_LOG_DIFF, val);

	val = (p->gtm_ymax_diff_thr & 0x3FFF)
		| ((p->gtm_cur_ymin_weight & 0x1FF) << 14)
		| ((p->gtm_pre_ymin_weight & 0x1FF) << 23);
	DCAM_REG_WR(idx, GTM_TM_YMIN_SMOOTH, val);

	for (i = 0; i < 8; i++ ) {
		val = (p->tm_filter_dist_c[i * 6 + 0] & 0x1F)
			| ((p->tm_filter_dist_c[i * 6 + 1] & 0x1F) << 5)
			| ((p->tm_filter_dist_c[i * 6 + 2] & 0x1F) << 10)
			| ((p->tm_filter_dist_c[i * 6 + 3] & 0x1F) << 15)
			| ((p->tm_filter_dist_c[i * 6 + 4] & 0x1F) << 20)
			| ((p->tm_filter_dist_c[i * 6 + 5] & 0x1F) << 25);
		DCAM_REG_WR(idx, GTM_TM_FILTER_DIST0 + 4 * i, val);
	}

	DCAM_REG_WR(idx, GTM_TM_FILTER_DIST8, 0x1F & p->tm_filter_dist_c[48]);

	for (i = 0; i < 6; i++ ) {
		val = (p->tm_filter_distw_c[i * 3 + 0] & 0x1FF)
			| ((p->tm_filter_distw_c[i * 3 + 1] & 0x1FF) << 9)
			| ((p->tm_filter_distw_c[i * 3 + 2] & 0x1FF) << 18);
		DCAM_REG_WR(idx, GTM_TM_FILTER_DISTW0 + 4 * i, val);
	}

	DCAM_REG_WR(idx, GTM_TM_FILTER_DISTW6, 0x1FF & p->tm_filter_distw_c[18]);

	for (i = 0; i < 20; i++ ) {
		val = (p->tm_filter_rangw_c[i * 3 + 0] & 0x1FF)
			| ((p->tm_filter_rangw_c[i * 3 + 1] & 0x1FF) << 9)
			| ((p->tm_filter_rangw_c[i * 3 + 2] & 0x1FF) << 18);
		DCAM_REG_WR(idx, GTM_TM_FILTER_RANGEW0 + 4 * i, val);
	}

	DCAM_REG_WR(idx, GTM_TM_FILTER_RANGEW20, 0x1FF & p->tm_filter_rangw_c[60]);

	val = (p->tm_rgb2y_r_coeff & 0x7FF) | ((p->tm_rgb2y_g_coeff & 0x7FF) << 16);
	DCAM_REG_MWR(idx, GTM_TM_RGB2YCOEFF0, 0x7FF07FF, val);

	val = (p->tm_rgb2y_b_coeff & 0x7FF);
	DCAM_REG_MWR(idx, GTM_TM_RGB2YCOEFF1, 0x7FF, val);

	for (i = 0; i < GTM_HIST_XPTS_CNT / 2; i += 2) {
		val = ((p->tm_hist_xpts[i] & 0x3FFF) << 16) | (p->tm_hist_xpts[i + 1] & 0x3FFF);
		DCAM_REG_BWR(idx, GTM_HIST_XPTS_0 + i * 2, val);
	}

	/* for slice */
	dcam_k_raw_gtm_slice(idx, &p->slice);

	return ret;
}

int dcam_k_rgb_gtm_mapping(struct dcam_dev_rgb_gtm_block_info *p, uint32_t idx)
{
	uint32_t val = 0;

	if (!p) {
		pr_err("fail to input ptr NULL\n");
		return -1;
	}

	if (g_dcam_bypass[idx] & (1 << _EISP_GTM))
		return 0;

	if ((!p->gtm_ymin) && (!p->gtm_target_norm) && (!p->gtm_lr_int) && (!p->gtm_log_min_int) &&
		(!p->gtm_log_diff_int) && (!p->gtm_log_diff)) {
		pr_err("fail to get normal mapping param\n");
		DCAM_REG_MWR(idx, DCAM_GTM_GLB_CTRL, BIT_0, BIT_0);
		return -1;
	}

	val = ((p->gtm_ymin & 0x3FFF) << 0);
	DCAM_REG_MWR(idx, GTM_HIST_YMIN, 0x3FFF, val);

	val = ((p->gtm_target_norm & 0xFFF) << 4);
	DCAM_REG_MWR(idx, GTM_HIST_CTRL1, 0xFFF0, val);

	val = ((p->gtm_lr_int & 0xFFFF) << 0) |
		((p->gtm_log_min_int & 0xFFFF) << 16);
	DCAM_REG_MWR(idx, GTM_HIST_CTRL3, 0xFFFFFFFF, val);

	val = ((p->gtm_log_diff_int & 0xFFFF) << 16);
	DCAM_REG_MWR(idx,  GTM_HIST_CTRL4, 0xFFFF0000, val);

	val = ((p->gtm_log_diff & 0x1FFFFFFF) << 0);
	DCAM_REG_WR(idx, GTM_LOG_DIFF, val);

	DCAM_REG_MWR(idx, DCAM_GTM_GLB_CTRL, BIT_2, BIT_2);

	pr_debug("ctx %d, gtm hw_ymin %d, target_norm %d, lr_int %d\n",
		idx, p->gtm_ymin, p->gtm_target_norm, p->gtm_lr_int);
	pr_debug("ctx %d, gtm log_min_int %d, log_diff_int %d, log_diff %d\n",
		idx, p->gtm_log_min_int, p->gtm_log_diff_int, p->gtm_log_diff);

	return 0;
}

int dcam_k_gtm_bypass(struct dcam_isp_k_block *param, struct dcam_dev_raw_gtm_bypass *bypass_info)
{
	uint32_t val = 0;

	if ((!param) || (!bypass_info)) {
		pr_err("fail to input ptr %p %p\n", param, bypass_info);
		return -1;
	}

	if (g_dcam_bypass[param->idx] & (1 << _E_GTM)) {
		pr_info("gtm mapping disable, idx %d\n", param->idx);
		return 0;
	}

	val = ((bypass_info->gtm_hist_stat_bypass & 1) << 1) | (bypass_info->gtm_map_bypass & 1);
	DCAM_REG_MWR(param->idx, DCAM_GTM_GLB_CTRL, 0x3, val);
	pr_debug("dcam%d hitst bypass %d, map bypass %d\n",
		param->idx, bypass_info->gtm_hist_stat_bypass, bypass_info->gtm_map_bypass);
	return 0;
}

int dcam_k_cfg_raw_gtm(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	struct dcam_dev_rgb_gtm_block_info *gtm_block = NULL;
	struct dcam_dev_raw_gtm_bypass *gtm_bypass = NULL;

	switch (param->property) {
	case DCAM_PRO_GTM_BLOCK:
		gtm_block = &p->rgb_gtm.rgb_gtm_info;
		ret = copy_from_user((void *)(gtm_block), param->property_param, sizeof(struct dcam_dev_rgb_gtm_block_info));
		if (ret) {
			pr_err("fail to copy, ret=0x%x\n", (unsigned int)ret);
			return -EPERM;
		}
		pr_debug("dcam%d scene_id %d, mod %d hist %d map %d init_flag %d\n",
			p->idx, param->scene_id, gtm_block->bypass_info.gtm_mod_en,
			gtm_block->bypass_info.gtm_hist_stat_bypass, gtm_block->bypass_info.gtm_map_bypass, gtm_block->init_flag);
		/* if stream on, init_flag is 1, else, init_flag is 0 */
		if (gtm_block->init_flag) {
			if (p->offline == 0) {
				if (p->idx >= DCAM_HW_CONTEXT_MAX)
					return 0;
				dcam_k_raw_gtm_block(p);
			}
		} else {
			if (p->offline == 0) {
				gtm_bypass = &p->rgb_gtm.rgb_gtm_info.bypass_info;
				if (p->idx >= DCAM_HW_CONTEXT_MAX)
					return 0;
				dcam_k_gtm_bypass(p, gtm_bypass);
				dcam_k_rgb_gtm_mapping(gtm_block, p->idx);
			}
			pr_debug("get mapping info, scene_id %d, offline %d\n", param->scene_id, p->offline);
		}
		break;
	default:
		pr_err("fail to support cmd id:%d\n", param->property);
		break;
	}

	return ret;
}
