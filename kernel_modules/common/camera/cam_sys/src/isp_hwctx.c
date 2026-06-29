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

#include "cam_port.h"
#include "cam_trusty.h"
#include "isp_cfg.h"
#include "isp_hw.h"
#include "isp_slice.h"
#include "cam_debugger.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_HWCTX: %d %d %s : " fmt, current->pid, __LINE__, __func__

int isp_hwctx_fetch_set(void *handle)
{
	struct isp_hw_context *pctx_hw = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_pipe_info *pipe_info = NULL;

	pctx_hw = (struct isp_hw_context *)handle;
	hw = pctx_hw->hw;
	pipe_info = &pctx_hw->pipe_info;

	hw->isp_ioctl(hw, ISP_HW_CFG_FETCH_FRAME_ADDR, &pipe_info->fetch);
	hw->isp_ioctl(hw, ISP_HW_CFG_FETCH_SET, &pipe_info->fetch);

	if (pipe_info->fetch.fetch_fmt == CAM_YVU420_2FRAME
		|| pipe_info->fetch.fetch_fmt == CAM_YVU420_2FRAME_10
		|| pipe_info->fetch.fetch_fmt == CAM_YVU420_2FRAME_MIPI)
		hw->isp_ioctl(hw, ISP_HW_CFG_ISP_CFG_SUBBLOCK, &pipe_info->fetch);

	return 0;
}

int isp_hwctx_fetch_fbd_set(void *handle)
{
	struct isp_hw_context *pctx_hw = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_pipe_info *pipe_info = NULL;

	pctx_hw = (struct isp_hw_context *)handle;
	hw = pctx_hw->hw;
	pipe_info = &pctx_hw->pipe_info;

	hw->isp_ioctl(hw, ISP_HW_CFG_FBD_ADDR_SET, &pipe_info->fetch_fbd_yuv);
	hw->isp_ioctl(hw, ISP_HW_CFG_FETCH_FBD_SET, &pipe_info->fetch_fbd_yuv);

	if (pipe_info->fetch.fetch_fmt == CAM_YVU420_2FRAME
		|| pipe_info->fetch.fetch_fmt == CAM_YVU420_2FRAME_10
		|| pipe_info->fetch.fetch_fmt == CAM_YVU420_2FRAME_MIPI)
		hw->isp_ioctl(hw, ISP_HW_CFG_ISP_CFG_SUBBLOCK, &pipe_info->fetch);

	return 0;
}

int isp_hwctx_scaler_set(void *handle, int path_id, uint32_t *param)
{
	uint32_t spath_id = 0;
	struct isp_hw_context *pctx_hw = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_pipe_info *pipe_info = NULL;

	pctx_hw = (struct isp_hw_context *)handle;
	hw = pctx_hw->hw;
	pipe_info = &pctx_hw->pipe_info;
	if (param)
		spath_id = *param;
	else
		spath_id = path_id;

	if (spath_id == ISP_SPATH_FD)
		hw->isp_ioctl(hw, ISP_HW_CFG_SET_PATH_THUMBSCALER,
			&pipe_info->thumb_scaler);
	else
		hw->isp_ioctl(hw, ISP_HW_CFG_SET_PATH_SCALER,
			&pipe_info->scaler[path_id]);

	return 0;
}

int isp_hwctx_store_set(void *handle, int path_id)
{
	struct isp_hw_context *pctx_hw = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_pipe_info *pipe_info = NULL;

	pctx_hw = (struct isp_hw_context *)handle;
	hw = pctx_hw->hw;
	pipe_info = &pctx_hw->pipe_info;

	hw->isp_ioctl(hw, ISP_HW_CFG_STORE_FRAME_ADDR, &pipe_info->store[path_id]);
	hw->isp_ioctl(hw, ISP_HW_CFG_SET_PATH_STORE, &pipe_info->store[path_id]);

	return 0;
}

uint32_t isp_hwctx_fmcu_reset(void *handle)
{
	struct isp_hw_context *pctx_hw = NULL;
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	pctx_hw = VOID_PTR_TO(handle, struct isp_hw_context);
	fmcu = (struct isp_fmcu_ctx_desc *)pctx_hw->fmcu_handle;
	if (fmcu)
		fmcu->ops->ctx_reset(fmcu);

	return 0;
}

uint32_t isp_hwctx_hw_start(struct isp_hw_context *pctx_hw, void *dev_handle, struct isp_start_param *param)
{
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct isp_cfg_ctx_desc *cfg_desc = NULL;
	struct isp_pipe_dev *dev = NULL;
	uint32_t ret = 0, fmcu_used = 0;
	dev = VOID_PTR_TO(dev_handle, struct isp_pipe_dev);
	/* start to prepare/kickoff cfg buffer. */
	if (likely(dev->wmode == ISP_CFG_MODE)) {
		pr_debug("cfg enter. %d\n", pctx_hw->hw_ctx_id);
		cfg_desc = VOID_PTR_TO(dev->cfg_handle, struct isp_cfg_ctx_desc);
		/* blkpm_lock to avoid user config block param across frame */
		mutex_lock(param->blkpm_lock);
		if (pctx_hw->fmcu_handle)
			fmcu_used = 1;
		ret = cfg_desc->ops->hw_cfg(cfg_desc, pctx_hw->cfg_id, pctx_hw->hw_ctx_id, fmcu_used);
		mutex_unlock(param->blkpm_lock);
		*param->isp_using_param = NULL;
		pctx_hw->isp_using_param = NULL;
		/* Tmp to solve camera dismiss in refocus continue capture scene */
		if (param->is_dual) {
			mutex_lock(&dev->dev_lock);
			ret = wait_for_completion_timeout(&dev->frm_done, ISP_CONTEXT_TIMEOUT);
			if (!ret)
				pr_warn("warning:wait isp hw ctx %d, timeout.\n", pctx_hw->hw_ctx_id);
			mutex_unlock(&dev->dev_lock);
		}
		if (pctx_hw->fmcu_handle) {
			fmcu = (struct isp_fmcu_ctx_desc *)pctx_hw->fmcu_handle;
			ret = fmcu->ops->hw_start(fmcu);
		} else
			pctx_hw->hw->isp_ioctl(pctx_hw->hw, ISP_HW_CFG_START_ISP, &pctx_hw->hw_ctx_id);
	} else {
		*param->isp_using_param = NULL;
		pctx_hw->isp_using_param = NULL;
		if (pctx_hw->fmcu_handle) {
			fmcu = (struct isp_fmcu_ctx_desc *)pctx_hw->fmcu_handle;
			ret = fmcu->ops->hw_start(fmcu);
		} else {
			pr_debug("fetch start.\n");
			pctx_hw->hw->isp_ioctl(pctx_hw->hw, ISP_HW_CFG_FETCH_START, NULL);
		}
	}
	return ret;
}

static int isphwctx_init_dyn_ov_param(struct slice_cfg_input *slc_cfg_in,
	struct isp_hw_context *pctx_hw, struct isp_pipe_info *pipe_info)
{
	if (!slc_cfg_in || !pctx_hw) {
		pr_err("fail to get input ptr slc_cfg_in %p, pctx %p\n", slc_cfg_in, pctx_hw);
		return -1;
	}

	slc_cfg_in->calc_dyn_ov.pyr_layer_num = pctx_hw->pyr_layer_num;
	slc_cfg_in->calc_dyn_ov.src.w = pipe_info->fetch.src.w;
	slc_cfg_in->calc_dyn_ov.src.h = pipe_info->fetch.src.h;
	slc_cfg_in->calc_dyn_ov.crop.start_x = pipe_info->fetch.in_trim.start_x;
	slc_cfg_in->calc_dyn_ov.crop.start_y = pipe_info->fetch.in_trim.start_y;
	slc_cfg_in->calc_dyn_ov.crop.size_x = pipe_info->fetch.in_trim.size_x;
	slc_cfg_in->calc_dyn_ov.crop.size_y = pipe_info->fetch.in_trim.size_y;
	slc_cfg_in->calc_dyn_ov.path_scaler[ISP_SPATH_CP] = &pipe_info->scaler[ISP_SPATH_CP];
	slc_cfg_in->calc_dyn_ov.path_scaler[ISP_SPATH_VID] = &pipe_info->scaler[ISP_SPATH_VID];
	slc_cfg_in->calc_dyn_ov.thumb_scaler = &pipe_info->thumb_scaler;
	slc_cfg_in->calc_dyn_ov.store[ISP_SPATH_CP] = &pipe_info->store[ISP_SPATH_CP].store;
	slc_cfg_in->calc_dyn_ov.store[ISP_SPATH_VID] = &pipe_info->store[ISP_SPATH_VID].store;
	slc_cfg_in->calc_dyn_ov.store[ISP_SPATH_FD] = &pipe_info->store[ISP_SPATH_FD].store;
	return 0;
}

int isp_hwctx_slice_ctx_init(struct isp_hw_context *pctx_hw, struct isp_pipe_info *pipe_info, uint32_t sw_slice_num)
{
	int ret = 0;
	int i = 0;
	uint32_t val = 0;
	struct slice_cfg_input slc_cfg_in;
	struct isp_hw_nlm_ynr radius_adapt;

	if (pctx_hw->slice_ctx == NULL) {
		pctx_hw->slice_ctx = isp_slice_ctx_get();
		if (IS_ERR_OR_NULL(pctx_hw->slice_ctx)) {
			pr_err("fail to get memory for slice_ctx.\n");
			pctx_hw->slice_ctx = NULL;
			ret = -ENOMEM;
			goto exit;
		}
	}

	memset(&slc_cfg_in, 0, sizeof(struct slice_cfg_input));

	slc_cfg_in.frame_in_size.w = pipe_info->fetch.in_trim.size_x;
	slc_cfg_in.frame_in_size.h = pipe_info->fetch.in_trim.size_y;
	slc_cfg_in.frame_fbd_yuv = &pipe_info->fetch_fbd_yuv;
	slc_cfg_in.frame_fetch = &pipe_info->fetch;
	slc_cfg_in.thumb_scaler = &pipe_info->thumb_scaler;
	slc_cfg_in.sw_slice_num = sw_slice_num;
	for (i = 0; i < ISP_SPATH_NUM; i++) {
		slc_cfg_in.calc_dyn_ov.path_en[i] = pipe_info->store[i].used;
		if (slc_cfg_in.calc_dyn_ov.path_en[i]) {
			slc_cfg_in.frame_out_size[i] = &pipe_info->store[i].store.size;
			slc_cfg_in.frame_store[i] = &pipe_info->store[i].store;
			slc_cfg_in.frame_scaler[i] = &pipe_info->scaler[i].scaler;
			slc_cfg_in.frame_deci[i] = &pipe_info->scaler[i].deci;
			slc_cfg_in.frame_trim0[i] = &pipe_info->scaler[i].in_trim;
			slc_cfg_in.frame_trim1[i] = &pipe_info->scaler[i].out_trim;
		}
	}

	slc_cfg_in.isp_k_param = pctx_hw->isp_k_param;
	slc_cfg_in.calc_dyn_ov.version = pctx_hw->hw->ip_isp->isphw_abt->dyn_overlap_version;

	if (slc_cfg_in.calc_dyn_ov.version != ALG_ISP_DYN_OVERLAP_NONE)
		isphwctx_init_dyn_ov_param(&slc_cfg_in, pctx_hw, pipe_info);

	radius_adapt.val = val;
	radius_adapt.ctx_id = pctx_hw->cfg_id;
	radius_adapt.slc_cfg_in = &slc_cfg_in;
	pctx_hw->hw->isp_ioctl(pctx_hw->hw, ISP_HW_CFG_GET_NLM_YNR, &radius_adapt);

	isp_slice_base_cfg(&slc_cfg_in, pctx_hw->slice_ctx, &pctx_hw->valid_slc_num);

	pr_debug("sw %d valid_slc_num %d\n", pctx_hw->node_id, pctx_hw->valid_slc_num);
exit:
	return ret;
}

int isp_hwctx_slice_fmcu(struct isp_hw_context *pctx_hw, struct slice_cfg_input *slc_cfg)
{
	uint32_t i = 0, ret = 0;
	struct img_trim intrim = {0};

	for (i = 0; i < ISP_SPATH_NUM; i++) {
		if (!pctx_hw->pipe_info.store[i].used)
			continue;
		slc_cfg->frame_store[i] = &pctx_hw->pipe_info.store[i].store;
		if (slc_cfg->sw_slice_num)
			slc_cfg->frame_trim0[i] = &pctx_hw->pipe_info.scaler[i].in_trim;
		else
			slc_cfg->frame_trim0[i] = &intrim;
	}

	slc_cfg->frame_fetch = &pctx_hw->pipe_info.fetch;
	slc_cfg->frame_fbd_yuv = &pctx_hw->pipe_info.fetch_fbd_yuv;
	slc_cfg->frame_in_size.w = pctx_hw->pipe_info.fetch.in_trim.size_x;
	slc_cfg->frame_in_size.h = pctx_hw->pipe_info.fetch.in_trim.size_y;
	slc_cfg->isp_k_param = pctx_hw->isp_using_param;
	slc_cfg->calc_dyn_ov.version = pctx_hw->hw->ip_isp->isphw_abt->dyn_overlap_version;
	isp_slice_info_cfg(slc_cfg, pctx_hw->slice_ctx);

	if (pctx_hw->fmcu_handle) {
		pr_debug("use fmcu support slices for ctx %d hw %d\n",
			pctx_hw->cfg_id, pctx_hw->hw_ctx_id);
		ret = isp_slice_fmcu_cmds_set(pctx_hw->fmcu_handle, pctx_hw);
	}
	return ret;
}

int isp_hwctx_slices_proc(struct isp_hw_context *pctx_hw, void *dev_handle, struct isp_start_param *param)
{
	int ret = 0;
	uint32_t slice_id, cnt = 0;
	struct isp_cfg_ctx_desc *cfg_desc = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_pipe_dev *dev = NULL;
	pr_debug("enter. valid_slc_num %d\n", pctx_hw->valid_slc_num);
	dev = (struct isp_pipe_dev *)dev_handle;
	cfg_desc = (struct isp_cfg_ctx_desc *)dev->cfg_handle;
	pctx_hw->is_last_slice = 0;
	hw = dev->isp_hw;
	for (slice_id = 0; slice_id < SLICE_NUM_MAX; slice_id++) {
		if (pctx_hw->is_last_slice == 1)
			break;

		ret = isp_slice_update(pctx_hw->slice_ctx, hw, pctx_hw->cfg_id, slice_id);
		if (ret < 0)
			continue;

		cnt++;
		if (cnt == pctx_hw->valid_slc_num)
			pctx_hw->is_last_slice = 1;
		pr_debug("slice %d, valid %d, last %d\n", slice_id, pctx_hw->valid_slc_num, pctx_hw->is_last_slice);

		mutex_lock(param->blkpm_lock);

		if (pctx_hw->is_last_slice) {
			*param->isp_using_param = NULL;
			pctx_hw->isp_using_param = NULL;
		}

		if (dev->wmode == ISP_CFG_MODE)
			cfg_desc->ops->hw_cfg(cfg_desc, pctx_hw->cfg_id, pctx_hw->hw_ctx_id, 0);
		mutex_unlock(param->blkpm_lock);

		if (dev->wmode == ISP_CFG_MODE)
			cfg_desc->hw->isp_ioctl(cfg_desc->hw, ISP_HW_CFG_START_ISP, &pctx_hw->hw_ctx_id);
		else
			hw->isp_ioctl(hw, ISP_HW_CFG_FETCH_START, NULL);

		ret = wait_for_completion_interruptible_timeout(&pctx_hw->slice_done, ISP_CONTEXT_TIMEOUT);
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

int isp_hwctx_hist2_frame_prepare(void *buf, uint32_t hw_idx, void *isp_handle)
{
	struct isp_pipe_dev *dev;
	struct isp_hw_context *pctx_hw;
	struct cam_frame *pframe;
	uint32_t sum = 0;
	struct isp_hw_hist_val hist_val;

	pframe = VOID_PTR_TO(buf, struct cam_frame);
	dev = (struct isp_pipe_dev *)isp_handle;
	pctx_hw = &dev->hw_ctx[hw_idx];

	if (!(uint32_t *)pframe->common.buf.addr_k)
		return -1;
	hist_val.hist_buf = (uint32_t *)pframe->common.buf.addr_k;
	pctx_hw->hw->isp_ioctl(pctx_hw->hw, ISP_HW_CFG_HIST_READ_VAL, &hist_val);
	sum = hist_val.sum;
	pframe->common.width = pctx_hw->pipe_info.fetch.in_trim.size_x;
	pframe->common.height = pctx_hw->pipe_info.fetch.in_trim.size_y;
	if (sum != (pframe->common.width * pframe->common.height)) {
		pr_debug("pixel num check wrong, sum %d, should be %d\n", sum, pframe->common.width * pframe->common.height);
		return -1;
	}

	return 0;
}

int isp_hwctx_gtm_hist_result_get(void *buf, uint32_t hw_idx, void *dev,
		uint32_t hist_total, uint32_t fid)
{
	int ret = 0;
	struct isp_pipe_dev *isp_dev;
	struct cam_frame *pframe;
	struct isp_hw_gtmhist_get_param param;

	pframe = VOID_PTR_TO(buf, struct cam_frame);
	isp_dev = (struct isp_pipe_dev *)dev;
	param.buf = (uint32_t *)pframe->common.buf.addr_k;
	param.hist_total = hist_total;
	param.fid = fid;
	ret = isp_dev->isp_hw->isp_ioctl(isp_dev->isp_hw, ISP_HW_CFG_GTMHIST_GET, &param);

	return ret;
}

int isp_hwctx_fetch_frm_set(void *dev_handle, struct isp_hw_fetch_info *fetch, struct camera_frame *frame)
{
	int ret = 0;
	int planes;
	unsigned long offset_u, offset_v, yuv_addr[3] = {0};
	struct isp_pipe_dev *dev = VOID_PTR_TO(dev_handle, struct isp_pipe_dev);

	if (!dev || !fetch || !frame) {
		pr_err("fail to get valid dev %p, fetch %p, frame %p\n", dev, fetch, frame);
		return -EINVAL;
	}

	if (!frame->is_compressed) {
		if (fetch->fetch_fmt == CAM_YUV422_3FRAME)
			planes = 3;
		else if ((fetch->fetch_fmt == CAM_YUV422_2FRAME)
				|| (fetch->fetch_fmt == CAM_YVU422_2FRAME)
				|| (fetch->fetch_fmt == CAM_YUV420_2FRAME)
				|| (fetch->fetch_fmt == CAM_YVU420_2FRAME)
				|| (fetch->fetch_fmt == CAM_YVU420_2FRAME_MIPI)
				|| (fetch->fetch_fmt == CAM_YUV420_2FRAME_MIPI))
			planes = 2;
		else
			planes = 1;

		yuv_addr[0] = frame->buf.iova[CAM_BUF_IOMMUDEV_ISP];

		if (planes > 1) {
			offset_u = fetch->pitch.pitch_ch0 * fetch->src.h;
			yuv_addr[1] = yuv_addr[0] + offset_u;
		}

		if ((planes > 2)) {
			offset_v = fetch->pitch.pitch_ch1 * fetch->src.h;
			yuv_addr[2] = yuv_addr[1] + offset_v;
		}

		/* set the start address of source frame */
		fetch->addr.addr_ch0 = yuv_addr[0];
		fetch->addr.addr_ch1 = yuv_addr[1];
		fetch->addr.addr_ch2 = yuv_addr[2];
		fetch->addr_hw.addr_ch0 = yuv_addr[0];
		fetch->addr_hw.addr_ch1 = yuv_addr[1];
		fetch->addr_hw.addr_ch2 = yuv_addr[2];
	} else {
		fetch->addr_hw.addr_ch0 = frame->buf.iova[CAM_BUF_IOMMUDEV_ISP];
		fetch->addr_hw.addr_ch1 = fetch->addr_hw.addr_ch0;
	}
	if (dev->sec_mode == SEC_TIME_PRIORITY)
		cam_trusty_isp_fetch_addr_set(yuv_addr[0], yuv_addr[1], yuv_addr[2]);

	pr_debug("camca  isp sec_mode=%d,  %lx %lx %lx\n", dev->sec_mode, yuv_addr[0], yuv_addr[1], yuv_addr[2]);
	return ret;
}

int isp_hwctx_store_frm_set(struct isp_pipe_info *pipe_info, uint32_t path_id, struct camera_frame *frame)
{
	int ret = 0;
	int planes = 0;
	struct isp_store_info *store = NULL;
	unsigned long offset_u, yuv_addr[3] = {0};
	if (!pipe_info || !frame || path_id == ISP_SPATH_NUM) {
		pr_err("fail to get valid input ptr, pipe_info %p,frame %p\n",
			pipe_info, frame);
		return -EINVAL;
	}
	pr_debug("enter.\n");
	pipe_info->store[path_id].used = 1;
	store = &pipe_info->store[path_id].store;

	if (store->color_fmt == CAM_UYVY_1FRAME)
		planes = 1;
	else if ((store->color_fmt == CAM_YUV422_3FRAME)
			|| (store->color_fmt == CAM_YUV420_3FRAME))
		planes = 3;
	else
		planes = 2;

	if (frame->buf.iova[CAM_BUF_IOMMUDEV_ISP] == 0) {
		pr_err("fail to get valid iova address, fd = 0x%x\n",
			frame->buf.mfd);
		return -EINVAL;
	}

	if (pipe_info->fetch.sec_mode == SEC_TIME_PRIORITY)
		pipe_info->store[ISP_SPATH_VID].sec_mode = SEC_TIME_PRIORITY;
	pipe_info->store[path_id].buf_sec = frame->buf.buf_sec;
	yuv_addr[0] = frame->buf.iova[CAM_BUF_IOMMUDEV_ISP];

	pr_debug("fmt %s, planes %d addr %lx %lx %lx, pitch:%d\n",
		camport_fmt_name_get(store->color_fmt), planes, yuv_addr[0], yuv_addr[1], yuv_addr[2], store->pitch.pitch_ch0);

	if ((planes > 1) && yuv_addr[1] == 0) {
		if (!frame->slice_info.slice_num) {
			offset_u = store->pitch.pitch_ch0 * store->size.h;
			yuv_addr[1] = yuv_addr[0] + offset_u;
		} else
			yuv_addr[1] = yuv_addr[0] + store->total_size * 2 / 3;
	}

	if (frame->slice_info.slice_num) {
		yuv_addr[0] += store->slice_offset.addr_ch0;
		yuv_addr[1] += store->slice_offset.addr_ch1;
	}

	pr_debug("path %d planes %d addr %lx %lx %lx\n",
		path_id, planes, yuv_addr[0], yuv_addr[1], yuv_addr[2]);

	store->addr.addr_ch0 = yuv_addr[0];
	store->addr.addr_ch1 = yuv_addr[1];
	store->addr.addr_ch2 = yuv_addr[2];
	pr_debug("done %x %x %x\n", store->addr.addr_ch0, store->addr.addr_ch1, store->addr.addr_ch2);

	return ret;
}
