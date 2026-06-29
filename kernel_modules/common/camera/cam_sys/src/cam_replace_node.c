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

#include "cam_dump_node.h"
#include "cam_node.h"
#include "cam_pipeline.h"
#include "cam_replace_node.h"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "CAM_REPLACE_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define CAM_REPLACE_NODE_PATH "/data/ylog/"
#define BYTE_PER_ONCE                   4096

static void camreplace_node_read_data_to_buf(uint8_t *buffer,
		ssize_t size, const char *file, loff_t offset)
{
	ssize_t result = 0, total = 0, read = 0;
	struct file *wfp = NULL;

	wfp = cam_kernel_adapt_filp_open(file, O_RDONLY, 0666);
	if (IS_ERR_OR_NULL(wfp)) {
		pr_err("fail to open file %s\n", file);
		return;
	}
	pr_debug("read image buf=%p, size=%d\n", buffer, (uint32_t)size);
	do {
		read = (BYTE_PER_ONCE < size) ? BYTE_PER_ONCE : size;
		result = cam_kernel_adapt_read(wfp, buffer, read, &offset);
		pr_debug("read result: %d, size: %d, pos: %d\n",
		(uint32_t)result,  (uint32_t)size, (uint32_t)offset);

		if (result > 0) {
			size -= result;
			buffer += result;
		}
		total += result;
	} while ((result > 0) && (size > 0));
	cam_kernel_adapt_filp_close(wfp, NULL);

	pr_debug("read image done, total=%d\n", (uint32_t)total);
}

static void camreplace_node_frame_size_get(struct cam_frame *frame, struct cam_replace_msg *msg, int cur_layer)
{
	if (cam_raw_fmt_get(frame->common.cam_fmt))
		msg->size = cal_sprd_pitch(frame->common.width, frame->common.cam_fmt) * frame->common.height;
	else if (frame->common.cam_fmt == CAM_FULL_RGB14)
		msg->size = frame->common.width * frame->common.height;
	else {
		if (cur_layer == 0)
			msg->size = cal_sprd_pitch(frame->common.width, frame->common.cam_fmt) * frame->common.height;
		else
			msg->size = cal_sprd_pitch(msg->align_w, frame->common.cam_fmt) * msg->align_h;
	}

	pr_debug("replace frame buf_size %zu cal_size %d fmt %s\n", frame->common.buf.size, msg->size, camport_fmt_name_get(frame->common.img_fmt));
}

static void camreplace_node_param_cfg(struct cam_replace_msg *msg, struct cam_frame *pframe)
{
	msg->offset = 0;
	msg->is_compressed = pframe->common.is_compressed;
	if (pframe->common.pyr_status) {
		if (pframe->common.link_from.node_type != CAM_NODE_TYPE_PYR_DEC)
			msg->layer_num = DCAM_PYR_DEC_LAYER_NUM;
		else
			msg->layer_num = ISP_PYR_DEC_LAYER_NUM;
		while (isp_rec_small_layer_w(pframe->common.width, msg->layer_num) < MIN_PYR_WIDTH
			|| isp_rec_small_layer_h(pframe->common.height, msg->layer_num) < MIN_PYR_HEIGHT) {
			if (--msg->layer_num == 0)
				break;
		}
		msg->align_w = isp_rec_layer0_width(pframe->common.width, msg->layer_num);
		msg->align_h = isp_rec_layer0_heigh(pframe->common.height, msg->layer_num);
	} else
		msg->layer_num = 0;
}

static void camreplace_node_frame_file_read(struct cam_frame *frame,
		struct cam_replace_msg *msg, uint8_t *name, uint8_t *name1)
{
	unsigned long  addr = 0;
	loff_t offset = 0;

	if (cam_buf_manager_buf_status_cfg(&frame->common.buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX)) {
		pr_err("fail to kmap replace buf\n");
		return;
	}

	addr = frame->common.buf.addr_k + msg->offset;
	if (frame->common.cam_fmt >= CAM_YUV_BASE && frame->common.cam_fmt <= CAM_YVU420_2FRAME_MIPI) {
		camreplace_node_read_data_to_buf((uint8_t *)addr, msg->size, name, offset);
		addr += msg->size;
		msg->size = msg->size / 2;
		camreplace_node_read_data_to_buf((uint8_t *)addr, msg->size, name1, offset);
		msg->offset += msg->size * 3;
	} else
		camreplace_node_read_data_to_buf((uint8_t *)addr, msg->size, name, offset);

	cam_buf_manager_buf_status_cfg(&frame->common.buf, CAM_BUF_STATUS_PUT_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
}

static int camreplace_node_compress_get(struct cam_frame *pframe, struct cam_replace_msg *msg, uint8_t* file_name)
{
	/* size of fbc header file */
	loff_t offset = sizeof(struct cam_dump_fbc_header);

	if (pframe->common.cam_fmt == CAM_RAW_14 || pframe->common.cam_fmt == CAM_RAW_HALFWORD_10)
		strcat(file_name, ".raw");
	else
		strcat(file_name, ".mipi_raw");

	if (cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX)) {
		pr_err("fail to kmap dump buf\n");
		return -EFAULT;
	}

	camreplace_node_read_data_to_buf((char *)pframe->common.buf.addr_k, pframe->common.fbc_info.buffer_size, file_name, offset);
	msg->offset += pframe->common.fbc_info.buffer_size;
	cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_PUT_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
	return 0;
}

int camreplace_node_image_name_get(struct cam_frame *frame, struct cam_replace_msg *msg,
	uint8_t *file_name, uint8_t *file_name1, int cur_layer)
{
	int ret = 0;
	uint8_t tmp_str[40] = { '\0' };

	strcat(file_name, CAM_REPLACE_NODE_PATH);
	snprintf(tmp_str, ARRAY_SIZE(tmp_str), "%d", cur_layer);
	strcat(file_name, tmp_str);
	strcat(file_name1, file_name);

	switch (g_dbg_replace_src) {
	case REPLACE_IMG_MIPIRAW:
		if (msg->is_compressed) {
			snprintf(tmp_str, ARRAY_SIZE(tmp_str), "_compress");
			strcat(file_name, tmp_str);
			camreplace_node_compress_get(frame, msg, file_name);
			return ret;
		}
		strcat(file_name, ".mipi_raw");
		break;
	case REPLACE_IMG_YUV:
		strcat(file_name, ".y");
		strcat(file_name1, ".uv");
		break;
	case REPLACE_IMG_RAW:
		strcat(file_name, ".raw");
		break;
	default:
		pr_err("fail to get valid replace type\n");
		return -EFAULT;
		break;
	}
	return ret;
}

static int camreplace_node_switch(struct cam_replace_node *node, struct cam_frame *pframe)
{
	int ret = 0;

	if (!node)
		return ret;

	/* Prev/video pipeline replace dcam online bin port need set g_dbg_replaceswitch,
			cap pipeline replace dcam online full bort need start capture. */
	if ((pframe->common.link_from.node_type == CAM_NODE_TYPE_DCAM_ONLINE)
		&& ((pframe->common.link_from.port_id == PORT_BIN_OUT && !g_dbg_replace_switch)
		|| (pframe->common.link_from.port_id == PORT_FULL_OUT && pframe->common.link_to.node_type == CAM_NODE_TYPE_DCAM_ONLINE)))
		ret = 0;
	else
		ret = pframe->common.replace_en;

	return ret;
}

static int camreplace_node_frame_start(void *param)
{
	int ret = 0, i = 0;
	struct cam_replace_msg msg  = {0};
	struct cam_replace_node *node = NULL;
	struct cam_frame *pframe = NULL;
	uint8_t file_name[256] = { '\0' };
	uint8_t file_name1[256] = { '\0' };

	node = (struct cam_replace_node *)param;
	if (!node) {
		pr_err("fail to get valid param\n");
		return -EFAULT;
	}

	pframe = CAM_QUEUE_DEQUEUE(&node->replace_queue, struct cam_frame, list);
	if (pframe == NULL) {
		pr_err("fail to get input frame for replace node %d\n", node->node_id);
		return -EFAULT;
	}

	if (camreplace_node_switch(node, pframe)) {
		camreplace_node_param_cfg(&msg, pframe);
		for (i = 0; i <= msg.layer_num; i++) {
			if (i > 0) {
				msg.is_compressed = 0;
				msg.align_w = msg.align_w / 2;
				msg.align_h = msg.align_h / 2;
			}
			camreplace_node_image_name_get(pframe, &msg, file_name, file_name1, i);
			if (msg.is_compressed == 0) {
				camreplace_node_frame_size_get(pframe, &msg, i);
				camreplace_node_frame_file_read(pframe, &msg, file_name, file_name1);
			}
			memset(file_name, 0, sizeof(file_name));
			memset(file_name1, 0, sizeof(file_name1));
		}
	}
	pframe->common.replace_en = CAM_DISABLE;
	pr_debug("cam replace node %d frame start\n", node->node_id);
	node->replace_cb_func(CAM_CB_REPLACE_DATA_DONE, pframe, node->replace_cb_handle);

	return ret;
}

int cam_replace_node_request_proc(struct cam_replace_node *node, void *param)
{
	int ret = 0;
	struct cam_node_cfg_param *node_param = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_pipeline *pipeline = NULL;
	struct cam_node *cam_node = NULL;

	if (!node || !param) {
		pr_err("fail to get valid param %p %p\n", node, param);
		return -EFAULT;
	}

	node_param = (struct cam_node_cfg_param *)param;
	pframe = (struct cam_frame *)node_param->param;
	pframe->common.priv_data = node;
	cam_node = (struct cam_node *)node->replace_cb_handle;
	pipeline = (struct cam_pipeline *)cam_node->data_cb_handle;

	if (pipeline->debug_log_switch)
		pr_info("pipeline_type %s, fid %d, ch_id %d, buf %x, w %d, h %d, pframe->common.is_reserved %d, compress_en %d\n",
			pipeline->pipeline_graph->name, pframe->common.fid, pframe->common.channel_id, pframe->common.buf.mfd,
			pframe->common.width, pframe->common.height, pframe->common.is_reserved, pframe->common.is_compressed);

	ret = CAM_QUEUE_ENQUEUE(&node->replace_queue, &pframe->list);
	if (ret == 0)
		complete(&node->thread.thread_com);
	else
		pr_err("fail to enqueue cam replace %d frame\n", node->node_id);

	return ret;
}

void cam_replace_node_set(void *param)
{
	uint32_t val = 0;
	int i = 0;
	uint8_t replace_cmd[REPLACE_CMD_MAX];

	if (!param) {
		pr_err("fail to get replace info\n");
		return;
	}

	val = *(uint32_t *)param;
	for (i = 0; i < 3; i++) {
		replace_cmd[i] = val % 10;
		val = val /10;
	}
	replace_cmd[REPLACE_CMD_PIPE_TYPE] = val;
	if (replace_cmd[REPLACE_CMD_EN]) {
		g_dbg_replace.replace_en = replace_cmd[REPLACE_CMD_EN];
		g_dbg_replace.replace_port_id = replace_cmd[REPLACE_CMD_PORT_ID];
		g_dbg_replace.replace_node_type = replace_cmd[REPLACE_CMD_NODE_TYPE];
		g_dbg_replace.replace_pipeline_type = replace_cmd[REPLACE_CMD_PIPE_TYPE];
		g_dbg_replace.replace_ongoing = 1;
		pr_info("replace pipeline %u node %u port id %u en %u\n",
			g_dbg_replace.replace_pipeline_type, g_dbg_replace.replace_node_type,
			g_dbg_replace.replace_port_id, g_dbg_replace.replace_en);
	} else {
		if ((g_dbg_replace.replace_pipeline_type == replace_cmd[REPLACE_CMD_PIPE_TYPE])
			&& (g_dbg_replace.replace_node_type == replace_cmd[REPLACE_CMD_NODE_TYPE])
			&& (g_dbg_replace.replace_port_id == replace_cmd[REPLACE_CMD_PORT_ID])) {
			g_dbg_replace.replace_en = replace_cmd[REPLACE_CMD_EN];
			pr_info("replace pipeline %u node %u port id %u en %u\n",
				g_dbg_replace.replace_pipeline_type, g_dbg_replace.replace_node_type,
				g_dbg_replace.replace_port_id, g_dbg_replace.replace_en);
		}
	}
}

void *cam_replace_node_get(uint32_t node_id, cam_data_cb cb_func, void *priv_data)
{
	int ret = 0;
	struct cam_replace_node *node = NULL;
	struct cam_thread_info *thrd = NULL;

	if (!cb_func || !priv_data) {
		pr_err("fail to get valid ptr %p %p\n", cb_func, priv_data);
		return NULL;
	}

	node = cam_buf_kernel_sys_vzalloc(sizeof(struct cam_replace_node));
	if (!node) {
		pr_err("fail to get valid cam replace node\n");
		return NULL;
	}

	if (!node->replace_cb_func) {
		node->replace_cb_func = cb_func;
		node->replace_cb_handle = priv_data;
	}

	CAM_QUEUE_INIT(&node->replace_queue, REPLACE_NODE_Q_LEN, cam_queue_empty_frame_put);
	init_completion(&node->replace_com);
	node->node_id = node_id;

	thrd = &node->thread;
	snprintf(thrd->thread_name, ARRAY_SIZE(thrd->thread_name), "cam_replace_node%d", node->node_id);
	ret = camthread_create(node, thrd, camreplace_node_frame_start);
	if (unlikely(ret != 0)) {
		pr_err("fail to create cam replace node%d thread\n", node->node_id);
		return NULL;
	}

	pr_info("cam replace node %d get\n", node->node_id);

	return node;
}

void cam_replace_node_put(struct cam_replace_node *node)
{
	if (!node) {
		pr_err("fail to get invalid node ptr\n");
		return;
	}

	camthread_stop(&node->thread);
	CAM_QUEUE_CLEAN(&node->replace_queue, struct cam_frame, list);
	node->replace_cb_func = NULL;
	node->replace_cb_handle = NULL;
	pr_info("cam replace node %d put\n", node->node_id);
	cam_buf_kernel_sys_vfree(node);
	node = NULL;
}

