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

#include <linux/miscdevice.h>
#include "cam_test.h"
#include "cam_trusty.h"
#include "cam_zoom.h"
#include "flash_interface.h"
#include "isp_drv.h"
#include "isp_hw_adpt.h"
#include "isp_ltm.h"
#include "isp_pyr_rec.h"
#include "sprd_camsys_domain.h"
#include "sprd_sensor_drv.h"
#include "cam_buf_manager.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_CORE: %d %d %s : " fmt, current->pid, __LINE__, __func__

spinlock_t g_reg_wr_lock;
struct cam_global_ctrl g_camctrl = {
	ZOOM_BINNING2,
	0,
	DCAM_SCALE_DOWN_MAX * 10,
	0,
	ISP_MAX_LINE_WIDTH
};
/* TBD: need move to cam_ability.c later */
uint32_t camcore_dcampath_id_convert(uint32_t path_k_id)
{
	uint32_t path_u_id = DCAM_PATH_ID_MAX - 1;

	switch(path_k_id) {
	case DCAM_PATH_RAW:
		path_u_id = DCAM_RAW_OUT_PATH;
		break;
	case DCAM_PATH_FULL:
		path_u_id = DCAM_FULL_OUT_PATH;
		break;
	default :
		pr_err("fail to support path_k_id %d\n", path_k_id);
		break;
	}

	return path_u_id;
}

static int camcore_dcam_online_buf_cfg(struct channel_context *ch,
		struct cam_frame *pframe, struct camera_module *module)
{
	int ret = 0;

	if (!ch || !pframe || !module) {
		pr_err("fail to get valid ptr %p %p\n", ch, pframe);
		return -EFAULT;
	}

	CAM_QUEUE_FRAME_FLAG_RESET(&pframe->common);
	ret = CAM_PIPEINE_DCAM_ONLINE_OUT_PORT_CFG(ch, ch->dcam_port_id, CAM_PIPELINE_CFG_BUF, pframe);
	return ret;
}

static int camcore_capture_3dnr_set(struct camera_module *module,
		struct channel_context *ch)
{
	uint32_t mode_3dnr = 0;
	uint32_t node_id = 0;

	if ((!module) || (!ch))
		return -EFAULT;
	if (ch->ch_id == CAM_CH_CAP) {
		mode_3dnr = ch->uinfo_3dnr ? MODE_3DNR_CAP : MODE_3DNR_OFF;
		node_id = ISP_NODE_MODE_CAP_ID;
	} else {
		mode_3dnr = ch->uinfo_3dnr ? MODE_3DNR_PRE : MODE_3DNR_OFF;
		node_id = ISP_NODE_MODE_PRE_ID;
	}
	pr_debug("mode %d\n", mode_3dnr);
	CAM_PIPEINE_ISP_NODE_CFG(ch, CAM_PIPELINE_CFG_3DNR_MODE, node_id, &mode_3dnr);

	return 0;
}

static int camcore_resframe_set(struct camera_module *module)
{
	int ret = 0;
	struct channel_context *ch = NULL;
	uint32_t i = 0, src_w = 0, src_h = 0;
	uint32_t max_size = 0, out_size = 0, in_size = 0, node_id = 0;
	struct cam_frame *pframe = NULL;
	struct dcam_compress_cal_para cal_fbc = {0};

	for (i = 0; i < CAM_CH_MAX; i++) {
		ch = &module->channel[i];
		if (ch->enable) {
			src_w = ch->ch_uinfo.src_size.w;
			src_h = ch->ch_uinfo.src_size.h;
			if (ch->ch_uinfo.dst_fmt != IMG_PIX_FMT_GREY)
				out_size = ch->ch_uinfo.dst_size.w *ch->ch_uinfo.dst_size.h * 3 / 2;
			else
				out_size = cal_sprd_pitch(ch->ch_uinfo.dst_size.w, ch->ch_uinfo.dcam_raw_fmt)
					* ch->ch_uinfo.dst_size.h;

			if (ch->compress_en) {
				cal_fbc.data_bits = cam_data_bits(ch->dcam_out_fmt);
				cal_fbc.fmt = ch->dcam_out_fmt;
				cal_fbc.height = src_h;
				cal_fbc.width = src_w;
				in_size = dcam_if_cal_compressed_size (&cal_fbc);
			} else if (ch->ch_uinfo.sn_fmt != IMG_PIX_FMT_GREY)
				in_size = src_w * src_h * 3 / 2;
			else if (cam_raw_fmt_get(ch->dcam_out_fmt))
				in_size = cal_sprd_size(src_w, src_h, ch->ch_uinfo.dcam_raw_fmt);
			else
				in_size = cal_sprd_size(src_w, src_h, ch->dcam_out_fmt);

			if (module->cam_uinfo.is_pyr_rec && ch->ch_id != CAM_CH_CAP)
				in_size += dcam_if_cal_pyramid_size(src_w, src_h, ch->ch_uinfo.pyr_out_fmt, 1, DCAM_PYR_DEC_LAYER_NUM);
			in_size = ALIGN(in_size, CAM_BUF_ALIGN_SIZE);

			max_size = max3(max_size, out_size, in_size);
			pr_debug("cam%d, ch %d, max_size = %d, %d, %d\n", module->idx, i, max_size, in_size, out_size);
		}
	}

	pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	if (!pframe) {
		pr_err("fail to get frame\n");
		ret = -EINVAL;
		return ret;
	}
	pframe->common.is_reserved = CAM_RESERVED_BUFFER_ORI;
	pframe->common.buf.type = CAM_BUF_USER;
	pframe->common.buf.mfd = module->reserved_buf_fd;
	pframe->common.buf.status = CAM_BUF_ALLOC;
	pframe->common.user_fid = CAMERA_RESERVE_FRAME_NUM;
	ret = cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_MOVE_TO_ION, CAM_BUF_IOMMUDEV_MAX);
	pframe->common.buf.size = max(max_size, module->reserved_size);
	ret |= cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_GET_SINGLE_PAGE_IOVA, CAM_BUF_IOMMUDEV_DCAM);
	ret |= cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_GET_SINGLE_PAGE_IOVA, CAM_BUF_IOMMUDEV_ISP);
	if (ret) {
		pr_err("fail to get ionbuf on cam%d\n", module->idx);
		cam_queue_empty_frame_put(pframe);
		ret = -EFAULT;
		return ret;
	}
	pframe->common.buf.bypass_iova_ops = CAM_ENABLE;
	for (i = 0; i < CAM_CH_MAX; i++) {
		ch = &module->channel[i];
		if (!ch->enable || ch->ch_id == CAM_CH_VIRTUAL)
			continue;
		if (ch->isp_port_id >= 0 && ch->ch_uinfo.dst_fmt != IMG_PIX_FMT_GREY) {
			pframe->common.channel_id = ch->ch_id;
			node_id = (ch->ch_id == CAM_CH_CAP && ch->isp_port_id != PORT_VID_OUT) ? ISP_NODE_MODE_CAP_ID : ISP_NODE_MODE_PRE_ID;
			ret = CAM_PIPEINE_ISP_OUT_PORT_CFG(ch, ch->isp_port_id, CAM_PIPILINE_CFG_RESEVER_BUF, node_id, pframe);
			if (ret) {
				pr_err("fail to set output buffer for ch%d.\n", ch->ch_id);
				cam_queue_empty_frame_put(pframe);
				ret = -EFAULT;
				break;
			}
		}
	}
	module->res_frame = pframe;

	return ret;
}

static void camcore_compression_cal(struct camera_module *module)
{
	struct cam_hw_info *hw = NULL;
	uint32_t nr3_compress_eb = 0;
	struct channel_context *ch_pre = NULL, *ch_cap = NULL, *ch_vid = NULL;

	hw = module->grp->hw_info;
	ch_pre = &module->channel[CAM_CH_PRE];
	ch_cap = &module->channel[CAM_CH_CAP];
	ch_vid = &module->channel[CAM_CH_VID];

	/* Enable compression for DCAM path by default. Full path is prior to bin path. */
	ch_cap->compress_en = ch_cap->enable
		&& ch_cap->ch_uinfo.sn_fmt == IMG_PIX_FMT_GREY
		&& !ch_cap->ch_uinfo.is_high_fps
		&& !module->cam_uinfo.is_4in1
		&& hw->ip_dcam[0]->dcampath_abt[DCAM_FULL_OUT_PATH]->fbc_sup
		&& !(g_dbg_fbc_control & DEBUG_FBC_CRL_FULL);
	ch_pre->compress_en = ch_pre->enable
		&& ch_pre->ch_uinfo.sn_fmt == IMG_PIX_FMT_GREY
		&& !ch_pre->ch_uinfo.is_high_fps
		&& !module->cam_uinfo.is_4in1
		&& module->cam_uinfo.alg_type != ALG_TYPE_VID_NR
		&& hw->ip_dcam[0]->dcampath_abt[DCAM_BIN_OUT_PATH]->fbc_sup
		&& !(g_dbg_fbc_control & DEBUG_FBC_CRL_BIN);

	ch_cap->compress_offline = hw->ip_dcam[0]->dcamhw_abt->dcam_offline_fbc_support;
	ch_vid->compress_en = ch_pre->compress_en;

	/* Disable compression for 3DNR by default */
	nr3_compress_eb = hw->ip_isp->isphw_abt->nr3_compress_support;
	if (ch_cap->uinfo_3dnr)
		ch_cap->compress_3dnr = nr3_compress_eb;
	if (ch_pre->uinfo_3dnr) {
		ch_pre->compress_3dnr = nr3_compress_eb;
		ch_vid->compress_3dnr = ch_pre->compress_3dnr;
	}

	/* dcam not support fbc when dcam need fetch */
	if (module->cam_uinfo.dcam_slice_mode ||
		module->cam_uinfo.is_4in1 || module->cam_uinfo.is_raw_alg) {
		ch_cap->compress_en = 0;
	}

	pr_info("cam%d: cap %u, pre %u, vid %u, offline %u, 3dnr %u %u %u.\n",
		module->idx, ch_cap->compress_en, ch_pre->compress_en, ch_vid->compress_en, ch_cap->compress_offline, ch_cap->compress_3dnr, ch_pre->compress_3dnr, ch_vid->compress_3dnr);
}

static int camcore_buffer_channel_config(
	struct camera_module *module, struct channel_context *channel)
{
	struct cam_frame *alloc_buf = NULL;

	if (!module || !channel) {
		pr_err("fail to get valid param %p %p\n", module, channel);
		return -EFAULT;
	}

	if ((channel->swap_size.w > 0) && (channel->ch_id != CAM_CH_RAW)) {
		/* alloc middle buffer for channel */
		mutex_lock(&module->buf_lock[channel->ch_id]);
		channel->alloc_start = 1;
		mutex_unlock(&module->buf_lock[channel->ch_id]);

		alloc_buf = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		if (!alloc_buf) {
			pr_err("fail to get frame\n");
			return -EINVAL;
		}
		alloc_buf->common.priv_data = (void *)channel;
		CAM_QUEUE_ENQUEUE(&module->alloc_queue, &alloc_buf->list);
		complete(&module->buf_thrd.thread_com);
	}

	return 0;
}

static int camcore_buffer_path_cfg(struct camera_module *module, uint32_t index)
{
	int ret = 0;
	struct channel_context *ch = NULL;
	struct cam_hw_info *hw = NULL;

	if (!module) {
		pr_err("fail to get input ptr\n");
		return -EFAULT;
	}

	ch = &module->channel[index];
	hw = module->grp->hw_info;

	if (index == CAM_CH_PRE || index == CAM_CH_VID
		|| (index == CAM_CH_CAP && !module->channel[CAM_CH_PRE].enable && !module->cam_uinfo.is_4in1)) {
		ret = wait_for_completion_interruptible(&ch->stream_on_buf_com);
		if (ret != 0) {
			pr_err("fail to config channel/path param work %d\n", ret);
			goto exit;
		}
	}

	if (atomic_read(&ch->err_status) != 0) {
		pr_err("fail to get ch %d correct status\n", ch->ch_id);
		ret = -EFAULT;
		goto exit;
	}

exit:
	return ret;
}

static int camcore_faceid_secbuf(uint32_t sec, struct camera_buf *buf, struct cam_hw_info *hw)
{
	int ret = 0;

	if (hw == NULL)
		return -EFAULT;

	if (sec) {
		buf->buf_sec = CAM_ENABLE;
		buf->reserved_iommu_map_support = hw->ip_isp->isphw_abt->reserved_iommu_map_support;

		if (hw->ip_isp->isphw_abt->reserved_iommu_map_support == CAM_DISABLE)
			ret = hw->isp_ioctl(hw, ISP_HW_CFG_MMU_FACEID_RECFG, NULL);
		if (unlikely(ret)) {
			pr_err("fail to enable vaor bypass mode, ret %d\n", ret);
			ret = -EFAULT;
		}
	}

	return ret;
}

static inline int camcore_mulsharebuf_verif(struct channel_context *ch, struct camera_uinfo *info)
{
	return ch->ch_id == CAM_CH_CAP && info->need_share_buf && !info->dcam_slice_mode && !info->is_4in1;
}

static int camcore_buffers_alloc(void *param)
{
	int ret = 0;
	uint32_t dcamonline_buf_num = 0, dcamoffline_buf_num = 0;
	uint32_t width = 0, height = 0;
	struct camera_module *module = NULL;
	struct channel_context *channel = NULL;
	struct channel_context *channel_vid = NULL;
	struct channel_context *channel_pre = NULL;
	struct cam_hw_info *hw = NULL;
	struct cam_frame *alloc_buf = NULL;
	struct camera_group *grp = NULL;
	struct cam_buf_alloc_desc alloc_param = {0};

	pr_info("enter.\n");
	module = (struct camera_module *)param;
	grp = module->grp;
	hw = module->grp->hw_info;
	channel_vid = &module->channel[CAM_CH_VID];
	channel_pre = &module->channel[CAM_CH_PRE];

	alloc_buf = CAM_QUEUE_DEQUEUE(&module->alloc_queue, struct cam_frame, list);

	if (alloc_buf) {
		channel = (struct channel_context *)alloc_buf->common.priv_data;
		cam_queue_empty_frame_put(alloc_buf);
	} else {
		pr_err("fail to dequeue alloc_buf\n");
		return -1;
	}

	if (camcore_mulsharebuf_verif(channel, &module->cam_uinfo)) {
		width = grp->mul_sn_max_size.w;
		height = grp->mul_sn_max_size.h;
		grp->is_mul_buf_share = 1;
	} else if (module->cam_uinfo.alg_type == ALG_TYPE_VID_NR) {
		width = channel->ch_uinfo.dcam_out_size.w;
		height = channel->ch_uinfo.dcam_out_size.h;
	} else {
		width = channel->swap_size.w;
		height = channel->swap_size.h;
	}

	dcamonline_buf_num = cam_scene_dcamonline_buffers_alloc_num(channel, module, channel->pipeline_handle->pipeline_graph->type);
	dcamoffline_buf_num = cam_scene_dcamoffline_buffers_alloc_num(channel, module);
	alloc_param.ch_id = channel->ch_id;
	alloc_param.cam_idx = module->idx;
	alloc_param.width = width;
	alloc_param.height = height;
	alloc_param.is_pyr_rec = module->cam_uinfo.is_pyr_rec;
	alloc_param.is_pyr_dec = module->cam_uinfo.is_pyr_dec;
	alloc_param.is_static_map = hw->ip_isp->isphw_abt->static_map_support;
	alloc_param.pyr_out_fmt = channel->ch_uinfo.pyr_out_fmt;
	alloc_param.pyr_layer_num = channel->pipeline_handle->pipeline_graph->pyr_layer_num;
	alloc_param.iommu_enable = module->iommu_enable;
	alloc_param.compress_en = channel->compress_en;
	alloc_param.compress_offline = channel->compress_offline;
	alloc_param.share_buffer = camcore_mulsharebuf_verif(channel, &module->cam_uinfo);
	alloc_param.is_super_size = (module->cam_uinfo.dcam_slice_mode == CAM_OFFLINE_SLICE_HW && width >= DCAM_HW_SLICE_WIDTH_MAX) ? 1 : 0;
	alloc_param.dst_size = channel->ch_uinfo.dst_size;
	alloc_param.ch_vid_enable = channel_vid->enable;
	alloc_param.chvid_dst_size = channel_vid->ch_uinfo.dst_size;
	alloc_param.dcamonline_buf_alloc_num = dcamonline_buf_num;
	alloc_param.dcamoffline_buf_alloc_num = dcamoffline_buf_num;
	alloc_param.stream_on_buf_com = &channel->stream_on_buf_com;
	alloc_param.alloc_stop_signal = &channel->alloc_stop_signal;
	alloc_param.not_to_isp = ((module->cam_uinfo.dcam_slice_mode ||
		module->cam_uinfo.is_4in1) && !module->cam_uinfo.virtualsensor);
	alloc_param.alg_type = module->cam_uinfo.alg_type;

	if (module->offline_icap_scene && channel->ch_id == CAM_CH_CAP) {
		alloc_param.cam_copy_buf_alloc_num = 3;
		alloc_param.dcamoffline_lsc_buf_alloc_num = 1;
	}
	if (channel->ch_id == CAM_CH_PRE || channel->ch_id == CAM_CH_VID) {
		if (channel->ch_uinfo.is_high_fps)
			alloc_param.stream_on_need_buf_num = channel->ch_uinfo.high_fps_skip_num;
		else
			alloc_param.stream_on_need_buf_num = 1;
	} else
		alloc_param.stream_on_need_buf_num = dcamonline_buf_num;
	alloc_param.nr3_enable = (channel->type_3dnr == CAM_3DNR_HW) && (!((channel->uinfo_3dnr == 0) && (channel->ch_id == CAM_CH_PRE))) ? 1 : 0;
	alloc_param.compress_3dnr = channel->compress_3dnr;
	alloc_param.dcam_out_fmt = channel->dcam_out_fmt;
	alloc_param.ltm_mode = channel->mode_ltm;
	alloc_param.ltm_rgb_enable = channel->ltm_rgb;
	/*non-zsl capture, or video path while preview path enable, need not use buffer*/
	if (channel->ch_id == CAM_CH_VID && channel_pre->enable)
		alloc_param.ltm_mode = MODE_LTM_OFF;

	ret = cam_pipeline_buffer_alloc(channel->pipeline_handle, &alloc_param);
	if (ret) {
		pr_err("fail to alloc buffer for cam%d channel%d\n", module->idx, channel->ch_id);
		atomic_inc(&channel->err_status);
	}
	if (channel->nonzsl_pre_pipeline) {
		uint32_t ratio = 0;

		ratio  = channel->ch_uinfo.nonzsl_pre_ratio;
		alloc_param.ch_id = 1;
		alloc_param.width = channel->ch_uinfo.src_crop.w / ratio;
		alloc_param.height = channel->ch_uinfo.src_crop.h / ratio;
		if (!module->cam_uinfo.virtualsensor) {
			alloc_param.dcamonline_buf_alloc_num = 0;
			alloc_param.dcamoffline_buf_alloc_num = 1;
		} else {
			alloc_param.dcamonline_buf_alloc_num = 1;
			alloc_param.dcamoffline_buf_alloc_num = 0;
		}
		ret = cam_pipeline_buffer_alloc(channel->nonzsl_pre_pipeline, &alloc_param);
		if (ret) {
			pr_err("fail to alloc buffer for cam%d channel_prev\n", module->idx);
			atomic_inc(&channel->err_status);
		}
	}

	if (channel->ch_id != CAM_CH_PRE && module->channel[CAM_CH_PRE].enable) {
		ret = camcore_buffer_path_cfg(module, channel->ch_id);
		if (ret)
			pr_err("fail to cfg path buffer\n");
	}
	complete(&channel->alloc_com);
	channel->alloc_start = 0;
	pr_info("ch %d done. status %d\n", channel->ch_id, atomic_read(&channel->err_status));
	ret = cam_buf_mdbg_check();
	return ret;
}

static int camcore_dual_same_frame_get(struct camera_module *module, struct cam_frame *mframe)
{
	int i = 0, ret = 0;
	int64_t t_sec = 0, t_usec = 0;
	struct camera_group *grp = NULL;
	struct camera_queue *q = NULL;
	struct cam_frame *pframe = NULL;
	struct camera_module *slave_module = NULL;
	struct channel_context *ch = NULL;

	grp = module->grp;
	if (!grp)
		return -EFAULT;
	/* get the slave module */
	for (i = 0; i < CAM_COUNT; i++) {
		if (!grp->module[i])
			continue;
		if (grp->module[i] != module) {
			slave_module = grp->module[i];
			break;
		}
	}

	if (!slave_module  || !slave_module->cam_uinfo.is_dual)
		return -EFAULT;

	if (atomic_read(&slave_module->state) != CAM_RUNNING) {
		pr_info("slave sensor no running , current state: %d\n", atomic_read(&slave_module->state));
		return ret;
	}
	ch = &slave_module->channel[CAM_CH_CAP];
	ret = CAM_PIPEINE_FRAME_CACHE_NODE_CFG(ch, CAM_PIPELINE_CFG_DUAL_SYNC_BUF_GET, FRAME_CACHE_CAP_NODE_ID, &q);
	if (ret) {
		pr_err("fail to get buffer\n");
		return -EFAULT;
	}

	t_sec = mframe->common.sensor_time.tv_sec;
	t_usec = mframe->common.sensor_time.tv_usec;
	ret = cam_queue_same_frame_get(q, &pframe, t_sec, t_usec);
	if (ret) {
		slave_module->dual_frame = NULL;
		atomic_set(&slave_module->dual_select_frame_done, 1);
		atomic_set(&module->dual_select_frame_done, 1);
		return ret;
	}
	slave_module->dual_frame = pframe;
	atomic_set(&slave_module->dual_select_frame_done, 1);
	atomic_set(&module->dual_select_frame_done, 1);

	return 0;
}

static int camcore_dual_slave_frame_set(void *param, void *priv_data)
{
	struct cam_frame *pframe = NULL;
	struct camera_module *module = NULL;
	struct camera_group *grp = NULL;
	int ret = CAM_FRAME_NO_DEAL;
	unsigned long flag = 0;

	pframe = (struct cam_frame *)param;
	module = (struct camera_module *)priv_data;
	grp = module->grp;
	if (!grp)
		return -EFAULT;

	spin_lock_irqsave(&grp->dual_deal_lock, flag);
	if (module->master_flag == 0 && atomic_read(&module->dual_select_frame_done) == 1 &&
		module->dual_frame == NULL && module->capture_type == CAM_CAPTURE_STOP) {
		module->dual_frame = pframe;
		ret = CAM_FRAME_DEAL;
	}
	spin_unlock_irqrestore(&grp->dual_deal_lock, flag);

	return ret;
}

static struct cam_frame *camcore_dual_frame_deal(void *param, void *priv_data, int *frame_flag)
{
	struct channel_context *channel = NULL;
	struct cam_frame *pframe = NULL;
	struct camera_module *module = NULL;
	struct camera_group *grp = NULL;
	int ret = 0;
	unsigned long flag = 0;

	if (!param || !priv_data) {
		pr_err("fail to get valid ptr %p\n", param);
		return NULL;
	}

	pframe = (struct cam_frame *)param;
	module = (struct camera_module *)priv_data;
	channel = &module->channel[pframe->common.channel_id];
	grp = module->grp;
	if (!grp)
		return NULL;

	spin_lock_irqsave(&grp->dual_deal_lock, flag);
	if (module->master_flag == 0) {
		if (atomic_read(&module->dual_select_frame_done) == 0)
			*frame_flag = CAM_FRAME_NO_DEAL;
		else {
			if (module->dual_frame) {
				camcore_dcam_online_buf_cfg(channel, pframe, module);
				pframe = module->dual_frame;
				module->dual_frame = NULL;
			}
			atomic_set(&module->dual_select_frame_done, 0);
			*frame_flag = CAM_FRAME_DEAL;
		}
	} else {
		if ((module->cap_frame_id && pframe->common.fid < module->cap_frame_id)
			|| (!module->cap_frame_id && pframe->common.boot_sensor_time < module->capture_times))
			*frame_flag = CAM_FRAME_NO_DEAL;
		else {
			if (atomic_read(&module->dual_select_frame_done) == 0) {
				ret = camcore_dual_same_frame_get(module , pframe);
				if (ret)
					pr_debug("No find match frame, adapt next slave frame\n");
			}
			*frame_flag = CAM_FRAME_DEAL;
		}
	}
	spin_unlock_irqrestore(&grp->dual_deal_lock, flag);

	return pframe;
}

int camcore_pre_frame_status(uint32_t param, void *priv_data)
{
	struct camera_module *module = NULL;
	struct channel_context *ch_cap = NULL;
	int ret = 0;

	if (!priv_data) {
		pr_err("fail to get valid ptr\n");
		return -EFAULT;
	}

	module = (struct camera_module *)priv_data;
	ch_cap = &module->channel[CAM_CH_CAP];

	ret =  CAM_PIPEINE_FRAME_CACHE_NODE_CFG(ch_cap, CAM_PIPELINE_CFG_PRE_FRAME_STATUS,
		FRAME_CACHE_CAP_NODE_ID, &param);
	if(ret)
		pr_err("fail to update cap status\n");

	return ret;
}

int camcore_param_start_end(struct camera_module *module, uint32_t scene_id)
{
	int ret = 0;
	uint32_t frame_id = 0;
	struct channel_context *channel = NULL;
	struct cam_pipeline_cfg_param pipe_param = {0};

	if (atomic_read(&module->state) != CAM_CFG_CH &&
		atomic_read(&module->state) != CAM_STREAM_ON &&
		atomic_read(&module->state) != CAM_RUNNING)
		return ret;

	frame_id = module->cur_frame_id + 1;
	pr_debug("cam%d fid %d, scene %d\n", module->idx, frame_id, scene_id);
	if (scene_id == PM_SCENE_OFFLINE_BPC &&
		(atomic_read(&module->lsc_frame_id) != frame_id)) {
		atomic_set(&module->lsc_frame_id, frame_id);
		channel = &module->channel[CAM_CH_CAP];
		pipe_param.node_type = CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW;
		pipe_param.node_param.param = &frame_id;
		pipe_param.node_id = DCAM_OFFLINE_NODE_ID;
		ret = channel->pipeline_handle->ops.cfg_param(channel->pipeline_handle,
				CAM_PIPELINE_CFG_PARAM_FID, &pipe_param);
		if (ret)
			pr_err("param sync error\n");
	}
	if (scene_id == PM_SCENE_OFFLINE_CAP &&
		(atomic_read(&module->dcam_frame_id) != frame_id)) {
		atomic_set(&module->dcam_frame_id, frame_id);
		channel = &module->channel[CAM_CH_CAP];
		pipe_param.node_type = CAM_NODE_TYPE_DCAM_OFFLINE;
		pipe_param.node_param.param = &frame_id;
		pipe_param.node_id = DCAM_OFFLINE_NODE_ID;
		ret = channel->pipeline_handle->ops.cfg_param(channel->pipeline_handle,
				CAM_PIPELINE_CFG_PARAM_FID, &pipe_param);
		if (ret)
			pr_err("param sync error\n");
	}

	return ret;
}

static int camcore_cap_frame_status(uint32_t param, void *priv_data)
{
	struct camera_module *module = NULL;
	struct channel_context *ch_pre = NULL;
	int ret = 0;

	if (!priv_data) {
		pr_err("fail to get valid ptr\n");
		return -EFAULT;
	}

	module = (struct camera_module *)priv_data;
	ch_pre = &module->channel[CAM_CH_PRE];

	ret = CAM_PIPEINE_DATA_COPY_NODE_CFG(ch_pre, CAM_PIPELINE_CFG_PRE_RAW_FLAG, &param);
	if(ret)
		pr_err("fail to update pre status\n");

	return ret;
}

static uint32_t camcore_frame_start_proc(struct cam_frame *pframe,
		enum cam_node_type node_type, struct channel_context *ch)
{
	int ret = 0;
	struct cam_pipeline_cfg_param param = {0};

	param.node_type = node_type;
	param.node_param.param = pframe;
	param.node_param.port_id = ch->dcam_port_id;
	ret = ch->pipeline_handle->ops.streamon(ch->pipeline_handle, &param);

	return ret;
}

static int camcore_zoom_param_get_callback(uint32_t pipeline_type, void *priv_data, void *param)
{
	int i = 0;
	unsigned long flag = 0;
	struct camera_module *module = NULL;
	struct channel_context *channel = NULL;
	struct cam_zoom_frame *zoom_param = NULL;
	struct cam_frame *zoom_frame = NULL;

	if (!priv_data || !param) {
		pr_err("fail to get valid param, priv_data:%p, param:%p\n", priv_data, param);
		return -1;
	}

	module = (struct camera_module *)priv_data;
	zoom_param = (struct cam_zoom_frame *)param;
	for (i = 0; i < CAM_CH_MAX; i++) {
		channel = &module->channel[i];
		if (!channel->enable || !channel->pipeline_handle)
			continue;
		if (channel->pipeline_type == pipeline_type) {
			pr_debug("ch %d type %d\n", channel->ch_id, pipeline_type);
			spin_lock_irqsave(&channel->lastest_zoom_lock, flag);
			zoom_frame = CAM_QUEUE_DEQUEUE(&channel->zoom_param_q, struct cam_frame, list);
			spin_unlock_irqrestore(&channel->lastest_zoom_lock, flag);
			if (zoom_frame) {
				memcpy(zoom_param, &zoom_frame->node_zoom, sizeof(struct cam_zoom_frame));
				cam_queue_empty_frame_put(zoom_frame);
			} else {
				spin_lock_irqsave(&channel->lastest_zoom_lock, flag);
				memcpy(zoom_param, &channel->latest_zoom_param, sizeof(struct cam_zoom_frame));
				spin_unlock_irqrestore(&channel->lastest_zoom_lock, flag);
			}
			break;
		}
		if (channel->nonzsl_pre_pipeline &&
			(pipeline_type == CAM_PIPELINE_ONLINERAW_2_OFFLINEPREVIEW ||
			(pipeline_type == CAM_PIPELINE_PREVIEW &&
			module->cam_uinfo.virtualsensor))) {
			pr_debug("ch %d type %d\n", channel->ch_id, pipeline_type);
			spin_lock_irqsave(&channel->nonzsl_pre.lastest_zoom_lock, flag);
			zoom_frame = CAM_QUEUE_DEQUEUE(&channel->nonzsl_pre.zoom_param_q, struct cam_frame, list);
			spin_unlock_irqrestore(&channel->nonzsl_pre.lastest_zoom_lock, flag);
			if (zoom_frame) {
				memcpy(zoom_param, &zoom_frame->node_zoom, sizeof(struct cam_zoom_frame));
				cam_queue_empty_frame_put(zoom_frame);
			} else {
				spin_lock_irqsave(&channel->nonzsl_pre.lastest_zoom_lock, flag);
				memcpy(zoom_param, &channel->nonzsl_pre.latest_zoom_param, sizeof(struct cam_zoom_frame));
				spin_unlock_irqrestore(&channel->nonzsl_pre.lastest_zoom_lock, flag);
			}
			break;
		}
	}

	return 0;
}

static int camcore_postproc_zoom_param_get(struct camera_module *module,
	enum cam_pipeline_type pipeline_type, struct cam_zoom_base *zoom_param,
	struct cam_zoom_frame *zoom_data, struct cam_zoom_index *zoom_index)
{
	uint32_t ret = 0;

	if (!module || !zoom_param || !zoom_data || !zoom_index) {
		pr_err("fail to get input handle module:%p, zoom_data:%p, zoom_index:%p, zoom_param:%p.\n",
			module, zoom_data, zoom_index, zoom_param);
		return -EFAULT;
	}

	ret = camcore_zoom_param_get_callback(pipeline_type, module, zoom_data);
	if (ret)
		pr_warn("Warning: Not get postproc zoom data info.\n");

	ret = cam_zoom_port_param_update(zoom_data, zoom_index, zoom_param);

	return ret;
}

/* img0|img1; width_img0 = width_img1, height_img0 = height_img2
 *  img2|img3;
 *  in each subimg, tile_num_x = tile_num_y = 8; overlap_x = tile_x; overlap_y = tile_y;
 *  width_img0 = tile_num_x * tile_x = (tile_num_x -1) *
 *  tile_x + overlap_x = 7 * tile_x + overlap_x;
 *  whole_img_width = 2 * width_img0 - 2 * overlap_x = 14 * tile_x;
 *  whole_img_height = 2 * height_img0 - 2 * overlap_y = 14 * tile_y;
 *  tile_x align 4, tile_y align 2
 *  tile_x = ceil(width / 14 / 4) * 4 = ceil(width / 56) * 4; tile_y =
 *  ceil(height / 14 / 2) * 2 = ceil(height / 28) * 2;
 */
void camcore_oversize_img_sliceproc(struct camera_module *module,
		struct cam_frame *pframe, uint32_t slice_no)
{
	int ret = 0;
	uint32_t pipeline_type = 0;
	uint32_t width = 0, height = 0, ltm_subimg_idx = 0;
	uint32_t tile_x_align_coeff = 1, tile_y_align_coeff = 1;
	uint32_t frm_trim_offset_x = 0, frm_trim_offset_y = 0;
	uint32_t bin_width_aligned = 0, bin_height_aligned = 0;
	uint32_t ratio = 2, w_align_cef = 1, h_align_cef = 1, width_aligned = 0, height_aligned = 0;
	uint32_t tile_x = 0, tile_y = 0, img0_size_x = 0;
	uint32_t  img0_size_y = 0, img1_start_x = 0, img2_start_y = 0;
	struct channel_context *channel = NULL;
	struct cam_zoom_index zoom_index = {0};
	struct cam_zoom_base zoom_param = {0};
	struct img_trim sub_img_trim[ISP_OVERSIZE_LTM_SUBIMG_NUM] = {0};
	struct img_trim sub_img_out_trim[ISP_OVERSIZE_LTM_SUBIMG_NUM] = {0};

	width = pframe->common.width;
	height = pframe->common.height;
	channel = &module->channel[CAM_CH_CAP];
	tile_x_align_coeff = ISP_LTM_TILE_W_ALIGNMENT * 14;
	tile_y_align_coeff = ISP_LTM_TILE_H_ALIGNMENT * 14;
	if (pframe->common.nonzsl_xtm.hist_eb == 0) {
		if (height > DCAM_SW_SLICE_HEIGHT_MAX)
			ratio = 4;
		w_align_cef = 2 * ratio * 14;
		h_align_cef = 2 * ratio * 14;
		width_aligned = (width / w_align_cef) * w_align_cef;
		height_aligned = (height / h_align_cef) * h_align_cef;
		tile_x = (width_aligned + tile_x_align_coeff - 1) /
			tile_x_align_coeff * ISP_LTM_TILE_W_ALIGNMENT;
		tile_y = (height_aligned + tile_y_align_coeff - 1) /
			tile_y_align_coeff * ISP_LTM_TILE_H_ALIGNMENT;
		img0_size_x = 8 * tile_x;
		img0_size_y = 8 * tile_y;
		frm_trim_offset_x = (width - width_aligned) >> 1;
		frm_trim_offset_y = (height - height_aligned) >> 1;
		img1_start_x = width_aligned - img0_size_x + frm_trim_offset_x;
		img2_start_y = height_aligned - img0_size_y + frm_trim_offset_y;
		pipeline_type = channel->pipeline_type;
		zoom_index.node_id = ISP_NODE_MODE_CAP_ID;
		zoom_index.port_id = PORT_CAP_OUT;
		zoom_param.dst.w = width_aligned;
		zoom_param.dst.h = height_aligned;
		zoom_param.crop.start_x = 0;
		zoom_param.crop.start_y = 0;
		zoom_param.crop.size_x = width_aligned;
		zoom_param.crop.size_y = height_aligned;
	} else {
		uint32_t full_size_x = 0, full_size_y = 0;

		full_size_x = pframe->common.nonzsl_xtm.full_size.w;
		full_size_y = pframe->common.nonzsl_xtm.full_size.h;
		if (full_size_x > DCAM_SW_SLICE_HEIGHT_MAX)
			ratio = 4;
		w_align_cef = 2 * ratio * 14;
		h_align_cef = 2 * ratio * 14;
		width_aligned = (full_size_x / w_align_cef) * w_align_cef;
		height_aligned = (full_size_y / h_align_cef) * h_align_cef;
		bin_width_aligned = width_aligned / ratio;
		bin_height_aligned = height_aligned / ratio;
		tile_x = (width_aligned + tile_x_align_coeff - 1) /
			tile_x_align_coeff * ISP_LTM_TILE_W_ALIGNMENT / ratio;
		tile_y = (height_aligned + tile_y_align_coeff - 1) /
			tile_y_align_coeff * ISP_LTM_TILE_H_ALIGNMENT / ratio;
		img0_size_x = 8 * tile_x;
		img0_size_y = 8 * tile_y;
		frm_trim_offset_x = (width - bin_width_aligned) >> 1;
		frm_trim_offset_y = (height - bin_height_aligned) >> 1;
		img1_start_x = bin_width_aligned - img0_size_x + frm_trim_offset_x;
		img2_start_y = bin_height_aligned - img0_size_y + frm_trim_offset_y;
		pipeline_type = channel->nonzsl_statis_pipeline_type;
		zoom_index.node_id = ISP_NODE_MODE_PRE_ID;
		zoom_index.port_id = PORT_PRE_OUT;
		zoom_param.dst.w = bin_width_aligned;
		zoom_param.dst.h = bin_height_aligned;
		zoom_param.crop.start_x = 0;
		zoom_param.crop.start_y = 0;
		zoom_param.crop.size_x = bin_width_aligned;
		zoom_param.crop.size_y = bin_height_aligned;
	}
	ltm_subimg_idx = slice_no;
	pr_debug("idx:%d, hist eb:%d, tile size %d %d, align size %d %d\n",
		ltm_subimg_idx, pframe->common.nonzsl_xtm.hist_eb,
		tile_x, tile_y, width_aligned, height_aligned);
	sub_img_trim[0].start_x = frm_trim_offset_x;
	sub_img_trim[0].start_y = frm_trim_offset_y;
	sub_img_trim[0].size_x = img0_size_x;
	sub_img_trim[0].size_y = img0_size_y;
	sub_img_trim[1].start_x = img1_start_x;
	sub_img_trim[1].start_y = frm_trim_offset_y;
	sub_img_trim[1].size_x = img0_size_x;
	sub_img_trim[1].size_y = img0_size_y;
	sub_img_trim[2].start_x = frm_trim_offset_x;
	sub_img_trim[2].start_y = img2_start_y;
	sub_img_trim[2].size_x = img0_size_x;
	sub_img_trim[2].size_y = img0_size_y;
	sub_img_trim[3].start_x = img1_start_x;
	sub_img_trim[3].start_y = img2_start_y;
	sub_img_trim[3].size_x = img0_size_x;
	sub_img_trim[3].size_y = img0_size_y;
	pr_debug("img_in_trim, (%d %d %d %d) (%d %d %d %d) (%d %d %d %d) (%d %d %d %d)\n",
		sub_img_trim[0].start_x, sub_img_trim[0].start_y,
		sub_img_trim[0].size_x, sub_img_trim[0].size_y,
		sub_img_trim[1].start_x, sub_img_trim[1].start_y,
		sub_img_trim[1].size_x, sub_img_trim[1].size_y,
		sub_img_trim[2].start_x, sub_img_trim[2].start_y,
		sub_img_trim[2].size_x, sub_img_trim[2].size_y,
		sub_img_trim[3].start_x, sub_img_trim[3].start_y,
		sub_img_trim[3].size_x, sub_img_trim[3].size_y);
	sub_img_out_trim[0].start_x = 0;
	sub_img_out_trim[0].start_y = 0;
	sub_img_out_trim[0].size_x = sub_img_trim[0].size_x - tile_x;
	sub_img_out_trim[0].size_y = sub_img_trim[0].size_y - tile_y;
	sub_img_out_trim[1].start_x = tile_x;
	sub_img_out_trim[1].start_y = 0;
	sub_img_out_trim[1].size_x = sub_img_trim[1].size_x - tile_x;
	sub_img_out_trim[1].size_y = sub_img_trim[1].size_y - tile_y;
	sub_img_out_trim[2].start_x = 0;
	sub_img_out_trim[2].start_y = tile_y;
	sub_img_out_trim[2].size_x = sub_img_trim[2].size_x - tile_x;
	sub_img_out_trim[2].size_y = sub_img_trim[2].size_y - tile_y;
	sub_img_out_trim[3].start_x = tile_x;
	sub_img_out_trim[3].start_y = tile_y;
	sub_img_out_trim[3].size_x = sub_img_trim[3].size_x - tile_x;
	sub_img_out_trim[3].size_y = sub_img_trim[3].size_y - tile_y;
	pr_debug("img_out_trim, (%d %d %d %d) (%d %d %d %d) (%d %d %d %d) (%d %d %d %d)\n",
		sub_img_out_trim[0].start_x, sub_img_out_trim[0].start_y,
		sub_img_out_trim[0].size_x, sub_img_out_trim[0].size_y,
		sub_img_out_trim[1].start_x, sub_img_out_trim[1].start_y,
		sub_img_out_trim[1].size_x, sub_img_out_trim[1].size_y,
		sub_img_out_trim[2].start_x, sub_img_out_trim[2].start_y,
		sub_img_out_trim[2].size_x, sub_img_out_trim[2].size_y,
		sub_img_out_trim[3].start_x, sub_img_out_trim[3].start_y,
		sub_img_out_trim[3].size_x, sub_img_out_trim[3].size_y);
	if (ltm_subimg_idx >= ISP_OVERSIZE_LTM_SUBIMG_NUM) {
		pr_err("fail to get target bin subimg idx, %d\n", ltm_subimg_idx);
		return;
	}
	if (pframe->common.slice_info.slice_no == 0) {
		uint32_t i = 0, j = 0;
		unsigned long flag = 0;
		struct cam_zoom_port *zoom_port = NULL;
		struct cam_zoom_node *zoom_node = NULL;

		zoom_index.node_type = CAM_NODE_TYPE_ISP_OFFLINE;
		zoom_index.port_type = PORT_TRANSFER_OUT;
		zoom_param.src.w = width;
		zoom_param.src.h = height;
		ret = camcore_postproc_zoom_param_get(module,
			pipeline_type, &zoom_param, &pframe->common.zoom_data, &zoom_index);
		zoom_index.port_type = PORT_TRANSFER_IN;
		zoom_index.port_id = PORT_ISP_OFFLINE_IN;
		zoom_param.src.w = sub_img_trim[ltm_subimg_idx].size_x;
		zoom_param.src.h = sub_img_trim[ltm_subimg_idx].size_y;
		zoom_param.dst.w = sub_img_trim[ltm_subimg_idx].size_x;
		zoom_param.dst.h = sub_img_trim[ltm_subimg_idx].size_y;
		zoom_param.crop = sub_img_trim[ltm_subimg_idx];
		for (i = 0; i < NODE_ZOOM_CNT_MAX; i++) {
			zoom_node = &pframe->common.zoom_data.zoom_node[i];
			if (zoom_node->node_type == CAM_NODE_TYPE_ISP_OFFLINE
				&& zoom_index.node_id == zoom_node->node_id) {
				for (j = 0; j < PORT_ZOOM_CNT_MAX; j++) {
					zoom_port = &zoom_node->zoom_port[j];
					if (zoom_port->port_id == PORT_ISP_OFFLINE_IN
						&& PORT_TRANSFER_IN == zoom_port->port_type)
						zoom_port->zoom_base = zoom_param;
				}
			}
		}
		spin_lock_irqsave(&channel->lastest_zoom_lock, flag);
		if (!pframe->common.nonzsl_xtm.hist_eb)
			memcpy(&channel->latest_zoom_param,
			&pframe->common.zoom_data, sizeof(struct cam_zoom_frame));
		else
			memcpy(&channel->nonzsl_pre.latest_zoom_param,
			&pframe->common.zoom_data, sizeof(struct cam_zoom_frame));
		spin_unlock_irqrestore(&channel->lastest_zoom_lock, flag);
	} else
		camcore_zoom_param_get_callback(pipeline_type,
			module,  &pframe->common.zoom_data);

	pr_debug("send sub img %d to isp in_queue\n", ltm_subimg_idx);
	pframe->common.slice_info.in_trim = sub_img_trim[ltm_subimg_idx];
	pframe->common.slice_info.out_trim = sub_img_out_trim[ltm_subimg_idx];
}

static int camcore_nonzsl_frame_slice(void *param, void *priv_data)
{
	int ret = 0, i = 0, j = 0;
	uint32_t ltm_eb = 0;
	uint32_t slice_num = 0, slice_num_h = 0, slice_num_w = 0;
	struct channel_context *ch = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_frame *pframe1 = NULL;
	struct camera_module *module = NULL;
	struct img_trim in_trim[SLICE_NUM_MAX] = {0};
	struct cam_pipeline_cfg_param cfg_param = {0};

	if (!param || !priv_data) {
		pr_err("fail to get valid param %p %p\n", param, priv_data);
		return -EFAULT;
	}
	module = (struct camera_module *)priv_data;
	ch = &module->channel[CAM_CH_CAP];
	if (!ch->enable) {
		pr_err("fail to get cap channel on nonzsl\n");
		return -EFAULT;
	}

	pframe = (struct cam_frame *)param;
	slice_num_h = pframe->common.nonzsl_xtm.full_size.h / ISP_SLCIE_HEIGHT_MAX + 1;
	ret = CAM_PIPELINE_NONZSL_ISP_PRE_NODE_CFG(ch,
		CAM_PIPELINE_CFG_XTM_EN, ISP_NODE_MODE_PRE_ID, &ltm_eb);
	if (ret)
		pr_err("warning: not get ltm enable status.\n");
	if (ltm_eb)
		slice_num_w = ISP_OVERSIZE_XTM_SLICENUM_W;
	else {
		memset(&pframe->common.zoom_data, 0, sizeof(struct cam_zoom_frame));
		camcore_zoom_param_get_callback(ch->pipeline_type,
			module, &pframe->common.zoom_data);
		slice_num_w = ISP_OVERSIZE_SLICENUM_W;
	}
	slice_num = slice_num_w * slice_num_h;
	pr_debug("ltm eb %d slicenum w %d h %d\n", ltm_eb, slice_num_w, slice_num_h);
	for (j = 0; j < slice_num_h; j++) {
		for (i = 0; i < slice_num_w; i++) {
			in_trim[i + slice_num_w * j].start_x =
				pframe->common.width / slice_num_w * i;
			in_trim[i + slice_num_w * j].start_y =
				pframe->common.height / slice_num_h * j;
			in_trim[i + slice_num_w * j].size_x =
				pframe->common.width / slice_num_w;
			in_trim[i + slice_num_w * j].size_y =
				pframe->common.height / slice_num_h;

			if ((i + slice_num_w * j) == slice_num - 1)
				pframe1 = pframe;
			else {
				pframe1 = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
				if (!pframe1) {
					pr_err("fail to get frame\n");
					return -EFAULT;
				}
				memcpy(pframe1, pframe, sizeof(struct cam_frame));
				pframe1->common.buf.type = CAM_BUF_NONE;
			}
			pframe1->common.slice_info.slice_num = slice_num_h * slice_num_w;
			pframe1->common.slice_info.slice_num_w = slice_num_w;
			pframe1->common.slice_info.slice_no = i + slice_num_w * j;
			pframe1->common.slice_info.in_trim = in_trim[i + slice_num_w * j];

			if (ltm_eb)
				camcore_oversize_img_sliceproc(
					module, pframe1, i + slice_num_w * j);
			cfg_param.node_type = pframe1->common.link_to.node_type;
			cfg_param.node_param.param = pframe1;
			cfg_param.node_param.port_id = pframe1->common.link_to.port_id;
			if (pframe1->common.nonzsl_xtm.hist_eb)
				ret = ch->nonzsl_pre_pipeline->ops.streamon(ch->nonzsl_pre_pipeline,
					&cfg_param);
			else
				ret = ch->pipeline_handle->ops.streamon(
					ch->pipeline_handle, &cfg_param);
		}
	}

	return ret;
}

static int camcore_pipeline_callback(enum cam_cb_type type, void *param, void *priv_data)
{
	int ret = 0, i = 0;
	struct cam_buf_pool_id pool_id = {0};
	struct camera_buf_get_desc buf_desc = {0};
	struct cam_hw_info *hw = NULL;
	struct cam_frame *pframe = NULL;
	struct camera_module *module = NULL;
	struct channel_context *channel = NULL;
	struct dcam_online_node *dcam_online_node_dev = NULL;
	struct cam_hw_reg_trace trace = {0};
	struct cam_buf_pool_id recycle_pool = {CAM_BUF_POOL_ABNORAM_RECYCLE, 0};
	struct dcam_switch_param csi_switch = {0};

	if (!param || !priv_data) {
		pr_err("fail to get valid param %p %p\n", param, priv_data);
		return -EFAULT;
	}

	module = (struct camera_module *)priv_data;
	hw = module->grp->hw_info;

	if (unlikely(type == CAM_CB_DCAM_DEV_ERR)) {
		pr_err("fail to fatal err may need recovery\n");
		hw->dcam_ioctl(hw, 0, DCAM_HW_CFG_CSI_REG_TRACE, module->grp->pdev);
		if (*(uint32_t *)param) {
			pr_info("cam %d start recovery\n", module->idx);
			if (atomic_read(&module->grp->recovery_state) == CAM_RECOVERY_DONE) {
				pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
				if (!pframe) {
					pr_err("fail to get frame\n");
					return -EFAULT;
				}
				pframe->common.evt = IMG_TX_ERR;
				pframe->common.irq_type = CAMERA_IRQ_MAX;
				pframe->common.irq_property = IRQ_MAX_DONE;
				ret = CAM_QUEUE_ENQUEUE(&module->img_queue, &pframe->list);
				if (ret) {
					pr_err("fail to enqueue frm queue cnt:%d, state:%d.\n",
						module->img_queue.cnt, module->img_queue.state);
					cam_queue_empty_frame_put(pframe);
				} else
					complete(&module->img_com);
				return 0;
			}
			if (atomic_cmpxchg(&module->grp->recovery_state, CAM_RECOVERY_NONE, CAM_RECOVERY_RUNNING) == CAM_RECOVERY_NONE)
				complete(&module->grp->recovery_thrd.thread_com);
		}
		return 0;
	}

	if (unlikely(type == CAM_CB_ISP_DEV_ERR) || unlikely(type == CAM_CB_ISP_RESET_ERR)) {
		dcam_online_node_dev = module->nodes_dev.dcam_online_node_dev;
		pr_err("fail to fatal err may not do anything\n");
		read_lock(&hw->soc_dcam->cam_ahb_lock);
		if (dcam_online_node_dev) {
			trace.type = ABNORMAL_REG_TRACE;
			if (dcam_online_node_dev->hw_ctx_id != DCAM_HW_CONTEXT_MAX &&
				module->grp->camsec_cfg.camsec_mode == SEC_UNABLE) {
				trace.idx = dcam_online_node_dev->hw_ctx_id;
				hw->isp_ioctl(hw, ISP_HW_CFG_REG_TRACE, &trace);
			}
		}
		for (i = 0; i < DCAM_HW_CONTEXT_MAX; ++i) {
			if (module->dcam_dev_handle->hw_ctx[i].dcam_irq_cb_func) {
				if (unlikely(type == CAM_CB_ISP_RESET_ERR)) {
					if (module->dcam_dev_handle->hw_ctx[i].is_offline_proc ==
						CAM_DISABLE && dcam_online_node_dev
						&& dcam_online_node_dev->hw_ctx_id == i) {
						csi_switch.csi_id =
							dcam_online_node_dev->csi_controller_idx;
						csi_switch.dcam_id =
							dcam_online_node_dev->hw_ctx_id;
						hw->dcam_ioctl(hw,
							dcam_online_node_dev->hw_ctx_id,
							DCAM_HW_CFG_DISCONECT_CSI,
							&csi_switch);
					}
					hw->dcam_ioctl(hw, i, DCAM_HW_CFG_IRQ_DISABLE, &i);
					hw->dcam_ioctl(
						hw, i, DCAM_HW_CFG_STOP,
						&module->dcam_dev_handle->hw_ctx[i]);
					hw->dcam_ioctl(hw, i, DCAM_HW_CFG_RESET, &i);
				}
			}
		}
		read_unlock(&hw->soc_dcam->cam_ahb_lock);
		return 0;
	}

	pframe = (struct cam_frame *)param;
	channel = &module->channel[pframe->common.channel_id];
	atomic_set(&module->timeout_flag, 0);

	switch (type) {
	case CAM_CB_FRAME_CACHE_CLEAR_BUF:
		camcore_dcam_online_buf_cfg(channel, pframe, module);
		break;
	case CAM_CB_DCAM_CLEAR_BUF:
	case CAM_CB_DCAM_DATA_DONE:
	case CAM_CB_DUMP_DATA_DONE:
	case CAM_CB_REPLACE_DATA_DONE:
	case CAM_CB_FRAME_CACHE_DATA_DONE:
	case CAM_CB_USER_BUF_DONE:
		if (atomic_read(&module->state) != CAM_RUNNING) {
			pr_info("stream off cmd %d put frame %p, state:%d\n",
				type, pframe, atomic_read(&module->state));
			if (pframe->common.buf.type == CAM_BUF_KERNEL) {
				if (channel->ch_id == CAM_CH_CAP && module->grp->is_mul_buf_share) {
					pool_id.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
					buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
					buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;
					ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, &buf_desc, (void *)module->grp->global_buf_manager);
				} else
					cam_queue_empty_frame_put(pframe);
			} else
				ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
			if (ret)
				pr_err("fail to enqueue frame\n");
			return ret;
		}

		pframe->common.evt = IMG_TX_DONE;
		if (pframe->common.irq_type == CAMERA_IRQ_4IN1_DONE)
			pframe->common.channel_id = CAM_CH_RAW;
		if (pframe->common.channel_id == CAM_CH_CAP && pframe->common.raw_src == ORI_RAW_SRC_SEL)
			pframe->common.irq_property = CAM_FRAME_ORIGINAL_RAW;
		if (pframe->common.irq_type != CAMERA_IRQ_4IN1_DONE)
			pframe->common.irq_type = CAMERA_IRQ_IMG;
		if (pframe->common.irq_property == CAM_FRAME_ORIGINAL_RAW)
			pframe->common.irq_type = CAMERA_IRQ_RAW_IMG;
		if (pframe->common.irq_property == CAM_FRAME_PROCESS_RAW)
			pframe->common.irq_type = CAMERA_IRQ_RAW_BPC_IMG;
		pframe->common.priv_data = module;
		ret = CAM_QUEUE_ENQUEUE(&module->img_queue, &pframe->list);
		if (ret) {
			pr_err("fail to img_queue overflow\n");
			ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
			if (ret)
				pr_err("fail to enqueue recycle_pool\n");
		} else {
			if ((pframe->common.irq_property == CAM_FRAME_ORIGINAL_RAW) &&
				(module->cap_scene == CAPTURE_AINR))
				CAM_PIPEINE_DCAM_ONLINE_NODE_CFG(channel,
					CAM_PIPELINE_CFG_SHUTOFF_PORT_CNT_DEC, &channel->raw_port_id);

			complete(&module->img_com);
		}
		break;
	case CAM_CB_DCAM_STATIS_DONE:
	case CAM_CB_ISP_STATIS_DONE:
	case CAM_CB_DUMP_STATIS_DONE:
		pframe->common.evt = IMG_TX_DONE;
		pframe->common.irq_type = CAMERA_IRQ_STATIS;

		if (module->flash_skip_fid != 0)
			pframe->common.is_flash_status = module->is_flash_status;

		pr_debug("pframe->common.fid %d is_flash_status %d irq_property %d\n", pframe->common.fid, pframe->common.is_flash_status, pframe->common.irq_property);
		if (atomic_read(&module->state) == CAM_RUNNING) {
			pframe->common.priv_data = module;
			ret = CAM_QUEUE_ENQUEUE(&module->frm_queue, &pframe->list);
			if (ret) {
				pr_warn("warning: frm_queue overflow\n");
				ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
			} else {
				complete(&module->frm_com);
			}
		} else
			ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
		if (ret)
			pr_err("fail to enqueue recycle_pool\n");
		break;
	case CAM_CB_DCAM_IRQ_EVENT:
		channel = &module->channel[CAM_CH_PRE];
		if (pframe->common.irq_property == IRQ_DCAM_SN_EOF) {
			cam_queue_empty_frame_put(pframe);
			break;
		}
		if (pframe->common.irq_property == IRQ_DCAM_SN_SOF &&
			module->cam_uinfo.ipg_skip_first_frm == 0) {
			cam_queue_empty_frame_put(pframe);
			break;
		}

		if (pframe->common.irq_property == IRQ_DCAM_SOF) {
			if ((module->flash_info.led0_ctrl && module->flash_info.led0_status < FLASH_STATUS_MAX) ||
				(module->flash_info.led1_ctrl && module->flash_info.led1_status < FLASH_STATUS_MAX)) {
				module->flash_core_handle->flash_core_ops->start_flash(module->flash_core_handle,
					&module->flash_info.set_param);
				if (module->flash_info.flash_last_status != module->flash_info.led0_status) {
					module->flash_skip_fid = pframe->common.fid;
					module->is_flash_status = module->flash_info.led0_status;
					ret = CAM_PIPEINE_DCAM_ONLINE_OUT_PORT_CFG(channel, PORT_BIN_OUT, CAM_PIPELINE_CFG_FLASH_SKIP_FID, &module->flash_skip_fid);
					if (ret)
						pr_err("fail to cfg flash skip fid\n");
				} else
					pr_info("do not need skip");
				pr_info("skip_fram=%d\n", pframe->common.fid);
				module->flash_info.flash_last_status = module->flash_info.led0_status;
				module->flash_info.led0_ctrl = 0;
				module->flash_info.led1_ctrl = 0;
				module->flash_info.led0_status = 0;
				module->flash_info.led1_status = 0;
			}
		}

		if (atomic_read(&module->state) == CAM_RUNNING ||
			(pframe->common.irq_property == IRQ_DCAM_SN_SOF &&
			module->cam_uinfo.ipg_skip_first_frm)){
			ret = CAM_QUEUE_ENQUEUE(&module->frm_queue, &pframe->list);
			if (ret)
				cam_queue_empty_frame_put(pframe);
			else
				complete(&module->frm_com);
		} else
			cam_queue_empty_frame_put(pframe);
		break;
	case CAM_CB_COPY_SRC_BUFFER:
	case CAM_CB_ISP_RET_SRC_BUF:
	case CAM_CB_PYRDEC_RET_SRC_BUF:
		if ((atomic_read(&module->state) != CAM_RUNNING) || (channel->dcam_port_id < 0)) {
			pr_info("isp ret src frame %p, ch_id:%d, share:%d\n", pframe,
				channel->ch_id, module->grp->is_mul_buf_share);
			pframe->common.not_use_isp_reserved_buf = 0;
			if (channel->ch_id == CAM_CH_CAP && module->grp->is_mul_buf_share &&
				pframe->common.buf.type == CAM_BUF_KERNEL) {
				pool_id.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
				buf_desc.buf_ops_cmd = CAM_BUF_STATUS_PUT_IOVA;
				buf_desc.mmu_type = CAM_BUF_IOMMUDEV_DCAM;
				ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, &buf_desc, (void *)module->grp->global_buf_manager);
				if (ret)
					pr_err("fail to enqueue frame\n");
			} else
				cam_queue_empty_frame_put(pframe);
		} else {
			if (pframe->common.buf.type != CAM_BUF_KERNEL) {
				ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
				if (ret)
					pr_err("fail to enqueue recycle_pool\n");
			} else
				ret = camcore_dcam_online_buf_cfg(channel, pframe, module);
		}
		break;
	case CAM_CB_ISP_RET_DST_BUF:
		if (atomic_read(&module->state) == CAM_RUNNING) {
			if (pframe->common.proc_mode == CAM_POSTPROC_SERIAL) {
				cam_queue_empty_frame_put(pframe);
				complete(&module->postproc_done);
			} else {
				pframe->common.priv_data = module;
				pframe->common.evt = IMG_TX_DONE;
				pframe->common.irq_type = CAMERA_IRQ_IMG;
				if (module->capture_type == CAM_CAPTURE_RAWPROC) {
					pr_info("raw proc return dst frame %p\n", pframe);

					if (pframe->common.buf.type == CAM_BUF_KERNEL) {
						cam_buf_free(&pframe->common.buf);
						cam_queue_empty_frame_put(pframe);
						return ret;
					}
					cam_buf_ionbuf_put(&pframe->common.buf);
					pframe->common.irq_property = IRQ_RAW_PROC_DONE;
				}
				ret = CAM_QUEUE_ENQUEUE(&module->img_queue, &pframe->list);
				if (ret)
					ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
				else
					complete(&module->img_com);
			}
		} else
			ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
		if (ret)
			pr_err("fail to enqueue recycle_pool\n");
		break;
	case CAM_CB_DCAM_RET_SRC_BUF:
		pr_debug("user buf return type:%d mfd %d\n", type, pframe->common.buf.mfd);
		ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
		if (ret)
			pr_err("fail to enqueue recycle_pool\n");
		break;
	case CAM_CB_ISP_SCALE_RET_ISP_BUF:
		pr_debug("user buf return type:%d mfd %d\n", type, pframe->common.buf.mfd);
		ret = cam_buf_manager_buf_enqueue(&recycle_pool, pframe, NULL, (void *)module->grp->global_buf_manager);
		if (ret)
			pr_err("fail to enqueue recycle_pool\n");
		break;
	default:
		pr_err("fail to get cb cmd: %d\n", type);
		break;
	}

	return ret;
}

static int camcore_icap_buffer_set(struct camera_module *module, struct channel_context *channel, void *param)
{
	int ret = 0, aux_dcam_path = 0, dcam_raw_path = 0;
	struct cam_frame *pframe = NULL;
	struct channel_context *ch_cap = NULL;
	struct channel_context *ch_vch = NULL;
	struct channel_context *ch_pre = NULL;
	struct channel_context *ch_raw = NULL;
	struct cam_hw_info *hw = NULL;

	hw = module->grp->hw_info;
	pframe = (struct cam_frame *)param;
	ch_cap = &module->channel[CAM_CH_CAP];
	ch_vch = &module->channel[CAM_CH_DCAM_VCH];
	ch_pre = &module->channel[CAM_CH_PRE];
	ch_raw = &module->channel[CAM_CH_RAW];

	if (channel->ch_id == CAM_CH_RAW) {
		if (hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == CAM_DISABLE
			&& ch_raw->ch_uinfo.dst_size.w != ch_cap->ch_uinfo.dst_size.w) {
			pr_debug("preview icap scene buf config\n");
			ret = CAM_PIPEINE_DATA_COPY_NODE_CFG(ch_pre, CAM_PIPELINE_CFG_BUF, pframe);
		} else {
			aux_dcam_path = module->grp->hw_info->ip_dcam[0]->dcamhw_abt->aux_dcam_path;
			ret = CAM_PIPEINE_DCAM_OFFLINE_OUT_PORT_CFG(ch_cap,
				dcamoffline_pathid_convert_to_portid(aux_dcam_path),
					CAM_PIPELINE_CFG_BUF, pframe, CAM_NODE_TYPE_DCAM_OFFLINE);
		}
	} else if (channel->ch_id == CAM_CH_DCAM_VCH) {
		/* 4in1 normal sensor raw only adopt full path,
		 * icap scene sensor raw one in two out, full & vch path
		 */
		if (module->cam_uinfo.is_4in1) {
			dcam_raw_path =
				module->grp->hw_info->ip_dcam[0]->dcamhw_abt->sensor_raw_path_id;
			ret = CAM_PIPEINE_DCAM_ONLINE_OUT_PORT_CFG(ch_vch,
				dcamonline_pathid_convert_to_portid(dcam_raw_path),
				CAM_PIPELINE_CFG_BUF, pframe);
		} else {
			struct cam_hw_info *hw = module->grp->hw_info;

			if (module->cam_uinfo.sn_rect.w >= DCAM_HW_WIDTH_MAX) {
				pframe->common.width = ch_cap->ch_uinfo.src_size.w;
				pframe->common.height = ch_cap->ch_uinfo.src_size.h;
				dcam_raw_path =
					hw->ip_dcam[0]->dcamhw_abt->sensor_raw_path_id;
			} else
				dcam_raw_path =
					hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id;
			ret = CAM_PIPEINE_DCAM_ONLINE_OUT_PORT_CFG(ch_cap,
				dcamonline_pathid_convert_to_portid(dcam_raw_path),
				CAM_PIPELINE_CFG_BUF, pframe);
		}
	} else {
		pr_err("fail to set output buffer for ch%d.\n", channel->ch_id);
		ret = -EFAULT;
	}

	return ret;
}

static int camcore_link_change(struct camera_module *module,
	enum camera_raw_scene type, enum cam_en_status flag)
{
	struct cam_pipeline_topology *pipeline_graph = NULL;
	struct cam_node_topology *node_graph = NULL;
	struct cam_port_topology *port_graph = NULL;
	uint32_t need_pyr_dec = 0;
	struct cam_hw_info *hw = NULL;
	int i = 0, ret = 0;

	hw = module->grp->hw_info;
	need_pyr_dec = hw->ip_isp->isphw_abt->pyr_dec_support;

	if (hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == CAM_DISABLE)
		pipeline_graph =
			&module->static_topology->pipeline_list[
			CAM_PIPELINE_ONLINERAW_2_COPY_2_USER_2_OFFLINEYUV];
	else {
		pipeline_graph =
			&module->static_topology->pipeline_list[
			CAM_PIPELINE_ONLINERAW_2_OFFLINEYUV];
		if (module->cam_uinfo.is_4in1) {
			pipeline_graph =
				&module->static_topology->pipeline_list[
				CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV];
			if (type == CAM_SENSOR_RAW)
				return ret;
		}
	}

	switch (type) {
	case CAM_DCAM_RAW:
		if (need_pyr_dec) {
			/* dec_node */
			for (i = 0; i < pipeline_graph->node_cnt; i++) {
				if (pipeline_graph->nodes[i].type == CAM_NODE_TYPE_PYR_DEC)
					break;
			}
			node_graph = &pipeline_graph->nodes[i];
			port_graph = &node_graph->inport[PORT_DEC_IN];
		} else {
			/* ispoffline_node */
			for (i = 0; i < pipeline_graph->node_cnt; i++) {
				if (pipeline_graph->nodes[i].type == CAM_NODE_TYPE_ISP_OFFLINE)
					break;
			}
			node_graph = &pipeline_graph->nodes[i];
			port_graph = &node_graph->inport[PORT_ISP_OFFLINE_IN];
		}
		port_graph->to_user_en = flag;
		break;
	case CAM_SENSOR_RAW:
		if (hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == CAM_DISABLE) {
			/* cam_copy_node */
			for (i = 0; i < pipeline_graph->node_cnt; i++) {
				if (pipeline_graph->nodes[i].type == CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW)
					break;
			}
			node_graph = &pipeline_graph->nodes[i];
			port_graph = &node_graph->inport[PORT_DCAM_OFFLINE_IN];
		} else {
			/* dcam_offline_node */
			for (i = 0; i < pipeline_graph->node_cnt; i++) {
				if (pipeline_graph->nodes[i].type == CAM_NODE_TYPE_DCAM_OFFLINE)
					break;
			}
			node_graph = &pipeline_graph->nodes[i];
			port_graph = &node_graph->inport[PORT_DCAM_OFFLINE_IN];
		}
		port_graph->to_user_en = flag;
		break;
	default:
		pr_err("fail to get type %d\n", type);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static int camcore_icap_scene_config(struct camera_module *module, enum cam_en_status flag)
{
	int ret = 0;
	uint32_t fmt = 0, raw_deal_switch = 0;
	struct cam_hw_info *hw = NULL;
	struct channel_context *ch_cap = NULL;
	struct channel_context *ch_raw = NULL;
	struct channel_context *ch_vch = NULL;

	ch_cap = &module->channel[CAM_CH_CAP];
	ch_raw = &module->channel[CAM_CH_RAW];
	ch_vch = &module->channel[CAM_CH_DCAM_VCH];
	hw = module->grp->hw_info;

	if (hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == CAM_DISABLE) {
		if (ch_raw->ch_uinfo.dst_size.w != ch_cap->ch_uinfo.dst_size.w) {
			raw_deal_switch = CAM_ENABLE;
			if (flag)
				module->raw_deal_switch = CAM_ENABLE;
			else
				module->raw_deal_switch = CAM_DISABLE;
		}
	}

	ret = camcore_link_change(module, CAM_SENSOR_RAW, flag);
	if (ret)
		pr_err("fail to cfg sesnor raw scene\n");

	if (ch_raw->enable && !raw_deal_switch) {
		fmt = ch_raw->ch_uinfo.dcam_raw_fmt;
		ret = camcore_link_change(module, CAM_DCAM_RAW, flag);
		if (ret)
			pr_err("fail to cfg dcam raw scene\n");

		if (hw->ip_isp->isphw_abt->fetch_raw_support == CAM_ENABLE) {
			/* update dcamoffline scene store fmt */
			ret = CAM_PIPEINE_DCAM_OFFLINE_OUT_PORT_CFG(ch_cap,
				dcamoffline_pathid_convert_to_portid(
				hw->ip_dcam[0]->dcamhw_abt->aux_dcam_path),
					CAM_PIPELINE_CFG_FMT, &fmt, CAM_NODE_TYPE_DCAM_OFFLINE);
			if (ret)
				pr_err("fail to cfg dcamoffline fmt\n");

			/* update ispoffline scene fetch fmt */
			ret = CAM_PIPEINE_ISP_IN_PORT_CFG(ch_cap,
				PORT_ISP_OFFLINE_IN, CAM_PIPELINE_CFG_FMT,
				ISP_NODE_MODE_CAP_ID, &fmt);
			if (ret)
				pr_err("fail to cfg isp fetch fmt\n");
		}
	}

	return ret;
}

static int camcore_vir_channel_config(
	struct camera_module *module, struct channel_context *channel)
{
	int ret = 0;
	struct channel_context *channel_prev = NULL;
	struct channel_context *channel_cap = NULL;
	struct camera_uchannel *ch_uinfo = NULL;
	struct isp_path_base_desc path_desc = {0};

	ch_uinfo = &channel->ch_uinfo;
	channel_prev = &module->channel[CAM_CH_PRE];
	channel_cap = &module->channel[CAM_CH_CAP];

	if (channel_prev->enable) {
		/*TODO: out_fmt need to follow hal fmt adjust*/
		path_desc.out_fmt = CAM_YVU420_2FRAME_MIPI;
		path_desc.endian = ENDIAN_LITTLE;
		path_desc.output_size.w = ch_uinfo->vir_channel[0].dst_size.w;
		path_desc.output_size.h = ch_uinfo->vir_channel[0].dst_size.h;
		path_desc.is_work = 1;
		ret = CAM_PIPEINE_ISP_OUT_PORT_CFG(channel_prev, PORT_VID_OUT, CAM_PIPELINE_CFG_BASE, ISP_NODE_MODE_PRE_ID, &path_desc);
		if (ret)
			pr_err("fail to cfg isp pre base.\n");
	}
	if (channel_cap->enable) {
		/*TODO: out_fmt need to follow hal fmt adjust*/
		path_desc.out_fmt = CAM_YVU420_2FRAME_MIPI;
		path_desc.endian = ENDIAN_LITTLE;
		path_desc.output_size.w = ch_uinfo->vir_channel[1].dst_size.w;
		path_desc.output_size.h = ch_uinfo->vir_channel[1].dst_size.h;
		path_desc.is_work = 1;
		ret = CAM_PIPEINE_ISP_OUT_PORT_CFG(channel_cap, PORT_VID_OUT, CAM_PIPELINE_CFG_BASE, ISP_NODE_MODE_CAP_ID, &path_desc);
		if (module->cam_uinfo.is_raw_alg && module->cam_uinfo.alg_type == ALG_TYPE_CAP_XDR)
			ret |= CAM_PIPEINE_ISP_OUT_PORT_CFG(channel_cap, PORT_VID_OUT, CAM_PIPELINE_CFG_BASE, ISP_NODE_MODE_OFFLINE_CAP_ID, &path_desc);
		if (ret)
			pr_err("fail to cfg isp cap base.\n");
	}
	return 0;
}

static int camcore_pyrdec_desc_get(struct camera_module *module, struct channel_context *channel,
	uint32_t pipeline_type, struct pyr_dec_node_desc *pyr_dec_desc)
{
	int ret = 0;
	uint32_t format = 0, temp_format = 0, rawpath_id = 0;
	struct cam_hw_info *hw = NULL;

	hw = module->grp->hw_info;
	format = module->cam_uinfo.sensor_if.img_fmt;
	rawpath_id = camcore_dcampath_id_convert(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
	if (rawpath_id >= DCAM_PATH_ID_MAX) {
		pr_err("fail to get rawpath id\n");
		return -ENOMEM;
	}

	pyr_dec_desc->hw = hw;
	pyr_dec_desc->layer_num = PYR_DEC_LAYER_NUM;
	pyr_dec_desc->node_idx = module->idx;
	pyr_dec_desc->dcam_slice_mode = module->cam_uinfo.dcam_slice_mode;
	pyr_dec_desc->is_4in1 = module->cam_uinfo.is_4in1;
	pyr_dec_desc->sn_size = module->cam_uinfo.sn_size;
	pyr_dec_desc->dev = module->isp_dev_handle;
	pyr_dec_desc->pyrdec_dev = module->isp_dev_handle->pyr_dec_handle;
	pyr_dec_desc->blkparam_buf_num = cam_scene_dcamonline_buffers_alloc_num(channel, module, pipeline_type) + 1;
	pyr_dec_desc->buf_manager_handle = module->grp->global_buf_manager;
	pyr_dec_desc->port_desc.share_buffer = camcore_mulsharebuf_verif(channel, &module->cam_uinfo);

	if (format == DCAM_CAP_MODE_YUV) {
		/*TBD: delet later after yuvsensor adapt dec*/
		pyr_dec_desc->is_yuvsensor = CAM_ENABLE;
		pyr_dec_desc->in_fmt = cam_format_get(channel->ch_uinfo.dst_fmt);
	} else
		pyr_dec_desc->in_fmt = channel->dcam_out_fmt;

	pyr_dec_desc->pyr_out_fmt = hw->ip_dcam[0]->dcamhw_abt->store_pyr_fmt;
	/*NEED check if necessary*/
	temp_format = channel->ch_uinfo.dcam_raw_fmt;
	if (cam_valid_fmt_get(&channel->ch_uinfo.dcam_raw_fmt, hw->ip_dcam[0]->dcampath_abt[rawpath_id]->format[0])) {
		if (hw->ip_dcam[0]->dcamhw_abt->save_band_for_bigsize)
			temp_format = CAM_RAW_PACK_10;
	}
	if ((hw->ip_isp->isphw_abt->fetch_raw_support == 0) || (format == DCAM_CAP_MODE_YUV))
		channel->ch_uinfo.dcam_out_fmt = pyr_dec_desc->in_fmt;
	else {
		channel->ch_uinfo.dcam_out_fmt = temp_format;
		pyr_dec_desc->in_fmt = temp_format;
	}
	return ret;
}

static int camcore_pyrdec_ctxid_cfg(struct channel_context *channel, void *isp_node)
{
	int ret = 0;
	ret = CAM_PIPEINE_PYR_DEC_NODE_CFG(channel, CAM_PIPELINE_CFG_CTXID, PYR_DEC_NODE_ID, isp_node);
	return ret;
}

static int camcore_framecache_desc_get(struct camera_module *module, struct frame_cache_node_desc *frame_cache_desc)
{
	int ret = 0;
	uint32_t real_cache_num = 0;

	real_cache_num = module->cam_uinfo.zsl_num;
	if (module->cam_uinfo.is_dual)
		real_cache_num = module->cam_uinfo.dual_cache_buf_num;
	frame_cache_desc->cache_real_num = real_cache_num;
	frame_cache_desc->cache_skip_num = module->cam_uinfo.zsk_skip_num;
	frame_cache_desc->raw_cache_real_num = module->cam_uinfo.raw_zsl_num;
	frame_cache_desc->raw_cache_skip_num = module->cam_uinfo.raw_zsl_skip_num;
	frame_cache_desc->need_cache_dcam_raw = (module->cam_uinfo.alg_type == ALG_TYPE_CAP_XDR
		&& !module->grp->hw_info->ip_dcam[0]->dcamhw_abt->bpc_raw_support);
	frame_cache_desc->is_share_buf = module->cam_uinfo.need_share_buf;
	frame_cache_desc->need_dual_sync = module->cam_uinfo.is_dual;
	frame_cache_desc->dual_sync_func = camcore_dual_frame_deal;
	frame_cache_desc->dual_slave_frame_set = camcore_dual_slave_frame_set;
	frame_cache_desc->cap_frame_status = camcore_cap_frame_status;
	frame_cache_desc->dual_sync_cb_data = module;
	frame_cache_desc->buf_manager_handle = module->grp->global_buf_manager;

	return ret;
}

static void camcore_pre_pipeline_info_get(struct camera_module *module, struct channel_context *channel,
		uint32_t *pipeline_type, int *dcam_port_id, int *isp_port_id)
{
	struct cam_hw_info *hw = NULL;

	hw = module->grp->hw_info;
	if (module->cam_uinfo.sensor_if.img_fmt == DCAM_CAP_MODE_YUV &&
		hw->ip_dcam[0]->dcamhw_abt->output_yuv_support == CAM_DISABLE)
		*dcam_port_id = PORT_FULL_OUT;

	if (module->cam_uinfo.alg_type == ALG_TYPE_VID_NR) {
		*dcam_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
		*pipeline_type = CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV;
	}

	if (module->cam_uinfo.alg_type == ALG_TYPE_VID_DR)
		*pipeline_type = CAM_PIPELINE_ONLINEYUV_2_USER_2_OFFLINEYUV_2_NR;
}

static void camcore_cap_pipeline_info_get(struct camera_module *module, struct channel_context *channel,
		uint32_t *pipeline_type, int *dcam_port_id, int *isp_port_id)
{
	struct cam_hw_info *hw = NULL;

	hw = module->grp->hw_info;
	if (module->cam_uinfo.zsl_num != 0 || module->cam_uinfo.is_dual)
		*pipeline_type = CAM_PIPELINE_ZSL_CAPTURE;

	if (module->cam_uinfo.is_4in1) {
		*pipeline_type = CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV;
		if (module->cam_uinfo.sn_rect.w > DCAM_HW_WIDTH_MAX)
			*dcam_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->sensor_raw_path_id);
		else
			*dcam_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
		module->auto_3dnr = channel->uinfo_3dnr = CAM_DISABLE;
		if (module->channel[CAM_CH_DCAM_VCH].enable)
			module->offline_icap_scene = CAM_ENABLE;
	}

	if (module->cam_uinfo.is_raw_alg) {
		if (module->cam_uinfo.alg_type == ALG_TYPE_CAP_AINR) {
			*pipeline_type = CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_RAW2USER2BPC2USER2YUV;
			channel->raw_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
			if (channel->ch_uinfo.sensor_raw_fmt == CAM_RAW_PACK_10 && !module->cam_uinfo.virtualsensor)
				channel->raw_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->sensor_raw_path_id);
			if (module->cam_uinfo.zsl_num != 0)
				*pipeline_type = CAM_PIPELINE_ONLINE_NORMALZSLCAPTURE_OR_RAW2USER2BPC2USER2YUV;
			if (module->cam_uinfo.param_frame_sync) {
				*dcam_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
				*pipeline_type = CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV;
			}
		} else if (module->cam_uinfo.alg_type == ALG_TYPE_CAP_MFNR) {
			*dcam_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
			channel->raw_port_id = *dcam_port_id;
			*pipeline_type = CAM_PIPELINE_ONLINERAW_2_USER_2_BPCRAW_2_USER_2_OFFLINEYUV;
		} else {
			*dcam_port_id = dcamoffline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
			*pipeline_type = CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_BPCRAW2USER2YUV;
			channel->raw_port_id = *dcam_port_id;
			if (module->cam_uinfo.zsl_num != 0)
				*pipeline_type = CAM_PIPELINE_ONLINE_NORMALZSLCAPTURE_OR_BPCRAW2USER2YUV;
			if (hw->ip_dcam[0]->dcampath_abt[DCAM_RAW_OUT_PATH]->enable == CAM_DISABLE)
				*pipeline_type = CAM_PIPELINE_ONLINEBPCRAW_2_USER_2_OFFLINEYUV;
		}
	}

	if (module->cam_uinfo.dcam_slice_mode && !module->cam_uinfo.is_4in1 && !module->cam_uinfo.virtualsensor) {
		*pipeline_type = CAM_PIPELINE_ONLINERAW_2_OFFLINEYUV;
		if (module->cam_uinfo.sn_rect.w >= DCAM_HW_WIDTH_MAX)
			*dcam_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->sensor_raw_path_id);
		else
			*dcam_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
		module->auto_3dnr = channel->uinfo_3dnr = CAM_DISABLE;
		if (module->channel[CAM_CH_DCAM_VCH].enable)
			module->offline_icap_scene = CAM_ENABLE;
	}

	if (module->cam_uinfo.dcam_slice_mode && module->cam_uinfo.virtualsensor) {
		module->auto_3dnr = channel->uinfo_3dnr = CAM_DISABLE;
	}

	if (module->cam_uinfo.sensor_if.img_fmt == DCAM_CAP_MODE_YUV &&
		hw->ip_dcam[0]->dcamhw_abt->output_yuv_support == CAM_DISABLE) {
		*isp_port_id = PORT_VID_OUT;
		*pipeline_type = CAM_PIPELINE_VIDEO;
	}

	if (module->cam_uinfo.sensor_if.img_fmt == DCAM_CAP_MODE_YUV &&
		module->cam_uinfo.dcam_slice_mode && !module->cam_uinfo.is_4in1) {
		*isp_port_id = PORT_CAP_OUT;
		*pipeline_type = CAM_PIPELINE_CAPTURE;
	}
	/* only for sharkl3 icap scene */
	if (module->channel[CAM_CH_DCAM_VCH].enable && hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == 0) {
		*pipeline_type = CAM_PIPELINE_ONLINERAW_2_COPY_2_USER_2_OFFLINEYUV;
		*dcam_port_id = dcamoffline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
		module->offline_icap_scene = CAM_ENABLE;
		module->auto_3dnr = channel->uinfo_3dnr = CAM_DISABLE;
	}
}

static int camcore_pipeline_init(struct camera_module *module,
		struct channel_context *channel)
{
	int ret = 0, i = 0;
	int dcam_port_id = 0, isp_port_id = 0;
	uint32_t pipeline_type = 0, pyrdec_support = 0;
	uint32_t icap_need_dcam_raw = 0;
	struct cam_hw_info *hw = NULL;
	struct channel_context *channel_cap = NULL;
	struct channel_context *channel_vid = NULL;
	struct channel_context *channel_prev = NULL;
	struct cam_pipeline_desc *pipeline_desc = NULL;
	struct pyr_dec_node_desc *pyr_dec_desc = NULL;
	struct cam_copy_node_desc *cam_copy_desc = NULL;
	struct isp_node_desc *isp_node_description = NULL;
	struct cam_buf_manager *buf_manager_handle = NULL;
	struct dcam_fetch_node_desc *dcam_fetch_desc = NULL;
	struct dcam_online_node_desc *dcam_online_desc = NULL;
	struct dcam_offline_node_desc *dcam_offline_desc = NULL;
	struct frame_cache_node_desc *frame_cache_desc = NULL;
	struct isp_yuv_scaler_node_desc *isp_yuv_scaler_desc = NULL;
	struct dcam_offline_node_desc *dcam_offline_lscraw_desc = NULL;
	struct dcam_offline_node_desc *dcam_offline_bpcraw_desc = NULL;


	if (!module || !channel) {
		pr_err("fail to get valid inptr %p %p\n", module, channel);
		return -EINVAL;
	}

	pipeline_desc = cam_buf_kernel_sys_vzalloc(sizeof(struct cam_pipeline_desc));
	if (!pipeline_desc) {
		pr_err("fail to alloc pipeline_des\n");
		return -ENOMEM;
	}

	hw = module->grp->hw_info;
	buf_manager_handle = module->grp->global_buf_manager;
	pyrdec_support = (hw->ip_isp->isphw_abt->pyr_dec_support &&
		module->grp->camsec_cfg.camsec_mode == SEC_UNABLE);
	channel_prev = &module->channel[CAM_CH_PRE];
	channel->ch_uinfo.src_size.w = module->cam_uinfo.sn_rect.w;
	channel->ch_uinfo.src_size.h = module->cam_uinfo.sn_rect.h;

	switch (channel->ch_id) {
	case CAM_CH_PRE:
		dcam_port_id = PORT_BIN_OUT;
		isp_port_id = PORT_PRE_OUT;
		pipeline_type = CAM_PIPELINE_PREVIEW;
		camcore_pre_pipeline_info_get(module, channel, &pipeline_type, &dcam_port_id, &isp_port_id);
		break;
	case CAM_CH_VID:
		if (channel_prev->enable) {
			dcam_port_id = channel_prev->dcam_port_id;
		} else {
			dcam_port_id = PORT_BIN_OUT;
			pr_info("vid channel enable without preview\n");
		}
		isp_port_id = PORT_VID_OUT;
		if (module->cam_uinfo.alg_type == ALG_TYPE_VID_NR)
			pipeline_type = CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV;
		else
			pipeline_type = CAM_PIPELINE_VIDEO;
		break;
	case CAM_CH_CAP:
		dcam_port_id = PORT_FULL_OUT;
		isp_port_id = PORT_CAP_OUT;
		pipeline_type = CAM_PIPELINE_CAPTURE;
		camcore_cap_pipeline_info_get(module, channel, &pipeline_type, &dcam_port_id, &isp_port_id);
		break;
	case CAM_CH_RAW:
		if ((module->cam_uinfo.sn_rect.w >= DCAM_HW_WIDTH_MAX) || module->raw_callback)
			dcam_port_id = PORT_VCH2_OUT;
		else
			dcam_port_id = dcamoffline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);

		channel_cap = &module->channel[CAM_CH_CAP];
		channel_vid = &module->channel[CAM_CH_VID];
		module->auto_3dnr = channel->uinfo_3dnr = 0;
		channel->need_dcam_raw = module->cam_uinfo.need_dcam_raw;
		isp_port_id = -1;
		pipeline_type = CAM_PIPELINE_SENSOR_RAW;
		if (!channel_cap->enable) {
			if (hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == 0 && channel_vid->enable && module->cam_uinfo.need_dcam_raw)
				goto fail;
			pr_info("ITS Only open preview pipeline,shoud open raw channel\n");
		} else {
			if ((channel_cap->pipeline_type == CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_RAW2USER2BPC2USER2YUV ||
				channel_cap->pipeline_type == CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_BPCRAW2USER2YUV) &&
				!hw->ip_isp->isphw_abt->fetch_raw_support && !module->cam_uinfo.need_dcam_raw) {
				cam_scene_offlineraw_port_dynamic_config(module, channel_cap->pipeline_type,
					CAM_NODE_TYPE_DCAM_OFFLINE, PORT_RAW_OUT, CAM_DISABLE);
			}
			if (module->cam_uinfo.need_dcam_raw &&
				(hw->ip_isp->isphw_abt->fetch_raw_support ||
				(module->cam_uinfo.alg_type == ALG_TYPE_CAP_XDR) ||
				(module->cam_uinfo.alg_type == ALG_TYPE_CAP_AINR) ||
				module->cam_uinfo.dcam_slice_mode))
				goto fail;
		}
		cam_scene_onlineraw_ports_enable(module, dcam_port_id);
		break;
	case CAM_CH_VIRTUAL:
		channel->isp_port_id = PORT_VID_OUT;
		camcore_vir_channel_config(module, channel);
		goto fail;
		break;
	case CAM_CH_DCAM_VCH:
		channel_vid = &module->channel[CAM_CH_VID];
		if (hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == 0 && channel_vid->enable)
			dcam_port_id = PORT_FULL_OUT;
		else
			dcam_port_id = PORT_VCH2_OUT;
		isp_port_id = -1;
		pipeline_type = CAM_PIPELINE_SENSOR_RAW;
		cam_scene_onlineraw_ports_enable(module, dcam_port_id);
		if (module->cam_uinfo.is_4in1)
			module->auto_3dnr = channel->uinfo_3dnr = CAM_DISABLE;

		if ((hw->ip_dcam[0]->dcamhw_abt->mul_raw_output_support == 0 && !channel_vid->enable) || module->cam_uinfo.dcam_slice_mode)
			goto fail;
		break;
	default:
		pr_err("fail to get channel id %d\n", channel->ch_id);
		ret = -EINVAL;
		goto fail;
	}

	if (pipeline_type == CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV) {
		ret = cam_scene_online2user2offline_dynamic_config(module, channel->ch_id, CAM_ENABLE);
		if (ret) {
			pr_err("fail to config onlineraw_2_offlineyuv pipeline\n");
			ret = -ENOMEM;
			goto fail;
		}
	}

	if ((pipeline_type == CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_RAW2USER2BPC2USER2YUV ||
		pipeline_type == CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_BPCRAW2USER2YUV) &&
		module->channel[CAM_CH_DCAM_VCH].enable && !hw->ip_isp->isphw_abt->fetch_raw_support) {
		cam_scene_offlineraw_port_dynamic_config(module, pipeline_type, CAM_NODE_TYPE_DCAM_OFFLINE,
			PORT_RAW_OUT, CAM_ENABLE);
		icap_need_dcam_raw = CAM_ENABLE;
		channel->offline_need_dcam_raw = CAM_ENABLE;
	}

	camcore_compression_cal(module);
	if (hw->ip_isp->isphw_abt->fetch_raw_support == 0 && channel->ch_id != CAM_CH_RAW)
		channel->dcam_out_fmt = CAM_YVU420_2FRAME_MIPI;
	else
		channel->dcam_out_fmt = channel->ch_uinfo.dcam_raw_fmt;
	/*TBD: mipi yuv420 need to adapt later*/
	if (module->cam_uinfo.sensor_if.img_fmt == DCAM_CAP_MODE_YUV)
		channel->dcam_out_fmt = CAM_YVU420_2FRAME;

	channel->isp_port_id = isp_port_id;
	if (dcam_port_id >= PORT_DCAM_OUT_MAX) {
		pr_err("fail to get port id\n");
		ret = -ENOMEM;
		goto fail;
	}
	channel->dcam_port_id = dcam_port_id;
	channel->aux_dcam_port_id =
		dcamoffline_pathid_convert_to_portid(hw->ip_dcam[1]->dcamhw_abt->aux_dcam_path);
	channel->aux_raw_port_id =
		dcamoffline_pathid_convert_to_portid(hw->ip_dcam[1]->dcamhw_abt->dcam_raw_path_id);
	if (channel->aux_raw_port_id >= PORT_DCAM_OFFLINE_OUT_MAX) {
		pr_err("fail to get port id\n");
		ret = -ENOMEM;
		goto fail;
	}
	channel->ch_uinfo.pyr_out_fmt = hw->ip_dcam[0]->dcamhw_abt->store_pyr_fmt;

	pipeline_desc->nodes_dev = &module->nodes_dev;
	pipeline_desc->zoom_cb_func = camcore_zoom_param_get_callback;
	pipeline_desc->slice_cb_func = camcore_nonzsl_frame_slice;
	pipeline_desc->data_cb_func = camcore_pipeline_callback;
	pipeline_desc->data_cb_handle = module;
	pipeline_desc->buf_manager_handle = buf_manager_handle;
	pipeline_desc->pipeline_graph = &module->static_topology->pipeline_list[pipeline_type];

	if (pipeline_desc->pipeline_graph->need_dcam_online) {
		dcam_online_desc = &pipeline_desc->dcam_online_desc;
		if (icap_need_dcam_raw == CAM_ENABLE)
			dcam_online_desc->need_dcam_raw = icap_need_dcam_raw;
		cam_scene_dcamonline_desc_get(module, channel, pipeline_type, dcam_online_desc);
		if (module->cam_uinfo.virtualsensor) {
			dcam_fetch_desc = &pipeline_desc->dcam_fetch_desc;
			dcam_fetch_desc->online_node_desc = dcam_online_desc;
			dcam_fetch_desc->virtualsensor = module->cam_uinfo.virtualsensor;
			dcam_fetch_desc->fetch_fmt = channel->ch_uinfo.sensor_raw_fmt;
		}
	}

	if (pipeline_desc->pipeline_graph->need_dcam_offline) {
		dcam_offline_desc = &pipeline_desc->dcam_offline_desc;
		cam_scene_dcamoffline_desc_get(module, channel, pipeline_type, dcam_offline_desc);
		if (icap_need_dcam_raw == CAM_ENABLE)
			dcam_offline_desc->port_desc.offline_need_dcam_raw = icap_need_dcam_raw;
	}

	if (pipeline_desc->pipeline_graph->need_dcam_offline_bpc) {
		dcam_offline_bpcraw_desc = &pipeline_desc->dcam_offline_bpcraw_desc;
		cam_scene_dcamoffline_bpcraw_desc_get(module, channel, dcam_offline_bpcraw_desc);
	}

	if (pipeline_desc->pipeline_graph->need_dcam_offline_lsc) {
		dcam_offline_lscraw_desc = &pipeline_desc->dcam_offline_lscraw_desc;
		cam_scene_dcamoffline_lscraw_desc_get(module, channel, dcam_offline_lscraw_desc);
	}

	if (pipeline_desc->pipeline_graph->need_isp_offline) {
		isp_node_description = &pipeline_desc->isp_node_description;
		isp_node_description->pyr_layer_num = pipeline_desc->pipeline_graph->pyr_layer_num;
		cam_scene_ispoffline_desc_get(module, channel, pipeline_type, isp_node_description);
	}

	if (pipeline_desc->pipeline_graph->need_frame_cache) {
		frame_cache_desc = &pipeline_desc->frame_cache_desc;
		camcore_framecache_desc_get(module, frame_cache_desc);
		channel->need_framecache = 1;
	}

	if (pyrdec_support && pipeline_desc->pipeline_graph->need_pyr_dec) {
		pyr_dec_desc = &pipeline_desc->pyr_dec_desc;
		camcore_pyrdec_desc_get(module, channel, pipeline_type, pyr_dec_desc);
	}

	if (pipeline_desc->pipeline_graph->need_yuv_scaler) {
		isp_yuv_scaler_desc = &pipeline_desc->isp_yuv_scaler_desc;
		cam_scene_isp_yuv_scaler_desc_get(module, channel, isp_yuv_scaler_desc);
	}

	if (pipeline_desc->pipeline_graph->need_copy_node) {
		cam_copy_desc = &pipeline_desc->cam_copy_desc;
		cam_scene_camcopy_desc_get(module, cam_copy_desc, pipeline_type);
	}

	if ((pipeline_type == CAM_PIPELINE_ONLINERAW_2_OFFLINEYUV || module->cam_uinfo.is_4in1) && !channel_prev->enable) {
		uint32_t statis_pipe_type = CAM_PIPELINE_ONLINERAW_2_OFFLINEPREVIEW;
		dcam_offline_desc->offline_pre_en = CAM_ENABLE;
		isp_node_description->ch_id = CAM_CH_PRE;
		if (module->cam_uinfo.is_rgb_ltm)
			isp_node_description->mode_ltm = MODE_LTM_PRE;
		isp_node_description->port_desc.output_size.w = channel->ch_uinfo.dst_size.w / 4;
		isp_node_description->port_desc.output_size.h = channel->ch_uinfo.dst_size.h / 4;

		channel->ch_uinfo.nonzsl_pre_ratio = 4;
		if ((channel->ch_uinfo.src_crop.w / 4) < LTM_MIN_TILE_WIDTH * TILE_NUM_MAX)
			channel->ch_uinfo.nonzsl_pre_ratio = 2;
		if ((channel->ch_uinfo.src_crop.w / 2) < LTM_MIN_TILE_WIDTH * TILE_NUM_MAX)
			channel->ch_uinfo.nonzsl_pre_ratio = 1;

		cam_zoom_crop_size_align(module, &channel->ch_uinfo.src_crop, CAM_CH_CAP);
		channel->latest_user_crop = channel->ch_uinfo.src_crop;

		pipeline_desc->pipeline_graph = &module->static_topology->pipeline_list[statis_pipe_type];
		channel->nonzsl_pre_pipeline = cam_pipeline_creat(pipeline_desc);
		if (!channel->nonzsl_pre_pipeline) {
			pr_err("fail to get statis pipeline.\n");
			return -ENOMEM;
		}
		channel->nonzsl_statis_pipeline_type = statis_pipe_type;
		pipeline_desc->pipeline_graph = &module->static_topology->pipeline_list[pipeline_type];
		dcam_offline_desc->offline_pre_en = CAM_DISABLE;
		isp_node_description->ch_id = channel->ch_id;
		if (module->cam_uinfo.is_rgb_ltm)
			isp_node_description->mode_ltm = MODE_LTM_CAP;
		isp_node_description->port_desc.output_size = channel->ch_uinfo.dst_size;
	}
	if (pipeline_desc->pipeline_graph->need_dcam_online &&
		pipeline_desc->pipeline_graph->need_isp_offline &&
			module->cam_uinfo.dcam_slice_mode && !module->cam_uinfo.is_4in1 &&
				module->cam_uinfo.virtualsensor) {
		uint32_t rawpath_id = 0, statis_pipe_type = CAM_PIPELINE_PREVIEW;

		rawpath_id =
			camcore_dcampath_id_convert(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
		if (rawpath_id >= DCAM_PATH_ID_MAX) {
			pr_err("fail to get rawpath id\n");
			ret = -ENOMEM;
			goto fail;
		}
		isp_node_description->ch_id = CAM_CH_PRE;
		dcam_online_desc->port_desc[PORT_BIN_OUT].raw_src = PROCESS_RAW_SRC_SEL;
		dcam_online_desc->port_desc[PORT_BIN_OUT].endian = ENDIAN_LITTLE;
		dcam_online_desc->port_desc[PORT_BIN_OUT].bayer_pattern =
			module->cam_uinfo.sensor_if.img_ptn;
		dcam_online_desc->port_desc[PORT_BIN_OUT].sn_if_fmt =
			module->cam_uinfo.sensor_if.img_fmt;
		dcam_online_desc->port_desc[PORT_BIN_OUT].pyr_out_fmt =
			hw->ip_dcam[0]->dcamhw_abt->store_pyr_fmt;
		dcam_online_desc->port_desc[PORT_BIN_OUT].resbuf_get_cb =
			cam_scene_reserved_buf_cfg;
		dcam_online_desc->port_desc[PORT_BIN_OUT].resbuf_cb_data = module;
		cam_valid_fmt_get(&channel->ch_uinfo.dcam_raw_fmt,
			hw->ip_dcam[0]->dcampath_abt[rawpath_id]->format[0]);
		dcam_online_desc->port_desc[PORT_BIN_OUT].dcam_out_fmt =
			channel->dcam_out_fmt;
		dcam_online_desc->port_desc[PORT_BIN_OUT].compress_en =
			channel->compress_en;
		dcam_fetch_desc->offline_pre_en = CAM_ENABLE;
		if (module->cam_uinfo.is_rgb_ltm)
			isp_node_description->mode_ltm = MODE_LTM_PRE;
		isp_node_description->port_desc.output_size.w = channel->ch_uinfo.dst_size.w / 4;
		isp_node_description->port_desc.output_size.h = channel->ch_uinfo.dst_size.h / 4;

		channel->ch_uinfo.nonzsl_pre_ratio = 4;
		if ((channel->ch_uinfo.src_crop.w / 4) < LTM_MIN_TILE_WIDTH * TILE_NUM_MAX)
			channel->ch_uinfo.nonzsl_pre_ratio = 2;
		if ((channel->ch_uinfo.src_crop.w / 2) < LTM_MIN_TILE_WIDTH * TILE_NUM_MAX)
			channel->ch_uinfo.nonzsl_pre_ratio = 1;

		cam_zoom_crop_size_align(module, &channel->ch_uinfo.src_crop, CAM_CH_CAP);
		channel->latest_user_crop = channel->ch_uinfo.src_crop;

		pipeline_desc->pipeline_graph =
			&module->static_topology->pipeline_list[statis_pipe_type];
		channel->nonzsl_pre_pipeline = cam_pipeline_creat(pipeline_desc);
		if (!channel->nonzsl_pre_pipeline) {
			pr_err("fail to get statis pipeline.\n");
			return -ENOMEM;
		}
		for (i = PORT_BIN_OUT; i <= PORT_FULL_OUT; i++) {
			struct cam_scene *topo = module->static_topology;
			uint32_t type = CAM_NODE_TYPE_DCAM_ONLINE;

			if (topo->pipeline_list[pipeline_type].nodes[type].outport[i].link_state !=
				PORT_LINK_NORMAL)
				continue;
			dcam_online_desc->port_desc[i].is_ultr_virtualsensor = 1;
		}
		pipeline_desc->pipeline_graph =
			&module->static_topology->pipeline_list[pipeline_type];
		isp_node_description->ch_id = channel->ch_id;
		dcam_fetch_desc->offline_pre_en = CAM_DISABLE;
		if (module->cam_uinfo.is_rgb_ltm)
			isp_node_description->mode_ltm = MODE_LTM_CAP;
		isp_node_description->port_desc.output_size = channel->ch_uinfo.dst_size;
	}

	channel->pipeline_type = pipeline_type;
	channel->pipeline_handle = cam_pipeline_creat(pipeline_desc);
	if (!channel->pipeline_handle) {
		pr_err("fail to get cam pipeline\n");
		ret = -ENOMEM;
		goto fail;
	}

	/*TEMP: config ispctxid to pyrdec node*/
	if (pyr_dec_desc && isp_node_description)
		camcore_pyrdec_ctxid_cfg(channel, isp_node_description->isp_node);
	pr_info("cam%d ch %d pipeline done. ret = %d pipeline_type %s, nonzsl_pre_ratio %d\n",
		module->idx, channel->ch_id, ret, pipeline_desc->pipeline_graph->name, channel->ch_uinfo.nonzsl_pre_ratio);
fail:
	cam_buf_kernel_sys_vfree(pipeline_desc);
	return ret;
}

static void camcore_pipeline_deinit(struct camera_module *module,
		struct channel_context *channel)
{
	int j = 0, k = 0;
	struct cam_nodes_dev *nodes_dev = NULL;

	nodes_dev = &module->nodes_dev;

	if (channel->pipeline_handle)
		cam_pipeline_destory(channel->pipeline_handle);
	if (channel->nonzsl_pre_pipeline)
		cam_pipeline_destory(channel->nonzsl_pre_pipeline);

	if (channel->ch_id == CAM_CH_RAW || channel->ch_id == CAM_CH_DCAM_VCH)
		cam_scene_onlineraw_ports_disable(module, channel->dcam_port_id);
	if (channel->pipeline_type == CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV && channel->ch_id != CAM_CH_PRE)
		cam_scene_online2user2offline_dynamic_config(module, channel->ch_id, CAM_DISABLE);

	if ((channel->pipeline_type == CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_RAW2USER2BPC2USER2YUV ||
		channel->pipeline_type == CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_BPCRAW2USER2YUV) &&
		!module->grp->hw_info->ip_isp->isphw_abt->fetch_raw_support) {
		cam_scene_offlineraw_port_dynamic_config(module, channel->pipeline_type,
			CAM_NODE_TYPE_DCAM_OFFLINE, PORT_RAW_OUT, CAM_DISABLE);
	}

	nodes_dev->dcam_online_node_dev = NULL;
	nodes_dev->dcam_fetch_node_dev = NULL;
	nodes_dev->dcam_offline_node_dev = NULL;
	nodes_dev->dcam_offline_node_bpcraw_dev = NULL;
	nodes_dev->dcam_offline_node_lscraw_dev = NULL;
	nodes_dev->dcam_offline_node_raw2frgb_dev = NULL;
	nodes_dev->dcam_offline_node_frgb2yuv_dev = NULL;
	for (k = 0; k < PYR_DEC_MAX_NODE_ID; k++) {
		nodes_dev->pyr_dec_node_dev[k] = NULL;
		for (j = 0; j < PORT_DEC_OUT_MAX; j++)
			nodes_dev->pyr_dec_out_port_dev[k ][j] = NULL;
	}
	for (k = 0; k < ISP_YUV_SCALER_MAX_NODE_ID; k++) {
		nodes_dev->isp_yuv_scaler_node_dev[k] = NULL;
		for (j = 0; j < PORT_ISP_YUV_SCALER_IN_MAX; j++)
			nodes_dev->isp_scaler_in_port_dev[k][j] = NULL;
		for (j = 0; j < PORT_ISP_YUV_SCALER_OUT_MAX; j++)
			nodes_dev->isp_scaler_out_port_dev[k][j] = NULL;
	}
	for (k = 0; k < ISP_NODE_MODE_MAX_ID; k++) {
		nodes_dev->isp_node_dev[k] = NULL;
		for (j = 0; j < PORT_ISP_IN_MAX; j++)
			nodes_dev->isp_in_port_dev[k][j] = NULL;
		for (j = 0; j < PORT_ISP_OUT_MAX; j++)
			nodes_dev->isp_out_port_dev[k][j] = NULL;
	}
	for (j = 0; j < PORT_DCAM_OUT_MAX; j++)
		nodes_dev->dcam_online_out_port_dev[j] = NULL;
	for (j = 0; j < PORT_DCAM_OUT_MAX; j++)
		nodes_dev->dcam_offline_out_port_dev[j] = NULL;
	for (j = 0; j < PORT_DCAM_OUT_MAX; j++)
		nodes_dev->dcam_offline_bpcraw_out_port_dev[j] = NULL;
	for (j = 0; j < PORT_DCAM_OUT_MAX; j++)
		nodes_dev->dcam_offline_lscraw_out_port_dev[j] = NULL;
	for (j = 0; j < PORT_DCAM_OUT_MAX; j++)
		nodes_dev->dcam_offline_raw2frgb_out_port_dev[j] = NULL;
	for (j = 0; j < PORT_DCAM_OUT_MAX; j++)
		nodes_dev->dcam_offline_frgb2yuv_out_port_dev[j] = NULL;
}

static int camcore_timer_start(struct timer_list *cam_timer, uint32_t time_val)
{
	int ret = 0;

	pr_debug("starting timer %ld\n", jiffies);
	ret = os_adapt_time_mod_timer(cam_timer, jiffies + msecs_to_jiffies(time_val));

	return ret;
}

static void camcore_timer_stop(struct timer_list *cam_timer)
{
	os_adapt_time_del_timer_sync(cam_timer);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static void camcore_timer_callback(struct timer_list *t)
{
	struct camera_module *module = os_find_container(module, t, cam_timer);
#else
static void camcore_timer_callback(unsigned long data)
{
	struct camera_module *module = (struct camera_module *)data;
#endif

	struct cam_frame *frame = NULL;
	uint32_t timer = 0;
	int ret = 0;

	if (!module || atomic_read(&module->state) != CAM_RUNNING) {
		pr_debug("warning get valid module %p or error state\n", module);
		return;
	}

	if (!module->dcam_ctx_bind_state) {
		pr_debug("dcam csi unbind");
		return;
	}

	if (atomic_read(&module->timeout_flag) == 1) {
		pr_err("fail to get frame data, CAM%d timeout.\n", module->idx);
		if (module->timeout_num) {
			frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
			if (!frame) {
				pr_err("fail to get frame\n");
				return;
			}
			if (module->capture_type == CAM_CAPTURE_RAWPROC) {
				module->capture_type = CAM_CAPTURE_RAWPROC_DONE;
				frame->common.evt = IMG_TX_DONE;
				frame->common.irq_type = CAMERA_IRQ_DONE;
				frame->common.irq_property = IRQ_RAW_PROC_TIMEOUT;
			} else {
				frame->common.evt = IMG_TIMEOUT;
				frame->common.irq_type = CAMERA_IRQ_MAX;
				frame->common.irq_property = IRQ_MAX_DONE;
			}
			ret = CAM_QUEUE_ENQUEUE(&module->img_queue, &frame->list);
			if (ret) {
				pr_err("fail to enqueue frm queue cnt:%d, state:%d.\n",
					module->img_queue.cnt, module->img_queue.state);
				cam_queue_empty_frame_put(frame);
			} else
				complete(&module->img_com);
		} else {
			module->timeout_num++;
			/*TODO csi reg print*/
			pr_info("cam %d start dcam recovery\n", module->idx);
#ifdef OPLUS_FEATURE_CAMERA_COMMON
			// first sensor need poweroff,poweron,init,setting,streamon
			frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
			if (frame) {
				frame->common.evt = IMG_CSI2_ERR;
				frame->common.irq_type = CAMERA_IRQ_MAX;
				frame->common.irq_property = IRQ_MAX_DONE;
				ret = CAM_QUEUE_ENQUEUE(&module->img_queue, &frame->list);
				if (ret) {
					pr_err("fail to enqueue frm queue cnt:%d, state:%d.\n",
						module->img_queue.cnt, module->img_queue.state);
					cam_queue_empty_frame_put(frame);
				} else{
					complete(&module->img_com);
				}
			}
#endif /* OPLUS_FEATURE_CAMERA_COMMON */
			if (atomic_cmpxchg(&module->grp->recovery_state, CAM_RECOVERY_NONE, CAM_RECOVERY_RUNNING) == CAM_RECOVERY_NONE
				&& !module->cam_uinfo.virtualsensor)
				complete(&module->grp->recovery_thrd.thread_com);
		}
	} else {
		module->timeout_num = 0;
		atomic_set(&module->timeout_flag, 1);
		if (module->cam_uinfo.is_longexp)
			timer = CAMERA_LONGEXP_TIMEOUT;
		else
			timer = CAMERA_WATCHDOG_TIMEOUT;
		camcore_timer_start(&module->cam_timer, timer);
	}
}

static void camcore_timer_init(struct timer_list *cam_timer, unsigned long data)
{
	os_adapt_time_init_timer(cam_timer, camcore_timer_callback, data);
}

static void camcore_timer_deinit(struct timer_list *cam_timer)
{
	os_adapt_time_del_timer_sync(cam_timer);
}

static int camcore_virtual_sensor_proc(struct camera_module *module,
		struct isp_raw_proc_info *proc_info)
{
	int ret = 0;
	timespec cur_ts = {0};
	struct channel_context *ch = NULL;
	struct cam_frame *src_frame = NULL;

	if (atomic_read(&module->state) != CAM_RUNNING) {
		pr_err("fail to get cam%d error state: %d\n", module->idx,
			atomic_read(&module->state));
		return -EFAULT;
	}

	ch = &module->channel[CAM_CH_CAP];
	if (ch->enable == 0) {
		pr_debug("cap channel is not enable \n");
		ch = &module->channel[CAM_CH_PRE];
		if (ch->enable == 0) {
			pr_err("fail to get channel enable state\n");
			return -EFAULT;
		}
	}
	src_frame = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	if (!src_frame) {
		pr_err("fail to get frame\n");
		return -EFAULT;
	}
	src_frame->common.buf.type = CAM_BUF_USER;
	src_frame->common.buf.mfd = proc_info->fd_src;
	src_frame->common.buf.offset[0] = proc_info->src_offset;
	src_frame->common.channel_id = ch->ch_id;
	src_frame->common.width = proc_info->src_size.width;
	src_frame->common.height = proc_info->src_size.height;
	src_frame->common.fid = module->simu_fid++;
	os_adapt_time_get_ts(&cur_ts);
	src_frame->common.sensor_time.tv_sec = cur_ts.tv_sec;
	src_frame->common.sensor_time.tv_usec = cur_ts.tv_nsec / NSEC_PER_USEC;
	src_frame->common.boot_sensor_time = os_adapt_time_get_boottime();
	src_frame->common.link_from.node_type = CAM_NODE_TYPE_USER;
	src_frame->common.link_from.node_id = CAM_LINK_DEFAULT_NODE_ID;
	ret = cam_buf_ionbuf_get(&src_frame->common.buf);
	if (ret) {
		pr_err("fail to get ion buffer\n");
		goto virtualsensor_src_fail;
	}

	ret = CAM_PIPEINE_DCAM_ONLINE_NODE_CFG(ch, CAM_PIPELINE_CFG_FETCH_BUF, src_frame);
	if (ret)
		goto virtualsensor_src_fail;

	atomic_set(&module->timeout_flag, 1);
	ret = camcore_timer_start(&module->cam_timer, CAMERA_TIMEOUT);

	return ret;
virtualsensor_src_fail:
	cam_queue_empty_frame_put(src_frame);
	pr_err("fail to call virtual sensor raw proc\n");
	return ret;
}

static int camcore_csi_switch_disconnect(struct camera_module *module, uint32_t mode, uint32_t csi_connect_stat)
{
	int ret = 0, i = 0;
	struct channel_context *ch = NULL;
	uint32_t dcam_path_state = DCAM_PATH_PAUSE;
	uint32_t node_id = 0;
	struct dcam_csi_reset_param csi_p = {0};

	if (atomic_read(&module->state) != CAM_RUNNING) {
		pr_warn("warning: module state: %d\n", atomic_read(&module->state));
		return 0;
	}

	ch = &module->channel[CAM_CH_PRE];
	if (!ch->pipeline_handle) {
		pr_warn("warning: pre pipeline_handle not get\n");
		ch = &module->channel[CAM_CH_CAP];
		if (!ch->pipeline_handle) {
			pr_err("fail to get pipeline handle %p\n", ch->pipeline_handle);
			return -1;
		}
	}

	csi_p.mode = mode;
	csi_p.csi_connect_stat = csi_connect_stat;
	ret = CAM_PIPEINE_DCAM_ONLINE_NODE_CFG(ch, CAM_PIPELINE_CFG_RESET, &csi_p);

	for(i = 0; i < CAM_CH_MAX; i++) {
		if (module->channel[i].ch_id == CAM_CH_VIRTUAL)
			continue;
		if (module->channel[i].enable) {
			node_id = (module->channel[i].ch_id == CAM_CH_CAP) ? ISP_NODE_MODE_CAP_ID : ISP_NODE_MODE_PRE_ID;
			ret = CAM_PIPEINE_ISP_NODE_CFG(&module->channel[i], CAM_PIPRLINE_CFG_PARAM_Q_CLEAR, node_id, NULL);
			if (ret)
				pr_err("fail to recycle cam%d ch %d blk param node\n", module->idx, i);
		}
	}

	ch = &module->channel[CAM_CH_CAP];
	if (ch->enable) {
		if (module->cam_uinfo.need_share_buf)
			ret = CAM_PIPEINE_DCAM_ONLINE_OUT_PORT_CFG(ch, PORT_FULL_OUT, CAM_PIPILINE_CFG_SHARE_BUF, &dcam_path_state);
		if (module->cam_uinfo.zsl_num != 0)
			ret = CAM_PIPEINE_FRAME_CACHE_NODE_CFG(ch, CAM_PIPELINE_CFG_CLR_CACHE_BUF, FRAME_CACHE_CAP_NODE_ID, NULL);
	}

	camcore_timer_stop(&module->cam_timer);
	if (atomic_read(&module->timeout_flag) == 1)
		atomic_set(&module->timeout_flag, 0);

	return ret;
}

static int camcore_csi_switch_connect(struct camera_module *module, uint32_t mode, uint32_t csi_connect_stat)
{
	int ret = 0;
	struct cam_pipeline_cfg_param param = {0};
	struct channel_context *ch_pre = NULL;
	uint32_t timer = 0;

	ch_pre = &module->channel[CAM_CH_PRE];
	param.node_type = CAM_NODE_TYPE_DCAM_ONLINE;
	param.node_param.param = NULL;
	param.node_param.port_id = 0xffffffff;
	ret = ch_pre->pipeline_handle->ops.streamon(ch_pre->pipeline_handle, &param);
	if (ret < 0)
		pr_err("fail to start dcam dev, ret %d\n", ret);

	atomic_set(&module->timeout_flag, 1);
	if (module->cam_uinfo.is_longexp)
		timer = CAMERA_LONGEXP_TIMEOUT;
	else
		timer = CAMERA_TIMEOUT;
	camcore_timer_start(&module->cam_timer, timer);
	return ret;
}

static int camcore_shutoff_param_prepare(struct camera_module *module,
		struct cam_pipeline_shutoff_param *pipeline_shutoff)
{
	uint32_t dcam_port_id = 0, shutoff_cnt = 0;
	uint32_t raw_port_id;
	struct channel_context *ch = NULL;
	struct cam_hw_info *hw = NULL;
	struct cam_node_shutoff_ctrl *node_shutoff = NULL;

	ch = &module->channel[CAM_CH_CAP];
	hw = module->grp->hw_info;
	shutoff_cnt = atomic_read(&module->capture_frames_dcam);
	node_shutoff = &pipeline_shutoff->node_shutoff;
	node_shutoff->cmd = CAM_NODE_SHUTOFF_CONFIG;
	raw_port_id = ch->raw_port_id;

	if (module->cam_uinfo.is_4in1 || module->cam_uinfo.dcam_slice_mode) {
		dcam_port_id = dcamonline_pathid_convert_to_portid(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
		if (dcam_port_id >= PORT_DCAM_OUT_MAX) {
			pr_err("fail to get port id\n");
			return 0;
		}
		pipeline_shutoff->node_type = CAM_NODE_TYPE_DCAM_ONLINE;
		CAM_NODE_SHUTOFF_PARAM_INIT(pipeline_shutoff->node_shutoff);
		atomic_set(&node_shutoff->outport_shutoff[dcam_port_id].cap_cnt, shutoff_cnt);
		node_shutoff->outport_shutoff[dcam_port_id].port_id = dcam_port_id;
		node_shutoff->outport_shutoff[dcam_port_id].shutoff_scene = SHUTOFF_SINGLE_PORT_NONZSL;
		node_shutoff->outport_shutoff[dcam_port_id].shutoff_type = SHUTOFF_PAUSE;
		return 1;
	}

	if (module->cam_uinfo.alg_type == ALG_TYPE_CAP_AINR && module->cap_scene == CAPTURE_AINR
		&& !module->cam_uinfo.param_frame_sync) {
		pipeline_shutoff->node_type = CAM_NODE_TYPE_DCAM_ONLINE;
		CAM_NODE_SHUTOFF_PARAM_INIT(pipeline_shutoff->node_shutoff);
		atomic_set(&node_shutoff->outport_shutoff[PORT_FULL_OUT].cap_cnt, shutoff_cnt + 1);
		node_shutoff->outport_shutoff[PORT_FULL_OUT].port_id = PORT_FULL_OUT;
		node_shutoff->outport_shutoff[PORT_FULL_OUT].shutoff_scene = SHUTOFF_MULTI_PORT_SWITCH;
		node_shutoff->outport_shutoff[PORT_FULL_OUT].shutoff_type = SHUTOFF_PAUSE;
		atomic_set(&node_shutoff->outport_shutoff[raw_port_id].cap_cnt, shutoff_cnt + 1);
		node_shutoff->outport_shutoff[raw_port_id].port_id = raw_port_id;
		node_shutoff->outport_shutoff[raw_port_id].shutoff_scene = SHUTOFF_MULTI_PORT_SWITCH;
		node_shutoff->outport_shutoff[raw_port_id].shutoff_type = SHUTOFF_RESUME;
		if (raw_port_id == PORT_RAW_OUT && ch->offline_need_dcam_raw) {
			CAM_NODE_SHUTOFF_PARAM_INIT(pipeline_shutoff->node_shutoff);
			atomic_set(&node_shutoff->outport_shutoff[raw_port_id].cap_cnt, shutoff_cnt);
			node_shutoff->outport_shutoff[raw_port_id].port_id = raw_port_id;
			node_shutoff->outport_shutoff[raw_port_id].raw_switch_en = CAM_ENABLE;
			node_shutoff->outport_shutoff[raw_port_id].cur_raw_sel = ORI_RAW_SRC_SEL;
				node_shutoff->outport_shutoff[raw_port_id].pre_raw_sel = BPC_RAW_SRC_SEL;
			pr_info("shutoff: port %d raw sel change! old %d new %d\n", raw_port_id,
				node_shutoff->outport_shutoff[raw_port_id].pre_raw_sel,
				node_shutoff->outport_shutoff[raw_port_id].cur_raw_sel);
		}
		if (raw_port_id == PORT_FULL_OUT) {
			CAM_NODE_SHUTOFF_PARAM_INIT(pipeline_shutoff->node_shutoff);
			atomic_set(&node_shutoff->outport_shutoff[raw_port_id].cap_cnt, shutoff_cnt);
			node_shutoff->outport_shutoff[raw_port_id].port_id = raw_port_id;
			node_shutoff->outport_shutoff[raw_port_id].raw_switch_en = CAM_ENABLE;
			node_shutoff->outport_shutoff[raw_port_id].cur_raw_sel = ORI_RAW_SRC_SEL;
			node_shutoff->outport_shutoff[raw_port_id].pre_raw_sel = PROCESS_RAW_SRC_SEL;
			pr_debug("shutoff: port %d raw sel change! old %d new %d\n", raw_port_id,
				node_shutoff->outport_shutoff[raw_port_id].pre_raw_sel,
				node_shutoff->outport_shutoff[raw_port_id].cur_raw_sel);
		}
		pr_debug("AINR set shutoff\n");
		return 1;
	}

	if (module->cam_uinfo.alg_type == ALG_TYPE_CAP_XDR && module->cap_scene != CAPTURE_RAWALG) {
		pipeline_shutoff->node_type = CAM_NODE_TYPE_DCAM_ONLINE;
		CAM_NODE_SHUTOFF_PARAM_INIT(pipeline_shutoff->node_shutoff);
		atomic_set(&node_shutoff->outport_shutoff[PORT_FULL_OUT].cap_cnt, shutoff_cnt + 1);
		if (module->capture_type == CAM_CAPTURE_STOP) {
			node_shutoff->outport_shutoff[PORT_FULL_OUT].port_id = PORT_FULL_OUT;
			node_shutoff->outport_shutoff[PORT_FULL_OUT].shutoff_scene = SHUTOFF_SINGLE_PORT_NORMAL;
			node_shutoff->outport_shutoff[PORT_FULL_OUT].shutoff_type = SHUTOFF_PAUSE;
			if (module->cam_uinfo.raw_zsl_num == 0 && !module->cam_uinfo.need_dcam_raw) {
				atomic_set(&node_shutoff->outport_shutoff[raw_port_id].cap_cnt, shutoff_cnt + 1);
				node_shutoff->outport_shutoff[raw_port_id].port_id = raw_port_id;
				node_shutoff->outport_shutoff[raw_port_id].shutoff_scene = SHUTOFF_SINGLE_PORT_NORMAL;
				node_shutoff->outport_shutoff[raw_port_id].shutoff_type = SHUTOFF_RESUME;
				pr_debug("shutoff: full pause, raw resume\n");
			}
		} else {
			node_shutoff->outport_shutoff[PORT_FULL_OUT].port_id = PORT_FULL_OUT;
			node_shutoff->outport_shutoff[PORT_FULL_OUT].shutoff_scene = SHUTOFF_SINGLE_PORT_NORMAL;
			node_shutoff->outport_shutoff[PORT_FULL_OUT].shutoff_type = SHUTOFF_RESUME;
			if (module->cam_uinfo.raw_zsl_num == 0 && !module->cam_uinfo.need_dcam_raw) {
				atomic_set(&node_shutoff->outport_shutoff[raw_port_id].cap_cnt, shutoff_cnt + 1);
				node_shutoff->outport_shutoff[raw_port_id].port_id = raw_port_id;
				node_shutoff->outport_shutoff[raw_port_id].shutoff_scene = SHUTOFF_SINGLE_PORT_NORMAL;
				node_shutoff->outport_shutoff[raw_port_id].shutoff_type = SHUTOFF_PAUSE;
				pr_debug("shutoff: raw pause, full resume\n");
			}
		}

		if (module->cap_scene == CAPTURE_AINR && raw_port_id == PORT_RAW_OUT) {
			CAM_NODE_SHUTOFF_PARAM_INIT(pipeline_shutoff->node_shutoff);
			atomic_set(&node_shutoff->outport_shutoff[raw_port_id].cap_cnt, shutoff_cnt);
			node_shutoff->outport_shutoff[raw_port_id].port_id = raw_port_id;
			node_shutoff->outport_shutoff[raw_port_id].raw_switch_en = CAM_ENABLE;
			node_shutoff->outport_shutoff[raw_port_id].cur_raw_sel = ORI_RAW_SRC_SEL;
				node_shutoff->outport_shutoff[raw_port_id].pre_raw_sel = BPC_RAW_SRC_SEL;
			pr_debug("shutoff: port %d raw sel change! old %d new %d\n", raw_port_id,
				node_shutoff->outport_shutoff[raw_port_id].pre_raw_sel,
				node_shutoff->outport_shutoff[raw_port_id].cur_raw_sel);
		}
		return 1;
	}

	return 0;
}

static void camcore_channel_default_param_set(struct channel_context *channel, uint32_t ch_id)
{
	channel->ch_id = ch_id;
	channel->dcam_port_id = -1;
	channel->aux_dcam_port_id = -1;
	channel->ch_uinfo.dcam_raw_fmt = -1;
	channel->ch_uinfo.sensor_raw_fmt = -1;
	channel->blk_pm.idx = DCAM_HW_CONTEXT_MAX;
	channel->dump_ee_buf_cnt = 0;
	channel->xtm_zoom.isp_scaler_crop_need = 0;
	cam_block_dcam_init(&channel->blk_pm);
	init_completion(&channel->alloc_com);
	init_completion(&channel->stream_on_buf_com);
	init_completion(&channel->fast_stop);
	spin_lock_init(&channel->lastest_zoom_lock);
	spin_lock_init(&channel->nonzsl_pre.lastest_zoom_lock);
}

static int camcore_module_init(struct camera_module *module)
{
	int ch = 0, ret = 0;
	struct channel_context *channel = NULL;

	pr_info("sprd_img: camera dev %d init start!\n", module->idx);

	atomic_set(&module->state, CAM_INIT);
	mutex_init(&module->ioctl_lock);
	mutex_init(&module->zoom_lock);
	init_completion(&module->frm_com);
	init_completion(&module->img_com);
	init_completion(&module->postproc_done);

	module->capture_type = CAM_CAPTURE_STOP;
	module->raw_cap_fetch_fmt = CAM_FORMAT_MAX;
	module->attach_sensor_id = SPRD_SENSOR_ID_MAX + 1;
	module->flash_core_handle = cam_flash_handle_get(module->idx);

	for (ch = 0; ch < CAM_CH_MAX; ch++) {
		channel = &module->channel[ch];
		mutex_init(&module->buf_lock[ch]);
		camcore_channel_default_param_set(channel, ch);
	}

	camcore_timer_init(&module->cam_timer, (unsigned long)module);
	/* no need release buffer, only release camera_frame */
	CAM_QUEUE_INIT(&module->frm_queue, CAM_FRAME_Q_LEN, cam_queue_empty_frame_put);
	CAM_QUEUE_INIT(&module->img_queue, CAM_IMG_Q_LEN, cam_queue_empty_frame_put);
	CAM_QUEUE_INIT(&module->alloc_queue, CAM_ALLOC_Q_LEN, cam_queue_empty_frame_put);

	pr_info("module[%d] init OK %p!\n", module->idx, module);

	return ret;
}

static int camcore_module_deinit(struct camera_module *module)
{
	int ch = 0;
	struct channel_context *channel = NULL;

	cam_flash_handle_put(module->flash_core_handle);
	CAM_QUEUE_CLEAN(&module->frm_queue, struct cam_frame, list);
	CAM_QUEUE_CLEAN(&module->alloc_queue, struct cam_frame, list);
	CAM_QUEUE_CLEAN(&module->img_queue, struct cam_frame, list);

	for (ch = 0; ch < CAM_CH_MAX; ch++) {
		channel = &module->channel[ch];
		mutex_destroy(&module->buf_lock[channel->ch_id]);
	}
	mutex_destroy(&module->zoom_lock);
	mutex_destroy(&module->ioctl_lock);
	return 0;
}

static int camcore_postproc_param_get(struct camera_module *module, struct cam_postproc_param *postproc_param,
	unsigned long arg)
{
	int ret = 0;
	uint32_t i = 0, reserved[4] = {0};
	struct cam_frame *pframe = NULL;
	struct sprd_img_postproc_param __user *uparam = NULL;
	struct cam_frame *pfrm[4] = {NULL, NULL, NULL, NULL};

	if (!module || !postproc_param) {
		pr_err("fail to get input handle:%p, %p.\n", module, postproc_param);
		return -EFAULT;
	}

	uparam = (struct sprd_img_postproc_param __user *)arg;
	ret |= get_user(postproc_param->fid, &uparam->index);
	ret |= get_user(postproc_param->scene_mode, &uparam->scene_mode);
	ret |= get_user(postproc_param->src_imgfmt, &uparam->src_imgfmt);
	ret |= get_user(postproc_param->dst_imgfmt, &uparam->dst_imgfmt);
	ret |= get_user(postproc_param->dst_size.w, &uparam->dst_size.w);
	ret |= get_user(postproc_param->dst_size.h, &uparam->dst_size.h);
	ret |= get_user(reserved[0], &uparam->reserved[0]);
	ret |= get_user(reserved[1], &uparam->reserved[1]);
	ret |= get_user(reserved[2], &uparam->reserved[2]);
	ret |= get_user(reserved[3], &uparam->reserved[3]);
	if (ret) {
		pr_err("fail to copy_from_user\n");
		return -EFAULT;
	}

	if (postproc_param->scene_mode == CAM_POSTPROC_VID_NOISE_RD ||
		module->cam_uinfo.alg_type == ALG_TYPE_VID_NR) {
		postproc_param->ch = &module->channel[CAM_CH_PRE];
		ret |= get_user(postproc_param->blkpm_ptr, &uparam->blkpm_ptr);
		if (ret) {
			pr_err("fail to get postproc blk param addr\n");
			return -EFAULT;
		}
	} else
		postproc_param->ch = &module->channel[CAM_CH_CAP];

	for (i = 0; i < SPRD_IMG_POSTPROC_BUF_CNT; i++) {
		pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		if (!pframe) {
			pr_err("fail to get frame\n");
			return -EFAULT;
		}
		ret |= get_user(pframe->common.buf.mfd, &uparam->fd_array[i]);
		if (pframe->common.buf.mfd == 0) {
			pr_warn("warning: invalid buffer from user\n");
			cam_queue_empty_frame_put(pframe);
			continue;
		}
		pframe->common.fid = postproc_param->fid;
		pframe->common.sensor_time.tv_sec = reserved[0];
		pframe->common.sensor_time.tv_usec = reserved[1];
		pframe->common.boot_sensor_time = reserved[3];
		pframe->common.boot_sensor_time = (pframe->common.boot_sensor_time << 32) | reserved[2];
		pframe->common.buf.type = CAM_BUF_USER;
		pframe->common.link_from.node_type = CAM_NODE_TYPE_USER;
		pframe->common.link_from.node_id = CAM_LINK_DEFAULT_NODE_ID;
		if (postproc_param->scene_mode == CAM_POSTPROC_VID_NOISE_RD) {
			pframe->common.width = postproc_param->ch->ch_uinfo.dst_size.w;
			pframe->common.height = postproc_param->ch->ch_uinfo.dst_size.h;
		} else {
			if (module->cam_uinfo.alg_type == ALG_TYPE_VID_NR) {
				pframe->common.width = postproc_param->dst_size.w;
				pframe->common.height  = postproc_param->dst_size.h;
			} else {
				pframe->common.width = postproc_param->ch->ch_uinfo.src_size.w;
				pframe->common.height = postproc_param->ch->ch_uinfo.src_size.h;
			}
		}
		ret |= get_user(pframe->common.buf.addr_vir[0], &uparam->frame_addr_vir_array[i].y);
		ret |= get_user(pframe->common.buf.addr_vir[1], &uparam->frame_addr_vir_array[i].u);
		ret |= get_user(pframe->common.buf.addr_vir[2], &uparam->frame_addr_vir_array[i].v);
		if (ret) {
			pr_err("fail to copy_from_user\n");
			cam_queue_empty_frame_put(pframe);
			return -EFAULT;
		}
		pframe->common.channel_id = postproc_param->ch->ch_id;
		pframe->common.buf.status = CAM_BUF_ALLOC;
		pfrm[i] = pframe;
	}

	if (postproc_param->scene_mode == CAM_POSTPROC_VID_NOISE_RD) {
		postproc_param->src_frm = pfrm[0];
		postproc_param->dst_frm = pfrm[1];
		postproc_param->dst_frm->common.proc_mode = CAM_POSTPROC_SERIAL;
		postproc_param->node_type = CAM_NODE_TYPE_PYR_DEC;
		postproc_param->postproc_mode = CAM_POSTPROC_SERIAL;
		postproc_param->need_cfg_blkpm = 1;
		postproc_param->need_cfg_dec = 1;
		postproc_param->need_cfg_isp = 1;
		postproc_param->need_update_zoom = 1;
		postproc_param->isp_port_id = PORT_VID_OUT;
		postproc_param->isp_node_id = ISP_NODE_MODE_CAP_ID;
	} else {
		postproc_param->src_frm = pfrm[0];
		postproc_param->mid_frm = pfrm[1];
		postproc_param->dst_frm = pfrm[2];
		if (postproc_param->scene_mode == CAM_POSTPROC_CAP_PROC_RAW) {
			postproc_param->node_type = CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW;
			postproc_param->mid_frm->common.irq_property = CAM_FRAME_PROCESS_RAW;
			postproc_param->dcam_port_id = postproc_param->ch->aux_raw_port_id;
			postproc_param->need_cfg_dcam = 1;
		} else {
			postproc_param->node_type = CAM_NODE_TYPE_DCAM_OFFLINE;
			if (module->cam_uinfo.alg_type != ALG_TYPE_VID_NR) {
				postproc_param->mid_frm->common.irq_property = CAM_FRAME_FDRH;
				postproc_param->dcam_port_id = postproc_param->ch->aux_dcam_port_id;
				postproc_param->need_cfg_dcam = 1;
				postproc_param->need_cfg_isp = 1;
				postproc_param->need_cfg_zoom = 1;
				if (cam_pipeline_have_node_id(postproc_param->ch->pipeline_handle,
					CAM_NODE_TYPE_ISP_OFFLINE, ISP_NODE_MODE_OFFLINE_CAP_ID))
					postproc_param->isp_node_id = ISP_NODE_MODE_OFFLINE_CAP_ID;
				else
					postproc_param->isp_node_id = ISP_NODE_MODE_CAP_ID;
				pr_debug("need dcam raw %d %d\n", module->cam_uinfo.need_dcam_raw, module->cap_scene);
				if (module->cam_uinfo.need_dcam_raw && module->cap_scene == CAPTURE_AINR &&
					!module->grp->hw_info->ip_isp->isphw_abt->fetch_raw_support) {
					postproc_param->icap_dump_frm = pfrm[3];
					postproc_param->need_cfg_icap = 1;
					if (!pfrm[3])
						postproc_param->need_cfg_icap = 0;
					else
						pfrm[3]->common.channel_id = CAM_CH_RAW;
				}
			} else {
				postproc_param->need_update_zoom = 1;
				postproc_param->need_cfg_zoom = 1;
			}
		}
		postproc_param->postproc_mode = CAM_POSTPROC_PARALLEL;
		postproc_param->isp_port_id = postproc_param->ch->isp_port_id;
	}
	pr_debug("scene_mode %d cfg_dcam %d cfg_isp %d zoom %d %d, node type %d\n",
		postproc_param->scene_mode, postproc_param->need_cfg_dcam, postproc_param->need_cfg_isp,
		postproc_param->need_cfg_zoom, postproc_param->need_update_zoom, postproc_param->node_type);

	return ret;
}

static void camcore_postproc_param_put(struct cam_postproc_param *postproc_param)
{
	if (postproc_param->src_frm)
		cam_queue_empty_frame_put(postproc_param->src_frm);

	if (postproc_param->mid_frm)
		cam_queue_empty_frame_put(postproc_param->mid_frm);

	if (postproc_param->dst_frm)
		cam_queue_empty_frame_put(postproc_param->dst_frm);
}

#define CAM_RAWCAP
#include "cam_rawcap.c"
#undef CAM_RAWCAP

#define CAM_IOCTL_LAYER
#include "cam_ioctl.c"
#undef CAM_IOCTL_LAYER

static struct cam_ioctl_cmd ioctl_cmds_table[] = {
	[_IOC_NR(SPRD_IMG_IO_SET_MODE)]             = {SPRD_IMG_IO_SET_MODE,             camioctl_mode_set},
	[_IOC_NR(SPRD_IMG_IO_SET_CAP_SKIP_NUM)]     = {SPRD_IMG_IO_SET_CAP_SKIP_NUM,     camioctl_cap_skip_num_set},
	[_IOC_NR(SPRD_IMG_IO_SET_SENSOR_SIZE)]      = {SPRD_IMG_IO_SET_SENSOR_SIZE,      camioctl_sensor_size_set},
	[_IOC_NR(SPRD_IMG_IO_SET_SENSOR_TRIM)]      = {SPRD_IMG_IO_SET_SENSOR_TRIM,      camioctl_sensor_trim_set},
	[_IOC_NR(SPRD_IMG_IO_SET_FRM_ID_BASE)]      = {SPRD_IMG_IO_SET_FRM_ID_BASE,      camioctl_frame_id_base_set},
	[_IOC_NR(SPRD_IMG_IO_SET_CROP)]             = {SPRD_IMG_IO_SET_CROP,             camioctl_crop_set},
	[_IOC_NR(SPRD_IMG_IO_SET_FLASH)]            = {SPRD_IMG_IO_SET_FLASH,            camioctl_flash_set},
	[_IOC_NR(SPRD_IMG_IO_SET_OUTPUT_SIZE)]      = {SPRD_IMG_IO_SET_OUTPUT_SIZE,      camioctl_output_size_set},
	[_IOC_NR(SPRD_IMG_IO_SET_SENSOR_IF)]        = {SPRD_IMG_IO_SET_SENSOR_IF,        camioctl_sensor_if_set},
	[_IOC_NR(SPRD_IMG_IO_SET_FRAME_ADDR)]       = {SPRD_IMG_IO_SET_FRAME_ADDR,       camioctl_frame_addr_set},
	[_IOC_NR(SPRD_IMG_IO_PATH_FRM_DECI)]        = {SPRD_IMG_IO_PATH_FRM_DECI,        camioctl_frm_deci_set},
	[_IOC_NR(SPRD_IMG_IO_PATH_PAUSE)]           = {SPRD_IMG_IO_PATH_PAUSE,           camioctl_path_pause},
	[_IOC_NR(SPRD_IMG_IO_PATH_RESUME)]          = {SPRD_IMG_IO_PATH_RESUME,          camioctl_path_resume},
	[_IOC_NR(SPRD_IMG_IO_STREAM_ON)]            = {SPRD_IMG_IO_STREAM_ON,            camioctl_stream_on},
	[_IOC_NR(SPRD_IMG_IO_STREAM_OFF)]           = {SPRD_IMG_IO_STREAM_OFF,           camioctl_stream_off},
	[_IOC_NR(SPRD_IMG_IO_STREAM_PAUSE)]         = {SPRD_IMG_IO_STREAM_PAUSE,         camioctl_stream_pause},
	[_IOC_NR(SPRD_IMG_IO_STREAM_RESUME)]        = {SPRD_IMG_IO_STREAM_RESUME,        camioctl_stream_resume},
	[_IOC_NR(SPRD_IMG_IO_GET_FMT)]              = {SPRD_IMG_IO_GET_FMT,              camioctl_fmt_get},
	[_IOC_NR(SPRD_IMG_IO_GET_CH_ID)]            = {SPRD_IMG_IO_GET_CH_ID,            camioctl_ch_id_get},
	[_IOC_NR(SPRD_IMG_IO_GET_TIME)]             = {SPRD_IMG_IO_GET_TIME,             camioctl_time_get},
	[_IOC_NR(SPRD_IMG_IO_CHECK_FMT)]            = {SPRD_IMG_IO_CHECK_FMT,            camioctl_fmt_check},
	[_IOC_NR(SPRD_IMG_IO_SET_SHRINK)]           = {SPRD_IMG_IO_SET_SHRINK,           camioctl_shrink_set},
	[_IOC_NR(SPRD_IMG_IO_CFG_FLASH)]            = {SPRD_IMG_IO_CFG_FLASH,            camioctl_flash_cfg},
	[_IOC_NR(SPRD_IMG_IO_GET_IOMMU_STATUS)]     = {SPRD_IMG_IO_GET_IOMMU_STATUS,     camioctl_iommu_status_get},
	[_IOC_NR(SPRD_IMG_IO_START_CAPTURE)]        = {SPRD_IMG_IO_START_CAPTURE,        camioctl_capture_start},
	[_IOC_NR(SPRD_IMG_IO_STOP_CAPTURE)]         = {SPRD_IMG_IO_STOP_CAPTURE,         camioctl_capture_stop},
	[_IOC_NR(SPRD_IMG_IO_DCAM_PATH_SIZE)]       = {SPRD_IMG_IO_DCAM_PATH_SIZE,       camioctl_dcam_path_size},
	[_IOC_NR(SPRD_IMG_IO_SET_SENSOR_MAX_SIZE)]  = {SPRD_IMG_IO_SET_SENSOR_MAX_SIZE,  camioctl_sensor_max_size_set},
	[_IOC_NR(SPRD_ISP_IO_SET_STATIS_BUF)]       = {SPRD_ISP_IO_SET_STATIS_BUF,       camioctl_statis_buf_set},
	[_IOC_NR(SPRD_ISP_IO_CFG_PARAM)]            = {SPRD_ISP_IO_CFG_PARAM,            camioctl_param_cfg},
	[_IOC_NR(SPRD_ISP_IO_RAW_CAP)]              = {SPRD_ISP_IO_RAW_CAP,              camioctl_raw_proc},
	[_IOC_NR(SPRD_IMG_IO_GET_DCAM_RES)]         = {SPRD_IMG_IO_GET_DCAM_RES,         camioctl_cam_res_get},
	[_IOC_NR(SPRD_IMG_IO_PUT_DCAM_RES)]         = {SPRD_IMG_IO_PUT_DCAM_RES,         camioctl_cam_res_put},
	[_IOC_NR(SPRD_IMG_IO_SET_FUNCTION_MODE)]    = {SPRD_IMG_IO_SET_FUNCTION_MODE,    camioctl_function_mode_set},
	[_IOC_NR(SPRD_IMG_IO_GET_FLASH_INFO)]       = {SPRD_IMG_IO_GET_FLASH_INFO,       camioctl_flash_get},
	[_IOC_NR(SPRD_IMG_IO_EBD_CONTROL)]          = {SPRD_IMG_IO_EBD_CONTROL,          camioctl_ebd_control},
	[_IOC_NR(SPRD_IMG_IO_SET_4IN1_ADDR)]        = {SPRD_IMG_IO_SET_4IN1_ADDR,        camioctl_4in1_raw_addr_set},
	[_IOC_NR(SPRD_IMG_IO_4IN1_POST_PROC)]       = {SPRD_IMG_IO_4IN1_POST_PROC,       camioctl_4in1_post_proc},
	[_IOC_NR(SPRD_IMG_IO_SET_CAM_SECURITY)]     = {SPRD_IMG_IO_SET_CAM_SECURITY,     camioctl_cam_security_set},
	[_IOC_NR(SPRD_IMG_IO_GET_PATH_RECT)]        = {SPRD_IMG_IO_GET_PATH_RECT,        camioctl_path_rect_get},
	[_IOC_NR(SPRD_IMG_IO_SET_3DNR_MODE)]        = {SPRD_IMG_IO_SET_3DNR_MODE,        camioctl_3dnr_mode_set},
	[_IOC_NR(SPRD_IMG_IO_SET_AUTO_3DNR_MODE)]   = {SPRD_IMG_IO_SET_AUTO_3DNR_MODE,   camioctl_auto_3dnr_mode_set},
	[_IOC_NR(SPRD_IMG_IO_CAPABILITY)]           = {SPRD_IMG_IO_CAPABILITY,           camioctl_capability_get},
	[_IOC_NR(SPRD_IMG_IO_POST_FDR)]             = {SPRD_IMG_IO_POST_FDR,             camioctl_cam_post_proc},
	[_IOC_NR(SPRD_IMG_IO_CAM_TEST)]             = {SPRD_IMG_IO_CAM_TEST,             camioctl_cam_test},
	[_IOC_NR(SPRD_IMG_IO_DCAM_SWITCH)]          = {SPRD_IMG_IO_DCAM_SWITCH,          camioctl_csi_switch},
	[_IOC_NR(SPRD_IMG_IO_GET_SCALER_CAP)]       = {SPRD_IMG_IO_GET_SCALER_CAP,       camioctl_scaler_capability_get},
	[_IOC_NR(SPRD_IMG_IO_GET_DWARP_HW_CAP)]     = {SPRD_IMG_IO_GET_DWARP_HW_CAP,     camioctl_dewarp_hw_capability_get},
	[_IOC_NR(SPRD_IMG_IO_SET_DWARP_OTP)]        = {SPRD_IMG_IO_SET_DWARP_OTP,        camioctl_dewarp_otp_set},
	[_IOC_NR(SPRD_IMG_IO_SET_LONGEXP_CAP)]      = {SPRD_IMG_IO_SET_LONGEXP_CAP,      camioctl_longexp_mode_set},
	[_IOC_NR(SPRD_IMG_IO_SET_MUL_MAX_SN_SIZE)]  = {SPRD_IMG_IO_SET_MUL_MAX_SN_SIZE,  camioctl_mul_max_sensor_size_set},
	[_IOC_NR(SPRD_IMG_IO_SET_CAP_ZSL_INFO)]     = {SPRD_IMG_IO_SET_CAP_ZSL_INFO,     camioctl_cap_zsl_info_set},
	[_IOC_NR(SPRD_IMG_IO_SET_DCAM_RAW_FMT)]     = {SPRD_IMG_IO_SET_DCAM_RAW_FMT,     camioctl_dcam_raw_fmt_set},
	[_IOC_NR(SPRD_IMG_IO_SET_KEY)]              = {SPRD_IMG_IO_SET_KEY,              camioctl_key_set},
	[_IOC_NR(SPRD_IMG_IO_SET_960FPS_PARAM)]     = {SPRD_IMG_IO_SET_960FPS_PARAM,     camioctl_960fps_param_set},
	[_IOC_NR(SPRD_IMG_IO_CFG_PARAM_STATUS)]     = {SPRD_IMG_IO_CFG_PARAM_STATUS,     camioctl_cfg_param_start_end},
	[_IOC_NR(SPRD_IMG_IO_SET_PRE_RAW_FLAG)]     = {SPRD_IMG_IO_SET_PRE_RAW_FLAG,     camioctl_pre_raw_flag_set},
	[_IOC_NR(SPRD_IMG_IO_PUT_OUTPUT_SIZE)]      = {SPRD_IMG_IO_PUT_OUTPUT_SIZE,      camioctl_output_size_put},
};

static long camcore_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int ret = 0;
	struct camera_module *module = NULL;
	struct cam_ioctl_cmd *ioctl_cmd_p = NULL;
	int nr = _IOC_NR(cmd);

	pr_debug("cam ioctl, cmd:0x%x, cmdnum %d\n", cmd, nr);

	module = (struct camera_module *)file->private_data;
	if (!module) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	if (unlikely(!(nr >= 0 && nr < ARRAY_SIZE(ioctl_cmds_table)))) {
		pr_info("invalid cmd: 0x%xn", cmd);
		return -EINVAL;
	}

	ioctl_cmd_p = &ioctl_cmds_table[nr];
	if (unlikely((ioctl_cmd_p->cmd != cmd) || (ioctl_cmd_p->cmd_proc == NULL))) {
		pr_debug("unsupported cmd_k: 0x%x, cmd_u: 0x%x, nr: %d\n", ioctl_cmd_p->cmd, cmd, nr);
		return 0;
	}

	mutex_lock(&module->ioctl_lock);
	/* ioctl from user may cfg hw register when start recovery, need lock
	 * this operatate to avoid bus hang issue when axi reset in recovery.
	*/
	down_read(&module->grp->switch_recovery_lock);
	if (cmd == SPRD_IMG_IO_SET_KEY || module->private_key == 1) {
		ret = ioctl_cmd_p->cmd_proc(module, arg);
		if (ret) {
			pr_debug("fail to ioctl cmd:%x, nr:%d, func %ps\n", cmd, nr, ioctl_cmd_p->cmd_proc);
			goto exit;
		}
	} else
		pr_err("cam %d fail to get ioctl permission %d\n", module->idx, module->private_key);

	pr_debug("cam id:%d, %ps, done!\n", module->idx, ioctl_cmd_p->cmd_proc);
exit:
	up_read(&module->grp->switch_recovery_lock);
	mutex_unlock(&module->ioctl_lock);

	return ret;
}

#ifdef CONFIG_COMPAT
static long camcore_ioctl_compat(struct file *file,
	unsigned int cmd, unsigned long arg)
{

	long ret = 0L;
	struct camera_module *module = NULL;
	void __user *data32 = compat_ptr(arg);

	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;

	module = (struct camera_module *)file->private_data;
	if (!module) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	module->compat_flag = 1;
	pr_debug("cmd [0x%x][%d]\n", cmd, _IOC_NR(cmd));

	switch (cmd) {
	case COMPAT_SPRD_ISP_IO_CFG_PARAM:
		ret = file->f_op->unlocked_ioctl(file, SPRD_ISP_IO_CFG_PARAM, (unsigned long)data32);
		break;
	case COMPAT_SPRD_IMG_IO_POST_FDR:;
		ret = file->f_op->unlocked_ioctl(file, SPRD_IMG_IO_POST_FDR, (unsigned long)data32);
		break;
	default:
		ret = file->f_op->unlocked_ioctl(file, cmd, (unsigned long)data32);
		break;
	}
	return ret;
}
#endif

static int camcore_recovery_proc(void *param)
{
	int ret = 0, i = 0, j = 0;
	uint32_t timer = 0;
	struct camera_group *grp = NULL;
	struct camera_module *module = NULL;
	struct channel_context *ch = NULL;
	struct cam_hw_info *hw = NULL;
	uint32_t switch_mode = CAM_CSI_RECOVERY_SWITCH;
	struct cam_hw_lbuf_info cam_lbuf_info = {0};
	struct cam_pipeline_cfg_param pipe_param = {0};
	struct dcam_online_start_param start_param = {0};
	struct cam_pipeline_cfg_param cfg_param = {0};
	struct dcam_pipe_dev *dcam_dev_handle = NULL;

	grp = (struct camera_group *)param;
	hw = grp->hw_info;

	pr_info("enter recovery\n");
	down_write(&grp->switch_recovery_lock);
	/* all avaiable dcam csi switch disconnect & stop */
	for (i = 0; i < CAM_COUNT; i++) {
		module = grp->module[i];
		if (module && (atomic_read(&module->state) == CAM_RUNNING)){
			if (!module->dcam_ctx_bind_state) {
				pr_warn("warning: module %d has been disconnected and unbinded already\n", module->idx);
				continue;
			}
			dcam_dev_handle = module->dcam_dev_handle;
			for (j = 0; j < CAM_CH_MAX; j++) {
				ch = &module->channel[j];
				if (ch->enable && ch->pipeline_handle) {
					enum dcam_stop_cmd stop_cmd = DCAM_RECOVERY;
					pipe_param.node_type = CAM_NODE_TYPE_DCAM_ONLINE;
					pipe_param.node_param.param = &stop_cmd;
					ret = ch->pipeline_handle->ops.streamoff(ch->pipeline_handle, &pipe_param);
					if (ret)
						pr_err("fail to stop module %d dcam online ret:%d\n", module->idx, ret);
				}
			}
			pr_info("modules %d recovery disconnect\n", module->idx);
			camcore_csi_switch_disconnect(module, switch_mode, DCAM_CSI_PAUSE);
		}
	}

	if (dcam_dev_handle)
		dcam_dev_handle->dcam_pipe_ops->recovery(dcam_dev_handle);

	/* all avaiable dcam reconfig & start */
	for (i = 0; i < CAM_COUNT; i++) {
		module = grp->module[i];
		if (module && (atomic_read(&module->state) == CAM_RUNNING)){
			if (!module->dcam_ctx_bind_state)
				continue;
			pr_info("modules %d recovery connect\n", module->idx);
			/* dcam online node stream on */
			for (j = 0; j < CAM_CH_MAX; j++) {
				ch = &module->channel[j];
				if (ch->enable && ch->pipeline_handle) {
					cam_lbuf_info.is_4in1 = module->cam_uinfo.is_4in1;
					cam_lbuf_info.line_w = module->cam_uinfo.sn_rect.w;
					cam_lbuf_info.is_offline = 0;
					start_param.lbuf_param = &cam_lbuf_info;
					cfg_param.node_type = CAM_NODE_TYPE_DCAM_ONLINE;
					cfg_param.node_param.param = &start_param;
					cfg_param.node_param.port_id = 0xffffffff;
					ret = ch->pipeline_handle->ops.streamon(ch->pipeline_handle, &cfg_param);
				}
			}
			atomic_set(&module->timeout_flag, 1);
			if (module->cam_uinfo.is_longexp)
				timer = CAMERA_LONGEXP_TIMEOUT;
			else
				timer = CAMERA_TIMEOUT;
			camcore_timer_start(&module->cam_timer, timer);
		}
	}
	atomic_set(&grp->recovery_state, CAM_RECOVERY_DONE);
	up_write(&grp->switch_recovery_lock);
	pr_info("cam recovery is finish\n");

	return ret;
}

void camcore_abnormal_reg_print(struct cam_hw_info *hw)
{
	int i = 0;
	uint32_t *buf = NULL;
	struct isphw_abnormal_regtab_size *size = NULL;

	if (!hw || !hw->abnormal_info.abnormal_record_buf ||
		atomic_read(&hw->abnormal_info.abnormal_record) == 0)
		return;

	if (atomic_inc_return(&hw->abnormal_info.is_print) != 1)
		return;

	size = &hw->abnormal_info.size_info;
	if (size->sum_buf_length == 0)
		return;

	buf = (uint32_t *)hw->abnormal_info.abnormal_record_buf;
	if (IS_DCAM_LITE(hw->abnormal_info.idx)) {
		pr_info("DCAM%d: Register list\n", hw->abnormal_info.idx);
		for (i = 0; i < size->dcamlite_tab_length; i += 5)
			pr_info("%x: %8x %8x %8x %8x\n",
				buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4]);

		buf = (uint32_t *)(hw->abnormal_info.abnormal_record_buf +
			size->dcamaxi_tab_offset);
		pr_info("DCAMAXI: Register list\n");
		for (i = 0; i < size->dcamliteaxi_tab_length; i += 5)
			pr_info("%x: %8x %8x %8x %8x\n",
				buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4]);

	} else {
		pr_info("DCAM%d: Register list\n", hw->abnormal_info.idx);
		for (i = 0; i < size->dcam_tab_length; i += 5)
			pr_info("%x: %8x %8x %8x %8x\n",
				buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4]);

		buf = (uint32_t *)(hw->abnormal_info.abnormal_record_buf +
			size->dcamaxi_tab_offset);
		pr_info("DCAMAXI: Register list\n");
		for (i = 0; i < size->dcamaxi_tab_length; i += 5)
			pr_info("%x: %8x %8x %8x %8x\n",
				buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4]);

		buf = (uint32_t *)(hw->abnormal_info.abnormal_record_buf +
			size->dcamfmcu_tab_offset);
		pr_info("DCAM FMCU: Register list\n");
		for (i = 0; i < size->dcamfmcu_tab_length; i += 5)
			pr_info("%x: %8x %8x %8x %8x\n",
				buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4]);
	}

	buf = (uint32_t *)(hw->abnormal_info.abnormal_record_buf + size->isp_tab_offset);
	pr_info("ISP: Register list\n");
	for (i = 0; i < size->isp_tab_length; i += 5)
		pr_info("%x: %8x %8x %8x %8x\n",
			buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4]);

	buf = (uint32_t *)(hw->abnormal_info.abnormal_record_buf + size->ispmmu_tab_offset);
	pr_info("ISP IOMMU: Register list\n");
	for (i = 0; i < size->ispmmu_tab_length; i += 5)
		pr_info("%x: %8x %8x %8x %8x\n",
			buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4]);

	atomic_set(&hw->abnormal_info.is_print, 0);
	atomic_set(&hw->abnormal_info.abnormal_record, 0);
}

static ssize_t camcore_read(struct file *file, char __user *u_data,
		size_t cnt, loff_t *cnt_ret)
{
	int ret = 0;
	uint32_t i = 0, superzoom_val = 0, rawpath_id = 0;
	int64_t sec = 0, usec = 0;
	timespec cur_ts = {0};
	struct cam_frame *pframe = NULL;
	struct sprd_img_read_op read_op = {0};
	struct cam_hw_info *hw = NULL;
	struct camera_module *module = NULL;
	struct channel_context *pchannel = NULL;
	struct sprd_img_path_capability *cap = NULL;
	struct dcam_online_node *dcam_online_node_dev = NULL;
	struct cam_buf_pool_id recycle_pool = {CAM_BUF_POOL_ABNORAM_RECYCLE, 0};

	module = (struct camera_module *)file->private_data;
	if (!module) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	hw = module->grp->hw_info;
	if (!hw) {
		pr_err("fail to get hw ops.\n");
		return -EFAULT;
	}

	if (cnt != sizeof(struct sprd_img_read_op)) {
		pr_err("fail to img read, cnt %zd read_op %d\n", cnt,
			(int32_t)sizeof(struct sprd_img_read_op));
		return -EIO;
	}

	if (copy_from_user(&read_op, (void __user *)u_data, cnt)) {
		pr_err("fail to get user info\n");
		return -EFAULT;
	}

	pr_debug("cam %d read cmd %d\n", module->idx, read_op.cmd);

	switch (read_op.cmd) {
	case SPRD_IMG_GET_SCALE_CAP:
		hw = module->grp->hw_info;
		for (i = 0; i < DCAM_ID_MAX; i++) {
			if (hw->ip_dcam[i]->dcamhw_abt && hw->ip_dcam[i]->dcamhw_abt->superzoom_support) {
				superzoom_val = 1;
				break;
			}
		}

		if (superzoom_val)
			read_op.parm.reserved[1] = 10;
		else
			read_op.parm.reserved[1] = 4;

		read_op.parm.reserved[0] = 4672;
		read_op.parm.reserved[2] = 4672;
		pr_debug("line threshold %d, sc factor %d, scaling %d.\n",
			read_op.parm.reserved[0],
			read_op.parm.reserved[1],
			read_op.parm.reserved[2]);
		break;
	case SPRD_IMG_GET_FRM_BUFFER:
	case SPRD_IMG_GET_IMG_BUFFER:
rewait:
		if (read_op.cmd == SPRD_IMG_GET_FRM_BUFFER) {
			memset(&read_op, 0, sizeof(struct sprd_img_read_op));
			while (1) {
				ret = wait_for_completion_interruptible(&module->frm_com);
				if (ret == 0) {
					break;
				} else if (ret == -ERESTARTSYS) {
					read_op.evt = IMG_SYS_BUSY;
					ret = 0;
					goto read_end;
				} else {
					pr_err("read frame buf, fail to down, %d\n", ret);
					return -EPERM;
				}
			}
			cam_buf_manager_buf_clear(&recycle_pool, (void *)module->grp->global_buf_manager);
			pframe = CAM_QUEUE_DEQUEUE(&module->frm_queue, struct cam_frame, list);
		} else {
			memset(&read_op, 0, sizeof(struct sprd_img_read_op));
			while (1) {
				ret = wait_for_completion_interruptible(&module->img_com);
				if (ret == 0) {
					break;
				} else if (ret == -ERESTARTSYS) {
					read_op.evt = IMG_SYS_BUSY;
					ret = 0;
					goto read_end;
				} else {
					pr_err("read img buf, fail to down, %d\n", ret);
					return -EPERM;
				}
			}
			cam_buf_manager_buf_clear(&recycle_pool, (void *)module->grp->global_buf_manager);
			pframe = CAM_QUEUE_DEQUEUE(&module->img_queue, struct cam_frame, list);
		}
		camcore_abnormal_reg_print(module->grp->hw_info);
		if (!pframe) {
			/* any exception happens or user trigger exit. */
			pr_info("No valid frame buffer. tx stop.\n");
			read_op.evt = IMG_TX_STOP;
		} else {
			if (pframe->common.evt == IMG_TX_DONE) {
				atomic_set(&module->timeout_flag, 0);
				os_adapt_time_get_ts(&cur_ts);
				sec = cur_ts.tv_sec - pframe->common.sensor_time.tv_sec;
				usec = (cur_ts.tv_nsec  / NSEC_PER_USEC) - pframe->common.sensor_time.tv_usec;
				pr_debug("ch %d, fid %d, cur_time %lld.%06ld, sof to farme done time: %lld.%06lld\n",
					pframe->common.channel_id, pframe->common.fid, cur_ts.tv_sec, cur_ts.tv_nsec / NSEC_PER_USEC, sec, usec);
				if ((pframe->common.irq_type == CAMERA_IRQ_4IN1_DONE)
					|| (pframe->common.irq_type == CAMERA_IRQ_IMG)
					|| (pframe->common.irq_type == CAMERA_IRQ_RAW_IMG)
					|| (pframe->common.irq_type == CAMERA_IRQ_RAW_BPC_IMG)) {
					atomic_set(&module->grp->recovery_state, CAM_RECOVERY_NONE);
					cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_MOVE_TO_ALLOC, CAM_BUF_IOMMUDEV_MAX);
					pchannel = &module->channel[pframe->common.channel_id];
					if (pframe->common.buf.mfd == module->reserved_buf_fd) {
						pr_info("get output buffer with reserved frame fd %d, ch %d\n",
							module->reserved_buf_fd, pchannel->ch_id);
						cam_queue_empty_frame_put(pframe);
						goto rewait;
					}
					read_op.parm.frame.channel_id = pframe->common.channel_id;
					read_op.parm.frame.index = pchannel->frm_base_id;
					read_op.parm.frame.frm_base_id = pchannel->frm_base_id;
					read_op.parm.frame.img_fmt = pframe->common.img_fmt;
				}
				read_op.evt = pframe->common.evt;
				read_op.parm.frame.irq_type = pframe->common.irq_type;
				read_op.parm.frame.irq_property = pframe->common.irq_property;
				read_op.parm.frame.length = pframe->common.width;
				read_op.parm.frame.height = pframe->common.height;
				read_op.parm.frame.real_index = pframe->common.fid;
				read_op.parm.frame.frame_id = pframe->common.fid;
				read_op.parm.frame.sec = pframe->common.sensor_time.tv_sec;
				read_op.parm.frame.usec = pframe->common.sensor_time.tv_usec;
				read_op.parm.frame.monoboottime = pframe->common.boot_sensor_time;
				read_op.parm.frame.yaddr_vir = pframe->common.buf.addr_vir[0];
				read_op.parm.frame.uaddr_vir = pframe->common.buf.addr_vir[1];
				read_op.parm.frame.vaddr_vir = pframe->common.buf.addr_vir[2];
				read_op.parm.frame.mfd = pframe->common.buf.mfd;
				read_op.parm.frame.yaddr = pframe->common.buf.offset[0];
				read_op.parm.frame.uaddr = pframe->common.buf.offset[1];
				read_op.parm.frame.vaddr = pframe->common.buf.offset[2];
				read_op.parm.frame.is_flash_status = pframe->common.is_flash_status;

				/* statis info */
				read_op.parm.frame.aem_info = pframe->common.aem_info;
				read_op.parm.frame.bayerhist_info = pframe->common.bayerhist_info;
				read_op.parm.frame.afm_info = pframe->common.afm_info;
				read_op.parm.frame.pdaf_info = pframe->common.pdaf_info;
				read_op.parm.frame.lscm_info = pframe->common.lscm_info;
				read_op.parm.frame.ltm_info = pframe->common.ltm_info;

				/* for statis buffer address below. */
				read_op.parm.frame.addr_offset = pframe->common.buf.offset[0];
				read_op.parm.frame.zoom_ratio = pframe->common.zoom_ratio;
				read_op.parm.frame.total_zoom = pframe->common.total_zoom;

				if (pframe->common.irq_property == IRQ_DCAM_SOF &&
					pframe->common.irq_type == CAMERA_IRQ_DONE)
					module->cur_frame_id = pframe->common.fid;

				pr_debug("Send usr buf %d ch %d ltm ch %d, evt %d, fid %d, buf_fd %d, time %06d.%06d\n",
					pframe->common.irq_type, read_op.parm.frame.channel_id,
					pframe->common.ltm_info.channel_id,
					read_op.evt, read_op.parm.frame.real_index,
					read_op.parm.frame.mfd,
					read_op.parm.frame.sec, read_op.parm.frame.usec);
			} else {
				pr_err("fail to get correct event %d\n", pframe->common.evt);
				hw->dcam_ioctl(hw, 0, DCAM_HW_CFG_CSI_REG_TRACE, module->grp->pdev);
				if (module->nodes_dev.dcam_online_node_dev) {
					dcam_online_node_dev = module->nodes_dev.dcam_online_node_dev;
					if (dcam_online_node_dev->hw_ctx_id != DCAM_HW_CONTEXT_MAX &&
						module->grp->camsec_cfg.camsec_mode == SEC_UNABLE) {
						hw->isp_ioctl(hw, ISP_HW_CFG_ABNORMAL_UEVENT,
							module->grp->pdev);
					}
				}
				read_op.evt = pframe->common.evt;
				read_op.parm.frame.irq_type = pframe->common.irq_type;
				read_op.parm.frame.irq_property = pframe->common.irq_property;
			}
			pr_debug("cam%d read frame, evt 0x%x irq %d, irq_property %d, ch 0x%x index %d mfd 0x%x\n",
				module->idx, read_op.evt, read_op.parm.frame.irq_type, read_op.parm.frame.irq_property, read_op.parm.frame.channel_id,
				read_op.parm.frame.real_index, read_op.parm.frame.mfd);
		}
		if (pframe) {
			if (pframe->common.irq_type != CAMERA_IRQ_4IN1_DONE) {
				cam_queue_empty_frame_put(pframe);
				break;
			}
		}
		break;
	case SPRD_IMG_GET_PATH_CAP:
		pr_debug("get path capbility\n");
		cap = &read_op.parm.capability;
		memset(cap, 0, sizeof(struct sprd_img_path_capability));
		cap->support_3dnr_mode = 1;
		cap->support_4in1 = 1;
		cap->count = 6;
		cap->path_info[CAM_CH_RAW].support_yuv = 0;
		cap->path_info[CAM_CH_RAW].support_raw = 1;
		cap->path_info[CAM_CH_RAW].support_jpeg = 0;
		cap->path_info[CAM_CH_RAW].support_scaling = 0;
		cap->path_info[CAM_CH_RAW].support_trim = 1;
		cap->path_info[CAM_CH_RAW].is_scaleing_path = 0;
		cap->path_info[CAM_CH_PRE].line_buf = ISP_WIDTH_MAX;
		cap->path_info[CAM_CH_PRE].support_yuv = 1;
		cap->path_info[CAM_CH_PRE].support_raw = 0;
		cap->path_info[CAM_CH_PRE].support_jpeg = 0;
		cap->path_info[CAM_CH_PRE].support_scaling = 1;
		cap->path_info[CAM_CH_PRE].support_trim = 1;
		cap->path_info[CAM_CH_PRE].is_scaleing_path = 0;
		cap->path_info[CAM_CH_CAP].line_buf = ISP_WIDTH_MAX;
		cap->path_info[CAM_CH_CAP].support_yuv = 1;
		cap->path_info[CAM_CH_CAP].support_raw = 0;
		cap->path_info[CAM_CH_CAP].support_jpeg = 0;
		cap->path_info[CAM_CH_CAP].support_scaling = 1;
		cap->path_info[CAM_CH_CAP].support_trim = 1;
		cap->path_info[CAM_CH_CAP].is_scaleing_path = 0;
		cap->path_info[CAM_CH_VID].line_buf = ISP_WIDTH_MAX;
		cap->path_info[CAM_CH_VID].support_yuv = 1;
		cap->path_info[CAM_CH_VID].support_raw = 0;
		cap->path_info[CAM_CH_VID].support_jpeg = 0;
		cap->path_info[CAM_CH_VID].support_scaling = 1;
		cap->path_info[CAM_CH_VID].support_trim = 1;
		cap->path_info[CAM_CH_VID].is_scaleing_path = 0;
		cap->path_info[CAM_CH_PRE_THM].line_buf = ISP_WIDTH_MAX;
		cap->path_info[CAM_CH_PRE_THM].support_yuv = 1;
		cap->path_info[CAM_CH_PRE_THM].support_raw = 0;
		cap->path_info[CAM_CH_PRE_THM].support_jpeg = 0;
		cap->path_info[CAM_CH_PRE_THM].support_scaling = 1;
		cap->path_info[CAM_CH_PRE_THM].support_trim = 1;
		cap->path_info[CAM_CH_PRE_THM].is_scaleing_path = 0;
		cap->path_info[CAM_CH_CAP_THM].line_buf = ISP_WIDTH_MAX;
		cap->path_info[CAM_CH_CAP_THM].support_yuv = 1;
		cap->path_info[CAM_CH_CAP_THM].support_raw = 0;
		cap->path_info[CAM_CH_CAP_THM].support_jpeg = 0;
		cap->path_info[CAM_CH_CAP_THM].support_scaling = 1;
		cap->path_info[CAM_CH_CAP_THM].support_trim = 1;
		cap->path_info[CAM_CH_CAP_THM].is_scaleing_path = 0;
		break;
	case SPRD_IMG_GET_DCAM_RAW_CAP:
		rawpath_id = camcore_dcampath_id_convert(hw->ip_dcam[0]->dcamhw_abt->dcam_raw_path_id);
		if (rawpath_id >= DCAM_PATH_ID_MAX) {
			pr_err("fail to get rawpath id\n");
			return -ENOMEM;
		}
		for (i = 0; i < DCAM_RAW_MAX; i++)
			read_op.parm.reserved[i] = hw->ip_dcam[0]->dcampath_abt[rawpath_id]->format[i];
		break;
	default:
		pr_err("fail to get valid cmd\n");
		return -EINVAL;
	}

read_end:
	if (copy_to_user((void __user *)u_data, &read_op, cnt))
		ret = -EFAULT;

	if (ret)
		return ret;

	return cnt;
}

static ssize_t camcore_write(struct file *file, const char __user *u_data,
		size_t cnt, loff_t *cnt_ret)
{
	int ret = 0;
	struct sprd_img_write_op write_op = {0};
	struct camera_module *module = NULL;

	module = (struct camera_module *)file->private_data;
	if (!module) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	if (cnt != sizeof(struct sprd_img_write_op)) {
		pr_err("fail to write, cnt %zd  write_op %d\n", cnt,
				(uint32_t)sizeof(struct sprd_img_write_op));
		return -EIO;
	}

	if (copy_from_user(&write_op, (void __user *)u_data, cnt)) {
		pr_err("fail to get user info\n");
		return -EFAULT;
	}

	switch (write_op.cmd) {
	case SPRD_IMG_STOP_DCAM:
		pr_info("user stop camera %d\n", module->idx);
		complete(&module->frm_com);
		complete(&module->img_com);
		break;
	default:
		pr_err("fail to get write cmd %d\n", write_op.cmd);
		break;
	}

	ret = copy_to_user((void __user *)u_data, &write_op, cnt);
	if (ret) {
		pr_err("fail to get user info\n");
		cnt = ret;
		return -EFAULT;
	}

	return cnt;
}

static int camcore_power_on(struct camera_group *grp)
{
	int ret = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	ret = sprd_glb_mm_pw_on_cfg();
	if (ret) {
		pr_err("fail to power on mm domain\n");
		atomic_dec(&grp->camera_opened);
		return -EMFILE;
	}
	pm_runtime_get_sync(&grp->hw_info->pdev->dev);
#else
	ret = sprd_cam_pw_on();
	ret = sprd_cam_domain_eb();
#endif
	if (atomic_read(&grp->camera_opened) == 1) {
		grp->hw_info->dcam_ioctl(grp->hw_info, DCAM_HW_CONTEXT_0, DCAM_HW_CFG_ENABLE_CLK, NULL);
		grp->hw_info->isp_ioctl(grp->hw_info, ISP_HW_CFG_ENABLE_CLK, NULL);
	}
	sprd_iommu_restore(&grp->hw_info->soc_dcam->pdev->dev);
	sprd_iommu_restore(&grp->hw_info->soc_isp->pdev->dev);
	return ret;
}

static int camcore_power_off(struct camera_group *grp)
{
	int ret = 0;

	if (atomic_read(&grp->camera_opened) == 0) {
		grp->hw_info->isp_ioctl(grp->hw_info, ISP_HW_CFG_DISABLE_CLK, NULL);
		grp->hw_info->dcam_ioctl(grp->hw_info, DCAM_HW_CONTEXT_0, DCAM_HW_CFG_DISABLE_CLK, NULL);
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	ret = sprd_glb_mm_pw_off_cfg();
	pm_runtime_put_sync(&grp->hw_info->pdev->dev);
#else
	ret = sprd_cam_pw_on();
	ret = sprd_cam_domain_eb();
#endif
	return ret;
}

void camcore_hw_abnormal_record_buf(struct cam_hw_info *hw)
{
	void *tmp = NULL;
	struct isphw_abnormal_regtab_size size = {0};

	hw->abnormal_info.abnormal_record_buf = NULL;
	hw->isp_ioctl(hw, ISP_HW_CFG_GET_ABNORMAL_REG_TAB, &size);
	if (size.sum_buf_length == 0)
		return;

	tmp = cam_buf_kernel_sys_vzalloc(size.sum_buf_length);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("fail to alloc hw abnormal record buf\n");
		return;
	}

	hw->abnormal_info.abnormal_record_buf = tmp;

	atomic_set(&hw->abnormal_info.is_print, 0);
	atomic_set(&hw->abnormal_info.abnormal_record, 0);
}

void camcore_hw_abnormal_record_buf_deinit(struct cam_hw_info *hw)
{
	if (hw->abnormal_info.abnormal_record_buf)
		cam_buf_kernel_sys_vfree(hw->abnormal_info.abnormal_record_buf);
}

static int camcore_open(struct inode *node, struct file *file)
{
	int ret = 0;
	struct camera_module *module = NULL;
	struct camera_group *grp = NULL;
	struct miscdevice *md = file->private_data;
	uint32_t i = 0, idx = 0, count = 0;
	struct cam_thread_info *thrd = NULL;
	struct cam_buf_pool_id pool_id = {0};

	grp = md->this_device->platform_data;
	count = grp->dcam_count;

	if (count == 0 || count > CAM_COUNT) {
		pr_err("fail to get valid dts configured dcam count\n");
		return -ENODEV;
	}
	mutex_lock(&grp->module_lock);
	if (atomic_inc_return(&grp->camera_opened) > count) {
		pr_err("fail to open camera, all %d cameras opened already.", count);
		atomic_dec(&grp->camera_opened);
		mutex_unlock(&grp->module_lock);
		return -EMFILE;
	}
	ret = camcore_power_on(grp);
	if (ret)
		goto power_fail;

	pr_info("sprd_img: the camera opened count %d, camsec_mode %d\n",
		atomic_read(&grp->camera_opened), grp->camsec_cfg.camsec_mode);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	__pm_stay_awake(grp->ws);
#endif
	for (i = 0, idx = count; i < count; i++) {
		if ((grp->module_used & (1 << i)) == 0) {
			if (grp->module[i] != NULL) {
				pr_err("fail to get null module, un-release camera module:  %p, idx %d\n",
					grp->module[i], i);
				ret = -EMFILE;
				goto exit;
			}
			idx = i;
			grp->module_used |= (1 << i);
			break;
		}
	}

	if (idx == count) {
		pr_err("fail to get available camera module.\n");
		ret = -EMFILE;
		goto exit;
	}

	pr_debug("alloc. size of module %x, group %x\n",
		(int)sizeof(struct camera_module),
		(int)sizeof(struct camera_group));

	module = cam_buf_kernel_sys_vzalloc(sizeof(struct camera_module));
	if (!module) {
		pr_err("fail to alloc camera module %d\n", idx);
		ret = -ENOMEM;
		goto alloc_fail;
	}

	module->idx = idx;
	module->static_topology = &grp->topology_info;
	ret = camcore_module_init(module);
	if (ret) {
		pr_err("fail to init camera module %d\n", idx);
		ret = -ENOMEM;
		goto init_fail;
	}

	ret = cam_buf_manager_init(idx, (void *)&grp->global_buf_manager);
	if (ret < 0) {
		pr_err("fail to init buf_manager\n");
		goto buf_manager_fail;
	}

	if (atomic_read(&grp->camera_opened) == 1) {
		cam_queue_empty_frame_init();
		/* should check all needed interface here. */
		rwlock_init(&grp->hw_info->soc_dcam->cam_ahb_lock);

		pool_id.tag_id = CAM_BUF_POOL_ABNORAM_RECYCLE;
		ret = cam_buf_manager_pool_reg(&pool_id, CAM_FRAME_Q_LEN, (void *)grp->global_buf_manager);
		if(ret) {
			pr_err("fail to reg CAM_BUF_POOL_ABNORMAL_RECYCLE pool.\n");
			return ret;
		}

		pool_id.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
		ret = cam_buf_manager_pool_reg(&pool_id, DCAM_OUT_BUF_Q_LEN, (void *)grp->global_buf_manager);
		if(ret) {
			pr_err("fail to reg CAM_BUF_POOL_SHARE_FULL_PATH pool.\n");
			return ret;
		}

		pool_id.tag_id = CAM_BUF_POOL_SHARE_DEC_BUF;
		ret = cam_buf_manager_pool_reg(&pool_id, PYR_DEC_REC_BUF_Q_LEN, (void *)grp->global_buf_manager);
		if(ret) {
			pr_err("fail to reg CAM_BUF_POOL_SHARE_DEC_BUF pool.\n");
			return ret;
		}

		pool_id.tag_id = CAM_BUF_POOL_SHARE_REC_BUF;
		ret = cam_buf_manager_pool_reg(&pool_id, PYR_DEC_REC_BUF_Q_LEN, (void *)grp->global_buf_manager);
		if(ret) {
			pr_err("fail to reg CAM_BUF_POOL_SHARE_REC_BUF pool.\n");
			return ret;
		}

		camcore_hw_abnormal_record_buf(grp->hw_info);

		/* create recovery thread */
		if (grp->hw_info->ip_dcam[DCAM_ID_0]->dcamhw_abt->recovery_support) {
			thrd = &grp->recovery_thrd;
			snprintf(thrd->thread_name, ARRAY_SIZE(thrd->thread_name), "cam_recovery");
			ret = camthread_create(grp, thrd, camcore_recovery_proc);
			if (ret)
				pr_warn("warning: creat recovery thread fail\n");
		}
	}

	module->idx = idx;
	module->grp = grp;
	grp->module[idx] = module;
	file->private_data = (void *)module;

	pr_info("sprd_img: open end! %d, %p, %p, grp %p\n",
		idx, module, grp->module[idx], grp);

	mutex_unlock(&grp->module_lock);
	return 0;

buf_manager_fail:
	camcore_module_deinit(module);
init_fail:
	cam_buf_kernel_sys_vfree(module);

alloc_fail:
	grp->module_used &= ~(1 << idx);
	grp->module[idx] = NULL;
exit:
	camcore_power_off(grp);
power_fail:
	atomic_dec(&grp->camera_opened);
	mutex_unlock(&grp->module_lock);
	pr_err("fail to open camera %d\n", ret);
	return ret;
}

static int camcore_release(struct inode *node, struct file *file)
{
	int ret = 0;
	int idx = 0;
	struct camera_group *group = NULL;
	struct camera_module *module = NULL;

	pr_info("sprd_img: cam release start.\n");

	module = (struct camera_module *)file->private_data;
	if (!module || !module->grp) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}
	module->private_key = 0;
	group = module->grp;
	idx = module->idx;
	mutex_lock(&group->module_lock);
	if (module->grp->camsec_cfg.camsec_mode  != SEC_UNABLE) {
		module->grp->camsec_cfg.camsec_mode = SEC_UNABLE;
		ret = cam_trusty_security_set(&module->grp->camsec_cfg,
			CAM_TRUSTY_EXIT);

		pr_info("camca :camsec_mode%d, ret %d\n",
			module->grp->camsec_cfg.camsec_mode, ret);
	}

	pr_info("cam %d, state %d\n", idx, atomic_read(&module->state));
	pr_info("used: %d, module %p, %p, grp %p\n",
		group->module_used, module, group->module[idx], group);

	if (((group->module_used & (1 << idx)) == 0) ||
		(group->module[idx] != module)) {
		pr_err("fail to release camera %d. used:%x, module:%p\n",
			idx, group->module_used, module);
		mutex_unlock(&group->module_lock);
		return -EFAULT;
	}

	down_read(&group->switch_recovery_lock);
	if (atomic_read(&module->state) == CAM_RUNNING)
		atomic_set(&module->state, CAM_FORCE_CLOSE);
	ret = camioctl_stream_off(module, 0L);
	up_read(&group->switch_recovery_lock);

	camcore_module_deinit(module);

	if (atomic_read(&module->state) == CAM_IDLE) {
	/* function "camioctl_cam_res_put" couldn't be called, when HAL sever is killed.
	* cause camera device did not close completely.
	* So. we need clear camera device resoures when HAL process exited abnormally.
	*/
		module->attach_sensor_id = -1;

		camthread_stop(&module->zoom_thrd);
		camthread_stop(&module->buf_thrd);

		if (module->dcam_dev_handle) {
			pr_info("force close dcam %p\n", module->dcam_dev_handle);
			module->dcam_dev_handle->dcam_pipe_ops->close(module->dcam_dev_handle);
			dcam_core_pipe_dev_put(module->dcam_dev_handle, (void *)&group->s_dcam_dev);
			module->dcam_dev_handle = NULL;
		}

		if (module->isp_dev_handle) {
			pr_info("force close isp %p\n", module->isp_dev_handle);
			module->isp_dev_handle->isp_ops->close(module->isp_dev_handle);
			isp_core_pipe_dev_put(module->isp_dev_handle, (void *)&group->s_isp_dev);
			module->isp_dev_handle = NULL;
		}
	}

	group->module_used &= ~(1 << idx);
	group->module[idx] = NULL;

	cam_buf_kernel_sys_vfree(module);
	file->private_data = NULL;

	if (atomic_dec_return(&group->camera_opened) == 0) {
		struct cam_buf_pool_id pool_id = {0};

		pool_id.tag_id = CAM_BUF_POOL_SHARE_DEC_BUF;
		cam_buf_manager_pool_unreg(&pool_id, (void *)group->global_buf_manager);

		pool_id.tag_id = CAM_BUF_POOL_SHARE_REC_BUF;
		cam_buf_manager_pool_unreg(&pool_id, (void *)group->global_buf_manager);

		pool_id.tag_id = CAM_BUF_POOL_SHARE_FULL_PATH;
		cam_buf_manager_pool_unreg(&pool_id, (void *)group->global_buf_manager);
		pool_id.tag_id = CAM_BUF_POOL_ABNORAM_RECYCLE;
		cam_buf_manager_pool_unreg(&pool_id, (void *)group->global_buf_manager);

		ret = cam_buf_mdbg_check();
		atomic_set(&group->runner_nr, 0);
		atomic_set(&group->mul_buf_alloced, 0);
		group->is_mul_buf_share = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		__pm_relax(group->ws);
#endif
		camthread_stop(&group->recovery_thrd);

		camcore_hw_abnormal_record_buf_deinit(group->hw_info);
	}

	cam_buf_manager_deinit(idx, (void *)group->global_buf_manager);

	if (atomic_read(&group->camera_opened) == 0) {
		group->global_buf_manager = NULL;
		group->s_dcam_dev = NULL;
		group->s_isp_dev = NULL;
		cam_queue_empty_frame_deinit();
	}

	ret = cam_buf_mdbg_check();

	camcore_power_off(group);

	mutex_unlock(&group->module_lock);
	pr_info("sprd_img: cam %d release end.\n", idx);

	return ret;
}

static const struct file_operations image_fops = {
	.open = camcore_open,
	.unlocked_ioctl = camcore_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = camcore_ioctl_compat,
#endif
	.release = camcore_release,
	.read = camcore_read,
	.write = camcore_write,
};

static struct miscdevice image_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = IMG_DEVICE_NAME,
	.fops = &image_fops,
};

static int camcore_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct camera_group *group = NULL;

	if (!pdev) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	pr_info("Start camera img probe\n");
	group = vzalloc(sizeof(struct camera_group));
	if (group == NULL) {
		pr_err("fail to alloc memory\n");
		return -ENOMEM;
	}

	ret = misc_register(&image_dev);
	if (ret) {
		pr_err("fail to register misc devices, ret %d\n", ret);
		vfree(group);
		return -EACCES;
	}

	image_dev.this_device->of_node = pdev->dev.of_node;
	image_dev.this_device->platform_data = (void *)group;
	group->md = &image_dev;
	group->pdev = pdev;
	group->hw_info = (struct cam_hw_info *)of_device_get_match_data(&pdev->dev);
	if (!group->hw_info) {
		pr_err("fail to get hw_info\n");
		goto probe_pw_fail;
	}
	atomic_set(&group->camera_opened, 0);
	atomic_set(&group->runner_nr, 0);
	atomic_set(&group->recovery_state, CAM_RECOVERY_NONE);
	mutex_init(&group->module_lock);
	spin_lock_init(&group->dual_deal_lock);
	init_rwsem(&group->switch_recovery_lock);
	spin_lock_init(&g_reg_wr_lock);

	pr_info("sprd img probe pdev name %s\n", pdev->name);
	pr_info("sprd dcam dev name %s\n", dev_name(&pdev->dev));

	ret = dcam_drv_dt_parse(pdev, group->hw_info, &group->dcam_count);
	if (ret) {
		pr_err("fail to parse dcam dts\n");
		goto probe_pw_fail;
	}

	pr_info("sprd isp dev name %s\n", dev_name(&pdev->dev));
	ret = isp_drv_dt_parse(pdev->dev.of_node, group->hw_info);
	if (ret) {
		pr_err("fail to parse isp dts\n");
		goto probe_pw_fail;
	}

	if (group->hw_info && group->hw_info->soc_dcam->pdev)
		ret = cam_buf_iommudev_reg(
			&group->hw_info->soc_dcam->pdev->dev,
			CAM_BUF_IOMMUDEV_DCAM);

	if (group->hw_info && group->hw_info->soc_dcam_lite && group->hw_info->soc_dcam_lite->pdev)
		ret = cam_buf_iommudev_reg(
			&group->hw_info->soc_dcam_lite->pdev->dev,
			CAM_BUF_IOMMUDEV_DCAM_LITE);

	if (group->hw_info && group->hw_info->soc_isp->pdev)
		ret = cam_buf_iommudev_reg(
			&group->hw_info->soc_isp->pdev->dev,
			CAM_BUF_IOMMUDEV_ISP);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	group->ws = wakeup_source_create("Camdrv Wakeuplock");
	wakeup_source_add(group->ws);
#endif

	ret = cam_scene_static_linkages_get(&group->topology_info, group->hw_info);
	if (ret)
		pr_err("fail to get invalid static linkages\n");

	ret = cam_debugger_init(group->hw_info);
	if (ret)
		pr_err("fail to init cam debugfs\n");

	return 0;

probe_pw_fail:
	misc_deregister(&image_dev);
	vfree(group);

	return ret;
}

static int camcore_remove(struct platform_device *pdev)
{
	struct camera_group *group = NULL;

	if (!pdev) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	group = image_dev.this_device->platform_data;
	if (group) {
		cam_buf_iommudev_unreg(CAM_BUF_IOMMUDEV_DCAM);
		cam_buf_iommudev_unreg(CAM_BUF_IOMMUDEV_DCAM_LITE);
		cam_buf_iommudev_unreg(CAM_BUF_IOMMUDEV_ISP);
		cam_debugger_deinit();
		if (group->ca_conn) {
			group->ca_conn = CAM_DISABLE;
			cam_trusty_disconnect();
		}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		wakeup_source_remove(group->ws);
		wakeup_source_destroy(group->ws);
#endif
		vfree(group);
		image_dev.this_device->platform_data = NULL;
	}
	misc_deregister(&image_dev);

	return 0;
}

static const struct of_device_id sprd_cam_of_match[] = {
	#if defined (PROJ_SHARKL3)
	{ .compatible = "sprd,sharkl3-cam", .data = &sharkl3_hw_info},
	#elif defined (PROJ_SHARKL5)
	{ .compatible = "sprd,sharkl5-cam", .data = &sharkl5_hw_info},
	#elif defined (PROJ_SHARKL5PRO)
	{ .compatible = "sprd,sharkl5pro-cam", .data = &sharkl5pro_hw_info},
	#elif defined (PROJ_QOGIRL6)
	{ .compatible = "sprd,qogirl6-cam", .data = &qogirl6_hw_info},
	#elif defined (PROJ_QOGIRN6PRO)
	{ .compatible = "sprd,qogirn6pro-cam", .data = &qogirn6pro_hw_info},
	#elif defined (PROJ_QOGIRN6L)
	{ .compatible = "sprd,qogirn6l-cam", .data = &qogirn6l_hw_info},
	#endif
	{ },
};

static struct platform_driver sprd_img_driver = {
	.probe = camcore_probe,
	.remove = camcore_remove,
	.driver = {
		.name = IMG_DEVICE_NAME,
		.of_match_table = of_match_ptr(sprd_cam_of_match),
	},
};

module_platform_driver(sprd_img_driver);

MODULE_DESCRIPTION("SPRD CAM Driver");
MODULE_AUTHOR("Multimedia_Camera@SPRD");
MODULE_LICENSE("GPL");
