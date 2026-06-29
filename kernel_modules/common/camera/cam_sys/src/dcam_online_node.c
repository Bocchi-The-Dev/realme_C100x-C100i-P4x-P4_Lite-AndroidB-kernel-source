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
#include <linux/uaccess.h>
#include "cam_debugger.h"
#include "dcam_dummy.h"
#include "cam_statis.h"
#include "dcam_int_common.h"
#include "dcam_hwctx.h"
#include "csi_api.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_ONLINE_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define DCAMONLINE_STATIS_WORK_SET(bypass, node, port_id) ( { \
	struct dcam_online_port *__port = NULL; \
	if ((bypass) == 0) { \
		__port = dcam_online_node_port_get(node, port_id); \
		if (__port) \
			atomic_set(&__port->is_work, 1); \
		else \
			pr_warn("warning:not get the port:%s.\n", cam_port_name_get(port_id)); \
	} \
})

#define DCAM_ONLINE_NODE_SHUTOFF_CALLBACK(dcam_port)  ({ \
	int ret = 0; \
	struct cam_node_cfg_param node_param = {0}; \
	node_param.port_id = (dcam_port)->port_id; \
	node_param.param = &(dcam_port)->is_shutoff; \
	ret = (dcam_port)->shutoff_cb_func(&node_param, (dcam_port)->shutoff_cb_handle); \
	ret; \
})

static void dcamonline_nr3_mv_get(struct dcam_hw_context *hw_ctx, struct cam_frame *frame)
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

static void dcamonline_nr3_store_addr(struct dcam_online_node *node)
{
	struct cam_frame *frame = NULL;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	hw = node->dev->hw;
	hw_ctx = node->hw_ctx;

	if (node->resbuf_get_cb)
		node->resbuf_get_cb((void *)&frame, node->resbuf_cb_data);
	if (frame) {
		pr_debug("use reserved buffer for 3dnr\n");
		frame->common.priv_data = node;
		dcam_hwctx_nr3_store_addr(hw_ctx, frame);
		node->nr3_frm = frame;
	}
}

static int dcamonline_rect_get(struct dcam_online_node *node, void *param)
{
	struct dcam_statis_param * statis_param = (struct dcam_statis_param *)param;
	struct sprd_img_path_rect *p = NULL;
	struct dcam_dev_aem_win *aem_win = NULL;
	struct isp_img_rect *afm_crop = NULL;
	struct dcam_isp_k_block *pm = NULL;
	struct dcam_online_port *port = NULL;

	if ((!node) || (!param)) {
		pr_err("fail to get valid param, dev=%p, param=%p\n", node, param);
		return -EINVAL;
	}

	p = (struct sprd_img_path_rect *)statis_param->param;
	port = dcam_online_node_port_get(node, statis_param->port_id);
	if (!port) {
		pr_err("fail to get dcam online port %s\n", cam_port_name_get(statis_param->port_id));
		return -EINVAL;
	}

	p->trim_valid_rect.x = port->ori_user_trim.start_x;
	p->trim_valid_rect.y = port->ori_user_trim.start_y;
	p->trim_valid_rect.w = port->ori_user_trim.size_x;
	p->trim_valid_rect.h = port->ori_user_trim.size_y;

	pm = &node->blk_pm;
	aem_win = &(pm->aem.win_info);
	afm_crop = &(pm->afm.win_parm.crop_size);
	p->ae_valid_rect.x = aem_win->offset_x;
	p->ae_valid_rect.y = aem_win->offset_y;
	p->ae_valid_rect.w = aem_win->blk_width * aem_win->blk_num_x;
	p->ae_valid_rect.h = aem_win->blk_height * aem_win->blk_num_y;

	p->af_valid_rect.x = afm_crop->x;
	p->af_valid_rect.y = afm_crop->y;
	p->af_valid_rect.w = afm_crop->w;
	p->af_valid_rect.h = afm_crop->h;

	return 0;
}

static struct cam_frame *dcamonline_frame_prepare(struct dcam_online_node *node,
			struct dcam_online_port *dcam_port, struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
 	timespec *ts = NULL;
	uint32_t dev_fid = 0, path_id = 0;
	struct cam_frame *frame = NULL;

	if (atomic_read(&dcam_port->set_frm_cnt) <= 1) {
		path_id = dcamonline_portid_convert_to_pathid(dcam_port->port_id);
		if (path_id >= DCAM_PATH_MAX)
			pr_err("fail to get correct path id port%s\n", cam_port_name_get(dcam_port->port_id));
		else
			pr_warn("warning: %s cnt %d, deci %u, out %u, result %u\n",
				cam_port_name_get(dcam_port->port_id),
				atomic_read(&dcam_port->set_frm_cnt), hw_ctx->hw_path[path_id].frm_deci,
				cam_buf_manager_pool_cnt(&dcam_port->unprocess_pool, node->buf_manager_handle),
				cam_buf_manager_pool_cnt(&dcam_port->result_pool, node->buf_manager_handle));
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
		if (!node->slowmotion_count)
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

static void dcamonline_frame_dispatch(void *param, void *handle)
{
	uint32_t ret = 0;
	struct cam_frame *frame = NULL;
	struct dcam_online_node *node = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_irq_proc *irq_proc = NULL;

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
			pr_err("fail to unmap %s buffer.\n", cam_port_name_get(irq_proc->dcam_port_id));
			ret = dcam_port->data_cb_func(CAM_CB_DCAM_RET_SRC_BUF, frame, dcam_port->data_cb_handle);
			if (ret) {
				struct cam_buf_pool_id pool_id = {0};
				pool_id.tag_id = CAM_BUF_POOL_ABNORAM_RECYCLE;
				ret = cam_buf_manager_buf_enqueue(&pool_id, frame, NULL, node->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue frame\n");
				pr_err("fail to enqueue %s frame, q_cnt:%d.\n", cam_port_name_get(irq_proc->dcam_port_id),
					cam_buf_manager_pool_cnt(&dcam_port->unprocess_pool, node->buf_manager_handle));
			}
			return;
		}
	}

	if (irq_proc->dummy_enable) {
		dcam_port->port_cfg_cb_func(frame, DCAM_PORT_BUFFER_CFG_SET, dcam_port);
		return;
	}

	if (dcam_port->flash_skip_fid == frame->common.fid && dcam_port->flash_skip_fid != 0) {
		pr_debug("flash scene skip frame fid %d\n",frame->common.fid);
		ret = dcam_online_port_buffer_cfg(dcam_port, frame);
		if (ret)
			pr_err("fail to set dcam online port outbuf_queue\n");
		return;
	}

	if (dcam_port->data_cb_func)
		dcam_port->data_cb_func(irq_proc->type, frame, dcam_port->data_cb_handle);
}

static int dcamonline_fmcu_slw_set(struct dcam_online_node *node)
{
	int ret = 0, j = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_online_port *port = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	if (!node) {
		pr_err("fail to get valid dcam_online_node\n");
		return -EFAULT;
	}

	hw = node->dev->hw;
	hw_ctx = node->hw_ctx;

	if (!hw || !hw_ctx) {
		pr_err("fail to check param hw %p, hw_ctx %p\n", hw, hw_ctx);
		return -EFAULT;
	}

	hw_ctx->fmcu->ops->ctx_reset(hw_ctx->fmcu);
	for (j = 0; j < hw_ctx->slowmotion_count; j++) {
		hw_ctx->slw_idx = j;
		CAM_QUEUE_FOR_EACH_ENTRY(port, &node->port_queue.head, list) {
			if (!port)
				continue;
			if (atomic_read(&port->is_work) < 1 || atomic_read(&port->is_shutoff) > 0)
				continue;

			pr_debug("set slow buffer, no.%d frame, port %s\n", j, cam_port_name_get(port->port_id));
			if (port->port_cfg_cb_func) {
				ret = port->port_cfg_cb_func(hw_ctx, DCAM_PORT_SLW_STORE_SET, port);
				if (ret < 0)
					pr_err("fail to set frame for DCAM%u %s , ret %d\n",
						node->hw_ctx_id, cam_port_name_get(port->port_id), ret);
			}
		}

		ret = dcam_hwctx_slw_fmcu_set(hw_ctx, node->hw_ctx_id, j);
		if (ret)
			pr_err("fail to prepare %s slw cmd\n", cam_port_name_get(port->port_id));
	}

	return ret;
}

static uint32_t dcamonline_frame_check(struct dcam_online_port *dcam_port, uint32_t reg_value)
{
	uint32_t frame_addr = 0;
	struct cam_frame *frame = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	buf_desc.q_ops_cmd = CAM_QUEUE_DEQ_PEEK;
	frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, &buf_desc, dcam_port->buf_manager_handle);
	if (unlikely(!frame))
		return 0;

	if (frame->common.is_compressed) {
		struct compressed_addr compressed_addr;
		struct img_size *size = &dcam_port->out_size;
		struct dcam_compress_cal_para cal_fbc;

		cal_fbc.data_bits = cam_data_bits(dcam_port->dcamout_fmt);
		cal_fbc.fbc_info = &frame->common.fbc_info;
		cal_fbc.in = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
		cal_fbc.fmt = dcam_port->dcamout_fmt;
		cal_fbc.height = size->h;
		cal_fbc.width = size->w;
		cal_fbc.out = &compressed_addr;

		dcam_if_cal_compressed_addr(&cal_fbc);
		frame_addr = compressed_addr.addr2;
	} else
		frame_addr = frame->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];

	pr_debug("port %s frame 0x%08x reg 0x%08x cnt %u frame->common.is_reserved %d\n",
			cam_port_name_get(dcam_port->port_id), frame_addr, reg_value,
			cam_buf_manager_pool_cnt(&dcam_port->result_pool, dcam_port->buf_manager_handle), frame->common.is_reserved);

	if (frame->common.is_reserved)
		return 0;

	return frame_addr == reg_value;
}

static int dcamonline_index(struct dcam_online_node *node,
			uint32_t begin, uint32_t num_group, struct dcam_hw_context *hw_ctx)
{
	int ret = 0, j = 0;
	uint32_t count = 0;
	struct list_head head;
	struct cam_q_head *_list = NULL;
	struct cam_frame *frame = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
		count = num_group;
		if (dcam_port->port_id == PORT_BIN_OUT)
			count *= node->slowmotion_count;
		if (atomic_read(&dcam_port->is_work) < 1 || atomic_read(&dcam_port->is_shutoff) > 0)
			return ret;

		if (cam_buf_manager_pool_cnt(&dcam_port->result_pool, dcam_port->buf_manager_handle) < count)
			return ret;

		pr_info("port %s fix %u index to %u\n", cam_port_name_get(dcam_port->port_id), count, begin);

		INIT_LIST_HEAD(&head);

		buf_desc.q_ops_cmd = CAM_QUEUE_DEL_TAIL;
		j = 0;
		while (j++ < count) {
			frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, &buf_desc, dcam_port->buf_manager_handle);
			CAM_QUEUE_LIST_ADD(&frame->list, &head, true);
		}

		j = 0;
		while (j++ < count) {
			_list = list_last_entry(&head, struct cam_q_head, list);
			CAM_QUEUE_LIST_DEL(_list);
			frame = container_of(_list, struct cam_frame, list);
			frame->common.fid = begin - 1;
			if (dcam_port->port_id == PORT_BIN_OUT) {
				frame->common.fid += j;
			} else if (dcam_port->port_id == PORT_AEM_OUT || dcam_port->port_id == PORT_BAYER_HIST_OUT) {
				frame->common.fid += j * node->slowmotion_count;
			} else {
				frame->common.fid += (j - 1) * node->slowmotion_count;
				frame->common.fid += 1;
			}
			hw_ctx->fid = frame->common.fid;
			ret = cam_buf_manager_buf_enqueue(&dcam_port->result_pool, frame, NULL, dcam_port->buf_manager_handle);
			if (ret)
				pr_err("fail to enqueue result_pool, frame id %d\n", frame->common.fid);
		}
	}

	return ret;
}

static enum dcam_fix_result dcamonline_fix_index(struct dcam_online_node *node, void *param,
			struct dcam_hw_context *hw_ctx)
{
	uint32_t cur_cnt = 0, dummy_skip_num = 0;
	uint32_t old_index = 0, begin = 0, end = 0;
	uint32_t old_n = 0, cur_n = 0, old_d = 0, cur_d = 0, cur_rd = 0;
	timespec cur_ts = {0};
	timespec delta_ts = {0};
	ktime_t delta_ns;
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	irq_proc = (struct dcam_irq_proc *)param;
	dummy_skip_num = hw_ctx->dummy_slave ? hw_ctx->dummy_slave->dummy_total_skip_num[hw_ctx->hw_ctx_id] : 0;
	cur_cnt = tsid(hw_ctx->fid + 1 + dummy_skip_num - hw_ctx->recovery_fid);

	/* adjust frame index for current frame */
	if (cur_cnt != irq_proc->frm_cnt) {
		uint32_t diff = DCAM_FRAME_TIMESTAMP_COUNT;

		/*
		 * leave fixing work to next frame to make sure there's enough
		 * time for us in slow motion scene, assuming that next CAP_SOF
		 * is not delayed
		*/
		if (node->slowmotion_count && !node->need_fix) {
			hw_ctx->handled_bits = 0xFFFFFFFF;
			hw_ctx->handled_bits_on_int1 = 0xFFFFFFFF;
			node->need_fix = CAM_ENABLE;
			return DEFER_TO_NEXT;
		}

		diff = diff + irq_proc->frm_cnt - cur_cnt;
		diff &= DCAM_FRAME_TIMESTAMP_COUNT - 1;

		if (hw_ctx->fid)
			old_index = hw_ctx->fid - 1;
		hw_ctx->fid += diff;
		pr_info("DCAM%u adjust index by %u, new %u dummy skip num:%d\n", node->hw_ctx_id, diff, hw_ctx->fid, dummy_skip_num);
	}

	/* record SOF timestamp for current frame */
	node->frame_ts_boot[tsid(hw_ctx->fid)] = os_adapt_time_get_boottime();
	os_adapt_time_get_ts(&node->frame_ts[tsid(hw_ctx->fid)]);
	cur_ts = node->frame_ts[tsid(hw_ctx->fid)];
	PERFORMANCE_DEBUG("fid %d, sof cur_time %lld.%06ld\n",
		hw_ctx->fid, cur_ts.tv_sec, cur_ts.tv_nsec / NSEC_PER_USEC);

	if (irq_proc->frm_cnt == cur_cnt) {
		hw_ctx->index_to_set = hw_ctx->fid + 1;
		return INDEX_FIXED;
	}

	if (!node->slowmotion_count) {
		struct cam_frame *frame = NULL;
		int ret = 0, vote = 0;
		uint32_t reg_value = 0;

		/* fix index for last 1 frame */
		CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
			if (atomic_read(&dcam_port->set_frm_cnt) < 1)
				continue;

			/*
			 * Use BIN or FULL to check if SOF is missing. If SOF is
			 * missing, we should discard TX_DONE accordingly.
			 */
			if (dcam_port->port_id == PORT_BIN_OUT
			    || dcam_port->port_id == PORT_FULL_OUT) {
				reg_value = dcam_port->port_id == PORT_BIN_OUT ?
						irq_proc->bin_addr_value : irq_proc->full_addr_value;
				vote |= dcamonline_frame_check(dcam_port, reg_value);
			}

			buf_desc.q_ops_cmd = CAM_QUEUE_DEL_TAIL;
			frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, &buf_desc, node->buf_manager_handle);
			if (frame == NULL)
				continue;
			frame->common.fid = hw_ctx->fid;
			if (frame->common.is_reserved)
				cam_queue_empty_frame_put(frame);
			else {
				ret = cam_buf_manager_buf_enqueue(&dcam_port->result_pool, frame, NULL, node->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue port %s result_pool\n", cam_port_name_get(dcam_port->port_id));
			}
		}

		if (vote) {
			pr_info("DCAM%u more TX_DONE than SOF\n", hw_ctx->hw_ctx_id);
			hw_ctx->handled_bits = irq_proc->handled_bits;
			hw_ctx->handled_bits_on_int1 = irq_proc->handled_bits_on_int1;
		}
		hw_ctx->index_to_set = hw_ctx->fid + 1;
		return INDEX_FIXED;
	}

	node->need_fix = CAM_DISABLE;

	end = hw_ctx->fid;
	begin = max(rounddown(end, node->slowmotion_count), old_index + 1);
	if (!begin) {
		return INDEX_FIXED;
	}

	/* restore timestamp and index for slow motion */
	if (old_index == 0) {
		pr_err("fail to get old_index\n");
		return INDEX_FIXED;
	}
	delta_ns = os_adapt_time_sub(node->frame_ts_boot[tsid(old_index)],
			     node->frame_ts_boot[tsid(old_index - 1)]);
	delta_ts = os_adapt_time_timespec_sub(node->frame_ts[tsid(old_index)],
				node->frame_ts[tsid(old_index - 1)]);

	while (--end >= begin) {
		node->frame_ts_boot[tsid(end)]
			= os_adapt_time_sub_ns(node->frame_ts_boot[tsid(end + 1)],
				       delta_ns);
		node->frame_ts[tsid(end)]
			= os_adapt_time_timespec_sub(node->frame_ts[tsid(end + 1)],
				       delta_ts);
	}

	/* still in-time if index not delayed to another group */
	old_d = old_index / node->slowmotion_count;
	cur_d = hw_ctx->fid / node->slowmotion_count;
	if (old_d == cur_d) {
		return INDEX_FIXED;
	}

	old_n = old_index % node->slowmotion_count;
	cur_n = hw_ctx->fid % node->slowmotion_count;
	cur_rd = rounddown(hw_ctx->fid, node->slowmotion_count);
	if (old_n != node->slowmotion_count - 1) {
		/* fix index for last 1~8 frames */
		hw_ctx->handled_bits = irq_proc->handled_bits;
		hw_ctx->handled_bits_on_int1 = irq_proc->handled_bits_on_int1;
		dcamonline_index(node, cur_rd, 2, hw_ctx);
		return BUFFER_READY;
	} else {
		/* fix index for last 1~4 frames */
		dcam_port = dcam_online_node_port_get(node, PORT_BIN_OUT);
		if (cam_buf_manager_pool_cnt(&dcam_port->result_pool, node->buf_manager_handle)
		    <= node->slowmotion_count) {
			/*
			 * ignore TX DONE if already handled in last interrupt
			 */
			hw_ctx->handled_bits = irq_proc->handled_bits;
			hw_ctx->handled_bits_on_int1 = irq_proc->handled_bits_on_int1;
		}
		dcamonline_index(node, cur_rd, 1, hw_ctx);
		return INDEX_FIXED;
	}
}

static void dcamonline_sof_event_dispatch(struct dcam_online_node *node, struct dcam_hw_context *hw_ctx)
{
	struct cam_frame *frame = NULL;
	timespec *ts = NULL;

	if (!node) {
		pr_err("fail to get valid input dcam_online_node\n");
		return;
	}

	pr_debug("DCAM%u cap_sof enter fid %d\n", hw_ctx->hw_ctx_id, hw_ctx->fid);

	CAM_DEBUG_LOG_WRITE(&hw_ctx->debug.block_param_log,
		"DUMP dcam cfg param hw_ctx_id %d fid %d \n",
		hw_ctx->hw_ctx_id, hw_ctx->fid + 1);

	frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	if (frame) {
		ts = &node->frame_ts[tsid(hw_ctx->fid)];
		frame->common.boot_sensor_time = node->frame_ts_boot[tsid(hw_ctx->fid)];
		frame->common.sensor_time.tv_sec = ts->tv_sec;
		frame->common.sensor_time.tv_usec = ts->tv_nsec / NSEC_PER_USEC;
		frame->common.evt = IMG_TX_DONE;
		frame->common.irq_type = CAMERA_IRQ_DONE;
		frame->common.irq_property = IRQ_DCAM_SOF;
		frame->common.fid = hw_ctx->fid;
		node->data_cb_func(CAM_CB_DCAM_IRQ_EVENT, frame, node->data_cb_handle);
	}
}

static void dcamonline_cap_sof(struct dcam_online_node *node, void *param,
			struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	uint32_t path_id = 0, slm_path = 0, idx = 0;
	unsigned long flag = 0;
	enum dcam_fix_result fix_result = {0};
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_auto_copy copyarg = {0};
	struct dcam_hw_path_ctrl path_ctrl = {0};
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_irq_proc *irq_proc = NULL;

	hw = hw_ctx->hw;
	idx = hw_ctx->hw_ctx_id;
	irq_proc = (struct dcam_irq_proc *)param;
	slm_path = hw->ip_dcam[idx]->dcamhw_abt->slm_path;
	fix_result = dcamonline_fix_index(node, param, hw_ctx);
	if (fix_result == DEFER_TO_NEXT)
		return;

	pr_debug("DCAM%u cap_sof enter\n", hw_ctx->hw_ctx_id);

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
		if (node->slowmotion_count) {
			uint32_t n = hw_ctx->fid % node->slowmotion_count;
			/*
			 *set at first frame of a group of slowmotion frames
			 *when the path support slm_path &
			 *set other paths buffers at last frame of a group of slowmotion frames
			 */
			if (((n != node->slowmotion_count - 1)
				&& !(slm_path & BIT(dcam_port->port_id)))
				|| (n && (slm_path & BIT(dcam_port->port_id)))
				|| fix_result == BUFFER_READY)
				continue;

			hw_ctx->index_to_set = hw_ctx->fid + node->slowmotion_count;
		}

		if (atomic_read(&dcam_port->is_work) < 1)
			continue;

		ret = DCAM_ONLINE_NODE_SHUTOFF_CALLBACK(dcam_port);
		if (ret) {
			pr_debug("port %s shutoff is 1, skip.\n", cam_port_name_get(dcam_port->port_id));
			continue;
		}

		/* @frm_deci is the frame index of output frame */
		path_id = dcamonline_portid_convert_to_pathid(dcam_port->port_id);
		if (path_id >= DCAM_PATH_MAX) {
			pr_err("fail to get correct path id\n");
			continue;
		}
		if ((dcam_port->frm_deci_cnt++ >= hw_ctx->hw_path[path_id].frm_deci)
			|| node->slowmotion_count) {
			dcam_port->frm_deci_cnt = 0;
			if (dcam_port->port_id == PORT_FULL_OUT) {
				spin_lock_irqsave(&dcam_port->state_lock, flag);
				if (dcam_port->port_state == DCAM_PORT_PAUSE
					&& dcam_port->state_update) {
					atomic_inc(&dcam_port->set_frm_cnt);
					path_ctrl.idx = node->hw_ctx_id;
					path_ctrl.path_id = DCAM_PATH_FULL;
					path_ctrl.type = HW_DCAM_PATH_PAUSE;
					hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_PATH_CTRL, &path_ctrl);
				} else if (dcam_port->port_state == DCAM_PORT_RESUME
					&& dcam_port->state_update) {
					path_ctrl.idx = node->hw_ctx_id;
					path_ctrl.path_id = DCAM_PATH_FULL;
					path_ctrl.type = HW_DCAM_PATH_RESUME;
					hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_PATH_CTRL, &path_ctrl);
				}
				dcam_port->state_update = 0;
				if (dcam_port->port_state == DCAM_PORT_PAUSE) {
					spin_unlock_irqrestore(&dcam_port->state_lock, flag);
					continue;
				}
				spin_unlock_irqrestore(&dcam_port->state_lock, flag);
			}

			if ((node->slw_type != DCAM_SLW_FMCU) && (dcam_port->port_cfg_cb_func))
				dcam_port->port_cfg_cb_func(hw_ctx, DCAM_PORT_STORE_SET, dcam_port);
		}
	}

	copyarg.id = DCAM_CTRL_ALL;
	copyarg.idx = node->hw_ctx_id;
	copyarg.glb_reg_lock = hw_ctx->glb_reg_lock;
	hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_AUTO_COPY, &copyarg);
	hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_RECORD_ADDR, hw_ctx);

	if (!irq_proc->not_dispatch && (!node->slowmotion_count
		|| !hw_ctx->fid || (!((hw_ctx->fid + 1) % node->slowmotion_count)))) {
		dcamonline_sof_event_dispatch(node, hw_ctx);
	}
	hw_ctx->iommu_status = 0xffffffff;
	hw_ctx->fid++;
}

static void dcamonline_preview_sof(struct dcam_online_node *node, struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	struct dcam_online_port *dcam_port = NULL;

	hw_ctx->fid += node->slowmotion_count;
	pr_debug("DCAM%u fid: %u\n", node->hw_ctx_id, hw_ctx->fid);

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
		if (atomic_read(&dcam_port->is_work) < 1 || atomic_read(&dcam_port->is_shutoff) > 0)
			continue;
		/* frame deci is deprecated in slow motion */
		ret = DCAM_ONLINE_NODE_SHUTOFF_CALLBACK(dcam_port);
		if (ret) {
			pr_debug("port %s shutoff is 1, skip.\n", cam_port_name_get(dcam_port->port_id));
			continue;
		}
		dcam_port->port_cfg_cb_func(hw_ctx, DCAM_PORT_STORE_SET, dcam_port);
	}

	dcamonline_sof_event_dispatch(node, hw_ctx);
}

static void dcamonline_sensor_sof(struct dcam_online_node *node)
{
	struct cam_frame *pframe = NULL;
	struct cam_hw_info *hw = NULL;

	if (!node) {
		pr_err("fail to get valid input dcam_online_node\n");
		return;
	}

	hw = node->dev->hw;
	pr_debug("DCAM%d sn_sof\n", node->hw_ctx_id);
	if (node->cap_info.ipg_skip_first_frm == 0)
		return;

	pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	if (pframe) {
		pframe->common.evt = IMG_TX_DONE;
		pframe->common.irq_type = CAMERA_IRQ_DONE;
		pframe->common.irq_property = IRQ_DCAM_SN_SOF;
		node->data_cb_func(CAM_CB_DCAM_IRQ_EVENT, pframe, node->data_cb_handle);
	};

	hw->dcam_ioctl(hw, node->hw_ctx->hw_ctx_id,
		DCAM_HW_CFG_DIS_SN_SOF, &node->hw_ctx->hw_ctx_id);
}

static void dcamonline_sensor_eof(struct dcam_online_node *node)
{
	struct cam_frame *pframe = NULL;
	struct cam_hw_info *hw = NULL;

	if (!node) {
		pr_err("fail to get valid input dcam_online_node\n");
		return;
	}

	pr_debug("DCAM%d sn_eof\n", node->hw_ctx_id);

	pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	if (pframe) {
		pframe->common.evt = IMG_TX_DONE;
		pframe->common.irq_type = CAMERA_IRQ_DONE;
		pframe->common.irq_property = IRQ_DCAM_SN_EOF;
		node->data_cb_func(CAM_CB_DCAM_IRQ_EVENT, pframe, node->data_cb_handle);
	};

	hw = node->dev->hw;
	if (node->cap_info.ipg_skip_first_frm)
		hw->dcam_ioctl(hw, node->hw_ctx->hw_ctx_id,
		DCAM_HW_CFG_DIS_SN_EOF, &node->hw_ctx->hw_ctx_id);
}

static int dcamonline_slw_frame_set(struct dcam_online_node *node,
	struct dcam_irq_proc *irq_proc, struct dcam_hw_context *hw_ctx)
{
	int ret = 0, i = 0;
	struct cam_frame *frame = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct isp_dev_hist2_info *p = NULL;

	while (i++ < node->slowmotion_count) {
		CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
			if (!dcam_port)
				continue;
			if (atomic_read(&dcam_port->is_work) < 1 ||
				atomic_read(&dcam_port->is_shutoff) > 0)
				continue;
			switch (dcam_port->port_id) {
			case PORT_BIN_OUT:
				frame = dcamonline_frame_prepare(
					node, dcam_port, hw_ctx);
				if (frame) {
					dcamonline_nr3_mv_get(hw_ctx, frame);
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_DATA_DONE;
					irq_proc->dcam_port_id = dcam_port->port_id;
					dcamonline_frame_dispatch(irq_proc, node);
				}
				break;
			case PORT_AFL_OUT:
				frame = dcamonline_frame_prepare(
					node, dcam_port, hw_ctx);
				if (frame) {
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
					irq_proc->dcam_port_id = dcam_port->port_id;
					dcamonline_frame_dispatch(irq_proc, node);
				}
				break;
			case PORT_FRGB_HIST_OUT:
				frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
				if (frame) {
					p = &node->blk_pm.hist_roi.hist_roi_info;
					frame->common.width = p->hist_roi.end_x & ~1;
					frame->common.height = p->hist_roi.end_y & ~1;
					pr_debug("w %d, h %d\n",
						frame->common.width, frame->common.height);
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
					irq_proc->dcam_port_id = dcam_port->port_id;
					dcamonline_frame_dispatch(irq_proc, node);
				}
				break;
			default:
				if (i != node->slowmotion_count - 1)
					break;
				frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
				if (frame) {
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
					irq_proc->dcam_port_id = dcam_port->port_id;
					dcamonline_frame_dispatch(irq_proc, node);
				}
				break;
			}
		}
	}
	return ret;
}
static int dcamonline_slw_fmcu_process(struct dcam_online_node *node,
	struct dcam_irq_proc *irq_proc, struct dcam_hw_context *hw_ctx)
{
	int ret = 0;
	uint32_t  slw_mv_cnt = 0;
	struct cam_frame *frame = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct isp_dev_hist2_info *p = NULL;

	if (irq_proc->slw_cmds_set) {
		ret = dcamonline_fmcu_slw_set(node);
		return 0;
	}
	if (irq_proc->is_nr3_done) {
		node->nr3_me.slw_mv_cnt ++;
		pr_debug("dcam %d, bin_path_cnt %d, slw_mv_cnt %d",
			node->hw_ctx_id, node->nr3_me.bin_path_cnt,
			node->nr3_me.slw_mv_cnt);
		if (node->nr3_me.bin_path_cnt &&
			node->nr3_me.slw_mv_cnt == node->slowmotion_count) {
			dcamonline_slw_frame_set(node, irq_proc, hw_ctx);
			node->nr3_me.bin_path_cnt = 0;
			node->nr3_me.slw_mv_cnt = 0;
		}
	}

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
		if (!dcam_port)
			continue;
		if (irq_proc->is_nr3_done == 1)
			continue;
		if (atomic_read(&dcam_port->is_work) < 1 || atomic_read(&dcam_port->is_shutoff) > 0)
			continue;

		switch (dcam_port->port_id) {
		case PORT_BIN_OUT:
			if (node->is_3dnr) {
				slw_mv_cnt = node->nr3_me.slw_mv_cnt;
				if (slw_mv_cnt < node->slowmotion_count) {
					node->nr3_me.bin_path_cnt++;
					return ret;
				} else {
					frame = dcamonline_frame_prepare(
						node, dcam_port, hw_ctx);
					if (frame) {
						dcamonline_nr3_mv_get(hw_ctx, frame);
						irq_proc->param = frame;
						irq_proc->type = CAM_CB_DCAM_DATA_DONE;
						irq_proc->dcam_port_id = dcam_port->port_id;
						dcamonline_frame_dispatch(irq_proc, node);
					}
					if (irq_proc->slw_count == node->slowmotion_count) {
						node->nr3_me.bin_path_cnt = 0;
						node->nr3_me.slw_mv_cnt = 0;
					}
				}
			} else {
				frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
				if (frame) {
					irq_proc->param = frame;
					irq_proc->type = CAM_CB_DCAM_DATA_DONE;
					irq_proc->dcam_port_id = dcam_port->port_id;
					dcamonline_frame_dispatch(irq_proc, node);
				}
			}
			break;
		case PORT_FRGB_HIST_OUT:
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (frame) {
				p = &node->blk_pm.hist_roi.hist_roi_info;
				frame->common.width = p->hist_roi.end_x & ~1;
				frame->common.height = p->hist_roi.end_y & ~1;
				pr_debug("w %d, h %d\n", frame->common.width, frame->common.height);
				irq_proc->param = frame;
				irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
				irq_proc->dcam_port_id = dcam_port->port_id;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			break;
		case PORT_AFL_OUT:
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (frame) {
				irq_proc->param = frame;
				irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
				irq_proc->dcam_port_id = dcam_port->port_id;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			break;
		default:
			if (irq_proc->slw_count != node->slowmotion_count)
				break;
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (frame) {
				irq_proc->param = frame;
				irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
				irq_proc->dcam_port_id = dcam_port->port_id;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			break;
		}
	}

	return 0;
}

static int dcamonline_done_full_process(struct dcam_irq_proc *irq_proc,
	struct dcam_online_node *node, struct dcam_hw_context *hw_ctx,
	struct dcam_online_port *dcam_port)
{
	int ret = 0;
	uint32_t mv_ready = 0;
	struct cam_frame *frame = NULL;

	if (node->is_3dnr) {
		mv_ready = node->nr3_me.full_path_mv_ready;
		if (irq_proc->dummy_enable) {
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (frame) {
				irq_proc->param = frame;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			return ret;
		}
		if (mv_ready == 0) {
			node->nr3_me.full_path_cnt++;
			return ret;
		} else if (mv_ready == 1) {
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (frame == NULL) {
				node->nr3_me.full_path_mv_ready = 0;
				node->nr3_me.full_path_cnt = 0;
				return ret;
			}

			dcamonline_nr3_mv_get(hw_ctx, frame);
			node->nr3_me.full_path_mv_ready = 0;
			node->nr3_me.full_path_cnt = 0;
			pr_debug("dcam %d,fid %d mv_x %d mv_y %d",
				node->hw_ctx_id, frame->common.fid,
				frame->common.nr3_me.mv_x,
				frame->common.nr3_me.mv_y);
			irq_proc->param = frame;
			irq_proc->type = CAM_CB_DCAM_DATA_DONE;
			dcamonline_frame_dispatch(irq_proc, node);
		}
	} else {
		frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
		if (frame) {
			if (node->is_4in1)
				frame->common.irq_type = CAMERA_IRQ_4IN1_DONE;
			irq_proc->param = frame;
			irq_proc->type = CAM_CB_DCAM_DATA_DONE;
			dcamonline_frame_dispatch(irq_proc, node);
		}
	}
	return 0;
}

static int dcamonline_done_bin_process(struct dcam_irq_proc *irq_proc,
	struct dcam_online_node *node, struct dcam_hw_context *hw_ctx,
	struct dcam_online_port *dcam_port)
{
	int ret = 0, cnt = 0, i = 0;
	uint32_t mv_ready = 0, slw_mv_cnt = 0;
	struct cam_frame *frame = NULL;
	struct cam_frame *frame1 = NULL;
	struct camera_buf_get_desc buf_desc = {0};

	cnt = atomic_read(&dcam_port->set_frm_cnt);
	if (cnt <= node->slowmotion_count) {
		pr_warn("warning: DCAM%u BIN cnt %d, deci %u, out %u, result %u\n",
			hw_ctx->hw_ctx_id, cnt, hw_ctx->hw_path[DCAM_PATH_BIN].frm_deci,
			cam_buf_manager_pool_cnt(
				&dcam_port->unprocess_pool, node->buf_manager_handle),
			cam_buf_manager_pool_cnt(
				&dcam_port->result_pool, node->buf_manager_handle));
		return ret;
	}
	buf_desc.q_ops_cmd = CAM_QUEUE_DEQ_PEEK;
	frame = cam_buf_manager_buf_dequeue(
		&dcam_port->result_pool, &buf_desc, node->buf_manager_handle);
	if (unlikely(!frame) || (frame->common.pyr_status && !hw_ctx->dec_all_done))
		return 0;
	hw_ctx->dec_all_done = 0;
	hw_ctx->dec_layer0_done = 0;

	if (node->is_3dnr) {
		if (irq_proc->dummy_enable) {
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (frame) {
				irq_proc->param = frame;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			return ret;
		}
		mv_ready = node->nr3_me.bin_path_mv_ready;
		slw_mv_cnt = node->nr3_me.slw_mv_cnt;
		pr_debug("mv_ready %d, slw_mv_cnt %d", mv_ready, slw_mv_cnt);
		if (mv_ready == 0 || (node->slowmotion_count &&
			slw_mv_cnt < node->slowmotion_count)) {
			node->nr3_me.bin_path_cnt++;
			goto exit;
		} else {
			node->nr3_me.bin_path_mv_ready = 0;
			node->nr3_me.bin_path_cnt = 0;
			if (!node->slowmotion_count) {
				frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
				if (frame == NULL)
					 return ret;
				dcamonline_nr3_mv_get(hw_ctx, frame);
				pr_debug("dcam %d,fid %d mv_x %d mv_y %d", node->hw_ctx_id,
					frame->common.fid, frame->common.nr3_me.mv_x,
					frame->common.nr3_me.mv_y);
				irq_proc->param = frame;
				irq_proc->type = CAM_CB_DCAM_DATA_DONE;
				dcamonline_frame_dispatch(irq_proc, node);
			}else{
				for (i = 0; i < node->slowmotion_count; i++) {
					frame1 = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
					if (frame1 == NULL)
						continue;
					node->nr3_me.slw_mv_cnt = 0;
					dcamonline_nr3_mv_get(hw_ctx, frame1);
					irq_proc->param = frame1;
					irq_proc->type = CAM_CB_DCAM_DATA_DONE;
					dcamonline_frame_dispatch(irq_proc, node);
				}
			}
			return ret;
		}
	} else {
		frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
		if (frame) {
			irq_proc->param = frame;
			irq_proc->type = CAM_CB_DCAM_DATA_DONE;
			dcamonline_frame_dispatch(irq_proc, node);
		}
	}

	i = 0;
	while (++i < node->slowmotion_count) {
		irq_proc->param = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
		irq_proc->type = CAM_CB_DCAM_DATA_DONE;
		dcamonline_frame_dispatch(irq_proc, node);
	}

exit:
	return 0;
}

static int dcamonline_done_proc(void *param, void *handle,
	struct dcam_hw_context *hw_ctx)
{
	int ret = 0, i = 0;
	uint32_t *buf = NULL;
	uint32_t sum = 0, w = 0, h = 0;
	struct dcam_hw_gtm_hist gtm_hist = {0};
	struct cam_hw_info *hw = NULL;
	struct cam_frame *frame = NULL;
	struct cam_frame *frame1 = NULL;
	struct isp_dev_hist2_info *p = NULL;
	struct dcam_online_node *node = NULL;
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_pipe_dev *dcam_dev = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_offline_slice_info *slice_info = NULL;

	hw = hw_ctx->hw;
	node = (struct dcam_online_node *)handle;
	irq_proc = (struct dcam_irq_proc *)param;
	slice_info = &node->hw_ctx->slice_info;

	if (node->slw_type == DCAM_SLW_FMCU) {
		dcamonline_slw_fmcu_process(node, irq_proc, hw_ctx);
		return ret;
	}

	if (irq_proc->is_nr3_done) {
		node->nr3_me.full_path_mv_ready = 1;
		node->nr3_me.bin_path_mv_ready = 1;
		if (node->slowmotion_count)
			node->nr3_me.slw_mv_cnt ++;

		pr_debug("dcam %d full_path_cnt %d bin_path_cnt %d, slw_mv_cnt %d", node->hw_ctx_id,
			node->nr3_me.full_path_cnt, node->nr3_me.bin_path_cnt, node->nr3_me.slw_mv_cnt);

		if (node->nr3_me.full_path_cnt && !node->slowmotion_count) {
			dcam_port = dcam_online_node_port_get(node, PORT_FULL_OUT);
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (frame == NULL) {
				node->nr3_me.full_path_mv_ready = 0;
				node->nr3_me.full_path_cnt = 0;
				return ret;
			}
			dcamonline_nr3_mv_get(hw_ctx, frame);
			irq_proc->param = frame;
			irq_proc->type = CAM_CB_DCAM_DATA_DONE;
			irq_proc->dcam_port_id = PORT_FULL_OUT;
			dcamonline_frame_dispatch(irq_proc, node);
			node->nr3_me.full_path_mv_ready = 0;
			node->nr3_me.full_path_cnt = 0;
		}
		if ((node->nr3_me.bin_path_cnt && !node->slowmotion_count) || (node->nr3_me.bin_path_cnt &&
			node->slowmotion_count && node->nr3_me.slw_mv_cnt == node->slowmotion_count)) {
			dcam_port = dcam_online_node_port_get(node, PORT_BIN_OUT);
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (frame == NULL) {
				node->nr3_me.bin_path_mv_ready = 0;
				node->nr3_me.bin_path_cnt = 0;
				return ret;
			}
			dcamonline_nr3_mv_get(hw_ctx, frame);
			irq_proc->param = frame;
			irq_proc->type = CAM_CB_DCAM_DATA_DONE;
			irq_proc->dcam_port_id = PORT_BIN_OUT;
			dcamonline_frame_dispatch(irq_proc, node);
			node->nr3_me.bin_path_mv_ready = 0;
			node->nr3_me.bin_path_cnt = 0;

			i = 0;
			while (++i < node->slowmotion_count) {
				dcam_port = dcam_online_node_port_get(node, PORT_BIN_OUT);
				frame1 = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
				if (frame1 == NULL)
					continue;
				dcamonline_nr3_mv_get(hw_ctx, frame1);
				irq_proc->param = frame1;
				irq_proc->type = CAM_CB_DCAM_DATA_DONE;
				irq_proc->dcam_port_id = PORT_BIN_OUT;
				dcamonline_frame_dispatch(irq_proc, node);
				node->nr3_me.slw_mv_cnt = 0;
			}
		}
		return ret;
	}

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
		if (dcam_port->port_id != irq_proc->dcam_port_id)
			continue;
		switch (irq_proc->dcam_port_id) {
		case PORT_FULL_OUT:
			dcamonline_done_full_process(irq_proc, node, hw_ctx, dcam_port);
			break;
		case PORT_BIN_OUT:
			dcamonline_done_bin_process(irq_proc, node, hw_ctx, dcam_port);
			break;
		case PORT_RAW_OUT:
			if ((frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx))) {
				irq_proc->param = frame;
				irq_proc->type = CAM_CB_DCAM_DATA_DONE;
				if (node->is_4in1)
					frame->common.irq_type = CAMERA_IRQ_4IN1_DONE;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			break;
		case PORT_VCH2_OUT:
			if ((frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx))) {
				dcam_port = dcam_online_node_port_get(node, PORT_VCH2_OUT);
				if (dcam_port->raw_src != ORI_RAW_SRC_SEL)
					irq_proc->type = CAM_CB_DCAM_DATA_DONE;
				else
					irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
				irq_proc->param = frame;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			break;
		case PORT_AFL_OUT:
			dcam_port->port_cfg_cb_func(hw_ctx, DCAM_PORT_STORE_SET, dcam_port);
			if ((frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx))) {
				frame->common.fid = hw_ctx->index_to_set - 1;
				irq_proc->param = frame;
				irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			break;
		case PORT_AEM_OUT:
		case PORT_AFM_OUT:
		case PORT_LSCM_OUT:
		case PORT_PDAF_OUT:
		case PORT_VCH3_OUT:
		case PORT_BAYER_HIST_OUT:
			if ((frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx))) {
				irq_proc->param = frame;
				irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			break;
		case PORT_FRGB_HIST_OUT:
			if ((frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx))) {
				p = &node->blk_pm.hist_roi.hist_roi_info;
				frame->common.width = p->hist_roi.end_x & ~1;
				frame->common.height = p->hist_roi.end_y & ~1;
				pr_debug("w %d, h %d\n", frame->common.width, frame->common.height);
				irq_proc->param = frame;
				irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
				dcamonline_frame_dispatch(irq_proc, node);
			}
			break;
		case PORT_GTM_HIST_OUT:
			frame = dcamonline_frame_prepare(node, dcam_port, hw_ctx);
			if (!frame) {
				ret = -EFAULT;
				break;
			}

			pr_debug("dcam hw ctx id %d, frame mfd %d\n", node->hw_ctx_id, frame->common.buf.mfd);

			buf = (uint32_t *)frame->common.buf.addr_k;
			if (!buf) {
				pr_err("fail to get frame buf\n");
				ret = -EFAULT;
				break;
			}

			dcam_dev = node->dev;
			if (!dcam_dev) {
				pr_err("fail to get dev, hw ctx id%d\n", node->hw_ctx_id);
				ret = -EFAULT;
				break;
			}

			gtm_hist.idx = hw_ctx->hw_ctx_id;
			gtm_hist.value = buf;
			hw->dcam_ioctl(hw, gtm_hist.idx, DCAM_HW_CFG_GTM_HIST_GET, &gtm_hist);

			sum = buf[GTM_HIST_ITEM_NUM];
			w = hw_ctx->cap_info.cap_size.size_x;
			h = hw_ctx->cap_info.cap_size.size_y;
			if (sum != (w * h)) {
				pr_debug("pixel num check wrong, fid %d, sum %d, should be %d\n", frame->common.fid, sum, (w * h));
				ret = cam_buf_manager_buf_enqueue(&dcam_port->unprocess_pool, frame, NULL, node->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue unprocess_pool\n");
				break;
			}
			buf[GTM_HIST_ITEM_NUM + 1] = frame->common.fid;
			irq_proc->param = frame;
			irq_proc->type = CAM_CB_DCAM_STATIS_DONE;
			dcamonline_frame_dispatch(irq_proc, node);
			pr_debug("success get frame w %d, h %d, user_fid %d, mfd %d, fid %d\n", node->cap_info.cap_size.size_x,
				node->cap_info.cap_size.size_y, frame->common.user_fid, frame->common.buf.mfd, frame->common.fid);
			break;
		default:
			pr_err("fail to get known node %d, port_id %s\n", node->node_id, cam_port_name_get(irq_proc->dcam_port_id));
			ret = -EFAULT;
			break;
		}
	}
	return ret;
}

static int dcamonline_ctx_bind(struct dcam_online_node *node)
{
	int ret = 0;
	uint32_t slw_cnt = 0;

	if (!node) {
		pr_err("fail to get valid dcam online node\n");
		return -EFAULT;
	}

	pr_debug("online ctx_bind enter\n");
	spin_lock(&node->dev->ctx_lock);
	slw_cnt = node->slowmotion_count;
	ret = node->dev->dcam_pipe_ops->bind(node->dev, node, node->node_id, node->csi_controller_idx,
			slw_cnt, &node->hw_ctx_id, &node->slw_type);
	if (ret) {
		pr_warn_ratelimited("warning: bind ctx %d fail\n", node->node_id);
		goto exit;
	}

	node->hw_ctx = &node->dev->hw_ctx[node->hw_ctx_id];
	node->hw_ctx->slowmotion_count = node->slowmotion_count;
	node->hw_ctx->dcam_slice_mode = node->dcam_slice_mode;
	node->hw_ctx->cap_info = node->cap_info;
	node->hw_ctx->is_pyr_rec = node->is_pyr_rec;
	node->hw_ctx_id = node->hw_ctx->hw_ctx_id;
	node->blk_pm.idx = node->hw_ctx_id;
	node->hw_ctx->dcam_irq_cb_func = dcam_online_node_irq_proc;
	node->hw_ctx->dcam_irq_cb_handle = node;
	node->hw_ctx->blk_pm = &node->blk_pm;
	node->hw_ctx->fid = 0;

exit:
	spin_unlock(&node->dev->ctx_lock);
	pr_info("csi control idx%d, hw_ctx_id %d, node_id %d online ctx_bind done\n", node->csi_controller_idx,
		node->hw_ctx_id, node->node_id);
	return ret;
}

static int dcamonline_ctx_unbind(struct dcam_online_node *node)
{
	int ret = 0;

	if (!node) {
		pr_err("fail to get valid dcam online node\n");
		return -EFAULT;
	}

	pr_debug("online ctx_unbind enter\n");
	spin_lock(&node->dev->ctx_lock);
	ret = node->dev->dcam_pipe_ops->unbind(node->dev, node, node->node_id);
	if (ret) {
		pr_err("fail to context_unbind\n");
		goto exit;
	}
	if (node->hw_ctx == NULL)
		goto exit;

	node->hw_ctx->dcam_slice_mode = CAM_SLICE_NONE;
	node->hw_ctx->dcam_irq_cb_func = NULL;
	node->hw_ctx->dcam_irq_cb_handle = NULL;
	node->hw_ctx_id = DCAM_HW_CONTEXT_MAX;
	node->blk_pm.idx = DCAM_HW_CONTEXT_MAX;
	node->hw_ctx->blk_pm = NULL;
	node->hw_ctx->slowmotion_count = 0;
	node->hw_ctx->fid = 0;
	node->hw_ctx->recovery_fid = 0;
	node->hw_ctx->is_pyr_rec = 0;
	memset(&node->hw_ctx->cap_info, 0, sizeof(struct dcam_mipi_info));
	node->hw_ctx = NULL;

exit:
	spin_unlock(&node->dev->ctx_lock);
	pr_info("online ctx_unbind done\n");
	return ret;
}

static int dcamonline_statis_cfg(struct dcam_online_node *node, void *param)
{
	int ret = 0;
	struct dcam_statis_param *statis_param = NULL;

	statis_param = (struct dcam_statis_param *)param;

	switch (statis_param->statis_cmd) {
	case DCAM_IOCTL_CFG_STATIS_BUF:
		ret = cam_statis_dcam_port_buffer_cfg(node, param);
		break;
	case DCAM_IOCTL_DEINIT_STATIS_Q:
		ret = cam_statis_dcam_port_buffer_deinit(node);
		break;
	default:
		pr_err("fail to get a known cmd: %d\n", statis_param->statis_cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

static int dcamonline_dev_start(struct dcam_online_node *node, void *param)
{
	int ret = 0, loop = 0, is_csi_connect = 0;
	unsigned long flag = 0;
	struct cam_frame *frame = NULL;
	struct cam_node *cam_node = NULL;
	struct dcam_isp_k_block *pm = NULL;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_reg_trace trace = {0};
	struct dcam_hw_path_ctrl pause = {0};
	struct dcam_hw_start parm = {0};
	struct dcam_hw_mipi_cap caparg = {0};
	struct dcam_hw_path_start patharg = {0};
	struct dcam_hw_sram_ctrl sramarg = {0};
	struct dcam_hw_fbc_ctrl fbc_arg = {0};
	struct cam_hw_lbuf_share camarg = {0};
	struct dcam_online_start_param *start_param = NULL;
	struct cam_hw_lbuf_info *lbuf_info = NULL;
	struct dcam_fmcu_enable fmcu_enable = {0};
	struct dcam_hw_context *hw_ctx = NULL;
	struct dcam_online_port *port = NULL;
	struct dcam_switch_param csi_switch = {0};
	struct cam_node_shutoff_ctrl node_shutoff = {0};
	struct dcam_dummy_param dummy_param = {0};

	if (!node) {
		pr_err("fail to get valid dcam_online_node\n");
		return -EFAULT;
	}

	if (param) {
		if (atomic_read(&node->ch_enable_cnt) == 1)
			return ret;
		atomic_set(&node->ch_enable_cnt, 1);
	}

	loop = 0;
	do {
		ret = dcamonline_ctx_bind(node);
		if (!ret) {
			if (node->hw_ctx_id >= DCAM_HW_CONTEXT_BIND_MAX)
				pr_err("fail to get hw_ctx_id\n");
			break;
		}
		pr_info_ratelimited("ctx %d wait for hw. loop %d\n", node->hw_ctx_id, loop);
		usleep_range(600, 800);
	} while (++loop < 5000);

	if (loop == 5000 || node->hw_ctx_id == DCAM_HW_CONTEXT_BIND_MAX) {
		pr_err("fail to connect. dcam %d\n", node->hw_ctx_id);
		return -1;
	}

	hw_ctx = node->hw_ctx;
	hw = node->dev->hw;
	pm = &node->blk_pm;
	pm->in_size = node->cap_info.cap_size;
	memset(&node->nr3_me, 0, sizeof(struct nr3_me_data));
	dcam_hwctx_debug_init(&node->dev->hw_ctx[node->hw_ctx_id]);
	CAM_DEBUG_LOG_WRITE(&node->dev->hw_ctx[node->hw_ctx_id].debug.block_param_log,
		"DUMP dcam online start hw_ctx_id %d fid %d \n", node->dev->hw_ctx[node->hw_ctx_id].hw_ctx_id, node->dev->hw_ctx[node->hw_ctx_id].fid);
	CAM_DEBUG_LOG_PRINT(&node->dev->hw_ctx[node->hw_ctx_id].debug.block_param_log);
	if (param) {
		start_param = (struct dcam_online_start_param *)param;
		lbuf_info = (struct cam_hw_lbuf_info *)start_param->lbuf_param;
		/* line buffer share mode setting
		 * Precondition: dcam0, dcam1 size not conflict
		 */
		if (lbuf_info->is_4in1)
			lbuf_info->line_w /= 2;
		camarg.width = lbuf_info->line_w;
		camarg.idx = node->hw_ctx_id;
		camarg.offline_flag =lbuf_info->is_offline;
		if ((lbuf_info->sn_max_size_w > DCAM_24M_WIDTH) && (lbuf_info->sn_max_size_w <= DCAM_32M_WIDTH))
			camarg.width = DCAM_32M_WIDTH;
		pr_info("dcam %d pdaf type%d\n", node->hw_ctx_id, node->blk_pm.pdaf.pdaf_type);
		if (hw->ip_dcam[node->hw_ctx_id]->dcamhw_abt && hw->ip_dcam[node->hw_ctx_id]->dcamhw_abt->lbuf_share_support) {
			ret = hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_LBUF_SHARE_SET, &camarg);
			if (ret) {
				pr_err("fail to set share lbuf\n");
				dcamonline_ctx_unbind(node);
				return -1;
			}
		}
		ret = hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_RESET, &hw_ctx->hw_ctx_id);
	} else {
		is_csi_connect = 1;
		node->csi_connect_stat = DCAM_CSI_RESUME;
	}

	if (hw->csi_connect_type == DCAM_BIND_DYNAMIC && node->csi_connect_stat != DCAM_CSI_RESUME) {
		csi_switch.csi_id = node->csi_controller_idx;
		csi_switch.dcam_id= node->hw_ctx_id;
		pr_info("csi_switch.csi_id = %d, csi_switch.dcam_id = %d\n", csi_switch.csi_id, csi_switch.dcam_id);
		/* switch connect */
		hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_FORCE_EN_CSI, &csi_switch);
		if (ret)
			pr_err("fail to force en csi%d\n", node->hw_ctx_id);
		node->csi_connect_stat = DCAM_CSI_RESUME;
	}

	if (node->slowmotion_count)
		pm->is_high_fps = 1;
	else
		pm->is_high_fps = 0;
	pm->latest_skip_num = 0xffffffff;
	if (atomic_read(&node->pm_cnt) <= 0) {
		pr_err("fail to user_cnt hw context%d\n", node->hw_ctx_id);
		dcamonline_ctx_unbind(node);
		return -1;
	}

	dcam_hwctx_block_set(hw_ctx);

	ret = atomic_read(&node->state);
	if (unlikely(ret != STATE_IDLE)) {
		pr_err("fail to get a valid state, starting DCAM%u in state %d\n", hw_ctx->hw_ctx_id, ret);
		dcamonline_ctx_unbind(node);
		return -EINVAL;
	}

	pr_info("DCAM%u start: %p, state = %d\n",
		hw_ctx->hw_ctx_id, node, atomic_read(&node->state));

	/* enable statistic paths  */
	DCAMONLINE_STATIS_WORK_SET(pm->aem.bypass, node, PORT_AEM_OUT);
	DCAMONLINE_STATIS_WORK_SET(pm->lscm.bypass, node, PORT_LSCM_OUT);
	DCAMONLINE_STATIS_WORK_SET(pm->afm.bypass, node, PORT_AFM_OUT);
	DCAMONLINE_STATIS_WORK_SET(pm->afl.afl_info.bypass, node, PORT_AFL_OUT);
	DCAMONLINE_STATIS_WORK_SET(pm->hist.bayerHist_info.hist_bypass, node, PORT_BAYER_HIST_OUT);
	if (hw->ip_isp->isphw_abt->frbg_hist_support)
		DCAMONLINE_STATIS_WORK_SET(pm->hist_roi.hist_roi_info.bypass, node, PORT_FRGB_HIST_OUT);
	DCAMONLINE_STATIS_WORK_SET(pm->pdaf.bypass, node, PORT_PDAF_OUT);
	if (hw_ctx->hw->ip_dcam[node->hw_ctx_id]->dcamhw_abt &&
		hw_ctx->hw->ip_dcam[node->hw_ctx_id]->dcamhw_abt->rgb_gtm_support
			== CAM_ENABLE) {
		if (!pm->rgb_gtm.rgb_gtm_info.bypass_info.gtm_hist_stat_bypass ||
			!pm->gtm.gtm_info.bypass_info.gtm_hist_stat_bypass)
			DCAMONLINE_STATIS_WORK_SET(0, node, PORT_GTM_HIST_OUT);
	}

	hw_ctx->fid = pm->recovery_fid ? pm->recovery_fid : 0;
	hw_ctx->recovery_fid = pm->recovery_fid;
	hw_ctx->index_to_set = hw_ctx->fid;
	pr_info("dcam%d start frame idx %d\n", hw_ctx->hw_ctx_id, hw_ctx->fid);
	node->hw_ctx->iommu_status = 0;
	memset(node->frame_ts, 0, sizeof(node->frame_ts[0]) * DCAM_FRAME_TIMESTAMP_COUNT);
	memset(node->frame_ts_boot, 0, sizeof(node->frame_ts_boot[0]) * DCAM_FRAME_TIMESTAMP_COUNT);

	caparg.idx = node->hw_ctx_id;
	caparg.cap_info = node->cap_info;
	caparg.slowmotion_count = node->slowmotion_count;
	ret = hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_MIPI_CAP_SET, &caparg);
	if (ret < 0) {
		pr_err("fail to set DCAM%u mipi cap\n", node->hw_ctx_id);
		dcamonline_ctx_unbind(node);
		return ret;
	}

	/* set store addr for ISP_NR3_WADDR */
	dcamonline_nr3_store_addr(node);

	CAM_QUEUE_FOR_EACH_ENTRY(port, &node->port_queue.head, list) {
		if (atomic_read(&port->user_cnt) > 0) {
			if ((port->port_id == PORT_RAW_OUT && node->alg_type == ALG_TYPE_CAP_AINR &&
				!node->param_frame_sync) || (port->port_id == PORT_FULL_OUT &&
				node->alg_type == ALG_TYPE_CAP_XDR)) {
				atomic_set(&port->is_shutoff, 0);
				if (port->port_id == PORT_FULL_OUT) {
					cam_node = (struct cam_node *)port->shutoff_cb_handle;
					cam_node->node_shutoff.outport_shutoff[port->port_id].port_id = port->port_id;
					cam_node->node_shutoff.outport_shutoff[port->port_id].shutoff_scene = SHUTOFF_SCENE_MAX;
				}
			}

			patharg.path_id = dcamonline_portid_convert_to_pathid(port->port_id);
			if (patharg.path_id >= DCAM_PATH_MAX) {
				pr_err("fail to get correct path id\n");
				continue;
			}
			patharg.idx = node->hw_ctx_id;
			patharg.slowmotion_count = node->slowmotion_count;
			patharg.pdaf_path_eb = (pm->pdaf.bypass == 0) ? 1 : 0;
			patharg.pdaf_type = pm->pdaf.pdaf_type;
			patharg.cap_info = node->cap_info;
			patharg.src_sel = port->raw_src;
			patharg.bayer_pattern = port->bayer_pattern;
			patharg.in_trim = port->in_trim;
			patharg.endian = port->endian;
			patharg.out_fmt = port->dcamout_fmt;
			pr_debug("path %d, fmt %s, src_sel %d\n", patharg.path_id, camport_fmt_name_get(patharg.out_fmt), patharg.src_sel);
			atomic_set(&port->set_frm_cnt, 0);

			if (atomic_read(&port->is_work) < 1)
				continue;

			ret = DCAM_ONLINE_NODE_SHUTOFF_CALLBACK(port);
			if (ret) {
				pr_info("port %s shutoff is 1, skip.\n", cam_port_name_get(port->port_id));
				continue;
			}

			if (port->port_id == PORT_FULL_OUT) {
				spin_lock_irqsave(&port->state_lock, flag);
				if (port->port_state == DCAM_PORT_PAUSE) {
					hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_PATH_START, &patharg);
					pause.idx = node->hw_ctx_id;
					pause.path_id = dcamonline_portid_convert_to_pathid(port->port_id);
					if (patharg.path_id >= DCAM_PATH_MAX) {
						pr_err("fail to get correct path id\n");
						continue;
					}
					pause.type = HW_DCAM_PATH_PAUSE;
					hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_PATH_CTRL, &pause);
					spin_unlock_irqrestore(&port->state_lock, flag);
					continue;
				}
				spin_unlock_irqrestore(&port->state_lock, flag);
			}
			/* online ctx use fmcu means slowmotion */
			if ((node->slw_type != DCAM_SLW_FMCU) && (port->port_cfg_cb_func)) {
				port->port_cfg_cb_func(hw_ctx, DCAM_PORT_STORE_SET, port);
				patharg.in_trim = port->in_trim;
			}

			if ((atomic_read(&port->set_frm_cnt) > 0) || (node->slw_type == DCAM_SLW_FMCU)) {
				hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_PATH_START, &patharg);
				if (port->compress_en) {
					fbc_arg.idx = node->hw_ctx_id;
					fbc_arg.path_id = dcamonline_portid_convert_to_pathid(port->port_id);
					if (fbc_arg.path_id >= DCAM_PATH_MAX) {
						pr_err("fail to get correct path id\n");
						continue;
					}
					fbc_arg.fmt = port->dcamout_fmt;
					fbc_arg.data_bits = cam_data_bits(port->dcamout_fmt);
					fbc_arg.compress_en = port->compress_en;
					hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_FBC_CTRL, &fbc_arg);
				}
			}
			if ((port->port_id == PORT_RAW_OUT && node->alg_type == ALG_TYPE_CAP_AINR &&
				!node->param_frame_sync && !node->need_dcam_raw) || (port->port_id == PORT_FULL_OUT &&
				node->alg_type == ALG_TYPE_CAP_XDR && cam_node->need_fetch == CAM_DISABLE &&
				hw->ip_dcam[0]->dcamhw_abt->bpc_raw_support == CAM_ENABLE)) {
				if (port->port_cfg_cb_func) {
					port->port_cfg_cb_func((void *)&frame, DCAM_PORT_BUFFER_CFG_GET, port);
					if (frame) {
						if (!frame->common.is_reserved)
							ret = port->port_cfg_cb_func(frame, DCAM_PORT_BUFFER_CFG_SET, port);
						else
							cam_queue_empty_frame_put(frame);
					}
				}
				CAM_NODE_SHUTOFF_PARAM_INIT(node_shutoff);
				node_shutoff.outport_shutoff[port->port_id].port_id = port->port_id;
				node_shutoff.outport_shutoff[port->port_id].shutoff_type = SHUTOFF_PAUSE;
				dcam_online_node_set_shutoff(node, &node_shutoff, port->port_id);
				node->shutoff_cfg_cb_func(node->shutoff_cfg_cb_handle, CAM_NODE_SHUTOFF_CONFIG, &node_shutoff);
			}
		}
	}

	hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_RECORD_ADDR, hw_ctx);
	ret = dcam_init_lsc(pm, 1);
	if (ret < 0) {
		pr_err("fail to init lsc\n");
		goto err;
	}

	if (node->slw_type == DCAM_SLW_FMCU) {
		ret = dcamonline_fmcu_slw_set(node);
		if (ret < 0) {
			pr_err("fail to set slw fmcu\n");
			goto err;
		}
	}

	if (pm->afl.afl_info.bypass == 0) {
		port = dcam_online_node_port_get(node, PORT_AFL_OUT);
		if (port)
			atomic_set(&port->is_work, 0);
	}

	if (node->hw_ctx->dummy_slave) {
		dummy_param.resbuf_get_cb = node->resbuf_get_cb;
		dummy_param.resbuf_cb_data = node->resbuf_cb_data;
		dummy_param.enable = CAM_ENABLE;
		node->dev->dcam_pipe_ops->dummy_cfg(hw_ctx, &dummy_param);
	}

	atomic_set(&node->state, STATE_RUNNING);

	dcam_int_common_tracker_reset(node->hw_ctx_id);
	/*recovery os irq enable*/
	if (!hw_ctx->irq_enable) {
		enable_irq(hw_ctx->irq);
		hw_ctx->irq_enable = 1;
	}
	if (node->slw_type == DCAM_SLW_FMCU) {
		fmcu_enable.enable = 1;
		fmcu_enable.idx = node->hw_ctx_id;
		hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_FMCU_EBABLE, &fmcu_enable);
		hw_ctx->fmcu->ops->hw_start(hw_ctx->fmcu);
	} else {
		parm.idx = node->hw_ctx_id;
		parm.cap_info = node->cap_info;
		parm.glb_reg_lock = hw_ctx->glb_reg_lock;
		pr_debug("idx %d  raw_callback %d x %d\n", parm.idx, parm.raw_callback, node->cap_info.cap_size.size_x);
		hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_START, &parm);
	}

	sramarg.sram_ctrl_en = 1;
	sramarg.idx = node->hw_ctx_id;
	hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_SRAM_CTRL_SET, &sramarg);

	trace.type = NORMAL_REG_TRACE;
	trace.idx = node->hw_ctx_id;
	hw->isp_ioctl(hw, ISP_HW_CFG_REG_TRACE, &trace);
	hw_ctx->err_count = 1;

	if (is_csi_connect && node->csi_connect_stat == DCAM_CSI_RESUME) {
		/* switch connect */
		csi_switch.csi_id = node->csi_controller_idx;
		csi_switch.dcam_id= node->hw_ctx_id;
		hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_CONECT_CSI, &csi_switch);
		pr_info("Connect csi_id = %d, dcam_id = %d\n", csi_switch.csi_id, csi_switch.dcam_id);
	}
	pr_info("dcam%d done state = %d\n", node->hw_ctx_id, atomic_read(&node->state));

	return ret;
err:
	if (node->nr3_frm)
		cam_queue_empty_frame_put(node->nr3_frm);
	dcamonline_ctx_unbind(node);
	return ret;
}

static int dcamonline_dev_stop(struct dcam_online_node *node, enum dcam_stop_cmd pause)
{
	int ret = 0, state = 0, i = 0;
	uint32_t hw_ctx_id = DCAM_HW_CONTEXT_MAX;
	struct dcam_hw_context *hw_ctx = NULL;
	struct dcam_isp_k_block *pm = NULL;
	struct cam_hw_info *hw = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_switch_param csi_switch = {0};
	struct nr3_me_data *nr3_mv = NULL;

	if (!node) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	if (atomic_read(&node->ch_enable_cnt) == 0)
		return ret;

	hw_ctx_id = node->hw_ctx_id;
	hw = node->dev->hw;
	hw_ctx = node->hw_ctx;
	state = atomic_read(&node->state);
	csi_switch.csi_id = node->csi_controller_idx;
	csi_switch.dcam_id = node->hw_ctx_id;

	if ((unlikely(state == STATE_INIT) || unlikely(state == STATE_IDLE)) &&
			((node->csi_connect_stat == DCAM_CSI_RESUME) || (hw->csi_connect_type == DCAM_BIND_FIXED))) {
		pr_debug("DCAM%d not started yet\n", node->hw_ctx_id);
		return ret;
	}

	atomic_set(&node->state, STATE_IDLE);
	if (hw_ctx_id != DCAM_HW_CONTEXT_MAX && pause != DCAM_RECOVERY) {
		if (pause == DCAM_FORCE_STOP)
			hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_DISCONECT_CSI, &csi_switch);
		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_STOP, hw_ctx);
		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_RESET, &hw_ctx_id);
		dcam_int_common_tracker_dump(hw_ctx_id);
		dcam_int_common_tracker_reset(hw_ctx_id);
	}

	if (pause != DCAM_RECOVERY && atomic_read(&node->pm_cnt) > 0)
		ret = dcam_online_node_pmctx_deinit(node);

	pm = &node->blk_pm;
	if (pause != DCAM_RECOVERY) {
		pm->aem.bypass = 1;
		pm->afm.bypass = 1;
		pm->afl.afl_info.bypass = 1;
		pm->hist.bayerHist_info.hist_bypass = 1;
		pm->hist_roi.hist_roi_info.bypass = 1;
		pm->lscm.bypass = 1;
		pm->pdaf.bypass = 1;
		pm->recovery_fid = 0;
		pr_info("stop all\n");

		node->is_3dnr = 0;
	} else if ((pause == DCAM_RECOVERY) && (hw_ctx_id != DCAM_HW_CONTEXT_MAX)) {
		pm->recovery_fid = node->hw_ctx->fid;
		pr_info("dcam%d online pause recovery_fid %d\n", hw_ctx_id, node->hw_ctx->recovery_fid);
	} else {
		pr_info("offline stopped %d\n", pause);
	}

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
		atomic_set(&dcam_port->is_shutoff, 0);
		if (pause == DCAM_RECOVERY) {
			pr_info("reconfig shutoff\n");
			node->shutoff_cfg_cb_func(node->shutoff_cfg_cb_handle, CAM_NODE_SHUTOFF_RECONFIG, &dcam_port->port_id);
		}
	}

	if (hw_ctx_id != DCAM_HW_CONTEXT_MAX) {
		hw_ctx->err_count = 0;
		atomic_set(&hw_ctx->shadow_done_cnt, 0);
		atomic_set(&hw_ctx->shadow_config_cnt, 0);

		for (i = 0; i < DCAM_NR3_MV_MAX; i++) {
			nr3_mv = &hw_ctx->nr3_mv_ctrl[i];
			nr3_mv->bin_path_cnt = 0;
			nr3_mv->full_path_cnt = 0;
			nr3_mv->bin_path_mv_ready = 0;
			nr3_mv->full_path_mv_ready = 0;
		}

	}

	pr_info("stop dcam pipe dev[%d] state = %d!\n", hw_ctx_id, atomic_read(&node->state));

	if (node->slw_type == DCAM_SLW_FMCU) {
		if (pause != DCAM_RECOVERY) {
			ret = hw->dcam_ioctl(hw, hw_ctx_id, DCAM_HW_CFG_FMCU_RESET, &hw_ctx_id);
			if (ret)
				pr_err("fail to reset fmcu dcam%d ret:%d\n", hw_ctx_id, ret);
		}
	}


	if(hw->csi_connect_type == DCAM_BIND_DYNAMIC && node->csi_connect_stat == DCAM_CSI_RESUME) {
		/* csi disconnect(DCAM_HW_DISCONECT_CSI) is already done in the function of
		csi_controller_disable(csi_driver). No need to do again */
		node->csi_connect_stat = DCAM_CSI_IDLE;
	}


	if (node->nr3_frm) {
		cam_queue_empty_frame_put(node->nr3_frm);
		node->nr3_frm = NULL;
	}
	if (pause != DCAM_RECOVERY)
		dcamonline_ctx_unbind(node);

	atomic_set(&node->ch_enable_cnt, 0);

	return ret;
}

static int dcamonline_dummy_proc(struct dcam_online_node *node, void *param, struct dcam_hw_context *hw_ctx)
{
	uint32_t ret = 0;
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct cam_frame *frame = NULL;

	irq_proc = VOID_PTR_TO(param, struct dcam_irq_proc);
	switch (irq_proc->dummy_cmd) {
	case DCAM_DUMMY_NODE_CFG_TIME_TAMP:
		node->frame_ts_boot[tsid(hw_ctx->fid)] = os_adapt_time_get_boottime();
		os_adapt_time_get_ts(&node->frame_ts[tsid(hw_ctx->fid)]);
		break;
	case DCAM_DUMMY_NODE_CFG_REG:
		CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
			if (atomic_read(&dcam_port->is_work) < 1 && !(irq_proc->dummy_int_status & BIT(dcam_port->port_id)))
				continue;
			dcam_port->port_cfg_cb_func(hw_ctx, DCAM_PORT_STORE_RECONFIG, dcam_port);
		}
		break;
	case DCAM_DUMMY_NODE_CFG_SHUTOFF_RESUME:
		CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
			if (atomic_read(&dcam_port->is_work) < 1 && !(irq_proc->dummy_int_status & BIT(dcam_port->port_id)))
				continue;
			ret = node->shutoff_cfg_cb_func(node->shutoff_cfg_cb_handle, CAM_NODE_SHUTOFF_RECONFIG, &dcam_port->port_id);
			if (ret) {
				while (cam_buf_manager_pool_cnt(&dcam_port->result_pool, node->buf_manager_handle)) {
					frame = cam_buf_manager_buf_dequeue(&dcam_port->result_pool, NULL, node->buf_manager_handle);
					if (!frame) {
						pr_err("fail to get frame\n");
						return 0;
					}
					if (frame->common.is_reserved)
						cam_queue_empty_frame_put(frame);
					else
						dcam_port->port_cfg_cb_func(frame, DCAM_PORT_BUFFER_CFG_SET, dcam_port);
				}
			}
		}
		break;
	default:
		pr_err("fail to get dummy cmd:%d\n", irq_proc->dummy_cmd);
		break;
	}
	return 0;
}

void dcam_online_mipi_fetch_reset(struct dcam_online_node *node)
{
	int ret = 0;
	struct dcam_hw_context *hw_ctx = NULL;

	if (node->alg_type != ALG_TYPE_DEFAULT) {
		hw_ctx = node->hw_ctx;
		ret = hw_ctx->hw->dcam_ioctl(hw_ctx->hw, hw_ctx->hw_ctx_id,
			DCAM_HW_CFG_MIPI_CAP_FETCH_RESET, &hw_ctx->hw_ctx_id);
		if (ret == 1)
			dcam_hwctx_slice_force_copy(hw_ctx, 1);
	}
	pr_debug("mipi fetch reset. type %d\n", node->alg_type);
}

int dcam_online_set_raw_sel(void *handle, void *param)
{
	uint32_t raw_sel = 0;
	struct dcam_online_node *node = NULL;
	struct dcam_hw_path_ctrl path_ctrl = {0};

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %px %px\n", handle, param);
		return -EFAULT;
	}

	node = (struct dcam_online_node *)handle;
	raw_sel = *(uint32_t *)param;
	path_ctrl.raw_sel = raw_sel;
	path_ctrl.idx = node->hw_ctx_id;
	pr_debug("dcam %d set raw sel %d\n", node->hw_ctx_id, path_ctrl.raw_sel);
	node->dev->hw->dcam_ioctl(node->dev->hw, node->hw_ctx_id, DCAM_HW_CFG_PATH_SRC_SEL, &path_ctrl);

	return 0;
}

int dcam_online_shutoff_cnt_dec(void *handle, void *param)
{
	uint32_t port_id = 0;
	struct cam_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %px %px\n", handle, param);
		return -EFAULT;
	}
	node = (struct cam_node *)handle;
	port_id = *(int32_t *)param;
	pr_debug("port %d, cnt %d\n", port_id, node->node_shutoff.outport_shutoff[port_id].cap_cnt);
	if (node->node_shutoff.outport_shutoff[port_id].raw_switch_en == CAM_ENABLE)
		atomic_dec(&node->node_shutoff.outport_shutoff[port_id].cap_cnt);

	return 0;
}

int dcam_online_port_param_update(void *handle, uint32_t port_id,
	struct cam_port_shutoff_ctrl *port_shutoff)
{
	int ret = 0;
	struct cam_node *node = NULL;
	struct dcam_fetch_node *fetch_node = NULL;
	struct dcam_online_node *online_node = NULL;
	struct dcam_online_port *port = NULL;

	if (!handle || !port_shutoff) {
		pr_err("fail to get valid input ptr %px %px\n", node, port_shutoff);
		return -EFAULT;
	}
	node = (struct cam_node *)handle;
	if (node->need_fetch) {
		fetch_node = (struct dcam_fetch_node *)node->handle;
		online_node = &fetch_node->online_node;
	} else
		online_node = (struct dcam_online_node *)node->handle;

	port = dcam_online_node_port_get(online_node, port_id);
	pr_debug("cap cnt %d, raw sel: new %d old %d\n", atomic_read(&port_shutoff->cap_cnt),
		port_shutoff->cur_raw_sel, port_shutoff->pre_raw_sel);
	if (atomic_read(&port_shutoff->cap_cnt)) {
		port->raw_src = port_shutoff->cur_raw_sel;
		port->need_post_proc = CAM_ENABLE;
	} else {
		port->raw_src = port_shutoff->pre_raw_sel;
		port->need_post_proc = CAM_DISABLE;
		port_shutoff->raw_switch_en = CAM_DISABLE;
		ret = dcam_online_set_raw_sel(online_node, &port_shutoff->pre_raw_sel);
	}
	pr_debug("raw sel %d, need_post_proc %d\n", port->raw_src, port->need_post_proc);

	return ret;
}

int dcam_online_node_set_shutoff(void *handle, void *param, uint32_t port_id)
{
	uint32_t shutoff = 0, cmd = 0;
	struct dcam_online_node *node = NULL;
	struct dcam_hw_path_ctrl path_ctrl = {0};
	struct dcam_online_port *dcam_port = NULL;
	struct cam_node_shutoff_ctrl *node_shutoff = NULL;
	struct cam_port_shutoff_ctrl *port_shutoff = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct dcam_online_node *)handle;
	node_shutoff = (struct cam_node_shutoff_ctrl *)param;
	port_shutoff = &node_shutoff->outport_shutoff[port_id];
	cmd = port_shutoff->shutoff_type;

	switch (cmd) {
	case SHUTOFF_PAUSE:
		shutoff = 1;
		path_ctrl.idx = node->hw_ctx_id;
		path_ctrl.type = HW_DCAM_PATH_PAUSE;
		path_ctrl.path_id = dcamonline_portid_convert_to_pathid(port_id);
		if (path_ctrl.path_id >= DCAM_PATH_MAX) {
			pr_err("fail to get correct path id\n");
			return -EFAULT;
		}
		node->dev->hw->dcam_ioctl(node->dev->hw, node->hw_ctx_id, DCAM_HW_CFG_PATH_CTRL, &path_ctrl);
		CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
			if (dcam_port->port_id == port_id)
				atomic_set(&dcam_port->is_shutoff, shutoff);
		}
		pr_debug("SHUTOFF_PAUSE, %s\n", cam_port_name_get(port_id));
		break;
	case SHUTOFF_RESUME:
		shutoff = 0;
		path_ctrl.idx = node->hw_ctx_id;
		path_ctrl.type = HW_DCAM_PATH_RESUME;
		path_ctrl.path_id = dcamonline_portid_convert_to_pathid(port_id);
		if (path_ctrl.path_id >= DCAM_PATH_MAX) {
			pr_err("fail to get correct path id\n");
			return -EFAULT;
		}
		node->dev->hw->dcam_ioctl(node->dev->hw, node->hw_ctx_id, DCAM_HW_CFG_PATH_CTRL, &path_ctrl);
		CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
			if (dcam_port->port_id == port_id)
				atomic_set(&dcam_port->is_shutoff, shutoff);
		}
		pr_debug("SHUTOFF_RESUME, %s\n", cam_port_name_get(port_id));
		break;
	default:
		pr_err("fail to support cmd %d", cmd);
		return -EFAULT;
	}

	return shutoff;
}

int dcam_online_node_irq_proc(void *param, void *handle)
{
	int ret = 0, is_recovery = 0;
	struct dcam_online_node *node = NULL;
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct cam_hw_reg_trace trace = {0};
	struct dcam_hw_force_copy copyarg = {0};
	struct cam_hw_info *hw = NULL;
	struct dcam_switch_param csi_switch = {0};

	if (!handle || !param) {
		pr_err("fail to get valid param %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct dcam_online_node *)handle;
	irq_proc = (struct dcam_irq_proc *)param;
	pr_debug("irq proc of %d", irq_proc->of);

	if (!node->hw_ctx) {
		pr_warn("warning: dcam_online_node hw_ctx %p\n", node->hw_ctx);
		return ret;
	}
	hw_ctx = node->hw_ctx;
	node->in_irq_proc = 1;

	switch (irq_proc->of) {
	case CAP_START_OF_FRAME:
		dcamonline_cap_sof(node, param, hw_ctx);
		break;
	case CAP_END_OF_FRAME:
		break;
	case PREV_START_OF_FRAME:
		dcamonline_preview_sof(node, hw_ctx);
		break;
	case SN_START_OF_FRAME:
		dcamonline_sensor_sof(node);
		break;
	case SN_END_OF_FRAME:
		dcamonline_sensor_eof(node);
		break;
	case CAP_DATA_DONE:
		dcamonline_done_proc(param, handle, hw_ctx);
		break;
	case DUMMY_RECONFIG:
		dcamonline_dummy_proc(node, param, hw_ctx);
		break;
	case DCAM_INT_ERROR:
		if (unlikely(atomic_read(&node->state) == STATE_INIT) || unlikely(atomic_read(&node->state) == STATE_IDLE)) {
			hw = node->dev->hw;
			hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_STOP, hw_ctx);
			hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_RESET, &hw_ctx->hw_ctx_id);
			break;
		}
		atomic_set(&node->state, STATE_ERROR);
		trace.type = ABNORMAL_REG_TRACE;
		trace.idx = hw_ctx->hw_ctx_id;
		node->dev->hw->isp_ioctl(node->dev->hw, ISP_HW_CFG_REG_TRACE, &trace);
		if ((g_dbg_recovery == DEBUG_DCAM_RECOVERY_IDLE
			&& (node->dev->hw->ip_dcam[0]->dcamhw_abt->recovery_support & irq_proc->status))
			|| g_dbg_recovery == DEBUG_DCAM_RECOVERY_OPEN) {
			hw = node->dev->hw;
			csi_switch.csi_id = node->csi_controller_idx;
			csi_switch.dcam_id = node->hw_ctx_id;
			hw->dcam_ioctl(hw, node->hw_ctx_id,
				DCAM_HW_CFG_IRQ_DISABLE, &node->hw_ctx_id);
			/* force copy must be after first load done and load clear */
			copyarg.id = DCAM_CTRL_ALL;
			copyarg.idx = node->hw_ctx_id;
			copyarg.glb_reg_lock = hw_ctx->glb_reg_lock;
			node->dev->hw->dcam_ioctl(node->dev->hw, node->hw_ctx_id, DCAM_HW_CFG_FORCE_COPY, &copyarg);
			disable_irq_nosync(hw_ctx->irq);
			hw_ctx->irq_enable = 0;
			is_recovery = 1;
		}
		node->data_cb_func(CAM_CB_DCAM_DEV_ERR, &is_recovery, node->data_cb_handle);
		break;
	default:
		break;
	}
	node->in_irq_proc = 0;
	pr_debug("irq proc done\n");
	return ret;
}

int dcam_online_node_pmctx_init(struct dcam_online_node *node)
{
	int ret = 0, iommu_enable = 0;
	struct dcam_isp_k_block *blk_pm_ctx = &node->blk_pm;
	struct dcam_dev_lsc_param *pa = &blk_pm_ctx->lsc;

	memset(blk_pm_ctx, 0, sizeof(struct dcam_isp_k_block));
	if (cam_buf_iommu_status_get(CAM_BUF_IOMMUDEV_DCAM) == 0)
		iommu_enable = 1;
	ret = cam_buf_alloc(&blk_pm_ctx->lsc.buf, DCAM_LSC_BUF_SIZE, iommu_enable);
	if (ret)
		goto alloc_fail;

	ret = cam_buf_manager_buf_status_cfg(&blk_pm_ctx->lsc.buf, CAM_BUF_STATUS_GET_IOVA_K_ADDR, CAM_BUF_IOMMUDEV_DCAM);
	if (ret)
		goto map_fail;
	blk_pm_ctx->lsc.buf.bypass_iova_ops = CAM_ENABLE;
	blk_pm_ctx->offline = 0;
	blk_pm_ctx->idx = node->hw_ctx_id;
	blk_pm_ctx->dev = (void *)node->dev;

	atomic_set(&node->pm_cnt, 1);
	mutex_init(&blk_pm_ctx->lsc.lsc_lock);
	mutex_init(&blk_pm_ctx->param_lock);
	spin_lock_init(&blk_pm_ctx->hist_update_lock);
	spin_lock_init(&blk_pm_ctx->aem_win_lock);

	cam_block_dcam_init(blk_pm_ctx);

	pa->weight_tab_x = cam_buf_kernel_sys_kzalloc(DCAM_LSC_WEIGHT_TAB_SIZE, GFP_KERNEL);
	if (pa->weight_tab_x == NULL) {
		ret = -ENOMEM;
		goto buf_fail;
	}
	pa->weight_tab_y = cam_buf_kernel_sys_kzalloc(DCAM_LSC_WEIGHT_TAB_SIZE, GFP_KERNEL);
	if (pa->weight_tab_y == NULL) {
		ret = -ENOMEM;
		goto weight_tab_y_fail;
	}
	pa->weight_tab = cam_buf_kernel_sys_kzalloc(DCAM_LSC_WEIGHT_TAB_SIZE, GFP_KERNEL);
	if (pa->weight_tab == NULL) {
		ret = -ENOMEM;
		goto weight_tab;
	}

	return 0;

weight_tab:
	cam_buf_kernel_sys_kfree(pa->weight_tab_y);
	pa->weight_tab_y = NULL;
weight_tab_y_fail:
	cam_buf_kernel_sys_kfree(pa->weight_tab_x);
	pa->weight_tab_x = NULL;
buf_fail:
	cam_buf_manager_buf_status_cfg(&blk_pm_ctx->lsc.buf, CAM_BUF_STATUS_MOVE_TO_ALLOC, CAM_BUF_IOMMUDEV_DCAM);
map_fail:
	cam_buf_free(&blk_pm_ctx->lsc.buf);
alloc_fail:
	pr_err("fail to alloc buffer%d\n", ret);
	return ret;
}

void dcam_online_node_pmctx_update(struct dcam_isp_k_block *node_pm,
	struct dcam_isp_k_block *input_pm)
{
	if (!node_pm || !input_pm) {
		pr_err("fail to get valid ptr %p, %p\n", node_pm, input_pm);
		return;
	}
	memcpy((void *)&node_pm->afm, (void *)&input_pm->afm, sizeof(struct dcam_dev_afm_param));
	memcpy((void *)&node_pm->afl, (void *)&input_pm->afl, sizeof(struct dcam_dev_afl_param));
	memcpy((void *)&node_pm->hist, (void *)&input_pm->hist, sizeof(struct dcam_dev_hist_param));
	memcpy((void *)&node_pm->aem, (void *)&input_pm->aem, sizeof(struct dcam_dev_aem_param));
	memcpy((void *)&node_pm->rgb, (void *)&input_pm->rgb, sizeof(struct dcam_dev_rgb_param));
	memcpy((void *)&node_pm->rgb_gtm, (void *)&input_pm->rgb_gtm, sizeof(struct dcam_dev_rgb_gtm_param));
	memcpy((void *)&node_pm->lscm, (void *)&input_pm->lscm, sizeof(struct dcam_dev_lscm_param));
	memcpy((void *)&node_pm->hist_roi, (void *)&input_pm->hist_roi, sizeof(struct dcam_dev_hist_roi_param));
}

int dcam_online_node_pmctx_deinit(struct dcam_online_node *node)
{
	struct dcam_isp_k_block *blk_pm_ctx = &node->blk_pm;

	mutex_destroy(&blk_pm_ctx->param_lock);
	mutex_destroy(&blk_pm_ctx->lsc.lsc_lock);

	cam_buf_manager_buf_status_cfg(&blk_pm_ctx->lsc.buf, CAM_BUF_STATUS_MOVE_TO_ALLOC, CAM_BUF_IOMMUDEV_DCAM);
	cam_buf_free(&blk_pm_ctx->lsc.buf);
	if(blk_pm_ctx->lsc.weight_tab) {
		cam_buf_kernel_sys_kfree(blk_pm_ctx->lsc.weight_tab);
		blk_pm_ctx->lsc.weight_tab = NULL;
	}
	if(blk_pm_ctx->lsc.weight_tab_x) {
		cam_buf_kernel_sys_kfree(blk_pm_ctx->lsc.weight_tab_x);
		blk_pm_ctx->lsc.weight_tab_x = NULL;
	}
	if(blk_pm_ctx->lsc.weight_tab_y) {
		cam_buf_kernel_sys_kfree(blk_pm_ctx->lsc.weight_tab_y);
		blk_pm_ctx->lsc.weight_tab_y = NULL;
	}
	atomic_set(&node->pm_cnt, 0);

	return 0;
}

int dcam_online_node_xtm_disable(struct dcam_online_node *node, void *param)
{
	uint32_t eb = 0;
	struct cam_hw_info *hw = NULL;

	if (!param) {
		pr_err("fail to get param\n");
		return -1;
	}

	hw = node->dev->hw;
	eb = *(uint32_t *)param;
	if (eb) {
		struct cam_hw_gtm_ltm_eb enable;
		enable.dcam_idx = node->hw_ctx_id;
		enable.dcam_param = &node->blk_pm;
		hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_GTM_LTM_EB, &enable);
	} else {
		struct cam_hw_gtm_ltm_dis dis;
		dis.dcam_idx = node->hw_ctx_id;
		hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_GTM_LTM_DIS, &dis);
	}
	return 0;
}

int dcam_online_node_port_insert(struct dcam_online_node *node, void *param)
{
	uint32_t is_exist = 0;
	struct dcam_online_port *q_port = NULL;
	struct dcam_online_port *new_port = NULL;

	if (!param || !node) {
		pr_err("fail to get valid param %p, %p\n", param, node);
		return -EFAULT;
	}

	new_port = (struct dcam_online_port *)param;
	pr_info("node type:%s, id:%d, port type:%s\n", cam_node_name_get(node->node_type), node->node_id, cam_port_name_get(new_port->port_id));

	CAM_QUEUE_FOR_EACH_ENTRY(q_port, &node->port_queue.head, list) {
		if (new_port == q_port) {
			is_exist = 1;
			break;
		}
	}

	if (!is_exist)
		CAM_QUEUE_ENQUEUE(&node->port_queue, &new_port->list);

	return 0;
}

int dcam_online_node_state_get(void *handle)
{
	struct dcam_online_node *node = (struct dcam_online_node *)handle;

	if (!handle) {
		pr_err("fail to get handle\n");
		return -EFAULT;
	}

	return atomic_read(&node->state);
}

struct dcam_online_port *dcam_online_node_port_get(struct dcam_online_node *node, uint32_t port_id)
{
	struct dcam_online_port *dcam_port = NULL;

	if (!node) {
		pr_err("fail to get valid dcam_online_node\n");
		return NULL;
	}

	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
		if (dcam_port->port_id == port_id)
			return dcam_port;
	}

	pr_err("fail to get dcam port %s\n", cam_port_name_get(port_id));
	return NULL;
}

int dcam_online_node_blk_param_set(struct dcam_online_node *node, void *param)
{
	int ret = 0;
	func_cam_cfg_param cfg_fun_ptr = NULL;
	struct isp_io_param *io_param = NULL;
	struct dcam_isp_k_block *pm = NULL;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_block_func_get blk_func = {0};

	if (!node || !param) {
		pr_err("fail to get valid param %p, %p\n", node, param);
		return -EFAULT;
	}

	hw = node->dev->hw;
	io_param = (struct isp_io_param *)param;
	pm = &node->blk_pm;

	blk_func.index = io_param->sub_block;
	hw->cam_ioctl(hw, CAM_HW_GET_BLK_FUN, &blk_func);
	if (blk_func.cam_entry != NULL &&
		blk_func.cam_entry->sub_block == io_param->sub_block) {
		cfg_fun_ptr = blk_func.cam_entry->cfg_func;
	} else { /* if not, some error */
		pr_err("fail to check param, io_param->sub_block = %d, error\n", io_param->sub_block);
	}
	if (cfg_fun_ptr == NULL) {
		pr_debug("block %d not supported.\n", io_param->sub_block);
		goto exit;
	}

	if (io_param->sub_block == DCAM_BLOCK_LSC)
		mutex_lock(&pm->lsc.lsc_lock);

	ret = cfg_fun_ptr(io_param, pm);

	if ((io_param->sub_block == DCAM_BLOCK_LSC) &&
		(atomic_read(&node->state) == STATE_RUNNING)) {
		dcam_update_lsc(pm);
	}

	if (io_param->sub_block == DCAM_BLOCK_LSC)
		mutex_unlock(&pm->lsc.lsc_lock);
exit:
	return ret;

}

int dcam_online_node_cfg_param(void *handle, uint32_t cmd, void *param)
{
	int ret = 0;
	struct dcam_online_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	node = (struct dcam_online_node *)handle;

	switch (cmd) {
	case CAM_NODE_CFG_BUF:
		break;
	case CAM_NODE_CFG_CAP_PARAM:
		break;
	case CAM_NODE_CFG_STATIS:
		ret = dcamonline_statis_cfg(node, param);
		break;
	case CAM_NODE_CFG_RECT_GET:
		ret = dcamonline_rect_get(node, param);
		break;
	default:
		pr_err("fail to support vaild cmd %d\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

int dcam_online_node_share_buf(void *handle, void *param)
{
	int ret = 0;
	enum dcam_path_state port_state = DCAM_PATH_IDLE;
	struct cam_frame *frame = NULL;
	struct dcam_online_node *node = NULL;
	struct dcam_online_port *port = NULL;
	struct cam_node_cfg_param *in_ptr = NULL;
	struct cam_node_cfg_param node_param = {0};
	struct cam_buf_pool_id share_pool = {0};
	struct camera_buf_get_desc buf_desc = {0};

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p %p\n", handle, param);
		return -EFAULT;
	}

	in_ptr = (struct cam_node_cfg_param *)param;
	port_state = *(uint32_t *)in_ptr->param;
	node = (struct dcam_online_node *)handle;
	node_param.port_id = in_ptr->port_id;
	share_pool.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
	buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &node->port_queue.head, list) {
		if (port->port_id == in_ptr->port_id) {
			if (atomic_read(&port->is_work) < 1 || atomic_read(&port->is_shutoff) > 0)
				break;
			do {
				if (cam_buf_manager_pool_cnt(&port->result_pool, node->buf_manager_handle) == 1 && port_state == DCAM_PATH_PAUSE)
					break;
				else {
					port->port_cfg_cb_func((void *)&frame, DCAM_PORT_BUFFER_CFG_GET, port);
					if (frame == NULL)
						break;
					ret = cam_buf_manager_buf_enqueue(&share_pool, frame, &buf_desc, node->buf_manager_handle);
					if (ret)
						pr_err("fail to enqueue share_pool\n");
				}

			} while (1);
		}
	}

	return ret;
}

int dcam_online_node_cfg_alter_buf(void *handle, void *param)
{
	int ret = 0;
	struct cam_node_cfg_param *in_ptr = NULL;
	struct dcam_online_node *node = NULL;
	struct dcam_online_port *dcam_online_port = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_buf_pool_id pool_id = {0};
	struct camera_buf_get_desc buf_desc = {0};

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %px %px\n", handle, param);
		return -EFAULT;
	}

	in_ptr = (struct cam_node_cfg_param *)param;
	pframe = (struct cam_frame *)in_ptr->param;
	node = (struct dcam_online_node *)handle;
	dcam_online_port = dcam_online_node_port_get(node, in_ptr->port_id);

	buf_desc.buf_ops_cmd = CAM_BUF_STATUS_GET_IOVA;
	if (pframe->common.buf.type== CAM_BUF_USER)
		buf_desc.buf_ops_cmd = CAM_BUF_STATUS_MOVE_TO_ION;
	buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;
	pframe->common.is_reserved = 0;
	pframe->common.not_use_isp_reserved_buf = 0;
	pframe->common.priv_data = dcam_online_port;
	CAM_QUEUE_FRAME_FLAG_RESET(&pframe->common);

	pool_id.private_pool_id = dcam_online_port->alter_pool.private_pool_id;
	ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, &buf_desc, dcam_online_port->buf_manager_handle);
	if (ret) {
		pr_err("fail to enqueue frame of online port %s\n", cam_port_name_get(dcam_online_port->port_id));
		return ret;
	}

	pr_debug("port %s, alter_buf_queue cnt:%d, addr:%x.\n", cam_port_name_get(dcam_online_port->port_id),
		cam_buf_manager_pool_cnt(&pool_id, dcam_online_port->buf_manager_handle),
		pframe->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM]);

	return ret;
}

int dcam_online_node_clr_alter_buf(void *handle, void *param)
{
	struct cam_node_cfg_param *in_ptr = NULL;
	struct dcam_online_node *node = NULL;
	struct dcam_online_port *dcam_online_port = NULL;
	struct cam_buf_pool_id pool_id = {0};

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %px %px\n", handle, param);
		return -EFAULT;
	}

	in_ptr = (struct cam_node_cfg_param *)param;
	node = (struct dcam_online_node *)handle;
	dcam_online_port = dcam_online_node_port_get(node, in_ptr->port_id);

	pool_id.private_pool_id = dcam_online_port->alter_pool.private_pool_id;
	cam_buf_manager_buf_clear(&pool_id, dcam_online_port->buf_manager_handle);

	pr_info("port %s, alter_buf_queue cnt:%d clear!\n", cam_port_name_get(dcam_online_port->port_id),
		cam_buf_manager_pool_cnt(&pool_id, dcam_online_port->buf_manager_handle));

	return 0;
}

int dcam_online_node_fid_get(void *handle, void *param)
{
	uint32_t fid = 0;
	struct dcam_online_node *node = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %px %px\n", handle, param);
		return -EFAULT;
	}

	node = (struct dcam_online_node *)handle;

	if (node->hw_ctx)
		fid = node->hw_ctx->fid;

	return fid;
}

int dcam_online_node_reset(struct dcam_online_node *node, void *param)
{
	int ret = 0;
	uint32_t mode = 0;
	struct cam_frame *frame = NULL;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct dcam_switch_param csi_switch = {0};
	struct dcam_csi_reset_param *csi_p = NULL;
	struct dcam_online_port *dcam_port = NULL;

	if (node->hw_ctx_id == DCAM_HW_CONTEXT_MAX) {
		pr_warn("warning: node has been disconnected and unbinded already.\n");
		return 0;
	}
	/* switch disconnect */
	hw = node->dev->hw;
	hw_ctx = node->hw_ctx;
	csi_p = (struct dcam_csi_reset_param *)param;
	mode = csi_p->mode;
	node->csi_connect_stat = csi_p->csi_connect_stat;
	csi_switch.csi_id = node->csi_controller_idx;
	csi_switch.dcam_id = node->hw_ctx_id;

	if (mode == CAM_CSI_RECOVERY_SWITCH)
		csi_switch.is_recovery = 1;

	if (atomic_read(&hw_ctx->user_cnt) > 0) {
		hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_DISCONECT_CSI, &csi_switch);
		/* reset */
		hw->dcam_ioctl(hw, hw_ctx->hw_ctx_id, DCAM_HW_CFG_STOP, hw_ctx);
		if (!csi_switch.is_recovery)
			hw->dcam_ioctl(hw, node->hw_ctx_id, DCAM_HW_CFG_RESET, &node->hw_ctx_id);
	} else {
		pr_err("fail to get DCAM%d valid user cnt %d\n", node->hw_ctx_id, atomic_read(&hw_ctx->user_cnt));
		return -1;
	}
	pr_info("Disconnect csi_id = %d, dcam_id = %d, hw_ctx_id = %d mode:%d\n",
		csi_switch.csi_id, csi_switch.dcam_id, node->hw_ctx_id, mode);

	atomic_set(&node->state, STATE_IDLE);
	/* reset */
	dcam_int_common_tracker_dump(node->hw_ctx_id);
	dcam_int_common_tracker_reset(node->hw_ctx_id);

	/* reset result q */
	CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
		dcam_port->port_cfg_cb_func((void *)&frame, DCAM_PORT_BUF_RESET_CFG_SET, dcam_port);
	}

	if (node->nr3_frm) {
		cam_queue_empty_frame_put(node->nr3_frm);
		node->nr3_frm = NULL;
	}

	if (mode == CAM_CSI_NORMAL_SWITCH)
		node->blk_pm.recovery_fid = 0;

	/* unbind */
	dcamonline_ctx_unbind(node);

	hw_ctx->dec_all_done = 0;
	hw_ctx->dec_layer0_done = 0;

	return ret;
}

int dcam_online_node_request_proc(struct dcam_online_node *node, void *param)
{
	int ret = 0, port_id_match = 0;
	struct cam_frame *pframe = NULL;
	struct cam_node_cfg_param *in_ptr = NULL;
	struct dcam_online_port *dcam_port = NULL;

	if (!node || !param) {
		pr_err("fail to get valid param %p %p\n", node, param);
		return -EFAULT;
	}

	in_ptr = (struct cam_node_cfg_param *)param;
	if (in_ptr->port_id < PORT_DCAM_OUT_MAX) {
		CAM_QUEUE_FOR_EACH_ENTRY(dcam_port, &node->port_queue.head, list) {
			if (dcam_port->port_id == in_ptr->port_id) {
				port_id_match = 1;
				pframe = (struct cam_frame *)in_ptr->param;
				if (pframe->common.cfg_to_alter_q)
					ret = dcam_online_node_cfg_alter_buf(node, in_ptr);
				else
					ret = dcam_port->port_cfg_cb_func(in_ptr->param, DCAM_PORT_BUFFER_CFG_SET, dcam_port);
				if (ret)
					pr_err("fail to set dcam online port outbuf_queue\n");
			}
		}
		if (port_id_match == 0) {
			if (in_ptr->port_id == 0xffffffff)
				pr_debug("in stream_on process dcamonline_dev started\n");
			else
				pr_err("fail to current port id no match %s\n", cam_port_name_get(in_ptr->port_id));
		}
	} else {
		ret = dcamonline_dev_start(node, in_ptr->param);
		if (ret)
			pr_err("fail to dcam online start\n");
	}

	return ret;
}

int dcam_online_node_stop_proc(struct dcam_online_node *node, void *param)
{
	int ret = 0;
	enum dcam_stop_cmd pause = DCAM_NORMAL_STOP;
	struct cam_node_cfg_param *in_ptr = NULL;

	if (!node || !param) {
		pr_err("fail to get valid param %p %p\n", node, param);
		return -EFAULT;
	}

	in_ptr = (struct cam_node_cfg_param *)param;
	pause = *(int *)(in_ptr->param);
	ret = dcamonline_dev_stop(node, pause);
	if (ret) {
		pr_err("fail to stop\n");
		return -EFAULT;
	}

	return ret;
}

void *dcam_online_node_get(uint32_t node_id, struct dcam_online_node_desc *param)
{
	int ret = 0;
	struct dcam_online_node *node = NULL;

	if (!param) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	if (node_id) {
		pr_err("dcam online node not support nonzero node id %d\n", node_id);
		return NULL;
	}

	pr_info("node id %d node_dev %p\n", node_id, *param->node_dev);
	if (*param->node_dev == NULL) {
		node = cam_buf_kernel_sys_vzalloc(sizeof(struct dcam_online_node));
		if (!node) {
			pr_err("fail to get valid dcam online node\n");
			return NULL;
		}
	} else {
		node = *param->node_dev;
		pr_info("dcam online node has been alloc %p\n", node);
		goto exit;
	}

	memset(&node->nr3_me, 0, sizeof(struct nr3_me_data));

	atomic_set(&node->state, STATE_IDLE);
	atomic_set(&node->pm_cnt, 0);
	atomic_set(&node->user_cnt, 0);
	atomic_set(&node->ch_enable_cnt, 0);
	node->slowmotion_count = param->slowmotion_count;
	if (!node->slowmotion_count)
		node->slw_type = DCAM_SLW_OFF;
	else
		node->slw_type = DCAM_SLW_AP;
	node->dcam_slice_mode = param->dcam_slice_mode;
	node->is_4in1 = param->is_4in1;
	node->is_3dnr = param->enable_3dnr;
	node->nr3_frm = NULL;
	node->cap_info = param->cap_info;
	node->is_pyr_rec = param->is_pyr_rec;
	node->csi_controller_idx = param->csi_controller_idx;
	node->dev = param->dev;
	node->hw_ctx_id = DCAM_HW_CONTEXT_MAX;
	node->alg_type = param->alg_type;
	node->param_frame_sync = param->param_frame_sync;
	node->buf_manager_handle = param->buf_manager_handle;

	CAM_QUEUE_INIT(&node->port_queue, PORT_DCAM_OUT_MAX, NULL);
	node->node_id = node_id;
	node->node_type = param->node_type;
	if (node->data_cb_func == NULL) {
		node->data_cb_func = param->data_cb_func;
		node->data_cb_handle = param->data_cb_handle;
	}
	if (node->resbuf_get_cb == NULL) {
		node->resbuf_get_cb = param->resbuf_get_cb;
		node->resbuf_cb_data = param->resbuf_cb_data;
	}
	if (node->port_cfg_cb_func == NULL) {
		node->port_cfg_cb_func = param->port_cfg_cb_func;
		node->port_cfg_cb_handle = param->port_cfg_cb_handle;
	}
	if (node->shutoff_cfg_cb_func == NULL) {
		node->shutoff_cfg_cb_func = param->shutoff_cfg_cb_func;
		node->shutoff_cfg_cb_handle = param->shutoff_cfg_cb_handle;
	}

	ret = dcam_online_node_pmctx_init(node);
	if (ret)
		return NULL;

	dcam_online_node_pmctx_update(&node->blk_pm, param->blk_pm);
	*param->node_dev = node;

exit:
	node->is_3dnr |= param->enable_3dnr;
	node->need_dcam_raw |= param->need_dcam_raw;
	atomic_inc(&node->user_cnt);
	pr_info("node id %d\n", node->node_id);
	return node;
}

void dcam_online_node_put(struct dcam_online_node *node)
{
	int loop = 0;

	if (!node) {
		pr_err("fail to get invalid node ptr\n");
		return;
	}

	while (node->in_irq_proc && loop < 1000) {
		pr_debug("ctx % in irq. wait %d\n", node->node_id, loop);
		loop++;
		udelay(1000);
	};
	if (loop == 1000)
		pr_warn("warning: dcam node put wait irq timeout\n");

	CAM_QUEUE_CLEAN(&node->port_queue, struct dcam_online_port, list);
	if (atomic_dec_return(&node->user_cnt) == 0) {
		memset(&node->nr3_me, 0, sizeof(struct nr3_me_data));
		if (atomic_read(&node->pm_cnt) > 0)
			dcam_online_node_pmctx_deinit(node);
		node->data_cb_func = NULL;
		node->data_cb_handle = NULL;
		node->resbuf_get_cb = NULL;
		node->resbuf_cb_data = NULL;
		node->port_cfg_cb_func = NULL;
		node->port_cfg_cb_handle = NULL;
		atomic_set(&node->state, STATE_INIT);
		pr_info("dcam online node %d put success\n", node->node_id);
		cam_buf_kernel_sys_vfree(node);
		node = NULL;
	}
}
