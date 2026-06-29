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

#include <linux/vmalloc.h>
#include <linux/sched.h>

#include "cam_node.h"
#include "cam_port.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_PORT: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define IS_CAM_FMT(type) ((type) < CAM_FORMAT_MAX)

static const char *CAMPORT_FMT_NAMES[CAM_FORMAT_MAX] = {
	[CAM_RAW_BASE] = "CAM_RAW_BASE",
	[CAM_RAW_PACK_10] = "CAM_RAW_PACK_10",
	[CAM_RAW_HALFWORD_10] = "CAM_RAW_HALFWORD_10",
	[CAM_RAW_14] = "CAM_RAW_14",
	[CAM_RAW_8] = "CAM_RAW_8",
	[CAM_RGB_BASE] = "CAM_RGB_BASE",
	[CAM_FULL_RGB10] = "CAM_FULL_RGB10",
	[CAM_FULL_RGB14] = "CAM_FULL_RGB14",
	[CAM_YUV_BASE] = "CAM_YUV_BASE",
	[CAM_YUV422_2FRAME] = "CAM_YUV422_2FRAME",
	[CAM_YVU422_2FRAME] = "CAM_YVU422_2FRAME",
	[CAM_YUV420_2FRAME] = "CAM_YUV420_2FRAME",
	[CAM_YVU420_2FRAME] = "CAM_YVU420_2FRAME",
	[CAM_YUV420_2FRAME_10] = "CAM_YUV420_2FRAME_10",
	[CAM_YVU420_2FRAME_10] = "CAM_YVU420_2FRAME_10",
	[CAM_YUV420_2FRAME_MIPI] = "CAM_YUV420_2FRAME_MIPI",
	[CAM_YVU420_2FRAME_MIPI] = "CAM_YVU420_2FRAME_MIPI",
	[CAM_YUV422_3FRAME] = "CAM_YUV422_3FRAME",
	[CAM_YUV420_3FRAME] = "CAM_YUV420_3FRAME",
	[CAM_YUYV_1FRAME] = "CAM_YUYV_1FRAME",
	[CAM_UYVY_1FRAME] = "CAM_UYVY_1FRAME",
	[CAM_YVYU_1FRAME] = "CAM_YVYU_1FRAME",
	[CAM_VYUY_1FRAME] = "CAM_VYUY_1FRAME",
};

const char *camport_fmt_name_get(enum cam_format type)
{
	return IS_CAM_FMT(type) ? CAMPORT_FMT_NAMES[type] : "(null)";
}

static const char *_DCAM_PORT_NAMES[PORT_DCAM_OUT_MAX] = {
	[PORT_RAW_OUT] = "RAW",
	[PORT_BIN_OUT] = "BIN",
	[PORT_FULL_OUT] = "FULL",
	[PORT_AEM_OUT] = "AEM",
	[PORT_AFM_OUT] = "AFM",
	[PORT_AFL_OUT] = "AFL",
	[PORT_PDAF_OUT] = "PDAF",
	[PORT_VCH2_OUT] = "VCH2",
	[PORT_VCH3_OUT] = "VCH3",
	[PORT_BAYER_HIST_OUT] = "BAYER_HIST",
	[PORT_FRGB_HIST_OUT] = "FRGB_HIST",
	[PORT_LSCM_OUT] = "LSCM",
	[PORT_GTM_HIST_OUT] = "GTMHIST",
};

const char *cam_port_name_get(enum cam_port_dcam_online_out_id port_id)
{
	return IS_VALID_DCAM_PORT_ID(port_id) ? _DCAM_PORT_NAMES[port_id] : "(null)";
}

static const char *_DCAM_PORT_DCAM_OFFLINE_OUT_NAMES[PORT_DCAM_OFFLINE_OUT_MAX] = {
	[PORT_OFFLINE_RAW_OUT] = "PORT_OFFLINE_RAW_OUT",
	[PORT_OFFLINE_BIN_OUT] = "PORT_OFFLINE_BIN_OUT",
	[PORT_OFFLINE_FULL_OUT] = "PORT_OFFLINE_FULL_OUT",
	[PORT_OFFLINE_AEM_OUT] = "PORT_OFFLINE_AEM_OUT",
	[PORT_OFFLINE_AFM_OUT] = "PORT_OFFLINE_AFM_OUT",
	[PORT_OFFLINE_AFL_OUT] = "PORT_OFFLINE_AFL_OUT",
	[PORT_OFFLINE_PDAF_OUT] = "PORT_OFFLINE_PDAF_OUT",
	[PORT_OFFLINE_BAYER_HIST_OUT] = "PORT_OFFLINE_BAYER_HIST_OUT",
	[PORT_OFFLINE_FRGB_HIST_OUT] = "PORT_OFFLINE_FRGB_HIST_OUT",
	[PORT_OFFLINE_LSCM_OUT] = "PORT_OFFLINE_LSCM_OUT",
	[PORT_OFFLINE_GTM_HIST_OUT] = "PORT_OFFLINE_GTMHIST_OUT",
};

const char *cam_port_dcam_offline_out_id_name_get(enum cam_port_dcam_offline_out_id port_id)
{
	return _DCAM_PORT_DCAM_OFFLINE_OUT_NAMES[port_id];
}

static int camport_link_init(struct cam_port_linkage *linkage)
{
	int ret = 0;

	if (!linkage) {
		pr_err("fail to get invalid param ptr\n");
		return -EFAULT;
	}

	linkage->node_type = CAM_NODE_TYPE_MAX;
	linkage->node_id = CAM_LINK_DEFAULT_NODE_ID;
	linkage->port_id = CAM_LINK_DEFAULT_PORT_ID;

	return ret;
}

static void camport_dcamonline_port_get(struct cam_port_topology *port,
		uint32_t node_type, uint32_t transfer_type)
{
	uint32_t i = 0;

	if (transfer_type == PORT_TRANSFER_IN) {
		port->node_type = node_type;
		port->transfer_type = transfer_type;
		port->id = PORT_ONLINE_SENSOR_IN;
		port->dynamic_link_en = 0;
		camport_link_init(&port->link);
		camport_link_init(&port->switch_link);
	} else {
		for (i = 0; i < PORT_DCAM_OUT_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	}
}

static void camport_dcamoffline_port_get(struct cam_port_topology *port,
		uint32_t node_type, uint32_t transfer_type)
{
	uint32_t i = 0;

	if (transfer_type == PORT_TRANSFER_IN) {
		port->node_type = node_type;
		port->transfer_type = transfer_type;
		port->id = PORT_DCAM_OFFLINE_IN;
		port->dynamic_link_en = 0;
		camport_link_init(&port->link);
		camport_link_init(&port->switch_link);
	} else {
		for (i = 0; i < PORT_DCAM_OFFLINE_OUT_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	}
}

static void camport_ispoffline_port_get(struct cam_port_topology *port,
		uint32_t node_type, uint32_t transfer_type)
{
	uint32_t i = 0;

	if (transfer_type == PORT_TRANSFER_IN) {
		for (i = 0; i < PORT_ISP_IN_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	} else {
		for (i = 0; i < PORT_ISP_OUT_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	}
}

static void camport_framecache_port_get(struct cam_port_topology *port,
		uint32_t node_type, uint32_t transfer_type)
{
	uint32_t i = 0;

	if (transfer_type == PORT_TRANSFER_IN) {
		port->node_type = node_type;
		port->transfer_type = transfer_type;
		port->id = PORT_FRAME_CACHE_IN;
		port->dynamic_link_en = 0;
		camport_link_init(&port->link);
		camport_link_init(&port->switch_link);
	} else {
		for (i = 0; i < PORT_FRAME_CACHE_OUT_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	}
}

static void camport_ispyuvscaler_port_get(struct cam_port_topology *port,
		uint32_t node_type, uint32_t transfer_type)
{
	int i = 0;

	if (transfer_type == PORT_TRANSFER_IN) {
		for (i = 0; i < PORT_ISP_YUV_SCALER_IN_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	} else {
		for (i = 0; i < PORT_ISP_YUV_SCALER_OUT_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	}

}

static void camport_pyrdec_port_get(struct cam_port_topology *port,
		uint32_t node_type, uint32_t transfer_type)
{
	int i = 0;

	if (transfer_type == PORT_TRANSFER_IN) {
		for (i = 0; i < PORT_DEC_IN_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	} else {
		for (i = 0; i < PORT_DEC_OUT_MAX; i++, port++) {
			port->node_type = node_type;
			port->transfer_type = transfer_type;
			port->id = i;
			port->dynamic_link_en = 0;
			camport_link_init(&port->link);
			camport_link_init(&port->switch_link);
		}
	}

}

static int camport_cfg_param(void *handle, enum cam_port_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_port *port = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	port = (struct cam_port *)handle;
	switch (port->port_graph->node_type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		ret = dcam_online_port_param_cfg(port->handle, cmd, param);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		ret = dcam_offline_port_param_cfg(port->handle, cmd, param);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		ret = isp_port_param_cfg(port->handle, cmd, param);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		ret = pyr_dec_port_param_cfg(port->handle, cmd, param);
		break;
	default:
		pr_err("fail to support node type %s\n", cam_node_name_get(port->port_graph->node_type));
		ret = -EFAULT;
		break;
	}

	return ret;
}

int cam_port_static_portlist_get(struct cam_port_topology *param,
	uint32_t node_type, uint32_t transfer_type)
{
	int ret = 0;

	if (!param) {
		pr_err("fail to get invalid param ptr\n");
		return -EFAULT;
	}

	switch (node_type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		camport_dcamonline_port_get(param, node_type, transfer_type);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		camport_dcamoffline_port_get(param, node_type, transfer_type);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		camport_ispoffline_port_get(param, node_type, transfer_type);
		break;
	case CAM_NODE_TYPE_ISP_YUV_SCALER:
		camport_ispyuvscaler_port_get(param, node_type, transfer_type);
		break;
	case CAM_NODE_TYPE_FRAME_CACHE:
		camport_framecache_port_get(param, node_type, transfer_type);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		camport_pyrdec_port_get(param, node_type, transfer_type);
		break;
	/* These nodes no need to use port now */
	case CAM_NODE_TYPE_DATA_COPY:
	case CAM_NODE_TYPE_DUMP:
	case CAM_NODE_TYPE_REPLACE:
		break;
	default:
		pr_err("fail to support node type %s\n", cam_node_name_get(node_type));
		ret = -EFAULT;
		break;
	}

	return ret;
}

int cam_port_buffers_alloc(void *handle, uint32_t node_id, struct cam_buf_alloc_desc *param)
{
	int ret = 0;
	struct cam_port *port = (struct cam_port *)handle;

	if (!handle) {
		pr_err("fail to get port handle\n");
		return -EFAULT;
	}

	if (port->port_graph->link_state != PORT_LINK_NORMAL)
		return ret;
	if ((port->port_graph->link.node_type == CAM_NODE_TYPE_USER) ||
		(port->port_graph->link.node_id == CAM_LINK_DEFAULT_NODE_ID))
		return ret;

	if ((port->port_graph->switch_link.node_type == CAM_NODE_TYPE_USER) &&
		(node_id == port->port_graph->link.node_id))
		return ret;
	switch (port->port_graph->node_type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		ret = dcam_online_port_buf_alloc(port->handle, param);
		if (ret)
			pr_err("fail to alloc port buf %s\n", cam_node_name_get(port->port_graph->node_type));
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
		ret = dcam_offline_port_buf_alloc(port->handle, param);
		if (ret)
			pr_err("fail to alloc port buf %s\n", cam_node_name_get(port->port_graph->node_type));
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
		ret = dcam_offline_lsc_raw_port_buf_alloc(port->handle, param);
		if (ret)
			pr_err("fail to alloc port buf %s\n", cam_node_name_get(port->port_graph->node_type));
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		ret = pyr_dec_port_buf_alloc(port->handle, param);
		if (ret)
			pr_err("fail to alloc port buf %s\n", cam_node_name_get(port->port_graph->node_type));
		break;
	default:
		pr_debug("not support node %s\n", cam_node_name_get(port->port_graph->node_type));
		return ret;
	}
	return ret;
}

void *cam_port_creat(struct cam_port_desc *param, uint32_t node_id)
{
	struct cam_port *port = NULL;
	struct cam_nodes_dev *nodes_dev = NULL;

	if (!param) {
		pr_err("fail to get invalid nodedesc info\n");
		return NULL;
	}

	port = cam_buf_kernel_sys_vzalloc(sizeof(struct cam_port));
	if (!port) {
		pr_err("fail to alloc cam port\n");
		return NULL;
	}

	port->port_graph = param->port_graph;
	if (!port->port_graph) {
		pr_info("port->port_graph is NULL\n");
		cam_buf_kernel_sys_vfree(port);
		return NULL;
	}

	nodes_dev = (struct cam_nodes_dev *)param->nodes_dev;
	pr_debug("port id %d nodes_dev %p\n", port->port_graph->id, nodes_dev);
	if (!nodes_dev) {
		pr_info("nodes_dev is NULL\n");
		cam_buf_kernel_sys_vfree(port);
		return NULL;
	}

	port->ops.cfg_param = camport_cfg_param;
	switch (port->port_graph->node_type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		param->dcam_online->port_dev = (void *)&nodes_dev->dcam_online_out_port_dev[port->port_graph->id];
		param->dcam_online->zoom_cb_func = param->zoom_cb_func;
		param->dcam_online->zoom_cb_handle = param->zoom_cb_handle;
		param->dcam_online->data_cb_func = param->data_cb_func;
		param->dcam_online->data_cb_handle = param->data_cb_handle;
		param->dcam_online->buf_manager_handle = param->buf_manager_handle;
		param->dcam_online->shutoff_cb_func = param->shutoff_cb_func;
		param->dcam_online->shutoff_cb_handle = param->shutoff_cb_handle;
		port->handle = dcam_online_port_get(port->port_graph->id, param->dcam_online);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
		param->dcam_offline->port_dev = (void *)&nodes_dev->dcam_offline_out_port_dev[port->port_graph->id];
		param->dcam_offline->zoom_cb_func = param->zoom_cb_func;
		param->dcam_offline->zoom_cb_handle = param->zoom_cb_handle;
		param->dcam_offline->data_cb_func = param->data_cb_func;
		param->dcam_offline->data_cb_handle = param->data_cb_handle;
		param->dcam_offline->buf_manager_handle = param->buf_manager_handle;
		param->dcam_offline->transfer_type = param->port_graph->transfer_type;
		port->handle = dcam_offline_port_get(port->port_graph->id, param->dcam_offline);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
		param->dcam_offline_bpcraw->port_dev = (void *)&nodes_dev->dcam_offline_bpcraw_out_port_dev[port->port_graph->id];
		param->dcam_offline_bpcraw->zoom_cb_func = param->zoom_cb_func;
		param->dcam_offline_bpcraw->zoom_cb_handle = param->zoom_cb_handle;
		param->dcam_offline_bpcraw->data_cb_func = param->data_cb_func;
		param->dcam_offline_bpcraw->data_cb_handle = param->data_cb_handle;
		param->dcam_offline_bpcraw->buf_manager_handle = param->buf_manager_handle;
		param->dcam_offline_bpcraw->transfer_type = param->port_graph->transfer_type;
		port->handle = dcam_offline_port_get(port->port_graph->id, param->dcam_offline_bpcraw);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
		param->dcam_offline_lscraw->port_dev = (void *)&nodes_dev->dcam_offline_lscraw_out_port_dev[port->port_graph->id];
		param->dcam_offline_lscraw->zoom_cb_func = param->zoom_cb_func;
		param->dcam_offline_lscraw->zoom_cb_handle = param->zoom_cb_handle;
		param->dcam_offline_lscraw->data_cb_func = param->data_cb_func;
		param->dcam_offline_lscraw->data_cb_handle = param->data_cb_handle;
		param->dcam_offline_lscraw->buf_manager_handle = param->buf_manager_handle;
		param->dcam_offline_lscraw->transfer_type = param->port_graph->transfer_type;
		port->handle = dcam_offline_port_get(port->port_graph->id, param->dcam_offline_lscraw);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
		param->dcam_offline_raw2frgb->port_dev = (void *)&nodes_dev->dcam_offline_raw2frgb_out_port_dev[port->port_graph->id];
		param->dcam_offline_raw2frgb->zoom_cb_func = param->zoom_cb_func;
		param->dcam_offline_raw2frgb->zoom_cb_handle = param->zoom_cb_handle;
		param->dcam_offline_raw2frgb->data_cb_func = param->data_cb_func;
		param->dcam_offline_raw2frgb->data_cb_handle = param->data_cb_handle;
		param->dcam_offline_raw2frgb->buf_manager_handle = param->buf_manager_handle;
		param->dcam_offline_raw2frgb->transfer_type = param->port_graph->transfer_type;
		port->handle = dcam_offline_port_get(port->port_graph->id, param->dcam_offline_raw2frgb);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		param->dcam_offline_frgb2yuv->port_dev = (void *)&nodes_dev->dcam_offline_frgb2yuv_out_port_dev[port->port_graph->id];
		param->dcam_offline_frgb2yuv->zoom_cb_func = param->zoom_cb_func;
		param->dcam_offline_frgb2yuv->zoom_cb_handle = param->zoom_cb_handle;
		param->dcam_offline_frgb2yuv->data_cb_func = param->data_cb_func;
		param->dcam_offline_frgb2yuv->data_cb_handle = param->data_cb_handle;
		param->dcam_offline_frgb2yuv->buf_manager_handle = param->buf_manager_handle;
		param->dcam_offline_frgb2yuv->transfer_type = param->port_graph->transfer_type;
		port->handle = dcam_offline_port_get(port->port_graph->id, param->dcam_offline_frgb2yuv);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		if (param->port_graph->transfer_type == PORT_TRANSFER_IN)
			param->isp_offline->port_dev = (void *)&nodes_dev->isp_in_port_dev[node_id][port->port_graph->id];
		else
			param->isp_offline->port_dev = (void *)&nodes_dev->isp_out_port_dev[node_id][port->port_graph->id];
		param->isp_offline->data_cb_func = param->data_cb_func;
		param->isp_offline->data_cb_handle = param->data_cb_handle;
		param->isp_offline->buf_manager_handle = param->buf_manager_handle;
		param->isp_offline->transfer_type = param->port_graph->transfer_type;
		param->isp_offline->depend_type = param->port_graph->depend_type;
		port->handle = isp_port_get(port->port_graph->id, param->isp_offline);
		break;
	case CAM_NODE_TYPE_ISP_YUV_SCALER:
		if (param->port_graph->transfer_type == PORT_TRANSFER_IN)
			param->isp_offline_scaler->port_dev = (void *)&nodes_dev->isp_scaler_in_port_dev[node_id][port->port_graph->id];
		else
			param->isp_offline_scaler->port_dev = (void *)&nodes_dev->isp_scaler_out_port_dev[node_id][port->port_graph->id];
		param->isp_offline_scaler->data_cb_func = param->data_cb_func;
		param->isp_offline_scaler->data_cb_handle = param->data_cb_handle;
		param->isp_offline_scaler->buf_manager_handle = param->buf_manager_handle;
		param->isp_offline_scaler->transfer_type = param->port_graph->transfer_type;
		port->handle = isp_scaler_port_get(port->port_graph->id, param->isp_offline_scaler);
		break;
	case CAM_NODE_TYPE_FRAME_CACHE:
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		param->pyr_dec->port_dev = (void *)&nodes_dev->pyr_dec_out_port_dev[node_id][port->port_graph->id];
		param->pyr_dec->data_cb_func = param->data_cb_func;
		param->pyr_dec->data_cb_handle = param->data_cb_handle;
		param->pyr_dec->buf_manager_handle = param->buf_manager_handle;
		port->handle = pyr_dec_port_get(port->port_graph->id, param->pyr_dec);
		break;
	case CAM_NODE_TYPE_DATA_COPY:
		break;
	default:
		pr_err("fail to support node type %s port id %d\n",
			cam_node_name_get(port->port_graph->node_type), port->port_graph->id);
		break;
	}

	pr_info("node %s port %p handle %p\n",
		cam_node_name_get(port->port_graph->node_type), port, port->handle);
	return port;
}

void cam_port_destory(struct cam_port *port)
{
	if (!port) {
		pr_err("fail to get valid port\n");
		return;
	}

	switch (port->port_graph->node_type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		dcam_online_port_put(port->handle);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		dcam_offline_port_put(port->handle);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		isp_port_put(port->handle);
		break;
	case CAM_NODE_TYPE_ISP_YUV_SCALER:
		isp_scaler_port_put(port->handle);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		pyr_dec_port_put(port->handle);
		break;
	case CAM_NODE_TYPE_FRAME_CACHE:
		break;
	default:
		pr_err("fail to support node type %s port id %d\n",
			cam_node_name_get(port->port_graph->node_type), port->port_graph->id);
		break;
	}

	pr_info("node type %s port %s free success\n", cam_node_name_get(port->port_graph->node_type),
			cam_port_name_get(port->port_graph->id));
	cam_buf_kernel_sys_vfree(port);
	port = NULL;
}
