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
#define pr_fmt(fmt) "CNR: %d %d %s : "fmt, current->pid, __LINE__, __func__

static void ispkcnr_param_dump(struct dcam_isp_k_block *isp_k_param,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct isp_dev_cnr_h_info *cnr = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	cnr = &isp_k_param->cnr_info;
	log_ctx = &debug->block_param_log;

	if (!log_ctx)
		return;

	CAM_DEBUG_LOG_WRITE(log_ctx, "Cnr: ctx %llu fid %llu, update %d, bypass %d radius_base %d\n",
		debug->idx, debug->fid, cnr->update_flag, cnr->bypass, cnr->radius_base);
	CAM_DEBUG_LOG_WRITE(log_ctx, "Cnr: sigma_y[0] [%d %d %d %d %d]\n",
		cnr->layer_cnr_h[0].sigma_y[0], cnr->layer_cnr_h[1].sigma_y[0], cnr->layer_cnr_h[2].sigma_y[0],
		cnr->layer_cnr_h[3].sigma_y[0], cnr->layer_cnr_h[4].sigma_y[0]);
	CAM_DEBUG_LOG_WRITE(log_ctx, "Cnr: sigma_y [%d %d %d %d %d]\n",
		cnr->layer_cnr_h[0].sigma_uv[0], cnr->layer_cnr_h[1].sigma_uv[0], cnr->layer_cnr_h[2].sigma_uv[0],
		cnr->layer_cnr_h[3].sigma_uv[0], cnr->layer_cnr_h[4].sigma_uv[0]);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int isp_k_cnr_block(struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	struct isp_dev_cnr_h_info *cnr = NULL;
	struct cam_debug_cfg *debug = NULL;

	if (isp_k_param->cnr_info.isupdate == 0)
		return ret;

	cnr = &isp_k_param->cnr_info;
	isp_k_param->cnr_info.isupdate = 0;
	debug = &isp_k_param->debug;

	if (g_isp_bypass[idx] & (1 << _EISP_CNR))
		cnr->bypass = 1;

	if (g_isp_block_dump & (1 << _EISP_CNR))
		ispkcnr_param_dump(isp_k_param, debug);

	ISP_REG_MWR(idx, ISP_YUV_REC_CNR_CONTRL0, BIT_0, cnr->bypass);

	return ret;
}

int isp_k_cfg_cnr(struct isp_io_param *param,
	struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	uint32_t idx = isp_k_param->cfg_id;
	struct isp_dev_cnr_h_info *cnr = NULL;

	cnr = &isp_k_param->cnr_info;

	switch (param->property) {
	case ISP_PRO_CNR_H_BLOCK:
		ret = copy_from_user((void *)cnr, param->property_param,
				sizeof(struct isp_dev_cnr_h_info));
		if (ret != 0) {
			pr_err("fail to copy from user, ret = %d\n", ret);
			return ret;
		}
		cnr->isupdate = 1;
		if (g_isp_bypass[idx] & (1 << _EISP_CNR))
			cnr->bypass = 1;
		break;
	default:
		pr_err("fail to support cmd id = %d\n", param->property);
		break;
	}

	return ret;
}

int isp_k_update_cnr(void *handle)
{
	int ret = 0;
	uint32_t radius = 0, radius_limit = 0;
	uint32_t idx = 0, new_width = 0, old_width = 0, sensor_width = 0;
	uint32_t new_height = 0, old_height = 0, sensor_height = 0;
	struct isp_dev_cnr_h_info *cnr_info_h = NULL;
	struct isp_cnr_h_info *param_cnr_info = NULL;
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

	cnr_info_h = &block_param->isp_using_param->cnr_info;
	param_cnr_info = &cnr_info_h->layer_cnr_h[0];

	if (cnr_info_h->bypass)
		return 0;

	if (cnr_info_h->radius_base == 0)
		cnr_info_h->radius_base = 1024;

	radius = ((sensor_width + sensor_height) / 4) * param_cnr_info->base_radius_factor / cnr_info_h->radius_base;
	radius_limit = (new_height + new_width) / 4;
	radius = (radius < radius_limit) ? radius : radius_limit;
	radius = new_height * radius / old_height;

	block_param->isp_using_param->cnr_radius = radius;
	pr_debug("base %d, factor %d, radius %d\n", cnr_info_h->radius_base, param_cnr_info->base_radius_factor, radius);

	return ret;
}

int isp_k_cpy_cnr(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->cnr_info.isupdate == 1) {
		memcpy(&param_block->cnr_info, &isp_k_param->cnr_info, sizeof(struct isp_dev_cnr_h_info));
		isp_k_param->cnr_info.isupdate = 0;
		param_block->cnr_info.isupdate = 1;
	}

	return ret;
}
