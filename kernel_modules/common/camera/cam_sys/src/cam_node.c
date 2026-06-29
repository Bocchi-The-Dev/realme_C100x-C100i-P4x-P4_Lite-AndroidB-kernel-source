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

#include "cam_copy_node.h"
#include "cam_dump_node.h"
#include "cam_node.h"
#include "cam_replace_node.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define DCAM_ONLINE_NODE_NUM      4
#define DCAM_OFFLINE_NODE_NUM     4
#define ISP_OFFLINE_NODE_NUM      8
#define IS_CAM_NODE(type) ((type) < CAM_NODE_TYPE_MAX)
#define IS_VALID_PORT_ID(id) (id < CAM_NODE_PORT_OUT_NUM)

static const char *CAM_NODE_NAMES[CAM_NODE_TYPE_MAX] = {
	[CAM_NODE_TYPE_DCAM_ONLINE] = "CAM_NODE_TYPE_DCAM_ONLINE",
	[CAM_NODE_TYPE_DCAM_OFFLINE] = "CAM_NODE_TYPE_DCAM_OFFLINE",
	[CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW] = "CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW",
	[CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW] = "CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW",
	[CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB] = "CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB",
	[CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV] = "CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV",
	[CAM_NODE_TYPE_ISP_OFFLINE] = "CAM_NODE_TYPE_ISP_OFFLINE",
	[CAM_NODE_TYPE_FRAME_CACHE] = "CAM_NODE_TYPE_FRAME_CACHE",
	[CAM_NODE_TYPE_ISP_YUV_SCALER] = "CAM_NODE_TYPE_ISP_YUV_SCALER",
	[CAM_NODE_TYPE_PYR_DEC] = "CAM_NODE_TYPE_PYR_DEC",
	[CAM_NODE_TYPE_PYR_REC] = "CAM_NODE_TYPE_PYR_REC",
	[CAM_NODE_TYPE_DUMP] = "CAM_NODE_TYPE_DUMP",
	[CAM_NODE_TYPE_USER] = "CAM_NODE_TYPE_USER",
	[CAM_NODE_TYPE_REPLACE] = "CAM_NODE_TYPE_REPLACE",
	[CAM_NODE_TYPE_DATA_COPY] = "CAM_NODE_TYPE_DATA_COPY",
};

static enum cam_en_status camnode_capture_skip_condition(struct cam_frame *pframe, struct cam_capture_param *cap_param)
{
	if ((cap_param->cap_scene == CAPTURE_COMMON ||
		cap_param->cap_scene == CAPTURE_MFSR ||
		cap_param->cap_scene == CAPTURE_SW3DNR ||
		cap_param->cap_scene == CAPTURE_HDR) &&
		!cap_param->no_need_skip_frame_scene &&
		(cap_param->cap_user_crop.w != pframe->common.width ||
		cap_param->cap_user_crop.h != pframe->common.height)) {
		pr_warn("warning: cap type[%d], fid %d skip, frame width %d %d, latest user crop %d %d, cap cnt %d\n",
			cap_param->cap_scene, pframe->common.fid, pframe->common.width, pframe->common.height,
			cap_param->cap_user_crop.w, cap_param->cap_user_crop.h, atomic_read(&cap_param->cap_cnt));
		if (cap_param->offline_icap_scene)
			return CAM_ENABLE;
		return CAM_DISABLE;
	}

	if (cap_param->skip_first_num && pframe->common.fid < 1)
		return CAM_DISABLE;

	return CAM_ENABLE;
}

static enum cam_en_status camnode_single_capture_switch_link_en(struct cam_frame *pframe,
	struct cam_capture_param *cap_param, struct frame_cache_node *frame_cache_node, uint32_t node_type)
{
	enum cam_en_status link_en = CAM_DISABLE;

	if (cap_param->fid && pframe->common.fid >= cap_param->fid)
		link_en |= CAM_ENABLE;

	if (!cap_param->fid && pframe->common.boot_sensor_time >= cap_param->cap_timestamp
		&& camnode_capture_skip_condition(pframe, cap_param))
		link_en |= CAM_ENABLE;

	if (cap_param->offline_icap_scene && cap_param->fid
		&& pframe->common.fid >= cap_param->fid)
		link_en |= CAM_ENABLE;

	if (cap_param->cap_opt_frame_scene)
		link_en |= CAM_ENABLE;

	if (node_type == CAM_NODE_TYPE_FRAME_CACHE
		&& frame_cache_node->cap_param.frm_sel_mode == CAM_NODE_FRAME_NO_SEL)
		link_en |= CAM_ENABLE;

	return link_en;
}

static enum cam_en_status camnode_rawalg_scene_skip(struct cam_frame *pframe,
	struct cam_capture_param *cap_param, uint32_t node_type)
{
	if ((cap_param->cap_scene == CAPTURE_AINR || cap_param->cap_scene == CAPTURE_RAWALG) &&
		pframe->common.buf.mfd == 0) {
		pr_debug("cam scene %d, mfd:0x%x.\n", cap_param->cap_scene, pframe->common.buf.mfd);
		return CAM_DISABLE;
	}

	if (cap_param->cap_scene == CAPTURE_AINR && pframe->common.raw_src != cap_param->cur_raw_sel) {
		pr_info("raw src not match %d %d\n", pframe->common.raw_src, cap_param->cur_raw_sel);
		return CAM_DISABLE;
	}

	if (node_type == CAM_NODE_TYPE_FRAME_CACHE && cap_param->alg_type == ALG_TYPE_CAP_XDR &&
		((cap_param->cap_scene != CAPTURE_RAWALG && cap_param->cap_scene != CAPTURE_AINR &&
		pframe->common.channel_id != CAM_CH_RAW) ||
		((cap_param->cap_scene == CAPTURE_RAWALG || cap_param->cap_scene == CAPTURE_AINR) &&
		pframe->common.channel_id == CAM_CH_RAW))) {
		pr_debug("cur scene is yuv capture, cache data no need link to user\n");
		return CAM_DISABLE;
	}

	return CAM_ENABLE;
}

static enum cam_en_status camnode_multi_capture_switch_link_en(struct cam_frame *pframe,
	struct cam_capture_param *cap_param, enum cam_node_frmsel_mode mode, uint32_t node_type)
{
	enum cam_en_status link_en = CAM_DISABLE;

	if (mode == CAM_NODE_FRAME_NO_SEL) {
		if(atomic_read(&cap_param->cap_cnt) > 0
			&& camnode_capture_skip_condition(pframe, cap_param))
			link_en |= CAM_ENABLE;
	} else {
		if (cap_param->fid && pframe->common.fid >= cap_param->fid)
			link_en |= CAM_ENABLE;

		if (!cap_param->fid && pframe->common.boot_sensor_time >= cap_param->cap_timestamp
			&& atomic_read(&cap_param->cap_cnt) > 0
			&& camnode_capture_skip_condition(pframe, cap_param))
			link_en |= CAM_ENABLE;

		if (cap_param->offline_icap_scene)
			link_en |= CAM_ENABLE;

		if (cap_param->cap_opt_frame_scene)
			link_en |= CAM_ENABLE;
	}

	link_en &= camnode_rawalg_scene_skip(pframe, cap_param, node_type);

	return link_en;
}

static int camnode_capture_switch_link_update(struct cam_frame *pframe,
	struct cam_port_linkage *switch_link, struct cam_capture_param *cap_param)
{
	if (cap_param->cap_scene != CAPTURE_RAWALG && cap_param->update_online_raw_link) {
		switch_link->node_type = CAM_NODE_TYPE_ISP_OFFLINE;
		switch_link->node_id = ISP_NODE_MODE_CAP_ID;
		switch_link->port_id = PORT_ISP_OFFLINE_IN;
		if (cap_param->cap_scene == CAPTURE_AINR) {
			switch_link->node_type = CAM_NODE_TYPE_USER;
			switch_link->node_id = 0;
			switch_link->port_id = PORT_USER_IN;
		}
		pr_debug("cur node switch link to node %d\n", switch_link->node_id);
	}

	return 0;
}

static int camnode_dynamic_link_update(struct cam_node *node, struct cam_frame *pframe)
{
	int ret = 0;
	uint32_t cur_port_id = 0, node_type = 0;
	struct cam_capture_param *cap_param = NULL;
	struct frame_cache_node *frame_cache_node = NULL;
	struct cam_port_linkage cap_new_link = {0}, cap_ori_link = {0}, cap_normal_link = {0};

	if (!node || !pframe) {
		pr_err("fail to get valid param %p %p\n", node, pframe);
		return -EFAULT;
	}

	node_type = node->node_graph->type;
	cur_port_id = pframe->common.link_from.port_id;
	cap_ori_link = node->node_graph->outport[cur_port_id].link;
	cap_new_link = node->node_graph->outport[cur_port_id].switch_link;
	cap_normal_link = node->node_graph->outport[cur_port_id].normal_link;
	cap_param = &node->cap_param;
	frame_cache_node = (struct frame_cache_node *)node->handle;

	pr_debug("node type %s id %d cap type %d, cnt %d, time %lld frame time %lld fid %d frame_id %d cur_port_id %d w %d h %d\n",
			cam_node_name_get(node->node_graph->type), node->node_graph->id, node->cap_param.cap_type,
			atomic_read(&node->cap_param.cap_cnt), node->cap_param.cap_timestamp, pframe->common.boot_sensor_time,
			node->cap_param.fid, pframe->common.fid, cur_port_id, pframe->common.width, pframe->common.height);
	switch (node->cap_param.cap_type) {
	case CAM_CAPTURE_START:
	case CAM_CAPTURE_START_3DNR:
		if (camnode_single_capture_switch_link_en(pframe, cap_param, frame_cache_node, node_type)) {
			if (camnode_rawalg_scene_skip(pframe, cap_param, node_type) == CAM_ENABLE) {
				camnode_capture_switch_link_update(pframe, &cap_new_link, cap_param);
				pframe->common.link_to = cap_new_link;
			} else
				pframe->common.link_to = cap_ori_link;
			if (cap_param->cap_scene == CAPTURE_RHDR)
				pframe->common.link_to = cap_normal_link;
		}
		else
			pframe->common.link_to = cap_ori_link;
		break;
	case CAM_CAPTURE_START_FROM_NEXT_SOF:
		if (node_type == CAM_NODE_TYPE_FRAME_CACHE &&
			frame_cache_node->cap_param.frm_sel_mode == CAM_NODE_FRAME_NO_SEL) {
			if (camnode_multi_capture_switch_link_en(pframe, cap_param,
				CAM_NODE_FRAME_NO_SEL, node_type)) {
				camnode_capture_switch_link_update(pframe, &cap_new_link, cap_param);
				pframe->common.link_to = cap_new_link;
				atomic_dec(&cap_param->cap_cnt);
			} else
				pframe->common.link_to = cap_ori_link;
		} else {
			if (camnode_multi_capture_switch_link_en(pframe, cap_param,
				CAM_NODE_FRAME_SEL, node_type)) {
				camnode_capture_switch_link_update(pframe, &cap_new_link, cap_param);
				pframe->common.link_to = cap_new_link;
				atomic_dec(&cap_param->cap_cnt);
			} else
				pframe->common.link_to = cap_ori_link;
		}
		break;
	case CAM_CAPTURE_START_WITH_TIMESTAMP:
		pframe->common.link_to = cap_new_link;
		break;
	case CAM_CAPTURE_STOP:
		pframe->common.link_to = cap_ori_link;
		break;
	default:
		break;
	}

	return ret;
}

static void camnode_frame_flag_update(struct cam_node *node, struct cam_frame *pframe)
{
	if (node->cap_param.cap_scene == CAPTURE_HW3DNR) {
		pframe->common.xtm_conflict.need_ltm_map = 0;
		pframe->common.xtm_conflict.need_ltm_hist = 0;
		pframe->common.xtm_conflict.need_gtm_map = 0;
		pframe->common.xtm_conflict.need_gtm_hist = 0;
		pframe->common.xtm_conflict.gtm_mod_en = 0;
	}
	if (node->cap_param.cap_type == CAM_CAPTURE_START_FROM_NEXT_SOF)
		pframe->common.not_use_isp_reserved_buf = 1;
}

static int camnode_shutoff_callback(void *param, void *priv_data)
{
	uint32_t ret = 0, port_id = 0, is_shutoff = 0;
	enum shutoff_scene shutoff_scene;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;
	struct cam_node_shutoff_ctrl *node_shutoff = NULL;
	struct cam_port_shutoff_ctrl *port_shutoff = NULL;

	if (!priv_data || !param) {
		pr_err("fail to get valid %p %p\n", priv_data, param);
		return -EFAULT;
	}

	node = (struct cam_node *)priv_data;
	in_param = (struct cam_node_cfg_param *)param;

	port_id = in_param->port_id;
	is_shutoff = *(uint32_t *)in_param->param;

	node_shutoff = &node->node_shutoff;
	port_shutoff = &node_shutoff->outport_shutoff[port_id];
	shutoff_scene = port_shutoff->shutoff_scene;

	if (port_shutoff->raw_switch_en) {
		ret = dcam_online_port_param_update(node, port_id, port_shutoff);
		return ret;
	}

	switch (shutoff_scene) {
	case SHUTOFF_MULTI_PORT_SWITCH:
		if (atomic_read(&port_shutoff->cap_cnt)) {
			is_shutoff = node->ops.set_shutoff(node, node_shutoff, port_id);
			atomic_dec(&port_shutoff->cap_cnt);
		} else {
			port_shutoff->shutoff_type = !port_shutoff->shutoff_type;
			is_shutoff = node->ops.set_shutoff(node, node_shutoff, port_id);
			port_shutoff->shutoff_scene = SHUTOFF_SCENE_MAX;
		}
		break;
	case SHUTOFF_SINGLE_PORT_NONZSL:
		if (atomic_read(&port_shutoff->cap_cnt)) {
			atomic_dec(&port_shutoff->cap_cnt);
			if (!atomic_read(&port_shutoff->cap_cnt))
				node->ops.set_shutoff(node, node_shutoff, port_id);
		}
		break;
	case SHUTOFF_SINGLE_PORT_NORMAL:
		is_shutoff = node->ops.set_shutoff(node, node_shutoff, port_id);
		pr_debug("is_shutoff %d, type %d, scene %d\n", is_shutoff, port_shutoff->shutoff_type,
			port_shutoff->shutoff_scene);
		break;
	default:
		break;
	}

	return is_shutoff;
}

static int camnode_set_shutoff(void *handle, void *param, uint32_t port_id)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_shutoff_ctrl *node_shutoff = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	node_shutoff = (struct cam_node_shutoff_ctrl *)param;

	if (node->need_fetch)
		return 0;

	switch (node->node_graph->type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		ret = dcam_online_node_set_shutoff(node->handle, node_shutoff, port_id);
		break;
	default:
		pr_warn("warning: node_type %d not support shutoff temp.", node->node_graph->type);
		break;
	}

	return ret;
}

static void camnode_cfg_shutoff_init(void *handle)
{
	int i = 0;
	struct cam_node *node = NULL;

	node = (struct cam_node *)handle;

	for (i = 0; i < CAM_NODE_PORT_IN_NUM; i++) {
		atomic_set(&node->node_shutoff.inport_shutoff[i].cap_cnt, 0);
		node->node_shutoff.inport_shutoff[i].port_id = CAM_NODE_PORT_IN_NUM;
		node->node_shutoff.inport_shutoff[i].shutoff_scene = SHUTOFF_SCENE_MAX;
		node->node_shutoff.inport_shutoff[i].shutoff_type = SHUTOFF_TYPE_MAX;
		node->node_shutoff.inport_shutoff[i].raw_switch_en = CAM_DISABLE;
		node->node_shutoff.inport_shutoff[i].pre_raw_sel = MAX_RAW_SRC_SEL;
		node->node_shutoff.inport_shutoff[i].cur_raw_sel = MAX_RAW_SRC_SEL;
	}
	for (i = 0; i < CAM_NODE_PORT_OUT_NUM; i++) {
		atomic_set(&node->node_shutoff.outport_shutoff[i].cap_cnt, 0);
		node->node_shutoff.outport_shutoff[i].port_id = CAM_NODE_PORT_OUT_NUM;
		node->node_shutoff.outport_shutoff[i].shutoff_scene = SHUTOFF_SCENE_MAX;
		node->node_shutoff.outport_shutoff[i].shutoff_type = SHUTOFF_TYPE_MAX;
		node->node_shutoff.outport_shutoff[i].raw_switch_en = CAM_DISABLE;
		node->node_shutoff.outport_shutoff[i].pre_raw_sel = MAX_RAW_SRC_SEL;
		node->node_shutoff.outport_shutoff[i].cur_raw_sel = MAX_RAW_SRC_SEL;
	}
}

static int camnode_cfg_shutoff_config(void *handle, void *param)
{
	int ret = 0, i = 0;
	struct cam_node *node = NULL;
	struct cam_node_shutoff_ctrl *node_shutoff = NULL;

	if (!param) {
		pr_err("fail to get valid input ptr  %p\n", param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	node_shutoff = (struct cam_node_shutoff_ctrl *)param;

	for (i = 0; i < CAM_NODE_PORT_IN_NUM; i++) {
		if (i == node_shutoff->inport_shutoff[i].port_id) {
			node->node_shutoff.inport_shutoff[i].cap_cnt = node_shutoff->inport_shutoff[i].cap_cnt;
			node->node_shutoff.inport_shutoff[i].port_id = node_shutoff->inport_shutoff[i].port_id;
			node->node_shutoff.inport_shutoff[i].shutoff_scene = node_shutoff->inport_shutoff[i].shutoff_scene;
			node->node_shutoff.inport_shutoff[i].shutoff_type = node_shutoff->inport_shutoff[i].shutoff_type;
			node->node_shutoff.inport_shutoff[i].raw_switch_en = node_shutoff->inport_shutoff[i].raw_switch_en;
			node->node_shutoff.inport_shutoff[i].pre_raw_sel = node_shutoff->inport_shutoff[i].pre_raw_sel;
			node->node_shutoff.inport_shutoff[i].cur_raw_sel = node_shutoff->inport_shutoff[i].cur_raw_sel;
			pr_debug("i %d, cap_cnt %d, port_id %d, shutoff_type %d, shutoff_scene %d\n",
				i, atomic_read(&node_shutoff->inport_shutoff[i].cap_cnt), node_shutoff->inport_shutoff[i].port_id,
				node_shutoff->inport_shutoff[i].shutoff_type, node_shutoff->inport_shutoff[i].shutoff_scene);
		}
	}
	for (i = 0; i < CAM_NODE_PORT_OUT_NUM; i++) {
		if (i == node_shutoff->outport_shutoff[i].port_id) {
			node->node_shutoff.outport_shutoff[i].cap_cnt = node_shutoff->outport_shutoff[i].cap_cnt;
			node->node_shutoff.outport_shutoff[i].port_id = node_shutoff->outport_shutoff[i].port_id;
			node->node_shutoff.outport_shutoff[i].shutoff_scene = node_shutoff->outport_shutoff[i].shutoff_scene;
			node->node_shutoff.outport_shutoff[i].shutoff_type = node_shutoff->outport_shutoff[i].shutoff_type;
			node->node_shutoff.outport_shutoff[i].raw_switch_en = node_shutoff->outport_shutoff[i].raw_switch_en;
			node->node_shutoff.outport_shutoff[i].pre_raw_sel = node_shutoff->outport_shutoff[i].pre_raw_sel;
			node->node_shutoff.outport_shutoff[i].cur_raw_sel = node_shutoff->outport_shutoff[i].cur_raw_sel;
			pr_debug("i %d, cap_cnt %d, port_id %d, shutoff_type %d, shutoff_scene %d\n",
				i, atomic_read(&node_shutoff->outport_shutoff[i].cap_cnt), node_shutoff->outport_shutoff[i].port_id,
				node_shutoff->outport_shutoff[i].shutoff_type, node_shutoff->outport_shutoff[i].shutoff_scene);
		}
	}

	return ret;
}

static int camnode_cfg_shutoff_reconfig(struct cam_node *node, uint32_t port_id)
{
	struct cam_port_shutoff_ctrl *outport_shutoff = NULL;
	struct cam_node_shutoff_ctrl node_shutoff = {0};
	uint32_t frame_cycle = 0;

	if (node->need_fetch)
		return 0;

	outport_shutoff = &node->node_shutoff.outport_shutoff[port_id];
	if (outport_shutoff->shutoff_scene != SHUTOFF_SCENE_MAX) {
		if (atomic_inc_return(&outport_shutoff->cap_cnt) == 1) {
			atomic_inc(&outport_shutoff->cap_cnt);
			CAM_NODE_SHUTOFF_PARAM_INIT(node_shutoff);
			node_shutoff.outport_shutoff[port_id].port_id = port_id;
			node_shutoff.outport_shutoff[port_id].shutoff_type = SHUTOFF_RESUME;
			dcam_online_node_set_shutoff(node->handle, &node_shutoff, port_id);
			frame_cycle = 1;
		}
	}
	return frame_cycle;
}

static int camnode_cfg_shutoff(void *handle, uint32_t cmd, void *param)
{
	int ret = 0, port_id = 0;
	struct cam_node *node = NULL;
	struct cam_node_shutoff_ctrl *node_shutoff = NULL;

	if (!handle) {
		pr_err("fail to get valid input ptr %p\n", handle);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;

	switch (cmd) {
	case CAM_NODE_SHUTOFF_INIT:
		camnode_cfg_shutoff_init(node);
		break;
	case CAM_NODE_SHUTOFF_CONFIG:
		node_shutoff = (struct cam_node_shutoff_ctrl *)param;
		camnode_cfg_shutoff_config(node, node_shutoff);
		break;
	case CAM_NODE_SHUTOFF_RECONFIG:
		port_id = *(uint32_t *)param;
		ret = camnode_cfg_shutoff_reconfig(node, port_id);
		break;
	default:
		pr_err("fail to support cmd %d\n", cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

static int camnode_cfg_node_param_dcam_online(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;
	struct cam_capture_param *cap_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_ZOOM:
		ret = node->ops.cfg_port_param(node, PORT_CFG_ZOOM_SET, param);
		break;
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_PRE_OUT_MODE:
		ret = node->ops.cfg_port_param(node, PORT_CFG_PRE_OUT_MODE, param);
		break;
	case CAM_NODE_CFG_ALTER_BUF:
		ret = dcam_online_node_cfg_alter_buf(node->handle, in_param);
		break;
	case CAM_NODE_CFG_ALTER_BUF_CLR:
		ret = dcam_online_node_clr_alter_buf(node->handle, in_param);
		break;
	case CAM_NODE_CFG_FID_GET:
		ret = dcam_online_node_fid_get(node->handle, in_param);
		break;
	case CAM_NODE_CFG_CAP_PARAM:
		cap_param = (struct cam_capture_param *)in_param->param;
		node->cap_param.cap_type = cap_param->cap_type;
		node->cap_param.cap_cnt = cap_param->cap_cnt;
		node->cap_param.cap_timestamp = cap_param->cap_timestamp;
		node->cap_param.cap_scene = cap_param->cap_scene;
		node->cap_param.cap_user_crop = cap_param->cap_user_crop;
		node->cap_param.no_need_skip_frame_scene = cap_param->no_need_skip_frame_scene;
		node->cap_param.skip_first_num = cap_param->skip_first_num;
		node->cap_param.cap_opt_frame_scene = cap_param->cap_opt_frame_scene;
		node->cap_param.fid = cap_param->fid;
		node->cap_param.offline_icap_scene = cap_param->offline_icap_scene;
		node->cap_param.alg_type = cap_param->alg_type;
		node->cap_param.update_online_raw_link = cap_param->update_online_raw_link;
		node->cap_param.pre_raw_sel = cap_param->pre_raw_sel;
		node->cap_param.cur_raw_sel = cap_param->cur_raw_sel;
		pr_info("node: %s id %d cap K_type %d, scene %d, cnt %d, skip first_frame %d time %lld, fid %d, opt_scene %d icap scene %d\n",
			cam_node_name_get(node->node_graph->type), node->node_graph->id, node->cap_param.cap_type,
			node->cap_param.cap_scene, atomic_read(&node->cap_param.cap_cnt),
			node->cap_param.skip_first_num, node->cap_param.cap_timestamp, node->cap_param.fid,
			node->cap_param.cap_opt_frame_scene, node->cap_param.offline_icap_scene);
		break;
	case CAM_NODE_CFG_RECT_GET:
	case CAM_NODE_CFG_STATIS:
		ret = dcam_online_node_cfg_param(node->handle, cmd, in_param->param);
		break;
	case CAM_NODE_CFG_BLK_PARAM:
		ret = dcam_online_node_blk_param_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_SHARE_BUF:
		ret = dcam_online_node_share_buf(node->handle, in_param);
		break;
	case CAM_NODE_CFG_RESET:
		ret = dcam_online_node_reset(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = dcam_online_node_port_insert(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_XTM_EN:
		ret = dcam_online_node_xtm_disable(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_FLASH_SKIP_FID:
		ret = node->ops.cfg_port_param(node, PORT_CFG_FLASH_SKIP_FID, param);
		break;
	case CAM_NODE_CFG_PORT_RAW_SEL:
		ret = dcam_online_set_raw_sel(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_SHUTOFF_PORT_CNT_DEC:
		ret = dcam_online_shutoff_cnt_dec(node, in_param->param);
		break;
	default:
		pr_err("fail to support node %s cmd %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_dcam_offline(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_FMT:
		ret = node->ops.cfg_port_param(node, PORT_CFG_FMT_SET, param);
		break;
	case CAM_NODE_CFG_BLK_PARAM:
		ret = dcam_offline_node_blk_param_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_PARAM_FID:
		ret = dcam_offline_node_blkpm_fid_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_RESET_PARAM_PTR:
		dcam_offline_node_param_ptr_reset(node->handle);
		break;
	case CAM_NODE_CFG_STATIS:
		ret = dcam_offline_node_statis_cfg(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = dcam_offline_node_port_insert(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_PARAM_SWITCH:
		ret = dcamoffline_blkpm_prepare(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_FETCH_PARAM:
		ret = dcam_offline_fetch_param_reconfig(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_OFFLINE_ICAP_DUMP_EN:
		ret = dcam_offline_icap_dump_param_en(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_BUF_CLR:
		if (in_param->port_type == PORT_TRANSFER_OUT)
			ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_CLR, node->handle);
		else if (in_param->port_type == PORT_TRANSFER_IN)
			dcam_offline_node_buffer_clr(node->handle);
		else
			pr_err("fail to support port type:%d.\n", in_param->port_type);
		break;
	default:
		pr_err("fail to support node: %s cmd %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_dcam_offline_bpc(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_BLK_PARAM:
		ret = dcam_offline_node_blk_param_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = dcam_offline_node_port_insert(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_BUF_CLR:
		if (in_param->port_type == PORT_TRANSFER_OUT)
			ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_CLR, node->handle);
		else if (in_param->port_type == PORT_TRANSFER_IN)
			dcam_offline_node_buffer_clr(node->handle);
		else
			pr_err("fail to support port type:%d.\n", in_param->port_type);
		break;
	default:
		pr_err("fail to support node %s cmd %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_dcam_offline_lsc(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_BLK_PARAM:
		ret = dcam_offline_node_blk_param_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = dcam_offline_node_port_insert(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_PARAM_FID:
		ret = dcam_offline_node_blkpm_fid_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_RESET_PARAM_PTR:
		dcam_offline_node_param_ptr_reset(node->handle);
		break;
	case CAM_NODE_CFG_BUF_CLR:
		if (in_param->port_type == PORT_TRANSFER_OUT)
			ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_CLR, node->handle);
		else if (in_param->port_type == PORT_TRANSFER_IN)
			dcam_offline_node_buffer_clr(node->handle);
		else
			pr_err("fail to support port type:%d.\n", in_param->port_type);
		break;
	default:
		pr_err("fail to support node %s cmd %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_dcam_offline_raw2frgb(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_BLK_PARAM:
		ret = dcam_offline_node_blk_param_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = dcam_offline_node_port_insert(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_STATIS:
		ret = dcam_offline_node_statis_cfg(node->handle, in_param->param);
		break;
	default:
		pr_err("fail to support node %s cmd %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_dcam_offline_frgb2yuv(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_BLK_PARAM:
		ret = dcam_offline_node_blk_param_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = dcam_offline_node_port_insert(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_STATIS:
		ret = dcam_offline_node_statis_cfg(node->handle, in_param->param);
		break;
	default:
		pr_err("fail to support node %s cmd %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_dcam_fetch(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;
	struct cam_capture_param *cap_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_CAP_PARAM:
		cap_param = (struct cam_capture_param *)in_param->param;
		node->cap_param.cap_type = cap_param->cap_type;
		node->cap_param.cap_cnt = cap_param->cap_cnt;
		node->cap_param.cap_timestamp = cap_param->cap_timestamp;
		node->cap_param.cap_scene = cap_param->cap_scene;
		node->cap_param.cap_user_crop = cap_param->cap_user_crop;
		node->cap_param.no_need_skip_frame_scene = cap_param->no_need_skip_frame_scene;
		node->cap_param.fid = cap_param->fid;
		node->cap_param.alg_type = cap_param->alg_type;
		node->cap_param.update_online_raw_link = cap_param->update_online_raw_link;
		node->cap_param.pre_raw_sel = cap_param->pre_raw_sel;
		node->cap_param.cur_raw_sel = cap_param->cur_raw_sel;
		pr_info("node: %s id %d cap K_type %d, scene %d, cnt %d, time %lld, fid %d\n", cam_node_name_get(node->node_graph->type),
			node->node_graph->id, node->cap_param.cap_type, node->cap_param.cap_scene, atomic_read(&node->cap_param.cap_cnt),
			node->cap_param.cap_timestamp, node->cap_param.fid);
		break;
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_FETCH_BUF:
	case CAM_NODE_CFG_RECT_GET:
	case CAM_NODE_CFG_STATIS:
	case CAM_NODE_CFG_ALTER_BUF:
	case CAM_NODE_CFG_ALTER_BUF_CLR:
	case CAM_NODE_CFG_PORT_RAW_SEL:
	case CAM_NODE_CFG_FETCH_ONLINE_EN:
	case CAM_NODE_CFG_FID_GET:
		ret = dcam_fetch_node_cfg_param(node->handle, cmd, in_param);
		break;
	case CAM_NODE_CFG_SHUTOFF_PORT_CNT_DEC:
		ret = dcam_online_shutoff_cnt_dec(node, in_param->param);
		break;
	case CAM_NODE_CFG_BLK_PARAM:
		ret = dcam_fetch_node_blk_param_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_SHARE_BUF:
		ret = dcam_fetch_share_buf(node->handle, in_param);
		break;
	case CAM_NODE_CFG_RESET:
		ret = dcam_fetch_node_reset(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = dcam_fetch_node_port_insert(node->handle, in_param->param);
		break;
	default:
		pr_err("fail to support node %s cmd %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_isp_offline(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;
	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_BUF_CLR:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_CLR, param);
		break;
	case CAM_NODE_CFG_FMT:
		ret = node->ops.cfg_port_param(node, PORT_CFG_FMT_SET, param);
		break;
	case CAM_NODE_CFG_BASE:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BASE_SET, param);
		break;
	case CAM_NODE_CFG_BLK_PARAM:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_BLK_PATAM, in_param->param);
		break;
	case CAM_NODE_CFG_CAP_PARAM:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_CAP_PARAM, in_param->param);
		break;
	case CAM_NODE_CFG_UFRAME:
		if (in_param->port_type == PORT_TRANSFER_IN)
			ret = isp_node_config(node->handle, ISP_NODE_CFG_PORT_UFRAME, in_param->param);
		if (in_param->port_type == PORT_TRANSFER_OUT)
			ret = node->ops.cfg_port_param(node, PORT_CFG_UFRAME_SET, param);
		break;
	case CAM_NODE_CFG_STATIS:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_STATIS, in_param->param);
		break;
	case CAM_NODE_CFG_RESERVE_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_RESBUF_SET, param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_INSERT_PORT, in_param->param);
		break;
	case CAM_NODE_CFG_3DNR_MODE:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_3DNR_MODE, in_param->param);
		break;
	case CAM_NODE_CFG_GTM:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_GTM, in_param->param);
		break;
	case CAM_NODE_CFG_PARAM_SWITCH:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_PARAM_SWITCH, in_param->param);
		break;
	case CAM_NODE_CFG_POSTPROC_PARAM:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_POSTPROC_PARAM, in_param->param);
		break;
	case CAM_NODE_CFG_PARAM_Q_CLEAR:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_PARAM_Q_CLEAR, in_param->param);
		break;
	case CAM_NODE_CFG_FAST_STOP:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_FAST_STOP, in_param->param);
		break;
	case CAM_NODE_CFG_XTM_EN:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_XTM_EN, in_param->param);
		break;
	case CAM_NODE_CFG_CLEAR_FAST_STOP:
		ret = isp_node_config(node->handle, ISP_NODE_CFG_CLEAR_FAST_STOP, in_param->param);
		break;
	default:
		pr_err("fail to support node: %s cmd: %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_isp_yuv_scaler(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, param);
		break;
	case CAM_NODE_CFG_UFRAME:
		ret = isp_scaler_node_cfg_param(node->handle, in_param->port_id, ISP_YUV_SCALER_NODE_CFG_PORT_UFRAME, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		ret = isp_scaler_node_cfg_param(node->handle, in_param->port_id, ISP_YUV_SCALER_NODE_INSERT_PORT, in_param->param);
		break;
	case CAM_NODE_CFG_RESERVE_BUF:
		ret = isp_scaler_node_cfg_param(node->handle, in_param->port_id, ISP_YUV_SCALER_NODE_CFG_RESERVE_BUF, in_param->param);
		break;
	case CAM_NODE_CFG_FAST_STOP:
		ret = isp_scaler_node_cfg_param(node->handle, in_param->port_id, ISP_YUV_SCALER_NODE_CFG_FAST_STOP, in_param->param);
		break;
	case CAM_NODE_CFG_CLEAR_FAST_STOP:
		ret = isp_scaler_node_cfg_param(node->handle, in_param->port_id, ISP_YUV_SCALER_NODE_CFG_CLEAR_FAST_STOP, in_param->param);
		break;
	default:
		pr_err("fail to support node: %s cmd: %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param_frame_cache(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;
	struct cam_capture_param *cap_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_CAP_PARAM:
		cap_param = (struct cam_capture_param *)in_param->param;
		node->cap_param.cap_type = cap_param->cap_type;
		node->cap_param.cap_cnt = cap_param->cap_cnt;
		node->cap_param.cap_timestamp = cap_param->cap_timestamp;
		node->cap_param.cap_scene = cap_param->cap_scene;
		node->cap_param.cap_user_crop = cap_param->cap_user_crop;
		node->cap_param.no_need_skip_frame_scene = cap_param->no_need_skip_frame_scene;
		node->cap_param.cap_opt_frame_scene = cap_param->cap_opt_frame_scene;
		node->cap_param.fid = cap_param->fid;
		node->cap_param.alg_type = cap_param->alg_type;
		node->cap_param.update_online_raw_link = cap_param->update_online_raw_link;
		node->cap_param.pre_raw_sel = cap_param->pre_raw_sel;
		node->cap_param.cur_raw_sel = cap_param->cur_raw_sel;
		pr_debug("node type %s id %d cap type %d, scene %d, cnt %d, time %lld opt_frame %d, fid %d\n", cam_node_name_get(node->node_graph->type),
			node->node_graph->id, node->cap_param.cap_type, node->cap_param.cap_scene, atomic_read(&node->cap_param.cap_cnt),
			node->cap_param.cap_timestamp, node->cap_param.cap_opt_frame_scene, node->cap_param.fid);
		ret = frame_cache_cfg_param(node->handle, cmd, in_param->param);
		break;
	case CAM_NODE_CFG_CLR_CACHE_BUF:
	case CAM_NODE_CFG_DUAL_SYNC_BUF_GET:
		ret = frame_cache_cfg_param(node->handle, cmd, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		break;
	case CAM_NODE_CFG_PRE_RAW_FLAG:
		ret = frame_cache_node_set_pre_raw_flag(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_GET_CAP_FRAME:
		ret = frame_cache_node_get_cap_frame(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_PRE_FRAME_STATUS:
		ret = frame_cache_node_get_pre_frame_status(node->handle, in_param->param);
		break;
	default:
		pr_err("fail to support node: %s cmd: %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

static int camnode_cfg_node_param_pyr_dec(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_BLK_PARAM:
		ret = pyr_dec_node_blk_param_set(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_CTXID:
		ret = pyr_dec_node_ctxid_cfg(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_FAST_STOP:
		ret = pyr_dec_node_fast_stop_cfg(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_PARAM_SWITCH:
		ret = pyr_dec_node_param_buf_cfg(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_POSTPROC_PARAM:
		ret = pyr_dec_node_postproc_param_cfg(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_BASE:
		ret = pyr_dec_node_base_cfg(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_BUF_CLR:
		pyr_dec_node_buffer_clr(node->handle);
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_CLR, in_param->param);
		if (ret)
			pr_err("fail to recycle dec out buffer.\n");
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		break;
	case CAM_NODE_CFG_CLEAR_FAST_STOP:
		ret = pyr_dec_node_clear_fast_stop_cfg(node->handle, in_param->param);
		break;
	default:
		pr_err("fail to support node: %s cmd: %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

static int camnode_cfg_node_param_copy(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;
	struct cam_capture_param *cap_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;
	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		ret = cam_copy_node_buffer_cfg(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_BUF_NUM:
		ret = cam_copy_node_buffer_num(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_PRE_RAW_FLAG:
		ret = cam_copy_node_set_pre_raw_flag(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_OPT_SCENE_SWITCH:
		ret = cam_copy_node_set_opt_scene(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_ICAP_SCENE_SWITCH:
		ret = cam_copy_node_set_icap_scene(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_ICAP_RAW_REPORT_CONDITION:
		ret = cam_copy_node_set_raw_report_condition(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_CAP_PARAM:
		cap_param = (struct cam_capture_param *)in_param->param;
		node->cap_param.cap_type = cap_param->cap_type;
		node->cap_param.cap_cnt = cap_param->cap_cnt;
		node->cap_param.cap_timestamp = cap_param->cap_timestamp;
		node->cap_param.cap_scene = cap_param->cap_scene;
		node->cap_param.cap_user_crop = cap_param->cap_user_crop;
		node->cap_param.no_need_skip_frame_scene = cap_param->no_need_skip_frame_scene;
		node->cap_param.skip_first_num = cap_param->skip_first_num;
		node->cap_param.cap_opt_frame_scene = cap_param->cap_opt_frame_scene;
		node->cap_param.offline_icap_scene = cap_param->offline_icap_scene;
		node->cap_param.fid = cap_param->fid;
		pr_info("node: %s id %d cap K_type %d, scene %d, cnt %d, skip first_frame %d time %lld, opt_scene %d offline_icap_scene %d\n",
			cam_node_name_get(node->node_graph->type), node->node_graph->id, node->cap_param.cap_type,
			node->cap_param.cap_scene, atomic_read(&node->cap_param.cap_cnt),
			node->cap_param.skip_first_num, node->cap_param.cap_timestamp,
			node->cap_param.cap_opt_frame_scene, node->cap_param.offline_icap_scene);
		ret = cam_copy_cfg_param(node->handle, in_param->param);
		break;
	case CAM_NODE_CFG_INSERT_PORT:
		break;
	default:
		pr_err("fail to support node: %s cmd: %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_cfg_node_param(void *handle, enum cam_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (node->node_graph->type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		if (node->need_fetch)
			ret = camnode_cfg_node_param_dcam_fetch(node, cmd, param);
		else
			ret = camnode_cfg_node_param_dcam_online(node, cmd, param);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
		ret = camnode_cfg_node_param_dcam_offline(node, cmd, param);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
		ret = camnode_cfg_node_param_dcam_offline_bpc(node, cmd, param);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
		ret = camnode_cfg_node_param_dcam_offline_lsc(node, cmd, param);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
		ret = camnode_cfg_node_param_dcam_offline_raw2frgb(node, cmd, param);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		ret = camnode_cfg_node_param_dcam_offline_frgb2yuv(node, cmd, param);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		ret = camnode_cfg_node_param_isp_offline(node, cmd, param);
		break;
	case CAM_NODE_TYPE_ISP_YUV_SCALER:
		ret = camnode_cfg_node_param_isp_yuv_scaler(node, cmd, param);
		break;
	case CAM_NODE_TYPE_FRAME_CACHE:
		ret = camnode_cfg_node_param_frame_cache(node, cmd, param);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		ret = camnode_cfg_node_param_pyr_dec(node, cmd, param);
		break;
	case CAM_NODE_TYPE_DATA_COPY:
		ret = camnode_cfg_node_param_copy(node, cmd, param);
		break;
	default:
		pr_err("fail to support node: %s cmd: %d\n", cam_node_name_get(node->node_graph->type), cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

static int camnode_cfg_port_param(void *handle, enum cam_port_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct cam_port *port = NULL;
	struct cam_node_cfg_param *in_param = NULL;
	struct cam_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;
	if ((node->node_graph->type == CAM_NODE_TYPE_DCAM_ONLINE) ||
		(node->node_graph->type == CAM_NODE_TYPE_DCAM_OFFLINE) ||
		(node->node_graph->type == CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW) ||
		(node->node_graph->type == CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW) ||
		(node->node_graph->type == CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB) ||
		(node->node_graph->type == CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV) ||
		(node->node_graph->type == CAM_NODE_TYPE_FRAME_CACHE) ||
		(node->node_graph->type == CAM_NODE_TYPE_PYR_DEC) ||
		(node->node_graph->type == CAM_NODE_TYPE_ISP_YUV_SCALER)) {
		/* find specific port by port id, now only for out port */
		if (in_param->port_id >= CAM_NODE_PORT_OUT_NUM) {
			pr_err("fail to get valid port id %d\n", in_param->port_id);
			ret = -EFAULT;
			goto exit;
		}
		port = node->outport_list[in_param->port_id];
	} else {
		if (in_param->port_type == PORT_TRANSFER_IN) {
			if (in_param->port_id >= CAM_NODE_PORT_IN_NUM) {
				pr_err("fail to get valid port id %d\n", in_param->port_id);
				ret = -EFAULT;
				goto exit;
			}
			port = node->inport_list[in_param->port_id];
		} else {
			/* find specific port by port id, now only for out port */
			if (in_param->port_id >= CAM_NODE_PORT_OUT_NUM) {
				pr_err("fail to get valid port id %d\n", in_param->port_id);
				ret = -EFAULT;
				goto exit;
			}
			port = node->outport_list[in_param->port_id];
		}
	}

	if (port) {
		if (port->port_graph->link_state != PORT_LINK_NORMAL || !port->ops.cfg_param) {
			pr_err("fail to get valid port id %d state %d\n", in_param->port_id,
				port->port_graph->link_state);
			ret = -EFAULT;
			goto exit;
		}
		ret = port->ops.cfg_param(port, cmd, in_param->param);
	}
exit:
	return ret;
}

static int camnode_request_proc(void *handle, void *in_param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *node_param = NULL;

	if (!in_param || !handle) {
		pr_err("fail to get valid param %p %p\n", in_param, node);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	node_param = (struct cam_node_cfg_param *)in_param;
	switch (node->node_graph->type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		if (node->need_fetch)
			ret = dcam_fetch_node_request_proc(node->handle, node_param);
		else
			ret = dcam_online_node_request_proc(node->handle, node_param);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		ret = dcam_offline_node_request_proc(node->handle, node_param);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		ret = isp_node_request_proc(node->handle, node_param->param);
		break;
	case CAM_NODE_TYPE_ISP_YUV_SCALER:
		ret = isp_scaler_node_request_proc(node->handle, node_param->param);
		break;
	case CAM_NODE_TYPE_FRAME_CACHE:
		ret = frame_cache_node_request_proc(node->handle, node_param->param);
		break;
	case CAM_NODE_TYPE_DUMP:
		ret = cam_dump_node_request_proc(node->handle, node_param);
		break;
	case CAM_NODE_TYPE_REPLACE:
		ret = cam_replace_node_request_proc(node->handle, node_param);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		ret = pyr_dec_node_request_proc(node->handle, node_param->param);
		break;
	case CAM_NODE_TYPE_DATA_COPY:
		ret = cam_copy_node_request_proc(node->handle, node_param);
		break;
	default:
		pr_err("fail to support node type %s\n", cam_node_name_get(node->node_graph->type));
		break;
	}

	return ret;
}

static int camnode_stop_proc(void *handle, void *in_param)
{
	int ret = 0;
	struct cam_node *node = NULL;

	if (!in_param || !handle) {
		pr_err("fail to get valid param %p %p\n", in_param, node);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;

	if (node->need_fetch)
		ret = dcam_fetch_node_stop_proc(node->handle, in_param);
	else
		ret = dcam_online_node_stop_proc(node->handle, in_param);
	if (ret)
		pr_err("fail to stop node\n");
	return ret;
}

static int camnode_outbuf_back(void *handle, void *in_param)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct cam_node_cfg_param *node_param = NULL;
	struct cam_frame *pframe = NULL;

	if (!handle || !in_param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, in_param);
		return -EFAULT;
	}

	node = (struct cam_node *)handle;
	node_param = (struct cam_node_cfg_param *)in_param;
	switch (node->node_graph->type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		pframe = (struct cam_frame *)node_param->param;
		if (pframe->common.cfg_to_alter_q ==  CAM_ENABLE)
			ret = node->ops.cfg_node_param(node, CAM_NODE_CFG_ALTER_BUF, node_param);
		else
			ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, node_param);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, node_param);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, node_param);
		break;
	case CAM_NODE_TYPE_FRAME_CACHE:
		ret = frame_cache_outbuf_back(node->handle, node_param->param);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		ret = node->ops.cfg_port_param(node, PORT_CFG_BUFFER_SET, node_param);
		break;
	case CAM_NODE_TYPE_DATA_COPY:
		ret = cam_copy_outbuf_back(node->handle, node_param->param);
		break;
	default:
		pr_err("fail to support node type %s\n", cam_node_name_get(node->node_graph->type));
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camnode_zoom_callback(void *priv_data, void *param)
{
	int ret = 0;
	struct cam_node *node = NULL;

	if (!priv_data || !param) {
		pr_err("fail to get valid param priv_data:%p, param:%p\n", priv_data, param);
		return -1;
	}

	node = (struct cam_node *)priv_data;
	ret = node->zoom_cb_func(node->data_cb_handle, param);

	return ret;
}

static int camnode_data_callback(enum cam_cb_type type, void *param, void *priv_data)
{
	int ret = 0;
	uint32_t cur_port_id = 0, cur_to_port_id = 0;
	struct cam_node *node = NULL;
	struct cam_frame *pframe = NULL;

	if (!param || !priv_data) {
		pr_err("fail to get valid param %p %p\n", param, priv_data);
		return -EFAULT;
	}
	node = (struct cam_node *)priv_data;
	if (type == CAM_CB_DCAM_DEV_ERR ||
		type == CAM_CB_ISP_DEV_ERR ||
		type == CAM_CB_ISP_RESET_ERR) {
		pr_err("fail to fatal err accured\n");
		goto exit;
	}

	pframe = (struct cam_frame *)param;
	cur_port_id = pframe->common.link_from.port_id;
	cur_to_port_id = pframe->common.link_to.port_id;
	switch (type) {
	case CAM_CB_DCAM_DATA_DONE:
	case CAM_CB_ISP_RET_PYR_DEC_BUF:
	case CAM_CB_FRAME_CACHE_DATA_DONE:
	case CAM_CB_YUV_SCALER_DATA_DONE:
		camnode_frame_flag_update(node, pframe);
		if (!IS_VALID_PORT_ID(cur_port_id)) {
			pr_err("fail to get valid cur port id %d\n", cur_port_id);
			return -EFAULT;
		}
		if (node->node_graph->outport[cur_port_id].link_state == PORT_LINK_NORMAL) {
			pframe->common.link_to = node->node_graph->outport[cur_port_id].link;
			if (node->node_graph->outport[cur_port_id].dynamic_link_en)
				camnode_dynamic_link_update(node, pframe);
			if (node->node_graph->outport[cur_port_id].dump_en) {
				pframe->common.dump_en = CAM_ENABLE;
				pframe->common.dump_node_id = node->node_graph->outport[cur_port_id].dump_node_id;
			}
			if (node->node_graph->outport[cur_port_id].replace_en) {
				pframe->common.replace_en = CAM_ENABLE;
				pframe->common.replace_node_id = node->node_graph->outport[cur_port_id].replace_node_id;
			}
			if (node->node_graph->outport[cur_port_id].copy_en &&
				(node->cap_param.cap_scene != CAPTURE_RAWALG)) {
				pframe->common.copy_en = CAM_ENABLE;
				pframe->common.copy_node_id = node->node_graph->outport[cur_port_id].copy_node_id;
			}
		}
		break;
	case CAM_CB_DCAM_STATIS_DONE:
		if (!IS_VALID_PORT_ID(cur_port_id)) {
			pr_err("fail to get valid cur port id %d\n", cur_port_id);
			return -EFAULT;
		}
		if (node->node_graph->outport[cur_port_id].dump_en) {
			pframe->common.dump_en = CAM_ENABLE;
			pframe->common.dump_node_id = node->node_graph->outport[cur_port_id].dump_node_id;
		}
		break;
	case CAM_CB_ISP_RET_SRC_BUF:
	case CAM_CB_DCAM_RET_SRC_BUF:
	case CAM_CB_FRAME_CACHE_RET_SRC_BUF:
	case CAM_CB_PYRDEC_RET_SRC_BUF:
	case CAM_CB_ISP_SCALE_RET_ISP_BUF:
	case CAM_CB_ICAP_COPY_SRC_BUFFER:
		if (!IS_VALID_PORT_ID(cur_to_port_id)) {
			pr_err("fail to get valid cur port id %d\n", cur_to_port_id);
			return -EFAULT;
		}
		CAM_QUEUE_FRAME_FLAG_RESET(&pframe->common);
		if (node->node_graph->inport[cur_to_port_id].to_user_en) {
			pframe->common.link_to.node_type = CAM_NODE_TYPE_USER;
			type = CAM_CB_USER_BUF_DONE;
		} else
			pframe->common.link_to = pframe->common.link_from;
		if (node->node_graph->inport[cur_to_port_id].copy_en) {
			pframe->common.copy_en = 1;
			pframe->common.copy_node_id = node->node_graph->inport[cur_to_port_id].copy_node_id;
		}
		break;
	default :
		break;
	}
exit:
	if (node->data_cb_func)
		node->data_cb_func(type, param, node->data_cb_handle);
	return ret;
}

static int camnode_port_cfg_callback(void *param, uint32_t cmd, void *cb_handle)
{
	int ret = 0, i = 0;
	struct cam_node *node = NULL;
	struct cam_port *port = NULL;
	struct cam_node_cfg_param *node_param = NULL;

	if (!param || !cb_handle) {
		pr_err("fail to get valid param %p %p\n", param, cb_handle);
		return -EFAULT;
	}

	node = (struct cam_node *)cb_handle;
	switch (cmd) {
	case PORT_CFG_PARAM_GET:
		for (i = 0; i < CAM_NODE_PORT_OUT_NUM; i++) {
			port = node->outport_list[i];
			if (!port || port->port_graph->link_state != PORT_LINK_NORMAL)
				continue;
			ret = port->ops.cfg_param(port, cmd, param);
			if (ret) {
				pr_err("fail to cfg port %d\n", i);
				goto exit;
			}
		}
		break;
	case PORT_CFG_BUFFER_SET:
	case PORT_CFG_BUFFER_ALLOC:
		node_param = (struct cam_node_cfg_param *)param;
		port = node->outport_list[node_param->port_id];
		if (!port) {
			pr_err("fail to get valid port id %d\n", node_param->port_id);
			goto exit;
		}
		ret = port->ops.cfg_param(port, cmd, node_param->param);
		if (ret) {
			pr_err("fail to cfg port %d\n", node_param->port_id);
			goto exit;
		}
		break;
	case PORT_CFG_BUFFER_GET:
	case PORT_CFG_BUFFER_CYCLE:
		node_param = (struct cam_node_cfg_param *)param;
		port = node->outport_list[node_param->port_id];
		if (!port) {
			pr_err("fail to get valid port id %d\n", node_param->port_id);
			ret = -EFAULT;
			goto exit;
		}
		ret = port->ops.cfg_param(port, cmd, &node_param->param);
		if (ret) {
			pr_err("fail to cfg port %d\n", node_param->port_id);
			goto exit;
		}
		break;
	default :
		pr_err("fail to support cfg cmd %d\n", cmd);
		ret = -EFAULT;
		break;
	}

exit:
	return ret;
}

const char *cam_node_name_get(enum cam_node_type type)
{
	return IS_CAM_NODE(type) ? CAM_NODE_NAMES[type] : "(null)";
}

int cam_node_work_state_get(struct cam_node_topology *param, uint32_t g_dump_en, uint32_t *g_replace_en)
{
	int is_work = 0;

	if (!param) {
		pr_err("fail to get invalid nodedesc info\n");
		return 0;
	}

	switch (param->state) {
	case CAM_NODE_STATE_WORK:
		is_work = 1;
		break;
	case CAM_NODE_STATE_IDLE:
		if (param->type == CAM_NODE_TYPE_DUMP && g_dump_en) {
			is_work = 1;
			g_dump_en = 0;
		}
		if (param->type == CAM_NODE_TYPE_REPLACE && *g_replace_en) {
			is_work = 1;
			*g_replace_en = 0;
		}
		break;
	default :
		pr_err("fail to get valid state %d\n", param->state);
		break;
	}
	return is_work;
};

int cam_node_buffer_alloc(void *handle, struct cam_buf_alloc_desc *param)
{
	int ret = 0, i = 0;
	struct pyr_dec_node *dec_node = NULL;
	struct cam_node *node = (struct cam_node *)handle;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	switch (node->node_graph->type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
		for (i = 0; i < CAM_NODE_PORT_OUT_NUM; i++) {
			if (!node->outport_list[i])
				continue;
			ret = cam_port_buffers_alloc(node->outport_list[i], node->node_graph->id, param);
			if (ret) {
				pr_err("fail to alloc buf %d\n", i);
			}
		}
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		dec_node = (struct pyr_dec_node *)node->handle;
		if (param->share_buffer) {
			mutex_lock(&dec_node->dev->pyr_mulshare_dec_lock);
			if (atomic_inc_return(&dec_node->dev->pyr_mulshare_dec_alloced) > 1) {
				mutex_unlock(&dec_node->dev->pyr_mulshare_dec_lock);
				goto exit;
			}
		}
		for (i = 0; i < CAM_NODE_PORT_OUT_NUM; i++) {
			if (!node->outport_list[i])
				continue;
			ret = cam_port_buffers_alloc(node->outport_list[i], node->node_graph->id, param);
			if (ret)
				pr_err("fail to alloc buf %d\n", i);
		}
		if (param->share_buffer)
			mutex_unlock(&dec_node->dev->pyr_mulshare_dec_lock);
		break;
	case CAM_NODE_TYPE_DATA_COPY:
		if (param->cam_copy_buf_alloc_num)
			ret = cam_copy_node_buffers_alloc(node->handle, param);
		if (ret)
			pr_err("fail to alloc copy node buf %s\n", cam_node_name_get(node->node_graph->type));
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		ret = isp_node_buffers_alloc(node->handle, param);
		if (ret)
			pr_err("fail to alloc isp node buf %s\n", cam_node_name_get(node->node_graph->type));
		break;
	default:
		pr_debug("not support node %s\n", cam_node_name_get(node->node_graph->type));
		break;
	}

exit:
	return ret;
}

int cam_node_static_nodelist_get(struct cam_node_topology *param, uint32_t type)
{
	int ret = 0;
	struct cam_port_topology *cur_inport = NULL, *cur_outport = NULL;

	if (!param) {
		pr_err("fail to get invalid param ptr\n");
		return -EFAULT;
	}

	param->type = type;
	param->state = CAM_NODE_STATE_WORK;
	if (type == CAM_NODE_TYPE_DUMP || type == CAM_NODE_TYPE_REPLACE)
		param->state = CAM_NODE_STATE_IDLE;
	cur_inport = &param->inport[0];
	cur_outport = &param->outport[0];
	cam_port_static_portlist_get(cur_inport, type, PORT_TRANSFER_IN);
	cam_port_static_portlist_get(cur_outport, type, PORT_TRANSFER_OUT);

	return ret;
}

void *cam_node_creat(struct cam_node_desc *param)
{
	int i = 0;
	struct cam_node *node = NULL;
	struct cam_port_desc port_desc = {0};
	struct cam_nodes_dev *nodes_dev = NULL;

	if (!param) {
		pr_err("fail to get invalid nodedesc info\n");
		return NULL;
	}

	node = cam_buf_kernel_sys_vzalloc(sizeof(struct cam_node));
	if (!node) {
		pr_err("fail to alloc cam node\n");
		return NULL;
	}

	node->node_graph = param->node_graph;
	nodes_dev = (struct cam_nodes_dev *)param->nodes_dev;
	if (!node->node_graph || !nodes_dev) {
		pr_err("fail to get valid node graph %p\n", nodes_dev);
		cam_buf_kernel_sys_vfree(node);
		return NULL;
	}

	pr_info("node: %s start creat\n", cam_node_name_get(node->node_graph->type));
	node->need_fetch = param->dcam_fetch_desc->virtualsensor;
	node->data_cb_handle = param->data_cb_handle;
	node->buf_manager_handle = param->buf_manager_handle;
	node->data_cb_func = param->data_cb_func;
	node->zoom_cb_func = param->zoom_cb_func;
	node->ops.cfg_node_param = camnode_cfg_node_param;
	node->ops.cfg_port_param = camnode_cfg_port_param;
	node->ops.request_proc = camnode_request_proc;
	node->ops.stop_proc = camnode_stop_proc;
	node->ops.outbuf_back = camnode_outbuf_back;
	node->ops.set_shutoff = camnode_set_shutoff;
	node->ops.cfg_shutoff = camnode_cfg_shutoff;
	node->ops.cfg_shutoff(node, CAM_NODE_SHUTOFF_INIT, NULL);
	switch (node->node_graph->type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		param->dcam_online_desc->data_cb_func = camnode_data_callback;
		param->dcam_online_desc->data_cb_handle = node;
		param->dcam_online_desc->buf_manager_handle = node->buf_manager_handle;
		param->dcam_online_desc->port_cfg_cb_func = camnode_port_cfg_callback;
		param->dcam_online_desc->port_cfg_cb_handle = node;
		param->dcam_online_desc->shutoff_cfg_cb_func = camnode_cfg_shutoff;
		param->dcam_online_desc->shutoff_cfg_cb_handle = node;
		param->dcam_online_desc->node_dev = (void *)&nodes_dev->dcam_online_node_dev;
		param->dcam_online_desc->node_type = node->node_graph->type;
		if (param->dcam_fetch_desc->virtualsensor) {
			param->dcam_fetch_desc->online_node_desc = param->dcam_online_desc;
			param->dcam_fetch_desc->node_dev = (void *)&nodes_dev->dcam_fetch_node_dev;
			node->handle = dcam_fetch_node_get(node->node_graph->id, param->dcam_fetch_desc);
		} else
			node->handle = dcam_online_node_get(node->node_graph->id, param->dcam_online_desc);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
		param->dcam_offline_desc->data_cb_func = camnode_data_callback;
		param->dcam_offline_desc->data_cb_handle = node;
		param->dcam_offline_desc->buf_manager_handle = node->buf_manager_handle;
		param->dcam_offline_desc->port_cfg_cb_func = camnode_port_cfg_callback;
		param->dcam_offline_desc->port_cfg_cb_handle = node;
		param->dcam_offline_desc->node_dev = (void *)&nodes_dev->dcam_offline_node_dev;
		param->dcam_offline_desc->node_type = node->node_graph->type;
		node->node_graph->buf_type = param->dcam_offline_desc->buf_type;
		node->handle = dcam_offline_node_get(node->node_graph->id, param->dcam_offline_desc);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
		param->dcam_offline_bpcraw_desc->data_cb_func = camnode_data_callback;
		param->dcam_offline_bpcraw_desc->data_cb_handle = node;
		param->dcam_offline_bpcraw_desc->buf_manager_handle = node->buf_manager_handle;
		param->dcam_offline_bpcraw_desc->port_cfg_cb_func = camnode_port_cfg_callback;
		param->dcam_offline_bpcraw_desc->port_cfg_cb_handle = node;
		param->dcam_offline_bpcraw_desc->node_dev = (void *)&nodes_dev->dcam_offline_node_bpcraw_dev;
		param->dcam_offline_bpcraw_desc->node_type = node->node_graph->type;
		node->handle = dcam_offline_node_get(node->node_graph->id, param->dcam_offline_bpcraw_desc);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
		param->dcam_offline_lscraw_desc->data_cb_func = camnode_data_callback;
		param->dcam_offline_lscraw_desc->data_cb_handle = node;
		param->dcam_offline_lscraw_desc->buf_manager_handle = node->buf_manager_handle;
		param->dcam_offline_lscraw_desc->port_cfg_cb_func = camnode_port_cfg_callback;
		param->dcam_offline_lscraw_desc->port_cfg_cb_handle = node;
		param->dcam_offline_lscraw_desc->node_dev = (void *)&nodes_dev->dcam_offline_node_lscraw_dev;
		param->dcam_offline_lscraw_desc->node_type = node->node_graph->type;
		node->handle = dcam_offline_node_get(node->node_graph->id, param->dcam_offline_lscraw_desc);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
		param->dcam_offline_raw2frgb_desc->data_cb_func = camnode_data_callback;
		param->dcam_offline_raw2frgb_desc->data_cb_handle = node;
		param->dcam_offline_raw2frgb_desc->buf_manager_handle = node->buf_manager_handle;
		param->dcam_offline_raw2frgb_desc->port_cfg_cb_func = camnode_port_cfg_callback;
		param->dcam_offline_raw2frgb_desc->port_cfg_cb_handle = node;
		param->dcam_offline_raw2frgb_desc->node_dev = (void *)&nodes_dev->dcam_offline_node_raw2frgb_dev;
		param->dcam_offline_raw2frgb_desc->node_type = node->node_graph->type;
		node->handle = dcam_offline_node_get(node->node_graph->id, param->dcam_offline_raw2frgb_desc);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		param->dcam_offline_frgb2yuv_desc->data_cb_func = camnode_data_callback;
		param->dcam_offline_frgb2yuv_desc->data_cb_handle = node;
		param->dcam_offline_frgb2yuv_desc->buf_manager_handle = node->buf_manager_handle;
		param->dcam_offline_frgb2yuv_desc->port_cfg_cb_func = camnode_port_cfg_callback;
		param->dcam_offline_frgb2yuv_desc->port_cfg_cb_handle = node;
		param->dcam_offline_frgb2yuv_desc->node_dev = (void *)&nodes_dev->dcam_offline_node_frgb2yuv_dev;
		param->dcam_offline_frgb2yuv_desc->node_type = node->node_graph->type;
		node->handle = dcam_offline_node_get(node->node_graph->id, param->dcam_offline_frgb2yuv_desc);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		param->isp_node_description->data_cb_func = camnode_data_callback;
		param->isp_node_description->data_cb_handle = node;
		param->isp_node_description->buf_manager_handle = node->buf_manager_handle;
		param->isp_node_description->node_dev = (void *)&nodes_dev->isp_node_dev[node->node_graph->id];
		param->isp_node_description->node_type = node->node_graph->type;
		node->handle = isp_node_get(node->node_graph->id, param->isp_node_description);
		break;
	case CAM_NODE_TYPE_ISP_YUV_SCALER:
		param->isp_yuv_scaler_desc->data_cb_func = camnode_data_callback;
		param->isp_yuv_scaler_desc->data_cb_handle = node;
		param->isp_yuv_scaler_desc->buf_manager_handle = node->buf_manager_handle;
		param->isp_yuv_scaler_desc->node_dev = (void *)&nodes_dev->isp_yuv_scaler_node_dev[node->node_graph->id];
		param->isp_yuv_scaler_desc->node_type = node->node_graph->type;
		node->handle = isp_yuv_scaler_node_get(node->node_graph->id, param->isp_yuv_scaler_desc);
		break;
	case CAM_NODE_TYPE_FRAME_CACHE:
		param->frame_cache_desc->data_cb_func = camnode_data_callback;
		param->frame_cache_desc->data_cb_handle = node;
		param->frame_cache_desc->buf_manager_handle = node->buf_manager_handle;
		param->frame_cache_desc->node_type = node->node_graph->type;
		node->handle = frame_cache_node_get(node->node_graph->id, param->frame_cache_desc);
		break;
	case CAM_NODE_TYPE_DUMP:
		node->handle = cam_dump_node_get(node->node_graph->id, camnode_data_callback, node);
		break;
	case CAM_NODE_TYPE_REPLACE:
		node->handle = cam_replace_node_get(node->node_graph->id, camnode_data_callback, node);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		param->pyr_dec_desc->data_cb_func = camnode_data_callback;
		param->pyr_dec_desc->data_cb_handle = node;
		param->pyr_dec_desc->buf_manager_handle = node->buf_manager_handle;
		param->pyr_dec_desc->port_cfg_cb_func = camnode_port_cfg_callback;
		param->pyr_dec_desc->port_cfg_cb_handle = node;
		param->pyr_dec_desc->node_dev = (void *)&nodes_dev->pyr_dec_node_dev[node->node_graph->id];
		node->handle = pyr_dec_node_get(node->node_graph->id, param->pyr_dec_desc);
		break;
	case CAM_NODE_TYPE_DATA_COPY:
		param->cam_copy_desc->copy_cb_func = camnode_data_callback;
		param->cam_copy_desc->copy_cb_handle = node;
		node->handle = cam_copy_node_get(node->node_graph->id, param->cam_copy_desc);
		break;
	default:
		pr_err("fail to support node type %s\n", cam_node_name_get(node->node_graph->type));
		break;
	}

	if (!node->handle) {
		pr_err("fail to get valid node handle\n");
		goto exit;
	}

	port_desc.isp_offline = &param->isp_node_description->port_desc;
	port_desc.dcam_offline = &param->dcam_offline_desc->port_desc;
	port_desc.dcam_offline_bpcraw = &param->dcam_offline_bpcraw_desc->port_desc;
	port_desc.dcam_offline_lscraw = &param->dcam_offline_lscraw_desc->port_desc;
	port_desc.dcam_offline_raw2frgb = &param->dcam_offline_raw2frgb_desc->port_desc;
	port_desc.dcam_offline_frgb2yuv = &param->dcam_offline_frgb2yuv_desc->port_desc;
	port_desc.isp_offline_scaler = &param->isp_yuv_scaler_desc->port_desc;
	port_desc.pyr_dec = &param->pyr_dec_desc->port_desc;
	port_desc.nodes_dev = param->nodes_dev;
	port_desc.zoom_cb_func = camnode_zoom_callback;
	port_desc.zoom_cb_handle = node;
	port_desc.data_cb_func = camnode_data_callback;
	port_desc.data_cb_handle = node;
	port_desc.buf_manager_handle = node->buf_manager_handle;
	port_desc.shutoff_cb_func = camnode_shutoff_callback;
	port_desc.shutoff_cb_handle = node;

	for (i = 0; i < CAM_NODE_PORT_IN_NUM; i++) {
		struct cam_node_cfg_param cfg_param = {0};
		port_desc.port_graph = &param->node_graph->inport[i];
		if (port_desc.port_graph->link_state != PORT_LINK_NORMAL)
			continue;
		node->inport_list[i] = cam_port_creat(&port_desc, node->node_graph->id);
		if (!node->inport_list[i]) {
			pr_err("fail to creat inport %d\n", i);
			goto exit;
		}
		cfg_param.param = node->inport_list[i]->handle;
		node->ops.cfg_node_param(node, CAM_NODE_CFG_INSERT_PORT, &cfg_param);
	}
	for (i = 0; i < CAM_NODE_PORT_OUT_NUM; i++) {
		struct cam_node_cfg_param cfg_param = {0};
		port_desc.port_graph = &param->node_graph->outport[i];
		port_desc.dcam_online = &param->dcam_online_desc->port_desc[i];
		if (port_desc.port_graph->link_state != PORT_LINK_NORMAL) {
			if (port_desc.dcam_online->update_state &&
				param->node_graph->type == CAM_NODE_TYPE_DCAM_ONLINE) {
				port_desc.port_graph->link_state = PORT_LINK_NORMAL;
				port_desc.dcam_online->update_state = 0;
			}
			continue;
		}
		port_desc.dcam_online->dev = param->dcam_online_desc->dev;
		if (((param->type == CAM_PIPELINE_PREVIEW)
			|| (param->type == CAM_PIPELINE_VIDEO)) && (i == PORT_FULL_OUT)) {
			pr_warn("Warning: preview or video pipeline not get full port\n");
			continue;
		}
		node->outport_list[i] = cam_port_creat(&port_desc, node->node_graph->id);
		if (!node->outport_list[i]) {
			pr_err("fail to creat outport %d\n", i);
			goto exit;
		}
		cfg_param.param = node->outport_list[i]->handle;
		node->ops.cfg_node_param(node, CAM_NODE_CFG_INSERT_PORT, &cfg_param);
	}

	return node;

exit:
	cam_node_destory(node);
	return NULL;
}

void cam_node_close(struct cam_node *node)
{
	if (!node) {
		pr_err("fail to get valid node\n");
		return;
	}

	switch (node->node_graph->type) {
	case CAM_NODE_TYPE_ISP_OFFLINE:
		isp_node_close(node->handle);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		pyr_dec_node_close(node->handle);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE:
		dcam_offline_node_close(node->handle);
		break;
	default:
		break;
	}
}

void cam_node_destory(struct cam_node *node)
{
	int i = 0;

	if (!node) {
		pr_err("fail to get valid node\n");
		return;
	}

	pr_info("node: %s start destory\n", cam_node_name_get(node->node_graph->type));

	switch (node->node_graph->type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		if (node->need_fetch)
			dcam_fetch_node_put(node->handle);
		else
			dcam_online_node_put(node->handle);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW:
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		dcam_offline_node_put(node->handle);
		break;
	case CAM_NODE_TYPE_ISP_OFFLINE:
		isp_node_put(node->handle);
		break;
	case CAM_NODE_TYPE_ISP_YUV_SCALER:
		isp_yuv_scaler_node_put(node->handle);
		break;
	case CAM_NODE_TYPE_FRAME_CACHE:
		frame_cache_node_put(node->handle);
		break;
	case CAM_NODE_TYPE_DUMP:
		cam_dump_node_put(node->handle);
		break;
	case CAM_NODE_TYPE_REPLACE:
		cam_replace_node_put(node->handle);
		break;
	case CAM_NODE_TYPE_PYR_DEC:
		pyr_dec_node_put(node->handle);
		break;
	case CAM_NODE_TYPE_DATA_COPY:
		cam_copy_node_put(node->handle);
		break;
	default:
		pr_err("fail to support node type %d\n", node->node_graph->type);
		break;
	}
	node->handle = NULL;
	node->zoom_cb_func = NULL;
	node->data_cb_func = NULL;
	node->data_cb_handle = NULL;
	node->buf_manager_handle = NULL;

	for (i = CAM_NODE_PORT_OUT_NUM - 1; i >= 0; i--) {
		if (node->node_graph->outport[i].link_state != PORT_LINK_NORMAL)
			continue;
		if (!node->outport_list[i])
			continue;
		cam_port_destory(node->outport_list[i]);
		node->outport_list[i] = NULL;
	}

	for (i = CAM_NODE_PORT_IN_NUM - 1; i >= 0; i--) {
		if (node->node_graph->inport[i].link_state != PORT_LINK_NORMAL)
			continue;
		cam_port_destory(node->inport_list[i]);
		node->inport_list[i] = NULL;
	}
	node->node_graph = NULL;
	cam_buf_kernel_sys_vfree(node);
	node = NULL;
}
