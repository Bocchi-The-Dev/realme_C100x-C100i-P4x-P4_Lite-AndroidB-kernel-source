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

#include "dcam_core.h"
#include "dcam_reg.h"
#include "cam_debugger.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "PDAF: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void write_pd_table(struct pdaf_ppi_info *pdaf_info, enum dcam_id idx)
{
	int i = 0;
	int line_start_num = 0, inner_linenum = 0, line_offset = 0;
	int col = 0, row = 0, is_pd = 0, is_right = 0, bit_start = 0;
	int block_size = 16;
	uint32_t pdafinfo[256] = {0};

	switch (pdaf_info->block_size.height) {
	case 0:
		block_size = 8;
		break;
	case 1:
		block_size = 16;
		break;
	case 2:
		block_size = 32;
		break;
	case 3:
		block_size = 64;
		break;
	default:
		pr_err("fail to check pd_block_num\n");
		break;
	}

	for (i = 0; i < pdaf_info->pd_pos_size * 2; i++) {
		col = pdaf_info->pattern_pixel_col[i];
		row = pdaf_info->pattern_pixel_row[i];
		is_right = pdaf_info->pattern_pixel_is_right[i] & 0x01;
		is_pd = 1;
		line_start_num = row * (block_size / 16);
		inner_linenum = (col / 16);
		line_offset = (col % 16);
		bit_start = 30 - 2 * line_offset;
		pdafinfo[line_start_num + inner_linenum] |=
				((is_pd | (is_right << 1)) << bit_start);
	}

	for (i = 0; i < 256; i++)
		DCAM_REG_WR(idx, DCAM0_PDAF_EXTR_POS + (i * 4), pdafinfo[i]);
}

static int pd_info_to_ppi_info(struct isp_dev_pdaf_info *pdaf_info,
		struct pdaf_ppi_info *ppi_info)
{
	if (!pdaf_info || !ppi_info)
		return -1;
	memcpy(&ppi_info->pattern_pixel_is_right[0],
			&pdaf_info->pattern_pixel_is_right[0],
			sizeof(pdaf_info->pattern_pixel_is_right));
	memcpy(&ppi_info->pattern_pixel_row[0],
			&pdaf_info->pattern_pixel_row[0],
			sizeof(pdaf_info->pattern_pixel_row));
	memcpy(&ppi_info->pattern_pixel_col[0],
			&pdaf_info->pattern_pixel_col[0],
			sizeof(pdaf_info->pattern_pixel_col));
	return 0;
}

static int isp_k_pdaf_type1_block(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	uint32_t idx = p->idx;
	struct dev_dcam_vc2_control *vch2_info = &p->pdaf.vch2_info;

	ret = copy_from_user((void *)vch2_info,
			param->property_param,
			sizeof(struct dev_dcam_vc2_control));

	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}
	if (idx == DCAM_HW_CONTEXT_MAX)
		return 0;

	DCAM_REG_WR(idx, DCAM_PDAF_CONTROL,
		(vch2_info->vch2_vc & 0x03) << 16
		|(vch2_info->vch2_data_type & 0x3f) << 8
		|(vch2_info->vch2_mode & 0x03));

	return ret;
}

static int isp_k_pdaf_type2_block(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	uint32_t idx = p->idx;
	struct dev_dcam_vc2_control *vch2_info = &p->pdaf.vch2_info;

	ret = copy_from_user((void *)vch2_info,
			param->property_param,
			sizeof(struct dev_dcam_vc2_control));

	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}
	if (idx == DCAM_HW_CONTEXT_MAX)
		return 0;

	DCAM_REG_WR(idx, DCAM_PDAF_CONTROL,
		(vch2_info->vch2_vc & 0x03) << 16
		|(vch2_info->vch2_data_type & 0x3f) << 8
		|(vch2_info->vch2_mode & 0x03));

	return ret;
}

static int isp_k_pdaf_type3_block(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	uint32_t idx = p->idx;
	struct dev_dcam_vc2_control *vch2_info = &p->pdaf.vch2_info;

	ret = copy_from_user((void *)vch2_info,
			param->property_param,
			sizeof(struct dev_dcam_vc2_control));

	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}
	pr_info("pdaf_info.vch2_mode = %d\n", vch2_info->vch2_mode);

	if (idx == DCAM_HW_CONTEXT_MAX)
		return 0;

	DCAM_REG_MWR(idx, DCAM_CFG, BIT_4, 0x1 << 4);
	DCAM_REG_WR(idx, DCAM_PDAF_CONTROL,
		(vch2_info->vch2_vc & 0x03) << 16
		|(vch2_info->vch2_data_type & 0x3f) << 8
		|(vch2_info->vch2_mode & 0x03));

	DCAM_REG_MWR(idx, DCAM_VC2_CONTROL, BIT_1 | BIT_0, 0);

	return ret;
}

static int isp_k_dual_pdaf_block(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	uint32_t idx = p->idx;
	struct dev_dcam_vc2_control *vch2_info = &p->pdaf.vch2_info;

	ret = copy_from_user((void *)vch2_info,
			param->property_param,
			sizeof(struct dev_dcam_vc2_control));

	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}
	if (idx == DCAM_HW_CONTEXT_MAX)
		return 0;

	DCAM_REG_MWR(idx, DCAM_CFG, BIT_4, 0x1 << 4);
	DCAM_REG_WR(idx, DCAM_PDAF_CONTROL,
		(vch2_info->vch2_vc & 0x03) << 16
		|(vch2_info->vch2_data_type & 0x3f) << 8
		|(vch2_info->vch2_mode & 0x03));

	return ret;
}

static int isp_k_pdaf_type3_set_info(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	unsigned int val = 0;
	uint32_t idx = p->idx;
	struct isp_dev_pdaf_info *pdaf_info = &p->pdaf.pdaf_info;
	struct pdaf_ppi_info ppi_info;

	memset(pdaf_info, 0x00, sizeof(struct isp_dev_pdaf_info));
	memset(&ppi_info, 0x00, sizeof(ppi_info));
	ret = copy_from_user((void *)pdaf_info,
		param->property_param, sizeof(struct isp_dev_pdaf_info));
	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}
	if (idx == DCAM_HW_CONTEXT_MAX)
		return 0;
	val = !pdaf_info->bypass;

	val = ((pdaf_info->block_size.height & 0x3) << 3) |
		((pdaf_info->block_size.width & 0x3) << 1);
	DCAM_REG_MWR(idx, DCAM_PDAF_EXTR_CTRL, 0x1E, val);

	val = ((pdaf_info->win.start_y & 0x1FFF) << 13) |
		(pdaf_info->win.start_x & 0x1FFF);
	DCAM_REG_WR(idx, DCAM_PDAF_EXTR_ROI_ST, val);

	val = (((pdaf_info->win.end_y - pdaf_info->win.start_y) & 0x1FFF) << 13) |
		((pdaf_info->win.end_x - pdaf_info->win.start_x) & 0x1FFF);
	DCAM_REG_WR(idx, DCAM_PDAF_EXTR_ROI_SIZE, val);

	ret = pd_info_to_ppi_info(pdaf_info, &ppi_info);
	if (!ret)
		write_pd_table(&ppi_info, idx);

	DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, 0xf0,
		pdaf_info->skip_num << 4);
	DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, BIT_2,
		pdaf_info->mode_sel << 2);

	if (pdaf_info->mode_sel)
		DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, BIT_3,
			(0x1 << 3));
	else
		DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM1, BIT_0, 0x1);

	return ret;
}

static int isp_k_pdaf_bypass(
	struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	unsigned int bypass = 0;

	ret = copy_from_user((void *)&bypass,
		param->property_param, sizeof(unsigned int));
	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}

	bypass = !!bypass;
	p->pdaf.bypass = bypass;
	pr_info("pdaf bypass %d\n", bypass);

	return ret;
}

static int isp_k_pdaf_type3_set_ppi_info(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	unsigned int val = 0;
	uint32_t idx = p->idx;
	struct pdaf_ppi_info *pdaf_info = &p->pdaf.ppi_info;

	ret = copy_from_user((void *)pdaf_info,
			param->property_param,
			sizeof(struct pdaf_ppi_info));

	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}
	if (idx == DCAM_HW_CONTEXT_MAX)
		return 0;

	val = ((pdaf_info->block_size.height & 0x3) << 3)
		| ((pdaf_info->block_size.width & 0x3) << 1);
	DCAM_REG_MWR(idx, DCAM_PDAF_EXTR_CTRL, 0x1E, val);
	write_pd_table(pdaf_info, idx);

	return ret;
}

static int isp_k_pdaf_type3_set_roi(struct isp_io_param *param, struct dcam_isp_k_block *p)
{

	int ret = 0;
	unsigned int val = 0;
	uint32_t idx = p->idx;
	struct pdaf_roi_info *roi_info = &p->pdaf.roi_info;

	ret = copy_from_user((void *)roi_info,
			param->property_param,
			sizeof(struct pdaf_roi_info));

	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}

	if (idx == DCAM_HW_CONTEXT_MAX)
		return 0;

	val = ((roi_info->win.start_y & 0x1FFF) << 13) |
		(roi_info->win.start_x & 0x1FFF);
	DCAM_REG_WR(idx, DCAM_PDAF_EXTR_ROI_ST, val);

	val = (((roi_info->win.end_y - roi_info->win.start_y) & 0x1FFF) << 13) |
		((roi_info->win.end_x - roi_info->win.start_x) & 0x1FFF);
	DCAM_REG_WR(idx, DCAM_PDAF_EXTR_ROI_SIZE, val);

	DCAM_REG_MWR(idx, DCAM_CONTROL, BIT_14, 0x1 << 14);
	DCAM_REG_MWR(idx, DCAM_CONTROL, BIT_16, 0x1 << 16);

	return ret;
}

static int isp_k_pdaf_type3_set_skip_num(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	unsigned int skip_num = 0;
	uint32_t idx = p->idx;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	ret = copy_from_user((void *)&skip_num,
			param->property_param, sizeof(skip_num));
	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}

	if (p->idx == DCAM_HW_CONTEXT_MAX)
		return 0;
	dev = p->dev;
	hw_ctx = &dev->hw_ctx[idx];

	if (p->is_high_fps)
		skip_num = hw_ctx->slowmotion_count - 1;
	p->pdaf.skip_num = skip_num;

	DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, 0xf0, skip_num << 4);
	DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM1, BIT_1, BIT_1);
	dcam_online_port_skip_num_set(p->dev, idx, DCAM_PATH_PDAF, skip_num);

	return ret;
}

static int isp_k_pdaf_type3_set_mode(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	uint32_t mode = 0;
	uint32_t idx = p->idx;

	ret = copy_from_user((void *)&mode,
			param->property_param, sizeof(mode));
	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		return -1;
	}

	/* single/multi mode */
	mode = !!mode;
	p->pdaf.mode = mode;

	if (idx == DCAM_HW_CONTEXT_MAX)
		return 0;

	DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, BIT_2, mode << 2);

	if (mode)
		DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, BIT_3, (0x1 << 3));
	else
		DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM1, BIT_0, 0x1);

	return ret;
}

int dcam_k_cfg_pdaf(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	enum dcam_id idx;

	if (!param || !p) {
		pr_err("fail to get param\n");
		return -1;
	}

	if (param->property_param == NULL) {
		pr_err("fail to get property_param\n");
		return -1;
	}

	idx = p->idx;

	switch (param->property) {
	case DCAM_PDAF_BYPASS:
		ret = isp_k_pdaf_bypass(param, p);
		break;
	case DCAM_PDAF_TYPE3_SET_INFO:
		ret = isp_k_pdaf_type3_set_info(param, p);
		break;
	case DCAM_PDAF_TYPE3_SET_MODE:
		ret = isp_k_pdaf_type3_set_mode(param, p);
		break;
	case DCAM_PDAF_TYPE3_SET_PPI_INFO:
		ret = isp_k_pdaf_type3_set_ppi_info(param, p);
		break;
	case DCAM_PDAF_TYPE3_SET_SKIP_NUM:
		ret = isp_k_pdaf_type3_set_skip_num(param, p);
		break;
	case DCAM_PDAF_TYPE3_SET_ROI:
		ret = isp_k_pdaf_type3_set_roi(param, p);
		break;
	case DCAM_PDAF_TYPE1_BLOCK:
		p->pdaf.pdaf_type = DCAM_PDAF_TYPE1;
		ret = isp_k_pdaf_type1_block(param, p);
		break;
	case DCAM_PDAF_TYPE2_BLOCK:
		p->pdaf.pdaf_type = DCAM_PDAF_TYPE2;
		ret = isp_k_pdaf_type2_block(param, p);
		break;
	case DCAM_PDAF_TYPE3_BLOCK:
		p->pdaf.pdaf_type = DCAM_PDAF_TYPE3;
		ret = isp_k_pdaf_type3_block(param, p);
		break;
	case DCAM_DUAL_PDAF_BLOCK:
		p->pdaf.pdaf_type = DCAM_PDAF_DUAL;
		ret = isp_k_dual_pdaf_block(param, p);
		break;
	default:
		pr_err("fail to support cmd id = %d\n", param->property);
		break;
	}
	pr_info("idx %d, Sub %d, ret %d\n", idx, param->property, ret);

	return ret;
}

static void dcam_k_pdaf_param_dump(struct dcam_dev_pdaf_param *pdaf_info,
		struct cam_debug_cfg *debug)
{
	int i = 0;
	uint32_t pdaf_type = DCAM_PDAF_PPE_PPC_PARAM;
	struct cam_debug_log_ctx *log_ctx = NULL;
	struct pdaf_roi_info *roi_info = NULL;
	struct pdaf_ppi_info *ppi_info = NULL;
	struct dev_dcam_vc2_control *vch2_info = NULL;
	struct dcam_ppe_ppc_info *ppe_ppc_info = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	if (!debug->block_param)
		return;

	log_ctx = &debug->block_param_log;
	pdaf_type = pdaf_info->pdaf_type;
	vch2_info = &pdaf_info->vch2_info;
	ppi_info = &pdaf_info->ppi_info;
	roi_info = &pdaf_info->roi_info;
	ppe_ppc_info = &pdaf_info->ppe_ppc_info;

	CAM_DEBUG_LOG_WRITE(log_ctx, "PDAF: bypass %d update %d pdaf_type %d\n",
		pdaf_info->bypass, pdaf_info->update_flag, pdaf_type);

	switch (pdaf_type) {
	case DCAM_PDAF_DUAL:
	case DCAM_PDAF_TYPE1:
		CAM_DEBUG_LOG_WRITE(log_ctx, "vch2_vc %d, vch2_data_type %d, vch2_mode %d\n",
			vch2_info->vch2_vc, vch2_info->vch2_data_type, vch2_info->vch2_mode);
		break;
	case DCAM_PDAF_TYPE2:
		CAM_DEBUG_LOG_WRITE(log_ctx, "vch2_vc %d, vch2_data_type %d, vch2_mode %d\n",
			vch2_info->vch2_vc, vch2_info->vch2_data_type, vch2_info->vch2_mode);
		break;
	case DCAM_PDAF_TYPE3:
		CAM_DEBUG_LOG_WRITE(log_ctx, "vch2_vc %d, vch2_data_type %d, vch2_mode %d\n",
			vch2_info->vch2_vc, vch2_info->vch2_data_type, vch2_info->vch2_mode);
		CAM_DEBUG_LOG_WRITE(log_ctx, "mode %d, skip_num %d, corr_en %d, grid_mode %d\n",
			pdaf_info->mode, pdaf_info->skip_num,
			pdaf_info->pdaf_info.phase_map_corr_en, pdaf_info->pdaf_info.grid_mode);
		CAM_DEBUG_LOG_WRITE(log_ctx, "roi: st_x %d end_x %d st_y %d end_y %d\n",
			roi_info->win.start_x, roi_info->win.end_x,
			roi_info->win.start_y, roi_info->win.end_y);
		CAM_DEBUG_LOG_WRITE(log_ctx, "ppi block: (%d %d %d %d) size (%d %d)\n",
			ppi_info->block.start_x, ppi_info->block.end_x,
			ppi_info->block.start_y, ppi_info->block.end_y,
			ppi_info->block_size.width, ppi_info->block_size.height);
		CAM_DEBUG_LOG_WRITE(log_ctx, "ppe: ppi_corr_en %d, ppi_bypass %d\n",
			ppe_ppc_info->ppi_phase_map_corr_en,
			ppe_ppc_info->ppi_bypass);
		for (i = 0; i < PDAF_PPI_GAIN_MAP_LEN / 4; i = i + 4) {
			CAM_DEBUG_LOG_WRITE(log_ctx, "i %d map l: %d %d %d %d r: %d %d %d %d\n",
				i, ppe_ppc_info->ppc_l_gain_map[i],
				ppe_ppc_info->ppc_l_gain_map[i + 1],
				ppe_ppc_info->ppc_l_gain_map[i + 2],
				ppe_ppc_info->ppc_l_gain_map[i + 3],
				ppe_ppc_info->ppc_r_gain_map[i],
				ppe_ppc_info->ppc_r_gain_map[i + 1],
				ppe_ppc_info->ppc_r_gain_map[i + 2],
				ppe_ppc_info->ppc_r_gain_map[i + 3]);
		}
		break;
	default:
		break;
	}
}

int dcam_k_pdaf(struct dcam_isp_k_block *p)
{
	uint32_t val = 0;
	uint32_t idx = p->idx;
	uint32_t bypass = p->pdaf.bypass;
	uint32_t mode = p->pdaf.mode;
	uint32_t skip_num = p->pdaf.skip_num;
	struct dev_dcam_vc2_control *vch2_info = &p->pdaf.vch2_info;
	struct pdaf_roi_info *roi_info = &p->pdaf.roi_info;
	struct pdaf_ppi_info *ppi_info = &p->pdaf.ppi_info;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (bypass) {
		pr_debug("dcam%d pdaf bypass\n", idx);
		return 0;
	}

	dev = p->dev;
	if (idx >= DCAM_HW_CONTEXT_MAX || dev == NULL) {
		pr_err("fail to get valid param dcam idx:%d, dev %p.\n", idx, dev);
		return 0;
	}

	hw_ctx = &dev->hw_ctx[idx];
	if (hw_ctx == NULL) {
		pr_err("fail to get hw_ctx\n");
		return -EPERM;
	}
	if (g_dcam_block_dump & (1 << _E_PDAF)) {
		hw_ctx->debug.idx = idx;
		hw_ctx->debug.fid = hw_ctx->fid;
		hw_ctx->debug.block_param = 1;
	}
	dcam_k_pdaf_param_dump(&p->pdaf, &hw_ctx->debug);

	pr_info("dcam%d reconfigure pdaf param, type %d\n", idx, p->pdaf.pdaf_type);

	DCAM_REG_WR(idx, DCAM_PDAF_CONTROL,
		(vch2_info->vch2_vc & 0x03) << 16
		|(vch2_info->vch2_data_type & 0x3f) << 8
		|(vch2_info->vch2_mode & 0x03));

	if (p->pdaf.pdaf_type == DCAM_PDAF_TYPE3) {
		/* mode */
		DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, BIT_2, mode << 2);

		if (mode)
			DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, BIT_3, (0x1 << 3));
		else
			DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM1, BIT_0, 0x1);

		/* skip num */
		if (hw_ctx->slowmotion_count) {
			p->pdaf.skip_num = hw_ctx->slowmotion_count - 1;
			skip_num = p->pdaf.skip_num;
		}

		DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM, 0xf0, skip_num << 4);
		DCAM_REG_MWR(idx, DCAM_PDAF_SKIP_FRM1, BIT_1, BIT_1);
		dcam_online_port_skip_num_set(p->dev, idx, DCAM_PATH_PDAF, skip_num);

		/* pdaf type */
		DCAM_REG_MWR(idx, DCAM_CFG, BIT_4, 0x1 << 4);
		DCAM_REG_MWR(idx, DCAM_VC2_CONTROL, BIT_1 | BIT_0, 0);

		/* pdaf roi */
		val = ((roi_info->win.start_y & 0x1FFF) << 13) |
			(roi_info->win.start_x & 0x1FFF);
		DCAM_REG_WR(idx, DCAM_PDAF_EXTR_ROI_ST, val);

		DCAM_REG_MWR(idx, DCAM_CFG, BIT_3, 0x1 << 3);
		DCAM_REG_MWR(idx, DCAM_CFG, BIT_4, 0x1 << 4);
		val = (((roi_info->win.end_y - roi_info->win.start_y) & 0x1FFF) << 13) |
			((roi_info->win.end_x - roi_info->win.start_x) & 0x1FFF);
		DCAM_REG_WR(idx, DCAM_PDAF_EXTR_ROI_SIZE, val);
		/*ppi info*/
		val = ((ppi_info->block_size.height & 0x3) << 3)
			| ((ppi_info->block_size.width & 0x3) << 1);
			DCAM_REG_MWR(idx, DCAM_PDAF_EXTR_CTRL, 0x1E, val);
		write_pd_table(ppi_info, idx);

		DCAM_REG_MWR(idx, DCAM_PDAF_CONTROL, BIT_1 | BIT_0, 0x3);
		DCAM_REG_MWR(idx, DCAM_VC2_CONTROL, BIT_1 | BIT_0, 0);

		DCAM_REG_MWR(idx, DCAM_CONTROL, BIT_14, 0x1 << 14);
		DCAM_REG_MWR(idx, DCAM_CONTROL, BIT_16, 0x1 << 16);
	}

	return 0;
}
