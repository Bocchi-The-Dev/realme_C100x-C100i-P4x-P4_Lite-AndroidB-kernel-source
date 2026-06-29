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

#include "cam_zoom.h"
#include "isp_drv.h"
#include "isp_int.h"
#include "isp_int_common.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_PORT: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define TIMEOUT_WAIT_FOR_VALID_BUFFER 50

static int ispport_slice_fetch_prepare(struct isp_port *port, void *param)
{
	uint32_t ret = 0, width = 0, height = 0;
	struct isp_port_cfg *port_cfg = NULL;
	struct isp_hw_fetch_info *fetch = NULL;
	struct img_slice_info *slice = NULL;

	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	fetch = &port_cfg->pipe_info->fetch;
	slice = &port_cfg->src_frame->common.slice_info;
	width = port_cfg->src_frame->common.width;
	height = port_cfg->src_frame->common.height;
	port->slice_info.slice_num = slice->slice_num;
	port->slice_info.slice_no = slice->slice_no;

	fetch->pitch.pitch_ch0 = cam_cal_hw_pitch(width, port->fmt);
	fetch->in_trim = slice->in_trim;
	fetch->src.w = slice->in_trim.size_x;
	fetch->src.h = slice->in_trim.size_y;

	pr_info("fetch src %d, %d, in_trim(%d, %d, %d, %d), slice info:(%d, %d)\n",
		fetch->src.w, fetch->src.h, fetch->in_trim.start_x, fetch->in_trim.start_y,
		fetch->in_trim.size_x, fetch->in_trim.size_y, slice->slice_num, slice->slice_no);

	return ret;
}

static int ispport_slice_store_prepare(struct isp_port *port, void *param)
{
	uint32_t ret = 0, width = 0, height = 0, path_id = 0;
	uint32_t store_start_x = 0, store_start_y = 0, slice_num_w = 0;
	struct isp_port_cfg *port_cfg = NULL;
	struct isp_store_info *store = NULL;
	struct isp_hw_path_scaler *path = NULL;
	struct img_slice_info *slice = NULL;

	path_id = isp_port_id_switch(port->port_id);
	if (path_id == ISP_SPATH_NUM) {
		pr_err("fail to get hw_path_id\n");
		return 0;
	}
	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	store = &port_cfg->pipe_info->store[path_id].store;
	path = &port_cfg->pipe_info->scaler[path_id];
	slice = &port_cfg->src_frame->common.slice_info;
	width = port_cfg->src_frame->common.width;
	height = port_cfg->src_frame->common.height;
	port->slice_info.slice_num = slice->slice_num;
	port->slice_info.slice_no = slice->slice_no;
	if ((slice->out_trim.size_x + slice->out_trim.size_y) == 0)
		slice->out_trim = slice->in_trim;

	path->src.w = slice->in_trim.size_x;
	path->src.h = slice->in_trim.size_y;
	path->in_trim = slice->out_trim;
	path->dst = port->size;
	path->out_trim = slice->out_trim;
	slice_num_w = slice->slice_num_w;
	store_start_x = (slice->slice_no % slice_num_w) * slice->out_trim.size_x;
	store_start_y = (slice->slice_no / slice_num_w) * slice->out_trim.size_y;
	store->pitch.pitch_ch0 = path->dst.w;
	store->pitch.pitch_ch1 = path->dst.w;
	store->size.w = ALIGN(path->out_trim.size_x, 2);
	store->size.h = ALIGN(path->out_trim.size_y, 2);
	if (slice->slice_num > SLICE_NUM_MAX) {
		store->slice_offset.addr_ch0 = slice->in_trim.start_x
			+ (slice->slice_no / (slice->slice_num / 2))
			* store->pitch.pitch_ch0 * (height - store->size.h);
		store->slice_offset.addr_ch1 = slice->in_trim.start_x
			+ (slice->slice_no / (slice->slice_num / 2))
			* store->pitch.pitch_ch1 * (height - store->size.h) / 2;
	} else {
		store->slice_offset.addr_ch0 =
			store_start_x + store_start_y * store->pitch.pitch_ch0;
		store->slice_offset.addr_ch1 =
			store_start_x + store_start_y * store->pitch.pitch_ch0 / 2;
		store->slice_offset.addr_ch2 = 0;
	}
	pr_debug("scaler src %d, %d, in_trim(%d, %d, %d, %d), dst %d, %d, out_trim(%d, %d, %d, %d), store %d, %d, %d, %d\n",
		path->src.w, path->src.h, path->in_trim.start_x, path->in_trim.start_y,
		path->in_trim.size_x, path->in_trim.size_y, path->dst.w, path->dst.h,
		path->out_trim.start_x, path->out_trim.start_y, path->out_trim.size_x, path->out_trim.size_y,
		store_start_x, store_start_y, store->size.w, store->size.h);

	return ret;
}

static void ispport_frame_ret(void *param)
{
	struct cam_frame * pframe = NULL;
	struct isp_port *port = NULL;
	int ret = 0;

	if (!param) {
		pr_err("fail to get input ptr.\n");
		return;
	}

	pframe = (struct cam_frame *)param;
	port = (struct isp_port *)pframe->common.priv_data;
	if (!port) {
		pr_err("fail to get out_frame port.\n");
		return;
	}

	pr_debug("port type:%d frame %p, ch_id %d, buf_fd %d\n",
		port->type, pframe, pframe->common.channel_id, pframe->common.buf.mfd);

	ret = cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
	if (ret)
		pr_err("fail to unmap buffer\n");

	port->data_cb_func(CAM_CB_ISP_RET_SRC_BUF, pframe, port->data_cb_handle);
}

static uint32_t ispport_base_cfg(struct isp_port *port, void *param)
{
	struct isp_path_base_desc *cfg_in = NULL;
	cfg_in = VOID_PTR_TO(param, struct isp_path_base_desc);

	port->fmt = cfg_in->out_fmt;
	port->data_endian = cfg_in->endian;
	port->size = cfg_in->output_size;
	if (cfg_in->is_work == 1)
		atomic_set(&port->is_work, 1);
	pr_info("port_id %d, out fmt %s, size:%d %d\n", port->port_id, camport_fmt_name_get(port->fmt), port->size.w, port->size.h);
	return 0;
}

static uint32_t ispport_bufq_clr(struct isp_port *port, void *param)
{
	struct cam_frame *pframe = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	if (port->type == PORT_TRANSFER_OUT) {
		buf_desc.buf_ops_cmd = CAM_BUF_STATUS_MOVE_TO_ALLOC;
		pframe = cam_buf_manager_buf_dequeue(&port->store_result_pool, &buf_desc, port->buf_manager_handle);
		if (!pframe)
			pframe = cam_buf_manager_buf_dequeue(&port->store_unprocess_pool, &buf_desc, port->buf_manager_handle);
	} else if (port->type == PORT_TRANSFER_IN) {
		buf_desc.buf_ops_cmd = CAM_BUF_STATUS_MOVE_TO_ALLOC;
		pframe = cam_buf_manager_buf_dequeue(&port->fetch_result_pool, &buf_desc, port->buf_manager_handle);
		if (pframe)
			port->data_cb_func(CAM_CB_ISP_RET_SRC_BUF, pframe, port->data_cb_handle);
	} else
		pr_err("fail to support port:%d.\n", port->type);

	pr_debug("port_type:%d, port_id:%d\n", port->type, port->port_id);
	return 0;
}

static uint32_t ispport_buf_set(struct isp_port *port, void *param)
{
	uint32_t ret = 0;
	struct cam_frame *pframe = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	pframe = VOID_PTR_TO(param, struct cam_frame);
	pframe->common.is_reserved = 0;
	pframe->common.priv_data = port;

	if (port->type == PORT_TRANSFER_IN) {
		if (pframe->common.link_from.node_type == CAM_NODE_TYPE_PYR_DEC)
			port->fetch_path_sel = ISP_FETCH_PATH_NORMAL;
		ret = cam_buf_manager_buf_enqueue(&port->fetch_unprocess_pool, pframe, NULL, port->buf_manager_handle);
		if (ret)
			pr_err("fail to enqueue output buffer,type:%d port_id %d.\n", port->type, port->port_id);
	}

	if (port->type == PORT_TRANSFER_OUT) {
		if (pframe->common.buf.type == CAM_BUF_USER) {
			buf_desc.buf_ops_cmd = CAM_BUF_STATUS_MOVE_TO_ION;
			ret = cam_buf_manager_buf_enqueue(
				&port->store_unprocess_pool, pframe,
				&buf_desc, port->buf_manager_handle);
		} else
			ret = cam_buf_manager_buf_enqueue(
			&port->store_unprocess_pool, pframe,
			NULL, port->buf_manager_handle);
		if (ret)
			pr_err("fail to enqueue output buffer,type:%d port_id %d.\n", port->type, port->port_id);
	}
	return ret;
}

static uint32_t ispport_reserved_buf_set(struct isp_port *port, void *param)
{
	struct cam_frame *pframe = NULL;

	pframe = VOID_PTR_TO(param, struct cam_frame);
	port->reserved_buf_fd = pframe->common.buf.mfd;
	port->reserve_buf_size = pframe->common.buf.size;
	return 0;
}

static int ispport_zoom_size_update(struct isp_port *port, void *param)
{
	int ret = 0;
	struct isp_port_cfg *port_cfg = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_zoom_base zoom_base = {0};
	struct cam_zoom_index zoom_index = {0};

	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	if (atomic_read(&port->user_cnt) < 1)
		return 0;

	pframe = port_cfg->src_frame;
	if (pframe) {
		zoom_index.node_type = CAM_NODE_TYPE_ISP_OFFLINE;
		zoom_index.node_id = port_cfg->node_id;
		zoom_index.port_type = port->type;
		zoom_index.port_id = port->port_id;
		zoom_index.zoom_data = &pframe->common.zoom_data;
		ret = cam_zoom_frame_base_get(&zoom_base, &zoom_index);
		if (!ret) {
			port->size = zoom_base.dst;
			port->trim = zoom_base.crop;
			port->need_post_proc = zoom_base.need_post_proc;
		}

		CAM_ZOOM_DEBUG("frame %d ret %d type %d id %d size %d %d trim %d %d %d %d\n", pframe->common.fid,
			ret, port->type, port->port_id, port->size.w, port->size.h, port->trim.start_x,
			port->trim.start_y, port->trim.size_x, port->trim.size_y);
	}

	return ret;
}

static int ispport_fetchport_postproc(struct isp_port *port, void *param)
{
	struct cam_frame *pframe = NULL;
	struct camera_frame *pframe_data = NULL;
	struct isp_node_postproc_param *post_param = NULL;

	post_param = VOID_PTR_TO(param, struct isp_node_postproc_param);

	pframe = cam_buf_manager_buf_dequeue(&port->fetch_result_pool, NULL, port->buf_manager_handle);
	if (pframe) {
		if (pframe->common.pframe_data != NULL) {
			pframe_data = &((struct cam_frame *)pframe->common.pframe_data)->common;
			cam_buf_manager_buf_status_cfg(&pframe_data->buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
			port->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, pframe->common.pframe_data, port->data_cb_handle);
			pframe->common.pframe_data = NULL;
			pr_debug("ch_id %d, fid:%d\n", pframe->common.channel_id, pframe->common.fid);
		}

		post_param->zoom_ratio = pframe->common.zoom_ratio;
		post_param->total_zoom = pframe->common.total_zoom;
		cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
		port->data_cb_func(CAM_CB_ISP_RET_SRC_BUF, pframe, port->data_cb_handle);
		pr_debug("port %d, ch_id %d, fid:%d, mfd:%d\n",
			port->port_id, pframe->common.channel_id, pframe->common.fid, pframe->common.buf.mfd);
	} else
		pr_err("fail to get src frame  sw_idx=%d\n",port->port_id);

	return 0;
}

static int ispport_storeport_postproc(struct isp_port *port, void *param)
{
	struct cam_frame *pframe = NULL;
	struct isp_node_postproc_param *post_param;
	struct cam_frame *pframe1 = NULL;

	post_param = VOID_PTR_TO(param, struct isp_node_postproc_param);
	pframe = cam_buf_manager_buf_dequeue(&port->store_result_pool, NULL, port->buf_manager_handle);

	if (!pframe) {
		pr_err("fail to get frame from queue. port:%d\n", port->port_id);
		return 0;
	}
	pframe->common.zoom_ratio = post_param->zoom_ratio;
	pframe->common.total_zoom = post_param->total_zoom;
	pr_debug("port%d, ch_id %d, fid %d, mfd 0x%x, is_reserved %d\n",
		port->port_id, pframe->common.channel_id, pframe->common.fid, pframe->common.buf.mfd, pframe->common.is_reserved);
	pr_debug("time_sensor %03d.%6d\n", (uint32_t)pframe->common.sensor_time.tv_sec, (uint32_t)pframe->common.sensor_time.tv_usec);

	if (unlikely(pframe->common.is_reserved))
		cam_queue_empty_frame_put(pframe);
	else if (pframe->common.state == ISP_STREAM_POST_PROC) {
		pframe1 = cam_buf_manager_buf_dequeue(&port->store_unprocess_pool, NULL, port->buf_manager_handle);
		if (!pframe1) {
			pr_info("warning no frame get from queue\n");
			return 0;
		}
		pframe->common.pframe_data = pframe1;
		port->data_cb_func(CAM_CB_YUV_SCALER_DATA_DONE, pframe, port->data_cb_handle);
	} else {
		if (pframe->common.buf.mfd == port->reserved_buf_fd) {
			pframe->common.buf.size = port->reserve_buf_size;
			pr_debug("pframe->common.buf.size = %zu, path->reserve_buf_size = %zu\n",
				pframe->common.buf.size, port->reserve_buf_size);
		}
		cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
		port->data_cb_func(CAM_CB_ISP_RET_DST_BUF, pframe, port->data_cb_handle);
	}
	return 0;
}

static int ispport_port_postproc(struct isp_port *port, void *param)
{
	if (port->type == PORT_TRANSFER_IN)
		ispport_fetchport_postproc(port, param);

	if (port->type == PORT_TRANSFER_OUT)
		ispport_storeport_postproc(port, param);
	return 0;
}

static enum cam_en_status ispport_fid_check(struct cam_frame *frame, void *data)
{
	uint32_t target_fid;

	if (!frame || !data)
		return false;

	target_fid = *(uint32_t *)data;

	pr_debug("target_fid = %d frame->common.user_fid = %d\n", target_fid, frame->common.user_fid);
	return frame->common.user_fid == CAMERA_RESERVE_FRAME_NUM
		|| frame->common.user_fid == target_fid;
}

static struct cam_frame *ispport_reserved_buf_get(reserved_buf_get_cb resbuf_get_cb,
		void *cb_data, struct isp_port *port)
{
	struct cam_frame *frame = NULL;

	if (resbuf_get_cb)
		resbuf_get_cb((void *)&frame, cb_data);
	pr_debug("isp use reserved buffer for port %d\n", port->port_id);
	return frame;
}

static struct cam_frame *ispport_out_frame_get(struct isp_port *port, struct isp_port_cfg *port_cfg)
{
	int ret = 0;
	struct cam_frame *out_frame = NULL;
	uint32_t buf_type = 0;
	struct camera_buf_get_desc buf_desc = {0};

	if (!port || !port_cfg) {
		pr_err("fail to get valid input pctx %p %p\n", port, port_cfg);
		return NULL;
	}
	if (!port_cfg->src_frame) {
		pr_err("fail to get valid input src frame\n");
		return NULL;
	}

	buf_type = port_cfg->src_frame->common.buf_type;
	switch (buf_type) {
	case ISP_STREAM_BUF_OUT:
		goto normal_out_put;
	case ISP_STREAM_BUF_RESERVED:
		out_frame = ispport_reserved_buf_get(
			port->resbuf_get_cb, port->resbuf_cb_data, port);
		if (out_frame) {
			port_cfg->valid_out_frame = 1;
			pr_debug("reserved buffer %d %lx\n",
				out_frame->common.is_reserved,
				out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP]);
		}
		break;
	case ISP_STREAM_BUF_POSTPROC:
		out_frame = port_cfg->superzoom_frame;
		if (out_frame) {
			port_cfg->valid_out_frame = 1;
			pr_debug("postproc buf %lx\n",
				out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP]);
			if (ret)
				out_frame = NULL;
		}
		break;
	default:
		pr_err("fail to support buf_type %d\n", buf_type);
		break;
	}
	goto exit;

normal_out_put:
	if (port->vid_cap_en && (!port_cfg->uinfo->cap_type || (port_cfg->uinfo->cap_type &&
		port_cfg->src_frame->common.boot_sensor_time < port_cfg->uinfo->cap_timestamp))) {
		out_frame = ispport_reserved_buf_get(port->resbuf_get_cb, port->resbuf_cb_data, port);
		goto exit;
	}

	if (port->slice_info.slice_num && port->slice_info.slice_no != 0)
		out_frame = cam_buf_manager_buf_dequeue(&port->store_result_pool, NULL, port->buf_manager_handle);
	else {
		if (port->uframe_sync && port_cfg->target_fid != CAMERA_RESERVE_FRAME_NUM) {
			buf_desc.q_ops_cmd = CAM_QUEUE_IF;
			buf_desc.filter = ispport_fid_check;
			buf_desc.target_fid = port_cfg->target_fid;
			out_frame = cam_buf_manager_buf_dequeue(&port->store_unprocess_pool, &buf_desc, port->buf_manager_handle);
		} else
			out_frame = cam_buf_manager_buf_dequeue(&port->store_unprocess_pool, NULL, port->buf_manager_handle);
	}

	if (out_frame) {
		port_cfg->valid_out_frame = 1;
		if (port->port_id == PORT_VID_OUT)
			port_cfg->sync_slw_vid_en = CAM_ENABLE;
	}
	else {
		out_frame = ispport_reserved_buf_get(port->resbuf_get_cb, port->resbuf_cb_data, port);
		if (out_frame && port_cfg->ltm_enable)
			port_cfg->valid_out_frame = 1;
		if (port->port_id == PORT_VID_OUT)
			port_cfg->sync_slw_vid_en = CAM_DISABLE;
	}

	if (out_frame != NULL) {
		if (out_frame->common.is_reserved == 0 && (out_frame->common.buf.mapping_state & CAM_BUF_MAPPING_ISP) == 0) {
			if (out_frame->common.buf.mfd == port->reserved_buf_fd) {
				out_frame->common.buf.size = port->reserve_buf_size;
				pr_debug("out_frame->common.buf.size = %zu, path->reserve_buf_size = %zu\n",
					out_frame->common.buf.size, port->reserve_buf_size);
			} else
				pr_debug("port%d, target_fid %d, out_frame->common.buf.size = %zu\n",
					port->port_id, port_cfg->target_fid, out_frame->common.buf.size);
		}
	}
exit:
	return out_frame;
}

static uint32_t ispport_uframe_fid_get(struct isp_port* port, void *param)
{
	uint32_t *port_fid = NULL;
	struct cam_frame *frame = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	port_fid = VOID_PTR_TO(param, uint32_t);
	*port_fid = CAMERA_RESERVE_FRAME_NUM;
	if (port->uframe_sync) {
		buf_desc.q_ops_cmd = CAM_QUEUE_DEQ_PEEK;
		frame = cam_buf_manager_buf_dequeue(&port->store_unprocess_pool, &buf_desc, port->buf_manager_handle);
		if (!frame)
			return 0;
		*port_fid = frame->common.user_fid;
	}
	return 0;
}

static int ispport_fetch_frame_cycle(struct isp_port *port, void *param)
{
	struct cam_frame *pframe = NULL;
	struct cam_frame *pframe_data = NULL;
	struct isp_port_cfg *port_cfg = NULL;
	uint32_t ret = 0, loop = 0;
	struct camera_buf_get_desc buf_desc = {0};

	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);

	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_IOVA;
	buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
	pframe = cam_buf_manager_buf_dequeue(&port->fetch_unprocess_pool, &buf_desc, port->buf_manager_handle);
	if (pframe == NULL) {
		pr_err("fail to get frame (%p) for port %d\n", pframe, port->port_id);
		ret = -EINVAL;
		goto err;
	}

	pframe_data = (struct cam_frame *)pframe->common.pframe_data;
	if (pframe_data != NULL) {
		ret = cam_buf_manager_buf_status_cfg(&pframe_data->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_ISP);
		if (ret) {
			pr_err("fail to map buf to ISP iommu. port_id %d\n", port->port_id);
			ret = -EINVAL;
			goto err;
		}
	}

	loop = 0;
	do {
		ret = cam_buf_manager_buf_enqueue(&port->fetch_result_pool, pframe, NULL, port->buf_manager_handle);
		if (ret == 0)
			break;
		pr_info_ratelimited("wait for proc queue. loop %d\n", loop);
		/* wait for previous frame proccessed done.*/
		os_adapt_time_usleep_range(600, 2000);
	} while (loop++ < 500);

	if (ret) {
		pr_err("fail to input frame queue, timeout.\n");
		ret = -EINVAL;
		goto err;
	}
	port_cfg->src_frame = pframe;
	return 0;
err:
	if (pframe_data) {
		pframe->common.pframe_data = NULL;
		ispport_frame_ret(pframe_data);
	}
	ispport_frame_ret(pframe);
	port_cfg->src_frame = NULL;
	return ret;
}

static int ispport_store_frameproc(struct isp_port *port, struct cam_frame *out_frame, struct isp_port_cfg *port_cfg)
{
	uint32_t ret = 0, loop = 0;
	uint32_t hw_path_id = -1;
	struct camera_buf_get_desc buf_desc = {0};

	if (out_frame == NULL || !port_cfg->src_frame) {
		pr_err("fail to get out_frame, port id:%d\n", port->port_id);
		return -EINVAL;
	}

	out_frame->common.fid = port_cfg->src_frame->common.fid;
	out_frame->common.sensor_time = port_cfg->src_frame->common.sensor_time;
	out_frame->common.boot_sensor_time = port_cfg->src_frame->common.boot_sensor_time;
	out_frame->common.link_from.port_id = port->port_id;
	out_frame->common.slice_info.slice_num = port->slice_info.slice_num;
	out_frame->common.width = port->size.w;
	out_frame->common.height = port->size.h;
	if (!port->need_post_proc) {
		do {
			if (!out_frame->common.is_reserved) {
				port_cfg->src_frame->common.is_3dnr_close =
					out_frame->common.is_3dnr_close;
				if (out_frame->common.buf.mfd == port->reserved_buf_fd)
					buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_SINGLE_PAGE_IOVA;
				else
					buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_IOVA;
			} else
				buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_SINGLE_PAGE_IOVA;
			buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
			ret = cam_buf_manager_buf_enqueue(&port->store_result_pool, out_frame, &buf_desc, port->buf_manager_handle);
			if (ret == 0)
				break;
			printk_ratelimited(KERN_INFO "wait for output queue. loop %d\n", loop);
			/* wait for previous frame output queue done */
			os_adapt_time_mdelay(1);
		} while (loop++ < 500);
	}

	if (ret) {
		isp_int_common_irq_hw_cnt_trace(port_cfg->hw_ctx_id);
		pr_err("fail to enqueue, hw %d, port %d\n", port_cfg->hw_ctx_id, port->port_id);
		/* ret frame to original queue */
		if (out_frame->common.is_reserved) {
			cam_queue_empty_frame_put(out_frame);
		} else {
			buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
			buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
			ret = cam_buf_manager_buf_enqueue(&port->store_unprocess_pool, out_frame, &buf_desc, port->buf_manager_handle);
			if (ret)
				pr_err("fail to enqueue frame\n");
		}
		return -EINVAL;
	}

	pr_debug("port %d, is_reserved %d iova 0x%lx, user_fid: %d mfd 0x%x size %zu fid: %d\n",
		port->port_id, out_frame->common.is_reserved,
		out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP],
		out_frame->common.user_fid,
		out_frame->common.buf.mfd,
		out_frame->common.buf.size,
		out_frame->common.fid);
	/* config store buffer */
	hw_path_id = isp_port_id_switch(port->port_id);
	if (hw_path_id >= ISP_SPATH_NUM) {
		pr_err("fail to get correct path id\n");
		return -EFAULT;
	}
	ret = isp_hwctx_store_frm_set(port_cfg->pipe_info, hw_path_id, &out_frame->common);
	/* If some error comes then do not start ISP */
	if (ret) {
		pr_err("fail to set store buffer\n");
		return -EINVAL;
	}

	return 0;
}

static int ispport_store_frame_cycle(struct isp_port *port, void *param)
{
	int ret = 0;
	struct isp_port_cfg *port_cfg = NULL;
	struct cam_frame *out_frame = NULL;

	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	out_frame = ispport_out_frame_get(port, port_cfg);
	ret = ispport_store_frameproc(port, out_frame, port_cfg);
	return ret;
}

static inline void ispport_slw960_frame_num_update(struct slowmotion_960fps_info *slw_960desc, struct isp_node_slwm960fps_frame_num *frame_num)
{
	if (frame_num->stage_a_frame_num > 0)
		frame_num->stage_a_frame_num--;
	else if (frame_num->stage_b_frame_num > 0)
		frame_num->stage_b_frame_num--;
	else if (frame_num->stage_c_frame_num > 0)
		frame_num->stage_c_frame_num--;

	if (slw_960desc->slowmotion_stage_a_valid_num && frame_num->stage_c_frame_num == 0) {
		frame_num->stage_a_frame_num = slw_960desc->slowmotion_stage_a_num;
		frame_num->stage_b_frame_num = slw_960desc->slowmotion_stage_b_num;
		frame_num->stage_c_frame_num = slw_960desc->slowmotion_stage_a_num;
	}
}

static int ispport_frame_cycle(struct isp_port *port, void *param)
{
	int ret = 0;
	struct isp_port_cfg *port_cfg = NULL;
	uint32_t valid_out_frame = 0;
	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);

	if (port->type == PORT_TRANSFER_IN)
		ret = ispport_fetch_frame_cycle(port, param);

	if (port->type == PORT_TRANSFER_OUT) {
		switch (port->port_id) {
		case PORT_PRE_OUT:
		case PORT_CAP_OUT:
			ret = ispport_store_frame_cycle(port, param);
			break;
		case PORT_VID_OUT:
			valid_out_frame = port_cfg->valid_out_frame;
			port_cfg->valid_out_frame = 0;
			ret = ispport_store_frame_cycle(port, param);
			if (!ret && port_cfg->uinfo->stage_a_valid_count && port_cfg->valid_out_frame)
				ispport_slw960_frame_num_update(&port_cfg->uinfo->slw_960desc, &port_cfg->uinfo->frame_num);
			port_cfg->valid_out_frame |= valid_out_frame;
			break;
		default:
			pr_err("fail to get port_id:%d or type:%d\n", port->port_id, port->type);
			break;
		}
	}
	return ret;
}

static int ispport_scaler_param_calc(struct img_trim *in_trim,
	struct img_size *out_size, struct yuv_scaler_info *scaler,
	struct img_deci_info *deci)
{
	int ret = 0;
	unsigned int tmp_dstsize = 0;
	unsigned int align_size = 0;
	unsigned int d_max = ISP_SC_COEFF_DOWN_MAX;
	unsigned int u_max = ISP_SC_COEFF_UP_MAX;
	unsigned int f_max = CAM_SCALER_DECI_FAC_MAX;

	pr_debug("in_trim_size_x:%d, in_trim_size_y:%d, out_size_w:%d,out_size_h:%d\n",
		in_trim->size_x, in_trim->size_y, out_size->w, out_size->h);
	/* check input crop limit with max scale up output size(2 bit aligned) */
	if (in_trim->size_x > (out_size->w * d_max * (1 << f_max)) ||
		in_trim->size_y > (out_size->h * d_max * (1 << f_max)) ||
		in_trim->size_x < ISP_DIV_ALIGN_W(out_size->w, u_max) ||
		in_trim->size_y < ISP_DIV_ALIGN_H(out_size->h, u_max)) {
		pr_err("fail to get in_trim %d %d. out _size %d %d, fmax %d, u_max %d\n",
				in_trim->size_x, in_trim->size_y,
				out_size->w, out_size->h, f_max, d_max);
		ret = -EINVAL;
	} else {
		scaler->scaler_factor_in = in_trim->size_x;
		scaler->scaler_ver_factor_in = in_trim->size_y;
		if (in_trim->size_x > out_size->w * d_max) {
			tmp_dstsize = out_size->w * d_max;
			deci->deci_x = cam_zoom_port_deci_factor_get(in_trim->size_x, tmp_dstsize, CAM_SCALER_DECI_FAC_MAX);
			deci->deci_x_eb = 1;
			align_size = (1 << (deci->deci_x + 1)) * ISP_PIXEL_ALIGN_WIDTH;
			in_trim->size_x = (in_trim->size_x) & ~(align_size - 1);
			in_trim->start_x = (in_trim->start_x) & ~(align_size - 1);
			scaler->scaler_factor_in = in_trim->size_x >> (deci->deci_x + 1);
		} else {
			deci->deci_x = 1;
			deci->deci_x_eb = 0;
		}

		if (in_trim->size_y > out_size->h * d_max) {
			tmp_dstsize = out_size->h * d_max;
			deci->deci_y = cam_zoom_port_deci_factor_get(in_trim->size_y, tmp_dstsize, CAM_SCALER_DECI_FAC_MAX);
			deci->deci_y_eb = 1;
			align_size = (1 << (deci->deci_y + 1)) * ISP_PIXEL_ALIGN_HEIGHT;
			in_trim->size_y = (in_trim->size_y) & ~(align_size - 1);
			in_trim->start_y = (in_trim->start_y) & ~(align_size - 1);
			scaler->scaler_ver_factor_in = in_trim->size_y >> (deci->deci_y + 1);
		} else {
			deci->deci_y = 1;
			deci->deci_y_eb = 0;
		}
		pr_debug("end out_size  w %d, h %d\n", out_size->w, out_size->h);

		scaler->scaler_factor_out = out_size->w;
		scaler->scaler_ver_factor_out = out_size->h;
		scaler->scaler_out_width = out_size->w;
		scaler->scaler_out_height = out_size->h;
	}

	return ret;
}

static int ispport_scaler_get(struct isp_port *port,
	struct isp_hw_path_scaler *path, struct img_slice_info *slice_info)
{
	int ret = 0;
	uint32_t is_yuv422 = 0, scale2yuv420 = 0;
	struct img_trim *out_trim = NULL;
	struct yuv_scaler_info *scaler = NULL;

	if (!port || !path || !slice_info) {
		pr_err("fail to get valid input ptr %p, %p, %p\n", port, path, slice_info);
		return -EFAULT;
	}

	scaler = &path->scaler;
	out_trim = &slice_info->out_trim;
	if (port->fmt == CAM_UYVY_1FRAME)
		path->uv_sync_v = 1;
	else
		path->uv_sync_v = 0;
	if (port->fmt == CAM_FULL_RGB14)
		path->path_sel = 2;
	else
		path->path_sel = 0;
	path->frm_deci = 0;

	if (!slice_info->slice_num) {
		path->dst = port->size;
		path->out_trim.start_x = 0;
		path->out_trim.start_y = 0;
		path->out_trim.size_x = port->size.w;
		path->out_trim.size_y = port->size.h;
	} else {
		path->dst.w = out_trim->size_x;
		path->dst.h = out_trim->size_y;
		path->out_trim.start_x = 0;
		path->out_trim.start_y = 0;
		path->out_trim.size_x = out_trim->size_x;
		path->out_trim.size_y = out_trim->size_y;
	}
	path->regular_info.regular_mode = port->regular_mode;
	ret = ispport_scaler_param_calc(&path->in_trim, &path->dst,
		&path->scaler, &path->deci);
	if (ret) {
		pr_err("fail to calc scaler param.\n");
		return ret;
	}

	if ((port->fmt == CAM_YUV422_2FRAME) || (port->fmt == CAM_YVU422_2FRAME))
		is_yuv422 = 1;

	if (((scaler->scaler_ver_factor_in == scaler->scaler_ver_factor_out)
		&& (scaler->scaler_factor_in == scaler->scaler_factor_out)
		&& (is_yuv422 || port->scaler_bypass_ctrl))
		|| port->fmt == CAM_FULL_RGB14) {
		scaler->scaler_bypass = 1;
		if (port->scaler_coeff_ex)
			scaler->work_mode = 2;
	} else {
		scaler->scaler_bypass = 0;

		if (port->scaler_coeff_ex) {
			/*0:yuv422 to 422; 1:yuv422 to 420 2:yuv420 to 420*/
			scaler->work_mode = 2;
			ret = cam_scaler_coeff_calc_ex(scaler);
		}  else {
			scale2yuv420 = is_yuv422 ? 0 : 1;
			ret = cam_scaler_coeff_calc(scaler, scale2yuv420);
		}

		if (ret) {
			pr_err("fail to calc scaler coeff.\n");
			return ret;
		}
	}
	scaler->odata_mode = is_yuv422 ? 0x00 : 0x01;

	return ret;
}

static int ispport_fetch_normal_get(void *cfg_in, void *cfg_out,
		struct cam_frame *frame)
{
	int ret = 0;
	uint32_t width = 0;
	uint32_t trim_offset[3] = { 0 };
	uint32_t mipi_word_num_start[16] = {
		0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5};
	uint32_t mipi_word_num_end[16] = {
		0, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5};
	struct img_size *src = NULL;
	struct isp_port *port = NULL;
	struct img_trim *intrim = NULL;
	struct isp_hw_fetch_info *fetch = NULL;
	struct camera_frame *pframe_data = NULL;

	if (!cfg_in || !cfg_out || !frame) {
		pr_err("fail to get valid input ptr, %p, %p\n", cfg_in, cfg_out);
		return -EFAULT;
	}

	port = VOID_PTR_TO(cfg_in, struct isp_port);
	fetch = (struct isp_hw_fetch_info *)cfg_out;
	src = &port->size;
	if (frame->common.pframe_data)
		pframe_data = &((struct cam_frame *)frame->common.pframe_data)->common;
	fetch->src = *src;
	if (!port->slice_info.slice_num) {
		width = src->w;
		intrim = &port->trim;
		fetch->in_trim = *intrim;
	} else {
		width = frame->common.width;
		intrim = &fetch->in_trim;
	}
	fetch->fetch_fmt = port->fmt;
	fetch->fetch_pyr_fmt = port->pyr_out_fmt;
	fetch->fetch_3dnr_fmt = port->store_3dnr_fmt;
	fetch->bayer_pattern = port->bayer_pattern;
	if (cam_raw_fmt_get(port->fmt))
		fetch->dispatch_color = 0;
	else if (port->fmt == CAM_FULL_RGB10)
		fetch->dispatch_color = 1;
	else
		fetch->dispatch_color = 2;
	fetch->fetch_path_sel = port->fetch_path_sel;
	fetch->addr.addr_ch0 = frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];
	if (pframe_data != NULL)
		fetch->addr_dcam_out.addr_ch0 = pframe_data->buf.iova[CAM_BUF_IOMMUDEV_ISP];
	else
		fetch->addr_dcam_out.addr_ch0 = 0;
	switch (fetch->fetch_fmt) {
	case CAM_YUV422_3FRAME:
		fetch->pitch.pitch_ch0 = src->w;
		fetch->pitch.pitch_ch1 = src->w / 2;
		fetch->pitch.pitch_ch2 = src->w / 2;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x;
		trim_offset[1] = intrim->start_y * fetch->pitch.pitch_ch1 + intrim->start_x / 2;
		trim_offset[2] = intrim->start_y * fetch->pitch.pitch_ch2 + intrim->start_x / 2;
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		fetch->addr.addr_ch2 = fetch->addr.addr_ch1 + fetch->pitch.pitch_ch1 * fetch->src.h;
		break;
	case CAM_YUYV_1FRAME:
	case CAM_UYVY_1FRAME:
	case CAM_YVYU_1FRAME:
	case CAM_VYUY_1FRAME:
		fetch->pitch.pitch_ch0 = src->w * 2;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x * 2;
		break;
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		fetch->pitch.pitch_ch0 = cal_sprd_pitch(width, port->fmt);
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x * 2;
		break;
	case CAM_YUV422_2FRAME:
	case CAM_YVU422_2FRAME:
		fetch->pitch.pitch_ch0 = src->w;
		fetch->pitch.pitch_ch1 = src->w;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x;
		trim_offset[1] = intrim->start_y * fetch->pitch.pitch_ch1 + intrim->start_x;
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		if (pframe_data != NULL)
			fetch->addr_dcam_out.addr_ch1 = pframe_data->buf.iova[CAM_BUF_IOMMUDEV_ISP] + fetch->pitch.pitch_ch0 * fetch->src.h;
		break;
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
		fetch->pitch.pitch_ch0 = src->w;
		fetch->pitch.pitch_ch1 = src->w;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x;
		trim_offset[1] = intrim->start_y * fetch->pitch.pitch_ch1 / 2 + intrim->start_x;
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		if (pframe_data != NULL)
			fetch->addr_dcam_out.addr_ch1 = pframe_data->buf.iova[CAM_BUF_IOMMUDEV_ISP] + fetch->pitch.pitch_ch0 * fetch->src.h;
		pr_debug("y_addr: %x, pitch: %x\n", fetch->addr.addr_ch0, fetch->pitch.pitch_ch0);
		break;
	case CAM_YUV420_2FRAME_10:
	case CAM_YVU420_2FRAME_10:
		fetch->pitch.pitch_ch0 = (src->w * 16 + 127) / 128 * 128 / 8;
		fetch->pitch.pitch_ch1 = (src->w * 16 + 127) / 128 * 128 / 8;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x * 2;
		trim_offset[1] = intrim->start_y * fetch->pitch.pitch_ch1 / 2 + intrim->start_x * 2;
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		if (pframe_data != NULL)
			fetch->addr_dcam_out.addr_ch1 = pframe_data->buf.iova[CAM_BUF_IOMMUDEV_ISP] + fetch->pitch.pitch_ch0 * fetch->src.h;
		break;
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
	{
		uint32_t start_col = intrim->start_x;
		uint32_t start_row = intrim->start_y;
		uint32_t end_col =  intrim->start_x + intrim->size_x - 1;
		fetch->pitch.pitch_ch0 = (src->w * 10 + 127) / 128 * 128 / 8;
		fetch->pitch.pitch_ch1 = (src->w * 10 + 127) / 128 * 128 / 8;
		fetch->mipi_byte_rel_pos = intrim->start_x & 0xf;
		fetch->mipi_word_num = ((end_col + 1) >> 4) * 5
			+ mipi_word_num_end[(end_col + 1) & 0xF]
			- ((start_col + 1) >> 4) * 5
			- mipi_word_num_start[(start_col + 1) & 0xF] + 1;
		fetch->mipi_byte_rel_pos_uv = fetch->mipi_byte_rel_pos;
		fetch->mipi_word_num_uv = fetch->mipi_word_num;
		trim_offset[0] = start_row * fetch->pitch.pitch_ch0 + (start_col >> 2) * 5 +
			(start_col & 0x3);
		trim_offset[1] = (start_row >> 1) * fetch->pitch.pitch_ch1 + (start_col >> 2) * 5 +
			(start_col & 0x3);
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		if (pframe_data != NULL)
			fetch->addr_dcam_out.addr_ch1 = pframe_data->buf.iova[CAM_BUF_IOMMUDEV_ISP] + fetch->pitch.pitch_ch0 * fetch->src.h;
		break;
	}
	case CAM_FULL_RGB10:
		fetch->pitch.pitch_ch0 = src->w * 8;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x * 8;
		break;
	case CAM_RAW_PACK_10:
	{
		uint32_t mipi_byte_info = 0;
		uint32_t mipi_word_info = 0;
		uint32_t start_col = intrim->start_x;
		uint32_t start_row = intrim->start_y;
		uint32_t end_col =  intrim->start_x + intrim->size_x - 1;
		mipi_byte_info = start_col & 0xF;
		mipi_word_info = ((end_col + 1) >> 4) * 5
			+ mipi_word_num_end[(end_col + 1) & 0xF]
			- ((start_col + 1) >> 4) * 5
			- mipi_word_num_start[(start_col + 1) & 0xF] + 1;
		fetch->mipi_byte_rel_pos = mipi_byte_info;
		fetch->mipi_word_num = mipi_word_info;
		fetch->pitch.pitch_ch0 = cal_sprd_pitch(width, fetch->fetch_fmt);
		/* same as slice starts */
		trim_offset[0] = start_row * fetch->pitch.pitch_ch0 + (start_col >> 2) * 5 + (start_col & 0x3);
		break;
	}
	default:
		pr_err("fail to get fetch format: %s\n", camport_fmt_name_get(fetch->fetch_fmt));
		break;
	}

	fetch->addr_hw.addr_ch0 = fetch->addr.addr_ch0 + trim_offset[0];
	fetch->addr_hw.addr_ch1 = fetch->addr.addr_ch1 + trim_offset[1];
	fetch->addr_hw.addr_ch2 = fetch->addr.addr_ch2 + trim_offset[2];
	pr_debug("fetch fmt %s, y_addr: %x, u_addr: %x, cfg_id %d\n",
		camport_fmt_name_get(fetch->fetch_fmt), fetch->addr_hw.addr_ch0, fetch->addr_hw.addr_ch1, fetch->ctx_id);

	return ret;
}

static int ispport_fbdfetch_yuv_get(void *cfg_in, void *cfg_out, struct cam_frame *frame)
{
	int32_t tile_col = 0, tile_row = 0;
	struct isp_fbd_yuv_info *fbd_yuv = NULL;
	struct isp_port *port = NULL;
	struct dcam_compress_cal_para cal_fbc = {0};

	if (!cfg_in || !cfg_out || !frame) {
		pr_err("fail to get valid input ptr, %p, %p\n", cfg_in, cfg_out);
		return -EFAULT;
	}

	fbd_yuv = (struct isp_fbd_yuv_info *)cfg_out;
	port = VOID_PTR_TO(cfg_in, struct isp_port);

	fbd_yuv->fetch_fbd_bypass = 0;
	fbd_yuv->slice_size = port->size;
	fbd_yuv->trim = port->trim;
	tile_col = (fbd_yuv->slice_size.w + ISP_FBD_TILE_WIDTH - 1) / ISP_FBD_TILE_WIDTH;
	tile_row =(fbd_yuv->slice_size.h + ISP_FBD_TILE_HEIGHT - 1) / ISP_FBD_TILE_HEIGHT;

	fbd_yuv->tile_num_pitch = tile_col;
	fbd_yuv->slice_start_pxl_xpt = 0;
	fbd_yuv->slice_start_pxl_ypt = 0;

	cal_fbc.data_bits = cam_data_bits(port->fmt);
	cal_fbc.fbc_info = &frame->common.fbc_info;
	cal_fbc.in = frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];
	if (port->fmt == CAM_YVU420_2FRAME)
		cal_fbc.fmt = CAM_YVU420_2FRAME;
	else if (port->fmt == CAM_YUV420_2FRAME)
		cal_fbc.fmt = CAM_YUV420_2FRAME;
	cal_fbc.height = fbd_yuv->slice_size.h;
	cal_fbc.width = fbd_yuv->slice_size.w;
	cal_fbc.out = &fbd_yuv->hw_addr;
	dcam_if_cal_compressed_addr(&cal_fbc);
	fbd_yuv->buffer_size = cal_fbc.fbc_info->buffer_size;

	/* store start address for slice use */
	fbd_yuv->frame_header_base_addr = fbd_yuv->hw_addr.addr0;
	fbd_yuv->slice_start_header_addr = fbd_yuv->frame_header_base_addr +
			((fbd_yuv->slice_start_pxl_ypt / ISP_FBD_TILE_HEIGHT) * fbd_yuv->tile_num_pitch +
			fbd_yuv->slice_start_pxl_xpt / ISP_FBD_TILE_WIDTH) * 16;
	fbd_yuv->data_bits = cal_fbc.data_bits;

	pr_debug("fetch_fbd: iova:%lx, fid %u 0x%x 0x%x, 0x%x, size %u %u, channel_id:%d, tile_col:%d\n",
			frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP], frame->common.fid, fbd_yuv->hw_addr.addr0,
			fbd_yuv->hw_addr.addr1, fbd_yuv->hw_addr.addr2,
			fbd_yuv->slice_size.w, fbd_yuv->slice_size.h, frame->common.channel_id, fbd_yuv->tile_num_pitch);

	return 0;
}

static int ispport_store_normal_get(struct isp_port *port,
	struct isp_hw_path_store *store_info, struct isp_port_cfg *port_cfg)
{
	int ret = 0;
	uint32_t width = 0, height = 0;
	struct isp_store_info *store = NULL;

	if (!port || !store_info) {
		pr_err("fail to get valid input ptr %p %p\n", port, store_info);
		return -EFAULT;
	}

	store = &store_info->store;
	store->color_fmt = port->fmt;
	store->bypass = 0;
	store->endian = port->data_endian;
	if (store->color_fmt == CAM_FULL_RGB14)
		store->speed_2x = 0;
	else
		store->speed_2x = 1;

	if (cam_data_bits(port->fmt) == CAM_10_BITS)
		store->need_bwd = 0;
	else
		store->need_bwd = 1;

	store->mirror_en = 0;
	store->max_len_sel = 0;
	store->shadow_clr_sel = 1;
	store->shadow_clr = 1;
	store->store_res = 1;
	store->rd_ctrl = 0;
	width = port->size.w;
	height = port->size.h;
	if (!port->slice_info.slice_num)
		store->size = port->size;
	switch (store->color_fmt) {
	case CAM_UYVY_1FRAME:
		store->pitch.pitch_ch0 = width * 2;
		store->total_size = width * height * 2;
		break;
	case CAM_YUV422_2FRAME:
	case CAM_YVU422_2FRAME:
		store->pitch.pitch_ch0 = width;
		store->pitch.pitch_ch1 = width;
		store->total_size = width * height * 2;
		break;
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
		store->pitch.pitch_ch0 = width;
		store->pitch.pitch_ch1 = width;
		store->total_size = width * height * 3 / 2;
		break;
	case CAM_YUV420_2FRAME_10:
	case CAM_YVU420_2FRAME_10:
		store->pitch.pitch_ch0 = width * 2;
		store->pitch.pitch_ch1 = width * 2;
		store->total_size = width * height * 3 / 2;
		break;
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
		store->pitch.pitch_ch0 = width * 10 / 8;
		store->pitch.pitch_ch1 = width * 10 / 8 ;
		store->total_size = width * height * 3 / 2;
		break;
	case CAM_YUV422_3FRAME:
		store->pitch.pitch_ch0 = width;
		store->pitch.pitch_ch1 = width / 2;
		store->pitch.pitch_ch2 = width / 2;
		store->total_size = width * height * 2;
		break;
	case CAM_YUV420_3FRAME:
		store->pitch.pitch_ch0 = width;
		store->pitch.pitch_ch1 = width / 2;
		store->pitch.pitch_ch2 = width / 2;
		store->total_size = width * height * 3 / 2;
		break;
	case CAM_FULL_RGB14:
		store->pitch.pitch_ch0 = width * 8;
		break;
	default:
		pr_err("fail to get support store fmt: %s\n", camport_fmt_name_get(store->color_fmt));
		store->pitch.pitch_ch0 = 0;
		store->pitch.pitch_ch1 = 0;
		store->pitch.pitch_ch2 = 0;
		break;
	}
	pr_debug("store fmt %s, cfg_id %d\n", camport_fmt_name_get(store->color_fmt), store_info->ctx_id);

	return ret;
}

static int ispport_thumb_scaler_get(struct isp_port *port, struct isp_hw_thumbscaler_info *scalerInfo)
{
	int ret = 0;
	uint32_t deci_w = 0;
	uint32_t deci_h = 0;
	uint32_t trim_w, trim_h, temp_w, temp_h;
	uint32_t offset, shift, is_yuv422 = 0;
	struct img_size src, dst;
	uint32_t align_size = 0;

	if (!port || !scalerInfo) {
		pr_err("fail to get valid input ptr %p, scalerInfo %p\n", port, scalerInfo);
		return -EFAULT;
	}

	scalerInfo->scaler_bypass = 0;
	scalerInfo->frame_deci = 0;
	/* y factor & deci */
	src.w = port->trim.size_x;
	src.h = port->trim.size_y;
	dst = port->size;
	ret = isp_drv_trim_deci_info_cal(src.w, dst.w, &temp_w, &deci_w);
	ret |= isp_drv_trim_deci_info_cal(src.h, dst.h, &temp_h, &deci_h);
	if (deci_w == 0 || deci_h == 0)
		return -EINVAL;
	if (ret) {
		pr_err("fail to set thumbscaler ydeci. src %d %d, dst %d %d\n",
					src.w, src.h, dst.w, dst.h);
		return ret;
	}

	scalerInfo->y_deci.deci_x = deci_w;
	scalerInfo->y_deci.deci_y = deci_h;
	if (deci_w > 1)
		scalerInfo->y_deci.deci_x_eb = 1;
	else
		scalerInfo->y_deci.deci_x_eb = 0;
	if (deci_h > 1)
		scalerInfo->y_deci.deci_y_eb = 1;
	else
		scalerInfo->y_deci.deci_y_eb = 0;
	align_size = deci_w * ISP_PIXEL_ALIGN_WIDTH;
	trim_w = (temp_w) & ~(align_size - 1);
	align_size = deci_h * ISP_PIXEL_ALIGN_HEIGHT;
	trim_h = (temp_h) & ~(align_size - 1);
	scalerInfo->y_factor_in.w = trim_w / deci_w;
	scalerInfo->y_factor_in.h = trim_h / deci_h;
	scalerInfo->y_factor_out = port->size;

	if ((port->fmt == CAM_YUV422_2FRAME) || (port->fmt == CAM_YVU422_2FRAME))
		is_yuv422 = 1;

	/* uv factor & deci, input: yuv422(isp pipeline format) */
	shift = is_yuv422 ? 0 : 1;
	scalerInfo->uv_deci.deci_x = deci_w;
	scalerInfo->uv_deci.deci_y = deci_h;
	if (deci_w > 1)
		scalerInfo->uv_deci.deci_x_eb = 1;
	else
		scalerInfo->uv_deci.deci_x_eb = 0;
	if (deci_h > 1)
		scalerInfo->uv_deci.deci_y_eb = 1;
	else
		scalerInfo->uv_deci.deci_y_eb = 0;
	trim_w >>= 1;
	scalerInfo->uv_factor_in.w = trim_w / deci_w;
	scalerInfo->uv_factor_in.h = trim_h / deci_h;
	scalerInfo->uv_factor_out.w = dst.w / 2;
	scalerInfo->uv_factor_out.h = dst.h >> shift;

	scalerInfo->src0.w = port->trim.size_x;
	scalerInfo->src0.h = port->trim.size_y;

	/* y trim */
	trim_w = scalerInfo->y_factor_in.w * scalerInfo->y_deci.deci_x;
	offset = (port->trim.size_x - trim_w) / 2;
	scalerInfo->y_trim.start_x = port->trim.start_x + offset;
	scalerInfo->y_trim.size_x = trim_w;

	trim_h = scalerInfo->y_factor_in.h * scalerInfo->y_deci.deci_y;
	offset = (port->trim.size_y - trim_h) / 2;
	scalerInfo->y_trim.start_y = port->trim.start_y + offset;
	scalerInfo->y_trim.size_y = trim_h;

	scalerInfo->y_src_after_deci = scalerInfo->y_factor_in;
	scalerInfo->y_dst_after_scaler = scalerInfo->y_factor_out;

	/* uv trim */
	trim_w = scalerInfo->uv_factor_in.w * scalerInfo->uv_deci.deci_x;
	offset = (port->trim.size_x / 2 - trim_w) / 2;
	scalerInfo->uv_trim.start_x = port->trim.start_x / 2 + offset;
	scalerInfo->uv_trim.size_x = trim_w;

	trim_h = scalerInfo->uv_factor_in.h * scalerInfo->uv_deci.deci_y;
	offset = (port->trim.size_y - trim_h) / 2;
	scalerInfo->uv_trim.start_y = port->trim.start_y + offset;
	scalerInfo->uv_trim.size_y = trim_h;

	scalerInfo->uv_src_after_deci = scalerInfo->uv_factor_in;
	scalerInfo->uv_dst_after_scaler = scalerInfo->uv_factor_out;
	scalerInfo->odata_mode = is_yuv422 ? 0x00 : 0x01;

	scalerInfo->y_deci.deci_x = isp_drv_deci_factor_cal(scalerInfo->y_deci.deci_x);
	scalerInfo->y_deci.deci_y = isp_drv_deci_factor_cal(scalerInfo->y_deci.deci_y);
	scalerInfo->uv_deci.deci_x = isp_drv_deci_factor_cal(scalerInfo->uv_deci.deci_x);
	scalerInfo->uv_deci.deci_y = isp_drv_deci_factor_cal(scalerInfo->uv_deci.deci_y);

	/* N6pro thumbscaler calculation regulation */
	if (scalerInfo->thumbscl_cal_version == 1) {
		scalerInfo->y_init_phase.w = scalerInfo->y_dst_after_scaler.w / 2;
		scalerInfo->y_init_phase.h = scalerInfo->y_dst_after_scaler.h / 2;
		scalerInfo->uv_src_after_deci.w = scalerInfo->y_src_after_deci.w / 2;
		scalerInfo->uv_src_after_deci.h = scalerInfo->y_src_after_deci.h;
		scalerInfo->uv_dst_after_scaler.w = scalerInfo->y_dst_after_scaler.w / 2;
		scalerInfo->uv_dst_after_scaler.h = scalerInfo->y_dst_after_scaler.h / 2;
		scalerInfo->uv_trim.size_x = scalerInfo->y_trim.size_x / 2;
		scalerInfo->uv_trim.size_y = scalerInfo->y_trim.size_y / 2;
		scalerInfo->uv_init_phase.w = scalerInfo->uv_dst_after_scaler.w / 2;
		scalerInfo->uv_init_phase.h = scalerInfo->uv_dst_after_scaler.h / 2;
		scalerInfo->uv_factor_in.w = scalerInfo->y_factor_in.w / 2;
		scalerInfo->uv_factor_in.h = scalerInfo->y_factor_in.h / 2;
		scalerInfo->uv_factor_out.w = scalerInfo->y_factor_out.w / 2;
		scalerInfo->uv_factor_out.h = scalerInfo->y_factor_out.h / 2;
	}

	pr_debug("deciY %d %d, Yfactor (%d %d) => (%d %d) ytrim (%d %d %d %d)\n",
		scalerInfo->y_deci.deci_x, scalerInfo->y_deci.deci_y,
		scalerInfo->y_factor_in.w, scalerInfo->y_factor_in.h,
		scalerInfo->y_factor_out.w, scalerInfo->y_factor_out.h,
		scalerInfo->y_trim.start_x, scalerInfo->y_trim.start_y,
		scalerInfo->y_trim.size_x, scalerInfo->y_trim.size_y);
	pr_debug("deciU %d %d, Ufactor (%d %d) => (%d %d), Utrim (%d %d %d %d)\n",
		scalerInfo->uv_deci.deci_x, scalerInfo->uv_deci.deci_y,
		scalerInfo->uv_factor_in.w, scalerInfo->uv_factor_in.h,
		scalerInfo->uv_factor_out.w, scalerInfo->uv_factor_out.h,
		scalerInfo->uv_trim.start_x, scalerInfo->uv_trim.start_y,
		scalerInfo->uv_trim.size_x, scalerInfo->uv_trim.size_y);

	pr_debug("my frameY: %d %d %d %d\n",
		scalerInfo->y_src_after_deci.w, scalerInfo->y_src_after_deci.h,
		scalerInfo->y_dst_after_scaler.w,
		scalerInfo->y_dst_after_scaler.h);
	pr_debug("my frameU: %d %d %d %d\n",
		scalerInfo->uv_src_after_deci.w,
		scalerInfo->uv_src_after_deci.h,
		scalerInfo->uv_dst_after_scaler.w,
		scalerInfo->uv_dst_after_scaler.h);

	pr_debug("init_phase: Y(%d %d), UV(%d %d)\n",
		scalerInfo->y_init_phase.w, scalerInfo->y_init_phase.h,
		scalerInfo->uv_init_phase.w, scalerInfo->uv_init_phase.h);

	return ret;
}

static int ispport_fetch_pipeinfo_get(struct isp_port *port, struct isp_port_cfg *port_cfg)
{
	struct isp_pipe_info *pipe_in = NULL;
	uint32_t ret = 0;

	pipe_in = port_cfg->pipe_info;
	pipe_in->fetch.ctx_id = port_cfg->cfg_id;
	pipe_in->fetch.sec_mode = port_cfg->sec_mode;
	if (port->fetch_path_sel == ISP_FETCH_PATH_NORMAL)
		pipe_in->fetch_fbd_yuv.fetch_fbd_bypass = 1;
	if (port_cfg->src_frame->common.slice_info.slice_num)
		ispport_slice_fetch_prepare(port, port_cfg);
	ret = ispport_fetch_normal_get(port, &pipe_in->fetch, port_cfg->src_frame);
	if (ret) {
		pr_err("fail to get pipe fetch info\n");
		return -EFAULT;
	}
	port_cfg->src_size = port->size;
	port_cfg->src_crop = port->trim;
	return ret;
}

static int ispport_store_pipeinfo_get(struct isp_port *port, struct isp_port_cfg *port_cfg)
{
	struct isp_pipe_info *pipe_in = NULL;
	uint32_t ret = 0, path_id = 0;

	path_id = isp_port_id_switch(port->port_id);
	if (path_id == ISP_SPATH_NUM) {
		pr_err("fail to get hw_path_id\n");
		return 0;
	}
	pipe_in = port_cfg->pipe_info;
	pipe_in->store[path_id].ctx_id = port_cfg->cfg_id;
	pipe_in->store[path_id].spath_id = path_id;

	pipe_in->scaler[path_id].ctx_id = port_cfg->cfg_id;
	pipe_in->scaler[path_id].spath_id = path_id;
	pipe_in->scaler[path_id].src.w = port_cfg->src_crop.size_x;
	pipe_in->scaler[path_id].src.h = port_cfg->src_crop.size_y;
	pipe_in->scaler[path_id].in_trim = port->trim;
	pr_debug("path %d scaler in_trim %d %d %d %d\n", path_id,
		port->trim.start_x, port->trim.start_y, port->trim.size_x, port->trim.size_y);

	if (port_cfg->src_frame->common.slice_info.slice_num) {
		pipe_in->scaler[path_id].in_trim = port_cfg->src_frame->common.slice_info.out_trim;
		ispport_slice_store_prepare(port, port_cfg);
	}

	ret = ispport_store_normal_get(port, &pipe_in->store[path_id], port_cfg);
	if (ret) {
		pr_err("fail to get pipe store normal info\n");
		return -EFAULT;
	}

	port->scaler_coeff_ex = port_cfg->scaler_coeff_ex;
	port->scaler_bypass_ctrl = port_cfg->scaler_bypass_ctrl;
	ret = ispport_scaler_get(port, &pipe_in->scaler[path_id],
		&port_cfg->src_frame->common.slice_info);
	if (ret) {
		pr_err("fail to get pipe path scaler info\n");
		return -EFAULT;
	}
	return ret;
}

static int ispport_thumb_pipeinfo_get(struct isp_port *port, struct isp_port_cfg *port_cfg)
{
	struct isp_pipe_info *pipe_in = NULL;
	uint32_t ret = 0, path_id = 0;

	path_id = isp_port_id_switch(port->port_id);
	if (path_id == ISP_SPATH_NUM) {
		pr_err("fail to get hw_path_id\n");
		return 0;
	}
	pipe_in = port_cfg->pipe_info;
	pipe_in->store[path_id].ctx_id = port_cfg->cfg_id;
	pipe_in->store[path_id].spath_id = path_id;
	ret = ispport_store_normal_get(port, &pipe_in->store[path_id], port_cfg);
	if (ret) {
		pr_err("fail to get pipe store normal info\n");
		return -EFAULT;
	}

	pipe_in->thumb_scaler.idx = port_cfg->cfg_id;
	if (port->hw->ip_isp->isphw_abt->thumb_scaler_cal_version)
		pipe_in->thumb_scaler.thumbscl_cal_version = 1;
	ret = ispport_thumb_scaler_get(port, &pipe_in->thumb_scaler);
	if (ret) {
		pr_err("fail to get pipe thumb scaler info\n");
		return -EFAULT;
	}
	return 0;
}

static int ispport_pipeinfo_get(struct isp_port *port, void *param)
{
	int ret = 0;
	struct isp_port_cfg *port_cfg = NULL;

	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	if (port->type == PORT_TRANSFER_IN) {
		port_cfg->pipe_info->fetch_fbd_yuv.ctx_id = port_cfg->cfg_id;
		if (port_cfg->src_frame->common.is_compressed) {
			port->fetch_path_sel = ISP_FETCH_PATH_FBD;
			ispport_fbdfetch_yuv_get(port, &port_cfg->pipe_info->fetch_fbd_yuv, port_cfg->src_frame);
		} else
			port->fetch_path_sel = ISP_FETCH_PATH_NORMAL;
		ret = ispport_fetch_pipeinfo_get(port, port_cfg);
		port_cfg->in_fmt = port->fmt;
	}

	if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->is_work) > 0) {
		switch (port->port_id) {
		case PORT_PRE_OUT:
		case PORT_VID_OUT:
		case PORT_CAP_OUT:
			ret = ispport_store_pipeinfo_get(port, port_cfg);
			break;
		case PORT_THUMB_OUT:
			ret = ispport_thumb_pipeinfo_get(port, port_cfg);
			break;
		default:
			pr_err("fail to get port_id:%d or type:%d\n", port->port_id, port->type);
			break;
		}
	}
	return ret;
}

static int ispport_port_slice_need(struct isp_port *port, void *param)
{
	if (port->type == PORT_TRANSFER_IN) {
		if (port->trim.size_x > g_camctrl.isp_linebuf_len)
			return 1;
	}
	if (port->type == PORT_TRANSFER_OUT) {
		if (port->size.w > g_camctrl.isp_linebuf_len)
			return 1;
	}
	return 0;
}

static int ispport_blksize_get(struct isp_port *port, void *param)
{
	struct isp_port_blksize_desc *size_desc = NULL;

	size_desc = VOID_PTR_TO(param, struct isp_port_blksize_desc);
	if (port->original.src_size.w > 0 && size_desc->ch_id != CAM_CH_CAP) {
		/* for input scaled image */
		size_desc->new_width = port->original.dst_size.w;
		size_desc->new_height = port->original.dst_size.h;
		size_desc->old_width = port->original.src_trim.size_x;
		size_desc->old_height = port->original.src_trim.size_y;
	} else if (port->slice_info.slice_num) {
		size_desc->new_width = port->size.w;
		size_desc->new_height = port->size.h;
		size_desc->old_width = port->size.w;
		size_desc->old_height = port->size.h;
	} else {
		size_desc->old_width = port->trim.size_x;
		size_desc->old_height = port->trim.size_y;
		size_desc->new_width = size_desc->old_width;
		size_desc->new_height = size_desc->old_height;
	}
	size_desc->sn_size = port->sn_size;
	size_desc->src_size = port->size;
	return 0;
}

static int ispport_start_error(struct isp_port *port, void *param)
{
	int ret = 0;
	struct cam_frame *pframe = NULL;
	struct cam_frame *pframe_data = NULL;
	struct isp_port_cfg *port_cfg = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	if (port->type == PORT_TRANSFER_IN) {
		if (port_cfg->out_buf_clear) {
			pframe = cam_buf_manager_buf_dequeue(&port->fetch_unprocess_pool, NULL, port->buf_manager_handle);
		} else {
			buf_desc.q_ops_cmd = port_cfg->result_queue_ops;
			buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
			buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
			pframe = cam_buf_manager_buf_dequeue(&port->fetch_result_pool, &buf_desc, port->buf_manager_handle);
		}

		if (pframe) {
			pframe_data = (struct cam_frame *)pframe->common.pframe_data;
			if (pframe_data != NULL) {
				cam_buf_manager_buf_status_cfg(&pframe_data->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
				pframe->common.pframe_data = NULL;
				port->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, pframe_data, port->data_cb_handle);
			}
			port->data_cb_func(CAM_CB_ISP_RET_SRC_BUF, pframe, port->data_cb_handle);
		}
	}

	if (port->type == PORT_TRANSFER_OUT) {
		buf_desc.q_ops_cmd = CAM_QUEUE_TAIL;
		pframe = cam_buf_manager_buf_dequeue(&port->store_result_pool, &buf_desc, port->buf_manager_handle);
		if (pframe) {
			/* ret frame to original queue */
			if (pframe->common.is_reserved)
				cam_queue_empty_frame_put(pframe);
			else {
				buf_desc.q_ops_cmd = CAM_QUEUE_FRONT;
				ret = cam_buf_manager_buf_enqueue(&port->store_unprocess_pool, pframe, &buf_desc, port->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue frame\n");
			}
		}
	}
	return 0;
}

static int ispport_video_slowmotion(struct isp_port *port, struct isp_port_cfg *port_cfg)
{
	uint32_t ret = 0, hw_path_id = 0, cnt = 0;
	struct cam_frame *out_frame = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	if (port_cfg->vid_valid) {
		pr_debug("target_fid %d uframe_sync %d sync_slw_vid_en %d\n",
			port_cfg->target_fid, port->uframe_sync, port_cfg->sync_slw_vid_en);
		if (port->uframe_sync) {
			if (port_cfg->target_fid != CAMERA_RESERVE_FRAME_NUM && port_cfg->sync_slw_vid_en) {
				buf_desc.q_ops_cmd = CAM_QUEUE_IF;
				buf_desc.filter = ispport_fid_check;
				buf_desc.target_fid = port_cfg->target_fid;
				do {
					if (port_cfg->uinfo->is_node_close == 1) {
						pr_warn("warning: node is closed, stop deq slw vid buffer.\n");
						return 0;
					}
					out_frame = cam_buf_manager_buf_dequeue(&port->store_unprocess_pool, &buf_desc, port->buf_manager_handle);
					if (!out_frame) {
						pr_warn_ratelimited("warning: wait for frame %d\n", cnt);
						cnt++;
						usleep_range(600, 800);
					}
				} while ((!out_frame) && (cnt < TIMEOUT_WAIT_FOR_VALID_BUFFER));
				if (cnt == TIMEOUT_WAIT_FOR_VALID_BUFFER)
					pr_err("fail to wait valid slw vid buffer timeout, target_fid %d\n", port_cfg->target_fid);
				if (out_frame)
					pr_debug("sync success, user_fid=%d, mfd=0x%x\n",
						out_frame->common.user_fid, out_frame->common.buf.mfd);
			}
		} else {
			out_frame = cam_buf_manager_buf_dequeue(&port->store_unprocess_pool, NULL, port->buf_manager_handle);
			pr_debug("vid use valid %p\n", out_frame);
		}
	}

	if (!out_frame) {
		out_frame = ispport_reserved_buf_get(port->resbuf_get_cb, port->resbuf_cb_data, port);
		if (!out_frame) {
			pr_err("fail to get isp reserved buf.\n");
			return -EFAULT;
		}
	} else if (port_cfg->uinfo->stage_a_valid_count)
		ispport_slw960_frame_num_update(&port_cfg->uinfo->slw_960desc, &port_cfg->uinfo->frame_num);

	if (out_frame->common.is_reserved == 0) {
		if (out_frame->common.buf.mfd == port->reserved_buf_fd) {
			out_frame->common.buf.size = port->reserve_buf_size;
			pr_debug("out_frame->common.buf.size = %u, path->reserve_buf_size = %u\n", (int)out_frame->common.buf.size, (int)port->reserve_buf_size);
		} else
			pr_debug("map output buffer %lx\n", out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP]);
	}
	ret = ispport_store_frameproc(port, out_frame, port_cfg);

	hw_path_id = isp_port_id_switch(port->port_id);
	if (hw_path_id == ISP_SPATH_NUM) {
		pr_err("fail to get hw_path_id\n");
		return 0;
	}
	port_cfg->slw->store[hw_path_id] = port_cfg->pipe_info->store[hw_path_id].store;
	return 0;
}

static int ispport_preview_slowmotion(struct isp_port *port, struct isp_port_cfg *port_cfg)
{
	struct cam_frame *out_frame = NULL;
	uint32_t ret = 0, hw_path_id = 0;

	out_frame = ispport_reserved_buf_get(port->resbuf_get_cb, port->resbuf_cb_data, port);
	ret = ispport_store_frameproc(port, out_frame, port_cfg);
	hw_path_id = isp_port_id_switch(port->port_id);
	if (hw_path_id == ISP_SPATH_NUM) {
		pr_err("fail to get hw_path_id\n");
		return 0;
	}
	port_cfg->slw->store[hw_path_id] = port_cfg->pipe_info->store[hw_path_id].store;
	return ret;
}

static int ispport_slowmotion(struct isp_port *port, void *param)
{
	struct isp_port_cfg *port_cfg = NULL;
	uint32_t ret = 0;
	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	if (port->type == PORT_TRANSFER_IN) {
		ret = ispport_fetch_frame_cycle(port, port_cfg);
		if (ret != 0 || port_cfg->src_frame == NULL) {
			pr_err("fail to get frame (%p) for port %d ret %d\n",
				port_cfg->src_frame, port->port_id, ret);
			return ret;
		}
		port_cfg->src_frame->common.width = port->size.w;
		port_cfg->src_frame->common.height = port->size.h;
		ret = isp_hwctx_fetch_frm_set(port_cfg->dev, &port_cfg->pipe_info->fetch, &port_cfg->src_frame->common);
	}

	if (port->type == PORT_TRANSFER_OUT) {
		switch (port->port_id) {
		case PORT_PRE_OUT:
		case PORT_CAP_OUT:
			ret = ispport_preview_slowmotion(port, port_cfg);
			break;
		case PORT_VID_OUT:
			ret = ispport_video_slowmotion(port, port_cfg);
			break;
		default:
			pr_err("fail to get port_id:%d or type:%d\n", port->port_id, port->type);
		}
	}
	return ret;
}

static int ispport_fast_stop(struct isp_port *port, void *param)
{
	struct isp_port_cfg *port_cfg = NULL;
	struct cam_frame *pframe = NULL;
	uint32_t ret = 0, out_buf_queue_cnt = 0, result_queue_cnt = 0;
	struct camera_buf_get_desc buf_desc = {0};

	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	if (port->type == PORT_TRANSFER_IN) {
		if (*(port_cfg->faststop) == 1)
			ispport_start_error(port, port_cfg);

		out_buf_queue_cnt = cam_buf_manager_pool_cnt(&port->fetch_unprocess_pool, port->buf_manager_handle);
		result_queue_cnt = cam_buf_manager_pool_cnt(&port->fetch_result_pool, port->buf_manager_handle);
		if (out_buf_queue_cnt == 0 && result_queue_cnt == 0) {
			*(port_cfg->faststop) = 0;
			complete(port_cfg->faststop_done);
		} else
			*(port_cfg->faststop) = 1;
	}

	if (port->type == PORT_TRANSFER_OUT) {
		pframe = cam_buf_manager_buf_dequeue(&port->store_result_pool, NULL, port->buf_manager_handle);

		if (!pframe) {
			pr_err("fail to get frame from queue. port:%d\n", port->port_id);
			return 0;
		}
		if (unlikely(pframe->common.is_reserved)) {
			cam_queue_empty_frame_put(pframe);
			return ret;
		}
		if (pframe->common.state != ISP_STREAM_POST_PROC) {
			if (pframe->common.buf.mfd == port->reserved_buf_fd) {
				pframe->common.buf.size = port->reserve_buf_size;
				pr_info("pframe->common.buf.size = %u, path->reserve_buf_size = %u\n",
					(int)pframe->common.buf.size, (int)port->reserve_buf_size);
			}
			buf_desc.q_ops_cmd = CAM_QUEUE_FRONT;
			buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
			buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
			ret = cam_buf_manager_buf_enqueue(&port->store_unprocess_pool, pframe, &buf_desc, port->buf_manager_handle);
			if (ret)
				pr_err("fail to enqueue frame\n");
		}
	}
	return ret;
}

static int ispport_irq_fast_stop(struct isp_port *port, void *param)
{
	struct isp_port_cfg *port_cfg = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_frame *pframe_data = NULL;
	uint32_t ret = 0, com_count = 0, out_buf_queue_cnt = 0, result_queue_cnt = 0;
	struct camera_buf_get_desc buf_desc = {0};

	port_cfg = VOID_PTR_TO(param, struct isp_port_cfg);
	if (port->type == PORT_TRANSFER_IN) {
		result_queue_cnt = cam_buf_manager_pool_cnt(
			&port->fetch_result_pool, port->buf_manager_handle);
		if (result_queue_cnt) {
			buf_desc.q_ops_cmd = port_cfg->result_queue_ops;
			buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
			buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
			pframe = cam_buf_manager_buf_dequeue(
				&port->fetch_result_pool,
				&buf_desc,
				port->buf_manager_handle);
			while (pframe) {
				com_count++;
				pframe_data = (struct cam_frame *)pframe->common.pframe_data;
				if (pframe_data != NULL) {
					cam_buf_manager_buf_status_cfg(
						&pframe_data->common.buf,
						CAM_BUF_STATUS_PUT_IOVA,
						CAM_BUF_IOMMUDEV_ISP);
					pframe->common.pframe_data = NULL;
					port->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF,
						pframe_data, port->data_cb_handle);
				}
				port->data_cb_func(CAM_CB_ISP_RET_SRC_BUF,
					pframe, port->data_cb_handle);
				pframe = cam_buf_manager_buf_dequeue(
					&port->fetch_result_pool, &buf_desc,
					port->buf_manager_handle);
			}
			out_buf_queue_cnt = cam_buf_manager_pool_cnt(
				&port->fetch_unprocess_pool, port->buf_manager_handle);
			result_queue_cnt = cam_buf_manager_pool_cnt(
				&port->fetch_result_pool, port->buf_manager_handle);
			if (out_buf_queue_cnt == 0 && result_queue_cnt == 0) {
				*(port_cfg->faststop) = 0;
				complete(port_cfg->faststop_done);
			}
		}
		return com_count;
	}

	if (port->type == PORT_TRANSFER_OUT) {
		buf_desc.q_ops_cmd = CAM_QUEUE_DEL_TAIL;
		pframe = cam_buf_manager_buf_dequeue(
			&port->store_result_pool, &buf_desc, port->buf_manager_handle);
		while (pframe) {
			if (unlikely(pframe->common.is_reserved)) {
				cam_queue_empty_frame_put(pframe);
			} else if (pframe->common.state != ISP_STREAM_POST_PROC) {
				if (pframe->common.buf.mfd == port->reserved_buf_fd) {
					pframe->common.buf.size = port->reserve_buf_size;
					pr_info("pframe->common.buf.size = %d, path->reserve_buf_size = %d\n",
							(int)pframe->common.buf.size,
							(int)port->reserve_buf_size);
				}
				buf_desc.q_ops_cmd = CAM_QUEUE_FRONT;
				buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
				buf_desc.mmu_type = CAM_BUF_IOMMUDEV_ISP;
				ret = cam_buf_manager_buf_enqueue(
					&port->store_unprocess_pool, pframe,
					&buf_desc, port->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue frame\n");
			}
			buf_desc.q_ops_cmd = CAM_QUEUE_DEL_TAIL;
			pframe = cam_buf_manager_buf_dequeue(
				&port->store_result_pool, &buf_desc, port->buf_manager_handle);
		}
	}
	return ret;
}

static int ispport_cfg_callback(void *param, uint32_t cmd, void *handle)
{
	int ret = 0;
	struct isp_port *port = NULL;

	port = VOID_PTR_TO(handle, struct isp_port);
	switch (cmd) {
	case ISP_PORT_SIZE_UPDATE:
		ret = ispport_zoom_size_update(port, param);
		break;
	case ISP_PORT_IRQ_POSTPORC:
		ret = ispport_port_postproc(port, param);
		break;
	case ISP_PORT_SLICE_NEED:
		ret = ispport_port_slice_need(port, param);
		break;
	case ISP_PORT_UFRAME_FID_GET:
		ret = ispport_uframe_fid_get(port, param);
		break;
	case ISP_PORT_FRAME_CYCLE:
		ret = ispport_frame_cycle(port, param);
		break;
	case ISP_PORT_PIPEINFO_GET:
		ret = ispport_pipeinfo_get(port, param);
		break;
	case ISP_PORT_BLKSIZE_GET:
		ret = ispport_blksize_get(port, param);
		break;
	case ISP_PORT_START_ERROR:
		ret = ispport_start_error(port, param);
		break;
	case ISP_PORT_SLOWMOTION:
		ret = ispport_slowmotion(port, param);
		break;
	case ISP_PORT_FAST_STOP:
		ret = ispport_fast_stop(port, param);
		break;
	case ISP_PORT_IRQ_FAST_STOP:
		ret = ispport_irq_fast_stop(port, param);
		break;
	default:
		pr_err("fail to support port type %d\n", cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

uint32_t isp_port_id_switch(uint32_t port_id)
{
	enum isp_sub_path_id hw_path_id = ISP_SPATH_NUM;
	switch (port_id) {
	case PORT_PRE_OUT:
		hw_path_id = ISP_SPATH_CP;
		break;
	case PORT_VID_OUT:
		hw_path_id = ISP_SPATH_VID;
		break;
	case PORT_CAP_OUT:
		hw_path_id = ISP_SPATH_CP;
		break;
	case PORT_THUMB_OUT:
		hw_path_id = ISP_SPATH_FD;
		break;
	default:
		pr_err("fail to get vaild port_id:%d\n", port_id);
	}
	return hw_path_id;
}

int isp_port_param_cfg(void *handle, enum cam_port_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct isp_port *port = NULL;

	if (!handle || !param) {
		pr_err("fail to get input param:handle:0x%p, param:0x%p.\n", handle, param);
		return -1;
	}

	port = VOID_PTR_TO(handle, struct isp_port);
	switch (cmd) {
	case PORT_CFG_UFRAME_SET:
		port->uframe_sync |= *(uint32_t *)param;
		break;
	case PORT_CFG_BUFFER_SET:
		ret = ispport_buf_set(port, param);
		break;
	case PORT_CFG_RESBUF_SET:
		ispport_reserved_buf_set(port, param);
		break;
	case PORT_CFG_BASE_SET:
		ret = ispport_base_cfg(port, param);
		break;
	case PORT_CFG_BUFFER_CLR:
		ret = ispport_bufq_clr(port, param);
		break;
	case PORT_CFG_FMT_SET:
		port->fmt = *(uint32_t *)param;
		break;
	default:
		pr_err("fail to support port type %d\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

void *isp_port_get(uint32_t port_id, struct isp_port_desc *port_desc)
{
	struct isp_port *port = NULL;
	int ret = 0, buffer_num = 0;

	if (!port_desc) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	if (*port_desc->port_dev == NULL) {
		port = cam_buf_kernel_sys_vzalloc(sizeof(struct isp_port));
		if (!port) {
			pr_err("fail to get valid isp port %d\n", port_id);
			return NULL;
		}
	} else {
		port = *port_desc->port_dev;
		if (port_desc->transfer_type == PORT_TRANSFER_OUT && port_id == PORT_VID_OUT)
			port->vid_cap_en = port_desc->vid_cap_en;
		pr_debug("isp port %p has been alloc. port_id %d\n", port, port_id);
		goto exit;
	}

	if (port_desc->is_high_fps == 0)
		buffer_num = ISP_RESULT_Q_LEN;
	else
		buffer_num = ISP_SLW_IN_Q_LEN;

	port->reserved_buf_fd = -1;
	port->resbuf_get_cb = port_desc->resbuf_get_cb;
	port->resbuf_cb_data = port_desc->resbuf_cb_data;
	port->data_cb_func = port_desc->data_cb_func;
	port->data_cb_handle = port_desc->data_cb_handle;
	port->buf_manager_handle = port_desc->buf_manager_handle;
	port->port_cfg_cb_func = ispport_cfg_callback;
	port->type = port_desc->transfer_type;
	port->hw = port_desc->hw;
	if (port_desc->transfer_type == PORT_TRANSFER_IN) {
		port->fmt = port_desc->in_fmt;
		port->bayer_pattern = port_desc->bayer_pattern;
		port->pyr_out_fmt = port_desc->pyr_out_fmt;
		port->store_3dnr_fmt= port_desc->store_3dnr_fmt;
		port->sn_size = port_desc->sn_size;
		port->fetch_path_sel = port_desc->fetch_path_sel;

		/*fetch*/
		ret = cam_buf_manager_pool_reg(NULL, ISP_OUT_BUF_Q_LEN, port->buf_manager_handle);
		if (ret < 0) {
			pr_err("fail to reg pool for port%d\n", port->port_id);
			cam_buf_kernel_sys_vfree(port);
			return NULL;
		}
		port->fetch_unprocess_pool.private_pool_id = ret;

		ret = cam_buf_manager_pool_reg(NULL, buffer_num, port->buf_manager_handle);
		if (ret < 0) {
			pr_err("fail to reg pool for port%s\n", cam_port_name_get(port->port_id));
			cam_buf_manager_pool_unreg(&port->fetch_unprocess_pool, port->buf_manager_handle);
			cam_buf_kernel_sys_vfree(port);
			return NULL;
		}
		port->fetch_result_pool.private_pool_id = ret;
	} else if (port_desc->transfer_type == PORT_TRANSFER_OUT) {
		port->fmt = port_desc->out_fmt;
		port->data_endian = port_desc->endian;
		port->regular_mode = port_desc->regular_mode;
		port->size = port_desc->output_size;
		/*store*/
		ret = cam_buf_manager_pool_reg(NULL, ISP_OUT_BUF_Q_LEN, port->buf_manager_handle);
		if (ret < 0) {
			pr_err("fail to reg pool for port%d\n", port->port_id);
			cam_buf_kernel_sys_vfree(port);
			return NULL;
		}
		port->store_unprocess_pool.private_pool_id = ret;

		ret = cam_buf_manager_pool_reg(NULL, buffer_num, port->buf_manager_handle);
		if (ret < 0) {
			pr_err("fail to reg pool for port%s\n", cam_port_name_get(port->port_id));
			cam_buf_manager_pool_unreg(&port->store_unprocess_pool, port->buf_manager_handle);
			cam_buf_kernel_sys_vfree(port);
			return NULL;
		}
		port->store_result_pool.private_pool_id = ret;
	}

	port->port_id = port_id;
	*port_desc->port_dev = port;
	pr_info("port id %d node_dev %p port_desc->depend_type %d\n",
		port_id, *port_desc->port_dev, port_desc->depend_type);

exit:
	port->data_cb_handle = port_desc->data_cb_handle;
	atomic_set(&port->is_work, 1);
	if (port_desc->depend_type == PORT_SLAVE)
		atomic_set(&port->is_work, 0);
	atomic_inc(&port->user_cnt);
	return port;
}

void isp_port_put(struct isp_port *port)
{

	if (!port) {
		pr_err("fail to get invalid port ptr\n");
		return;
	}

	if (atomic_dec_return(&port->user_cnt) == 0) {
		port->resbuf_get_cb = NULL;
		port->data_cb_func = NULL;
		port->port_cfg_cb_func = NULL;

		if (port->type == PORT_TRANSFER_IN) {
			cam_buf_manager_pool_unreg(&port->fetch_unprocess_pool, port->buf_manager_handle);
			cam_buf_manager_pool_unreg(&port->fetch_result_pool, port->buf_manager_handle);
		} else if (port->type == PORT_TRANSFER_OUT) {
			cam_buf_manager_pool_unreg(&port->store_unprocess_pool, port->buf_manager_handle);
			cam_buf_manager_pool_unreg(&port->store_result_pool, port->buf_manager_handle);
		}

		cam_buf_kernel_sys_vfree(port);
		port = NULL;
		pr_info("isp portfree success\n");
	}

}
