/* SPDX-License-Identifier: GPL-2.0-only
 *
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

#ifndef _ISP_DEV_H_
#define _ISP_DEV_H_

#include <linux/version.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
#include <linux/sprd_ion.h>
#endif
#include "sprd_cam.h"
#include "isp_hwctx.h"
#include "cam_hw.h"
#include "isp_interface.h"
#include "cam_types.h"
#include "cam_queue.h"

#define AFBC_PADDING_W_YUV420      32
#define AFBC_PADDING_H_YUV420      8
#define AFBC_HEADER_SIZE           16
#define AFBC_PAYLOAD_SIZE          84
#define ISP_FBD_BASE_ALIGN         256

#define ISP_LINE_BUFFER_W          ISP_MAX_LINE_WIDTH

/*
 * Before N6pro all use ISP_THUMB_SCL_VER_0
 * N6pro use ISP_THUMB_SCL_VER_1.
 */
enum isp_thumb_scaler_version {
	ISP_THUMB_SCL_VER_0,
	ISP_THUMB_SCL_VER_1,
	ISP_THUMB_SCL__MAX,
};

struct isp_pipe_ops {
	int (*open)(void *isp_handle, void *arg);
	int (*close)(void *isp_handle);
	int (*reset)(void *isp_handle, void *arg);
	int (*ioctl)(void *isp_handle, enum isp_ioctrl_cmd cmd, void *param);
	void *(*bind)(void *node, int slice_need, isp_irq_postproc postproc_func);
	uint32_t (*unbind)(void *node);
};

struct isp_pipe_dev {
	uint32_t slice_num;
	uint32_t irq_no[2];
	atomic_t user_cnt;
	atomic_t pd_clk_rdy;
	atomic_t enable;
	atomic_t pyr_mulshare_dec_alloced;
	atomic_t pyr_mulshare_rec_alloced;
	struct mutex pyr_mulshare_dec_lock;
	struct mutex pyr_mulshare_rec_lock;
	struct mutex path_mutex;
	struct mutex dev_lock;
	struct mutex dev_mutex;
	struct completion frm_done;
	spinlock_t ctx_lock;
	enum isp_work_mode wmode;
	enum sprd_cam_sec_mode sec_mode;
	void *cfg_handle;
	void *pyr_dec_handle;

	struct isp_hw_context hw_ctx[ISP_CONTEXT_HW_NUM];
	struct cam_hw_info *isp_hw;
	struct isp_pipe_ops *isp_ops;
};

struct isp_init_param {
	uint32_t is_high_fps;
	uint32_t cam_id;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	reserved_buf_get_cb resbuf_get_cb;
	void *resbuf_cb_data;
	struct isp_pipe_dev *dev;
	uint32_t blkparam_node_num;
};

void *isp_core_pipe_dev_get(struct cam_hw_info *hw, void *s_isp_dev);
int isp_core_pipe_dev_put(void *isp_handle, void *s_isp_dev);

#endif
