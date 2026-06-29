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
#include "cam_statis.h"
#include "cam_zoom.h"
#include "isp_cfg.h"
#include "isp_gtm.h"
#include "isp_int_common.h"
#include "isp_pyr_rec.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_OFFLINE_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static uint32_t ispnode_slice_needed(struct isp_node *node)
{
	struct isp_port *port = NULL;

	CAM_QUEUE_FOR_EACH_ENTRY(port, &node->port_queue.head, list) {
		if (atomic_read(&port->user_cnt) > 0 && port->port_cfg_cb_func(NULL, ISP_PORT_SLICE_NEED, port))
			return 1;
	}
	return 0;
}

static int ispnode_ltm_hist_done(struct isp_node *inode)
{
	struct cam_frame *pframe = NULL;
	int ret = 0;

	pframe = cam_buf_manager_buf_dequeue(
		&inode->ltmhist_resultpool, NULL, inode->buf_manager_handle);
	if (!pframe) {
		pr_warn("warning: no frame in node[%d] ltmhist_result_queue\n", inode->node_id);
		ret = -EFAULT;
		return ret;
	}
	if (unlikely(pframe->common.is_reserved)) {
		cam_queue_empty_frame_put(pframe);
	} else {
		if (inode->ultra_cap_en)
			pframe->common.ltm_info.channel_id = CAM_CH_CAP;
		inode->data_cb_func(CAM_CB_ISP_STATIS_DONE, pframe, inode->data_cb_handle);
	}
	return 0;
}

static int ispnode_postproc_frame_return(struct isp_node *inode)
{
	struct isp_port *port = NULL;
	struct isp_node_postproc_param post_param = {0};

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (atomic_read(&port->user_cnt) > 0 && atomic_read(&port->is_work) > 0) {
			if (port->slice_info.slice_num) {
				if (port->slice_info.slice_cnt != port->slice_info.slice_num - 1) {
					pr_debug("slice %d done\n", port->slice_info.slice_cnt);
					port->slice_info.slice_cnt++;
					if (port->type == PORT_TRANSFER_OUT)
						return 0;
				} else {
					pr_debug("lastslice %d done\n", port->slice_info.slice_cnt);
					port->slice_info.slice_no = 0;
					port->slice_info.slice_num = 0;
					port->slice_info.slice_cnt = 0;
				}
			}
			port->port_cfg_cb_func(&post_param, ISP_PORT_IRQ_POSTPORC, port);
		}
	}
	return 0;
}

static void ispnode_rgb_gtm_hist_done_process(struct isp_node *inode, uint32_t hw_idx, void *dev)
{
	struct isp_gtm_ctx_desc *gtm_ctx = NULL;
	struct cam_frame *frame = NULL;
	uint32_t *buf = NULL;
	uint32_t hist_total = 0;
	int ret = 0;

	gtm_ctx = (struct isp_gtm_ctx_desc *)inode->rgb_gtm_handle;
	if (!gtm_ctx) {
		pr_err("fail to gtm handle ptr\n");
		return;
	}

	frame  = cam_buf_manager_buf_dequeue(&inode->gtmhist_resultpool, NULL, inode->buf_manager_handle);
	if (!frame) {
		pr_debug("isp ctx_id[%d] gtmhist_result_queue buffer\n", gtm_ctx->ctx_id);
		return;
	} else {
		buf = (uint32_t *)frame->common.buf.addr_k;
		if (!buf) {
			ret = cam_buf_manager_buf_enqueue(&inode->gtmhist_outpool, frame, NULL, inode->buf_manager_handle);
			if (ret)
				pr_err("fail to enqueue gtm frame\n");
			return;
		} else {
			hist_total= gtm_ctx->src.w * gtm_ctx->src.h;
			ret = isp_hwctx_gtm_hist_result_get(frame, hw_idx, dev, hist_total, frame->common.fid);
			if (ret) {
				ret = cam_buf_manager_buf_enqueue(&inode->gtmhist_outpool, frame, NULL, inode->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue gtm frame\n");
				return;
			}
			inode->data_cb_func(CAM_CB_ISP_STATIS_DONE, frame, inode->data_cb_handle);
		}
	}
}

static int ispnode_node_ts_cal(struct isp_node *inode, struct isp_hw_context *pctx_hw)
{
	uint32_t sec = 0, usec = 0;
	timespec consume_ts = {0};
	struct isp_pipe_dev *dev = NULL;

	dev = inode->dev;
	os_adapt_time_get_ts(&inode->end_ts);
	dev->hw_ctx[pctx_hw->hw_ctx_id].hw_start_ts = inode->end_ts;
	consume_ts = os_adapt_time_timespec_sub(inode->end_ts, inode->start_ts);
	sec = consume_ts.tv_sec;
	usec = consume_ts.tv_nsec / NSEC_PER_USEC;
	if ((sec * USEC_PER_SEC + usec) > ISP_NODE_TIME)
		pr_warn("Warning: ispnode process too long. consume_time %d.%06d\n", sec, usec);

	PERFORMANCE_DEBUG("ispnode: cur_time %lld.%06ld, consume_time %lld.%06ld\n",
		inode->end_ts.tv_sec, inode->end_ts.tv_nsec / NSEC_PER_USEC,
		consume_ts.tv_sec, consume_ts.tv_nsec / NSEC_PER_USEC);

	return 0;
}

static int ispnode_hw_ts_cal(struct isp_node *inode)
{
	uint32_t size = 0;
	int64_t sec = 0, usec = 0, time_ratio = 0;
	timespec consume_ts = {0};
	timespec cur_ts = {0};
	struct isp_pipe_dev *dev = NULL;

	dev = inode->dev;
	if (inode->ch_id == CAM_CH_CAP) {
		os_adapt_time_get_ts(&cur_ts);
		consume_ts = os_adapt_time_timespec_sub(cur_ts, dev->hw_ctx[inode->pctx_hw_id].hw_start_ts);
		sec = consume_ts.tv_sec;
		usec = consume_ts.tv_nsec / NSEC_PER_USEC;
		size = inode->src.w * inode->src.h;
		time_ratio = div_s64(TIME_SIZE_RATIO * (sec * USEC_PER_SEC + usec), size);
		if ((sec * USEC_PER_SEC + usec) > ISP_HW_TIME && time_ratio > ISP_HW_TIME_RATIO)
			pr_warn("Warning: isp hw process too long. size: %d %d, consume_time %lld.%06lld\n",
				inode->src.w, inode->src.h, sec, usec);

		PERFORMANCE_DEBUG("size %d %d, cur_time %lld.%06ld, consume_time %lld.%06ld\n",
			inode->src.w, inode->src.h, cur_ts.tv_sec, cur_ts.tv_nsec / NSEC_PER_USEC,
			consume_ts.tv_sec, consume_ts.tv_nsec / NSEC_PER_USEC);
	}

	return 0;
}

static int ispnode_hist2_done(struct isp_node *inode, uint32_t hw_idx, struct isp_pipe_dev *dev)
{
	struct cam_frame *pframe = NULL;
	int ret = 0;

	if (inode->node_id != 0) {
		ret = -EFAULT;
		return -1;
	}
	pframe = cam_buf_manager_buf_dequeue(&inode->hist2_pool, NULL, inode->buf_manager_handle);
	if (!pframe) {
		pr_debug("isp ctx_id[%d] hist2_result_queue unavailable\n", inode->node_id);
		ret = -EFAULT;
		return ret;
	}
	ret = isp_hwctx_hist2_frame_prepare(pframe, hw_idx, dev);
	if (ret) {
		ret = cam_buf_manager_buf_enqueue(
			&inode->hist2_pool, pframe, NULL, inode->buf_manager_handle);
		if (ret)
			pr_err("fail to enqueue hist2 frame\n");
		return -1;
	}
	inode->data_cb_func(CAM_CB_ISP_STATIS_DONE, pframe, inode->data_cb_handle);

	return ret;
}

static int ispnode_postproc_irq(
	void *handle, uint32_t hw_idx, enum isp_postproc_type type, void *param)
{
	int i = 0, ret = 0, result_queue_cnt = 0;
	struct isp_port *port = NULL;
	struct isp_node *inode = NULL;
	struct isp_pipe_dev *dev = NULL;
	struct isp_port_cfg port_cfg = {0};
	enum cam_en_status *is_reset = NULL;

	dev = (struct isp_pipe_dev *)handle;
	if (!handle || type >= POSTPROC_MAX) {
		pr_err("fail to get valid input handle %p, type %d\n",
			handle, type);
		return -EFAULT;
	}

	inode = (struct isp_node *)dev->hw_ctx[hw_idx].node;
	inode->in_irq_postproc = 1;
	if (atomic_read(&inode->user_cnt) < 1) {
		pr_debug("contex %d is stopped\n", inode->node_id);
		inode->in_irq_postproc = 0;
		return 0;
	}

	ispnode_hw_ts_cal(inode);

	switch (type) {
	case POSTPROC_FRAME_DONE:
		if (inode->is_fast_stop) {
			struct isp_port_cfg port_cfg = {0};
			struct isp_port *port = NULL;
			if (inode->uinfo.enable_slowmotion == 0)
				inode->dev->isp_ops->unbind(inode);

			if (inode->is_dual)
				complete(&dev->frm_done);
			port_cfg.faststop = &inode->is_fast_stop;
			port_cfg.faststop_done = inode->fast_stop_done;
			port_cfg.out_buf_clear = 0;
			port_cfg.result_queue_ops = CAM_QUEUE_FRONT;
			CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
				if (atomic_read(&port->user_cnt) > 0 &&
					atomic_read(&port->is_work) > 0) {
					if (port->type == PORT_TRANSFER_IN)
						result_queue_cnt = port->port_cfg_cb_func(
							&port_cfg, ISP_PORT_IRQ_FAST_STOP, port);
					else
						port->port_cfg_cb_func(&port_cfg,
							ISP_PORT_IRQ_FAST_STOP, port);
				}
			}
			for (i = 0; i < result_queue_cnt; i++)
				complete(&inode->frm_done);

			break;
		}
		if (inode->ltm_hist_enable)
			ispnode_ltm_hist_done(inode);
		if (inode->uinfo.enable_slowmotion == 0) {
			if (dev->hw_ctx[hw_idx].hist_enable)
				ispnode_hist2_done(inode, hw_idx, dev);
			inode->dev->isp_ops->unbind(inode);
			complete(&inode->frm_done);
			if (inode->is_dual)
				complete(&dev->frm_done);
		}

		ispnode_postproc_frame_return(inode);
		break;
	case POSTPROC_SLOWMOTION_FRAMEDONE:
		inode->dev->isp_ops->unbind(inode);
		complete(&inode->frm_done);
		for (i = 0; i < inode->uinfo.slowmotion_count - 1; i++)
			ispnode_postproc_frame_return(inode);
		break;
	case POSTPROC_RGB_GTM_HISTS_DONE:
		ispnode_rgb_gtm_hist_done_process(inode, hw_idx, dev);
		break;
	case POSTPROC_HIST_CAL_DONE:
		/* only use isp hist in preview channel */
		ispnode_hist2_done(inode, hw_idx, dev);
		break;
	case POSTPROC_FRAME_ERROR_DONE:
		is_reset = VOID_PTR_TO(param, enum cam_en_status);
		if (*is_reset) {
			CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
				port->data_cb_func(CAM_CB_ISP_RESET_ERR,
					&ret, port->data_cb_handle);
				break;
			}
		}
		port_cfg.out_buf_clear = 0;
		port_cfg.result_queue_ops = CAM_QUEUE_TAIL;
		CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
			if (atomic_read(&port->user_cnt) > 0)
				port->port_cfg_cb_func(&port_cfg, ISP_PORT_START_ERROR, port);
		}
		if (inode->uinfo.enable_slowmotion) {
			for (i = 0; i < inode->uinfo.slowmotion_count - 1; i++) {
				port_cfg.out_buf_clear = 1;
				CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
					if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0)
						port->port_cfg_cb_func(&port_cfg, ISP_PORT_START_ERROR, port);
				}
			}
		}
		break;
	default:
		pr_err("fail to get type:%d\n", type);
	}

	inode->in_irq_postproc = 0;
	return ret;
}

static uint32_t ispnode_slw_need_vid_num(struct isp_node_uinfo *uinfo)
{
	uint32_t vid_valid_count = 0, res_num = 0, valid_cnt = 0;

	if (uinfo->stage_a_valid_count == 0)
		return (uinfo->slowmotion_count - 1);

	if (uinfo->frame_num.stage_a_frame_num > 0) {
		res_num = uinfo->frame_num.stage_a_frame_num;
		valid_cnt = uinfo->stage_a_valid_count;
	} else if (uinfo->frame_num.stage_b_frame_num > 0) {
		res_num = uinfo->frame_num.stage_b_frame_num;
		valid_cnt = uinfo->slowmotion_count;
	} else if(uinfo->frame_num.stage_c_frame_num > 0) {
		res_num = uinfo->frame_num.stage_c_frame_num;
		valid_cnt = uinfo->stage_a_valid_count;
	} else if (uinfo->stage_a_valid_count)
		return 0;

	if ((res_num / valid_cnt) >= 1)
		vid_valid_count = valid_cnt - 1;
	else
		vid_valid_count = res_num % valid_cnt;
	return vid_valid_count;
}

static uint32_t ispnode_fid_across_context_get(struct isp_node *inode, enum camera_id cam_id)
{
	struct isp_port *port = NULL;
	uint32_t target_fid = 0, port_fid = 0, vid_valid_count = 0, is_pre_reserved = 0;

	if (!inode)
		return CAMERA_RESERVE_FRAME_NUM;

	target_fid = CAMERA_RESERVE_FRAME_NUM;

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) > 0) {
			port->port_cfg_cb_func(&port_fid, ISP_PORT_UFRAME_FID_GET, port);
			if (port->port_id == PORT_PRE_OUT && port_fid == CAMERA_RESERVE_FRAME_NUM)
				is_pre_reserved = 1;
			target_fid = min(target_fid, port_fid);
			pr_debug("port%d user_fid %u\n", port->port_id, port_fid);
		}
	}

	if (is_pre_reserved && inode->uinfo.enable_slowmotion) {
		vid_valid_count = ispnode_slw_need_vid_num(&inode->uinfo);
		target_fid = target_fid + vid_valid_count;
	}
	pr_debug("target_fid %u, cam_id = %d, is_pre_reserved = %d\n", target_fid, cam_id, is_pre_reserved);

	return target_fid;
}

static int ispnode_hist_roi_update(
	struct cam_hw_info *hw, uint32_t cfg_id, struct isp_dev_hist2_info *hist2_info)
{
	int ret = 0;
	struct isp_hw_hist_roi hist_arg = {0};

	hist_arg.hist_roi = &hist2_info->hist_roi;
	hist_arg.bypass = hist2_info->bypass;
	hist_arg.ctx_id = cfg_id;
	hw->isp_ioctl(hw, ISP_HW_CFG_UPDATE_HIST_ROI, &hist_arg);

	return ret;
}

static int ispnode_blkparam_adapt(struct isp_node *inode, struct isp_hw_context *pctx_hw)
{
	struct isp_hw_k_blk_func sub_blk_func = {0};
	struct isp_port_blksize_desc size_desc = {0};
	struct isp_port *port = NULL;
	struct isp_k_block_param block_param = {0};
	struct isp_dev_hist2_info hist2_info = {0};

	size_desc.ch_id = inode->ch_id;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0)
			port->port_cfg_cb_func(&size_desc, ISP_PORT_BLKSIZE_GET, port);
	}

	inode->isp_k_param.blkparam_info.new_height = size_desc.new_height;
	inode->isp_k_param.blkparam_info.new_width = size_desc.new_width;
	inode->isp_k_param.blkparam_info.old_height = size_desc.old_height;
	inode->isp_k_param.blkparam_info.old_width = size_desc.old_width;
	inode->isp_k_param.sn_size = size_desc.sn_size;
	inode->isp_k_param.src_w = size_desc.src_size.w;
	inode->isp_k_param.src_h = size_desc.src_size.h;
	inode->isp_using_param->blkparam_info.new_height = size_desc.new_height;
	inode->isp_using_param->blkparam_info.new_width = size_desc.new_width;
	inode->isp_using_param->blkparam_info.old_height = size_desc.old_height;
	inode->isp_using_param->blkparam_info.old_width = size_desc.old_width;
	inode->isp_using_param->sn_size = size_desc.sn_size;
	inode->isp_using_param->src_w = size_desc.src_size.w;
	inode->isp_using_param->src_h = size_desc.src_size.h;
	if (!inode->isp_using_param->hist2_info.bypass && inode->ch_id != CAM_CH_CAP) {
		hist2_info.hist_roi.start_x = 0;
		hist2_info.hist_roi.start_y = 0;

		hist2_info.hist_roi.end_x = pctx_hw->pipe_info.fetch.in_trim.size_x - 1;
		hist2_info.hist_roi.end_y = pctx_hw->pipe_info.fetch.in_trim.size_y - 1;
		pctx_hw->hist_enable = 1;
		hist2_info.bypass = 0;
	} else {
		hist2_info.bypass = 1;
		pctx_hw->hist_enable = 0;
	}
	if (inode->ch_id == CAM_CH_CAP && inode->pipe_src.mode_ltm == MODE_LTM_CAP) {
		inode->isp_k_param.ltm_rgb_info.ltm_stat.bypass = 1;
		inode->isp_using_param->ltm_rgb_info.ltm_stat.bypass = 1;
	}
	block_param.cfg_id = inode->cfg_id;
	block_param.isp_k_param = &inode->isp_k_param;
	block_param.isp_using_param = inode->isp_using_param;
	pr_debug("ch_id: %d ctx_id:%d size(%d %d) => (%d %d)\n", inode->ch_id, inode->node_id,
		size_desc.old_width, size_desc.old_height, size_desc.new_width, size_desc.new_height);

	ispnode_hist_roi_update(inode->dev->isp_hw, inode->cfg_id, &hist2_info);

	sub_blk_func.index = ISP_K_BLK_NLM_UPDATE;
	inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_K_BLK_FUNC_GET, &sub_blk_func);
	if (sub_blk_func.k_blk_func)
		sub_blk_func.k_blk_func(&block_param);

	sub_blk_func.index = ISP_K_BLK_IMBLANCE_UPDATE;
	inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_K_BLK_FUNC_GET, &sub_blk_func);
	if (sub_blk_func.k_blk_func)
		sub_blk_func.k_blk_func(&block_param);

	sub_blk_func.index = ISP_K_BLK_YNR_UPDATE;
	inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_K_BLK_FUNC_GET, &sub_blk_func);
	if (sub_blk_func.k_blk_func)
		sub_blk_func.k_blk_func(&block_param);

	sub_blk_func.index = ISP_K_BLK_CNR_UPDATE;
	inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_K_BLK_FUNC_GET, &sub_blk_func);
	if (sub_blk_func.k_blk_func)
		sub_blk_func.k_blk_func(&block_param);

	sub_blk_func.index = ISP_K_BLK_POST_CNR_UPDATE;
	inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_K_BLK_FUNC_GET, &sub_blk_func);
	if (sub_blk_func.k_blk_func)
		sub_blk_func.k_blk_func(&block_param);

	sub_blk_func.index = ISP_K_BLK_EDGE_UPDATE;
	inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_K_BLK_FUNC_GET, &sub_blk_func);
	if (sub_blk_func.k_blk_func)
		sub_blk_func.k_blk_func(&block_param);

	if (inode->uinfo.mode_3dnr != MODE_3DNR_OFF)
		isp_k_update_3dnr(inode->node_id, inode->isp_using_param,
			 size_desc.new_width, size_desc.old_width, size_desc.new_height, size_desc.old_height);

	return 0;
}

static int ispnode_offline_param_set(struct isp_node *inode, struct isp_hw_context *pctx_hw)
{
	struct isp_port *port = NULL;
	int hw_path_id = 0;

	if (!inode || !pctx_hw) {
		pr_err("fail to get input ptr, inode %p, pctx_hw %p\n",
			inode, pctx_hw);
		return -EFAULT;
	}

	isp_hwctx_fetch_set(pctx_hw);
	if (inode->uinfo.fetch_path_sel == ISP_FETCH_PATH_FBD)
		isp_hwctx_fetch_fbd_set(pctx_hw);

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) > 0 && atomic_read(&port->is_work) > 0) {
			hw_path_id = isp_port_id_switch(port->port_id);
			if (hw_path_id == ISP_SPATH_NUM) {
				pr_err("fail to get hw_path_id\n");
				return 0;
			}
			isp_hwctx_scaler_set(pctx_hw, hw_path_id, NULL);
			isp_hwctx_store_set(pctx_hw, hw_path_id);
		}
	}

	return 0;
}

static int ispnode_port_param_cfg(struct isp_node *inode, struct isp_hw_context *pctx_hw, struct isp_port_cfg *port_cfg)
{
	uint32_t ret = 0;
	struct isp_port *port = NULL;
	struct isp_hw_yuv_block_ctrl blk_ctrl = {0};

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0)
			ret |= port->port_cfg_cb_func(port_cfg, ISP_PORT_FRAME_CYCLE, port);
	}

	if (ret) {
		pr_err("fail fetch frame cycle\n");
		return -1;
	}

	pctx_hw->fid = port_cfg->src_frame->common.fid;
	isp_node_prepare_blk_param(inode, port_cfg->src_frame->common.fid, &port_cfg->src_frame->common.blkparam_info);
	isp_node_debug_init(inode, port_cfg->src_frame->common.channel_id);
	inode->debug.fid = pctx_hw->fid;
	inode->debug.idx = pctx_hw->hw_ctx_id;
	CAM_DEBUG_LOG_WRITE(&inode->debug.block_param_log,
		"DUMP ch_id %d pctx_hw->fid %d inode->cfg_id %d \n",
		port_cfg->src_frame->common.channel_id,
		pctx_hw->fid, inode->cfg_id);
	CAM_DEBUG_LOG_PRINT(&inode->debug.block_param_log);
	if (port_cfg->src_frame->common.blkparam_info.update == 1) {
		blk_ctrl.idx = inode->cfg_id;
		blk_ctrl.blk_param = inode->isp_using_param;
		blk_ctrl.blk_param->debug = inode->debug;
		inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_SUBBLOCK_RECFG, &blk_ctrl);
	}

	port_cfg->target_fid = CAMERA_RESERVE_FRAME_NUM;
	port_cfg->ltm_enable = port_cfg->src_frame->common.xtm_conflict.need_ltm_hist ? ((inode->isp_using_param->ltm_rgb_info.ltm_stat.bypass == 0) && (inode->pipe_src.mode_ltm != MODE_LTM_OFF)) : 0;
	port_cfg->rgb_ltm = (struct isp_ltm_ctx_desc *)inode->rgb_ltm_handle;
	port_cfg->pipe_info = &pctx_hw->pipe_info;
	port_cfg->cfg_id = inode->cfg_id;
	port_cfg->sec_mode = inode->dev->sec_mode;
	port_cfg->scaler_coeff_ex = inode->dev->isp_hw->ip_isp->isphw_abt->scaler_coeff_ex;
	port_cfg->scaler_bypass_ctrl = inode->dev->isp_hw->ip_isp->isphw_abt->scaler_bypass_ctrl;
	port_cfg->uinfo = &inode->uinfo;
	port_cfg->superzoom_frame = inode->postproc_buf;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list)
		ret |= port->port_cfg_cb_func(port_cfg, ISP_PORT_PIPEINFO_GET, port);

	if (inode->pipe_src.uframe_sync)
		port_cfg->target_fid = ispnode_fid_across_context_get(inode, inode->attach_cam_id);

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) > 0 && atomic_read(&port->is_work) > 0)
			ret |= port->port_cfg_cb_func(port_cfg, ISP_PORT_FRAME_CYCLE, port);
	}
	ret |= ispnode_offline_param_set(inode, pctx_hw);

	return ret;
}

static int ispcore_slw_3dnr_set(struct isp_node *inode, struct isp_3dnr_slw_mem *slw_mem_ctrl,
	struct isp_3dnr_slw_store *nr3_slw_store, struct isp_port_cfg *port_cfg)
{
	struct isp_3dnr_ctx_desc *nr3_ctx = NULL;

	if (inode == NULL || port_cfg == NULL) {
		pr_err("fail to get valid parameter node %p pframe %p\n", inode, port_cfg);
		return 0;
	}

	if (inode->uinfo.mode_3dnr != MODE_3DNR_PRE)
		return 0 ;

	pr_debug("fid %d, valid %d, x %d, y %d, w %u, h %u\n",
		 port_cfg->src_frame->common.fid, port_cfg->src_frame->common.nr3_me.valid,
		 port_cfg->src_frame->common.nr3_me.mv_x, port_cfg->src_frame->common.nr3_me.mv_y,
		 port_cfg->src_frame->common.nr3_me.src_width, port_cfg->src_frame->common.nr3_me.src_height);

	nr3_ctx = (struct isp_3dnr_ctx_desc *)inode->nr3_handle;

	nr3_ctx->ops.cfg_param(nr3_ctx, ISP_3DNR_CFG_SLW_SET, &port_cfg->src_frame->common.nr3_me);
	slw_mem_ctrl->last_line_mode = nr3_ctx->mem_ctrl.last_line_mode;
	slw_mem_ctrl->first_line_mode = nr3_ctx->mem_ctrl.first_line_mode;
	slw_mem_ctrl->ft_y_height = nr3_ctx->mem_ctrl.ft_y_height;
	slw_mem_ctrl->ft_y_width = nr3_ctx->mem_ctrl.ft_y_width;
	slw_mem_ctrl->ft_uv_width = nr3_ctx->mem_ctrl.ft_uv_width;
	slw_mem_ctrl->ft_uv_height = nr3_ctx->mem_ctrl.ft_uv_height;
	slw_mem_ctrl->mv_x = nr3_ctx->mem_ctrl.mv_x;
	slw_mem_ctrl->mv_y = nr3_ctx->mem_ctrl.mv_y;
	slw_mem_ctrl->ft_luma_addr = nr3_ctx->mem_ctrl.ft_luma_addr;
	slw_mem_ctrl->ft_chroma_addr = nr3_ctx->mem_ctrl.ft_chroma_addr;
	nr3_slw_store->st_luma_addr = nr3_ctx->nr3_store.st_luma_addr;
	nr3_slw_store->st_chroma_addr = nr3_ctx->nr3_store.st_chroma_addr;

	return 0;
}

static int ispnode_3dnr_frame_process(struct isp_node *inode, struct isp_hw_fetch_info *fetch, struct isp_port_cfg *port_cfg)
{
	uint32_t mv_version = 0;
	struct isp_3dnr_ctx_desc *nr3_handle = NULL;

	if (inode == NULL || fetch == NULL || port_cfg == NULL) {
		pr_err("fail to get valid parameter pctx %p fetch %p pframe %p\n", inode, fetch, port_cfg);
		return 0;
	}
	if (port_cfg->src_frame->common.is_3dnr_close == CAM_ENABLE) {
		port_cfg->src_frame->common.nr3_me.mv_x = 0;
		port_cfg->src_frame->common.nr3_me.mv_y = 0;
	}
	pr_debug("fid %d, valid %d, x %d, y %d, w %u, h %u\n",
		port_cfg->src_frame->common.fid,port_cfg->src_frame->common.nr3_me.valid,
		port_cfg->src_frame->common.nr3_me.mv_x, port_cfg->src_frame->common.nr3_me.mv_y,
		port_cfg->src_frame->common.nr3_me.src_width, port_cfg->src_frame->common.nr3_me.src_height);

	mv_version = inode->dev->isp_hw->ip_isp->isphw_abt->nr3_mv_alg_version;
	nr3_handle = (struct isp_3dnr_ctx_desc *)inode->nr3_handle;
	nr3_handle->pyr_rec_eb = port_cfg->src_frame->common.pyr_status;

	nr3_handle->ops.cfg_param(nr3_handle, ISP_3DNR_CFG_MV_VERSION, &mv_version);
	nr3_handle->ops.cfg_param(nr3_handle, ISP_3DNR_CFG_FBC_FBD_INFO, &inode->pipe_src.nr3_fbc_fbd);
	nr3_handle->ops.cfg_param(nr3_handle, ISP_3DNR_CFG_SIZE_INFO, &port_cfg->src_crop);
	nr3_handle->ops.cfg_param(nr3_handle, ISP_3DNR_CFG_MEMCTL_STORE_INFO, fetch);
	nr3_handle->ops.cfg_param(nr3_handle, ISP_3DNR_CFG_BLEND_INFO, inode->isp_using_param);
	nr3_handle->ops.pipe_proc(nr3_handle, &port_cfg->src_frame->common.nr3_me, inode->uinfo.mode_3dnr);

	return 0;
}

static int ispnode_ltm_frame_process(struct isp_node *inode, struct isp_port_cfg *port_cfg)
{
	int ret = 0;
	struct isp_ltm_ctx_desc *rgb_ltm = NULL;

	if (!inode || !port_cfg) {
		pr_err("fail to get valid parameter inode %p port_cfg %p\n",
			inode, port_cfg);
		return -EINVAL;
	}

	rgb_ltm = (struct isp_ltm_ctx_desc *)inode->rgb_ltm_handle;
	if (!rgb_ltm) {
		port_cfg->ltm_hist_bypass = 1;
		return 0;
	}

	rgb_ltm->ltm_ops.cfg_param(rgb_ltm, ISP_LTM_CFG_HIST_BYPASS, &port_cfg->src_frame->common.xtm_conflict.need_ltm_hist);
	rgb_ltm->ltm_ops.cfg_param(rgb_ltm, ISP_LTM_CFG_MAP_BYPASS, &port_cfg->src_frame->common.xtm_conflict.need_ltm_map);
	rgb_ltm->ltm_ops.cfg_param(rgb_ltm, ISP_LTM_CFG_MODE, &inode->pipe_src.mode_ltm);
	rgb_ltm->ltm_ops.cfg_param(rgb_ltm, ISP_LTM_CFG_FRAME_ID, port_cfg->src_frame);
	rgb_ltm->ltm_ops.cfg_param(rgb_ltm, ISP_LTM_CFG_SIZE_INFO, &port_cfg->src_crop);
	rgb_ltm->ltm_ops.cfg_param(rgb_ltm, ISP_LTM_CFG_MAP_BUF, inode->isp_using_param);
	ret = rgb_ltm->ltm_ops.pipe_proc(rgb_ltm, &inode->isp_using_param->ltm_rgb_info);
	if (ret == -1) {
		inode->pipe_src.mode_ltm = MODE_LTM_OFF;
		pr_err("fail to rgb LTM cfg frame, CAM_DISABLE\n");
	}
	port_cfg->ltm_hist_bypass = rgb_ltm->hists.bypass;
	return ret;
}

static int ispnode_gtm_frame_process(struct isp_node *inode, struct isp_port_cfg *port_cfg)
{
	int ret = 0;
	struct isp_gtm_ctx_desc *rgb_gtm = NULL;

	if (!inode || !port_cfg) {
		pr_err("fail to get valid inode %p port_cfg %p\n", inode, port_cfg);
		return -EINVAL;
	}

	rgb_gtm = (struct isp_gtm_ctx_desc *)inode->rgb_gtm_handle;

	if (!rgb_gtm)
		return 0;/*not support GTM in isp*/

	pr_debug("ctx_id %d, mode %d, fid %d\n", rgb_gtm->ctx_id, rgb_gtm->mode, port_cfg->src_frame->common.fid);

	rgb_gtm->gtm_ops.cfg_param(rgb_gtm, ISP_GTM_CFG_FRAME_ID, &port_cfg->src_frame->common.fid);
	rgb_gtm->gtm_ops.cfg_param(rgb_gtm, ISP_GTM_CFG_HIST_BYPASS, &port_cfg->src_frame->common.xtm_conflict.need_gtm_hist);
	rgb_gtm->gtm_ops.cfg_param(rgb_gtm, ISP_GTM_CFG_MAP_BYPASS, &port_cfg->src_frame->common.xtm_conflict.need_gtm_map);
	rgb_gtm->gtm_ops.cfg_param(rgb_gtm, ISP_GTM_CFG_MOD_EN, &port_cfg->src_frame->common.xtm_conflict.gtm_mod_en);
	rgb_gtm->src.w = port_cfg->src_crop.size_x;
	rgb_gtm->src.h = port_cfg->src_crop.size_y;
	ret = rgb_gtm->gtm_ops.pipe_proc(rgb_gtm, inode->isp_using_param);
	if (ret) {
		inode->pipe_src.mode_gtm = MODE_GTM_OFF;
		pr_err("fail to rgb GTM process, GTM_OFF\n");
	}

	return ret;
}

static int ispnode_rec_frame_process(struct isp_node *inode, struct isp_hw_context *pctx_hw, struct isp_port_cfg *port_cfg)
{
	int ret = 0;
	struct isp_pipe_info *pipe_info = NULL;
	struct isp_pyr_rec_in cfg_in = {0};
	struct isp_rec_ctx_desc *rec_ctx = NULL;
	struct isp_slice_context *slc_ctx = NULL;

	if (!inode || !pctx_hw || !port_cfg) {
		pr_err("fail to get valid parameter inode %p pipe_info %p port_cfg %p\n",
			inode, pctx_hw, port_cfg);
		return -EINVAL;
	}

	if (!inode->dev->isp_hw->ip_isp->isphw_abt->pyr_rec_support || inode->dev->sec_mode != SEC_UNABLE)
		return 0;

	pipe_info = &pctx_hw->pipe_info;
	slc_ctx = (struct isp_slice_context *)pctx_hw->slice_ctx;
	rec_ctx = (struct isp_rec_ctx_desc *)inode->rec_handle;
	if (!rec_ctx || !pctx_hw->slice_ctx) {
		pr_err("fail to get valid rec_ctx %p pctx_hw->slice_ctx %p\n", rec_ctx, pctx_hw->slice_ctx);
		return 0;
	}

	cfg_in.src = pipe_info->fetch.src;
	cfg_in.in_addr = pipe_info->fetch.addr;
	cfg_in.in_addr_dcam_out = pipe_info->fetch.addr_dcam_out;
	cfg_in.in_trim = pipe_info->fetch.in_trim;
	cfg_in.in_fmt = pipe_info->fetch.fetch_fmt;
	cfg_in.pyr_fmt = pipe_info->fetch.fetch_pyr_fmt;
	cfg_in.pyr_ynr_radius = inode->isp_using_param->ynr_radius;
	cfg_in.pyr_cnr_radius = inode->isp_using_param->cnr_radius;
	cfg_in.slice_overlap = &slc_ctx->slice_overlap;
	cfg_in.pyr_cnr = &inode->isp_using_param->cnr_info;
	cfg_in.pyr_ynr = &inode->isp_using_param->ynr_info_v3;
	cfg_in.out_addr = pipe_info->store[ISP_SPATH_CP].store.addr;

	rec_ctx->fetch_path_sel = (uint32_t)port_cfg->src_frame->common.is_compressed;
	if (port_cfg->src_frame->common.is_compressed == 1) {
		rec_ctx->fetch_fbd = pipe_info->fetch_fbd_yuv;
		rec_ctx->fbcd_buffer_size = pipe_info->fetch_fbd_yuv.buffer_size;
	}

	rec_ctx->ops.cfg_param(rec_ctx, ISP_REC_CFG_FMCU_HANDLE, pctx_hw->fmcu_handle);
	rec_ctx->ops.cfg_param(rec_ctx, ISP_REC_CFG_LAYER_NUM, &inode->uinfo.pyr_layer_num);
	if (inode->share_buffer)
		rec_ctx->ops.cfg_param(rec_ctx, ISP_REC_CFG_SHARE_BUFFER, &inode->share_buffer);
	if (port_cfg->src_frame->common.pyr_status) {
		rec_ctx->ops.cfg_param(rec_ctx, ISP_REC_CFG_WORK_MODE, &inode->dev->wmode);
		rec_ctx->ops.cfg_param(rec_ctx, ISP_REC_CFG_HW_CTX_IDX, &pctx_hw->hw_ctx_id);
		ret = rec_ctx->ops.pipe_proc(rec_ctx, &cfg_in, inode->buf_manager_handle);
		if (ret)
			pr_err("fail to proc rec frame\n");
	} else
		rec_ctx->pyr_rec.reconstruct_bypass = 1;

	return ret;
}

static int ispnode_fmcu_slw_queue_set(struct isp_fmcu_ctx_desc *fmcu, struct isp_node *inode,
	struct isp_hw_context *pctx_hw, uint32_t vid_valid, uint32_t target_fid, uint32_t sync_slw_vid_en)
{
	int ret = 0;
	struct isp_port *port = NULL;
	struct isp_hw_slw_fmcu_cmds slw = {0};
	struct isp_3dnr_slw_mem slw_mem_ctrl = {0};
	struct isp_3dnr_slw_store nr3_slw_store = {0};
	struct isp_port_cfg port_cfg = {0};
	if (!fmcu)
		return -EINVAL;

	port_cfg.slw = &slw;
	port_cfg.pipe_info = &pctx_hw->pipe_info;
	port_cfg.dev = inode->dev;
	port_cfg.vid_valid = vid_valid;
	port_cfg.uinfo = &inode->uinfo;
	port_cfg.target_fid = target_fid;
	port_cfg.sync_slw_vid_en = sync_slw_vid_en;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (atomic_read(&port->user_cnt) > 0 && atomic_read(&port->is_work) > 0) {
			ret |= port->port_cfg_cb_func(&port_cfg, ISP_PORT_SLOWMOTION, port);
			if (ret) {
				pr_err("fail to set queue for slowmotion. port:%d\n", port->port_id);
				return ret;
			}
		}
	}

	if (!port_cfg.src_frame) {
		pr_err("fail to valid src_frame.\n");
		return -EFAULT;
	}

	ispcore_slw_3dnr_set(inode, &slw_mem_ctrl, &nr3_slw_store, &port_cfg);
	slw.fmcu_handle = fmcu;
	slw.ctx_id = pctx_hw->hw_ctx_id;
	slw.fetchaddr = pctx_hw->pipe_info.fetch.addr;
	slw.isp_store = pctx_hw->pipe_info.store;
	slw.mode_3dnr = inode->uinfo.mode_3dnr;
	slw.slw_mem_ctrl = slw_mem_ctrl;
	slw.nr3_slw_store = nr3_slw_store;
	slw.is_compressed = port_cfg.src_frame->common.is_compressed;
	ret = inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_SLW_FMCU_CMDS, &slw);

	pr_debug("fmcu slw queue done!\n");
	return ret;
}

static int ispnode_copy_param(struct cam_ispblk_frame *param_frame, struct isp_node *inode)
{
	int ret = 0, i = 0;
	struct cam_hw_info *hw_ops = inode->dev->isp_hw;
	struct isp_cfg_block_param fucarg = {0};
	func_isp_cfg_block_param cfg_fun_ptr = NULL;

	for(i = ISP_BLOCK_BCHS; i < ISP_BLOCK_TOTAL; i++) {
		fucarg.index = i - ISP_BLOCK_BASE;
		hw_ops->isp_ioctl(hw_ops, ISP_HW_CFG_PARAM_BLOCK_FUNC_GET, &fucarg);
		if (fucarg.isp_param != NULL && fucarg.isp_param->cfg_block_func != NULL) {
			cfg_fun_ptr = fucarg.isp_param->cfg_block_func;
			ret = cfg_fun_ptr(&inode->isp_k_param, param_frame->param_block);
		}
	}
	return ret;
}

static void ispnode_3dnr_cap_stream_state_set(struct isp_node *node, struct camera_frame *pframe)
{
	struct isp_3dnr_ctx_desc *nr3_ctx = NULL;

	if (node->nr3_blend_cnt == (NR3_BLEND_CNT - 1))
		pframe->buf_type = ISP_STREAM_BUF_OUT;
	else
		pframe->buf_type = ISP_STREAM_BUF_RESERVED;
	nr3_ctx = (struct isp_3dnr_ctx_desc *)node->nr3_handle;
	nr3_ctx->blending_cnt = node->nr3_blend_cnt;
	node->nr3_blend_cnt++;
	if (node->nr3_blend_cnt == NR3_BLEND_CNT)
		node->nr3_blend_cnt = 0;
}

static int ispnode_stream_state_get(struct isp_node *node, struct camera_frame *pframe)
{
	int ret = 0, hw_path_id = 0, need_post_proc = 0;
	struct isp_port *port = NULL;

	if (!node || !pframe) {
		pr_err("fail to get valid input ptr, node %p pframe %p\n", node, pframe);
		return -EFAULT;
	}

	CAM_QUEUE_FOR_EACH_ENTRY(port, &node->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) >= 1 && atomic_read(&port->is_work) > 0) {
			need_post_proc = port->need_post_proc;
			hw_path_id = isp_port_id_switch(port->port_id);
		}
	}
	switch (need_post_proc) {
	case ISP_STREAM_NORMAL_PROC:
		pframe->state = ISP_STREAM_NORMAL_PROC;
		pframe->buf_type = ISP_STREAM_BUF_OUT;
		if (node->uinfo.mode_3dnr == MODE_3DNR_CAP)
			ispnode_3dnr_cap_stream_state_set(node, pframe);
		break;
	case ISP_STREAM_POST_PROC:
		pframe->state = ISP_STREAM_POST_PROC;
		pframe->buf_type = ISP_STREAM_BUF_POSTPROC;
		/*video path need to confirm*/
		if (hw_path_id != ISP_SPATH_CP)
			pframe->buf_type = ISP_STREAM_BUF_RESERVED;
		if (node->uinfo.mode_3dnr == MODE_3DNR_CAP)
			ispnode_3dnr_cap_stream_state_set(node, pframe);
		break;
	default:
		pr_err("fail to surport scaler state:%d\n", need_post_proc);
		break;
	}

	return ret;
}

static int ispnode_start_proc(void *node)
{
	struct isp_node *inode = NULL;
	struct isp_pipe_dev *dev = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	uint32_t ret = 0, loop = 0, i = 0, slice_need = 0, out_buf_queue_cnt = 0, result_queue_cnt = 0;
	struct isp_start_param start_param = {0};
	struct isp_port_cfg port_cfg = {0};
	struct isp_port *port = NULL;
	struct cam_frame *frame = NULL;
	struct camera_buf_get_desc buf_desc = {0};
	uint32_t target_fid = CAMERA_RESERVE_FRAME_NUM;

	inode = VOID_PTR_TO(node, struct isp_node);
	if (atomic_read(&inode->user_cnt) < 1) {
		pr_err("fail to init isp node\n");
		return EFAULT;
	}

	if (inode->is_fast_stop) {
		port_cfg.faststop = &inode->is_fast_stop;
		port_cfg.faststop_done = inode->fast_stop_done;
		port_cfg.out_buf_clear = 1;
		CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
			if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0)
				port->port_cfg_cb_func(&port_cfg, ISP_PORT_FAST_STOP, port);
		}
		return 0;
	}

	port_cfg.node_id = inode->node_id;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0) {
			buf_desc.q_ops_cmd = CAM_QUEUE_DEQ_PEEK;
			port_cfg.src_frame = cam_buf_manager_buf_dequeue(&port->fetch_unprocess_pool, &buf_desc, inode->buf_manager_handle);
		}
		if (atomic_read(&port->user_cnt) > 0)
			port->port_cfg_cb_func(&port_cfg, ISP_PORT_SIZE_UPDATE, port);
	}

	if (!port_cfg.src_frame) {
		pr_err("fail to dequeue src_frame\n");
		return -EFAULT;
	}

	if (inode->ch_id == CAM_CH_CAP) {
		os_adapt_time_get_ts(&inode->start_ts);
		PERFORMANCE_DEBUG("node_start_time %03lld.%06ld\n",
			inode->start_ts.tv_sec, inode->start_ts.tv_nsec / NSEC_PER_USEC);
		if (inode->ultra_cap_en && inode->uinfo.mode_ltm &&
			(port_cfg.src_frame->common.slice_info.slice_no == 0)) {
			ret = wait_for_completion_timeout(
				&inode->ultra_cap_com, ISP_CONTEXT_TIMEOUT);
			if (ret == 0) {
				pr_err("fail to wait isp node %d, timeout.\n", inode->node_id);
				return -EFAULT;
			}
		}
	}
	/*TBD: need delet opt after buffer opt.*/
	ispnode_stream_state_get(inode, &port_cfg.src_frame->common);

	dev = inode->dev;
	hw = dev->isp_hw;
	slice_need = ispnode_slice_needed(inode);

	ret = wait_for_completion_timeout(&inode->frm_done, ISP_CONTEXT_TIMEOUT);
	if (ret == 0) {
		pr_err("fail to wait isp node %d, timeout.\n", inode->node_id);
		ret = -EFAULT;
		goto exit;
	}

	do {
		pctx_hw = (struct isp_hw_context *)dev->isp_ops->bind(inode, slice_need, ispnode_postproc_irq);
		if (!pctx_hw) {
			pr_info_ratelimited("ctx %d wait for hw. loop %d\n", inode->node_id, loop);
			os_adapt_time_usleep_range(600, 800);
		} else
			break;
	} while (loop++ < 5000);

	if (pctx_hw == NULL) {
		pr_err("fail to get pctx_hw\n");
		return -EFAULT;
	}

	/*TBD: other hw info need to check*/
	memset(&pctx_hw->pipe_info, 0, sizeof(struct isp_pipe_info));
	port_cfg.valid_out_frame = 0;
	port_cfg.hw_ctx_id = pctx_hw->hw_ctx_id;
	memcpy(&inode->pipe_src, &inode->uinfo, sizeof(inode->uinfo));
	ret = ispnode_port_param_cfg(inode, pctx_hw, &port_cfg);
	if (ret || !port_cfg.valid_out_frame) {
		complete(&inode->frm_done);
		pr_info("No available output buffer cam%d, node_id %d, cfg_id %d, hw %d, valid_out_frame %d, fid %d\n",
			inode->attach_cam_id, pctx_hw->node_id, inode->cfg_id, pctx_hw->hw_ctx_id, port_cfg.valid_out_frame,
			port_cfg.src_frame->common.fid);
		goto exit;
	}
	if (inode->ch_id == CAM_CH_CAP || (port_cfg.src_frame && (port_cfg.src_frame->common.fid & 0x1f) == 0))
		pr_info("cam%d inode type:%d node_id:%d, cfg_id %d, fid %d, ch_id %d, buf_fd %d iova 0x%lx\n",
			inode->attach_cam_id, inode->node_type, inode->node_id, inode->cfg_id,
			port_cfg.src_frame->common.fid, port_cfg.src_frame->common.channel_id,
			port_cfg.src_frame->common.buf.mfd, port_cfg.src_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP]);
	/* update NR param for crop/scaling image */
	ispnode_blkparam_adapt(inode, pctx_hw);

	pctx_hw->isp_k_param = &inode->isp_k_param;
	pctx_hw->isp_using_param = inode->isp_using_param;
	pctx_hw->pyr_layer_num = inode->uinfo.pyr_layer_num;
	pctx_hw->wmode = dev->wmode;
	pctx_hw->valid_slc_num = 0;
	if (pctx_hw->fmcu_handle || slice_need) {
		ret = isp_hwctx_slice_ctx_init(pctx_hw, &pctx_hw->pipe_info, port_cfg.src_frame->common.slice_info.slice_num);
		if (ret) {
			pr_err("fail to slice ctx init\n");
		}
		isp_hwctx_fmcu_reset(pctx_hw);
	}

	ispnode_3dnr_frame_process(inode, &pctx_hw->pipe_info.fetch, &port_cfg);
	ispnode_ltm_frame_process(inode, &port_cfg);
	ispnode_rec_frame_process(inode, pctx_hw, &port_cfg);
	ispnode_gtm_frame_process(inode, &port_cfg);
	if (pctx_hw->fmcu_handle || slice_need) {
		struct slice_cfg_input slc_cfg = {0};
		slc_cfg.pyr_rec_eb = port_cfg.src_frame->common.pyr_status;
		slc_cfg.ltm_rgb_eb = inode->pipe_src.ltm_rgb;
		slc_cfg.rgb_ltm = (struct isp_ltm_ctx_desc *)inode->rgb_ltm_handle;
		slc_cfg.gtm_rgb_eb = inode->pipe_src.gtm_rgb;
		slc_cfg.rgb_gtm = (struct isp_gtm_ctx_desc *)inode->rgb_gtm_handle;
		slc_cfg.nr3_ctx = (struct isp_3dnr_ctx_desc *)inode->nr3_handle;
		slc_cfg.hist2_eb = !inode->isp_using_param->hist2_info.bypass;
		pctx_hw->rec_handle = inode->rec_handle;
		pctx_hw->nr3_fbc_fbd = inode->pipe_src.nr3_fbc_fbd;
		ret = isp_hwctx_slice_fmcu(pctx_hw, &slc_cfg);
		if (ret) {
			pr_err("fail to slice fmcu cfg, hwctx:%d\n", pctx_hw->hw_ctx_id);
			goto exit;
		}
	}

	if (inode->uinfo.enable_slowmotion) {
		uint32_t vid_valid_count = 0, i = 0;
		vid_valid_count = ispnode_slw_need_vid_num(&inode->uinfo);
		pr_debug("vid count %d, stage a frm_num %d, stage b frm_num %d, stage c frm_num %d, fps %d\n",
			vid_valid_count, inode->uinfo.frame_num.stage_a_frame_num, inode->uinfo.frame_num.stage_b_frame_num,
			inode->uinfo.frame_num.stage_c_frame_num, inode->uinfo.slowmotion_count);
		for (i = 0; i < inode->uinfo.slowmotion_count - 1; i++) {
			target_fid = port_cfg.target_fid + i + 1;
			ret = ispnode_fmcu_slw_queue_set(pctx_hw->fmcu_handle, inode, pctx_hw, i < vid_valid_count,
				target_fid, port_cfg.sync_slw_vid_en);
			if (ret)
				pr_err("fail to set fmcu slw queue\n");
			if (inode->uinfo.is_node_close) {
				pr_info("node%d close, exit isp slowmotion proc.\n", inode->cfg_id);
				complete(&inode->frm_done);
				goto exit;
			}
		}
	}

	inode->ltm_hist_enable = !port_cfg.ltm_hist_bypass;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0) {
			result_queue_cnt = cam_buf_manager_pool_cnt(
				&port->fetch_result_pool, inode->buf_manager_handle);
			if (result_queue_cnt == 0) {
				if (inode->blk_param_node &&
					inode->blk_param_node->isp_blk.param_block)
					cam_queue_recycle_blk_param(
						&inode->param_share_queue, inode->blk_param_node);
				inode->dev->isp_ops->unbind(inode);
				return 0;
			}
		}
	}
	loop = 0;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) > 0 && atomic_read(&port->is_work) > 0) {
			if (port->need_post_proc) {
				do {
					frame = port_cfg.superzoom_frame;
					if (frame) {
						frame->common.state = port_cfg.src_frame->common.state;
						frame->common.link_from.node_type = inode->node_type;
						frame->common.link_from.port_id = port->port_id;
						frame->common.link_from.node_id = inode->node_id;
						frame->common.zoom_data = port_cfg.src_frame->common.zoom_data;
						ret = cam_buf_manager_buf_enqueue(&port->store_result_pool, frame, NULL, inode->buf_manager_handle);
						if (ret == 0)
							break;
					}
					printk_ratelimited(KERN_INFO "wait for output queue. loop %d\n", loop);
					/* wait for previous frame output queue done */
					os_adapt_time_mdelay(1);
				} while (loop++ < 500);
			}
		}
	}

	pctx_hw->iommu_status = 0xffffffff;
	pctx_hw->enable_slowmotion = inode->pipe_src.enable_slowmotion;
	pctx_hw->slowmotion_count = inode->pipe_src.slowmotion_count;
	start_param.blkpm_lock = &inode->blkpm_lock;
	if (!pctx_hw->fmcu_handle && slice_need) {
		start_param.isp_using_param = &inode->isp_using_param;
		ret = isp_hwctx_slices_proc(pctx_hw, inode->dev, &start_param);
	} else {
		start_param.isp_using_param = &inode->isp_using_param;
		start_param.is_dual = inode->is_dual;
		isp_hwctx_hw_start(pctx_hw, inode->dev, &start_param);
	}

	if (inode->blk_param_node && inode->blk_param_node->isp_blk.param_block)
		cam_queue_recycle_blk_param(&inode->param_share_queue, inode->blk_param_node);

	if (inode->ch_id == CAM_CH_CAP)
		ispnode_node_ts_cal(inode, pctx_hw);

	pr_debug("isp start done node:%p\n", inode);
	return 0;

exit:
	port_cfg.out_buf_clear = 0;
	port_cfg.result_queue_ops = CAM_QUEUE_TAIL;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (atomic_read(&port->user_cnt) > 0)
			port->port_cfg_cb_func(&port_cfg, ISP_PORT_START_ERROR, port);
	}

	if (inode->uinfo.enable_slowmotion) {
		for (i = 0; i < inode->uinfo.slowmotion_count - 1; i++) {
			port_cfg.out_buf_clear = 1;
			CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
				if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0)
					port->port_cfg_cb_func(&port_cfg, ISP_PORT_START_ERROR, port);
			}
		}
	}

	if (inode->is_fast_stop) {
		CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
			if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0) {
				out_buf_queue_cnt = cam_buf_manager_pool_cnt(&port->fetch_unprocess_pool, inode->buf_manager_handle);
				result_queue_cnt = cam_buf_manager_pool_cnt(&port->fetch_result_pool, inode->buf_manager_handle);
				if (out_buf_queue_cnt == 0 && result_queue_cnt == 0) {
					inode->is_fast_stop = 0;
					complete(inode->fast_stop_done);
				}
			}
		}
	}

	inode->isp_using_param = NULL;
	if (inode->blk_param_node && inode->blk_param_node->isp_blk.param_block)
		cam_queue_recycle_blk_param(&inode->param_share_queue, inode->blk_param_node);

	if (pctx_hw) {
		pctx_hw->isp_using_param = NULL;
		dev->isp_ops->unbind(inode);
	}

	return ret;
}

static int ispnode_blkparam_cfg(void *node, void *param)
{
	int ret = 0, i = 0;
	struct isp_node* inode = NULL;
	struct isp_pipe_dev *dev = NULL;
	struct isp_io_param *io_param = NULL;
	func_cam_cfg_param cfg_fun_ptr = NULL;
	struct cam_hw_info *ops = NULL;
	struct cam_hw_block_func_get fucarg = {0};
	struct isp_port_blksize_desc size_desc = {0};
	struct isp_port *port = NULL;

	inode = VOID_PTR_TO(node, struct isp_node);
	dev = inode->dev;
	if (!dev) {
		pr_err("fail to get dev\n");
		return -1;
	}

	ops = dev->isp_hw;
	if (!ops) {
		pr_err("fail to get ops\n");
		return -1;
	}
	io_param = VOID_PTR_TO(param, struct isp_io_param);
	pr_debug("cam %d, cfg_id %d, scene_id %d, sub_block %d\n",
		inode->attach_cam_id, inode->cfg_id, io_param->scene_id, io_param->sub_block);

	mutex_lock(&dev->path_mutex);
	if (atomic_read(&inode->user_cnt) < 1) {
		pr_err("fail to use unable isp ctx %d.\n", inode->node_id);
		mutex_unlock(&dev->path_mutex);
		return -EINVAL;
	}

	if (inode->isp_receive_param == NULL && (io_param->scene_id == PM_SCENE_PRE || io_param->scene_id == PM_SCENE_VID)) {
		pr_warn("warning:not get recive handle, param out of range, isp%d blk %d\n", inode->cfg_id, io_param->sub_block);
		mutex_unlock(&dev->path_mutex);
		return 0;
	}
	/* lock to avoid block param across frame */
	mutex_lock(&inode->blkpm_lock);
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0)
			port->port_cfg_cb_func(&size_desc, ISP_PORT_BLKSIZE_GET, port);
	}
	inode->isp_k_param.src_w = size_desc.src_size.w;
	inode->isp_k_param.src_h = size_desc.src_size.h;

	if (io_param->sub_block == ISP_BLOCK_3DNR) {
		if (inode->uinfo.mode_3dnr != MODE_3DNR_OFF) {
			if (io_param->scene_id == PM_SCENE_PRE || io_param->scene_id == PM_SCENE_VID)
				ret = isp_k_cfg_3dnr(param, inode->isp_receive_param->isp_blk.param_block);
			else
				ret = isp_k_cfg_3dnr(param, &inode->isp_k_param);
		}
	} else if (io_param->sub_block == ISP_BLOCK_NOISEFILTER) {
		if (io_param->scene_id == PM_SCENE_CAP)
			ret = isp_k_cfg_yuv_noisefilter(param, &inode->isp_k_param);
	} else {
		i = io_param->sub_block;
		fucarg.index = i;
		ops->cam_ioctl(ops, CAM_HW_GET_BLK_FUN, &fucarg);
		if (fucarg.cam_entry != NULL &&
			fucarg.cam_entry->sub_block == io_param->sub_block)
			cfg_fun_ptr = fucarg.cam_entry->cfg_func;

		if (cfg_fun_ptr == NULL) {
			pr_debug("isp block 0x%x is not supported.\n", io_param->sub_block);
			mutex_unlock(&inode->blkpm_lock);
			mutex_unlock(&dev->path_mutex);
			return 0;
		}
		if (io_param->scene_id == PM_SCENE_PRE) {
			ret = cfg_fun_ptr(io_param, inode->isp_receive_param->isp_blk.param_block);
			if (io_param->sub_block == ISP_BLOCK_RGB_LTM)
				inode->ltm_hist_en = !inode->isp_receive_param->isp_blk.param_block->ltm_rgb_info.ltm_stat.bypass;
		} else {
			ret = cfg_fun_ptr(io_param, &inode->isp_k_param);
			if (inode->ultra_cap_en && inode->ch_id == CAM_CH_CAP &&
				io_param->sub_block == ISP_BLOCK_RGB_LTM) {
				if (io_param->property == ISP_PRO_RGB_LTM_CAP_PARAM)
					complete(&inode->ultra_cap_com);
				else if (io_param->property == ISP_PRO_RGB_LTM_BLOCK &&
					inode->isp_k_param.ltm_rgb_info.ltm_stat.bypass)
					inode->ultra_cap_en = CAM_DISABLE;
				else
					pr_debug("ultra capture: property:%d, bypass:%d.\n",
						io_param->property,
						inode->isp_k_param.ltm_rgb_info.ltm_stat.bypass);
			}
		}
	}

	mutex_unlock(&inode->blkpm_lock);
	mutex_unlock(&dev->path_mutex);

	return ret;
}

static uint32_t ispnode_insert_port(struct isp_node* node, void *param)
{
	struct isp_port *q_port = NULL;
	struct isp_port *new_port = NULL;
	uint32_t is_exist = 0;

	new_port = VOID_PTR_TO(param, struct isp_port);
	pr_debug("node type:%d,id:%d, port type:%d, port type:%d\n", node->node_type, node->node_id, new_port->type, new_port->port_id);

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

static int ispnode_postproc_buffer_alloc(void *handle, struct cam_buf_alloc_desc *param)
{
	int ret = 0;
	struct isp_node *inode = NULL;
	uint32_t postproc_w = 0, postproc_h = 0, block_size = 0;

	inode = (struct isp_node *)handle;

	postproc_w = param->dst_size.w / ISP_SCALER_UP_MAX;
	postproc_h = param->dst_size.h / ISP_SCALER_UP_MAX;

	if (param->ch_id != CAM_CH_CAP && param->ch_vid_enable) {
		postproc_w = MAX(param->dst_size.w,
			param->chvid_dst_size.w) / ISP_SCALER_UP_MAX;
		postproc_h = MAX(param->dst_size.h,
			param->chvid_dst_size.h) / ISP_SCALER_UP_MAX;
	}

	block_size = ((postproc_w + 1) & (~1)) * postproc_h * 3 / 2;
	block_size = ALIGN(block_size, CAM_BUF_ALIGN_SIZE);
	inode->postproc_buf = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	if (!inode->postproc_buf) {
		pr_err("fail to get valid buf\n");
		return -EFAULT;
	}

	inode->postproc_buf->common.channel_id = param->ch_id;
	ret = cam_buf_alloc(&inode->postproc_buf->common.buf, block_size, param->iommu_enable);
	if (ret) {
		pr_err("fail to alloc superzoom buf\n");
	}
	cam_buf_manager_buf_status_cfg(&inode->postproc_buf->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_ISP);
	inode->postproc_buf->common.buf.bypass_iova_ops = CAM_ENABLE;

	pr_info("node_id %d, superzoom w %d, h %d, inode->postproc_buf->common.buf.status %d\n",
		inode->node_id, postproc_w, postproc_h, inode->postproc_buf->common.buf.status);

	return ret;
}

int isp_node_prepare_blk_param(struct isp_node *inode, uint32_t target_fid, struct blk_param_info *out)
{
	uint32_t param_update = 0;
	int ret = 0, loop = 0, param_last_fid = -1;
	struct isp_cfg_block_param fucarg = {0};
	struct cam_hw_info *hw_ops = NULL;
	struct cam_frame *last_param = NULL;
	struct cam_frame *param_pframe = NULL;
	func_isp_cfg_block_param cfg_fun_ptr = NULL;

	if (inode->ch_id == CAM_CH_CAP) {
		out->update = 1;
		param_update = 1;
		inode->isp_using_param = NULL;
		inode->blk_param_node = NULL;
		goto capture_param;
	} else {
		inode->blk_param_node = NULL;
		inode->isp_using_param = &inode->isp_k_param;
		goto preview_param;
	}

preview_param:
	do {
		mutex_lock(&inode->blkpm_q_lock);
		param_pframe = CAM_QUEUE_DEQUEUE_PEEK(&inode->param_buf_queue, struct cam_frame, list);
		if (param_pframe) {
			pr_debug("pre cam %d inode =%d, param_pframe.id=%d,pframe.id=%d\n",
				inode->attach_cam_id, inode->cfg_id, param_pframe->isp_blk.fid, target_fid);
			if (param_pframe->isp_blk.fid <= target_fid) {
				/*update old preview param into inode, cache latest capture param*/
				CAM_QUEUE_DEQUEUE(&inode->param_buf_queue, struct cam_frame, list);
				mutex_unlock(&inode->blkpm_q_lock);
				if (param_pframe->isp_blk.update) {
					pr_debug("cam %d cfg_id %d update param %d\n", inode->attach_cam_id, inode->cfg_id, param_pframe->isp_blk.fid);
					ispnode_copy_param(&param_pframe->isp_blk, inode);
					param_update = 1;
				}
				param_last_fid = param_pframe->isp_blk.fid;
				cam_queue_recycle_blk_param(&inode->param_share_queue, param_pframe);
				if (param_last_fid == target_fid)
					break;
				param_pframe = NULL;
			} else {
				mutex_unlock(&inode->blkpm_q_lock);
				param_pframe = NULL;
				break;
			}
		} else
			mutex_unlock(&inode->blkpm_q_lock);
	} while (loop++ < inode->param_buf_queue.max);
	if (!inode->isp_using_param)
		inode->isp_using_param = &inode->isp_k_param;
	out->update = param_update;
	return param_update;

capture_param:
	do {
		mutex_lock(&inode->blkpm_q_lock);
		param_pframe = CAM_QUEUE_DEQUEUE_PEEK(&inode->param_buf_queue, struct cam_frame, list);
		if (param_pframe) {
			pr_debug("cap cam %d cfg_id %d, param_pframe.id %d pframe.id %d\n",
				inode->attach_cam_id, inode->cfg_id, param_pframe->isp_blk.fid, target_fid);
			if (param_pframe->isp_blk.fid <= target_fid) {
				CAM_QUEUE_DEQUEUE(&inode->param_buf_queue, struct cam_frame, list);
				mutex_unlock(&inode->blkpm_q_lock);
				/*return old param*/
				if (last_param)
					ret = cam_queue_recycle_blk_param(&inode->param_share_queue, last_param);
				/*cache latest capture param*/
				last_param = param_pframe;
				param_last_fid = param_pframe->isp_blk.fid;

				if (param_last_fid == target_fid)
					break;
				param_pframe = NULL;
			} else {
				mutex_unlock(&inode->blkpm_q_lock);
				if (!last_param) {
					pr_warn("warning:dont have old param, use latest param, cam %d, cfg_id %d, fid %d\n",
						inode->attach_cam_id, inode->cfg_id, target_fid);
					inode->isp_using_param = &inode->isp_k_param;
					inode->blk_param_node = NULL;
				}
				break;
			}
		} else {
			pr_warn("warning:no capture param in param q:state:%d, cnt:%d.\n", inode->param_buf_queue.state,
				inode->param_buf_queue.cnt);
			mutex_unlock(&inode->blkpm_q_lock);
		}
	} while (loop++ < inode->param_buf_queue.max);

	if (last_param) {
		hw_ops = inode->dev->isp_hw;
		inode->isp_using_param = last_param->isp_blk.param_block;
		if (inode->ultra_cap_en && hw_ops->ip_isp->isphw_abt->rgb_ltm_support) {
			fucarg.index = ISP_BLOCK_RGB_LTM - ISP_BLOCK_BASE;
			hw_ops->isp_ioctl(hw_ops, ISP_HW_CFG_PARAM_BLOCK_FUNC_GET, &fucarg);
			if (fucarg.isp_param != NULL && fucarg.isp_param->cfg_block_func != NULL) {
				cfg_fun_ptr = fucarg.isp_param->cfg_block_func;
				ret = cfg_fun_ptr(last_param->isp_blk.param_block, &inode->isp_k_param);
			}
		}
		inode->blk_param_node = last_param;
		if (param_last_fid != target_fid)
			pr_debug("warning:cap use old param, cam %d, cfg_id %d, param id %d, frame id %d\n",
				inode->attach_cam_id, inode->cfg_id, param_last_fid, target_fid);
	} else if (inode->isp_using_param == NULL) {
		pr_warn("warning:cap cam %d cfg_id %d not have param in q, use latest param, frame id %d\n",
			inode->attach_cam_id, inode->cfg_id, target_fid);
		inode->isp_using_param = &inode->isp_k_param;
		inode->blk_param_node = NULL;
	}
	out->update = param_update;
	return param_update;
}

int isp_node_buffers_alloc(void *handle, struct cam_buf_alloc_desc *param)
{
	int ret = 0;
	uint32_t i = 0, width = 0, height = 0, block_size = 0;
	struct isp_3dnr_ctx_desc *nr3_ctx = NULL;
	struct isp_ltm_ctx_desc *rgb_ltm = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_node *inode = NULL;

	if(!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	inode = (struct isp_node *)handle;
	hw = inode->dev->isp_hw;

	if (hw->ip_dcam[0]->dcamhw_abt->superzoom_support && !param->is_super_size) {
		ret = ispnode_postproc_buffer_alloc(inode, param);
		if(ret) {
			pr_err("fail to alloc postproc buffer.\n");
			return ret;
		}
	}

	if ((param->is_pyr_rec && param->ch_id != CAM_CH_CAP)
		|| (param->is_pyr_dec && param->ch_id == CAM_CH_CAP)) {
		ret = isp_pyr_rec_buffer_alloc(inode->rec_handle, param, inode->buf_manager_handle);
		if(ret) {
			pr_err("fail to alloc rec buffer.\n");
			return ret;
		}
	}

	if (param->nr3_enable) {
		nr3_ctx = (struct isp_3dnr_ctx_desc *)inode->nr3_handle;
		width = param->width;
		height = param->height;
		if (param->compress_3dnr)
			block_size = isp_3dnr_cal_compressed_size(width, height);
		else {
			if ((param->dcam_out_fmt == CAM_YUV420_2FRAME)
				|| (param->dcam_out_fmt == CAM_YVU420_2FRAME)
				|| (param->dcam_out_fmt == CAM_YUV420_2FRAME_MIPI)
				|| (param->dcam_out_fmt == CAM_YVU420_2FRAME_MIPI)) {
				block_size = ((width + 1) & (~1)) * height * 3;
				block_size = ALIGN(block_size, CAM_BUF_ALIGN_SIZE);
			} else {
				block_size = ((width + 1) & (~1)) * height * 3 / 2;
				block_size = ALIGN(block_size, CAM_BUF_ALIGN_SIZE);
			}
		}

		pr_info("ch %d width %d height %d 3dnr buffer size: %u.\n", param->ch_id, width, height, block_size);
		for (i = 0; i < ISP_NR3_BUF_NUM; i++) {

			ret = cam_buf_alloc(&nr3_ctx->nr3_frame[i].buf, block_size, param->iommu_enable);
			if (ret) {
				pr_err("fail to alloc 3dnr buf: %d ch %d\n", i, param->ch_id);
				return -1;
			}

			ret = cam_buf_manager_buf_status_cfg(&nr3_ctx->nr3_frame[i].buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_ISP);
			if (ret) {
				pr_err("fail to map isp 3dnr iommu buf.\n");
				cam_buf_free(&nr3_ctx->nr3_frame[i].buf);
				return -1;
			}
			nr3_ctx->nr3_frame[i].buf.bypass_iova_ops = CAM_ENABLE;

			pr_debug("3DNR CFGB[%d][0x%p] = 0x%lx\n",
				i, &nr3_ctx->nr3_frame[i],
				nr3_ctx->nr3_frame[i].buf.iova[CAM_BUF_IOMMUDEV_ISP]);
		}
	}

	if (param->ltm_mode != MODE_LTM_OFF) {
		/* todo: ltm buffer size needs to be refined.*/
		/* size = ((width + 1) & (~1)) * height * 3 / 2; */
		/*
		 * sizeof histo from 1 tile: 128 * 16 bit
		 * MAX tile num: 8 * 8
		 */
		block_size = 64 * 128 * 2;
		block_size = ALIGN(block_size, CAM_BUF_ALIGN_SIZE);

		pr_info("ch %d ltm buffer size: %u ltm_rgb_enable %d.\n", param->ch_id, block_size, param->ltm_rgb_enable);
		if (param->ltm_rgb_enable) {
			rgb_ltm = (struct isp_ltm_ctx_desc *)inode->rgb_ltm_handle;
			if (!rgb_ltm) {
				pr_err("fail to get rgb ltm.\n");
				return -1;
			}
			for (i = 0; i < ISP_LTM_BUF_NUM; i++) {
				ret = cam_buf_alloc(&rgb_ltm->ltm_frame[i].buf, block_size, param->iommu_enable);
				if (ret) {
					pr_err("fail to alloc ltm buf: %d ch %d\n", i, param->ch_id);
					return -1;
				}

				ret = cam_buf_manager_buf_status_cfg(&rgb_ltm->ltm_frame[i].buf, CAM_BUF_STATUS_GET_IOVA_K_ADDR, CAM_BUF_IOMMUDEV_ISP);
				if (ret) {
					pr_err("fail to map isp ltm iommu buf.\n");
					cam_buf_free(&rgb_ltm->ltm_frame[i].buf);
					return -1;
				}
				rgb_ltm->ltm_frame[i].buf.bypass_iova_ops = CAM_ENABLE;
				pr_debug("ctx id %d, LTM CFGB[%d][0x%p] = 0x%lx\n",
					rgb_ltm->ctx_id, i, &rgb_ltm->ltm_frame[i],
					rgb_ltm->ltm_frame[i].buf.iova[CAM_BUF_IOMMUDEV_ISP]);
			}
		}
	}

	return ret;
}

uint32_t isp_node_config(void *node, enum isp_node_cfg_cmd cmd, void *param)
{
	int ret = 0;
	unsigned long blkpm_addr = 0;
	struct isp_port_cfg port_cfg = {0};
	struct cam_hw_gtm_ltm_eb eb = {0};
	struct cam_hw_gtm_ltm_dis dis = {0};
	struct cam_capture_param *cap_param = NULL;
	struct isp_port *port = NULL;
	struct isp_node *inode = NULL;
	struct cam_frame *param_frame = NULL;
	struct isp_hw_gtm_func *gtm_func = NULL;
	struct cfg_param_status *param_status = NULL;
	struct cam_postproc_param *postproc_param = NULL;

	inode = VOID_PTR_TO(node, struct isp_node);
	switch (cmd) {
	case ISP_NODE_CFG_BLK_PATAM:
		ret = ispnode_blkparam_cfg(inode, param);
		break;
	case ISP_NODE_CFG_STATIS:
		if (atomic_read(&inode->user_cnt) == 0) {
			pr_info("isp ctx %d is not enable\n", inode->node_id);
			return 0;
		}
		ret = cam_statis_isp_port_buffer_cfg(inode->dev, inode, param);
		break;
	case ISP_NODE_CFG_INSERT_PORT:
		ispnode_insert_port(inode, param);
		break;
	case ISP_NODE_CFG_CAP_PARAM:
		/* for L6/L5pro yuv sensor capture */
		cap_param = (struct cam_capture_param *)param;
		inode->uinfo.cap_timestamp = cap_param->cap_timestamp;
		inode->uinfo.cap_type = cap_param->cap_type;
		break;
	case ISP_NODE_CFG_CLEAR_PORT:
		break;
	case ISP_NODE_CFG_PORT_UFRAME:
		inode->uinfo.uframe_sync |= *(uint32_t *)param;
		break;
	case ISP_NODE_CFG_3DNR_MODE:
		inode->uinfo.mode_3dnr = *(uint32_t *)param;
		break;
	case ISP_NODE_CFG_GTM:
		gtm_func = VOID_PTR_TO(param, struct isp_hw_gtm_func);
		inode->dev->isp_hw->isp_ioctl(inode->dev->isp_hw, ISP_HW_CFG_GTM_FUNC_GET, gtm_func);
		switch (gtm_func->index) {
		case ISP_K_GTM_LTM_DIS:
			dis.isp_idx = inode->cfg_id;
			gtm_func->k_blk_func(&dis);
			break;
		case ISP_K_GTM_STATUS_GET:
			gtm_func->cap_skip_num = gtm_func->k_blk_func(&inode->cfg_id);
			break;
		case ISP_K_GTM_LTM_EB:
			eb.isp_idx = inode->cfg_id;
			gtm_func->k_blk_func(&inode->cfg_id);
			break;
		default:;
		}
		break;
	case ISP_NODE_CFG_POSTPROC_PARAM:
		postproc_param = (struct cam_postproc_param *)param;
		blkpm_addr = (unsigned long)postproc_param->blkpm_ptr;
		param_frame = cam_queue_empty_blk_param_get(&inode->param_share_queue);
		if (param_frame) {
			param_frame->isp_blk.fid = postproc_param->fid;
			/* Temp get param from mw by do offset on base addr, need to discuss
				param & image buf share set way in offline proc scene. */
			ret |= copy_from_user((void *)&param_frame->isp_blk.param_block->post_cnr_h_info,
				(void __user *)blkpm_addr, sizeof(struct isp_dev_post_cnr_h_info));
			param_frame->isp_blk.param_block->post_cnr_h_info.isupdate = 1;

			blkpm_addr += sizeof(struct isp_dev_post_cnr_h_info);
			ret |= copy_from_user((void *)&param_frame->isp_blk.param_block->ynr_info_v3,
				(void __user *)blkpm_addr, sizeof(struct isp_dev_ynr_info_v3));
			param_frame->isp_blk.param_block->ynr_info_v3.isupdate = 1;

			blkpm_addr += sizeof(struct isp_dev_ynr_info_v3);
			ret |= copy_from_user((void *)&param_frame->isp_blk.param_block->cnr_info,
				(void __user *)blkpm_addr, sizeof(struct isp_dev_cnr_h_info));
			param_frame->isp_blk.param_block->cnr_info.isupdate = 1;
			param_frame->isp_blk.update = 1;
			if (ret)
				pr_warn("Warning:Not get the isp nr block param.\n");
			ret = CAM_QUEUE_ENQUEUE(&inode->param_buf_queue, &param_frame->list);
			if (ret) {
				pr_err("fail to enquene cap param_buf_queue\n");
				cam_queue_recycle_blk_param(&inode->param_share_queue, param_frame);
			}
		} else {
			pr_err("fail to recive param, fid %d\n", postproc_param->fid);
			return -EFAULT;
		}
		break;
	case ISP_NODE_CFG_PARAM_SWITCH:
		param_status = VOID_PTR_TO(param, struct cfg_param_status);
		if (param_status->status == 0 && (param_status->scene_id == PM_SCENE_PRE
			|| param_status->scene_id == PM_SCENE_VID)) {
			param_frame = cam_queue_empty_blk_param_get(&inode->param_share_queue);
			if (param_frame) {
				param_frame->isp_blk.param_block->cfg_id = inode->cfg_id;
				if (inode->isp_receive_param) {
					cam_queue_recycle_blk_param(&inode->param_share_queue, param_frame);
					pr_err("fail to get param_frame, last param not use\n");
					return 0;
				}
				inode->isp_receive_param = param_frame;
			} else {
				pr_err("fail to recive param, cam %d inode->cfg_id %d scene %d fid %d param_share_queue cnt %d\n",
					inode->attach_cam_id, inode->cfg_id, param_status->scene_id, param_status->frame_id, inode->param_share_queue.cnt);
				return -EFAULT;
			}
		}

		if (param_status->status) {
			if (param_status->scene_id == PM_SCENE_PRE || param_status->scene_id == PM_SCENE_VID) {
				param_frame = inode->isp_receive_param;
				if (!param_frame) {
					pr_warn("warning: isp%d lose param fid %d\n", inode->cfg_id, param_status->frame_id);
					return 0;
				}
				/* give up current param and no longer receive new param after dcam_ctx unbind */
				if (!param_status->dcam_ctx_bind_state && param_status->frame_id > 0) {
					ret = cam_queue_recycle_blk_param(&inode->param_share_queue, param_frame);
					inode->isp_receive_param = NULL;
					pr_debug("csi disconnect scene, give up current param\n");
					return ret;
				}

				param_frame->isp_blk.update = param_status->update;
				param_frame->isp_blk.fid = param_status->frame_id;
				ret = CAM_QUEUE_ENQUEUE(&inode->param_buf_queue, &param_frame->list);
				if (ret) {
					pr_err("fail to enquene pre param_buf_queue\n");
					cam_queue_recycle_blk_param(&inode->param_share_queue, param_frame);
				}
				inode->isp_receive_param = NULL;
			} else {
				param_frame = cam_queue_empty_blk_param_get(&inode->param_share_queue);
				if (!param_frame) {
					mutex_lock(&inode->blkpm_q_lock);
					param_frame = CAM_QUEUE_DEQUEUE(&inode->param_buf_queue, struct cam_frame, list);
					mutex_unlock(&inode->blkpm_q_lock);
					if (param_frame)
						pr_debug("isp deq param node %d cnt %d max %d param_block %p\n",
							param_frame->isp_blk.fid, inode->param_buf_queue.cnt, inode->param_buf_queue.max, param_frame->isp_blk.param_block);
				}

				if (param_frame) {
					if (param_status->blkpm_ptr) {
						inode->dev->isp_hw->isp_ioctl(
							param_frame->isp_blk.param_block,
							ISP_HW_CFG_BLOCKPARAM_CFG,
							param_status->blkpm_ptr);
						memcpy(&inode->isp_k_param,
							param_frame->isp_blk.param_block,
							sizeof(struct dcam_isp_k_block));
					} else
						memcpy(param_frame->isp_blk.param_block,
						&inode->isp_k_param,
						sizeof(struct dcam_isp_k_block));
					param_frame->isp_blk.update = param_status->update;
					param_frame->isp_blk.fid = param_status->frame_id;
					ret = CAM_QUEUE_ENQUEUE(&inode->param_buf_queue, &param_frame->list);
					if (ret) {
						pr_err("fail to enquene cap param_buf_queue, q state:%d, cnt:%d\n", inode->param_buf_queue.state, inode->param_buf_queue.cnt);
						cam_queue_recycle_blk_param(&inode->param_share_queue, param_frame);
					}
				} else
					pr_warn("isp%d cap lose param %d\n", inode->cfg_id, param_status->frame_id);
			}
		}
		break;
	case ISP_NODE_CFG_PARAM_Q_CLEAR:
		do {
			mutex_lock(&inode->blkpm_q_lock);
			param_frame = CAM_QUEUE_DEQUEUE(&inode->param_buf_queue, struct cam_frame, list);
			mutex_unlock(&inode->blkpm_q_lock);
			if (param_frame)
				cam_queue_recycle_blk_param(&inode->param_share_queue, param_frame);
		} while (CAM_QUEUE_CNT_GET(&inode->param_buf_queue) > 0);
		break;
	case ISP_NODE_CFG_FAST_STOP:
		inode->fast_stop_done = (struct completion *)param;
		port_cfg.faststop = &inode->is_fast_stop;
		port_cfg.faststop_done = inode->fast_stop_done;
		CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
			if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0)
				port->port_cfg_cb_func(&port_cfg, ISP_PORT_FAST_STOP, port);
		}
		break;
	case ISP_NODE_CFG_XTM_EN:
		*((uint32_t *)param) = inode->ltm_hist_en;
		break;
	case ISP_NODE_CFG_CLEAR_FAST_STOP:
		inode->is_fast_stop = 0;
		break;
	default:
		pr_err("fail to support vaild cmd:%d\n", cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

int isp_node_request_proc(struct isp_node *node, void *param)
{
	int ret = 0;
	struct cam_frame *pframe = NULL;
	struct isp_port *port = NULL;
	struct cam_pipeline *pipeline = NULL;
	struct cam_node *cam_node = NULL;
	if (!node || !param) {
		pr_err("fail to get valid param %p %p\n", node, param);
		return -EFAULT;
	}
	node = VOID_PTR_TO(node, struct isp_node);
	pframe = VOID_PTR_TO(param, struct cam_frame);
	cam_node = (struct cam_node *)node->data_cb_handle;
	pipeline = (struct cam_pipeline *)cam_node->data_cb_handle;
	cam_node->buf_manager_handle = (struct cam_node *)node->buf_manager_handle;
	pipeline->buf_manager_handle = (struct cam_pipeline *)cam_node->buf_manager_handle;
	node->src.w = pframe->common.width;
	node->src.h = pframe->common.height;
	if (pipeline->debug_log_switch)
		pr_info("pipeline_type %s, fid %d, ch_id %d, buf %x, w %d, h %d, pframe->common.is_reserved %d, compress_en %d\n",
			pipeline->pipeline_graph->name, pframe->common.fid, pframe->common.channel_id, pframe->common.buf.mfd,
			pframe->common.width, pframe->common.height, pframe->common.is_reserved, pframe->common.is_compressed);

	CAM_QUEUE_FOR_EACH_ENTRY(port, &node->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0) {
			ret = isp_port_param_cfg(port, PORT_CFG_BUFFER_SET, pframe);
			if (ret) {
				pr_warn("warning: isp node proc in q may full\n");
				return ret;
			}
		}
	}

	if (node->uinfo.enable_slowmotion) {
		if (++(node->slw_frm_cnt) < node->uinfo.slowmotion_count)
			return ret;
	}
	if (atomic_read(&node->user_cnt) > 0)
		complete(&node->thread.thread_com);
	node->slw_frm_cnt = 0;
	return ret;
}

void *isp_node_get(uint32_t node_id, struct isp_node_desc *param)
{
	int ret = 0, i = 0, loop = 0;
	struct isp_node *node = NULL;
	struct cam_thread_info *thrd = NULL;
	struct isp_node_uinfo *uinfo = NULL;
	struct isp_ltm_ctx_desc *rgb_ltm = NULL;
	struct isp_gtm_ctx_desc *rgb_gtm = NULL;
	struct isp_rec_ctx_desc *rec_ctx = NULL;
	struct cam_frame *param_frame = NULL;
	if (!param) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	pr_info("node id %d node_dev %p\n", node_id, *param->node_dev);
	if (*param->node_dev == NULL) {
		node = cam_buf_kernel_sys_vzalloc(sizeof(struct isp_node));
		if (!node) {
			pr_err("fail to get valid isp node\n");
			return NULL;
		}
	} else {
		node = *param->node_dev;
		pr_info("isp offline node has been alloc %p\n", node);
		goto exit;
	}
	if (node->data_cb_func == NULL) {
		node->data_cb_func = param->data_cb_func;
		node->data_cb_handle = param->data_cb_handle;
	}
	node->dev = param->dev;
	node->buf_manager_handle = param->buf_manager_handle;
	ret = node->dev->isp_ops->ioctl(node->dev, ISP_IOCTL_INIT_NODE_HW, &node->cfg_id);
	mutex_init(&node->blkpm_lock);
	init_completion(&node->frm_done);
	init_completion(&node->ultra_cap_com);
	node->isp_k_param.cfg_id = node->cfg_id;
	/* complete for first frame config */
	complete(&node->frm_done);
	cam_block_isp_init(&node->isp_k_param);

	node->is_dual = param->is_dual;
	node->attach_cam_id = param->cam_id;
	node->uinfo.enable_slowmotion = 0;
	node->uinfo.is_node_close = 0;
	ret = cam_buf_manager_pool_reg(NULL, ISP_HIST2_BUF_Q_LEN, node->buf_manager_handle);
	if (ret <= 0)
		goto pool_reg_err;
	node->hist2_pool.private_pool_id = ret;

	ret = cam_buf_manager_pool_reg(NULL, ISP_GTMHIST_BUF_Q_LEN, node->buf_manager_handle);
	if (ret <= 0) {
		cam_buf_manager_pool_unreg(&node->hist2_pool, node->buf_manager_handle);
		goto pool_reg_err;
	}
	node->gtmhist_outpool.private_pool_id = ret;

	ret = cam_buf_manager_pool_reg(NULL, ISP_GTMHIST_BUF_Q_LEN, node->buf_manager_handle);
	if (ret <= 0) {
		cam_buf_manager_pool_unreg(&node->hist2_pool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->gtmhist_outpool, node->buf_manager_handle);
		goto pool_reg_err;
	}
	node->gtmhist_resultpool.private_pool_id = ret;

	ret = cam_buf_manager_pool_reg(NULL, ISP_LTMHIST_BUF_Q_LEN, node->buf_manager_handle);
	if (ret <= 0) {
		cam_buf_manager_pool_unreg(&node->hist2_pool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->gtmhist_outpool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->gtmhist_resultpool, node->buf_manager_handle);
		goto pool_reg_err;
	}
	node->ltmhist_outpool.private_pool_id = ret;

	ret = cam_buf_manager_pool_reg(NULL, ISP_LTMHIST_BUF_Q_LEN, node->buf_manager_handle);
	if (ret <= 0) {
		cam_buf_manager_pool_unreg(&node->ltmhist_outpool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->gtmhist_outpool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->gtmhist_resultpool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->hist2_pool, node->buf_manager_handle);
		goto pool_reg_err;
	}
	node->ltmhist_resultpool.private_pool_id = ret;

	CAM_QUEUE_INIT(&node->port_queue, PORT_ISP_MAX, NULL);
	if (param->is_high_fps)
		node->uinfo.enable_slowmotion = node->dev->isp_hw->ip_isp->isphw_abt->slm_cfg_support;
	if (node->uinfo.enable_slowmotion) {
		node->slw_frm_cnt = 0;
		node->uinfo.enable_slowmotion = param->enable_slowmotion;
		node->uinfo.slowmotion_count = param->slowmotion_count;
	}
	pr_debug("cam%d isp slowmotion eb %d, is_dual:%d\n",node->attach_cam_id, node->uinfo.enable_slowmotion, node->is_dual);

	node->is_bind = 0;
	uinfo = &node->uinfo;

	if (node->rgb_ltm_handle == NULL && node->dev->isp_hw->ip_isp->isphw_abt->rgb_ltm_support) {
		node->rgb_ltm_handle = isp_ltm_rgb_ctx_get(node->cfg_id, node->attach_cam_id, node->dev->isp_hw);
		if (!node->rgb_ltm_handle) {
			pr_err("fail to get memory for ltm_rgb_ctx.\n");
			ret = -ENOMEM;
			goto rgb_ltm_err;
		}
		rgb_ltm = (struct isp_ltm_ctx_desc *)node->rgb_ltm_handle;
	}

	uinfo->mode_ltm = param->mode_ltm;
	uinfo->ltm_rgb = param->ltm_rgb;
	uinfo->pyr_layer_num = param->pyr_layer_num;
	if (rgb_ltm) {
		rgb_ltm->resbuf_get_cb = param->resbuf_get_cb;
		rgb_ltm->resbuf_cb_data = param->resbuf_cb_data;
		rgb_ltm->hist_outpool = &node->ltmhist_outpool;
		rgb_ltm->hist_resultpool = &node->ltmhist_resultpool;
		rgb_ltm->buf_manager_handle = param->buf_manager_handle;
		rgb_ltm->ltm_ops.cfg_param(rgb_ltm, ISP_LTM_CFG_EB, &uinfo->ltm_rgb);
		rgb_ltm->ltm_ops.cfg_param(rgb_ltm, ISP_LTM_CFG_MODE, &uinfo->mode_ltm);
	}

	if (node->rgb_gtm_handle == NULL && node->dev->isp_hw->ip_isp->isphw_abt->rgb_gtm_support) {
		node->rgb_gtm_handle = ispgtm_rgb_ctx_get(node->cfg_id, node->dev->isp_hw);
		if (!node->rgb_gtm_handle) {
			pr_err("fail to get memory for gtm_rgb_ctx.\n");
			ret = -ENOMEM;
			goto rgb_gtm_err;
		}
		rgb_gtm = (struct isp_gtm_ctx_desc *)node->rgb_gtm_handle;
		rgb_gtm->outpool = &node->gtmhist_outpool;
		rgb_gtm->resultpool = &node->gtmhist_resultpool;
		rgb_gtm->buf_manager_handle = param->buf_manager_handle;
	}
	uinfo->gtm_rgb = param->gtm_rgb;
	uinfo->mode_gtm = param->mode_gtm;
	if (rgb_gtm) {
		rgb_gtm->gtm_ops.cfg_param(rgb_gtm, ISP_GTM_CFG_EB, &uinfo->gtm_rgb);
		rgb_gtm->gtm_ops.cfg_param(rgb_gtm, ISP_GTM_CFG_MODE, &uinfo->mode_gtm);
	}

	if (node->nr3_handle == NULL) {
		node->nr3_handle = isp_3dnr_ctx_get(node->cfg_id);
		if (!node->nr3_handle) {
			pr_err("fail to get memory for nr3_ctx.\n");
			ret = -ENOMEM;
			goto nr3_err;
		}
	}
	uinfo->fetch_path_sel = param->fetch_path_sel;
	uinfo->nr3_fbc_fbd = param->nr3_fbc_fbd;

	uinfo->mode_3dnr = param->mode_3dnr;
	node->nr3_blend_cnt = 0;

	if (node->rec_handle == NULL && node->dev->isp_hw->ip_isp->isphw_abt->pyr_rec_support &&
		node->dev->sec_mode == SEC_UNABLE) {
		node->rec_handle = isp_pyr_rec_ctx_get(node->cfg_id, node->dev, node->buf_manager_handle);
		if (!node->rec_handle) {
			pr_err("fail to get memory for rec_ctx.\n");
			ret = -ENOMEM;
			goto rec_err;
		}
		rec_ctx = (struct isp_rec_ctx_desc *)node->rec_handle;
	}

	/* create isp offline thread */
	thrd = &node->thread;
	thrd->data_cb_func = node->data_cb_func;
	thrd->data_cb_handle = node->data_cb_handle;
	thrd->error_type = CAM_CB_ISP_DEV_ERR;
	snprintf(thrd->thread_name, ARRAY_SIZE(thrd->thread_name), "isp_node%d_offline", node_id);
	ret = camthread_create(node, thrd, ispnode_start_proc);
	if (ret) {
		pr_err("fail to get isp node start proc.\n");
		ret = -ENOMEM;
		goto proc_thread_err;
	}

	mutex_init(&node->blkpm_q_lock);
	CAM_QUEUE_INIT(&node->param_share_queue, param->blkparam_node_num, cam_queue_empty_frame_put);
	CAM_QUEUE_INIT(&node->param_buf_queue, param->blkparam_node_num, cam_queue_empty_frame_put);
	for (i = 0; i < param->blkparam_node_num; i++) {
		param_frame = cam_queue_empty_frame_get(CAM_FRAME_ISP_BLK);
		if (!param_frame) {
			pr_err("fail to get param_frame.\n");
			goto alloc_error;
		}
		param_frame->isp_blk.param_block = cam_buf_kernel_sys_vzalloc(sizeof(struct dcam_isp_k_block));
		if (!param_frame->isp_blk.param_block) {
			pr_err("fail to alloc.\n");
			ret = -ENOMEM;
			goto alloc_error;
		}

		ret = cam_queue_recycle_blk_param(&node->param_share_queue, param_frame);
		if (ret) {
			pr_err("fail to enqueue shared buf: %d ch %d\n", i, node->ch_id);
			ret = -ENOMEM;
			goto alloc_error;
		}
	}

	isp_int_common_irq_sw_cnt_reset(node->cfg_id);
	node->ch_id = param->ch_id;
	node->share_buffer = param->share_buffer;
	node->node_id = node_id;
	node->node_type = param->node_type;
	node->ultra_cap_en = param->ultra_cap_en;
	*param->node_dev = node;

	pr_info("node id %d cur_ctx_id %d cfg_id %d blkparam_node_num:%d\n", node_id, node->pctx_hw_id, node->cfg_id, param->blkparam_node_num);

exit:
	if (param->is_high_fps) {
		node->uinfo.slw_960desc = param->fps960_info;
		if (node->uinfo.slw_960desc.slowmotion_stage_a_valid_num) {
			node->uinfo.frame_num.stage_a_frame_num = node->uinfo.slw_960desc.slowmotion_stage_a_num;
			node->uinfo.stage_a_valid_count = node->uinfo.slw_960desc.slowmotion_stage_a_valid_num;
			node->uinfo.frame_num.stage_b_frame_num = node->uinfo.slw_960desc.slowmotion_stage_b_num;
			node->uinfo.frame_num.stage_c_frame_num = node->uinfo.slw_960desc.slowmotion_stage_a_num;
		}
	}
	atomic_inc(&node->user_cnt);
	param->isp_node = node;
	return node;
alloc_error:
	loop = CAM_QUEUE_CNT_GET(&node->param_share_queue);
	do {
		param_frame = CAM_QUEUE_DEQUEUE(&node->param_share_queue, struct cam_frame, list);
		if (param_frame)
			cam_queue_empty_frame_put(param_frame);
	} while (loop-- > 0);
	camthread_stop(&node->thread);
proc_thread_err:
	if (node->rec_handle && node->dev->isp_hw->ip_isp->isphw_abt->pyr_rec_support &&
		node->dev->sec_mode == SEC_UNABLE) {
		isp_pyr_rec_ctx_put(node->rec_handle, node->buf_manager_handle);
		node->rec_handle = NULL;
	}
rec_err:
	if (node->nr3_handle) {
		isp_3dnr_ctx_put(node->nr3_handle);
		node->nr3_handle = NULL;
	}
nr3_err:
	if (node->rgb_gtm_handle && node->dev->isp_hw->ip_isp->isphw_abt->rgb_gtm_support) {
		isp_gtm_rgb_ctx_put(node->rgb_gtm_handle);
		node->rgb_gtm_handle = NULL;
	}
rgb_gtm_err:
	if (node->rgb_ltm_handle) {
		isp_ltm_rgb_ctx_put(node->rgb_ltm_handle);
		node->rgb_ltm_handle = NULL;
	}
rgb_ltm_err:
pool_reg_err:
	cam_buf_kernel_sys_vfree(node);
	node = NULL;
	return NULL;
}

void isp_node_put(struct isp_node *node)
{
	struct isp_ltm_ctx_desc *rgb_ltm = NULL;

	if (!node) {
		pr_err("fail to get invalid node ptr\n");
		return;
	}

	if (atomic_dec_return(&node->user_cnt) == 0) {
		struct isp_cfg_ctx_desc *cfg_desc = node->dev->cfg_handle;
		if (node->rgb_gtm_handle && node->dev->isp_hw->ip_isp->isphw_abt->rgb_gtm_support) {
			isp_gtm_rgb_ctx_put(node->rgb_gtm_handle);
			node->rgb_gtm_handle = NULL;
		}

		if (node->rgb_ltm_handle) {
			rgb_ltm = (struct isp_ltm_ctx_desc *)node->rgb_ltm_handle;
			rgb_ltm->resbuf_get_cb = NULL;
			rgb_ltm->resbuf_cb_data = NULL;
			isp_ltm_rgb_ctx_put(node->rgb_ltm_handle);
			node->rgb_ltm_handle = NULL;
		}
		if (node->nr3_handle) {
			isp_3dnr_ctx_put(node->nr3_handle);
			node->nr3_handle = NULL;
		}
		if (node->rec_handle && node->dev->isp_hw->ip_isp->isphw_abt->pyr_rec_support &&
			node->dev->sec_mode == SEC_UNABLE) {
			isp_pyr_rec_ctx_put(node->rec_handle, node->buf_manager_handle);
			node->rec_handle = NULL;
		}

		if (node->postproc_buf)
			cam_queue_empty_frame_put(node->postproc_buf);
		pr_debug("cfg_id %d, postproc out buffer unmap\n", node->cfg_id);

		if (node->isp_receive_param) {
			cam_queue_recycle_blk_param(&node->param_share_queue, node->isp_receive_param);
			node->isp_receive_param = NULL;
		}
		pr_debug("share %d, buf %d\n", node->param_share_queue.cnt, node->param_buf_queue.cnt);
		mutex_lock(&node->blkpm_q_lock);
		CAM_QUEUE_CLEAN(&node->param_share_queue, struct cam_frame, list);
		CAM_QUEUE_CLEAN(&node->param_buf_queue, struct cam_frame, list);
		mutex_unlock(&node->blkpm_q_lock);

		cam_buf_manager_pool_unreg(&node->hist2_pool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->gtmhist_outpool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->gtmhist_resultpool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->ltmhist_outpool, node->buf_manager_handle);
		cam_buf_manager_pool_unreg(&node->ltmhist_resultpool, node->buf_manager_handle);

		node->data_cb_func = NULL;
		isp_int_common_irq_sw_cnt_trace(node->cfg_id);
		cam_statis_isp_port_buffer_deinit(node->dev, node);
		cfg_desc->ops->ctx_put(cfg_desc, node->cfg_id);

		pr_info("isp offline node %d put success\n", node->node_id);
		cam_buf_kernel_sys_vfree(node);
		node = NULL;
	}
}

void isp_node_close(struct isp_node *node)
{
	int ret = 0, loop = 0;
	if (!node) {
		pr_err("fail to get invalid node ptr\n");
		return;
	}
	if (node->thread.thread_task) {
		node->uinfo.is_node_close = 1;
		camthread_stop(&node->thread);
		/* wait for last frame done */
		ret = wait_for_completion_timeout(
			&node->frm_done, ISP_CONTEXT_TIMEOUT);
		if (ret == 0)
			pr_err("fail to wait node %d, timeout.\n", node->node_id);
		else
			pr_info("wait time %d ms\n", ret);

		cam_debug_logfile_deinit(&node->debug.block_param_log);
		node->dev->isp_ops->unbind(node);
		/* make sure irq handler exit to avoid crash */
		while (node->in_irq_postproc && loop < 1000) {
			pr_debug("node %d in irq. wait %d\n", node->node_id, loop);
			loop++;
			os_adapt_time_udelay(500);
		};
		if (loop == 1000)
			pr_warn("warning: isp node close wait irq timeout\n");
		CAM_QUEUE_CLEAN(&node->port_queue, struct isp_port, list);
	}
}

void isp_node_debug_init(struct isp_node *node, uint32_t ch_id)
{
	char filename[128] = {0};
	timespec timestamp = {0};

	if (node->debug.block_param_log.log_file)
		return;

	os_adapt_time_get_ts(&timestamp);
	snprintf(filename, sizeof(filename), "/data/ylog/isp node%d_block_param_%lld.%lld.txt",
		node->cfg_id, timestamp.tv_sec,
		(long long)(timestamp.tv_nsec / NSEC_PER_USEC));
	node->debug.block_param_log.ip_info = IP_ISP;

	cam_debugger_logfile_init(&node->debug.block_param_log, filename);
}

