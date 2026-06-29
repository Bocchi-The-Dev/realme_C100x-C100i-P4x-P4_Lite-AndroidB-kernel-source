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
#define pr_fmt(fmt) "PRECDN: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkcdn_block_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_pre_cdn_info *pre_cdn_info = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	pre_cdn_info = &isp_k_param->pre_cdn_info;
	log_ctx = &debug->block_param_log;

	if (!log_ctx)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "PCdn: ctx %llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, pre_cdn_info->update_flag, pre_cdn_info->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "PCdn: mode %d mode %d, thr %d, writeback_en %d\n",
		pre_cdn_info->mode, pre_cdn_info->median_mode, pre_cdn_info->median_thr,
		pre_cdn_info->median_writeback_en);
	CAM_DEBUG_LOG_WRITE(log_ctx, "PCdn: y_thr %d uv_thr %d, uv_joint %d\n",
		pre_cdn_info->y_thr, pre_cdn_info->uv_thr, pre_cdn_info->uv_joint);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int isp_k_precdn_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	uint32_t val = 0;
	uint32_t i = 0;
	struct isp_dev_pre_cdn_info *pre_cdn_info = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->pre_cdn_info.isupdate == 0)
		return ret;

	pre_cdn_info = &isp_k_param->pre_cdn_info;
	isp_k_param->pre_cdn_info.isupdate = 0;

	debug = &isp_k_param->debug;

	if (g_isp_block_dump & (1 << _EISP_PRECDN))
		ispkcdn_block_param_dump(isp_k_param, debug);

	if (g_isp_bypass[idx] & (1 << _EISP_PRECDN))
		pre_cdn_info->bypass = 1;

	ISP_REG_MWR(idx, ISP_PRECDN_PARAM, BIT_0, pre_cdn_info->bypass);
	if (pre_cdn_info->bypass)
		return 0;

	val = ((pre_cdn_info->mode & 0x1) << 4) |
		((pre_cdn_info->median_writeback_en & 0x1) << 8) |
		((pre_cdn_info->median_mode & 0x3) << 12) |
		((pre_cdn_info->den_stren & 0x3) << 16) |
		((pre_cdn_info->uv_joint & 0x1) << 20);
	ISP_REG_MWR(idx, ISP_PRECDN_PARAM, 0x133110, val);

	val = (pre_cdn_info->median_thr_uv.thru0 & 0x7F) |
		((pre_cdn_info->median_thr_uv.thru1 & 0xFF) << 8) |
		((pre_cdn_info->median_thr_uv.thrv0 & 0x7F) << 16) |
		((pre_cdn_info->median_thr_uv.thrv1 & 0xFF) << 24);
	ISP_REG_WR(idx, ISP_PRECDN_MEDIAN_THRUV01, val);

	val = (pre_cdn_info->median_thr & 0x1FF) |
		((pre_cdn_info->uv_thr & 0xFF) << 16) |
		((pre_cdn_info->y_thr & 0xFF) << 24);
	ISP_REG_WR(idx, ISP_PRECDN_THRYUV, val);

	for (i = 0; i < 3; i++) {
		val = (pre_cdn_info->r_segu[0][i * 2] & 0xFF) |
			((pre_cdn_info->r_segu[1][i * 2] & 0x7) << 8) |
			((pre_cdn_info->r_segu[0][i * 2 + 1] & 0xFF) << 16) |
			((pre_cdn_info->r_segu[1][i * 2 + 1] & 0x7) << 24);
		ISP_REG_WR(idx, ISP_PRECDN_SEGU_0 + i * 4, val);
	}

	val = (pre_cdn_info->r_segu[0][6] & 0xFF)
		| ((pre_cdn_info->r_segu[1][6] & 0x7) << 8);
	ISP_REG_WR(idx, ISP_PRECDN_SEGU_3, val);

	for (i = 0; i < 3; i++) {
		val = (pre_cdn_info->r_segv[0][i * 2] & 0xFF) |
			((pre_cdn_info->r_segv[1][i * 2] & 0x7) << 8) |
			((pre_cdn_info->r_segv[0][i * 2 + 1] & 0xFF) << 16) |
			((pre_cdn_info->r_segv[1][i * 2 + 1] & 0x7) << 24);
		ISP_REG_WR(idx, ISP_PRECDN_SEGV_0 + i * 4, val);
	}

	val = (pre_cdn_info->r_segv[0][6] & 0xFF)
		| ((pre_cdn_info->r_segv[1][6] & 0x7) << 8);
	ISP_REG_WR(idx, ISP_PRECDN_SEGV_3, val);

	for (i = 0; i < 3; i++) {
		val = (pre_cdn_info->r_segy[0][i * 2] & 0xFF) |
			((pre_cdn_info->r_segy[1][i * 2] & 0x7) << 8) |
			((pre_cdn_info->r_segy[0][i * 2 + 1] & 0xFF) << 16) |
			((pre_cdn_info->r_segy[1][i * 2 + 1] & 0x7) << 24);
		ISP_REG_WR(idx, ISP_PRECDN_SEGY_0 + i * 4, val);
	}
	val = (pre_cdn_info->r_segy[0][6] & 0xFF)
		| ((pre_cdn_info->r_segy[1][6] & 0x7) << 8);
	ISP_REG_WR(idx, ISP_PRECDN_SEGY_3, val);

	for (i = 0; i < 3; i++) {
		val =  (pre_cdn_info->r_distw[i * 8] & 0x7) |
			((pre_cdn_info->r_distw[i * 8 + 1] & 0x7) << 4) |
			((pre_cdn_info->r_distw[i * 8 + 2] & 0x7) << 8) |
			((pre_cdn_info->r_distw[i * 8 + 3] & 0x7) << 12) |
			((pre_cdn_info->r_distw[i * 8 + 4] & 0x7) << 16) |
			((pre_cdn_info->r_distw[i * 8 + 5] & 0x7) << 20) |
			((pre_cdn_info->r_distw[i * 8 + 6] & 0x7) << 24) |
			((pre_cdn_info->r_distw[i * 8 + 7] & 0x7) << 28);
		ISP_REG_WR(idx, ISP_PRECDN_DISTW0 + i * 4, val);
	}
	val = pre_cdn_info->r_distw[24] & 0x7;
	ISP_REG_WR(idx, ISP_PRECDN_DISTW3, val);

	return ret;
}

int isp_k_cfg_pre_cdn(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct isp_dev_pre_cdn_info *pre_cdn_info = NULL;

	pre_cdn_info = &isp_k_param->pre_cdn_info;

	switch (param->property) {
	case ISP_PRO_PRE_CDN_BLOCK:
		ret = copy_from_user((void *)pre_cdn_info, param->property_param, sizeof(struct isp_dev_pre_cdn_info));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return ret;
		}
		isp_k_param->pre_cdn_info.isupdate = 1;
		break;
	default:
		pr_err("fail to support cmd id = %d\n",
			param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_pre_cdn(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->pre_cdn_info.isupdate == 1) {
		memcpy(&param_block->pre_cdn_info, &isp_k_param->pre_cdn_info, sizeof(struct isp_dev_pre_cdn_info));
		isp_k_param->pre_cdn_info.isupdate = 0;
		param_block->pre_cdn_info.isupdate = 1;
	}

	return ret;
}
