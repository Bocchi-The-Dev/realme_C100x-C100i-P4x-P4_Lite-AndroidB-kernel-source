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

#define DCAM_64M_WIDTH                 9280
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
#define DCAM_HW_WIDTH_MAX              10368
#define DCAM_OFFSET_RANGE              0x3E103E0

#define DCAM_PATH_CROP_W_ALIGN         4
#define DCAM_PATH_CROP_H_ALIGN         2
#define DCAM_SCALE_DOWN_MAX            10
#define DCAM_SCALER_MAX_WIDTH          3840
#define DCAM_SCALER_DECI_MAX_WIDTH     3872
#define DCAM_FRAME_TIMESTAMP_COUNT     0x100
#define DCAM_RDS_MAX_OUT_WIDTH         2160
#define GTM_HIST_ITEM_NUM              128
#define GTM_HIST_VALUE_SIZE            174
#define GTM_HIST_XPTS_CNT              256

/*Total lbuf DCAM0+DCAM1: 5184+5184 */
#define DCAM_TOTAL_LBUF                10368

 /* dcamoffline limit hw: 1700us(1.7ms)/M, node:30ms*/
#define DCAMOFFLINE_HW_TIME_RATIO      1700
#define DCAMOFFLINE_NODE_TIME          30000

/*
 * dcam_if fbc capability limit
 * modification to these values may cause some function in isp_slice.c not
 * work, check all symbol references for details
 */

#define DCAM_FBC_TILE_WIDTH            32
#define DCAM_FBC_TILE_HEIGHT           8
#define FBC_PAYLOAD_YUV10_BYTE_UNIT    512
#define FBC_PAYLOAD_YUV8_BYTE_UNIT     384
#define FBC_PAYLOAD_RAW10_BYTE_UNIT    640
#define FBC_HEADER_BYTE_UNIT           16
#define FBC_TILE_HEAD_SIZE_ALIGN       1024
#define FBC_STORE_ADDR_ALIGN           16
#define FBC_LOWBIT_PITCH_ALIGN         16

#define FBC_HEADER_REDUNDANT           0
#define FBC_TILE_ADDR_ALIGN            1
#define CAL_HW_PACK_PITCH(w)           (((w) * 10 + 127) / 128)
#define CAL_HW_UNPACK_PITCH(w)         (((w) * 16 + 127) / 128)
#define CAL_PACK_PITCH(w)              (CAL_HW_PACK_PITCH(w) * 128 / 8)
#define CAL_UNPACK_PITCH(w)            (CAL_HW_UNPACK_PITCH(w) * 128 / 8)
#if defined (PROJ_QOGIRN6L)
#define CAL_FULLRGB14_PITCH(w)         ((w) * 6)
#else
#define CAL_FULLRGB14_PITCH(w)         ((w) * 8)
#endif

enum dcam_hwformat_val {
	CAM_FRGB14BIT = 0,
	CAM_TWOPLANE_YUV422 = 1,
	CAM_TWOPLANE_YVU422 = 2,
	CAM_TWOPLANE_YUV420 = 4,
	CAM_TWOPLANE_YVU420 = 5,
	CAM_RAW10PACK_RAW,
	CAM_HALFWORD_RAW10,
	CAM_HALFWORD_RAW14,
	CAM_RAW8,
};

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
	DCAMLITE_HW_CONTEXT_0,
	DCAMLITE_HW_CONTEXT_1,
	DCAM_HW_CONTEXT_MAX
};

#define IS_DCAM_LITE(idx) ((idx) > DCAM_HW_CONTEXT_1)
/*ctx bind unsupport dcamlite*/
#define DCAM_HW_CONTEXT_BIND_MAX DCAM_HW_CONTEXT_1 + 1

enum dcam_id {
	DCAM_ID_0 = 0,
	DCAM_ID_1,
	DCAM_ID_2,
	DCAM_ID_3,
	DCAM_ID_MAX,
};

enum csi_id {
	CSI_ID_0 = 0,
	CSI_ID_1,
	CSI_ID_2,
	CSI_ID_3,
	CSI_ID_4,
	CSI_ID_5,
	CSI_ID_MAX,
};

static inline uint32_t dcamonline_dec_align_width(uint32_t w, uint32_t layer_num)
{
	uint32_t width = 0, i = 0;
	uint32_t w_align = PYR_DEC_WIDTH_ALIGN;

	for (i = 0; i < layer_num; i++)
		w_align *= 2;

	width = ALIGN(w, w_align);
	return width;
}

static inline uint32_t dcamonline_dec_align_heigh(uint32_t h, uint32_t layer_num)
{
	uint32_t height = 0, i = 0;
	uint32_t h_align = PYR_DEC_HEIGHT_ALIGN;

	for (i = 0; i < layer_num; i++)
		h_align *= 2;

	height = ALIGN(h, h_align);
	return height;
}

static inline uint32_t dcam_if_cal_compressed_size(struct dcam_compress_cal_para *para)
{
	uint32_t payload_size = 0, lowbits_size = 0;
	int32_t tile_col = 0, tile_row = 0, header_size = 0, payload_unit = 0;

	if (para->data_bits == CAM_10_BITS)
		payload_unit = FBC_PAYLOAD_YUV10_BYTE_UNIT;
	else
		payload_unit = FBC_PAYLOAD_YUV8_BYTE_UNIT;

	tile_col = (para->width + DCAM_FBC_TILE_WIDTH - 1) / DCAM_FBC_TILE_WIDTH;
	tile_row = (para->height + DCAM_FBC_TILE_HEIGHT - 1) / DCAM_FBC_TILE_HEIGHT;

	/* header addr 16byte align, size 1024byte align*/
	header_size = tile_col * tile_row * FBC_HEADER_BYTE_UNIT;
	header_size = ALIGN(header_size, FBC_TILE_HEAD_SIZE_ALIGN);

	/*payload/lowbit addr 16byte align, lowbit addr = payload addr + payload size, so payload size 16byte align*/
	payload_size = tile_col * tile_row * payload_unit;
	payload_size = ALIGN(payload_size, FBC_STORE_ADDR_ALIGN);

	return header_size + payload_size + lowbits_size;
}

static inline void dcam_if_cal_compressed_addr(struct dcam_compress_cal_para *para)
{
	uint32_t payload_size = 0, lowbits_size = 0;
	int32_t tile_col = 0, tile_row = 0, header_size = 0, payload_unit = 0;
	struct dcam_compress_info *fbc_info = para->fbc_info;

	if (unlikely(!para || !para->out))
		return;

	if (para->data_bits == CAM_10_BITS)
		payload_unit = FBC_PAYLOAD_YUV10_BYTE_UNIT;
	else
		payload_unit = FBC_PAYLOAD_YUV8_BYTE_UNIT;

	tile_col = (para->width + DCAM_FBC_TILE_WIDTH - 1) / DCAM_FBC_TILE_WIDTH;
	tile_row = (para->height + DCAM_FBC_TILE_HEIGHT - 1) / DCAM_FBC_TILE_HEIGHT;

	/* header addr 16byte align, size 1024byte align*/
	header_size = tile_col * tile_row * FBC_HEADER_BYTE_UNIT;
	header_size = ALIGN(header_size, FBC_TILE_HEAD_SIZE_ALIGN);

	/*payload/lowbit addr 16byte align, lowbit addr = payload addr + payload size, so payload size 16byte align*/
	payload_size= tile_col * tile_row * payload_unit;
	payload_size = ALIGN(payload_size, FBC_STORE_ADDR_ALIGN);

	para->out->addr0 = ALIGN(para->in, FBC_STORE_ADDR_ALIGN);
	para->out->addr1 = ALIGN(para->out->addr0 + header_size, FBC_STORE_ADDR_ALIGN);

	if (fbc_info) {
		fbc_info->tile_col = tile_col;
		fbc_info->tile_row = tile_row;
		fbc_info->is_compress = 1;
		fbc_info->tile_row_lowbit = (para->width / 2 + FBC_LOWBIT_PITCH_ALIGN - 1) /
						FBC_LOWBIT_PITCH_ALIGN * FBC_LOWBIT_PITCH_ALIGN;
		fbc_info->header_size = header_size;
		fbc_info->payload_size = payload_size;
		fbc_info->lowbits_size = lowbits_size;
		fbc_info->buffer_size = header_size + payload_size + lowbits_size;
	}
}

#endif
