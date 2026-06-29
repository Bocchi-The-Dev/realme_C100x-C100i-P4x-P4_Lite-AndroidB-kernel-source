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
#include "pyr_dec_node.h"
#include "cam_debugger.h"
#include "isp_dev.h"
#include "isp_hwctx.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCT: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkdct_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_dct_info *dct = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	dct = &isp_k_param->dct_info;
	log_ctx = &debug->block_param_log;

	if (!log_ctx)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "dct: ctx %llu fid %llu, update %d, bypass %d dct->blend_en %d\n",
		debug->idx, debug->fid, dct->update_flag, dct->bypass, dct->blend_en);
	CAM_DEBUG_LOG_WRITE(log_ctx, "dct: base %d, factor %d\n", dct->rnr_radius_base, dct->rnr_radius_factor);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int isp_k_dct_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	struct isp_dev_dct_info *dct = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->dct_info.isupdate == 0)
		return ret;

	dct = &isp_k_param->dct_info;
	isp_k_param->dct_info.isupdate = 0;

	debug = &isp_k_param->debug;

	if (g_isp_block_dump & (1 << _EISP_DCT))
		ispkdct_param_dump(isp_k_param, debug);

	if (g_isp_bypass[idx] & (1 << _EISP_DCT))
		dct->bypass = 1;

	return ret;
}

int isp_k_cfg_dct(struct isp_io_param *param, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct isp_dev_dct_info *dct = NULL;

	dct = &isp_k_param->dct_info;

	switch (param->property) {
	case ISP_PRO_DCT_BLOCK:
		ret = copy_from_user((void *)dct, param->property_param, sizeof(struct isp_dev_dct_info));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return ret;
		}
		dct->isupdate = 1;
		break;
	default:
		pr_err("fail to support cmd id = %d\n",
			param->property);
		break;
	}

	return ret;
}

int isp_k_update_dct(void *handle)
{
	int ret = 0;
	uint32_t radius = 0, radius_limit = 0;
	uint32_t new_width = 0, old_width = 0, sensor_width = 0;
	uint32_t new_height = 0, old_height = 0, sensor_height = 0;
	struct isp_dev_dct_info *dct_info = NULL;
	struct pyr_dec_node *node = NULL;

	if (!handle) {
		pr_err("fail to get invalid in ptr\n");
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;

	new_width = node->dct_ynr_info.new_width;
	new_height = node->dct_ynr_info.new_height;
	old_width = node->dct_ynr_info.old_width;
	old_height = node->dct_ynr_info.old_height;
	sensor_width = node->dct_ynr_info.sensor_width;
	sensor_height = node->dct_ynr_info.sensor_height;

	dct_info = node->dct_ynr_info.dct;
	if (dct_info->bypass)
		return 0;

	if (dct_info->rnr_radius_base == 0)
		dct_info->rnr_radius_base = 1024;

	radius = sensor_height * dct_info->rnr_radius_factor / dct_info->rnr_radius_base;
	radius_limit = new_height;
	radius = (radius < radius_limit) ? radius : radius_limit;
	radius = new_height * radius / old_height;

	node->dct_ynr_info.dct_radius = radius;
	pr_debug("base %d, factor %d, radius %d\n", dct_info->rnr_radius_base, dct_info->rnr_radius_factor, radius);

	return ret;
}

int isp_k_cpy_dct(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->dct_info.isupdate == 1) {
		memcpy(&param_block->dct_info, &isp_k_param->dct_info, sizeof(struct isp_dev_dct_info));
		isp_k_param->dct_info.isupdate = 0;
		param_block->dct_info.isupdate = 1;
	}

	return ret;
}
