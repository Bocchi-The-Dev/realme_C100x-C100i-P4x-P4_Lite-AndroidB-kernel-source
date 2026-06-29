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

#ifndef _CAM_HW_H_
#define _CAM_HW_H_

#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "alg_nr3_calc.h"
#include "cam_block.h"
#include "isp_hw.h"

extern struct cam_hw_info sharkl3_hw_info;
extern struct cam_hw_info sharkl5_hw_info;
extern struct cam_hw_info sharkl5pro_hw_info;
extern struct cam_hw_info qogirl6_hw_info;
extern struct cam_hw_info qogirn6pro_hw_info;
extern struct cam_hw_info qogirn6l_hw_info;

typedef int (*hw_ioctl_fun)(void *handle, void *arg);
typedef int (*isp_k_blk_func)(void *handle);
typedef int (*pyrdec_k_blk_func)(void *handle);

/* The project id must keep same with the DT cfg
 * new added project should always added in the end
 */
enum cam_prj_id {
	SHARKL3,
	SHARKL5,
	ROC1,
	SHARKL5pro,
	QOGIRN6pro,
	QOGIRL6,
	QOGIRN6L,
	PROJECT_MAX
};

enum isp_default_type {
	ISP_HW_PARA,
	ISP_CFG_PARA,
	ISP_MAX_PARA
};

enum cam_block_type {
	DCAM_BLOCK_TYPE,
	ISP_BLOCK_TYPE,
	DEC_BLOCK_TYPE,
	MAX_BLOCK_TYPE
};

enum isp_k_blk_idx {
	ISP_K_BLK_LTM,
	ISP_K_BLK_PYR_REC_BYPASS,
	ISP_K_BLK_PYR_REC_SHARE,
	ISP_K_BLK_PYR_REC_FRAME,
	ISP_K_BLK_PYR_REC_SLICE,
	ISP_K_BLK_PYR_REC_SLICE_COMMON,
	ISP_K_BLK_DEWARP_CACHE_CFG,
	ISP_K_BLK_DEWARP_CFG,
	ISP_K_BLK_DEWARP_SLICE,
	ISP_K_BLK_NLM_UPDATE,
	ISP_K_BLK_IMBLANCE_UPDATE,
	ISP_K_BLK_YNR_UPDATE,
	ISP_K_BLK_CNR_UPDATE,
	ISP_K_BLK_POST_CNR_UPDATE,
	ISP_K_BLK_EDGE_UPDATE,
	ISP_K_BLK_MAX
};

enum pyrdec_k_blk_idx {
	PYR_DEC_K_BLK_IRQ_FUNC,
	PYR_DEC_K_BLK_CFG,
	PYR_DEC_K_BLK_DCT_UPDATE,
	PYR_DEC_K_BLK_MAX
};

enum isp_k_gtm_blk_idx {
	ISP_K_GTM_LTM_EB,
	ISP_K_GTM_LTM_DIS,
	ISP_K_GTM_STATUS_GET,
	ISP_K_GTM_BLOCK_SET,
	ISP_K_GTM_MAPPING_SET,
	ISP_K_GTM_SLICE_SET,
	ISP_K_GTM_BYPASS_SET,
	ISP_K_GTM_MAX
};

/* TBD: need move to sprd_camsys.h later */
enum dcam_full_src_sel_type {
	ORI_RAW_SRC_SEL,
	PROCESS_RAW_SRC_SEL,
	LSC_RAW_SRC_SEL,
	BPC_RAW_SRC_SEL,
	NLM_RAW_SRC_SEL,
	MAX_RAW_SRC_SEL
};

/* 0 for normal dec, 1 for receive 4 yuv sensor data */
enum dcam_dec_path_sel {
	DACM_DEC_PATH_DEC,
	DCAM_DEC_PATH_YUV_DECI,
	DCAM_DEC_PATH_MAX,
};

enum cam_reg_trace_type {
	NORMAL_REG_TRACE,
	ABNORMAL_REG_TRACE,
	MAX_REG_TRACE
};

enum cam_bypass_type {
	DCAM_BYPASS_TYPE,
	ISP_BYPASS_TYPE,
	MAX_BYPASS_TYPE
};

enum isp_sub_path_id {
	ISP_SPATH_CP = 0,
	ISP_SPATH_VID,
	ISP_SPATH_FD,
	ISP_SPATH_NUM,
};

enum dcam_fbc_path {
	DCAM_FBC_PATH_BIN,
	DCAM_FBC_PATH_FULL,
	DCAM_FBC_PATH_RAW,
	DCAM_FBC_PATH_NUM,
};

/* only temp for N6pro, after isp reset eco, need delete*/
enum isp_reset_flag {
	ISP_RESET_AFTER_POWER_ON,
	ISP_RESET_BEFORE_POWER_OFF,
	ISP_RESET_FLAG_MAX,
};

enum dcam_hw_cfg_cmd {
	DCAM_HW_CFG_ENABLE_CLK,
	DCAM_HW_CFG_DISABLE_CLK,
	DCAM_HW_CFG_INIT_AXI,
	DCAM_HW_CFG_FMCU_RESET,
	DCAM_HW_CFG_SET_QOS,
	DCAM_HW_CFG_RESET,
	DCAM_HW_CFG_START,
	DCAM_HW_CFG_STOP,
	DCAM_HW_CFG_FETCH_START,
	DCAM_HW_CFG_RECORD_ADDR,
	DCAM_HW_CFG_AUTO_COPY,
	DCAM_HW_CFG_FORCE_COPY,
	DCAM_HW_CFG_PATH_START,
	DCAM_HW_CFG_PATH_CTRL,
	DCAM_HW_CFG_PATH_SIZE_UPDATE,
	DCAM_HW_CFG_MIPI_CAP_SET,
	DCAM_HW_CFG_FETCH_SET,
	DCAM_HW_CFG_BINNING_4IN1_SET,
	DCAM_HW_CFG_SRAM_CTRL_SET,
	DCAM_HW_CFG_LBUF_SHARE_SET,
	DCAM_HW_CFG_LBUF_SHARE_GET,
	DCAM_HW_CFG_SLICE_FETCH_SET,
	DCAM_HW_CFG_SLICE_STORE_SET,
	DCAM_HW_CFG_FBC_CTRL,
	DCAM_HW_CFG_FBC_ADDR_SET,
	DCAM_HW_CFG_BLOCKS_SETALL,
	DCAM_HW_CFG_BLOCKS_SETSTATIS,
	DCAM_HW_CFG_DISCONECT_CSI,
	DCAM_HW_CFG_CONECT_CSI,
	DCAM_HW_CFG_FORCE_EN_CSI,
	DCAM_HW_CFG_FETCH_STATUS_GET,
	DCAM_HW_CFG_BAYER_HIST_ROI_UPDATE,
	DCAM_HW_CFG_STORE_ADDR,
	DCAM_HW_CFG_FMCU_CMD,
	DCAM_HW_CFG_FMCU_START,
	DCAM_HW_CFG_FMCU_EBABLE,
	DCAM_HW_CFG_SLW_FMCU_CMDS,
	DCAM_HW_CFG_GTM_LTM_EB,
	DCAM_HW_CFG_GTM_LTM_DIS,
	DCAM_HW_CFG_DEC_ONLINE,
	DCAM_HW_CFG_BYPASS_DEC,
	DCAM_HW_CFG_DEC_STORE_ADDR,
	DCAM_HW_CFG_DEC_SIZE_UPDATE,
	DCAM_HW_CFG_GTM_HIST_GET,
	DCAM_HW_CFG_ALL_RESET,
	DCAM_HW_CFG_IRQ_DISABLE,
	DCAM_HW_CFG_SLW_ADDR,
	DCAM_HW_CFG_DUMMY_SET,
	DCAM_HW_CFG_DUMMY_ENABLE,
	DCAM_HW_CFG_GTM_HIST_BYPASS_GET,
	DCAM_HW_CFG_DIS_SN_SOF,
	DCAM_HW_CFG_DIS_SN_EOF,
	DCAM_HW_CFG_SLICE_IMBLANCE_SET,
	DCAM_HW_CFG_NONZSL_BLOCK_PARAM,
	DCAM_HW_CFG_MIPI_CAP_FETCH_RESET,
	DCAM_HW_CFG_PATH_SRC_SEL,
	DCAM_HW_CFG_CSI_REG_TRACE,
	DCAM_HW_CFG_MAX
};

enum isp_hw_cfg_cmd {
	ISP_HW_CFG_ENABLE_CLK,
	ISP_HW_CFG_DISABLE_CLK,
	ISP_HW_CFG_RESET,
	ISP_HW_CFG_ENABLE_IRQ,
	ISP_HW_CFG_DISABLE_IRQ,
	ISP_HW_CFG_CLEAR_IRQ,
	ISP_HW_CFG_FETCH_SET,
	ISP_HW_CFG_FETCH_FBD_SET,
	ISP_HW_CFG_DEFAULT_PARA_SET,
	ISP_HW_CFG_DEFAULT_PARA_CFG,
	ISP_HW_CFG_PARAM_BLOCK_FUNC_GET,
	ISP_HW_CFG_K_BLK_FUNC_GET,
	ISP_HW_CFG_CFG_MAP_INFO_GET,
	ISP_HW_CFG_ABNORMAL_UEVENT,
	ISP_HW_CFG_FMCU_VALID_GET,
	ISP_HW_CFG_BYPASS_DATA_GET,
	ISP_HW_CFG_BYPASS_COUNT_GET,
	ISP_HW_CFG_REG_TRACE,
	ISP_HW_CFG_ISP_CFG_SUBBLOCK,
	ISP_HW_CFG_SET_PATH_STORE,
	ISP_HW_CFG_SET_PATH_SCALER,
	ISP_HW_CFG_SET_PATH_THUMBSCALER,
	ISP_HW_CFG_SLICE_SCALER,
	ISP_HW_CFG_SLICE_STORE,
	ISP_HW_CFG_FBD_SLICE_SET,
	ISP_HW_CFG_FBD_ADDR_SET,
	ISP_HW_CFG_LTM_SLICE_SET,
	ISP_HW_CFG_LTM_SLICE,
	ISP_HW_CFG_NR3_FBC_SLICE_SET,
	ISP_HW_CFG_NR3_FBD_SLICE_SET,
	ISP_HW_CFG_SLW_FMCU_CMDS,
	ISP_HW_CFG_FMCU_CFG,
	ISP_HW_CFG_SLICE_FETCH,
	ISP_HW_CFG_SLICE_HIST2_SET,
	ISP_HW_CFG_SLICE_NR_INFO,
	ISP_HW_CFG_SLICE_FMCU_CMD,
	ISP_HW_CFG_SLICE_FMCU_PYR_REC_CMD,
	ISP_HW_CFG_SLICE_NOFILTER,
	ISP_HW_CFG_SLICE_HIST2,
	ISP_HW_CFG_SLICE_3DNR_CROP,
	ISP_HW_CFG_SLICE_3DNR_STORE,
	ISP_HW_CFG_SLICE_3DNR_MEMCTRL,
	ISP_HW_CFG_SLICE_SPATH_STORE,
	ISP_HW_CFG_SLICE_SPATH_SCALER,
	ISP_HW_CFG_SLICE_SPATH_THUMBSCALER,
	ISP_HW_CFG_SET_SLICE_FETCH,
	ISP_HW_CFG_SET_SLICE_NR_INFO,
	ISP_HW_CFG_LTM_PARAM,
	ISP_HW_CFG_3DNR_PARAM,
	ISP_HW_CFG_GET_NLM_YNR,
	ISP_HW_CFG_STOP,
	ISP_HW_CFG_STORE_FRAME_ADDR,
	ISP_HW_CFG_FETCH_FRAME_ADDR,
	ISP_HW_CFG_MAP_INIT,
	ISP_HW_CFG_CMD_READY,
	ISP_HW_CFG_START_ISP,
	ISP_HW_CFG_UPDATE_HIST_ROI,
	ISP_HW_CFG_HIST_READ_VAL,
	ISP_HW_CFG_FETCH_START,
	ISP_HW_CFG_FMCU_START,
	ISP_HW_CFG_YUV_BLOCK_BYPASS,
	ISP_HW_CFG_SUBBLOCK_RECFG,
	ISP_HW_CFG_FMCU_CMD_ALIGN,
	ISP_HW_CFG_GTM_FUNC_GET,
	ISP_HW_CFG_GTMHIST_GET,
	ISP_HW_CFG_MMU_FACEID_RECFG,
	ISP_HW_CFG_BLOCKPARAM_CFG,
	ISP_HW_CFG_GET_ABNORMAL_REG_TAB,
	ISP_HW_CFG_MAX
};

enum pyrdec_hw_cfg_cmd {
	PYRDEC_HW_CFG_K_BLK_FUNC_GET,
	PYRDEC_HW_CFG_MAX
};

enum cam_hw_cfg_cmd {
	CAM_HW_GET_ALL_RST,
	CAM_HW_GET_AXI_BASE,
	CAM_HW_GET_DCAM_DTS_CLK,
	CAM_HW_GET_ISP_DTS_CLK,
	CAM_HW_GET_BLK_FUN,
	CAM_HW_GET_BLOCK_TYPE,
	CAM_HW_GET_CSI_SWITCH,
	CAM_HW_GET_MAX
};

enum dcam_path_ctrl {
	HW_DCAM_PATH_PAUSE = 0,
	HW_DCAM_PATH_RESUME,
	HW_DCAM_PATH_MAX
};

enum isp_fetch_type {
	FETCH_NORMAL_FMT = 0,
	FETCH_PYR_FMT,
	FETCH_3DNR_FMT,
	FETCH_ALL_MAX
};

/*
 * Enumerating output paths in dcam_if device.
 *
 * @DCAM_PATH_FULL: with biggest line buffer, full path is often used as capture
 *                  path. Crop function available on this path.
 * @DCAM_PATH_BIN:  bin path is used as preview path. Crop and scale function
 *                  available on this path.
 * @DCAM_PATH_PDAF: this path is used to receive PDAF data
 * @DCAM_PATH_VCH2: can receive data according to data_type or
 *                  virtual_channel_id in a MIPI packet
 * @DCAM_PATH_VCH3: receive all data left
 * @DCAM_PATH_AEM:  output exposure by blocks
 * @DCAM_PATH_AFM:  output focus related data
 * @DCAM_PATH_AFL:  output anti-flicker data, including global data and region
 *                  data
 * @DCAM_PATH_HIST: output bayer histogram data in RGB channel
 * @DCAM_PATH_FRGB_HIST: n6pro only
 * @DCAM_PATH_3DNR: output noise reduction data
 * @DCAM_PATH_BPC:  output bad pixel data
 */
enum dcam_path_index {
	DCAM_PATH_FULL = 0,
	DCAM_PATH_BIN,
	DCAM_PATH_RAW,
	DCAM_PATH_PDAF,
	DCAM_PATH_VCH2,
	DCAM_PATH_VCH3,
	DCAM_PATH_AEM,
	DCAM_PATH_AFM,
	DCAM_PATH_AFL,
	DCAM_PATH_HIST,
	DCAM_PATH_FRGB_HIST,
	DCAM_PATH_3DNR,
	DCAM_PATH_BPC,
	DCAM_PATH_LSCM,
	DCAM_PATH_GTM_HIST,
	DCAM_PATH_MAX,
};

enum isp_fetch_path_select {
	ISP_FETCH_PATH_NORMAL = 0,
	ISP_FETCH_PATH_FBD,
	ISP_FETCH_PATH_MAX
};

struct isp_fbd_yuv_info {
	uint32_t ctx_id;
	uint32_t data_bits;
	uint32_t hblank_en;
	uint32_t afbc_mode;
	uint32_t buffer_size;
	uint32_t hblank_num;
	uint32_t start_isp_afbd;
	uint32_t tile_num_pitch;
	uint32_t fetch_fbd_bypass;
	uint32_t chk_sum_auto_clr;
	uint32_t slice_start_pxl_xpt;
	uint32_t slice_start_pxl_ypt;
	uint32_t dout_req_signal_type;
	uint32_t slice_start_header_addr;
	uint32_t frame_header_base_addr;
	struct img_trim trim;
	struct img_size slice_size;
	struct compressed_addr hw_addr;
};

struct isp_hw_fetch_info {
	uint32_t ctx_id;
	uint32_t dispatch_color;
	enum isp_fetch_path_select fetch_path_sel;
	uint32_t bayer_pattern;
	enum sprd_cam_sec_mode sec_mode;
	enum cam_format fetch_fmt;
	enum cam_format fetch_pyr_fmt;
	enum cam_format fetch_3dnr_fmt;
	struct img_size src;
	struct img_trim in_trim;
	struct img_addr addr;
	struct img_addr addr_dcam_out; /* only for N6L cap rec layer0 form dcam store4 */
	struct img_addr trim_off;
	struct img_addr addr_hw;
	struct img_pitch pitch;
	uint32_t mipi_byte_rel_pos;
	uint32_t mipi_word_num;
	uint32_t mipi_byte_rel_pos_uv;
	uint32_t mipi_word_num_uv;
};

struct store_border {
	uint32_t up_border;
	uint32_t down_border;
	uint32_t left_border;
	uint32_t right_border;
};

struct isp_hw_afbc_path {
	uint32_t ctx_id;
	enum isp_sub_path_id spath_id;
};

struct isp_store_info {
	uint32_t bypass;
	uint32_t endian;
	uint32_t speed_2x;
	uint32_t need_bwd;
	uint32_t mirror_en;
	uint32_t max_len_sel;
	uint32_t shadow_clr;
	uint32_t store_res;
	uint32_t rd_ctrl;
	uint32_t shadow_clr_sel;
	uint32_t total_size;
	enum cam_format color_fmt;
	struct img_size size;
	struct img_addr addr;
	struct img_addr slice_offset;
	struct img_pitch pitch;
};

struct isp_hw_path_store {
	uint32_t ctx_id;
	uint32_t used;
	enum cam_en_status buf_sec;
	enum isp_sub_path_id spath_id;
	enum sprd_cam_sec_mode sec_mode;
	struct isp_store_info store;
};

struct scaler_phase_info {
	int32_t scaler_init_phase[2];
	/* [hor/ver][luma/chroma] */
	int16_t scaler_init_phase_int[2][2];
	/* [hor/ver][luma/chroma] */
	uint16_t scaler_init_phase_rmd[2][2];
};

struct yuv_scaler_info {
	uint32_t scaler_bypass;
	uint32_t scaler_path_stop;
	uint32_t odata_mode;
	uint32_t scaler_y_hor_tap;
	uint32_t scaler_uv_hor_tap;
	uint32_t scaler_y_ver_tap;
	uint32_t scaler_uv_ver_tap;
	uint32_t scaler_ip_int;
	uint32_t scaler_ip_rmd;
	uint32_t scaler_cip_int;
	uint32_t scaler_cip_rmd;
	uint32_t scaler_factor_in;
	uint32_t scaler_factor_out;
	uint32_t scaler_ver_ip_int;
	uint32_t scaler_ver_ip_rmd;
	uint32_t scaler_ver_cip_int;
	uint32_t scaler_ver_cip_rmd;
	uint32_t scaler_ver_factor_in;
	uint32_t scaler_ver_factor_out;
	uint32_t scaler_out_width;
	uint32_t scaler_out_height;
	uint32_t work_mode;
	uint32_t coeff_buf[ISP_SC_COEFF_BUF_SIZE];
	struct scaler_phase_info init_phase_info;
};

struct isp_regular_info {
	uint32_t regular_mode;
	uint32_t shrink_uv_dn_th;
	uint32_t shrink_uv_up_th;
	uint32_t shrink_y_dn_th;
	uint32_t shrink_y_up_th;
	uint32_t effect_v_th;
	uint32_t effect_u_th;
	uint32_t effect_y_th;
	uint32_t shrink_c_range;
	uint32_t shrink_c_offset;
	uint32_t shrink_y_range;
	uint32_t shrink_y_offset;
};

struct img_deci_info {
	uint32_t deci_y_eb;
	uint32_t deci_y;
	uint32_t deci_x_eb;
	uint32_t deci_x;
};

struct isp_hw_path_scaler {
	uint32_t ctx_id;
	uint32_t uv_sync_v;
	uint32_t frm_deci;
	uint32_t path_sel;
	enum isp_sub_path_id spath_id;
	struct img_deci_info deci;
	struct img_size src;
	struct img_trim in_trim;
	struct img_trim out_trim;
	struct img_size dst;
	struct yuv_scaler_info scaler;
	struct isp_regular_info regular_info;
};

struct isp_hw_hist_roi {
	uint32_t ctx_id;
	uint32_t bypass;
	struct isp_coord *hist_roi;
};

struct isp_hw_hist_val {
	uint32_t *hist_buf;
	uint32_t sum;
};

struct isp_hw_cfg_map {
	atomic_t map_cnt;
	struct isp_dev_cfg_info *s_cfg_settings;
};

struct isp_hw_set_slice_nr_info {
	uint32_t start_row_mod4;
	struct slice_nlm_info *slice_nlm;
	struct slice_imblance_info *slice_imblance;
	struct slice_ynr_info *slice_ynr;
	struct isp_fmcu_ctx_desc *fmcu;
	struct slice_postcnr_info *slice_postcnr_info;
	struct slice_edge_info *slice_edge;
};

struct isp_hw_set_slice_fetch {
	uint32_t ctx_id;
	struct isp_fmcu_ctx_desc *fmcu;
	struct slice_fetch_info *fetch_info;
};

struct isp_hw_slice_3dnr_memctrl {
	struct isp_fmcu_ctx_desc *fmcu;
	struct slice_3dnr_memctrl_info *mem_ctrl;
};

struct isp_hw_slice_3dnr_store {
	struct isp_fmcu_ctx_desc *fmcu;
	struct slice_3dnr_store_info *store;
};

struct isp_hw_slice_3dnr_crop {
	struct isp_fmcu_ctx_desc *fmcu;
	struct slice_3dnr_crop_info *crop;
};

struct isp_hw_slice_nofilter {
	struct slice_noisefilter_info *noisefilter_info;
	struct isp_fmcu_ctx_desc *fmcu;
};

struct isp_hw_slice_hist2 {
	struct isp_fmcu_ctx_desc *fmcu;
	struct slice_hist2_info *slice_hist2;
	uint32_t idx;
};

struct dcam_hw_fetch_block {
	uint32_t idx;
	uint32_t raw_fetch_count;
};

struct dcam_hw_cfg_mipicap {
	uint32_t idx;
	uint32_t reg_val;
};

struct cam_hw_gtm_update {
	uint32_t gtm_idx;
	uint32_t idx;
	spinlock_t glb_reg_lock;
	struct dcam_isp_k_block *blk_dcam_pm;
	struct cam_hw_info *hw;
};

struct cam_cfg_block {
	uint32_t sub_block;
	uint32_t property;
	uint32_t block_type;
};

struct dcam_rds_slice_ctrl {
	uint32_t rds_input_h_global;
	uint32_t rds_input_w_global;
	uint32_t rds_output_h_global;
	uint32_t rds_output_w_global;
	uint32_t rds_init_phase_int1;
	uint32_t rds_init_phase_int0;
	uint32_t rds_init_phase_rdm1;
	uint32_t rds_init_phase_rdm0;
};

struct dcam_hw_gtm_hist {
	uint32_t idx;
	uint32_t *value;
};

struct dcam_hw_scaler {
	struct img_size src_size;
	struct img_size dst_size;
	struct img_trim im_trim0;
	struct img_trim im_trim1;
};

struct dcam_slice_store_param {
	enum cam_en_status bin_en;
	uint32_t pitch;
	uint32_t store_offset[2];
	struct store_border out_border;
	struct store_border fbc_border;
	struct img_trim crop;
	struct img_size store_size;
};

struct dcam_slice_imblance_info {
	uint32_t idx;
	uint32_t global_x_start;
	uint32_t global_y_start;
};

struct dcam_hw_slice_param {
	uint32_t idx;
	uint32_t is_compress;
	uint32_t virtualsensor_pre_sof;
	uint32_t path_id;
	uint32_t relative_offset;
	uint32_t fetch_offset;
	enum cam_en_status is_last_slice;
	struct dcam_fetch_info *fetch;
	struct img_trim *cur_slice;
	struct img_size fetch_size;
	struct dcam_hw_scaler bin_scaler;
	struct dcam_hw_scaler full_scaler;
	struct dcam_slice_store_param bin_store;
	struct dcam_slice_store_param full_store;
};

struct dcam_hw_sram_ctrl {
	uint32_t sram_ctrl_en;
	uint32_t idx;
};

struct dcam_hw_binning_4in1 {
	uint32_t idx;
	uint32_t binning_4in1_en;
};

struct dcam_hw_path_size {
	uint32_t idx;
	uint32_t size_x;
	uint32_t size_y;
	uint32_t path_id;
	uint32_t src_sel;
	uint32_t bin_ratio;
	uint32_t scaler_sel;
	uint32_t out_pitch;
	struct img_size in_size;
	struct img_trim in_trim;
	struct img_size out_size;
	struct img_deci_info deci;
	struct dcam_compress_info compress_info;
	struct yuv_scaler_info *scaler_info;
	uint32_t rds_coeff_size;
	void *rds_coeff_buf;
	uint32_t rds_init_phase_int1;
	uint32_t rds_init_phase_int0;
	uint32_t rds_init_phase_rdm1;
	uint32_t rds_init_phase_rdm0;
};

struct dcam_hw_auto_copy {
	uint32_t id;
	uint32_t idx;
	spinlock_t glb_reg_lock;
};

struct dcam_hw_path_stop {
	uint32_t idx;
	uint32_t path_id;
	enum alg_types alg_type;
};

struct dcam_hw_path_restart {
	uint32_t idx;
	uint32_t path_id;
	enum alg_types alg_type;
};

struct dcam_fetch_info {
	enum cam_format fmt;
	uint32_t endian;
	uint32_t pattern;
	struct img_size size;
	struct img_trim trim;
	struct img_addr addr;
};

struct dcam_mipi_info {
	uint32_t sensor_if;
	uint32_t format;
	uint32_t mode;
	uint32_t data_bits;
	uint32_t pattern;
	uint32_t href;
	uint32_t frm_deci;
	uint32_t frm_skip;
	uint32_t x_factor;
	uint32_t y_factor;
	uint32_t is_4in1;
	uint32_t dcam_slice_mode;
	uint32_t is_cphy;
	uint32_t ipg_skip_first_frm;
	uint32_t fetch_online_en;
	struct img_trim cap_size;
};

struct dcam_hw_mipi_cap {
	uint32_t idx;
	struct dcam_mipi_info cap_info;
	uint32_t slowmotion_count;
};

struct dcam_hw_path_start {
	uint32_t path_id;
	uint32_t idx;
	uint32_t slowmotion_count;
	uint32_t pdaf_path_eb;
	/*out para*/
	uint32_t out_fmt;
	uint32_t endian;
	/*in para*/
	uint32_t src_sel;
	uint32_t bayer_pattern;
	uint32_t pdaf_type;
	struct img_trim in_trim;
	struct dcam_mipi_info cap_info;
};

struct dcam_hw_fetch_set {
	uint32_t idx;
	uint32_t simu_mode;
	uint32_t virtualsensor_pre_sof;
	uint32_t fetch_online_en;
	struct dcam_fetch_info *fetch_info;
};

struct dcam_hw_force_copy {
	uint32_t id;
	uint32_t idx;
	spinlock_t glb_reg_lock;
};

struct dcam_hw_start {
	uint32_t idx;
	uint32_t raw_callback;
	spinlock_t glb_reg_lock;
	struct dcam_mipi_info cap_info;
};

struct reg_add_val_tag {
	unsigned int addr;
	unsigned int valid;
	unsigned int dvalue;
	unsigned int rw;
	unsigned int wc;
};

struct coeff_arg {
	uint32_t *h_coeff;
	uint32_t *h_chroma_coeff;
	uint32_t *v_coeff;
	uint32_t *v_chroma_coeff;
	uint32_t h_coeff_addr;
	uint32_t h_chroma_coeff_addr;
	uint32_t v_coeff_addr;
	uint32_t v_chroma_coeff_addr;
};

struct dcam_hw_path_ctrl {
	uint32_t path_id;
	uint32_t idx;
	uint32_t raw_sel;
	enum dcam_path_ctrl type;
};

typedef int (*func_isp_cfg_block_param)(
	struct dcam_isp_k_block *param_block,
	struct dcam_isp_k_block *isp_k_param);

typedef int (*func_cam_cfg_param)
			(struct isp_io_param *param, struct dcam_isp_k_block *p);

struct isp_cfg_pre_param {
	uint32_t sub_block;
	func_isp_cfg_block_param cfg_block_func;
};

struct isp_cfg_block_param {
	uint32_t index;
	struct isp_cfg_pre_param *isp_param;
};

struct isp_hw_k_blk_func {
	enum isp_k_blk_idx index;
	isp_k_blk_func k_blk_func;
};

struct pyrdec_hw_k_blk_func {
	enum pyrdec_k_blk_idx index;
	pyrdec_k_blk_func k_blk_func;
};

struct isp_hw_gtm_func {
	enum isp_k_gtm_blk_idx index;
	isp_k_blk_func k_blk_func;
	uint32_t cap_skip_num;
};

struct cam_cfg_entry {
	uint32_t sub_block;
	func_cam_cfg_param cfg_func;
};

struct cam_hw_block_func_get {
	struct cam_cfg_entry *cam_entry;
	uint32_t index;
};

struct isp_hw_thumbscaler_info {
	uint32_t idx;
	uint32_t scaler_bypass;
	uint32_t odata_mode;
	uint32_t frame_deci;
	uint32_t thumbscl_cal_version;

	struct img_deci_info y_deci;
	struct img_deci_info uv_deci;

	struct img_size y_factor_in;
	struct img_size y_factor_out;
	struct img_size uv_factor_in;
	struct img_size uv_factor_out;

	struct img_size src0;
	struct img_trim y_trim;
	struct img_size y_src_after_deci;
	struct img_size y_dst_after_scaler;
	struct img_size y_init_phase;

	struct img_trim uv_trim;
	struct img_size uv_src_after_deci;
	struct img_size uv_dst_after_scaler;
	struct img_size uv_init_phase;
};

struct cam_hw_bypass_data {
	struct bypass_tag *tag;
	enum cam_bypass_type type;
	uint32_t i;
};

struct cam_hw_reg_trace {
	int type;
	uint32_t idx;
};

struct dcam_store {
	uint32_t color_fmt;
	unsigned long  reg_addr;
	uint32_t is_compressed;
	struct img_addr store_addr;
};

struct dcam_dec_store {
	uint32_t pitch;
	struct img_addr addr;
	struct img_size size;
	struct img_border border;
};

struct dcam_hw_slw_fmcu_cmds {
	uint32_t ctx_id;
	uint32_t is_first_cycle;
	struct dcam_fmcu_ctx_desc *fmcu_handle;
	struct dcam_store store_info[DCAM_PATH_MAX];
	uint32_t slw_id;
	uint32_t slw_cnt;
};

struct dcam_hw_fmcu_cfg {
	uint32_t ctx_id;
	struct dcam_fmcu_ctx_desc *fmcu;
};

struct isp_hw_slice_store {
	uint32_t path_en;
	uint32_t ctx_id;
	enum isp_sub_path_id spath_id;
	struct slice_store_info *slc_store;
};

struct isp_hw_slice_scaler {
	uint32_t path_en;
	uint32_t ctx_id;
	enum isp_sub_path_id spath_id;
	struct slice_scaler_info *slc_scaler;
};

struct isp_hw_nr3_fbd_slice {
	struct isp_fmcu_ctx_desc *fmcu_handle;
	struct slice_3dnr_fbd_fetch_info *fbd_fetch;
	struct slice_3dnr_memctrl_info *mem_ctrl;
};

struct isp_hw_nr3_fbc_slice {
	struct isp_fmcu_ctx_desc *fmcu_handle;
	struct slice_3dnr_fbc_store_info *fbc_store;
};

struct isp_hw_ltm_slice {
	uint32_t idx;
	struct isp_fmcu_ctx_desc *fmcu_handle;
	struct slice_ltm_map_info *map;
	struct slice_ltm_hist_info *hist;
};

struct isp_hw_gtm_slice{
	uint32_t idx;
	struct isp_fmcu_ctx_desc *fmcu_handle;
	struct slice_gtm_info *slice_param;
};

struct isp_hw_afbc_path_slice {
	struct isp_fmcu_ctx_desc *fmcu_handle;
	struct slice_afbc_store_info *slc_afbc_store;
	uint32_t path_en;
	uint32_t ctx_idx;
	uint32_t spath_id;
};

struct isp_3dnr_slw_mem {
	int mv_y;
	int mv_x;
	uint32_t first_line_mode;
	uint32_t last_line_mode;
	uint32_t ft_y_width;
	uint32_t ft_y_height;
	uint32_t ft_uv_width;
	uint32_t ft_uv_height;
	unsigned long ft_luma_addr;
	unsigned long ft_chroma_addr;
};

struct isp_3dnr_slw_store {
	unsigned long st_luma_addr;
	unsigned long st_chroma_addr;
};

struct isp_hw_slw_fmcu_cmds {
	uint32_t ctx_id;
	enum isp_3dnr_mode mode_3dnr;
	enum cam_en_status is_compressed;
	struct img_addr fetchaddr;
	struct isp_fmcu_ctx_desc *fmcu_handle;
	struct isp_store_info store[ISP_SPATH_NUM];
	struct isp_hw_path_store *isp_store;
	struct isp_3dnr_slw_mem slw_mem_ctrl;
	struct isp_3dnr_slw_store nr3_slw_store;
};

struct isp_hw_fmcu_cfg {
	uint32_t ctx_id;
	struct isp_fmcu_ctx_desc *fmcu;
};

struct isp_hw_slice_fetch {
	uint32_t ctx_id;
	struct slice_fetch_info *fetch_info;
};

struct isp_hw_slice_nr_info {
	uint32_t ctx_id;
	struct isp_slice_desc *cur_slc;
};

struct isp_hw_slices_fmcu_cmds {
	int hw_ctx_id;
	uint32_t wmode;
	enum isp_fetch_path_select fetch_path_sel;
	struct isp_fmcu_ctx_desc *fmcu;
};

struct isp_hw_afbc_fmcu_addr {
	struct isp_fmcu_ctx_desc *fmcu;
	uint32_t yaddr;
	uint32_t yheader;
	int index;
};

struct isp_hw_afbc_addr {
	uint32_t idx;
	uint32_t spath_id;
	unsigned long *yuv_addr;
};

struct isp_hw_fbd_slice {
	struct isp_fmcu_ctx_desc *fmcu_handle;
	struct slice_fbd_yuv_info *yuv_info;
};

struct cam_hw_gtm_ltm_dis {
	uint32_t dcam_idx;
	uint32_t isp_idx;
};

struct cam_hw_gtm_ltm_eb {
	uint32_t dcam_idx;
	uint32_t isp_idx;
	struct dcam_isp_k_block *dcam_param;
};

struct dcam_hw_fbc_addr {
	uint32_t idx;
	unsigned long addr;
	uint32_t path_id;
	uint32_t data_bits;
	uint32_t frame_addr[3];
};

struct dcam_hw_fbc_ctrl {
	uint32_t idx;
	uint32_t path_id;
	uint32_t fmt;
	uint32_t data_bits;
	int compress_en;
};

struct cam_hw_lbuf_share {
	enum dcam_id idx;
	uint32_t width;
	uint32_t offline_flag;
	uint32_t pdaf_share_flag;
};

struct cam_hw_lbuf_info {
	uint32_t is_offline;
	uint32_t is_4in1;
	uint32_t line_w;
	uint32_t sn_max_size_w;
};

struct isp_hw_slice_spath {
	uint32_t path_en;
	uint32_t ctx_idx;
	enum isp_sub_path_id spath_id;
	struct slice_store_info *slc_store;
	struct slice_scaler_info *slc_scaler;
	struct isp_fmcu_ctx_desc *fmcu;
};

struct isp_hw_slice_spath_thumbscaler {
	uint32_t path_en;
	uint32_t ctx_idx;
	struct isp_fmcu_ctx_desc *fmcu;
	struct slice_thumbscaler_info *slc_scaler;
};

struct isp_hw_ltm_3dnr_param {
	uint32_t idx;
	uint32_t val;
};

struct isp_hw_nlm_ynr {
	uint32_t val;
	uint32_t ctx_id;
	struct slice_cfg_input *slc_cfg_in;
};

struct dcam_hw_start_fetch {
	uint32_t start_x;
	uint32_t size_x;
	uint32_t fetch_pitch;
	uint32_t idx;
};

struct dcam_hw_dummy_param {
	uint32_t idx;
	uint32_t clr_mode;
	uint32_t skip_num;
	uint32_t dfifo_lvl;
	uint32_t cfifo_lvl;
	uint32_t enable;
};

struct dcam_hw_cfg_bin_path {
	uint32_t idx;
	uint32_t start_x;
	uint32_t fetch_pitch;
};

struct dcam_hw_cfg_store_addr {
	uint32_t idx;
	uint32_t path_id;
	uint32_t frame_addr[3];
	uint32_t frame_size;
	uint32_t buf_size;
	uint32_t out_fmt;
	uint32_t in_fmt;
	uint32_t out_pitch;
	uint32_t reg_addr;
	struct dcam_isp_k_block *blk_param;
	struct img_size out_size;
};

struct dcam_hw_dec_store_cfg {
	uint32_t idx;
	uint32_t cur_layer;
	uint32_t layer_num;
	uint32_t bypass;
	uint32_t endian;
	uint32_t mono_en;
	uint32_t color_format;
	uint32_t burst_len;
	uint32_t mirror_en;
	uint32_t flip_en;
	uint32_t data_10b;
	uint32_t speed2x;
	uint32_t shadow_clr_sel;
	uint32_t pitch[3];
	uint32_t addr[3];
	uint32_t width;
	uint32_t height;
	uint32_t border_up;
	uint32_t border_down;
	uint32_t border_left;
	uint32_t border_right;
	uint32_t rd_ctrl;
	uint32_t store_res;
	uint32_t shadow_clr;
	uint32_t last_frm_en;
	uint32_t align_w;
	uint32_t align_h;
	struct img_size size_t[5];
	struct img_pitch pitch_t[5];
};

struct dcam_hw_dec_online_cfg {
	uint32_t idx;
	uint32_t layer_num;
	enum dcam_dec_path_sel path_sel;
	uint32_t chksum_clr_mode;
	uint32_t chksum_work_mode;
	uint32_t hor_padding_en;
	uint32_t hor_padding_num;
	uint32_t ver_padding_en;
	uint32_t ver_padding_num;
	uint32_t flush_hblank_num;
	uint32_t flush_line_num;
	uint32_t flust_width;
};

struct dcam_hw_fmcu_cmd {
	unsigned long base;
	unsigned long hw_addr;
	int cmd_num;
	uint32_t idx;
};

struct dcam_hw_fmcu_start {
	unsigned long base;
	unsigned long hw_addr;
	int cmd_num;
	uint32_t idx;
};

struct dcam_hw_fmcu_sel {
	uint32_t fmcu_id;
	uint32_t hw_idx;
};

struct dcam_fmcu_enable {
	uint32_t idx;
	uint32_t enable;
};

struct isp_hw_fmcu_cmd {
	unsigned long base;
	unsigned long hw_addr;
	int cmd_num;
};

struct isp_hw_fmcu_start {
	uint32_t fid;
	unsigned long hw_addr;
	int cmd_num;
};

struct isp_hw_fmcu_sel {
	uint32_t fmcu_id;
	uint32_t hw_idx;
};

struct isp_hw_yuv_block_ctrl {
	uint32_t idx;
	uint32_t type;
	struct dcam_isp_k_block *blk_param;
};

struct isp_hw_gtmhist_get_param {
	uint32_t *buf;
	uint32_t hist_total;
	uint32_t fid;
};

struct camhw_cal_fbc_size_para {
	uint32_t fmt;
	uint32_t data_bits;
	uint32_t width;
	uint32_t height;
	uint32_t bypass_4bit;
	struct dcam_compress_info fbc_info;
};

struct camhw_cal_fbc_addr_para {
	uint32_t fmt;
	uint32_t width;
	uint32_t height;
	uint32_t bypass_4bit;
	unsigned long in;
	struct compressed_addr *out;
	struct dcam_compress_info fbc_info;
};

struct isphw_abnormal_regtab_size {
	uint32_t sum_buf_length;
	uint32_t dcam_tab_length;
	uint32_t dcam_tab_offset;
	uint32_t dcamaxi_tab_length;
	uint32_t dcamaxi_tab_offset;
	uint32_t isp_tab_length;
	uint32_t isp_tab_offset;
	uint32_t ispmmu_tab_length;
	uint32_t ispmmu_tab_offset;
	uint32_t dcamfmcu_tab_offset;
	uint32_t dcamfmcu_tab_length;
	uint32_t dcamlite_tab_length;
	uint32_t dcamliteaxi_tab_length;
};

enum isp_alldone_ctrl_int {
	ISP_ALLDONE_WAIT_DISPATCH,
	ISP_ALLDONE_WAIT_LTMHIST,
	ISP_ALLDONE_WAIT_REC,
	ISP_ALLDONE_WAIT_YUV_DONE,
	ISP_ALLDONE_WAIT_STORE,
};

struct isp_hw_alldone_ctrl {
	uint32_t hw_ctx_id;
	enum cam_en_status wait;
	enum isp_alldone_ctrl_int int_bit;
};

struct hw_io_ctrl_fun {
	uint32_t cmd;
	hw_ioctl_fun hw_ctrl;
};

struct glb_syscon {
	uint32_t rst;
	uint32_t rst_mipi_mask;
	uint32_t rst_fmcu_mask;
	uint32_t rst_mask;
	uint32_t rst_ahb_mask;
	uint32_t rst_vau_mask;
	uint32_t all_rst;
	uint32_t all_rst_mask;
	uint32_t axi_rst_mask;
	uint32_t sys_soft_rst;
	uint32_t sys_h2p_db_soft_rst;
};

struct qos_reg {
	const char *reg_name;
	u32 base_addr;
	u32 mask_value;
	u32 set_value;
};

/* TBD: need move to sprd_camsys.h later */
enum sprd_dcam_path_id {
	DCAM_SENSOR_IN_PATH,
	DCAM_FETCH_IN_PATH,
	DCAM_RAW_OUT_PATH,
	DCAM_BIN_OUT_PATH,
	DCAM_FULL_OUT_PATH,
	DCAM_PATH_ID_MAX
};

enum sprd_isp_path_id {
	ISP_FETCH_IN_PATH,
	ISP_PRECAP_OUT_PATH,
	ISP_VID_OUT_PATH,
	ISP_THUMB_OUT_PATH,
	ISP_PATH_ID_MAX
};

enum sprd_cam_path_type {
	CAM_PATH_INPUT,
	CAM_PATH_OUTPUT,
	CAM_PATH_TYPE_MAX
};

struct path_hw_abt {
	uint32_t enable;
	uint32_t fbc_sup;
	uint32_t fbd_sup;
	enum sprd_cam_path_type type;
	uint32_t format[CAM_FORMAT_MAX];
	uint32_t full_src_sel_sup;
	uint32_t full_src_sel_type[MAX_RAW_SRC_SEL];
	uint32_t scaler_sup;
	uint32_t scaler_max_ratio;
	uint32_t scaler_min_ratio;
	uint32_t max_in_size_w;
	uint32_t max_out_size_w;
	uint32_t max_out_size_h;
};

struct dcam_hw_abt {
	uint32_t slm_path;
	uint32_t aux_dcam_path;
	uint32_t dcam_raw_path_id;
	uint32_t sensor_raw_path_id;
	uint32_t dcam_scaling_path;
	enum cam_en_status fmcu_support;
	enum cam_en_status output_yuv_support;
	enum cam_en_status pyramid_support;
	enum cam_en_status bpc_raw_support;
	enum cam_en_status superzoom_support;
	enum cam_en_status lbuf_share_support;
	enum cam_en_status rgb_gtm_support;
	uint32_t recovery_support;
	uint32_t dummy_slave_support;
	enum cam_en_status offline_slice_support;
	enum cam_en_status dcam_offline_fbc_support;
	enum cam_en_status vch3_output_pdaf_support;
	enum cam_en_status mul_raw_output_support;
	enum cam_en_status sensor_32M_support;
	uint32_t sensor_raw_fmt;
	uint32_t store_pyr_fmt;
	uint32_t store_3dnr_fmt[CAM_FORMAT_MAX];
	uint32_t dcam_zoom_mode;
	enum cam_en_status preview_full_size;
	uint32_t save_band_for_bigsize;
	enum dcam_output_strategy output_strategy;
	uint32_t dcam_block[ISP_BLOCK_TOTAL];
};

struct dcamlite_hw_abt {
	uint32_t sensor_raw_fmt;
	uint32_t raw_fmt_support[CAM_FORMAT_MAX];
	uint32_t dcam_output_fmt[CAM_FORMAT_MAX];
};

struct isp_hw_abt {
	enum cam_en_status slm_cfg_support;
	enum cam_en_status *ctx_fmcu_support;
	enum cam_en_status fetch_raw_support;
	enum cam_en_status pyr_rec_support;
	enum cam_en_status pyr_dec_support;
	enum cam_en_status rgb_ltm_support;
	enum cam_en_status rgb_gtm_support;
	enum cam_en_status frbg_hist_support;
	enum cam_en_status nr3_compress_support;
	enum cam_en_status pyr_rec_lay0_support;
	enum cam_en_status static_map_support;
	enum cam_en_status reserved_iommu_map_support;
	uint32_t nr3_mv_alg_version;
	uint32_t dyn_overlap_version;
	uint32_t thumb_scaler_cal_version;
	uint32_t scaler_coeff_ex;
	uint32_t scaler_bypass_ctrl;
	uint32_t max_size_w;
	uint32_t max_size_h;
	uint32_t isp_scaling_path_id;
	uint32_t isp_block[ISP_BLOCK_TOTAL];
};

struct cam_hw_ip_info {
	uint32_t idx;
	uint32_t irq_no;
	uint32_t dec_irq_no;
	unsigned long phy_base;
	unsigned long reg_base;
	struct glb_syscon syscon;

	struct path_hw_abt *dcampath_abt[DCAM_PATH_ID_MAX];
	struct dcam_hw_abt *dcamhw_abt;
	struct dcamlite_hw_abt *dcamlitehw_abt;
	struct path_hw_abt *isppath_abt[ISP_PATH_ID_MAX];
	struct isp_hw_abt *isphw_abt;
};

struct cam_hw_soc_info {
	struct platform_device *pdev;
	uint32_t count;
	unsigned long axi_reg_base;
	unsigned long fmcu_reg_base;

	struct regmap *cam_ahb_gpr;
	struct regmap *aon_apb_gpr;
	struct regmap *cam_switch_gpr;
	rwlock_t cam_ahb_lock;
	struct clk *core_eb;
	struct clk *axi_eb;
	struct clk *mtx_en;
	struct clk *blk_cfg_en;
	struct clk *tck_en;
	struct clk *clk;
	struct clk *clk_parent;
	struct clk *clk_default;
	struct clk *bpc_clk;
	struct clk *bpc_clk_parent;
	struct clk *bpc_clk_default;
	struct clk *axi_clk;
	struct clk *axi_clk_parent;
	struct clk *axi_clk_default;
	struct clk *mtx_clk;
	struct clk *mtx_clk_parent;
	struct clk *mtx_clk_default;
	struct clk *blk_cfg_clk;
	struct clk *blk_cfg_clk_parent;
	struct clk *blk_cfg_clk_default;

	uint32_t arqos_high;
	uint32_t arqos_low;
	uint32_t awqos_high;
	uint32_t awqos_low;
};

struct isp_hw_cfg_info {
	uint32_t hw_ctx_id;
	uint32_t fmcu_enable;
	uint32_t hw_addr;
};

struct abnormal_record_info {
	atomic_t is_print;
	atomic_t abnormal_record;
	void *abnormal_record_buf;
	uint32_t idx;
	struct isphw_abnormal_regtab_size size_info;
};

struct cam_hw_info {
	enum cam_prj_id prj_id;
	uint32_t csi_connect_type;
	spinlock_t isp_cfg_lock;
	struct platform_device *pdev;
	struct cam_hw_soc_info *soc_dcam;
	struct cam_hw_soc_info *soc_dcam_lite;
	struct cam_hw_soc_info *soc_isp;
	struct cam_hw_ip_info *ip_dcam[DCAM_ID_MAX];
	struct cam_hw_ip_info *ip_isp;
	int (*dcam_ioctl)(void *, enum dcam_hw_context_id, enum dcam_hw_cfg_cmd, void *);
	int (*isp_ioctl)(void *,enum isp_hw_cfg_cmd, void *);
	int (*pyrdec_ioctl)(void *,enum pyrdec_hw_cfg_cmd, void *);
	int (*cam_ioctl)(void *,enum cam_hw_cfg_cmd, void *);
	void *s_dcam_dev_debug;
	void *s_isp_dev_debug;
	struct abnormal_record_info abnormal_info;
};

#endif
