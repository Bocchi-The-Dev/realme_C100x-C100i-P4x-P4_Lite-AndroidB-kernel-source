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

#ifndef _DCAM_OFFLINE_NODE_H_
#define _DCAM_OFFLINE_NODE_H_

#include "cam_queue.h"
#include "cam_types.h"
#include "dcam_offline_port.h"
#include "dcam_core.h"

enum dcam_offline_node_id {
	DCAM_OFFLINE_NODE_ID,
	DCAM_OFFLINE_MAX_NODE_ID,
};

struct dcam_offline_node_desc {
	enum cam_node_type node_type;
	uint32_t csi_controller_idx;
	uint32_t pattern;
	enum cam_data_endian endian;
	enum cam_format fetch_fmt;
	enum cam_en_status statis_en;
	enum cam_en_status offline_pre_en;
	enum cam_en_status enable_3dnr;
	enum alg_types alg_type;
	enum camera_slice_mode dcam_slice_mode;
	struct img_size input_size;
	struct img_trim input_trim;
	struct img_size output_size;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	port_cfg_cb port_cfg_cb_func;
	void *port_cfg_cb_handle;
	void **node_dev;
	struct dcam_pipe_dev *dev;
	struct dcam_offline_port_desc port_desc;
	enum cam_node_buf_type buf_type;
	void *switch_recovery_lock;
};

struct dcam_offline_pm_context {
	atomic_t user_cnt;
	uint32_t ctx_id;
	struct dcam_isp_k_block blk_pm;
};

struct dcam_offline_node {
	enum cam_node_type node_type;
	uint32_t node_id;
	atomic_t user_cnt;
	atomic_t status;
	uint32_t offline_icap_dump_en;
	enum cam_en_status statis_en;
	enum cam_en_status offline_pre_en;
	enum cam_en_status is_3dnr;
	enum cam_en_status is_offline_gtm;
	enum camera_slice_mode dcam_slice_mode;
	enum alg_types alg_type;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	port_cfg_cb port_cfg_cb_func;
	void *port_cfg_cb_handle;
	struct cam_thread_info thread;
	timespec start_ts ;
	timespec end_ts;

	struct dcam_fetch_info fetch;
	struct dcam_offline_pm_context pm_ctx;
	struct camera_queue port_queue;
	struct cam_buf_pool_id in_pool;
	struct cam_buf_pool_id proc_pool;
	struct camera_queue blk_param_queue;
	struct completion frm_done;
	struct completion slice_done;
	struct camera_buf statis_buf_array[STATIS_TYPE_MAX][STATIS_BUF_NUM_MAX];

	uint32_t csi_controller_idx;
	uint32_t hw_ctx_id;
	uint32_t in_irq_proc;
	struct dcam_pipe_dev *dev;
	struct dcam_hw_context *hw_ctx;
	struct dcam_isp_k_block *pm;
	struct mutex blkpm_dcam_lock;
	void *switch_recovery_lock;
	atomic_t recovery_state;
};

int dcamoffline_blkpm_prepare(struct dcam_offline_node *node, void *param);
int dcam_offline_node_port_insert(struct dcam_offline_node *node, void *param);
struct dcam_offline_port *dcam_offline_node_port_get(struct dcam_offline_node *node, uint32_t port_id);
void dcam_offline_node_buffer_clr(struct dcam_offline_node *node);
int dcam_offline_fetch_param_reconfig(struct dcam_offline_node *node, void *param);
int dcam_offline_icap_dump_param_en(struct dcam_offline_node *node, void *param);
int dcam_offline_node_statis_cfg(struct dcam_offline_node *node, void *param);
int dcam_offline_node_blkpm_fid_set(struct dcam_offline_node *node, void *param);
void dcam_offline_node_param_ptr_reset(struct dcam_offline_node *node);
int dcam_offline_node_blk_param_set(struct dcam_offline_node *node, void *param);
int dcam_offline_node_request_proc(struct dcam_offline_node *node, void *param);
void dcam_offline_node_close(void *handle);
void *dcam_offline_node_get(uint32_t node_id, struct dcam_offline_node_desc *param);
void dcam_offline_node_put(struct dcam_offline_node *node);

#endif
