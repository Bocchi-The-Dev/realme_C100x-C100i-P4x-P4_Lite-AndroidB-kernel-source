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

#include "cam_trusty.h"
#include "isp_reg.h"
#include "isp_3dnr.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "3DNR: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void isp_3dnr_blend_param_debug(struct isp_dev_3dnr_info *pnr3)
{
	uint32_t i = 0;

	pr_debug("3dnr_blend param: bypass %d, filter_switch %d, fusion_mode %d\n",
		pnr3->blend.bypass, pnr3->blend.filter_switch, pnr3->blend.fusion_mode);
	for (i = 0; i < PIXEL_SRC_WEIGHT_NUM; i++)
		pr_debug("pixel_src_weight: i %d, y %d, u %d, v %d\n",
			i, pnr3->blend.y_pixel_src_weight[i], pnr3->blend.u_pixel_src_weight[i], pnr3->blend.v_pixel_src_weight[i]);
	pr_debug("noise_threshold: y %d, u %d, v %d\n",
		pnr3->blend.y_pixel_noise_threshold, pnr3->blend.u_pixel_noise_threshold, pnr3->blend.v_pixel_noise_threshold);
	pr_debug("noise_weight: y %d, u %d, v %d\n",
		pnr3->blend.y_pixel_noise_weight, pnr3->blend.u_pixel_noise_weight, pnr3->blend.v_pixel_noise_weight);
	pr_debug("threshold_radial_variation: u_min %d, u_max %d, v_min %d, v_max %d\n",
		pnr3->blend.threshold_radial_variation_u_range_min, pnr3->blend.threshold_radial_variation_u_range_max,
		pnr3->blend.threshold_radial_variation_v_range_min, pnr3->blend.threshold_radial_variation_v_range_min);
	pr_debug("threshold_polyline: y0 %d, y1 %d, y2 %d, y3 %d, y4 %d, y5 %d, y6 %d, y7 %d, y8 %d\n",
		pnr3->blend.y_threshold_polyline_0, pnr3->blend.y_threshold_polyline_1, pnr3->blend.y_threshold_polyline_2,
		pnr3->blend.y_threshold_polyline_3, pnr3->blend.y_threshold_polyline_4, pnr3->blend.y_threshold_polyline_5,
		pnr3->blend.y_threshold_polyline_6, pnr3->blend.y_threshold_polyline_7, pnr3->blend.y_threshold_polyline_8);
	pr_debug("threshold_polyline: u0 %d, u1 %d, u2 %d, u3 %d, u4 %d, u5 %d, u6 %d, u7 %d, u8 %d\n",
		pnr3->blend.u_threshold_polyline_0, pnr3->blend.u_threshold_polyline_1, pnr3->blend.u_threshold_polyline_2,
		pnr3->blend.u_threshold_polyline_3, pnr3->blend.u_threshold_polyline_4, pnr3->blend.u_threshold_polyline_5,
		pnr3->blend.u_threshold_polyline_6, pnr3->blend.u_threshold_polyline_7, pnr3->blend.u_threshold_polyline_8);
	pr_debug("threshold_polyline: v0 %d, v1 %d, v2 %d, v3 %d, v4 %d, v5 %d, v6 %d, v7 %d, v8 %d\n",
		pnr3->blend.v_threshold_polyline_0, pnr3->blend.v_threshold_polyline_1, pnr3->blend.v_threshold_polyline_2,
		pnr3->blend.v_threshold_polyline_3, pnr3->blend.v_threshold_polyline_4, pnr3->blend.v_threshold_polyline_5,
		pnr3->blend.v_threshold_polyline_6, pnr3->blend.v_threshold_polyline_7, pnr3->blend.v_threshold_polyline_8);
	pr_debug("intensity_gain_polyline: y0 %d, y1 %d, y2 %d, y3 %d, y4 %d, y5 %d, y6 %d, y7 %d, y8 %d\n",
		pnr3->blend.y_intensity_gain_polyline_0, pnr3->blend.y_intensity_gain_polyline_1, pnr3->blend.y_intensity_gain_polyline_2,
		pnr3->blend.y_intensity_gain_polyline_3, pnr3->blend.y_intensity_gain_polyline_4, pnr3->blend.y_intensity_gain_polyline_5,
		pnr3->blend.y_intensity_gain_polyline_6, pnr3->blend.y_intensity_gain_polyline_7, pnr3->blend.y_intensity_gain_polyline_8);
	pr_debug("intensity_gain_polyline: u0 %d, u1 %d, u2 %d, u3 %d, u4 %d, u5 %d, u6 %d, u7 %d, u8 %d\n",
		pnr3->blend.u_intensity_gain_polyline_0, pnr3->blend.u_intensity_gain_polyline_1, pnr3->blend.u_intensity_gain_polyline_2,
		pnr3->blend.u_intensity_gain_polyline_3, pnr3->blend.u_intensity_gain_polyline_4, pnr3->blend.u_intensity_gain_polyline_5,
		pnr3->blend.u_intensity_gain_polyline_6, pnr3->blend.u_intensity_gain_polyline_7, pnr3->blend.u_intensity_gain_polyline_8);
	pr_debug("intensity_gain_polyline: v0 %d, v1 %d, v2 %d, v3 %d, v4 %d, v5 %d, v6 %d, v7 %d, v8 %d\n",
		pnr3->blend.v_intensity_gain_polyline_0, pnr3->blend.v_intensity_gain_polyline_1, pnr3->blend.v_intensity_gain_polyline_2,
		pnr3->blend.v_intensity_gain_polyline_3, pnr3->blend.v_intensity_gain_polyline_4, pnr3->blend.v_intensity_gain_polyline_5,
		pnr3->blend.v_intensity_gain_polyline_6, pnr3->blend.v_intensity_gain_polyline_7, pnr3->blend.v_intensity_gain_polyline_8);
	pr_debug("gradient_weight_polyline: %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
		pnr3->blend.gradient_weight_polyline_0, pnr3->blend.gradient_weight_polyline_1, pnr3->blend.gradient_weight_polyline_2,
		pnr3->blend.gradient_weight_polyline_3, pnr3->blend.gradient_weight_polyline_4, pnr3->blend.gradient_weight_polyline_5,
		pnr3->blend.gradient_weight_polyline_6, pnr3->blend.gradient_weight_polyline_7, pnr3->blend.gradient_weight_polyline_8);
	pr_debug("threshold_factor: u0 %d, u1 %d, u2 %d, u3 %d, v0 %d, v1 %d, v2 %d, v3 %d\n",
		pnr3->blend.u_threshold_factor0, pnr3->blend.u_threshold_factor1, pnr3->blend.u_threshold_factor2, pnr3->blend.u_threshold_factor3,
		pnr3->blend.v_threshold_factor0, pnr3->blend.v_threshold_factor1, pnr3->blend.v_threshold_factor2, pnr3->blend.v_threshold_factor3);
	pr_debug("divisor_factor: u0 %d, u1 %d, u2 %d, u3 %d, v0 %d, v1 %d, v2 %d, v3 %d\n",
		pnr3->blend.u_divisor_factor0, pnr3->blend.u_divisor_factor1, pnr3->blend.u_divisor_factor2, pnr3->blend.u_divisor_factor3,
		pnr3->blend.v_divisor_factor0, pnr3->blend.v_divisor_factor1, pnr3->blend.v_divisor_factor2, pnr3->blend.v_divisor_factor3);
	pr_debug("circle: r1 %d, r2 %d, r3 %d\n",
		pnr3->blend.r1_circle, pnr3->blend.r2_circle, pnr3->blend.r3_circle);
	pr_debug("circle_factor: r1 %d, r2 %d, r3 %d\n",
		pnr3->blend.r1_circle_factor, pnr3->blend.r2_circle_factor, pnr3->blend.r3_circle_factor);
	pr_debug("radius %d, r_circle_base %d\n",
		pnr3->blend.radius, pnr3->blend.r_circle_base);
}

static void isp_3dnr_config_mem_ctrl(uint32_t idx,
		struct isp_3dnr_mem_ctrl *mem_ctrl)
{
	unsigned int val;

	if ((g_isp_bypass[idx] >> _EISP_NR3) & 1)
		mem_ctrl->bypass = 1;

	val = ((mem_ctrl->nr3_done_mode & 0x1) << 1) |
		((mem_ctrl->nr3_ft_path_sel & 0x1) << 2) |
		((mem_ctrl->back_toddr_en & 0x1) << 6) |
		((mem_ctrl->chk_sum_clr_en & 0x1) << 9) |
		((mem_ctrl->data_toyuv_en & 0x1) << 12) |
		((mem_ctrl->roi_mode & 0x1) << 14) |
		((mem_ctrl->retain_num & 0x7F) << 16) |
		((mem_ctrl->ref_pic_flag & 0x1) << 23) |
		((mem_ctrl->ft_max_len_sel & 0x1) << 28) |
		(mem_ctrl->bypass & 0x1);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM0, val);

	val = ((mem_ctrl->last_line_mode & 0x1) << 1) |
		((mem_ctrl->first_line_mode & 0x1));
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_LINE_MODE, val);

	val = ((mem_ctrl->start_col & 0x1FFF) << 16) |
		(mem_ctrl->start_row & 0x1FFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM1, val);

	val = ((mem_ctrl->global_img_height & 0x1FFF) << 16) |
		(mem_ctrl->global_img_width & 0x1FFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM2, val);

	val = ((mem_ctrl->img_height & 0x1FFF) << 16) |
		(mem_ctrl->img_width & 0x1FFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM3, val);

	val = ((mem_ctrl->ft_y_height & 0x1FFF) << 16) |
		(mem_ctrl->ft_y_width & 0x1FFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM4, val);

	val = (mem_ctrl->ft_uv_width & 0x1FFF)
		| ((mem_ctrl->ft_uv_height & 0x1FFF) << 16);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM5, val);

	val = ((mem_ctrl->mv_x & 0xFF) << 8) |
		(mem_ctrl->mv_y & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM7, val);

	ISP_REG_WR(idx,
		ISP_3DNR_MEM_CTRL_FT_CUR_LUMA_ADDR,
		mem_ctrl->ft_luma_addr);

	ISP_REG_WR(idx,
		ISP_3DNR_MEM_CTRL_FT_CUR_CHROMA_ADDR,
		mem_ctrl->ft_chroma_addr);

	val = mem_ctrl->img_width & 0xFFFF;
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_FT_CTRL_PITCH, val);

	val = ((mem_ctrl->blend_y_en_start_col & 0x1FFF) << 16) |
		(mem_ctrl->blend_y_en_start_row & 0x1FFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM8, val);

	val = ((mem_ctrl->blend_y_en_end_col & 0x1FFF) << 16) |
		(mem_ctrl->blend_y_en_end_row & 0x1FFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM9, val);

	val = ((mem_ctrl->blend_uv_en_start_col & 0x1FFF) << 16) |
		(mem_ctrl->blend_uv_en_start_row & 0x1FFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM10, val);

	val = ((mem_ctrl->blend_uv_en_end_col & 0x1FFF) << 16) |
		(mem_ctrl->blend_uv_en_end_row & 0x1FFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM11, val);

	val = ((mem_ctrl->ft_hblank_num & 0xFFFF) << 16) |
		((mem_ctrl->pipe_hblank_num & 0xFF) << 8) |
		(mem_ctrl->pipe_flush_line_num & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM12, val);

	val = ((mem_ctrl->pipe_nfull_num & 0x7FF) << 16) |
		(mem_ctrl->ft_fifo_nfull_num & 0xFFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PARAM13, val);
}

void isp_3dnr_config_blend(uint32_t idx,
		struct isp_3dnr_blend_info *blend)
{
	unsigned int val = 0;

	if (blend == NULL) {
		pr_err("fail to 3ndr config_blend_reg param NULL\n");
		return;
	}

	if (blend->isupdate == ISP_3DNR_ISUPDATE_STAUE_IDLE) {
		pr_debug("ctx_id %d blend is not update, return\n", idx);
		return;
	}
	blend->isupdate = ISP_3DNR_ISUPDATE_STAUE_IDLE;
	pr_debug("ctx_id %d config blend regs\n", idx);

	ISP_REG_MWR(idx, ISP_3DNR_BLEND_CONTROL0, BIT_0, 0);
	ISP_REG_MWR(idx, ISP_3DNR_BLEND_CONTROL0, BIT_1, blend->fusion_mode << 1);
	ISP_REG_MWR(idx, ISP_3DNR_BLEND_CONTROL0, BIT_2, blend->filter_switch << 2);

	val = ((blend->y_pixel_src_weight[0] & 0xFF) << 24) |
		((blend->u_pixel_src_weight[0] & 0xFF) << 16) |
		((blend->v_pixel_src_weight[0] & 0xFF) << 8) |
		(blend->y_pixel_noise_threshold & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG1, val);

	val = ((blend->u_pixel_noise_threshold & 0xFF) << 24) |
		((blend->v_pixel_noise_threshold & 0xFF) << 16) |
		((blend->y_pixel_noise_weight & 0xFF) << 8) |
		(blend->u_pixel_noise_weight & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG2, val);

	val = ((blend->v_pixel_noise_weight & 0xFF) << 24) |
		((blend->threshold_radial_variation_u_range_min & 0xFF) << 16) |
		((blend->threshold_radial_variation_u_range_max & 0xFF) << 8) |
		(blend->threshold_radial_variation_v_range_min & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG3, val);

	val = ((blend->threshold_radial_variation_v_range_max & 0xFF) << 24) |
		((blend->y_threshold_polyline_0 & 0xFF) << 16) |
		((blend->y_threshold_polyline_1 & 0xFF) << 8) |
		(blend->y_threshold_polyline_2 & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG4, val);

	val = ((blend->y_threshold_polyline_3 & 0xFF) << 24) |
		((blend->y_threshold_polyline_4 & 0xFF) << 16) |
		((blend->y_threshold_polyline_5 & 0xFF) << 8) |
		(blend->y_threshold_polyline_6 & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG5, val);

	val = ((blend->y_threshold_polyline_7 & 0xFF) << 24) |
		((blend->y_threshold_polyline_8 & 0xFF) << 16) |
		((blend->u_threshold_polyline_0 & 0xFF) << 8) |
		(blend->u_threshold_polyline_1 & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG6, val);

	val = ((blend->u_threshold_polyline_2 & 0xFF) << 24) |
		((blend->u_threshold_polyline_3 & 0xFF) << 16) |
		((blend->u_threshold_polyline_4 & 0xFF) << 8) |
		(blend->u_threshold_polyline_5 & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG7, val);

	val = ((blend->u_threshold_polyline_6 & 0xFF) << 24) |
		((blend->u_threshold_polyline_7 & 0xFF) << 16) |
		((blend->u_threshold_polyline_8 & 0xFF) << 8) |
		(blend->v_threshold_polyline_0 & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG8, val);

	val = ((blend->v_threshold_polyline_1 & 0xFF) << 24) |
		((blend->v_threshold_polyline_2 & 0xFF) << 16) |
		((blend->v_threshold_polyline_3 & 0xFF) << 8) |
		(blend->v_threshold_polyline_4 & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG9, val);

	val = ((blend->v_threshold_polyline_5 & 0xFF) << 24) |
		((blend->v_threshold_polyline_6 & 0xFF) << 16) |
		((blend->v_threshold_polyline_7 & 0xFF) << 8) |
		(blend->v_threshold_polyline_8 & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG10, val);

	val = ((blend->y_intensity_gain_polyline_0 & 0x7F) << 24) |
		((blend->y_intensity_gain_polyline_1 & 0x7F) << 16) |
		((blend->y_intensity_gain_polyline_2 & 0x7F) << 8) |
		(blend->y_intensity_gain_polyline_3 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG11, val);

	val = ((blend->y_intensity_gain_polyline_4 & 0x7F) << 24) |
		((blend->y_intensity_gain_polyline_5 & 0x7F) << 16) |
		((blend->y_intensity_gain_polyline_6 & 0x7F) << 8) |
		(blend->y_intensity_gain_polyline_7 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG12, val);

	val = ((blend->y_intensity_gain_polyline_8 & 0x7F) << 24) |
		((blend->u_intensity_gain_polyline_0 & 0x7F) << 16) |
		((blend->u_intensity_gain_polyline_1 & 0x7F) << 8) |
		(blend->u_intensity_gain_polyline_2 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG13, val);

	val = ((blend->u_intensity_gain_polyline_3 & 0x7F) << 24) |
		((blend->u_intensity_gain_polyline_4 & 0x7F) << 16) |
		((blend->u_intensity_gain_polyline_5 & 0x7F) << 8) |
		(blend->u_intensity_gain_polyline_6 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG14, val);

	val = ((blend->u_intensity_gain_polyline_7 & 0x7F) << 24) |
		((blend->u_intensity_gain_polyline_8 & 0x7F) << 16) |
		((blend->v_intensity_gain_polyline_0 & 0x7F) << 8) |
		(blend->v_intensity_gain_polyline_1 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG15, val);

	val = ((blend->v_intensity_gain_polyline_2 & 0x7F) << 24) |
		((blend->v_intensity_gain_polyline_3 & 0x7F) << 16) |
		((blend->v_intensity_gain_polyline_4 & 0x7F) << 8) |
		(blend->v_intensity_gain_polyline_5 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG16, val);

	val = ((blend->v_intensity_gain_polyline_6 & 0x7F) << 24) |
		((blend->v_intensity_gain_polyline_7 & 0x7F) << 16) |
		((blend->v_intensity_gain_polyline_8 & 0x7F) << 8) |
		(blend->gradient_weight_polyline_0 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG17, val);

	val = ((blend->gradient_weight_polyline_1 & 0x7F) << 24) |
		((blend->gradient_weight_polyline_2 & 0x7F) << 16) |
		((blend->gradient_weight_polyline_3 & 0x7F) << 8) |
		(blend->gradient_weight_polyline_4 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG18, val);

	val = ((blend->gradient_weight_polyline_5 & 0x7F) << 24) |
		((blend->gradient_weight_polyline_6 & 0x7F) << 16) |
		((blend->gradient_weight_polyline_7 & 0x7F) << 8) |
		(blend->gradient_weight_polyline_8 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG19, val);

	val = ((blend->gradient_weight_polyline_9 & 0x7F) << 24) |
		((blend->gradient_weight_polyline_10 & 0x7F) << 16) |
		((blend->u_threshold_factor0 & 0x7F) << 8) |
		(blend->u_threshold_factor1 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG20, val);

	val = ((blend->u_threshold_factor2 & 0x7F) << 24) |
		((blend->u_threshold_factor3 & 0x7F) << 16) |
		((blend->v_threshold_factor0 & 0x7F) << 8) |
		(blend->v_threshold_factor1 & 0x7F);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG21, val);

	val = ((blend->v_threshold_factor2 & 0x7F) << 24) |
		((blend->v_threshold_factor3 & 0x7F) << 16) |
		((blend->u_divisor_factor0 & 0x7) << 12) |
		((blend->u_divisor_factor1 & 0x7) << 8) |
		((blend->u_divisor_factor2 & 0x7) << 4) |
		(blend->u_divisor_factor3 & 0x7);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG22, val);

	val = ((blend->v_divisor_factor0 & 0x7) << 28) |
		((blend->v_divisor_factor1 & 0x7) << 24) |
		((blend->v_divisor_factor2 & 0x7) << 20) |
		((blend->v_divisor_factor3 & 0x7) << 16) |
		(blend->r1_circle & 0xFFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG23, val);

	val = ((blend->r2_circle & 0xFFF) << 16) |
		(blend->r3_circle & 0xFFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG24, val);
}

static void isp_3dnr_config_store(uint32_t idx,
				  struct isp_3dnr_store *nr3_store)
{
	unsigned int val;

	if ((g_isp_bypass[idx] >> _EISP_NR3) & 1)
		nr3_store->st_bypass = 1;
	val = ((nr3_store->chk_sum_clr_en & 0x1) << 4) |
		((nr3_store->shadow_clr_sel & 0x1) << 3) |
		((nr3_store->st_max_len_sel & 0x1) << 1) |
		(nr3_store->st_bypass & 0x1);
	ISP_REG_WR(idx, ISP_3DNR_STORE_PARAM, val);

	val = ((nr3_store->img_height & 0xFFFF) << 16) |
		(nr3_store->img_width & 0xFFFF);
	ISP_REG_WR(idx, ISP_3DNR_STORE_SIZE, val);

	val = nr3_store->st_luma_addr;
	ISP_REG_WR(idx, ISP_3DNR_STORE_LUMA_ADDR, val);

	val = nr3_store->st_chroma_addr;
	ISP_REG_WR(idx, ISP_3DNR_STORE_CHROMA_ADDR, val);

	val = nr3_store->st_pitch & 0xFFFF;
	ISP_REG_WR(idx, ISP_3DNR_STORE_PITCH, val);

	ISP_REG_MWR(idx, ISP_3DNR_STORE_SHADOW_CLR,
		BIT_0, nr3_store->shadow_clr);
}

static void isp_3dnr_config_crop(uint32_t idx,
		struct isp_3dnr_crop *crop)
{
	unsigned int val;

	if ((g_isp_bypass[idx] >> _EISP_NR3) & 1)
		crop->crop_bypass = 1;
	ISP_REG_MWR(idx, ISP_3DNR_MEM_CTRL_PRE_PARAM0,
		BIT_0, crop->crop_bypass);

	val = ((crop->src_height & 0xFFFF) << 16) |
		(crop->src_width & 0xFFFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PRE_PARAM1, val);

	val = ((crop->dst_height & 0xFFFF) << 16) |
		(crop->dst_width & 0xFFFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PRE_PARAM2, val);

	val = ((crop->start_x & 0xFFFF) << 16) |
		(crop->start_y & 0xFFFF);
	ISP_REG_WR(idx, ISP_3DNR_MEM_CTRL_PRE_PARAM3, val);
}

static int isp_k_3dnr_block(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	struct isp_dev_3dnr_info *pnr3 = NULL;

	pnr3 = &isp_k_param->nr3_info_base;

	ret = copy_from_user((void *)pnr3,
			param->property_param,
			sizeof(struct isp_dev_3dnr_info));
	if (ret != 0) {
		pr_err("fail to 3dnr copy from user, ret = %d\n", ret);
		return -EPERM;
	}

	pnr3->blend.isupdate = ISP_3DNR_ISUPDATE_STAUE_WORK;
	memcpy(&isp_k_param->nr3d_info, pnr3, sizeof(struct isp_dev_3dnr_info));

	isp_3dnr_blend_param_debug(pnr3);
	pr_debug("ctx_id %d get 3dnr_blend param done\n", idx);
	return ret;
}

/*
 * global function
 */
void isp_3dnr_bypass_config(uint32_t idx)
{
	ISP_REG_MWR(idx, ISP_3DNR_MEM_CTRL_PARAM0, BIT_0, 1);
	ISP_REG_MWR(idx, ISP_3DNR_BLEND_CONTROL0, BIT_0, 1);
	ISP_REG_MWR(idx, ISP_3DNR_STORE_PARAM, BIT_0, 1);
	ISP_REG_MWR(idx, ISP_FBC_3DNR_STORE_PARAM, BIT_0, 1);
	ISP_REG_MWR(idx, ISP_3DNR_MEM_CTRL_PRE_PARAM0, BIT_0, 1);
	ISP_REG_MWR(idx, ISP_COMMON_SCL_PATH_SEL, BIT_8, 0 << 8);
}

void isp_3dnr_config_param(struct isp_3dnr_ctx_desc *ctx)
{
	struct isp_3dnr_mem_ctrl *mem_ctrl = NULL;
	struct isp_3dnr_store *nr3_store = NULL;
	struct isp_3dnr_blend_info *blend = NULL;
	struct isp_3dnr_crop *crop = NULL;
	uint32_t blend_cnt = 0, idx = 0;
	unsigned int val;

	if (!ctx) {
		pr_err("fail to 3dnr_config_reg parm NULL\n");
		return;
	}

	idx = ctx->ctx_id;
	blend = &ctx->isp_block->nr3_info_base.blend;
	mem_ctrl = &ctx->mem_ctrl;
	isp_3dnr_config_mem_ctrl(idx, mem_ctrl);

	nr3_store = &ctx->nr3_store;
	isp_3dnr_config_store(idx, nr3_store);

	crop = &ctx->crop;
	isp_3dnr_config_crop(idx, crop);

	pr_debug("ctx %d blend_bypass %d\n", ctx->ctx_id, blend->bypass);
	ISP_REG_MWR(idx, ISP_3DNR_BLEND_CONTROL0, BIT_0, blend->bypass);
	ISP_REG_MWR(idx, ISP_COMMON_SCL_PATH_SEL, BIT_8, 0x1 << 8);

	blend_cnt = ctx->blending_cnt;
	if (blend_cnt > 3)
		blend_cnt = 3;

	val = ((blend->y_pixel_src_weight[blend_cnt] & 0xFF) << 24) |
		((blend->u_pixel_src_weight[blend_cnt] & 0xFF) << 16) |
		((blend->v_pixel_src_weight[blend_cnt] & 0xFF) << 8) |
		(blend->y_pixel_noise_threshold & 0xFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG1, val);
}

int isp_k_update_3dnr(uint32_t idx,
	struct dcam_isp_k_block *isp_k_param,
	uint32_t new_width, uint32_t old_width,
	uint32_t new_height, uint32_t old_height)
{
	unsigned int val;
	uint32_t r1_circle, r1_circle_limit;
	uint32_t r2_circle, r2_circle_limit;
	uint32_t r3_circle, r3_circle_limit;
	struct isp_dev_3dnr_info *pnr3, *pdst;

	pdst = &isp_k_param->nr3d_info;
	pnr3 = &isp_k_param->nr3_info_base;

	if (old_width == 0) {
		pr_err("fail to get valid old size %d %d\n", old_width, old_height);
		return -EFAULT;
	}
	r1_circle = pnr3->blend.r1_circle * new_width / old_width;
	r1_circle_limit = (new_width / 2);
	r1_circle_limit *= pnr3->blend.r1_circle_factor;
	r1_circle_limit /= pnr3->blend.r_circle_base;
	r1_circle = (r1_circle < r1_circle_limit) ? r1_circle : r1_circle_limit;

	r2_circle = pnr3->blend.r2_circle * new_width / old_width;
	r2_circle_limit = (new_width / 2);
	r2_circle_limit *= pnr3->blend.r2_circle_factor;
	r2_circle_limit /= pnr3->blend.r_circle_base;
	r2_circle = (r2_circle < r2_circle_limit) ? r2_circle : r2_circle_limit;

	r3_circle = pnr3->blend.r3_circle * new_width / old_width;
	r3_circle_limit = (new_width / 2);
	r3_circle_limit *= pnr3->blend.r3_circle_factor;
	r3_circle_limit /= pnr3->blend.r_circle_base;
	r3_circle = (r3_circle < r3_circle_limit) ? r3_circle : r3_circle_limit;

	val = ((pnr3->blend.v_divisor_factor0 & 0x7) << 28) |
		((pnr3->blend.v_divisor_factor1 & 0x7) << 24) |
		((pnr3->blend.v_divisor_factor2 & 0x7) << 20) |
		((pnr3->blend.v_divisor_factor3 & 0x7) << 16) |
		(r1_circle & 0xFFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG23, val);

	val = ((r2_circle & 0xFFF) << 16) |
		(r3_circle & 0xFFF);
	ISP_REG_WR(idx, ISP_3DNR_BLEND_CFG24, val);

	pdst->blend.r1_circle = r1_circle;
	pdst->blend.r2_circle = r2_circle;
	pdst->blend.r3_circle = r3_circle;

	pr_debug("orig %d %d %d, factor %d %d %d, base %d, new %d %d %d\n",
		pnr3->blend.r1_circle, pnr3->blend.r2_circle, pnr3->blend.r3_circle,
		pnr3->blend.r1_circle_factor, pnr3->blend.r2_circle_factor,
		pnr3->blend.r3_circle_factor, pnr3->blend.r_circle_base,
		r1_circle, r2_circle, r3_circle);
	return 0;
}

int isp_k_cfg_3dnr(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	uint32_t idx = isp_k_param->cfg_id;

	if (!param || !param->property_param) {
		pr_err("fail to get valid param error %p\n", param);
		return -1;
	}

	switch (param->property) {
	case ISP_PRO_3DNR_BLOCK:
		ret = isp_k_3dnr_block(param, isp_k_param, idx);
		break;
	default:
		pr_err("fail to 3dnr cmd id = %d\n", param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_3dnr(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->nr3_info_base.blend.isupdate == ISP_3DNR_ISUPDATE_STAUE_WORK) {
		pr_debug("ctx_id %d copy 3dnr_blend param from receive_param to isp_k_param of node\n", param_block->cfg_id);
		memcpy(&param_block->nr3_info_base, &isp_k_param->nr3_info_base, sizeof(struct isp_dev_3dnr_info));
		memcpy(&param_block->nr3d_info, &isp_k_param->nr3d_info, sizeof(struct isp_dev_3dnr_info));
		isp_k_param->nr3_info_base.blend.isupdate = ISP_3DNR_ISUPDATE_STAUE_IDLE;
		param_block->nr3_info_base.blend.isupdate = ISP_3DNR_ISUPDATE_STAUE_WORK;
	}

	return ret;
}
