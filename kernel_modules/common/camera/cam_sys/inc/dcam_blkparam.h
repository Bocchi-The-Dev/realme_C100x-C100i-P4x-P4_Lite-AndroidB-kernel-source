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

#ifndef _DCAM_BLKPARAM_H_
#define _DCAM_BLKPARAM_H_

#include "isp_hw.h"

struct dcam_dev_lsc_param {
	uint32_t update;
	uint32_t load_trigger;
	uint32_t weight_tab_size;
	uint32_t weight_tab_size_x;
	uint32_t weight_tab_size_y;
	uint32_t grid_x_index;
	uint32_t grid_offset;
	void *weight_tab;
	void *weight_tab_x;
	void *weight_tab_y;
	struct mutex lsc_lock;
	struct camera_buf buf;
	struct dcam_dev_lsc_info lens_info;
};

struct dcam_dev_gamma_param_v1 {
	uint32_t buf_sel;
	struct isp_dev_gamma_info_v1 gamma_info;
};

struct dcam_dev_blc_param {
	struct dcam_dev_blc_info blc_info;
};

struct dcam_dev_rgb_param {
	struct dcam_dev_rgb_gain_info gain_info;
	struct dcam_dev_rgb_dither_info rgb_dither;
};

struct dcam_dev_hist_param {
	uint32_t update;
	struct dcam_dev_hist_info bayerHist_info;
};

struct dcam_dev_hist_roi_param {
	uint32_t update;
	struct isp_dev_hist2_info hist_roi_info;
};

struct dcam_dev_afl_param {
	struct isp_dev_anti_flicker_new_info afl_info;
};

struct dcam_dev_awbc_param {
	struct dcam_dev_awbc_info awbc_info;
};

struct dcam_dev_bpc_param {
	struct dcam_bpc_ppi_info bpc_ppi_info;
	union {
		struct dcam_dev_bpc_info bpc_info;
		struct dcam_dev_bpc_info_l3 bpc_info_l3;
	} bpc_param;
};

struct dcam_dev_n6pro_bpc_param {
	struct dcam_bpc_ppi_info ppi_info;
	union {
		struct dcam_dev_bpc_info_v1 bpc_info;
		struct dcam_dev_bpc_info_l3 bpc_info_l3;
	} bpc_param_n6pro;
};

struct dcam_dev_grgb_param {
	struct isp_dev_grgb_info grgb_info;
};

struct dcam_dev_3dnr_param {
	struct dcam_dev_3dnr_me nr3_me;
};

struct dcam_dev_gtm_param {
	uint32_t gtm_calc_mode;
	struct dcam_dev_raw_gtm_block_info gtm_info;
};

struct dcam_dev_rgb_gtm_param {
	uint32_t gtm_calc_mode;
	struct dcam_dev_rgb_gtm_block_info rgb_gtm_info;
};

#endif
