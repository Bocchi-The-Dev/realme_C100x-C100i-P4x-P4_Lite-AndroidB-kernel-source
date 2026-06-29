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

#ifndef _DCAM_DUMMY_H_
#define _DCAM_DUMMY_H_

#include "dcam_core.h"

#define DCAM_DUMMY_SLAVE_SKIP_NUM 2
#define DCAM_DUMMY_SLAVE_START_TIME_OUT 500

enum dcam_dummy_id {
	DCAM_DUMMY_0,
	DCAM_DUMMY_MAX,
};

enum dcam_dummy_hw_mode {
	DCAM_DUMMY_MODE_HW_AUTO,
	DCAM_DUMMY_MODE_SW_CFG,
	DCAM_DUMMY_MODE_MAX,
};

enum dcam_dummy_sw_mode {
	DCAM_DUMMY_SW_CLOSE,
	DCAM_DUMMY_SW_SENSELESS,
	DCAM_DUMMY_SW_NEW_FRAME,
	DCAM_DUMMY_SW_MAX,
};

enum dcam_dummy_status {
	DCAM_DUMMY_DISABLE,
	DCAM_DUMMY_IDLE,
	DCAM_DUMMY_TRIGGER,
	DCAM_DUMMY_DONE,
	DCAM_DUMMY_STATUS_MAX,
};

enum dcam_dummy_cfg_cmd {
	DCAM_DUMMY_CFG_HW_MODE,
	DCAM_DUMMY_CFG_SW_MODE,
	DCAM_DUMMY_CFG_SKIP_NUM,
	DCAM_DUMMY_CFG_FIFO_LV,
	DCAM_DUMMY_CFG_RESERVED_BUF,
	DCAM_DUMMY_CFG_MAX,
};

enum dcam_dummy_callback_cmd {
	DCAM_DUMMY_CALLBACK_DUMMY_START,
	DCAM_DUMMY_CALLBACK_DUMMY_DONE,
	DCAM_DUMMY_CALLBACK_CAP_SOF,
	DCAM_DUMMY_CALLBACK_PATH_DONE,
	DCAM_DUMMY_CALLBACK_MAX,
};

struct dcam_dummy_ops {
	int (*cfg_param)(void *handle, enum dcam_dummy_cfg_cmd cmd, void *param);
	int (*dummy_enable)(void *handle, void *param);
	int (*dummyint_callback)(void *handle, enum dcam_dummy_callback_cmd cmd, void *param);
};

enum dcam_dummy_hw_status {
	DCAM_DUMMY_HW_OFF = 0,
	DCAM_DUMMY_HW_IDLE,
	DCAM_DUMMY_HW_ONLINE_RUNNING,
	DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME,
	DCAM_DUMMY_HW_ONLINE_DUMMY_SKIP_FRAME,
	DCAM_DUMMY_HW_ONLINE_DUMMY_DONE_FIRST_FRAME,
	DCAM_DUMMY_HW_ALREADY,
	DCAM_DUMMY_HW_OFFLINE_RESET,
	DCAM_DUMMY_HW_STATUS_MAX,
};

enum dcam_dummy_node_cfg {
	DCAM_DUMMY_NODE_CFG_REG,
	DCAM_DUMMY_NODE_CFG_TIME_TAMP,
	DCAM_DUMMY_NODE_CFG_SHUTOFF_RESUME,
	DCAM_DUMMY_NODE_CFG_MAX,
};

enum dcam_dummy_use_reserved_buf {
	DCAM_DUMMY_RESERVED_BUF_NOUSE,
	DCAM_DUMMY_RESERVED_BUF_USE,
	DCAM_DUMMY_RESERVED_BUF_MAX,
};

struct dcam_dummy_slave {
	atomic_t user_cnt;
	uint32_t idx;
	struct dcam_hw_context *hw_ctx[DCAM_HW_CONTEXT_MAX];
	spinlock_t dummy_lock;/*reserved_buf multi-dcam using and release*/
	enum dcam_dummy_hw_mode hw_mode;
	enum dcam_dummy_sw_mode sw_mode;
	atomic_t status;
	uint32_t skip_num;
	uint32_t dfifo_lvl;
	uint32_t cfifo_lvl;
	uint32_t skip_frame_num;
	struct cam_hw_info *hw;
	struct dcam_dummy_ops *dummy_ops;
	uint64_t int_status[DCAM_HW_CONTEXT_MAX];
	atomic_t hw_status[DCAM_HW_CONTEXT_MAX];
	uint32_t dummy_total_skip_num[DCAM_HW_CONTEXT_MAX];
	reserved_buf_get_cb resbuf_get_cb;
	void *resbuf_cb_data;
	struct cam_frame *reserved_buf;
	enum dcam_dummy_use_reserved_buf use_reserved_buf;
};

struct dcam_dummy_slave *dcam_dummy_ctx_desc_get(void *arg , uint32_t idx);
int dcam_dummy_ctx_desc_put(struct dcam_dummy_slave *dummy_slave);
#endif
