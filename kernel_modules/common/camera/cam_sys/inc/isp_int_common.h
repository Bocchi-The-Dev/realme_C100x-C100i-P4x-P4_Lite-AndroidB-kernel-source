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

#ifndef _ISP_INT_COMMON_H_
#define _ISP_INT_COMMON_H_

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include "cam_types.h"
#include "isp_dev.h"
#include "isp_interface.h"

#define ISP_INT_LOGICAL_COUNT 2

void isp_int_common_all_done(enum isp_context_hw_id hw_idx, void *isp_handle);
void isp_int_common_shadow_done(enum isp_context_hw_id idx, void *isp_handle);
void isp_int_common_dispatch_done(enum isp_context_hw_id idx, void *isp_handle);
void isp_int_common_pre_store_done(enum isp_context_hw_id idx, void *isp_handle);
void isp_int_common_vid_store_done(enum isp_context_hw_id idx, void *isp_handle);
void isp_int_common_thumb_store_done(enum isp_context_hw_id idx, void *isp_handle);
void isp_int_common_fmcu_store_done(enum isp_context_hw_id hw_idx, void *isp_handle);
void isp_int_common_fmcu_shadow_done(enum isp_context_hw_id hw_idx, void *isp_handle);
void isp_int_common_fmcu_load_done(enum isp_context_hw_id idx, void *isp_handle);
void isp_int_common_3dnr_all_done(enum isp_context_hw_id hw_idx, void *isp_handle);
void isp_int_common_3dnr_shadow_done(enum isp_context_hw_id hw_idx, void *isp_handle);
void isp_int_common_hist2_cal_done(enum isp_context_hw_id hw_idx, void *isp_handle);
int isp_int_common_irq_hw_cnt_reset(int ctx_id);
int isp_int_common_irq_hw_cnt_trace(int ctx_id);
int isp_int_common_irq_request(struct device *p_dev, uint32_t *irq_no, void *isp_handle);
int isp_int_common_irq_sw_cnt_reset(int ctx_id);
int isp_int_common_irq_sw_cnt_trace(int ctx_id);
int isp_int_common_irq_free(struct device *p_dev, void *isp_handle);

#endif
