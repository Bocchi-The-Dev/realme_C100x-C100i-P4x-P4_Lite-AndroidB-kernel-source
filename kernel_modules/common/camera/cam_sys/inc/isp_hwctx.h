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

#ifndef _ISP_HWCTX_H_
#define _ISP_HWCTX_H_

#include "cam_hw.h"
#include "isp_interface.h"
#include "cam_queue.h"
#include "cam_debugger.h"

#define ISP_HIST_VALUE_SIZE             256

enum isp_postproc_type {
	POSTPROC_FRAME_DONE,
	POSTPROC_SLOWMOTION_FRAMEDONE,
	POSTPROC_RGB_GTM_HISTS_DONE,
	POSTPROC_HIST_CAL_DONE,
	POSTPROC_FRAME_ERROR_DONE,
	POSTPROC_MAX,
};

typedef int(*isp_irq_postproc)(void *handle, uint32_t idx,
	enum isp_postproc_type type, void *param);

struct isp_pipe_info {
	struct isp_hw_fetch_info fetch;
	struct isp_fbd_yuv_info fetch_fbd_yuv;
	struct isp_hw_path_scaler scaler[ISP_SPATH_NUM];
	struct isp_hw_thumbscaler_info thumb_scaler;
	struct isp_hw_path_store store[ISP_SPATH_NUM];
};

struct isp_hw_context {
	atomic_t user_cnt;
	uint32_t sw_ctx_id;
	uint32_t node_id;
	uint32_t cfg_id;
	uint32_t hw_ctx_id;
	uint32_t fmcu_used;
	uint32_t fid;
	void *node;
	void *fmcu_handle;
	struct cam_hw_info *hw;
	struct isp_pipe_info pipe_info;
	struct dcam_isp_k_block *isp_k_param;
	struct dcam_isp_k_block *isp_using_param;
	struct cam_debug_cfg debug;
	timespec hw_start_ts;
	void *slice_ctx;
	void *rec_handle;
	uint32_t is_last_slice;
	uint32_t valid_slc_num;
	uint32_t pyr_layer_num;
	enum cam_en_status enable_slowmotion;
	uint32_t slowmotion_count;
	uint32_t nr3_fbc_fbd;
	uint32_t iommu_status;
	struct completion slice_done;
	enum isp_work_mode wmode;
	isp_irq_postproc postproc_func;
	uint32_t hist_enable;
};

struct isp_start_param {
	struct mutex *blkpm_lock;
	uint32_t type;
	uint32_t is_dual;
	struct dcam_isp_k_block **isp_using_param;
};

int isp_hwctx_fetch_set(void *handle);
int isp_hwctx_fetch_fbd_set(void *handle);
int isp_hwctx_scaler_set(void *handle, int path_id, uint32_t *param);
int isp_hwctx_store_set(void *handle, int path_id);
uint32_t isp_hwctx_hw_start(struct isp_hw_context *pctx_hw, void *dev_handle, struct isp_start_param *param);
uint32_t isp_hwctx_fmcu_reset(void *handle);
int isp_hwctx_slice_ctx_init(struct isp_hw_context *pctx_hw, struct isp_pipe_info *pipe_info, uint32_t sw_slice_num);
int isp_hwctx_slice_fmcu(struct isp_hw_context *pctx_hw, struct slice_cfg_input *slc_cfg);
int isp_hwctx_slices_proc(struct isp_hw_context *pctx_hw, void *dev_handle, struct isp_start_param *param);
int isp_hwctx_hist2_frame_prepare(void *buf, uint32_t hw_idx, void *isp_handle);
int isp_hwctx_store_frm_set(struct isp_pipe_info *pipe_info, uint32_t path_id, struct camera_frame *frame);
int isp_hwctx_fetch_frm_set(void *dev_handle, struct isp_hw_fetch_info *fetch, struct camera_frame *frame);
int isp_hwctx_gtm_hist_result_get(void *buf, uint32_t hw_idx, void *dev, uint32_t hist_total, uint32_t fid);

#endif
