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

#include "cam_zoom.h"
#include "dcam_reg.h"
#include "dcam_hwctx.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_ONLINE_PORT: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define DCAM_PIXEL_ALIGN_WIDTH                   4
#define DCAM_PIXEL_ALIGN_HEIGHT                  2

static int dcamonline_port_scaler_param_calc(struct img_trim *in_trim,
	struct img_size *out_size, struct yuv_scaler_info *scaler,
	struct img_deci_info *deci)
{
	int ret = 0;
	uint32_t tmp_dstsize = 0, align_size = 0, d_max = DCAM_SCALE_DOWN_MAX;

	pr_debug("dcam scaler in_trim_size_x:%d, in_trim_size_y:%d, out_size_w:%d, out_size_h:%d\n",
		in_trim->size_x, in_trim->size_y, out_size->w, out_size->h);

	scaler->scaler_factor_in = in_trim->size_x;
	scaler->scaler_ver_factor_in = in_trim->size_y;
	/* Down scaling should not be smaller then 1/10 */
	if (in_trim->size_x > out_size->w * d_max) {
		tmp_dstsize = out_size->w * d_max;
		deci->deci_x = cam_zoom_port_deci_factor_get(in_trim->size_x, tmp_dstsize, CAM_SCALER_DECI_FAC_MAX);
		deci->deci_x_eb = 1;
		align_size = (1 << (deci->deci_x + 1)) * DCAM_PIXEL_ALIGN_WIDTH;
		in_trim->size_x = (in_trim->size_x) & ~(align_size - 1);
		in_trim->start_x = (in_trim->start_x) & ~(align_size - 1);
		scaler->scaler_factor_in = in_trim->size_x >> (deci->deci_x + 1);
		pr_debug("deci x %d %d, intrim x %d %d, align %d, in %d\n",
			deci->deci_x_eb, deci->deci_x, in_trim->start_x, in_trim->size_x,
			align_size, scaler->scaler_factor_in);
	} else {
		deci->deci_x = 0;
		deci->deci_x_eb = 0;
	}

	if (in_trim->size_y > out_size->h * d_max) {
		tmp_dstsize = out_size->h * d_max;
		deci->deci_y = cam_zoom_port_deci_factor_get(in_trim->size_y, tmp_dstsize, CAM_SCALER_DECI_FAC_MAX);
		deci->deci_y_eb = 1;
		align_size = (1 << (deci->deci_y + 1)) * DCAM_PIXEL_ALIGN_HEIGHT;
		in_trim->size_y = (in_trim->size_y) & ~(align_size - 1);
		in_trim->start_y = (in_trim->start_y) & ~(align_size - 1);
		scaler->scaler_ver_factor_in = in_trim->size_y >> (deci->deci_y + 1);
		pr_debug("deci y %d %d, intrim y %d %d, align %d, in %d\n",
			deci->deci_y_eb, deci->deci_y, in_trim->start_y, in_trim->size_y,
			align_size, scaler->scaler_ver_factor_in);
	} else {
		deci->deci_y = 0;
		deci->deci_y_eb = 0;
	}
	pr_debug("dcam scaler out_size  w %d, h %d, deci eb %d %d\n",
		out_size->w, out_size->h, deci->deci_x_eb, deci->deci_y_eb);

	scaler->scaler_factor_out = out_size->w;
	scaler->scaler_ver_factor_out = out_size->h;
	scaler->scaler_out_width = out_size->w;
	scaler->scaler_out_height = out_size->h;

	return ret;
}

static void dcamonline_port_check_status(struct dcam_online_port *dcam_port,
	struct dcam_hw_context *hw_ctx, struct dcam_isp_k_block *blk_dcam_pm)
{
	struct cam_frame *frame = NULL;
	struct camera_buf_get_desc buf_desc = {0};
	int ret = 0, recycle = 0;

	if (unlikely(!dcam_port || !hw_ctx || !blk_dcam_pm))
		return;

	buf_desc.q_ops_cmd = CAM_QUEUE_DEL_TAIL;
	if (hw_ctx->gtm_hist_stat_bypass)
		recycle = 1;
	if (g_dcam_bypass[hw_ctx->hw_ctx_id] & (1 << _E_GTM))
		recycle = 1;
	if (recycle) {
		pr_debug("dcam hw%d gtm bypass, no need buf\n", hw_ctx->hw_ctx_id);
		frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, &buf_desc, dcam_port->buf_manager_handle);
		if (frame) {
			if (frame->common.is_reserved)
				cam_queue_empty_frame_put(frame);
			else
				ret = cam_buf_manager_buf_enqueue(&dcam_port->unprocess_pool, frame, NULL, dcam_port->buf_manager_handle);
			if (ret)
				pr_err("fail to enqueue gtm\n");
		}
	}
}

static int dcamonline_port_buffer_reset_cfg(void *handle, void *param)
{
	int ret = 0;
	struct dcam_online_port *dcam_online_port = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_buf_pool_id pool_id = {0};
	struct camera_buf_get_desc buf_desc = {0};

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}
	dcam_online_port = (struct dcam_online_port *)handle;
	pframe = (struct cam_frame *)param;

	buf_desc.q_ops_cmd = CAM_QUEUE_DEL_TAIL;
	pframe = cam_buf_manager_buf_dequeue(&dcam_online_port->result_pool, &buf_desc, dcam_online_port->buf_manager_handle);
	while (pframe) {
		pr_debug("port %s fid %u\n", cam_port_name_get(dcam_online_port->port_id), pframe->common.fid);
		if (pframe->common.is_reserved)
			cam_queue_empty_frame_put(pframe);
		else {
			buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_IOVA;
			buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;
			buf_desc.q_ops_cmd = CAM_QUEUE_FRONT;
			pframe->common.is_reserved = 0;
			pframe->common.not_use_isp_reserved_buf = 0;
			pframe->common.priv_data = dcam_online_port;

			if (dcam_online_port->share_full_path)
				pool_id.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
			else
				pool_id.private_pool_id = dcam_online_port->unprocess_pool.private_pool_id;

			ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, &buf_desc, dcam_online_port->buf_manager_handle);
			if (ret) {
				pr_err("fail to enqueue frame of online port %s\n", cam_port_name_get(dcam_online_port->port_id));
				cam_queue_empty_frame_put(pframe);
				return ret;
			}

			pr_debug("port %s, out_buf_queue cnt:%d, addr:%lx.\n", cam_port_name_get(dcam_online_port->port_id),
				cam_buf_manager_pool_cnt(&pool_id, dcam_online_port->buf_manager_handle),
				pframe->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM]);
		}
		buf_desc.q_ops_cmd = CAM_QUEUE_DEL_TAIL;
		pframe = cam_buf_manager_buf_dequeue(&dcam_online_port->result_pool, &buf_desc, dcam_online_port->buf_manager_handle);
	}

	return ret;
}

static struct cam_frame *dcamonline_port_reserved_buf_get(struct dcam_online_port *dcam_port)
{
	struct cam_frame *frame = NULL;

	if (dcam_port->resbuf_get_cb)
		dcam_port->resbuf_get_cb((void *)&frame, dcam_port->resbuf_cb_data);
	if (frame) {
		frame->common.priv_data = dcam_port;
		if (dcam_port->compress_en)
			frame->common.is_compressed = 1;
	}
	pr_debug("dcam use reserved buffer for port %s\n", cam_port_name_get(dcam_port->port_id));
	return frame;
}

int dcamonline_port_zoom_cfg(struct dcam_online_port *dcam_port, struct cam_zoom_base *zoom_base)
{
	int ret = 0;
	uint32_t invalid = 0;
	struct img_size dst_size = {0};
	struct img_size crop_size = {0};

	if (!dcam_port || !zoom_base) {
		pr_err("fail to get valid %p %p\n", dcam_port, zoom_base);
		return -EFAULT;
	}

	switch (dcam_port->port_id) {
	case PORT_RAW_OUT:
	case PORT_FULL_OUT:
		dcam_port->in_size = zoom_base->src;
		dcam_port->in_trim = zoom_base->crop;
		dcam_port->ori_user_trim = zoom_base->ori_user_crop;
		invalid |= ((dcam_port->in_size.w == 0) || (dcam_port->in_size.h == 0));
		invalid |= ((dcam_port->in_trim.start_x + dcam_port->in_trim.size_x) > dcam_port->in_size.w);
		invalid |= ((dcam_port->in_trim.start_y + dcam_port->in_trim.size_y) > dcam_port->in_size.h);

		if (invalid) {
			pr_err("fail to get valid size, size:%d %d, trim %d %d %d %d\n",
				dcam_port->in_size.w, dcam_port->in_size.h,
				dcam_port->in_trim.start_x, dcam_port->in_trim.start_y,
				dcam_port->in_trim.size_x, dcam_port->in_trim.size_y);
			return -EINVAL;
		}
		if ((dcam_port->in_size.w > dcam_port->in_trim.size_x) ||
			(dcam_port->in_size.h > dcam_port->in_trim.size_y)) {
			dcam_port->out_size.w = dcam_port->in_trim.size_x;
			dcam_port->out_size.h = dcam_port->in_trim.size_y;
		} else {
			dcam_port->out_size.w = dcam_port->in_size.w;
			dcam_port->out_size.h = dcam_port->in_size.h;
		}

		if (dcam_port->need_post_proc) {
			dcam_port->in_trim.start_x = 0;
			dcam_port->in_trim.start_y = 0;
			dcam_port->in_trim.size_x = dcam_port->in_size.w;
			dcam_port->in_trim.size_y = dcam_port->in_size.h;
			dcam_port->out_size.w = dcam_port->in_size.w;
			dcam_port->out_size.h = dcam_port->in_size.h;
		}
		dcam_port->out_pitch = cal_sprd_pitch(dcam_port->out_size.w, dcam_port->dcamout_fmt);

		CAM_ZOOM_DEBUG("cfg %s path done. size %d %d %d %d, pitch %d\n",
			dcam_port->port_id == PORT_RAW_OUT ? "raw" : "full", dcam_port->in_size.w, dcam_port->in_size.h,
			dcam_port->out_size.w, dcam_port->out_size.h, dcam_port->out_pitch);
		CAM_ZOOM_DEBUG("sel %d. trim %d %d %d %d\n", dcam_port->raw_src,
			dcam_port->in_trim.start_x, dcam_port->in_trim.start_y,
			dcam_port->in_trim.size_x, dcam_port->in_trim.size_y);
		break;
	case PORT_BIN_OUT:
		dcam_port->in_size = zoom_base->src;
		dcam_port->in_trim = zoom_base->crop;
		dcam_port->out_size = zoom_base->dst;
		dcam_port->zoom_ratio_w = zoom_base->ratio_width;
		dcam_port->total_zoom_crop_w = zoom_base->total_crop_width;
		dcam_port->ori_user_trim = zoom_base->ori_user_crop;
		invalid = 0;
		invalid |= ((dcam_port->in_size.w == 0) || (dcam_port->in_size.h == 0));
		invalid |= ((dcam_port->in_trim.start_x +
				dcam_port->in_trim.size_x) > dcam_port->in_size.w);
		invalid |= ((dcam_port->in_trim.start_y +
				dcam_port->in_trim.size_y) > dcam_port->in_size.h);
		invalid |= dcam_port->in_trim.size_x < dcam_port->out_size.w;
		invalid |= dcam_port->in_trim.size_y < dcam_port->out_size.h;

		if (invalid) {
			pr_err("fail to get valid size, size:%d %d, trim %d %d %d %d, dst %d %d\n",
				dcam_port->in_size.w, dcam_port->in_size.h,
				dcam_port->in_trim.start_x, dcam_port->in_trim.start_y,
				dcam_port->in_trim.size_x, dcam_port->in_trim.size_y,
				dcam_port->out_size.w, dcam_port->out_size.h);
			return -EINVAL;
		}
		crop_size.w = dcam_port->in_trim.size_x;
		crop_size.h = dcam_port->in_trim.size_y;
		dst_size = dcam_port->out_size;
		dcam_port->out_pitch = cal_sprd_pitch(dcam_port->out_size.w, dcam_port->dcamout_fmt);

		switch (dcam_port->dcamout_fmt) {
			case CAM_YUV_BASE:
			case CAM_YUV422_2FRAME:
			case CAM_YVU422_2FRAME:
			case CAM_YUV420_2FRAME:
			case CAM_YVU420_2FRAME:
			case CAM_YUV420_2FRAME_MIPI:
			case CAM_YVU420_2FRAME_MIPI:
				/*TBD: yuvdeci & scaler need confirm*/
				if ((crop_size.w == dst_size.w && crop_size.h == dst_size.h) ||
					dcam_port->sn_if_fmt == DCAM_CAP_MODE_YUV) {
					dcam_port->scaler_sel = PORT_SCALER_BYPASS;
					dcam_port->deci.deci_x_eb = 0;
					dcam_port->deci.deci_y_eb = 0;
					dcam_port->deci.deci_x = 0;
					dcam_port->deci.deci_y = 0;
					break;
				}
				if (dst_size.w > DCAM_SCALER_MAX_WIDTH) {
					pr_err("fail to support scaler, in width %d, out width %d\n",
						dcam_port->in_trim.size_x, dst_size.w);
				}
				dcam_port->scaler_sel = PORT_SCALER_BY_YUVSCALER;
				ret = dcamonline_port_scaler_param_calc(&dcam_port->in_trim, &dst_size,
						&dcam_port->scaler_info, &dcam_port->deci);
				if (ret) {
					pr_err("fail to calc scaler param.\n");
					return ret;
				}

				dcam_port->scaler_info.work_mode = 2;
				dcam_port->scaler_info.scaler_bypass = 0;
				ret = cam_scaler_coeff_calc_ex(&dcam_port->scaler_info);
				if (ret)
					pr_err("fail to calc scaler coeff\n");
				break;
			case CAM_RAW_PACK_10:
			case CAM_RAW_HALFWORD_10:
			case CAM_RAW_14:
			case CAM_RAW_8:
			case CAM_FULL_RGB14:
				if (dcam_port->is_rds_out && (crop_size.w != dst_size.w || crop_size.h != dst_size.h)) {
					dcam_port->scaler_sel = PORT_SCALER_RAW_DOWNSISER;
					dcam_port->gphase.rds_input_h_global = dcam_port->in_trim.size_y;
					dcam_port->gphase.rds_input_w_global = dcam_port->in_trim.size_x;
					dcam_port->gphase.rds_output_w_global = dcam_port->out_size.w;
					dcam_port->gphase.rds_output_h_global = dcam_port->out_size.h;
					pr_debug("rds used mode %d B %d R %d in %d %d, out %d %d\n",
						dcam_port->rds.mode, dcam_port->rds.B, dcam_port->rds.R,
						crop_size.w, crop_size.h, dst_size.w, dst_size.h);
					cam_scaler_rds_phase_info_calc(&dcam_port->gphase);
					cam_scaler_dcam_rds_coeff_gen((uint16_t)crop_size.w,
						(uint16_t)crop_size.h,
						(uint16_t)dst_size.w,
						(uint16_t)dst_size.h,
						(uint32_t *)dcam_port->rds_coeff_buf,
						dcam_port->rds.mode, dcam_port->rds.B, dcam_port->rds.R);
				} else
					dcampath_bin_scaler_get(crop_size, dst_size, &dcam_port->scaler_sel, &dcam_port->bin_ratio);
				break;

			default:
				pr_err("fail to get path->out_fmt :%s\n", camport_fmt_name_get(dcam_port->dcamout_fmt));
				break;
		}

		CAM_ZOOM_DEBUG("cfg bin path done. size %d %d  dst %d %d, pitch %d\n",
			dcam_port->in_size.w, dcam_port->in_size.h,
			dcam_port->out_size.w, dcam_port->out_size.h, dcam_port->out_pitch);
		CAM_ZOOM_DEBUG("scaler %d. trim %d %d %d %d\n", dcam_port->scaler_sel,
			dcam_port->in_trim.start_x, dcam_port->in_trim.start_y,
			dcam_port->in_trim.size_x, dcam_port->in_trim.size_y);
		break;
	default:
		pr_err("fail to get known port %s\n", cam_port_name_get(dcam_port->port_id));
		ret = -EFAULT;
		break;
	}
	return ret;
}

static inline struct cam_frame *dcamonline_port_frame_cycle(struct dcam_online_port *dcam_port, struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	uint32_t path_id = 0;
	struct cam_frame *frame = NULL;
	struct dcam_isp_k_block *blk_dcam_pm = NULL;
	struct cam_buf_pool_id pool_id = {0};
	struct camera_buf_get_desc buf_desc = {0};
	struct cam_zoom_base zoom_base = {0};
	struct cam_zoom_index zoom_index = {0};

	if (!dcam_port || !hw_ctx) {
		pr_err("fail to get input handle:%p, %p.\n", dcam_port, hw_ctx);
		return NULL;
	}

	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_IOVA;
	buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;

	blk_dcam_pm = hw_ctx->blk_pm;
	if (dcam_port->port_id == PORT_FULL_OUT && dcam_port->need_post_proc) {
		pool_id = dcam_port->alter_pool;
		pr_debug("buffer come from alter buffer queue\n");
	} else {
		if (dcam_port->share_full_path)
			pool_id.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
		else
			pool_id = dcam_port->unprocess_pool;
	}

	frame = cam_buf_manager_buf_dequeue(&pool_id, &buf_desc, dcam_port->buf_manager_handle);
	if (unlikely(!frame)) {
		if (dcam_port->port_id == PORT_RAW_OUT &&
			dcam_port->raw_src == BPC_RAW_SRC_SEL) {
			pool_id = dcam_port->alter_pool;
			frame = cam_buf_manager_buf_dequeue(&pool_id, &buf_desc, dcam_port->buf_manager_handle);
			pr_debug("buffer come from alter buffer queue\n");
		}
		if (unlikely(!frame)) {
			frame = dcamonline_port_reserved_buf_get(dcam_port);
			if (!frame) {
				pr_err("fail to get port %s reserved buf\n", cam_port_name_get(dcam_port->port_id));
				return NULL;
			}
		}
	}

	frame->common.fid = hw_ctx->index_to_set;
	path_id = dcamonline_portid_convert_to_pathid(dcam_port->port_id);
	if (path_id >= DCAM_PATH_MAX) {
		pr_err("fail to get correct path id\n");
		return NULL;
	}
	if (!hw_ctx->slowmotion_count)
		frame->common.fid += hw_ctx->hw_path[path_id].frm_deci;
	frame->common.priv_data = dcam_port;
	if (dcam_port->compress_en)
		frame->common.is_compressed = 1;
	frame->common.cam_fmt = dcam_port->dcamout_fmt;
	if (dcam_port->port_id == PORT_FULL_OUT ||
		dcam_port->port_id == PORT_BIN_OUT ||
		dcam_port->port_id == PORT_RAW_OUT) {
		dcam_port->rds.mode = blk_dcam_pm->rds.mode;
		dcam_port->rds.B = blk_dcam_pm->rds.B;
		dcam_port->rds.R = blk_dcam_pm->rds.R;
		memset(&frame->common.zoom_data, 0 , sizeof(struct cam_zoom_frame));
		ret = dcam_port->zoom_cb_func(dcam_port->zoom_cb_handle, &frame->common.zoom_data);
		if (!ret) {
			zoom_index.node_type = CAM_NODE_TYPE_DCAM_ONLINE;
			zoom_index.node_id = DCAM_ONLINE_PRE_NODE_ID;
			zoom_index.port_type = PORT_TRANSFER_OUT;
			zoom_index.port_id = dcam_port->port_id;
			zoom_index.zoom_data = &frame->common.zoom_data;
			ret = cam_zoom_frame_base_get(&zoom_base, &zoom_index);
			if (!ret)
				dcamonline_port_zoom_cfg(dcam_port, &zoom_base);
		}
	}

	ret = cam_buf_manager_buf_enqueue(&dcam_port->result_pool, frame, NULL, dcam_port->buf_manager_handle);
	if (ret) {
		pr_err("fail to set next frm buffer, pool %d, %s\n", dcam_port->result_pool.private_pool_id, cam_port_name_get(dcam_port->port_id));
		if (dcam_port->port_id == PORT_FULL_OUT || dcam_port->port_id == PORT_RAW_OUT || dcam_port->port_id == PORT_VCH2_OUT) {
			buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
			buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;
		}
		if (frame->common.is_reserved)
			cam_queue_empty_frame_put(frame);
		else
			ret = cam_buf_manager_buf_enqueue(&pool_id, frame, &buf_desc, dcam_port->buf_manager_handle);
		if (ret)
			pr_err("fail to enqueue frame\n");
		return NULL;
	}

	return frame;
}

static int dcamonline_port_update_pyr_dec_addr(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, uint32_t idx)
{
	int ret = 0, i = 0;
	uint32_t layer_num = 0;
	uint32_t offset = 0, align = 1, size = 0;
	uint32_t align_w = 0, align_h = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_dec_store_cfg *dec_store = NULL;

	if (!dcam_port || !frame || !hw_ctx) {
		pr_err("fail to check param, port%px, frame%px, hw_ctx%px\n", dcam_port, frame, hw_ctx);
		return -EINVAL;
	}

	dec_store = &dcam_port->dec_store_info;
	hw = hw_ctx->hw;
	dec_store->idx = idx;

	layer_num = DCAM_PYR_DEC_LAYER_NUM;
	/* update layer num based on img size */
	while (isp_rec_small_layer_w(dcam_port->out_size.w, layer_num) < MIN_PYR_WIDTH ||
		isp_rec_small_layer_h(dcam_port->out_size.h, layer_num) < MIN_PYR_HEIGHT) {
		pr_debug("layer num need decrease based on small input %d %d\n",
			dcam_port->out_size.w, dcam_port->out_size.h);
		layer_num--;
		dec_store->layer_num = layer_num;
		hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_BYPASS_DEC, dec_store);
		if (layer_num == 0)
			break;
	}
	frame->common.pyr_status = (layer_num == 0) ? CAM_DISABLE : CAM_ENABLE;

	align_w = dcamonline_dec_align_width(dcam_port->out_size.w, layer_num);
	align_h = dcamonline_dec_align_heigh(dcam_port->out_size.h, layer_num);
	size = dcam_port->out_pitch * dcam_port->out_size.h;
	dec_store->layer_num = layer_num;
	dec_store->align_w = align_w;
	dec_store->align_h = align_h;

	pr_debug("dcam %d out pitch %d addr %lx\n", dec_store->idx, dcam_port->out_pitch, frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM]);
	for (i = 0; i < layer_num; i++) {
		align = align * 2;
		if (i == 0 && frame->common.is_compressed)
			offset += frame->common.fbc_info.buffer_size;
		else
			offset += (size * 3 / 2);
		dec_store->cur_layer = i;
		dec_store->size_t[i].w = align_w / align;
		dec_store->size_t[i].h = align_h / align;
		dec_store->pitch_t[i].pitch_ch0 = cal_sprd_pitch(dec_store->size_t[i].w, dcam_port->pyr_out_fmt);
		dec_store->pitch_t[i].pitch_ch1 = dec_store->pitch_t[i].pitch_ch0;
		size = dec_store->pitch_t[i].pitch_ch0 * dec_store->size_t[i].h;
		dec_store->addr[0] = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM] + offset;
		dec_store->addr[1] = dec_store->addr[0] + size;
		hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_DEC_STORE_ADDR, dec_store);

		pr_debug("dcam %d dec_layer %d w %d h %d\n", dec_store->idx, i, dec_store->size_t[i].w, dec_store->size_t[i].h);
		pr_debug("dcam %d dec_layer %d addr %08x %08xn", dec_store->idx, i, dec_store->addr[0], dec_store->addr[1]);
	}

	return ret;
}

static void dcamonline_port_cfg_store_addr(struct dcam_online_port *dcam_port,struct cam_frame *frame,
	struct dcam_hw_slw_fmcu_cmds *slw, struct dcam_hw_context *hw_ctx, uint32_t idx, struct dcam_isp_k_block *blk_dcam_pm)
{
	uint32_t path_id = 0;
	struct dcam_hw_fbc_addr fbcadr = {0};
	struct dcam_hw_cfg_store_addr store_arg = {0};
	struct cam_hw_info *hw = hw_ctx->hw;

	path_id = dcamonline_portid_convert_to_pathid(dcam_port->port_id);
	if (path_id >= DCAM_PATH_MAX) {
		pr_err("fail to get correct path id\n");
		return;
	}
	if (slw == NULL) {
		if (frame->common.is_compressed) {
			struct compressed_addr fbc_addr = {0};
			struct img_size *size = &dcam_port->out_size;
			struct dcam_compress_cal_para cal_fbc = {0};

			cal_fbc.data_bits = cam_data_bits(dcam_port->dcamout_fmt);
			cal_fbc.fbc_info = &frame->common.fbc_info;
			cal_fbc.in = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
			cal_fbc.fmt = dcam_port->dcamout_fmt;
			cal_fbc.height = size->h;
			cal_fbc.width = size->w;
			cal_fbc.out = &fbc_addr;
			dcam_if_cal_compressed_addr(&cal_fbc);
			fbcadr.idx = idx;
			fbcadr.frame_addr[0] = fbc_addr.addr0;
			fbcadr.frame_addr[1] = fbc_addr.addr1;
			fbcadr.path_id = path_id;
			fbcadr.data_bits = cam_data_bits(dcam_port->dcamout_fmt);
			hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_FBC_ADDR_SET, &fbcadr);
		} else {
			store_arg.idx = idx;
			store_arg.frame_addr[0] = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
			store_arg.frame_addr[1] = 0;
			store_arg.path_id= path_id;
			store_arg.out_fmt = dcam_port->dcamout_fmt;
			store_arg.out_size.h = dcam_port->out_size.h;
			store_arg.out_size.w = dcam_port->out_size.w;
			store_arg.out_pitch = dcam_port->out_pitch;
			store_arg.in_fmt = hw_ctx->cap_info.format;
			store_arg.blk_param = blk_dcam_pm;
			store_arg.frame_size = frame->common.buf.size;
			hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_STORE_ADDR, &store_arg);
		}
	}
}

static void dcamonline_port_update_addr_and_size(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, struct dcam_hw_slw_fmcu_cmds *slw, uint32_t idx, struct dcam_isp_k_block *blk_dcam_pm)
{
	dcamonline_port_cfg_store_addr(dcam_port, frame, slw, hw_ctx, idx, blk_dcam_pm);

	switch (dcam_port->port_id) {
	case PORT_RAW_OUT:
		dcam_hwctx_update_path_size(dcam_port, frame, hw_ctx, idx);
		frame->common.width = dcam_port->out_size.w;
		frame->common.height = dcam_port->out_size.h;
		frame->common.raw_src = dcam_port->raw_src;
		pr_debug("fid %d raw src %d store set\n", frame->common.fid, frame->common.raw_src);
		break;
	case PORT_BIN_OUT:
		if (dcam_port->is_pyr_rec)
			dcamonline_port_update_pyr_dec_addr(dcam_port, frame, hw_ctx, idx);

		if (!frame->common.is_reserved || slw != NULL) {
			if (dcam_port->is_pyr_rec)
				dcam_hwctx_pyr_dec_cfg(dcam_port, frame, hw_ctx, idx);
			dcam_hwctx_update_path_size(dcam_port, frame, hw_ctx, idx);
			hw_ctx->next_roi = dcam_port->in_trim;
			hw_ctx->zoom_ratio = ZOOM_RATIO_DEFAULT * dcam_port->zoom_ratio_w / dcam_port->ori_user_trim.size_x;
			hw_ctx->total_zoom = ZOOM_RATIO_DEFAULT * dcam_port->in_size.w / dcam_port->total_zoom_crop_w;
			pr_debug("total_zoom %d, zoom_ratio %d\n", hw_ctx->total_zoom, hw_ctx->zoom_ratio);
		}
		frame->common.width = dcam_port->out_size.w;
		frame->common.height = dcam_port->out_size.h;
		break;
	case PORT_FULL_OUT:
		dcam_hwctx_update_path_size(dcam_port, frame, hw_ctx, idx);
		frame->common.width = dcam_port->out_size.w;
		frame->common.height = dcam_port->out_size.h;
		frame->common.raw_src = dcam_port->raw_src;
		pr_debug("fid %d raw src %d store set\n", frame->common.fid, frame->common.raw_src);
		break;
	case PORT_AFM_OUT:
		frame->common.afm_info.skip_num = blk_dcam_pm->afm.skip_num;
		frame->common.afm_info.crop_eb = blk_dcam_pm->afm.win_parm.crop_eb;
		frame->common.afm_info.crop_size = blk_dcam_pm->afm.win_parm.crop_size;
		frame->common.afm_info.win = blk_dcam_pm->afm.win_parm.win;
		frame->common.afm_info.win_num = blk_dcam_pm->afm.win_parm.win_num;
		frame->common.afm_info.iir_info = blk_dcam_pm->afm.af_iir_info;
		break;
	case PORT_PDAF_OUT:
		frame->common.pdaf_info.skip_num = blk_dcam_pm->pdaf.skip_num;
		frame->common.pdaf_info.roi_info = blk_dcam_pm->pdaf.roi_info;
		break;
	case PORT_LSCM_OUT:
		frame->common.lscm_info = blk_dcam_pm->lscm;
		break;
	case PORT_VCH2_OUT:
		frame->common.width = dcam_port->out_size.w;
		frame->common.height = dcam_port->out_size.h;
		break;
	default:
		break;
	}

	frame->common.zoom_ratio = hw_ctx->zoom_ratio;
	frame->common.total_zoom = hw_ctx->total_zoom;
}

static void dcamonline_port_aem_update_statis_head(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, struct dcam_isp_k_block *blk_dcam_pm)
{
	unsigned long flags = 0;

	/* Re-config aem win if it is updated */
	spin_lock_irqsave(&blk_dcam_pm->aem_win_lock, flags);
	dcam_k_aem_win(blk_dcam_pm);
	frame->common.aem_info.win = blk_dcam_pm->aem.win_info;
	frame->common.aem_info.skip_num = blk_dcam_pm->aem.skip_num;
	spin_unlock_irqrestore(&blk_dcam_pm->aem_win_lock, flags);
}

static void dcamonline_port_hist_update_statis_head(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, struct dcam_isp_k_block *blk_dcam_pm)
{
	unsigned long flags = 0;
	struct cam_hw_info *hw = NULL;

	hw = hw_ctx->hw;

	/* Re-config hist win if it is updated */
	spin_lock_irqsave(&blk_dcam_pm->hist_update_lock, flags);
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_BAYER_HIST_ROI_UPDATE, blk_dcam_pm);
	frame->common.bayerhist_info.roi_info.stx = blk_dcam_pm->hist.bayerHist_info.bayer_hist_stx;
	frame->common.bayerhist_info.roi_info.sty = blk_dcam_pm->hist.bayerHist_info.bayer_hist_sty;
	frame->common.bayerhist_info.roi_info.endx =
		blk_dcam_pm->hist.bayerHist_info.bayer_hist_endx;

	frame->common.bayerhist_info.roi_info.endy =
		blk_dcam_pm->hist.bayerHist_info.bayer_hist_endy;
	spin_unlock_irqrestore(&blk_dcam_pm->hist_update_lock, flags);

}

static inline int dcamonline_port_hist_out_frm_set(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, uint32_t idx, struct dcam_isp_k_block *blk_dcam_pm)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_cfg_store_addr slw_addr = {0};
	uint32_t port_id = 0;
	unsigned long addr = 0;
	const int _hist = 2;
	int i = 0;
	uint32_t slm_path = 0;

	hw = hw_ctx->hw;
	port_id = dcam_port->port_id;
	slm_path = hw->ip_dcam[idx]->dcamhw_abt->slm_path;

	/* assign last buffer for AEM and HIST in slow motion */
	i = hw_ctx->slowmotion_count - 1;
	if (hw_ctx->slowmotion_count && (!frame->common.fid || (slm_path & BIT(port_id))))
		frame->common.fid += i;

	dcamonline_port_update_addr_and_size(dcam_port, frame, hw_ctx, NULL, idx, blk_dcam_pm);
	dcamonline_port_hist_update_statis_head(dcam_port, frame, hw_ctx, blk_dcam_pm);

	if (hw_ctx->slowmotion_count && !hw_ctx->index_to_set && (slm_path & BIT(port_id))) {
		/* configure reserved buffer for AEM and hist */
		frame = dcamonline_port_reserved_buf_get(dcam_port);
		if (!frame) {
			pr_debug("DCAM%u %s buffer unavailable\n",
				idx, cam_port_name_get(port_id));
			return -ENOMEM;
		}
		i = 1;
		while (i < hw_ctx->slowmotion_count) {
			addr = slowmotion_store_addr[_hist][i];
			slw_addr.reg_addr = addr;
			slw_addr.idx = idx;
			slw_addr.frame_addr[0] = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
			hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_SLW_ADDR, &slw_addr);
			pr_debug("DCAM%u %s set reserved frame\n", idx, cam_port_name_get(port_id));
			i++;
		}

		/* put it back */
		cam_queue_empty_frame_put(frame);
	}
	return ret;
}

static inline int dcamonline_port_aem_out_frm_set(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, uint32_t idx, struct dcam_isp_k_block *blk_dcam_pm)
{
	int ret = 0;
	struct dcam_hw_cfg_store_addr slw_addr = {0};
	struct cam_hw_info *hw = NULL;
	uint32_t port_id = 0;
	unsigned long addr = 0;
	const int _aem = 1;
	int i = 0;
	uint32_t slm_path = 0;

	hw = hw_ctx->hw;
	port_id = dcam_port->port_id;
	slm_path = hw->ip_dcam[idx]->dcamhw_abt->slm_path;

	/* assign last buffer for AEM and HIST in slow motion */
	i = hw_ctx->slowmotion_count - 1;
	if (hw_ctx->slowmotion_count && (!frame->common.fid || (slm_path & BIT(port_id))))
		frame->common.fid += i;

	dcamonline_port_update_addr_and_size(dcam_port, frame, hw_ctx, NULL, idx, blk_dcam_pm);
	dcamonline_port_aem_update_statis_head(dcam_port, frame, hw_ctx, blk_dcam_pm);

	if (hw_ctx->slowmotion_count && !hw_ctx->index_to_set && (slm_path & BIT(port_id))) {
		/* configure reserved buffer for AEM and hist */
		frame = dcamonline_port_reserved_buf_get(dcam_port);
		if (!frame) {
			pr_debug("DCAM%u %s buffer unavailable\n",
				idx, cam_port_name_get(port_id));
			return -ENOMEM;
		}
		i = 1;
		while (i < hw_ctx->slowmotion_count) {
			addr = slowmotion_store_addr[_aem][i];
			slw_addr.reg_addr = addr;
			slw_addr.idx = idx;
			slw_addr.frame_addr[0] = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
			hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_SLW_ADDR, &slw_addr);
			pr_debug("DCAM%u %s set reserved frame\n", idx, cam_port_name_get(port_id));
			i++;
		}

		/* put it back */
		cam_queue_empty_frame_put(frame);
	}

	return ret;
}

static inline int dcamonline_port_bin_out_frm_set(struct dcam_online_port *dcam_port,
	struct cam_frame *frame, struct dcam_hw_context *hw_ctx, uint32_t idx, struct dcam_isp_k_block *blk_dcam_pm)
{
	int ret = 0, i = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_cfg_store_addr slw_addr = {0};
	uint32_t port_id = 0;
	const int _bin = 0;
	unsigned long addr = 0;

	hw = hw_ctx->hw;
	port_id = dcam_port->port_id;

	dcamonline_port_update_addr_and_size(dcam_port, frame, hw_ctx, NULL, idx, blk_dcam_pm);

	if (hw_ctx->slowmotion_count) {
		i = 1;
		while (i < hw_ctx->slowmotion_count) {
			frame = dcamonline_port_frame_cycle(dcam_port, hw_ctx);
			/* in init phase, return failure if error happens */
			if (IS_ERR_OR_NULL(frame) && !hw_ctx->index_to_set)
				ret = -EINVAL;

			/* in normal running, just stop configure */
			if (IS_ERR_OR_NULL(frame))
				break;

			addr = slowmotion_store_addr[_bin][i];
			slw_addr.reg_addr = addr;
			slw_addr.idx = idx;
			slw_addr.frame_addr[0] = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
			hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_SLW_ADDR, &slw_addr);
			atomic_inc(&dcam_port->set_frm_cnt);

			frame->common.fid = hw_ctx->index_to_set + i;

			pr_debug("DCAM%u set frame: fid %u, count %d\n", idx, frame->common.fid,
				atomic_read(&dcam_port->set_frm_cnt));
			i++;
		}

		if (unlikely(i != hw_ctx->slowmotion_count))
			pr_warn("warning: DCAM%u BIN %d frame missed\n",
				idx, hw_ctx->slowmotion_count - i);
	}
	return ret;
}

static inline struct cam_frame *dcamonline_port_slw_frame_cycle(struct dcam_online_port *dcam_port, struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	int slw_idx = 0;
	int port_id = 0;
	struct cam_frame *out_frame = NULL;

	if (!dcam_port || !hw_ctx) {
		pr_err("fail to get valid inptr %p %p\n", dcam_port, hw_ctx);
		return NULL;
	}

	port_id = dcam_port->port_id;
	slw_idx = hw_ctx->slw_idx;

	if (hw_ctx->slw_idx == hw_ctx->slowmotion_count - 1) {
		out_frame = dcamonline_port_frame_cycle(dcam_port, hw_ctx);
		return out_frame;
	}
	switch (port_id) {
	case PORT_RAW_OUT:
	case PORT_FULL_OUT:
	case PORT_BIN_OUT:
	case PORT_PDAF_OUT:
	case PORT_VCH2_OUT:
	case PORT_VCH3_OUT:
		out_frame = dcamonline_port_frame_cycle(dcam_port, hw_ctx);
		pr_debug("enqueue bin path buffer, no.%d, cnt %d\n", slw_idx, cam_buf_manager_pool_cnt(&dcam_port->result_pool, dcam_port->buf_manager_handle));
		break;
	case PORT_FRGB_HIST_OUT:
	case PORT_AFL_OUT:
		out_frame = dcamonline_port_reserved_buf_get(dcam_port);
		if (!out_frame) {
			pr_err("fail to get port %s reserved buf\n", cam_port_name_get(dcam_port->port_id));
			return NULL;
		}
		ret = cam_buf_manager_buf_enqueue(&dcam_port->result_pool, out_frame, NULL, dcam_port->buf_manager_handle);
		if (ret) {
			cam_queue_empty_frame_put(out_frame);
			return ERR_PTR(-ENOMEM);
		}
		break;
	default:
		break;
	}

	return out_frame;
}

static int dcamonline_port_base_cfg(struct dcam_online_port *port, struct dcam_online_port_desc *param)
{
	int ret = 0;

	if (!port || !param) {
		pr_err("fail to get valid param, port=%p, param=%p.\n", port, param);
		return -EFAULT;
	}

	if (port->data_cb_func == NULL) {
		port->data_cb_func = param->data_cb_func;
		port->data_cb_handle = param->data_cb_handle;
	}

	if (port->zoom_cb_func == NULL) {
		port->zoom_cb_func = param->zoom_cb_func;
		port->zoom_cb_handle = param->zoom_cb_handle;
	}

	if (port->resbuf_get_cb == NULL) {
		port->resbuf_get_cb = param->resbuf_get_cb;
		port->resbuf_cb_data = param->resbuf_cb_data;
	}

	port->endian = param->endian;
	port->bayer_pattern = param->bayer_pattern;
	port->sn_if_fmt = param->sn_if_fmt;
	port->dcamout_fmt = param->dcam_out_fmt;

	switch (port->port_id) {
	case PORT_FULL_OUT:
		port->compress_en = param->compress_en;
		port->raw_src = param->is_raw ? ORI_RAW_SRC_SEL : PROCESS_RAW_SRC_SEL;
		pr_info("full path out fmt %s\n", camport_fmt_name_get(port->dcamout_fmt));
		atomic_set(&port->is_work, 1);
		break;
	case PORT_BIN_OUT:
		port->compress_en = param->compress_en;
		port->pyr_out_fmt = param->pyr_out_fmt;
		port->is_pyr_rec = param->is_pyr_rec;
		pr_info("bin path out fmt %s\n", camport_fmt_name_get(port->dcamout_fmt));
		atomic_set(&port->is_work, 1);
		break;
	case PORT_RAW_OUT:
		port->raw_src = param->is_raw ? ORI_RAW_SRC_SEL : param->raw_src;
		pr_info("raw path src %d, raw path out fmt %s\n", port->raw_src, camport_fmt_name_get(port->dcamout_fmt));
		atomic_set(&port->is_work, 1);
		break;
	case PORT_VCH2_OUT:
		port->raw_src = param->is_raw ? 1 : 0;
		if (param->is_raw)
			atomic_set(&port->is_work, 1);
		port->out_size = param->sn_size;
		pr_info("vch2 raw_src %d, out fmt %s size %d %d\n", port->raw_src,
			camport_fmt_name_get(port->dcamout_fmt), port->out_size.w, port->out_size.h);
		break;
	default:
		pr_debug("get known path %s\n", cam_port_name_get(port->port_id));
		break;
	}

	return ret;
}

static int dcamonline_port_slw_store_set(void *handle, void *param)
{
	int ret = 0, slw_idx = 0, port_id = 0, path_id = 0;
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct cam_frame *out_frame = NULL;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_slw_fmcu_cmds *slw = NULL;
	uint32_t idx = 0;
	unsigned long addr_ch = 0;
	struct dcam_isp_k_block *blk_dcam_pm = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}
	dcam_port = (struct dcam_online_port *)handle;
	hw_ctx = (struct dcam_hw_context *)param;

	hw = hw_ctx->hw;
	port_id = dcam_port->port_id;
	slw = &hw_ctx->slw;
	slw_idx = hw_ctx->slw_idx;
	idx = hw_ctx->hw_ctx_id;
	blk_dcam_pm = hw_ctx->blk_pm;

	if (idx >= DCAM_HW_CONTEXT_MAX || !blk_dcam_pm)
		return 0;

	if (atomic_read(&dcam_port->is_work) < 1 || atomic_read(&dcam_port->is_shutoff) > 0)
		return ret;

	out_frame = dcamonline_port_slw_frame_cycle(dcam_port, hw_ctx);
	if (IS_ERR_OR_NULL(out_frame))
		return PTR_ERR(out_frame);

	atomic_inc(&dcam_port->set_frm_cnt);

	out_frame->common.fid = hw_ctx->index_to_set + hw_ctx->slw_idx;
	out_frame->common.pyr_status = CAM_DISABLE;

	path_id = dcamonline_portid_convert_to_pathid(port_id);
	if (path_id >= DCAM_PATH_MAX) {
		pr_err("fail to get correct path id\n");
		return -EFAULT;
	}
	if (out_frame->common.is_compressed) {
		struct compressed_addr fbc_addr;
		struct img_size *size = &dcam_port->out_size;
		struct dcam_compress_cal_para cal_fbc;

		cal_fbc.data_bits = cam_data_bits(dcam_port->dcamout_fmt);
		cal_fbc.fbc_info = &out_frame->common.fbc_info;
		cal_fbc.in = out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
		cal_fbc.fmt = dcam_port->dcamout_fmt;
		cal_fbc.height = size->h;
		cal_fbc.width = size->w;
		cal_fbc.out = &fbc_addr;
		dcam_if_cal_compressed_addr(&cal_fbc);
		slw->store_info[path_id].is_compressed = out_frame->common.is_compressed;
		slw->store_info[path_id].store_addr.addr_ch0 = fbc_addr.addr0;
		slw->store_info[path_id].store_addr.addr_ch1 = fbc_addr.addr1;
		slw->store_info[path_id].store_addr.addr_ch2 = fbc_addr.addr2;
	} else {
		slw->store_info[path_id].store_addr.addr_ch0 = out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];

		if (dcam_port->dcamout_fmt & CAM_YUV_BASE)
			addr_ch = out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM] + dcam_port->out_pitch * dcam_port->out_size.h;

		slw->store_info[path_id].store_addr.addr_ch1 = addr_ch;
	}

	dcamonline_port_update_addr_and_size(dcam_port, out_frame, hw_ctx, slw, idx, blk_dcam_pm);
	switch (port_id) {
	case PORT_AEM_OUT:
		dcamonline_port_aem_update_statis_head(dcam_port, out_frame, hw_ctx, blk_dcam_pm);
		break;
	case PORT_BAYER_HIST_OUT:
		dcamonline_port_hist_update_statis_head(dcam_port, out_frame, hw_ctx, blk_dcam_pm);
		break;
	case PORT_AFM_OUT:
		if (slw && blk_dcam_pm) {
			uint32_t w = 0, h = 0;
			w = blk_dcam_pm->afm.win_parm.win_num.width;
			h = blk_dcam_pm->afm.win_parm.win_num.height;
			slw->store_info[path_id].store_addr.addr_ch1 = out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM] + w * h * 16;
		}
		break;
	}
	pr_debug("path %s set no.%d buffer done!pitch:%d.\n", cam_port_name_get(port_id), hw_ctx->slw_idx, dcam_port->out_pitch);
	return ret;
}

static inline int dcamonline_port_frm_set(struct dcam_online_port *dcam_port, struct cam_frame *frame, struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	switch (dcam_port->port_id) {
	case PORT_BIN_OUT:
		ret = dcamonline_port_bin_out_frm_set(dcam_port, frame, hw_ctx, hw_ctx->hw_ctx_id, hw_ctx->blk_pm);
		break;
	case PORT_AEM_OUT:
		ret = dcamonline_port_aem_out_frm_set(dcam_port, frame, hw_ctx, hw_ctx->hw_ctx_id, hw_ctx->blk_pm);
		break;
	case PORT_BAYER_HIST_OUT:
		ret = dcamonline_port_hist_out_frm_set(dcam_port, frame, hw_ctx, hw_ctx->hw_ctx_id, hw_ctx->blk_pm);
		break;
	case PORT_FULL_OUT:
	case PORT_PDAF_OUT:
	case PORT_AFL_OUT:
	default:
		if (dcam_port->port_id == PORT_GTM_HIST_OUT)
			return 0;
		if (hw_ctx->slowmotion_count && !frame->common.fid &&
			dcam_port->port_id != PORT_AFL_OUT)
			frame->common.fid += hw_ctx->slowmotion_count - 1;
		dcamonline_port_update_addr_and_size(dcam_port, frame, hw_ctx, NULL, hw_ctx->hw_ctx_id, hw_ctx->blk_pm);
		break;
	}
	return ret;
}

static int dcamonline_port_store_reconfig(struct dcam_online_port *dcam_port, void *param)
{
	int ret = 0;
	struct dcam_hw_context *hw_ctx = NULL;
	struct cam_frame *frame = NULL;

	hw_ctx = VOID_PTR_TO(param, struct dcam_hw_context);

	frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, NULL, dcam_port->buf_manager_handle);
	if (IS_ERR_OR_NULL(frame))
		return PTR_ERR(frame);

	ret = cam_buf_manager_buf_enqueue(&dcam_port->result_pool, frame, NULL, dcam_port->buf_manager_handle);
	if (!ret)
		ret = dcamonline_port_frm_set(dcam_port, frame, hw_ctx);
	else
		pr_err("fail to enqueue port %s result_pool\n", cam_port_name_get(dcam_port->port_id));

	return ret;
}

static int dcamonline_port_store_set(void *handle, void *param)
{
	int ret = 0;
	uint32_t idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_frame *frame = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_isp_k_block *blk_dcam_pm = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	dcam_port = (struct dcam_online_port *)handle;
	hw_ctx = (struct dcam_hw_context *)param;

	if (!hw_ctx) {
		pr_err("fail to get hw ctx\n");
		return -EFAULT;
	}

	idx = hw_ctx->hw_ctx_id;
	hw = hw_ctx->hw;
	blk_dcam_pm = hw_ctx->blk_pm;

	if(idx >= DCAM_HW_CONTEXT_MAX || !blk_dcam_pm) {
		pr_err("fail to get info ctx:%d, pm:%p.\n", idx, blk_dcam_pm);
		return 0;
	}

	pr_debug("DCAM%u ONLINE %s enter\n", idx, cam_port_name_get(dcam_port->port_id));
	if (dcam_port->port_id == PORT_GTM_HIST_OUT) {
		hw_ctx->gtm_hist_stat_bypass = hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_GTM_HIST_BYPASS_GET, blk_dcam_pm);
		dcamonline_port_check_status(dcam_port, hw_ctx, blk_dcam_pm);
	}
	frame = dcamonline_port_frame_cycle(dcam_port, hw_ctx);
	if (IS_ERR_OR_NULL(frame))
		return PTR_ERR(frame);

	atomic_inc(&dcam_port->set_frm_cnt);

	pr_debug("dcam%u, fid %u, count %d, port out_size %d %d, is_reserver %d, channel_id %d, addr %08x, fbc %u mfd 0x%x\n",
		idx, frame->common.fid, atomic_read(&dcam_port->set_frm_cnt), dcam_port->out_size.w, dcam_port->out_size.h,
		frame->common.is_reserved, frame->common.channel_id, (uint32_t)frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM],
		frame->common.is_compressed, frame->common.buf.mfd);

	ret = dcamonline_port_frm_set(dcam_port, frame, hw_ctx);
	return ret;

}

static int dcamonline_port_cfg_callback(void *param, uint32_t cmd, void *handle)
{
	int ret = 0;
	struct cam_frame **frame = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	dcam_port = (struct dcam_online_port *)handle;
	switch (cmd) {
	case DCAM_PORT_STORE_SET:
		ret = dcamonline_port_store_set(dcam_port, param);
		break;
	case DCAM_PORT_STORE_RECONFIG:
		ret = dcamonline_port_store_reconfig(dcam_port, param);
		break;
	case DCAM_PORT_SLW_STORE_SET:
		ret = dcamonline_port_slw_store_set(dcam_port, param);
		break;
	case DCAM_PORT_BUFFER_CFG_SET:
		ret = dcam_online_port_buffer_cfg(dcam_port, param);
		break;
	case DCAM_PORT_BUFFER_CFG_GET:
		frame = (struct cam_frame **)param;
		*frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, NULL, dcam_port->buf_manager_handle);
		break;
	case DCAM_PORT_BUFFER_CFG_OUTBUF_GET:
		frame = (struct cam_frame **)param;
		*frame = cam_buf_manager_buf_dequeue(&dcam_port->unprocess_pool, NULL, dcam_port->buf_manager_handle);
		break;
	case DCAM_PORT_NEXT_FRM_BUF_GET:
		frame = (struct cam_frame **)param;
		buf_desc.q_ops_cmd = CAM_QUEUE_TAIL_PEEK;
		*frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, &buf_desc, dcam_port->buf_manager_handle);
		break;
	case DCAM_PORT_BUF_RESET_CFG_SET:
		ret = dcamonline_port_buffer_reset_cfg(dcam_port, param);
		break;
	default:
		pr_err("fail to support port type %d\n", cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

int dcam_online_port_buffer_cfg(void *handle, void *param)
{
	int ret = 0;
	struct dcam_online_port *dcam_online_port = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_buf_pool_id pool_id = {0};
	struct camera_buf_get_desc buf_desc = {0};

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	dcam_online_port = (struct dcam_online_port *)handle;
	pframe = (struct cam_frame *)param;

	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_IOVA;
	if (pframe->common.buf.type== CAM_BUF_USER)
		buf_desc.buf_ops_cmd = CAM_BUF_STATUS_MOVE_TO_ION;
	buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;
	pframe->common.is_reserved = 0;
	pframe->common.not_use_isp_reserved_buf = 0;
	pframe->common.priv_data = dcam_online_port;
	CAM_QUEUE_FRAME_FLAG_RESET(&pframe->common);

	if (dcam_online_port->share_full_path)
		pool_id.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
	else
		pool_id.private_pool_id = dcam_online_port->unprocess_pool.private_pool_id;

	ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, &buf_desc, dcam_online_port->buf_manager_handle);
	if (ret) {
		struct cam_buf_pool_id pool_id = {0};
		pool_id.tag_id = CAM_BUF_POOL_ABNORAM_RECYCLE;
		ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, NULL, dcam_online_port->buf_manager_handle);
		if (ret)
			pr_err("fail to enqueue frame\n");
		pr_err("fail to enqueue frame of online port %s\n", cam_port_name_get(dcam_online_port->port_id));
		return ret;
	}

	pr_debug("port %s, out_buf_queue cnt:%d, addr:%lx.\n", cam_port_name_get(dcam_online_port->port_id),
		cam_buf_manager_pool_cnt(&pool_id, dcam_online_port->buf_manager_handle),
		pframe->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM]);

	return ret;
}

int dcam_online_port_param_cfg(void *handle, enum cam_port_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct dcam_online_port *dcam_port = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}
	dcam_port = (struct dcam_online_port *)handle;

	switch (cmd) {
	case PORT_CFG_BUFFER_SET:
		ret = dcam_online_port_buffer_cfg(dcam_port, param);
		break;
	case PORT_CFG_ZOOM_SET:
		ret = dcamonline_port_zoom_cfg(dcam_port, param);
		break;
	case PORT_CFG_FLASH_SKIP_FID:
		dcam_port->flash_skip_fid = *(uint32_t *)param;
		break;
	case PORT_CFG_PRE_OUT_MODE:
		dcam_port->is_rds_out = CAM_ENABLE;
		break;
	default:
		pr_err("fail to support port type %d\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

int dcam_online_port_skip_num_set(void *dcam_ctx_handle, uint32_t hw_id,
		int path_id, uint32_t skip_num)
{
	struct dcam_pipe_dev *dev = (struct dcam_pipe_dev *)dcam_ctx_handle;
	struct dcam_hw_context *dcam_hw_ctx = NULL;
	struct dcam_hw_path *hw_path = NULL;

	dcam_hw_ctx = &dev->hw_ctx[hw_id];
	if (unlikely(!dcam_hw_ctx || !is_path_id(path_id)))
		return -EINVAL;

	hw_path = &dcam_hw_ctx->hw_path[path_id];
	hw_path->frm_deci = skip_num;
	hw_path->frm_deci_cnt = 0;

	pr_debug("DCAM%u path_id %d set skip_num %u\n", dcam_hw_ctx->hw_ctx_id, path_id, skip_num);

	return 0;
}

int dcam_online_port_buf_alloc(void *handle, struct cam_buf_alloc_desc *param)
{
	int ret = 0;
	uint32_t i = 0, alloc_cnt = 0, total = 0, size = 0, width = 0, height = 0, ch_id = 0, iommu_enable = 0;
	uint32_t dcam_out_bits = 0;
	struct dcam_online_port *port = (struct dcam_online_port *)handle;
	struct cam_frame *pframe = NULL;
	struct dcam_compress_info fbc_info = {0};
	struct dcam_compress_cal_para cal_fbc = {0};
	struct cam_buf_pool_id share_buffer_q = {0};

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -1;
	}

	dcam_out_bits = cam_data_bits(port->dcamout_fmt);
	height = param->height;
	width = param->width;
	ch_id = param->ch_id;
	iommu_enable = param->iommu_enable;

	if (param->compress_en) {
		cal_fbc.data_bits = dcam_out_bits;
		cal_fbc.fbc_info = &fbc_info;
		cal_fbc.fmt = port->dcamout_fmt;
		cal_fbc.height = height;
		cal_fbc.width = width;
		size = dcam_if_cal_compressed_size (&cal_fbc);
		pr_debug("dcam fbc buffer size %u\n", size);
	} else {
		size = cal_sprd_size(width, height, port->dcamout_fmt);
	}

	if (param->is_pyr_rec && port->port_id == PORT_BIN_OUT)
		size += dcam_if_cal_pyramid_size(width, height,
			port->pyr_out_fmt, 1, DCAM_PYR_DEC_LAYER_NUM);
	size = ALIGN(size, CAM_BUF_ALIGN_SIZE);

	total = param->dcamonline_buf_alloc_num;

	if (param->alg_type == ALG_TYPE_CAP_XDR && port->port_id == PORT_RAW_OUT)
		total = 0;

	pr_info("port:%s, cam%d, ch_id %d, buffer size: %u (%u x %u), fmt %s, pyr fmt %s, num %d\n",
		cam_port_name_get(port->port_id), param->cam_idx, ch_id, size, width, height,
		camport_fmt_name_get(port->dcamout_fmt), camport_fmt_name_get(param->pyr_out_fmt), total);

	if ((total == 0) && param->stream_on_buf_com)
		complete(param->stream_on_buf_com);
	for (i = 0; i < total; i++) {
		if (param->alloc_stop_signal && (*param->alloc_stop_signal)) {
			if (param->stream_on_buf_com)
				complete(param->stream_on_buf_com);
			break;
		}
		pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		if (!pframe) {
			pr_err("fail to get frame\n");
			ret = -EINVAL;
			break;
		}
		pframe->common.channel_id = ch_id;
		pframe->common.is_compressed = param->compress_en;
		pframe->common.width = param->width;
		pframe->common.height = param->height;
		pframe->common.fbc_info = fbc_info;
		CAM_QUEUE_FRAME_FLAG_RESET(&pframe->common);

		ret = cam_buf_alloc(&pframe->common.buf, size, iommu_enable);
		if (ret) {
			pr_err("fail to alloc buf: %d ch %d\n", i, ch_id);
			cam_queue_empty_frame_put(pframe);
			continue;
		}

		if ((ch_id != CAM_CH_CAP) || (param->is_static_map)) {
			cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_DCAM);
			if (!param->not_to_isp)
				cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_ISP);
			pframe->common.buf.bypass_iova_ops = CAM_ENABLE;
		}

		if (param->share_buffer && (port->port_id == PORT_FULL_OUT)) {
			share_buffer_q.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
			ret = cam_buf_manager_buf_enqueue(&share_buffer_q, pframe, NULL, port->buf_manager_handle);
		} else
			ret = cam_buf_manager_buf_enqueue(&port->unprocess_pool, pframe, NULL, port->buf_manager_handle);

		if (ret) {
			pr_err("fail to enq, port %s, buffer i=%d\n", cam_port_name_get(port->port_id), i);
			cam_queue_empty_frame_put(pframe);
		}

		alloc_cnt++;
		if ((alloc_cnt == param->stream_on_need_buf_num) && param->stream_on_buf_com)
			complete(param->stream_on_buf_com);
	}

	return ret;
}

void *dcam_online_port_get(uint32_t port_id, struct dcam_online_port_desc *param)
{
	int ret = 0;
	struct dcam_online_port *port = NULL;

	if (!param) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	pr_debug("port id %s param %p node_dev %p\n",
		cam_port_name_get(port_id), param, *param->port_dev);
	if (*param->port_dev == NULL) {
		port = cam_buf_kernel_sys_vzalloc(sizeof(struct dcam_online_port));
		if (!port) {
			pr_err("fail to get valid dcam online port %s\n", cam_port_name_get(port_id));
			return NULL;
		}
	} else if (param->is_raw && (port_id == PORT_VCH2_OUT)) {
		port = *param->port_dev;
		dcamonline_port_base_cfg(port, param);
		atomic_set(&port->is_work, 1);
		pr_debug("dcam online vch2 has been alloc %p %s\n", port, cam_port_name_get(port_id));
		goto exit;
	} else {
		port = *param->port_dev;
		if (param->is_ultr_virtualsensor)
			port->zoom_cb_handle = param->zoom_cb_handle;
		pr_debug("dcam online port has been alloc %p %s\n", port, cam_port_name_get(port_id));
		goto exit;
	}

	port->port_id = port_id;
	atomic_set(&port->user_cnt, 0);
	atomic_set(&port->set_frm_cnt, 0);
	atomic_set(&port->is_work, 0);
	atomic_set(&port->is_shutoff, 0);
	spin_lock_init(&port->state_lock);
	port->port_cfg_cb_func = dcamonline_port_cfg_callback;

	port->shutoff_cb_func = param->shutoff_cb_func;
	port->shutoff_cb_handle = param->shutoff_cb_handle;
	port->buf_manager_handle = param->buf_manager_handle;

	dcamonline_port_base_cfg(port, param);

	ret = cam_buf_manager_pool_reg(NULL, DCAM_OUT_BUF_Q_LEN, port->buf_manager_handle);
	if (ret < 0) {
		pr_err("fail to reg pool for port %s\n", cam_port_name_get(port_id));
		cam_buf_kernel_sys_vfree(port);
		return NULL;
	}
	port->unprocess_pool.private_pool_id = ret;

	ret = cam_buf_manager_pool_reg(NULL, DCAM_OUT_BUF_Q_LEN, port->buf_manager_handle);
	if (ret < 0) {
		pr_err("fail to reg pool for port %s\n", cam_port_name_get(port->port_id));
		cam_buf_manager_pool_unreg(&port->unprocess_pool, port->buf_manager_handle);
		cam_buf_kernel_sys_vfree(port);
		return NULL;
	}
	port->result_pool.private_pool_id = ret;

	ret = cam_buf_manager_pool_reg(NULL, DCAM_OUT_BUF_Q_LEN, port->buf_manager_handle);
	if (ret < 0) {
		pr_err("fail to reg pool for port %s\n", cam_port_name_get(port_id));
		cam_buf_kernel_sys_vfree(port);
		return NULL;
	}
	port->alter_pool.private_pool_id = ret;

	port->is_rds_out = CAM_DISABLE;
	if (port->port_id == PORT_BIN_OUT) {
		port->rds_coeff_size = RDS_COEF_TABLE_SIZE;
		port->rds_coeff_buf = cam_buf_kernel_sys_vzalloc(port->rds_coeff_size);
		if (port->rds_coeff_buf == NULL) {
			port->rds_coeff_size = 0;
			pr_err("fail to alloc rds coeff buffer.\n");
			return NULL;
		}
	}

	if (port->port_id == PORT_FULL_OUT)
		port->share_full_path = param->share_full_path;
	else
		port->share_full_path = 0;
	port->flash_skip_fid = 0;

	*param->port_dev = port;
	pr_info("port %s reg pool %d %d %d\n", cam_port_name_get(port_id), port->unprocess_pool.private_pool_id,
		port->result_pool.private_pool_id, port->alter_pool.private_pool_id);

exit:
	port->data_cb_handle = param->data_cb_handle;
	atomic_inc(&port->user_cnt);
	return port;
}

void dcam_online_port_put(struct dcam_online_port *port)
{
	int ret = 0;
	uint32_t port_id = 0;

	if (!port) {
		pr_err("fail to get invalid port ptr\n");
		return;
	}

	if (atomic_dec_return(&port->user_cnt) == 0) {
		port_id = port->port_id;
		if (port->share_full_path) {
			struct cam_buf_pool_id pool_id = {0};
			struct camera_buf_get_desc buf_desc = {0};
			struct cam_frame *frame_unprocess = NULL;
			struct cam_frame *frame_result_pool = NULL;

			pool_id.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
			buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
			buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;
			do {
				frame_unprocess = cam_buf_manager_buf_dequeue(&port->unprocess_pool, &buf_desc, port->buf_manager_handle);
				if (!frame_unprocess)
					break;
				frame_unprocess->common.blkparam_info.param_block = NULL;
				ret = cam_buf_manager_buf_enqueue(&pool_id, frame_unprocess, &buf_desc, port->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue port %s frame\n", cam_port_name_get(port_id));
			} while(1);

			do {
				frame_result_pool = cam_buf_manager_buf_dequeue(&port->result_pool, &buf_desc, port->buf_manager_handle);
				if (!frame_result_pool)
					break;
				frame_result_pool->common.blkparam_info.param_block = NULL;
				if (frame_result_pool->common.is_reserved)
					cam_queue_empty_frame_put(frame_result_pool);
				else
					ret = cam_buf_manager_buf_enqueue(&pool_id, frame_result_pool, &buf_desc, port->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue port %s frame\n", cam_port_name_get(port_id));
			} while(1);
		}

		if (port->port_id == PORT_BIN_OUT && port->rds_coeff_buf) {
			cam_buf_kernel_sys_vfree(port->rds_coeff_buf);
			port->rds_coeff_buf = NULL;
			port->rds_coeff_size = 0;
		}
		port->data_cb_func = NULL;
		port->port_cfg_cb_func = NULL;
		port->zoom_cb_func = NULL;
		cam_buf_manager_pool_unreg(&port->unprocess_pool, port->buf_manager_handle);
		pr_info("unreg unprocess_pool_id %d\n", port->unprocess_pool.private_pool_id);
		cam_buf_manager_pool_unreg(&port->result_pool, port->buf_manager_handle);
		pr_info("unreg result_pool_id %d\n", port->result_pool.private_pool_id);
		cam_buf_manager_pool_unreg(&port->alter_pool, port->buf_manager_handle);
		pr_info("unreg alter_pool_id %d\n", port->alter_pool.private_pool_id);

		pr_debug("dcam online port %s put success\n", cam_port_name_get(port->port_id));
		cam_buf_kernel_sys_vfree(port);
		port = NULL;
	}
}
