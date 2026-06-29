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

#ifndef _CAM_THREAD_H_
#define _CAM_THREAD_H_

#include <linux/kthread.h>
#include <linux/module.h>

#define CAMTHREAD_STOP_TIMEOUT             msecs_to_jiffies(3500)

enum cam_cb_type {
	CAM_CB_DCAM_DATA_DONE,
	CAM_CB_DCAM_IRQ_EVENT,
	CAM_CB_DCAM_STATIS_DONE,
	CAM_CB_DCAM_RET_SRC_BUF,
	CAM_CB_DCAM_CLEAR_BUF,
	CAM_CB_DCAM_DEV_ERR,
	CAM_CB_ISP_RET_SRC_BUF,
	CAM_CB_ISP_RET_DST_BUF,
	CAM_CB_ISP_RET_PYR_DEC_BUF,
	CAM_CB_ISP_DEV_ERR,
	CAM_CB_ISP_RESET_ERR,
	CAM_CB_ISP_STATIS_DONE,
	CAM_CB_ISP_SCALE_RET_ISP_BUF,
	CAM_CB_FRAME_CACHE_DATA_DONE,
	CAM_CB_FRAME_CACHE_RET_SRC_BUF,
	CAM_CB_FRAME_CACHE_CLEAR_BUF,
	CAM_CB_DUMP_DATA_DONE,
	CAM_CB_DUMP_STATIS_DONE,
	CAM_CB_PYRDEC_RET_SRC_BUF,
	CAM_CB_COPY_SRC_BUFFER,
	CAM_CB_ICAP_COPY_SRC_BUFFER,
	CAM_CB_YUV_SCALER_DATA_DONE,
	CAM_CB_REPLACE_DATA_DONE,
	CAM_CB_USER_BUF_DONE,
	CAM_CB_COPY_DST_BUFFER,
	CAM_CB_TYPE_MAX
};

typedef int(*cam_data_cb)(enum cam_cb_type type, void *param, void *cb_handle);
typedef int(*shutoff_cb)(void *param, void *cb_handle);

struct cam_thread_info {
	atomic_t thread_stop;
	void *ctx_handle;
	int (*proc_func)(void *param);
	uint8_t thread_name[32];
	struct completion thread_com;
	struct completion thread_stop_com;
	struct task_struct *thread_task;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
	uint32_t error_type;
};

typedef int (*proc_func)(void *param);

int camthread_create(void *param, struct cam_thread_info *thrd, proc_func func);
int camthread_stop(struct cam_thread_info *thrd);


#endif/* _CAM_THREAD_H_ */
