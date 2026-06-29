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

#ifndef _DCAM_OFFLINE_PORT_H_
#define _DCAM_OFFLINE_PORT_H_

#include "cam_types.h"
#include "cam_port.h"

struct dcam_offline_port_desc {
	void **port_dev;
	cam_data_cb data_cb_func;
	zoom_get_cb zoom_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	void *zoom_cb_handle;
	enum cam_en_status compress_en;
	enum cam_data_endian endian;
	enum cam_format dcam_out_fmt;
	enum cam_port_transfer_type transfer_type;
	enum cam_en_status is_pyr_rec;
	enum cam_format pyr_out_fmt;
	uint32_t src_sel;
	uint32_t offline_need_dcam_raw;
};

struct dcam_offline_port {
	struct cam_q_head list;
	enum cam_port_dcam_offline_out_id port_id;
	atomic_t user_cnt;
	atomic_t is_work; /* dynamic switch counter of port */

	uint32_t src_sel;
	enum cam_data_endian endian;
	enum cam_format out_fmt;
	enum cam_port_transfer_type type;
	enum cam_en_status is_pyr_rec;
	enum cam_format pyr_out_fmt;
	uint32_t out_pitch;
	uint32_t bin_ratio;
	uint32_t scaler_sel;
	uint32_t offline_need_dcam_raw;
	enum cam_en_status compress_en;
	struct img_size in_size;
	struct img_trim in_trim;
	struct img_size out_size;
	struct yuv_scaler_info scaler_info;
	void *priv_size_data;

	void *data_cb_handle;
	void *buf_manager_handle;
	void *zoom_cb_handle;
	cam_data_cb data_cb_func;
	zoom_get_cb zoom_cb_func;
	port_param_cfg port_param_cfg_func;

	struct cam_buf_pool_id unprocess_pool;
	struct cam_buf_pool_id result_pool;
};

int dcam_offline_port_size_cfg(void *handle, void *param);
int dcam_offline_port_param_cfg(void *handle, enum cam_port_cfg_cmd cmd, void *param);
int dcam_offline_port_buf_alloc(void *handle, struct cam_buf_alloc_desc *param);
int dcam_offline_lsc_raw_port_buf_alloc(void *handle, struct cam_buf_alloc_desc *param);
void *dcam_offline_port_get(uint32_t port_id, struct dcam_offline_port_desc *param);
void dcam_offline_port_put(struct dcam_offline_port *port);

#endif
