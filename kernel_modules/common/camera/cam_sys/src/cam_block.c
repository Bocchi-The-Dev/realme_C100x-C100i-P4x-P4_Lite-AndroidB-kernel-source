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

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "cam_block.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_BLOCK: %d %d %s : " fmt, current->pid, __LINE__, __func__

static uint32_t camblock_noisefilter_24b_shift8(uint32_t seed,
	uint32_t *data_out)
{
	uint32_t bit_0 = 0, bit_1 = 0;
	uint32_t bit_2 = 0, bit_3 = 0;
	uint32_t bit_in[8] = {0}, bit_in8b = 0;
	uint32_t out = 0;
	uint32_t i = 0;

	for (i = 0; i < 8; i++) {
		bit_0 = (seed >> (0 + i)) & 0x1;
		bit_1 = (seed >> (1 + i)) & 0x1;
		bit_2 = (seed >> (2 + i)) & 0x1;
		bit_3 = (seed >> (7 + i)) & 0x1;
		bit_in[i] = bit_0 ^ bit_1 ^ bit_2 ^ bit_3;
	}
	bit_in8b = (bit_in[7] << 7) | (bit_in[6] << 6) | (bit_in[5] << 5) |
		(bit_in[4] << 4) | (bit_in[3] << 3) | (bit_in[2] << 2) |
		(bit_in[1] << 1) | bit_in[0];

	out = seed & 0xffffff;
	out = out | (bit_in8b << 24);
	if (data_out)
		*data_out = out;

	out = out >> 8;

	return out;
}

void cam_block_noisefilter_seeds(uint32_t image_width,
	uint32_t seed0, uint32_t *seed1, uint32_t *seed2, uint32_t *seed3)
{
	uint32_t i = 0;

	*seed1 = camblock_noisefilter_24b_shift8(seed0, NULL);
	*seed2 = seed0;

	for (i = 0; i < image_width; i++)
		*seed2 = camblock_noisefilter_24b_shift8(*seed2, NULL);

	*seed3 = camblock_noisefilter_24b_shift8(*seed2, NULL);
}

int cam_block_dcam_init(struct dcam_isp_k_block *blk_pm_ctx)
{
	/* bypass all blocks by default: based on L3 & L5*/
	blk_pm_ctx->lsc.lens_info.bypass = 1;
	blk_pm_ctx->lsc.grid_offset = 0;
	blk_pm_ctx->blc.blc_info.bypass = 1;
	blk_pm_ctx->rgb.gain_info.bypass = 1;
	blk_pm_ctx->rgb.rgb_dither.random_bypass = 1;
	blk_pm_ctx->hist.bayerHist_info.hist_bypass = 1;
	blk_pm_ctx->aem.bypass = 1;
	blk_pm_ctx->afl.afl_info.bypass = 1;
	blk_pm_ctx->awbc.awbc_info.awbc_bypass = 1;
	blk_pm_ctx->pdaf.ppe_ppc_info.ppi_bypass = 1;
	blk_pm_ctx->bpc.bpc_param.bpc_info.bpc_bypass = 1;
	blk_pm_ctx->bpc.bpc_param.bpc_info_l3.bpc_bypass = 1;
	blk_pm_ctx->bpc_n6pro.bpc_param_n6pro.bpc_info.bpc_bypass = 1;
	blk_pm_ctx->grgb.grgb_info.bypass = 1;
	blk_pm_ctx->nr3.nr3_me.bypass = 1;
	blk_pm_ctx->afm.bypass = 1;
	blk_pm_ctx->lscm.bypass = 1;
	blk_pm_ctx->pdaf.bypass = 1;

	/* L5pro added */
	blk_pm_ctx->gtm.gtm_info.bypass_info.gtm_mod_en = 0;
	blk_pm_ctx->gtm.gtm_info.bypass_info.gtm_map_bypass = 1;
	blk_pm_ctx->gtm.gtm_info.bypass_info.gtm_hist_stat_bypass = 1;

	/* N6P & N6L added */
	blk_pm_ctx->hist_roi.hist_roi_info.bypass = 1;
	blk_pm_ctx->gamma_info_v1.gamma_info.bypass = 1;
	blk_pm_ctx->cce_info.bypass = 1;
	blk_pm_ctx->cmc10_info.bypass = 1;
	blk_pm_ctx->cfa_info_v1.bypass = 1;
	blk_pm_ctx->nlm_info_base.ivst_bypass = 1;
	blk_pm_ctx->nlm_info_base.vst_bypass = 1;
	blk_pm_ctx->nlm_info_base.bypass = 1;
	blk_pm_ctx->imbalance_info_base2.nlm_imblance_bypass = 1;
	blk_pm_ctx->rgb_gtm.rgb_gtm_info.bypass_info.gtm_mod_en = 0;
	blk_pm_ctx->rgb_gtm.rgb_gtm_info.bypass_info.gtm_map_bypass = 1;
	blk_pm_ctx->rgb_gtm.rgb_gtm_info.bypass_info.gtm_hist_stat_bypass = 1;

	return 0;
}

int cam_block_isp_init(struct dcam_isp_k_block *isp_k_param)
{
	/* bypass all blocks by default: based on L3*/
	isp_k_param->nlm_info_base.bypass = 1;
	isp_k_param->nlm_info_base.vst_bypass = 1;
	isp_k_param->nlm_info_base.ivst_bypass = 1;
	isp_k_param->nr3_info_base.blend.bypass = 1;
	isp_k_param->brightness_info.bypass = 1;
	isp_k_param->contrast_info.bypass = 1;
	isp_k_param->csa_info.bypass = 1;
	isp_k_param->hue_info.bypass = 1;
	isp_k_param->pstrz_info.bypass = 1;
	isp_k_param->uvd_info.bypass = 1;
	isp_k_param->ynr_info.bypass = 1;
	isp_k_param->nr3d_info.blend.bypass = 1;
	isp_k_param->cce_info.bypass = 1;
	isp_k_param->pre_cdn_info.bypass = 1;
	isp_k_param->cdn_info.bypass = 1;
	isp_k_param->post_cdn_info.bypass = 1;
	isp_k_param->cfa_info.bypass = 1;
	isp_k_param->cmc10_info.bypass = 1;
	isp_k_param->edge_info.bypass = 1;
	isp_k_param->gamma_info.bypass = 1;
	isp_k_param->hsv_info.bypass = 1;
	isp_k_param->iircnr_info.bypass = 1;
	isp_k_param->ygamma_info.bypass = 1;
	isp_k_param->yrandom_info.bypass = 1;
	isp_k_param->nf_info.yrandom_bypass = 1;

	/* L5 added */
	isp_k_param->imbalance_info_v0_base.nlm_imblance_en = 1;
	isp_k_param->ynr_info_v2_base.bypass = 1;
	isp_k_param->grgb_info.bypass = 1;
	isp_k_param->bchs_info.bchs_bypass = 1;
	isp_k_param->ltm_yuv_info.ltm_stat.bypass = 1;
	isp_k_param->ltm_yuv_info.ltm_map.bypass = 1;
	isp_k_param->pstrz_info_v2.bypass = 1;
	isp_k_param->uvd_info_v2.bypass = 1;
	isp_k_param->ynr_info_v2.bypass = 1;

	/* L5pro added */
	isp_k_param->imbalance_info_base.nlm_imblance_bypass = 1;
	isp_k_param->ltm_rgb_info.ltm_stat.bypass = 1;
	isp_k_param->ltm_rgb_info.ltm_map.bypass = 1;

	/* L6 added */
	isp_k_param->imbalance_info_base2.nlm_imblance_bypass = 1;
	isp_k_param->gtm_rgb_info.bypass_info.gtm_mod_en = 0;
	isp_k_param->gtm_rgb_info.bypass_info.gtm_hist_stat_bypass = 1;
	isp_k_param->gtm_rgb_info.bypass_info.gtm_map_bypass = 1;
	isp_k_param->hist2_info.bypass = 1;

	/* N6P added */
	isp_k_param->ynr_info_v3.bypass = 1;
	isp_k_param->cnr_info.bypass = 1;
	isp_k_param->post_cnr_h_info.bypass = 1;
	isp_k_param->uvd_info_v1.bypass = 1;
	isp_k_param->nr3_info_base_v1.blend.bypass = 1;
	isp_k_param->nr3d_info_v1.blend.bypass = 1;
	isp_k_param->hsv_info3.hsv_bypass = 1;
	isp_k_param->lut3d_info.rgb3dlut_bypass = 1;
	isp_k_param->ygamma_info_v1.bypass = 1;
	isp_k_param->edge_info_v3.bypass = 1;

	/* N6L added */
	isp_k_param->hsv_info4.hsv_bypass = 1;

	return 0;
}
