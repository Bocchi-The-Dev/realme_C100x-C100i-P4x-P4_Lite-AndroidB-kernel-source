// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022-2023 Unisoc(Shanghai) Technologies Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "cam_pipeline.h"
#include "isp_cfg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_SCALER_NODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static void *ispscaler_node_context_bind(void *node, int slice_need, isp_irq_postproc postproc_func)
{
	int i = 0, m = 0, use_fmcu = 0;
	int hw_ctx_id = -1;
	unsigned long flag = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	struct isp_yuv_scaler_node *inode = NULL;

	if (!node) {
		pr_err("fail to get node\n");
		return NULL;
	}
	inode = (struct isp_yuv_scaler_node *)node;
	dev = inode->dev;
	spin_lock_irqsave(&dev->ctx_lock, flag);

	if (slice_need)
		use_fmcu = FMCU_IS_NEED;

	for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
		pctx_hw = &dev->hw_ctx[i];
		if (pctx_hw->node == inode) {
			atomic_inc(&pctx_hw->user_cnt);
			pr_debug("node %d & hw %d already binding, cnt=%d\n",
				inode->node_id, i, atomic_read(&pctx_hw->user_cnt));
			spin_unlock_irqrestore(&dev->ctx_lock, flag);
			return pctx_hw;
		}
	}

	for (m = 0; m < 2; m++) {
		for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
			pctx_hw = &dev->hw_ctx[i];
			if (m == 0) {
				/* first pass we just select fmcu/no-fmcu context */
				if ((!use_fmcu && pctx_hw->fmcu_handle) ||
					(use_fmcu && !pctx_hw->fmcu_handle))
					continue;
			}

			if (atomic_inc_return(&pctx_hw->user_cnt) == 1) {
				hw_ctx_id = pctx_hw->hw_ctx_id;
				goto exit;
			}
			atomic_dec(&pctx_hw->user_cnt);
		}
	}

exit:
	spin_unlock_irqrestore(&dev->ctx_lock, flag);

	if (hw_ctx_id == -1)
		return NULL;

	pctx_hw->node = inode;
	pctx_hw->node_id = inode->node_id;
	pctx_hw->cfg_id = inode->cfg_id;
	pctx_hw->fmcu_used = 0;
	inode->is_bind = 1;
	pctx_hw->postproc_func = postproc_func;
	pr_debug("sw %d, hw %d %d, fmcu_need %d fmcu %p\n",
		inode->node_id, hw_ctx_id, pctx_hw->hw_ctx_id,
		use_fmcu, pctx_hw->fmcu_handle);
	return pctx_hw;
}

static uint32_t ispscaler_node_context_unbind(void *node)
{
	int i = 0, cnt = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	struct isp_yuv_scaler_node *inode = NULL;
	unsigned long flag = 0;

	inode = VOID_PTR_TO(node, struct isp_yuv_scaler_node);
	dev = inode->dev;

	spin_lock_irqsave(&dev->ctx_lock, flag);
	if (!inode->is_bind) {
		pr_debug("binding node_id %d to any hw ctx, inode->is_bind %d\n",
			inode->node_id, inode->is_bind);
		spin_unlock_irqrestore(&dev->ctx_lock, flag);
		return 0;
	}

	for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
		pctx_hw = &dev->hw_ctx[i];
		if ((inode->node_id != pctx_hw->node_id) ||
			(inode != pctx_hw->node))
			continue;

		if (atomic_dec_return(&pctx_hw->user_cnt) == 0) {
			pr_debug("sw_id=%d, hw_id=%d unbind success\n",
				inode->node_id, pctx_hw->hw_ctx_id);
			pctx_hw->node = NULL;
			pctx_hw->postproc_func = NULL;
			inode->is_bind = 0;
			goto exit;
		}

		cnt = atomic_read(&pctx_hw->user_cnt);
		if (cnt >= 1) {
			pr_debug("sw id=%d, hw_id=%d, cnt=%d\n",
				inode->node_id, pctx_hw->hw_ctx_id, cnt);
		} else {
			pr_debug("should not be here: sw id=%d, hw id=%d, cnt=%d\n",
				inode->node_id, pctx_hw->hw_ctx_id, cnt);
			spin_unlock_irqrestore(&dev->ctx_lock, flag);
			return -EINVAL;
		}
	}

exit:
	pctx_hw->fmcu_used = 0;
	spin_unlock_irqrestore(&dev->ctx_lock, flag);
	return 0;
}

static void ispscaler_node_src_frame_ret(void *param)
{
	struct cam_frame *frame = NULL;
	struct isp_yuv_scaler_node *inode = NULL;

	if (!param) {
		pr_err("fail to get input ptr.\n");
		return;
	}

	frame = (struct cam_frame *)param;
	inode = (struct isp_yuv_scaler_node *)frame->common.priv_data;
	if (!inode) {
		pr_err("fail to get src_frame pctx.\n");
		return;
	}

	pr_debug("frame %p, ch_id %d, buf_fd %d\n",
		frame, frame->common.channel_id, frame->common.buf.mfd);
	if (frame->common.buf.mapping_state & CAM_BUF_MAPPING_ISP)
		cam_buf_manager_buf_status_cfg(&frame->common.buf,
			CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
}

static uint32_t ispscaler_node_insert_port(struct isp_yuv_scaler_node *node, void *param)
{
	struct isp_scaler_port *q_port = NULL;
	struct isp_scaler_port *new_port = NULL;
	uint32_t is_exist = 0;

	new_port = VOID_PTR_TO(param, struct isp_scaler_port);
	pr_debug("node type:%d, id:%d, port type:%d new_port %p\n",
		node->node_type, node->node_id, new_port->port_id, new_port);

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

static int ispscaler_node_fast_stop_cfg(void *handle)
{
	struct isp_yuv_scaler_node *node = NULL;

	node = VOID_PTR_TO(handle, struct isp_yuv_scaler_node);
	if (CAM_QUEUE_CNT_GET(&node->in_queue) == 0 &&
		CAM_QUEUE_CNT_GET(&node->proc_queue) == 0) {
		node->is_fast_stop = 0;
		complete(node->fast_stop_done);
	} else
		node->is_fast_stop = 1;
	return 0;
}

static struct cam_frame *ispscaler_node_fetch_frame_cycle(struct isp_yuv_scaler_node *node)
{
	int ret = 0;
	uint32_t loop = 0;
	struct cam_frame *pframe = NULL;

	pframe = CAM_QUEUE_DEQUEUE(&node->in_queue, struct cam_frame, list);
	if (!pframe) {
		pr_err("fail to get input frame (%p) for node %d\n", pframe, node->node_id);
		return NULL;
	}
	if (node->is_fast_stop) {
		ispscaler_node_fast_stop_cfg(node);
		return NULL;
	}

	loop = 0;
	do {
		ret = CAM_QUEUE_ENQUEUE(&node->proc_queue, &pframe->list);
		if (ret == 0)
			break;
		pr_info_ratelimited("wait for proc queue. loop %d\n", loop);
		/* wait for previous frame proccessed done. */
		os_adapt_time_usleep_range(600, 2000);
	} while (loop++ < 500);

	if (ret) {
		pr_err("fail to get proc queue, timeout.\n");
		return NULL;
	}

	return pframe;
}

static uint32_t ispscaler_node_fid_across_context_get(
	struct isp_yuv_scaler_node *inode, enum camera_id cam_id)
{
	struct isp_scaler_port *port = NULL;
	struct cam_frame *frame;
	uint32_t target_fid;

	if (!inode)
		return CAMERA_RESERVE_FRAME_NUM;

	target_fid = CAMERA_RESERVE_FRAME_NUM;

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT &&
			atomic_read(&port->user_cnt) >= 1 && inode->uinfo.uframe_sync) {
			frame = CAM_QUEUE_DEQUEUE_PEEK(
				&port->out_buf_queue, struct cam_frame, list);


			if (frame) {
				target_fid = min(target_fid, frame->common.user_fid);
				pr_debug("node id%d port%d user_fid %u\n",
					inode->node_id, port->port_id, frame->common.user_fid);
			} else
				pr_err("fail to dequeue frame\n");
		}
	}
	pr_debug("target_fid %u, cam_id = %d\n", target_fid, cam_id);

	return target_fid;
}

static void *ispscaler_node_hwctx_to_node(enum isp_context_hw_id hw_ctx_id, void *dev_handle)
{
	struct isp_hw_context *pctx_hw = NULL;
	struct isp_yuv_scaler_node *node = NULL;
	struct isp_pipe_dev *dev = NULL;

	dev = (struct isp_pipe_dev *)dev_handle;
	if (hw_ctx_id < ISP_CONTEXT_HW_NUM) {
		pctx_hw = &dev->hw_ctx[hw_ctx_id];
		if (pctx_hw->node) {
			node = pctx_hw->node;
			return node;
		}
	}
	return NULL;
}

static void *ispscaler_node_hw_start(
	struct isp_yuv_scaler_node *inode, struct isp_hw_context *pctx_hw)
{	int ret = 0;
	struct isp_hw_yuv_block_ctrl blk_ctrl = {0};
	struct isp_cfg_ctx_desc *cfg_desc = NULL;
	struct isp_pipe_dev *dev = NULL;
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct cam_hw_info *hw = NULL;

	dev = inode->dev;
	hw = dev->isp_hw;
	cfg_desc = (struct isp_cfg_ctx_desc *)dev->cfg_handle;
	fmcu = (struct isp_fmcu_ctx_desc *)pctx_hw->fmcu_handle;

	/* start to prepare/kickoff cfg buffer. */
	if (likely(dev->wmode == ISP_CFG_MODE)) {
		pr_debug("cfg enter.");

		/* blkpm_lock to avoid user config block param across frame */
		mutex_lock(&inode->blkpm_lock);
		blk_ctrl.idx = inode->cfg_id;
		hw->isp_ioctl(hw, ISP_HW_CFG_YUV_BLOCK_BYPASS, &blk_ctrl);
		ret = cfg_desc->ops->hw_cfg(cfg_desc, inode->cfg_id,
			pctx_hw->hw_ctx_id, pctx_hw->fmcu_used);
		mutex_unlock(&inode->blkpm_lock);
		if (pctx_hw->fmcu_used) {
			pr_info("isp scaler node %d w %d h %d fmcu start\n",
				inode->cfg_id, inode->pipe_src.crop.size_x,
				inode->pipe_src.crop.size_y);
			ret = fmcu->ops->hw_start(fmcu);
		} else
			hw->isp_ioctl(hw, ISP_HW_CFG_START_ISP, &pctx_hw->hw_ctx_id);
	} else {
		if (pctx_hw->fmcu_used) {
			pr_info("fmcu start.\n");
			ret = fmcu->ops->hw_start(fmcu);
		} else {
			pr_debug("fetch start.\n");
			hw->isp_ioctl(hw, ISP_HW_CFG_FETCH_START, NULL);
		}
	}
	return 0;
}

static int ispscaler_node_slice_needed(struct isp_yuv_scaler_node *inode)
{
	struct isp_scaler_port *port = NULL;

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT) {
			if (port->size.w > g_camctrl.isp_linebuf_len)
				return 1;
		}
	}
	return 0;
}

static int ispscaler_node_slice_base_cfg(void *cfg_in, void *slice_ctx, uint32_t *valid_slc_num)
{
	int i = 0;
	int ret = 0;
	struct slice_cfg_input *in_ptr = (struct slice_cfg_input *)cfg_in;
	struct isp_slice_context *slc_ctx = (struct isp_slice_context *)slice_ctx;

	if (!in_ptr || !slc_ctx || !valid_slc_num) {
		pr_err("fail to get input ptr, null.\n");
		return -EFAULT;
	}

	memset(slc_ctx, 0, sizeof(struct isp_slice_context));
	isp_slice_base_info_cfg(in_ptr, slc_ctx);
	isp_slice_scaler_info_cfg(in_ptr, slc_ctx);

	*valid_slc_num = 0;
	for (i = 0; i < SLICE_NUM_MAX; i++) {
		if (slc_ctx->slices[i].valid)
			*valid_slc_num = (*valid_slc_num) + 1;
	}

	return ret;
}

static int ispscaler_node_slice_ctx_init(
	struct isp_yuv_scaler_node *inode, struct isp_pipe_info *pipe_info)
{
	int ret = 0, hw_path_id = 0;
	struct slice_cfg_input slc_cfg_in = {0};
	struct cam_hw_info *hw_info = NULL;
	struct isp_scaler_port *port = NULL;

	hw_info = (struct cam_hw_info *)inode->dev->isp_hw;
	if (!hw_info) {
		pr_err("fail to get hw info NULL\n");
		goto exit;
	}

	if (inode->slice_ctx == NULL) {
		inode->slice_ctx = isp_slice_ctx_get();
		if (IS_ERR_OR_NULL(inode->slice_ctx)) {
			pr_err("fail to get memory for slice_ctx.\n");
			inode->slice_ctx = NULL;
			ret = -ENOMEM;
			goto exit;
		}
	}

	memset(&slc_cfg_in, 0, sizeof(struct slice_cfg_input));

	slc_cfg_in.frame_in_size.w = pipe_info->fetch.in_trim.size_x;
	slc_cfg_in.frame_in_size.h = pipe_info->fetch.in_trim.size_y;
	slc_cfg_in.frame_fetch = &pipe_info->fetch;
	pipe_info->fetch_fbd_yuv.fetch_fbd_bypass = 1;
	slc_cfg_in.frame_fbd_yuv = &pipe_info->fetch_fbd_yuv;
	slc_cfg_in.thumb_scaler = &pipe_info->thumb_scaler;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) >= 1) {
			hw_path_id = isp_scaler_port_id_switch(port->port_id);
			if (hw_path_id >= ISP_SPATH_NUM)
				continue;
			slc_cfg_in.frame_out_size[hw_path_id] =
				&pipe_info->store[hw_path_id].store.size;
			slc_cfg_in.frame_store[hw_path_id] = &pipe_info->store[hw_path_id].store;
			slc_cfg_in.frame_scaler[hw_path_id] = &pipe_info->scaler[hw_path_id].scaler;
			slc_cfg_in.frame_deci[hw_path_id] = &pipe_info->scaler[hw_path_id].deci;
			slc_cfg_in.frame_trim0[hw_path_id] = &pipe_info->scaler[hw_path_id].in_trim;
			slc_cfg_in.frame_trim1[hw_path_id] =
				&pipe_info->scaler[hw_path_id].out_trim;
		}
	}
	slc_cfg_in.calc_dyn_ov.version = hw_info->ip_isp->isphw_abt->dyn_overlap_version;
	ispscaler_node_slice_base_cfg(&slc_cfg_in, inode->slice_ctx, &inode->valid_slc_num);

	pr_debug("sw %d valid_slc_num %d\n", inode->cfg_id, inode->valid_slc_num);
exit:
	return ret;
}

static int ispscaler_node_slice_info_cfg(void *cfg_in, struct isp_slice_context *slc_ctx)
{
	struct slice_cfg_input *in_ptr = NULL;

	if (!cfg_in || !slc_ctx) {
		pr_err("fail to get input ptr, null.\n");
		return -EFAULT;
	}

	in_ptr = (struct slice_cfg_input *)cfg_in;
	isp_slice_fetch_info_cfg(cfg_in, slc_ctx);
	isp_slice_store_info_cfg(cfg_in, slc_ctx);

	return 0;
}

static int ispscaler_node_slice_fmcu_cmds_set(
	void *fmcu_handle, void *node, struct isp_hw_context *pctx_hw)
{
	int i = 0, j = 0;
	int cfg_id = 0;
	int hw_ctx_id = 0;
	struct isp_yuv_scaler_node *inode = NULL;
	enum isp_work_mode wmode;
	struct isp_slice_desc *cur_slc;
	struct slice_store_info *slc_store;
	struct slice_scaler_info *slc_scaler;
	struct isp_slice_context *slc_ctx;
	struct isp_fmcu_ctx_desc *fmcu;
	struct cam_hw_info *hw = NULL;
	struct isp_hw_fmcu_cfg fmcu_cfg;
	struct isp_hw_slices_fmcu_cmds parg;
	struct isp_hw_slice_spath spath_sotre;
	struct isp_hw_slice_spath spath_scaler;
	struct isp_hw_slice_spath_thumbscaler thumbscaler;
	struct isp_hw_set_slice_fetch fetcharg;

	if (!fmcu_handle || !node || !pctx_hw) {
		pr_err("fail to get valid input ptr, fmcu_handle %p, node %p, pctx_hw %p\n",
			fmcu_handle, node, pctx_hw);
		return -EFAULT;
	}
	inode = (struct isp_yuv_scaler_node *)node;
	hw = inode->dev->isp_hw;
	cfg_id = inode->cfg_id;
	hw_ctx_id = pctx_hw->hw_ctx_id;
	pr_debug("get hw context id=%d\n", hw_ctx_id);

	wmode = inode->dev->wmode;
	slc_ctx = (struct isp_slice_context *)inode->slice_ctx;
	if (slc_ctx->slice_num < 1) {
		pr_err("fail to use slices, not support here.\n");
		return -EINVAL;
	}

	fmcu = (struct isp_fmcu_ctx_desc *)fmcu_handle;
	cur_slc = &slc_ctx->slices[0];
	for (i = 0; i < SLICE_NUM_MAX; i++, cur_slc++) {
		if (cur_slc->valid == 0)
			continue;
		if (wmode != ISP_CFG_MODE) {
			pr_debug("no need to cfg\n");
		} else {
			fmcu_cfg.fmcu = fmcu;
			fmcu_cfg.ctx_id = hw_ctx_id;
			hw->isp_ioctl(hw, ISP_HW_CFG_FMCU_CFG, &fmcu_cfg);
		}

		fetcharg.fmcu = fmcu;
		fetcharg.fetch_info = &cur_slc->slice_fetch;
		hw->isp_ioctl(fmcu, ISP_HW_CFG_SET_SLICE_FETCH, &fetcharg);

		for (j = 0; j < ISP_SPATH_NUM; j++) {
			slc_store = &cur_slc->slice_store[j];
			if (j == ISP_SPATH_FD) {
				thumbscaler.fmcu = fmcu;
				thumbscaler.path_en = cur_slc->path_en[j];
				thumbscaler.ctx_idx = cfg_id;
				thumbscaler.slc_scaler = &cur_slc->slice_thumbscaler;
				hw->isp_ioctl(hw, ISP_HW_CFG_SLICE_SPATH_THUMBSCALER, &thumbscaler);
			} else {
				slc_scaler = &cur_slc->slice_scaler[j];
				spath_scaler.fmcu = fmcu;
				spath_scaler.path_en = cur_slc->path_en[j];
				spath_scaler.ctx_idx = cfg_id;
				spath_scaler.spath_id = j;
				spath_scaler.slc_scaler = slc_scaler;
				hw->isp_ioctl(hw, ISP_HW_CFG_SLICE_SPATH_SCALER, &spath_scaler);
			}
			spath_sotre.fmcu = fmcu;
			spath_sotre.path_en = cur_slc->path_en[j];
			spath_sotre.ctx_idx = cfg_id;
			spath_sotre.spath_id = j;
			spath_sotre.slc_store = slc_store;
			hw->isp_ioctl(hw, ISP_HW_CFG_SLICE_SPATH_STORE, &spath_sotre);
		}

		parg.wmode = wmode;
		parg.hw_ctx_id = hw_ctx_id;
		parg.fmcu = fmcu;
		hw->isp_ioctl(hw, ISP_HW_CFG_SLICE_FMCU_CMD, &parg);
	}
	return 0;
}

static int ispscaler_node_slice_fmcu(struct isp_yuv_scaler_node *inode,
	struct isp_hw_context *pctx_hw, struct slice_cfg_input *slc_cfg)
{
	uint32_t ret = 0, hw_path_id = 0;
	struct isp_scaler_port *port = NULL;

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) >= 1) {
			hw_path_id = isp_scaler_port_id_switch(port->port_id);
			if (hw_path_id >= ISP_SPATH_NUM) {
				pr_err("fail to get correct path id\n");
				return -EFAULT;
			}
			slc_cfg->frame_store[hw_path_id] =
				&pctx_hw->pipe_info.store[hw_path_id].store;
		}
	}
	slc_cfg->frame_fetch = &pctx_hw->pipe_info.fetch;
	pctx_hw->pipe_info.fetch_fbd_yuv.fetch_fbd_bypass = 1;
	slc_cfg->frame_fbd_yuv = &pctx_hw->pipe_info.fetch_fbd_yuv;
	slc_cfg->frame_in_size.w = pctx_hw->pipe_info.fetch.in_trim.size_x;
	slc_cfg->frame_in_size.h = pctx_hw->pipe_info.fetch.in_trim.size_y;
	slc_cfg->calc_dyn_ov.version =
		pctx_hw->hw->ip_isp->isphw_abt->dyn_overlap_version;
	ispscaler_node_slice_info_cfg(slc_cfg, inode->slice_ctx);

	pr_info("use fmcu support slices for ctx %d\n", inode->cfg_id);
	if (pctx_hw->fmcu_handle)
		ret = ispscaler_node_slice_fmcu_cmds_set(pctx_hw->fmcu_handle, inode, pctx_hw);

	return ret;
}

static int ispscaler_node_offline_param_set(
	struct isp_yuv_scaler_node *inode, struct isp_hw_context *pctx_hw)
{
	int hw_path_id = 0;
	struct isp_scaler_port *port = NULL;

	if (!inode || !pctx_hw) {
		pr_err("fail to get input ptr, node %p, pctx_hw %p\n", inode, pctx_hw);
		return -EFAULT;
	}

	isp_hwctx_fetch_set(pctx_hw);

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) >= 1) {
			hw_path_id = isp_scaler_port_id_switch(port->port_id);
			isp_hwctx_scaler_set(pctx_hw, hw_path_id, NULL);
			isp_hwctx_store_set(pctx_hw, hw_path_id);
		}
	}
	pr_debug("done\n");
	return 0;
}

static int ispscaler_node_offline_param_cfg(struct isp_yuv_scaler_node *inode,
	struct isp_hw_context *pctx_hw, struct isp_scaler_port_cfg *port_cfg)
{
	int ret = 0;
	struct isp_scaler_port *port = NULL;

	if (!inode || !pctx_hw || !port_cfg) {
		pr_err("fail to get param\n");
		return -EINVAL;
	}

	port_cfg->cfg_id = inode->cfg_id;
	port_cfg->scaler_coeff_ex = inode->dev->isp_hw->ip_isp->isphw_abt->scaler_coeff_ex;
	port_cfg->scaler_bypass_ctrl = inode->dev->isp_hw->ip_isp->isphw_abt->scaler_bypass_ctrl;
	port_cfg->thumb_scaler_cal_version =
		inode->dev->isp_hw->ip_isp->isphw_abt->thumb_scaler_cal_version;
	port_cfg->pipe_info = &pctx_hw->pipe_info;
	port_cfg->target_fid = CAMERA_RESERVE_FRAME_NUM;
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		ret |= port->port_cfg_cb_func(port_cfg, ISP_SCALER_PORT_PIPEINFO_GET, port);
	}

	if (inode->pipe_src.uframe_sync)
		port_cfg->target_fid = ispscaler_node_fid_across_context_get(inode,
			inode->attach_cam_id);

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) >= 1)
			ret |= port->port_cfg_cb_func(port_cfg, ISP_SCALER_PORT_FRAME_CYCLE, port);
	}

	ret |= ispscaler_node_offline_param_set(inode, pctx_hw);
	return ret;
}

static int ispscaler_node_postproc_irq(
	void *handle, uint32_t hw_idx, enum isp_postproc_type type, void *param)
{
	int hw_path_id = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_yuv_scaler_node *inode = NULL;
	struct cam_frame *pframe = NULL;
	struct isp_scaler_port *port = NULL;

	if (!handle || type >= POSTPROC_MAX) {
		pr_err("fail to get valid input handle %p, type %d\n",
			handle, type);
		return -EFAULT;
	}

	dev = (struct isp_pipe_dev *)handle;
	inode = (struct isp_yuv_scaler_node *)ispscaler_node_hwctx_to_node(hw_idx, handle);

	if (atomic_read(&inode->user_cnt) < 1) {
		pr_debug("contex %d is stopped\n", inode->node_id);
		return 0;
	}
	if (unlikely(inode->started == 0)) {
		pr_debug("ctx %d not started\n", inode->node_id);
		return 0;
	}

	ispscaler_node_context_unbind(inode);
	complete(&inode->frm_done);

	pframe = CAM_QUEUE_DEQUEUE(&inode->proc_queue, struct cam_frame, list);
	if (pframe)
		pr_debug("isp cfg_id %d post proc, do not need to return frame\n", inode->cfg_id);
	else
		pr_err("fail to get src frame  sw_idx=%d  proc_queue.cnt:%d\n",
			inode->node_id, inode->proc_queue.cnt);

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) >= 1) {
			hw_path_id = isp_scaler_port_id_switch(port->port_id);
			pframe = CAM_QUEUE_DEQUEUE(&port->result_queue, struct cam_frame, list);
			if (!pframe) {
				pr_err("fail to get frame from queue. ctx:%d, path:%d\n",
					inode->node_id, hw_path_id);
				continue;
			}

			pr_debug("scaler node  %d hw_path_id %d, ch_id %d, fid %d, mfd 0x%x, queue cnt:%d, is_reserved %d\n",
				inode->node_id, hw_path_id, pframe->common.channel_id,
				pframe->common.fid, pframe->common.buf.mfd,
				port->result_queue.cnt, pframe->common.is_reserved);
			pr_debug("time_sensor %03d.%6d\n",
				(uint32_t)pframe->common.sensor_time.tv_sec,
				(uint32_t)pframe->common.sensor_time.tv_usec);

			if (unlikely(pframe->common.is_reserved))
				cam_queue_empty_frame_put(pframe);
			else {
				cam_buf_manager_buf_status_cfg(&pframe->common.buf,
					CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
				if (inode->is_fast_stop) {
					ispscaler_node_fast_stop_cfg(inode);
					port->data_cb_func(CAM_CB_ISP_SCALE_RET_ISP_BUF,
						pframe, port->data_cb_handle);
					return 0;
				}
				inode->data_cb_func(CAM_CB_ISP_RET_DST_BUF, pframe,
					inode->data_cb_handle);
			}
		}
	}
	return 0;
}

static int ispscaler_node_hwctx_slices_proc(void *inode, struct isp_hw_context *pctx_hw)
{
	int ret = 0;
	uint32_t slice_id, cnt = 0;
	struct isp_cfg_ctx_desc *cfg_desc = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_pipe_dev *dev = NULL;
	struct isp_yuv_scaler_node *node = NULL;
	struct isp_hw_yuv_block_ctrl blk_ctrl;

	node = VOID_PTR_TO(inode, struct isp_yuv_scaler_node);
	dev = (struct isp_pipe_dev *)node->dev;
	cfg_desc = (struct isp_cfg_ctx_desc *)dev->cfg_handle;

	pr_info("enter. valid_slc_num %d\n", node->valid_slc_num);
	pctx_hw->valid_slc_num = node->valid_slc_num;
	pctx_hw->is_last_slice = 0;
	hw = dev->isp_hw;
	for (slice_id = 0; slice_id < SLICE_NUM_MAX; slice_id++) {
		if (pctx_hw->is_last_slice == 1)
			break;

		ret = isp_slice_update(node->slice_ctx, hw, node->cfg_id, slice_id);
		if (ret < 0)
			continue;

		cnt++;
		if (cnt == pctx_hw->valid_slc_num)
			pctx_hw->is_last_slice = 1;
		pr_info("slice %d, valid %d, last %d\n", slice_id, pctx_hw->valid_slc_num,
			pctx_hw->is_last_slice);

		mutex_lock(&node->blkpm_lock);
		blk_ctrl.idx = pctx_hw->cfg_id;
		blk_ctrl.blk_param = pctx_hw->isp_using_param;
		hw->isp_ioctl(hw, ISP_HW_CFG_YUV_BLOCK_BYPASS, &blk_ctrl);

		if (dev->wmode == ISP_CFG_MODE)
			cfg_desc->ops->hw_cfg(cfg_desc, node->cfg_id, pctx_hw->hw_ctx_id, 0);
		mutex_unlock(&node->blkpm_lock);

		if (dev->wmode == ISP_CFG_MODE)
			cfg_desc->hw->isp_ioctl(pctx_hw->hw, ISP_HW_CFG_START_ISP,
				&pctx_hw->hw_ctx_id);
		else
			hw->isp_ioctl(hw, ISP_HW_CFG_FETCH_START, NULL);

		ret = wait_for_completion_interruptible_timeout(&pctx_hw->slice_done,
			ISP_CONTEXT_TIMEOUT);
		if (ret == -ERESTARTSYS) {
			pr_err("fail to interrupt, when isp wait\n");
			ret = -EFAULT;
			goto exit;
		} else if (ret == 0) {
			pr_err("fail to wait isp context %d, timeout.\n", pctx_hw->hw_ctx_id);
			ret = -EFAULT;
			goto exit;
		}
		pr_debug("slice %d done\n", slice_id);
	}

exit:
	return ret;
}

static int ispscaler_node_start_proc(void *node)
{
	int ret = 0, kick_fmcu = 0, result_ret = 0, slice_need = 0;
	struct cam_frame *pframe = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	struct isp_scaler_port *port = NULL;
	struct slice_cfg_input slc_cfg = {0};
	struct isp_scaler_port_cfg port_cfg = {0};
	struct isp_yuv_scaler_node *inode = NULL;

	inode = VOID_PTR_TO(node, struct isp_yuv_scaler_node);
	if (atomic_read(&inode->user_cnt) < 1) {
		pr_err("fail to init isp node\n");
		return -EFAULT;
	}

	port_cfg.node_id = inode->node_id;

	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_IN && atomic_read(&port->user_cnt) > 0)
			port_cfg.src_frame = CAM_QUEUE_DEQUEUE_PEEK(&inode->in_queue,
				struct cam_frame, list);

		if (atomic_read(&port->user_cnt) > 0)
			port->port_cfg_cb_func(&port_cfg, ISP_SCALER_PORT_SIZE_UPDATE, port);
	}

	slice_need = ispscaler_node_slice_needed(inode);
	pctx_hw = (struct isp_hw_context *)ispscaler_node_context_bind(inode, slice_need,
			ispscaler_node_postproc_irq);

	pframe = ispscaler_node_fetch_frame_cycle(inode);
	if (!pframe || !pctx_hw) {
		pr_err("fail to get param\n");
		ret = -EINVAL;
		goto input_err;
	}

	memcpy(&inode->pipe_src, &inode->uinfo, sizeof(inode->uinfo));
	port_cfg.valid_out_frame = 0;
	/*TBD: use to get node->queue, delete after buffer opt.*/
	port_cfg.node_handle = inode;
	ret = ispscaler_node_offline_param_cfg(inode, pctx_hw, &port_cfg);

	if (ret) {
		pr_err("fail to cfg offline param.\n");
		ret = -EINVAL;
		goto dequeue;
	}

	if (!port_cfg.valid_out_frame) {
		pr_info(" No available output buffer cfg id %d, hw %d,discard\n",
			inode->cfg_id, pctx_hw->hw_ctx_id);
		ret = -EINVAL;
		goto dequeue;
	}

	pctx_hw->valid_slc_num = 0;
	if (pctx_hw->fmcu_handle || slice_need) {
		ret = ispscaler_node_slice_ctx_init(inode, &pctx_hw->pipe_info);
		isp_hwctx_fmcu_reset(pctx_hw);
		ret = ispscaler_node_slice_fmcu(inode, pctx_hw, &slc_cfg);
		if (!ret)
			kick_fmcu = 1;
	}

	ret = wait_for_completion_interruptible_timeout(&inode->frm_done, ISP_CONTEXT_TIMEOUT);
	if (ret == -ERESTARTSYS) {
		pr_err("fail to interrupt, when isp wait\n");
		ret = -EFAULT;
		goto dequeue;
	} else if (ret == 0) {
		pr_err("fail to wait isp context %d, timeout.\n", inode->cfg_id);
		ret = -EFAULT;
		goto dequeue;
	}

	pctx_hw->iommu_status = 0xffffffff;
	inode->started = 1;
	pctx_hw->fmcu_used = kick_fmcu;

	if (!pctx_hw->fmcu_handle)
		ispscaler_node_hwctx_slices_proc(inode, pctx_hw);
	else
		ispscaler_node_hw_start(inode, pctx_hw);

	pr_debug("done.\n");
	return 0;
dequeue:
	result_ret = 1;
	pframe = CAM_QUEUE_DEQUEUE_TAIL(&inode->proc_queue, struct cam_frame, list);
input_err:
	CAM_QUEUE_FOR_EACH_ENTRY(port, &inode->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) >= 1) {
			if (result_ret)
				pframe = CAM_QUEUE_DEQUEUE_TAIL(&port->result_queue,
					struct cam_frame, list);
			else
				pframe = CAM_QUEUE_DEQUEUE_TAIL(&port->out_buf_queue,
					struct cam_frame, list);
			if (pframe) {
				if (pframe->common.buf.mapping_state & CAM_BUF_MAPPING_ISP)
					cam_buf_manager_buf_status_cfg(&pframe->common.buf,
						CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
				if (pframe->common.is_reserved)
					cam_queue_empty_frame_put(pframe);
				else
					port->data_cb_func(CAM_CB_ISP_SCALE_RET_ISP_BUF,
						pframe, port->data_cb_handle);

			}
		}
	}
	if (pctx_hw)
		ispscaler_node_context_unbind(inode);
	return ret;
}

int isp_scaler_node_cfg_param(void *node, uint32_t port_id, uint32_t cmd, void *param)
{
	int ret = 0;
	struct isp_yuv_scaler_node *inode = NULL;

	inode = VOID_PTR_TO(node, struct isp_yuv_scaler_node);

	switch (cmd) {
	case ISP_YUV_SCALER_NODE_INSERT_PORT:
		ispscaler_node_insert_port(inode, param);
		break;
	case ISP_YUV_SCALER_NODE_CFG_RESERVE_BUF:
		break;
	case ISP_YUV_SCALER_NODE_CFG_FAST_STOP:
		inode->fast_stop_done = VOID_PTR_TO(param, struct completion);
		ispscaler_node_fast_stop_cfg(inode);
		break;
	case ISP_YUV_SCALER_NODE_CFG_CLEAR_FAST_STOP:
		inode->is_fast_stop = 0;
		break;
	default:
		pr_err("fail to get node_id:%d cmd:%d\n", inode->node_id, cmd);
		break;
	}
	return ret;
}

int isp_scaler_node_request_proc(struct isp_yuv_scaler_node *node, void *param)
{
	struct cam_frame *pframe = NULL;
	struct cam_frame *dst_frame = NULL;
	struct isp_scaler_port *port = NULL;
	struct cam_pipeline *pipeline = NULL;
	struct cam_node *cam_node = NULL;
	int ret = 0;

	if (!node || !param) {
		pr_err("fail to get valid param %p %p\n", node, param);
		return -1;
	}

	pr_debug("node %p node->node_id %d cfg_id %d\n", node, node->node_id, node->cfg_id);
	pframe = (struct cam_frame *)param;
	pframe->common.priv_data = node;
	cam_node = (struct cam_node *)node->data_cb_handle;
	pipeline = (struct cam_pipeline *)cam_node->data_cb_handle;
	/*buffer is transing from isp_port_out_of_queue to isp_scaler_port out_of_queue*/
	dst_frame = (struct cam_frame *)pframe->common.pframe_data;
	pframe->common.pframe_data = NULL;
	dst_frame->common.is_reserved = 0;
	dst_frame->common.priv_data = port;

	if (pipeline->debug_log_switch)
		pr_info("pipeline_type %s, fid %d, ch_id %d, buf %x, w %d, h %d, pframe->common.is_reserved %d, compress_en %d\n",
			pipeline->pipeline_graph->name, pframe->common.fid,
			pframe->common.channel_id, pframe->common.buf.mfd,
			pframe->common.width, pframe->common.height,
			pframe->common.is_reserved, pframe->common.is_compressed);

	CAM_QUEUE_FOR_EACH_ENTRY(port, &node->port_queue.head, list) {
		if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) > 0)
			ret = CAM_QUEUE_ENQUEUE(&port->out_buf_queue, &dst_frame->list);
		if (ret) {
			pr_err("fail to enqueue output buffer, path %d.\n", port->port_id);
			return ret;
		}
	}

	CAM_QUEUE_ENQUEUE(&node->in_queue, &pframe->list);
	if (ret) {
		pr_err("fail to enqueue scaler node in_queue.\n");
		return ret;
	}

	if (atomic_read(&node->user_cnt) > 0)
		complete(&node->thread.thread_com);
	return 0;
}

void *isp_yuv_scaler_node_get(uint32_t node_id, struct isp_yuv_scaler_node_desc *param)
{
	int ret = 0;
	struct isp_yuv_scaler_node *node = NULL;
	struct cam_thread_info *thrd = NULL;
	struct isp_cfg_ctx_desc *cfg_desc = NULL;

	if (!param) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	if (*param->node_dev == NULL) {
		node = cam_buf_kernel_sys_vzalloc(sizeof(struct isp_yuv_scaler_node));
		if (!node) {
			pr_err("fail to get valid isp yuv scaler node\n");
			return NULL;
		}
	} else {
		node = *param->node_dev;
		pr_info("isp yuv scaler node has been alloc %p\n", node);
		goto exit;
	}

	node->dev = (struct isp_pipe_dev *)param->dev;
	cfg_desc = (struct isp_cfg_ctx_desc *)node->dev->cfg_handle;
	ret = cfg_desc->ops->ctx_get(cfg_desc);
	if (ret < 0) {
		pr_err("fail to get cfg ctx\n");
		cam_buf_kernel_sys_vfree(node);
		return NULL;
	}
	node->cfg_id = ret;
	cfg_desc->ops->ctx_reset(cfg_desc, node->cfg_id);
	node->dev->isp_hw->isp_ioctl(node->dev->isp_hw, ISP_HW_CFG_DEFAULT_PARA_CFG, &node->cfg_id);

	if (node->data_cb_func == NULL) {
		node->data_cb_func = param->data_cb_func;
		node->data_cb_handle = param->data_cb_handle;
	}

	mutex_init(&node->blkpm_lock);
	init_completion(&node->frm_done);
	complete(&node->frm_done);

	node->started = 0;
	node->ch_id = param->ch_id;
	node->attach_cam_id = param->cam_id;
	node->node_id = node_id;
	node->node_type = param->node_type;
	node->is_bind = 0;
	node->uinfo.uframe_sync = param->uframe_sync;
	node->buf_manager_handle = param->buf_manager_handle;

	CAM_QUEUE_INIT(&node->in_queue, ISP_SCALER_IN_Q_LEN, ispscaler_node_src_frame_ret);
	CAM_QUEUE_INIT(&node->proc_queue, ISP_SCALER_PROC_Q_LEN, ispscaler_node_src_frame_ret);
	CAM_QUEUE_INIT(&node->port_queue, PORT_ISP_MAX, NULL);

	thrd = &node->thread;
	thrd->data_cb_func = node->data_cb_func;
	thrd->data_cb_handle = node->data_cb_handle;
	thrd->buf_manager_handle = node->buf_manager_handle;
	thrd->error_type = CAM_CB_ISP_DEV_ERR;
	snprintf(thrd->thread_name, ARRAY_SIZE(thrd->thread_name), "isp scaler node %d_offline", node->node_id);
	ret = camthread_create(node, thrd, ispscaler_node_start_proc);
	if (ret) {
		pr_err("fail to get isp scaler node proc thread.\n");
		cam_buf_kernel_sys_vfree(node);
		return NULL;
	}

	pr_info("isp scaler node cfg_id %d node %p, node_id %d\n", node->cfg_id, node, node_id);
	*param->node_dev = node;

exit:
	atomic_inc(&node->user_cnt);
	return node;
}

void isp_yuv_scaler_node_put(struct isp_yuv_scaler_node *node)
{
	int ret = 0;

	if (!node) {
		pr_err("fail to get invalid node ptr\n");
		return;
	}

	if (node->started == 1) {
		pr_info("free context %d without users.\n", node->node_id);
		/* wait for last frame done */
		ret = wait_for_completion_interruptible_timeout(
			&node->frm_done, ISP_CONTEXT_TIMEOUT);
		if (ret == -ERESTARTSYS)
			pr_err("fail to interrupt, when isp wait\n");
		else if (ret == 0)
			pr_err("fail to wait ctx %d, timeout.\n", node->node_id);
		else
			pr_info("wait time %d\n", ret);

		node->started = 0;
		ispscaler_node_context_unbind(node);
		CAM_QUEUE_CLEAN(&node->in_queue, struct cam_frame, list);
		CAM_QUEUE_CLEAN(&node->proc_queue, struct cam_frame, list);
		CAM_QUEUE_CLEAN(&node->port_queue, struct isp_scaler_port, list);
	}

	if (atomic_dec_return(&node->user_cnt) == 0) {
		struct isp_cfg_ctx_desc *cfg_desc = node->dev->cfg_handle;

		mutex_destroy(&node->blkpm_lock);
		camthread_stop(&node->thread);
		if (node->slice_ctx)
			isp_slice_ctx_put(&node->slice_ctx);
		cfg_desc->ops->ctx_put(cfg_desc, node->cfg_id);

		pr_info("isp yuv scaler node %d put success\n", node->node_id);
		cam_buf_kernel_sys_vfree(node);
		node = NULL;
	}
}
