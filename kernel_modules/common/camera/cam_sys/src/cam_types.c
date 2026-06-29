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

#include "cam_types.h"
#include "dcam_core.h"
#include "dcam_hw_adpt.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_TYPES: %d %d %s : " fmt, current->pid, __LINE__, __func__

uint32_t cam_data_bits(uint32_t dcam_out_fmt)
{
	uint32_t data_bwd_bits = 0;

	switch (dcam_out_fmt) {
	case CAM_RAW_BASE:
		data_bwd_bits = 0;
		break;
	case CAM_YUV422_2FRAME:
	case CAM_YVU422_2FRAME:
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
	case CAM_YUV422_3FRAME:
	case CAM_YUV420_3FRAME:
	case CAM_YUYV_1FRAME:
	case CAM_UYVY_1FRAME:
	case CAM_YVYU_1FRAME:
	case CAM_VYUY_1FRAME:
	case CAM_RAW_8:
		data_bwd_bits = 8;
		break;
	case CAM_RAW_PACK_10:
	case CAM_FULL_RGB10:
	case CAM_YUV420_2FRAME_10:
	case CAM_YVU420_2FRAME_10:
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
	case CAM_RAW_HALFWORD_10:
		data_bwd_bits = 10;
		break;
	case CAM_FULL_RGB14:
	case CAM_RAW_14:
		data_bwd_bits = 14;
		break;
	default:
		pr_err("fail to get dcam_out_fmt : %d\n", dcam_out_fmt);
		break;
	}
	return data_bwd_bits;
}

uint32_t cam_is_pack(uint32_t dcam_out_fmt)
{
	uint32_t data_is_pack  = 0;

	switch (dcam_out_fmt) {
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
	case CAM_RAW_8:
	case CAM_FULL_RGB10:
	case CAM_FULL_RGB14:
	case CAM_YUV422_2FRAME:
	case CAM_YVU422_2FRAME:
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
	case CAM_YUV420_2FRAME_10:
	case CAM_YVU420_2FRAME_10:
	case CAM_YUV422_3FRAME:
	case CAM_YUV420_3FRAME:
	case CAM_YUYV_1FRAME:
	case CAM_UYVY_1FRAME:
	case CAM_YVYU_1FRAME:
	case CAM_VYUY_1FRAME:
		data_is_pack = 0;
		break;
	case CAM_RAW_PACK_10:
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
		data_is_pack = 1;
		break;
	default:
		pr_err("fail to get dcam_out_fmt : %d\n", dcam_out_fmt);
		break;
	}
	return data_is_pack;
}

uint32_t cam_format_get(uint32_t img_pix_fmt)
{
	uint32_t format = 0;

	switch (img_pix_fmt) {
	case IMG_PIX_FMT_GREY:
		format = CAM_RAW_BASE;
		break;
	case IMG_PIX_FMT_YUYV:
		format = CAM_YUYV_1FRAME;
		break;
	case IMG_PIX_FMT_YVYU:
		format = CAM_YVYU_1FRAME;
		break;
	case IMG_PIX_FMT_UYVY:
		format = CAM_UYVY_1FRAME;
		break;
	case IMG_PIX_FMT_VYUY:
		format = CAM_VYUY_1FRAME;
		break;
	case IMG_PIX_FMT_NV12:
		format = CAM_YUV420_2FRAME;
		break;
	case IMG_PIX_FMT_NV21:
		format = CAM_YVU420_2FRAME;
		break;
	case IMG_PIX_FMT_YUV422P:
		format = CAM_YUV422_3FRAME;
		break;
	case IMG_PIX_FMT_YUV420:
		format = CAM_YUV420_3FRAME;
		break;
	case IMG_PIX_FMT_FULL_RGB:
		format = CAM_FULL_RGB14;
		break;
	default:
		format = CAM_FORMAT_MAX;
		pr_err("fail to get support format 0x%x\n", img_pix_fmt);
		break;
	}
	return format;
}

int cam_raw_fmt_get(uint32_t fmt)
{
	if (CAM_RAW_PACK_10 <= fmt && fmt <= CAM_RAW_8)
		return 1;
	else
		return 0;
}

int cam_valid_fmt_get(int32_t *fmt, uint32_t default_value)
{
	if ((*fmt < CAM_RAW_PACK_10) || (*fmt >= CAM_FORMAT_MAX)) {
		*fmt = default_value;
		return 1;
	}

	return 0;
}

uint32_t cam_cal_hw_pitch(uint32_t w, uint32_t format)
{
	uint32_t pitch = 0;

	switch (format) {
	case CAM_RAW_BASE:
		pitch = 0;
		break;
	case CAM_RAW_8:
		pitch = w;
		break;
	case CAM_RAW_PACK_10:
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
		pitch = CAL_HW_PACK_PITCH(w);
		break;
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		pitch = CAL_HW_UNPACK_PITCH(w);
		break;
	case CAM_FULL_RGB14:
		pitch = CAL_FULLRGB14_PITCH(w) * 8 / 128;
		break;
	default:
		pr_err("fail to format : %d\n", format);
		break;
	}
	return pitch;
}

uint32_t cam_byte_get(uint32_t format)
{
	uint32_t data_byte = 0;

	switch (format) {
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		data_byte = 8;
		break;
	case CAM_RAW_8:
		data_byte = 4;
		break;
	case CAM_RAW_PACK_10:
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
		data_byte = 5;
		break;
	default:
		pr_err("fail to get format : %d\n", format);
		break;
	}
	return data_byte;
}

int dcampath_bin_scaler_get(struct img_size crop, struct img_size dst,
		uint32_t *scaler_sel, uint32_t *bin_ratio)
{
	int ret = 0;

	if ((crop.w == dst.w) &&
		(crop.h == dst.h)) {
		*scaler_sel = PORT_SCALER_BYPASS;
		pr_debug("bypass used. src %d %d, dst %d %d\n", crop.w, crop.h, dst.w, dst.h);
	} else if ((dst.w * 2 == crop.w) &&
		(dst.h * 2 == crop.h)) {
		pr_debug("1/2 binning used. src %d %d, dst %d %d\n", crop.w, crop.h, dst.w, dst.h);
		*scaler_sel = PORT_SCALER_BINNING;
		*bin_ratio = 0;
	} else if ((dst.w * 4 == crop.w) &&
		(dst.h * 4 == crop.h)) {
		pr_debug("1/4 binning used. src %d %d, dst %d %d\n", crop.w, crop.h, dst.w, dst.h);
		*scaler_sel = PORT_SCALER_BINNING;
		*bin_ratio = 1;
	} else {
		*scaler_sel = 0;
		*bin_ratio = 0;
		pr_warn("warning: not support src %d %d, dst %d %d\n", crop.w, crop.h, dst.w, dst.h);
	}

	return ret;
}

uint32_t dcamonline_portid_convert_to_pathid(uint32_t port_id)
{
	uint32_t path_id = DCAM_PATH_MAX;

	switch (port_id) {
	case PORT_FULL_OUT:
		path_id = DCAM_PATH_FULL;
		break;
	case PORT_BIN_OUT:
		path_id = DCAM_PATH_BIN;
		break;
	case PORT_RAW_OUT:
		path_id = DCAM_PATH_RAW;
		break;
	case PORT_PDAF_OUT:
		path_id = DCAM_PATH_PDAF;
		break;
	case PORT_VCH2_OUT:
		path_id = DCAM_PATH_VCH2;
		break;
	case PORT_VCH3_OUT:
		path_id = DCAM_PATH_VCH3;
		break;
	case PORT_AEM_OUT:
		path_id = DCAM_PATH_AEM;
		break;
	case PORT_AFM_OUT:
		path_id = DCAM_PATH_AFM;
		break;
	case PORT_AFL_OUT:
		path_id = DCAM_PATH_AFL;
		break;
	case PORT_BAYER_HIST_OUT:
		path_id = DCAM_PATH_HIST;
		break;
	case PORT_FRGB_HIST_OUT:
		path_id = DCAM_PATH_FRGB_HIST;
		break;
	case PORT_LSCM_OUT:
		path_id = DCAM_PATH_LSCM;
		break;
	case PORT_GTM_HIST_OUT:
		path_id = DCAM_PATH_GTM_HIST;
		break;
	default :
		pr_err("fail to support port id %d\n", port_id);
		break;
	}

	return path_id;
}

/* now just add for dcam online pathid portid convert */
uint32_t dcamonline_pathid_convert_to_portid(uint32_t path_id)
{
	uint32_t port_id = PORT_DCAM_OUT_MAX;

	switch (path_id) {
	case DCAM_PATH_FULL:
		port_id = PORT_FULL_OUT;
		break;
	case DCAM_PATH_BIN:
		port_id = PORT_BIN_OUT;
		break;
	case DCAM_PATH_RAW:
		port_id = PORT_RAW_OUT;
		break;
	case DCAM_PATH_VCH2:
		port_id = PORT_VCH2_OUT;
		break;
	default :
		pr_err("fail to support path id %d\n", path_id);
		break;
	}

	return port_id;
}

/* now just add for dcam offline pathid portid convert */
uint32_t dcamoffline_pathid_convert_to_portid(uint32_t path_id)
{
	uint32_t port_id = PORT_DCAM_OFFLINE_OUT_MAX;

	switch (path_id) {
	case DCAM_PATH_FULL:
		port_id = PORT_OFFLINE_FULL_OUT;
		break;
	case DCAM_PATH_BIN:
		port_id = PORT_OFFLINE_BIN_OUT;
		break;
	case DCAM_PATH_RAW:
		port_id = PORT_OFFLINE_RAW_OUT;
		break;
	default :
		pr_err("fail to support path id %d\n", path_id);
		break;
	}

	return port_id;
}

uint32_t dcamoffline_portid_convert_to_pathid(uint32_t port_id)
{
	uint32_t path_id = DCAM_PATH_MAX;

	switch (port_id) {
	case PORT_OFFLINE_FULL_OUT:
		path_id = DCAM_PATH_FULL;
		break;
	case PORT_OFFLINE_BIN_OUT:
		path_id = DCAM_PATH_BIN;
		break;
	case PORT_OFFLINE_RAW_OUT:
		path_id = DCAM_PATH_RAW;
		break;
	case PORT_OFFLINE_PDAF_OUT:
		path_id = DCAM_PATH_PDAF;
		break;
	case PORT_OFFLINE_AEM_OUT:
		path_id = DCAM_PATH_AEM;
		break;
	case PORT_OFFLINE_AFM_OUT:
		path_id = DCAM_PATH_AFM;
		break;
	case PORT_OFFLINE_AFL_OUT:
		path_id = DCAM_PATH_AFL;
		break;
	case PORT_OFFLINE_BAYER_HIST_OUT:
		path_id = DCAM_PATH_HIST;
		break;
	case PORT_OFFLINE_FRGB_HIST_OUT:
		path_id = DCAM_PATH_FRGB_HIST;
		break;
	case PORT_OFFLINE_LSCM_OUT:
		path_id = DCAM_PATH_LSCM;
		break;
	case PORT_OFFLINE_GTM_HIST_OUT:
		path_id = DCAM_PATH_GTM_HIST;
		break;
	default :
		pr_err("fail to support port id %d\n", port_id);
		break;
	}

	return path_id;
}

