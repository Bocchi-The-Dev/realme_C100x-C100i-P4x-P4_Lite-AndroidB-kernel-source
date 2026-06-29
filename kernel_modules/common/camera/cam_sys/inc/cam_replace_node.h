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

#ifndef _CAM_REPLACE_NODE_H_
#define _CAM_REPLACE_NODE_H_

#include "cam_types.h"
#include "cam_queue.h"

#define REPLACE_NODE_Q_LEN              50
#define REPLACE_NUM_MAX                 12

struct cam_dbg_replace {
	uint32_t replace_en;
	uint32_t replace_pipeline_type;
	uint32_t replace_node_type;
	uint32_t replace_port_id;
	uint32_t replace_ongoing;
};
extern struct cam_dbg_replace g_dbg_replace;
extern uint32_t g_dbg_replace_src;
extern uint32_t g_dbg_replace_switch;

enum cam_replace_node_id {
	CAM_REPLACE_NODE_ID_0,
	CAM_REPLACE_NODE_ID_1,
	CAM_REPLACE_NODE_ID_2,
	CAM_REPLACE_NODE_ID_MAX,
};

enum replace_src_sel_type {
	REPLACE_IMG_YUV,
	REPLACE_IMG_MIPIRAW,
	REPLACE_IMG_RAW,
	REPLACE_IMG_RGB,
};

enum replace_cmd_type {
	REPLACE_CMD_EN,
	REPLACE_CMD_PORT_ID,
	REPLACE_CMD_NODE_TYPE,
	REPLACE_CMD_PIPE_TYPE,
	REPLACE_CMD_MAX,
};

struct cam_replace_msg {
	uint32_t size;
	uint32_t align_w;
	uint32_t align_h;
	uint32_t offset;
	uint32_t is_compressed;
	int layer_num;
};

struct cam_replace_node {
	uint32_t node_id;
	void *replace_cb_handle;
	cam_data_cb replace_cb_func;
	struct camera_queue replace_queue;
	struct completion replace_com;
	struct cam_thread_info thread;
};

int cam_replace_node_request_proc(struct cam_replace_node *node, void *param);
void cam_replace_node_set(void *param);
void *cam_replace_node_get(uint32_t node_id, cam_data_cb cb_func, void *priv_data);
void cam_replace_node_put(struct cam_replace_node *node);

#endif
