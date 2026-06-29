// SPDX-License-Identifier: GPL-2.0-only
/*
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

#include <linux/version.h>
#include <linux/types.h>
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
#include <soc/unisoc/sprd_iommu.h>
#else
#include <linux/sprd_iommu.h>
#include <linux/sprd_ion.h>
#endif
#include <os_adapt_common.h>
#include <sprd_mm.h>

#include "cam_types.h"
#include "dcam_reg.h"
#include "dcam_fmcu.h"
#include "cam_hw.h"
#include "dcam_dummy.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_DUMMY: %d %d %s : " fmt, current->pid, __LINE__, __func__

static struct dcam_dummy_slave *s_pdummy_desc[DCAM_DUMMY_MAX] = {0};

static void dcamdummy_trigger_init(struct dcam_dummy_slave *dummy_slave)
{
	struct dcam_hw_context *hw_ctx = NULL;
	uint32_t i = 0;
	int val = 0;

	atomic_set(&dummy_slave->status, DCAM_DUMMY_TRIGGER);
	for (i = 0; i < DCAM_HW_CONTEXT_BIND_MAX; i++) {
		hw_ctx = dummy_slave->hw_ctx[i];
		if (hw_ctx->dcam_irq_cb_func) {
			if (hw_ctx->is_offline_proc) {
				val = atomic_cmpxchg(&dummy_slave->hw_status[i],
					DCAM_DUMMY_HW_IDLE, DCAM_DUMMY_HW_OFFLINE_RESET);
				if (val != DCAM_DUMMY_HW_IDLE)
					pr_warn("warning: hw_status is %d, not updated\n",
						atomic_read(&dummy_slave->hw_status[i]));
				hw_ctx->hw->dcam_ioctl(hw_ctx->hw, hw_ctx->hw_ctx_id,
					DCAM_HW_CFG_RESET, &hw_ctx->hw_ctx_id);
			} else {
				val = atomic_cmpxchg(&dummy_slave->hw_status[i],
					DCAM_DUMMY_HW_IDLE, DCAM_DUMMY_HW_ONLINE_RUNNING);
				if (val != DCAM_DUMMY_HW_IDLE)
					pr_warn("warning: hw_status is %d, not updated\n",
						atomic_read(&dummy_slave->hw_status[i]));
			}
		}
		pr_debug("hw ctx:%d, is_offline:%d, status:%d\n",
			hw_ctx->hw_ctx_id, hw_ctx->is_offline_proc,
			atomic_read(&dummy_slave->hw_status[i]));
	}
	dummy_slave->skip_frame_num = dummy_slave->skip_num;
}

static int dcamdummy_senselessmode_trigger(struct dcam_dummy_slave *dummy_slave, void *param)
{
	struct dcam_hw_context *hw_ctx = NULL;
	struct cam_frame *frame = NULL;
	uint32_t i = 0;
	unsigned long flags = 0;

	pr_info("dummy trigger status:%d skip_num: %d\n",
		atomic_read(&dummy_slave->status), dummy_slave->skip_num);
	switch (atomic_read(&dummy_slave->status)) {
	case DCAM_DUMMY_IDLE:
		dcamdummy_trigger_init(dummy_slave);
		dummy_slave->int_status[DCAM_HW_CONTEXT_0] = 0;
		dummy_slave->int_status[DCAM_HW_CONTEXT_1] = 0;
		spin_lock_irqsave(&dummy_slave->dummy_lock, flags);
		if (!dummy_slave->reserved_buf) {
			if (dummy_slave->resbuf_get_cb)
				dummy_slave->resbuf_get_cb((void *)&frame,
				dummy_slave->resbuf_cb_data);
			if (frame) {
				dummy_slave->reserved_buf = frame;
				dummy_slave->use_reserved_buf = DCAM_DUMMY_RESERVED_BUF_USE;
			}
		}
		spin_unlock_irqrestore(&dummy_slave->dummy_lock, flags);
		break;
	case DCAM_DUMMY_DONE:
		dcamdummy_trigger_init(dummy_slave);
		dummy_slave->use_reserved_buf = DCAM_DUMMY_RESERVED_BUF_NOUSE;
		for (i = 0; i < DCAM_HW_CONTEXT_BIND_MAX; i++) {
			hw_ctx = dummy_slave->hw_ctx[i];
			if (hw_ctx->dcam_irq_cb_func && !hw_ctx->is_offline_proc) {
				for (i = 0; i < PORT_DCAM_OUT_MAX; ++i) {
					if (dummy_slave->int_status[hw_ctx->hw_ctx_id] & BIT(i)) {
						struct dcam_irq_proc irq_proc = {0};

						irq_proc.of = CAP_DATA_DONE;
						irq_proc.dcam_port_id = i;
						irq_proc.dummy_enable = 1;
						hw_ctx->dcam_irq_cb_func(&irq_proc,
							hw_ctx->dcam_irq_cb_handle);
					}
				}
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

static void dcamdummy_node_start(struct dcam_dummy_slave *dummy_slave)
{
	uint32_t i = 0;
	int val = 0;

	for (i = 0; i < DCAM_HW_CONTEXT_BIND_MAX; i++) {
		val = atomic_cmpxchg(&dummy_slave->hw_status[i],
			DCAM_DUMMY_HW_ALREADY,
			DCAM_DUMMY_HW_ONLINE_DUMMY_DONE_FIRST_FRAME);
		if (val != DCAM_DUMMY_HW_ALREADY)
			pr_warn("warning: hw_status is %d, not updated\n",
				atomic_read(&dummy_slave->hw_status[i]));
		if (atomic_read(&dummy_slave->hw_status[i]) == DCAM_DUMMY_HW_OFFLINE_RESET) {
			dcam_core_offline_reset(dummy_slave->hw_ctx[i]);
			atomic_set(&dummy_slave->hw_status[i], DCAM_DUMMY_HW_IDLE);
		}
	}
}

static int dcamdummy_senselessmode_intdone(struct dcam_dummy_slave *dummy_slave, void *param)
{
	struct dcam_hw_context *hw_ctx = NULL;

	hw_ctx = VOID_PTR_TO(param, struct dcam_hw_context);
	pr_debug("dummy done hw_ctx:%d, dummy status:%d\n",
		hw_ctx->hw_ctx_id, atomic_read(&dummy_slave->status));
	atomic_set(&dummy_slave->status, DCAM_DUMMY_DONE);
	dcamdummy_node_start(dummy_slave);
	return 0;
}

static int dcamdummy_dec_addr_config(
	struct dcam_dummy_slave *dummy_slave, struct dcam_hw_context *hw_ctx)
{
	uint32_t layer = 0;
	uint32_t type = CAM_BUF_IOMMUDEV_DCAM;
	struct dcam_hw_dec_store_cfg dec_store = {0};

	hw_ctx->dec_all_done = 0;
	hw_ctx->dec_layer0_done = 0;
	dec_store.idx = hw_ctx->hw_ctx_id;
	for (layer = 0; layer < DCAM_PYR_DEC_LAYER_NUM; layer++) {
		dec_store.cur_layer = layer;
		dec_store.addr[0] =
			dummy_slave->reserved_buf->common.buf.iova[type];
		dec_store.addr[1] =
			dummy_slave->reserved_buf->common.buf.iova[type];
		dummy_slave->hw->dcam_ioctl(dummy_slave->hw,
			hw_ctx->hw_ctx_id,
			DCAM_HW_CFG_DEC_STORE_ADDR,
			&dec_store);
	}
	return 0;
}

static int dcamdummy_senselessmode_addr_config(
	struct dcam_dummy_slave *dummy_slave, struct dcam_hw_context *hw_ctx)
{
	struct dcam_hw_cfg_store_addr store_arg = {0};
	uint32_t i = 0;
	struct dcam_irq_proc irq_proc = {0};
	unsigned long flags = 0;
	struct cam_frame *resbuf = dummy_slave->reserved_buf;

	if (dummy_slave->int_status[hw_ctx->hw_ctx_id]) {
		spin_lock_irqsave(&dummy_slave->dummy_lock, flags);
		if (dummy_slave->reserved_buf) {
			for (i = 0; i < PORT_DCAM_OUT_MAX; ++i) {
				if (i == PORT_GTM_HIST_OUT)
					continue;
				if (!(dummy_slave->int_status[hw_ctx->hw_ctx_id] & BIT(i))) {
					store_arg.idx = hw_ctx->hw_ctx_id;
					store_arg.blk_param = hw_ctx->blk_pm;
					store_arg.path_id = i;
					store_arg.frame_addr[0] =
						resbuf->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM];
					store_arg.frame_addr[1] = 0;
					dummy_slave->hw->dcam_ioctl(dummy_slave->hw,
						hw_ctx->hw_ctx_id,
						DCAM_HW_CFG_STORE_ADDR, &store_arg);
					if (i == DCAM_PATH_BIN)
						dcamdummy_dec_addr_config(dummy_slave, hw_ctx);
				}
			}
		}
		spin_unlock_irqrestore(&dummy_slave->dummy_lock, flags);
		irq_proc.of = DUMMY_RECONFIG;
		irq_proc.dummy_cmd = DCAM_DUMMY_NODE_CFG_REG;
		irq_proc.dummy_int_status = dummy_slave->int_status[hw_ctx->hw_ctx_id];
		hw_ctx->dcam_irq_cb_func(&irq_proc, hw_ctx->dcam_irq_cb_handle);
	}
	hw_ctx->dec_all_done = 0;
	hw_ctx->dec_layer0_done = 0;
	return 0;
}

static inline int dcamdummy_frame_skipproc(struct dcam_dummy_slave *dummy_slave, void *param)
{
	int val = 0;
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	irq_proc = VOID_PTR_TO(param, struct dcam_irq_proc);
	hw_ctx = VOID_PTR_TO(irq_proc->hw_ctx, struct dcam_hw_context);
	if (dummy_slave->skip_frame_num) {
		val = atomic_cmpxchg(&dummy_slave->hw_status[hw_ctx->hw_ctx_id],
			DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME,
			DCAM_DUMMY_HW_ONLINE_DUMMY_SKIP_FRAME);
		if (val != DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME)
			pr_warn("warning: hw_status is %d, not updated\n",
				atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]));
		val = atomic_cmpxchg(&dummy_slave->hw_status[hw_ctx->hw_ctx_id],
			DCAM_DUMMY_HW_ONLINE_RUNNING,
			DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME);
		if (val != DCAM_DUMMY_HW_ONLINE_RUNNING)
			pr_warn("warning: hw_status is %d, not updated\n",
				atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]));
		if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) ==
			DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME) {
			struct dcam_irq_proc proc = {0};

			proc.of = DUMMY_RECONFIG;
			proc.dummy_cmd = DCAM_DUMMY_NODE_CFG_SHUTOFF_RESUME;
			proc.dummy_int_status = dummy_slave->int_status[hw_ctx->hw_ctx_id];
			hw_ctx->dcam_irq_cb_func(&proc, hw_ctx->dcam_irq_cb_handle);
		}
		if (dummy_slave->use_reserved_buf &&
			atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) ==
			DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME)
			dcamdummy_senselessmode_addr_config(dummy_slave, hw_ctx);
		else {
			val = atomic_cmpxchg(&dummy_slave->hw_status[hw_ctx->hw_ctx_id],
				DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME,
				DCAM_DUMMY_HW_ONLINE_DUMMY_SKIP_FRAME);
			if (val != DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME)
				pr_warn("warning: hw_status is %d, not updated\n",
					atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]));
		}
		irq_proc->of = DUMMY_RECONFIG;
		irq_proc->dummy_cmd = DCAM_DUMMY_NODE_CFG_TIME_TAMP;
		hw_ctx->dcam_irq_cb_func(irq_proc, hw_ctx->dcam_irq_cb_handle);
		dummy_slave->dummy_total_skip_num[hw_ctx->hw_ctx_id]++;
		dummy_slave->skip_frame_num--;
	} else {
		dummy_slave->dummy_ops->dummyint_callback(dummy_slave,
			DCAM_DUMMY_CALLBACK_DUMMY_DONE, hw_ctx);
		dummy_slave->dummy_ops->dummyint_callback(dummy_slave,
			DCAM_DUMMY_CALLBACK_CAP_SOF, param);
	}
	return 0;
}

static void dcamdummy_dummy_finish(struct dcam_dummy_slave *dummy_slave,
	struct dcam_hw_context *hw_ctx)
{
	uint32_t i = 0, dummy_enable = 0;
	unsigned long flags = 0;

	spin_lock_irqsave(&dummy_slave->dummy_lock, flags);
	atomic_set(&dummy_slave->hw_status[hw_ctx->hw_ctx_id], DCAM_DUMMY_HW_IDLE);
	for (i = 0; i < DCAM_HW_CONTEXT_BIND_MAX; i++) {
		if (atomic_read(&dummy_slave->hw_status[i]) != DCAM_DUMMY_HW_IDLE &&
			atomic_read(&dummy_slave->hw_status[i]) != DCAM_DUMMY_HW_OFF) {
			dummy_enable = 1;
			break;
		}
	}
	if (!dummy_enable) {
		if (dummy_slave->reserved_buf) {
			cam_queue_empty_frame_put(dummy_slave->reserved_buf);
			dummy_slave->reserved_buf = NULL;
		}
		atomic_set(&dummy_slave->status, DCAM_DUMMY_IDLE);
		pr_info("dummy finish\n");
	}
	spin_unlock_irqrestore(&dummy_slave->dummy_lock, flags);
}

static int dcamdummy_senselessmode_cap_sof(struct dcam_dummy_slave *dummy_slave, void *param)
{
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	irq_proc = VOID_PTR_TO(param, struct dcam_irq_proc);
	hw_ctx = VOID_PTR_TO(irq_proc->hw_ctx, struct dcam_hw_context);
	pr_info("hw_ctx:%d, dummy status:%d hw_status:%d int record:%llx\n",
		hw_ctx->hw_ctx_id, atomic_read(&dummy_slave->status),
		atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]),
		dummy_slave->int_status[hw_ctx->hw_ctx_id]);
	switch (atomic_read(&dummy_slave->status)) {
	case DCAM_DUMMY_TRIGGER:
		dcamdummy_frame_skipproc(dummy_slave, irq_proc);
		break;
	case DCAM_DUMMY_DONE:
		if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) ==
			DCAM_DUMMY_HW_ONLINE_DUMMY_DONE_FIRST_FRAME
			|| !dummy_slave->int_status[hw_ctx->hw_ctx_id])
			dcamdummy_dummy_finish(dummy_slave, hw_ctx);
		else if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) !=
			DCAM_DUMMY_HW_IDLE) {
			atomic_set(&dummy_slave->hw_status[hw_ctx->hw_ctx_id],
				DCAM_DUMMY_HW_ONLINE_DUMMY_DONE_FIRST_FRAME);
			irq_proc->not_dispatch = 1;
		}
		hw_ctx->dcam_irq_cb_func(irq_proc, hw_ctx->dcam_irq_cb_handle);
		break;
	default:
		pr_err("fail to get dummy status:%d\n", atomic_read(&dummy_slave->status));
		break;
	}
	return 0;
}

static int dcamdummy_senselessmode_path_done(struct dcam_dummy_slave *dummy_slave, void *param)
{
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	irq_proc = VOID_PTR_TO(param, struct dcam_irq_proc);
	hw_ctx = VOID_PTR_TO(irq_proc->hw_ctx, struct dcam_hw_context);
	pr_debug("hw_ctx:%d, dummy status:%d int record:%d port_id:%d\n",
		hw_ctx->hw_ctx_id, atomic_read(&dummy_slave->status),
		atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]), irq_proc->dcam_port_id);
	switch (atomic_read(&dummy_slave->status)) {
	case DCAM_DUMMY_TRIGGER:
		if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) ==
			DCAM_DUMMY_HW_ONLINE_RUNNING && dummy_slave->use_reserved_buf)
			dummy_slave->int_status[hw_ctx->hw_ctx_id] |= BIT(irq_proc->dcam_port_id);
		if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) ==
			DCAM_DUMMY_HW_ONLINE_DUMMY_FIRST_FRAME &&
			dummy_slave->int_status[hw_ctx->hw_ctx_id]) {
			irq_proc->dummy_enable = 1;
			hw_ctx->dcam_irq_cb_func(irq_proc, hw_ctx->dcam_irq_cb_handle);
		}
		break;
	case DCAM_DUMMY_DONE:
		if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) ==
			DCAM_DUMMY_HW_ONLINE_DUMMY_DONE_FIRST_FRAME) {
			if (dummy_slave->int_status[hw_ctx->hw_ctx_id] &
				BIT(irq_proc->dcam_port_id)) {
				if (irq_proc->dcam_port_id != PORT_BIN_OUT ||
					(hw_ctx->dec_all_done && hw_ctx->dec_layer0_done))
					dummy_slave->int_status[hw_ctx->hw_ctx_id] &=
					(~BIT(irq_proc->dcam_port_id));
				hw_ctx->dcam_irq_cb_func(irq_proc, hw_ctx->dcam_irq_cb_handle);
			}
		}
		if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) == DCAM_DUMMY_HW_IDLE)
			hw_ctx->dcam_irq_cb_func(irq_proc, hw_ctx->dcam_irq_cb_handle);
		break;
	default:
		pr_err("fail to get dummy status:%d\n", atomic_read(&dummy_slave->status));
		break;
	}
	return 0;
}

static int dcamdummy_senselessmode_callback(void *handle,
	enum dcam_dummy_callback_cmd cmd, void *param)
{
	struct dcam_dummy_slave *dummy_slave = NULL;

	dummy_slave = VOID_PTR_TO(handle, struct dcam_dummy_slave);
	switch (cmd) {
	case DCAM_DUMMY_CALLBACK_DUMMY_START:
		dcamdummy_senselessmode_trigger(dummy_slave, param);
		break;
	case DCAM_DUMMY_CALLBACK_DUMMY_DONE:
		dcamdummy_senselessmode_intdone(dummy_slave, param);
		break;
	case DCAM_DUMMY_CALLBACK_CAP_SOF:
		dcamdummy_senselessmode_cap_sof(dummy_slave, param);
		break;
	case DCAM_DUMMY_CALLBACK_PATH_DONE:
		dcamdummy_senselessmode_path_done(dummy_slave, param);
		break;
	default:
		pr_err("fail to get cmd:%d\n", cmd);
		break;
	}
	return 0;
}

static int dcamdummy_newframemode_trigger(struct dcam_dummy_slave *dummy_slave, void *param)
{
	pr_info("dummy status:%d %d\n", atomic_read(&dummy_slave->status), dummy_slave->skip_num);

	switch (atomic_read(&dummy_slave->status)) {
	case DCAM_DUMMY_IDLE:
	case DCAM_DUMMY_DONE:
		dcamdummy_trigger_init(dummy_slave);
		dummy_slave->int_status[DCAM_HW_CONTEXT_0] = 0;
		dummy_slave->int_status[DCAM_HW_CONTEXT_1] = 0;
		break;
	default:/*DCAM_DUMMY_TRIGGER multi dcam trigger same dummy, second dcam do noing*/
		break;
	}
	return 0;
}

static int dcamdummy_newframemode_intdone(struct dcam_dummy_slave *dummy_slave, void *param)
{
	int val = 0;
	uint32_t i = 0;
	struct dcam_hw_context *hw_ctx = NULL;

	hw_ctx = VOID_PTR_TO(param, struct dcam_hw_context);
	pr_debug("hw_ctx:%d, dummy status:%d\n",
		hw_ctx->hw_ctx_id, atomic_read(&dummy_slave->status));
	atomic_set(&dummy_slave->status, DCAM_DUMMY_DONE);
	for (i = 0; i < DCAM_HW_CONTEXT_BIND_MAX; i++) {
		val = atomic_cmpxchg(&dummy_slave->hw_status[i],
			DCAM_DUMMY_HW_ALREADY, DCAM_DUMMY_HW_IDLE);
		if (val != DCAM_DUMMY_HW_ALREADY)
			pr_warn("warning: hw_status is %d, not updated\n",
				atomic_read(&dummy_slave->hw_status[i]));
		if (atomic_read(&dummy_slave->hw_status[i]) ==
			DCAM_DUMMY_HW_OFFLINE_RESET &&
			dummy_slave->hw_ctx[i]->dcam_irq_cb_func) {
			dcam_core_offline_reset(dummy_slave->hw_ctx[i]);
			atomic_set(&dummy_slave->hw_status[i], DCAM_DUMMY_HW_IDLE);
		}
	}
	return 0;
}

static int dcamdummy_newframemode_cap_sof(struct dcam_dummy_slave *dummy_slave, void *param)
{
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	irq_proc = VOID_PTR_TO(param, struct dcam_irq_proc);
	hw_ctx = VOID_PTR_TO(irq_proc->hw_ctx, struct dcam_hw_context);
	pr_info("hw_ctx:%d, dummy status:%d hw_status:%d\n",
		hw_ctx->hw_ctx_id, atomic_read(&dummy_slave->status),
		atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]));

	switch (atomic_read(&dummy_slave->status)) {
	case DCAM_DUMMY_TRIGGER:
		dcamdummy_frame_skipproc(dummy_slave, irq_proc);
		break;
	case DCAM_DUMMY_DONE:
		dcamdummy_dummy_finish(dummy_slave, hw_ctx);
		hw_ctx->dcam_irq_cb_func(irq_proc, hw_ctx->dcam_irq_cb_handle);
		break;
	default:
		break;
	}
	return 0;
}

static int dcamdummy_newframemode_path_done(struct dcam_dummy_slave *dummy_slave, void *param)
{
	struct dcam_irq_proc *irq_proc = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	irq_proc = VOID_PTR_TO(param, struct dcam_irq_proc);
	hw_ctx = VOID_PTR_TO(irq_proc->hw_ctx, struct dcam_hw_context);
	pr_debug("hw_ctx:%d, dummy status:%d port_id:%d\n", hw_ctx->hw_ctx_id,
		atomic_read(&dummy_slave->status), irq_proc->dcam_port_id);
	switch (atomic_read(&dummy_slave->status)) {
	case DCAM_DUMMY_TRIGGER:
		if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) ==
			DCAM_DUMMY_HW_ONLINE_RUNNING) {
			dummy_slave->int_status[hw_ctx->hw_ctx_id] |= BIT(irq_proc->dcam_port_id);
			irq_proc->dummy_enable = 1;
			hw_ctx->dcam_irq_cb_func(irq_proc, hw_ctx->dcam_irq_cb_handle);
		}
		break;
	case DCAM_DUMMY_DONE:
		if (atomic_read(&dummy_slave->hw_status[hw_ctx->hw_ctx_id]) == DCAM_DUMMY_HW_IDLE)
			hw_ctx->dcam_irq_cb_func(irq_proc, hw_ctx->dcam_irq_cb_handle);
		break;
	default:
		break;
	}
	return 0;
}

static int dcamdummy_newframemode_callback(void *handle,
	enum dcam_dummy_callback_cmd cmd, void *param)
{
	struct dcam_dummy_slave *dummy_slave = NULL;

	dummy_slave = VOID_PTR_TO(handle, struct dcam_dummy_slave);
	switch (cmd) {
	case DCAM_DUMMY_CALLBACK_DUMMY_START:
		dcamdummy_newframemode_trigger(dummy_slave, param);
		break;
	case DCAM_DUMMY_CALLBACK_DUMMY_DONE:
		dcamdummy_newframemode_intdone(dummy_slave, param);
		break;
	case DCAM_DUMMY_CALLBACK_CAP_SOF:
		dcamdummy_newframemode_cap_sof(dummy_slave, param);
		break;
	case DCAM_DUMMY_CALLBACK_PATH_DONE:
		dcamdummy_newframemode_path_done(dummy_slave, param);
		break;
	default:
		pr_err("fail to get cmd:%d\n", cmd);
		break;
	}
	return 0;
}

typedef int (*dcamdummy_callback_func)(void *handle, enum dcam_dummy_callback_cmd cmd, void *param);
static const dcamdummy_callback_func _DCAM_DUMMY_CALLBACK[] = {
	[DCAM_DUMMY_SW_SENSELESS] = dcamdummy_senselessmode_callback,
	[DCAM_DUMMY_SW_NEW_FRAME] = dcamdummy_newframemode_callback,
};

static int dcamdummy_param_cfg(void *handle, enum dcam_dummy_cfg_cmd cmd, void *param)
{
	struct dcam_dummy_slave *dummy_slave = NULL;
	struct dcam_dummy_param *dummy_param = NULL;

	dummy_slave = VOID_PTR_TO(handle, struct dcam_dummy_slave);
	switch (cmd) {
	case DCAM_DUMMY_CFG_HW_MODE:
		dummy_slave->hw_mode = *(uint32_t *)param;
		break;
	case DCAM_DUMMY_CFG_SW_MODE:
		dummy_slave->sw_mode = *(uint32_t *)param;
		break;
	case DCAM_DUMMY_CFG_SKIP_NUM:
		dummy_slave->skip_num = *(uint32_t *)param;
		break;
	case DCAM_DUMMY_CFG_FIFO_LV:
		break;
	case DCAM_DUMMY_CFG_RESERVED_BUF:
		dummy_param = VOID_PTR_TO(param, struct dcam_dummy_param);
		dummy_slave->resbuf_cb_data = dummy_param->resbuf_cb_data;
		dummy_slave->resbuf_get_cb = dummy_param->resbuf_get_cb;
		break;
	default:
		pr_err("fail to get support cmd:%d\n", cmd);
		break;
	}

	return 0;
}

static int dcamdummy_enable(void *handle, void *arg)
{
	struct dcam_dummy_slave *dummy_slave = NULL;
	uint32_t enable = 0, i = 0, loop = 0;
	struct dcam_dummy_param *dummy_param = NULL;
	struct dcam_hw_dummy_param enable_param = {0};
	struct dcam_hw_dummy_param param = {0};
	struct cam_hw_info *hw;
	unsigned long flags = 0;

	dummy_slave = VOID_PTR_TO(handle, struct dcam_dummy_slave);
	dummy_param = VOID_PTR_TO(arg, struct dcam_dummy_param);
	hw = dummy_slave->hw;
	enable_param.idx = dummy_slave->idx;
	if (dummy_param->enable) {
		switch (atomic_read(&dummy_slave->status)) {
		case DCAM_DUMMY_DISABLE:
			if (dummy_slave->hw_mode == DCAM_DUMMY_MODE_HW_AUTO) {
				param.clr_mode = DCAM_DUMMY_MODE_HW_AUTO;
				param.skip_num = dummy_slave->skip_num;
				dummy_slave->dummy_ops->dummyint_callback =
					_DCAM_DUMMY_CALLBACK[dummy_slave->sw_mode];
			}
			dummy_slave->use_reserved_buf = DCAM_DUMMY_RESERVED_BUF_NOUSE;
			param.dfifo_lvl = dummy_slave->dfifo_lvl;
			param.cfifo_lvl = dummy_slave->cfifo_lvl;
			hw->dcam_ioctl(hw, dummy_param->hw_ctx_id, DCAM_HW_CFG_DUMMY_SET, &param);
			atomic_set(&dummy_slave->status, DCAM_DUMMY_IDLE);
			atomic_set(&dummy_slave->hw_status[dummy_param->hw_ctx_id],
				DCAM_DUMMY_HW_IDLE);
			dummy_slave->dummy_total_skip_num[dummy_param->hw_ctx_id] = 0;
			enable_param.enable = 1;
			hw->dcam_ioctl(hw, dummy_param->hw_ctx_id,
				DCAM_HW_CFG_DUMMY_ENABLE, &enable_param);
			break;
		case DCAM_DUMMY_TRIGGER:
			atomic_set(&dummy_slave->hw_status[dummy_param->hw_ctx_id],
				DCAM_DUMMY_HW_ALREADY);
			while (atomic_read(&dummy_slave->hw_status[dummy_param->hw_ctx_id]) ==
				DCAM_DUMMY_HW_ALREADY && loop <
				DCAM_DUMMY_SLAVE_START_TIME_OUT) {
				loop++;
				os_adapt_time_msleep(1);
			};
			break;
		default:
			atomic_set(&dummy_slave->hw_status[dummy_param->hw_ctx_id],
				DCAM_DUMMY_HW_IDLE);
			dummy_slave->dummy_total_skip_num[dummy_param->hw_ctx_id] = 0;
			break;
		}
	} else {
		spin_lock_irqsave(&dummy_slave->dummy_lock, flags);
		atomic_set(&dummy_slave->hw_status[dummy_param->hw_ctx_id], DCAM_DUMMY_HW_OFF);
		dummy_slave->dummy_total_skip_num[dummy_param->hw_ctx_id] = 0;
		for (i = 0; i < DCAM_HW_CONTEXT_BIND_MAX; i++) {
			if (atomic_read(&dummy_slave->hw_status[i]) != DCAM_DUMMY_HW_OFF) {
				enable = 1;
				break;
			}
		}
		if (!enable) {
			if (dummy_slave->reserved_buf) {
				cam_queue_empty_frame_put(dummy_slave->reserved_buf);
				dummy_slave->reserved_buf = NULL;
			}
			atomic_set(&dummy_slave->status, DCAM_DUMMY_DISABLE);
			enable_param.enable = 0;
			hw->dcam_ioctl(hw, dummy_param->hw_ctx_id,
				DCAM_HW_CFG_DUMMY_ENABLE, &enable_param);
		}
		spin_unlock_irqrestore(&dummy_slave->dummy_lock, flags);
	}
	pr_info("dummy slave dummy_slave->sw_mode%d, status:%d, skip_num:%d\n",
		dummy_slave->sw_mode, atomic_read(&dummy_slave->status), dummy_slave->skip_num);
	return 0;
}

static struct dcam_dummy_ops dummy_ops = {
	.cfg_param = dcamdummy_param_cfg,
	.dummy_enable = dcamdummy_enable,
};

struct dcam_dummy_slave *dcam_dummy_ctx_desc_get(void *arg, uint32_t idx)
{
	struct dcam_dummy_slave *dummy_slave = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return NULL;
	}

	if (s_pdummy_desc[idx])
		dummy_slave = s_pdummy_desc[idx];
	else {
		dummy_slave = cam_buf_kernel_sys_kzalloc(sizeof(struct dcam_dummy_slave),
			GFP_KERNEL);
		if (dummy_slave == NULL) {
			pr_err("fail to alloc memory.\n");
			return NULL;
		}
		s_pdummy_desc[idx] = dummy_slave;
		atomic_set(&dummy_slave->user_cnt, 0);
		spin_lock_init(&dummy_slave->dummy_lock);
		dummy_slave->idx = idx;
		dummy_slave->hw = (struct cam_hw_info *)arg;
		dummy_slave->dummy_ops = &dummy_ops;
	}

	atomic_inc(&dummy_slave->user_cnt);
	return dummy_slave;
}

int dcam_dummy_ctx_desc_put(struct dcam_dummy_slave *dummy_slave)
{
	struct dcam_hw_dummy_param param = {0};

	if (!dummy_slave) {
		pr_warn("warning dummy slave already put\n");
		return 0;
	}

	if (!atomic_dec_return(&dummy_slave->user_cnt)) {
		param.enable = 0;
		param.idx = dummy_slave->idx;
		dummy_slave->hw->dcam_ioctl(dummy_slave->hw,
			dummy_slave->idx, DCAM_HW_CFG_DUMMY_ENABLE, &param);
		s_pdummy_desc[dummy_slave->idx] = NULL;
		cam_buf_kernel_sys_kfree(dummy_slave);
		dummy_slave = NULL;
	}
	return 0;
}
