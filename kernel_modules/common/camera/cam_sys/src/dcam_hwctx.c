// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022-2023 Unisoc(Shanghai) Technologies Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "cam_offline_statis.h"
#include "cam_pipeline.h"
#include "cam_zoom.h"
#include "cam_debugger.h"
#include "dcam_dummy.h"
#include "dcam_slice.h"
#include "dcam_hwctx.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_HWCTX: %d %d %s : " fmt, current->pid, __LINE__, __func__

static int dcamhwctx_slice_fetch_param_get(struct dcam_hw_context *hw_ctx,
		struct dcam_fetch_info *fetch, struct dcam_hw_slice_param *slicearg)
{
	int ret = 0;
	uint32_t data_byte = 0, fetch_offset = 0;
	struct dcam_offline_slice_info *slice = NULL;

	slice = &hw_ctx->slice_info;
	data_byte = cam_byte_get(fetch->fmt);

	if ((slice->slice_count <= slice->w_slice_num)
		&& (slice->w_slice_num != slice->slice_num))
		fetch_offset = fetch->size.w * slice->cur_slice->start_y * data_byte / 4;

	if (slice->slice_count % slice->w_slice_num == 0) {
		slicearg->is_last_slice = 0;
		slicearg->fetch_offset = fetch_offset;
		slicearg->fetch_size.w = slice->cur_slice->size_x + DCAM_OVERLAP;
		slicearg->fetch_size.h = slice->cur_slice->size_y;
	} else if (slice->slice_count % slice->w_slice_num == 1) {
		slicearg->is_last_slice = 1;
		slicearg->fetch_offset =
			(slice->cur_slice->start_x - DCAM_OVERLAP) *
			data_byte / 4 + fetch_offset;
		slicearg->fetch_size.w = slice->cur_slice->size_x + DCAM_OVERLAP;
		slicearg->fetch_size.h = slice->cur_slice->size_y;
	} else {
		slicearg->is_last_slice = 0;
		slicearg->fetch_offset =
			(slice->cur_slice->start_x - DCAM_OVERLAP) *
			data_byte / 4 + fetch_offset;
		slicearg->fetch_size.w = slice->cur_slice->size_x + DCAM_OVERLAP * 2;
		slicearg->fetch_size.h = slice->cur_slice->size_y;
	}
	pr_info("fetch fmt %d cur_slice %d, %d, %d, %d, fetch_size %d, %d, offset %d\n",
		fetch->fmt, slice->cur_slice->start_x, slice->cur_slice->start_y,
		slice->cur_slice->size_x, slice->cur_slice->size_y,
		slicearg->fetch_size.w, slicearg->fetch_size.h, slicearg->fetch_offset);

	return ret;
}

static int dcamhwctx_slice_store_param_get(
	struct dcam_hw_context *hw_ctx,
	struct dcam_hw_slice_param *slicearg,
	struct dcam_hw_scaler *scaler_p,
	struct dcam_slice_store_param *store_p,
	uint32_t ratio)
{
	int ret = 0;
	uint32_t store_offset[2] = {0};
	uint32_t data_byte = 0, out_fmt = 0, out_w = 0;
	struct dcam_offline_slice_info *slice = NULL;
	struct img_trim in_trim = {0};
	uint32_t relative_offset = 0, trim_y_coef = 0;

	if (slicearg->path_id > DCAM_PATH_RAW)
		return 0;
	relative_offset = hw_ctx->relative_offset[slicearg->path_id];
	slicearg->relative_offset = relative_offset;
	slice = &hw_ctx->slice_info;
	out_fmt = hw_ctx->hw_path[slicearg->path_id].hw_start.out_fmt;
	out_w = hw_ctx->hw_path[slicearg->path_id].hw_size.out_size.w;
	in_trim = hw_ctx->hw_path[slicearg->path_id].hw_size.in_trim;
	data_byte = cam_byte_get(out_fmt);
	store_p->pitch = cam_cal_hw_pitch(out_w, out_fmt);
	pr_debug("path %d, out_fmt %d, w %d, in_trim %d, %d, %d, %d\n", slicearg->path_id, out_fmt,
		out_w, in_trim.start_x, in_trim.start_y, in_trim.size_x, in_trim.size_y);

	if (slice->w_slice_num == slice->slice_num) {
		scaler_p->im_trim0.start_y = in_trim.start_y;
		trim_y_coef = 2;
	} else if ((slice->slice_count > slice->w_slice_num)
		&& (slice->w_slice_num != slice->slice_num)) {
		scaler_p->im_trim0.start_y = in_trim.start_y;
		trim_y_coef = 1;
	} else {
		scaler_p->im_trim0.start_y = 0;
		trim_y_coef = 1;
		store_offset[0] = cal_sprd_pitch(out_w, out_fmt) * slice->cur_slice->start_y / ratio
					- (relative_offset - in_trim.start_x) / ratio
					* data_byte / 4;
		store_offset[1] = cal_sprd_pitch(out_w, out_fmt) *
			slice->cur_slice->start_y / ratio / 2 -
			(relative_offset - in_trim.start_x) /
			ratio * data_byte / 4;
	}

	scaler_p->src_size.w = slicearg->fetch_size.w;
	scaler_p->src_size.h = slicearg->fetch_size.h;
	if (slice->slice_count % slice->w_slice_num == 0) {
		scaler_p->im_trim0.start_x = in_trim.start_x;
		scaler_p->im_trim0.size_x = scaler_p->src_size.w - in_trim.start_x;
		if (in_trim.start_x + in_trim.size_x < slice->cur_slice->size_x)
			scaler_p->im_trim0.size_x = in_trim.size_x + DCAM_OVERLAP;
		scaler_p->im_trim0.size_y = scaler_p->src_size.h - in_trim.start_y * trim_y_coef;
		scaler_p->dst_size.w = scaler_p->im_trim0.size_x / ratio;
		scaler_p->dst_size.h = scaler_p->im_trim0.size_y / ratio;
		store_p->store_size.w = scaler_p->dst_size.w - DCAM_OVERLAP / ratio;
		store_p->store_size.h = scaler_p->dst_size.h;
		if (store_p->store_size.w % 4) {
			store_p->store_size.w = store_p->store_size.w / 4 * 4;
			scaler_p->dst_size.w = store_p->store_size.w + DCAM_OVERLAP / ratio;
		}
		store_p->store_offset[0] = store_offset[0];
		store_p->store_offset[1] = store_offset[1];
		store_p->crop.start_x = in_trim.start_x;
		store_p->crop.start_y = in_trim.start_y;
		store_p->crop.size_x = store_p->store_size.w * ratio;
		store_p->crop.size_y = store_p->store_size.h * ratio;
		store_p->out_border.left_border = 0;
		store_p->out_border.right_border = DCAM_OVERLAP / ratio;
		store_p->fbc_border.left_border = 0;
		hw_ctx->relative_offset[slicearg->path_id] = in_trim.start_x;
	} else if (slice->slice_count % slice->w_slice_num == 1) {
		scaler_p->im_trim0.start_x = 0;
		scaler_p->im_trim0.start_y = in_trim.start_y;
		scaler_p->im_trim0.size_x = scaler_p->src_size.w - in_trim.start_x;
		scaler_p->im_trim0.size_y = scaler_p->src_size.h - in_trim.start_y * trim_y_coef;
		scaler_p->dst_size.w = scaler_p->im_trim0.size_x / ratio;
		scaler_p->dst_size.h = scaler_p->im_trim0.size_y / ratio;
		store_p->store_size.w = scaler_p->dst_size.w - DCAM_OVERLAP / ratio;
		store_p->store_size.h = scaler_p->dst_size.h;
		if (store_p->store_size.w % 4) {
			store_p->store_size.w = store_p->store_size.w / 4 * 4;
			scaler_p->dst_size.w = store_p->store_size.w + DCAM_OVERLAP / ratio;
		}
		store_p->store_offset[0] = store_p->store_offset[1] =
			(slice->cur_slice->start_x - relative_offset) / ratio * data_byte / 4;
		store_p->crop.start_x = DCAM_OVERLAP;
		store_p->crop.start_y = in_trim.start_y;
		store_p->crop.size_x = store_p->store_size.w * ratio;
		store_p->crop.size_y = store_p->store_size.h * ratio;
		store_p->out_border.left_border = DCAM_OVERLAP / ratio;
		store_p->out_border.right_border = 0;
		store_p->fbc_border.left_border = DCAM_OVERLAP / ratio;
		hw_ctx->relative_offset[slicearg->path_id] = slice->cur_slice->start_x;
	} else {
		scaler_p->im_trim0.start_x = 0;
		scaler_p->im_trim0.size_x = scaler_p->src_size.w;
		scaler_p->im_trim0.size_y = scaler_p->src_size.h - in_trim.start_y * trim_y_coef;
		scaler_p->dst_size.w = scaler_p->im_trim0.size_x / ratio;
		scaler_p->dst_size.h = scaler_p->im_trim0.size_y / ratio;
		store_p->store_size.w = scaler_p->dst_size.w - DCAM_OVERLAP * 2 / ratio;
		store_p->store_size.h = scaler_p->dst_size.h;
		store_p->store_offset[0] = store_p->store_offset[1] =
			(slice->cur_slice->start_x - relative_offset) / ratio * data_byte / 4;
		store_p->crop.start_x = DCAM_OVERLAP;
		store_p->crop.size_x = store_p->store_size.w * ratio;
		store_p->crop.size_y = store_p->store_size.h * ratio;
		store_p->out_border.left_border = DCAM_OVERLAP / ratio;
		store_p->out_border.right_border = DCAM_OVERLAP / ratio;
		store_p->fbc_border.left_border = DCAM_OVERLAP / ratio;
		hw_ctx->relative_offset[slicearg->path_id] = slice->cur_slice->start_x;
	}
	scaler_p->im_trim1.start_x = 0;
	scaler_p->im_trim1.start_y = 0;
	scaler_p->im_trim1.size_x = scaler_p->dst_size.w;
	scaler_p->im_trim1.size_y = scaler_p->dst_size.h;

	pr_debug("slice %d/%d, src %d, %d, trim0 %d, %d, %d, %d, trim1 %d, %d, %d, %d\n",
		(slice->slice_num - slice->slice_count), slice->slice_num,
		scaler_p->src_size.w, scaler_p->src_size.h,
		scaler_p->im_trim0.start_x, scaler_p->im_trim0.start_y,
		scaler_p->im_trim0.size_x, scaler_p->im_trim0.size_y,
		scaler_p->im_trim1.start_x, scaler_p->im_trim1.start_y,
		scaler_p->im_trim1.size_x, scaler_p->im_trim1.size_y);

	pr_debug("dst size %d, %d, store size %d, %d, store_offset %d relative_offset %d\n",
		scaler_p->dst_size.w, scaler_p->dst_size.h,
		store_p->store_size.w, store_p->store_size.h,
		store_p->store_offset[0], relative_offset);

	return ret;
}

int dcam_hwctx_offline_reset(struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;

	hw = hw_ctx->hw;
	hw_ctx->fid = 0;
	ret = hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_RESET, &hw_ctx->hw_ctx_id);
	if (ret)
		pr_err("fail to reset dcam%d\n", hw_ctx->hw_ctx_id);
	return ret;
}

void dcam_hwctx_nr3_store_addr(struct dcam_hw_context *hw_ctx, struct cam_frame *frame)
{
	struct dcam_hw_cfg_store_addr store_arg = {0};
	struct cam_hw_info *hw = NULL;

	hw = hw_ctx->hw;
	store_arg.idx = hw_ctx->hw_ctx_id;
	store_arg.frame_addr[0] = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
	store_arg.path_id = DCAM_PATH_3DNR;
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_STORE_ADDR, &store_arg);
}

void dcam_hwctx_binning_4in1_set(struct dcam_hw_context *hw_ctx)
{
	struct cam_hw_info *hw = NULL;

	hw = hw_ctx->hw;
	hw_ctx->binning.binning_4in1_en = 0;
	hw_ctx->binning.idx = hw_ctx->hw_ctx_id;
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_BINNING_4IN1_SET, &hw_ctx->binning);
}

void dcam_hwctx_block_set(struct dcam_hw_context *hw_ctx)
{
	struct cam_hw_info *hw = NULL;

	hw = hw_ctx->hw;
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_BLOCKS_SETSTATIS, hw_ctx->blk_pm);
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_BLOCKS_SETALL, hw_ctx->blk_pm);
}

void dcam_hwctx_debug_init(struct dcam_hw_context *hw_ctx)
{
	char filename[128] = {0};
	timespec timestamp = {0};

	if (hw_ctx->debug.block_param_log.log_file)
		return;

	os_adapt_time_get_ts(&timestamp);

	snprintf(filename, sizeof(filename), "/data/ylog/dcam%d_block_param_%lld.%lld.txt", hw_ctx->hw_ctx_id,
			timestamp.tv_sec, (long long)(timestamp.tv_nsec / NSEC_PER_USEC));
	hw_ctx->debug.block_param_log.ip_info = IP_DCAM;

	cam_debugger_logfile_init(&hw_ctx->debug.block_param_log, filename);
}

void dcam_hwctx_frame_param_set(struct dcam_hw_context *hw_ctx)
{
	int i = 0;
	struct cam_hw_info *hw = NULL;

	hw = hw_ctx->hw;
	for (i = 0; i < DCAM_PATH_MAX; i++) {
		if (!hw_ctx->hw_path[i].need_update)
			continue;
		pr_debug("dcam path id %d update, compress_en %d\n",
			i, hw_ctx->hw_path[i].hw_fbc.compress_en);
		if (hw_ctx->hw_path[i].hw_fbc.compress_en)
			hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id,
				DCAM_HW_CFG_FBC_ADDR_SET, &hw_ctx->hw_path[i].hw_fbc_store);
		else
			hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id,
				DCAM_HW_CFG_STORE_ADDR, &hw_ctx->hw_path[i].hw_store);

		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id,
			DCAM_HW_CFG_PATH_SIZE_UPDATE, &hw_ctx->hw_path[i].hw_size);
		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id,
			DCAM_HW_CFG_PATH_START, &hw_ctx->hw_path[i].hw_start);
		if (hw_ctx->hw_path[i].hw_fbc.compress_en)
			hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id,
			DCAM_HW_CFG_FBC_CTRL, &hw_ctx->hw_path[i].hw_fbc);
	}
}

int dcam_hwctx_fetch_set(struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;

	hw = hw_ctx->hw;
	ret = hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_FETCH_SET, &hw_ctx->hw_fetch);
	return ret;
}

void dcam_hwctx_slice_set(
	struct dcam_hw_context *hw_ctx,
	struct dcam_fetch_info *fetch,
	struct dcam_offline_slice_info *slice)
{
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_slice_param slicearg = {0};
	uint32_t ratio = 1;

	hw = hw_ctx->hw;
	slicearg.idx = hw_ctx->hw_ctx_id;
	slicearg.fetch = fetch;
	slicearg.cur_slice = slice->cur_slice;
	if (hw_ctx->slice_proc_mode == OFFLINE_SLICE_PROC) {
		slicearg.path_id = hw->ip_dcam[hw_ctx->hw_ctx_id]->dcamhw_abt->aux_dcam_path;
		slicearg.is_compress = hw_ctx->hw_path[slicearg.path_id].hw_fbc.compress_en;
	} else {
		slicearg.virtualsensor_pre_sof = 1;
		slicearg.path_id = hw->ip_dcam[1]->dcamhw_abt->aux_dcam_path;
	}

	dcamhwctx_slice_fetch_param_get(hw_ctx, fetch, &slicearg);
	dcamhwctx_slice_store_param_get(
		hw_ctx, &slicearg, &slicearg.full_scaler, &slicearg.full_store, 1);
	if (hw_ctx->offline_pre_en) {
		slicearg.bin_store.bin_en = hw_ctx->offline_pre_en;
		slicearg.path_id = DCAM_PATH_BIN;
		ratio = hw_ctx->hw_path[slicearg.path_id].hw_size.in_trim.size_x /
			hw_ctx->hw_path[slicearg.path_id].hw_size.out_size.w;
		dcamhwctx_slice_store_param_get(
			hw_ctx, &slicearg, &slicearg.bin_scaler, &slicearg.bin_store, ratio);
	}
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_SLICE_FETCH_SET, &slicearg);
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_SLICE_STORE_SET, &slicearg);
}

void dcam_hwctx_slice_force_copy(struct dcam_hw_context *hw_ctx, int x)
{
	uint32_t force_ids = DCAM_CTRL_ALL;
	struct dcam_hw_force_copy copyarg = {0};
	struct cam_hw_info *hw = NULL;
	struct cam_hw_reg_trace trace = {0};

	hw = hw_ctx->hw;
	copyarg.id = force_ids;
	copyarg.idx = hw_ctx->hw_ctx_id;
	copyarg.glb_reg_lock = hw_ctx->glb_reg_lock;
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_FORCE_COPY, &copyarg);
	os_adapt_time_udelay(500);
	if (x == 0) {
		trace.type = NORMAL_REG_TRACE;
		trace.idx = hw_ctx->hw_ctx_id;
		hw->isp_ioctl(hw, ISP_HW_CFG_REG_TRACE, &trace);
	}
}

void dcam_hwctx_cfg_fetch_start(struct cam_hw_info *hw)
{
	hw->dcam_ioctl(hw, 0, DCAM_HW_CFG_FETCH_START, hw);
}

int dcam_hwctx_slw_fmcu_set(struct dcam_hw_context *hw_ctx, uint32_t hw_ctx_id, int j)
{
	int ret = 0;
	struct dcam_fmcu_ctx_desc *fmcu;
	struct cam_hw_info *hw = NULL;

	hw = hw_ctx->hw;
	fmcu = hw_ctx->fmcu;
	if (!fmcu)
		pr_err("fail to check param fmcu%p\n", fmcu);
	hw_ctx->slw.fmcu_handle = fmcu;
	hw_ctx->slw.ctx_id = hw_ctx_id;
	hw_ctx->slw.slw_id = j;
	hw_ctx->slw.slw_cnt = hw_ctx->slowmotion_count;
	if ((hw_ctx->index_to_set == 0) && (j == 0))
		hw_ctx->slw.is_first_cycle = 1;
	else
		hw_ctx->slw.is_first_cycle = 0;
	ret = hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_SLW_FMCU_CMDS, &hw_ctx->slw);
	return ret;
}

void dcam_hwctx_update_path_size(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, uint32_t idx)
{
	struct dcam_hw_path_size path_size = {0};
	struct cam_hw_info *hw;

	hw = hw_ctx->hw;

	path_size.path_id = dcamonline_portid_convert_to_pathid(dcam_port->port_id);
	path_size.idx = idx;
	path_size.size_x = hw_ctx->cap_info.cap_size.size_x;
	path_size.size_y = hw_ctx->cap_info.cap_size.size_y;
	path_size.src_sel = dcam_port->src_sel;
	path_size.bin_ratio = dcam_port->bin_ratio;
	path_size.scaler_sel = dcam_port->scaler_sel;
	path_size.in_size = dcam_port->in_size;
	path_size.in_trim = dcam_port->in_trim;
	path_size.out_size = dcam_port->out_size;
	path_size.out_pitch = dcam_port->out_pitch;
	path_size.compress_info = frame->common.fbc_info;
	path_size.deci = dcam_port->deci;
	path_size.scaler_info = &dcam_port->scaler_info;
	if (dcam_port->port_id == PORT_BIN_OUT) {
		path_size.rds_coeff_size = dcam_port->rds_coeff_size;
		path_size.rds_coeff_buf = dcam_port->rds_coeff_buf;
		path_size.rds_init_phase_int0 = dcam_port->gphase.rds_init_phase_int0;
		path_size.rds_init_phase_int1 = dcam_port->gphase.rds_init_phase_int1;
		path_size.rds_init_phase_rdm0 = dcam_port->gphase.rds_init_phase_rdm0;
		path_size.rds_init_phase_rdm1 = dcam_port->gphase.rds_init_phase_rdm1;
	}
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_PATH_SIZE_UPDATE, &path_size);
}

int dcam_hwctx_pyr_dec_cfg(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, uint32_t idx)
{
	int ret = 0, i = 0;
	uint32_t align_w = 0, align_h = 0;
	uint32_t layer_num = 0;
	struct cam_hw_info *hw;
	struct dcam_hw_dec_store_cfg dec_store;
	struct dcam_hw_dec_online_cfg dec_online;

	if (!dcam_port || !frame || !hw_ctx) {
		pr_err("fail to check param, path%p, frame%p\n", dcam_port, frame);
		return -EINVAL;
	}
	hw = hw_ctx->hw;

	memset(&dec_store, 0, sizeof(struct dcam_hw_dec_store_cfg));
	memset(&dec_online, 0, sizeof(struct dcam_hw_dec_online_cfg));

	layer_num = dcam_port->dec_store_info.layer_num;
	dec_online.idx = idx;
	dec_online.layer_num = layer_num;
	dec_online.chksum_clr_mode = 0;
	dec_online.chksum_work_mode = 0;
	dec_online.path_sel = DACM_DEC_PATH_DEC;
	dec_online.hor_padding_num = dcam_port->dec_store_info.align_w - dcam_port->out_size.w;
	dec_online.ver_padding_num = dcam_port->dec_store_info.align_h - dcam_port->out_size.h;
	if (dec_online.hor_padding_num)
		dec_online.hor_padding_en = 1;
	if (dec_online.ver_padding_num)
		dec_online.ver_padding_en = 1;
	dec_online.flust_width = dcam_port->out_size.w;
	dec_online.flush_hblank_num = dec_online.hor_padding_num + 20;
	dec_online.flush_line_num = dec_online.ver_padding_num + 20;
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_DEC_ONLINE, &dec_online);

	dec_store.idx = idx;
	dec_store.bypass = 1;
	dec_store.endian = dcam_port->endian;
	dec_store.color_format = dcam_port->dcamout_fmt;
	dec_store.border_up = 0;
	dec_store.border_down = 0;
	dec_store.border_left = 0;
	dec_store.border_right = 0;
	/*because hw limit, pyr output 10bit*/
	dec_store.data_10b = 1;
	dec_store.flip_en = 0;
	dec_store.last_frm_en = 1;
	dec_store.mirror_en = 0;
	dec_store.mono_en = 0;
	dec_store.speed2x = 1;
	dec_store.rd_ctrl = 0;
	dec_store.store_res = 0;
	dec_store.burst_len = 1;

	pr_debug("dcam %d padding w %d h %d alignw %d h%d\n", idx,
		dec_online.hor_padding_num, dec_online.ver_padding_num, align_w, align_h);
	for (i = 0; i < layer_num; i++) {
		dec_store.bypass = 0;
		dec_store.cur_layer = i;
		dec_store.width = dcam_port->dec_store_info.size_t[i].w;
		dec_store.height = dcam_port->dec_store_info.size_t[i].h;
		dec_store.pitch[0] = dcam_port->dec_store_info.pitch_t[i].pitch_ch0;
		dec_store.pitch[1] = dcam_port->dec_store_info.pitch_t[i].pitch_ch1;
		/* when zoom, if necessary size update may set with path size udapte
		 * thus, the dec_store need remember on path or ctx, and calc & reg set
		 * need separate too, now just
		 */
		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id,
			DCAM_HW_CFG_DEC_SIZE_UPDATE, &dec_store);

		pr_debug("dcam %d dec_layer %d w %d h %d\n",
			idx, i, dec_store.width, dec_store.height);
	}

	return ret;
}

