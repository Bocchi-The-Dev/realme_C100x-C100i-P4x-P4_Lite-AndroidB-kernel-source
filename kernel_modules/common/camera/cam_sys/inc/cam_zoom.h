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

#ifndef _CAM_ZOOM_H_
#define _CAM_ZOOM_H_

#include "cam_core.h"
#include "cam_port.h"

#ifdef CAM_ZOOM_DEBUG_ON
#define CAM_ZOOM_DEBUG pr_info
#else
#define CAM_ZOOM_DEBUG pr_debug
#endif

#define CAM_SCALER_DECI_FAC_MAX    4
#define ISP_SC_COEFF_UP_MAX        ISP_SCALER_UP_MAX
#define ISP_SC_COEFF_DOWN_MAX      ISP_SCALER_UP_MAX

#define ZOOM_PORT_DCAM_MAX   (PORT_FULL_OUT + 1)
#define ZOOM_PORT_ISP_MAX    (PORT_THUMB_OUT + 1)

struct cam_zoom_index {
	struct cam_zoom_frame *zoom_data;
	enum cam_node_type node_type;
	uint32_t node_id;
	enum cam_port_transfer_type port_type;
	uint32_t port_id;
};

struct cam_zoom_desc {
	enum cam_pipeline_type pipeline_type;
	struct cam_pipeline_topology *pipeline_graph;
	struct cam_zoom_frame *latest_zoom_info;
	spinlock_t *zoom_lock;
	struct camera_queue *zoom_info_q;
	struct img_size sn_rect_size;
	uint32_t zoom_ratio_width;
	uint32_t total_crop_width;
	struct img_trim ori_user_crop;
	struct img_size dcam_src_size;
	struct img_trim dcam_crop[CAM_NODE_TYPE_MAX][ZOOM_PORT_DCAM_MAX];
	struct img_size dcam_dst[CAM_NODE_TYPE_MAX][ZOOM_PORT_DCAM_MAX];
	struct img_size isp_src_size;
	struct img_trim isp_input_crop;
	struct img_trim isp_crop[ZOOM_PORT_ISP_MAX];
	struct img_size isp_dst[ZOOM_PORT_ISP_MAX];
};

uint32_t cam_zoom_port_deci_factor_get(uint32_t src_size, uint32_t dst_size, uint32_t deci_fac_max);
void cam_zoom_diff_trim_get(struct sprd_img_rect *orig, uint32_t ratio16, struct img_trim *trim0, struct img_trim *trim1);
int cam_zoom_crop_size_align(struct camera_module *module, struct sprd_img_rect *crop, uint32_t channel_id);
int cam_zoom_channels_size_init(struct camera_module *module);
int cam_zoom_channel_size_calc(struct camera_module *module);
int cam_zoom_channel_size_config(struct camera_module *module, struct channel_context *channel);
int cam_zoom_start_proc(void *param);
int cam_zoom_param_set(struct cam_zoom_desc *zoom_info);
int cam_zoom_frame_base_get(struct cam_zoom_base *zoom_base, struct cam_zoom_index *zoom_index);
int cam_zoom_port_param_update(struct cam_zoom_frame *zoom_data, struct cam_zoom_index *zoom_index, struct cam_zoom_base *zoom_param);

#endif
