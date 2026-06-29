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
#include "isp_reg.h"
#include "cam_debugger.h"
#include "isp_dev.h"
#include "isp_hwctx.h"


#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "HSV: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkhsv_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_hsv_info_v3 *hsv_info = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	hsv_info = &isp_k_param->hsv_info3;

	log_ctx = &debug->block_param_log;

	if (!log_ctx)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "HSV: calc ctx %llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, hsv_info->update_flag, hsv_info->hsv_bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "HSV: y_blending_factor %d buf_sel %d, delta_value_en %d\n",
		hsv_info->y_blending_factor, hsv_info->buf_param.hsv_buf_sel,
		hsv_info->hsv_delta_value_en);
	CAM_DEBUG_LOG_WRITE(log_ctx, "HSV: y_blending_factor %d\n",
		 hsv_info->y_blending_factor);
	CAM_DEBUG_LOG_WRITE(log_ctx, "HSV: hsv_hue_thr %d %d %d %d %d %d\n",
		hsv_info->hsv_hue_thr[0][0], hsv_info->hsv_hue_thr[0][1],
		hsv_info->hsv_hue_thr[1][0], hsv_info->hsv_hue_thr[1][1],
		hsv_info->hsv_hue_thr[2][0], hsv_info->hsv_hue_thr[2][1]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "HSV: 1d_sat_lut 0-9 %d %d %d %d %d %d %d %d %d %d\n",
		hsv_info->hsv_1d_sat_lut[0], hsv_info->hsv_1d_sat_lut[1],
		hsv_info->hsv_1d_sat_lut[2], hsv_info->hsv_1d_sat_lut[3],
		hsv_info->hsv_1d_sat_lut[4], hsv_info->hsv_1d_sat_lut[5],
		hsv_info->hsv_1d_sat_lut[6], hsv_info->hsv_1d_sat_lut[7],
		hsv_info->hsv_1d_sat_lut[8], hsv_info->hsv_1d_sat_lut[9]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "HSV: 1d_sat_lut 10-16 %d %d %d %d %d %d %d\n",
		hsv_info->hsv_1d_sat_lut[10], hsv_info->hsv_1d_sat_lut[11],
		hsv_info->hsv_1d_sat_lut[12], hsv_info->hsv_1d_sat_lut[13],
		hsv_info->hsv_1d_sat_lut[14], hsv_info->hsv_1d_sat_lut[15],
		hsv_info->hsv_1d_sat_lut[16]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "HSV: 1d_hue_lut 0-11 %d %d %d %d %d %d %d %d %d %d %d %d\n",
		hsv_info->hsv_1d_hue_lut[0], hsv_info->hsv_1d_hue_lut[1],
		hsv_info->hsv_1d_hue_lut[2], hsv_info->hsv_1d_hue_lut[3],
		hsv_info->hsv_1d_hue_lut[4], hsv_info->hsv_1d_hue_lut[5],
		hsv_info->hsv_1d_hue_lut[6], hsv_info->hsv_1d_hue_lut[7],
		hsv_info->hsv_1d_hue_lut[8], hsv_info->hsv_1d_hue_lut[9],
		hsv_info->hsv_1d_hue_lut[10], hsv_info->hsv_1d_hue_lut[11]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "HSV: 12-24 %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
		hsv_info->hsv_1d_hue_lut[12], hsv_info->hsv_1d_hue_lut[13],
		hsv_info->hsv_1d_hue_lut[14], hsv_info->hsv_1d_hue_lut[15],
		hsv_info->hsv_1d_hue_lut[16], hsv_info->hsv_1d_hue_lut[17],
		hsv_info->hsv_1d_hue_lut[18], hsv_info->hsv_1d_hue_lut[19],
		hsv_info->hsv_1d_hue_lut[20], hsv_info->hsv_1d_hue_lut[21],
		hsv_info->hsv_1d_hue_lut[22], hsv_info->hsv_1d_hue_lut[23],
		hsv_info->hsv_1d_hue_lut[24]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int isp_k_hsv_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int i = 0, j = 0, ret = 0;
	uint32_t val = 0, addr = 0;
	struct isp_dev_hsv_info_v3 *hsv_info = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->hsv_info3.isupdate == 0)
		return ret;

	hsv_info = &isp_k_param->hsv_info3;
	isp_k_param->hsv_info3.isupdate = 0;
	debug = &isp_k_param->debug;

	if (g_isp_block_dump & (1 << _EISP_HSV))
		ispkhsv_param_dump(isp_k_param, debug);

	if (g_isp_bypass[idx] & (1 << _EISP_HSV))
		hsv_info->hsv_bypass = 1;

	ISP_REG_MWR(idx, ISP_HSV_PARAM, BIT_0, hsv_info->hsv_bypass);
	if (hsv_info->hsv_bypass) {
		pr_debug("idx %d, hsv_bypass!\n", idx);
		return 0;
	}

	hsv_info->buf_param.hsv_buf_sel = 0;

	val = (((hsv_info->buf_param.hsv_buf_sel & 0x1) << 1) |
		((hsv_info->hsv_delta_value_en & 0x1) << 2));
	ISP_REG_MWR(idx, ISP_HSV_PARAM , 0x6, val);

	val = (hsv_info->hsv_hue_thr[0][0] & 0x1F) |
		((hsv_info->hsv_hue_thr[0][1] & 0x1F) << 5) |
		((hsv_info->hsv_hue_thr[1][0] & 0x1F) << 10) |
		((hsv_info->hsv_hue_thr[1][1] & 0x1F) << 15) |
		((hsv_info->hsv_hue_thr[2][0] & 0x1F) << 20) |
		((hsv_info->hsv_hue_thr[2][1] & 0x1F) << 25);
	ISP_REG_MWR(idx, ISP_HSV_CFG0, 0x3FFFFFFF, val);

	val = (hsv_info->hsv_param[0].hsv_curve_param.start_a & 0x3FF) |
		((hsv_info->hsv_param[0].hsv_curve_param.end_a & 0x3FF) << 10) |
		((hsv_info->hsv_param[0].hsv_curve_param.start_b & 0x3FF) << 20);
	ISP_REG_MWR(idx, ISP_HSV_CFG1, 0x3FFFFFFF, val);

	val = (hsv_info->hsv_param[0].hsv_curve_param.end_b & 0x3FF) |
		((hsv_info->hsv_param[1].hsv_curve_param.start_a & 0x3FF) << 10) |
		((hsv_info->hsv_param[1].hsv_curve_param.end_a & 0x3FF) << 20);
	ISP_REG_MWR(idx, ISP_HSV_CFG2, 0x3FFFFFFF, val);

	val = (hsv_info->hsv_param[1].hsv_curve_param.start_b & 0x3FF) |
		((hsv_info->hsv_param[1].hsv_curve_param.end_b & 0x3FF) << 10) |
		((hsv_info->hsv_param[2].hsv_curve_param.start_a & 0x3FF) << 20);
	ISP_REG_MWR(idx, ISP_HSV_CFG3, 0x3FFFFFFF, val);

	val = (hsv_info->hsv_param[2].hsv_curve_param.end_a & 0x3FF) |
		((hsv_info->hsv_param[2].hsv_curve_param.start_b & 0x3FF) << 10) |
		((hsv_info->hsv_param[2].hsv_curve_param.end_b & 0x3FF) << 20);
	ISP_REG_MWR(idx, ISP_HSV_CFG4, 0x3FFFFFFF, val);

	val = (hsv_info->hsv_param[3].hsv_curve_param.start_a & 0x3FF) |
		((hsv_info->hsv_param[3].hsv_curve_param.end_a & 0x3FF) << 10) |
		((hsv_info->hsv_param[3].hsv_curve_param.start_b & 0x3FF) << 20);
	ISP_REG_MWR(idx, ISP_HSV_CFG5, 0x3FFFFFFF, val);

	val = (hsv_info->hsv_param[3].hsv_curve_param.end_b & 0x3FF) |
		((hsv_info->y_blending_factor & 0x7FF) << 16);
	ISP_REG_MWR(idx, ISP_HSV_CFG6, 0x7FF03FF, val);

	for(i = 0; i < 12; i++) {
		val = (hsv_info->hsv_1d_hue_lut[2*i] & 0x1FF) |
			((hsv_info->hsv_1d_hue_lut[2*i+1] & 0x1FF) << 9) |
			((hsv_info->hsv_1d_sat_lut[i] & 0x1FFF) << 18);
		ISP_REG_MWR(idx, ISP_HSV_CFG7+i*4, 0x7FFFFFFF, val);
	}

	val = (hsv_info->hsv_1d_hue_lut[24] & 0x1FF) |
		((hsv_info->hsv_1d_sat_lut[12] & 0x1FFF) << 16);
	ISP_REG_MWR(idx, ISP_HSV_CFG19, 0x1FFF01FF,  val);
	val = (hsv_info->hsv_1d_sat_lut[13] & 0x1FFF) |
		((hsv_info->hsv_1d_sat_lut[14] & 0x1FFF) << 16);
	ISP_REG_MWR(idx, ISP_HSV_CFG20, 0x1FFF1FFF, val);
	val = (hsv_info->hsv_1d_sat_lut[15] & 0x1FFF) |
		((hsv_info->hsv_1d_sat_lut[16] & 0x1FFF) << 16);
	ISP_REG_MWR(idx, ISP_HSV_CFG21, 0x1FFF1FFF, val);

	for (i = 0; i < 25; i++) {
		for (j = 0; j < 17; j++) {
			val = 0;
			if (((i + 1) % 2 == 1) && ((j + 1) % 2 == 1)) {
				addr = ((i >> 1) * 9 + (j >> 1)) * 4;
				val = (hsv_info->buf_param.hsv_2d_hue_lut_reg[i][j] & 0x1FF)
					| ((hsv_info->buf_param.hsv_2d_sat_lut[i][j] & 0x1FFF) << 9);
				ISP_REG_WR(idx, ISP_HSV_A_BUF0_CH0 + addr, val);
			} else if (((i + 1) % 2 == 1) && ((j + 1) % 2 != 1)) {
				addr = ((i >> 1) * 8 + (j >> 1)) * 4;
				val = (hsv_info->buf_param.hsv_2d_hue_lut_reg[i][j] & 0x1FF)
					| ((hsv_info->buf_param.hsv_2d_sat_lut[i][j] & 0x1FFF) << 9);
				ISP_REG_WR(idx, ISP_HSV_B_BUF0_CH0 +addr, val);
			} else if (((i + 1) % 2 != 1) && ((j + 1) % 2 == 1)) {
				addr = ((i >> 1) * 9 + (j >> 1)) * 4;
				val = (hsv_info->buf_param.hsv_2d_hue_lut_reg[i][j] & 0x1FF)
					| ((hsv_info->buf_param.hsv_2d_sat_lut[i][j] & 0x1FFF) << 9);
				ISP_REG_WR(idx, ISP_HSV_C_BUF0_CH0 +addr, val);
			} else if (((i + 1) % 2 != 1) && ((j + 1) % 2 != 1)) {
				addr = ((i >> 1) * 8 + (j >> 1)) * 4;
				val = (hsv_info->buf_param.hsv_2d_hue_lut_reg[i][j] & 0x1FF)
					| ((hsv_info->buf_param.hsv_2d_sat_lut[i][j] & 0x1FFF) << 9);
				ISP_REG_WR(idx, ISP_HSV_D_BUF0_CH0 +addr, val);
			}
		}
	}

	return ret;
}

int isp_k_cfg_hsv(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct isp_dev_hsv_info_v3 *hsv_info = NULL;

	hsv_info = &isp_k_param->hsv_info3;

	switch (param->property) {
	case ISP_PRO_HSV_BLOCK:
		ret = copy_from_user((void *)hsv_info, param->property_param, sizeof(struct isp_dev_hsv_info_v3));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return ret;
		}
		isp_k_param->hsv_info3.isupdate = 1;
		break;
	default:
		pr_err("fail to idx %d, support cmd id = %d\n", isp_k_param->cfg_id, param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_hsv(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->hsv_info3.isupdate == 1) {
		memcpy(&param_block->hsv_info3, &isp_k_param->hsv_info3, sizeof(struct isp_dev_hsv_info_v3));
		isp_k_param->hsv_info3.isupdate = 0;
		param_block->hsv_info3.isupdate = 1;
	}

	return ret;
}
