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
#define pr_fmt(fmt) "YNR: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void ispkynr_block_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_ynr_info_v3 *pynr_param = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	pynr_param = &isp_k_param->ynr_info_v3;
	log_ctx = &debug->block_param_log;

	if (!log_ctx)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "YNR: ctx %llu fid %llu, update %d, bypass %d\n",
		debug->idx, debug->fid, pynr_param->update_flag, pynr_param->bypass);
	CAM_DEBUG_LOG_WRITE(log_ctx, "YNR: rad %d, rad_base %d, radius_factor %d imgCenterX %d imgCenterY %d\n",
		pynr_param->radius, pynr_param->radius_base, pynr_param->radius_factor,
		pynr_param->imgCenterX,pynr_param->imgCenterY);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int isp_k_ynr_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	struct isp_dev_ynr_info_v3 *ynr = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->ynr_info_v3.isupdate == 0)
		return ret;

	ynr = &isp_k_param->ynr_info_v3;
	isp_k_param->ynr_info_v3.isupdate = 0;
	debug = &isp_k_param->debug;

	if (g_isp_bypass[idx] & (1 << _EISP_YNR))
		ynr->bypass = 1;

	if (g_isp_block_dump & (1 << _EISP_YNR))
		ispkynr_block_param_dump(isp_k_param, debug);

	ISP_REG_MWR(idx, ISP_YUV_REC_YNR_CONTRL0, BIT_0, ynr->bypass);

	return ret;
}

int isp_k_cfg_ynr(struct isp_io_param *param, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	struct isp_dev_ynr_info_v3 *ynr = NULL;

	ynr = &isp_k_param->ynr_info_v3;

	switch (param->property) {
	case ISP_PRO_YNR_BLOCK:
		ret = copy_from_user((void *)ynr, param->property_param, sizeof(struct isp_dev_ynr_info_v3));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return ret;
		}

		isp_k_param->ynr_info_v3.isupdate = 1;

		break;
	default:
		pr_err("fail to support cmd id = %d\n",
			param->property);
		break;
	}

	return ret;
}

int isp_k_update_ynr(void *handle)
{
	int ret = 0;
	uint32_t radius = 0, radius_limit = 0;
	uint32_t idx = 0, new_width = 0, old_width = 0, sensor_width = 0;
	uint32_t new_height = 0, old_height = 0, sensor_height = 0;
	struct isp_dev_ynr_info_v3 *ynr_info = NULL;
	struct isp_k_block_param *block_param = NULL;

	if (!handle) {
		pr_err("fail to get invalid in ptr\n");
		return -EFAULT;
	}

	block_param = (struct isp_k_block_param *)handle;
	idx = block_param->cfg_id;
	new_width = block_param->isp_using_param->blkparam_info.new_width;
	new_height = block_param->isp_using_param->blkparam_info.new_height;
	old_width = block_param->isp_using_param->blkparam_info.old_width;
	old_height = block_param->isp_using_param->blkparam_info.old_height;
	sensor_width = block_param->isp_using_param->sn_size.w;
	sensor_height = block_param->isp_using_param->sn_size.h;

	ynr_info = &block_param->isp_using_param->ynr_info_v3;
	if (ynr_info->bypass)
		return 0;

	if (ynr_info->radius_base == 0)
		ynr_info->radius_base = 1024;

	radius = sensor_height * ynr_info->radius_factor / ynr_info->radius_base;
	radius_limit = new_height;
	radius = (radius < radius_limit) ? radius : radius_limit;
	radius = new_height * radius / old_height;

	block_param->isp_using_param->ynr_radius = radius;
	pr_debug("base %d, factor %d, radius %d\n", ynr_info->radius_base, ynr_info->radius_factor, radius);

	return ret;
}

int isp_k_cpy_ynr(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->ynr_info_v3.isupdate == 1) {
		memcpy(&param_block->ynr_info_v3, &isp_k_param->ynr_info_v3, sizeof(struct isp_dev_ynr_info_v3));
		isp_k_param->ynr_info_v3.isupdate = 0;
		param_block->ynr_info_v3.isupdate = 1;
	}

	return ret;
}
