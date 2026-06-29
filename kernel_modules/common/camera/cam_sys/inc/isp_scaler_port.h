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

#ifndef _ISP_SCALER_PORT_H_
#define _ISP_SCALER_PORT_H_

#include "cam_types.h"
#include "cam_port.h"

#define ISP_SCALER_IN_Q_LEN         8
#define ISP_SCALER_PROC_Q_LEN       2

enum isp_scaler_port_cfg_callback {
	ISP_SCALER_PORT_FRAME_CYCLE,
	ISP_SCALER_PORT_PIPEINFO_GET,
	ISP_SCALER_PORT_SIZE_UPDATE,
	ISP_SCALER_PORT_CFG_MAX,
};

struct isp_scaler_port {
	struct cam_q_head list;
	uint32_t bayer_pattern;
	uint32_t data_endian;
	atomic_t user_cnt;
	uint32_t fmt;
	uint32_t port_id;
	uint32_t scaler_coeff_ex;
	uint32_t scaler_bypass_ctrl;
	uint32_t regular_mode;
	uint32_t type;
	struct img_size size;
	struct img_trim trim;
	struct img_size dst;
	void *data_cb_handle;
	void *buf_manager_handle;
	cam_data_cb data_cb_func;
	port_cfg_cb port_cfg_cb_func;
	struct camera_queue out_buf_queue;
	struct camera_queue result_queue;
	reserved_buf_get_cb resbuf_get_cb;
	void *resbuf_cb_data;
};

struct isp_scaler_port_cfg {
	uint32_t cfg_id;
	uint32_t in_fmt;
	uint32_t node_id;
	int valid_out_frame;
	int hw_ctx_id;
	uint32_t scaler_coeff_ex;
	uint32_t scaler_bypass_ctrl;
	uint32_t thumb_scaler_cal_version;
	uint32_t target_fid;
	void *node_handle;
	struct img_size src_size;
	struct img_trim src_crop;
	struct isp_pipe_info *pipe_info;
	struct cam_frame *src_frame;
};

struct isp_scaler_port_desc {
	enum cam_port_transfer_type transfer_type;
	uint32_t in_fmt;
	uint32_t out_fmt;
	uint32_t bayer_pattern;
	uint32_t endian;
	uint32_t regular_mode;
	void **port_dev;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	reserved_buf_get_cb resbuf_get_cb;
	void *resbuf_cb_data;
	struct img_size output_size;
};

uint32_t isp_scaler_port_id_switch(uint32_t port_id);
void *isp_scaler_port_get(uint32_t port_id, struct isp_scaler_port_desc *param);
void isp_scaler_port_put(struct isp_scaler_port *port);

#endif

