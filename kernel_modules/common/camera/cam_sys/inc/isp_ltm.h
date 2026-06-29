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

#ifndef _ISP_LTM_H_
#define _ISP_LTM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cam_queue.h"

#define LTM_HIST_TABLE_NUM              128
#define LTM_MAX_TILE_RANGE              65536
#define LTM_MAX_ROI_X                   240
#define LTM_MAX_ROI_Y                   180
#define LTM_ID_MAX                      3

/*
 * 1. The static of preview frame N can be applied to another preview frame N+1
 * 2. The static of preview frame N can be applied to capture frame N
 *
 * 3. histogram statics support 128 bins (size 16 bits), Max statics is 65535
 * 4. ROI crop, max support is 240 x 180
 * 5. Horizen (8, 6, 4, 2) for L5/L5P/L6, Horizen (8, 6, 4) for N6P/N6L, no limitation in vertical
 * 6. Min tile is 128 x 20 for L5/L5P/L6, Min tile is 160 x 20 for N6P/N6L
 * 7. Max tile is 65536 (tile_height x tile_width)
 */
#define BIN_NUM_BIT                      7
#define TILE_NUM_MIN                     4
#define TILE_NUM_MAX                     8
#define TILE_MAX_SIZE                    (1 << 16)

typedef struct isp_ltm_hist_param {
	enum cam_en_status update;
	/* match ltm stat info */
	uint32_t bypass;
	uint32_t region_est_en;
	uint32_t text_point_thres;
	uint32_t text_proportion;
	uint32_t channel_sel;
	uint32_t buf_sel;
	/* input */
	uint32_t strength;
	uint32_t tile_num_auto;

	/* output / input */
	uint32_t tile_num_x;
	uint32_t tile_num_y;

	/* output */
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t tile_size;

	uint32_t frame_width;
	uint32_t frame_height;
	uint32_t clipLimit;
	uint32_t clipLimit_min;
	uint32_t binning_en;

	uint32_t cropUp;
	uint32_t cropDown;
	uint32_t cropLeft;
	uint32_t cropRight;
	uint32_t cropRows;
	uint32_t cropCols;
} ltm_param_t;

typedef struct isp_ltm_rtl_param {
	int tile_index_xs;
	int tile_index_ys;
	int tile_index_xe;
	int tile_index_ye;
	uint32_t tile_x_num_rtl;
	uint32_t tile_y_num_rtl;
	uint32_t tile_width_rtl;
	uint32_t tile_height_rtl;
	uint32_t tile_size_pro_rtl;
	uint32_t tile_start_x_rtl;
	uint32_t tile_start_y_rtl;
	uint32_t tile_left_flag_rtl;
	uint32_t tile_right_flag_rtl;
} ltm_map_rtl_t;

struct isp_ltm_hists {
	uint32_t bypass;
	uint32_t binning_en;
	uint32_t region_est_en;
	uint32_t buf_full_mode;
	uint32_t buf_sel;
	uint32_t channel_sel;
	uint32_t roi_start_x;
	uint32_t roi_start_y;
	uint32_t tile_width;
	uint32_t tile_num_x;
	uint32_t tile_num_x_minus;
	uint32_t tile_height;
	uint32_t tile_num_y;
	uint32_t tile_num_y_minus;
	uint32_t clip_limit;
	uint32_t clip_limit_min;
	uint32_t texture_proportion;
	uint32_t text_point_thres;
	unsigned long addr;
	uint32_t pitch;
	uint32_t wr_num;
	uint32_t ltm_hist_table[LTM_HIST_TABLE_NUM];
};

struct isp_ltm_map {
	uint32_t bypass;
	uint32_t burst8_en;
	uint32_t hist_error_en;
	uint32_t fetch_wait_en;
	uint32_t fetch_wait_line;
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t tile_x_num;
	uint32_t tile_y_num;
	uint32_t tile_size_pro;
	uint32_t tile_start_x;
	uint32_t tile_left_flag;
	uint32_t tile_start_y;
	uint32_t tile_right_flag;
	unsigned long mem_init_addr;
	uint32_t hist_pitch;
};

struct isp_ltm_sync {
	atomic_t user_cnt;
	uint32_t pre_ctx_status;
	uint32_t cap_ctx_status;

	uint32_t pre_cid;
	uint32_t cap_cid;
	atomic_t pre_fid;
	atomic_t cap_fid;

	uint32_t pre_update;
	uint32_t cap_update;

	uint32_t pre_hist_bypass;

	uint32_t pre_frame_h;
	uint32_t pre_frame_w;
	uint32_t cap_frame_h;
	uint32_t cap_frame_w;

	uint32_t tile_num_x_minus;
	uint32_t tile_num_y_minus;
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t pre_slice_ltm_bypass;

	atomic_t wait_completion;
	struct completion share_comp;
	struct mutex share_mutex;
	struct camera_frame_buf ltm_frame[ISP_LTM_BUF_NUM];
};

enum isp_ltm_cfg_cmd {
	ISP_LTM_CFG_EB,
	ISP_LTM_CFG_MODE,
	ISP_LTM_CFG_SIZE_INFO,
	ISP_LTM_CFG_FRAME_ID,
	ISP_LTM_CFG_MAP_BUF,
	ISP_LTM_CFG_HIST_BYPASS,
	ISP_LTM_CFG_MAP_BYPASS,
	ISP_LTM_CFG_MAX,
};

struct isp_ltm_ops {
	int (*cfg_param)(void *handle, enum isp_ltm_cfg_cmd cmd, void *param);
	int (*pipe_proc)(void *handle, void *param);
};

struct isp_ltm_ctx_desc {
	uint32_t enable;
	uint32_t bypass;
	uint32_t mode;
	uint32_t ctx_id;
	uint32_t cam_id;
	uint32_t cur_mapbuf_id;

	uint32_t fid;
	uint32_t slice_num;
	uint32_t slice_no;
	uint32_t frame_width;
	uint32_t frame_height;
	uint32_t frame_width_stat;
	uint32_t frame_height_stat;

	enum cam_en_status ltmhist_update;
	struct isp_ltm_hists hists;
	struct isp_ltm_map map;
	struct camera_frame_buf ltm_frame[ISP_LTM_BUF_NUM];

	struct isp_ltm_ops ltm_ops;
	struct cam_hw_info *hw;

	reserved_buf_get_cb resbuf_get_cb;
	void *resbuf_cb_data;
	struct cam_buf_pool_id *hist_outpool;
	struct cam_buf_pool_id *hist_resultpool;
	void *buf_manager_handle;
};

struct isp_ltm_tilenum_minus1 {
	uint32_t tile_num_x;
	uint32_t tile_num_y;
};

struct isp_ltm_text {
	uint32_t text_point_alpha;
	uint32_t text_point_thres;
	uint32_t textture_proporion;
};

struct isp_ltm_stat_info {
	uint32_t bypass;
	struct isp_ltm_tilenum_minus1 tile_num;
	struct isp_ltm_text ltm_text;
	uint32_t strength;
	uint32_t tile_num_auto;
	uint32_t text_point_thres;
	uint32_t region_est_en;
	uint32_t channel_sel;
	uint32_t ltm_hist_table[LTM_HIST_TABLE_NUM];
	uint16_t clip_limit;
	uint16_t clip_limit_min;
};

struct isp_ltm_map_info {
	uint32_t bypass;
	uint32_t ltm_map_video_mode;
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t tile_x_num;
	uint32_t tile_y_num;
	uint32_t frame_width;
	uint32_t frame_height;
};

struct isp_ltm_info {
	uint32_t isupdate;
	uint32_t update_flag;
	struct isp_ltm_stat_info ltm_stat;
	struct isp_ltm_map_info ltm_map;
};

int isp_ltm_map_slice_config_gen(struct isp_ltm_ctx_desc *ctx, struct isp_ltm_rtl_param *prtl, uint32_t *slice_info, void *ltminfo);
void *isp_ltm_rgb_ctx_get(uint32_t idx, enum camera_id cam_id, void *hw);
void isp_ltm_rgb_ctx_put(void *ltm_handle);

#ifdef __cplusplus
}
#endif

#endif /* _ISP_LTM_H_ */
