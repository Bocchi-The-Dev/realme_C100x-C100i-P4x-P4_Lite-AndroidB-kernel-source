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

#ifndef _CAM_PORT_H_
#define _CAM_PORT_H_

#include "cam_types.h"
#include "cam_queue.h"
#include "dcam_hw_adpt.h"
#include "cam_scaler.h"
#include "cam_buf_manager.h"

#define CAM_LINK_DEFAULT_NODE_ID     0xFFFF
#define CAM_LINK_DEFAULT_PORT_ID     0xFFFF

#define IS_VALID_DCAM_PORT_ID(id) ((id) >= PORT_RAW_OUT && (id) < PORT_DCAM_OUT_MAX)
#define IS_VALID_ISP_PORT_ID(id) ((id) >= PORT_PRE_OUT && (id) < PORT_ISP_OUT_MAX)
#define IS_VALID_DCAM_IMG_PORT(id) ((id) >= PORT_RAW_OUT && (id) < PORT_AEM_OUT)
#define IS_VALID_ISP_IMG_PORT(id) ((id) >= PORT_PRE_OUT && (id) < PORT_YUV_HIST_OUT)
#define PORT_ISP_MAX  (PORT_ISP_IN_MAX + PORT_ISP_OUT_MAX)

enum cam_port_dcam_online_in_id {
	PORT_ONLINE_SENSOR_IN,
	PORT_DCAM_ONLINE_IN_MAX,
};

enum cam_port_isp_outport_type {
	ISP_OUT,
	ISP_SCALER_OUT,
};

enum cam_port_dcam_online_out_id {
	PORT_RAW_OUT,
	PORT_BIN_OUT,
	PORT_FULL_OUT,
	PORT_AEM_OUT,
	PORT_AFM_OUT,
	PORT_AFL_OUT,
	PORT_PDAF_OUT,
	PORT_VCH2_OUT,
	PORT_VCH3_OUT,
	PORT_BAYER_HIST_OUT,
	PORT_FRGB_HIST_OUT,
	PORT_LSCM_OUT,
	PORT_GTM_HIST_OUT,
	PORT_DCAM_OUT_MAX,
};

enum cam_port_dcam_offline_in_id {
	PORT_DCAM_OFFLINE_IN,
	PORT_DCAM_OFFLINE_IN_MAX,
};

enum cam_port_dcam_offline_out_id {
	PORT_OFFLINE_RAW_OUT,
	PORT_OFFLINE_BIN_OUT,
	PORT_OFFLINE_FULL_OUT,
	PORT_OFFLINE_AEM_OUT,
	PORT_OFFLINE_AFM_OUT,
	PORT_OFFLINE_AFL_OUT,
	PORT_OFFLINE_PDAF_OUT,
	PORT_OFFLINE_BAYER_HIST_OUT,
	PORT_OFFLINE_FRGB_HIST_OUT,
	PORT_OFFLINE_LSCM_OUT,
	PORT_OFFLINE_GTM_HIST_OUT,
	PORT_DCAM_OFFLINE_OUT_MAX,
};

enum cam_port_isp_in_id {
	PORT_ISP_OFFLINE_IN,
	PORT_ISP_OFFLINE_COMPRESS_IN,
	PORT_ISP_IN_MAX,
};

enum cam_port_isp_out_id {
	PORT_PRE_OUT,
	PORT_VID_OUT,
	PORT_CAP_OUT,
	PORT_THUMB_OUT,
	PORT_YUV_HIST_OUT,
	PORT_LTM_HIST_OUT,
	PORT_GTM_HIST_ISP_OUT,
	PORT_NR3_IN,
	PORT_NR3_OUT,
	PORT_ISP_OUT_MAX,
};

enum cam_port_isp_yuv_scaler_in_id {
	PORT_PRE_ISP_YUV_SCALER_IN,
	PORT_CAP_ISP_YUV_SCALER_IN,
	PORT_ISP_YUV_SCALER_IN_MAX,
};

enum cam_port_isp_yuv_scaler_out_id {
	PORT_PRE_ISP_YUV_SCALER_OUT,
	PORT_VID_ISP_YUV_SCALER_OUT,
	PORT_CAP_ISP_YUV_SCALER_OUT,
	PORT_THUMB_ISP_YUV_SCALER_OUT,
	PORT_ISP_YUV_SCALER_OUT_MAX,
};

enum cam_port_cache_in_id {
	PORT_FRAME_CACHE_IN,
	PORT_FRAME_CACHE_IN_MAX,
};

enum cam_port_cache_out_id {
	PORT_FRAME_CACHE_OUT,
	PORT_FRAME_CACHE_OUT_MAX,
};

enum cam_port_rec_in_id {
	PORT_REC_CUR_IN,
	PORT_REC_PRE_IN,
	PORT_REC_IN_MAX,
};

enum cam_port_rec_out_id {
	PORT_REC_OUT,
	PORT_REC_OUT_MAX,
};

enum cam_port_dec_in_id {
	PORT_DEC_IN,
	PORT_DEC_IN_MAX,
};

enum cam_port_dec_out_id {
	PORT_DEC_OUT,
	PORT_DEC_OUT_MAX,
};

enum cam_port_copy_in_id {
	PORT_COPY_IN,
	PORT_COPY_IN_MAX,
};

enum cam_port_copy_out_id {
	PORT_COPY_OUT,
	PORT_COPY_OUT_MAX,
};

enum cam_port_user_in_id {
	PORT_USER_IN,
	PORT_USER_IN_MAX,
};

enum cam_port_buf_type {
	PORT_BUFFER_NORMAL,
	PORT_BUFFER_RESERVED,
	PORT_BUFFER_MAX,
};

enum cam_port_cfg_cmd {
	PORT_CFG_ZOOM_SET,
	PORT_CFG_BUFFER_SET,
	PORT_CFG_PARAM_GET,
	PORT_CFG_BUFFER_CYCLE,
	PORT_CFG_BUFFER_GET,
	PORT_CFG_BUFFER_ALLOC,
	PORT_CFG_BASE_SET,
	PORT_CFG_UFRAME_SET,
	PORT_CFG_RESBUF_SET,
	PORT_CFG_BUFFER_CLR,
	PORT_CFG_FMT_SET,
	PORT_CFG_FLASH_SKIP_FID,
	PORT_CFG_PRE_OUT_MODE,
	PORT_CFG_PARAM_MAX,
};

enum cam_port_link_state {
	PORT_LINK_IDLE,
	PORT_LINK_NORMAL,
	PORT_LINK_MAX,
};

enum cam_port_scaler_type {
	PORT_SCALER_BINNING = 0,
	PORT_SCALER_RAW_DOWNSISER,
	PORT_SCALER_BY_YUVSCALER,
	PORT_SCALER_BYPASS,
	PORT_SCALER_MAX,
};

enum export_type {
	PORT_MASTER = 0,
	PORT_SLAVE,
};

struct cam_port_shutoff_ctrl {
	atomic_t cap_cnt;
	uint32_t port_id;
	enum shutoff_scene shutoff_scene;
	enum shutoff_type shutoff_type;
	enum cam_en_status raw_switch_en;
	enum dcam_full_src_sel_type cur_raw_sel;
	enum dcam_full_src_sel_type pre_raw_sel;
};

struct cam_port_topology {
	enum cam_node_type node_type;
	enum cam_port_transfer_type transfer_type;
	enum cam_port_link_state link_state;
	uint32_t id;
	enum cam_en_status dump_en;
	uint32_t dump_node_id;
	uint32_t replace_en;
	uint32_t replace_node_id;
	enum cam_en_status copy_en;
	uint32_t copy_node_id;
	enum cam_en_status dynamic_link_en;
	enum cam_en_status to_user_en;
	enum export_type depend_type;
	struct cam_port_linkage link;
	struct cam_port_linkage switch_link;
	struct cam_port_linkage normal_link;
};

struct cam_port_desc {
	void *nodes_dev;
	void **port_dev;
	zoom_get_cb zoom_cb_func;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	void *zoom_cb_handle;
	shutoff_cb shutoff_cb_func;
	void *shutoff_cb_handle;
	struct dcam_online_port_desc *dcam_online;
	struct dcam_offline_port_desc *dcam_offline;
	struct dcam_offline_port_desc *dcam_offline_bpcraw;
	struct dcam_offline_port_desc *dcam_offline_lscraw;
	struct dcam_offline_port_desc *dcam_offline_raw2frgb;
	struct dcam_offline_port_desc *dcam_offline_frgb2yuv;
	struct pyr_dec_port_desc *pyr_dec;
	struct isp_port_desc *isp_offline;
	struct isp_scaler_port_desc *isp_offline_scaler;
	struct cam_port_topology *port_graph;
};

struct cam_port_ops {
	int (*cfg_param)(void *handle, enum cam_port_cfg_cmd cmd, void *param);
};

struct cam_port {
	void *handle;
	struct cam_port_topology *port_graph;
	struct cam_port_ops ops;
};

extern struct cam_buf_manager *global_buf_manager;
const char *camport_fmt_name_get(enum cam_format type);
const char *cam_port_name_get(enum cam_port_dcam_online_out_id port_id);
const char *cam_port_dcam_offline_out_id_name_get(enum cam_port_dcam_offline_out_id port_id);
int cam_port_static_portlist_get(struct cam_port_topology *param, uint32_t node_type, uint32_t transfer_type);
int cam_port_buffers_alloc(void *handle, uint32_t node_id,struct cam_buf_alloc_desc *param);
void *cam_port_creat(struct cam_port_desc *param, uint32_t node_id);
void cam_port_destory(struct cam_port *port);

#endif
