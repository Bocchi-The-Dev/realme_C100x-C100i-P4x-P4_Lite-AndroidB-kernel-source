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
#include <linux/vmalloc.h>
#include "cam_pipeline.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "PYR_DEC_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void pyrdec_node_src_frame_ret(void *param)
{
	struct cam_frame *frame = NULL;
	struct pyr_dec_node *node = NULL;

	if (!param) {
		pr_err("fail to get input ptr.\n");
		return;
	}

	frame = (struct cam_frame *)param;
	node = (struct pyr_dec_node *)frame->common.priv_data;
	if (!node) {
		pr_err("fail to get pyrdec node.\n");
		return;
	}

	cam_buf_manager_buf_status_cfg(&frame->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_MAX);
	node->data_cb_func(CAM_CB_PYRDEC_RET_SRC_BUF, frame, node->data_cb_handle);
}

static uint32_t pyrdec_node_cal_pitch(uint32_t w, uint32_t format)
{
	uint32_t pitch = 0;

	switch (format) {
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
		pitch = w;
		break;
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
		pitch = (w * 10 + 127) / 128 * 128 / 8;
		break;
	case CAM_YVU420_2FRAME_10:
	case CAM_YUV420_2FRAME_10:
		pitch = (w * 16 + 127) / 128 * 128 / 8;
		break;
	default:
		pitch = w;
		pr_err("fail to support format %d w%d\n", format, w);
		break;
	}

	return pitch;
}

static int pyrdec_node_fetch_get(struct pyr_dec_node *node, uint32_t idx)
{
	int ret = 0, i = 0;
	uint32_t start_col = 0, start_row = 0;
	uint32_t end_col = 0, end_row = 0;
	uint32_t ch_offset[3] = { 0 };
	uint32_t mipi_word_num_start[16] = {0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5};
	uint32_t mipi_word_num_end[16] = {0, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5};
	struct slice_fetch_info *slc_fetch = NULL;
	struct pyr_dec_fetch_info *fetch = NULL;
	struct pyr_dec_slice_desc *cur_slc = NULL;

	if (!node) {
		pr_err("fail to get valid input dev\n");
		return -EFAULT;
	}

	fetch = &node->fetch_dec_info;
	fetch->bypass = 0;
	fetch->color_format = node->in_fmt;
	fetch->addr[0] = node->fetch_addr[idx].addr_ch0;
	fetch->addr[1] = node->fetch_addr[idx].addr_ch1;
	fetch->width = node->dec_layer_size[idx].w;
	fetch->height = node->dec_layer_size[idx].h;
	if (idx != 0)
		fetch->color_format = node->pyr_out_fmt;
	fetch->pitch[0] = pyrdec_node_cal_pitch(fetch->width, fetch->color_format);
	fetch->pitch[1] = fetch->pitch[0];
	fetch->chk_sum_clr_en = 0;
	fetch->ft0_axi_reorder_en = 0;
	fetch->ft1_axi_reorder_en = 0;
	fetch->substract = 0;
	fetch->ft0_max_len_sel = 1;
	fetch->ft1_max_len_sel = 1;
	/* this default val come from spec need to confirm */
	fetch->ft0_retain_num = 0x20;
	fetch->ft1_retain_num = 0x20;

	cur_slc = &node->slices[0];
	for (i = 0; i < node->slice_num; i++, cur_slc++) {
		slc_fetch = &cur_slc->slice_fetch;
		start_col = cur_slc->slice_fetch_pos.start_col;
		start_row = cur_slc->slice_fetch_pos.start_row;
		end_col = cur_slc->slice_fetch_pos.end_col;
		end_row = cur_slc->slice_fetch_pos.end_row;

		switch (fetch->color_format) {
		case CAM_YUV420_2FRAME:
		case CAM_YVU420_2FRAME:
			ch_offset[0] = start_row * fetch->pitch[0] + start_col;
			ch_offset[1] = (start_row >> 1) * fetch->pitch[1] + start_col;
			break;
		case CAM_YUV420_2FRAME_MIPI:
		case CAM_YVU420_2FRAME_MIPI:
			ch_offset[0] = start_row * fetch->pitch[0]
				+ (start_col >> 2) * 5 + (start_col & 0x3);
			ch_offset[1] = (start_row >> 1) * fetch->pitch[1]
				+ (start_col >> 2) * 5 + (start_col & 0x3);
			slc_fetch->mipi_byte_rel_pos = start_col & 0x0f;
			slc_fetch->mipi_word_num = ((((end_col + 1) >> 4) * 5
				+ mipi_word_num_end[(end_col + 1) & 0x0f])
				-(((start_col + 1) >> 4) * 5
				+ mipi_word_num_start[(start_col + 1) & 0x0f]) + 1);
			slc_fetch->mipi_byte_rel_pos_uv = slc_fetch->mipi_byte_rel_pos;
			slc_fetch->mipi_word_num_uv = slc_fetch->mipi_word_num;
			slc_fetch->mipi10_en = 1;
			if (node->fetch_path_sel) {
				slc_fetch->fetch_fbd.slice_size.w = end_col - start_col + 1;
				slc_fetch->fetch_fbd.slice_size.h = end_row - start_row + 1;
				slc_fetch->fetch_fbd.slice_start_pxl_xpt = start_col;
				slc_fetch->fetch_fbd.slice_start_pxl_ypt = start_row;
				slc_fetch->fetch_fbd.slice_start_header_addr = node->yuv_afbd_info.slice_start_header_addr
					+ ((start_row / ISP_FBD_TILE_HEIGHT) * node->yuv_afbd_info.tile_num_pitch +
					start_col / ISP_FBD_TILE_WIDTH) * 16;
			}
			PYR_DEC_DEBUG("(%d %d %d %d), pitch %d, offset0 %x offset1 %x, mipi %d %d\n",
				start_row, start_col, end_row, end_col,
				fetch->pitch[0], ch_offset[0], ch_offset[1],
				slc_fetch->mipi_byte_rel_pos, slc_fetch->mipi_word_num);
			break;
		default:
			ch_offset[0] = start_row * fetch->pitch[0] + start_col * 2;
			break;
		}

		slc_fetch->addr.addr_ch0 = fetch->addr[0] + ch_offset[0];
		slc_fetch->addr.addr_ch1 = fetch->addr[1] + ch_offset[1];
		slc_fetch->size.h = end_row - start_row + 1;
		slc_fetch->size.w = end_col - start_col + 1;

		PYR_DEC_DEBUG("fetch fmt %s, slice fetch size %d, %d\n",
			camport_fmt_name_get(fetch->color_format), slc_fetch->size.w, slc_fetch->size.h);
	}

	return ret;
}

static int pyrdec_node_store_dct_get(struct pyr_dec_node *node, uint32_t idx)
{
	int ret = 0, i = 0;
	uint32_t start_col = 0, start_row = 0;
	uint32_t end_col = 0, end_row = 0;
	uint32_t overlap_left = 0, overlap_up = 0;
	uint32_t overlap_right = 0, overlap_down = 0;
	uint32_t ch_offset[3] = { 0 };
	struct pyr_dec_slice_desc *cur_slc = NULL;
	struct slice_store_info *slc_dct_store = NULL;
	struct pyr_dec_store_info *store_dct = NULL;

	if (!node) {
		pr_err("fail to get valid input dev\n");
		return -EFAULT;
	}

	store_dct = &node->store_dct_info;
	if (idx != 0) {
		/* store dct only need output for layer0 */
		store_dct->bypass = 1;
		return 0;
	}
	store_dct->bypass = 0;
	store_dct->color_format = node->pyr_out_fmt;
	store_dct->addr[0] = node->store_addr[idx].addr_ch0;
	store_dct->addr[1] = node->store_addr[idx].addr_ch1;
	store_dct->width = node->dec_layer_size[idx].w;
	store_dct->height = node->dec_layer_size[idx].h;
	store_dct->pitch[0] = pyrdec_node_cal_pitch(store_dct->width, store_dct->color_format);
	store_dct->pitch[1] = store_dct->pitch[0];
	store_dct->data_10b = 0;
	store_dct->mipi_en = 0;
	store_dct->flip_en = 0;
	store_dct->last_frm_en = 1;
	store_dct->mono_en = 0;
	store_dct->mirror_en = 0;
	store_dct->burst_len = 0;
	store_dct->speed2x = 1;
	store_dct->shadow_clr_sel = 1;
	store_dct->shadow_clr = 1;
	store_dct->rd_ctrl = 0;

	cur_slc = &node->slices[0];
	for (i = 0; i < node->slice_num; i++, cur_slc++) {
		slc_dct_store = &cur_slc->slice_dct_store;
		start_col = cur_slc->slice_store_dct_pos.start_col;
		start_row = cur_slc->slice_store_dct_pos.start_row;
		end_col = cur_slc->slice_store_dct_pos.end_col;
		end_row = cur_slc->slice_store_dct_pos.end_row;
		overlap_up = cur_slc->slice_dct_overlap.overlap_up;
		overlap_down = cur_slc->slice_dct_overlap.overlap_down;
		overlap_left = cur_slc->slice_dct_overlap.overlap_left;
		overlap_right = cur_slc->slice_dct_overlap.overlap_right;

		switch (store_dct->color_format) {
		case CAM_YVU420_2FRAME_MIPI:
		case CAM_YUV420_2FRAME_MIPI:
			ch_offset[0] = start_row * store_dct->pitch[0] + start_col * 5 /4 + (start_col & 0x3);
			ch_offset[1] = ((start_row * store_dct->pitch[1]) >> 1) + start_col * 5 /4 + (start_col & 0x3);
			break;
		case CAM_YUV420_2FRAME:
		case CAM_YVU420_2FRAME:
			ch_offset[0] = start_row * store_dct->pitch[0] + start_col;
			ch_offset[1] = ((start_row * store_dct->pitch[1]) >> 1) + start_col;
			break;
		case CAM_YVU420_2FRAME_10:
		case CAM_YUV420_2FRAME_10:
			ch_offset[0] = start_row * store_dct->pitch[0] + (start_col << 1);
			ch_offset[1] = ((start_row * store_dct->pitch[1]) >> 1) + (start_col << 1);
			break;
		default:
			ch_offset[0] = start_row * store_dct->pitch[0] + start_col;
			ch_offset[1] = ((start_row * store_dct->pitch[1]) >> 1) + start_col;
			break;
		}

		slc_dct_store->addr.addr_ch0 = store_dct->addr[0] + ch_offset[0];
		slc_dct_store->addr.addr_ch1 = store_dct->addr[1] + ch_offset[1];
		slc_dct_store->size.h = end_row - start_row + 1;
		slc_dct_store->size.w = end_col - start_col + 1;
		slc_dct_store->border.up_border = overlap_up;
		slc_dct_store->border.down_border = overlap_down;
		slc_dct_store->border.left_border = overlap_left;
		slc_dct_store->border.right_border = overlap_right;

		PYR_DEC_DEBUG("dct store fmt %s, slice store size %d, %d offset %x %x\n",
				camport_fmt_name_get(store_dct->color_format), slc_dct_store->size.w, slc_dct_store->size.h, ch_offset[0], ch_offset[1]);
	}

	return ret;
}

static int pyrdec_node_store_dec_get(struct pyr_dec_node *node, uint32_t idx)
{
	int ret = 0, i = 0;
	uint32_t start_col = 0, start_row = 0;
	uint32_t end_col = 0, end_row = 0;
	uint32_t overlap_left = 0, overlap_up = 0;
	uint32_t overlap_right = 0, overlap_down = 0;
	uint32_t ch_offset[3] = { 0 };
	struct pyr_dec_slice_desc *cur_slc = NULL;
	struct slice_store_info *slc_dec_store = NULL;
	struct pyr_dec_store_info *store_dec = NULL;

	if (!node) {
		pr_err("fail to get valid input dev\n");
		return -EFAULT;
	}

	store_dec = &node->store_dec_info;
	idx = idx +1;
	store_dec->bypass = 0;
	store_dec->color_format = node->pyr_out_fmt;
	store_dec->addr[0] = node->store_addr[idx].addr_ch0;
	store_dec->addr[1] = node->store_addr[idx].addr_ch1;
	store_dec->width = node->dec_layer_size[idx].w;
	store_dec->height = node->dec_layer_size[idx].h;
	store_dec->pitch[0] = pyrdec_node_cal_pitch(store_dec->width, store_dec->color_format);
	store_dec->pitch[1] = store_dec->pitch[0];
	store_dec->data_10b = 0;
	store_dec->mipi_en = 0;
	store_dec->flip_en = 0;
	store_dec->last_frm_en = 1;
	store_dec->mono_en = 0;
	store_dec->mirror_en = 0;
	store_dec->burst_len = 1;
	store_dec->speed2x = 1;
	store_dec->shadow_clr_sel = 1;
	store_dec->shadow_clr = 1;
	store_dec->rd_ctrl = 0;

	cur_slc = &node->slices[0];
	for (i = 0; i < node->slice_num; i++, cur_slc++) {
		slc_dec_store = &cur_slc->slice_dec_store;
		start_col = cur_slc->slice_store_dec_pos.start_col;
		start_row = cur_slc->slice_store_dec_pos.start_row;
		end_col = cur_slc->slice_store_dec_pos.end_col;
		end_row = cur_slc->slice_store_dec_pos.end_row;
		overlap_up = cur_slc->slice_dec_overlap.overlap_up;
		overlap_down = cur_slc->slice_dec_overlap.overlap_down;
		overlap_left = cur_slc->slice_dec_overlap.overlap_left;
		overlap_right = cur_slc->slice_dec_overlap.overlap_right;

		switch (store_dec->color_format) {
		case CAM_YVU420_2FRAME_MIPI:
		case CAM_YUV420_2FRAME_MIPI:
			ch_offset[0] = start_row * store_dec->pitch[0] + start_col * 5 /4 + (start_col & 0x3);
			ch_offset[1] = ((start_row * store_dec->pitch[1]) >> 1) + start_col * 5 /4 + (start_col & 0x3);
			break;
		case CAM_YVU420_2FRAME_10:
		case CAM_YUV420_2FRAME_10:
			ch_offset[0] = start_row * store_dec->pitch[0] + (start_col << 1);
			ch_offset[1] = ((start_row * store_dec->pitch[1]) >> 1) + (start_col << 1);
			break;
		default:
			ch_offset[0] = start_row * store_dec->pitch[0] + start_col;
			ch_offset[1] = ((start_row * store_dec->pitch[1]) >> 1) + start_col;
			break;
		}

		slc_dec_store->addr.addr_ch0 = store_dec->addr[0] + ch_offset[0];
		slc_dec_store->addr.addr_ch1 = store_dec->addr[1] + ch_offset[1];
		slc_dec_store->size.h = end_row - start_row + 1;
		slc_dec_store->size.w = end_col - start_col + 1;
		slc_dec_store->border.up_border = overlap_up;
		slc_dec_store->border.down_border = overlap_down;
		slc_dec_store->border.left_border = overlap_left;
		slc_dec_store->border.right_border = overlap_right;

		PYR_DEC_DEBUG("store fmt %s, slice store size %d, %d offset %x %x\n",
			camport_fmt_name_get(store_dec->color_format), slc_dec_store->size.w, slc_dec_store->size.h, ch_offset[0], ch_offset[1]);
	}

	return ret;
}

static int pyrdec_node_offline_get(struct pyr_dec_node *node, uint32_t idx)
{
	int ret = 0, i = 0;
	struct pyr_dec_slice_desc *cur_slc = NULL;
	struct slice_pyr_dec_node_info *slc_pyr_dec = NULL;
	struct pyr_dec_offline_info *dec_offline = NULL;

	if (!node) {
		pr_err("fail to get valid input dev\n");
		return -EFAULT;
	}

	dec_offline = &node->offline_dec_info;
	dec_offline->bypass = 0;
	dec_offline->hor_padding_en = 0;
	dec_offline->hor_padding_num = 0;
	dec_offline->ver_padding_en = 0;
	dec_offline->ver_padding_num = 0;
	dec_offline->dispatch_dbg_mode_ch0 = 1;
	dec_offline->dispatch_done_cfg_mode = 1;
	dec_offline->dispatch_pipe_flush_num = 4;
	dec_offline->dispatch_pipe_hblank_num = 60;
	dec_offline->dispatch_pipe_nfull_num = 100;
	dec_offline->dispatch_width_dly_num_flash = 640;
	dec_offline->dispatch_width_flash_mode = 0;
	dec_offline->dispatch_yuv_start_order = 0;
	dec_offline->dispatch_yuv_start_row_num = 1;

	if(idx == 0) {
		dec_offline->hor_padding_num = node->dec_padding_size.w;
		dec_offline->ver_padding_num = node->dec_padding_size.h;
		if (dec_offline->hor_padding_num)
			dec_offline->hor_padding_en = 1;
		if (dec_offline->ver_padding_num)
			dec_offline->ver_padding_en = 1;
	}

	cur_slc = &node->slices[0];
	for (i = 0; i < node->slice_num; i++, cur_slc++) {
		slc_pyr_dec = &cur_slc->slice_pyr_dec;
		slc_pyr_dec->hor_padding_en = dec_offline->hor_padding_en;
		slc_pyr_dec->ver_padding_en = dec_offline->ver_padding_en;
		slc_pyr_dec->hor_padding_num = dec_offline->hor_padding_num;
		slc_pyr_dec->ver_padding_num = dec_offline->ver_padding_num;
		/* padding num only need for last slice */
		if (i != (node->slice_num - 1)) {
			slc_pyr_dec->hor_padding_en = 0;
			slc_pyr_dec->hor_padding_num = 0;
		}
		slc_pyr_dec->dispatch_dly_width_num = 80;
		slc_pyr_dec->dispatch_dly_height_num = 80;
		if (cur_slc->slice_fetch.size.w <= 40 || cur_slc->slice_fetch.size.h <= 32)
			slc_pyr_dec->dispatch_dly_height_num = 256;

		if (idx == 0) {
			slc_pyr_dec->dispatch_dly_width_num = slc_pyr_dec->hor_padding_num + 20;
			slc_pyr_dec->dispatch_dly_height_num = slc_pyr_dec->ver_padding_num + 20;
		}
	}

	return ret;
}

static int pyrdec_node_dct_ynr_get(struct pyr_dec_node *node, uint32_t idx)
{
	int ret = 0, i = 0;
	struct pyr_dec_slice_desc *cur_slc = NULL;
	struct pyr_dec_dct_ynr_info *dct_ynr = NULL;

	if (!node) {
		pr_err("fail to get valid input dev\n");
		return -EFAULT;
	}

	dct_ynr = &node->dct_ynr_info;
	cur_slc = &node->slices[0];
	dct_ynr->img.w = node->dec_layer_size[0].w;
	dct_ynr->img.h = node->dec_layer_size[0].h;

	dct_ynr->dct->rnr_radius = dct_ynr->dct_radius;
	dct_ynr->dct->rnr_imgCenterX = node->dec_layer_size[0].w >> 1;
	dct_ynr->dct->rnr_imgCenterY = node->dec_layer_size[0].h >> 1;
	pr_debug("radius %d, Center x %d, y %d, img  w %d, h %d\n", dct_ynr->dct->rnr_radius, dct_ynr->dct->rnr_imgCenterX,
		dct_ynr->dct->rnr_imgCenterY, dct_ynr->img.w, dct_ynr->img.h);

	for (i = 0; i < node->slice_num; i++, cur_slc++) {
		dct_ynr->start[i].w = cur_slc->slice_fetch_pos.start_col;
		dct_ynr->start[i].h = cur_slc->slice_fetch_pos.start_row;
	}

	return ret;
}

struct dcam_isp_k_block *pyrdecnode_blk_param_get(struct pyr_dec_node *node, uint32_t target_fid)
{
	int loop = 0;
	struct cam_frame *decblk_frame = NULL;
	struct dcam_isp_k_block *out = NULL;

	do {
		mutex_lock(&node->blkpm_q_lock);
		decblk_frame = CAM_QUEUE_DEQUEUE_PEEK(&node->param_buf_queue, struct cam_frame, list);
		if (decblk_frame) {
			pr_debug("decblk_frame.fid=%d,pframe.id=%d\n",decblk_frame->dec_blk.fid, target_fid);
			decblk_frame = CAM_QUEUE_DEQUEUE(&node->param_buf_queue, struct cam_frame, list);
			mutex_unlock(&node->blkpm_q_lock);
			CAM_QUEUE_ENQUEUE(&node->param_share_queue, &decblk_frame->list);
			if (decblk_frame->dec_blk.fid <= target_fid) {
				out = decblk_frame->dec_blk.decblk_pm;
				if (decblk_frame->dec_blk.fid == target_fid)
					break;
			} else {
				pr_warn("warning:no blk param in param buf q, use latest param, frame %d\n", target_fid);
				out = &node->decblk_param;
				break;
			}
		} else {
			mutex_unlock(&node->blkpm_q_lock);
			pr_warn("warning:no %d fid blk param in param buf q: state:%d, cnt:%d.\n", target_fid,
				node->param_buf_queue.state, node->param_buf_queue.cnt);
			out = &node->decblk_param;
		}
	} while (loop++ < node->param_buf_queue.max);

	return out;
}

static int pyrdec_node_afbd_get(uint32_t fmt, void *cfg_out, struct cam_frame *frame)
{
	int32_t tile_col = 0, tile_row = 0;
	struct isp_fbd_yuv_info *fbd_yuv = NULL;
	struct dcam_compress_cal_para cal_fbc = {0};

	if (!cfg_out || !frame) {
		pr_err("fail to get valid input ptr %p\n", cfg_out);
		return -EFAULT;
	}

	fbd_yuv = (struct isp_fbd_yuv_info *)cfg_out;

	if (frame->common.is_compressed == 0)
		return 0;

	fbd_yuv->fetch_fbd_bypass = 0;
	fbd_yuv->slice_size.w = frame->common.width;
	fbd_yuv->slice_size.h = frame->common.height;
	tile_col = (fbd_yuv->slice_size.w + ISP_FBD_TILE_WIDTH - 1) / ISP_FBD_TILE_WIDTH;
	tile_row =(fbd_yuv->slice_size.h + ISP_FBD_TILE_HEIGHT - 1) / ISP_FBD_TILE_HEIGHT;

	fbd_yuv->tile_num_pitch = tile_col;
	fbd_yuv->slice_start_pxl_xpt = 0;
	fbd_yuv->slice_start_pxl_ypt = 0;

	cal_fbc.data_bits = cam_data_bits(fmt);
	cal_fbc.fbc_info = &frame->common.fbc_info;
	cal_fbc.in = frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];
	if (fmt == CAM_YUV420_2FRAME_10 || fmt == CAM_YUV420_2FRAME_MIPI)
		cal_fbc.fmt = CAM_YUV420_2FRAME;
	else if (fmt == CAM_YVU420_2FRAME_10 || fmt == CAM_YVU420_2FRAME_MIPI)
		cal_fbc.fmt = CAM_YVU420_2FRAME;
	cal_fbc.height = fbd_yuv->slice_size.h;
	cal_fbc.width = fbd_yuv->slice_size.w;
	cal_fbc.out = &fbd_yuv->hw_addr;
	dcam_if_cal_compressed_addr(&cal_fbc);
	fbd_yuv->buffer_size = cal_fbc.fbc_info->buffer_size;

	/* store start address for slice use */
	fbd_yuv->frame_header_base_addr = fbd_yuv->hw_addr.addr0;
	fbd_yuv->slice_start_header_addr = fbd_yuv->frame_header_base_addr +
		((fbd_yuv->slice_start_pxl_ypt / ISP_FBD_TILE_HEIGHT) * fbd_yuv->tile_num_pitch +
		fbd_yuv->slice_start_pxl_xpt / ISP_FBD_TILE_WIDTH) * 16;
	fbd_yuv->data_bits = cal_fbc.data_bits;

	pr_debug("iova:%lx, fetch_fbd: %u 0x%x 0x%x, 0x%x, size %u %u, channel_id:%d, tile_col:%d\n",
		frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP],
		frame->common.fid, fbd_yuv->hw_addr.addr0,
		fbd_yuv->hw_addr.addr1, fbd_yuv->hw_addr.addr2,
		frame->common.width, frame->common.height,
		frame->common.channel_id, fbd_yuv->tile_num_pitch);

	return 0;
}

static int pyrdec_node_param_cfg(struct pyr_dec_node *node, uint32_t index)
{
	int ret = 0;

	if (!node) {
		pr_err("fail to get valid input handle\n");
		return -EFAULT;
	}

	ret = pyrdec_node_fetch_get(node, index);
	if (ret) {
		pr_err("fail to get isp pyr_dec fetch\n");
		return ret;
	}

	ret = pyrdec_node_store_dct_get(node, index);
	if (ret) {
		pr_err("fail to get isp pyr_dec store dct\n");
		return ret;
	}

	ret = pyrdec_node_store_dec_get(node, index);
	if (ret) {
		pr_err("fail to get isp pyr_dec store dec\n");
		return ret;
	}

	ret = pyrdec_node_offline_get(node, index);
	if (ret) {
		pr_err("fail to get isp pyr_dec offline\n");
		return ret;
	}

	ret = pyrdec_node_dct_ynr_get(node, index);
	if (ret) {
		pr_err("fail to get isp dct_ynr\n");
		return ret;
	}

	return ret;
}

static int pyrdec_node_calc_base_info(struct pyr_dec_node *node, struct cam_frame *pframe,
		struct cam_frame *out_frame)
{
	int ret = 0;
	uint32_t offset = 0, in_size = 0, out_size = 0;
	uint32_t layer_num = 0, i = 0, in_pitch = 0, out_pitch = 0;

	if (!node || !pframe || !out_frame) {
		pr_err("fail to get valid input ptr:0x%p, 0x%p, 0x%p\n", node, pframe, out_frame);
		return -EFAULT;
	}

	layer_num = PYR_DEC_LAYER_NUM;
	node->src.w = pframe->common.width;
	node->src.h = pframe->common.height;
	out_frame->common.width = pframe->common.width;
	out_frame->common.height = pframe->common.height;
	out_frame->common.nr3_me = pframe->common.nr3_me;
	/* update layer num based on img size */
	while (pyrdec_small_layer_w(node->src.w, layer_num) < MIN_PYR_WIDTH ||
		pyrdec_small_layer_h(node->src.h, layer_num) < MIN_PYR_HEIGHT) {
		pr_debug("layer num need decrease based on small input %d %d\n",
			node->src.w, node->src.h);
		layer_num--;
	}
	node->layer_num = layer_num;
	in_pitch = pyrdec_node_cal_pitch(node->src.w, node->in_fmt);
	out_pitch = pyrdec_node_cal_pitch(node->src.w, node->pyr_out_fmt);
	in_size = in_pitch * node->src.h;
	out_size = out_pitch * node->src.h;
	node->fetch_path_sel = pframe->common.is_compressed;
	node->fetch_addr[0].addr_ch0 = pframe->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];
	node->fetch_addr[0].addr_ch1 = pframe->common.buf.iova[CAM_BUF_IOMMUDEV_ISP] + in_size;
	node->store_addr[0].addr_ch0 = out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];
	node->store_addr[0].addr_ch1 = node->store_addr[0].addr_ch0 + out_size;
	node->yuv_afbd_info.frame_header_base_addr = pframe->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];
	node->yuv_afbd_info.slice_start_header_addr = pframe->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];
	node->dec_layer_size[0].w = pyrdec_layer0_width(node->src.w, layer_num);
	node->dec_layer_size[0].h = pyrdec_layer0_heigh(node->src.h, layer_num);
	node->dec_padding_size.w = node->dec_layer_size[0].w - node->src.w;
	node->dec_padding_size.h = node->dec_layer_size[0].h - node->src.h;
	PYR_DEC_DEBUG("layer0 size %d %d padding size %d %d\n",
		node->dec_layer_size[0].w, node->dec_layer_size[0].h,
		node->dec_padding_size.w, node->dec_padding_size.h);
	PYR_DEC_DEBUG("layer0 fetch addr %x %x store addr %x %x\n",
		node->fetch_addr[0].addr_ch0, node->fetch_addr[0].addr_ch1,
		node->store_addr[0].addr_ch0, node->store_addr[0].addr_ch1);
	for (i = 1; i < layer_num + 1; i++) {
		offset += (out_size * 3 / 2);
		node->dec_layer_size[i].w = node->dec_layer_size[i - 1].w / 2;
		node->dec_layer_size[i].h = node->dec_layer_size[i - 1].h / 2;
		out_pitch = pyrdec_node_cal_pitch(node->dec_layer_size[i].w, node->pyr_out_fmt);
		out_size = out_pitch * node->dec_layer_size[i].h;
		node->store_addr[i].addr_ch0 = out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP] + offset;
		node->store_addr[i].addr_ch1 = node->store_addr[i].addr_ch0 + out_size;
		if (i < layer_num) {
			node->fetch_addr[i].addr_ch0 = node->store_addr[i].addr_ch0;
			node->fetch_addr[i].addr_ch1 = node->store_addr[i].addr_ch1;
		}
		PYR_DEC_DEBUG("layer%d size %d %d\n", i,
			node->dec_layer_size[i].w, node->dec_layer_size[i].h);
		if (i < layer_num)
			PYR_DEC_DEBUG("layer %d fetch addr %x %x\n", i,
				node->fetch_addr[i].addr_ch0, node->fetch_addr[i].addr_ch1);
		PYR_DEC_DEBUG("layer %d store addr %x %x\n", i,
			node->store_addr[i].addr_ch0, node->store_addr[i].addr_ch1);
	}

	return ret;
}

static int pyrdec_node_calc_overlap_info(struct pyr_dec_node *node)
{
	int ret = 0, i = 0, j = 0;
	uint32_t slice_num, slice_w;
	uint32_t slice_max_w, max_w;
	uint32_t linebuf_len;
	struct alg_dec_offline_overlap *dec_ovlap = NULL;
	struct pyr_dec_overlap_info *cur_ovlap = NULL;
	uint32_t dec_slice_num[MAX_PYR_DEC_LAYER_NUM] = {0};

	if (!node) {
		pr_err("fail to get valid input handle\n");
		return -EFAULT;
	}
	dec_ovlap = cam_buf_kernel_sys_vzalloc(sizeof(struct alg_dec_offline_overlap));
	if (!dec_ovlap)
		return -EFAULT;

	cur_ovlap = &node->overlap_dec_info[0];
	/* calc the slice w & h base on input size */
	max_w = node->src.w;
	slice_num = 1;
	linebuf_len = ISP_MAX_LINE_WIDTH;
	slice_max_w = linebuf_len - SLICE_OVERLAP_W_MAX;
	if (max_w <= linebuf_len) {
		slice_w = max_w;
	} else {
		do {
			slice_num++;
			slice_w = (max_w + slice_num - 1) / slice_num;
		} while (slice_w >= slice_max_w);
	}
	pr_debug("input_w %d, slice_num %d, slice_w %d\n", max_w, slice_num, slice_w);

	dec_ovlap->slice_w = slice_w;
	dec_ovlap->slice_h = SLICE_HEIGHT_STATIC_MAX;
	dec_ovlap->slice_w = ISP_ALIGNED(dec_ovlap->slice_w);
	dec_ovlap->slice_h = ISP_ALIGNED(dec_ovlap->slice_h);
	dec_ovlap->img_w = node->src.w;
	dec_ovlap->img_h = node->src.h;
	dec_ovlap->crop_en = 0;
	dec_ovlap->img_type = 4;
	dec_ovlap->dct_bypass = 0;
	dec_ovlap->dec_offline_bypass = 0;
	dec_ovlap->layerNum = node->layer_num + 1;
	dec_ovlap->slice_mode = 1;
	dec_ovlap->MaxSliceWidth = linebuf_len;
	alg_slice_calc_dec_offline_overlap(dec_ovlap);

	for (i = 0; i < dec_ovlap->layerNum; i++, cur_ovlap++) {
		cur_ovlap->slice_num = dec_ovlap->slice_num[i];
		dec_slice_num[i] = cur_ovlap->slice_num;
		PYR_DEC_DEBUG("layer %d num %d\n", i, cur_ovlap->slice_num);
		if (i < node->layer_num) {
			for (j = 0; j < cur_ovlap->slice_num; j++) {
				cur_ovlap->slice_fetch_region[j].start_col = dec_ovlap->fecth_dec_region[i][j].sx;
				cur_ovlap->slice_fetch_region[j].start_row = dec_ovlap->fecth_dec_region[i][j].sy;
				cur_ovlap->slice_fetch_region[j].end_col = dec_ovlap->fecth_dec_region[i][j].ex;
				cur_ovlap->slice_fetch_region[j].end_row = dec_ovlap->fecth_dec_region[i][j].ey;
				PYR_DEC_DEBUG("fetch sx %d sy %d ex %d ey %d\n", cur_ovlap->slice_fetch_region[j].start_col,
					cur_ovlap->slice_fetch_region[j].start_row, cur_ovlap->slice_fetch_region[j].end_col,
					cur_ovlap->slice_fetch_region[j].end_row);
			}
		}
		if (i == 0)
			slice_num = dec_slice_num[i];
		else
			slice_num = dec_slice_num[i - 1];
		for (j = 0; j < slice_num; j++) {
			cur_ovlap->slice_store_region[j].start_col = dec_ovlap->store_dec_region[i][j].sx;
			cur_ovlap->slice_store_region[j].start_row = dec_ovlap->store_dec_region[i][j].sy;
			cur_ovlap->slice_store_region[j].end_col = dec_ovlap->store_dec_region[i][j].ex;
			cur_ovlap->slice_store_region[j].end_row = dec_ovlap->store_dec_region[i][j].ey;
			PYR_DEC_DEBUG("store sx %d sy %d ex %d ey %d\n", cur_ovlap->slice_store_region[j].start_col,
				cur_ovlap->slice_store_region[j].start_row, cur_ovlap->slice_store_region[j].end_col,
				cur_ovlap->slice_store_region[j].end_row);
			cur_ovlap->slice_store_overlap[j].overlap_up = dec_ovlap->store_dec_overlap[i][j].ov_up;
			cur_ovlap->slice_store_overlap[j].overlap_down = dec_ovlap->store_dec_overlap[i][j].ov_down;
			cur_ovlap->slice_store_overlap[j].overlap_left = dec_ovlap->store_dec_overlap[i][j].ov_left;
			cur_ovlap->slice_store_overlap[j].overlap_right = dec_ovlap->store_dec_overlap[i][j].ov_right;
			PYR_DEC_DEBUG("store up %d down %d left %d right %d\n", cur_ovlap->slice_store_overlap[j].overlap_up,
				cur_ovlap->slice_store_overlap[j].overlap_down, cur_ovlap->slice_store_overlap[j].overlap_left,
				cur_ovlap->slice_store_overlap[j].overlap_right);
		}
	}
	if (dec_ovlap) {
		cam_buf_kernel_sys_vfree(dec_ovlap);
		dec_ovlap = NULL;
	}

	return ret;
}

static int pyrdec_node_calc_slice_info(struct pyr_dec_node *node)
{
	int ret = 0, i = 0, j = 0;
	uint32_t layer_num = 0;
	struct pyrdec_hw_k_blk_func dec_cfg_func = {0};
	struct pyr_dec_slice_desc *cur_slc = NULL;
	struct pyr_dec_overlap_info *cur_ovlap = NULL, *temp_ovlap = NULL;

	if (!node) {
		pr_err("fail to get valid inptr\n");
		return -EFAULT;
	}

	dec_cfg_func.index = PYR_DEC_K_BLK_CFG;
	node->hw->pyrdec_ioctl(node->hw, PYRDEC_HW_CFG_K_BLK_FUNC_GET, &dec_cfg_func);

	layer_num = node->layer_num;
	for (i = 0; i < layer_num; i++) {
		cur_ovlap = &node->overlap_dec_info[i];
		cur_slc = &node->slices[0];
		node->slice_num = cur_ovlap->slice_num;
		node->cur_layer_id = i;
		for (j = 0; j < node->slice_num; j++, cur_slc++) {
			cur_slc->slice_fetch_pos = cur_ovlap->slice_fetch_region[j];
			cur_slc->slice_store_dct_pos = cur_ovlap->slice_store_region[j];
			cur_slc->slice_dct_overlap = cur_ovlap->slice_store_overlap[j];
			temp_ovlap = &node->overlap_dec_info[i + 1];
			cur_slc->slice_store_dec_pos = temp_ovlap->slice_store_region[j];
			cur_slc->slice_dec_overlap = temp_ovlap->slice_store_overlap[j];
		}

		pyrdec_node_param_cfg(node, i);
		for (j = 0; j < node->slice_num; j++) {
			node->cur_slice_id = j;
			if (dec_cfg_func.k_blk_func)
				dec_cfg_func.k_blk_func(node);
		}
	}

	return ret;
}

static int pyrdec_node_blkparam_size_cfg(struct pyr_dec_node *node, struct dcam_isp_k_block *param)
{
	uint32_t new_width = 0, old_width = 0;
	uint32_t new_height = 0, old_height = 0;

	if (!node || !param) {
		pr_err("fail to get valid inptr %p %p\n", node, param);
		return -EFAULT;
	}

	old_width = node->src.w;
	old_height = node->src.h;
	new_width = old_width;
	new_height = old_height;

	param->blkparam_info.new_height = new_height;
	param->blkparam_info.new_width = new_width;
	param->blkparam_info.old_height = old_height;
	param->blkparam_info.old_width = old_width;
	param->blkparam_info.sensor_width = node->sn_size.w;
	param->blkparam_info.sensor_height = node->sn_size.h;
	pr_debug("size(%d %d) => (%d %d)\n", old_width, old_height, new_width, new_height);

	return 0;
}

static int pyrdec_node_dct_blkparam_update(struct pyr_dec_node *node, struct dcam_isp_k_block *dct_param)
{
	int ret = 0;
	struct pyrdec_hw_k_blk_func dct_update_func = {0};

	if (!node || !dct_param) {
		pr_err("fail to get valid inptr %p %p.\n", node, dct_param);
		return -EFAULT;
	}

	node->dct_ynr_info.dct = &dct_param->dct_info;
	node->dct_ynr_info.old_width = dct_param->blkparam_info.old_width;
	node->dct_ynr_info.old_height = dct_param->blkparam_info.old_height;
	node->dct_ynr_info.new_width = dct_param->blkparam_info.new_width;
	node->dct_ynr_info.new_height = dct_param->blkparam_info.new_height;
	node->dct_ynr_info.sensor_height = dct_param->blkparam_info.sensor_height;
	node->dct_ynr_info.sensor_width = dct_param->blkparam_info.sensor_width;
	dct_update_func.index = PYR_DEC_K_BLK_DCT_UPDATE;
	node->hw->pyrdec_ioctl(node->hw, PYRDEC_HW_CFG_K_BLK_FUNC_GET, &dct_update_func);
	if (dct_update_func.k_blk_func)
		dct_update_func.k_blk_func(node);
	dct_param->dct_radius = node->dct_ynr_info.dct_radius;

	return ret;
}

static int pyrdec_node_irq_free(struct pyrdec_pipe_dev *dev)
{
	struct cam_hw_info *hw = NULL;

	if (!dev) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	hw = dev->hw;
	devm_free_irq(&hw->pdev->dev, dev->irq_no, (void *)dev);

	return 0;
}

static int pyrdec_node_irq_request(struct pyrdec_pipe_dev *dev)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;

	if (!dev) {
		pr_err("fail to get valid input dec dev, address is NULL\n");
		return -EFAULT;
	}

	hw = dev->hw;
	dev->irq_no = hw->ip_isp->dec_irq_no;
	if (!dev->isr_func) {
		pr_err("fail to get pyr dec irq call back func\n");
		return -EFAULT;
	}

	ret = devm_request_irq(&hw->pdev->dev, dev->irq_no, dev->isr_func,
			IRQF_SHARED, "pyr_dec dev", (void *)dev);
	if (ret) {
		pr_err("fail to install pyr_dec node irq_no %d\n", dev->irq_no);
		return -EFAULT;
	}

	return ret;
}

static int pyrdec_node_ts_cal(struct pyr_dec_node *node, struct pyrdec_pipe_dev *dec_dev)
{
	uint32_t sec = 0, usec = 0;
	timespec consume_ts = {0};

	os_adapt_time_get_ts(&node->end_ts);
	dec_dev->hw_start_ts = node->end_ts;
	consume_ts = os_adapt_time_timespec_sub(node->end_ts, node->start_ts);
	sec = consume_ts.tv_sec;
	usec = consume_ts.tv_nsec / NSEC_PER_USEC;
	if ((sec * USEC_PER_SEC + usec) > DEC_NODE_TIME)
		pr_warn("Warning: pyrdec node process too long. consume_time %d.%06d\n", sec, usec);

	PERFORMANCE_DEBUG("pyrdec_node: cur_time %lld.%06ld, consume_time %lld.%06ld\n",
		node->end_ts.tv_sec, node->end_ts.tv_nsec / NSEC_PER_USEC,
		consume_ts.tv_sec, consume_ts.tv_nsec / NSEC_PER_USEC);

	return 0;
}

static int pyrdec_node_hw_ts_cal(struct pyr_dec_node *node)
{
	uint32_t size = 0;
	int64_t sec = 0, usec = 0, time_ratio = 0;
	timespec consume_ts = {0};
	timespec cur_ts = {0};
	struct pyrdec_pipe_dev *dec_dev = NULL;

	dec_dev = node->pyrdec_dev;
	os_adapt_time_get_ts(&cur_ts);
	consume_ts = os_adapt_time_timespec_sub(cur_ts, dec_dev->hw_start_ts);
	sec = consume_ts.tv_sec;
	usec = consume_ts.tv_nsec / NSEC_PER_USEC;
	size = node->src.w * node->src.h;
	time_ratio = div_s64(TIME_SIZE_RATIO * (sec * USEC_PER_SEC + usec), size);
	if ((sec * USEC_PER_SEC + usec) > DEC_HW_TIME && time_ratio > DEC_HW_TIME_RATIO)
		pr_warn("Warning: pyrdec hw process too long. consume_time %lld.%06lld\n", sec, usec);

	PERFORMANCE_DEBUG("pyrdec_hw: cur_time %lld.%06ld, consume_time %lld.%06ld\n",
		cur_ts.tv_sec, cur_ts.tv_nsec / NSEC_PER_USEC,
		consume_ts.tv_sec, consume_ts.tv_nsec / NSEC_PER_USEC);

	return 0;
}

static void *pyrdec_node_dev_bind(void *node)
{
	int ret = -1;
	unsigned long flag = 0;
	struct pyrdec_pipe_dev *dec_dev = NULL;
	struct pyr_dec_node *pnode = NULL;

	if (!node) {
		pr_err("fail to get node\n");
		return NULL;
	}
	pnode = (struct pyr_dec_node*)node;
	dec_dev = pnode->pyrdec_dev;
	spin_lock_irqsave(&dec_dev->ctx_lock, flag);

	if (dec_dev->node == pnode) {
		atomic_inc(&dec_dev->user_cnt);
		pr_debug("pnode & dec already binding\n");
		spin_unlock_irqrestore(&dec_dev->ctx_lock, flag);
		return dec_dev;
	}

	if (atomic_inc_return(&dec_dev->user_cnt) == 1) {
		ret++;
		goto exit;
	}
	atomic_dec(&dec_dev->user_cnt);

exit:
	spin_unlock_irqrestore(&dec_dev->ctx_lock, flag);

	if (ret == -1)
		return NULL;

	dec_dev->node = pnode;
	pnode->is_bind = 1;
	pr_debug("pnode %p, dec %p\n", pnode, dec_dev);

	return dec_dev;
}

static uint32_t pyrdec_node_dev_unbind(void *node)
{
	unsigned long flag = 0;
	struct pyrdec_pipe_dev *dec_dev = NULL;
	struct pyr_dec_node *pnode = NULL;
	pnode = VOID_PTR_TO(node, struct pyr_dec_node);
	dec_dev = pnode->pyrdec_dev;

	spin_lock_irqsave(&dec_dev->ctx_lock, flag);
	if (!pnode->is_bind) {
		pr_debug("binding pnode to dec, is_bind:%d\n", pnode->is_bind);
		spin_unlock_irqrestore(&dec_dev->ctx_lock, flag);
		return 0;
	}

	if (pnode == dec_dev->node) {
		if (atomic_dec_return(&dec_dev->user_cnt) == 0) {
			pr_debug("pnode dec unbind success node:%p\n", pnode);
			dec_dev->node = NULL;
			pnode->is_bind = 0;
		}
	}
	spin_unlock_irqrestore(&dec_dev->ctx_lock, flag);
	return 0;
}

static int pyrdec_node_irq_proc(void *handle)
{
	int ret = 0;
	uint32_t in_queue_cnt = 0, proc_queue_cnt = 0;
	struct pyrdec_pipe_dev *pyrdec = NULL;
	struct pyr_dec_node *node = NULL;
	struct cam_frame *out_frame = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_node_cfg_param node_param = {0};
	struct camera_buf_get_desc buf_desc = {0};
	if (!handle) {
		pr_err("fail to get invalid ptr\n");
		return -EFAULT;
	}

	pyrdec = (struct pyrdec_pipe_dev *)handle;
	node = (struct pyr_dec_node *)pyrdec->pyrdec_node[pyrdec->cur_node_idx][pyrdec->cur_node_id];

	pr_debug("node%d dec done\n", node->node_id);
	pyrdec_node_hw_ts_cal(node);
	pyrdec->in_irq_handler = 1;
	pyrdec_node_dev_unbind(node);
	complete(&node->frm_done);

	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
	buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
	pframe = cam_buf_manager_buf_dequeue(&node->fetch_result_pool, &buf_desc, node->buf_manager_handle);
	if (pframe && node->data_cb_func) {
		node_param.port_id = PORT_DEC_OUT;
		node->port_cfg_cb_func(&node_param, PORT_CFG_BUFFER_GET, node->port_cfg_cb_handle);
		out_frame = (struct cam_frame *)node_param.param;
		if (out_frame) {
			out_frame->common.fid = pframe->common.fid;
			out_frame->common.sensor_time = pframe->common.sensor_time;
			out_frame->common.boot_sensor_time = pframe->common.boot_sensor_time;
			out_frame->common.pyr_status = pframe->common.pyr_status;
			pframe->common.pyr_status = CAM_DISABLE;
			out_frame->common.cam_fmt = pframe->common.cam_fmt;
			out_frame->common.zoom_data = pframe->common.zoom_data;
			out_frame->common.xtm_conflict = pframe->common.xtm_conflict;
			if (node->hw->ip_isp->isphw_abt->pyr_rec_lay0_support)
				out_frame->common.pframe_data = pframe;
			else
				node->data_cb_func(CAM_CB_PYRDEC_RET_SRC_BUF, pframe, node->data_cb_handle);
			/* return buffer to cam core for start pyrrec proc */
			cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
			if (node->is_fast_stop) {
				node_param.port_id = PORT_DEC_OUT;
				node_param.param = out_frame;
				out_frame->common.pframe_data = NULL;
				node->port_cfg_cb_func(&node_param, PORT_CFG_BUFFER_SET, node->port_cfg_cb_handle);
				if (node->hw->ip_isp->isphw_abt->pyr_rec_lay0_support)
					node->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, pframe, node->data_cb_handle);

				in_queue_cnt = cam_buf_manager_pool_cnt(&node->fetch_unprocess_pool, node->buf_manager_handle);
				proc_queue_cnt = cam_buf_manager_pool_cnt(&node->fetch_result_pool, node->buf_manager_handle);
				if (in_queue_cnt == 0 && proc_queue_cnt == 0) {
					node->is_fast_stop = 0;
					complete(node->fast_stop_done);
				}
				pyrdec->in_irq_handler = 0;

				return ret;
			}
			out_frame->common.link_from.node_type = CAM_NODE_TYPE_PYR_DEC;
			out_frame->common.link_from.node_id = node->node_id;
			out_frame->common.link_from.port_id = PORT_DEC_OUT;

			pr_debug("dec irq proc done\n");
			if (node->data_cb_func)
				node->data_cb_func(CAM_CB_ISP_RET_PYR_DEC_BUF, out_frame, node->data_cb_handle);
			else
				pr_err("fail to get data_cb_func ptr at ret pyr dec\n");
		} else {
			node->data_cb_func(CAM_CB_PYRDEC_RET_SRC_BUF, pframe, node->data_cb_handle);
			pr_err("fail to get out frame\n");
		}
	}else
		pr_err("fail to get src frame 0x%px\n", pframe);

	pyrdec->in_irq_handler = 0;

	return ret;
}

static int pyrdec_node_start_proc(void *handle)
{
	int ret = 0;
	uint32_t loop = 0, in_queue_cnt = 0, proc_queue_cnt = 0;
	struct pyr_dec_node *node = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_frame *out_frame = NULL;
	struct cam_node_cfg_param node_param = {0};
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct pyrdec_pipe_dev *dec_dev = NULL;
	struct camera_buf_get_desc buf_desc = {0};
	struct dcam_isp_k_block *decblk_param = NULL;

	if (!handle) {
		pr_err("fail to get valid input handle\n");
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;
	dec_dev = node->pyrdec_dev;
	if (!dec_dev) {
		pr_err("fail to get dec_dev %p\n", dec_dev);
		return -EFAULT;
	}

	fmcu = (struct isp_fmcu_ctx_desc *)dec_dev->fmcu_handle;
	if (fmcu == NULL) {
		pr_err("fail to get fmcu %p\n", fmcu);
		goto exit;
	}

	os_adapt_time_get_ts(&node->start_ts);
	PERFORMANCE_DEBUG("node_start_time %lld.%06ld", node->start_ts.tv_sec, node->start_ts.tv_nsec / NSEC_PER_USEC);

	pframe = cam_buf_manager_buf_dequeue(&node->fetch_unprocess_pool, NULL, node->buf_manager_handle);
	if (pframe == NULL) {
		pr_err("fail to get input frame %p\n", pframe);
		goto exit;
	}

	if (node->is_fast_stop) {
		pyrdec_node_src_frame_ret(pframe);
		in_queue_cnt = cam_buf_manager_pool_cnt(&node->fetch_unprocess_pool, node->buf_manager_handle);
		proc_queue_cnt = cam_buf_manager_pool_cnt(&node->fetch_result_pool, node->buf_manager_handle);
		if (in_queue_cnt == 0 && proc_queue_cnt == 0) {
			node->is_fast_stop = 0;
			complete(node->fast_stop_done);
		}
		return 0;
	}

	decblk_param = pyrdecnode_blk_param_get(node, pframe->common.fid);
	pyrdec_node_blkparam_size_cfg(node, decblk_param);
	do {
		dec_dev = (struct pyrdec_pipe_dev *)pyrdec_node_dev_bind(node);
		if (!dec_dev) {
			pr_info_ratelimited("pnode wait for dec. loop %d\n", loop);
			os_adapt_time_usleep_range(600, 800);
		} else
			break;
	} while (loop++ < 5000);

	if (dec_dev == NULL) {
		pr_err("fail to get dec\n");
		return EFAULT;
	}

	loop = 0;
	do {
		ret = cam_buf_manager_buf_enqueue(&node->fetch_result_pool, pframe, NULL, node->buf_manager_handle);
		if (ret == 0)
			break;
		pr_debug("wait for proc queue. loop %d\n", loop);
		os_adapt_time_usleep_range(600, 2000);
	} while (loop++ < 500);
	if (ret) {
		pr_err("fail to input frame queue, timeout.\n");
		ret = -EINVAL;
		goto inq_overflow;
	}

	loop = 0;
	do {
		if(node->port_cfg_cb_func == NULL) {
			pr_err("fail to get buf_cb_func\n");
			return -EINVAL;
		}

		node_param.port_id = PORT_DEC_OUT;
		node->port_cfg_cb_func(&node_param, PORT_CFG_BUFFER_CYCLE, node->port_cfg_cb_handle);
		out_frame = (struct cam_frame *)node_param.param;
		if (out_frame)
			break;
		pr_debug("wait for out buf. loop %d\n", loop);
		os_adapt_time_usleep_range(600, 2000);
	} while (loop++ < 500);
	if (!out_frame || ret) {
		pr_err("fail to get outframe loop cnt %d ret %d\n", loop, ret);
		goto out_err;
	}

	ret = pyrdec_node_dct_blkparam_update(node, decblk_param);
	if (ret) {
		pr_err("fail to update dct blkparam.\n");
		goto out_err;
	}

	if (pframe->common.is_compressed)
		ret = pyrdec_node_afbd_get(node->in_fmt, &node->yuv_afbd_info, pframe);

	ret = pyrdec_node_calc_base_info(node, pframe, out_frame);
	if (ret) {
		pr_err("fail to cal pyrdec base info\n");
		ret = -EINVAL;
		goto calc_err;
	}

	ret = pyrdec_node_calc_overlap_info(node);
	if (ret) {
		pr_err("fail to cal pyrdec overlap info\n");
		ret = -EINVAL;
		goto calc_err;
	}

	/* layer 0 fetch & store size is real size not include padding size */
	node->dec_layer_size[0].w = node->src.w;
	node->dec_layer_size[0].h = node->src.h;

	fmcu->ops->ctx_reset(fmcu);

	ret = pyrdec_node_calc_slice_info(node);
	if (ret) {
		pr_err("fail to cal pyrdec slice info\n");
		ret = -EINVAL;
		goto calc_err;
	}

	ret = wait_for_completion_timeout(&node->frm_done, ISP_CONTEXT_TIMEOUT);
	if (ret == 0) {
		pr_err("fail to wait isp dec context, timeout.\n");
		ret = -EFAULT;
		goto calc_err;
	}

	dec_dev->cur_node_id = node->node_id;
	dec_dev->cur_node_idx = node->node_idx;
	/* start pyr dec fmcu */
	PYR_DEC_DEBUG("pyr dec fmcu start\n");
	fmcu->ops->hw_start(fmcu);

	pyrdec_node_ts_cal(node, dec_dev);

	return 0;

calc_err:
	if (out_frame) {
		ret = cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
		node_param.port_id = PORT_DEC_OUT;
		node_param.param = out_frame;
		node->port_cfg_cb_func(&node_param, PORT_CFG_BUFFER_SET, node->port_cfg_cb_handle);
	}
out_err:
	buf_desc.q_ops_cmd = CAM_QUEUE_TAIL;
	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
	pframe = cam_buf_manager_buf_dequeue(&node->fetch_result_pool, &buf_desc, node->buf_manager_handle);
inq_overflow:
	if (pframe)
		pyrdec_node_src_frame_ret(pframe);
exit:
	return ret;
}

static int pyrdec_node_proc_init(void *handle)
{
	int ret = 0;
	uint32_t i = 0;
	struct pyr_dec_node *node = NULL;
	struct cam_thread_info *thrd = NULL;
	struct cam_frame *decblk_frame = NULL;

	if (!handle) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;
	mutex_init(&node->blkpm_q_lock);
	CAM_QUEUE_INIT(&node->param_share_queue, node->blkparam_buf_num, cam_queue_empty_frame_put);
	CAM_QUEUE_INIT(&node->param_buf_queue, node->blkparam_buf_num, cam_queue_empty_frame_put);
	for (i = 0; i < node->blkparam_buf_num; i++) {
		decblk_frame = cam_queue_empty_frame_get(CAM_FRAME_DEC_BLK);
		if (!decblk_frame) {
			pr_err("fail to get frame.\n");
			ret = -EFAULT;
			goto alloc_err;
		}
		decblk_frame->dec_blk.decblk_pm = cam_buf_kernel_sys_vzalloc(sizeof(struct dcam_isp_k_block));
		if (!decblk_frame->dec_blk.decblk_pm) {
			pr_err("fail to alloc memory.\n");
			ret = -EFAULT;
			goto alloc_err;
		}
		ret = CAM_QUEUE_ENQUEUE(&node->param_share_queue, &decblk_frame->list);
		if (ret) {
			pr_err("fail to enqueue shared buf: %d node id %d\n", i, node->node_id);
			goto alloc_err;
		}
	}

	ret = cam_buf_manager_pool_reg(NULL, PYR_DEC_BUF_Q_LEN, node->buf_manager_handle);
	if (ret < 0) {
		pr_err("fail to reg pool for node_id %d\n", node->node_id);
		goto alloc_err;
	}
	node->fetch_unprocess_pool.private_pool_id = ret;

	ret = cam_buf_manager_pool_reg(NULL, PYR_DEC_BUF_Q_LEN, node->buf_manager_handle);
	if (ret < 0) {
		pr_err("fail to reg pool for node_id %d\n", node->node_id);
		goto result_pool_err;
	}
	node->fetch_result_pool.private_pool_id = ret;

	/* create pyr dec thread */
	thrd = &node->thread;
	thrd->data_cb_func = node->data_cb_func;
	thrd->data_cb_handle = node->data_cb_handle;
	thrd->error_type = CAM_CB_ISP_DEV_ERR;
	snprintf(thrd->thread_name, ARRAY_SIZE(thrd->thread_name), "pyrdec%d", node->node_id);
	ret = camthread_create(node, thrd, pyrdec_node_start_proc);
	if (unlikely(ret != 0)) {
		pr_err("fail to create offline thread for pyr dec\n");
		goto proc_thrd_err;
	}

	return ret;
proc_thrd_err:
	cam_buf_manager_pool_unreg(&node->fetch_result_pool, node->buf_manager_handle);
result_pool_err:
	cam_buf_manager_pool_unreg(&node->fetch_unprocess_pool, node->buf_manager_handle);
alloc_err:
	CAM_QUEUE_CLEAN(&node->param_share_queue, struct cam_frame, list);
	return ret;
}

int pyr_dec_node_request_proc(struct pyr_dec_node *node, void *param)
{
	int ret = 0;
	uint32_t layer_num = PYR_DEC_LAYER_NUM;
	struct cam_frame *pframe = NULL;
	struct isp_pipe_dev *dev = NULL;
	struct cam_pipeline *pipeline = NULL;
	struct cam_node *cam_node = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	if (!node || !param) {
		pr_err("fail to get valid param %p %p\n", node, param);
		return -EFAULT;
	}

	pframe = (struct cam_frame *)param;
	while (isp_rec_small_layer_w(pframe->common.width, layer_num) < MIN_PYR_WIDTH ||
		isp_rec_small_layer_h(pframe->common.height, layer_num) < MIN_PYR_HEIGHT) {
		pr_debug("layer num need decrease based on small input %d %d\n",
			pframe->common.width, pframe->common.height);
		if (--layer_num == 0)
			break;
	}
	/*TBD: yuv sensor temp close pyr*/
	cam_node = (struct cam_node *)node->data_cb_handle;
	if (!layer_num || node->is_yuvsensor == CAM_ENABLE ||
		!node->hw->ip_isp->isphw_abt->pyr_dec_support) {
		pframe->common.pyr_status = CAM_DISABLE;
		pframe->common.link_to.node_type = CAM_NODE_TYPE_ISP_OFFLINE;
		pframe->common.link_to.node_id =
			cam_node->node_graph->outport[PORT_DEC_OUT].link.node_id;
		if (node->data_cb_func)
			node->data_cb_func(CAM_CB_ISP_RET_PYR_DEC_BUF, pframe, node->data_cb_handle);
		return 0;
	} else
		pframe->common.pyr_status = CAM_ENABLE;

	pframe->common.priv_data = node;
	dev = node->dev;
	pipeline = (struct cam_pipeline *)cam_node->data_cb_handle;

	if (pipeline->debug_log_switch)
		pr_info("pipeline_type %s, fid %d, ch_id %d, buf %x, w %d, h %d, pframe->common.is_reserved %d, compress_en %d\n",
			pipeline->pipeline_graph->name, pframe->common.fid, pframe->common.channel_id, pframe->common.buf.mfd,
			pframe->common.width, pframe->common.height, pframe->common.is_reserved, pframe->common.is_compressed);

	node->src.w = pframe->common.width;
	node->src.h = pframe->common.height;
	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_IOVA;
	buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
	ret = cam_buf_manager_buf_enqueue(&node->fetch_unprocess_pool, pframe, &buf_desc, node->buf_manager_handle);
	if (ret == 0)
		complete(&node->thread.thread_com);
	else
		pr_err("fail to enqueue pyrdec frame\n");

	return ret;
}

int pyr_dec_node_blk_param_set(void *handle, void *param)
{
	int ret = 0, index = 0;
	struct pyr_dec_node *node = NULL;
	struct isp_io_param *blk_param = NULL;
	func_cam_cfg_param cfg_fun_ptr = NULL;
	struct cam_hw_block_func_get fucarg = {0};
	struct cam_hw_info *hw = NULL;
	struct isp_pipe_dev *dev = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid param %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;
	blk_param = (struct isp_io_param *)param;
	hw = node->hw;
	dev = node->dev;

	mutex_lock(&dev->path_mutex);
	if (atomic_read(&node->user_cnt) < 1) {
		pr_err("fail to use unable pyrdec node %d.\n", node->node_id);
		mutex_unlock(&dev->path_mutex);
		return -EINVAL;
	}

	/* lock to avoid block param across frame */
	mutex_lock(&node->blkpm_lock);
	index = blk_param->sub_block;
	fucarg.index = index;
	hw->cam_ioctl(hw, CAM_HW_GET_BLK_FUN, &fucarg);
	if (fucarg.cam_entry != NULL &&
		fucarg.cam_entry->sub_block == blk_param->sub_block) {
		cfg_fun_ptr = fucarg.cam_entry->cfg_func;
	} else {
		pr_err("fail to check param, io_param->sub_block = %d, error\n", blk_param->sub_block);
	}
	if (cfg_fun_ptr == NULL) {
		pr_debug("block %d not supported.\n", blk_param->sub_block);
		goto exit;
	}

	ret = cfg_fun_ptr(blk_param, &node->decblk_param);

	mutex_unlock(&node->blkpm_lock);
	mutex_unlock(&dev->path_mutex);
exit:
	return ret;
}

int pyr_dec_node_ctxid_cfg(void *handle, void *param)
{
	int ret = 0;
	struct isp_node *inode = NULL;
	struct pyr_dec_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid param %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;
	inode = (struct isp_node *)param;
	node->isp_node_cfg_id = inode->cfg_id;

	return ret;
}

int pyr_dec_node_base_cfg(void *handle, void *param)
{
	int ret = 0;
	struct pyr_dec_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid param %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;
	node->in_fmt = *((uint32_t *)param);

	return ret;
}

void pyr_dec_node_buffer_clr(void *handle)
{
	struct cam_frame *pframe = NULL;
	struct pyr_dec_node *node = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	node = (struct pyr_dec_node *)handle;
	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_MOVE_TO_ALLOC;
	pframe = cam_buf_manager_buf_dequeue(&node->fetch_result_pool, &buf_desc, node->buf_manager_handle);
	if (!pframe)
		pframe = cam_buf_manager_buf_dequeue(&node->fetch_unprocess_pool, &buf_desc, node->buf_manager_handle);
}

int pyr_dec_node_param_buf_cfg(void *handle, void *param)
{
	int ret = 0;
	struct pyr_dec_node *node = NULL;
	struct cam_frame *decblk_frame = NULL;
	struct cfg_param_status *param_status = NULL;
	struct isp_pipeline_param_n6pro *blkpm_ptr = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid param %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;
	param_status = (struct cfg_param_status *)param;
	if (param_status->status) {
		decblk_frame = CAM_QUEUE_DEQUEUE(&node->param_share_queue, struct cam_frame, list);
		if (!decblk_frame) {
			mutex_lock(&node->blkpm_q_lock);
			decblk_frame = CAM_QUEUE_DEQUEUE(&node->param_buf_queue, struct cam_frame, list);
			mutex_unlock(&node->blkpm_q_lock);
			if (decblk_frame)
				pr_debug("pyr dec node %d deq param fid %d\n", node->node_id, decblk_frame->dec_blk.fid);
		}

		if (decblk_frame) {
			decblk_frame->dec_blk.fid = param_status->frame_id;
			if (decblk_frame->dec_blk.decblk_pm) {
				if (param_status->blkpm_ptr) {
					blkpm_ptr =
						(struct isp_pipeline_param_n6pro *)
							param_status->blkpm_ptr;
					memcpy(&decblk_frame->dec_blk.decblk_pm->dct_info,
						&blkpm_ptr->dct_param,
						sizeof(struct isp_dev_dct_info));
				} else
					memcpy(decblk_frame->dec_blk.decblk_pm,
					&node->decblk_param, sizeof(struct dcam_isp_k_block));
				ret = CAM_QUEUE_ENQUEUE(&node->param_buf_queue, &decblk_frame->list);
				if (ret) {
					pr_err("fail to enquene dec param_buf_queue:state:%d, cnt:%d\n", node->param_buf_queue.state, node->param_buf_queue.cnt);
				CAM_QUEUE_ENQUEUE(&node->param_share_queue, &decblk_frame->list);
				}
			} else
				pr_warn("warning:get decblk frame, but not decblk_pm.\n");
		} else
			pr_warn("warning:not get decblk frame.\n");
	}

	return ret;
}

int pyr_dec_node_postproc_param_cfg(void *handle, void *param)
{
	int ret = 0;
	unsigned long blkpm_addr = 0;
	struct pyr_dec_node *node = NULL;
	struct cam_frame *decblk_frame = NULL;
	struct cam_postproc_param *postproc_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid param %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;
	postproc_param = (struct cam_postproc_param *)param;
	blkpm_addr = (unsigned long)postproc_param->blkpm_ptr;
	decblk_frame = cam_queue_empty_blk_param_get(&node->param_share_queue);
	if (decblk_frame) {
		/* Temp get param from mw by do offset on base addr, need to discuss
			param & image buf share set way in offline proc scene. */
		blkpm_addr = blkpm_addr + sizeof(struct isp_dev_post_cnr_h_info) +
					sizeof(struct isp_dev_ynr_info_v3) +
					sizeof(struct isp_dev_cnr_h_info);
		ret = copy_from_user((void *)&decblk_frame->dec_blk.decblk_pm->dct_info,
			(void __user *)blkpm_addr, sizeof(struct isp_dev_dct_info));
		if (ret)
			pr_warn("Warning:not get the dec dct info.\n");
		decblk_frame->dec_blk.fid = postproc_param->fid;
		decblk_frame->dec_blk.decblk_pm->dct_info.isupdate = 1;
		ret = CAM_QUEUE_ENQUEUE(&node->param_buf_queue, &decblk_frame->list);
		if (ret) {
			pr_err("fail to enquene cap param_buf_queue\n");
			cam_queue_recycle_blk_param(&node->param_share_queue, decblk_frame);
		}
	} else {
		pr_err("fail to recive dec blk param, fid %d\n", postproc_param->fid);
		return -EFAULT;
	}
	return ret;
}

int pyr_dec_node_fast_stop_cfg(void *handle, void *param)
{
	struct pyr_dec_node *node = NULL;
	uint32_t in_queue_cnt = 0, proc_queue_cnt = 0;

	node = VOID_PTR_TO(handle, struct pyr_dec_node);
	node->fast_stop_done = VOID_PTR_TO(param, struct completion);
	in_queue_cnt = cam_buf_manager_pool_cnt(&node->fetch_unprocess_pool, node->buf_manager_handle);
	proc_queue_cnt = cam_buf_manager_pool_cnt(&node->fetch_result_pool, node->buf_manager_handle);
	if (in_queue_cnt == 0 && proc_queue_cnt == 0) {
		node->is_fast_stop = 0;
		complete(node->fast_stop_done);
	} else
		node->is_fast_stop = 1;
	return 0;
}

int pyr_dec_node_clear_fast_stop_cfg(void *handle, void *param)
{
	struct pyr_dec_node *node = NULL;

	node = VOID_PTR_TO(handle, struct pyr_dec_node);
	node->is_fast_stop = 0;
	return 0;
}

int pyr_dec_node_close(void *handle)
{
	int ret = 0, loop = 0;
	struct pyr_dec_node *node = NULL;
	struct pyrdec_pipe_dev *dec_dev = NULL;

	if (!handle) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	node = (struct pyr_dec_node *)handle;
	dec_dev = node->pyrdec_dev;

	camthread_stop(&node->thread);

	ret = wait_for_completion_timeout(&node->frm_done, ISP_CONTEXT_TIMEOUT);
	if (ret == 0)
		pr_err("fail to wait pyr dec, timeout.\n");
	else
		pr_info("wait time %d\n", ret);

	/* make sure irq handler exit to avoid crash */
	while (dec_dev->in_irq_handler && (loop < 1000)) {
		pr_debug("ctx %d in irq. wait %d", node->node_id, loop);
		loop++;
		os_adapt_time_udelay(500);
	};
	if (loop == 1000)
		pr_warn("warning: pyrdec node close wait irq timeout\n");
	pyrdec_node_dev_unbind(node);

	cam_buf_manager_pool_unreg(&node->fetch_unprocess_pool, node->buf_manager_handle);
	cam_buf_manager_pool_unreg(&node->fetch_result_pool, node->buf_manager_handle);
	mutex_lock(&node->blkpm_q_lock);
	CAM_QUEUE_CLEAN(&node->param_share_queue, struct cam_frame, list);
	CAM_QUEUE_CLEAN(&node->param_buf_queue, struct cam_frame, list);
	mutex_unlock(&node->blkpm_q_lock);

	return ret;
}

void *pyr_dec_node_get(uint32_t node_id, struct pyr_dec_node_desc *param)
{
	int ret = 0;
	struct pyr_dec_node *node = NULL;

	if (!param) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	pr_info("node id %d node_dev %p\n", node_id, *param->node_dev);
	if (*param->node_dev == NULL) {
		node = cam_buf_kernel_sys_vzalloc(sizeof(struct pyr_dec_node));
		if (!node) {
			pr_err("fail to get valid isp node\n");
			return NULL;
		}
	} else {
		node = *param->node_dev;
		pr_info("pyr dec node has been alloc %p\n", node);
		goto exit;
	}

	init_completion(&node->frm_done);
	complete(&node->frm_done);
	node->node_id = node_id;
	node->node_idx = param->node_idx;
	node->is_4in1 = param->is_4in1;
	node->dcam_slice_mode = param->dcam_slice_mode;
	node->sn_size = param->sn_size;
	node->is_yuvsensor = param->is_yuvsensor;
	node->layer_num = param->layer_num;
	node->in_fmt = param->in_fmt;
	node->pyr_out_fmt = param->pyr_out_fmt;
	node->hw = param->hw;
	node->dev = param->dev;
	node->pyrdec_dev = param->pyrdec_dev;
	node->buf_manager_handle = param->buf_manager_handle;
	node->blkparam_buf_num = param->blkparam_buf_num;

	if (node->data_cb_func == NULL) {
		node->data_cb_func = param->data_cb_func;
		node->data_cb_handle = param->data_cb_handle;
	}

	if (node->port_cfg_cb_func == NULL) {
		node->port_cfg_cb_func = param->port_cfg_cb_func;
		node->port_cfg_cb_handle = param->port_cfg_cb_handle;
	}

	mutex_init(&node->blkpm_lock);

	ret = pyrdec_node_proc_init(node);
	if (unlikely(ret != 0)) {
		pr_err("fail to proc init for pyr dec\n");
		goto init_dec_err;
	}

	*param->node_dev = node;
	if (node->pyrdec_dev) {
		node->pyrdec_dev->pyrdec_node[node->node_idx][node->node_id] = node;
	} else {
		pr_err("fail to get pyrdec_dev.\n");
		return NULL;
	}

exit:
	atomic_inc(&node->user_cnt);
	pr_info("node id %d, node_idx %d\n", node->node_id, node->node_idx);
	return node;

init_dec_err:
	cam_buf_kernel_sys_vfree(node);
	node = NULL;
	return NULL;

}

void pyr_dec_node_put(struct pyr_dec_node *node)
{
	if (!node) {
		pr_err("fail to get valid pyrdec node\n");
		return;
	}

	if (atomic_dec_return(&node->user_cnt) == 0) {
		node->data_cb_func = NULL;
		node->data_cb_handle = NULL;
		pr_info("pyr dec node %d put success\n", node->node_id);
		cam_buf_kernel_sys_vfree(node);
		node = NULL;
	}

	return;
}

void *pyr_dec_dev_get(void *isp_handle, void *hw)
{
	int ret = 0;
	struct pyrdec_pipe_dev *dec_dev = NULL;
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct pyrdec_hw_k_blk_func irq_func = {0};

	dec_dev = cam_buf_kernel_sys_vzalloc(sizeof(struct pyrdec_pipe_dev));
	if (!dec_dev) {
		pr_err("fail to get dec_dev\n");
		return NULL;
	}
	dec_dev->hw = (struct cam_hw_info *)hw;

	/* get pyr dec irq call back function */
	irq_func.index = PYR_DEC_K_BLK_IRQ_FUNC;
	dec_dev->hw->pyrdec_ioctl(dec_dev->hw, PYRDEC_HW_CFG_K_BLK_FUNC_GET, &irq_func);
	if (irq_func.k_blk_func)
		irq_func.k_blk_func(dec_dev);

	spin_lock_init(&dec_dev->ctx_lock);
	ret = pyrdec_node_irq_request(dec_dev);
	if (unlikely(ret != 0)) {
		pr_err("fail to request irq for isp pyr dec\n");
		goto irq_err;
	}
	fmcu = isp_fmcu_dec_ctx_get(hw);
	if (fmcu && fmcu->ops) {
		fmcu->hw = hw;
		ret = fmcu->ops->ctx_init(fmcu);
		if (ret) {
			pr_err("fail to init fmcu ctx\n");
			isp_fmcu_ctx_desc_put(fmcu);
		} else {
			dec_dev->fmcu_handle = fmcu;
			pr_debug("fmcu %p\n", dec_dev->fmcu_handle);
		}
	} else
		pr_info("no more fmcu or ops\n");

	dec_dev->irq_proc_func = pyrdec_node_irq_proc;
	pr_info("pyrdec dev get success\n");
	return dec_dev;

irq_err:
	if (dec_dev) {
		cam_buf_kernel_sys_vfree(dec_dev);
		dec_dev = NULL;
	}
	return dec_dev;
}

void pyr_dec_dev_put(void *dec_handle)
{
	struct pyrdec_pipe_dev *dec_dev = NULL;
	struct isp_fmcu_ctx_desc *fmcu = NULL;

	if (!dec_handle) {
		pr_err("fail to get valid rec handle\n");
		return;
	}

	dec_dev = (struct pyrdec_pipe_dev *)dec_handle;

	/* irq disable */
	pyrdec_node_irq_free(dec_dev);
	fmcu = (struct isp_fmcu_ctx_desc *)dec_dev->fmcu_handle;
	if (fmcu) {
		fmcu->ops->ctx_deinit(fmcu);
		isp_fmcu_ctx_desc_put(fmcu);
	}
	dec_dev->fmcu_handle = NULL;
	if (dec_dev)
		cam_buf_kernel_sys_vfree(dec_dev);
	dec_dev = NULL;

	pr_info("pyrdec dev put success\n");
	return;
}

