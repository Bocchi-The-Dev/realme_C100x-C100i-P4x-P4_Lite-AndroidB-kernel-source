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

#ifndef _PYR_DEC_NODE_H_
#define _PYR_DEC_NODE_H_

#include "cam_types.h"
#include "cam_queue.h"
#include "isp_slice.h"
#include "sprd_isp_2v6.h"
#include "cam_buf_manager.h"
#include "pyr_dec_port.h"

#ifdef PYR_DEC_DEBUG_ON
#define PYR_DEC_DEBUG pr_info
#else
#define PYR_DEC_DEBUG pr_debug
#endif

#define PYR_DEC_LAYER_NUM           ISP_PYR_DEC_LAYER_NUM
#define PYR_DEC_BUF_Q_LEN           31
#define PYR_DEC_ADDR_NUM            PYR_DEC_LAYER_NUM + 1
#define PYR_DEC_NODE_NUM_MAX        4
#define PYR_DEC_INT_PROC_FRM_NUM    256

 /*dec limit hw: 30ms, && 3000us(3ms)/M, node:50ms*/
#define DEC_HW_TIME_RATIO           3000
#define DEC_HW_TIME                 30000
#define DEC_NODE_TIME               50000

enum pyr_dec_node_id {
	PYR_DEC_NODE_ID,
	PYR_DEC_OFFLINE_CAP_NODE_ID,
	PYR_DEC_MAX_NODE_ID,
};

enum {
	PYR_DEC_STORE_DCT,
	PYR_DEC_STORE_DEC,
	PYR_DEC_MAX,
};

typedef int(*pyrdec_irq_proc_func)(void *handle);

static inline uint32_t pyrdec_small_layer_w(uint32_t w, uint32_t layer_num)
{
	uint32_t width = 0, i = 0;
	uint32_t w_align = PYR_DEC_WIDTH_ALIGN;

	for (i = 0; i < layer_num; i++)
		w_align *= 2;

	width = ALIGN(w, w_align);
	for (i = 0; i < layer_num; i++)
		width = width / 2;

	return width;
}

static inline uint32_t pyrdec_small_layer_h(uint32_t h, uint32_t layer_num)
{
	uint32_t height = 0, i = 0;
	uint32_t h_align = PYR_DEC_HEIGHT_ALIGN;

	for (i = 0; i < layer_num; i++)
		h_align *= 2;

	height = ALIGN(h, h_align);
	for (i = 0; i < layer_num; i++)
		height = height / 2;

	return height;
}

static inline uint32_t pyrdec_layer0_width(uint32_t w, uint32_t layer_num)
{
	uint32_t width = 0, i = 0;
	uint32_t w_align = PYR_DEC_WIDTH_ALIGN;

	for (i = 0; i < layer_num; i++)
		w_align *= 2;

	width = ALIGN(w, w_align);
	return width;
}

static inline uint32_t pyrdec_layer0_heigh(uint32_t h, uint32_t layer_num)
{
	uint32_t height = 0, i = 0;
	uint32_t h_align = PYR_DEC_HEIGHT_ALIGN;

	for (i = 0; i < layer_num; i++)
		h_align *= 2;

	height = ALIGN(h, h_align);
	return height;
}

struct pyr_dec_overlap_info {
	uint32_t slice_num;
	struct slice_pos_info slice_fetch_region[SLICE_NUM_MAX];
	struct slice_pos_info slice_store_region[SLICE_NUM_MAX];
	struct slice_overlap_info slice_fetch_overlap[SLICE_NUM_MAX];
	struct slice_overlap_info slice_store_overlap[SLICE_NUM_MAX];
};

struct pyr_dec_fetch_info {
	enum cam_en_status bypass;
	enum cam_format color_format;
	uint32_t width;
	uint32_t height;
	uint32_t pitch[2];
	uint32_t addr[2];
	enum cam_en_status chk_sum_clr_en;
	enum cam_en_status ft1_axi_reorder_en;
	enum cam_en_status ft0_axi_reorder_en;
	uint32_t substract;
	uint32_t ft1_max_len_sel;
	uint32_t ft1_retain_num;
	uint32_t ft0_max_len_sel;
	uint32_t ft0_retain_num;
};

struct pyr_dec_offline_info {
	enum cam_en_status bypass;
	uint32_t fmcu_path_sel;
	uint32_t fetch_path_sel;
	uint32_t vector_channel_idx;
	uint32_t chksum_wrk_mode;
	uint32_t chksum_clr_mode;
	enum cam_en_status hor_padding_en;
	uint32_t hor_padding_num;
	enum cam_en_status ver_padding_en;
	uint32_t ver_padding_num;
	uint32_t dispatch_dbg_mode_ch0;
	uint32_t dispatch_done_cfg_mode;
	uint32_t dispatch_width_dly_num_flash;
	uint32_t dispatch_pipe_nfull_num;
	uint32_t dispatch_pipe_flush_num;
	uint32_t dispatch_pipe_hblank_num;
	uint32_t dispatch_yuv_start_order;
	uint32_t dispatch_yuv_start_row_num;
	uint32_t dispatch_width_flash_mode;
};

struct slice_pyr_dec_node_info {
	enum cam_en_status hor_padding_en;
	uint32_t hor_padding_num;
	enum cam_en_status ver_padding_en;
	uint32_t ver_padding_num;
	uint32_t dispatch_dly_width_num;
	uint32_t dispatch_dly_height_num;
};

struct pyr_dec_slice_desc {
	struct slice_pos_info slice_fetch_pos;
	struct slice_pos_info slice_store_dct_pos;
	struct slice_pos_info slice_store_dec_pos;
	struct slice_overlap_info slice_dct_overlap;
	struct slice_overlap_info slice_dec_overlap;
	struct slice_fetch_info slice_fetch;
	struct slice_store_info slice_dec_store;
	struct slice_store_info slice_dct_store;
	struct slice_pyr_dec_node_info slice_pyr_dec;
};

struct pyr_dec_store_info {
	enum cam_en_status bypass;
	uint32_t endian;
	enum cam_en_status mono_en;
	enum cam_format color_format;
	uint32_t burst_len;
	enum cam_en_status mirror_en;
	enum cam_en_status flip_en;
	uint32_t speed2x;
	uint32_t shadow_clr_sel;
	enum cam_en_status last_frm_en;
	uint32_t pitch[2];
	uint32_t addr[2];
	uint32_t data_10b;
	enum cam_en_status mipi_en;
	uint32_t width;
	uint32_t height;
	uint32_t border_up;
	uint32_t border_down;
	uint32_t border_left;
	uint32_t border_right;
	uint32_t rd_ctrl;
	uint32_t shadow_clr;
	uint32_t slice_offset;
};

struct pyr_dec_dct_ynr_info {
	uint32_t dct_radius;
	uint32_t old_width;
	uint32_t old_height;
	uint32_t new_width;
	uint32_t new_height;
	uint32_t sensor_width;
	uint32_t sensor_height;
	struct img_size img;
	struct img_size start[SLICE_NUM_MAX];
	struct isp_dev_dct_info *dct;
};

struct pyrdec_pipe_dev {
	uint32_t cur_node_id;
	uint32_t cur_node_idx;
	uint32_t irq_no;
	spinlock_t ctx_lock;
	atomic_t user_cnt;
	irq_handler_t isr_func;
	uint32_t in_irq_handler;
	timespec hw_start_ts;

	void *fmcu_handle;
	struct pyr_dec_node *node;
	struct cam_hw_info *hw;
	pyrdec_irq_proc_func irq_proc_func;
	struct pyr_dec_node *pyrdec_node[PYR_DEC_NODE_NUM_MAX][PYR_DEC_NODE_NUM_MAX];
};

struct pyr_dec_node {
	uint32_t node_id;
	/*used to connect dev&node*/
	uint32_t node_idx;
	uint32_t isp_node_cfg_id;
	atomic_t user_cnt;
	uint32_t dcam_slice_mode;
	enum cam_en_status is_yuvsensor;
	enum cam_en_status is_4in1;
	enum cam_en_status is_bind;
	enum cam_en_status is_fast_stop;
	struct completion *fast_stop_done;
	struct completion frm_done;
	struct isp_pipe_dev *dev;
	struct pyrdec_pipe_dev *pyrdec_dev;
	struct cam_hw_info *hw;
	struct cam_thread_info thread;
	/* lock block param to avoid acrossing frame */
	struct mutex blkpm_lock;
	timespec start_ts;
	timespec end_ts;
	struct mutex blkpm_q_lock;

	uint32_t layer_num;
	uint32_t slice_num;
	uint32_t cur_layer_id;
	uint32_t cur_slice_id;
	enum cam_format in_fmt;
	enum cam_format pyr_out_fmt;
	uint32_t fetch_path_sel;
	uint32_t blkparam_buf_num;

	struct dcam_isp_k_block decblk_param;
	struct img_addr fetch_addr[ISP_PYR_DEC_LAYER_NUM];
	struct img_addr store_addr[PYR_DEC_ADDR_NUM];
	struct img_size src;
	struct img_size dec_padding_size;
	struct img_size dec_layer_size[PYR_DEC_ADDR_NUM];
	struct img_size sn_size;

	struct pyr_dec_fetch_info fetch_dec_info;
	struct pyr_dec_offline_info offline_dec_info;
	struct pyr_dec_store_info store_dct_info;
	struct pyr_dec_store_info store_dec_info;
	struct pyr_dec_dct_ynr_info dct_ynr_info;

	struct pyr_dec_slice_desc slices[SLICE_NUM_MAX];
	struct pyr_dec_overlap_info overlap_dec_info[MAX_PYR_DEC_LAYER_NUM];
	struct isp_fbd_yuv_info yuv_afbd_info;

	struct camera_queue param_buf_queue;
	struct camera_queue param_share_queue;
	struct cam_buf_pool_id fetch_unprocess_pool;
	struct cam_buf_pool_id fetch_result_pool;

	void *data_cb_handle;
	void *buf_manager_handle;
	cam_data_cb data_cb_func;
	port_cfg_cb port_cfg_cb_func;
	void *port_cfg_cb_handle;
};

struct pyr_dec_node_desc {
	void **node_dev;
	struct isp_pipe_dev *dev;
	struct pyrdec_pipe_dev *pyrdec_dev;
	struct cam_hw_info *hw;

	/*used to connect dev&node*/
	uint32_t node_idx;
	enum cam_format in_fmt;
	enum cam_format pyr_out_fmt;
	uint32_t layer_num;
	uint32_t dcam_slice_mode;
	enum cam_en_status is_yuvsensor;
	enum cam_en_status is_4in1;
	uint32_t blkparam_buf_num;
	struct img_size sn_size;

	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	port_cfg_cb port_cfg_cb_func;
	void *port_cfg_cb_handle;

	struct pyr_dec_port_desc port_desc;
};

int pyr_dec_node_request_proc(struct pyr_dec_node *node, void *param);
int pyr_dec_node_blk_param_set(void *handle, void *param);
int pyr_dec_node_ctxid_cfg(void *handle, void *param);
int pyr_dec_node_base_cfg(void *handle, void *param);
void pyr_dec_node_buffer_clr(void *handle);
int pyr_dec_node_param_buf_cfg(void *handle, void *param);
int pyr_dec_node_postproc_param_cfg(void *handle, void *param);
int pyr_dec_node_fast_stop_cfg(void *handle, void *param);
int pyr_dec_node_clear_fast_stop_cfg(void *handle, void *param);
int pyr_dec_node_close(void *handle);
void *pyr_dec_node_get(uint32_t node_id, struct pyr_dec_node_desc *param);
void pyr_dec_node_put(struct pyr_dec_node *node);
void *pyr_dec_dev_get(void *isp_handle, void *hw);
void pyr_dec_dev_put(void *dec_handle);

#endif
