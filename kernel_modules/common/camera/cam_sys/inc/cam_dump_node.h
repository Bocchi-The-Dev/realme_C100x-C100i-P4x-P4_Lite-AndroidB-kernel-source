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

#ifndef _CAM_DUMP_NODE_H_
#define _CAM_DUMP_NODE_H_

#include "cam_types.h"
#include "cam_queue.h"

#define DUMP_NODE_Q_LEN                      50
#define DUMP_NUM_MAX                         10
#define AFBC_PADDING_W_YUV420_scaler         32
#define AFBC_PADDING_H_YUV420_scaler         8
#define AFBC_VERSION                         5
#define AFBC_FILEHEADER_SIZE                 32


extern uint32_t g_dbg_dumpswitch;
extern uint32_t g_dbg_dumpcount;

struct cam_dbg_dump {
	uint32_t dump_id;
	enum cam_en_status dump_en;
	enum cam_pipeline_type dump_pipeline_type;
	enum cam_node_type dump_node_type;
	uint32_t dump_port_id;
	uint32_t dump_count;
	enum cam_en_status dump_ongoing;
};
extern struct cam_dbg_dump g_dbg_dump[DUMP_NUM_MAX];

enum dump_afbc_format {
	DUMP_AFBC_Y_UV = 1,
	DUMP_AFBC_Y_VU = 2,
	DUMP_AFBC_FORMAT_MAX
};

enum cam_dump_node_id {
	CAM_DUMP_NODE_ID_0,
	CAM_DUMP_NODE_ID_1,
	CAM_DUMP_NODE_ID_2,
	CAM_DUMP_NODE_ID_3,
	CAM_DUMP_NODE_ID_MAX,
};

struct cam_dump_msg {
	uint32_t size;
	uint32_t align_w;
	uint32_t align_h;
	uint32_t offset;
	enum cam_en_status is_compressed;
	int layer_num;
};

struct cam_dump_fbc_header {
    uint8_t yuv_mirror_en;
    uint8_t yuv_format;
    uint8_t endian;
    uint8_t bits;
    int32_t img_h_pad;
    int32_t img_w_pad;
    int32_t img_h;
    int32_t img_w;
    int32_t fbc_buffer_size;
    uint8_t fbc_hdr_buffer[32];
};

struct cam_dump_node {
	uint32_t node_id;
	atomic_t user_cnt;
	uint32_t dump_cnt;
	void *dump_cb_handle;
	cam_data_cb dump_cb_func;
	struct camera_queue dump_queue;
	struct completion dump_com;
	struct cam_thread_info thread;
};

int cam_dump_node_request_proc(struct cam_dump_node *node, void *param);
void cam_dump_node_set(void *param);
void *cam_dump_node_get(uint32_t node_id, cam_data_cb cb_func, void *priv_data);
void cam_dump_node_put(struct cam_dump_node *node);

#endif
