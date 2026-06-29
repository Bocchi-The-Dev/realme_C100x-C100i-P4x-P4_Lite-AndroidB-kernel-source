/*
 * Copyright (C) 2022-2023 UNISOC Communications Inc.
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

#ifdef DCAMINT_HW_ADPT_LAYER

static void dcamliteint_irq_cap_sof(void *param)
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

static const dcam_int_isr _DCAMLITE_ISRS[32] = {
	[DCAM_LITE_IRQ_INT_CAP_SOF] = dcamliteint_irq_cap_sof,
	[DCAM_LITE_IRQ_INT_FULL_PATH_TX_DONE] = dcam_int_common_full_port_done,
};

static const int _DCAMLITE_SEQUENCE[] = {
	DCAM_LITE_IRQ_INT_SENSOR_SOF,
	DCAM_LITE_IRQ_INT_CAP_SOF,/* must */
	DCAM_LITE_IRQ_INT_CAP_EOF,
	DCAM_LITE_IRQ_INT_SENSOR_EOF,
	DCAM_LITE_IRQ_INT_FULL_PATH_TX_DONE,/* for full path */
};

static struct dcam_irq_info dcamlite_int_mask_clr(uint32_t idx)
{
	struct dcam_irq_info irq_info = {0};

	irq_info.status = DCAM_REG_RD(idx, DCAM_LITE_INT_MASK);
	DCAM_REG_WR(idx, DCAM_LITE_INT_CLR, irq_info.status);
	return irq_info;
}

static struct dcam_irq_info dcamlite_int_isr_handle(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;
	struct dcam_irq_info irq_info = {0};

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	irq_info = dcam_hw_ctx->irq_ops.mask_clr(dcam_hw_ctx->hw_ctx_id);

	irq_info.irq_num = DCAM_IF_IRQ_INT0_NUMBER;
	irq_info.status &= DCAMINT_IRQ_LINE_INT0_MASK;
	irq_info.status1 &= DCAMINT_IRQ_LINE_INT1_MASK;
	dcam_int_common_record(dcam_hw_ctx->hw_ctx_id, &irq_info);

	return irq_info;
}

static void dcamlite_int_status_warning(void *param, uint32_t status, uint32_t status1)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	if (unlikely(status))
		pr_warn("warning: DCAM%u unhandled int 0x%x\n", dcam_hw_ctx->hw_ctx_id, status);
}
#endif
