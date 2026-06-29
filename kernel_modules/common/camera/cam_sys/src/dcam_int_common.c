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

#include <sprd_mm.h>

#include "dcam_dummy.h"
#include "dcam_int.h"
#include "dcam_int_common.h"
#include "dcam_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_INT_COMMON: %d %d %s : " fmt, current->pid, __LINE__, __func__

/* #define DCAM_INT_RECORD 1 */
/* #define DCAM_INT_RECORD_LSCM_TX_DONE 1 */
#ifdef DCAM_INT_RECORD
#define INT_RCD_SIZE 0x10000
static uint32_t dcam_int_recorder[DCAM_HW_CONTEXT_MAX][DCAM_IRQ_NUMBER][INT_RCD_SIZE] = {0};
static uint32_t int_index[DCAM_HW_CONTEXT_MAX][DCAM_IRQ_NUMBER] = {0};
#endif

#define DCAM_IRQ_INT1_NUMBER 26
static uint32_t dcam_int0_tracker[DCAM_HW_CONTEXT_MAX][DCAM_IRQ_NUMBER] = {0};
static uint32_t dcam_int1_tracker[DCAM_HW_CONTEXT_MAX][DCAM_IRQ_INT1_NUMBER] = {0};
static char *dcam_dev_name[] = {"DCAM0", "DCAM1", "DCAM2", "DCAM3"};

static void dcamintcommon_dcam_status_rw(struct dcam_irq_info irq_info, void *priv)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)priv;
	unsigned int i = 0;

	for (i = 0; i < dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[0]->count; i++) {
		int cur_int = dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[0]->bits[i];

		if (irq_info.status & BIT(cur_int)) {
			if ((*dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[0])[cur_int]) {
				(*dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[0])[cur_int](dcam_hw_ctx);
				irq_info.status &= ~dcam_hw_ctx->handled_bits;
				irq_info.status1 &= ~dcam_hw_ctx->handled_bits_on_int1;
				dcam_hw_ctx->handled_bits = 0;
				dcam_hw_ctx->handled_bits_on_int1 = 0;
			} else
				pr_warn("warning: DCAM%u missing handler for int %d\n",
					dcam_hw_ctx->hw_ctx_id, cur_int);
			irq_info.status &= ~BIT(cur_int);
			if (!irq_info.status)
				break;
		}
	}

	if (dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[1] && dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[1]) {
		for (i = 0; i < dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[1]->count; i++) {
			int cur_int = 0;
			if (dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[1]->bits == NULL)
				break;

			cur_int = dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[1]->bits[i];
			if (irq_info.status1 & BIT(cur_int)) {
				if ((*dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[1])[cur_int]) {
					(*dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[1])[cur_int](dcam_hw_ctx);
				} else
					pr_warn("warning: DCAM%u missing handler for int1 bit%d\n",
						dcam_hw_ctx->hw_ctx_id, cur_int);
				irq_info.status1 &= ~BIT(cur_int);
				if (!irq_info.status1)
					break;
			}
		}
	}

	dcam_hw_ctx->irq_ops.status_warning(dcam_hw_ctx, irq_info.status, irq_info.status1);
}

static int dcamintcommon_error_handler_param(struct dcam_hw_context *dcam_hw_ctx, uint32_t status)
{
	enum dcam_state state = STATE_INIT;
	struct dcam_irq_proc_desc irq_desc = {0};
	struct dcam_irq_proc irq_proc = {0};
	struct dcam_irq_error_flag *error_bit = NULL;
	uint32_t recovery_support_abt = 0;

	const char *tb_ovr[2] = {"", ", overflow"};
	const char *tb_lne[2] = {"", ", line error"};
	const char *tb_frm[2] = {"", ", frame error"};
	const char *tb_mmu[2] = {"", ", mmu"};

	if (!dcam_hw_ctx->dcam_irq_cb_handle) {
		pr_err("fail to get valid dcam_online_node\n");
		return 0;
	}
	error_bit = &dcam_hw_ctx->irq_ops.error_bit;
	pr_err("fail to get normal status DCAM%u 0x%x%s%s%s%s\n", dcam_hw_ctx->hw_ctx_id, status,
		tb_ovr[!!(status & error_bit->overflow)],
		tb_lne[!!(status & error_bit->line_err)],
		tb_frm[!!(status & error_bit->frm_err)],
		tb_mmu[!!(status & error_bit->mmu_int)]);

	if (status & error_bit->mmu_int) {
		uint32_t val = DCAM_IOMMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_STS);

		if (val != dcam_hw_ctx->iommu_status) {
			dcam_hw_ctx->irq_ops.iommu_regs_dump(dcam_hw_ctx);
			dcam_hw_ctx->iommu_status = val;

			if (dcam_hw_ctx->hw && dcam_hw_ctx->hw->ip_dcam[dcam_hw_ctx->hw_ctx_id]
					&& dcam_hw_ctx->hw->ip_dcam[dcam_hw_ctx->hw_ctx_id]->dcamhw_abt)
				recovery_support_abt = dcam_hw_ctx->hw->ip_dcam[dcam_hw_ctx->hw_ctx_id]->dcamhw_abt->recovery_support;
			if (!dcam_hw_ctx->is_offline_proc && !dcam_hw_ctx->is_virtualsensor_proc
					&& (error_bit->mmu_int & recovery_support_abt) && dcam_hw_ctx->dcam_irq_cb_func) {
				irq_proc.of = DCAM_INT_ERROR;
				irq_proc.status = status;
				dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
				return -1;
			}
		}
	}

	if (dcam_hw_ctx->is_offline_proc && (status & error_bit->fatal_error)) {
		irq_desc.dcam_cb_type = CAM_CB_DCAM_DEV_ERR;
		dcam_hw_ctx->dcam_irq_cb_func(&irq_desc, dcam_hw_ctx->dcam_irq_cb_handle);
		return -1;
	}

	if (dcam_hw_ctx->is_virtualsensor_proc)
		state = dcam_fetch_node_state_get(dcam_hw_ctx->dcam_irq_cb_handle);
	else
		state = dcam_online_node_state_get(dcam_hw_ctx->dcam_irq_cb_handle);

	if (dcam_hw_ctx->dcam_irq_cb_func) {
		if ((status & error_bit->fatal_error) && (state != STATE_ERROR)) {
			irq_proc.of = DCAM_INT_ERROR;
			irq_proc.status = status;
			dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
			return -1;
		}
	}

	return 0;
}

static irqreturn_t dcamintcommon_isr_root(int irq, void *priv)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;
	struct dcam_irq_info irq_status = {0};
	int ret = 0;

	if (unlikely(!priv)) {
		pr_err("fail to get valid priv\n");
		return IRQ_HANDLED;
	}

	dcam_hw_ctx = (struct dcam_hw_context *)priv;
	if (unlikely(irq != dcam_hw_ctx->irq)) {
		pr_err("fail to match DCAM%u irq %d %d\n", dcam_hw_ctx->hw_ctx_id, irq, dcam_hw_ctx->irq);
		return IRQ_NONE;
	}
	if (!read_trylock(&dcam_hw_ctx->hw->soc_dcam->cam_ahb_lock))
		return IRQ_HANDLED;
	dcam_hw_ctx->in_irq_proc = 1;
	if (!dcam_hw_ctx->dcam_irq_cb_handle) {
		irq_status = dcam_hw_ctx->irq_ops.mask_clr(dcam_hw_ctx->hw_ctx_id);
		pr_err("fail to get hw ctx:%d irq cb handle, irq status: 0x%x 0x%x\n",
			dcam_hw_ctx->hw_ctx_id, irq_status.status, irq_status.status1);
		ret = IRQ_NONE;
		goto exit;
	}

	/* get irq_status*/
	irq_status = dcam_hw_ctx->irq_ops.isr_handle(dcam_hw_ctx);
	if (unlikely(!irq_status.status && !irq_status.status1)) {
		ret = IRQ_NONE;
		goto exit;
	}

	/* Interrupt err pro: may need to put it into isr_root */
	if (unlikely(dcam_hw_ctx->irq_ops.error_bit.all_error & irq_status.status)) {
		ret = dcamintcommon_error_handler_param(dcam_hw_ctx, irq_status.status);
		if (ret < 0) {
			ret = IRQ_HANDLED;
			goto exit;
		}
		irq_status.status &= (~dcam_hw_ctx->irq_ops.error_bit.all_error);
	}

	pr_debug("DCAM%u status=0x%x\n", dcam_hw_ctx->hw_ctx_id, irq_status.status);
	if (dcam_hw_ctx->is_offline_proc) {
		dcam_core_offline_irq_proc(dcam_hw_ctx, &irq_status);
	} else
		/*dcam isr root write status */
		dcamintcommon_dcam_status_rw(irq_status, priv);
	ret = IRQ_HANDLED;
exit:
	read_unlock(&dcam_hw_ctx->hw->soc_dcam->cam_ahb_lock);
	dcam_hw_ctx->in_irq_proc = 0;
	return ret;
}

void dcam_int_common_full_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (!dcam_hw_ctx) {
		pr_err("fail to get valid inptr %p\n", dcam_hw_ctx);
		return;
	}

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	pr_debug("dcamint_full_path_done hw_id: %d\n", dcam_hw_ctx->hw_ctx_id);
	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_FULL_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_bin_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	pr_debug("bin_path_done hw id:%d\n", dcam_hw_ctx->hw_ctx_id);
	dcam_hw_ctx->dec_layer0_done = 1;
	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_BIN_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_vch2_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	pr_debug("dcamint_vch2_path_done hw_ctx_id = %d\n", dcam_hw_ctx->hw_ctx_id);
	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_VCH2_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_gtm_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (!dcam_hw_ctx) {
		pr_err("fail to get valid inptr %p\n", dcam_hw_ctx);
		return;
	}

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_GTM_HIST_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_irq_sensor_sof(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = SN_START_OF_FRAME;
	dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_irq_sensor_eof(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = SN_END_OF_FRAME;
	dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_lscm_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_LSCM_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_aem_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_AEM_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_pdaf_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_PDAF_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_afm_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_AFM_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_afl_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_AFL_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_hist_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_BAYER_HIST_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}


void dcam_int_common_nr3_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	uint32_t i = 0, fid = 0, pingpong_en = 0, shift_count = 0;
	struct dcam_irq_proc irq_proc = {0};
	struct nr3_done nr3_done_com;

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL || dcam_hw_ctx->hw_ctx_id >= DCAM_HW_CONTEXT_MAX) {
		pr_err("fail to dcamonline callback fun is NULL, hw_ctx_id %d\n", dcam_hw_ctx->hw_ctx_id);
		return;
	}

	fid = dcam_hw_ctx->fid - 1;
	i = fid % DCAM_NR3_MV_MAX;
	nr3_done_com = dcam_int_nr3_done_rd(dcam_hw_ctx, i);
	if(nr3_done_com.hw_ctx == 1)
		return;

	/* if ping-pong open, all project first frame will always be stored in ping */
	pingpong_en = (nr3_done_com.p >> 1) & 0x1;
	if ((fid % 2) && pingpong_en)
		shift_count = 16;

	dcam_hw_ctx->nr3_mv_ctrl[i].mv_x = (nr3_done_com.out0 >> (8 + shift_count)) & 0xff;
	dcam_hw_ctx->nr3_mv_ctrl[i].mv_y = (nr3_done_com.out0 >> shift_count) & 0xff;
	dcam_hw_ctx->nr3_mv_ctrl[i].src_width = dcam_hw_ctx->cap_info.cap_size.size_x;
	dcam_hw_ctx->nr3_mv_ctrl[i].src_height = dcam_hw_ctx->cap_info.cap_size.size_y;
	dcam_hw_ctx->nr3_mv_ctrl[i].valid = 1;
	pr_debug("dcam%d fid %d, pingpong_en %d, mv_x %d, mv_y %d\n", dcam_hw_ctx->hw_ctx_id, fid,
		pingpong_en, dcam_hw_ctx->nr3_mv_ctrl[i].mv_x, dcam_hw_ctx->nr3_mv_ctrl[i].mv_y);

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.is_nr3_done = 1;
	irq_proc.dcam_port_id = PORT_DCAM_OUT_MAX;
	dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_irq_preview_sof(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = PREV_START_OF_FRAME;
	dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

void dcam_int_common_vch3_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	if(dcam_hw_ctx->hw->ip_dcam[dcam_hw_ctx->hw_ctx_id]->dcamhw_abt->vch3_output_pdaf_support)
		irq_proc.dcam_port_id = PORT_PDAF_OUT;
	else
		irq_proc.dcam_port_id = PORT_VCH3_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

inline void dcam_int_common_record(uint32_t idx, struct dcam_irq_info *irq_info)
{
	uint32_t i = 0;

	for (i = 0; i < DCAM_IRQ_NUMBER; i++) {
		if (irq_info->status & BIT(i))
			dcam_int0_tracker[idx][i]++;
		if (irq_info->status1 & BIT(i))
			dcam_int1_tracker[idx][i]++;
	}

#ifdef DCAM_INT_RECORD
	{
		uint32_t cnt = 0, time = 0, int_no = 0;
		timespec cur_ts = {0};

		os_adapt_time_get_ts(&cur_ts);
		time = (uint32_t)(cur_ts.tv_sec & 0xffff);
		time <<= 16;
		time |= (uint32_t)((cur_ts.tv_nsec / (NSEC_PER_USEC * 100)) & 0xffff);
		for (int_no = 0; int_no < DCAM_IRQ_NUMBER; int_no++) {
			if (irq_info->status & BIT(int_no)) {
				cnt = int_index[idx][int_no];
				dcam_int_recorder[idx][int_no][cnt] = time;
				cnt++;
				int_index[idx][int_no] = (cnt & (INT_RCD_SIZE - 1));
			}
		}
	}
#endif
}

void dcam_int_common_tracker_dump(uint32_t idx)
{
	int i = 0;

	if (!is_dcam_id(idx))
		return;

	for (i = 0; i < DCAM_IRQ_NUMBER; i++) {
		if (dcam_int0_tracker[idx][i])
			pr_info("DCAM%u i=%d, int0=%u\n", idx, i, dcam_int0_tracker[idx][i]);
	}
	for (i = 0; i < DCAM_IRQ_INT1_NUMBER; i++) {
		if (dcam_int1_tracker[idx][i])
			pr_info("DCAM%u i=%d, int1=%u\n", idx, i, dcam_int1_tracker[idx][i]);
	}
#ifdef DCAM_INT_RECORD
	{
		uint32_t cnt = 0, j = 0;
		for (cnt = 0; cnt < (uint32_t)dcam_int0_tracker[idx][DCAM_SENSOR_EOF]; cnt += 4) {
			j = (cnt & (INT_RCD_SIZE - 1));
			pr_info("DCAM%u j=%d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d\n",
				idx, j, (uint32_t)dcam_int_recorder[idx][DCAM_SENSOR_EOF][j] >> 16,
				(uint32_t)dcam_int_recorder[idx][DCAM_SENSOR_EOF][j] & 0xffff,
				(uint32_t)dcam_int_recorder[idx][DCAM_CAP_SOF][j] >> 16,
				(uint32_t)dcam_int_recorder[idx][DCAM_CAP_SOF][j] & 0xffff,
				(uint32_t)dcam_int_recorder[idx][DCAM_FULL_PATH_TX_DONE][j] >> 16,
				(uint32_t)dcam_int_recorder[idx][DCAM_FULL_PATH_TX_DONE][j] & 0xffff,
				(uint32_t)dcam_int_recorder[idx][DCAM_PREV_PATH_TX_DONE][j] >> 16,
				(uint32_t)dcam_int_recorder[idx][DCAM_PREV_PATH_TX_DONE][j] & 0xffff,
				(uint32_t)dcam_int_recorder[idx][DCAM_AEM_TX_DONE][j] >> 16,
				(uint32_t)dcam_int_recorder[idx][DCAM_AEM_TX_DONE][j] & 0xffff,
				(uint32_t)dcam_int_recorder[idx][DCAM_AFM_INTREQ1][j] >> 16,
				(uint32_t)dcam_int_recorder[idx][DCAM_AFM_INTREQ1][j] & 0xffff);
#ifdef DCAM_INT_RECORD_LSCM_TX_DONE
			pr_info("DCAM_LSCM_TX_DONE %03d.%04d\n",
				(uint32_t)dcam_int_recorder[idx][DCAM_LSCM_TX_DONE][j] >> 16,
				(uint32_t)dcam_int_recorder[idx][DCAM_LSCM_TX_DONE][j] & 0xffff);
#endif
		}
	}
#endif
}

inline int dcam_int_common_dummy_callback(void *hw_ctx, struct dcam_irq_proc *irq_proc)
{
	uint32_t dummy_status = 0;
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)hw_ctx;
	if (dcam_hw_ctx->dummy_slave) {
		dummy_status = atomic_read(&dcam_hw_ctx->dummy_slave->status);
		if (dummy_status == DCAM_DUMMY_TRIGGER || dummy_status == DCAM_DUMMY_DONE) {
			irq_proc->hw_ctx = dcam_hw_ctx;
			switch (irq_proc->of) {
			case CAP_START_OF_FRAME:
				dcam_hw_ctx->dummy_slave->dummy_ops->dummyint_callback(dcam_hw_ctx->dummy_slave, DCAM_DUMMY_CALLBACK_CAP_SOF, irq_proc);
				break;
			case CAP_DATA_DONE:
				dcam_hw_ctx->dummy_slave->dummy_ops->dummyint_callback(dcam_hw_ctx->dummy_slave, DCAM_DUMMY_CALLBACK_PATH_DONE, irq_proc);
				break;
			default:
				pr_debug("warning irq_proc of:%d\n", irq_proc->of);
			}
			return 1;
		} else
			return 0;
	}
	return 0;
}

int dcam_int_common_irq_request(struct device *pdev, int irq, void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;
	int ret = 0;

	if (unlikely(!pdev || !param || irq < 0)) {
		pr_err("fail to get valid param %p %p %d\n", pdev, param, irq);
		return -EINVAL;
	}

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	dcam_hw_ctx->irq = irq;

	ret = devm_request_irq(pdev, dcam_hw_ctx->irq, dcamintcommon_isr_root,
			IRQF_SHARED, dcam_dev_name[dcam_hw_ctx->hw_ctx_id], dcam_hw_ctx);
	if (ret < 0) {
		pr_err("DCAM%u fail to install irq %d\n", dcam_hw_ctx->hw_ctx_id, dcam_hw_ctx->irq);
		return -EFAULT;
	}
	dcam_hw_ctx->irq_enable = 1;
	dcam_int_common_tracker_reset(dcam_hw_ctx->hw_ctx_id);
	return ret;
}

void dcam_int_common_irq_free(struct device *pdev, void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	if (unlikely(!pdev || !param)) {
		pr_err("fail to get valid param %p %p\n", pdev, param);
		return;
	}

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	devm_free_irq(pdev, dcam_hw_ctx->irq, dcam_hw_ctx);
}

void dcam_int_common_tracker_reset(uint32_t idx)
{
	if (is_dcam_id(idx)) {
		memset(dcam_int0_tracker[idx], 0, sizeof(dcam_int0_tracker[idx]));
		memset(dcam_int1_tracker[idx], 0, sizeof(dcam_int1_tracker[idx]));
	}

#ifdef DCAM_INT_RECORD
	if (is_dcam_id(idx)) {
		memset(dcam_int_recorder[idx][0], 0, sizeof(dcam_int_recorder) / 3);
		memset(int_index[idx], 0, sizeof(int_index) / 3);
	}
#endif
}

