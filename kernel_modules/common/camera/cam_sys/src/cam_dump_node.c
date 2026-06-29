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

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "CAM_DUMP_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__
#define IS_STATIS_PORT(port) (((port) > PORT_FULL_OUT) && ((port) < PORT_DCAM_OUT_MAX))
#define CAM_DUMP_NUM(n, m, k) ((n >> m) & (0xff >> k))

#define CAM_DUMP_NODE_PATH "/data/ylog/"
#define BYTE_PER_ONCE                   4096

static void camdump_node_write_data_to_file(uint8_t *buffer,
		ssize_t size, const char *file)
{
	ssize_t result = 0, total = 0, writ = 0;
	struct file *wfp = NULL;

	wfp = cam_kernel_adapt_filp_open(file, O_CREAT | O_RDWR | O_APPEND, 0666);
	if (IS_ERR_OR_NULL(wfp)) {
		pr_err("fail to open file %s\n", file);
		return;
	}
	pr_debug("write image buf=%p, size=%d\n", buffer, (uint32_t)size);
	do {
		writ = (BYTE_PER_ONCE < size) ? BYTE_PER_ONCE : size;
		result = cam_kernel_adapt_write(wfp, buffer, writ, &wfp->f_pos);
		pr_debug("write result: %d, size: %d, pos: %d\n",
		(uint32_t)result,  (uint32_t)size, (uint32_t)wfp->f_pos);

		if (result > 0) {
			size -= result;
			buffer += result;
		}
		total += result;
	} while ((result > 0) && (size > 0));
	cam_kernel_adapt_filp_close(wfp, NULL);
	pr_debug("write image done, total=%d\n", (uint32_t)total);
}

static int camdump_node_format_name_get(struct cam_dump_msg *msg, struct camera_frame *frame, uint8_t *file_name,
	uint8_t *file_name1, uint32_t format)
{
	int data_bits = 0, is_pack = 0, ret = 0;

	data_bits = cam_data_bits(format);
	is_pack = cam_is_pack(format);

	if (frame->link_from.port_id == PORT_PDAF_OUT) {
		strcat(file_name1, file_name);
		strcat(file_name, "_left.yuv");
		strcat(file_name1, "_right.yuv");
		return 0;
	}

	if (IS_STATIS_PORT(frame->link_from.port_id)) {
		strcat(file_name, ".yuv");
		return 0;
	}

	switch(format) {
	case CAM_RAW_PACK_10:
	case CAM_RAW_8:
		strcat(file_name, ".mipi_raw");
		break;
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		strcat(file_name, ".raw");
		break;
	case CAM_FULL_RGB14:
		strcat(file_name, ".rgb");
		break;
	case CAM_YUV420_2FRAME:
	case CAM_YUV420_2FRAME_10:
	case CAM_YUV420_2FRAME_MIPI:
		if (data_bits == CAM_8_BITS)
			strcat(file_name, "_8bit");
		else {
			strcat(file_name, "_10bit");
			if (is_pack)
				strcat(file_name, "_mipi");
		}
		strcat(file_name1, file_name);
		strcat(file_name, "_yuv420.y");
		strcat(file_name1, "_yuv420.uv");
		break;
	case CAM_YVU420_2FRAME:
	case CAM_YVU420_2FRAME_10:
	case CAM_YVU420_2FRAME_MIPI:
		if (data_bits == CAM_8_BITS)
			strcat(file_name, "_8bit");
		else {
			strcat(file_name, "_10bit");
			if (is_pack)
				strcat(file_name, "_mipi");
		}
		strcat(file_name1, file_name);
		strcat(file_name, "_yvu420.y");
		strcat(file_name1, "_yvu420.uv");
		break;
	default:
		break;
	}

	return ret;
}

static void camdump_node_port_type_get(uint8_t *type_name, struct camera_frame *frame)
{
	uint8_t tmp_str[40] = { '\0' };

	snprintf(tmp_str, ARRAY_SIZE(tmp_str), "%s_", cam_node_name_get(frame->link_from.node_type));
	strcat(type_name, tmp_str);
	switch (frame->link_from.node_type) {
	case CAM_NODE_TYPE_DCAM_ONLINE:
		snprintf(tmp_str, ARRAY_SIZE(tmp_str), "%s_", cam_port_name_get(frame->link_from.port_id));
		strcat(type_name, tmp_str);
		break;
	case CAM_NODE_TYPE_DCAM_OFFLINE:
	case CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB:
	case CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV:
		snprintf(tmp_str, ARRAY_SIZE(tmp_str), "%s_", cam_port_dcam_offline_out_id_name_get(frame->link_from.port_id));
		strcat(type_name, tmp_str);
		break;
	default:
		break;
	}
}

static int camdump_node_compress_set(struct camera_frame *pframe, struct cam_dump_msg *msg, uint8_t* file_name)
{
	struct cam_dump_fbc_header fbc_hdr = {0};
	uint8_t superblock_layout = 5, file_message = 2;
	uint8_t tiled = 0, yuv_transform = 0, block_split = 0, left_crop = 0, top_crop = 0;
	uint8_t ncomponents[2];

	if (pframe->cam_fmt == CAM_RAW_14 || pframe->cam_fmt == CAM_RAW_HALFWORD_10)
		strcat(file_name, ".raw");
	else
		strcat(file_name, ".mipi_raw");
	/* The fbd tool need to configure fbc_hdr.The fbc_hdr setting from Algorithm team. */
	ncomponents[0] = 1;
	ncomponents[1] = 2;
	fbc_hdr.yuv_mirror_en = 0;
	fbc_hdr.yuv_format = DUMP_AFBC_Y_UV;
	fbc_hdr.endian = 0;
	fbc_hdr.bits = cam_data_bits(pframe->cam_fmt);
	fbc_hdr.img_h = pframe->height;
	fbc_hdr.img_w = pframe->width;
	fbc_hdr.img_h_pad = (pframe->height + AFBC_PADDING_H_YUV420_scaler - 1) / AFBC_PADDING_H_YUV420_scaler * AFBC_PADDING_H_YUV420_scaler;
	fbc_hdr.img_w_pad = (pframe->width  + AFBC_PADDING_W_YUV420_scaler - 1) / AFBC_PADDING_W_YUV420_scaler * AFBC_PADDING_W_YUV420_scaler;
	fbc_hdr.fbc_buffer_size = pframe->fbc_info.buffer_size;
	fbc_hdr.fbc_hdr_buffer[0] = 'A';
	fbc_hdr.fbc_hdr_buffer[1] = 'F';
	fbc_hdr.fbc_hdr_buffer[2] = 'B';
	fbc_hdr.fbc_hdr_buffer[3] = 'C';
	fbc_hdr.fbc_hdr_buffer[4] = AFBC_FILEHEADER_SIZE;
	fbc_hdr.fbc_hdr_buffer[5] = 0x00;
	fbc_hdr.fbc_hdr_buffer[6] = AFBC_VERSION;
	fbc_hdr.fbc_hdr_buffer[7] = 0x00;
	memcpy(&fbc_hdr.fbc_hdr_buffer[8], &(fbc_hdr.fbc_buffer_size), sizeof(fbc_hdr.fbc_buffer_size));
	fbc_hdr.fbc_hdr_buffer[12] = ncomponents[0] + ncomponents[1];
	fbc_hdr.fbc_hdr_buffer[13] = superblock_layout;
	fbc_hdr.fbc_hdr_buffer[14] = yuv_transform;
	fbc_hdr.fbc_hdr_buffer[15] = block_split;
	fbc_hdr.fbc_hdr_buffer[16] = fbc_hdr.bits & 0xff;
	fbc_hdr.fbc_hdr_buffer[17] = fbc_hdr.bits & 0xff;
	fbc_hdr.fbc_hdr_buffer[18] = fbc_hdr.bits & 0xff;
	fbc_hdr.fbc_hdr_buffer[19] = 0x00;
	fbc_hdr.fbc_hdr_buffer[20] = pframe->fbc_info.tile_col & 0xff;
	fbc_hdr.fbc_hdr_buffer[21] = (pframe->fbc_info.tile_col >> 8) & 0xff;
	fbc_hdr.fbc_hdr_buffer[22] = pframe->fbc_info.tile_row & 0xff;
	fbc_hdr.fbc_hdr_buffer[23] = (pframe->fbc_info.tile_row >> 8) & 0xff;
	fbc_hdr.fbc_hdr_buffer[24] = fbc_hdr.img_w_pad & 0xff;
	fbc_hdr.fbc_hdr_buffer[25] = (fbc_hdr.img_w_pad >> 8) & 0xff;
	fbc_hdr.fbc_hdr_buffer[26] = fbc_hdr.img_h_pad & 0xff;
	fbc_hdr.fbc_hdr_buffer[27] = (fbc_hdr.img_h_pad >> 8) & 0xff;
	fbc_hdr.fbc_hdr_buffer[28] = left_crop;
	fbc_hdr.fbc_hdr_buffer[29] = top_crop;
	fbc_hdr.fbc_hdr_buffer[30] = tiled;
	fbc_hdr.fbc_hdr_buffer[31] = file_message;

	if (cam_buf_manager_buf_status_cfg(&pframe->buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX)) {
		pr_err("fail to kmap dump buf\n");
		return -EFAULT;
	}

	camdump_node_write_data_to_file((char *)&fbc_hdr, sizeof(fbc_hdr), file_name);
	camdump_node_write_data_to_file((char *)pframe->buf.addr_k, pframe->fbc_info.buffer_size, file_name);
	msg->offset += pframe->fbc_info.buffer_size;
	cam_buf_manager_buf_status_cfg(&pframe->buf, CAM_BUF_STATUS_PUT_K_ADDR, CAM_BUF_IOMMUDEV_MAX);

	return 0;
}

static void camdump_node_frame_name_get(struct camera_frame *frame,
	struct cam_dump_msg *msg, uint8_t *file_name, uint8_t *file_name1, int cur_layer)
{
	uint8_t tmp_str[40] = { '\0' };

	strcat(file_name, CAM_DUMP_NODE_PATH);
	camdump_node_port_type_get(tmp_str, frame);
	strcat(file_name, tmp_str);

	if (cur_layer != 0) {
		snprintf(tmp_str, ARRAY_SIZE(tmp_str), "layer%d_", cur_layer);
		strcat(file_name, tmp_str);
	}
	snprintf(tmp_str, ARRAY_SIZE(tmp_str), "%d.", (uint32_t)frame->sensor_time.tv_sec);
	strcat(file_name, tmp_str);
	snprintf(tmp_str, ARRAY_SIZE(tmp_str), "%06d", (uint32_t)(frame->sensor_time.tv_usec));
	strcat(file_name, tmp_str);
	if (cur_layer == 0) {
		snprintf(tmp_str, ARRAY_SIZE(tmp_str), "_w%d", frame->width);
		strcat(file_name, tmp_str);
		snprintf(tmp_str, ARRAY_SIZE(tmp_str), "_h%d", frame->height);
		strcat(file_name, tmp_str);
	} else {
		snprintf(tmp_str, ARRAY_SIZE(tmp_str), "_w%d", msg->align_w);
		strcat(file_name, tmp_str);
		snprintf(tmp_str, ARRAY_SIZE(tmp_str), "_h%d", msg->align_h);
		strcat(file_name, tmp_str);
	}
	snprintf(tmp_str, ARRAY_SIZE(tmp_str), "_No%d", frame->fid);
	strcat(file_name, tmp_str);
	if (IS_STATIS_PORT(frame->link_from.port_id)) {
		camdump_node_format_name_get(msg, frame, file_name, file_name1, frame->cam_fmt);
		return;
	}
	if (msg->is_compressed) {
		snprintf(tmp_str, ARRAY_SIZE(tmp_str), "_compress");
		strcat(file_name, tmp_str);
		camdump_node_compress_set(frame, msg, file_name);
		return;
	}

	camdump_node_format_name_get(msg, frame, file_name, file_name1, frame->cam_fmt);

}

static void camdump_node_frame_size_get(struct camera_frame *frame, struct cam_dump_msg *msg, int cur_layer)
{
	uint32_t outpitch = 0;

	if (IS_STATIS_PORT(frame->link_from.port_id))
		msg->size = frame->buf.size;
	else if (cur_layer != 0) {
		outpitch = cal_sprd_pitch(msg->align_w, frame->cam_fmt);
		msg->size = outpitch * msg->align_h;
	} else {
		outpitch = cal_sprd_pitch(frame->width, frame->cam_fmt);
		msg->size = outpitch * frame->height;
	}

	pr_debug("dump frame buf_size %zu cal_size %d fmt %s\n",
		frame->buf.size, msg->size, camport_fmt_name_get(frame->cam_fmt));
}

static void camdump_node_frame_file_write(struct camera_frame *frame,
		struct cam_dump_msg *msg, uint8_t *name, uint8_t *name1)
{
	unsigned long addr = 0;

	if (frame->link_from.port_id != PORT_GTM_HIST_OUT) {
		if (cam_buf_kmap(&frame->buf)) {
			pr_err("fail to kmap dump buf\n");
			return;
		}
	}

	addr = frame->buf.addr_k + msg->offset;
	if (frame->cam_fmt >= CAM_YUV_BASE && frame->cam_fmt <= CAM_YVU420_2FRAME_MIPI && !IS_STATIS_PORT(frame->link_from.port_id)) {
		camdump_node_write_data_to_file((uint8_t *)addr, msg->size, name);
		addr += msg->size;
		msg->size = msg->size / 2;
		camdump_node_write_data_to_file((uint8_t *)addr, msg->size, name1);
		msg->offset += msg->size * 3;
	} else if (frame->link_from.port_id == PORT_PDAF_OUT) {
		addr = frame->buf.addr_k;
		msg->size = msg->size / 2;
		camdump_node_write_data_to_file((uint8_t *)addr, msg->size, name);
		addr += msg->size;
		camdump_node_write_data_to_file((uint8_t *)addr, msg->size, name1);
	} else
		camdump_node_write_data_to_file((uint8_t *)addr, msg->size, name);

	if (frame->link_from.port_id != PORT_GTM_HIST_OUT)
		cam_buf_kunmap(&frame->buf);
}

static void camdump_node_param_cfg(struct cam_dump_msg *msg, struct camera_frame *pframe)
{
	msg->offset = 0;
	msg->is_compressed = pframe->is_compressed;
	if (pframe->pyr_status) {
		if (pframe->link_from.node_type != CAM_NODE_TYPE_PYR_DEC)
			msg->layer_num = DCAM_PYR_DEC_LAYER_NUM;
		else
			msg->layer_num = ISP_PYR_DEC_LAYER_NUM;
		while (isp_rec_small_layer_w(pframe->width, msg->layer_num) < MIN_PYR_WIDTH
			|| isp_rec_small_layer_h(pframe->height, msg->layer_num) < MIN_PYR_HEIGHT) {
			if (--msg->layer_num == 0)
				break;
		}
		msg->align_w = isp_rec_layer0_width(pframe->width, msg->layer_num);
		msg->align_h = isp_rec_layer0_heigh(pframe->height, msg->layer_num);
	} else
		msg->layer_num = 0;
}

static int camdump_node_switch(struct cam_dump_node *node, struct camera_frame *pframe)
{
	int ret = 0;

	if (!node->dump_cnt)
		return ret;

	/* Prev/video pipeline dump dcam online bin port need set g_dbg_dumpswitch,
			cap pipeline dump dcam online full bort need start capture. */
	if ((pframe->link_from.node_type == CAM_NODE_TYPE_DCAM_ONLINE)
		&& ((pframe->link_from.port_id == PORT_BIN_OUT && !g_dbg_dumpswitch)
		|| (pframe->link_from.port_id == PORT_FULL_OUT && pframe->link_to.node_type == CAM_NODE_TYPE_DCAM_ONLINE)))
		ret = 0;
	else
		ret = pframe->dump_en;

	return ret;
}

static int camdump_node_frame_start(void *param)
{
	int ret = 0, i = 0;
	struct cam_dump_msg msg  = {0};
	uint8_t file_name[256] = { '\0' };
	uint8_t file_name1[256] = { '\0' };
	struct cam_dump_node *node = NULL;
	struct cam_frame *pframe = NULL;

	node = (struct cam_dump_node *)param;
	if (!node) {
		pr_err("fail to get valid param\n");
		return -EFAULT;
	}

	pframe = CAM_QUEUE_DEQUEUE(&node->dump_queue, struct cam_frame, list);
	if (pframe == NULL) {
		pr_err("fail to get input frame for dump node %d\n", node->node_id);
		return -EFAULT;
	}

	if (camdump_node_switch(node, &pframe->common)) {
		camdump_node_param_cfg(&msg, &pframe->common);
		for (i = 0; i <= msg.layer_num; i++) {
			if (i > 0) {
				msg.is_compressed = 0;
				msg.align_w = msg.align_w / 2;
				msg.align_h = msg.align_h / 2;
			}
			camdump_node_frame_name_get(&pframe->common, &msg, file_name, file_name1, i);
			if (msg.is_compressed == 0) {
				camdump_node_frame_size_get(&pframe->common, &msg, i);
				camdump_node_frame_file_write(&pframe->common, &msg, file_name, file_name1);
			}
			memset(file_name, 0, sizeof(file_name));
			memset(file_name1, 0, sizeof(file_name1));
		}
		node->dump_cnt--;
	}
	pframe->common.dump_en = CAM_DISABLE;
	pr_debug("cam dump node %d frame start, count = %d\n", node->node_id, node->dump_cnt);

	if (IS_STATIS_PORT(pframe->common.link_from.port_id))
		node->dump_cb_func(CAM_CB_DUMP_STATIS_DONE, pframe, node->dump_cb_handle);
	else
		node->dump_cb_func(CAM_CB_DUMP_DATA_DONE, pframe, node->dump_cb_handle);

	return ret;
}

int cam_dump_node_request_proc(struct cam_dump_node *node, void *param)
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
	cam_node = (struct cam_node *)node->dump_cb_handle;
	pipeline = (struct cam_pipeline *)cam_node->data_cb_handle;

	if (pipeline->debug_log_switch)
		pr_info("pipeline_type %s, fid %d, ch_id %d, buf %x, w %d, h %d, pframe->common.is_reserved %d, compress_en %d\n",
			pipeline->pipeline_graph->name, pframe->common.fid, pframe->common.channel_id, pframe->common.buf.mfd,
			pframe->common.width, pframe->common.height, pframe->common.is_reserved, pframe->common.is_compressed);

	ret = CAM_QUEUE_ENQUEUE(&node->dump_queue, &pframe->list);
	if (ret == 0)
		complete(&node->thread.thread_com);
	else
		pr_err("fail to enqueue cam dump %d frame\n", node->node_id);

	return ret;
}

void cam_dump_node_set(void *param)
{
	uint32_t val = 0, dump_cmd = 0, port = 0, node = 0, pipeline = 0;
	int i = 0;

	if (!param) {
		pr_err("fail to get dump info\n");
		return;
	}

	val = *(uint32_t *)param;
	dump_cmd = CAM_DUMP_NUM(val, 0, 4);
	port = CAM_DUMP_NUM(val, 4, 4);
	node = CAM_DUMP_NUM(val, 8, 4);
	pipeline = CAM_DUMP_NUM(val, 12, 0);

	if (dump_cmd) {
		for (i = 0; i < DUMP_NUM_MAX; i++) {
			if (g_dbg_dump[i].dump_ongoing)
				continue;
			g_dbg_dump[i].dump_en = dump_cmd;
			g_dbg_dump[i].dump_port_id = port;
			g_dbg_dump[i].dump_node_type = node;
			g_dbg_dump[i].dump_pipeline_type = pipeline;
			g_dbg_dump[i].dump_id = i;
			g_dbg_dump[i].dump_ongoing = 1;
			pr_info("dump_id%d dump pipeline %u node %u port id %u en %u\n",
				i, g_dbg_dump[i].dump_pipeline_type, g_dbg_dump[i].dump_node_type,
				g_dbg_dump[i].dump_port_id, g_dbg_dump[i].dump_en);
			break;
		}
	} else {
		for (i = 0; i < DUMP_NUM_MAX; i++) {
			if (!g_dbg_dump[i].dump_ongoing)
				continue;
			if ((g_dbg_dump[i].dump_pipeline_type == pipeline)
				&& (g_dbg_dump[i].dump_node_type == node)
				&& (g_dbg_dump[i].dump_port_id == port)) {
				g_dbg_dump[i].dump_en = dump_cmd;
				g_dbg_dump[i].dump_ongoing = 0;
				pr_info("dump_id%d dump pipeline %u node %u port id %u en %u\n",
					i, g_dbg_dump[i].dump_pipeline_type, g_dbg_dump[i].dump_node_type,
					g_dbg_dump[i].dump_port_id, g_dbg_dump[i].dump_en);
				break;
			}
		}
	}
}

void *cam_dump_node_get(uint32_t node_id, cam_data_cb cb_func, void *priv_data)
{
	int ret = 0;
	struct cam_dump_node *node = NULL;
	struct cam_thread_info *thrd = NULL;

	if (!cb_func || !priv_data) {
		pr_err("fail to get valid ptr %p %p\n", cb_func, priv_data);
		return NULL;
	}

	node = cam_buf_kernel_sys_vzalloc(sizeof(struct cam_dump_node));
	if (!node) {
		pr_err("fail to get valid cam dump node\n");
		return NULL;
	}

	if (!node->dump_cb_func) {
		node->dump_cb_func = cb_func;
		node->dump_cb_handle = priv_data;
	}

	CAM_QUEUE_INIT(&node->dump_queue, DUMP_NODE_Q_LEN, cam_queue_empty_frame_put);
	init_completion(&node->dump_com);
	node->node_id = node_id;
	node->dump_cnt = 99;
	if (g_dbg_dumpcount)
		node->dump_cnt = g_dbg_dumpcount;

	thrd = &node->thread;
	snprintf(thrd->thread_name, ARRAY_SIZE(thrd->thread_name), "cam_dump_node%d", node->node_id);
	ret = camthread_create(node, thrd, camdump_node_frame_start);
	if (unlikely(ret != 0)) {
		pr_err("fail to create cam dump node%d thread\n", node->node_id);
		return NULL;
	}

	pr_info("cam dump node %d get\n", node->node_id);

	return node;
}

void cam_dump_node_put(struct cam_dump_node *node)
{
	if (!node) {
		pr_err("fail to get invalid node ptr\n");
		return;
	}

	camthread_stop(&node->thread);
	CAM_QUEUE_CLEAN(&node->dump_queue, struct cam_frame, list);
	node->dump_cb_func = NULL;
	node->dump_cb_handle = NULL;
	pr_info("cam dump node %d put\n", node->node_id);
	cam_buf_kernel_sys_vfree(node);
	node = NULL;
}

