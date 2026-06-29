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

#include "cam_copy_node.h"
#include "cam_pipeline.h"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "CAM_COPY_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static int camcopy_node_copy_frame(struct cam_copy_node *node, int loop_num)
{
	int i = 0, ret = 0;
	uint32_t buf_size = 0;
	struct cam_frame *in_frame = NULL, *out_frame = NULL;

	for (i = 0; i < loop_num; i++) {
		in_frame = CAM_QUEUE_DEQUEUE(&node->in_queue, struct cam_frame, list);
		if (in_frame == NULL) {
			pr_err("fail to get input frame for dump node %d\n", node->node_id);
			goto get_in_frame_fail;
		}
		out_frame = CAM_QUEUE_DEQUEUE(&node->out_queue, struct cam_frame, list);
		if (out_frame == NULL) {
			pr_debug("no get out frame copy mode:%d\n", node->copy_mode);
			in_frame->common.copy_en = 0;
			switch (node->scene_id) {
			case CAM_COPY_NORMAL_SCENE:
			case CAM_COPY_OPT_SCENE:
				if (node->copy_mode == CAM_COPY_FRAME_IN_DSTBUF)
					node->copy_cb_func(CAM_CB_COPY_DST_BUFFER, in_frame, node->copy_cb_handle);
				else
					node->copy_cb_func(CAM_CB_COPY_SRC_BUFFER, in_frame, node->copy_cb_handle);
				break;
			case CAM_COPY_ICAP_SCENE:
				node->copy_cb_func(CAM_CB_ICAP_COPY_SRC_BUFFER, in_frame, node->copy_cb_handle);
				break;
			default :
				pr_err("fail to support scene id %d\n", node->scene_id);
				return -EFAULT;
			}
		} else {
			pr_debug("start copy src frame data in copy mode:%d\n", node->copy_mode);
			switch (node->scene_id) {
			case CAM_COPY_NORMAL_SCENE:
			case CAM_COPY_OPT_SCENE:
				buf_size = in_frame->common.buf.size;
				if (in_frame->common.buf.size > out_frame->common.buf.size) {
					pr_err("warning: out buff is smaller, in buf size %d and out buf size %d\n",
						in_frame->common.buf.size, out_frame->common.buf.size);
					buf_size = out_frame->common.buf.size;
				}
				if (cam_buf_manager_buf_status_cfg(&in_frame->common.buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX)) {
					pr_err("fail to kmap in buf\n");
					goto in_frame_kmap_fail;
				}
				if (cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX)) {
					pr_err("fail to kmap out buf\n");
					goto out_frame_kmap_fail;
				}
				memcpy((char *)out_frame->common.buf.addr_k, (char *)in_frame->common.buf.addr_k, buf_size);
				/* use SOF time instead of ISP time for better accuracy */
				out_frame->common.width = in_frame->common.width;
				out_frame->common.height = in_frame->common.height;
				out_frame->common.fid = in_frame->common.fid;
				out_frame->common.sensor_time.tv_sec = in_frame->common.sensor_time.tv_sec;
				out_frame->common.sensor_time.tv_usec = in_frame->common.sensor_time.tv_usec;
				out_frame->common.boot_sensor_time = in_frame->common.boot_sensor_time;
				/* end copy, out src buf*/
				cam_buf_manager_buf_status_cfg(&in_frame->common.buf, CAM_BUF_STATUS_PUT_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
				cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_PUT_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
				in_frame->common.copy_en = 0;
				if (node->copy_mode == CAM_COPY_FRAME_IN_DSTBUF)
					node->copy_cb_func(CAM_CB_COPY_DST_BUFFER, in_frame, node->copy_cb_handle);
				else
					node->copy_cb_func(CAM_CB_COPY_SRC_BUFFER, in_frame, node->copy_cb_handle);
				out_frame->common.evt = IMG_TX_DONE;
				out_frame->common.irq_type = CAMERA_IRQ_IMG;
				out_frame->common.priv_data = node;
				out_frame->common.channel_id = CAM_CH_RAW;
				out_frame->common.link_to.node_type = CAM_NODE_TYPE_USER;
				out_frame->common.link_to.node_id = CAM_LINK_DEFAULT_NODE_ID;
				pr_debug("get out raw frame fd 0x%x raw fram fid %d\n", out_frame->common.buf.mfd, out_frame->common.fid);
				node->copy_cb_func(CAM_CB_DCAM_DATA_DONE, out_frame, node->copy_cb_handle);
				break;
			case CAM_COPY_ICAP_SCENE:
				if (cam_buf_manager_buf_status_cfg(&in_frame->common.buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX)) {
					pr_err("fail to kmap in buf\n");
					goto in_frame_kmap_fail;
				}
				if (cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_GET_K_ADDR, CAM_BUF_IOMMUDEV_MAX)) {
					pr_err("fail to kmap out buf\n");
					goto out_frame_kmap_fail;
				}
				atomic_dec(&node->icap_cap_num);
				memcpy((char *)out_frame->common.buf.addr_k, (char *)in_frame->common.buf.addr_k, out_frame->common.buf.size);
				/* use SOF time instead of ISP time for better accuracy */
				out_frame->common.width = in_frame->common.width;
				out_frame->common.height = in_frame->common.height;
				out_frame->common.fid = in_frame->common.fid;
				out_frame->common.sensor_time.tv_sec = in_frame->common.sensor_time.tv_sec;
				out_frame->common.sensor_time.tv_usec = in_frame->common.sensor_time.tv_usec;
				out_frame->common.boot_sensor_time = in_frame->common.boot_sensor_time;
				/* end copy, out src buf*/
				cam_buf_manager_buf_status_cfg(&in_frame->common.buf, CAM_BUF_STATUS_PUT_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
				cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_PUT_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
				in_frame->common.copy_en = 0;
				pr_debug("get out pframe fd 0x%x pframe fid %d\n", in_frame->common.buf.mfd, in_frame->common.fid);
				node->copy_cb_func(CAM_CB_ICAP_COPY_SRC_BUFFER, in_frame, node->copy_cb_handle);
				out_frame->common.link_from.node_type = CAM_NODE_TYPE_DATA_COPY;
				out_frame->common.link_from.port_id = PORT_COPY_OUT;
				ret = node->copy_cb_func(CAM_CB_DCAM_DATA_DONE, out_frame, node->copy_cb_handle);
				break;
			default :
				pr_err("fail to support scene id %d\n", node->scene_id);
				return -EFAULT;
			}
		}
	}

	return ret;

out_frame_kmap_fail:
	cam_buf_manager_buf_status_cfg(&in_frame->common.buf, CAM_BUF_STATUS_PUT_K_ADDR, CAM_BUF_IOMMUDEV_MAX);
in_frame_kmap_fail:
	in_frame->common.copy_en = 0;
	node->copy_cb_func(CAM_CB_COPY_SRC_BUFFER, in_frame, node->copy_cb_handle);
	CAM_QUEUE_ENQUEUE(&node->out_queue, &out_frame->list);
get_in_frame_fail:

	return -EFAULT;
}

static int camcopy_node_frame_start(void *param)
{
	int ret = 0, loop_num = 1;
	struct cam_copy_node *node = NULL;
	struct cam_frame *pframe = NULL;

	if (!param) {
		pr_err("fail to get valid param\n");
		return -EFAULT;
	}
	node = (struct cam_copy_node *)param;

	switch (node->scene_id) {
	case CAM_COPY_NORMAL_SCENE:
		break;
	case CAM_COPY_OPT_SCENE:
		if (node->record_channel_id == CAM_CH_PRE && node->opt_buffer_num) {
			if (node->pre_raw_flag == PRE_RAW_CACHE) {
				if (node->in_queue.cnt > node->opt_buffer_num) {
					pframe = CAM_QUEUE_DEQUEUE(&node->in_queue, struct cam_frame, list);
					if (pframe) {
						pframe->common.copy_en = 0;
						ret = node->copy_cb_func(CAM_CB_COPY_SRC_BUFFER, pframe, node->copy_cb_handle);
						return ret;
					} else {
						pr_err("fail to dequeue frame for node %d\n", node->node_id);
						return -EFAULT;
					}
				}
				return ret;
			} else if (atomic_read(&node->opt_frame_done) == 0 && node->pre_raw_flag == PRE_RAW_DEAL) {
				if (node->in_queue.cnt < node->opt_buffer_num) {
					node->pre_cache_done = CAM_DISABLE;
					node->pre_frame_status(node->pre_cache_done,
						node->private_cb_data);
					return ret;
				}
				atomic_set(&node->opt_frame_done, 1);
				loop_num = node->opt_buffer_num;
				node->pre_cache_done = CAM_ENABLE;
				node->pre_frame_status(node->pre_cache_done, node->private_cb_data);
			} else {
				pframe = CAM_QUEUE_DEQUEUE_TAIL(&node->in_queue, struct cam_frame, list);
				if (pframe) {
					pframe->common.copy_en = 0;
					ret = node->copy_cb_func(CAM_CB_COPY_SRC_BUFFER, pframe, node->copy_cb_handle);
					return ret;
				} else {
					pr_err("fail to dequeue frame for node %d\n", node->node_id);
					return -EFAULT;
				}
			}
		}
		break;
	case CAM_COPY_ICAP_SCENE:
		if (node->cap_param.cap_type == DCAM_CAPTURE_STOP ||
			atomic_read(&node->icap_cap_num) == 0) {
			pr_debug("current condition cap_type %d and icap_cap_num %d\n",
				node->cap_param.cap_type, atomic_read(&node->icap_cap_num));
			pframe = CAM_QUEUE_DEQUEUE(&node->in_queue, struct cam_frame, list);
			if (pframe) {
				pframe->common.copy_en = 0;
				if (node->raw_deal_switch)
					node->copy_cb_func(CAM_CB_ICAP_COPY_SRC_BUFFER,
						pframe, node->copy_cb_handle);
				else {
					pframe->common.link_to = pframe->common.link_from;
					ret = node->copy_cb_func(CAM_CB_COPY_SRC_BUFFER,
						pframe, node->copy_cb_handle);
				}
				return ret;

			} else {
				pr_err("fail to dequeue frame for node %d\n", node->node_id);
				return -EFAULT;
			}
		}
		break;
	default :
		pr_err("fail to support scene id %d\n", node->scene_id);
		return -EFAULT;
	}

	ret = camcopy_node_copy_frame(node, loop_num);

	return ret;
}

int cam_copy_node_buffer_cfg(void *handle, void *param)
{
	int ret = 0;
	struct cam_copy_node *node = NULL;
	struct cam_frame *pframe = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_copy_node *)handle;
	node->copy_flag = CAM_ENABLE;
	pframe = (struct cam_frame *)param;
	ret = cam_buf_ionbuf_get(&pframe->common.buf);
	if (ret) {
		pr_err("fail to get ion copy buffer\n");
		return ret;
	}

	ret = CAM_QUEUE_ENQUEUE(&node->out_queue, &pframe->list);
	if (ret)
		pr_err("fail to enqueue copy buffer\n");

	return ret;
}

int cam_copy_node_buffers_alloc(void *handle, struct cam_buf_alloc_desc *param)
{
	int ret = 0;
	uint32_t i = 0, total = 0, size = 0, width = 0, height = 0, ch_id = 0;
	struct cam_copy_node *node = NULL;
	struct cam_frame *pframe = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -1;
	}

	node = (struct cam_copy_node *)handle;

	total = param->cam_copy_buf_alloc_num;
	if (total == 0)
		return ret;
	height = param->height;
	width = param->width;
	ch_id = param->ch_id;
	size = cal_sprd_size(width, height, CAM_RAW_PACK_10);

	pr_info("port:%s, cam%d, ch_id %d, buffer size: %u (%u x %u), num %d\n",
		cam_port_name_get(node->node_id), param->cam_idx, ch_id, size, width, height, total);

	for (i = 0; i < total; i++) {
		pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		if (!pframe) {
			pr_err("fail to get frame\n");
			ret = -EINVAL;
			break;
		}
		pframe->common.channel_id = param->ch_id;
		pframe->common.is_compressed = param->compress_en;
		pframe->common.width = width;
		pframe->common.height = height;
		ret = cam_buf_alloc(&pframe->common.buf, size, param->iommu_enable);
		if (ret) {
			pr_err("fail to alloc buf: %d ch %d\n",
				i, param->ch_id);
			cam_queue_empty_frame_put(pframe);
			continue;
		}

		cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_DCAM);
		pframe->common.buf.bypass_iova_ops = CAM_ENABLE;
		ret = CAM_QUEUE_ENQUEUE(&node->out_queue, &pframe->list);
	}

	return ret;
}

int cam_copy_node_buffer_num(void *handle, void *param)
{
	int ret = 0;
	struct cam_copy_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_copy_node *)handle;
	atomic_inc(&node->icap_cap_num);
	pr_debug("icap scene need copy num %d\n", atomic_read(&node->icap_cap_num));

	return ret;
}

int cam_copy_outbuf_back(void *handle, void *param)
{
	int ret = 0;
	struct cam_copy_node *node = NULL;
	struct cam_frame *pframe = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct cam_copy_node *)handle;
	pframe = (struct cam_frame *)param;

	ret = CAM_QUEUE_ENQUEUE(&node->out_queue, &pframe->list);

	return ret;
}

int cam_copy_cfg_param(void *handle, void *param)
{
	int ret = 0;
	struct cam_copy_node *node = NULL;
	struct cam_capture_param *cap_param = NULL;

	if (!handle) {
		pr_err("fail to get valid input ptr %p\n", handle);
		return -EFAULT;
	}

	node = (struct cam_copy_node *)handle;
	cap_param = (struct cam_capture_param *)param;

	node->cap_param.cap_type = cap_param->cap_type;
	node->cap_param.cap_cnt = cap_param->cap_cnt;
	node->cap_param.cap_timestamp = cap_param->cap_timestamp;
	node->cap_param.cap_user_crop = cap_param->cap_user_crop;
	node->cap_param.zsl_num = cap_param->zsl_num;
	node->cap_param.frm_sel_mode = cap_param->frm_sel_mode;
	pr_info("cap type %d, cnt %d, time %lld, frm_sel_mode:%d, zsl_num:%d\n", node->cap_param.cap_type,
		atomic_read(&node->cap_param.cap_cnt), node->cap_param.cap_timestamp, cap_param->frm_sel_mode,
		cap_param->zsl_num);

	return ret;
}

int cam_copy_node_set_icap_scene(void *handle, void *param)
{
	int ret = 0;
	struct cam_copy_node *node = NULL;
	uint32_t icap_buffer_num = 0;

	if (!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	icap_buffer_num = *(uint32_t *)param;
	node = (struct cam_copy_node *)handle;
	node->copy_flag = CAM_ENABLE;
	node->scene_id = CAM_COPY_ICAP_SCENE;

	return ret;
}

int cam_copy_node_set_raw_report_condition(void *handle, void *param)
{
	int ret = 0;
	struct cam_copy_node *node = NULL;
	uint32_t raw_deal_switch = 0;

	if (!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	raw_deal_switch = *(uint32_t *)param;
	node = (struct cam_copy_node *)handle;
	node->raw_deal_switch = raw_deal_switch;

	return ret;
}

int cam_copy_node_set_pre_raw_flag(void *handle, void *param)
{
	int ret = 0;
	uint32_t pre_raw_flag = 0;
	struct cam_copy_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	pre_raw_flag = *(uint32_t *)param;
	node = (struct cam_copy_node *)handle;
	node->pre_raw_flag = pre_raw_flag;
	atomic_set(&node->opt_frame_done, 0);

	return ret;
}

int cam_copy_node_set_opt_scene(void *handle, void *param)
{
	int ret = 0;
	uint32_t opt_buffer_num = 0;
	struct cam_copy_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	opt_buffer_num = *(uint32_t *)param;
	node = (struct cam_copy_node *)handle;
	node->copy_flag = CAM_ENABLE;
	node->opt_buffer_num = opt_buffer_num;
	node->scene_id = CAM_COPY_OPT_SCENE;

	return ret;
}

int cam_copy_node_request_proc(struct cam_copy_node *node, void *param)
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

	cam_node = (struct cam_node *)node->copy_cb_handle;
	pipeline = (struct cam_pipeline *)cam_node->data_cb_handle;
	if (node->copy_flag) {
		if (pipeline->debug_log_switch)
			pr_info("pipeline_type %s, fid %d, ch_id %d, buf %x, w %d, h %d, pframe->common.is_reserved %d, compress_en %d\n",
				pipeline->pipeline_graph->name, pframe->common.fid, pframe->common.channel_id, pframe->common.buf.mfd,
				pframe->common.width, pframe->common.height, pframe->common.is_reserved, pframe->common.is_compressed);

		node->record_channel_id = pframe->common.channel_id;
		ret = CAM_QUEUE_ENQUEUE(&node->in_queue, &pframe->list);
		if (ret == 0)
			complete(&node->thread.thread_com);
		else
			pr_err("fail to enqueue cam copy %d frame\n", node->node_id);
	} else {
		pframe->common.copy_en = 0;
		if (node->copy_mode == CAM_COPY_FRAME_IN_SRCBUF) {
			node->copy_cb_func(CAM_CB_COPY_SRC_BUFFER, pframe, node->copy_cb_handle);
		} else if (node->copy_mode == CAM_COPY_FRAME_IN_DSTBUF) {
			node->copy_cb_func(CAM_CB_COPY_DST_BUFFER, pframe, node->copy_cb_handle);
		} else {
			ret = -EFAULT;
			pr_err("fail to support copy mode:%d.\n", node->copy_mode);
		}
	}

	return ret;
}

void *cam_copy_node_get(uint32_t node_id, struct cam_copy_node_desc *param)
{
	int ret = 0;
	struct cam_copy_node *node = NULL;
	struct cam_thread_info *thrd = NULL;

	if (!param) {
		pr_err("fail to get valid ptr %p\n", param);
		return NULL;
	}

	node = cam_buf_kernel_sys_vzalloc(sizeof(struct cam_copy_node));
	if (!node) {
		pr_err("fail to get valid cam copy node\n");
		return NULL;
	}

	if (!node->copy_cb_func) {
		node->copy_cb_func = param->copy_cb_func;
		node->copy_cb_handle = param->copy_cb_handle;
	}

	CAM_QUEUE_INIT(&node->in_queue, COPY_NODE_Q_LEN, cam_queue_empty_frame_put);
	CAM_QUEUE_INIT(&node->out_queue, COPY_NODE_Q_LEN, cam_queue_empty_frame_put);
	node->node_id = node_id;
	node->copy_mode = param->copy_mode;
	node->copy_flag = CAM_DISABLE;
	node->pre_raw_flag = PRE_RAW_CACHE;
	node->opt_buffer_num = 0;
	node->scene_id = CAM_COPY_NORMAL_SCENE;
	atomic_set(&node->icap_cap_num, 0);
	node->pre_frame_status = param->pre_frame_status;
	node->private_cb_data = param->private_cb_data;
	node->raw_deal_switch = 0;

	thrd = &node->thread;
	snprintf(thrd->thread_name, ARRAY_SIZE(thrd->thread_name), "cam_copy_node%d", node->node_id);
	ret = camthread_create(node, thrd, camcopy_node_frame_start);
	if (unlikely(ret != 0)) {
		pr_err("fail to create cam copy node%d thread\n", node->node_id);
		return NULL;
	}

	pr_info("cam copy node %d get\n", node->node_id);

	return node;
}

void cam_copy_node_put(struct cam_copy_node *node)
{
	if (!node) {
		pr_err("fail to get invalid node ptr\n");
		return;
	}

	camthread_stop(&node->thread);
	CAM_QUEUE_CLEAN(&node->in_queue, struct cam_frame, list);
	CAM_QUEUE_CLEAN(&node->out_queue, struct cam_frame, list);
	node->copy_cb_func = NULL;
	node->copy_cb_handle = NULL;
	pr_info("cam copy node %d put\n", node->node_id);
	cam_buf_kernel_sys_vfree(node);
	node = NULL;
}

