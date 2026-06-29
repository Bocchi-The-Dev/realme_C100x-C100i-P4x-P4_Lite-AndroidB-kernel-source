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

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_ZOOM: %d %d %s : " fmt, current->pid, __LINE__, __func__

static inline uint32_t camzoom_ratio16_divide(uint64_t num, uint32_t ratio16)
{
	return (uint32_t)div64_u64(num << 16, ratio16);
}

static inline uint32_t camzoom_scale_fix(uint32_t size_in, uint32_t ratio16)
{
	uint64_t size_scaled = 0;

	size_scaled = (uint64_t)size_in;
	size_scaled <<= (2 * RATIO_SHIFT);
	size_scaled = ((div64_u64(size_scaled, ratio16)) >> RATIO_SHIFT);
	return (uint32_t)size_scaled;
}

static inline void camzoom_largest_crop_get(
	struct sprd_img_rect *crop_dst, struct sprd_img_rect *crop1)
{
	uint32_t end_x = 0, end_y = 0;
	uint32_t end_x_new = 0, end_y_new = 0;

	if (crop1) {
		end_x = crop_dst->x + crop_dst->w;
		end_y = crop_dst->y + crop_dst->h;
		end_x_new = crop1->x + crop1->w;
		end_y_new = crop1->y + crop1->h;

		crop_dst->x = MIN(crop1->x, crop_dst->x);
		crop_dst->y = MIN(crop1->y, crop_dst->y);
		end_x_new = MAX(end_x, end_x_new);
		end_y_new = MAX(end_y, end_y_new);
		crop_dst->w = end_x_new - crop_dst->x;
		crop_dst->h = end_y_new - crop_dst->y;
	}
}

static inline int camzoom_deci_enable_get(uint32_t intrim_w, uint32_t intrim_h,
	uint32_t dst_w, uint32_t dst_h)
{
	return (intrim_w > DCAM_SCALER_DECI_MAX_WIDTH) &&
		((intrim_w > dst_w * DCAM_SCALE_DOWN_MAX) ||
		(intrim_h > dst_h * DCAM_SCALE_DOWN_MAX));
}

static int camzoom_binning_swapsize_get(struct camera_module *module, struct img_size *max_bin)
{
	uint32_t i = 0, shift = 0, binning_limit = 2;
	uint32_t output_stg = 0;
	struct channel_context *ch_prev = NULL;
	struct channel_context *ch_vid = NULL;
	struct img_size dst_p = {0}, dst_v = {0}, max = {0};

	ch_prev = &module->channel[CAM_CH_PRE];
	ch_vid = &module->channel[CAM_CH_VID];
	output_stg = module->grp->hw_info->ip_dcam[0]->dcamhw_abt->output_strategy;

	dst_p.w = ch_prev->ch_uinfo.dst_size.w;
	dst_p.h = ch_prev->ch_uinfo.dst_size.h;
	dst_v.w = dst_v.h = 1;
	if (ch_vid->enable) {
		dst_p.w = ch_vid->ch_uinfo.dst_size.w;
		dst_p.h = ch_vid->ch_uinfo.dst_size.h;
	}

	switch (output_stg) {
	case IMG_POWER_CONSUM_PRI:
		if ((ch_prev->ch_uinfo.src_size.w * 2) <= module->cam_uinfo.sn_max_size.w) {
			if ((max_bin->w > g_camctrl.isp_linebuf_len) &&
				(dst_p.w <= g_camctrl.isp_linebuf_len) &&
				(dst_v.w <= g_camctrl.isp_linebuf_len))
				shift = 1;

			module->binning_limit = 0;
			if (module->zoom_solution == ZOOM_BINNING4)
				module->binning_limit = 1;
			else if (shift == 1)
				module->binning_limit = 1;
		} else {
			if ((max_bin->w >= (ch_prev->ch_uinfo.dst_size.w * 2)) &&
				(max_bin->w >= (ch_vid->ch_uinfo.dst_size.w * 2)))
				shift = 1;
			else if ((max_bin->w > g_camctrl.isp_linebuf_len) &&
				(dst_p.w <= g_camctrl.isp_linebuf_len) &&
				(dst_v.w <= g_camctrl.isp_linebuf_len))
				shift = 1;

			module->binning_limit = 1;
			if (module->zoom_solution == ZOOM_BINNING4)
				module->binning_limit = 2;
			if ((module->cam_uinfo.sn_size.w > DCAM_24M_WIDTH) && (module->cam_uinfo.sn_size.w <= DCAM_32M_WIDTH)) {
			shift = 2;
			module->binning_limit = 2;
		}
		}
		break;
	case IMG_QUALITY_PRI:
		max.w = max_bin->w;
		while (i < binning_limit) {
			if (max.w > g_camctrl.isp_linebuf_len) {
				max.w = max.w >> 1;
				if ((max.w > dst_p.w) && (max.w > dst_v.w))
					shift++;
				else
					break;
			}
			i++;
		}
		break;
	default:
		pr_err("fail to get invalid output type %d\n", output_stg);
		break;
	}

	if (SEC_UNABLE != module->grp->camsec_cfg.camsec_mode)
		shift = 1;
	max_bin->w >>= shift;
	max_bin->h >>= shift;
	max_bin->w = ALIGN_UP(max_bin->w, DCAM_PATH_CROP_W_ALIGN);
	max_bin->h = ALIGN_UP(max_bin->h, DCAM_PATH_CROP_H_ALIGN);

	pr_debug("shift %d for binning, p=%d v=%d src=%d, %d\n",
		shift, dst_p.w, dst_v.w, max_bin->w, g_camctrl.isp_linebuf_len);

	if ((dst_p.w == 0) || (dst_p.h == 0)) {
		pr_err("fail to get valid w %d h %d\n", dst_p.w, dst_p.h);
		return -EFAULT;
	}
	return 0;
}

static void camzoom_scaler_swapsize_get(struct img_size src_size, struct channel_context *ch_prev,
	struct channel_context *ch_vid, struct img_size *max_scaler, struct img_size *max_bin)
{
	uint32_t ratio_p = 0, ratio_v = 0, ratio_src = 0, ltm_hist_en = 0;
	uint32_t max_dst_size = 0, min_dst_size = 0, min_dst_ratio = 0, max_dst_ratio = 0;
	struct img_size min_swap = {0};
	struct img_size xtm_size = {0};

	/* TBD: slowmotion case dcam keep no scaler first */
	if (ch_prev->ch_uinfo.is_high_fps ||
		camzoom_deci_enable_get(ch_prev->ch_uinfo.src_crop.w, ch_prev->ch_uinfo.src_crop.h, ch_prev->ch_uinfo.dst_size.w, ch_prev->ch_uinfo.dst_size.h)) {
		max_scaler->w = MAX(ch_prev->ch_uinfo.dst_size.w, ch_vid->ch_uinfo.dst_size.w);
		max_scaler->h = MAX(ch_prev->ch_uinfo.dst_size.h, ch_vid->ch_uinfo.dst_size.h);
		max_scaler->w = MAX(max_bin->w, max_scaler->w);
		max_scaler->h = MAX(max_bin->h, max_scaler->h);
		return;
	}

	ratio_src = (1 << RATIO_SHIFT) * src_size.w / src_size.h;
	if (ch_prev->enable)
		ratio_p = (1 << RATIO_SHIFT) * ch_prev->ch_uinfo.dst_size.w / ch_prev->ch_uinfo.dst_size.h;
	if (ch_vid->enable)
		ratio_v = (1 << RATIO_SHIFT) * ch_vid->ch_uinfo.dst_size.w / ch_vid->ch_uinfo.dst_size.h;
	min_dst_ratio = max_dst_ratio = ratio_src;
	if (ratio_p) {
		min_dst_ratio = MIN(min_dst_ratio, ratio_p);
		max_dst_ratio = MAX(max_dst_ratio, ratio_p);
	}
	if (ratio_v) {
		min_dst_ratio = MIN(min_dst_ratio, ratio_v);
		max_dst_ratio = MAX(max_dst_ratio, ratio_v);
	}
	max_scaler->w = MAX(ch_prev->ch_uinfo.dst_size.w, ch_vid->ch_uinfo.dst_size.w);
	max_scaler->h = MAX(ch_prev->ch_uinfo.dst_size.h, ch_vid->ch_uinfo.dst_size.h);
	max_dst_size = MAX(max_scaler->w, max_scaler->h);
	min_dst_size = MIN(max_scaler->w, max_scaler->h);
	pr_debug("ratio %d %d %d max_scaler %d %d min_ratio %d max_ratio %d min_dst %d max_dst %d\n", ratio_p,
		ratio_v, ratio_src, max_scaler->w, max_scaler->h, min_dst_ratio, max_dst_ratio, min_dst_size, max_dst_size);
	/* use max size update swap size */
	max_scaler->w = min_dst_size * max_dst_ratio / (1 << RATIO_SHIFT);
	max_scaler->h = camzoom_ratio16_divide(max_dst_size, min_dst_ratio);
	max_scaler->w = ALIGN_UP(max_scaler->w, 2);
	max_scaler->h = ALIGN_UP(max_scaler->h, 2);
	max_scaler->w = MIN(max_scaler->w, src_size.w);
	max_scaler->h = MIN(max_scaler->h, src_size.h);
	/* for stream on set 10x scenes */
	min_swap.w = src_size.w / DCAM_SCALE_DOWN_MAX;
	min_swap.h = (min_swap.w * src_size.h + src_size.w - 1) / src_size.w;
	min_swap.w = ALIGN_UP(min_swap.w, 4);
	min_swap.h = ALIGN_UP(min_swap.h, 2);
	if (ch_prev->compress_en)
		min_swap.h = ALIGN_UP(min_swap.h, DCAM_FBC_TILE_HEIGHT);
	if (max_scaler->w < min_swap.w)
		max_scaler->w = min_swap.w;
	if (max_scaler->h < min_swap.h)
		max_scaler->h = min_swap.h;
	CAM_PIPEINE_ISP_NODE_CFG(ch_prev,
		CAM_PIPELINE_CFG_XTM_EN, ISP_NODE_MODE_PRE_ID, &ltm_hist_en);
	if (ltm_hist_en) {
		xtm_size.w = LTM_MIN_TILE_WIDTH * 8;
		xtm_size.h = ch_prev->ch_uinfo.dst_size.h * xtm_size.w /
			ch_prev->ch_uinfo.dst_size.w;
		xtm_size.w = MAX(max_scaler->w, xtm_size.w);
		xtm_size.h = MAX(max_scaler->h, xtm_size.h);
		xtm_size.w = ALIGN_UP(xtm_size.w, 4);
		xtm_size.h = ALIGN_UP(xtm_size.h, 2);
		*max_scaler = xtm_size;
	}
}

static void camzoom_rds_swapsize_get(struct img_size src_size, struct img_size *max_rds)
{
	max_rds->w = DCAM_RDS_MAX_OUT_WIDTH;
	max_rds->h = src_size.h * max_rds->w / src_size.w;
	max_rds->w = ALIGN(max_rds->w + DCAM_OUTPUT_DEBUG_ALIGN, DCAM_OUTPUT_DEBUG_ALIGN);
	max_rds->h = ALIGN(max_rds->h + DCAM_OUTPUT_DEBUG_ALIGN, DCAM_OUTPUT_DEBUG_ALIGN);
}

static int camzoom_channel_swapsize_calc(struct camera_module *module)
{
	uint32_t ret = 0;
	struct channel_context *ch_prev = NULL;
	struct channel_context *ch_vid = NULL;
	struct channel_context *ch_cap = NULL;
	struct channel_context *ch_raw = NULL;
	struct img_size max_bypass = {0}, max_bin = {0}, max_scaler = {0}, max = {0}, src_p = {0}, max_rds = {0};

	ch_prev = &module->channel[CAM_CH_PRE];
	ch_cap = &module->channel[CAM_CH_CAP];
	ch_vid = &module->channel[CAM_CH_VID];
	ch_raw = &module->channel[CAM_CH_RAW];

	if (ch_cap->enable) {
		max.w = ch_cap->ch_uinfo.src_size.w;
		max.h = ch_cap->ch_uinfo.src_size.h;
		ch_cap->swap_size = max;
		pr_info("idx %d , cap swap size %d %d\n", module->idx, max.w, max.h);
	}

	if (ch_raw->enable) {
		max.w = ch_raw->ch_uinfo.src_size.w;
		max.h = ch_raw->ch_uinfo.src_size.h;
		ch_raw->swap_size = max;
		pr_info("idx %d , raw swap size %d %d\n", module->idx, max.w, max.h);
	}

	if (ch_prev->enable)
		ch_prev = &module->channel[CAM_CH_PRE];
	else if (!ch_prev->enable && ch_vid->enable)
		ch_prev = &module->channel[CAM_CH_VID];
	else
		return 0;

	if (module->cam_uinfo.is_4in1 || module->cam_uinfo.dcam_slice_mode) {
		ch_prev->ch_uinfo.src_size.w >>= 1;
		ch_prev->ch_uinfo.src_size.h >>= 1;
		ch_vid->ch_uinfo.src_size.w >>= 1;
		ch_vid->ch_uinfo.src_size.h >>= 1;
	}

	src_p.w = ch_prev->ch_uinfo.src_size.w;
	src_p.h = ch_prev->ch_uinfo.src_size.h;

	/* max_bypass: no scaler & binning; max_bin: bining */
	max_bypass = max_bin = src_p;
	/* update max_bin */
	ret = camzoom_binning_swapsize_get(module, &max_bin);

	switch (module->zoom_solution) {
	case ZOOM_DEFAULT:
		ch_prev->swap_size = max_bypass;
		break;
	case ZOOM_BINNING2:
	case ZOOM_BINNING4:
		ch_prev->swap_size = max_bin;
		break;
	case ZOOM_RDS:
		camzoom_rds_swapsize_get(src_p, &max_rds);
		ch_prev->swap_size = max_rds;
		break;
	case ZOOM_SCALER:
		camzoom_scaler_swapsize_get(src_p, ch_prev, ch_vid, &max_scaler, &max_bin);
		ch_prev->swap_size = max_scaler;
		break;
	default:
		pr_warn("warning: unknown zoom solution %d\n", module->zoom_solution);
		ch_prev->swap_size = max_bypass;
		break;
	}
	pr_info("prev bypass size (%d %d), bin size (%d %d), rds size(%d %d)\n",
		max_bypass.w, max_bypass.h, max_bin.w, max_bin.h, max_rds.w, max_rds.h);
	pr_info("prev swap size (%d %d)\n", ch_prev->swap_size.w, ch_prev->swap_size.h);

	return 0;
}

static int camzoom_binning_shift_calc(struct camera_module *module, struct img_trim trim_pv)
{
	uint32_t output_stg = 0;
	uint32_t factor = 0, src_binning = 0;
	uint32_t i = 0, shift = 0, binning_limit = 2;
	struct channel_context *ch_prev = NULL;
	struct channel_context *ch_vid = NULL;
	struct img_size max_dst_pv = {0}, max = {0};

	ch_prev = &module->channel[CAM_CH_PRE];
	ch_vid = &module->channel[CAM_CH_VID];
	output_stg = module->grp->hw_info->ip_dcam[0]->dcamhw_abt->output_strategy;

	switch (output_stg) {
	case IMG_POWER_CONSUM_PRI:
		if ((ch_prev->ch_uinfo.src_size.w * 2) <= module->cam_uinfo.sn_max_size.w)
			src_binning = 1;
		factor = (src_binning ? 10 : 9);
		if ((trim_pv.size_x >= (ch_prev->ch_uinfo.dst_size.w * 4)) &&
			(trim_pv.size_x >= (ch_vid->ch_uinfo.dst_size.w * 4)) &&
			(trim_pv.size_y >= (ch_prev->ch_uinfo.dst_size.h * 4)) &&
			(trim_pv.size_y >= (ch_vid->ch_uinfo.dst_size.h * 4)))
			shift = 2;
		else if ((trim_pv.size_x >= (ch_prev->ch_uinfo.dst_size.w * 2 * factor / 10)) &&
			(trim_pv.size_x >= (ch_vid->ch_uinfo.dst_size.w * 2 * factor / 10)) &&
			(trim_pv.size_y >= (ch_prev->ch_uinfo.dst_size.h * 2 * factor / 10)) &&
			(trim_pv.size_y >= (ch_vid->ch_uinfo.dst_size.h * 2 * factor / 10)))
			shift = 1;
		break;
	case IMG_QUALITY_PRI:
		max_dst_pv.w = MAX(ch_prev->ch_uinfo.dst_size.w, ch_vid->ch_uinfo.dst_size.w);
		max.w = trim_pv.size_x;
		while (i < binning_limit) {
			if (max.w > g_camctrl.isp_linebuf_len) {
				max.w = max.w >> 1;
				if (max.w > max_dst_pv.w)
					shift++;
				else
					break;
			}
			i++;
		}
		break;
	default:
		pr_err("fail to get invalid output type %d\n", output_stg);
		break;
	}
	if (((trim_pv.size_x >> shift) > ch_prev->swap_size.w) ||
		((trim_pv.size_y >> shift) > ch_prev->swap_size.h))
		shift++;
	pr_debug("dcam binning zoom shift %d, type %d\n", shift, output_stg);
	if (shift > 2) {
		pr_info("dcam binning should limit to 1/4\n");
		shift = 2;
	}
	if (shift > module->binning_limit && output_stg == IMG_POWER_CONSUM_PRI) {
		pr_info("bin shift limit to %d\n", module->binning_limit);
		shift = module->binning_limit;
	}

	return shift;
}

static int camzoom_scaler_node_need(struct cam_zoom_base *zoom_base,
	struct cam_zoom_desc *zoom_info, uint32_t port_id)
{
	struct img_size dst = {0};
	struct img_trim crop = {0};

	crop = zoom_base->crop;
	dst = zoom_base->dst;
	if (dst.w > (crop.size_x * ISP_SCALER_UP_MAX) || dst.h > (crop.size_y * ISP_SCALER_UP_MAX)) {
		/*TBD: isp more than two scaler scenes need to confirm*/
		if (dst.w == 0 || dst.h == 0) {
			pr_err("fail to get dst.w %d dst.h %d\n", dst.w, dst.h);
			return 0;
		}
		dst.w = dst.w / ISP_SCALER_UP_MAX;
		dst.w = ALIGN_DOWN(dst.w, ISP_PIXEL_ALIGN_WIDTH);
		dst.h = dst.h / ISP_SCALER_UP_MAX;
		dst.h = ALIGN_DOWN(dst.h, ISP_PIXEL_ALIGN_HEIGHT);
		/* update current isp node dst size */
		zoom_base->dst.w = dst.w;
		zoom_base->dst.h = dst.h;
		/* update next scaler node trim size */
		zoom_info->isp_src_size.w = zoom_base->dst.w;
		zoom_info->isp_src_size.h = zoom_base->dst.h;
		zoom_info->isp_input_crop.start_x = 0;
		zoom_info->isp_input_crop.start_y = 0;
		zoom_info->isp_input_crop.size_x = zoom_info->isp_src_size.w;
		zoom_info->isp_input_crop.size_y = zoom_info->isp_src_size.h;
		zoom_info->isp_crop[port_id].start_x = 0;
		zoom_info->isp_crop[port_id].start_y = 0;
		zoom_info->isp_crop[port_id].size_x = zoom_base->dst.w;
		zoom_info->isp_crop[port_id].size_y = zoom_base->dst.h;
		CAM_ZOOM_DEBUG("isp port %d size update old %d %d new %d %d\n",
			port_id, zoom_info->isp_dst[port_id].w, zoom_info->isp_dst[port_id].h, zoom_base->dst.w, zoom_base->dst.h);

		return 1;
	}
	return 0;
}

static int camzoom_port_info_cfg(struct cam_zoom_port *zoom_port,
	uint32_t node_type, uint32_t node_id, struct cam_zoom_desc *param)
{
	int valid = 0;
	struct cam_zoom_base *zoom_base = NULL;

	zoom_base = &zoom_port->zoom_base;
	switch (node_type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		if (zoom_port->port_id == PORT_BIN_OUT ||
			zoom_port->port_id == PORT_RAW_OUT ||
			zoom_port->port_id == PORT_FULL_OUT) {
			zoom_base->src = param->sn_rect_size;
			zoom_base->crop = param->dcam_crop[node_type][zoom_port->port_id];
			zoom_base->dst = param->dcam_dst[node_type][zoom_port->port_id];
			zoom_base->ratio_width = param->zoom_ratio_width;
			zoom_base->total_crop_width = param->total_crop_width;
			zoom_base->ori_user_crop = param->ori_user_crop;
			valid = 1;
			CAM_ZOOM_DEBUG("%s: dcam port %d size %d %d trim %d %d %d %d dst %d %d\n",
				cam_node_name_get(node_type), zoom_port->port_id, zoom_base->src.w, zoom_base->src.h,
				zoom_base->crop.start_x, zoom_base->crop.start_y, zoom_base->crop.size_x,
				zoom_base->crop.size_y, zoom_base->dst.w, zoom_base->dst.h);
		}
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
	case CAM_NODE_TYPE_ISP_YUV_SCALER:
		if (zoom_port->port_type == PORT_TRANSFER_IN) {
			zoom_base->src = param->isp_src_size;
			zoom_base->crop = param->isp_input_crop;
			zoom_base->dst = zoom_base->src;
			valid = 1;
			CAM_ZOOM_DEBUG("%s: isp size %d %d trim %d %d %d %d dst %d %d\n",
				cam_node_name_get(node_type), zoom_base->src.w, zoom_base->src.h,
				zoom_base->crop.start_x, zoom_base->crop.start_y, zoom_base->crop.size_x,
				zoom_base->crop.size_y, zoom_base->dst.w, zoom_base->dst.h);
		}
		if (zoom_port->port_type == PORT_TRANSFER_OUT &&
			(zoom_port->port_id == PORT_PRE_OUT || zoom_port->port_id == PORT_VID_OUT
			|| zoom_port->port_id == PORT_CAP_OUT || zoom_port->port_id == PORT_THUMB_OUT)) {
			zoom_base->src = param->isp_src_size;
			zoom_base->crop = param->isp_crop[zoom_port->port_id];
			zoom_base->dst = param->isp_dst[zoom_port->port_id];
			if (camzoom_scaler_node_need(zoom_base, param, zoom_port->port_id))
				zoom_base->need_post_proc = ISP_STREAM_POST_PROC;
			valid = 1;
			CAM_ZOOM_DEBUG("%s: isp id %d size %d %d trim %d %d %d %d dst %d %d\n",
				cam_node_name_get(node_type), zoom_port->port_id, zoom_base->src.w, zoom_base->src.h,
				zoom_base->crop.start_x, zoom_base->crop.start_y, zoom_base->crop.size_x,
				zoom_base->crop.size_y, zoom_base->dst.w, zoom_base->dst.h);
		}
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		if (zoom_port->port_id == PORT_OFFLINE_BIN_OUT ||
			zoom_port->port_id == PORT_OFFLINE_RAW_OUT ||
			zoom_port->port_id == PORT_OFFLINE_FULL_OUT) {
			zoom_base->src = param->dcam_src_size;
			zoom_base->crop = param->dcam_crop[node_type][zoom_port->port_id];
			zoom_base->dst = param->dcam_dst[node_type][zoom_port->port_id];
			valid = 1;
			CAM_ZOOM_DEBUG("%s: port_id %d size %d %d trim %d %d %d %d dst %d %d\n",
				cam_node_name_get(node_type), zoom_port->port_id, zoom_base->src.w, zoom_base->src.h,
				zoom_base->crop.start_x, zoom_base->crop.start_y, zoom_base->crop.size_x,
				zoom_base->crop.size_y, zoom_base->dst.w, zoom_base->dst.h);
		}
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		break;
	default :
		pr_err("fail to support port %d zoom cfg in node %d\n", zoom_port->port_id, node_type);
		break;
	}

	return valid;
}

static int camzoom_channel_size_rds_calc(struct camera_module *module)
{
	uint32_t isp_scaler_crop_need = 0;
	uint32_t ltm_hist_en = 0, xtm_need_size = 0;
	uint32_t ratio_min = 0, is_same_fov = 0;
	uint32_t ratio_p_rw = 0, ratio_p_h = 0;
	uint32_t ratio_p_rh = 0, ratio_p_r = 0;
	uint32_t ratio_v_w = 0, ratio_v_h = 0;
	uint32_t ratio16_w = 0, ratio16_h = 0;
	uint32_t align_w = 0, align_h = 0;
	uint32_t ratio_min_w = 0, ratio_min_h = 0;
	struct channel_context *ch_prev = NULL;
	struct channel_context *ch_vid = NULL;
	struct channel_context *ch_cap = NULL;
	struct sprd_img_rect *crop_p = NULL, *crop_v = NULL, *crop_c = NULL;
	struct sprd_img_rect crop_dst = {0};
	struct img_trim trim_pv = {0};
	struct img_trim trim_c = {0};
	struct img_size src_p = {0}, dst_p = {0}, dst_v = {0}, dcam_out = {0};
	struct cam_hw_info *hw = NULL;

	ch_prev = &module->channel[CAM_CH_PRE];
	ch_cap = &module->channel[CAM_CH_CAP];
	ch_vid = &module->channel[CAM_CH_VID];
	if (!ch_prev->enable && !ch_cap->enable && !ch_vid->enable)
		return 0;

	dst_p.w = dst_p.h = 1;
	dst_v.w = dst_v.h = 1;
	ratio_v_w = ratio_v_h = 1;
	crop_p = crop_v = crop_c = NULL;
	if (ch_prev->enable) {
		src_p.w = ch_prev->ch_uinfo.src_size.w;
		src_p.h = ch_prev->ch_uinfo.src_size.h;
		crop_p = &ch_prev->ch_uinfo.src_crop;
		dst_p.w = ch_prev->ch_uinfo.dst_size.w;
		dst_p.h = ch_prev->ch_uinfo.dst_size.h;
		pr_info("src crop prev %u %u %u %u\n",
			crop_p->x, crop_p->y, crop_p->w, crop_p->h);
	}

	if (ch_vid->enable) {
		crop_v = &ch_vid->ch_uinfo.src_crop;
		dst_v.w = ch_vid->ch_uinfo.dst_size.w;
		dst_v.h = ch_vid->ch_uinfo.dst_size.h;
		pr_info("src crop vid %u %u %u %u\n",
			crop_v->x, crop_v->y, crop_v->w, crop_v->h);
	}

	if (ch_cap->enable && ch_prev->enable &&
		(ch_cap->mode_ltm == MODE_LTM_PRE)) {
		crop_c = &ch_cap->ch_uinfo.src_crop;
		is_same_fov = 1;
		pr_info("src crop cap %d %d %d %d\n", crop_c->x, crop_c->y,
			crop_c->x + crop_c->w, crop_c->y + crop_c->h);
	}

	if (ch_prev->enable) {
		crop_dst = *crop_p;
		camzoom_largest_crop_get(&crop_dst, crop_v);
		camzoom_largest_crop_get(&crop_dst, crop_c);
		trim_pv.start_x = crop_dst.x;
		trim_pv.start_y = crop_dst.y;
		trim_pv.size_x = crop_dst.w;
		trim_pv.size_y = crop_dst.h;
	}

	if (is_same_fov)
		trim_c = trim_pv;
	else if (ch_cap->enable) {
		trim_c.start_x = ch_cap->ch_uinfo.src_crop.x & ~1;
		trim_c.start_y = ch_cap->ch_uinfo.src_crop.y & ~1;
		trim_c.size_x = (ch_cap->ch_uinfo.src_crop.w + 1) & ~1;
		trim_c.size_y = (ch_cap->ch_uinfo.src_crop.h + 1) & ~1;

		if (ch_cap->compress_en) {
			uint32_t aligned_y;

			aligned_y = ALIGN_DOWN(trim_c.start_y, 4);
			trim_c.size_y += trim_c.start_y - aligned_y;
			trim_c.start_y = aligned_y;
		}
	}

	pr_info("trim_pv: %u %u %u %u\n", trim_pv.start_x, trim_pv.start_y,
		trim_pv.size_x, trim_pv.size_y);
	pr_info("trim_c: %u %u %u %u\n", trim_c.start_x, trim_c.start_y,
		trim_c.size_x, trim_c.size_y);

	if (ch_prev->enable) {
		ratio_min = 1 << RATIO_SHIFT;
		if (module->zoom_solution >= ZOOM_RDS) {
			ratio_p_rw = (1 << RATIO_SHIFT) * crop_p->w / MAX (DCAM_RDS_MAX_OUT_WIDTH , dst_p.w);
			ratio_p_rh = (1 << RATIO_SHIFT) * crop_p->h / MAX (ch_prev->swap_size.h , dst_p.h);
			ratio_p_r = MAX(ratio_p_rw, ratio_p_rh);
			ratio_p_h = (1 << RATIO_SHIFT) * crop_p->h / dst_p.h;
			ratio_min = MIN(ratio_p_r, ratio_p_h);
			if (ch_vid->enable) {
				ratio_v_w = (1 << RATIO_SHIFT) * crop_v->w / MAX (DCAM_RDS_MAX_OUT_WIDTH , dst_v.w);
				ratio_v_h = (1 << RATIO_SHIFT) * crop_v->h / dst_v.h;
				ratio_min = MIN(ratio_min, MIN(ratio_v_w, ratio_v_h));
			}
			ratio_min = MIN(ratio_min, ((module->rds_limit << RATIO_SHIFT) / 10));
			ratio_min = MAX(ratio_min, (1 << RATIO_SHIFT));
			pr_info("ratio_p_rw %d ratio_p_rh %d, ratio_p_r %d ratio_p_h %d ratio_v %d %d ratio_min %d\n",
				ratio_p_rw, ratio_p_rh, ratio_p_r, ratio_p_h, ratio_v_w, ratio_v_h, ratio_min);
		}

		/* align bin path output size */
		align_w = align_h = DCAM_RDS_OUT_ALIGN;
		align_w = MAX(align_w, DCAM_OUTPUT_DEBUG_ALIGN);

		if (ratio_min == 0) {
			pr_err("fail to get ratio_min %d\n", ratio_min);
			return 0;
		}

		dcam_out.w = camzoom_ratio16_divide(trim_pv.size_x, ratio_min);
		dcam_out.h = camzoom_ratio16_divide(trim_pv.size_y, ratio_min);

		/* avoid isp fetch fbd timeout when isp src width > 1856 */
		hw = module->dcam_dev_handle->hw;
		if ((dcam_out.w > ISP_FBD_MAX_WIDTH) && (hw->prj_id == SHARKL5pro))
			ch_prev->compress_en = 0;

		if (ch_prev->compress_en)
			align_h = MAX(align_h, DCAM_FBC_TILE_HEIGHT);

		dcam_out.w = ALIGN(dcam_out.w, align_w);
		dcam_out.h = ALIGN(dcam_out.h, align_h);

		if (dcam_out.w == 0 || dcam_out.h == 0) {
			pr_err("fail to get dcam_out.w %d dcam_out.h %d\n", dcam_out.w, dcam_out.h);
			return 0;
		}

		/* keep same ratio between width and height */
		ratio16_w = (uint32_t)div_u64((uint64_t)trim_pv.size_x << RATIO_SHIFT, dcam_out.w);
		ratio16_h = (uint32_t)div_u64((uint64_t)trim_pv.size_y << RATIO_SHIFT, dcam_out.h);
		ratio_min = min(ratio16_w, ratio16_h);

		/* if sensor size is too small */
		if (src_p.w < dcam_out.w || src_p.h < dcam_out.h) {
			dcam_out.w = src_p.w;
			dcam_out.h = src_p.h;
			if (ch_prev->compress_en)
				dcam_out.h = ALIGN_DOWN(dcam_out.h,
							DCAM_FBC_TILE_HEIGHT);
			ratio_min = 1 << RATIO_SHIFT;
		}

		if ((1 << RATIO_SHIFT) >= ratio_min) {
			/* enlarge @trim_pv and crop it in isp */
			uint32_t align = 2;/* TODO set to 4 for zzhdr */

			pr_debug("BYPASS RDS OUTPUT\n");
			trim_pv.size_x = max(trim_pv.size_x, dcam_out.w);
			trim_pv.size_y = max(trim_pv.size_y, dcam_out.h);
			trim_pv.size_x = ALIGN(trim_pv.size_x, align >> 1);
			trim_pv.size_y = ALIGN(trim_pv.size_y, align >> 1);
			if (src_p.w >= trim_pv.size_x)
				trim_pv.start_x = (src_p.w - trim_pv.size_x) >> 1;
			else
				trim_pv.start_x = 0;
			if (src_p.h >= trim_pv.size_y)
				trim_pv.start_y = (src_p.h - trim_pv.size_y) >> 1;
			else
				trim_pv.start_y = 0;
			trim_pv.start_x = ALIGN_DOWN(trim_pv.start_x, align);
			trim_pv.start_y = ALIGN_DOWN(trim_pv.start_y, align);

			ratio_min = 1 << RATIO_SHIFT;
			pr_info("trim_pv aligned %u %u %u %u\n",
				trim_pv.start_x, trim_pv.start_y,
				trim_pv.size_x, trim_pv.size_y);
		} else {
			pr_debug("RDS OUTPUT\n");
			dcam_out.w = camzoom_ratio16_divide(trim_pv.size_x, ratio_min);
			dcam_out.h = camzoom_ratio16_divide(trim_pv.size_y, ratio_min);
			dcam_out.w = ALIGN(dcam_out.w, align_w);
			dcam_out.h = ALIGN(dcam_out.h, align_h);
		}

		/* check rds out limit if rds used */
		if (dcam_out.w > DCAM_RDS_MAX_OUT_WIDTH ) {
			dcam_out.w = DCAM_RDS_MAX_OUT_WIDTH ;
			dcam_out.h = dcam_out.w * trim_pv.size_y / trim_pv.size_x;
			dcam_out.w = ALIGN(dcam_out.w, align_w);
			dcam_out.h = ALIGN(dcam_out.h, align_h);

			/* keep same ratio between width and height */
			ratio16_w = (uint32_t)div_u64((uint64_t)trim_pv.size_x << RATIO_SHIFT, dcam_out.w);
			ratio16_h = (uint32_t)div_u64((uint64_t)trim_pv.size_y << RATIO_SHIFT, dcam_out.h);
			ratio_min = min(ratio16_w, ratio16_h);
		}

		pr_info("dst_p %u %u, dst_v %u %u, dcam_out %u %u, ratio %u\n",
			dst_p.w, dst_p.h, dst_v.w, dst_v.h,
			dcam_out.w, dcam_out.h, ratio_min);

		/*limit dcam minimum output size to keep isp ltm hist working*/
		CAM_PIPEINE_ISP_NODE_CFG(ch_prev,
			CAM_PIPELINE_CFG_XTM_EN, ISP_NODE_MODE_PRE_ID, &ltm_hist_en);
		xtm_need_size = (dcam_out.h * LTM_MIN_TILE_WIDTH *
			8 / dcam_out.w) * LTM_MIN_TILE_WIDTH * 8;

		if ((dcam_out.w < LTM_MIN_TILE_WIDTH * 8)
			&& ((atomic_read(&module->state) == CAM_RUNNING)
			|| (atomic_read(&module->state) == CAM_STREAM_ON)) && ltm_hist_en
			&& (ch_prev->swap_size.w * ch_prev->swap_size.h >= xtm_need_size)) {
			isp_scaler_crop_need = 1;

			dcam_out.h = dcam_out.h * LTM_MIN_TILE_WIDTH * 8 / dcam_out.w;
			if (dcam_out.h > ch_prev->ch_uinfo.src_size.h)
				dcam_out.h = ch_prev->ch_uinfo.src_size.h;
			dcam_out.h = ALIGN_DOWN(dcam_out.h, 2);
			dcam_out.w = LTM_MIN_TILE_WIDTH * 8;

			ch_prev->trim_dcam.size_x = MAX(trim_pv.size_x, dcam_out.w);
			ch_prev->trim_dcam.size_y = MAX(trim_pv.size_y, dcam_out.h);
			ch_prev->trim_dcam.size_x = ALIGN(ch_prev->trim_dcam.size_x, 4);
			ch_prev->trim_dcam.size_y = ALIGN(ch_prev->trim_dcam.size_y, 2);

			ch_prev->trim_dcam.start_x =
				((ch_prev->ch_uinfo.src_size.w -
				ch_prev->trim_dcam.size_x) >> 1) & ~1;
			ch_prev->trim_dcam.start_y =
				((ch_prev->ch_uinfo.src_size.h -
				ch_prev->trim_dcam.size_y) >> 1) & ~1;
			ratio_min_w = (1 << RATIO_SHIFT) *
				ch_prev->trim_dcam.size_x / dcam_out.w;
			ratio_min_h = (1 << RATIO_SHIFT) *
				ch_prev->trim_dcam.size_y / dcam_out.h;
			ch_vid->trim_dcam = ch_prev->trim_dcam;
			pr_info("dcam_out w %d h %d, trim_dcam %d %d %d %d\n",
				dcam_out.w, dcam_out.h, ch_prev->trim_dcam.start_x,
				ch_prev->trim_dcam.start_y, ch_prev->trim_dcam.size_x,
				ch_prev->trim_dcam.size_y);
		}

		ch_prev->dst_dcam = dcam_out;
		if (isp_scaler_crop_need) {
			ch_prev->trim_isp.size_x =
				camzoom_ratio16_divide(ch_prev->ch_uinfo.src_crop.w, ratio_min_w);
			ch_prev->trim_isp.size_y =
				camzoom_ratio16_divide(ch_prev->ch_uinfo.src_crop.h, ratio_min_h);
		} else {
			ch_prev->trim_dcam = trim_pv;
			ch_prev->trim_isp.size_x =
				camzoom_ratio16_divide(ch_prev->ch_uinfo.src_crop.w, ratio_min);
			ch_prev->trim_isp.size_y =
				camzoom_ratio16_divide(ch_prev->ch_uinfo.src_crop.h, ratio_min);
		}
		ch_prev->trim_isp.size_x = ALIGN(ch_prev->trim_isp.size_x, DCAM_RDS_OUT_ALIGN);
		ch_prev->trim_isp.size_y = ALIGN(ch_prev->trim_isp.size_y, DCAM_RDS_OUT_ALIGN);
		ch_prev->trim_isp.size_x = min(ch_prev->trim_isp.size_x, dcam_out.w);
		ch_prev->trim_isp.size_y = min(ch_prev->trim_isp.size_y, dcam_out.h);
		ch_prev->trim_isp.start_x =
			(dcam_out.w - ch_prev->trim_isp.size_x) >> 1;
		ch_prev->trim_isp.start_y =
			(dcam_out.h - ch_prev->trim_isp.size_y) >> 1;
		pr_info("trim isp, prev %u %u %u %u\n",
			ch_prev->trim_isp.start_x, ch_prev->trim_isp.start_y,
			ch_prev->trim_isp.size_x, ch_prev->trim_isp.size_y);

		if (ch_vid->enable) {
			ch_vid->dst_dcam = dcam_out;
			if (isp_scaler_crop_need) {
				ch_vid->trim_isp.size_x =
					camzoom_ratio16_divide(ch_vid->ch_uinfo.src_crop.w,
						ratio_min_w);
				ch_vid->trim_isp.size_y =
					camzoom_ratio16_divide(ch_vid->ch_uinfo.src_crop.h,
						ratio_min_h);
			} else {
				ch_vid->trim_dcam = trim_pv;
				ch_vid->trim_isp.size_x =
					camzoom_ratio16_divide(ch_vid->ch_uinfo.src_crop.w,
						ratio_min);
				ch_vid->trim_isp.size_y =
					camzoom_ratio16_divide(ch_vid->ch_uinfo.src_crop.h,
						ratio_min);
			}
			ch_vid->trim_isp.size_x = ALIGN(ch_vid->trim_isp.size_x, DCAM_RDS_OUT_ALIGN);
			ch_vid->trim_isp.size_y = ALIGN(ch_vid->trim_isp.size_y, DCAM_RDS_OUT_ALIGN);
			ch_vid->trim_isp.size_x = min(ch_vid->trim_isp.size_x, dcam_out.w);
			ch_vid->trim_isp.size_y = min(ch_vid->trim_isp.size_y, dcam_out.h);
			ch_vid->trim_isp.start_x =
				(dcam_out.w - ch_vid->trim_isp.size_x) >> 1;
			ch_vid->trim_isp.start_y =
				(dcam_out.h - ch_vid->trim_isp.size_y) >> 1;
			pr_info("trim isp, vid %d %d %d %d\n",
				ch_vid->trim_isp.start_x, ch_vid->trim_isp.start_y,
				ch_vid->trim_isp.size_x, ch_vid->trim_isp.size_y);
		}
	}

	if (ch_cap->enable) {
		ch_cap->trim_dcam = trim_c;
		ch_cap->dst_dcam.w = ch_cap->trim_dcam.size_x;
		ch_cap->dst_dcam.h = ch_cap->trim_dcam.size_y;
		cam_zoom_diff_trim_get(&ch_cap->ch_uinfo.src_crop,
			(1 << RATIO_SHIFT), &trim_c, &ch_cap->trim_isp);
		ch_cap->trim_isp.start_x &= ~1;
		ch_cap->trim_isp.start_y &= ~1;
		ch_cap->trim_isp.size_x &= ~1;
		ch_cap->trim_isp.size_y &= ~1;
		if (ch_cap->trim_isp.size_x > trim_c.size_x)
			ch_cap->trim_isp.size_x = trim_c.size_x;
		if (ch_cap->trim_isp.size_y > trim_c.size_y)
			ch_cap->trim_isp.size_y = trim_c.size_y;
		pr_info("trim isp, cap %d %d %d %d\n",
			ch_cap->trim_isp.start_x, ch_cap->trim_isp.start_y,
			ch_cap->trim_isp.size_x, ch_cap->trim_isp.size_y);
	}

	pr_info("done.\n");

	return 0;
}

static int camzoom_channel_size_normal_calc(struct camera_module *module)
{
	uint32_t shift = 0, align_size = 0;
	uint32_t ratio_p_w = 0, ratio_p_h = 0, ratio_v_w = 0, ratio_v_h = 0;
	uint32_t ratio_min_w = 0, ratio_min_h = 0, ratio_min = 0;
	uint32_t isp_scaler_crop_need = 0, ltm_hist_en = 0;
	uint32_t trim_pv_size = 0, swap_size = 0, xtm_need_size = 0;
	struct channel_context *ch_prev = NULL;
	struct channel_context *ch_vid = NULL;
	struct channel_context *ch_cap = NULL;
	struct channel_context *ch_cap_thm = NULL;
	struct sprd_img_rect *crop_p = NULL, *crop_v = NULL;
	struct sprd_img_rect *total_crop_p = NULL;
	struct sprd_img_rect crop_dst = {0};
	struct sprd_img_rect total_crop_dst = {0};
	struct img_trim total_trim_pv = {0};
	struct img_trim trim_pv = {0};
	struct img_trim trim_c = {0};
	struct img_trim *isp_trim = NULL;
	struct img_size src_p = {0}, dst_p = {0}, dst_v = {0}, dcam_out = {0}, max_dst_pv = {0};

	ch_prev = &module->channel[CAM_CH_PRE];
	ch_cap = &module->channel[CAM_CH_CAP];
	ch_vid = &module->channel[CAM_CH_VID];
	ch_cap_thm = &module->channel[CAM_CH_CAP_THM];
	if (!ch_prev->enable && !ch_cap->enable && !ch_vid->enable)
		return 0;

	dst_p.w = dst_p.h = 1;
	dst_v.w = dst_v.h = 1;
	if (ch_prev->enable || (!ch_prev->enable && ch_vid->enable)) {
		src_p.w = ch_prev->ch_uinfo.src_size.w;
		src_p.h = ch_prev->ch_uinfo.src_size.h;
		crop_p = &ch_prev->ch_uinfo.src_crop;
		total_crop_p = &ch_prev->ch_uinfo.total_src_crop;
		dst_p.w = ch_prev->ch_uinfo.dst_size.w;
		dst_p.h = ch_prev->ch_uinfo.dst_size.h;
		pr_info("src crop prev %u %u %u %u\n", crop_p->x, crop_p->y, crop_p->w, crop_p->h);
	}
	if (ch_vid->enable) {
		crop_v = &ch_vid->ch_uinfo.src_crop;
		dst_v.w = ch_vid->ch_uinfo.dst_size.w;
		dst_v.h = ch_vid->ch_uinfo.dst_size.h;
		pr_info("src crop vid %u %u %u %u\n",
			crop_v->x, crop_v->y, crop_v->w, crop_v->h);
	}
	if (ch_prev->enable || (!ch_prev->enable && ch_vid->enable)) {
		crop_dst = *crop_p;
		camzoom_largest_crop_get(&crop_dst, crop_v);
		trim_pv.start_x = crop_dst.x;
		trim_pv.start_y = crop_dst.y;
		trim_pv.size_x = crop_dst.w;
		trim_pv.size_y = crop_dst.h;
		max_dst_pv.w = MAX(dst_p.w, dst_v.w);
		max_dst_pv.h = MAX(dst_p.h, dst_v.h);
	}

	if (ch_cap->enable) {
		trim_c.start_x = ch_cap->ch_uinfo.src_crop.x;
		trim_c.start_y = ch_cap->ch_uinfo.src_crop.y;
		trim_c.size_x = ch_cap->ch_uinfo.src_crop.w;
		trim_c.size_y = ch_cap->ch_uinfo.src_crop.h;
	}
	pr_info("trim_pv: %u %u %u %u\n", trim_pv.start_x,
		trim_pv.start_y, trim_pv.size_x, trim_pv.size_y);
	pr_info("trim_c: %u %u %u %u\n", trim_c.start_x,
		trim_c.start_y, trim_c.size_x, trim_c.size_y);

	if (ch_prev->enable || (!ch_prev->enable && ch_vid->enable)) {
		if (module->zoom_solution == ZOOM_BINNING2 || module->zoom_solution == ZOOM_BINNING4)
			shift = camzoom_binning_shift_calc(module, trim_pv);
		if (shift == 2) {
			/* make sure output 2 aligned and trim invalid */
			pr_debug("shift 2 trim_pv %d %d %d %d\n",
				trim_pv.start_x, trim_pv.start_y,
				trim_pv.size_x, trim_pv.size_y);
			if ((trim_pv.size_x >> 2) & 1) {
				trim_pv.size_x = (trim_pv.size_x + 4) & ~7;
				if ((trim_pv.start_x + trim_pv.size_x) > src_p.w)
					trim_pv.size_x -= 8;
				trim_pv.start_x = (src_p.w - trim_pv.size_x) >> 1;
			}
			if ((trim_pv.size_y >> 2) & 1) {
				trim_pv.size_y = (trim_pv.size_y + 4) & ~7;
				if ((trim_pv.start_y + trim_pv.size_y) > src_p.h)
					trim_pv.size_y -= 8;
				trim_pv.start_y = (src_p.h - trim_pv.size_y) >> 1;
			}
			pr_debug("shift 2 trim_pv final %d %d %d %d\n",
				trim_pv.start_x, trim_pv.start_y,
				trim_pv.size_x, trim_pv.size_y);
		}

		if (shift == 1)
			align_size = 8;
		else if (shift == 2)
			align_size = 16;
		else
			align_size = 4;

		trim_pv.size_x = ALIGN_DOWN(trim_pv.size_x, align_size);
		trim_pv.size_y = ALIGN_DOWN(trim_pv.size_y, align_size / 2);

		dcam_out.w = (trim_pv.size_x >> shift);
		dcam_out.w = ALIGN_DOWN(dcam_out.w, 2);
		dcam_out.h = (trim_pv.size_y >> shift);
		dcam_out.h = ALIGN_DOWN(dcam_out.h, 2);

		if (module->zoom_solution == ZOOM_SCALER) {
			ratio_min = 1 << RATIO_SHIFT;
			ratio_min_w = 1 << RATIO_SHIFT;
			ratio_min_h = 1 << RATIO_SHIFT;
			/* TBD: slowmotion case dcam keep no scaler first */
			if (ch_prev->ch_uinfo.is_high_fps) {
				dst_p = ch_prev->swap_size;
				dst_v = ch_prev->swap_size;
			}
			if (trim_pv.size_x > ch_prev->swap_size.w || trim_pv.size_y > ch_prev->swap_size.h) {
				ratio_p_w = (1 << RATIO_SHIFT) * trim_pv.size_x / dst_p.w;
				ratio_p_h = (1 << RATIO_SHIFT) * trim_pv.size_y / dst_p.h;
				ratio_v_w = (1 << RATIO_SHIFT) * trim_pv.size_x / dst_v.w;
				ratio_v_h = (1 << RATIO_SHIFT) * trim_pv.size_y / dst_v.h;
				ratio_min = MIN(MIN(ratio_p_w, ratio_p_h), MIN(ratio_v_w, ratio_v_h));
				ratio_min_w = ratio_min_h = ratio_min;
				dcam_out.w = camzoom_ratio16_divide(trim_pv.size_x, ratio_min);
				dcam_out.h = camzoom_ratio16_divide(trim_pv.size_y, ratio_min);
				dcam_out.w = ALIGN(dcam_out.w, 4);
				dcam_out.h = ALIGN(dcam_out.h, 2);
				if ((dcam_out.w != max_dst_pv.w && abs(dcam_out.w - max_dst_pv.w) <= DCAM_PATH_CROP_W_ALIGN) ||
					(dcam_out.h != max_dst_pv.h && abs(dcam_out.h - max_dst_pv.h) <= DCAM_PATH_CROP_H_ALIGN)) {
					dcam_out.w = dcam_out.w > max_dst_pv.w ? max_dst_pv.w : dcam_out.w;
					dcam_out.h = dcam_out.h > max_dst_pv.h ? max_dst_pv.h : dcam_out.h;
					if (dcam_out.w == 0 || dcam_out.h == 0) {
						pr_err("fail to get dcam_out.w %d dcam_out.h %d\n", dcam_out.w, dcam_out.h);
						return 0;
					}
					ratio_min_w = (1 << RATIO_SHIFT) * trim_pv.size_x / dcam_out.w;
					ratio_min_h = (1 << RATIO_SHIFT) * trim_pv.size_y / dcam_out.h;
					pr_debug("ratio %d %d\n", ratio_min_w, ratio_min_h);
				}
				if (abs(dcam_out.w - max_dst_pv.w) > DCAM_PATH_CROP_W_ALIGN ||
					abs(dcam_out.h - max_dst_pv.h) > DCAM_PATH_CROP_H_ALIGN)
					pr_warn("warning: inconsistent scaling of width and height\n");
			}

			if (camzoom_deci_enable_get(trim_pv.size_x, trim_pv.size_y, dcam_out.w, dcam_out.h)) {
				dcam_out.w = trim_pv.size_x / DCAM_SCALE_DOWN_MAX;
				dcam_out.h = (dcam_out.w * trim_pv.size_y + trim_pv.size_x - 1) / trim_pv.size_x;
				dcam_out.w = ALIGN(dcam_out.w, 4);
				dcam_out.h = ALIGN(dcam_out.h, 2);
				if (ch_prev->compress_en)
					dcam_out.h = ALIGN_UP(dcam_out.h, DCAM_FBC_TILE_HEIGHT);
				ratio_min_w = (1 << RATIO_SHIFT) * trim_pv.size_x / dcam_out.w;
				ratio_min_h = (1 << RATIO_SHIFT) * trim_pv.size_y / dcam_out.h;
			}
		}

		/* applied latest rect for aem */
		ch_prev->trim_dcam = trim_pv;

		if (dcam_out.w > DCAM_SCALER_MAX_WIDTH) {
			dcam_out.h = dcam_out.h * DCAM_SCALER_MAX_WIDTH / dcam_out.w;
			dcam_out.h = ALIGN_DOWN(dcam_out.h, 2);
			dcam_out.w = DCAM_SCALER_MAX_WIDTH;
			ratio_min_w = (1 << RATIO_SHIFT) * trim_pv.size_x / dcam_out.w;
			ratio_min_h = (1 << RATIO_SHIFT) * trim_pv.size_y / dcam_out.h;
		}

		/*limit dcam minimum output size to keep isp ltm hist working*/
		CAM_PIPEINE_ISP_NODE_CFG(ch_prev,
			CAM_PIPELINE_CFG_XTM_EN, ISP_NODE_MODE_PRE_ID, &ltm_hist_en);
		if (module->zoom_solution == ZOOM_SCALER || shift == 0)
			xtm_need_size = (dcam_out.h * LTM_MIN_TILE_WIDTH *
			8 / dcam_out.w) * LTM_MIN_TILE_WIDTH * 8;
		else
			xtm_need_size = trim_pv.size_x * trim_pv.size_y;

		if ((dcam_out.w < LTM_MIN_TILE_WIDTH * 8)
			&& ((atomic_read(&module->state) == CAM_RUNNING)
			|| (atomic_read(&module->state) == CAM_STREAM_ON)) && ltm_hist_en
			&& (ch_prev->swap_size.w * ch_prev->swap_size.h >= xtm_need_size)) {
			isp_scaler_crop_need = 1;
			if (module->zoom_solution == ZOOM_SCALER) {
				dcam_out.h = dcam_out.h * LTM_MIN_TILE_WIDTH * 8 / dcam_out.w;
				if (dcam_out.h > ch_prev->ch_uinfo.src_size.h)
					dcam_out.h = ch_prev->ch_uinfo.src_size.h;

				dcam_out.h = ALIGN_DOWN(dcam_out.h, 2);
				dcam_out.w = LTM_MIN_TILE_WIDTH * 8;
				ch_prev->trim_dcam.size_x = MAX(trim_pv.size_x, dcam_out.w);
				ch_prev->trim_dcam.size_y = MAX(trim_pv.size_y, dcam_out.h);
				ch_prev->trim_dcam.size_x = ALIGN(ch_prev->trim_dcam.size_x, 4);
				ch_prev->trim_dcam.size_y = ALIGN(ch_prev->trim_dcam.size_y, 2);
				ch_prev->trim_dcam.start_x =
					((ch_prev->ch_uinfo.src_size.w -
					ch_prev->trim_dcam.size_x) >> 1) & ~1;
				ch_prev->trim_dcam.start_y =
					((ch_prev->ch_uinfo.src_size.h -
					ch_prev->trim_dcam.size_y) >> 1) & ~1;
				ratio_min_w = (1 << RATIO_SHIFT) *
					ch_prev->trim_dcam.size_x / dcam_out.w;
				ratio_min_h = (1 << RATIO_SHIFT) *
					ch_prev->trim_dcam.size_y / dcam_out.h;
			} else {
				swap_size = ch_prev->swap_size.w * ch_prev->swap_size.h;
				trim_pv_size = trim_pv.size_x * trim_pv.size_y;
				/*decide binning or not*/
				if (shift && swap_size >= trim_pv_size) {
					dcam_out.w = trim_pv.size_x;
					dcam_out.h = trim_pv.size_y;
				} else if (shift == 0) {
					dcam_out.h = dcam_out.h *
						LTM_MIN_TILE_WIDTH * 8 / dcam_out.w;
					if (dcam_out.h > ch_prev->ch_uinfo.src_size.h)
						dcam_out.h = ch_prev->ch_uinfo.src_size.h;
					dcam_out.h = ALIGN_DOWN(dcam_out.h, 2);
					dcam_out.w = LTM_MIN_TILE_WIDTH * 8;
					ch_prev->trim_dcam.size_x = MAX(trim_pv.size_x, dcam_out.w);
					ch_prev->trim_dcam.size_y = MAX(trim_pv.size_y, dcam_out.h);
					ch_prev->trim_dcam.size_x =
						ALIGN(ch_prev->trim_dcam.size_x, 4);
					ch_prev->trim_dcam.size_y =
						ALIGN(ch_prev->trim_dcam.size_y, 2);
					ch_prev->trim_dcam.start_x =
						((ch_prev->ch_uinfo.src_size.w -
						ch_prev->trim_dcam.size_x) >> 1) & ~1;
					ch_prev->trim_dcam.start_y =
						((ch_prev->ch_uinfo.src_size.h -
						ch_prev->trim_dcam.size_y) >> 1) & ~1;
				} else {
					isp_scaler_crop_need = 0;
				}
			}
		}

		if (ch_prev->compress_en) {
			dcam_out.h = ALIGN_UP(dcam_out.h, DCAM_FBC_TILE_HEIGHT);
			ch_prev->trim_dcam.size_y = MAX(ch_prev->trim_dcam.size_y, dcam_out.h);
			ch_prev->trim_dcam.start_y =
				((ch_prev->ch_uinfo.src_size.h -
				  ch_prev->trim_dcam.size_y) >> 1) & ~1;
			ratio_min_h = (1 << RATIO_SHIFT) *
				ch_prev->trim_dcam.size_y / dcam_out.h;
		}

		pr_info("shift %d, isp_scaler_crop %d, dst_p %u %u, dst_v %u %u, dcam_out %u %u swap w:%d h:%d\n",
			shift, isp_scaler_crop_need, dst_p.w, dst_p.h,
			dst_v.w, dst_v.h, dcam_out.w, dcam_out.h,
			ch_prev->swap_size.w, ch_prev->swap_size.h);

		total_crop_dst = *total_crop_p;
		total_trim_pv.size_x = total_crop_dst.w;
		total_trim_pv.size_y = total_crop_dst.h;
		ch_prev->total_trim_dcam = total_trim_pv;
		ch_prev->dst_dcam = dcam_out;

		isp_trim = &ch_prev->trim_isp;
		if (module->zoom_solution == ZOOM_SCALER) {
			isp_trim->size_x =
				camzoom_ratio16_divide(ch_prev->ch_uinfo.src_crop.w, ratio_min_w);
			isp_trim->size_y =
				camzoom_ratio16_divide(ch_prev->ch_uinfo.src_crop.h, ratio_min_h);
			isp_trim->size_x = ALIGN(isp_trim->size_x, 4);
			isp_trim->size_y = ALIGN(isp_trim->size_y, 2);
		} else {
			isp_trim->size_x = ((ch_prev->ch_uinfo.src_crop.w >> shift) + 1) & ~1;
			isp_trim->size_y = ((ch_prev->ch_uinfo.src_crop.h >> shift) + 1) & ~1;
		}
		isp_trim->size_x = min(isp_trim->size_x, dcam_out.w);
		isp_trim->size_y = min(isp_trim->size_y, dcam_out.h);
		isp_trim->start_x = ((dcam_out.w - isp_trim->size_x) >> 1) & ~1;
		isp_trim->start_y = ((dcam_out.h - isp_trim->size_y) >> 1) & ~1;

		if (isp_scaler_crop_need) {
			if (module->zoom_solution == ZOOM_SCALER &&
				ch_prev->ch_uinfo.src_crop.w < dcam_out.w) {
				isp_trim->size_x = ch_prev->ch_uinfo.src_crop.w;
				isp_trim->size_y = ch_prev->ch_uinfo.src_crop.h;
				isp_trim->size_x = ALIGN(isp_trim->size_x, 4);
				isp_trim->size_y = ALIGN(isp_trim->size_y, 2);
				isp_trim->start_x = ((dcam_out.w - isp_trim->size_x) >> 1) & ~1;
				isp_trim->start_y = ((dcam_out.h - isp_trim->size_y) >> 1) & ~1;
			} else if (module->zoom_solution == ZOOM_BINNING2 ||
				module->zoom_solution == ZOOM_BINNING4) {
				if (shift) {
					isp_trim->size_x = dcam_out.w;
					isp_trim->size_y = dcam_out.h;
					isp_trim->size_x = ALIGN(isp_trim->size_x, align_size);
					isp_trim->size_y = ALIGN(isp_trim->size_y, align_size/2);
					isp_trim->start_x =
						((dcam_out.w - isp_trim->size_x) >> 1) & ~1;
					isp_trim->start_y =
						((dcam_out.h - isp_trim->size_y) >> 1) & ~1;
				} else {
					isp_trim->size_x = ch_prev->ch_uinfo.src_crop.w;
					isp_trim->size_y = ch_prev->ch_uinfo.src_crop.h;
					isp_trim->size_x = ALIGN(isp_trim->size_x, 4);
					isp_trim->size_y = ALIGN(isp_trim->size_y, 2);
					isp_trim->start_x =
						((dcam_out.w - isp_trim->size_x) >> 1) & ~1;
					isp_trim->start_y =
						((dcam_out.h - isp_trim->size_y) >> 1) & ~1;
				}
			}
		}
		pr_info("trim isp, prev %u %u %u %u\n",
			isp_trim->start_x, isp_trim->start_y, isp_trim->size_x, isp_trim->size_y);
	}

	if (ch_vid->enable) {
		ch_vid->dst_dcam = dcam_out;
		ch_vid->trim_dcam = trim_pv;
		isp_trim = &ch_vid->trim_isp;
		if (module->zoom_solution == ZOOM_SCALER) {
			isp_trim->size_x =
				camzoom_ratio16_divide(ch_vid->ch_uinfo.src_crop.w, ratio_min_w);
			isp_trim->size_y =
				camzoom_ratio16_divide(ch_vid->ch_uinfo.src_crop.h, ratio_min_h);
			isp_trim->size_x = ALIGN(isp_trim->size_x, 4);
			isp_trim->size_y = ALIGN(isp_trim->size_y, 2);
		} else {
			isp_trim->size_x = ((ch_vid->ch_uinfo.src_crop.w >> shift) + 1) & ~1;
			isp_trim->size_y = ((ch_vid->ch_uinfo.src_crop.h >> shift) + 1) & ~1;
			if (isp_scaler_crop_need && swap_size >= trim_pv_size && shift) {
				isp_trim->size_x = (ch_vid->ch_uinfo.src_crop.w  + 1) & ~1;
				isp_trim->size_y = (ch_vid->ch_uinfo.src_crop.h + 1) & ~1;
			}
		}
		isp_trim->size_x = min(isp_trim->size_x, dcam_out.w);
		isp_trim->size_y = min(isp_trim->size_y, dcam_out.h);
		isp_trim->start_x = ((dcam_out.w - isp_trim->size_x) >> 1) & ~1;
		isp_trim->start_y = ((dcam_out.h - isp_trim->size_y) >> 1) & ~1;
		pr_info("trim isp, vid %u %u %u %u\n",
			isp_trim->start_x, isp_trim->start_y,
			isp_trim->size_x, isp_trim->size_y);
	}

	if (ch_cap->enable) {
		ch_cap->xtm_zoom.isp_scaler_crop_need = isp_scaler_crop_need;
		ch_cap->xtm_zoom.dcam_out = dcam_out;
		ch_cap->xtm_zoom.trim_c = trim_c;
		if (isp_scaler_crop_need &&
			(trim_c.size_x < dcam_out.w || trim_c.size_y < dcam_out.h)) {
			trim_c.size_x = dcam_out.w;
			trim_c.size_y = dcam_out.h;
			trim_c.start_x = ((ch_cap->ch_uinfo.src_size.w - trim_c.size_x) >> 1) & ~1;
			trim_c.start_y = ((ch_cap->ch_uinfo.src_size.h - trim_c.size_y) >> 1) & ~1;
			pr_info("trim_c new: %u %u %u %u\n",
				trim_c.start_x, trim_c.start_y, trim_c.size_x, trim_c.size_y);
		}
		ch_cap->trim_dcam = trim_c;
		ch_cap->dst_dcam.w = ch_cap->trim_dcam.size_x;
		ch_cap->dst_dcam.h = ch_cap->trim_dcam.size_y;
		cam_zoom_diff_trim_get(&ch_cap->ch_uinfo.src_crop,
			(1 << RATIO_SHIFT), &trim_c, &ch_cap->trim_isp);
		ch_cap->trim_isp.start_x = ALIGN_DOWN(ch_cap->trim_isp.start_x, 2);
		ch_cap->trim_isp.start_y = ALIGN_DOWN(ch_cap->trim_isp.start_y, 2);
		ch_cap->trim_isp.size_x = ALIGN_DOWN(ch_cap->trim_isp.size_x, 2);
		ch_cap->trim_isp.size_y = ALIGN_DOWN(ch_cap->trim_isp.size_y, 2);
		ch_cap->trim_isp.size_x = min(ch_cap->trim_isp.size_x, trim_c.size_x);
		ch_cap->trim_isp.size_y = min(ch_cap->trim_isp.size_y, trim_c.size_y);
		pr_info("trim isp, cap %d %d %d %d\n",
			ch_cap->trim_isp.start_x, ch_cap->trim_isp.start_y,
			ch_cap->trim_isp.size_x, ch_cap->trim_isp.size_y);
		if (ch_cap_thm->enable) {
			ch_cap_thm->trim_dcam = ch_cap->trim_dcam;
			ch_cap_thm->trim_isp = ch_cap->trim_isp;
		}
	}

	pr_info("done\n");
	return 0;
}

static void camzoom_frame_param_cfg(struct cam_zoom_desc *param, struct cam_zoom_frame *zoom_param)
{
	int ret = 0, i = 0, j = 0, m = 0, n = 0;
	struct cam_pipeline_topology *graph = NULL;

	graph = param->pipeline_graph;
	for (i = 0; i < graph->node_cnt; i++) {
		n = 0;
		if (graph->nodes[i].state != CAM_NODE_STATE_WORK ||
			graph->nodes[i].type == CAM_NODE_TYPE_DATA_COPY ||
			graph->nodes[i].type == CAM_NODE_TYPE_FRAME_CACHE)
			continue;
		zoom_param->zoom_node[m].node_type = graph->nodes[i].type;
		zoom_param->zoom_node[m].node_id = graph->nodes[i].id;
		for (j = 0; j < PORT_ZOOM_CNT_MAX; j++) {
			if (graph->nodes[i].inport[j].link_state != PORT_LINK_NORMAL)
				continue;
			zoom_param->zoom_node[m].zoom_port[n].port_type = graph->nodes[i].inport[j].transfer_type;
			zoom_param->zoom_node[m].zoom_port[n].port_id = graph->nodes[i].inport[j].id;
			ret = camzoom_port_info_cfg(&zoom_param->zoom_node[m].zoom_port[n],
				graph->nodes[i].type, graph->nodes[i].id, param);
			if (!ret)
				continue;
			n++;
			break;
		}
		for (j = 0; j < PORT_ZOOM_CNT_MAX - 1; j++) {
			if (graph->nodes[i].outport[j].link_state != PORT_LINK_NORMAL)
				continue;
			zoom_param->zoom_node[m].zoom_port[n].port_type = graph->nodes[i].outport[j].transfer_type;
			zoom_param->zoom_node[m].zoom_port[n].port_id = graph->nodes[i].outport[j].id;
			ret = camzoom_port_info_cfg(&zoom_param->zoom_node[m].zoom_port[n],
				graph->nodes[i].type, graph->nodes[i].id, param);
			if (!ret)
				continue;
			n++;
			if (n >= PORT_ZOOM_CNT_MAX)
				break;
		}
		m++;
		if (m >= NODE_ZOOM_CNT_MAX)
			break;
	}
}

uint32_t cam_zoom_port_deci_factor_get(uint32_t src_size, uint32_t dst_size, uint32_t deci_fac_max)
{
	uint32_t factor = 0;

	if (!src_size || !dst_size)
		return factor;

	/* factor: 0 - 1/2, 1 - 1/4, 2 - 1/8, 3 - 1/16 */
	for (factor = 0; factor < deci_fac_max; factor++) {
		if (src_size < (uint32_t) (dst_size * (1 << (factor + 1))))
			break;
	}

	return factor;
}

void cam_zoom_diff_trim_get(struct sprd_img_rect *orig,
	uint32_t ratio16, struct img_trim *trim0, struct img_trim *trim1)
{
	trim1->start_x = camzoom_scale_fix(orig->x - trim0->start_x, ratio16);
	trim1->start_y = camzoom_scale_fix(orig->y - trim0->start_y, ratio16);
	trim1->size_x = camzoom_scale_fix(orig->w, ratio16);
	trim1->size_x = ALIGN(trim1->size_x, 2);
	trim1->size_y = camzoom_scale_fix(orig->h, ratio16);
}

int cam_zoom_crop_size_align(struct camera_module *module,
		struct sprd_img_rect *crop, uint32_t channel_id)
{
	struct img_size max_size = {0};
	uint32_t ratio = 0, w_align = DCAM_PATH_CROP_W_ALIGN, h_align = DCAM_PATH_CROP_H_ALIGN;
	struct channel_context *channel = NULL;

	channel = &module->channel[channel_id];
	if (channel)
		ratio = channel->ch_uinfo.nonzsl_pre_ratio;

	max_size.w = module->cam_uinfo.sn_rect.w;
	max_size.h = module->cam_uinfo.sn_rect.h;

	/* 4in1 prev, enable 4in1 binning OR size > 24M, size/2 */
	if ((module->cam_uinfo.is_4in1 || module->cam_uinfo.dcam_slice_mode) &&
		((channel_id == CAM_CH_PRE) || (channel_id == CAM_CH_VID))) {
		crop->x >>= 1;
		crop->y >>= 1;
		crop->w >>= 1;
		crop->h >>= 1;
		max_size.w >>= 1;
		max_size.h >>= 1;
	}

	if (ratio) {
		w_align = w_align * ratio;
		h_align = h_align * ratio;
		w_align = MAX(w_align, DCAM_SLICE_CROP_W_ALIGN);
	}
	crop->w = ALIGN_UP(crop->w, w_align);
	crop->h = ALIGN_UP(crop->h, h_align);

	if (!module->cam_uinfo.zoom_crop_mode) {
		if (max_size.w > crop->w)
			crop->x = (max_size.w - crop->w) / 2;
		if (max_size.h > crop->h)
			crop->y = (max_size.h - crop->h) / 2;
	}
	crop->x &= ~1;
	crop->y &= ~1;

	if ((crop->x + crop->w) > max_size.w)
		crop->w -= DCAM_PATH_CROP_W_ALIGN;
	if ((crop->y + crop->h) > max_size.h)
		crop->h -= DCAM_PATH_CROP_H_ALIGN;

	pr_info("aligned crop %d %d %d %d.  max %d %d\n",
		crop->x, crop->y, crop->w, crop->h, max_size.w, max_size.h);

	return 0;
}

int cam_zoom_channels_size_init(struct camera_module *module)
{
	uint32_t format = module->cam_uinfo.sensor_if.img_fmt;

	module->zoom_solution = module->grp->hw_info->ip_dcam[0]->dcamhw_abt->dcam_zoom_mode;
	if (g_camctrl.dcam_zoom_mode >= ZOOM_DEBUG_DEFAULT)
		module->zoom_solution = g_camctrl.dcam_zoom_mode - ZOOM_DEBUG_DEFAULT;
	module->rds_limit = g_camctrl.dcam_rds_limit;

	if (module->cam_uinfo.image_sharpness_mode == CAM_SHARPNESS_RDS)
		module->zoom_solution = ZOOM_RDS;

	if (module->cam_uinfo.image_sharpness_mode == CAM_SHARPNESS_FULLSIZE
		&& (module->grp->hw_info->ip_dcam[0]->dcamhw_abt->preview_full_size || g_camctrl.preview_full_size)
		&& module->channel[CAM_CH_PRE].ch_uinfo.src_size.w <= ZOOM_FULL_SIZE_WIDTH_LIMIT)
		module->zoom_solution = ZOOM_DEFAULT;

	/* force binning as smaller as possible for security */
	if (module->grp->camsec_cfg.camsec_mode != SEC_UNABLE)
		module->zoom_solution = ZOOM_BINNING4;

	if (format == DCAM_CAP_MODE_YUV)
		module->zoom_solution = ZOOM_DEFAULT;

	camzoom_channel_swapsize_calc(module);

	pr_info("zoom_solution %d, binning_limit %d rds_limit %d src w %d h %d\n",
		module->zoom_solution, module->binning_limit, module->rds_limit,
		module->channel[CAM_CH_PRE].ch_uinfo.src_size.w,
		module->channel[CAM_CH_PRE].ch_uinfo.src_size.h);

	return 0;
}

void camzoom_channel_crop_mode(struct camera_module *module)
{
	struct channel_context *ch_cap = NULL;
	struct channel_context *ch_cap_thm = NULL;
	struct img_trim trim_c = {0};
	struct img_size dcam_out = {0};

	ch_cap = &module->channel[CAM_CH_CAP];
	ch_cap_thm = &module->channel[CAM_CH_CAP_THM];
	if (!ch_cap->enable)
		return;

	trim_c.start_x = ch_cap->ch_uinfo.src_crop.x;
	trim_c.start_y = ch_cap->ch_uinfo.src_crop.y;
	trim_c.size_x = ch_cap->ch_uinfo.src_crop.w;
	trim_c.size_y = ch_cap->ch_uinfo.src_crop.h;
	dcam_out.w = ch_cap->ch_uinfo.src_size.w;
	dcam_out.h = ch_cap->ch_uinfo.src_size.h;
	ch_cap->latest_user_crop = ch_cap->ch_uinfo.src_crop;

	ch_cap->trim_dcam.start_x = 0;
	ch_cap->trim_dcam.start_y = 0;
	ch_cap->trim_dcam.size_x = dcam_out.w;
	ch_cap->trim_dcam.size_y = dcam_out.h;
	ch_cap->dst_dcam.w = ch_cap->trim_dcam.size_x;
	ch_cap->dst_dcam.h = ch_cap->trim_dcam.size_y;

	ch_cap->trim_isp.start_x = ALIGN_DOWN(trim_c.start_x, 2);
	ch_cap->trim_isp.start_y = ALIGN_DOWN(trim_c.start_y, 2);
	ch_cap->trim_isp.size_x = ALIGN_DOWN(trim_c.size_x, 2);
	ch_cap->trim_isp.size_y = ALIGN_DOWN(trim_c.size_y, 2);
	ch_cap->trim_isp.size_x = min(ch_cap->trim_isp.size_x, dcam_out.w);
	ch_cap->trim_isp.size_y = min(ch_cap->trim_isp.size_y, dcam_out.h);
	pr_info("trim isp, cap %d %d %d %d\n",
		ch_cap->trim_isp.start_x, ch_cap->trim_isp.start_y,
		ch_cap->trim_isp.size_x, ch_cap->trim_isp.size_y);
	if (ch_cap_thm->enable) {
		ch_cap_thm->trim_dcam = ch_cap->trim_dcam;
		ch_cap_thm->trim_isp = ch_cap->trim_isp;
	}
}

int cam_zoom_channel_size_calc(struct camera_module *module)
{
	if (module->zoom_solution == ZOOM_RDS)
		camzoom_channel_size_rds_calc(module);
	else if (module->cam_uinfo.zoom_crop_mode)
		camzoom_channel_crop_mode(module);
	else
		camzoom_channel_size_normal_calc(module);

	return 0;
}

int cam_zoom_channel_size_config(
	struct camera_module *module, struct channel_context *channel)
{
	int ret = 0;
	uint32_t need_raw_port = 0, raw_port_id = 0, raw2yuv_port_id = 0;
	uint32_t node_type = CAM_NODE_TYPE_MAX, isp_scaler_crop_need = 0;
	struct channel_context *ch_cap = NULL;
	struct channel_context *ch_vid = NULL;
	struct channel_context *ch_vir = NULL;
	struct camera_uchannel *ch_uinfo = NULL;
	struct cam_hw_info *hw = NULL;
	struct cam_zoom_desc zoom_info = {0};
	struct cam_zoom_base raw_zoom_base = {0};
	struct img_trim trim_c = {0};
	struct img_size dcam_out = {0};

	if (!module || !channel) {
		pr_err("fail to get valid param %p %p\n", module, channel);
		return -EFAULT;
	}

	ch_uinfo = &channel->ch_uinfo;
	ch_cap = &module->channel[CAM_CH_CAP];
	ch_vid = &module->channel[CAM_CH_VID];
	ch_vir = &module->channel[CAM_CH_VIRTUAL];
	hw = module->grp->hw_info;

	raw_port_id = hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id;
	raw_port_id = dcamonline_pathid_convert_to_portid(raw_port_id);
	if (raw_port_id >= PORT_DCAM_OUT_MAX) {
		pr_err("fail to get port id\n");
		return -EFAULT;
	}
	if (module->cam_uinfo.alg_type == ALG_TYPE_VID_NR)
		raw2yuv_port_id = hw->ip_dcam[0]->dcamhw_abt->dcam_scaling_path;
	else
		raw2yuv_port_id = hw->ip_dcam[0]->dcamhw_abt->aux_dcam_path;
	raw2yuv_port_id = dcamoffline_pathid_convert_to_portid(raw2yuv_port_id);
	if (raw2yuv_port_id >= PORT_DCAM_OFFLINE_OUT_MAX) {
		pr_err("fail to get port id\n");
		return -EFAULT;
	}
	raw_zoom_base.src = ch_uinfo->src_size;
	raw_zoom_base.dst = ch_uinfo->src_size;
	raw_zoom_base.crop.start_x = 0;
	raw_zoom_base.crop.start_y = 0;
	raw_zoom_base.crop.size_x = ch_uinfo->src_size.w;
	raw_zoom_base.crop.size_y = ch_uinfo->src_size.h;
	if (channel->ch_id == CAM_CH_RAW
		|| (module->cam_uinfo.alg_type != ALG_TYPE_DEFAULT)
		|| module->cam_uinfo.is_4in1
		|| (channel->ch_id == CAM_CH_CAP && module->cam_uinfo.dcam_slice_mode && !module->cam_uinfo.is_4in1)
		|| (module->offline_icap_scene && channel->ch_id == CAM_CH_CAP)
		|| (channel->ch_id == CAM_CH_DCAM_VCH && channel->dcam_port_id == PORT_FULL_OUT)) {
		need_raw_port = 1;
	}

	zoom_info.pipeline_type = channel->pipeline_type;
	zoom_info.pipeline_graph = &module->static_topology->pipeline_list[channel->pipeline_type];
	zoom_info.latest_zoom_info = &channel->latest_zoom_param;
	zoom_info.zoom_lock = &channel->lastest_zoom_lock;
	zoom_info.zoom_info_q = &channel->zoom_param_q;
	zoom_info.sn_rect_size = ch_uinfo->src_size;
	zoom_info.zoom_ratio_width = ch_uinfo->zoom_ratio_base.w;
	zoom_info.total_crop_width = channel->total_trim_dcam.size_x;
	zoom_info.dcam_src_size = zoom_info.sn_rect_size;
	zoom_info.ori_user_crop.start_x = channel->ch_uinfo.src_crop.x;
	zoom_info.ori_user_crop.start_y = channel->ch_uinfo.src_crop.y;
	zoom_info.ori_user_crop.size_x = channel->ch_uinfo.src_crop.w;
	zoom_info.ori_user_crop.size_y = channel->ch_uinfo.src_crop.h;
	if (IS_VALID_DCAM_IMG_PORT(channel->dcam_port_id)) {
		node_type = CAM_NODE_TYPE_DCAM_ONLINE;
		zoom_info.dcam_crop[node_type][channel->dcam_port_id] = channel->trim_dcam;
		zoom_info.dcam_dst[node_type][channel->dcam_port_id] = channel->dst_dcam;
	}

	if (need_raw_port && (IS_VALID_DCAM_IMG_PORT(channel->dcam_port_id) || channel->dcam_port_id == PORT_VCH2_OUT) &&
		!(module->cam_uinfo.virtualsensor && channel->nonzsl_pre_pipeline)) {
		node_type = CAM_NODE_TYPE_DCAM_ONLINE;
		if (module->cam_uinfo.alg_type == ALG_TYPE_CAP_MFNR ||
			module->cam_uinfo.alg_type == ALG_TYPE_CAP_AINR) {
			zoom_info.dcam_crop[node_type][PORT_RAW_OUT] = raw_zoom_base.crop;
			zoom_info.dcam_dst[node_type][PORT_RAW_OUT] = raw_zoom_base.dst;
			node_type = CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW;
			zoom_info.dcam_crop[node_type][raw_port_id] = raw_zoom_base.crop;
			zoom_info.dcam_dst[node_type][raw_port_id] = raw_zoom_base.dst;
		} else {
			zoom_info.dcam_crop[node_type][raw_port_id] = raw_zoom_base.crop;
			zoom_info.dcam_dst[node_type][raw_port_id] = raw_zoom_base.dst;
		}

		if (module->cam_uinfo.alg_type == ALG_TYPE_CAP_XDR &&
			channel->raw_port_id == PORT_RAW_OUT) {
			node_type = CAM_NODE_TYPE_DCAM_ONLINE;
			zoom_info.dcam_crop[node_type][PORT_FULL_OUT] = channel->trim_dcam;
			zoom_info.dcam_dst[node_type][PORT_FULL_OUT] = channel->dst_dcam;
		}

		if (channel->ch_id != CAM_CH_RAW && IS_VALID_DCAM_IMG_PORT(raw2yuv_port_id)) {
			node_type = CAM_NODE_TYPE_DCAM_OFFLINE;
			if (module->cam_uinfo.alg_type == ALG_TYPE_VID_NR)
				zoom_info.dcam_src_size = channel->dst_dcam;

			zoom_info.dcam_crop[node_type][raw2yuv_port_id] = channel->trim_dcam;
			zoom_info.dcam_dst[node_type][raw2yuv_port_id] = channel->dst_dcam;
			if (module->offline_icap_scene &&
				hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support ==
				CAM_DISABLE) {
				node_type = CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW;
				zoom_info.dcam_crop[node_type][raw2yuv_port_id] = raw_zoom_base.crop;
				zoom_info.dcam_dst[node_type][raw2yuv_port_id] = raw_zoom_base.dst;
				node_type = CAM_NODE_TYPE_DCAM_OFFLINE;
				zoom_info.dcam_crop[node_type][raw2yuv_port_id] = raw_zoom_base.crop;
				zoom_info.dcam_dst[node_type][raw2yuv_port_id] = raw_zoom_base.dst;
			}
			if (channel->offline_need_dcam_raw == CAM_ENABLE) {
				zoom_info.dcam_crop[node_type][raw_port_id] = raw_zoom_base.crop;
				zoom_info.dcam_dst[node_type][raw_port_id] = raw_zoom_base.dst;
			}
		}
	}

	if (channel->ch_id == CAM_CH_CAP && module->offline_icap_scene
		&& hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == CAM_DISABLE) {
		zoom_info.isp_src_size = raw_zoom_base.src;
		zoom_info.isp_input_crop = channel->trim_dcam;
	} else {
		zoom_info.isp_src_size = channel->dst_dcam;
		zoom_info.isp_input_crop.start_x = 0;
		zoom_info.isp_input_crop.start_y = 0;
		zoom_info.isp_input_crop.size_x = channel->dst_dcam.w;
		zoom_info.isp_input_crop.size_y = channel->dst_dcam.h;
	}

	if (IS_VALID_ISP_IMG_PORT(channel->isp_port_id)) {
		zoom_info.isp_crop[channel->isp_port_id] = channel->trim_isp;
		zoom_info.isp_dst[channel->isp_port_id] = channel->ch_uinfo.dst_size;
		CAM_ZOOM_DEBUG("ch %d trim isp %d %d, dst %d %d\n", channel->ch_id, channel->trim_isp.size_x,
			channel->trim_isp.size_y, channel->ch_uinfo.dst_size.w, channel->ch_uinfo.dst_size.h);
	}
	if (ch_vid->enable) {
		zoom_info.isp_crop[PORT_VID_OUT] = ch_vid->trim_isp;
		zoom_info.isp_dst[PORT_VID_OUT] = ch_vid->ch_uinfo.dst_size;
	}
	if (ch_vir->enable && channel->ch_id == CAM_CH_PRE) {
		zoom_info.isp_crop[PORT_VID_OUT] = channel->trim_isp;
		zoom_info.isp_dst[PORT_VID_OUT].w = ch_vir->ch_uinfo.vir_channel[0].dst_size.w;
		zoom_info.isp_dst[PORT_VID_OUT].h = ch_vir->ch_uinfo.vir_channel[0].dst_size.h;
	}
	if (ch_vir->enable && channel->ch_id == CAM_CH_CAP) {
		zoom_info.isp_crop[PORT_VID_OUT] = channel->trim_isp;
		zoom_info.isp_dst[PORT_VID_OUT].w = ch_vir->ch_uinfo.vir_channel[1].dst_size.w;
		zoom_info.isp_dst[PORT_VID_OUT].h = ch_vir->ch_uinfo.vir_channel[1].dst_size.h;
	}

	if (ch_cap->enable && ch_cap->isp_port_id == PORT_VID_OUT) {
		zoom_info.isp_crop[PORT_VID_OUT] = channel->trim_isp;
		zoom_info.isp_dst[PORT_VID_OUT] = ch_cap->ch_uinfo.dst_size;
	}

	if (channel->ch_id == CAM_CH_CAP && module->cam_uinfo.zoom_crop_mode) {
		zoom_info.isp_dst[channel->isp_port_id].w = channel->trim_isp.size_x;
		zoom_info.isp_dst[channel->isp_port_id].h = channel->trim_isp.size_y;
	}
	ret = cam_zoom_param_set(&zoom_info);

	if (channel->ch_id == CAM_CH_CAP && ch_cap->enable) {
		isp_scaler_crop_need = ch_cap->xtm_zoom.isp_scaler_crop_need;
		dcam_out = ch_cap->xtm_zoom.dcam_out;
		trim_c = ch_cap->xtm_zoom.trim_c;
		if (isp_scaler_crop_need &&
			(trim_c.size_x < dcam_out.w || trim_c.size_y < dcam_out.h)) {
			ch_cap->latest_user_crop.x = ch_cap->trim_dcam.start_x;
			ch_cap->latest_user_crop.y = ch_cap->trim_dcam.start_y;
			ch_cap->latest_user_crop.w = ch_cap->trim_dcam.size_x;
			ch_cap->latest_user_crop.h = ch_cap->trim_dcam.size_y;
		}
	}
	if (channel->nonzsl_pre_pipeline) {
		uint32_t ratio = 1;
		if (module->cam_uinfo.virtualsensor)
			zoom_info.pipeline_type = CAM_PIPELINE_PREVIEW;
		else
			zoom_info.pipeline_type = CAM_PIPELINE_ONLINERAW_2_OFFLINEPREVIEW;

		zoom_info.pipeline_graph = &module->static_topology->pipeline_list[zoom_info.pipeline_type];
		zoom_info.latest_zoom_info = &channel->nonzsl_pre.latest_zoom_param;
		zoom_info.zoom_lock = &channel->nonzsl_pre.lastest_zoom_lock;
		zoom_info.zoom_info_q = &channel->nonzsl_pre.zoom_param_q;

		ratio = channel->ch_uinfo.nonzsl_pre_ratio;
		if (module->cam_uinfo.virtualsensor) {
			node_type = CAM_NODE_TYPE_DCAM_ONLINE;
			zoom_info.dcam_crop[node_type][PORT_BIN_OUT] = channel->trim_dcam;
			zoom_info.dcam_dst[node_type][PORT_BIN_OUT].w = channel->dst_dcam.w / ratio;
			zoom_info.dcam_dst[node_type][PORT_BIN_OUT].h = channel->dst_dcam.h / ratio;
			zoom_info.dcam_dst[node_type][PORT_BIN_OUT].w =
				ALIGN_DOWN(zoom_info.dcam_dst[node_type][PORT_BIN_OUT].w, 4);
			zoom_info.dcam_dst[node_type][PORT_BIN_OUT].h =
				ALIGN_DOWN(zoom_info.dcam_dst[node_type][PORT_BIN_OUT].h, 2);
		} else {
			node_type = CAM_NODE_TYPE_DCAM_OFFLINE;
			zoom_info.dcam_crop[node_type][PORT_OFFLINE_BIN_OUT] =
				channel->trim_dcam;
			zoom_info.dcam_dst[node_type][PORT_OFFLINE_BIN_OUT].w =
				channel->dst_dcam.w / ratio;
			zoom_info.dcam_dst[node_type][PORT_OFFLINE_BIN_OUT].h =
				channel->dst_dcam.h / ratio;
			zoom_info.dcam_dst[node_type][PORT_OFFLINE_BIN_OUT].w =
				ALIGN_DOWN(zoom_info.dcam_dst[node_type][PORT_OFFLINE_BIN_OUT].w, 4);
			zoom_info.dcam_dst[node_type][PORT_OFFLINE_BIN_OUT].h =
				ALIGN_DOWN(zoom_info.dcam_dst[node_type][PORT_OFFLINE_BIN_OUT].h, 2);
		}
		zoom_info.isp_src_size.w = channel->dst_dcam.w / ratio;
		zoom_info.isp_src_size.h = channel->dst_dcam.h / ratio;
		zoom_info.isp_src_size.w = ALIGN_DOWN(zoom_info.isp_src_size.w, 4);
		zoom_info.isp_src_size.h = ALIGN_DOWN(zoom_info.isp_src_size.h, 2);
		zoom_info.isp_input_crop.start_x = 0;
		zoom_info.isp_input_crop.start_y = 0;
		zoom_info.isp_input_crop.size_x = zoom_info.isp_src_size.w;
		zoom_info.isp_input_crop.size_y = zoom_info.isp_src_size.h;
		zoom_info.isp_dst[PORT_PRE_OUT] = zoom_info.isp_src_size;
		zoom_info.isp_crop[PORT_PRE_OUT].start_x = 0;
		zoom_info.isp_crop[PORT_PRE_OUT].start_y = 0;
		zoom_info.isp_crop[PORT_PRE_OUT].size_x = zoom_info.isp_src_size.w;
		zoom_info.isp_crop[PORT_PRE_OUT].size_y = zoom_info.isp_src_size.h;
		ret = cam_zoom_param_set(&zoom_info);
	}

	return ret;
}

int cam_zoom_start_proc(void *param)
{
	int update_pv = 0, update_c = 0;
	struct camera_module *module = NULL;
	struct channel_context *ch_prev = NULL, *ch_vid = NULL, *ch_cap = NULL;
	struct cam_frame *pre_zoom_coeff = NULL;
	struct cam_frame *vid_zoom_coeff = NULL;
	struct cam_frame *cap_zoom_coeff = NULL;

	module = (struct camera_module *)param;
	ch_prev = &module->channel[CAM_CH_PRE];
	ch_cap = &module->channel[CAM_CH_CAP];
	ch_vid = &module->channel[CAM_CH_VID];
next:
	update_pv = update_c = 0;
	if (ch_prev->enable && ch_vid->enable
		&& (CAM_QUEUE_CNT_GET(&ch_prev->zoom_user_crop_q) == 0 || CAM_QUEUE_CNT_GET(&ch_vid->zoom_user_crop_q) == 0))
		goto update_cap;
	/* Get node from the preview/video/cap coef queue if exist */
	if (ch_prev->enable)
		pre_zoom_coeff = CAM_QUEUE_DEQUEUE(&ch_prev->zoom_user_crop_q, struct cam_frame, list);
	if (pre_zoom_coeff) {
		ch_prev->ch_uinfo.src_crop = pre_zoom_coeff->user_zoom.zoom_crop;
		ch_prev->ch_uinfo.total_src_crop = pre_zoom_coeff->user_zoom.total_zoom_crop;
		update_pv |= 1;
		cam_queue_empty_frame_put(pre_zoom_coeff);
		pre_zoom_coeff = NULL;
	}

	if (ch_vid->enable)
		vid_zoom_coeff = CAM_QUEUE_DEQUEUE(&ch_vid->zoom_user_crop_q, struct cam_frame, list);
	if (vid_zoom_coeff) {
		ch_vid->ch_uinfo.src_crop = vid_zoom_coeff->user_zoom.zoom_crop;
		update_pv |= 1;
		cam_queue_empty_frame_put(vid_zoom_coeff);
		vid_zoom_coeff = NULL;
	}

update_cap:
	if (ch_cap->enable)
		cap_zoom_coeff = CAM_QUEUE_DEQUEUE(&ch_cap->zoom_user_crop_q, struct cam_frame, list);
	if (cap_zoom_coeff) {
		ch_cap->ch_uinfo.src_crop = cap_zoom_coeff->user_zoom.zoom_crop;
		update_c |= 1;
		cam_queue_empty_frame_put(cap_zoom_coeff);
		cap_zoom_coeff = NULL;
	}

	if (update_pv || update_c) {
		cam_zoom_channel_size_calc(module);
		if (ch_cap->enable && update_c) {
			mutex_lock(&module->zoom_lock);
			cam_zoom_channel_size_config(module, ch_cap);
			mutex_unlock(&module->zoom_lock);
		}
		if (ch_prev->enable && update_pv) {
			mutex_lock(&module->zoom_lock);
			cam_zoom_channel_size_config(module, ch_prev);
			mutex_unlock(&module->zoom_lock);
		}
		goto next;
	}

	return 0;
}

int cam_zoom_param_set(struct cam_zoom_desc *in_ptr)
{
	int ret = 0, i = 0;
	unsigned long flag = 0;
	struct cam_frame *cur_zoom = NULL;
	struct cam_zoom_frame *latest_zoom = NULL;
	struct camera_queue *zoom_q = NULL;

	if (!in_ptr) {
		pr_err("fail to get valid ptr\n");
		return -EFAULT;
	}

	latest_zoom = in_ptr->latest_zoom_info;
	zoom_q = in_ptr->zoom_info_q;
	if (!latest_zoom || !zoom_q || !in_ptr->pipeline_graph) {
		pr_err("fail to get valid %p %p\n", latest_zoom, zoom_q);
		return -EFAULT;
	}

	if (in_ptr->pipeline_type < CAM_PIPELINE_TYPE_MAX) {
		cur_zoom = cam_queue_empty_frame_get(CAM_FRAME_NODE_ZOOM);
		if (cur_zoom) {
			camzoom_frame_param_cfg(in_ptr, &cur_zoom->node_zoom);
			pr_debug("set cur_zoom %p\n", cur_zoom);
			spin_lock_irqsave(in_ptr->zoom_lock, flag);
			ret = CAM_QUEUE_ENQUEUE(zoom_q, &cur_zoom->list);
			if (ret) {
				spin_unlock_irqrestore(in_ptr->zoom_lock, flag);
				pr_err("fail to enq zoom cnt %d, state:%d\n", zoom_q->cnt, zoom_q->state);
				return ret;
			}
			for (i = 0; i < NODE_ZOOM_CNT_MAX; i++)
				latest_zoom->zoom_node[i] = cur_zoom->node_zoom.zoom_node[i];
			spin_unlock_irqrestore(in_ptr->zoom_lock, flag);
		}
	} else {
		pr_err("fail to support pipeline type %d\n", in_ptr->pipeline_type);
	}

	return ret;
}

int cam_zoom_port_param_update(struct cam_zoom_frame *zoom_data,
	struct cam_zoom_index *zoom_index, struct cam_zoom_base *zoom_param)
{
	int i = 0, j = 0;
	struct cam_zoom_port *zoom_port = NULL;
	struct cam_zoom_node *zoom_node = NULL;

	if (!zoom_data || !zoom_index || !zoom_param) {
		pr_err("fail to get valid param %px %px %px\n", zoom_data, zoom_index, zoom_param);
		return -EFAULT;
	}

	for (i = 0; i < NODE_ZOOM_CNT_MAX; i++) {
		zoom_node = &zoom_data->zoom_node[i];
		if (zoom_index->node_type == zoom_node->node_type
			&& zoom_index->node_id == zoom_node->node_id) {
			for (j = 0; j < PORT_ZOOM_CNT_MAX; j++) {
				zoom_port = &zoom_node->zoom_port[j];
				if (zoom_index->port_id == zoom_port->port_id
					&& zoom_index->port_type == zoom_port->port_type)
					zoom_port->zoom_base = *zoom_param;
			}
		}
	}

	return 0;
}

int cam_zoom_frame_base_get(struct cam_zoom_base *zoom_base, struct cam_zoom_index *zoom_index)
{
	int ret = 0, i = NODE_ZOOM_CNT_MAX, j = PORT_ZOOM_CNT_MAX;
	struct cam_zoom_frame *cur_zoom = NULL;
	struct cam_zoom_node *zoom_node = NULL;
	struct cam_zoom_port *zoom_port = NULL;

	if (!zoom_base || !zoom_index) {
		pr_err("fail to get valid param %p %p\n", zoom_base, zoom_index);
		return -EFAULT;
	}

	cur_zoom = zoom_index->zoom_data;
	for (i = 0; i < NODE_ZOOM_CNT_MAX; i++) {
		zoom_node = &cur_zoom->zoom_node[i];
		if (zoom_index->node_type == zoom_node->node_type
			&& zoom_index->node_id == zoom_node->node_id) {
			for (j = 0; j < PORT_ZOOM_CNT_MAX; j++) {
				zoom_port = &zoom_node->zoom_port[j];
				if (zoom_index->port_id == zoom_port->port_id
					&& zoom_index->port_type == zoom_port->port_type) {
					zoom_base->src = zoom_port->zoom_base.src;
					zoom_base->crop = zoom_port->zoom_base.crop;
					zoom_base->dst = zoom_port->zoom_base.dst;
					zoom_base->ratio_width = zoom_port->zoom_base.ratio_width;
					zoom_base->total_crop_width = zoom_port->zoom_base.total_crop_width;
					zoom_base->ori_user_crop = zoom_port->zoom_base.ori_user_crop;
					zoom_base->need_post_proc = zoom_port->zoom_base.need_post_proc;
					break;
				}
			}
			break;
		}
	}

	if (i == NODE_ZOOM_CNT_MAX || j == PORT_ZOOM_CNT_MAX)
		pr_warn("warning: not find correct zoom data i %d j %d\n", i, j);

	return ret;
}
