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

#ifndef _CAM_NODE_H_
#define _CAM_NODE_H_

#include "cam_copy_node.h"
#include "cam_dump_node.h"
#include "cam_port.h"
#include "cam_copy_node.h"
#include "dcam_online_node.h"
#include "dcam_fetch_node.h"
#include "isp_node.h"
#include "isp_scaler_node.h"
#include "frame_cache_node.h"
#include "dcam_offline_node.h"
#include "pyr_dec_node.h"
#include "cam_replace_node.h"

#define CAM_NODE_PORT_IN_NUM       PORT_DCAM_OUT_MAX
#define CAM_NODE_PORT_OUT_NUM      PORT_DCAM_OUT_MAX

#define CAM_NODE_SHUTOFF_PARAM_INIT(node_shutoff)  ({ \
	int i = 0, ret = 0; \
	for (i = 0; i < CAM_NODE_PORT_IN_NUM; i++) { \
		atomic_set(&(node_shutoff).inport_shutoff[i].cap_cnt, 0); \
		(node_shutoff).inport_shutoff[i].port_id = CAM_NODE_PORT_IN_NUM; \
		(node_shutoff).inport_shutoff[i].shutoff_scene = SHUTOFF_SCENE_MAX; \
		(node_shutoff).inport_shutoff[i].shutoff_type = SHUTOFF_TYPE_MAX; \
		(node_shutoff).inport_shutoff[i].raw_switch_en = CAM_DISABLE; \
		(node_shutoff).inport_shutoff[i].pre_raw_sel = MAX_RAW_SRC_SEL; \
		(node_shutoff).inport_shutoff[i].cur_raw_sel = MAX_RAW_SRC_SEL; \
	} \
	for (i = 0; i < CAM_NODE_PORT_OUT_NUM; i++) { \
		atomic_set(&(node_shutoff).outport_shutoff[i].cap_cnt, 0); \
		(node_shutoff).outport_shutoff[i].port_id = CAM_NODE_PORT_OUT_NUM; \
		(node_shutoff).outport_shutoff[i].shutoff_scene = SHUTOFF_SCENE_MAX; \
		(node_shutoff).outport_shutoff[i].shutoff_type = SHUTOFF_TYPE_MAX; \
		(node_shutoff).outport_shutoff[i].raw_switch_en = CAM_DISABLE; \
		(node_shutoff).outport_shutoff[i].pre_raw_sel = MAX_RAW_SRC_SEL; \
		(node_shutoff).outport_shutoff[i].cur_raw_sel = MAX_RAW_SRC_SEL; \
	} \
	ret; \
})

enum cam_node_state {
	CAM_NODE_STATE_IDLE,
	CAM_NODE_STATE_WORK,
	CAM_NODE_STATE_MAX,
};

enum cam_node_cfg_cmd {
	CAM_NODE_CFG_BUF,
	CAM_NODE_CFG_CAP_PARAM,
	CAM_NODE_CFG_FMT,
	CAM_NODE_CFG_ZOOM,
	CAM_NODE_CFG_BASE,
	CAM_NODE_CFG_CLR_CACHE_BUF,
	CAM_NODE_CFG_DUAL_SYNC_BUF_GET,
	CAM_NODE_CFG_BLK_PARAM,
	CAM_NODE_CFG_UFRAME,
	CAM_NODE_CFG_INSERT_PORT,
	CAM_NODE_CFG_STATIS,
	CAM_NODE_CFG_RESERVE_BUF,
	CAM_NODE_CFG_SHARE_BUF,
	CAM_NODE_CFG_RECT_GET,
	CAM_NODE_CFG_BLK_GTM_LTM,
	CAM_NODE_CFG_RESET,
	CAM_NODE_CFG_XTM_EN,
	CAM_NODE_CFG_RECYCLE_BLK_PARAM,
	CAM_NODE_CFG_BUF_CLR,
	/*TEMP:for cfg isp cur_ctx_id to pyrdecnode*/
	CAM_NODE_CFG_CTXID,
	CAM_NODE_CFG_3DNR_MODE,
	CAM_NODE_CFG_GTM,
	CAM_NODE_CFG_PARAM_SWITCH,
	CAM_NODE_CFG_POSTPROC_PARAM,
	CAM_NODE_CFG_PARAM_Q_CLEAR,
	CAM_NODE_CFG_FAST_STOP,
	CAM_NODE_CFG_PARAM_FID,
	CAM_NODE_CFG_RESET_PARAM_PTR,
	CAM_NODE_CFG_PRE_RAW_FLAG,
	CAM_NODE_CFG_OPT_SCENE_SWITCH,
	CAM_NODE_CFG_ICAP_SCENE_SWITCH,
	CAM_NODE_CFG_ICAP_RAW_REPORT_CONDITION,
	CAM_NODE_CFG_GET_CAP_FRAME,
	CAM_NODE_CFG_PRE_FRAME_STATUS,
	CAM_NODE_CFG_PRE_OUT_MODE,
	CAM_NODE_CFG_FLASH_SKIP_FID,
	CAM_NODE_CFG_BUF_NUM,
	CAM_NODE_CFG_FETCH_BUF,
	CAM_NODE_CFG_FETCH_PARAM,
	CAM_NODE_CFG_PORT_RAW_SEL,
	CAM_NODE_CFG_ALTER_BUF,
	CAM_NODE_CFG_ALTER_BUF_CLR,
	CAM_NODE_CFG_OFFLINE_ICAP_DUMP_EN,
	CAM_NODE_CFG_FETCH_ONLINE_EN,
	CAM_NODE_CFG_SHUTOFF_PORT_CNT_DEC,
	CAM_NODE_CFG_FID_GET,
	CAM_NODE_CFG_CLEAR_FAST_STOP,
	CAM_NODE_CFG_MAX,
};

enum cam_node_status_rd_cmd {
	CAM_NODE_STATUS_OUT_FRM_PARAM,
	CAM_NODE_STATUS_CMD_MAX,
};

enum cam_node_shutoff_cmd {
	CAM_NODE_SHUTOFF_INIT,
	CAM_NODE_SHUTOFF_CONFIG,
	CAM_NODE_SHUTOFF_RECONFIG,
	CAM_NODE_SHUTOFF_TYPE_MAX,
};

struct cam_node_cfg_param {
	enum cam_port_transfer_type port_type;
	uint32_t port_id;
	void *param;
};

struct cam_node_shutoff_ctrl {
	enum cam_node_shutoff_cmd cmd;
	struct cam_port_shutoff_ctrl inport_shutoff[CAM_NODE_PORT_IN_NUM];
	struct cam_port_shutoff_ctrl outport_shutoff[CAM_NODE_PORT_OUT_NUM];
};

/**
 * struct cam_node_topology - The topology info description of node
 *
 * @type: The type of the node.
 * @id: The id of the node.
 * @inport: For input port topology description.
 * @outport: For output port topology description.
 *
 * A topology info is used to descript a hw node. It always contains the port
 * info, specific node type & node id, and the link info between different nodes
 * is descripted in @cam_port_topology. Thease can be used to creat the base framework
 * of pipeline/node/port structure.
 */
struct cam_node_topology {
	enum cam_node_type type;
	enum cam_node_state state;
	enum cam_en_status dump_en;
	uint32_t dump_node_id;
	uint32_t id;
	enum cam_node_buf_type buf_type;
	uint32_t replace_en;
	uint32_t replace_node_id;
	struct cam_port_topology inport[CAM_NODE_PORT_IN_NUM];
	struct cam_port_topology outport[CAM_NODE_PORT_OUT_NUM];
};

/* the node necessary info for creat a new node */
struct cam_node_desc {
	void *nodes_dev;
	zoom_get_cb zoom_cb_func;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	enum cam_pipeline_type type;
	struct dcam_online_node_desc *dcam_online_desc;
	struct dcam_offline_node_desc *dcam_offline_desc;
	struct dcam_offline_node_desc *dcam_offline_bpcraw_desc;
	struct dcam_offline_node_desc *dcam_offline_lscraw_desc;
	struct dcam_offline_node_desc *dcam_offline_raw2frgb_desc;
	struct dcam_offline_node_desc *dcam_offline_frgb2yuv_desc;
	struct dcam_fetch_node_desc *dcam_fetch_desc;
	struct isp_node_desc *isp_node_description;
	struct isp_yuv_scaler_node_desc *isp_yuv_scaler_desc;
	struct frame_cache_node_desc *frame_cache_desc;
	struct pyr_dec_node_desc *pyr_dec_desc;
	struct cam_copy_node_desc *cam_copy_desc;
	struct cam_node_topology *node_graph;
};

struct cam_node_ops {
	int (*cfg_node_param)(void *handle, enum cam_node_cfg_cmd cmd, void *param);
	int (*cfg_port_param)(void *handle, enum cam_port_cfg_cmd cmd, void *param);
	int (*request_proc)(void *handle, void *param);
	int (*stop_proc)(void *handle, void *param);
	int (*outbuf_back)(void *handle, void *param);
	int (*set_shutoff)(void *handle, void *param, uint32_t port_id);
	int (*cfg_shutoff)(void *handle, uint32_t cmd, void *param);
};

/* the main node struct for software */
struct cam_node {
	struct cam_node_topology *node_graph;
	void *handle;
	zoom_get_cb zoom_cb_func;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	struct cam_port *inport_list[CAM_NODE_PORT_IN_NUM];
	struct cam_port *outport_list[CAM_NODE_PORT_OUT_NUM];
	struct cam_node_ops ops;

	struct cam_capture_param cap_param;
	struct cam_node_shutoff_ctrl node_shutoff;

	enum cam_en_status need_fetch;
};

/* the global all main node/port index for different
 * pipeline share one node/port in a same session.
 */
struct cam_nodes_dev {
	struct dcam_online_node *dcam_online_node_dev;
	struct dcam_fetch_node *dcam_fetch_node_dev;
	struct dcam_online_port *dcam_online_out_port_dev[PORT_DCAM_OUT_MAX];
	struct dcam_offline_node *dcam_offline_node_dev;
	struct dcam_offline_port *dcam_offline_out_port_dev[PORT_DCAM_OUT_MAX];
	struct dcam_offline_node *dcam_offline_node_bpcraw_dev;
	struct dcam_offline_port *dcam_offline_bpcraw_out_port_dev[PORT_DCAM_OUT_MAX];
	struct dcam_offline_node *dcam_offline_node_lscraw_dev;
	struct dcam_offline_port *dcam_offline_lscraw_out_port_dev[PORT_DCAM_OUT_MAX];
	struct dcam_offline_node *dcam_offline_node_raw2frgb_dev;
	struct dcam_offline_port *dcam_offline_raw2frgb_out_port_dev[PORT_DCAM_OUT_MAX];
	struct dcam_offline_node *dcam_offline_node_frgb2yuv_dev;
	struct dcam_offline_port *dcam_offline_frgb2yuv_out_port_dev[PORT_DCAM_OUT_MAX];
	struct isp_node *isp_node_dev[ISP_NODE_MODE_MAX_ID];
	struct isp_port *isp_out_port_dev[ISP_NODE_MODE_MAX_ID][PORT_ISP_OUT_MAX];
	struct isp_port *isp_in_port_dev[ISP_NODE_MODE_MAX_ID][PORT_ISP_IN_MAX];
	struct isp_yuv_scaler_node *isp_yuv_scaler_node_dev[ISP_YUV_SCALER_MAX_NODE_ID];
	struct isp_scaler_port *isp_scaler_in_port_dev[ISP_YUV_SCALER_MAX_NODE_ID][PORT_ISP_YUV_SCALER_IN_MAX];
	struct isp_scaler_port *isp_scaler_out_port_dev[ISP_YUV_SCALER_MAX_NODE_ID][PORT_ISP_YUV_SCALER_OUT_MAX];
	struct pyr_dec_port *pyr_dec_out_port_dev[PYR_DEC_MAX_NODE_ID][PORT_DEC_OUT_MAX];
	struct pyr_dec_node *pyr_dec_node_dev[PYR_DEC_MAX_NODE_ID];
};

const char *cam_node_name_get(enum cam_node_type type);
int cam_node_work_state_get(struct cam_node_topology *param, uint32_t g_dump_en, uint32_t *g_replace_en);
int cam_node_buffer_alloc(void *handle, struct cam_buf_alloc_desc *param);
int cam_node_static_nodelist_get(struct cam_node_topology *param, uint32_t type);
void *cam_node_creat(struct cam_node_desc *param);
void cam_node_close(struct cam_node *node);
void cam_node_destory(struct cam_node *node);

#endif
