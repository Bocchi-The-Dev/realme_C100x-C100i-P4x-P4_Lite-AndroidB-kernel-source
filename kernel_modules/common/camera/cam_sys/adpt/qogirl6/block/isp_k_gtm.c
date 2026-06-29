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
#include "isp_gtm.h"
#include "isp_reg.h"
#include "cam_debugger.h"
#include "isp_dev.h"
#include "isp_hwctx.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "GTM: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkgtm_param_dump(struct dcam_dev_raw_gtm_block_info *p,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;

	if (!debug || !p) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	log_ctx = &debug->block_param_log;

	if (!log_ctx)
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
}

int isp_k_gtm_mapping_set(void *param)
{
	uint32_t val = 0, idx = 0;
	uint32_t gtm_imgkey_set_mode = 0;
	uint32_t gtm_cur_is_first_frame = 1;
	uint32_t gtm_tm_param_calc_by_hw = 0;
	uint32_t gtm_target_norm_setting_mode = 0;
	struct isp_gtm_k_block *gtm_blk = NULL;

	if (!param) {
		pr_err("fail to get input ptr \n");
		return -1;
	}

	gtm_blk = (struct isp_gtm_k_block *)param;
	idx = gtm_blk->ctx->ctx_id;

	ISP_REG_MWR(idx, ISP_GTM_HIST_CTRL0, BIT_0, gtm_imgkey_set_mode);
	ISP_REG_MWR(idx, ISP_GTM_HIST_CTRL1, BIT_0, gtm_target_norm_setting_mode);
	ISP_REG_MWR(idx, ISP_GTM_GLB_CTRL, BIT_3, gtm_tm_param_calc_by_hw);
	ISP_REG_MWR(idx, ISP_GTM_GLB_CTRL, BIT_4, gtm_cur_is_first_frame << 4);

	if (g_isp_bypass[idx] & (1 << _EISP_GTM))
		return 0;

	if ((!gtm_blk->map->ymin) && (!gtm_blk->map->target) && (!gtm_blk->map->lr_int)
		&& (!gtm_blk->map->log_min_int) && (!gtm_blk->map->log_diff_int) && (!gtm_blk->map->diff)) {
		pr_err("fail to get normal gtm mapping param\n");
		ISP_REG_MWR(idx, ISP_GTM_GLB_CTRL, BIT_1, BIT_1);
		return -1;
	}
	val = ((gtm_blk->map->ymin & 0xFF) << 0);
	ISP_REG_MWR(idx, ISP_GTM_HIST_YMIN, 0xFF, val);

	val = ((gtm_blk->map->target & 0x3FFF) << 2);
	ISP_REG_MWR(idx, ISP_GTM_HIST_CTRL1, 0x3FFF << 2, val);

	val = ((gtm_blk->map->lr_int & 0xFFFF) << 0) |
		((gtm_blk->map->log_min_int & 0xFFFF) << 16);
	ISP_REG_MWR(idx, ISP_GTM_HIST_CTRL3, 0xFFFFFFFF, val);

	val = ((gtm_blk->map->log_diff_int & 0xFFFF) << 16);
	ISP_REG_MWR(idx,  ISP_GTM_HIST_CTRL4, 0xFFFF0000, val);

	val = ((gtm_blk->map->diff & 0x1FFFFFFF) << 0);
	ISP_REG_WR(idx, ISP_GTM_LOG_DIFF, val);

	pr_debug("ctx %d, sw mode %d, gtm hw_ymin %d, target_norm %d, lr_int %d\n",
		idx, gtm_blk->tuning->gtm_tm_param_calc_by_hw, gtm_blk->map->ymin, gtm_blk->map->target, gtm_blk->map->lr_int);
	pr_debug("ctx %d, gtm log_min_int %d, log_diff_int %d, log_diff %d\n",
		idx, gtm_blk->map->log_min_int, gtm_blk->map->log_diff_int, gtm_blk->map->diff);

	return 0;
}

int isp_k_gtm_block(void *pctx, void *param)
{
	int ret = 0;
	uint32_t idx = 0, i = 0, val = 0;
	uint32_t gtm_imgkey_set_mode = 0;
	uint32_t gtm_cur_is_first_frame = 1;
	uint32_t gtm_tm_param_calc_by_hw = 0;
	uint32_t gtm_target_norm_setting_mode = 0;
	struct isp_gtm_ctx_desc *ctx = NULL;
	struct dcam_dev_raw_gtm_block_info *p = NULL;
	struct dcam_dev_gtm_slice_info *gtm_slice = NULL;

	if (!pctx || !param) {
		pr_err("fail to get input ptr ctx %p, param %p\n", pctx,  param);
		return -1;
	}

	p = (struct dcam_dev_raw_gtm_block_info *)param;
	gtm_slice = &p->slice;

	ctx = (struct isp_gtm_ctx_desc *)pctx;
	idx = ctx->ctx_id;

	if (g_isp_bypass[idx] & (1 << _EISP_GTM)) {
		pr_debug("ctx_id %d, g_isp_bypass GTM\n", idx);
		p->bypass_info.gtm_mod_en = 0;
	}

	ISP_REG_MWR(idx, ISP_GTM_GLB_CTRL, BIT_0, (p->bypass_info.gtm_mod_en & 0x1));
	if (p->bypass_info.gtm_mod_en == 0) {
		pr_debug("ctx_id %d, gtm mod_en disable\n", idx);
		ISP_REG_MWR(idx, ISP_GTM_GLB_CTRL, BIT_2 | BIT_1, 3 << 1);
		return 0;
	}

	pr_debug("ctx_id %d, mod_en %d, map %d, hist_stat %d.\n",
		idx, p->bypass_info.gtm_mod_en, p->bypass_info.gtm_map_bypass, p->bypass_info.gtm_hist_stat_bypass);

	val = ((p->bypass_info.gtm_map_bypass & 0x1) << 1)
		| ((p->bypass_info.gtm_hist_stat_bypass & 0x1) << 2)
		| ((gtm_tm_param_calc_by_hw & 0x1) << 3)
		| ((gtm_cur_is_first_frame & 0x1) << 4)
		| ((p->gtm_tm_luma_est_mode & 0x3) << 5)
		| ((0 & 0x1) << 21)/*last slice*/
		| ((0 & 0x1) << 22)/*first slice*/
		| ((0 & 0x1) << 23)/*stat slice en*/
		| ((p->gtm_tm_in_bit_depth & 0xF) << 24)
		| ((p->gtm_tm_out_bit_depth & 0xF) << 28);
	ISP_REG_MWR(idx, ISP_GTM_GLB_CTRL, 0xFFE0007E, val);
	pr_debug("ctx_id %d, gtm_hist_total w %d, h %d\n", idx, ctx->src.w, ctx->src.h);
	val = (gtm_imgkey_set_mode & 0x1)
		| ((p->gtm_imgkey_setting_value & 0x7FFF) << 4);
	ISP_REG_MWR(idx, ISP_GTM_HIST_CTRL0, 0x7FFF1, val);

	val = (gtm_target_norm_setting_mode & 0x1)
		| ((p->gtm_target_norm & 0x3FFF) << 2)
		| ((p->gtm_target_norm_coeff & 0x3FFF) << 16);
	ISP_REG_MWR(idx, ISP_GTM_HIST_CTRL1, 0x3FFFFFFD, val);

	val = p->gtm_yavg_diff_thr & 0x3FFF;
	ISP_REG_WR(idx, ISP_GTM_HIST_CTRL2, val);

	val = ((p->gtm_lr_int & 0xFFFF) << 0) |
		((p->gtm_log_min_int & 0xFFFF) << 16);
	ISP_REG_MWR(idx, ISP_GTM_HIST_CTRL3, 0xFFFFFFFF, val);

	val = ((p->gtm_log_diff_int & 0xFFFF) << 16);
	ISP_REG_MWR(idx,  ISP_GTM_HIST_CTRL4, 0xFFFFFFFF, val);

	p->gtm_hist_total = ctx->src.w * ctx->src.h;
	val = ((p->gtm_hist_total & 0x3FFFFFF) << 0);
	ISP_REG_WR(idx,  ISP_GTM_HIST_CTRL5, val);

	val = ((p->gtm_min_per * p->gtm_hist_total) >> 16) & 0xFFFFF;
	ISP_REG_WR(idx, ISP_GTM_HIST_CTRL6, val);

	val = ((p->gtm_max_per * p->gtm_hist_total) >> 16) & 0xFFFFF;
	ISP_REG_WR(idx, ISP_GTM_HIST_CTRL7, val);

	val = ((p->gtm_log_diff & 0x1FFFFFFF) << 0);
	ISP_REG_WR(idx, ISP_GTM_LOG_DIFF, val);

	val = (p->gtm_ymax_diff_thr & 0x3FFF)
		| ((p->gtm_cur_ymin_weight & 0x1FF) << 14)
		| ((p->gtm_pre_ymin_weight & 0x1FF) << 23);
	ISP_REG_WR(idx, ISP_GTM_TM_YMIN_SMOOTH, val);

	val = (p->tm_lumafilter_c[0][0] & 0xFF)
		| ((p->tm_lumafilter_c[0][1] & 0xFF) << 8)
		| ((p->tm_lumafilter_c[0][2] & 0xFF) << 16)
		| ((p->tm_lumafilter_c[1][0] & 0xFF) << 24);
	ISP_REG_WR(idx, ISP_GTM_TM_LUMAFILTER0, val);

	val = (p->tm_lumafilter_c[1][1] & 0xFF)
		| ((p->tm_lumafilter_c[1][2] & 0xFF) << 8)
		| ((p->tm_lumafilter_c[2][0] & 0xFF) << 16)
		| ((p->tm_lumafilter_c[2][1] & 0xFF) << 24);
	ISP_REG_WR(idx, ISP_GTM_TM_LUMAFILTER1, val);

	val = (p->tm_lumafilter_c[2][2] & 0xFF)
		| ((p->tm_lumafilter_shift & 0xF) << 28);
	ISP_REG_MWR(idx, ISP_GTM_TM_LUMAFILTER2, 0xF00000FF, val);

	val = ((p->tm_rgb2y_g_coeff & 0x7FF) << 16)|
		((p->tm_rgb2y_r_coeff & 0x7FF) << 0);
	ISP_REG_MWR(idx, ISP_GTM_TM_RGB2TCOEFF0, 0x7FF07FF, val);

	val = ((p->tm_rgb2y_b_coeff & 0x7FF) << 0);
	ISP_REG_MWR(idx, ISP_GTM_TM_RGB2TCOEFF1, 0x7FF, val);

	for (i = 0; i < GTM_HIST_XPTS_CNT / 2; i += 2) {
		val = ((p->tm_hist_xpts[i] & 0x3FFF) << 16) | (p->tm_hist_xpts[i + 1] & 0x3FFF);
		ISP_REG_WR(idx, ISP_GTM_HIST_XPTS_0 + i * 2, val);
	}

	return ret;
}

int isp_k_rgb_gtm_bypass(void *param)
{
	uint32_t idx = 0, val = 0, mod_en = 0;
	uint32_t map_bypass = 0, hist_bypass = 0;
	struct isp_gtm_k_block *gtm_blk = NULL;

	if (!param) {
		pr_err("fail to input ptr NULL\n");
		return -1;
	}

	gtm_blk = (struct isp_gtm_k_block *)param;
	idx = gtm_blk->ctx->ctx_id;
	if (g_isp_bypass[idx] & (1 << _E_GTM)) {
		pr_debug("gtm mapping disable, idx %d\n", idx);
		return 0;
	}

	hist_bypass = gtm_blk->ctx->gtm_hist_stat_bypass || gtm_blk->tuning->bypass_info.gtm_hist_stat_bypass;
	map_bypass = gtm_blk->ctx->gtm_map_bypass || gtm_blk->tuning->bypass_info.gtm_map_bypass;
	mod_en = gtm_blk->ctx->gtm_mode_en || gtm_blk->tuning->bypass_info.gtm_mod_en;

	val = ((hist_bypass & 1) << 2) | ((map_bypass & 1) << 1) | (mod_en & 1);
	ISP_REG_MWR(idx, ISP_GTM_GLB_CTRL, 0x7, val);
	pr_debug("isp%d mod en %d, hitst bypass %d, map bypass %d\n",
		idx, mod_en, hist_bypass, map_bypass);
	return 0;
}

int isp_k_cfg_rgb_gtm(struct isp_io_param *param,
		struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct dcam_dev_raw_gtm_block_info *gtm = NULL;
	struct cam_gtm_mapping *gtm_alg_calc = NULL;
	struct cam_debug_cfg *debug = NULL;

	switch (param->property) {
	case DCAM_PRO_GTM_BLOCK:
		gtm = &isp_k_param->gtm_rgb_info;
		ret = copy_from_user((void *)gtm, param->property_param, sizeof(struct dcam_dev_raw_gtm_block_info));
		if (ret) {
			pr_err("fail to copy, ret=0x%x\n", (unsigned int)ret);
			return -EPERM;
		}
		pr_debug("ctx_id%d, scene_id %d, mod_en %d, hist %d map %d, init_flag %d\n",
			isp_k_param->cfg_id, param->scene_id, gtm->bypass_info.gtm_mod_en, gtm->bypass_info.gtm_hist_stat_bypass, gtm->bypass_info.gtm_map_bypass, gtm->init_flag);

		debug = &isp_k_param->debug;

		if (g_isp_block_dump & (1 << _EISP_GTM))
			ispkgtm_param_dump(gtm, debug);

		/* if stream on, init_flag is 1, else, init_flag is 0 */
		if (gtm->init_flag) {
			if (param->scene_id == PM_SCENE_CAP)
				gtm->bypass_info.gtm_hist_stat_bypass = 1;
			isp_k_param->gtm_rgb_info.isupdate = 1;
		}
		gtm_alg_calc = &isp_k_param->gtm_sw_map_info;
		gtm_alg_calc->idx = isp_k_param->cfg_id;
		gtm_alg_calc->ymin = gtm->gtm_ymin;
		gtm_alg_calc->ymax = gtm->gtm_ymax;
		gtm_alg_calc->yavg = gtm->gtm_yavg;
		gtm_alg_calc->target = gtm->gtm_target_norm;
		gtm_alg_calc->lr_int = gtm->gtm_lr_int;
		gtm_alg_calc->log_min_int = gtm->gtm_log_min_int;
		gtm_alg_calc->log_diff_int = gtm->gtm_log_diff_int;
		gtm_alg_calc->diff = gtm->gtm_log_diff;
		isp_k_param->gtm_sw_map_info.isupdate = 1;
		break;
	default:
		pr_err("fail to support cmd id:%d\n", param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_rgb_gtm(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;

	if (isp_k_param->gtm_rgb_info.isupdate == 1) {
		memcpy(&param_block->gtm_rgb_info, &isp_k_param->gtm_rgb_info, sizeof(struct dcam_dev_raw_gtm_block_info));
		isp_k_param->gtm_rgb_info.isupdate = 0;
		param_block->gtm_rgb_info.isupdate = 1;
	}

	if (isp_k_param->gtm_sw_map_info.isupdate == 1) {
		memcpy(&param_block->gtm_sw_map_info, &isp_k_param->gtm_sw_map_info, sizeof(struct cam_gtm_mapping));
		memcpy(&param_block->gtm_rgb_info.bypass_info, &isp_k_param->gtm_rgb_info.bypass_info, sizeof(struct dcam_dev_raw_gtm_bypass));
		isp_k_param->gtm_sw_map_info.isupdate = 0;
		param_block->gtm_sw_map_info.isupdate = 1;
	}
	return ret;
}
