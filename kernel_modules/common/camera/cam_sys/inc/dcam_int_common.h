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

#ifndef _DCAM_INT_COMMON_H_
#define _DCAM_INT_COMMON_H_

#include <linux/bitops.h>
#include <linux/device.h>

#include "cam_types.h"
#include "dcam_interface.h"

typedef void (*dcam_int_isr)(void *param);
typedef const dcam_int_isr (*dcam_int_isr_array)[32];

struct nr3_done {
	uint32_t hw_ctx;
	uint32_t p;
	uint32_t out0;
	uint32_t out1;
};

struct dcam_sequences{
	size_t count;
	const int *bits;
};

struct dcam_irq_info {
	uint32_t irq_num;
	uint32_t status;
	uint32_t status1;
};

struct dcam_irq_error_flag {
	uint32_t overflow;
	uint32_t line_err;
	uint32_t mmu_int;
	uint32_t frm_err;
	uint32_t fatal_error;
	uint32_t all_error;
};

struct dcam_irq_ops {
	struct dcam_irq_info (*mask_clr)(uint32_t idx);
	struct dcam_irq_info (*isr_handle)(void *param);
	void (*iommu_regs_dump)(void *param);
	void (*status_warning)(void *param, uint32_t status, uint32_t status1);
	dcam_int_isr_array _DCAM_ISR_IRQ[2];
	const struct dcam_sequences *DCAM_SEQUENCES[2];
	struct dcam_irq_error_flag error_bit;
};

void dcam_int_common_full_port_done(void *param);
void dcam_int_common_bin_port_done(void *param);
void dcam_int_common_vch2_port_done(void *param);
void dcam_int_common_gtm_port_done(void *param);
void dcam_int_common_irq_sensor_sof(void *param);
void dcam_int_common_irq_sensor_eof(void *param);
void dcam_int_common_lscm_port_done(void *param);
void dcam_int_common_aem_port_done(void *param);
void dcam_int_common_pdaf_port_done(void *param);
void dcam_int_common_afm_port_done(void *param);
void dcam_int_common_afl_port_done(void *param);
void dcam_int_common_hist_port_done(void *param);
void dcam_int_common_nr3_port_done(void *param);
void dcam_int_common_irq_preview_sof(void *param);
void dcam_int_common_vch3_port_done(void *param);
inline void dcam_int_common_record(uint32_t idx, struct dcam_irq_info *irq_info);
void dcam_int_common_tracker_dump(uint32_t idx);
inline int dcam_int_common_dummy_callback(void *hw_ctx, struct dcam_irq_proc *irq_proc);
int dcam_int_common_irq_request(struct device *pdev, int irq, void *param);
void dcam_int_common_irq_free(struct device *pdev, void *param);
void dcam_int_common_tracker_reset(uint32_t idx);

#endif
