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

#ifndef _ISP_INTERFACE_H_
#define _ISP_INTERFACE_H_

#include <linux/of.h>
#include <linux/platform_device.h>

#include "dcam_hw_adpt.h"

#define ISP_MAX_LINE_WIDTH              2592
#define ISP_NR3_BUF_NUM                 2
#define ISP_LTM_BUF_NUM                 2
#define ISP_PYR_REC_BUF_NUM             2
#define CAMERA_RESERVE_FRAME_NUM        0xffffffff
#define ISP_FBC_3DNR_PAD_WIDTH          256
#define ISP_FBC_3DNR_PAD_HEIGHT         4
#define ISP_FBC_STORE_TILE_WIDTH        32
#define ISP_FBC_STORE_TILE_HEIGHT       8
#define ISP_PYR_DEC_LAYER_NUM           5
#define MAX_PYR_DEC_LAYER_NUM           (ISP_PYR_DEC_LAYER_NUM + 1)
#define ISP_PYRDEC_BUF_Q_LEN            8
#define ISP_GTMHIST_BUF_Q_LEN           16
#define ISP_LTMHIST_BUF_Q_LEN           16
#define ISP_HIST2_BUF_Q_LEN             16
#define ISP_CONTEXT_TIMEOUT             msecs_to_jiffies(2000)
#define ISP_MAX_COUNT                   1
#define ISP_CONTEXT_MAX                 4
#define ISP_CONTEXT_SW_NUM              13
#define ISP_OVERSIZE_LTM_SUBIMG_NUM     4
#define ISP_NONZSL_PARAM_BUF_NUM        4

extern unsigned long s_isp_regbase[ISP_MAX_COUNT];
extern unsigned long isp_phys_base[ISP_MAX_COUNT];
extern unsigned long *isp_cfg_poll_addr[ISP_CONTEXT_SW_NUM];
extern unsigned long s_isp_mmubase;

enum isp_context_hw_id {
	ISP_CONTEXT_HW_P0,
	ISP_CONTEXT_HW_P1,
	ISP_CONTEXT_HW_C0,
	ISP_CONTEXT_HW_C1,
	ISP_CONTEXT_HW_NUM
};

enum isp_work_mode {
	ISP_CFG_MODE,
	ISP_AP_MODE,
	ISP_WM_MAX
};

enum isp_3dnr_mode {
	MODE_3DNR_OFF,
	MODE_3DNR_PRE,
	MODE_3DNR_CAP,
	MODE_3DNR_MAX,
};

enum isp_ltm_mode {
	MODE_LTM_OFF,
	MODE_LTM_PRE,
	MODE_LTM_CAP,
	MODE_LTM_MAX
};

enum isp_ltm_buf_mode {
	MODE_LTM_BUF_OFF,
	MODE_LTM_BUF_SET,
	MODE_LTM_BUF_GET,
};

enum isp_gtm_mode {
	MODE_GTM_OFF,
	MODE_GTM_PRE,
	MODE_GTM_CAP,
	MODE_GTM_MAX
};

enum isp_ioctrl_cmd {
	ISP_IOCTL_CFG_SEC,
	ISP_IOCTL_INIT_NODE_HW,
	ISP_IOCTL_CFG_MAX,
};

enum isp_stream_state {
	ISP_STREAM_NORMAL_PROC,
	ISP_STREAM_POST_PROC,
	ISP_STREAM_MAX,
};

enum isp_stream_buf_type {
	ISP_STREAM_BUF_OUT,
	ISP_STREAM_BUF_RESERVED,
	ISP_STREAM_BUF_POSTPROC,
	ISP_STREAM_BUF_RESULT,
	ISP_STREAM_BUF_MAX,
};

struct isp_size_desc {
	struct img_size size;
	struct img_trim trim;
	uint32_t zoom_conflict_with_ltm;
};

struct isp_ctx_compress_desc {
	uint32_t fetch_fbd;
	uint32_t nr3_fbc_fbd;
};

struct isp_path_base_desc {
	uint32_t port_id;
	enum cam_format out_fmt;
	uint32_t regular_mode;
	enum cam_data_endian endian;
	uint32_t is_work;
	struct img_size output_size;
};

struct cfg_param_status {
	uint32_t status; /* 0: start; 1: end */
	uint32_t frame_id; /* sof id */
	uint32_t scene_id;
	uint32_t update;
	uint32_t dcam_ctx_bind_state;/* 0: dcam_ctx_unbind, 1: dcam_ctx_bind */
	void *blkpm_ptr;
};

static inline void isp_3dnr_cal_compressed_addr(uint32_t width,
			uint32_t height, unsigned long in,
			struct compressed_addr *out)
{
	uint32_t pixel_count, header_bytes_y, header_bytes_c;

	if (unlikely(!out))
		return;

	pixel_count = roundup(width, ISP_FBC_3DNR_PAD_WIDTH) *
		roundup(height, ISP_FBC_3DNR_PAD_HEIGHT);
	/* add some redundant space for fbc output */
	header_bytes_y = pixel_count >> 9;
	header_bytes_y += FBC_HEADER_REDUNDANT;
	header_bytes_c = pixel_count >> 10;
	header_bytes_c += FBC_HEADER_REDUNDANT;

	out->addr0 = (uint32_t)in;
	out->addr1 = ALIGN(out->addr0 + header_bytes_y, FBC_TILE_ADDR_ALIGN);
	out->addr2 = ALIGN(out->addr1 + header_bytes_c + pixel_count,
			FBC_TILE_ADDR_ALIGN);
}

static inline uint32_t
isp_3dnr_cal_compressed_size(uint32_t width, uint32_t height)
{
	uint32_t pixel_count, header_y, tile_y, header_c, tile_c;

	pixel_count = roundup(width, ISP_FBC_3DNR_PAD_WIDTH) *
		roundup(height, ISP_FBC_3DNR_PAD_HEIGHT);
	header_y = pixel_count >> 9;
	header_y += FBC_HEADER_REDUNDANT + FBC_TILE_ADDR_ALIGN;
	tile_y = pixel_count;
	header_c = pixel_count >> 10;
	header_c += FBC_HEADER_REDUNDANT + FBC_TILE_ADDR_ALIGN;
	tile_c = pixel_count >> 1;

	return header_y + tile_y + header_c + tile_c;
}

static inline uint32_t isp_rec_layer0_width(uint32_t w, uint32_t layer_num)
{
	uint32_t width = 0, i = 0;
	uint32_t w_align = PYR_DEC_WIDTH_ALIGN;

	for (i = 0; i < layer_num; i++)
		w_align *= 2;

	width = ALIGN(w, w_align);
	return width;
}

static inline uint32_t isp_rec_layer0_heigh(uint32_t h, uint32_t layer_num)
{
	uint32_t height = 0, i = 0;
	uint32_t h_align = PYR_DEC_HEIGHT_ALIGN;

	for (i = 0; i < layer_num; i++)
		h_align *= 2;

	height = ALIGN(h, h_align);
	return height;
}

static inline uint32_t isp_rec_small_layer_w(uint32_t w, uint32_t layer_num)
{
	uint32_t width = 0, i = 0;
	uint32_t w_align = PYR_DEC_WIDTH_ALIGN;

	for (i = 0; i < layer_num; i++)
		w_align *= 2;

	width = ALIGN(w, w_align);
	for (i = 0; i < layer_num; i++)
		width = width / 2;

	return width;
}

static inline uint32_t isp_rec_small_layer_h(uint32_t h, uint32_t layer_num)
{
	uint32_t height = 0, i = 0;
	uint32_t h_align = PYR_DEC_HEIGHT_ALIGN;

	for (i = 0; i < layer_num; i++)
		h_align *= 2;

	height = ALIGN(h, h_align);
	for (i = 0; i < layer_num; i++)
		height = height / 2;

	return height;
}

/******************************************************************************/
#define ISP_PHYS_ADDR(idx)                      (isp_phys_base[idx])
#define ISP_BASE_ADDR(idx)                      (*(isp_cfg_poll_addr[idx]))
#define ISP_GET_REG(reg)                        (ISP_PHYS_ADDR(0) + (reg))

#define ISP_REG_WR(idx, reg, val)               (REG_WR(ISP_BASE_ADDR(idx) + (reg), (val)))
#define ISP_REG_RD(idx, reg)                    (REG_RD(ISP_BASE_ADDR(idx) + (reg)))
#define ISP_REG_MWR(idx, reg, msk, val)         (ISP_REG_WR((idx), (reg), ((val) & (msk)) |(ISP_REG_RD((idx), (reg)) & (~(msk)))))
#define ISP_REG_OWR(idx, reg, val)              (ISP_REG_WR((idx), (reg), (ISP_REG_RD((idx), (reg)) | (val))))

#define ISP_HREG_WR(reg, val)                   (REG_WR(s_isp_regbase[0] + (reg), (val)))
#define ISP_HREG_RD(reg)                        (REG_RD(s_isp_regbase[0] + (reg)))
#define ISP_HREG_MWR(reg, msk, val)             (REG_WR(s_isp_regbase[0] + (reg), ((val) & (msk)) | (REG_RD(s_isp_regbase[0] + (reg)) & (~(msk)))))
#define ISP_HREG_OWR(reg, val)                  (REG_WR(s_isp_regbase[0] + (reg), (REG_RD(s_isp_regbase[0] + (reg)) | (val))))

#define ISP_MMU_BASE s_isp_mmubase
#define ISP_MMU_WR(reg, val)                    (REG_WR(ISP_MMU_BASE+(reg), (val)))
#define ISP_MMU_RD(reg)                         (REG_RD(ISP_MMU_BASE+(reg)))
#define ISP_MMU_MWR(reg, msk, val)              ISP_MMU_WR((reg), ((val) & (msk)) | (ISP_MMU_RD(reg) & (~(msk))))

#endif
