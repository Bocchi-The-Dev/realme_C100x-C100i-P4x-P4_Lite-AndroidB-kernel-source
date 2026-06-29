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

#ifndef _SPRD_ISP_2V6_H_
#define _SPRD_ISP_2V6_H_

#define ISP_HSV_SAT_GRID         17
#define ISP_HSV_HUE_GRID         25
#define ISP_HSV_TABLE_A          117 /*9*13*/
#define ISP_HSV_TABLE_B          104 /*8*13*/
#define ISP_HSV_TABLE_C          108 /*9*12*/
#define ISP_HSV_TABLE_D          96  /*8*128*/
#define ISP_HSV_HUE3             3
#define ISP_HSV_HUE2             2
#define ISP_HSV_HUE4             4

#define PDAF_PPI_NUM             64
#define PDAF_PPI_GAIN_MAP_LEN    128
#define ISP_HSV_TABLE_NUM        360
#define ISP_VST_IVST_NUM         1024
#define ISP_VST_IVST_NUM2        1025
#define ISP_FRGB_GAMMA_PT_NUM    257
#define ISP_FRGB_GAMMA_PT_NUM_V1 1025
#define POSTERIZE_NUM            8
#define POSTERIZE_NUM2           129
#define ISP_YUV_GAMMA_NUM        129
#define ISP_YUV_GAMMA_NUM_V1     513
#define GTM_HIST_BIN_NUM         128

#define MAX_WTAB_LEN             1024
#define MAX_LSCTAB_LEN           (16*1024)

#define PARAM_BUF_NUM_MAX        32
#define STATIS_BUF_NUM_MAX       20
#define CAMERA_PARAM_NUM_MAX     20

#define STATIS_AEM_HEADER_SIZE   512
#define STATIS_HIST_HEADER_SIZE  128
#define CNR_H_LAYER_NUM          5
#define RGB_AFM_GAMMA_POINT_NUM  1025

/* SharkL5/ROC1/SharkL5Pro */
/* AFL: global 80 x 16 bytes for one frame, region 482 x 16 bytes one frame */
#define STATIS_AFL_GBUF_SIZE    (80 * 16 * 3 + 64)

/* SharkL3 */
/* AFL: global 240 * 8 bytes for one frame, region 964 x 8 bytes one frame */
#define STATIS_AFL_GBUF_SIZE3   (240 * 8 * 3 + 64)


enum SCINFO_COLOR_ORDER {
	COLOR_ORDER_RG = 0,
	COLOR_ORDER_GR,
	COLOR_ORDER_GB,
	COLOR_ORDER_BG
};

enum aem_mode {
	AEM_MODE_SINGLE = 0,
	AEM_MODE_MULTI,
};

enum afm_mode {
	AFL_MODE_SINGLE = 0,
	AFL_MODE_MULTI,
};

enum isp_irq_done_id {
	IRQ_DCAM_SOF,
	IRQ_RAW_PROC_DONE,
	IRQ_RAW_PROC_TIMEOUT,
	IRQ_DCAM_SN_SOF,
	IRQ_DCAM_SN_EOF,
	IRQ_MAX_DONE,
};

enum isp_statis_buf_type {
	STATIS_INIT = 0,
	STATIS_AEM,
	STATIS_AFM,
	STATIS_AFL,
	STATIS_HIST,
	STATIS_PDAF,
	STATIS_EBD,
	STATIS_3DNR,
	STATIS_LSCM,
	STATIS_HIST2,
	STATIS_GTMHIST,
	STATIS_LTMHIST,
	STATIS_TYPE_MAX,
	STATIS_DBG_INIT,
	STATIS_PARAM,
};

enum dcam_block {
	DCAM_ISP_BLOCK_MASK = (1 << 8),
	DCAM_BLOCK_BASE = (0 << 8),
	DCAM_BLOCK_BLC = DCAM_BLOCK_BASE,
	DCAM_BLOCK_RGBG,
	DCAM_BLOCK_RGBG_DITHER,
	DCAM_BLOCK_PDAF,
	DCAM_BLOCK_LSC,
	DCAM_BLOCK_BAYERHIST,
	DCAM_BLOCK_AEM,
	DCAM_BLOCK_AFL,
	DCAM_BLOCK_AWBC,
	DCAM_BLOCK_BPC,
	DCAM_BLOCK_GRGB,
	DCAM_BLOCK_3DNR_ME,
	DCAM_BLOCK_AFM,
	DCAM_BLOCK_GTM,
	DCAM_BLOCK_LSCM,
	DCAM_BLOCK_RDS,
	DCAM_BLOCK_TOTAL,

	ISP_BLOCK_BASE = (1 << 8),
	ISP_BLOCK_BCHS = ISP_BLOCK_BASE,
	ISP_BLOCK_CCE,
	ISP_BLOCK_CDN,
	ISP_BLOCK_CFA,
	ISP_BLOCK_CMC,
	ISP_BLOCK_EDGE,
	ISP_BLOCK_GAMMA,
	ISP_BLOCK_GRGB,
	ISP_BLOCK_HIST,
	ISP_BLOCK_HIST2,
	ISP_BLOCK_HSV,
	ISP_BLOCK_IIRCNR,
	ISP_BLOCK_LTM,
	ISP_BLOCK_NLM,
	ISP_BLOCK_POST_CDN,
	ISP_BLOCK_PRE_CDN,
	ISP_BLOCK_PSTRZ,
	ISP_BLOCK_UVD,
	ISP_BLOCK_YGAMMA,
	ISP_BLOCK_YNR,
	ISP_BLOCK_BRIGHTNESS,
	ISP_BLOCK_CONTRAST,
	ISP_BLOCK_CSA,
	ISP_BLOCK_HUE,
	ISP_BLOCK_YRANDOM,
	ISP_BLOCK_NOISEFILTER,
	ISP_BLOCK_3DNR,
	ISP_BLOCK_RGB_LTM,
	ISP_BLOCK_YUV_LTM,
	ISP_BLOCK_PYRAMID_ONL,
	ISP_BLOCK_PYRAMID_OFFL,
	ISP_BLOCK_DCT,
	ISP_BLOCK_3DLUT,
	ISP_BLOCK_RGB_GTM,
	ISP_BLOCK_CNR_H,
	ISP_BLOCK_POST_CNR_H,
	ISP_BLOCK_TOTAL,
};

enum dcam_blc_property {
	DCAM_PRO_BLC_BLOCK,
};

enum dcam_gain_property {
	DCAM_PRO_GAIN_BLOCK,
	DCAM_PRO_GAIN_DITHER_BLOCK,
};

enum dcam_lsc_property {
	DCAM_PRO_LSC_BLOCK,
};

enum dcam_afl_property {
	DCAM_PRO_AFL_BLOCK,
	DCAM_PRO_AFL_BYPASS,
};

enum dcam_awbc_property {
	DCAM_PRO_AWBC_BLOCK,
	DCAM_PRO_AWBC_GAIN,
	DCAM_PRO_AWBC_BYPASS,
};

enum dcam_bpc_property {
	DCAM_PRO_BPC_BLOCK,
	DCAM_PRO_BPC_MAP,
	DCAM_PRO_BPC_HDR_PARAM,
	DCAM_PRO_BPC_PPE_PARAM,
	DCAM_PRO_BPC_PPI_PARAM,
};

enum dcam_3dnr_me_property {
	DCAM_PRO_3DNR_ME,
};

enum dcam_afm_property {
	DCAM_PRO_AFM_BYPASS,
	DCAM_PRO_AFM_BLOCK,
	DCAM_PRO_AFM_MODE,
	DCAM_PRO_AFM_SKIPNUM,
	DCAM_PRO_AFM_IIR_CFG,
	DCAM_PRO_AFM_WIN_CFG,
};

enum dcam_pdaf_property {
	DCAM_PDAF_BYPASS,
	DCAM_PDAF_TYPE3_SET_INFO,
	DCAM_PDAF_TYPE3_SET_MODE,
	DCAM_PDAF_TYPE3_SET_SKIP_NUM,
	DCAM_PDAF_TYPE3_SET_ROI,
	DCAM_PDAF_TYPE3_SET_PPI_INFO,
	DCAM_PDAF_TYPE1_BLOCK,
	DCAM_PDAF_TYPE2_BLOCK,
	DCAM_PDAF_TYPE3_BLOCK,
	DCAM_DUAL_PDAF_BLOCK,
	DCAM_PDAF_PPE_PPC_PARAM,
};

enum isp_bchs_property {
	ISP_PRO_BCHS_BLOCK,
	ISP_PRO_BCHS_BRIGHT,
	ISP_PRO_BCHS_CONTRAST,
	ISP_PRO_BCHS_SATUATION,
	ISP_PRO_BCHS_HUE,
};

enum isp_brightness_property {
	ISP_PRO_BRIGHT_BLOCK,
};

enum isp_contrast_property {
	ISP_PRO_CONTRAST_BLOCK,
};

enum isp_csa_property {
	ISP_PRO_CSA_BLOCK,
};

enum isp_hua_property {
	ISP_PRO_HUE_BLOCK,
};

enum isp_cce_property {
	ISP_PRO_CCE_BLOCK,
};

enum isp_cdn_property {
	ISP_PRO_CDN_BLOCK,
};

enum isp_cfa_property {
	ISP_PRO_CFA_BLOCK,
};

enum isp_cmc_property {
	ISP_PRO_CMC_BLOCK,
};

enum isp_edge_property {
	ISP_PRO_EDGE_BLOCK,
};

enum isp_gamma_property {
	ISP_PRO_GAMMA_BLOCK,
};

enum isp_grgb_property {
	ISP_PRO_GRGB_BLOCK,
};

enum isp_dct_property {
	ISP_PRO_DCT_BLOCK,
};

enum isp_hist2_property {
	ISP_PRO_HIST2_BLOCK,
};

enum isp_hist_property {
	ISP_PRO_HIST_BLOCK,
};

enum isp_hsv_property {
	ISP_PRO_HSV_BLOCK,
};

enum isp_iircnr_property {
	ISP_PRO_IIRCNR_BLOCK,
};

enum isp_nlm_property {
	ISP_PRO_NLM_BLOCK,
	ISP_PRO_NLM_IMBLANCE,
};

enum isp_post_cdn_property {
	ISP_PRO_POST_CDN_BLOCK,
};

enum isp_pre_cdn_property {
	ISP_PRO_PRE_CDN_BLOCK,
};

enum isp_pstrz_property {
	ISP_PRO_POSTERIZE_BLOCK,
};

enum isp_uvd_property {
	ISP_PRO_UVD_BLOCK,
};

enum isp_ygamma_property {
	ISP_PRO_YGAMMA_BLOCK,
};

enum isp_ynr_property {
	ISP_PRO_YNR_BLOCK,
};

enum isp_cnr_h_property {
	ISP_PRO_CNR_H_BLOCK,
};

enum isp_post_cnr_h_property {
	ISP_PRO_POST_CNR_H_BLOCK,
};

enum isp_yrandom_property {
	ISP_PRO_YRANDOM_BLOCK,
};

enum isp_noise_filter_property {
	ISP_PRO_NOISE_FILTER_BLOCK,
};

enum isp_3dnr_property {
	ISP_PRO_3DNR_BLOCK,
};

enum isp_ltm_property {
	ISP_PRO_LTM_BLOCK,
	ISP_PRO_LTM_PRE_PARAM,
	ISP_PRO_LTM_CAP_PARAM,
};

enum isp_rgb_ltm_property {
	ISP_PRO_RGB_LTM_BLOCK,
	ISP_PRO_RGB_LTM_CAP_PARAM,
};

enum isp_yuv_ltm_property {
	ISP_PRO_YUV_LTM_BLOCK,
	ISP_PRO_YUV_LTM_PRE_PARAM,
	ISP_PRO_YUV_LTM_CAP_PARAM,
};

enum isp_pyramid_onl_property {
	ISP_PRO_PYRAMID_ONL_BLOCK,
};

enum isp_pyramid_offl_property {
	ISP_PRO_PYRAMID_OFFL_BLOCK,
};

enum cam_gtm_calc_mode {
	GTM_SW_CALC = 1,
	GTM_HW_CALC,
};

enum dcam_gtm_property {
	DCAM_PRO_GTM_BLOCK,
};

enum isp_3dlut_property {
	ISP_PRO_3DLUT_BLOCK,
};

enum cam_pm_scene {
	PM_SCENE_PRE,
	PM_SCENE_CAP,
	PM_SCENE_VID,
	PM_SCENE_FDRL,
	PM_SCENE_FDRH,
	PM_SCENE_FDR_PRE,
	PM_SCENE_FDR_DRC,
	PM_SCENE_OFFLINE_CAP,
	PM_SCENE_RAWALG,
	PM_SCENE_OFFLINE_BPC,
	PM_SCENE_FDR_MERGE,
	PM_SCENE_SFNR,
	PM_SCENE_MAX,
};

struct isp_io_param {
	uint32_t scene_id;
	uint32_t sub_block;
	uint64_t uaddr_fid;
	uint32_t property;
	void  __user  *property_param;
};

struct isp_buf_param {
	uint32_t stream_type;
	uint32_t frame_id;
	uint32_t mfd;
	uint32_t offset;
};

struct isp_buf_param_init {
	/* for init all */
	int32_t mfd_array[CAMERA_PARAM_NUM_MAX];
	uint32_t offset;
};

struct isp_addr {
	unsigned long	chn0;
	unsigned long	chn1;
	unsigned long	chn2;
};

struct isp_img_size {
	uint32_t width;
	uint32_t height;
};

struct isp_img_rect {
	uint32_t x;
	uint32_t y;
	uint32_t w;
	uint32_t h;
};

struct isp_img_offset {
	uint32_t x;
	uint32_t y;
	uint32_t Z;
};

struct isp_coord {
	uint32_t start_x;
	uint32_t start_y;
	uint32_t end_x;
	uint32_t end_y;
};

struct dcam_dev_blc_info {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t r;
	uint32_t b;
	uint32_t gr;
	uint32_t gb;
};

struct dcam_dev_gtm_slice_info {
	uint32_t gtm_slice_main;
	uint32_t gtm_slice_line_startpos;
	uint32_t gtm_slice_line_endpos;
	uint32_t slice_width;
	uint32_t slice_height;
};

struct cam_gtm_mapping {
	uint32_t idx;
	uint32_t ymin;
	uint32_t ymax;
	uint32_t yavg;
	uint32_t target;
	uint32_t lr_int;
	uint32_t log_min_int;
	uint32_t log_diff_int;
	uint32_t diff;
	uint32_t ltm_strength;
	uint32_t isupdate;
};

struct dcam_dev_raw_gtm_bypass {
	uint32_t gtm_hist_stat_bypass;
	uint32_t gtm_map_bypass;
	uint32_t gtm_mod_en;
};

struct dcam_dev_raw_gtm_block_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t gtm_tm_out_bit_depth;
	uint32_t gtm_tm_in_bit_depth;
	uint32_t gtm_tm_luma_est_mode;
	uint32_t gtm_cur_is_first_frame;
	uint32_t gtm_tm_param_calc_by_hw;
	struct dcam_dev_raw_gtm_bypass bypass_info;
	uint32_t gtm_imgkey_setting_value;
	uint32_t gtm_imgkey_setting_mode;
	uint32_t gtm_target_norm_coeff;
	uint32_t gtm_target_norm;
	uint32_t gtm_target_norm_setting_mode;
	uint32_t gtm_ymin;
	uint32_t gtm_yavg;
	uint32_t gtm_ymax;
	uint32_t gtm_log_min_int;
	uint32_t gtm_lr_int;
	uint32_t gtm_log_diff_int;
	uint32_t gtm_log_max_int;
	uint32_t gtm_hist_total;
	uint32_t gtm_min_per;
	uint32_t gtm_max_per;
	uint32_t gtm_log_diff;
	uint32_t gtm_pre_ymin_weight;
	uint32_t gtm_cur_ymin_weight;
	uint32_t gtm_ymax_diff_thr;
	uint32_t gtm_yavg_diff_thr;
	uint32_t tm_lumafilter_c[3][3];
	uint32_t tm_lumafilter_shift;
	uint32_t tm_rgb2y_g_coeff;
	uint32_t tm_rgb2y_r_coeff;
	uint32_t tm_rgb2y_b_coeff;
	uint16_t tm_hist_xpts[GTM_HIST_BIN_NUM];
	struct dcam_dev_gtm_slice_info slice;
	uint32_t gtm_map_video_mode;
	uint32_t init_flag;
};

struct dcam_dev_rgb_gtm_block_info {
	uint32_t update_flag;
	uint32_t gtm_tm_out_bit_depth;
	uint32_t gtm_tm_in_bit_depth;
	uint32_t gtm_cur_is_first_frame;
	uint32_t gtm_tm_param_calc_by_hw;
	struct dcam_dev_raw_gtm_bypass bypass_info;
	uint32_t gtm_imgkey_setting_value;
	uint32_t gtm_imgkey_setting_mode;
	uint32_t gtm_target_norm_coeff;
	uint32_t gtm_target_norm;
	uint32_t gtm_target_norm_setting_mode;
	uint32_t gtm_ymin;
	uint32_t gtm_yavg;
	uint32_t gtm_ymax;
	uint32_t gtm_log_min_int;
	uint32_t gtm_lr_int;
	uint32_t gtm_log_diff_int;
	uint32_t gtm_log_max_int;
	uint32_t gtm_hist_total;
	uint32_t gtm_min_per;
	uint32_t gtm_max_per;
	uint32_t gtm_log_diff;
	uint32_t gtm_pre_ymin_weight;
	uint32_t gtm_cur_ymin_weight;
	uint32_t gtm_ymax_diff_thr;
	uint32_t gtm_yavg_diff_thr;
	uint32_t tm_lumafilter_c[3][3];
	uint32_t tm_lumafilter_shift;
	uint32_t gtm_rgb2y_mode;
	uint32_t tm_rgb2y_g_coeff;
	uint32_t tm_rgb2y_r_coeff;
	uint32_t tm_rgb2y_b_coeff;
	uint16_t tm_hist_xpts[GTM_HIST_BIN_NUM];
	struct dcam_dev_gtm_slice_info slice;
	uint32_t gtm_map_video_mode;
	uint32_t gtm_map_bilateral_sigma_d;
	uint32_t gtm_map_bilateral_sigma_r;
	uint32_t gtm_tm_param_calc_by_sw;
	uint32_t tm_filter_dist_c[49];
	uint32_t tm_filter_distw_c[19];
	uint32_t tm_filter_rangw_c[61];
	uint32_t init_flag;
};

struct isp_cnr_h_info {
	uint32_t lowpass_filter_en;
	uint32_t denoise_radial_en;
	uint32_t filter_size;
	uint32_t order_y[3];
	uint32_t order_uv[3];
	uint32_t imgCenterX;
	uint32_t imgCenterY;
	uint32_t radius;
	uint32_t base_radius;
	uint32_t base_radius_factor;
	uint32_t minRatio;
	uint32_t slope;
	uint32_t luma_th[2];
	uint32_t sigma_y[3];
	uint32_t sigma_uv[3];
	uint32_t weight_y[3][72];
	uint32_t weight_uv[3][72];
};

struct isp_dev_cnr_h_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t radius_base;
	struct isp_cnr_h_info layer_cnr_h[CNR_H_LAYER_NUM];
};

struct isp_post_cnr_h {
	uint32_t lowpass_filter_en;
	uint32_t denoise_radial_en;
	uint32_t filter_size;
	uint32_t order_y[3];
	uint32_t order_uv[3];
	uint32_t imgCenterX;
	uint32_t imgCenterY;
	uint32_t radius;
	uint32_t base_radius;
	uint32_t base_radius_factor;
	uint32_t minRatio;
	uint32_t slope;
	uint32_t luma_th[2];
	uint32_t sigma_y[3];
	uint32_t sigma_uv[3];
	uint32_t weight_y[3][72];
	uint32_t weight_uv[3][72];
};

struct isp_dev_post_cnr_h_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t radius_base;
	struct isp_post_cnr_h param_post_cnr_h;
};

struct dcam_dev_rgb_gain_info {
	uint32_t update_flag;
	uint32_t  bypass;
	uint32_t  global_gain;
	uint32_t  r_gain;
	uint32_t  g_gain;
	uint32_t  b_gain;
};

struct dcam_dev_rgb_dither_info {
	uint32_t update_flag;
	uint32_t random_bypass;
	uint32_t random_mode;
	uint32_t seed;
	uint32_t range;
	uint32_t r_offset;
	uint32_t r_shift;
	uint32_t takebit[8];
};

struct dcam_dev_lsc_info {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t update_all;
	uint32_t grid_width;
	uint32_t grid_x_num;
	uint32_t grid_y_num;
	uint32_t grid_num_t;
	uint32_t gridtab_len;
	uint32_t weight_num;
	uint32_t weight_num_x;
	uint32_t weight_num_y;
	uint64_t grid_tab_addr;
	uint64_t weight_tab_addr;
	uint64_t weight_tab_addr_x;
	uint64_t weight_tab_addr_y;
	uint32_t grid_width_x;
	uint32_t grid_width_y;
};

struct dcam_dev_hist_info {
	uint32_t update_flag;
	uint32_t hist_bypass;
	uint32_t hist_skip_num;
	uint32_t hist_mul_enable;
	uint32_t hist_mode_sel;
	uint32_t hist_initial_clear;
	uint32_t hist_skip_num_clr;
	uint32_t hist_sgl_start;
	uint32_t bayer_hist_sty;
	uint32_t bayer_hist_stx;
	uint32_t bayer_hist_endy;
	uint32_t bayer_hist_endx;
	uint32_t bayer_hist_hdr_en;
	uint32_t bayer_hist_hdr_zigzag_pattern;
};

struct dcam_dev_aem_win {
	uint32_t offset_x;
	uint32_t offset_y;
	uint32_t blk_width;
	uint32_t blk_height;
	uint32_t blk_num_x;
	uint32_t blk_num_y;
};

struct thr_info{
	uint32_t low_thr;
	uint32_t high_thr;
};

struct dcam_dev_rds_param {
	uint32_t update_flag;
	uint8_t mode;
	int16_t B;
	uint16_t R;
};

struct dcam_dev_aem_thr {
	uint32_t aem_hdr_en;
	struct thr_info  aem_r_thr;
	struct thr_info  aem_g_thr;
	struct thr_info  aem_b_thr;
	struct thr_info  aem_short_r_thr;
	struct thr_info  aem_short_g_thr;
	struct thr_info  aem_short_b_thr;
};

struct dcam_dev_lscm_param {
	uint32_t update_flag;
	uint32_t update;
	uint32_t bypass;
	uint32_t mode;
	uint32_t offset_x;
	uint32_t offset_y;
	uint32_t blk_width;
	uint32_t blk_height;
	uint32_t blk_num_x;
	uint32_t blk_num_y;
	uint32_t skip_num;
};

/*anti flicker */
struct isp_dev_anti_flicker_new_info {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t bayer2y_mode;
	uint32_t bayer2y_chanel;
	uint32_t mode;
	uint32_t skip_frame_num;
	uint32_t afl_stepx;
	uint32_t afl_stepy;
	uint32_t frame_num;
	uint32_t start_col;
	uint32_t end_col;
	uint32_t step_x_region;
	uint32_t step_y_region;
	uint32_t step_x_start_region;
	uint32_t step_x_end_region;
	uint32_t afl_glb_total_num;
	uint32_t afl_region_total_num;
	struct isp_img_size img_size;
};

struct img_rgb_info {
	uint32_t r;
	uint32_t b;
	uint32_t gr;
	uint32_t gb;
};

struct dcam_dev_awbc_info {
	uint32_t update_flag;
	uint32_t awbc_bypass;
	struct img_rgb_info gain;
	struct img_rgb_info thrd;
	struct img_rgb_info gain_offset;
};

struct dcam_bpc_ppi_info{
	uint32_t update_flag;
	uint32_t bpc_ppi_en;
	uint32_t bpc_ppi_start_row;
	uint32_t bpc_ppi_end_row;
	uint32_t bpc_ppi_start_col;
	uint32_t bpc_ppi_end_col;
};

struct dcam_ppe_ppc_info{
	uint32_t ppi_bypass;
	uint32_t ppi_upperbound_r;
	uint32_t ppi_upperbound_b;
	uint32_t ppi_upperbound_gr;
	uint32_t ppi_upperbound_gb;
	uint32_t ppi_blc_r;
	uint32_t ppi_blc_b;
	uint32_t ppi_blc_gr;
	uint32_t ppi_blc_gb;
	uint32_t ppi_phase_map_corr_en;
	uint16_t ppc_l_gain_map[PDAF_PPI_GAIN_MAP_LEN];
	uint16_t ppc_r_gain_map[PDAF_PPI_GAIN_MAP_LEN];
};

struct dcam_dev_bpc_info {
	uint32_t update_flag;
	uint32_t bpc_bypass;
	uint32_t bpc_double_bypass;
	uint32_t bpc_three_bypass;
	uint32_t bpc_four_bypass;
	uint32_t bpc_mode;
	uint32_t bpc_is_mono_sensor;
	uint32_t bpc_ppi_en;
	uint32_t bpc_edge_hv_mode;
	uint32_t bpc_edge_rd_mode;
	uint32_t bpc_hdr_en;
	uint32_t bpc_pos_out_en;
	uint32_t bpc_map_clr_en;
	uint32_t bpc_rd_max_len_sel;
	uint32_t bpc_wr_max_len_sel;
	uint32_t bpc_blk_mode;
	uint32_t bpc_mod_en;
	uint32_t bpc_cg_dis;

	uint32_t bpc_four_badpixel_th[4];
	uint32_t bpc_three_badpixel_th[4];
	uint32_t bpc_double_badpixel_th[4];

	uint32_t bpc_texture_th;

	uint32_t bpc_flat_th;
	uint32_t bpc_shift[3];

	uint32_t bpc_edgeratio_hv;
	uint32_t bpc_edgeratio_rd;

	uint32_t bpc_highoffset;
	uint32_t bpc_lowoffset;
	uint32_t bpc_highcoeff;
	uint32_t bpc_lowcoeff;

	uint32_t bpc_mincoeff;
	uint32_t bpc_maxcoeff;

	uint32_t bpc_intercept_b[8];
	uint32_t bpc_slope_k[8];
	uint32_t bpc_lut_level[8];

	uint32_t bad_pixel_num;
	uint32_t bpc_map_addr;
	uint32_t bpc_bad_pixel_pos_out_addr;
	uint32_t bpc_last_waddr;
};

struct dcam_dev_bpc_info_l3 {
	uint32_t update_flag;
	uint32_t bpc_gc_cg_dis;
	uint32_t bpc_mode_en;
	uint32_t bpc_mode_en_gc;
	uint32_t bpc_bypass;
	uint32_t bpc_double_bypass;
	uint32_t bpc_three_bypass;
	uint32_t bpc_four_bypass;
	uint32_t bpc_mode;
	uint32_t pos_out_continue_mode;
	uint32_t is_mono_sensor;
	uint32_t edge_hv_mode;
	uint32_t edge_rd_mode;
	uint32_t pos_out_skip_num;
	uint32_t rd_retain_num;
	uint32_t rd_max_len_sel;
	uint32_t wr_max_len_sel;
	uint32_t bpc_blk_mode;
	uint32_t double_badpixel_th[4];
	uint32_t three_badpixel_th[4];
	uint32_t four_badpixel_th[4];
	uint32_t texture_th;
	uint32_t flat_th;
	uint32_t shift[3];
	uint32_t edge_ratio_hv;
	uint32_t edge_ratio_rd;
	uint32_t high_offset;
	uint32_t low_offset;
	uint32_t high_coeff;
	uint32_t low_coeff;
	uint32_t min_coeff;
	uint32_t max_coeff;
	unsigned short intercept_b[8];
	unsigned short slope_k[8];
	unsigned short lut_level[8];
	uint32_t map_addr;
	uint32_t bad_map_hw_fifo_clr_en;
	uint32_t bad_pixel_num;
	uint32_t bad_pixel_pos_out_addr;
};

struct dcam_dev_bpc_info_v1 {
	uint32_t update_flag;
	uint32_t bpc_bypass;
	uint32_t bpc_mode;
	uint32_t bpc_is_mono_sensor;
	uint32_t bpc_double_bypass;
	uint32_t bpc_three_bypass;
	uint32_t bpc_four_bypass;
	uint32_t bpc_intercept_b[8];
	uint32_t bpc_slope_k[8];
	uint32_t bpc_lut_level[8];

	uint32_t bpc_ppi_en;
	uint32_t bpc_hdr_en;
	uint32_t bpc_pos_out_en;
	uint32_t bpc_map_clr_en;
	uint32_t bpc_rd_max_len_sel;
	uint32_t bpc_wr_max_len_sel;
	uint32_t bpc_blk_mode;
	uint32_t bpc_mod_en;
	uint32_t bpc_cg_dis;
	uint32_t bpc_bad_pixel_th[4];

	uint32_t bpc_ig_th;
	uint32_t bpc_flat_th;
	uint32_t bpc_shift[3];

	uint32_t bpc_edgeratio_hv;
	uint32_t bpc_edgeratio_rd;

	uint32_t bpc_highoffset;
	uint32_t bpc_lowoffset;
	uint32_t bpc_highcoeff;
	uint32_t bpc_lowcoeff;
	uint32_t bpc_edgeratio_g;
	uint32_t bpc_edgeratio_dirc;
	uint32_t bpc_diffcoeff_limit;
	uint32_t bpc_diffcoeff_detect;
	uint32_t bpc_difflimit;

	uint32_t bad_pixel_num;
	uint32_t bpc_map_addr;
	uint32_t bpc_bad_pixel_pos_out_addr;
	uint32_t bpc_last_waddr;
};

struct dcam_dev_3dnr_me {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t nr3_channel_sel;
	uint32_t nr3_project_mode;
};

struct enhanced_lum {
	uint32_t afm_gamma_en;
	uint32_t afm_hist_en;
	uint32_t afm_scale_en;
	uint32_t afm_scale_hx;
	uint32_t afm_scale_vx;
	uint32_t afm_max_lum_num_th;
	uint32_t afm_max_lum_th;
	uint32_t afm_gamma_curve[RGB_AFM_GAMMA_POINT_NUM];
};

struct thrd_min_max {
	uint32_t min;
	uint32_t max;
};

struct dcam_dev_afm_info {
	uint32_t bypass;
	uint32_t afm_mode_sel;
	uint32_t afm_mul_enable;
	uint32_t afm_skip_num;
	uint32_t afm_skip_num_clr;
	uint32_t afm_sgl_start;
	uint32_t afm_done_tile_num_x;
	uint32_t afm_done_tile_num_y;
	uint32_t afm_lum_stat_chn_sel;
	uint32_t afm_iir_enable;
	uint32_t afm_cg_dis;
	uint32_t afm_fv1_shift;
	uint32_t afm_fv0_shift;
	uint32_t afm_clip_en1;
	uint32_t afm_clip_en0;
	uint32_t afm_center_weight;
	uint32_t afm_denoise_mode;
	uint32_t afm_channel_sel;
	uint32_t afm_crop_eb;
	uint16_t afm_iir_g0;
	uint16_t afm_iir_g1;
	uint16_t afm_iir_c[10];
	struct thrd_min_max afm_fv0_th;
	struct thrd_min_max afm_fv1_th;
	uint16_t afm_fv1_coeff[4][9];
	struct enhanced_lum afm_enhanced_lum;
};

struct dcam_dev_afm_iir_info {
	uint8_t afm_iir_enable;
	uint16_t afm_iir_g0;
	uint16_t afm_iir_c1;
	uint16_t afm_iir_c2;
	uint16_t afm_iir_c3;
	uint16_t afm_iir_c4;
	uint16_t afm_iir_c5;
	uint16_t afm_iir_g1;
	uint16_t afm_iir_c6;
	uint16_t afm_iir_c7;
	uint16_t afm_iir_c8;
	uint16_t afm_iir_c9;
	uint16_t afm_iir_c10;
};

struct dcam_dev_vc2_control {
	uint32_t bypass;
	uint32_t vch2_vc;
	uint32_t vch2_data_type;
	uint32_t vch2_mode;
};

struct isp_3dnr_blend_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t filter_switch;
	uint32_t fusion_mode;
	uint32_t y_pixel_src_weight[4];
	uint32_t u_pixel_src_weight[4];
	uint32_t v_pixel_src_weight[4];
	uint32_t y_pixel_noise_threshold;
	uint32_t u_pixel_noise_threshold;
	uint32_t v_pixel_noise_threshold;
	uint32_t y_pixel_noise_weight;
	uint32_t u_pixel_noise_weight;
	uint32_t v_pixel_noise_weight;
	uint32_t threshold_radial_variation_u_range_min;
	uint32_t threshold_radial_variation_u_range_max;
	uint32_t threshold_radial_variation_v_range_min;
	uint32_t threshold_radial_variation_v_range_max;
	uint32_t y_threshold_polyline_0;
	uint32_t y_threshold_polyline_1;
	uint32_t y_threshold_polyline_2;
	uint32_t y_threshold_polyline_3;
	uint32_t y_threshold_polyline_4;
	uint32_t y_threshold_polyline_5;
	uint32_t y_threshold_polyline_6;
	uint32_t y_threshold_polyline_7;
	uint32_t y_threshold_polyline_8;
	uint32_t u_threshold_polyline_0;
	uint32_t u_threshold_polyline_1;
	uint32_t u_threshold_polyline_2;
	uint32_t u_threshold_polyline_3;
	uint32_t u_threshold_polyline_4;
	uint32_t u_threshold_polyline_5;
	uint32_t u_threshold_polyline_6;
	uint32_t u_threshold_polyline_7;
	uint32_t u_threshold_polyline_8;
	uint32_t v_threshold_polyline_0;
	uint32_t v_threshold_polyline_1;
	uint32_t v_threshold_polyline_2;
	uint32_t v_threshold_polyline_3;
	uint32_t v_threshold_polyline_4;
	uint32_t v_threshold_polyline_5;
	uint32_t v_threshold_polyline_6;
	uint32_t v_threshold_polyline_7;
	uint32_t v_threshold_polyline_8;
	uint32_t y_intensity_gain_polyline_0;
	uint32_t y_intensity_gain_polyline_1;
	uint32_t y_intensity_gain_polyline_2;
	uint32_t y_intensity_gain_polyline_3;
	uint32_t y_intensity_gain_polyline_4;
	uint32_t y_intensity_gain_polyline_5;
	uint32_t y_intensity_gain_polyline_6;
	uint32_t y_intensity_gain_polyline_7;
	uint32_t y_intensity_gain_polyline_8;
	uint32_t u_intensity_gain_polyline_0;
	uint32_t u_intensity_gain_polyline_1;
	uint32_t u_intensity_gain_polyline_2;
	uint32_t u_intensity_gain_polyline_3;
	uint32_t u_intensity_gain_polyline_4;
	uint32_t u_intensity_gain_polyline_5;
	uint32_t u_intensity_gain_polyline_6;
	uint32_t u_intensity_gain_polyline_7;
	uint32_t u_intensity_gain_polyline_8;
	uint32_t v_intensity_gain_polyline_0;
	uint32_t v_intensity_gain_polyline_1;
	uint32_t v_intensity_gain_polyline_2;
	uint32_t v_intensity_gain_polyline_3;
	uint32_t v_intensity_gain_polyline_4;
	uint32_t v_intensity_gain_polyline_5;
	uint32_t v_intensity_gain_polyline_6;
	uint32_t v_intensity_gain_polyline_7;
	uint32_t v_intensity_gain_polyline_8;
	uint32_t gradient_weight_polyline_0;
	uint32_t gradient_weight_polyline_1;
	uint32_t gradient_weight_polyline_2;
	uint32_t gradient_weight_polyline_3;
	uint32_t gradient_weight_polyline_4;
	uint32_t gradient_weight_polyline_5;
	uint32_t gradient_weight_polyline_6;
	uint32_t gradient_weight_polyline_7;
	uint32_t gradient_weight_polyline_8;
	uint32_t gradient_weight_polyline_9;
	uint32_t gradient_weight_polyline_10;
	uint32_t u_threshold_factor0;
	uint32_t u_threshold_factor1;
	uint32_t u_threshold_factor2;
	uint32_t u_threshold_factor3;
	uint32_t v_threshold_factor0;
	uint32_t v_threshold_factor1;
	uint32_t v_threshold_factor2;
	uint32_t v_threshold_factor3;
	uint32_t u_divisor_factor0;
	uint32_t u_divisor_factor1;
	uint32_t u_divisor_factor2;
	uint32_t u_divisor_factor3;
	uint32_t v_divisor_factor0;
	uint32_t v_divisor_factor1;
	uint32_t v_divisor_factor2;
	uint32_t v_divisor_factor3;
	uint32_t r1_circle;
	uint32_t r2_circle;
	uint32_t r3_circle;
	uint32_t r1_circle_factor;
	uint32_t r2_circle_factor;
	uint32_t r3_circle_factor;
	uint32_t radius;
	uint32_t r_circle_base;
};

struct isp_3dnr_fast_me {
	uint32_t nr3_channel_sel;
	uint32_t nr3_project_mode;
};

struct isp_dev_3dnr_info {
	uint32_t isupdate;
	uint32_t update_flag;
	struct isp_3dnr_fast_me fast_me;
	struct isp_3dnr_blend_info blend;
};

struct isp_3dnr_fast_me_v1 {
	uint32_t nr3_channel_sel;
	uint32_t nr3_project_mode;
	uint32_t nr3_project_shift_bypass;
};

struct isp_dev_3dnr_info_v1 {
	uint32_t update_flag;
	struct isp_3dnr_fast_me_v1 fast_me;
	struct isp_3dnr_blend_info blend;
};

struct isp_dev_brightness_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t factor;
};

struct isp_dev_contrast_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t factor;
};

struct isp_dev_csa_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t csa_factor_u;
	uint32_t csa_factor_v;
};

struct isp_dev_hue_info {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t hua_cos_value;
	uint32_t hua_sin_value;
};

struct isp_dev_hue_info_l3 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t theta;
};

struct isp_dev_bchs_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bchs_bypass;
	uint32_t cnta_en;
	uint32_t brta_en;
	uint32_t hua_en;
	uint32_t csa_en;
	uint32_t csa_factor_u;
	uint32_t csa_factor_v;
	uint32_t hua_cos_value;
	uint32_t hua_sina_value;
	uint32_t brta_factor;
	uint32_t cnta_factor;
};

struct isp_dev_cce_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint16_t matrix[9];
	uint16_t y_offset;
	uint16_t u_offset;
	uint16_t v_offset;
};

struct isp_dev_cdn_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t filter_bypass;
	uint32_t median_writeback_en;
	uint32_t median_mode;
	uint32_t gaussian_mode;
	uint32_t median_thr;
	uint32_t median_thru0;
	uint32_t median_thru1;
	uint32_t median_thrv0;
	uint32_t median_thrv1;
	uint32_t rangewu[31];
	uint32_t rangewv[31];
	uint32_t level;
};

struct isp_dev_cfa_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t css_bypass;
	uint32_t grid_thr;
	uint32_t min_grid_new;
	uint32_t grid_gain_new;
	uint32_t strong_edge_thr;
	uint32_t uni_dir_intplt_thr_new;
	uint32_t weight_control_bypass;
	uint32_t cdcr_adj_factor;
	uint32_t smooth_area_thr;
	uint32_t readblue_high_sat_thr;
	uint32_t grid_dir_weight_t1;
	uint32_t grid_dir_weight_t2;
	uint32_t round_diff_03_thr;
	uint32_t low_lux_03_thr;
	uint32_t round_diff_12_thr;
	uint32_t low_lux_12_thr;
	uint32_t css_weak_edge_thr;
	uint32_t css_edge_thr;
	uint32_t css_texture1_thr;
	uint32_t css_texture2_thr;
	uint32_t css_uv_val_thr;
	uint32_t css_uv_diff_thr;
	uint32_t css_gray_thr;
	uint32_t css_pix_similar_thr;
	uint32_t css_green_edge_thr;
	uint32_t css_green_weak_edge_thr;
	uint32_t css_green_tex1_thr;
	uint32_t css_green_tex2_thr;
	uint32_t css_green_flat_thr;
	uint32_t css_edge_corr_ratio_r;
	uint32_t css_edge_corr_ratio_b;
	uint32_t css_text1_corr_ratio_r;
	uint32_t css_text1_corr_ratio_b;
	uint32_t css_text2_corr_ratio_r;
	uint32_t css_text2_corr_ratio_b;
	uint32_t css_flat_corr_ratio_r;
	uint32_t css_flat_corr_ratio_b;
	uint32_t css_wedge_corr_ratio_r;
	uint32_t css_wedge_corr_ratio_b;
	uint32_t css_alpha_for_tex2;
	uint32_t css_skin_u_top[2];
	uint32_t css_skin_u_down[2];
	uint32_t css_skin_v_top[2];
	uint32_t css_skin_v_down[2];
};

struct isp_dev_cfa_info_v1 {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t grid_thr;
	uint32_t min_grid_new;
	uint32_t grid_gain_new;
	uint32_t strong_edge_thr;
	uint32_t uni_dir_intplt_thr_new;
	uint32_t cdcr_adj_factor;
	uint32_t smooth_area_thr;
	uint32_t readblue_high_sat_thr;
	uint32_t weight_control_bypass;
	uint32_t grid_dir_weight_t1;
	uint32_t grid_dir_weight_t2;
	uint32_t round_diff_03_thr;
	uint32_t low_lux_03_thr;
	uint32_t round_diff_12_thr;
	uint32_t low_lux_12_thr;
};

struct cmc_matrix {
	uint16_t val[9];
};

struct isp_dev_cmc10_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	struct cmc_matrix matrix;
};

struct edge_pn_config {
	uint32_t p;
	uint32_t n;
};

struct isp_dev_edge_info_v2 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t flat_smooth_mode;
	uint32_t edge_smooth_mode;
	struct edge_pn_config ee_str_d;
	uint32_t mode;
	struct edge_pn_config ee_incr_d;
	struct edge_pn_config ee_edge_thr_d;
	struct edge_pn_config ee_corner_sm;
	struct edge_pn_config ee_corner_gain;
	struct edge_pn_config ee_corner_th;
	uint32_t ee_corner_cor;
	uint32_t ee_cv_t[4];
	struct edge_pn_config ee_cv_clip;
	uint32_t ee_cv_r[3];
	uint32_t ipd_enable; /*ipd_bypass in v1 */
	uint32_t ipd_mask_mode;
	struct edge_pn_config ipd_less_thr;
	uint32_t ipd_smooth_en;
	struct edge_pn_config ipd_smooth_mode;
	struct edge_pn_config ipd_flat_thr;
	struct edge_pn_config ipd_eq_thr;
	struct edge_pn_config ipd_more_thr;
	struct edge_pn_config ipd_smooth_edge_thr;
	struct edge_pn_config ipd_smooth_edge_diff;
	uint32_t ee_ratio_hv_3;
	uint32_t ee_ratio_hv_5;
	uint32_t ee_ratio_diag_3;
	uint32_t ee_weight_hv2diag;
	uint32_t ee_gradient_computation_type;
	uint32_t ee_weight_diag2hv;
	uint32_t ee_gain_hv_t[2][4];
	uint32_t ee_gain_hv_r[2][3];
	uint32_t ee_ratio_diag_5;
	uint32_t ee_gain_diag_t[2][4];
	uint32_t ee_gain_diag_r[2][3];
	uint32_t ee_lum_t[4];
	uint32_t ee_lum_r[3];
	uint32_t ee_pos_t[4];
	uint32_t ee_pos_r[3];
	uint32_t ee_pos_c[3];
	uint32_t ee_neg_t[4];
	uint32_t ee_neg_r[3];
	uint32_t ee_neg_c[3];
	uint32_t ee_freq_t[4];
	uint32_t ee_freq_r[3];

	/* new added below */
	uint32_t ee_new_pyramid_en;
	uint32_t ee_old_gradient_en;
	uint32_t  ee_ratio_old_gradient;
	uint32_t  ee_ratio_new_pyramid;
	uint32_t  ee_offset_thr_layer_curve_pos[3][4];
	uint32_t  ee_offset_ratio_layer_curve_pos[3][3];
	uint32_t  ee_offset_clip_layer_curve_pos[3][3];
	uint32_t  ee_offset_thr_layer_curve_neg[3][4];
	uint32_t  ee_offset_ratio_layer_curve_neg[3][3];
	uint32_t  ee_offset_clip_layer_curve_neg[3][3];
	uint32_t  ee_offset_ratio_layer_lum_curve[3][3];
	uint32_t  ee_offset_ratio_layer_freq_curve[3][3];
};

struct isp_dev_edge_info_v3 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t radius_base;

	uint32_t ipd_enable; /*ipd_bypass in v1 */
	uint32_t ipd_mask_mode;
	uint32_t ee_ipd_1d_en;
	uint32_t ee_ipd_orientation_filter_mode;
	uint32_t ipd_smooth_en;
	struct edge_pn_config ipd_less_thr;
	struct edge_pn_config ipd_smooth_mode;
	struct edge_pn_config ipd_flat_thr;
	struct edge_pn_config ipd_eq_thr;
	struct edge_pn_config ipd_more_thr;
	struct edge_pn_config ipd_smooth_edge_thr;
	struct edge_pn_config ipd_smooth_edge_diff;
	uint32_t ee_ipd_direction_mode;
	uint32_t ee_ipd_direction_freq_hop_control_en;
	uint32_t direction_freq_hop_thresh;
	uint32_t freq_hop_total_num_thresh;
	uint32_t direction_thresh_diff;
	uint32_t direction_thresh_min;
	uint32_t direction_hop_thresh_diff;
	uint32_t direction_hop_thresh_min;

	uint32_t ee_ratio_hv_3;
	uint32_t ee_ratio_hv_5;
	uint32_t ee_ratio_diag_3;
	uint32_t ee_weight_hv2diag;
	uint32_t ee_gradient_computation_type;
	uint32_t ee_gain_hv_t[2][4];
	uint32_t ee_gain_hv_r[2][3];
	uint32_t ee_ratio_diag_5;
	uint32_t ee_gain_diag_t[2][4];
	uint32_t ee_gain_diag_r[2][3];

	uint32_t ee_lum_t[4];
	uint32_t ee_lum_r[3];
	uint32_t ee_freq_t[4];
	uint32_t ee_freq_r[3];
	uint32_t ee_pos_t[4];
	uint32_t ee_pos_r[3];
	uint32_t ee_pos_c[3];
	uint32_t ee_neg_t[4];
	uint32_t ee_neg_r[3];
	uint32_t ee_neg_c[3];

	/* new added below */
	uint32_t ee_new_pyramid_en;
	uint32_t ee_old_gradient_en;
	uint32_t  ee_ratio_old_gradient;
	uint32_t  ee_ratio_new_pyramid;
	uint32_t  ee_offset_thr_layer_curve_pos[2][4];
	uint32_t  ee_offset_ratio_layer_curve_pos[2][3];
	uint32_t  ee_offset_clip_layer_curve_pos[2][3];
	uint32_t  ee_offset_thr_layer_curve_neg[2][4];
	uint32_t  ee_offset_ratio_layer_curve_neg[2][3];
	uint32_t  ee_offset_clip_layer_curve_neg[2][3];
	uint32_t  ee_offset_ratio_layer_lum_curve[2][3];
	uint32_t  ee_offset_ratio_layer_freq_curve[2][3];

	uint32_t ee_offset_ratio_layer0_computation_type;
	uint32_t  ee_offset_layer0_thr_layer_curve_pos[4];
	uint32_t  ee_offset_layer0_ratio_layer_curve_pos[3];
	uint32_t  ee_offset_layer0_clip_layer_curve_pos[3];
	uint32_t  ee_offset_layer0_thr_layer_curve_neg[4];
	uint32_t  ee_offset_layer0_ratio_layer_curve_neg[3];
	uint32_t  ee_offset_layer0_clip_layer_curve_neg[3];
	uint32_t  ee_offset_layer0_ratio_layer_lum_curve[3];
	uint32_t  ee_offset_layer0_ratio_layer_freq_curve[3];
	uint32_t  ee_offset_layer0_gradient_curve_pos[4];
	uint32_t  ee_offset_layer0_gradient_curve_neg[4];

	uint32_t ee_cal_radius_en;
	uint32_t ee_radial_1D_en;
	uint32_t ee_radial_1D_old_gradient_ratio_en;
	uint32_t ee_radial_1D_new_pyramid_ratio_en;
	uint32_t ee_radial_1D_pyramid_layer_offset_en;
	uint32_t center_x;
	uint32_t center_y;
	uint32_t radius;
	uint32_t radius_threshold;
	uint32_t radius_threshold_factor;
	uint32_t old_gradient_ratio_coef;
	uint32_t new_pyramid_ratio_coef;
	uint32_t layer_pyramid_offset_gain_max;
	uint32_t layer_pyramid_offset_clip_ratio_max;
	uint32_t layer_pyramid_offset_coef1;
	uint32_t layer_pyramid_offset_coef2;
	uint32_t layer_pyramid_offset_coef3;
	uint32_t layer_pyramid_offset_gain_min1;
	uint32_t layer_pyramid_offset_gain_min2;
	uint32_t layer_pyramid_offset_gain_min3;
	uint32_t old_gradient_ratio_gain_max;
	uint32_t old_gradient_ratio_gain_min;
	uint32_t new_pyramid_ratio_gain_max;
	uint32_t new_pyramid_ratio_gain_min;
};

struct isp_dev_gamma_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint8_t gain_r[ISP_FRGB_GAMMA_PT_NUM];
	uint8_t gain_g[ISP_FRGB_GAMMA_PT_NUM];
	uint8_t gain_b[ISP_FRGB_GAMMA_PT_NUM];
};

struct isp_dev_gamma_info_v1 {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t gain_r[ISP_FRGB_GAMMA_PT_NUM_V1];
	uint32_t gain_g[ISP_FRGB_GAMMA_PT_NUM_V1];
	uint32_t gain_b[ISP_FRGB_GAMMA_PT_NUM_V1];
};

struct grgb_param {
	uint32_t curve_t[3][4];
	uint32_t curve_r[3][3];
};

struct isp_dev_grgb_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t diff_thd;
	uint32_t hv_edge_thr;
	uint32_t check_sum_clr;
	uint32_t slash_edge_thr;
	uint32_t slash_flat_thr;
	uint32_t gr_ratio;
	uint32_t hv_flat_thr;
	uint32_t gb_ratio;
	struct grgb_param lum;
	struct grgb_param frez;
};

struct isp_dev_hist_info {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t mode;
	uint32_t skip_num;
};

struct isp_dev_hist2_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t mode;
	uint32_t skip_num;
	uint32_t channel_sel;
	struct isp_coord hist_roi;
	uint32_t skip_num_clr;
};

struct isp_dev_hsv_curve_info {
	uint16_t  s_curve[5][4];
	uint16_t  v_curve[5][4];
	uint16_t  r_s[5][2];
	uint16_t  r_v[5][2];
	uint32_t  hrange_left[5];
	uint32_t  hrange_right[5];
};

struct hsv_data {
	uint16_t  hue[360];
	uint16_t  sat[360];
};

struct isp_dev_hsv_info_v2 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t  bypass;
	struct isp_dev_hsv_curve_info curve_info;
	uint32_t size;
	union {
		struct hsv_data hs; /* new format from sharkl5pro... */
		uint32_t hsv_table[ISP_HSV_TABLE_NUM]; /* for roc1/sharkl5/sharkl3...*/
	} d;
};

struct hsv_curve_param {
	uint32_t start_a;
	uint32_t end_a;
	uint32_t start_b;
	uint32_t end_b;
};

struct hsv_param {
	struct hsv_curve_param hsv_curve_param;
};

struct hsv_buf_param{
	uint32_t hsv_buf_sel;
	uint32_t hsv_2d_hue_lut_reg[ISP_HSV_HUE_GRID][ISP_HSV_SAT_GRID];
	uint32_t hsv_2d_sat_lut[ISP_HSV_HUE_GRID][ISP_HSV_SAT_GRID];

};

struct isp_dev_hsv_info_v3 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t  hsv_bypass;
	struct hsv_buf_param buf_param;
	uint32_t  hsv_delta_value_en;
	uint32_t  hsv_hue_thr[ISP_HSV_HUE3][ISP_HSV_HUE2];
	struct hsv_param hsv_param[4];
	uint32_t  y_blending_factor;
	uint32_t  hsv_1d_sat_lut[ISP_HSV_SAT_GRID];
	uint32_t  hsv_1d_hue_lut[ISP_HSV_HUE_GRID];
};

struct isp_dev_hsv_info_v4 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t  hsv_bypass;
	struct hsv_buf_param buf_param;
	uint32_t  hsv_delta_value_en;
	uint32_t  hsv_curve_mode;
	uint32_t  hsv_hue_thr[ISP_HSV_HUE4][ISP_HSV_HUE2];
	struct hsv_param hsv_h_param[4];
	struct hsv_param hsv_s_param[4];
	uint32_t  y_blending_factor;
	uint32_t  hsv_1d_sat_lut[ISP_HSV_SAT_GRID];
	uint32_t  hsv_1d_hue_lut[ISP_HSV_HUE_GRID];
};

struct isp_dev_3dlut_info{
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t rgb3dlut_bypass;
	uint32_t rgb3dlut_ct_table[729][6];
};

struct isp_dev_iircnr_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t mode;
	uint32_t uv_th;
	uint32_t y_max_th;
	uint32_t y_min_th;
	uint32_t uv_dist;
	uint32_t uv_pg_th;
	uint32_t sat_ratio;
	uint32_t uv_low_thr2;
	uint32_t uv_low_thr1;
	uint32_t ymd_u;
	uint32_t ymd_v;
	uint32_t uv_s_th;
	uint32_t slope_y_0;
	uint32_t y_th;
	uint32_t alpha_low_u;
	uint32_t alpha_low_v;
	uint32_t middle_factor_y_0;
	uint32_t uv_high_thr2_0;
	uint32_t ymd_min_u;
	uint32_t ymd_min_v;
	uint32_t uv_low_thr[7][2];
	uint32_t y_edge_thr_max[8];
	uint32_t y_edge_thr_min[8];
	uint32_t uv_high_thr2[7];
	uint32_t slope_y[7];
	uint32_t middle_factor_y[7];
	uint32_t middle_factor_uv[8];
	uint32_t slope_uv[8];
	uint32_t pre_uv_th;
	uint32_t css_lum_thr;
	uint32_t uv_diff_thr;
};

struct isp_dev_dct_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t shrink_en;
	uint32_t addback_en;
	uint32_t addback_ratio;
	uint32_t addback_clip;

	uint32_t coef_thresh0;
	uint32_t coef_thresh1;
	uint32_t coef_thresh2;
	uint32_t coef_thresh3;
	uint32_t coef_ratio0;
	uint32_t coef_ratio1;
	uint32_t coef_ratio2;
	uint32_t lnr_en;
	uint32_t luma_thresh0;
	uint32_t luma_thresh1;
	uint32_t luma_ratio0;
	uint32_t luma_ratio1;
	uint32_t fnr_en;
	uint32_t flat_th;
	uint32_t fnr_ratio0;
	uint32_t fnr_ratio1;
	uint32_t fnr_thresh0;
	uint32_t fnr_thresh1;

	uint32_t rnr_en;
	uint32_t rnr_ratio0;
	uint32_t rnr_ratio1;
	uint32_t rnr_radius;
	uint32_t rnr_radius_factor;
	uint32_t rnr_radius_base;
	uint32_t rnr_imgCenterX;
	uint32_t rnr_imgCenterY;
	uint32_t rnr_step;

	uint32_t blend_en;
	uint32_t blend_radius;
	uint32_t blend_weight;
	uint32_t blend_epsilon;
	uint32_t blend_thresh0;
	uint32_t blend_thresh1;
	uint32_t blend_ratio0;
	uint32_t blend_ratio1;

	uint32_t direction_smooth_en;
	uint32_t direction_mode;
	uint32_t direction_freq_hop_control_en;
	uint32_t direction_freq_hop_thresh;
	uint32_t direction_freq_hop_total_num_thresh;
	uint32_t direction_thresh_diff;
	uint32_t direction_thresh_min;
	uint32_t direction_hop_thresh_diff;
	uint32_t direction_hop_thresh_min;
};

struct isp_dev_nlm_imblance {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t nlm_imblance_en;
	uint32_t nlm_imblance_hv_edge_thr;
	uint32_t nlm_imblance_slash_edge_thr;
	uint32_t nlm_imblance_hv_flat_thr;
	uint32_t nlm_imblance_slash_flat_thr;
	uint32_t nlm_imblance_flag3_grid;
	uint32_t nlm_imblance_flag3_lum;
	uint32_t nlm_imblance_flag3_frez;
	uint32_t nlm_imblance_S_baohedu1;
	uint32_t nlm_imblance_S_baohedu2;
	uint32_t nlm_imblance_lum1_flag2_r;
	uint32_t nlm_imblance_lum1_flag4_r;
	uint32_t nlm_imblance_lum1_flag0_rs;
	uint32_t nlm_imblance_lum1_flag0_r;
	uint32_t nlm_imblance_lum1_flag1_r;
	uint32_t nlm_imblance_lum2_flag2_r;
	uint32_t nlm_imblance_lum2_flag4_r;
	uint32_t nlm_imblance_lum2_flag0_rs;
	uint32_t nlm_imblance_lum2_flag0_r;
	uint32_t nlm_imblance_lum2_flag1_r;
	uint32_t nlm_imblance_lum3_flag2_r;
	uint32_t nlm_imblance_lum3_flag4_r;
	uint32_t nlm_imblance_lum3_flag0_rs;
	uint32_t nlm_imblance_lum3_flag0_r;
	uint32_t nlm_imblance_lum3_flag1_r;
	uint32_t nlm_imblance_lumth1;
	uint32_t nlm_imblance_lumth2;
	uint32_t nlm_imblance_flag12_frezthr;
	uint32_t nlm_imblance_diff;
	uint32_t nlm_imblance_faceRmin;
	uint32_t nlm_imblance_faceRmax;
	uint32_t nlm_imblance_faceBmin;
	uint32_t nlm_imblance_faceBmax;
	uint32_t nlm_imblance_faceGmin;
	uint32_t nlm_imblance_faceGmax;
};

struct isp_dev_nlm_imblance_v1 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t nlm_imblance_bypass;
	uint32_t imblance_radial_1D_en;
	uint32_t nlm_imblance_hv_edge_thr[3];
	uint32_t nlm_imblance_slash_edge_thr[3];
	uint32_t nlm_imblance_hv_flat_thr[3];
	uint32_t nlm_imblance_slash_flat_thr[3];
	uint32_t nlm_imblance_flag3_grid;
	uint32_t nlm_imblance_flag3_lum;
	uint32_t nlm_imblance_flag3_frez;
	uint32_t nlm_imblance_S_baohedu[3][2];
	uint32_t imblance_sat_lumth;
	uint32_t radius_base;
	uint32_t nlm_imblance_lumth1;
	uint32_t nlm_imblance_lumth2;
	uint32_t nlm_imblance_lum1_flag0_rs;
	uint32_t nlm_imblance_lum1_flag0_r;
	uint32_t nlm_imblance_lum1_flag1_r;
	uint32_t nlm_imblance_lum1_flag2_r;
	uint32_t nlm_imblance_lum1_flag3_r;
	uint32_t nlm_imblance_lum1_flag4_r;
	uint32_t nlm_imblance_lum2_flag0_rs;
	uint32_t nlm_imblance_lum2_flag0_r;
	uint32_t nlm_imblance_lum2_flag1_r;
	uint32_t nlm_imblance_lum2_flag2_r;
	uint32_t nlm_imblance_lum2_flag3_r;
	uint32_t nlm_imblance_lum2_flag4_r;
	uint32_t nlm_imblance_lum3_flag0_rs;
	uint32_t nlm_imblance_lum3_flag0_r;
	uint32_t nlm_imblance_lum3_flag1_r;
	uint32_t nlm_imblance_lum3_flag2_r;
	uint32_t nlm_imblance_lum3_flag3_r;
	uint32_t nlm_imblance_lum3_flag4_r;
	uint32_t nlm_imblance_flag12_frezthr;
	uint32_t nlm_imblance_ff_wt0;
	uint32_t nlm_imblance_ff_wt1;
	uint32_t nlm_imblance_ff_wt2;
	uint32_t nlm_imblance_ff_wt3;
	uint32_t nlm_imblance_ff_wr0;
	uint32_t nlm_imblance_ff_wr1;
	uint32_t nlm_imblance_ff_wr2;
	uint32_t nlm_imblance_ff_wr3;
	uint32_t nlm_imblance_ff_wr4;
	uint32_t nlm_imblance_diff[3];
	uint32_t imblance_radial_1D_center_x;
	uint32_t imblance_radial_1D_center_y;
	uint32_t imblance_radial_1D_radius_thr;
	uint32_t imblance_radial_1D_radius_thr_factor;
	uint32_t imblance_radial_1D_protect_ratio_max;
	uint32_t imblance_radial_1D_coef_r0;
	uint32_t imblance_radial_1D_coef_r1;
	uint32_t imblance_radial_1D_coef_r2;
	uint32_t imblance_radial_1D_coef_r3;
	uint32_t imblance_radial_1D_coef_r4;
	uint32_t nlm_imblance_faceRmin;
	uint32_t nlm_imblance_faceRmax;
	uint32_t nlm_imblance_faceBmin;
	uint32_t nlm_imblance_faceBmax;
	uint32_t nlm_imblance_faceGmin;
	uint32_t nlm_imblance_faceGmax;
};

struct isp_dev_nlm_imblance_v2 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t nlm_imblance_bypass;
	uint32_t imblance_radial_1D_en;
	uint32_t nlm_imblance_hv_edge_thr[3];
	uint32_t nlm_imblance_slash_edge_thr[3];
	uint32_t nlm_imblance_hv_flat_thr[3];
	uint32_t nlm_imblance_slash_flat_thr[3];
	uint32_t nlm_imblance_flag3_grid;
	uint32_t nlm_imblance_flag3_lum;
	uint32_t nlm_imblance_flag3_frez;
	uint32_t nlm_imblance_S_baohedu[3][2];
	uint32_t imblance_sat_lumth;
	uint32_t radius_base;
	uint32_t nlm_imblance_lumth1;
	uint32_t nlm_imblance_lumth2;
	uint32_t nlm_imblance_lum1_flag0_rs;
	uint32_t nlm_imblance_lum1_flag0_r;
	uint32_t nlm_imblance_lum1_flag1_r;
	uint32_t nlm_imblance_lum1_flag2_r;
	uint32_t nlm_imblance_lum1_flag3_r;
	uint32_t nlm_imblance_lum1_flag4_r;
	uint32_t nlm_imblance_lum2_flag0_rs;
	uint32_t nlm_imblance_lum2_flag0_r;
	uint32_t nlm_imblance_lum2_flag1_r;
	uint32_t nlm_imblance_lum2_flag2_r;
	uint32_t nlm_imblance_lum2_flag3_r;
	uint32_t nlm_imblance_lum2_flag4_r;
	uint32_t nlm_imblance_lum3_flag0_rs;
	uint32_t nlm_imblance_lum3_flag0_r;
	uint32_t nlm_imblance_lum3_flag1_r;
	uint32_t nlm_imblance_lum3_flag2_r;
	uint32_t nlm_imblance_lum3_flag3_r;
	uint32_t nlm_imblance_lum3_flag4_r;
	uint32_t nlm_imblance_flag12_frezthr;
	uint32_t nlm_imblance_ff_wt0;
	uint32_t nlm_imblance_ff_wt1;
	uint32_t nlm_imblance_ff_wt2;
	uint32_t nlm_imblance_ff_wt3;
	uint32_t nlm_imblance_ff_wr0;
	uint32_t nlm_imblance_ff_wr1;
	uint32_t nlm_imblance_ff_wr2;
	uint32_t nlm_imblance_ff_wr3;
	uint32_t nlm_imblance_ff_wr4;
	uint32_t nlm_imblance_diff[3];
	uint32_t imblance_radial_1D_center_x;
	uint32_t imblance_radial_1D_center_y;
	uint32_t radius;
	uint32_t imblance_radial_1D_radius_thr;
	uint32_t imblance_radial_1D_radius_thr_factor;
	uint32_t imblance_radial_1D_protect_ratio_max;
	uint32_t imblance_radial_1D_coef_r0;
	uint32_t imblance_radial_1D_coef_r1;
	uint32_t imblance_radial_1D_coef_r2;
	uint32_t imblance_radial_1D_coef_r3;
	uint32_t imblance_radial_1D_coef_r4;
	uint32_t nlm_imblance_faceRmin;
	uint32_t nlm_imblance_faceRmax;
	uint32_t nlm_imblance_faceBmin;
	uint32_t nlm_imblance_faceBmax;
	uint32_t nlm_imblance_faceGmin;
	uint32_t nlm_imblance_faceGmax;
	uint32_t grgb_mode;
	uint32_t med_diff[3];
	uint32_t gr_ratio;
	uint32_t gb_ratio;
};

struct lum_flat_param {
	uint16_t thresh;
	uint16_t match_count;
	uint16_t inc_strength;
	uint16_t reserved;
};

struct isp_dev_nlm_info_v2 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t imp_opt_bypass;
	uint32_t flat_opt_bypass;
	uint32_t direction_mode_bypass;
	uint32_t first_lum_byapss;
	uint32_t simple_bpc_bypass;
	uint32_t dist_mode;
	uint32_t radius_bypass;
	uint32_t update_flat_thr_bypass;
	uint8_t w_shift[3];
	uint8_t pack_reserved;
	uint32_t direction_cnt_th;
	uint32_t simple_bpc_lum_th;
	uint32_t simple_bpc_th;
	uint32_t lum_th0;
	uint32_t lum_th1;
	uint32_t diff_th;
	uint32_t tdist_min_th;
	uint16_t lut_w[72];
	struct lum_flat_param lum_flat[3][3];
	uint16_t lum_flat_addback0[3][4];
	uint16_t lum_flat_addback1[3][4];
	uint16_t lum_flat_addback_min[3][4];
	uint16_t lum_flat_addback_max[3][4];
	uint32_t lum_flat_dec_strenth[3];
	uint32_t vst_bypass;
	uint32_t ivst_bypass;
	uint32_t nlm_first_lum_flat_thresh_coef[3][3];
	uint32_t nlm_first_lum_flat_thresh_max[3][3];
	uint32_t nlm_radial_1D_center_x;
	uint32_t nlm_radial_1D_center_y;
	uint32_t nlm_radial_1D_radius_threshold;
	uint32_t nlm_radial_1D_bypass;
	uint32_t nlm_radial_1D_protect_gain_max;
	uint32_t nlm_radial_1D_radius_threshold_filter_ratio[3][4];
	uint32_t nlm_radial_1D_coef2[3][4];
	uint32_t nlm_radial_1D_protect_gain_min[3][4];

	uint32_t radius;
	uint32_t nlm_radial_1D_radius_threshold_factor;
	uint32_t nlm_radial_1D_radius_threshold_filter_ratio_factor[3][4];
	uint32_t radius_base;

	uint32_t nlm_direction_addback_mode_bypass;
	uint32_t nlm_first_lum_direction_addback[3][4];
	uint32_t nlm_first_lum_direction_addback_noise_clip[3][4];

	uint32_t vst_len;
	uint32_t ivst_len;
	/* uint64_t for 32bits/64bits userspace/kernel compatable*/
	uint64_t vst_table_addr;
	uint64_t ivst_table_addr;
};

struct cdn_thruv {
	uint16_t thru0;
	uint16_t thru1;
	uint16_t thrv0;
	uint16_t thrv1;
};

#pragma pack(push)
#pragma pack(4)
struct isp_dev_post_cdn_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t downsample_bypass;
	uint32_t mode;
	uint32_t writeback_en;
	uint32_t uvjoint;
	uint32_t median_mode;
	uint32_t adapt_med_thr;
	uint32_t uvthr0;
	uint32_t uvthr1;
	struct cdn_thruv thr_uv;
	uint8_t r_segu[2][7];
	uint8_t r_segv[2][7];
	uint8_t r_distw[15][5];
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack(4)
struct isp_dev_pre_cdn_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t mode;
	uint32_t median_writeback_en;
	uint32_t median_mode;
	uint32_t den_stren;
	uint32_t uv_joint;
	struct cdn_thruv median_thr_uv;
	uint32_t median_thr;
	uint32_t uv_thr;
	uint32_t y_thr;
	uint8_t r_segu[2][7];
	uint8_t r_segv[2][7];
	uint8_t r_segy[2][7];
	uint8_t r_distw[25];
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack(4)
struct isp_dev_posterize_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	unsigned char posterize_level_bottom[POSTERIZE_NUM];
	unsigned char posterize_level_top[POSTERIZE_NUM];
	unsigned char posterize_level_out[POSTERIZE_NUM];
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack(4)
struct isp_dev_posterize_info_v2 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t  bypass;
	uint32_t  sample_en;
	uint8_t pstrz_r_data[POSTERIZE_NUM2];
	uint8_t pstrz_g_data[POSTERIZE_NUM2];
	uint8_t pstrz_b_data[POSTERIZE_NUM2];
};
#pragma pack(pop)

struct uvd_th {
	uint32_t th_h[2];
	uint32_t th_l[2];
};

struct isp_dev_uvd_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t chk_sum_clr_en;
	uint32_t lum_th_h_len;
	uint32_t lum_th_h;
	uint32_t lum_th_l_len;
	uint32_t lum_th_l;
	uint32_t chroma_min_h;
	uint32_t chroma_min_l;
	uint32_t chroma_max_h;
	uint32_t chroma_max_l;
	struct uvd_th u_th;
	struct uvd_th v_th;
	uint32_t ratio;
	uint32_t ratio_uv_min;
	uint32_t ratio_y_min[2];
	uint32_t ratio0;
	uint32_t ratio1;
	uint32_t y_th_l_len;
	uint32_t y_th_h_len;
	uint32_t uv_abs_th_len;
};

struct isp_dev_uvd_info_v2 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t chk_sum_clr_en;
	uint32_t lum_th_h_len;
	uint32_t lum_th_h;
	uint32_t lum_th_l_len;
	uint32_t lum_th_l;
	uint32_t chroma_ratio;
	uint32_t chroma_max_h;
	uint32_t chroma_max_l;
	struct uvd_th u_th;
	struct uvd_th v_th;
	uint32_t luma_ratio;
	uint32_t ratio_uv_min;
	uint32_t ratio_y_min[2];
	uint32_t ratio0;
	uint32_t ratio1;
	uint32_t y_th_l_len;
	uint32_t y_th_h_len;
	uint32_t uv_abs_th_len;
};

struct isp_dev_uvd_info_v1 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t y_th_l_len;
	uint32_t y_th_h_len;
	uint32_t lum_th_h_len;
	uint32_t lum_th_h;
	uint32_t lum_th_l_len;
	uint32_t lum_th_l;
	struct uvd_th u_th;
	struct uvd_th v_th;
	uint32_t chroma_ratio;
	uint32_t luma_ratio;
	uint32_t ratio_y_min[2];
	uint32_t ratio0;
	uint32_t ratio1;
	uint32_t chroma_max1_h;
	uint32_t chroma_max1_m;
	uint32_t chroma_max1_l;
	uint32_t chroma_max2_h;
	uint32_t chroma_max2_m;
	uint32_t chroma_max2_l;
};

#pragma pack(push)
#pragma pack(4)
struct isp_dev_ygamma_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint8_t gain[ISP_YUV_GAMMA_NUM];
};

struct isp_dev_ygamma_info_v1 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t gain[ISP_YUV_GAMMA_NUM_V1];
};
#pragma pack(pop)

struct isp_dev_ynr_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t lowlux_bypass;
	uint32_t nr_enable;
	uint32_t l_blf_en[3];
	uint32_t txt_th;
	uint32_t edge_th;
	uint32_t flat_th[7];
	uint32_t lut_th[7];
	uint32_t addback[9];
	uint32_t sub_th[9];
	uint32_t l_euroweight[3][3];
	uint32_t l_wf_index[3];
	uint32_t l0_lut_th0;
	uint32_t l0_lut_th1;
	uint32_t l1_txt_th0;
	uint32_t l1_txt_th1;
	uint32_t wlt_th[24];
	uint32_t freq_ratio[24];
	struct isp_img_offset start_pos;
	struct isp_img_offset center;
	uint32_t radius;
	uint32_t dist_interval;
	unsigned char sal_nr_str[8];
	unsigned char sal_offset[8];
	uint32_t edgeStep[8];
	uint32_t wlt_T[3];
	uint32_t ad_para[3];
	uint32_t ratio[3];
	uint32_t maxRadius;
};

/* new ynr: all updated. */
struct isp_dev_ynr_info_v2 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t l3_addback_enable;
	uint32_t l2_addback_enable;
	uint32_t l1_addback_enable;
	uint32_t l0_addback_enable;
	uint32_t l3_blf_en;
	uint32_t sal_enable;
	uint32_t l3_wv_nr_enable;
	uint32_t l2_wv_nr_enable;
	uint32_t l1_wv_nr_enable;
	uint32_t blf_range_index;
	uint32_t blf_dist_weight2;
	uint32_t blf_dist_weight1;
	uint32_t blf_dist_weight0;
	int32_t blf_range_s4;
	int32_t blf_range_s3;
	int32_t blf_range_s2;
	int32_t blf_range_s1;
	uint32_t coef_model;
	uint32_t blf_range_s0_high;
	uint32_t blf_range_s0_mid;
	uint32_t blf_range_s0_low;
	uint32_t lum_thresh1;
	uint32_t lum_thresh0;
	uint32_t l1_wv_ratio2_low;
	uint32_t l1_wv_ratio1_low;
	uint32_t l1_soft_offset_low;
	uint32_t l1_wv_thr1_low;
	uint32_t l1_wv_ratio_d2_low;
	uint32_t l1_wv_ratio_d1_low;
	uint32_t l1_soft_offset_d_low;
	uint32_t l1_wv_thr_d1_low;
	uint32_t l1_wv_ratio2_mid;
	uint32_t l1_wv_ratio1_mid;
	uint32_t l1_soft_offset_mid;
	uint32_t l1_wv_thr1_mid;
	uint32_t l1_wv_ratio_d2_mid;
	uint32_t l1_wv_ratio_d1_mid;
	uint32_t l1_soft_offset_d_mid;
	uint32_t l1_wv_thr_d1_mid;
	uint32_t l1_wv_ratio2_high;
	uint32_t l1_wv_ratio1_high;
	uint32_t l1_soft_offset_high;
	uint32_t l1_wv_thr1_high;
	uint32_t l1_wv_ratio_d2_high;
	uint32_t l1_wv_ratio_d1_high;
	uint32_t l1_soft_offset_d_high;
	uint32_t l1_wv_thr_d1_high;
	uint32_t l2_wv_ratio2_low;
	uint32_t l2_wv_ratio1_low;
	uint32_t l2_soft_offset_low;
	uint32_t l2_wv_thr1_low;
	uint32_t l2_wv_ratio_d2_low;
	uint32_t l2_wv_ratio_d1_low;
	uint32_t l2_soft_offset_d_low;
	uint32_t l2_wv_thr_d1_low;
	uint32_t l2_wv_ratio2_mid;
	uint32_t l2_wv_ratio1_mid;
	uint32_t l2_soft_offset_mid;
	uint32_t l2_wv_thr1_mid;
	uint32_t l2_wv_ratio_d2_mid;
	uint32_t l2_wv_ratio_d1_mid;
	uint32_t l2_soft_offset_d_mid;
	uint32_t l2_wv_thr_d1_mid;
	uint32_t l2_wv_ratio2_high;
	uint32_t l2_wv_ratio1_high;
	uint32_t l2_soft_offset_high;
	uint32_t l2_wv_thr1_high;
	uint32_t l2_wv_ratio_d2_high;
	uint32_t l2_wv_ratio_d1_high;
	uint32_t l2_soft_offset_d_high;
	uint32_t l2_wv_thr_d1_high;
	uint32_t l3_wv_ratio2_low;
	uint32_t l3_wv_ratio1_low;
	uint32_t l3_soft_offset_low;
	uint32_t l3_wv_thr1_low;
	uint32_t l3_wv_ratio_d2_low;
	uint32_t l3_wv_ratio_d1_low;
	uint32_t l3_soft_offset_d_low;
	uint32_t l3_wv_thr_d1_low;
	uint32_t l3_wv_ratio2_mid;
	uint32_t l3_wv_ratio1_mid;
	uint32_t l3_soft_offset_mid;
	uint32_t l3_wv_thr1_mid;
	uint32_t l3_wv_ratio_d2_mid;
	uint32_t l3_wv_ratio_d1_mid;
	uint32_t l3_soft_offset_d_mid;
	uint32_t l3_wv_thr_d1_mid;
	uint32_t l3_wv_ratio2_high;
	uint32_t l3_wv_ratio1_high;
	uint32_t l3_soft_offset_high;
	uint32_t l3_wv_thr1_high;
	uint32_t l3_wv_ratio_d2_high;
	uint32_t l3_wv_ratio_d1_high;
	uint32_t l3_soft_offset_d_high;
	uint32_t l3_wv_thr_d1_high;
	uint32_t l3_wv_thr2_high;
	uint32_t l3_wv_thr2_mid;
	uint32_t l3_wv_thr2_low;
	uint32_t l2_wv_thr2_high;
	uint32_t l2_wv_thr2_mid;
	uint32_t l2_wv_thr2_low;
	uint32_t l1_wv_thr2_high;
	uint32_t l1_wv_thr2_mid;
	uint32_t l1_wv_thr2_low;
	uint32_t l3_wv_thr_d2_high;
	uint32_t l3_wv_thr_d2_mid;
	uint32_t l3_wv_thr_d2_low;
	uint32_t l2_wv_thr_d2_high;
	uint32_t l2_wv_thr_d2_mid;
	uint32_t l2_wv_thr_d2_low;
	uint32_t l1_wv_thr_d2_high;
	uint32_t l1_wv_thr_d2_mid;
	uint32_t l1_wv_thr_d2_low;
	uint32_t l1_addback_ratio;
	uint32_t l1_addback_clip;
	uint32_t l0_addback_ratio;
	uint32_t l0_addback_clip;
	uint32_t l3_addback_ratio;
	uint32_t l3_addback_clip;
	uint32_t l2_addback_ratio;
	uint32_t l2_addback_clip;
	uint32_t lut_thresh3;
	uint32_t lut_thresh2;
	uint32_t lut_thresh1;
	uint32_t lut_thresh0;
	uint32_t lut_thresh6;
	uint32_t lut_thresh5;
	uint32_t lut_thresh4;
	uint32_t sal_offset3;
	uint32_t sal_offset2;
	uint32_t sal_offset1;
	uint32_t sal_offset0;
	uint32_t sal_offset7;
	uint32_t sal_offset6;
	uint32_t sal_offset5;
	uint32_t sal_offset4;
	uint32_t sal_nr_str3;
	uint32_t sal_nr_str2;
	uint32_t sal_nr_str1;
	uint32_t sal_nr_str0;
	uint32_t sal_nr_str7;
	uint32_t sal_nr_str6;
	uint32_t sal_nr_str5;
	uint32_t sal_nr_str4;
	uint32_t start_row;
	uint32_t start_col;
	uint32_t center_y;
	uint32_t center_x;
	uint32_t dis_interval;
	uint32_t radius;
	uint32_t radius_factor;
	uint32_t max_radius;
	uint32_t max_radius_factor;
	uint32_t radius_base;
};

struct isp_ynr_layer{
	uint32_t gf_enable;
	uint32_t gf_radius;
	uint32_t gf_rnr_offset;
	uint32_t gf_rnr_ratio;
	uint32_t gf_addback_enable;
	uint32_t gf_addback_ratio;
	uint32_t gf_addback_clip;
	uint32_t gf_epsilon_low;
	uint32_t gf_epsilon_mid;
	uint32_t gf_epsilon_high;
	uint32_t lum_thresh[2];
};

struct isp_dev_ynr_info_v3 {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t radius;
	uint32_t radius_base;
	uint32_t radius_factor;
	uint32_t imgCenterX;
	uint32_t imgCenterY;
	struct isp_ynr_layer ynr_layer[5];
};

struct isp_dev_yrandom_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t mode;
	uint32_t seed;
	uint32_t offset;
	uint32_t shift;
	uint8_t takeBit[8];
};

struct isp_dev_noise_filter_info {
	uint32_t isupdate;
	uint32_t update_flag;
	uint32_t yrandom_bypass;
	uint32_t shape_mode;
	uint32_t filter_thr_mode;
	uint32_t yrandom_mode;
	uint32_t yrandom_init;
	uint32_t yrandom_seed[4];
	uint32_t takebit[8];
	uint32_t r_offset;
	uint32_t r_shift;
	uint32_t filter_thr;
	uint32_t cv_t[4];
	uint32_t cv_r[3];
	struct edge_pn_config  noise_clip;
};

struct isp_pyramid_onl_info {
	uint32_t update_flag;
	uint32_t dec1_onine_bypass;
	uint32_t store_dec1_bypass;
	uint32_t store_dec2_bypass;
	uint32_t store_dec3_bypass;
	uint32_t store_dec4_bypass;
	uint32_t reconstruction_byass;
	uint32_t online_layer_num;
};

struct isp_pyramid_offl_info {
	uint32_t update_flag;
	uint32_t dec_offline_bypass;
	uint32_t reconstruction_byass;
	uint32_t offline_layer_num;
};

struct isp_dev_ltm_bypass {
	uint32_t ltm_hist_stat_bypass;
	uint32_t ltm_map_bypass;
};

struct isp_ltm_tile_num_minus1 {
	uint32_t tile_num_x;
	uint32_t tile_num_y;
};

struct isp_ltm_tile_size {
	uint32_t tile_width;
	uint32_t tile_height;
};

struct isp_ltm_clip_limit {
	uint32_t limit;
	uint32_t limit_min;
};

struct isp_dev_ltm_stat_info {
	uint32_t bypass; /* bypass */

	struct isp_ltm_tile_num_minus1 tile_num;
	struct isp_ltm_tile_size tile_size;
	struct isp_ltm_clip_limit tile_clip;

	uint32_t strength;
	uint32_t tile_num_auto;

	uint32_t text_point_thres; /* text_point_thres */
	uint32_t text_proportion; /* texture_proportion */
	float text_point_alpha; /* text_point_alpha */
	uint32_t region_est_en; /* region_est_en */
	uint32_t binning_en;
	uint32_t channel_sel;
	uint32_t ltm_hist_table[128];
};

struct isp_dev_ltm_map_info {
	uint32_t bypass; /* ltm map bypass */
	uint32_t ltm_map_video_mode;
};

struct isp_dev_ltm_info {
	uint32_t update_flag;
	struct isp_dev_ltm_stat_info ltm_stat;
	struct isp_dev_ltm_map_info ltm_map;
};

struct isp_rgb_ltm_tile_num_minus1 {
	uint32_t tile_num_x;
	uint32_t tile_num_y;
};

struct isp_rgb_ltm_text{
	uint32_t text_point_alpha;
	uint32_t text_point_thres;
	uint32_t textture_proporion;
};

struct isp_dev_rgb_ltm_stat_info {
	uint32_t bypass; /* bypass */
	struct isp_rgb_ltm_tile_num_minus1 tile_num;
	struct isp_rgb_ltm_text ltm_text;
	uint32_t strength;
	uint32_t tile_num_auto;
	uint32_t text_point_thres; /* text_point_thres */
	uint32_t region_est_en; /* region_est_en */
	uint32_t channel_sel;
	uint32_t ltm_hist_table[128];
	uint16_t clip_limit;
	uint16_t clip_limit_min;
};

struct isp_dev_rgb_ltm_map_info {
	uint32_t bypass; /* ltm map bypass */
	uint32_t ltm_map_video_mode;
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t tile_x_num;
	uint32_t tile_y_num;
	uint32_t frame_width;
	uint32_t frame_height;
/*
 * normal is 8192 * sizeof(uint16_t),
 * abnormal is 8192 * 4 * sizeof(uint16_t) in 108M nonzsl capture for qogirl6
 */
	uint32_t ltm_map_size;
	uint64_t ltm_map_addr;
};

struct isp_dev_rgb_ltm_info {
	uint32_t isupdate;
	uint32_t update_flag;
	struct isp_dev_rgb_ltm_stat_info ltm_stat;
	struct isp_dev_rgb_ltm_map_info ltm_map;
};

struct isp_yuv_ltm_tile_num_minus1 {
	uint32_t tile_num_x;
	uint32_t tile_num_y;
};

struct isp_yuv_ltm_text{
	uint32_t text_point_alpha;
	uint32_t text_point_thres;
	uint32_t textture_proporion;
};

struct isp_dev_yuv_ltm_stat_info {
	uint32_t bypass; /* bypass */
	struct isp_yuv_ltm_tile_num_minus1 tile_num;
	struct isp_yuv_ltm_text ltm_text;
	uint32_t strength;
	uint32_t tile_num_auto;
	uint32_t text_point_thres; /* text_point_thres */
	uint32_t region_est_en; /* region_est_en */
	uint32_t channel_sel;
	uint32_t ltm_hist_table[128];
};

struct isp_dev_yuv_ltm_map_info {
	uint32_t bypass; /* ltm map bypass */
	uint32_t ltm_map_video_mode;
};

struct isp_dev_yuv_ltm_info {
	uint32_t isupdate;
	uint32_t update_flag;
	struct isp_dev_yuv_ltm_stat_info ltm_stat;
	struct isp_dev_yuv_ltm_map_info ltm_map;
};

/*********************************************/
struct isp_rrgb {
	uint32_t r;
	uint32_t b;
	uint32_t gr;
	uint32_t gb;
};

struct isp_dev_pdaf_info {
	uint32_t bypass;
	uint32_t corrector_bypass;
	uint32_t phase_map_corr_en;
	struct isp_img_size block_size;
	uint32_t grid_mode;
	struct isp_coord win;
	struct isp_coord block;
	struct isp_rrgb gain_upperbound;
	uint32_t phase_txt_smooth;
	uint32_t phase_gfilter;
	uint32_t phase_flat_smoother;
	uint32_t hot_pixel_th[3];
	uint32_t dead_pixel_th[3];
	uint32_t flat_th;
	uint32_t edge_ratio_hv;
	uint32_t edge_ratio_rd;
	uint32_t edge_ratio_hv_rd;
	uint32_t phase_left_addr;
	uint32_t phase_right_addr;
	uint32_t phase_pitch;
	uint32_t pattern_pixel_is_right[PDAF_PPI_NUM];
	uint32_t pattern_pixel_row[PDAF_PPI_NUM];
	uint32_t pattern_pixel_col[PDAF_PPI_NUM];
	uint32_t gain_ori_left[2];
	uint32_t gain_ori_right[2];
	uint32_t extractor_bypass;
	uint32_t mode_sel;
	uint32_t skip_num;
	uint32_t phase_data_dword_num;
	struct isp_rrgb pdaf_blc;
	uint32_t data_ptr_left[2];
	uint32_t data_ptr_right[2];
};

struct pdaf_ppi_info {
	struct isp_img_size block_size;
	struct isp_coord block;
	uint32_t bypass;
	uint32_t pd_pos_size;
	uint32_t pattern_pixel_is_right[PDAF_PPI_NUM];
	uint32_t pattern_pixel_row[PDAF_PPI_NUM];
	uint32_t pattern_pixel_col[PDAF_PPI_NUM];
};

struct pdaf_roi_info {
	struct isp_coord win;
	uint32_t phase_data_write_num;
};

struct dev_dcam_vc2_control {
	uint32_t bypass;
	uint32_t vch2_vc;
	uint32_t vch2_data_type;
	uint32_t vch2_mode;
};

struct isp_statis_buf_input {
	enum isp_statis_buf_type type;

	/* for init all */
	union {
		int32_t mfd_array[STATIS_TYPE_MAX][STATIS_BUF_NUM_MAX];
		int32_t mfd_pmdbg[PARAM_BUF_NUM_MAX];
	};
	union {
		int32_t offset_array[STATIS_TYPE_MAX][STATIS_BUF_NUM_MAX];
		int32_t offset_pmdbg[PARAM_BUF_NUM_MAX];
	};

	/* for single buffer set */
	int32_t mfd;
	uint32_t offset;
};

enum raw_proc_cmd {
	RAW_PROC_PRE,
	RAW_PROC_POST,
	RAW_PROC_DONE,
	VIRTUAL_SENSOR_PROC,
};


enum camraw_postproc_scene {
	CAMRAW_POSTPROC_PIPELINE_DCAM_PROCESS = 0,
	CAMRAW_POSTPROC_PIPELINE_ISP_PROCESS,
	CAMRAW_POSTPROC_PIPELINE_PROCESS,
	CAMRAW_POSTPROC_TYPE_MAX,
};

struct isp_raw_proc_info {
	enum raw_proc_cmd cmd;
	struct isp_img_size src_size;
	struct isp_img_size dst_size;
	struct isp_img_rect crop;
	uint32_t src_y_endian;
	uint32_t src_uv_endian;
	uint32_t dst_y_endian;
	uint32_t dst_uv_endian;
	uint32_t src_format;
	uint32_t src_pattern;
	uint32_t dst_format;
	uint32_t frame_id;
	int32_t fd_src;
	int32_t fd_dst;
	int32_t fd_pm;
	uint32_t pm_offset;/* mw camera frame param pipeline param offset */
	uint32_t src_offset;/*first bytes offset in buffer fd_src*/
	uint32_t dst_offset;/*first bytes offset in buffer fd_dst0*/
	uint32_t dcam_raw_fmt;
	enum camraw_postproc_scene scene;
};

/********** which blocks need open in postproc process *************/
struct isp_pipeline_debug_param {
	uint32_t lscm_id;
	uint32_t aem_id;
	uint32_t awbc_id;
	uint32_t ltm_id;
};

struct cam_blk_nr_param {
	struct isp_dev_post_cnr_h_info post_cnr_h_info;
	struct isp_dev_ynr_info_v3 ynr_info_v3;
	struct isp_dev_cnr_h_info cnr_info;
	struct isp_dev_dct_info dct_info;
};

struct dcam_dev_aem_param {
	uint32_t update_flag;
	uint32_t mode;
	uint32_t bypass;
	uint32_t skip_num;
	struct dcam_dev_aem_win win_info;
	struct dcam_dev_aem_thr aem_info;
};

struct afm_win_param {
	uint32_t crop_eb;
	struct isp_img_rect crop_size;
	struct isp_img_size win_num;
	struct isp_img_size done_tile_num;
	struct isp_img_rect win;
};

struct dcam_dev_afm_param {
	uint32_t update_flag;
	struct dcam_dev_afm_info af_param;
	uint32_t mode;
	uint32_t bypass;
	uint32_t skip_num;
	struct dcam_dev_afm_iir_info af_iir_info;
	struct afm_win_param win_parm;
};

struct dcam_dev_pdaf_param {
	uint32_t update_flag;
	uint32_t bypass;
	uint32_t mode;
	uint32_t skip_num;
	uint32_t pdaf_type;
	struct isp_dev_pdaf_info pdaf_info;
	struct dev_dcam_vc2_control vch2_info;
	struct pdaf_ppi_info ppi_info;
	struct pdaf_roi_info roi_info;
	struct dcam_ppe_ppc_info ppe_ppc_info;
};

struct isp_pipeline_param_l3 {
	uint32_t stream_type;
	uint32_t frame_id;
	uint32_t real_index;

	/* 3A owner: */
	struct dcam_dev_aem_param aem_info;
	struct dcam_dev_afm_param afm_info;
	struct isp_dev_anti_flicker_new_info afl_param;
	struct dcam_dev_hist_info bhist_info;
	struct isp_dev_hist_info hist_info;
	struct dcam_dev_pdaf_param pdaf_info;

	/* dcam related tuning blocks */
	struct dcam_dev_awbc_info awbc_param;
	struct dcam_dev_rgb_gain_info gain_param;
	struct dcam_dev_blc_info blc_param;
	struct dcam_dev_lsc_info lsc_param;
	struct dcam_dev_rgb_dither_info dither_param;
	struct dcam_dev_bpc_info_l3 bpc_param;
	struct isp_dev_grgb_info grgb_param;
	/* dcam blocks end. */

	/* isp blocks below */
	struct isp_dev_posterize_info posterize_param;
	struct isp_dev_3dnr_info nr3_param;
	struct isp_dev_contrast_info c_param;
	struct isp_dev_csa_info s_param;
	struct isp_dev_brightness_info b_param;
	struct isp_dev_hue_info_l3 h_param;
	struct isp_dev_cce_info cce_param;
	struct isp_dev_cfa_info cfa_param;
	struct isp_dev_cmc10_info cmc10_param;
	struct isp_dev_edge_info_v2 edge_param;
	struct isp_dev_gamma_info gamma_param;
	struct isp_dev_hsv_info_v2 hsv_param;
	struct isp_dev_iircnr_info iircnr_param;
	struct isp_dev_yrandom_info yrandom_param;
	struct isp_dev_nlm_info_v2 nlm_param;
	struct isp_dev_pre_cdn_info precdn_param;
	struct isp_dev_cdn_info cdn_param;
	struct isp_dev_post_cdn_info postcdn_param;
	struct isp_dev_uvd_info uvd_param;
	struct isp_dev_ynr_info ynr_param;
	struct isp_dev_noise_filter_info nf_param;
	struct isp_dev_ygamma_info ygamma_param;
	struct isp_pipeline_debug_param dbg_param;
};

struct isp_pipeline_param_l5 {
	uint32_t stream_type;
	uint32_t frame_id;
	uint32_t real_index;

	/* 3A owner: */
	struct dcam_dev_aem_param aem_info;
	struct dcam_dev_afm_param afm_info;
	struct isp_dev_anti_flicker_new_info afl_param;
	struct dcam_dev_hist_info bhist_info;
	struct isp_dev_hist2_info hist_info;
	struct dcam_dev_pdaf_param pdaf_info;

	/* dcam related tuning blocks */
	struct dcam_dev_awbc_info awbc_param;
	struct dcam_dev_rgb_gain_info gain_param;
	struct dcam_dev_blc_info blc_param;
	struct dcam_dev_lsc_info lsc_param;
	struct dcam_dev_rgb_dither_info dither_param;
	struct dcam_dev_bpc_info bpc_param;
	struct isp_dev_grgb_info grgb_param;
	struct dcam_bpc_ppi_info ppi_param;
	/* dcam blocks end. */

	/* isp blocks below */
	struct isp_dev_nlm_imblance imbalance_param;
	struct isp_dev_ltm_info ltm_param;
	struct isp_dev_posterize_info_v2 posterize_param;
	struct isp_dev_3dnr_info nr3_param;
	struct isp_dev_bchs_info bchs_param;
	struct isp_dev_cce_info cce_param;
	struct isp_dev_cfa_info cfa_param;
	struct isp_dev_cmc10_info cmc10_param;
	struct isp_dev_edge_info_v2 edge_param;
	struct isp_dev_gamma_info gamma_param;
	struct isp_dev_hsv_info_v2 hsv_param;
	struct isp_dev_iircnr_info iircnr_param;
	struct isp_dev_yrandom_info yrandom_param;
	struct isp_dev_nlm_info_v2 nlm_param;
	struct isp_dev_pre_cdn_info precdn_param;
	struct isp_dev_cdn_info cdn_param;
	struct isp_dev_post_cdn_info postcdn_param;
	struct isp_dev_uvd_info_v2 uvd_param;
	struct isp_dev_ynr_info_v2 ynr_param;
	struct isp_dev_noise_filter_info nf_param;
	struct isp_dev_ygamma_info ygamma_param;
	struct isp_pipeline_debug_param dbg_param;
};

struct isp_pipeline_param_l5pro {
	uint32_t stream_type;
	uint32_t frame_id;
	uint32_t real_index;

	/* 3A owner: */
	struct dcam_dev_aem_param aem_info;
	struct dcam_dev_afm_param afm_info;
	struct isp_dev_anti_flicker_new_info afl_param;
	struct dcam_dev_lscm_param lscm_info;
	struct dcam_dev_hist_info bhist_info;
	struct isp_dev_hist2_info hist_info;
	struct dcam_dev_pdaf_param pdaf_info;

	/* dcam related tuning blocks */
	struct dcam_dev_awbc_info awbc_param;
	struct dcam_dev_rgb_gain_info gain_param;
	struct dcam_dev_blc_info blc_param;
	struct dcam_dev_rgb_dither_info dither_param;
	struct dcam_dev_bpc_info bpc_param;
	struct dcam_bpc_ppi_info ppi_param;
	struct dcam_dev_lsc_info lsc_param;
	struct isp_dev_grgb_info grgb_param;
	/* dcam blocks end. */

	/* isp blocks below */
	struct  isp_dev_posterize_info_v2 posterize_param;
	struct isp_dev_3dnr_info nr3_param;
	struct isp_dev_bchs_info bchs_param;
	struct isp_dev_cce_info cce_param;
	struct isp_dev_cfa_info cfa_param;
	struct isp_dev_cmc10_info cmc10_param;
	struct isp_dev_edge_info_v2 edge_param;
	struct isp_dev_gamma_info gamma_param;
	struct isp_dev_hsv_info_v2 hsv_param;
	struct isp_dev_iircnr_info iircnr_param;
	struct isp_dev_yrandom_info yrandom_param;
	struct isp_dev_rgb_ltm_info ltm_param;
	struct dcam_dev_raw_gtm_block_info gtm_param;
	struct isp_dev_nlm_info_v2 nlm_param;
	struct isp_dev_nlm_imblance_v1 imbalance_param;
	struct isp_dev_pre_cdn_info precdn_param;
	struct isp_dev_cdn_info cdn_param;
	struct isp_dev_post_cdn_info postcdn_param;
	struct isp_dev_uvd_info_v2 uvd_param;
	struct isp_dev_ynr_info_v2 ynr_param;
	struct isp_dev_noise_filter_info nf_param;
	struct isp_dev_ygamma_info ygamma_param;
	struct isp_pipeline_debug_param dbg_param;
};

struct isp_pipeline_param_l6 {
	uint32_t stream_type;
	uint32_t frame_id;
	uint32_t real_index;

	/* 3A owner: */
	struct dcam_dev_aem_param aem_info;
	struct dcam_dev_afm_param afm_info;
	struct dcam_dev_pdaf_param pdaf_info;
	struct dcam_dev_lscm_param lscm_info;
	struct isp_dev_hist2_info hist_info;
	struct dcam_dev_hist_info bhist_info;
	struct isp_dev_anti_flicker_new_info afl_param;

	/* dcam related tuning blocks */
	struct dcam_dev_awbc_info awbc_param;
	struct dcam_dev_rgb_gain_info gain_param;
	struct dcam_dev_lsc_info lsc_param;
	struct dcam_dev_blc_info blc_param;
	struct dcam_dev_rgb_dither_info dither_param;
	struct dcam_dev_bpc_info bpc_param;
	struct dcam_bpc_ppi_info ppi_param;
	struct dcam_dev_rds_param rds_param;
	/* dcam blocks end. */

	/* isp blocks below */
	struct isp_dev_3dnr_info nr3_param;
	struct isp_dev_bchs_info bchs_param;
	struct isp_dev_cce_info cce_param;
	struct isp_dev_cfa_info cfa_param;
	struct isp_dev_cmc10_info cmc10_param;
	struct isp_dev_edge_info_v2 edge_param;
	struct isp_dev_gamma_info gamma_param;
	struct isp_dev_hsv_info_v3 hsv3_param;
	struct isp_dev_iircnr_info iircnr_param;
	struct isp_dev_yrandom_info yrandom_param;
	struct isp_dev_rgb_ltm_info ltm_param;
	struct dcam_dev_raw_gtm_block_info gtm_param;
	struct isp_dev_nlm_info_v2 nlm_param;
	struct isp_dev_nlm_imblance_v2 imbalance_param;
	struct isp_dev_pre_cdn_info precdn_param;
	struct isp_dev_cdn_info cdn_param;
	struct isp_dev_post_cdn_info postcdn_param;
	struct isp_dev_uvd_info_v2 uvd_param;
	struct isp_dev_ynr_info_v2 ynr_param;
	struct isp_dev_noise_filter_info nf_param;
	struct isp_dev_ygamma_info ygamma_param;
	struct isp_pipeline_debug_param dbg_param;
};

struct isp_pipeline_param_n6pro {
	uint32_t stream_type;
	uint32_t frame_id;
	uint32_t real_index;

	/* DCAM BLOCK */
	struct dcam_dev_aem_param aem_info;
	struct dcam_dev_afm_param afm_info;
	struct dcam_dev_pdaf_param pdaf_info;
	struct dcam_dev_lscm_param lscm_info;
	struct isp_dev_hist2_info hist_info;
	struct dcam_dev_hist_info bhist_info;

	struct dcam_dev_blc_info blc_param;
	struct dcam_dev_rgb_gain_info gain_param;
	struct dcam_dev_rgb_dither_info dither_param;
	struct dcam_dev_lsc_info lsc_param;
	struct isp_dev_anti_flicker_new_info afl_param;
	struct dcam_dev_awbc_info awbc_param;
	struct dcam_dev_bpc_info_v1 bpc_param;
	struct dcam_bpc_ppi_info ppi_param;
	struct dcam_dev_rgb_gtm_block_info gtm_param;
	struct isp_dev_gamma_info_v1 gamma_param;
	struct isp_dev_cmc10_info cmc10_param;
	struct isp_dev_cfa_info_v1 cfa_param;
	struct isp_dev_nlm_info_v2 nlm_param;
	struct isp_dev_nlm_imblance_v2 imbalance_param;
	struct isp_dev_cce_info cce_param;

	/* ISP BLOCK */
	struct isp_dev_rgb_ltm_info ltm_param;
	struct isp_dev_bchs_info bchs_param;
	struct isp_dev_cnr_h_info cnr_param;
	struct isp_dev_post_cnr_h_info pcnr_param;
	struct isp_dev_cdn_info cdn_param;
	struct isp_dev_edge_info_v3 edge_param;
	struct isp_dev_3dlut_info lut3d_param;
	struct isp_dev_uvd_info_v1 uvd_param;
	struct isp_dev_ygamma_info_v1 ygamma_param;
	struct isp_dev_yrandom_info yrandom_param;
	struct isp_dev_ynr_info_v3 ynr_param;
	struct isp_dev_hsv_info_v3 hsv3_param;
	struct isp_dev_hsv_info_v4 hsv4_param;
	struct isp_dev_3dnr_info_v1 nr3_param;
	struct isp_dev_noise_filter_info nf_param;
	struct isp_pyramid_onl_info pyronl_param;
	struct isp_pyramid_offl_info pyroffl_param;
	struct isp_dev_dct_info dct_param;
	struct isp_pipeline_debug_param dbg_param;
};

#endif
