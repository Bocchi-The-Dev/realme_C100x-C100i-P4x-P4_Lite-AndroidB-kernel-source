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

#include "cam_node.h"
#include "dcam_slice.h"
#include "dcam_hwctx.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_FETCH_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static int dcamfetch_cfg_fetch_online_en(struct dcam_fetch_node *node, void *param)
{
	if (!node || !param) {
		pr_err("fail to get valid param %px %px\n", node, param);
		return -EFAULT;
	}
	node->fetch_online_en = *(uint32_t *)param;
	pr_debug("fetch online en %d\n", node->fetch_online_en);

	return 0;
}

static void dcamfetch_nr3_mv_get(struct dcam_hw_context *hw_ctx, struct cam_frame *frame)
{
	int i = 0;

	i = frame->common.fid % DCAM_NR3_MV_MAX;
	frame->common.nr3_me.mv_x = hw_ctx->nr3_mv_ctrl[i].mv_x;
	frame->common.nr3_me.mv_y = hw_ctx->nr3_mv_ctrl[i].mv_y;
	frame->common.nr3_me.project_mode = hw_ctx->nr3_mv_ctrl[i].project_mode;
	frame->common.nr3_me.sub_me_bypass = hw_ctx->nr3_mv_ctrl[i].sub_me_bypass;
	frame->common.nr3_me.src_height = hw_ctx->nr3_mv_ctrl[i].src_height;
	frame->common.nr3_me.src_width = hw_ctx->nr3_mv_ctrl[i].src_width;
	frame->common.nr3_me.valid = hw_ctx->nr3_mv_ctrl[i].valid;
	pr_debug("dcam%d fid %d, valid %d, x %d, y %d, w %u, h %u\n",
		hw_ctx->hw_ctx_id, frame->common.fid, frame->common.nr3_me.valid,
		frame->common.nr3_me.mv_x, frame->common.nr3_me.mv_y,
		frame->common.nr3_me.src_width, frame->common.nr3_me.src_height);
}

static struct cam_frame *dcamfetch_frame_prepare(struct dcam_online_node *node,
			struct dcam_online_port *dcam_port, struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	struct cam_frame *frame = NULL;
 	timespec *ts = NULL;
	uint32_t dev_fid = 0, path_id = 0;

	if (atomic_read(&dcam_port->set_frm_cnt) <= 1) {
		path_id = dcamonline_portid_convert_to_pathid(dcam_port->port_id);
		if (path_id >= DCAM_PATH_MAX)
			pr_err("fail to get correct path id\n");
		else
			pr_warn("warning: %s cnt %d, deci %u, out %u, result %u alter %u\n",
				cam_port_name_get(dcam_port->port_id),
				atomic_read(&dcam_port->set_frm_cnt), hw_ctx->hw_path[path_id].frm_deci,
				cam_buf_manager_pool_cnt(&dcam_port->unprocess_pool, node->buf_manager_handle),
				cam_buf_manager_pool_cnt(&dcam_port->result_pool, node->buf_manager_handle),
				cam_buf_manager_pool_cnt(&dcam_port->alter_pool, node->buf_manager_handle));
		return NULL;
	}

	if (atomic_read(&node->state) != STATE_RUNNING) {
		pr_warn("warning: DCAM%u state %d %s\n", hw_ctx->hw_ctx_id,
			atomic_read(&node->state), cam_port_name_get(dcam_port->port_id));
		return NULL;
	}

	frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, NULL, node->buf_manager_handle);
	if (!frame) {
		pr_err("fail to available output buffer DCAM%u %s\n",
			node->hw_ctx_id, cam_port_name_get(dcam_port->port_id));
		return NULL;
	}
	atomic_dec(&dcam_port->set_frm_cnt);

	if (unlikely(frame->common.is_reserved)) {
		pr_info_ratelimited("DCAM%u %s use reserved buffer\n",
			hw_ctx->hw_ctx_id, cam_port_name_get(dcam_port->port_id));
		cam_queue_empty_frame_put(frame);
		return NULL;
	}

	/* assign same SOF time here for each port */
	dev_fid = frame->common.fid;
	ts = &node->frame_ts[tsid(dev_fid)];
	frame->common.sensor_time.tv_sec = ts->tv_sec;
	frame->common.sensor_time.tv_usec = ts->tv_nsec / NSEC_PER_USEC;
	frame->common.boot_sensor_time = node->frame_ts_boot[tsid(dev_fid)];
	if (dev_fid == 0)
		frame->common.frame_interval_time = 0;
	else
		frame->common.frame_interval_time = frame->common.boot_sensor_time - node->frame_ts_boot[tsid(dev_fid - 1)];

	pr_debug("DCAM%u %s: TX DONE, fid %u, %lld\n", hw_ctx->hw_ctx_id,
		cam_port_name_get(dcam_port->port_id), frame->common.fid, frame->common.frame_interval_time);

	if (!frame->common.boot_sensor_time) {
		pr_info("DCAM%u %s fid %u invalid 0 timestamp\n",
			hw_ctx->hw_ctx_id, cam_port_name_get(dcam_port->port_id), frame->common.fid);
		ret = dcam_online_port_buffer_cfg(dcam_port, frame);
		if (ret) {
			pr_err("fail to set dcam online port outbuf_queue\n");
		}
		frame = NULL;
	}

	return frame;
}

static void dcamfetch_frame_dispatch(void *param, void *handle)
{
	uint32_t ret = 0;
	struct dcam_online_node *node = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_irq_proc *irq_proc = NULL;
	struct cam_frame *frame = NULL;

	node = (struct dcam_online_node *)handle;
	irq_proc = (struct dcam_irq_proc *)param;
	frame = (struct cam_frame *)irq_proc->param;

	if (unlikely(!node || !frame || !IS_VALID_DCAM_PORT_ID(irq_proc->dcam_port_id))) {
		pr_err("fail to get valid param %p %p %d\n", node, frame, irq_proc->dcam_port_id);
		return;
	}

	dcam_port = dcam_online_node_port_get(node, irq_proc->dcam_port_id);
	frame->common.link_from.node_type = CAM_NODE_TYPE_DCAM_ONLINE;
	frame->common.link_from.port_id = irq_proc->dcam_port_id;
	if (irq_proc->type == CAM_CB_DCAM_DATA_DONE) {
		ret = cam_buf_manager_buf_status_cfg(&frame->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_DCAM);
		if (ret) {
			pr_err("fail ro unmap %s buffer.\n", cam_port_name_get(irq_proc->dcam_port_id));
			ret = dcam_port->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, frame, dcam_port->data_cb_handle);
			if (ret) {
				struct cam_buf_pool_id pool_id = {0};
				pool_id.tag_id = CAM_BUF_POOL_ABNORAM_RECYCLE;
				ret = cam_buf_manager_buf_enqueue(&pool_id, frame, NULL, node->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue frame\n");
				pr_err("fail to enqueue %s frame, q_cnt:%d.\n", cam_port_name_get(irq_proc->dcam_port_id), cam_buf_manager_pool_cnt(&dcam_port->unprocess_pool, node->buf_manager_handle));
			}
			return;
		}
	}
	if (dcam_port->data_cb_func)
		dcam_port->data_cb_func(irq_proc->type, frame, dcam_port->data_cb_handle);
	else
		node->data_cb_func(irq_proc->type, frame, node->data_cb_handle);
}

static int dcamfetch_done_proc(void *param, void *handle, struct dcam_hw_context *hw_ctx)
{
	int ret = 0, cnt = 0, i = 0, path_done[DCAM_PATH_MAX] = {0};
	struct dcam_fetch_node *node = NULL;
	struct dcam_online_node *online_node = NULL;
	struct dcam_irq_proc *irq_proc = NULL;
	struct cam_frame *frame = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_online_port *dcam_full_port = NULL;
	uint32_t mv_ready = 0;
	struct camera_buf_get_desc buf_desc = {0};
	struct dcam_offline_slice_info *slice_info = NULL;

	node = (struct dcam_fetch_node *)handle;
	irq_proc = (struct dcam_irq_proc *)param;
	online_node = &node->online_node;
	slice_info = &node->hw_ctx->slice_info;

	if (slice_info->slice_num > 0) {
		pr_debug("dcam%d fetch slice num %d count %d, offline_pre %d\n",
			online_node->hw_ctx_id, slice_info->slice_num, slice_info->slice_count, node->offline_pre_en);
		for (i = DCAM_PATH_FULL; i <= DCAM_PATH_BIN; i++)
			path_done[i] = atomic_read(&node->hw_ctx->path_done[i]);
		if ((node->offline_pre_en && path_done[DCAM_PATH_FULL] == path_done[DCAM_PATH_BIN])
			|| (!node->offline_pre_en && hw_ctx->virtualsensor_dec_int_done == 0)) {
			slice_info->slice_count--;
			complete(&node->slice_done);
		}
		if (path_done[DCAM_PATH_FULL] != slice_info->slice_num
			&& path_done[DCAM_PATH_BIN] != slice_info->slice_num)
			return 0;
	}

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &online_node->port_queue.head, list) {
		if (dcam_port->port_id != irq_proc->dcam_port_id)
			continue;
		switch (irq_proc->dcam_port_id) {
		case PORT_FULL_OUT:
			if (online_node->is_3dnr) {
				mv_ready = online_node->nr3_me.full_path_mv_ready;
				if (mv_ready == 0) {
					online_node->nr3_me.full_path_cnt++;
					goto return_fetch_buf;
				} else if (mv_ready == 1) {
					frame = dcamfetch_frame_prepare(online_node, dcam_port, hw_ctx);
					if (frame == NULL)
						goto return_fetch_buf;

					dcamfetch_nr3_mv_get(hw_ctx, frame);
					online_node->nr3_me.full_path_mv_ready = 0;
					online_node->nr3_me.full_path_cnt = 0;
					pr_debug("dcam %d, fid %d mv_x %d mv_y %d\n", online_node->hw_ctx_id, frame->common.fid, frame->common.nr3_me.mv_x, frame->common.nr3_me.mv_y);
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_DATA_DONE;
					dcamfetch_frame_dispatch(irq_proc, online_node);
				}
			} else {
				if ((frame = dcamfetch_frame_prepare(online_node, dcam_port, hw_ctx))) {
					if (online_node->is_4in1)
						frame->common.irq_type = CAMERA_IRQ_4IN1_DONE;
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_DATA_DONE;
					dcamfetch_frame_dispatch(irq_proc, online_node);
				}
			}
return_fetch_buf:
			frame = CAM_QUEUE_DEQUEUE(&node->proc_queue, struct cam_frame, list);
			if (frame) {
				cam_buf_manager_buf_status_cfg(&frame->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_DCAM);
				if (online_node->data_cb_func)
					online_node->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, frame, online_node->data_cb_handle);
			}
			break;
		case PORT_BIN_OUT:
			cnt = atomic_read(&dcam_port->set_frm_cnt);
			if (cnt <= online_node->slowmotion_count) {
				pr_warn("warning: DCAM%u BIN cnt %d, deci %u, out %u, result %u\n",
					hw_ctx->hw_ctx_id, cnt, hw_ctx->hw_path[DCAM_PATH_BIN].frm_deci,
					cam_buf_manager_pool_cnt(&dcam_port->unprocess_pool, online_node->buf_manager_handle),
					cam_buf_manager_pool_cnt(&dcam_port->result_pool, online_node->buf_manager_handle));
				return ret;
			}
			buf_desc.q_ops_cmd = CAM_QUEUE_DEQ_PEEK;
			frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, &buf_desc, online_node->buf_manager_handle);
			if (unlikely(!frame) || (frame->common.pyr_status && !hw_ctx->dec_all_done)) {
				atomic_dec(&hw_ctx->path_done[DCAM_PATH_BIN]);
				return 0;
			}
			hw_ctx->dec_all_done = 0;
			hw_ctx->dec_layer0_done = 0;

			if (online_node->is_3dnr) {
				mv_ready = online_node->nr3_me.bin_path_mv_ready;
				if (mv_ready == 0) {
					online_node->nr3_me.bin_path_cnt++;
					goto bin_return_fetch_buf;
				} else if (mv_ready == 1) {
					frame = dcamfetch_frame_prepare(online_node, dcam_port, hw_ctx);
					if (frame == NULL) {
						goto bin_return_fetch_buf;
					}
					dcamfetch_nr3_mv_get(hw_ctx, frame);
					online_node->nr3_me.bin_path_mv_ready = 0;
					online_node->nr3_me.bin_path_cnt = 0;
					pr_debug("dcam %d, fid %d mv_x %d mv_y %d\n", online_node->hw_ctx_id, frame->common.fid, frame->common.nr3_me.mv_x, frame->common.nr3_me.mv_y);
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_DATA_DONE;
					dcamfetch_frame_dispatch(irq_proc, online_node);
				}
			} else {
				if ((frame = dcamfetch_frame_prepare(online_node, dcam_port, hw_ctx))) {
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_DATA_DONE;
					dcamfetch_frame_dispatch(irq_proc, online_node);
				}
			}

			i = 0;
			while (++i < online_node->slowmotion_count) {
				irq_proc->param = dcamfetch_frame_prepare(online_node, dcam_port, hw_ctx);
				irq_proc->type = CAM_CB_DCAM_DATA_DONE;
				dcamfetch_frame_dispatch(irq_proc, online_node);
			}
bin_return_fetch_buf:
			dcam_full_port = dcam_online_node_port_get(online_node, PORT_FULL_OUT);
			if (dcam_full_port == NULL || (dcam_full_port && (atomic_read(&dcam_full_port->is_work) < 1
				|| dcam_full_port->port_state == DCAM_PORT_PAUSE || atomic_read(&dcam_full_port->is_shutoff) > 0))) {
				frame = CAM_QUEUE_DEQUEUE(&node->proc_queue, struct cam_frame, list);
				if (frame) {
					cam_buf_manager_buf_status_cfg(&frame->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_DCAM);
					if (online_node->data_cb_func)
						online_node->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, frame, online_node->data_cb_handle);
				}
			}
			break;
		default:
			pr_err("fail to get known node %d, port_id %s\n", online_node->node_id, cam_port_name_get(irq_proc->dcam_port_id));
			ret = -EFAULT;
			break;
		}
	}
	online_node->hw_ctx->sw_all_done &= ~(1 << (irq_proc->dcam_port_id));
	if (!online_node->hw_ctx->sw_all_done) {
		dcam_online_mipi_fetch_reset(&node->online_node);
		complete(&online_node->dev->fetch_done);
	}

	return ret;
}

static int dcamfetch_irq_proc(void *param, void *handle)
{
	int ret = 0, path_id = 0;
	struct dcam_fetch_node *node = NULL;
	struct dcam_online_node *online_node = NULL;
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct dcam_offline_slice_info *slice_info = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid param %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct dcam_fetch_node *)handle;
	irq_proc = (struct dcam_irq_proc *)param;
	slice_info = &node->hw_ctx->slice_info;
	pr_debug("irq proc of %d", irq_proc->of);

	if (!node->hw_ctx) {
		pr_warn("warning: dcam_online_node hw_ctx %p\n", node->hw_ctx);
		return ret;
	}
	hw_ctx = node->hw_ctx;
	online_node = &node->online_node;

	switch (irq_proc->of) {
	case CAP_START_OF_FRAME:
	case CAP_END_OF_FRAME:
	case SN_START_OF_FRAME:
	case SN_END_OF_FRAME:
	case DCAM_INT_ERROR:
		dcam_online_node_irq_proc(param, online_node);
		break;
	case PREV_START_OF_FRAME:
		if (slice_info->slice_count <= 1) {
			dcam_online_node_irq_proc(param, online_node);
			hw_ctx->fid++;
		}
		break;
	case CAP_DATA_DONE:
		if (irq_proc->dcam_port_id == PORT_FULL_OUT
			|| irq_proc->dcam_port_id == PORT_BIN_OUT) {
			path_id = dcamonline_portid_convert_to_pathid(irq_proc->dcam_port_id);
			atomic_inc(&hw_ctx->path_done[path_id]);
			dcamfetch_done_proc(param, handle, hw_ctx);
		} else {
			if (irq_proc->dcam_port_id == PORT_RAW_OUT || irq_proc->is_nr3_done) {
				online_node->hw_ctx->sw_all_done &= ~(1 << irq_proc->dcam_port_id);
				if (!online_node->hw_ctx->sw_all_done) {
					dcam_online_mipi_fetch_reset(online_node);
					complete(&online_node->dev->fetch_done);
				}
			}
			dcam_online_node_irq_proc(param, online_node);
		}
		break;
	default:
		break;
	}
	pr_debug("irq proc done\n");
	return ret;
}

static int dcamfetch_frm_set(struct dcam_fetch_node *node, void *param)
{
	int ret = 0;
	struct cam_frame *pframe = NULL;

	pframe = (struct cam_frame *)param;
	pframe->common.priv_data = node;
	ret = CAM_QUEUE_ENQUEUE(&node->in_queue, &pframe->list);
	if (ret) {
		pr_err("fail to enqueue src buffer cnt %d\n", node->in_queue.cnt);
		return -1;
	}
	complete(&node->thread.thread_com);

	return ret;
}

static void dcamfetch_src_frame_ret(void *param)
{
	struct cam_frame *frame = NULL;
	struct dcam_fetch_node *node = NULL;

	if (!param) {
		pr_err("fail to get valid param.\n");
		return;
	}

	frame = (struct cam_frame *)param;
	node = (struct dcam_fetch_node *)frame->common.priv_data;
	if (!node) {
		pr_err("fail to get valid src_frame node.\n");
		return;
	}

	pr_debug("frame %p, ch_id %d, buf_fd %d\n",
		frame, frame->common.channel_id, frame->common.buf.mfd);

	cam_buf_manager_buf_status_cfg(&frame->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_DCAM);
	node->online_node.data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, frame, node->online_node.data_cb_handle);
}

static struct cam_frame *dcamfetch_cycle_frame(struct dcam_fetch_node *node)
{
	int ret = 0, loop = 0;
	struct cam_frame *pframe = NULL;

	if (!node) {
		pr_err("fail to get valid dcam fetch node\n");
		return NULL;
	}

	pframe = CAM_QUEUE_DEQUEUE(&node->in_queue, struct cam_frame, list);
	if (pframe == NULL) {
		pr_err("fail to get input frame (%p) for node %d\n", pframe, node->online_node.node_id);
		return NULL;
	}

	pr_debug("frame %p, dcam%d ch_id %d.  buf_fd %d\n", pframe,
			node->online_node.hw_ctx_id, pframe->common.channel_id, pframe->common.buf.mfd);
	pr_debug("size %d %d\n", pframe->common.width, pframe->common.height);

	ret = cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_DCAM);
	if (ret) {
		pr_err("fail to map buf to DCAM iommu. ctx %d\n", node->online_node.node_id);
		goto map_err;
	}

	loop = 0;
	do {
		ret = CAM_QUEUE_ENQUEUE(&node->proc_queue, &pframe->list);
		if (ret == 0)
			break;
		pr_info_ratelimited("wait for proc queue. loop %d\n", loop);
		/* wait for previous frame proccessed done.*/
		os_adapt_time_usleep_range(600, 2000);
	} while (loop++ < 500);

	if (ret) {
		pr_err("fail to get proc queue, timeout.\n");
		ret = -EINVAL;
		goto inq_overflow;
	}
	return pframe;

inq_overflow:
	cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_DCAM);

map_err:
	node->online_node.data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, pframe, node->online_node.data_cb_handle);

	return NULL;
}

static int dcamfetch_param_get(struct dcam_fetch_node *node,
		struct cam_frame *pframe)
{
	int ret = 0;
	struct dcam_fetch_info *fetch = NULL;

	fetch = &node->fetch_info;
	fetch->size.w = pframe->common.width;
	fetch->size.h = pframe->common.height;
	fetch->trim.start_x = 0;
	fetch->trim.start_y = 0;
	fetch->trim.size_x = pframe->common.width;
	fetch->trim.size_y = pframe->common.height;
	fetch->addr.addr_ch0 = (uint32_t)pframe->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];

	node->online_node.hw_ctx->hw_fetch.idx = node->online_node.hw_ctx_id;
	node->online_node.hw_ctx->hw_fetch.virtualsensor_pre_sof = 1;
	pr_debug("fetch online en %d\n", node->fetch_online_en);
	node->online_node.hw_ctx->hw_fetch.fetch_online_en = node->fetch_online_en;
	node->online_node.hw_ctx->hw_fetch.fetch_info = &node->fetch_info;
	node->online_node.hw_ctx->hw_fetch.simu_mode = CAM_ENABLE;

	return ret;
}

static int dcamfetch_hw_frame_param_set(struct dcam_hw_context *hw_ctx, void *dev_handle)
{
	int ret = 0;
	struct dcam_pipe_dev *dev = NULL;
	struct cam_hw_info *hw = NULL;

	if (!hw_ctx || !dev_handle) {
		pr_err("fail to get valid ptr %p %p\n", hw_ctx, dev_handle);
		return -EFAULT;
	}
	dev = (struct dcam_pipe_dev *)dev_handle;
	hw = hw_ctx->hw;
	dcam_hwctx_frame_param_set(hw_ctx);
	dcam_hwctx_binning_4in1_set(hw_ctx);

	ret = dcam_hwctx_fetch_set(hw_ctx);
	hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_RECORD_ADDR, hw_ctx);
	return ret;
}

static int dcamfetch_hw_fetch_online_set(struct dcam_online_node *node)
{
	int ret = 0;
	struct dcam_hw_mipi_cap caparg = {0};
	struct cam_hw_info *hw = NULL;

	if (!node) {
		pr_err("fail to get valid hw_ctx\n");
		return -EFAULT;
	}
	hw = node->hw_ctx->hw;
	caparg.idx = node->hw_ctx_id;
	caparg.cap_info = node->cap_info;
	caparg.slowmotion_count = node->slowmotion_count;
	caparg.cap_info.fetch_online_en = CAM_ENABLE;
	hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_MIPI_CAP_SET, &caparg);
	pr_debug("mipi cap fetch online enable\n");

	return ret;
}

static int dcamfetch_slice_proc(struct dcam_fetch_node *node, struct dcam_isp_k_block *pm)
{
	int i = 0, ret = 0;
	struct cam_hw_reg_trace trace = {0};
	struct dcam_fetch_info *fetch = NULL;
	struct dcam_offline_slice_info *slice = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct cam_hw_info *hw = NULL;
	struct dcam_slice_imblance_info slice_imblance = {0};

	fetch = &node->fetch_info;
	hw_ctx = node->online_node.hw_ctx;
	hw = hw_ctx->hw;
	slice = &hw_ctx->slice_info;

	for (i = 0; i < slice->slice_num; i++) {
		ret = wait_for_completion_interruptible_timeout(
				&node->slice_done, DCAM_OFFLINE_TIMEOUT);
		if (ret <= 0) {
			pr_err("fail to wait as dcam%d offline timeout. ret: %d\n", hw_ctx->hw_ctx_id, ret);
			trace.type = NORMAL_REG_TRACE;
			trace.idx = hw_ctx->hw_ctx_id;
			hw->isp_ioctl(hw, ISP_HW_CFG_REG_TRACE, &trace);
			return -EFAULT;
		}

		slice->cur_slice = &slice->slice_trim[i];
		pr_debug("slice%d/%d proc start\n", i, slice->slice_num);

		if (hw->ip_dcam[hw_ctx->hw_ctx_id]->dcamhw_abt->offline_slice_support && slice->slice_num > 1) {
			hw_ctx->slice_proc_mode = FETCH_SLICE_PROC;
			pr_debug("slc%d, (%d %d %d %d)\n", i,
				slice->slice_trim[i].start_x, slice->slice_trim[i].start_y,
				slice->slice_trim[i].size_x, slice->slice_trim[i].size_y);
			dcam_hwctx_slice_set(hw_ctx, fetch, slice);
		}

		slice_imblance.idx = hw_ctx->hw_ctx_id;
		slice_imblance.global_x_start = slice->slice_trim[i].start_x;
		slice_imblance.global_y_start = slice->slice_trim[i].start_y;
		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id,
			DCAM_HW_CFG_SLICE_IMBLANCE_SET, &slice_imblance);

		mutex_lock(&pm->lsc.lsc_lock);
		if (slice->slice_num > 1) {
			if (i == 0) {
				pm->lsc.grid_offset = 0;
				ret = dcam_init_lsc(pm, 0);
				if (ret < 0) {
					mutex_unlock(&pm->lsc.lsc_lock);
					return ret;
				}
			} else if (i == slice->w_slice_num) {
				/* LSC table is divided into two pieces,
				 * grid_offset is the offset address of the lower one,
				 * grid_offset = index * grid_x_num * 4 * sizeof(uint16_t)
				 * index:the height of each cell in the table,
				 * grid_x_num:the number of cells
				 * in the wide direction of the table,
				 * 4:four channels; the type of the table is uint16_t.
				 */
				pm->lsc.grid_offset = pm->lsc.grid_x_index *
				pm->lsc.lens_info.grid_x_num * 4 * sizeof(uint16_t);
				ret = dcam_init_lsc(pm, 0);
				if (ret < 0) {
					mutex_unlock(&pm->lsc.lsc_lock);
					return ret;
				}
			} else {
				ret = dcam_init_lsc_slice(pm, 0);
				if (ret < 0) {
					pr_err("fail to init lsc\n");
					return ret;
				}
			}
		}

		mutex_unlock(&pm->lsc.lsc_lock);

		/* DCAM_CTRL_COEF will always set in dcam_init_lsc() */

		dcam_hwctx_slice_force_copy(hw_ctx, i);

		/* start fetch */
		dcam_hwctx_cfg_fetch_start(hw);
	}
	return 0;
}

static int dcamfetch_node_frame_start(void *param)
{
	int ret = 0, i = 0, time_out = 0, wait_time_flag = 0;
	struct dcam_fetch_node *node = NULL;
	struct dcam_online_node *online_node = NULL;
	struct dcam_online_port *dcam_port = NULL;
	uint32_t path_id = 0;
	struct dcam_hw_path_start *hw_start = NULL;
	struct dcam_hw_path_size *hw_size = NULL;
	struct dcam_hw_cfg_store_addr *hw_store = NULL;
	struct cam_frame *pframe = NULL;
	struct dcam_isp_k_block *pm = NULL;
	struct dcam_offline_slice_info *slice = NULL;
	uint32_t lbuf_width = 0, dev_fid = 0;

	node = (struct dcam_fetch_node *)param;
	online_node = &node->online_node;
	if (!node || !online_node) {
		pr_err("fail to get valid param\n");
		return -EFAULT;
	}

	pr_debug("dcam fetch frame start %p\n", param);
	ret = wait_for_completion_interruptible_timeout(&online_node->dev->fetch_done, DCAM_OFFLINE_TIMEOUT);
	if (ret <= 0) {
		pframe = CAM_QUEUE_DEQUEUE(&node->in_queue, struct cam_frame, list);
		if (!pframe) {
			pr_warn("warning: no frame from in_q node %px\n", node);
			return 0;
		}
		ret = -EFAULT;
		online_node->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, pframe, online_node->data_cb_handle);
		return ret;
	}

	wait_time_flag = WAIT_IN_NORMAL_SCENE;
	time_out = node->hw_ctx->hw->dcam_ioctl(&wait_time_flag, node->hw_ctx->hw_ctx_id,
		DCAM_HW_CFG_FETCH_STATUS_GET, &node->hw_ctx->hw_ctx_id);
	if (time_out > 2000) {
		pr_warn("warning:dcam fetch status is busy. timeout %d\n", time_out);
		pframe = CAM_QUEUE_DEQUEUE(&node->in_queue, struct cam_frame, list);
		if (!pframe) {
			pr_warn("warning: no frame from in_q node %px\n", node);
			return 0;
		}
		ret = -EFAULT;
		online_node->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, pframe, online_node->data_cb_handle);
		return ret;
	}

	slice = &online_node->hw_ctx->slice_info;

	dcam_hwctx_debug_init(online_node->hw_ctx);
	CAM_DEBUG_LOG_WRITE(&online_node->hw_ctx->debug.block_param_log,
		"DUMP dcam fetch hw_ctx_id %d fid %d \n", online_node->hw_ctx->hw_ctx_id, online_node->hw_ctx->fid);
	CAM_DEBUG_LOG_PRINT(&online_node->hw_ctx->debug.block_param_log);

	pframe = dcamfetch_cycle_frame(node);
	if (!pframe)
		goto fetch_input_err;

	pm = &online_node->blk_pm;
	pm->dev = online_node->dev;
	node->hw_ctx->index_to_set = pframe->common.fid + 1;
	dev_fid = pframe->common.fid;
	if (pframe->common.sensor_time.tv_sec || pframe->common.sensor_time.tv_usec) {
		online_node->frame_ts[tsid(dev_fid)].tv_sec = pframe->common.sensor_time.tv_sec;
		online_node->frame_ts[tsid(dev_fid)].tv_nsec = pframe->common.sensor_time.tv_usec * NSEC_PER_USEC;
		online_node->frame_ts_boot[tsid(dev_fid)] = pframe->common.boot_sensor_time;
	}
	ret = dcamfetch_param_get(node, pframe);
	if (ret) {
		pr_err("fail to get dcam online fetch param\n");
		goto fetch_return_buf;
	}

	/* fetch in node may put it into hw ctx */
	slice->fetch_fmt= node->fetch_info.fmt;

	dcam_slice_needed_info_get(online_node->hw_ctx, &lbuf_width, pframe->common.width);

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &online_node->port_queue.head, list) {
		if (atomic_read(&dcam_port->is_work) < 1) {
			pr_debug("dcam offline port %s is not work\n",
				cam_port_name_get(dcam_port->port_id));
			continue;
		}
		path_id = dcamonline_portid_convert_to_pathid(dcam_port->port_id);
		if (path_id >= DCAM_PATH_MAX) {
			pr_err("fail to port %s convert to path id %d\n",
				cam_port_name_get(dcam_port->port_id), path_id);
			continue;
		}
		hw_store = &online_node->hw_ctx->hw_path[path_id].hw_store;
		hw_size = &online_node->hw_ctx->hw_path[path_id].hw_size;
		hw_start = &online_node->hw_ctx->hw_path[path_id].hw_start;

		hw_store->idx = online_node->hw_ctx->hw_ctx_id;
		hw_store->path_id = path_id;
		hw_store->out_fmt = dcam_port->dcamout_fmt;
		hw_store->out_size.h = dcam_port->out_size.h;
		hw_store->out_size.w = dcam_port->out_size.w;
		hw_store->out_pitch = dcam_port->out_pitch;
		hw_store->blk_param = online_node->hw_ctx->blk_pm;

		hw_size->idx = online_node->hw_ctx->hw_ctx_id;
		hw_size->path_id = path_id;
		hw_size->bin_ratio = dcam_port->bin_ratio;
		hw_size->scaler_sel = dcam_port->scaler_sel;
		hw_size->in_size = dcam_port->in_size;
		hw_size->in_trim = dcam_port->in_trim;
		hw_size->out_size = dcam_port->out_size;
		hw_size->out_pitch = dcam_port->out_pitch;
		hw_size->scaler_info = &dcam_port->scaler_info;

		hw_start->idx = online_node->hw_ctx->hw_ctx_id;
		hw_start->path_id = path_id;
		hw_start->slowmotion_count = 0;
		hw_start->pdaf_path_eb = 0;
		hw_start->src_sel = dcam_port->src_sel;
		hw_start->in_trim = dcam_port->in_trim;
		hw_start->endian = dcam_port->endian;
		hw_start->out_fmt = dcam_port->dcamout_fmt;

		if (dcam_port->port_id == PORT_BIN_OUT ||
			dcam_port->port_id == PORT_FULL_OUT ||
			dcam_port->port_id == PORT_RAW_OUT)
			online_node->hw_ctx->sw_all_done |= 1 << (dcam_port->port_id);
		if (online_node->is_3dnr)
			online_node->hw_ctx->sw_all_done |= 1 << (PORT_DCAM_OUT_MAX);
	}

	for (i = DCAM_PATH_FULL; i <= DCAM_PATH_BIN; i++)
		atomic_set(&online_node->hw_ctx->path_done[i], 0);

	dcam_slice_hw_info_set(slice, pframe, lbuf_width, &online_node->blk_pm);
	ret = dcamfetch_hw_frame_param_set(online_node->hw_ctx, online_node->dev);
	if (ret) {
		pr_err("fail to config dcam online hw param\n");
		goto fetch_return_buf;
	}

	if (node->fetch_online_en &&
		online_node->hw_ctx->hw->ip_isp->isphw_abt->fetch_raw_support)
		dcamfetch_hw_fetch_online_set(online_node);

	ret = dcamfetch_slice_proc(node, pm);
	pr_debug("done\n");

	return ret;

fetch_return_buf:
	pframe = CAM_QUEUE_DEQUEUE(&node->proc_queue, struct cam_frame, list);
	cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_DCAM);
	online_node->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, pframe, online_node->data_cb_handle);
fetch_input_err:
	complete(&node->slice_done);
	complete(&online_node->dev->fetch_done);
	return ret;
}

int dcam_fetch_node_blk_param_set(struct dcam_fetch_node *node, void *param)
{
	int ret = 0;

	if (!node || !param) {
		pr_err("fail to get valid param %p, %p\n", node, param);
		return -EFAULT;
	}

	ret = dcam_online_node_blk_param_set(&node->online_node, param);
	return ret;

}

int dcam_fetch_node_state_get(void *handle)
{
	int ret = 0;
	struct dcam_fetch_node *node = (struct dcam_fetch_node *)handle;

	if (!handle) {
		pr_err("fail to get handle\n");
		return -EFAULT;
	}

	ret = dcam_online_node_state_get(&node->online_node);

	return ret;
}

int dcam_fetch_node_port_insert(struct dcam_fetch_node *node, void *param)
{
	int ret = 0;

	ret = dcam_online_node_port_insert(&node->online_node, param);
	return ret;
}

int dcam_fetch_node_cfg_param(void *handle, uint32_t cmd, void *param)
{
	int ret = 0;
	struct dcam_fetch_node *node = NULL;
	struct cam_node_cfg_param *in_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct dcam_fetch_node *)handle;
	in_param = (struct cam_node_cfg_param *)param;

	switch (cmd) {
	case CAM_NODE_CFG_FETCH_BUF:
		ret = dcamfetch_frm_set(node, in_param->param);
		break;
	case CAM_NODE_CFG_STATIS:
	case CAM_NODE_CFG_RECT_GET:
		ret = dcam_online_node_cfg_param(&node->online_node, cmd, in_param->param);
		break;
	case CAM_NODE_CFG_ALTER_BUF:
		ret = dcam_online_node_cfg_alter_buf(&node->online_node, in_param);
		break;
	case CAM_NODE_CFG_ALTER_BUF_CLR:
		ret = dcam_online_node_clr_alter_buf(&node->online_node, in_param);
		break;
	case CAM_NODE_CFG_PORT_RAW_SEL:
		ret = dcam_online_set_raw_sel(&node->online_node, in_param->param);
		break;
	case CAM_NODE_CFG_FETCH_ONLINE_EN:
		ret = dcamfetch_cfg_fetch_online_en(node, in_param->param);
		break;
	case CAM_NODE_CFG_FID_GET:
		ret = dcam_online_node_fid_get(&node->online_node, in_param);
		break;
	default:
		pr_err("fail to support vaild cmd %d\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

int dcam_fetch_share_buf(void *handle, void *param)
{
	int ret = 0;
	struct dcam_fetch_node *node = NULL;

	node = (struct dcam_fetch_node *)handle;
	ret = dcam_online_node_share_buf(&node->online_node, param);
	return ret;
}

int dcam_fetch_node_reset(struct dcam_fetch_node *node, void *param)
{
	int ret = 0;

	node->hw_ctx->is_virtualsensor_proc = 0;
	ret = dcam_online_node_reset(&node->online_node, param);
	return ret;
}

int dcam_fetch_node_request_proc(struct dcam_fetch_node *node, void *param)
{
	int ret = 0;

	if (!node || !param) {
		pr_err("fail to get valid param %p %p\n", node, param);
		return -EFAULT;
	}

	ret = dcam_online_node_request_proc(&node->online_node, param);
	node->hw_ctx = node->online_node.hw_ctx;
	node->hw_ctx->dcam_irq_cb_func = dcamfetch_irq_proc;
	node->hw_ctx->dcam_irq_cb_handle = node;
	node->hw_ctx->is_virtualsensor_proc = 1;
	node->hw_ctx->offline_pre_en = node->offline_pre_en;
	return ret;
}

int dcam_fetch_node_stop_proc(struct dcam_fetch_node *node, void *param)
{
	int ret = 0;

	if (!node || !param) {
		pr_err("fail to get valid param %p %p\n", node, param);
		return -EFAULT;
	}
	if (!node->hw_ctx->dcam_irq_cb_func)
		return ret;

	camthread_stop(&node->thread);
	ret = wait_for_completion_interruptible_timeout(&node->online_node.dev->fetch_done, DCAM_OFFLINE_TIMEOUT);
	if (ret == 0)
		pr_err("fail to wait node %d, timeout.\n", node->online_node.node_id);
	else
		pr_info("wait time %d ms\n", ret);
	node->hw_ctx->is_virtualsensor_proc = 0;
	node->hw_ctx->dcam_irq_cb_func = NULL;
	node->hw_ctx->dcam_irq_cb_handle = NULL;
	node->hw_ctx->offline_pre_en = CAM_DISABLE;

	ret = dcam_online_node_stop_proc(&node->online_node, param);

	return ret;
}

void *dcam_fetch_node_get(uint32_t node_id, struct dcam_fetch_node_desc *param)
{
	int ret = 0;
	struct dcam_fetch_node *node = NULL;
	struct cam_thread_info *thrd = NULL;

	if (!param) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	if (node_id) {
		pr_err("dcam fetch node not support nonzero node id %d\n", node_id);
		return NULL;
	}

	pr_info("node id %d node_dev %p\n", node_id, *param->node_dev);
	if (*param->node_dev == NULL) {
		node = cam_buf_kernel_sys_vzalloc(sizeof(struct dcam_fetch_node));
		if (!node) {
			pr_err("fail to get valid dcam fetch node\n");
			return NULL;
		}
	} else {
		node = *param->node_dev;
		pr_info("dcam fetch node has been alloc %p\n", node);
		goto exit;
	}

	memset(&node->online_node.nr3_me, 0, sizeof(struct nr3_me_data));

	atomic_set(&node->online_node.state, STATE_IDLE);
	atomic_set(&node->online_node.pm_cnt, 0);
	atomic_set(&node->user_cnt, 0);
	atomic_set(&node->online_node.ch_enable_cnt, 0);
	node->online_node.slowmotion_count = param->online_node_desc->slowmotion_count;
	if (!node->online_node.slowmotion_count)
		node->online_node.slw_type = DCAM_SLW_OFF;
	else
		node->online_node.slw_type = DCAM_SLW_AP;
	node->online_node.dcam_slice_mode = param->online_node_desc->dcam_slice_mode;
	node->online_node.is_4in1 = param->online_node_desc->is_4in1;
	node->online_node.is_3dnr = param->online_node_desc->enable_3dnr;
	node->online_node.nr3_frm = NULL;
	node->online_node.cap_info = param->online_node_desc->cap_info;
	node->online_node.is_pyr_rec = param->online_node_desc->is_pyr_rec;
	node->online_node.csi_controller_idx = param->online_node_desc->csi_controller_idx;
	node->online_node.dev = param->online_node_desc->dev;
	node->online_node.hw_ctx_id = DCAM_HW_CONTEXT_MAX;
	node->online_node.alg_type = param->online_node_desc->alg_type;
	node->online_node.param_frame_sync = param->online_node_desc->param_frame_sync;
	node->online_node.buf_manager_handle = param->online_node_desc->buf_manager_handle;
	node->fetch_info.fmt = param->fetch_fmt;
	node->fetch_info.pattern = param->online_node_desc->pattern;
	node->fetch_info.endian = param->online_node_desc->endian;
	pr_debug("pattern %d, endian %d\n", node->fetch_info.pattern, node->fetch_info.endian);

	CAM_QUEUE_INIT(&node->online_node.port_queue, PORT_DCAM_OUT_MAX, NULL);
	node->online_node.node_id = node_id;
	node->online_node.node_type = param->online_node_desc->node_type;
	init_completion(&node->slice_done);
	complete(&node->slice_done);
	init_completion(&node->online_node.dev->fetch_done);
	complete(&node->online_node.dev->fetch_done);
	CAM_QUEUE_INIT(&node->in_queue, DCAM_FETCH_IN_Q_LEN, dcamfetch_src_frame_ret);
	CAM_QUEUE_INIT(&node->proc_queue, DCAM_FETCH_PROC_Q_LEN, dcamfetch_src_frame_ret);
	thrd = &node->thread;
	snprintf(thrd->thread_name, ARRAY_SIZE(thrd->thread_name), "dcam_fetch_node%d", node->online_node.node_id);
	ret = camthread_create(node, thrd, dcamfetch_node_frame_start);
	if (unlikely(ret != 0)) {
		pr_err("fail to create dcam offline node thread\n");
		return NULL;
	}
	if (node->online_node.data_cb_func == NULL) {
		node->online_node.data_cb_func = param->online_node_desc->data_cb_func;
		node->online_node.data_cb_handle = param->online_node_desc->data_cb_handle;
	}
	if (node->online_node.resbuf_get_cb == NULL) {
		node->online_node.resbuf_get_cb = param->online_node_desc->resbuf_get_cb;
		node->online_node.resbuf_cb_data = param->online_node_desc->resbuf_cb_data;
	}
	if (node->online_node.port_cfg_cb_func == NULL) {
		node->online_node.port_cfg_cb_func = param->online_node_desc->port_cfg_cb_func;
		node->online_node.port_cfg_cb_handle = param->online_node_desc->port_cfg_cb_handle;
	}
	if (node->online_node.shutoff_cfg_cb_func == NULL) {
		node->online_node.shutoff_cfg_cb_func = param->online_node_desc->shutoff_cfg_cb_func;
		node->online_node.shutoff_cfg_cb_handle = param->online_node_desc->shutoff_cfg_cb_handle;
	}

	ret = dcam_online_node_pmctx_init(&node->online_node);
	if (ret)
		return NULL;

	dcam_online_node_pmctx_update(&node->online_node.blk_pm, param->online_node_desc->blk_pm);
	*param->node_dev = node;

exit:
	node->offline_pre_en |= param->offline_pre_en;
	node->online_node.is_3dnr |= param->online_node_desc->enable_3dnr;
	atomic_inc(&node->user_cnt);
	pr_info("node id %d offline pre en %d\n", node->online_node.node_id, node->offline_pre_en);
	return node;
}

void dcam_fetch_node_put(struct dcam_fetch_node *node)
{
	if (!node) {
		pr_err("fail to get invalid node ptr\n");
		return;
	}

	CAM_QUEUE_CLEAN(&node->online_node.port_queue, struct dcam_online_port, list);
	CAM_QUEUE_CLEAN(&node->in_queue, struct cam_frame, list);
	CAM_QUEUE_CLEAN(&node->proc_queue, struct cam_frame, list);
	if (atomic_dec_return(&node->user_cnt) == 0) {
		memset(&node->online_node.nr3_me, 0, sizeof(struct nr3_me_data));
		if (atomic_read(&node->online_node.pm_cnt) > 0)
			dcam_online_node_pmctx_deinit(&node->online_node);
		node->online_node.data_cb_func = NULL;
		node->online_node.data_cb_handle = NULL;
		node->online_node.buf_manager_handle = NULL;
		node->online_node.resbuf_get_cb = NULL;
		node->online_node.resbuf_cb_data = NULL;
		node->online_node.port_cfg_cb_func = NULL;
		node->online_node.port_cfg_cb_handle = NULL;
		atomic_set(&node->online_node.state, STATE_INIT);
		pr_info("dcam fetch node %d put success\n", node->online_node.node_id);
		cam_buf_kernel_sys_vfree(node);
		node = NULL;
	}
}
