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

#ifndef _DCAM_FETCH_NODE_H_
#define _DCAM_FETCH_NODE_H_

#include "cam_types.h"
#include "dcam_online_node.h"
#include "dcam_online_port.h"
#include "cam_debugger.h"
#include "cam_hw.h"
#include "dcam_interface.h"

#define DCAM_FETCH_IN_Q_LEN             20
#define DCAM_FETCH_PROC_Q_LEN           12
#define DCAM_FETCH_TIMEOUT              msecs_to_jiffies(2000)

struct dcam_fetch_node_desc {
	struct dcam_online_node_desc *online_node_desc;
	enum cam_format fetch_fmt;
	enum cam_en_status virtualsensor;
	enum cam_en_status offline_pre_en;
	void **node_dev;
};

struct dcam_fetch_node {
	atomic_t user_cnt;
	uint32_t fetch_online_en;
	struct dcam_online_node online_node;
	struct dcam_hw_context *hw_ctx;
	enum cam_en_status offline_pre_en;

	struct dcam_fetch_info fetch_info;
	struct camera_queue in_queue;
	struct camera_queue proc_queue;
	struct completion frm_done;
	struct completion slice_done;
	struct cam_thread_info thread;
};

int dcam_fetch_node_blk_param_set(struct dcam_fetch_node *node, void *param);
int dcam_fetch_node_state_get(void *handle);
int dcam_fetch_node_port_insert(struct dcam_fetch_node* node, void *param);
int dcam_fetch_node_cfg_param(void *handle, uint32_t cmd, void *param);
int dcam_fetch_share_buf(void *handle, void *param);
int dcam_fetch_node_reset(struct dcam_fetch_node *node, void *param);
int dcam_fetch_node_request_proc(struct dcam_fetch_node *node, void *param);
int dcam_fetch_node_stop_proc(struct dcam_fetch_node *node, void *param);
void *dcam_fetch_node_get(uint32_t node_id, struct dcam_fetch_node_desc *param);
void dcam_fetch_node_put(struct dcam_fetch_node *node);

#endif
