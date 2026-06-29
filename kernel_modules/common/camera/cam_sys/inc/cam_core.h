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

#ifndef _CAM_CORE_H_
#define _CAM_CORE_H_

#include "cam_pipeline.h"
#include "cam_debugger.h"
#include "cam_scene.h"

#define IMG_DEVICE_NAME                 "sprd_image"
#define CAMERA_TIMEOUT                  5000
#define CAMERA_WATCHDOG_TIMEOUT         1000
#define CAMERA_RECOVERY_TIMEOUT         100
#define CAMERA_LONGEXP_TIMEOUT          50000
#define THREAD_STOP_TIMEOUT             3000
#define FAST_STOP_TIME_OUT              200

#define CAM_COUNT                       CAM_ID_MAX
#define CAM_SHARED_BUF_NUM              50
#define CAM_FRAME_Q_LEN                 96
#define CAM_IMG_Q_LEN                   96
#define CAM_IRQ_Q_LEN                   16
#define CAM_STATIS_Q_LEN                16
#define CAM_ZOOM_COEFF_Q_LEN            64
#define CAM_ALLOC_Q_LEN                 24
#define CAM_RESERVE_BUF_Q_MAX           500
#define CAM_FRAME_DEAL                  1
#define CAM_FRAME_NO_DEAL               0

/* TODO: tuning ratio limit for power/image quality */
#define RATIO_SHIFT                     16

/* TODO: need to pass the num to driver by hal */
#define CAP_NUM_COMMON                  1

#define COMPAT_SPRD_ISP_IO_CFG_PARAM \
	_IOWR(SPRD_IMG_IO_MAGIC, 41, struct compat_isp_io_param)

#define COMPAT_SPRD_IMG_IO_POST_FDR \
	_IOW(SPRD_IMG_IO_MAGIC, 71, struct compat_sprd_img_postproc_param)

#ifndef OPLUS_FEATURE_CAMERA_COMMON
#define OPLUS_FEATURE_CAMERA_COMMON
#endif /* OPLUS_FEATURE_CAMERA_COMMON */

enum camera_module_state {
	CAM_INIT = 0,
	CAM_IDLE,
	CAM_CFG_CH,
	CAM_STREAM_ON,
	CAM_STREAM_OFF,
	CAM_RUNNING,
	CAM_FORCE_CLOSE,
	CAM_ERROR,
};

enum camera_recovery_state {
	CAM_RECOVERY_NONE = 0,
	CAM_RECOVERY_RUNNING,
	CAM_RECOVERY_DONE,
	CAM_RECOVERY_MAX,
};

enum camera_raw_scene {
	CAM_SENSOR_RAW = 0,
	CAM_DCAM_RAW,
	CAM_SCENE_RAW_MAX,
};

struct compat_isp_io_param {
	uint32_t scene_id;
	uint32_t sub_block;
	uint64_t uaddr_fid;
	uint32_t property;
	uint32_t property_param;
};

struct compat_sprd_img_postproc_param {
	enum cam_postproc_scene scene_mode;
	uint32_t channel_id;
	uint32_t index;
	struct sprd_img_size src_size;
	struct sprd_img_size dst_size;
	uint32_t fd_array[SPRD_IMG_POSTPROC_BUF_CNT];
	uint32_t blk_param;
	uint32_t src_imgfmt;
	uint32_t dst_imgfmt;
	struct sprd_img_frm_addr frame_addr_vir_array[SPRD_IMG_POSTPROC_BUF_CNT];
	uint32_t reserved[4];
};

struct camera_uchannel {
	uint32_t sn_fmt;
	uint32_t dst_fmt;

	enum cam_en_status is_high_fps;/* for DCAM slow motion feature */
	uint32_t high_fps_skip_num;/* for DCAM slow motion feature */
	uint32_t slowmotion_stage_a_num;/* for DCAM slow motion feature 60 frame*/
	uint32_t slowmotion_stage_a_valid_num;/* for DCAM slow motion feature */
	uint32_t slowmotion_stage_b_num;/* for DCAM slow motion feature 90 frame*/

	int32_t sensor_raw_fmt;
	int32_t dcam_raw_fmt;
	enum cam_format dcam_out_fmt;
	enum cam_format pyr_out_fmt;

	uint32_t nonzsl_pre_ratio;
	struct sprd_img_rect zoom_ratio_base;
	struct img_size src_size;
	struct img_size dcam_out_size;
	struct sprd_img_rect src_crop;
	struct sprd_img_rect total_src_crop;
	struct img_size dst_size;
	/* need to delete after hal opt.*/
	struct img_size dst_size_tmp;

	/* for close callback stream frame sync */
	uint32_t frame_sync_close;

	struct dcam_regular_desc regular_desc;
	struct sprd_vir_ch_info vir_channel[VIR_CH_NUM];
};

struct camera_uinfo {
	struct sprd_img_sensor_if sensor_if;
	struct img_size sn_size;
	struct img_size sn_max_size;
	struct sprd_img_rect sn_rect;
	uint32_t capture_mode;
	uint32_t capture_skip;
	enum cam_en_status is_longexp;
	enum cam_en_status is_4in1;
	enum cam_en_status is_rgb_ltm;
	enum cam_en_status is_pyr_rec;
	enum cam_en_status is_pyr_dec;
	enum cam_en_status is_rgb_gtm;
	enum cam_en_status is_dual;
	enum camera_sharpness_mode image_sharpness_mode;
	enum camera_slice_mode dcam_slice_mode;/*1: hw,  2:sw*/
	uint32_t slice_num;
	uint32_t slice_count;
	uint32_t zsl_num;
	uint32_t zsk_skip_num;
	uint32_t raw_zsl_num;
	uint32_t raw_zsl_skip_num;
	enum cam_en_status need_share_buf;
	uint32_t zoom_conflict_with_ltm;
	/* for raw alg*/
	enum cam_en_status is_raw_alg;
	enum alg_types alg_type;
	enum cam_rawdata_src rawdata_src;
	uint32_t param_frame_sync;
	/* for dcam raw*/
	enum cam_en_status need_dcam_raw;
	enum cam_en_status virtualsensor;/* 1: virtual sensor 0: normal */
	uint32_t opt_buffer_num;
	uint32_t buf_num;
	uint32_t dual_cache_buf_num;
	uint32_t ipg_skip_first_frm;
	enum cam_en_status zoom_crop_mode;
};

struct sprd_img_flash_info {
	uint32_t led0_ctrl;
	uint32_t led1_ctrl;
	uint32_t led0_status;
	uint32_t led1_status;
	uint32_t flash_last_status;
	struct sprd_img_set_flash set_param;
};

struct nonzsl_pre_info {
	struct cam_zoom_frame latest_zoom_param;
	spinlock_t lastest_zoom_lock;
	struct camera_queue zoom_param_q;
};

struct xtm_zoom_info {
	struct img_size dcam_out;
	struct img_trim trim_c;
	uint32_t isp_scaler_crop_need;
};

struct channel_context {
	enum cam_ch_id ch_id;
	enum cam_en_status enable;
	uint32_t frm_base_id;
	uint32_t frm_cnt;
	uint32_t dump_ee_buf_cnt;
	atomic_t err_status;

	enum cam_en_status compress_en;
	enum cam_en_status compress_3dnr;
	enum cam_en_status compress_offline;

	int32_t dcam_port_id;
	int32_t raw_port_id;
	int32_t aux_dcam_port_id;
	int32_t aux_raw_port_id;
	int32_t isp_port_id;
	enum cam_en_status need_framecache;
	enum cam_en_status need_dcam_raw;
	enum cam_en_status offline_need_dcam_raw;

	uint32_t zsl_buffer_num;
	uint32_t zsl_skip_num;

	enum cam_pipeline_type pipeline_type;
	struct cam_pipeline *pipeline_handle;
	enum cam_pipeline_type nonzsl_statis_pipeline_type;
	struct cam_pipeline *nonzsl_pre_pipeline;

	struct camera_uchannel ch_uinfo;
	struct img_size swap_size;
	struct img_trim trim_dcam;
	struct img_trim total_trim_dcam;
	struct img_trim trim_isp;
	struct img_size dst_dcam;
	struct xtm_zoom_info xtm_zoom;
	enum cam_format dcam_out_fmt; /*for online path*/

	enum cam_en_status alloc_start;
	enum cam_en_status alloc_stop_signal;
	struct completion alloc_com;
	struct completion stream_on_buf_com;
	struct completion fast_stop;
	enum cam_en_status uinfo_3dnr;/* set by hal, 1:hw 3dnr; */
	enum cam_3dnr_type type_3dnr;/* CAM_3DNR_HW:enable hw,and alloc buffer */
	enum isp_ltm_mode mode_ltm;
	enum cam_en_status ltm_rgb;
	enum isp_gtm_mode mode_gtm;
	enum cam_en_status gtm_rgb;
	struct cam_zoom_frame latest_zoom_param;
	struct sprd_img_rect latest_user_crop;/*use to compare with current crop*/
	spinlock_t lastest_zoom_lock;
	struct camera_queue zoom_user_crop_q;/* channel specific coef queue */
	struct camera_queue zoom_param_q;
	struct dcam_isp_k_block blk_pm;
	struct nonzsl_pre_info nonzsl_pre;
};

struct camera_module {
	uint32_t idx;
	atomic_t state;
	atomic_t timeout_flag;
	uint32_t timeout_num;
	struct mutex ioctl_lock;
	enum cam_en_status master_flag; /* master cam capture flag */
	enum cam_en_status compat_flag;
	struct camera_group *grp;
	enum cam_en_status private_key;
	int attach_sensor_id;
	enum cam_en_status iommu_enable;

	enum cam_format raw_cap_fetch_fmt;
	enum cam_cap_type capture_type;

	struct isp_pipe_dev *isp_dev_handle;
	struct dcam_pipe_dev *dcam_dev_handle;
	struct pyrdec_pipe_dev *pyrdec_dev_handle;
	struct cam_flash_task_info *flash_core_handle;
	uint32_t csi_controller_idx;

	uint32_t binning_limit;/* binning limit: 1 - 1/2,  2 - 1/4 */
	uint32_t rds_limit;/* raw downsizer limit */
	enum cam_zoom_type zoom_solution;/* for dynamic zoom type swicth. */
	struct camera_uinfo cam_uinfo;

	enum cam_ch_id last_channel_id;
	struct channel_context channel[CAM_CH_MAX];
	struct mutex buf_lock[CAM_CH_MAX];

	struct completion frm_com;
	struct completion img_com;
	struct completion postproc_done;
	struct camera_queue frm_queue;/* frame message queue for user*/
	struct camera_queue alloc_queue;/* alloc data queue or user*/
	struct camera_queue img_queue;/*img meessage queue for user*/

	struct cam_thread_info zoom_thrd;
	struct cam_thread_info buf_thrd;

	uint32_t opt_frame_fid;/* mul_frame opt frame fid*/
	uint32_t cap_frame_id;
	uint32_t cur_frame_id;/* current frame fid */
	atomic_t lsc_frame_id;
	atomic_t dcam_frame_id;

	struct timer_list cam_timer;

	struct cam_frame *dual_frame;/* 0: no, to find, -1: no need find */
	atomic_t capture_frames_dcam;/* how many frames to report, -1:always */
	atomic_t cap_total_frames;
	atomic_t cap_skip_frames;
	atomic_t cap_zsl_frames;
	atomic_t dual_select_frame_done;
	int64_t capture_times;/* *ns, timestamp get from start_capture */
	enum capture_scene cap_scene;
	enum cam_en_status auto_3dnr;/* 1: enable hw,and alloc buffer before stream on */
	struct sprd_img_flash_info flash_info;
	uint32_t flash_skip_fid;
	enum dcam_path_state path_state;
	enum cam_en_status offline_icap_scene;

	enum cam_en_status raw_callback;
	struct mutex zoom_lock;
	/* cam data path topology info */
	struct cam_scene *static_topology;
	struct cam_nodes_dev nodes_dev;
	struct cam_frame *res_frame;
	int32_t reserved_buf_fd;
	uint32_t reserved_size;
	uint32_t dcam_ctx_bind_state;/* 0: dcam_ctx_unbind, 1: dcam_ctx_bind */
	uint32_t is_flash_status;
	uint32_t simu_fid;
	uint32_t raw_deal_switch;
};

struct camera_group {
	atomic_t camera_opened;
	enum cam_en_status ca_conn;

	struct mutex module_lock;
	uint32_t module_used;
	struct camera_module *module[CAM_COUNT];

	spinlock_t dual_deal_lock;
	uint32_t dcam_count;/*dts cfg dcam count*/
	atomic_t runner_nr; /*running camera num*/
	atomic_t mul_buf_alloced;

	struct miscdevice *md;
	struct platform_device *pdev;
	struct sprd_cam_sec_cfg camsec_cfg;
	struct cam_hw_info *hw_info;
	struct img_size mul_sn_max_size;
	enum cam_en_status is_mul_buf_share;
	struct wakeup_source *ws;

	atomic_t recovery_state;
	struct rw_semaphore switch_recovery_lock;
	struct cam_thread_info recovery_thrd;

	/* cam data path topology info */
	struct cam_scene topology_info;

	struct cam_buf_manager *global_buf_manager;
	struct dcam_pipe_dev *s_dcam_dev;
	struct isp_pipe_dev *s_isp_dev;
};

struct cam_ioctl_cmd {
	unsigned int cmd;
	int (*cmd_proc)(struct camera_module *module, unsigned long arg);
};

uint32_t camcore_dcampath_id_convert(uint32_t path_k_id);
int camcore_pre_frame_status(uint32_t param, void *priv_data);
int camcore_param_start_end(struct camera_module *module, uint32_t scene_id);

#endif
