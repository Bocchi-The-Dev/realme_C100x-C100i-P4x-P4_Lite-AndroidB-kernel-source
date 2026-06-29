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

#include <linux/err.h>
#include <linux/interrupt.h>
#include <os_adapt_common.h>
#include <sprd_mm.h>

#include "dcam_dummy.h"
#include "dcam_int_common.h"
#include "dcam_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_INT: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define DCAMINT_HW_ADPT_LAYER
#include "dcamlite_int.c"

static const char *_DCAM_ADDR_NAME[DCAM_RECORD_PORT_INFO_MAX] = {
	[DCAM_RECORD_PORT_FBC_BIN] = "fbc_bin",
	[DCAM_RECORD_PORT_FBC_FULL] = "fbc_full",
	[DCAM_RECORD_PORT_FULL_Y] = "full_y",
	[DCAM_RECORD_PORT_FULL_U] = "full_u",
	[DCAM_RECORD_PORT_BIN_Y] = "bin_y",
	[DCAM_RECORD_PORT_BIN_U] = "bin_u",
	[DCAM_RECORD_PORT_RAW] = "raw",
	[DCAM_RECORD_PORT_DEC_L1_Y] = "dec_l1_y",
	[DCAM_RECORD_PORT_DEC_L1_U] = "dec_l1_u",
	[DCAM_RECORD_PORT_DEC_L2_Y] = "dec_l2_y",
	[DCAM_RECORD_PORT_DEC_L2_U] = "dec_l2_u",
	[DCAM_RECORD_PORT_DEC_L3_Y] = "dec_l3_y",
	[DCAM_RECORD_PORT_DEC_L3_U] = "dec_l3_u",
	[DCAM_RECORD_PORT_DEC_L4_Y] = "dec_l4_y",
	[DCAM_RECORD_PORT_DEC_L4_U] = "dec_l4_u",
	[DCAM_RECORD_PORT_PDAF] = "pdaf",
	[DCAM_RECORD_PORT_VCH2] = "vch2",
	[DCAM_RECORD_PORT_VCH3] = "vch3",
	[DCAM_RECORD_PORT_LENS] = "lens",
	[DCAM_RECORD_PORT_LSCM] = "lscm",
	[DCAM_RECORD_PORT_AEM] = "aem",
	[DCAM_RECORD_PORT_BAYER_HIST] = "bayer_hist",
	[DCAM_RECORD_PORT_PPE] = "ppe",
	[DCAM_RECORD_PORT_AFL_DDR_INIT] = "afl_ddr_init",
	[DCAM_RECORD_PORT_AFL_REGION] = "afl_region",
	[DCAM_RECORD_PORT_BPC_MAP] = "bpc_map",
	[DCAM_RECORD_PORT_BPC_OUT] = "bpc_out",
	[DCAM_RECORD_PORT_AFM] = "afm",
	[DCAM_RECORD_PORT_NR3] = "nr3",
	[DCAM_RECORD_PORT_HIST_ROI] = "hist_roi",
};

static const char *dcamint_addr_name_get(uint32_t type)
{
	return (type < DCAM_RECORD_PORT_IMG_FETCH) ? _DCAM_ADDR_NAME[type] : "(null)";
}

static void dcamint_fbc_set(struct dcam_hw_context *dcam_hw_ctx, uint32_t status)
{
	spin_lock(&dcam_hw_ctx->fbc_lock);
	if (status & BIT(DCAM_IF_IRQ_INT0_CAP_SOF)) {
		dcam_hw_ctx->cap_fbc_done = 1;
		dcam_hw_ctx->prev_fbc_done = 0;
		if (DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_PATH_SEL) & BIT_8)
			dcam_hw_ctx->cap_fbc_done = 0;
	}
	if (status & BIT(DCAM_IF_IRQ_INT0_CAPTURE_PATH_TX_DONE))
		dcam_hw_ctx->cap_fbc_done = 1;
	if (status & BIT(DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE))
		dcam_hw_ctx->prev_fbc_done = 1;
	spin_unlock(&dcam_hw_ctx->fbc_lock);
}

static void dcamint_dcam_dummy_proc(struct dcam_irq_info *irq_info, void *priv)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)priv;

	if (dcam_hw_ctx->dummy_slave) {
		if (irq_info->status1 & BIT(DCAM_IF_IRQ_INT1_DUMMY_START))
			dcam_hw_ctx->dummy_slave->dummy_ops->dummyint_callback(dcam_hw_ctx->dummy_slave, DCAM_DUMMY_CALLBACK_DUMMY_START, dcam_hw_ctx);
		if (irq_info->status1 & BIT(DCAM_IF_IRQ_INT1_DUMMY_DONE))
			dcam_hw_ctx->dummy_slave->dummy_ops->dummyint_callback(dcam_hw_ctx->dummy_slave, DCAM_DUMMY_CALLBACK_DUMMY_DONE, dcam_hw_ctx);
	}
}

static void dcamint_irq_cap_eof(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	DCAM_AXIM_WR(dcam_hw_ctx->hw_ctx_id, AXIM_CNT_CLR, 1);
	pr_debug("get into dcamint_cap_eof success\n");
}

static void dcamint_irq_cap_sof(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (!param || dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	irq_proc.of = CAP_START_OF_FRAME;
	irq_proc.frm_cnt = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_CAP_FRM_CLR) & 0xFF;
	irq_proc.bin_addr_value = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_STORE0_SLICE_Y_ADDR);
	irq_proc.full_addr_value = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_STORE4_SLICE_Y_ADDR);
	irq_proc.handled_bits = DCAMINT_INT0_TX_DONE;
	irq_proc.handled_bits_on_int1 = DCAMINT_INT1_TX_DONE;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

static void dcamint_irq_sensor_sof(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	pr_debug("DCAM%d, dcamint_sensor_sof\n", dcam_hw_ctx->hw_ctx_id);

	if (dcam_hw_ctx->cap_info.cap_size.size_x > DCAM_TOTAL_LBUF) {
		if (dcam_hw_ctx->fid == 0)
			dcam_hw_ctx->fid++;
		else
			dcamint_irq_cap_sof(param);
	}
	irq_proc.of = SN_START_OF_FRAME;
	dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

static void dcamint_raw_port_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (!dcam_hw_ctx) {
		pr_err("fail to get valid inptr %p\n", dcam_hw_ctx);
		return;
	}

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to get valid dcamonline callback fun.\n");
		return;
	}

	pr_debug("dcamint_raw_path_done\n");
	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_RAW_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

static void dcamint_frgb_hist_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx == NULL) {
		pr_err("fail to get valid dcam_hw_ctx.\n");
		return;
	}

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to get valid dcamonline callback fun.\n");
		return;
	}

	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_FRGB_HIST_OUT;
	if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

static void dcamint_dec_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (!dcam_hw_ctx) {
		pr_err("fail to get valid inptr %p\n", dcam_hw_ctx);
		return;
	}

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to get valid dcamonline callback fun.\n");
		return;
	}

	pr_debug("dcamint_dec_done\n");
	dcam_hw_ctx->dec_all_done = 1;
	if (dcam_hw_ctx->dec_layer0_done) {
		dcam_hw_ctx->virtualsensor_dec_int_done = 1;
		irq_proc.of = CAP_DATA_DONE;
		irq_proc.dcam_port_id = PORT_BIN_OUT;
		if (!dcam_int_common_dummy_callback(dcam_hw_ctx, &irq_proc))
			dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
		dcam_hw_ctx->dec_all_done = 0;
		dcam_hw_ctx->dec_layer0_done = 0;
		dcam_hw_ctx->virtualsensor_dec_int_done = 0;
	}
}

static void dcamint_offline_proc(struct dcam_hw_context *dcam_hw_ctx, struct dcam_irq_info *irq_info)
{
	uint32_t i = 0, fid = 0;
	struct nr3_done nr3_done_com = {0};

	if (irq_info->status & BIT(DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE)) {
		dcam_hw_ctx->dec_layer0_done = 1;
		if (!dcam_hw_ctx->is_pyr_rec && dcam_hw_ctx->is_3dnr)
			dcam_hw_ctx->nr3_path_cnt++;
	}
	if (irq_info->status1 & BIT(DCAM_IF_IRQ_INT1_DEC_DONE)) {
		dcam_hw_ctx->dec_all_done = 1;
		irq_info->status1 &= ~BIT(DCAM_IF_IRQ_INT1_DEC_DONE);
		if (dcam_hw_ctx->dec_layer0_done)
			irq_info->status |= BIT(DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE);
	}
	if (dcam_hw_ctx->dec_all_done && dcam_hw_ctx->dec_layer0_done) {
		dcam_hw_ctx->dec_layer0_done = 0;
		dcam_hw_ctx->dec_all_done = 0;
		if (dcam_hw_ctx->is_3dnr)
			dcam_hw_ctx->nr3_path_cnt++;
	} else {
		if (dcam_hw_ctx->is_pyr_rec && !dcam_hw_ctx->is_3dnr) {
			irq_info->status &= ~BIT(DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE);
			return;
		}
	}

	if (irq_info->status & BIT(DCAM_IF_IRQ_INT0_CAPTURE_PATH_TX_DONE))
		dcam_hw_ctx->nr3_path_cnt++;
	if (irq_info->status & BIT(DCAM_IF_IRQ_INT0_NR3_TX_DONE)) {
		dcam_hw_ctx->nr3_path_mv_ready = 1;
		fid = dcam_hw_ctx->fid - 1;
		i = fid % DCAM_NR3_MV_MAX;
		nr3_done_com = dcam_int_nr3_done_rd(dcam_hw_ctx, i);
		dcam_hw_ctx->nr3_mv_ctrl[i].sub_me_bypass = (nr3_done_com.p >> 3) & 0x1;
		dcam_hw_ctx->nr3_mv_ctrl[i].project_mode = (nr3_done_com.p >> 4) & 0x3;
		/* currently ping-pong is disabled, mv will always be stored in ping */
		dcam_hw_ctx->nr3_mv_ctrl[i].mv_x = (nr3_done_com.out0 >> 8) & 0xff;
		dcam_hw_ctx->nr3_mv_ctrl[i].mv_y = nr3_done_com.out0 & 0xff;
		dcam_hw_ctx->nr3_mv_ctrl[i].src_width = dcam_hw_ctx->cap_info.cap_size.size_x;
		dcam_hw_ctx->nr3_mv_ctrl[i].src_height = dcam_hw_ctx->cap_info.cap_size.size_y;
		dcam_hw_ctx->nr3_mv_ctrl[i].valid = 1;
		pr_debug("dcam%d fid %d, mv_x %d, mv_y %d src_size=%dx%d\n", dcam_hw_ctx->hw_ctx_id, fid,
			dcam_hw_ctx->nr3_mv_ctrl[i].mv_x, dcam_hw_ctx->nr3_mv_ctrl[i].mv_y, dcam_hw_ctx->cap_info.cap_size.size_x,
			dcam_hw_ctx->cap_info.cap_size.size_y);
		irq_info->status &= ~BIT(DCAM_IF_IRQ_INT0_NR3_TX_DONE);
		if (dcam_hw_ctx->nr3_path_cnt) {
			if ((DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_STORE0_PARAM) & BIT_0) == 0)
				irq_info->status |= BIT(DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE);
			if ((DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_STORE4_PARAM) & BIT_0) == 0)
				irq_info->status |= BIT(DCAM_IF_IRQ_INT0_CAPTURE_PATH_TX_DONE);
		}
	}

	if (dcam_hw_ctx->nr3_path_cnt & dcam_hw_ctx->nr3_path_mv_ready) {
		dcam_hw_ctx->nr3_path_cnt = 0;
		dcam_hw_ctx->nr3_path_mv_ready = 0;
	} else {
		if (dcam_hw_ctx->is_3dnr) {
			if ((DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_STORE0_PARAM) & BIT_0) == 0)
				irq_info->status &= ~BIT(DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE);
			if ((DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_STORE4_PARAM) & BIT_0) == 0)
				irq_info->status &= ~BIT(DCAM_IF_IRQ_INT0_CAPTURE_PATH_TX_DONE);
		}
	}
}

static void dcamint_dummy_start(void *param)
{
	pr_debug("dcamint_dummy_start\n");
}

static void dcamint_fmcu_config_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	int i = 0;
	struct dcam_irq_proc irq_proc = {0};

	if (!dcam_hw_ctx) {
		pr_err("fail to get valid inptr %p\n", dcam_hw_ctx);
		return;
	}

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to get valid dcamonline callback fun.\n");
		return;
	}

	pr_debug("dcamint_fmcu_config_done\n");
	atomic_inc(&dcam_hw_ctx->shadow_config_cnt);

	while (i++ < dcam_hw_ctx->slowmotion_count) {
		irq_proc.slw_count = i;
		irq_proc.of = CAP_DATA_DONE;
		irq_proc.dcam_port_id = PORT_BIN_OUT;
		dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
	}
	/* shadow done & config done happen in the same time
	* shadow done have smaller int num, sw will process (n+1)shadow done before (n)config done
	* int sequence : s sc sc sc, 1 more s than c
	* if lack one shadow done, slowmotion will stop after config done, re-config next cmd q in config done
	* int sequence: s sc sc sc  c (lack s, re-config cmd q) s sc sc sc... 2 more s than c
	*/
	if (atomic_read(&dcam_hw_ctx->shadow_config_cnt) == atomic_read(&dcam_hw_ctx->shadow_done_cnt)) {
		atomic_inc(&dcam_hw_ctx->shadow_done_cnt);
		atomic_inc(&dcam_hw_ctx->shadow_config_cnt);
		if (dcam_hw_ctx->fmcu) {
			dcam_hw_ctx->index_to_set = dcam_hw_ctx->fid +
				dcam_hw_ctx->slowmotion_count;
			irq_proc.of = CAP_DATA_DONE;
			irq_proc.dcam_port_id = PORT_BIN_OUT;
			irq_proc.slw_cmds_set = 1;
			dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
			dcam_hw_ctx->fmcu->ops->cmd_ready(dcam_hw_ctx->fmcu);
		} else
			pr_err("fail to get fmcu handle\n");
	}
}

static void dcamint_fmcu_shadow_done(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (dcam_hw_ctx == NULL) {
		pr_err("fail to check param dcam_hw_ctx %p\n", dcam_hw_ctx);
		return;
	}

	if (dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to get valid dcamonline callback fun.\n");
		return;
	}

	atomic_inc(&dcam_hw_ctx->shadow_done_cnt);

	dcam_hw_ctx->index_to_set = dcam_hw_ctx->fid + dcam_hw_ctx->slowmotion_count;
	irq_proc.of = CAP_DATA_DONE;
	irq_proc.dcam_port_id = PORT_BIN_OUT;
	irq_proc.slw_cmds_set = 1;
	dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
	dcam_hw_ctx->fmcu->ops->cmd_ready(dcam_hw_ctx->fmcu);
}

static void dcamint_dummy_done(void *param)
{
	pr_debug("dcamint_dummy_done\n");
}

static void dcamint_sensor_sof1(void *param)
{
	pr_debug("dcamint_sensor_sof1\n");
}

static void dcamint_sensor_sof2(void *param)
{
	pr_debug("dcamint_sensor_sof2\n");
}

static void dcamint_sensor_sof3(void *param)
{
	pr_debug("dcamint_sensor_sof3\n");
}

static const dcam_int_isr _DCAM0_ISR_IRQ[32] = {
	[DCAM_IF_IRQ_INT0_SENSOR_SOF] = dcamint_irq_sensor_sof,
	[DCAM_IF_IRQ_INT0_SENSOR_EOF] = dcam_int_common_irq_sensor_eof,
	[DCAM_IF_IRQ_INT0_CAP_SOF] = dcamint_irq_cap_sof,
	[DCAM_IF_IRQ_INT0_CAP_EOF] = dcamint_irq_cap_eof,
	[DCAM_IF_IRQ_INT0_RAW_PATH_TX_DONE] = dcamint_raw_port_done,
	[DCAM_IF_IRQ_INT0_PREIEW_SOF] = dcam_int_common_irq_preview_sof,
	[DCAM_IF_IRQ_INT0_CAPTURE_PATH_TX_DONE] = dcam_int_common_full_port_done,
	[DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE] = dcam_int_common_bin_port_done,
	[DCAM_IF_IRQ_INT0_PDAF_PATH_TX_DONE] = dcam_int_common_pdaf_port_done,
	[DCAM_IF_IRQ_INT0_VCH2_PATH_TX_DONE] = dcam_int_common_vch2_port_done,
	[DCAM_IF_IRQ_INT0_VCH3_PATH_TX_DONE] = dcam_int_common_vch3_port_done,
	[DCAM_IF_IRQ_INT0_AEM_PATH_TX_DONE] = dcam_int_common_aem_port_done,
	[DCAM_IF_IRQ_INT0_BAYER_HIST_PATH_TX_DONE] = dcam_int_common_hist_port_done,
	[DCAM_IF_IRQ_INT0_AFL_TX_DONE] = dcam_int_common_afl_port_done,
	[DCAM_IF_IRQ_INT0_AFM_INTREQ1] = dcam_int_common_afm_port_done,
	[DCAM_IF_IRQ_INT0_NR3_TX_DONE] = dcam_int_common_nr3_port_done,
	[DCAM_IF_IRQ_INT0_LSCM_TX_DONE] = dcam_int_common_lscm_port_done,
	[DCAM_IF_IRQ_INT0_GTM_DONE] = dcam_int_common_gtm_port_done,
};

static dcam_int_isr_array _DCAM_ISR_IRQ[DCAM_HW_CONTEXT_MAX] = {
	[DCAM_HW_CONTEXT_0] = &_DCAM0_ISR_IRQ,
	[DCAM_HW_CONTEXT_1] = &_DCAM0_ISR_IRQ,
	[DCAMLITE_HW_CONTEXT_0] = &_DCAMLITE_ISRS,
	[DCAMLITE_HW_CONTEXT_1] = &_DCAMLITE_ISRS,
};

static dcam_int_isr _DCAM_ISRS1[32] = {
	[DCAM_IF_IRQ_INT1_FRGB_HIST_DONE] = dcamint_frgb_hist_done,
	[DCAM_IF_IRQ_INT1_DEC_DONE] = dcamint_dec_done,
	[DCAM_IF_IRQ_INT1_SENSOR_SOF1] = dcamint_sensor_sof1,
	[DCAM_IF_IRQ_INT1_SENSOR_SOF2] = dcamint_sensor_sof2,
	[DCAM_IF_IRQ_INT1_SENSOR_SOF3] = dcamint_sensor_sof3,
	[DCAM_IF_IRQ_INT1_DUMMY_START] = dcamint_dummy_start,
	[DCAM_IF_IRQ_INT1_FMCU_INT1] = dcamint_fmcu_config_done,
	[DCAM_IF_IRQ_INT1_FMCU_INT2] = dcamint_fmcu_shadow_done,
	[DCAM_IF_IRQ_INT1_DUMMY_DONE] = dcamint_dummy_done,
};

static const int _DCAM0_SEQUENCE[] = {
	DCAM_IF_IRQ_INT0_SENSOR_SOF,
	DCAM_IF_IRQ_INT0_CAP_EOF,
	DCAM_IF_IRQ_INT0_PREIEW_SOF,
	DCAM_IF_IRQ_INT0_SENSOR_EOF,
	DCAM_IF_IRQ_INT0_NR3_TX_DONE,
	DCAM_IF_IRQ_INT0_RAW_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_CAPTURE_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_AEM_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_BAYER_HIST_PATH_TX_DONE,
	/* AFM INT0 is some tile done, INT1 is all tile done,
	 * INT0 can use to triger AF ALG eaglier, but the specific time
	 * is not sure, so INT1 is suggested to use now.
	 */
	DCAM_IF_IRQ_INT0_AFM_INTREQ1,
	DCAM_IF_IRQ_INT0_AFL_TX_DONE,
	DCAM_IF_IRQ_INT0_PDAF_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_VCH2_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_VCH3_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_LSCM_TX_DONE,
	DCAM_IF_IRQ_INT0_GTM_DONE,
};

static const int _DCAM0_SEQUENCE_INT1[] = {
	DCAM_IF_IRQ_INT1_FRGB_HIST_DONE,
	DCAM_IF_IRQ_INT1_DEC_DONE,
	DCAM_IF_IRQ_INT1_SENSOR_SOF1,
	DCAM_IF_IRQ_INT1_SENSOR_SOF2,
	DCAM_IF_IRQ_INT1_SENSOR_SOF3,
	DCAM_IF_IRQ_INT1_DUMMY_START,
	DCAM_IF_IRQ_INT1_FMCU_INT2,
	DCAM_IF_IRQ_INT1_FMCU_INT1,
	DCAM_IF_IRQ_INT1_DUMMY_DONE,
};

static const int _DCAM1_SEQUENCE[] = {
	DCAM_IF_IRQ_INT0_SENSOR_SOF,
	DCAM_IF_IRQ_INT0_CAP_EOF,
	DCAM_IF_IRQ_INT0_SENSOR_EOF,
	DCAM_IF_IRQ_INT0_NR3_TX_DONE,
	DCAM_IF_IRQ_INT0_RAW_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_CAPTURE_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_AEM_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_BAYER_HIST_PATH_TX_DONE,
	/* AFM INT0 is some tile done, INT1 is all tile done,
	 * INT0 can use to triger AF ALG eaglier, but the specific time
	 * is not sure, so INT1 is suggested to use now.
	 */
	DCAM_IF_IRQ_INT0_AFM_INTREQ1,
	DCAM_IF_IRQ_INT0_AFL_TX_DONE,
	DCAM_IF_IRQ_INT0_PDAF_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_VCH2_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_VCH3_PATH_TX_DONE,
	DCAM_IF_IRQ_INT0_LSCM_TX_DONE,
	DCAM_IF_IRQ_INT0_GTM_DONE,
};

static const int _DCAM1_SEQUENCE_INT1[] = {
	DCAM_IF_IRQ_INT1_FRGB_HIST_DONE,
	DCAM_IF_IRQ_INT1_DEC_DONE,
	DCAM_IF_IRQ_INT1_SENSOR_SOF1,
	DCAM_IF_IRQ_INT1_SENSOR_SOF2,
	DCAM_IF_IRQ_INT1_SENSOR_SOF3,
	DCAM_IF_IRQ_INT1_DUMMY_START,
	DCAM_IF_IRQ_INT1_FMCU_INT2,
	DCAM_IF_IRQ_INT1_FMCU_INT1,
	DCAM_IF_IRQ_INT1_DUMMY_DONE,
};

static const struct dcam_sequences DCAM_SEQUENCES[DCAM_HW_CONTEXT_MAX][2] = {
	{
		{
			ARRAY_SIZE(_DCAM0_SEQUENCE),
			_DCAM0_SEQUENCE,
		},
		{
			ARRAY_SIZE(_DCAM0_SEQUENCE_INT1),
			_DCAM0_SEQUENCE_INT1,
		},
	},
	{
		{
			ARRAY_SIZE(_DCAM1_SEQUENCE),
			_DCAM1_SEQUENCE,
		},
		{
			ARRAY_SIZE(_DCAM1_SEQUENCE_INT1),
			_DCAM1_SEQUENCE_INT1,
		},
	},
	{
		{
			ARRAY_SIZE(_DCAMLITE_SEQUENCE),
			_DCAMLITE_SEQUENCE,
		}
	},
	{
		{
			ARRAY_SIZE(_DCAMLITE_SEQUENCE),
			_DCAMLITE_SEQUENCE,
		}
	},
};

struct nr3_done dcam_int_nr3_done_rd(void *param, uint32_t idx)
{
	struct nr3_done com = {0};
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	com.p = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_NR3_FAST_ME_PARAM);
	com.out0 = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_NR3_FAST_ME_OUT0);
	com.out1 = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_NR3_FAST_ME_OUT1);

	dcam_hw_ctx->nr3_mv_ctrl[idx].sub_me_bypass = (com.p >> 3) & 0x1;
	dcam_hw_ctx->nr3_mv_ctrl[idx].project_mode = (com.p >> 4) & 0x3;

	return com;
}

void dcam_int_iommu_regs_dump(void *param)
{
	uint32_t i = 0, j = 0, cnt = 0, mask = 0xf;
	uint8_t val = 0xff, dcam_id = DCAM_HW_CONTEXT_MAX;
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	if (!param) {
		pr_err("fail to get valid input hw_ctx\n");
		return;
	}

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	if (dcam_hw_ctx->err_count) {
		for (i = 0; i <= 28; i += 4, mask <<= 4) {
			val = (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_DEBUG_USER) & mask) >> i;
			if (val == 0 && cnt != 7) {
				cnt++;
				continue;
			}
			dcam_id = (val < DCAM1_RAW_PATH_D0) ? DCAM_HW_CONTEXT_0 : DCAM_HW_CONTEXT_1;
			switch (val) {
			case DCAM0_RAW_PATH_D0:
			case DCAM1_RAW_PATH_D0:
				pr_err("fail to handle dcam%d port_raw addr\n", dcam_id);
				break;
			case DCAM0_BIN_DEC_PATH_D0:
			case DCAM1_BIN_DEC_PATH_D0:
				pr_err("fail to handle dcam%d port_bin/dec addr\n", dcam_id);
				break;
			case DCAM0_FULL_PATH_D0:
			case DCAM1_FULL_PATH_D0:
				pr_err("fail to handle dcam%d port_full addr\n", dcam_id);
				break;
			case DCAM0_FULL_FBC_PATH_D0:
			case DCAM1_FULL_FBC_PATH_D0:
				pr_err("fail to handle dcam%d port_full/raw addr\n", dcam_id);
				break;
			case DCAM0_BLOCK_PATH_D0:
			case DCAM1_BLOCK_PATH_D0:
				pr_err("fail to handle dcam%d port: aem/lscm/afl/pdaf/afm/rgb/"
					"hist/nr3/pos/vch2/vch3 addr\n", dcam_id);
				break;
			default:
				break;
			}
		}

		if (dcam_hw_ctx->is_offline_proc)
			pr_err("fail to handle offline dcam frame_cnt=%d, MMU_DEBUG_USER: %08x, "
				"MMU_INT_STS: %08x, MMU_INT_RAW: %08x\n", dcam_hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM],
				DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_DEBUG_USER), DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_STS),
				DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW));
		else
			pr_err("fail to handle fid=%d last_frame_cnt=%d, MMU_DEBUG_USER: %08x, MMU_INT_STS: %08x, "
				"MMU_INT_RAW: %08x\n",
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_CAP_FRM_CLR) & 0xFF,
				dcam_hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM],
				DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_DEBUG_USER), DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_STS),
				DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW));

		if (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW) & BIT_0)
			pr_err("fail to handle, MMU_OR_ADDR_RD: %08x\n", DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_OR_ADDR_RD));
		if (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW) & BIT_1)
			pr_err("fail to handle, MMU_OR_ADDR_WR: %08x\n", DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_OR_ADDR_WR));
		if (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW) & BIT_2)
			pr_err("fail to handle, MMU_INV_ADDR_RD: %08x\n", DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INV_ADDR_RD));
		if (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW) & BIT_3)
			pr_err("fail to handle, MMU_INV_ADDR_WR: %08x\n", DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INV_ADDR_WR));
		if (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW) & BIT_4)
			pr_err("fail to handle, MMU_UNS_ADDR_RD: %08x\n", DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_UNS_ADDR_RD));
		if (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW) & BIT_5)
			pr_err("fail to handle, MMU_UNS_ADDR_WR: %08x\n", DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_UNS_ADDR_WR));
		if (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW) & BIT_6)
			pr_err("fail to handle, MMU_VPN_PAOR_RD: %08x, MMU_PPN_PAOR_RD: %08x\n",
				DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_VPN_PAOR_RD), DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_PPN_PAOR_RD));
		if (DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_INT_RAW) & BIT_7)
			pr_err("fail to handle, MMU_VPN_PAOR_WR: %08x, MMU_PPN_PAOR_WR: %08x\n",
				DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_VPN_PAOR_WR), DCAM_MMU_RD(dcam_hw_ctx->hw_ctx_id, DCAM_MMU_PPN_PAOR_WR));

		for (i = 0; i < DCAM_ADDR_RECORD_FRAME_NUM; i++) {
			for (j = DCAM_RECORD_PORT_FBC_BIN; j < DCAM_RECORD_PORT_IMG_FETCH; j += 5) {
				pr_err("fail to handle used frame_cnt%d, %s: %08x, %s: %08x, %s: %08x,"
					" %s: %08x, %s: %08x\n", dcam_hw_ctx->frame_addr[i][DCAM_RECORD_FRAME_CUR_COUNT],
					dcamint_addr_name_get(j), dcam_hw_ctx->frame_addr[i][j],
					dcamint_addr_name_get(j + 1), dcam_hw_ctx->frame_addr[i][j + 1],
					dcamint_addr_name_get(j + 2), dcam_hw_ctx->frame_addr[i][j + 2],
					dcamint_addr_name_get(j + 3), dcam_hw_ctx->frame_addr[i][j + 3],
					dcamint_addr_name_get(j + 4), dcam_hw_ctx->frame_addr[i][j + 4]);
			}
			if (dcam_hw_ctx->is_offline_proc)
				pr_err("fail to handle used frame_cnt%d, offline fetch %08x\n",
					dcam_hw_ctx->frame_addr[i][DCAM_RECORD_FRAME_CUR_COUNT],
					dcam_hw_ctx->frame_addr[i][DCAM_RECORD_PORT_IMG_FETCH]);
		}
		dcam_hw_ctx->err_count -= 1;
	}
}

struct dcam_irq_info dcam_int_mask_clr(uint32_t idx)
{
	struct dcam_irq_info irq_info = {0};

	irq_info.status = DCAM_REG_RD(idx, DCAM_INT0_MASK);
	irq_info.status1 = DCAM_REG_RD(idx, DCAM_INT1_MASK);
	DCAM_REG_WR(idx, DCAM_INT0_CLR, irq_info.status);
	DCAM_REG_WR(idx, DCAM_INT1_CLR, irq_info.status1);

	return irq_info;
}

struct dcam_irq_info dcam_int_isr_handle(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;
	struct dcam_irq_info irq_info = {0};

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	irq_info = dcam_hw_ctx->irq_ops.mask_clr(dcam_hw_ctx->hw_ctx_id);

	irq_info.irq_num = DCAM_IF_IRQ_INT0_NUMBER;
	irq_info.status &= DCAMINT_IRQ_LINE_INT0_MASK;
	irq_info.status1 &= DCAMINT_IRQ_LINE_INT1_MASK;
	dcam_int_common_record(dcam_hw_ctx->hw_ctx_id, &irq_info);

	if (irq_info.status & DCAMINT_INT0_FBC)
		dcamint_fbc_set(dcam_hw_ctx, irq_info.status);

	dcamint_dcam_dummy_proc(&irq_info, dcam_hw_ctx);
	/*
	 *bug 1651427
	 * When reset dcam appears on cap mipi overflow, clear the first frame.
	 * Need to be removed in AB chip
	 */
	if ((irq_info.status & BIT(DCAM_IF_IRQ_INT0_DCAM_OVF)) && DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_CAP_FRM_CLR) == 0){
		pr_debug("warning: to clean first frame DCAM%u 0x%x overflow\n", dcam_hw_ctx->hw_ctx_id, irq_info.status);
		irq_info.status &= (~BIT(DCAM_IF_IRQ_INT0_DCAM_OVF));
	}

	if (dcam_hw_ctx->is_offline_proc && (dcam_hw_ctx->is_3dnr || dcam_hw_ctx->is_pyr_rec))
		dcamint_offline_proc(dcam_hw_ctx, &irq_info);

	return irq_info;
}

void dcam_int_status_warning(void *param, uint32_t status, uint32_t status1)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)param;

	if (unlikely(status && status != BIT(DCAM_IF_IRQ_INT0_CAP_SOF)))
		pr_warn("warning: DCAM%u unhandled int0 bit0x%x\n", dcam_hw_ctx->hw_ctx_id, status);

	if (unlikely(status1))
		pr_warn("warning: DCAM%u unhandled int1 bit0x%x\n", dcam_hw_ctx->hw_ctx_id, status1);

	if (status & BIT(DCAM_IF_IRQ_INT0_CAP_SOF))
		(*dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[0])[DCAM_IF_IRQ_INT0_CAP_SOF](dcam_hw_ctx);
}


void dcam_int_irq_init(void *hw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)hw_ctx;
	if (dcam_hw_ctx->hw_ctx_id <= DCAM_HW_CONTEXT_1) {
		dcam_hw_ctx->irq_ops.mask_clr = dcam_int_mask_clr;
		dcam_hw_ctx->irq_ops.isr_handle = dcam_int_isr_handle;
		dcam_hw_ctx->irq_ops.status_warning = dcam_int_status_warning;
		dcam_hw_ctx->irq_ops.iommu_regs_dump = dcam_int_iommu_regs_dump;
		dcam_hw_ctx->irq_ops.error_bit.all_error = DCAMINT_INT0_ERROR;
		dcam_hw_ctx->irq_ops.error_bit.fatal_error = DCAMINT_INT0_FATAL_ERROR;
		dcam_hw_ctx->irq_ops.error_bit.frm_err = BIT(DCAM_IF_IRQ_INT0_CAP_FRM_ERR);
		dcam_hw_ctx->irq_ops.error_bit.mmu_int = BIT(DCAM_IF_IRQ_INT0_MMU_INT);
		dcam_hw_ctx->irq_ops.error_bit.line_err = BIT(DCAM_IF_IRQ_INT0_CAP_LINE_ERR);
		dcam_hw_ctx->irq_ops.error_bit.overflow = BIT(DCAM_IF_IRQ_INT0_DCAM_OVF);
		dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[1] = &_DCAM_ISRS1;
		dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[1] = &DCAM_SEQUENCES[dcam_hw_ctx->hw_ctx_id][1];
	} else {
		dcam_hw_ctx->irq_ops.mask_clr = dcamlite_int_mask_clr;
		dcam_hw_ctx->irq_ops.isr_handle = dcamlite_int_isr_handle;
		dcam_hw_ctx->irq_ops.status_warning = dcamlite_int_status_warning;
		dcam_hw_ctx->irq_ops.iommu_regs_dump = dcam_int_iommu_regs_dump;
		dcam_hw_ctx->irq_ops.error_bit.all_error = DCAMLITE_INT_ERROR;
		dcam_hw_ctx->irq_ops.error_bit.fatal_error = DCAMLITE_INT_FATAL_ERROR;
		dcam_hw_ctx->irq_ops.error_bit.frm_err = BIT(DCAM_LITE_IRQ_INT_CAP_FRM_ERR);
		dcam_hw_ctx->irq_ops.error_bit.mmu_int = BIT(DCAM_LITE_IRQ_INT_MMU_INT);
		dcam_hw_ctx->irq_ops.error_bit.line_err = BIT(DCAM_LITE_IRQ_INT_CAP_LINE_ERR);
		dcam_hw_ctx->irq_ops.error_bit.overflow = BIT(DCAM_LITE_IRQ_INT_DCAM_OVF);
		dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[1] = NULL;
		dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[1] = NULL;
	}
	dcam_hw_ctx->irq_ops._DCAM_ISR_IRQ[0] = _DCAM_ISR_IRQ[dcam_hw_ctx->hw_ctx_id];
	dcam_hw_ctx->irq_ops.DCAM_SEQUENCES[0] = &DCAM_SEQUENCES[dcam_hw_ctx->hw_ctx_id][0];
}

int dcam_int_irq_desc_get(uint32_t index, void *param)
{
	int ret = 0;
	struct dcam_irq_proc_desc *irq_desc = NULL;

	if (!param) {
		pr_err("fail to get valid param\n");
		return -EFAULT;
	}

	irq_desc = (struct dcam_irq_proc_desc *)param;
	switch (index) {
	case DCAM_IF_IRQ_INT0_RAW_PATH_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_DATA_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_RAW_OUT;
		break;
	case DCAM_IF_IRQ_INT0_CAPTURE_PATH_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_DATA_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_FULL_OUT;
		break;
	case DCAM_IF_IRQ_INT0_PREVIEW_PATH_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_DATA_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_BIN_OUT;
		break;
	case DCAM_IF_IRQ_INT0_AEM_PATH_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_STATIS_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_AEM_OUT;
		break;
	case DCAM_IF_IRQ_INT0_BAYER_HIST_PATH_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_STATIS_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_BAYER_HIST_OUT;
		break;
	case DCAM_IF_IRQ_INT0_LSCM_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_STATIS_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_LSCM_OUT;
		break;
	case DCAM_IF_IRQ_INT0_GTM_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_STATIS_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_GTM_HIST_OUT;
		break;
	default :
		pr_err("fail to support irq index BIT%d\n", index);
		ret = -1;
		break;
	}

	pr_debug("irq dcam port id %d\n", irq_desc->dcam_port_id);
	return ret;
}

