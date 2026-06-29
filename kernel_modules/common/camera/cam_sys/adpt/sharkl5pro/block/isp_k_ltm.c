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
#include "isp_ltm.h"
#include "isp_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_K_LTM: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define ISP_LTM_HIST_BUF0                0

int isp_ltm_map_param_get(struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	unsigned long ltm_map_addr = 0;
	struct isp_dev_rgb_ltm_info *p = NULL;

	p = &isp_k_param->ltm_rgb_info;
	ltm_map_addr = (unsigned long)p->ltm_map.ltm_map_addr;
	ret = copy_from_user((void *)isp_k_param->ltm_map_info,
			(void __user *)ltm_map_addr,
			p->ltm_map.ltm_map_size);
	if (ret != 0) {
		pr_err("fail to get ltm from user, ret = %d\n", ret);
		return -EPERM;
	}

	return ret;
}

static void isp_ltm_config_hists(uint32_t idx, struct isp_ltm_hists *hists)
{
	unsigned int val, i;
	unsigned int base = ISP_LTM_HIST_RGB_BASE;
	unsigned int buf_addr = 0;
	unsigned int buf_addr_0 = ISP_LTM_RGB_HIST_BUF0_ADDR;
	unsigned int buf_addr_1 = ISP_LTM_RGB_HIST_BUF1_ADDR;

	pr_debug("isp %d rgb ltm hist bypass %d\n", idx, hists->bypass);
	if ((g_isp_bypass[idx] >> _EISP_LTM) & 1) {
		pr_debug("ltm hist g_isp_bypass\n");
		hists->bypass = 1;
	}
	pr_debug("isp %d rgb ltm hist bypass %d\n", idx, hists->bypass);
	ISP_REG_MWR(idx, base + ISP_LTM_HIST_PARAM, BIT_0, hists->bypass);
	if (hists->bypass)
		return;

	val = ((hists->buf_sel & 0x1) << 5) |
		((hists->channel_sel & 0x1) << 4) |
		((hists->buf_full_mode & 0x1) << 3) |
		((hists->region_est_en & 0x1) << 2) |
		((hists->binning_en & 0x1) << 1) |
		(hists->bypass & 0x1);
	ISP_REG_WR(idx, base + ISP_LTM_HIST_PARAM, val);

	val = ((hists->roi_start_y & 0x1FFF) << 16) |
		(hists->roi_start_x & 0x1FFF);
	ISP_REG_WR(idx, base + ISP_LTM_ROI_START, val);

	val = ((hists->tile_num_y_minus & 0x7) << 28) |
		((hists->tile_height & 0x1FF) << 16) |
		((hists->tile_num_x_minus & 0x7) << 12) |
		(hists->tile_width & 0x1FF);
	ISP_REG_WR(idx, base + ISP_LTM_TILE_RANGE, val);

	val = ((hists->clip_limit_min & 0xFFFF) << 16) |
		(hists->clip_limit & 0xFFFF);
	ISP_REG_WR(idx, base + ISP_LTM_CLIP_LIMIT, val);

	val = hists->texture_proportion & 0x1F;
	ISP_REG_WR(idx, base + ISP_LTM_THRES, val);

	val = hists->addr;
	ISP_REG_WR(idx, base + ISP_LTM_ADDR, val);

	val = (((hists->wr_num * 2) & 0x1FF) << 16) |
		(hists->pitch & 0xFFFF);
	ISP_REG_WR(idx, base + ISP_LTM_PITCH, val);

	if (hists->buf_sel == ISP_LTM_HIST_BUF0)
		buf_addr = buf_addr_0;
	else
		buf_addr = buf_addr_1;
	for (i = 0; i < LTM_HIST_TABLE_NUM; i++)
		ISP_REG_WR(idx, buf_addr + i * 4, hists->ltm_hist_table[i]);
}

static void isp_ltm_config_map(uint32_t idx, struct isp_ltm_map *map)
{
	unsigned int val;
	unsigned int base = 0;

	pr_debug("isp %d rgb ltm map bypass %d\n", idx, map->bypass);

	if ((g_isp_bypass[idx] >> _EISP_LTM) & 1) {
		pr_debug("ltm map g_isp_bypass\n");
		map->bypass = 1;
	}

	pr_debug("isp %d rgb ltm map bypass %d\n", idx, map->bypass);

	ISP_REG_MWR(idx, base + ISP_LTM_MAP_PARAM0, BIT_0, map->bypass);
	if (map->bypass)
		return;

	val = ((map->fetch_wait_line & 0x1) << 4) |
		((map->fetch_wait_en & 0x1) << 3) |
		((map->hist_error_en & 0x1) << 2) |
		((map->burst8_en & 0x1) << 1) |
		(map->bypass & 0x1);
	ISP_REG_WR(idx, base + ISP_LTM_MAP_PARAM0, val);

	val = ((map->tile_y_num & 0x7) << 28) |
		((map->tile_x_num & 0x7) << 24) |
		((map->tile_height & 0x3FF) << 12) |
		(map->tile_width & 0x3FF);
	ISP_REG_WR(idx, base + ISP_LTM_MAP_PARAM1, val);

	val = map->tile_size_pro & 0xFFFFF;
	ISP_REG_WR(idx, base + ISP_LTM_MAP_PARAM2, val);

	val = ((map->tile_right_flag & 0x1) << 23) |
		((map->tile_start_y & 0x7FF) << 12) |
		((map->tile_left_flag & 0x1) << 11) |
		(map->tile_start_x & 0x7FF);
	ISP_REG_WR(idx, base + ISP_LTM_MAP_PARAM3, val);

	val = map->mem_init_addr;
	ISP_REG_WR(idx, base + ISP_LTM_MAP_PARAM4, val);

	val = (map->hist_pitch & 0x7) << 24;
	ISP_REG_WR(idx, base + ISP_LTM_MAP_PARAM5, val);
}

int isp_ltm_config_param(void *handle)
{
	uint32_t idx = 0;
	struct isp_ltm_ctx_desc *ctx = NULL;
	struct isp_ltm_hists *hists = NULL;
	struct isp_ltm_map *map = NULL;

	if (!handle) {
		pr_err("fail to get invalid in ptr\n");
		return -EFAULT;
	}

	ctx = (struct isp_ltm_ctx_desc *)handle;
	idx = ctx->ctx_id;
	hists = &ctx->hists;
	map = &ctx->map;
	if (ctx->bypass) {
		hists->bypass = 1;
		map->bypass = 1;
	}

	pr_debug("isp %d ltm ctx bypass %d, hist %d map %d\n", idx, ctx->bypass, hists->bypass, map->bypass);

	isp_ltm_config_hists(idx, hists);
	isp_ltm_config_map(idx, map);

	return 0;
}

int isp_k_ltm_rgb_block(struct isp_io_param *param,
		struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	struct isp_dev_rgb_ltm_info *p = NULL;

	p = &isp_k_param->ltm_rgb_info;

	ret = copy_from_user((void *)p,
			(void __user *)param->property_param,
			sizeof(struct isp_dev_rgb_ltm_info));
	isp_k_param->ltm_rgb_info.isupdate = 1;
	if (ret != 0) {
		pr_err("fail to get ltm from user, ret = %d\n", ret);
		return -EPERM;
	}
	ret = isp_ltm_map_param_get(isp_k_param);
	pr_debug("isp %d ltm hist %d map %d\n", idx, p->ltm_stat.bypass, p->ltm_map.bypass);

	return ret;
}

int isp_k_ltm_yuv_block(struct isp_io_param *param,
		struct dcam_isp_k_block *isp_k_param, uint32_t idx)
{
	int ret = 0;
	struct isp_dev_yuv_ltm_info *p = NULL;

	p = &isp_k_param->ltm_yuv_info;

	ret = copy_from_user((void *)p,
			(void __user *)param->property_param,
			sizeof(struct isp_dev_yuv_ltm_info));
	isp_k_param->ltm_yuv_info.isupdate = 1;
	if (ret != 0) {
		pr_err("fail to get ltm from user, ret = %d\n", ret);
		return -EPERM;
	}

	return ret;
}

int isp_k_cfg_rgb_ltm(struct isp_io_param *param,
		struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	uint32_t idx = isp_k_param->cfg_id;

	switch (param->property) {
	case ISP_PRO_RGB_LTM_BLOCK:
	case ISP_PRO_RGB_LTM_CAP_PARAM:
		ret = isp_k_ltm_rgb_block(param, isp_k_param, idx);
		break;
	default:
		pr_err("fail to support cmd id = %d\n", param->property);
		break;
	}

	return ret;
}

int isp_k_cfg_yuv_ltm(struct isp_io_param *param,
		struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	uint32_t idx = isp_k_param->cfg_id;

	switch (param->property) {
	case ISP_PRO_YUV_LTM_BLOCK:
		ret = isp_k_ltm_yuv_block(param, isp_k_param, idx);
		break;
	default:
		pr_err("fail to support cmd id = %d\n",
			param->property);
		break;
	}

	return ret;
}

int isp_k_cpy_yuv_ltm(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if(isp_k_param->ltm_yuv_info.isupdate == 1) {
		memcpy(&param_block->ltm_yuv_info, &isp_k_param->ltm_yuv_info, sizeof(struct isp_dev_yuv_ltm_info));
		isp_k_param->ltm_yuv_info.isupdate = 0;
		param_block->ltm_yuv_info.isupdate = 1;
	}

	return ret;
}

int isp_k_cpy_rgb_ltm(struct dcam_isp_k_block *param_block, struct dcam_isp_k_block *isp_k_param)
{
	int ret = 0;
	if (isp_k_param->ltm_rgb_info.isupdate == 1) {
		memcpy(param_block->ltm_map_info,
			isp_k_param->ltm_map_info,
			isp_k_param->ltm_rgb_info.ltm_map.ltm_map_size);
		memcpy(&param_block->ltm_rgb_info,
			&isp_k_param->ltm_rgb_info, sizeof(struct isp_dev_rgb_ltm_info));
		isp_k_param->ltm_rgb_info.isupdate = 0;
		param_block->ltm_rgb_info.isupdate = 1;
	}

	return ret;
}
