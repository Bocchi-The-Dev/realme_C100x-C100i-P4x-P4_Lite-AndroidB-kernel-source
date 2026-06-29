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

#ifndef _CAM_TYPES_H_
#define _CAM_TYPES_H_

#include <linux/completion.h>
#include <linux/spinlock.h>
#include <os_adapt_common.h>
#include "sprd_cam.h"
#include "cam_kernel_adapt.h"
#include "cam_thread.h"

#ifndef MAX
#define MAX(__a, __b) (((__a) > (__b)) ? (__a) : (__b))
#endif
#ifndef MIN
#define MIN(__a, __b) (((__a) < (__b)) ? (__a) : (__b))
#endif

#define ZOOM_RATIO_DEFAULT              1000
#define CAM_BUF_ALIGN_SIZE              4
#define PYR_DEC_WIDTH_ALIGN             4
#define PYR_DEC_HEIGHT_ALIGN            2
#define TIME_SIZE_RATIO                 1000000
#define ZOOM_FULL_SIZE_WIDTH_LIMIT      3840

#ifdef PERFORMANCE_DEBUG_ON
#define PERFORMANCE_DEBUG pr_info
#else
#define PERFORMANCE_DEBUG pr_debug
#endif

/* To avoid rec fifo err, isp fetch burst_lens = 8, then MIN_PYR_WIDTH >= 256;
 isp fetch burst_lens = 16, then MIN_PYR_WIDTH >= 256. */
#define MIN_PYR_WIDTH                   256
#define MIN_PYR_HEIGHT                  16
#define ALIGN_UP(a, x)                  (((a) + (x) - 1) & (~((x) - 1)))

/*************** for global debug starts********************/

/* for iommu
 * 0: auto - dts configured
 * 1: iommu off and reserved memory
 * 2: iommu on and reserved memory
 * 3: iommu on and system memory
 */
enum cam_iommu_mode {
	IOMMU_AUTO = 0,
	IOMMU_OFF,
	IOMMU_ON_RESERVED,
	IOMMU_ON,
};
extern int g_dbg_iommu_mode;
extern int g_dbg_set_iommu_mode;
extern uint32_t g_pyr_dec_online_bypass;
extern uint32_t g_pyr_dec_offline_bypass;
extern uint32_t g_dbg_fbc_control;
extern uint32_t g_dbg_recovery;
#define VOID_PTR_TO(from_param, type) \
({	type *__a = NULL; \
	if (!(from_param)) { \
		pr_err("fail to get param\n"); \
		return -1; \
	} \
	__a = (type *)(from_param); \
})

enum dcam_offline_time_wait_status {
	WAIT_IN_NORMAL_SCENE,
	WAIT_IN_INTERRUPT_SCENE,
};

enum cam_en_status {
	CAM_DISABLE,
	CAM_ENABLE,
};

/* for global camera control */
struct cam_global_ctrl {
	uint32_t dcam_zoom_mode;
	uint32_t preview_full_size;
	uint32_t dcam_rds_limit;
	uint32_t isp_wmode;
	uint32_t isp_linebuf_len;
};
extern struct cam_global_ctrl g_camctrl;

/* for memory leak debug */
struct cam_mem_dbg_info {
	atomic_t ion_alloc_cnt;
	atomic_t ion_alloc_size;
	atomic_t ion_kmap_cnt;
	atomic_t ion_dma_cnt;
	atomic_t iommu_map_cnt[6];
	atomic_t empty_frm_cnt;
	atomic_t mem_kzalloc_cnt;
	atomic_t mem_vzalloc_cnt;
};
extern struct cam_mem_dbg_info *g_mem_dbg;

/*************** for global debug ends ********************/

enum camera_slice_mode {
	CAM_SLICE_NONE = 0,
	CAM_OFFLINE_SLICE_HW,
};

enum camera_cap_type {
	CAM_CAP_NORMAL = 0,
	CAM_CAP_RAW_FULL,
	CAM_CAP_RAW_BIN
};

enum camera_sharpness_mode {
	CAM_SHARPNESS_BINNING = 0,
	CAM_SHARPNESS_RDS,
	CAM_SHARPNESS_FULLSIZE
};

enum camera_id {
	CAM_ID_0 = 0,
	CAM_ID_1,
	CAM_ID_2,
	CAM_ID_3,
	CAM_ID_MAX,
};

enum cam_cap_type {
	CAM_CAPTURE_STOP = 0,
	CAM_CAPTURE_START,
	CAM_CAPTURE_RAWPROC,
	CAM_CAPTURE_RAWPROC_DONE,
	CAM_CAPTURE_START_3DNR,
	CAM_CAPTURE_START_WITH_TIMESTAMP,
	CAM_CAPTURE_START_FROM_NEXT_SOF,
	CAM_CAPTURE_MAX,
};

enum cam_ch_id {
	CAM_CH_RAW = 0,
	CAM_CH_PRE,
	CAM_CH_VID,
	CAM_CH_CAP,
	CAM_CH_PRE_THM,
	CAM_CH_CAP_THM,
	CAM_CH_VIRTUAL,
	CAM_CH_DCAM_VCH,
	CAM_CH_MAX,
};

enum pre_raw_status {
	PRE_RAW_CACHE = 0,
	PRE_RAW_DEAL,
	PRE_RAW_MAX,
};

enum dump_channel_type {
	DUMP_CH_RES = 0,
	DUMP_CH_PRE,
	DUMP_CH_CAP,
	DUMP_CH_MAX,
};

enum cam_3dnr_type {
	CAM_3DNR_OFF = 0,
	CAM_3DNR_HW,
	CAM_3DNR_SW,
};

enum cam_data_endian {
	ENDIAN_LITTLE = 0,
	ENDIAN_BIG,
	ENDIAN_HALFBIG,
	ENDIAN_HALFLITTLE,
	ENDIAN_MAX
};

enum dcam_output_strategy {
	IMG_POWER_CONSUM_PRI,
	IMG_QUALITY_PRI,
	IMG_STRATEGY_TYPEMAX,
};

enum cam_zoom_type {
	ZOOM_DEFAULT = 0,
	ZOOM_BINNING2,
	ZOOM_BINNING4,
	ZOOM_SCALER,
	ZOOM_RDS,
	ZOOM_TYPEMAX,
	ZOOM_DEBUG_DEFAULT = 10,
	ZOOM_DEBUG_BINNING2,
	ZOOM_DEBUG_BINNING4,
	ZOOM_DEBUG_SCALER,
	ZOOM_DEBUG_RDS,
	ZOOM_DEBUG_TYPEMAX,
};

enum cam_frame_scene {
	CAM_FRAME_COMMON = 0,
	CAM_FRAME_FDRL,
	CAM_FRAME_FDRH,
	CAM_FRAME_PRE_FDR,
	CAM_FRAME_DRC,
	CAM_FRAME_PROCESS_RAW,
	CAM_FRAME_ORIGINAL_RAW,
};

enum cam_postproc_mode {
	CAM_POSTPROC_DEFAULT,
	CAM_POSTPROC_SERIAL,
	CAM_POSTPROC_PARALLEL,
	CAM_POSTPROC_TYPE_MAX,
};

struct img_addr {
	uint32_t addr_ch0;
	uint32_t addr_ch1;
	uint32_t addr_ch2;
};

struct img_pitch {
	uint32_t pitch_ch0;
	uint32_t pitch_ch1;
	uint32_t pitch_ch2;
};

struct img_size {
	uint32_t w;
	uint32_t h;
};

struct img_border {
	uint32_t border_up;
	uint32_t border_down;
	uint32_t border_left;
	uint32_t border_right;
};

struct img_trim {
	uint32_t start_x;
	uint32_t start_y;
	uint32_t size_x;
	uint32_t size_y;
};

struct img_slice_info {
	uint32_t slice_num;
	uint32_t slice_num_w;
	uint32_t slice_no;
	uint32_t slice_cnt;
	struct img_trim in_trim;
	struct img_trim out_trim;
};

struct img_scaler_info {
	struct img_size src_size;
	struct img_trim src_trim;
	struct img_size dst_size;
};

struct cam_postproc_param {
	uint32_t fid;
	uint32_t src_imgfmt;
	uint32_t dst_imgfmt;
	uint32_t node_type;
	uint32_t dcam_port_id;
	uint32_t isp_port_id;
	uint32_t isp_node_id;
	uint32_t scene_mode;
	uint32_t need_cfg_isp;
	uint32_t need_cfg_dcam;
	uint32_t need_cfg_dec;
	uint32_t need_cfg_blkpm;
	uint32_t need_cfg_zoom;
	uint32_t need_update_zoom;
	uint32_t need_cfg_icap;
	struct channel_context *ch;
	uint64_t blkpm_ptr;
	struct cam_frame *src_frm;
	struct cam_frame *mid_frm;
	struct cam_frame *dst_frm;
	struct cam_frame *icap_dump_frm;
	struct sprd_img_size dst_size;
	enum cam_postproc_mode postproc_mode;
};

enum cam_reserved_buf_type {
	CAM_RESERVED_BUFFER_OFF = 0,
	CAM_RESERVED_BUFFER_ORI = 1,
	CAM_RESERVED_BUFFER_COPY,
};

struct dcam_compress_info {
	uint32_t is_compress;
	uint32_t tile_col;
	uint32_t tile_row;
	uint32_t tile_row_lowbit;
	uint32_t header_size;
	uint32_t payload_size;
	uint32_t lowbits_size;
	uint32_t buffer_size;
};

struct compressed_addr {
	uint32_t addr0;
	uint32_t addr1;
	uint32_t addr2;
	uint32_t addr3;
};

struct dcam_compress_cal_para {
	uint32_t fmt;
	uint32_t data_bits;
	uint32_t width;
	uint32_t height;
	struct dcam_compress_info *fbc_info;
	unsigned long in;
	struct compressed_addr *out;
};

/* TBD: need move to sprd_camsys.h later */
/* dev fetch & store data type*/
enum cam_format {
	CAM_RAW_BASE = 0x10,
	CAM_RAW_PACK_10 = 0x0,
	CAM_RAW_HALFWORD_10,
	CAM_RAW_14,
	CAM_RAW_8,
	CAM_RGB_BASE = 0x20,
	CAM_FULL_RGB10,
	CAM_FULL_RGB14,
	CAM_YUV_BASE = 0x40,
	CAM_YUV422_2FRAME,
	CAM_YVU422_2FRAME,
	CAM_YUV420_2FRAME,
	CAM_YVU420_2FRAME,
	CAM_YUV420_2FRAME_10,
	CAM_YVU420_2FRAME_10,
	CAM_YUV420_2FRAME_MIPI,
	CAM_YVU420_2FRAME_MIPI,
	CAM_YUV422_3FRAME,
	CAM_YUV420_3FRAME,
	CAM_YUYV_1FRAME,
	CAM_UYVY_1FRAME,
	CAM_YVYU_1FRAME,
	CAM_VYUY_1FRAME,
	CAM_FORMAT_MAX
};

/* dev fetch & store data bw*/
enum cam_bit_width {
	CAM_UNSUPPORT = 0,
	CAM_1_BITS = 1,
	CAM_2_BITS = 2,
	CAM_4_BITS = 4,
	CAM_8_BITS = 8,
	CAM_10_BITS = 10,
	CAM_12_BITS = 12,
	CAM_14_BITS = 14,
	CAM_BIT_MAX
};

struct cam_capture_param {
	uint32_t cap_type;
	uint32_t cap_scene;
	atomic_t cap_cnt;
	int64_t cap_timestamp;
	uint32_t cap_opt_frame_scene;
	uint32_t offline_icap_scene;
	uint32_t no_need_skip_frame_scene;
	struct sprd_img_rect cap_user_crop;
	uint32_t skip_first_num;
	uint32_t zsl_num;
	enum cam_node_frmsel_mode frm_sel_mode;
	uint32_t fid;
	uint32_t pre_raw_sel;
	uint32_t cur_raw_sel;
	uint32_t update_online_raw_link;
	uint32_t alg_type;
};

struct cam_buf_alloc_desc {
	uint32_t height;
	uint32_t width;
	uint32_t ch_id;
	enum cam_en_status compress_en;
	uint32_t compress_offline;
	uint32_t is_pyr_rec;
	uint32_t is_pyr_dec;
	uint32_t pyr_out_fmt;
	uint32_t pyr_layer_num;
	uint32_t cam_idx;
	uint32_t dcamonline_buf_alloc_num;
	uint32_t dcamoffline_buf_alloc_num;
	uint32_t dcamoffline_lsc_buf_alloc_num;
	uint32_t cam_copy_buf_alloc_num;
	uint32_t share_buffer;
	uint32_t iommu_enable;
	uint32_t is_super_size;
	uint32_t ch_vid_enable;
	struct img_size dst_size;
	struct img_size chvid_dst_size;
	uint32_t stream_on_need_buf_num;
	struct completion *stream_on_buf_com;
	uint32_t *alloc_stop_signal;
	uint32_t nr3_enable;
	uint32_t compress_3dnr;
	uint32_t dcam_out_fmt;
	uint32_t ltm_mode;
	uint32_t ltm_rgb_enable;
	uint32_t is_static_map;
	uint32_t not_to_isp;
	uint32_t alg_type;
};

struct slowmotion_960fps_info {
	uint32_t slowmotion_stage_a_num;
	uint32_t slowmotion_stage_b_num;
	uint32_t slowmotion_stage_a_valid_num;
};

enum shutoff_scene {
	SHUTOFF_MULTI_PORT_SWITCH = 0,
	SHUTOFF_SINGLE_PORT_NONZSL,
	SHUTOFF_SINGLE_PORT_NORMAL,
	SHUTOFF_SCENE_MAX,
};

enum shutoff_type {
	SHUTOFF_PAUSE = 0,
	SHUTOFF_RESUME,
	SHUTOFF_TYPE_MAX,
};

uint32_t cam_data_bits(uint32_t dcam_out_fmt);
uint32_t cam_is_pack(uint32_t dcam_out_fmt);
uint32_t cam_format_get(uint32_t img_pix_fmt);
uint32_t cam_cal_hw_pitch(uint32_t w, uint32_t format);
uint32_t cam_byte_get(uint32_t format);
int cam_raw_fmt_get(uint32_t fmt);
int dcampath_bin_scaler_get(struct img_size crop, struct img_size dst, uint32_t *scaler_sel, uint32_t *bin_ratio);
int cam_valid_fmt_get(int32_t *fmt, uint32_t default_value);

uint32_t dcamonline_portid_convert_to_pathid(uint32_t port_id);
uint32_t dcamoffline_portid_convert_to_pathid(uint32_t port_id);
uint32_t dcamoffline_pathid_convert_to_portid(uint32_t path_id);
uint32_t dcamonline_pathid_convert_to_portid(uint32_t path_id);

typedef int(*pyr_dec_buf_cb)(void *param, void *cb_handle);
typedef int(*reserved_buf_get_cb)(void *param, void *cb_handle);
typedef struct cam_frame *(*dual_frame_sync_cb)(void *param, void *cb_handle, int *flag);
typedef int(*dual_slave_frame_set_cb)(void *param, void *cb_handle);
typedef int(*cap_frame_status_cb)(uint32_t param, void *cb_handle);
typedef int(*port_cfg_cb)(void *param, uint32_t cmd, void *cb_handle);
typedef int(*shutoff_cfg_cb)(void *cb_handle, uint32_t cmd, void *param);
typedef int(*dcam_irq_proc_cb)(void *param, void *cb_handle);
typedef int(*zoom_get_cb)(void *cb_handle, void *param);
typedef int(*cam_zoom_get_cb)(uint32_t param, void *cb_handle, void *zoom_param);
typedef int(*cam_slice_cb)(void *param, void *cb_handle);
typedef int(*port_param_cfg)(void *cb_handle, uint32_t cmd, void *param);

#endif/* _CAM_TYPES_H_ */
