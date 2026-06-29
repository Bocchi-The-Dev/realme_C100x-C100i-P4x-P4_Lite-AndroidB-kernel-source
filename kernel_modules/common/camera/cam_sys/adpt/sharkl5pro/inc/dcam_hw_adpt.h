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

#ifndef _DCAM_HW_ADPT_H_
#define _DCAM_HW_ADPT_H_

#include "cam_types.h"

#define DCAM_64M_WIDTH                 9216
#define DCAM_32M_WIDTH                 6528
#define DCAM_24M_WIDTH                 5664
#define DCAM_16M_WIDTH                 4672
#define DCAM_13M_WIDTH                 4160
#define DCAM_8M_WIDTH                  3264
#define DCAM_OVERLAP                   64

#define DCAM_PATH_WMAX                 8048
#define DCAM_PATH_HMAX                 6036
#define RAW_OVERLAP_UP                 58
#define RAW_OVERLAP_DOWN               78
#define RAW_OVERLAP_LEFT               118
#define RAW_OVERLAP_RIGHT              138
#define DCAM_SW_SLICE_HEIGHT_MAX       8192
#define DCAM_HW_SLICE_WIDTH_MAX        8192
#define DCAM_HW_WIDTH_MAX              8192
#define DCAM_SCALE_DOWN_MAX            4
#define DCAM_SCALER_MAX_WIDTH          0xFFFFFFFF
#define DCAM_SCALER_DECI_MAX_WIDTH     0xFFFFFFFF
#define DCAM_RDS_UP_MAX                1
#define DCAM_RDS_DOWN_MAX              4
#define DCAM_RDS_MAX_IN_WIDTH          5664
#define DCAM_RDS_MAX_OUT_WIDTH         2160

#define DCAM_PATH_CROP_W_ALIGN         8
#define DCAM_PATH_CROP_H_ALIGN         8
#define DCAM_FBC_TILE_WIDTH            64
#define DCAM_FBC_TILE_HEIGHT           4
#define FBC_TILE_ADDR_ALIGN            256
#define FBC_HEADER_REDUNDANT           64
#define DCAM_FRAME_TIMESTAMP_COUNT     0x100
#define GTM_HIST_ITEM_NUM              128
#define GTM_HIST_VALUE_SIZE            129
#define GTM_HIST_XPTS_CNT              256
#define CAL_HW_PACK_PITCH(w)           (((w) * 10 + 127) / 128)
#define CAL_HW_UNPACK_PITCH(w)         (((w) * 16 + 127) / 128)
#define CAL_PACK_PITCH(w)              (CAL_HW_PACK_PITCH(w) * 128 / 8)
#define CAL_UNPACK_PITCH(w)            (CAL_HW_UNPACK_PITCH(w) * 128 / 8)
#define CAL_FULLRGB14_PITCH(w)         (0)

 /* dcamoffline limit hw: 1500us(2.0ms)/M, node:3ms*/
#define DCAMOFFLINE_HW_TIME_RATIO      2000
#define DCAMOFFLINE_NODE_TIME          3000
enum dcam_hwformat_val {
	CAM_RAW10PACK_RAW = 0,
	CAM_HALFWORD_RAW10 = 1,
	CAM_HALFWORD_RAW14 = 2,
	CAM_RAW8 = 3,
	CAM_FRGB14BIT,
	CAM_TWOPLANE_YUV422,
	CAM_TWOPLANE_YVU422,
	CAM_TWOPLANE_YUV420,
	CAM_TWOPLANE_YVU420,
};


/*
 *DCAM_CONTROL register bit map id
 * for force_cpy/auto_cpy control
 */
enum dcam_ctrl_id {
	DCAM_CTRL_CAP = (1 << 0),
	DCAM_CTRL_COEF = (1 << 1),
	DCAM_CTRL_RDS = (1 << 2),
	DCAM_CTRL_FULL = (1 << 3),
	DCAM_CTRL_BIN = (1 << 4),
	DCAM_CTRL_PDAF = (1 << 5),
	DCAM_CTRL_VCH2 = (1 << 6),
	DCAM_CTRL_VCH3 = (1 << 7),
	DCAM_CTRL_ALL = 0xff,
};

enum dcam_hw_context_id {
	DCAM_HW_CONTEXT_0 = 0,
	DCAM_HW_CONTEXT_1,
	DCAM_HW_CONTEXT_2,
	DCAM_HW_CONTEXT_MAX,
};

#define IS_DCAM_LITE(idx) 0
#define DCAM_HW_CONTEXT_BIND_MAX DCAM_HW_CONTEXT_MAX

enum dcam_id {
	DCAM_ID_0 = 0,
	DCAM_ID_1,
	DCAM_ID_2,
	DCAM_ID_MAX,
};

enum csi_id {
	CSI_ID_0 = 0,
	CSI_ID_1,
	CSI_ID_2,
	CSI_ID_MAX,
};

static inline uint32_t dcamonline_dec_align_width(uint32_t w, uint32_t layer_num)
{
	return 0;
}

static inline uint32_t dcamonline_dec_align_heigh(uint32_t h, uint32_t layer_num)
{
	return 0;
}

static inline uint32_t dcam_if_cal_compressed_size(struct dcam_compress_cal_para *para)
{
	pr_debug("sharkl5pro not support fbc\n");
	return 0;
}

static inline void dcam_if_cal_compressed_addr(struct dcam_compress_cal_para *para)
{
	pr_debug("sharkl5pro not support fbc\n");
}
#endif
