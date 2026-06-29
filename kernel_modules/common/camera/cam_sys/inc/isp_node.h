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

#ifndef _ISP_NODE_H_
#define _ISP_NODE_H_

#include <os_adapt_common.h>
#include "cam_types.h"
#include "isp_port.h"
#include "isp_ltm.h"
#include "isp_int.h"

enum isp_node_mode_id {
	ISP_NODE_MODE_PRE_ID,
	ISP_NODE_MODE_VID_ID,
	ISP_NODE_MODE_CAP_ID,
	ISP_NODE_MODE_OFFLINE_CAP_ID,
	ISP_NODE_MODE_MAX_ID,
};

enum isp_node_cfg_cmd {
	ISP_NODE_CFG_INIT,
	ISP_NODE_CFG_BASE,
	ISP_NODE_CFG_INSERT_PORT,
	ISP_NODE_CFG_CLEAR_PORT,
	ISP_NODE_CFG_CAP_PARAM,
	ISP_NODE_CFG_SIZE_CFG,
	ISP_NODE_CFG_BLK_PATAM,
	ISP_NODE_CFG_PORT_UFRAME,
	ISP_NODE_CFG_PORT_BUF,
	ISP_NODE_CFG_STATIS,
	ISP_NODE_CFG_RESERVE_BUF,
	ISP_NODE_CFG_3DNR_MODE,
	ISP_NODE_CFG_GTM,
	ISP_NODE_CFG_PARAM_SWITCH,
	ISP_NODE_CFG_POSTPROC_PARAM,
	ISP_NODE_CFG_PARAM_Q_CLEAR,
	ISP_NODE_CFG_FAST_STOP,
	ISP_NODE_CFG_XTM_EN,
	ISP_NODE_CFG_CLEAR_FAST_STOP,
	ISP_NODE_CFG_CMD_MAX,
};

struct isp_node_postproc_param {
	uint32_t zoom_ratio;
	uint32_t total_zoom;
};

struct isp_node_slwm960fps_frame_num {
	uint32_t stage_a_frame_num;
	uint32_t stage_b_frame_num;
	uint32_t stage_c_frame_num;
};

struct isp_node_uinfo {
	enum cam_en_status ltm_rgb;
	enum isp_ltm_mode mode_ltm;
	enum cam_en_status gtm_rgb;
	enum isp_gtm_mode mode_gtm;
	enum isp_3dnr_mode mode_3dnr;
	enum cam_en_status enable_slowmotion;
	uint32_t slowmotion_240fp_count;
	uint32_t slowmotion_count;
	uint32_t cap_type;
	int64_t cap_timestamp;
	struct slowmotion_960fps_info slw_960desc;
	struct isp_node_slwm960fps_frame_num frame_num;
	uint32_t stage_a_valid_count;
	uint32_t uframe_sync;
	uint32_t scaler_coeff_ex;
	uint32_t pyr_layer_num;
	enum cam_en_status nr3_fbc_fbd;
	enum isp_fetch_path_select fetch_path_sel;
	enum cam_en_status is_node_close;
};

struct isp_node_desc {
	enum cam_en_status is_dual;
	enum cam_en_status ultra_cap_en;
	enum cam_node_type node_type;
	enum isp_3dnr_mode mode_3dnr;
	enum isp_ltm_mode mode_ltm;
	enum cam_en_status ltm_rgb;
	enum isp_gtm_mode mode_gtm;
	enum cam_en_status gtm_rgb;
	enum cam_en_status vid_cap_en;
	enum cam_format in_fmt;
	uint32_t bayer_pattern;
	enum cam_en_status enable_slowmotion;
	uint32_t slowmotion_count;
	enum cam_format pyr_out_fmt;
	enum cam_format store_3dnr_fmt;
	enum cam_en_status nr3_fbc_fbd;
	uint32_t share_buffer;
	uint32_t pyr_layer_num;
	enum isp_fetch_path_select fetch_path_sel;
	enum cam_ch_id ch_id;
	struct img_size sn_size;
	enum cam_en_status is_high_fps;
	uint32_t cam_id;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	void **node_dev;
	struct isp_port_desc port_desc;
	reserved_buf_get_cb resbuf_get_cb;
	void *resbuf_cb_data;
	uint32_t blkparam_node_num;
	/*TEMP DEC*/
	void *isp_node;
	void *dev;
	struct slowmotion_960fps_info fps960_info;
};

struct isp_node {
	uint32_t cfg_id;
	enum cam_node_type node_type;
	uint32_t node_id;
	atomic_t user_cnt;
	enum cam_en_status is_bind;
	enum cam_en_status is_dual;
	enum cam_en_status ultra_cap_en;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	uint32_t pctx_hw_id;
	uint32_t in_irq_postproc;
	atomic_t state_user_cnt;
	uint32_t slw_frm_cnt;
	uint32_t is_fast_stop;
	uint32_t iommu_status;
	uint32_t share_buffer;
	enum camera_id attach_cam_id;
	enum cam_ch_id ch_id;
	struct img_size src;
	timespec start_ts;
	timespec end_ts;

	struct isp_node_uinfo uinfo;
	struct isp_node_uinfo pipe_src;

	struct camera_queue port_queue;
	struct isp_pipe_dev *dev;
	struct cam_debug_cfg debug;
	struct dcam_isp_k_block isp_k_param;
	struct cam_frame *isp_receive_param;
	struct dcam_isp_k_block *isp_using_param;
	struct cam_frame *blk_param_node;

	struct cam_thread_info thread;
	struct completion frm_done;
	struct completion ultra_cap_com;
	struct completion *fast_stop_done;
	/* lock block param to avoid acrossing frame */
	struct mutex blkpm_lock;
	struct camera_queue param_share_queue;
	struct camera_queue param_buf_queue;
	struct mutex blkpm_q_lock;

	struct camera_buf statis_buf_array[STATIS_TYPE_MAX][STATIS_BUF_NUM_MAX];
	struct cam_buf_pool_id hist2_pool;
	struct cam_buf_pool_id gtmhist_outpool;
	struct cam_buf_pool_id gtmhist_resultpool;
	struct cam_buf_pool_id ltmhist_outpool;
	struct cam_buf_pool_id ltmhist_resultpool;
	enum cam_en_status ltm_hist_enable;
	enum cam_en_status ltm_hist_en;
	struct isp_int_ctxs_com ctxs_com;

	void *rgb_ltm_handle;
	void *rgb_gtm_handle;
	void *nr3_handle;
	void *rec_handle;
	struct cam_frame *postproc_buf;
	uint32_t nr3_blend_cnt;
};

int isp_node_prepare_blk_param(struct isp_node *inode, uint32_t target_fid, struct blk_param_info *out);
int isp_node_buffers_alloc(void *handle, struct cam_buf_alloc_desc *param);
uint32_t isp_node_config(void *node, enum isp_node_cfg_cmd cmd, void *param);
int isp_node_request_proc(struct isp_node *node, void *param);
void *isp_node_get(uint32_t node_id, struct isp_node_desc *param);
void isp_node_put(struct isp_node *node);
void isp_node_close(struct isp_node *node);
void isp_node_debug_init(struct isp_node *node, uint32_t ch_id);

#endif
