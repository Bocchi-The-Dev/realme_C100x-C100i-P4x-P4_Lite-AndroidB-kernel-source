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

#ifndef _PYR_DEC_PORT_H_
#define _PYR_DEC_PORT_H_

#include "cam_types.h"
#include "cam_port.h"

#define PYR_DEC_OUT_BUF_Q_LEN                1

struct pyr_dec_port_desc {
	void **port_dev;
	uint32_t share_buffer;
	cam_data_cb data_cb_func;
	void *data_cb_handle;
	void *buf_manager_handle;
};

struct pyr_dec_port {
	struct list_head list;
	uint32_t share_buffer;
	enum cam_port_dec_out_id port_id;
	atomic_t user_cnt;
	atomic_t is_work; /* dynamic switch counter of port */

	void *data_cb_handle;
	void *buf_manager_handle;
	cam_data_cb data_cb_func;

	struct cam_buf_pool_id unprocess_pool;
	struct cam_buf_pool_id result_pool;
};

int pyr_dec_port_param_cfg(void *handle, enum cam_port_cfg_cmd cmd, void *param);
int pyr_dec_port_buf_alloc(void *handle, struct cam_buf_alloc_desc *param);
void *pyr_dec_port_get(uint32_t port_id, struct pyr_dec_port_desc *param);
void pyr_dec_port_put(struct pyr_dec_port *port);

#endif
