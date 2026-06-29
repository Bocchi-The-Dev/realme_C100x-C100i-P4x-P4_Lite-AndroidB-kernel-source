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

#ifndef _DCAM_CORE_H_
#define _DCAM_CORE_H_

#include <os_adapt_common.h>
#include "sprd_cam.h"
#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
#include <linux/sprd_ion.h>
#endif
#include "cam_types.h"
#include "cam_queue.h"
#include "cam_block.h"
#include "cam_hw.h"
#include "dcam_interface.h"
#include "dcam_int.h"
#include "dcam_fmcu.h"
#include "dcam_online_node.h"
#include "dcam_offline_node.h"
#include "dcam_fetch_node.h"
#include "dcam_int_common.h"

#define DCAM_OUT_BUF_Q_LEN                50
#define DCAM_LSC_BUF_SIZE                 0x3000
#define DCAM_LSC_WEIGHT_TAB_SIZE          774

#define DCAM_OFFLINE_TIMEOUT              msecs_to_jiffies(2000)
#define DCAM_OFFLINE_SLC_MAX              6

#define DCAM_ADDR_RECORD_FRAME_NUM        5

#define DCAM_SLICE_CROP_W_ALIGN           8
/* get index of timestamp from frame index */
#define tsid(x)                           ((x) & (DCAM_FRAME_TIMESTAMP_COUNT - 1))
#define DCAM_NR3_MV_MAX                   16

enum dcam_path_state {
	DCAM_PATH_IDLE,
	DCAM_PATH_PAUSE,
	DCAM_PATH_RESUME,
};

/*
 * state machine for DCAM
 *
 * @INIT:             initial state of this dcam_pipe_dev
 * @IDLE:             after hardware initialized, power/clk/int should be
 *                    prepared, which indicates registers are ready for
 *                    accessing
 * @RUNNING:          this dcam_pipe_dev is receiving data from CSI or DDR, and
 *                    writing data from one or more paths
 * @ERROR:            something is wrong, we should reset hardware right now
 */
enum dcam_state {
	STATE_INIT = 0,
	STATE_IDLE = 1,
	STATE_RUNNING = 2,
	STATE_ERROR = 3,
};

enum slice_proc_chose {
	FETCH_SLICE_PROC,
	OFFLINE_SLICE_PROC,
};

struct dcam_offline_slice_info {
	uint32_t slice_num;
	uint32_t w_slice_num;
	uint32_t slice_count;
	enum cam_format fetch_fmt;
	struct img_trim *cur_slice;
	struct img_trim slice_trim[DCAM_OFFLINE_SLC_MAX];
	timespec slice_start_ts[DCAM_FRAME_TIMESTAMP_COUNT];
	timespec slice_end_ts[DCAM_FRAME_TIMESTAMP_COUNT];
	uint32_t slice_valid_num[DCAM_OFFLINE_SLC_MAX];
	atomic_t slice_valid_cnt;
	uint32_t slice_valid_num_static;
};

struct dcam_online_start_param {
	void *lbuf_param;
	void *param;
};

struct dcam_hw_path {
	enum cam_en_status need_update;
	uint32_t frm_deci;
	uint32_t frm_deci_cnt;
	struct img_trim in_trim;
	struct dcam_hw_cfg_store_addr hw_store;
	struct dcam_hw_path_size hw_size;
	struct dcam_hw_path_start hw_start;
	struct dcam_hw_fbc_addr hw_fbc_store;
	struct dcam_hw_fbc_ctrl hw_fbc;
};

struct dcam_hw_context {
	enum cam_en_status is_offline_proc;
	enum cam_en_status is_virtualsensor_proc;
	enum cam_en_status offline_pre_en;
	uint32_t fid;
	uint32_t recovery_fid;
	uint32_t index_to_set;
	uint32_t slw_idx;
	atomic_t user_cnt;
	uint32_t irq;
	enum cam_en_status irq_enable;
	uint32_t node_id;
	enum cam_node_type node_type;
	uint32_t hw_ctx_id;
	uint32_t handled_bits;
	uint32_t handled_bits_on_int1;
	uint32_t in_irq_proc;
	uint32_t nr3_path_mv_ready;
	uint32_t nr3_path_cnt;
	enum cam_en_status is_3dnr;
	enum camera_slice_mode dcam_slice_mode;
	uint32_t slowmotion_count;
	spinlock_t glb_reg_lock;
	void *node;
	timespec hw_start_ts;
	struct cam_debug_cfg debug;

	uint32_t zoom_ratio;
	uint32_t total_zoom;
	struct img_trim next_roi;
	enum cam_en_status is_pyr_rec;
	uint32_t iommu_status;
	uint32_t err_count;/* iommu register dump count in dcam_err */
	enum cam_en_status dec_all_done;
	enum cam_en_status virtualsensor_dec_int_done;
	enum cam_en_status dec_layer0_done;
	atomic_t shadow_done_cnt;
	atomic_t shadow_config_cnt;
	enum cam_en_status prev_fbc_done;
	enum cam_en_status cap_fbc_done;
	spinlock_t fbc_lock;
	struct dcam_offline_slice_info slice_info;
	struct dcam_fmcu_ctx_desc *fmcu;
	struct cam_hw_info *hw;
	struct dcam_hw_fetch_set hw_fetch;
	struct dcam_hw_path hw_path[DCAM_PATH_MAX];
	struct dcam_isp_k_block *blk_pm;
	struct dcam_hw_slw_fmcu_cmds slw;
	struct dcam_mipi_info cap_info;
	struct nr3_me_data nr3_mv_ctrl[DCAM_NR3_MV_MAX];
	struct dcam_dummy_slave *dummy_slave;
	struct dcam_hw_binning_4in1 binning;
	struct dcam_irq_ops irq_ops;

	void *dcam_irq_cb_handle;
	dcam_irq_proc_cb dcam_irq_cb_func;
	uint32_t frame_addr[DCAM_ADDR_RECORD_FRAME_NUM][DCAM_RECORD_PORT_INFO_MAX];
	enum slice_proc_chose slice_proc_mode;
	uint32_t gtm_hist_stat_bypass;
	uint32_t relative_offset[DCAM_PATH_MAX];
	atomic_t path_done[DCAM_PATH_MAX];
	uint32_t sw_all_done;
};

struct dcam_pipe_dev {
	atomic_t user_cnt;
	atomic_t enable;
	struct dcam_hw_context hw_ctx[DCAM_HW_CONTEXT_MAX];
	struct mutex ctx_mutex;
	struct mutex path_mutex;
	struct completion fetch_done;
	spinlock_t ctx_lock;
	struct dcam_pipe_ops *dcam_pipe_ops;
	struct cam_hw_info *hw;
};

enum dcam_bind_mode {
	DCAM_BIND_FIXED = 0,
	DCAM_BIND_DYNAMIC,
	DCAM_BIND_MAX
};

enum camera_csi_switch_mode {
	CAM_CSI_NORMAL_SWITCH,
	CAM_CSI_RECOVERY_SWITCH,
	CAM_CSI_MAX_SWITCH,
};

struct dcam_switch_param {
	uint32_t csi_id;
	uint32_t dcam_id;
	enum cam_en_status is_recovery;
};

struct dcam_csi_reset_param {
	uint32_t mode;
	enum dcam_csi_state csi_connect_stat;
	void *param;
};

static inline uint32_t cal_sprd_pitch(uint32_t w, uint32_t fmt)
{
	uint32_t pitchsize = 0;

	switch (fmt) {
	case CAM_YUV422_2FRAME:
	case CAM_YVU422_2FRAME:
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
	case CAM_YUV422_3FRAME:
	case CAM_YUV420_3FRAME:
	case CAM_YUYV_1FRAME:
	case CAM_UYVY_1FRAME:
	case CAM_YVYU_1FRAME:
	case CAM_VYUY_1FRAME:
	case CAM_RAW_8:
		pitchsize = w;
		break;
	case CAM_RAW_14:
	case CAM_RAW_HALFWORD_10:
		pitchsize = CAL_UNPACK_PITCH(w);
		break;
	case CAM_YUV420_2FRAME_10:
	case CAM_YVU420_2FRAME_10:
		pitchsize = (w * 16 + 127) / 128 * 128 / 8;
		break;
	case CAM_RAW_PACK_10:
		pitchsize = CAL_PACK_PITCH(w);
		break;
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
		pitchsize = (w * 10 + 127) / 128 * 128 / 8;
		break;
	case CAM_FULL_RGB14:
		pitchsize = CAL_FULLRGB14_PITCH(w);
		break;
	default :
		pr_err("fail to get fmt : %d\n", fmt);
		break;
	}

	return pitchsize;
}

static inline uint32_t cal_sprd_size(uint32_t w, uint32_t h, uint32_t fmt)
{
	uint32_t size = 0;

	switch (fmt) {
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
	case CAM_YUV420_3FRAME:
		size = cal_sprd_pitch(w, fmt) * h * 3 / 2;
		break;
	case CAM_RAW_14:
	case CAM_RAW_8:
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_PACK_10:
	case CAM_RGB_BASE:
	case CAM_FULL_RGB10:
	case CAM_FULL_RGB14:
		size = cal_sprd_pitch(w, fmt) * h;
		break;
	case CAM_YUV422_2FRAME:
	case CAM_YVU422_2FRAME:
	case CAM_YUV422_3FRAME:
	case CAM_YUYV_1FRAME:
	case CAM_UYVY_1FRAME:
	case CAM_YVYU_1FRAME:
	case CAM_VYUY_1FRAME:
		size = cal_sprd_pitch(w, fmt) * h * 2;
		break;
	case CAM_YVU420_2FRAME_10:
	case CAM_YUV420_2FRAME_10:
		size = w * h * 3;
		break;
	default :
		pr_err("fail to get fmt : %d\n", fmt);
		break;
	}

	return size;
}

static inline uint32_t cal_dcamhw_format(uint32_t fmt)
{
	uint32_t hwfmt = 0;

	switch (fmt) {
	case CAM_FULL_RGB14:
		hwfmt = CAM_FRGB14BIT;
		break;
	case CAM_RAW_PACK_10:
		hwfmt = CAM_RAW10PACK_RAW;
		break;
	case CAM_RAW_HALFWORD_10:
		hwfmt = CAM_HALFWORD_RAW10;
		break;
	case CAM_YUV422_2FRAME:
		hwfmt = CAM_TWOPLANE_YUV422;
		break;
	case CAM_YVU422_2FRAME:
		hwfmt = CAM_TWOPLANE_YVU422;
		break;
	case CAM_YUV420_2FRAME:
	case CAM_YUV420_2FRAME_MIPI:
		hwfmt = CAM_TWOPLANE_YUV420;
		break;
	case CAM_YVU420_2FRAME:
	case CAM_YVU420_2FRAME_MIPI:
		hwfmt = CAM_TWOPLANE_YVU420;
		break;
	case CAM_RAW_8:
		hwfmt = CAM_RAW8;
		break;
	case CAM_RAW_14:
		hwfmt = CAM_HALFWORD_RAW14;
		break;
	default :
		pr_err("fail to get fmt : %d\n", fmt);
		break;
	}

	return hwfmt;
}

struct dcam_dummy_param {
	uint32_t hw_ctx_id;
	enum cam_en_status enable;
	reserved_buf_get_cb resbuf_get_cb;
	void *resbuf_cb_data;
};

inline void dcam_core_offline_reset(struct dcam_hw_context *hw_ctx);
void dcam_core_offline_irq_proc(struct dcam_hw_context *dcam_hw_ctx, struct dcam_irq_info *irq_info);
void *dcam_core_pipe_dev_get(struct cam_hw_info *hw, void *s_dcam_dev);
int dcam_core_pipe_dev_put(void *dcam_handle, void *s_dcam_dev);

#endif
