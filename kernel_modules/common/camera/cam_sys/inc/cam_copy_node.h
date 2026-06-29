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

#ifndef _CAM_COPY_NODE_H_
#define _CAM_COPY_NODE_H_

#include "cam_types.h"
#include "cam_queue.h"

#define COPY_NODE_Q_LEN                      50

enum cam_copy_frame_mode {
	CAM_COPY_FRAME_IN_SRCBUF,
	CAM_COPY_FRAME_IN_DSTBUF,
	CAM_COPY_FRAME_MAX,
};

struct cam_copy_node_desc {
	enum cam_copy_frame_mode copy_mode;
	void *copy_cb_handle;
	cam_data_cb copy_cb_func;
	cap_frame_status_cb pre_frame_status;
	void *private_cb_data;
};

enum cam_copy_node_id {
	CAM_COPY_NODE_ID_0,
	CAM_COPY_NODE_ID_1,
	CAM_COPY_NODE_ID_MAX,
};

enum cam_copy_scene {
	CAM_COPY_NORMAL_SCENE,
	CAM_COPY_OPT_SCENE,
	CAM_COPY_ICAP_SCENE,
	CAM_COPY_MAX_SCENE,
};

struct cam_copy_node {
	uint32_t node_id;
	enum cam_en_status copy_flag;
	enum cam_copy_frame_mode copy_mode;
	enum cam_copy_scene scene_id;
	enum pre_raw_status pre_raw_flag;
	enum cam_en_status pre_cache_done;
	atomic_t icap_cap_num;
	uint32_t opt_buffer_num;
	atomic_t opt_frame_done;
	uint32_t record_channel_id;
	void *copy_cb_handle;
	cam_data_cb copy_cb_func;
	struct camera_queue in_queue;
	struct camera_queue out_queue;
	struct cam_thread_info thread;
	struct cam_capture_param cap_param;
	cap_frame_status_cb pre_frame_status;
	void *private_cb_data;
	uint32_t raw_deal_switch;
};

int cam_copy_node_buffer_cfg(void *handle, void *param);
int cam_copy_node_buffer_num(void *handle, void *param);
int cam_copy_node_buffers_alloc(void *handle, struct cam_buf_alloc_desc *param);
int cam_copy_outbuf_back(void *handle, void *param);
int cam_copy_cfg_param(void *handle, void *param);
int cam_copy_node_set_icap_scene(void *handle, void *param);
int cam_copy_node_set_pre_raw_flag(void *handle, void *param);
int cam_copy_node_set_raw_report_condition(void *handle, void *param);
int cam_copy_node_set_opt_scene(void *handle, void *param);
int cam_copy_node_request_proc(struct cam_copy_node *node, void *param);
void *cam_copy_node_get(uint32_t node_id, struct cam_copy_node_desc *param);
void cam_copy_node_put(struct cam_copy_node *node);

#endif
