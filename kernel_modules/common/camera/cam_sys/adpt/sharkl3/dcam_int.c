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

#include "dcam_core.h"
#include "dcam_int_common.h"
#include "dcam_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_INT: %d %d %s : " fmt, current->pid, __LINE__, __func__

static const char *_DCAM_ADDR_NAME[DCAM_RECORD_PORT_INFO_MAX] = {
	[DCAM_RECORD_PORT_FULL] = "full",
	[DCAM_RECORD_PORT_BIN] = "bin",
	[DCAM_RECORD_PORT_BIN_1] = "bin1",
	[DCAM_RECORD_PORT_BIN_2] = "bin2",
	[DCAM_RECORD_PORT_BIN_3] = "bin3",
	[DCAM_RECORD_PORT_PDAF] = "pdaf",
	[DCAM_RECORD_PORT_VCH2] = "vch2",
	[DCAM_RECORD_PORT_VCH3] = "vch3",
	[DCAM_RECORD_PORT_LENS] = "lens",
	[DCAM_RECORD_PORT_AEM] = "aem",
	[DCAM_RECORD_PORT_AFL_GLB] = "afl_glb",
	[DCAM_RECORD_PORT_AFL_REGION] = "afl_region",
	[DCAM_RECORD_PORT_BPC_MAP] = "bpc_map",
	[DCAM_RECORD_PORT_BPC_OUT] = "bpc_out",
	[DCAM_RECORD_PORT_AFM] = "afm",
	[DCAM_RECORD_PORT_NR3] = "nr3",
};

static const char *dcamint_addr_name_get(uint32_t type)
{
	return (type < DCAM_RECORD_PORT_IMG_FETCH) ? _DCAM_ADDR_NAME[type] : "(null)";
}

static void dcamint_irq_cap_sof(void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_irq_proc irq_proc = {0};

	if (!param || dcam_hw_ctx->dcam_irq_cb_func == NULL) {
		pr_err("fail to dcamonline callback fun is NULL\n");
		return;
	}

	DCAM_MMU_MWR(MMU_UPDATE, BIT_0, 0x1);
	irq_proc.of = CAP_START_OF_FRAME;
	irq_proc.frm_cnt = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_CAP_FRM_CLR) & 0x3F;
	irq_proc.bin_addr_value = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_BIN_BASE_WADDR0);
	irq_proc.full_addr_value = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_FULL_BASE_WADDR);
	irq_proc.handled_bits = DCAMINT_ALL_TX_DONE;
	dcam_hw_ctx->dcam_irq_cb_func(&irq_proc, dcam_hw_ctx->dcam_irq_cb_handle);
}

static const dcam_int_isr _DCAM0_ISR_IRQ[32] = {
	[DCAM_SENSOR_SOF] = dcam_int_common_irq_sensor_sof,
	[DCAM_SENSOR_EOF] = dcam_int_common_irq_sensor_eof,
	[DCAM_CAP_SOF] = dcamint_irq_cap_sof,
	[DCAM_PREVIEW_SOF] = dcam_int_common_irq_preview_sof,
	[DCAM_FULL_PATH_TX_DONE] = dcam_int_common_full_port_done,
	[DCAM_PREV_PATH_TX_DONE] = dcam_int_common_bin_port_done,
	[DCAM_PDAF_PATH_TX_DONE] = dcam_int_common_pdaf_port_done,
	[DCAM_VCH2_PATH_TX_DONE] = dcam_int_common_vch2_port_done,
	[DCAM_VCH3_PATH_TX_DONE] = dcam_int_common_vch3_port_done,
	[DCAM_AEM_TX_DONE] = dcam_int_common_aem_port_done,
	[DCAM_AFL_TX_DONE] = dcam_int_common_afl_port_done,
	[DCAM_AFM_INTREQ1] = dcam_int_common_afm_port_done,
	[DCAM_NR3_TX_DONE] = dcam_int_common_nr3_port_done,
};

static dcam_int_isr_array _DCAM_ISR_IRQ[DCAM_HW_CONTEXT_MAX] = {
	[DCAM_HW_CONTEXT_0] = &_DCAM0_ISR_IRQ,
	[DCAM_HW_CONTEXT_1] = &_DCAM0_ISR_IRQ,
};

static const int _DCAM0_SEQUENCE[] = {
	DCAM_CAP_SOF,
	DCAM_SENSOR_SOF,
	DCAM_PREVIEW_SOF,
	DCAM_SENSOR_EOF,
	DCAM_NR3_TX_DONE,
	DCAM_PREV_PATH_TX_DONE,
	DCAM_FULL_PATH_TX_DONE,
	DCAM_AEM_TX_DONE,
	/* AFM INT0 is some tile done, INT1 is all tile done,
	 * INT0 can use to triger AF ALG eaglier, but the specific time
	 * is not sure, so INT1 is suggested to use now.
	 */
	DCAM_AFM_INTREQ1,
	DCAM_AFL_TX_DONE,
	DCAM_PDAF_PATH_TX_DONE,
	DCAM_VCH2_PATH_TX_DONE,
	DCAM_VCH3_PATH_TX_DONE,
};

static const int _DCAM1_SEQUENCE[] = {
	DCAM_CAP_SOF,
	DCAM_SENSOR_EOF,
	DCAM_NR3_TX_DONE,
	DCAM_PREV_PATH_TX_DONE,
	DCAM_FULL_PATH_TX_DONE,
	DCAM_AEM_TX_DONE,
	/* AFM INT0 is some tile done, INT1 is all tile done,
	 * INT0 can use to triger AF ALG eaglier, but the specific time
	 * is not sure, so INT1 is suggested to use now.
	 */
	DCAM_AFM_INTREQ1,
	DCAM_AFL_TX_DONE,
	DCAM_PDAF_PATH_TX_DONE,
	DCAM_VCH2_PATH_TX_DONE,
	DCAM_VCH3_PATH_TX_DONE,
};

static const struct dcam_sequences DCAM_SEQUENCES[DCAM_HW_CONTEXT_MAX][1] = {
	{
		{
			ARRAY_SIZE(_DCAM0_SEQUENCE),
			_DCAM0_SEQUENCE,
		},
	},
	{
		{
			ARRAY_SIZE(_DCAM1_SEQUENCE),
			_DCAM1_SEQUENCE,
		},
	},
};

struct nr3_done dcam_int_nr3_done_rd(void *param, uint32_t idx)
{
	struct nr3_done com = {0};
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	com.p = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, NR3_FAST_ME_PARAM0);
	com.out0 = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, NR3_FAST_ME_OUT0);
	com.out1 = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, NR3_FAST_ME_OUT1);
	dcam_hw_ctx->nr3_mv_ctrl[idx].project_mode = com.p & 0x3;

	return com;
}

void dcam_int_iommu_regs_dump(void *param)
{
	uint32_t i = 0, j = 0, addr = 0;
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	if (!param) {
		pr_err("fail to get valid input hw_ctx\n");
		return;
	}

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	if (dcam_hw_ctx->err_count) {
		if (dcam_hw_ctx->is_offline_proc)
			pr_err("fail to handle offline dcam frame_cnt=%d, MMU_INT_STS: %08x\n",
				dcam_hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM],
				DCAM_MMU_RD(MMU_STS));
		else
			pr_err("fail to handle fid=%d last_frame_cnt=%d, MMU_INT_STS: %08x\n",
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_CAP_FRM_CLR) & 0x3F,
				dcam_hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM],
				DCAM_MMU_RD(MMU_STS));

		if (DCAM_MMU_RD(MMU_STS) & BIT_0)
			pr_err("fail to handle, MMU_UNS_ADDR_WR: %08x\n", DCAM_MMU_RD(MMU_UNS_ADDR_WR));
		if (DCAM_MMU_RD(MMU_STS) & BIT_1)
			pr_err("fail to handle, MMU_UNS_ADDR_RD: %08x\n", DCAM_MMU_RD(MMU_UNS_ADDR_RD));
		if (DCAM_MMU_RD(MMU_STS) & BIT_2)
			pr_err("fail to handle, MMU_INV_ADDR_WR: %08x\n", DCAM_MMU_RD(MMU_INV_ADDR_WR));
		if (DCAM_MMU_RD(MMU_STS) & BIT_3)
			pr_err("fail to handle, MMU_INV_ADDR_RD: %08x\n", DCAM_MMU_RD(MMU_INV_ADDR_RD));
		if (DCAM_MMU_RD(MMU_STS) & BIT_4)
			pr_err("fail to handle, MMU_VAOR_ADDR_WR: %08x\n", DCAM_MMU_RD(MMU_VAOR_ADDR_WR));
		if (DCAM_MMU_RD(MMU_STS) & BIT_5)
			pr_err("fail to handle, MMU_VAOR_ADDR_RD: %08x\n", DCAM_MMU_RD(MMU_VAOR_ADDR_RD));
		if (DCAM_MMU_RD(MMU_STS) & BIT_6)
			pr_err("fail to handle, MMU_VPN_PAOR_WR: %08x, MMU_PPN_PAOR_WR: %08x\n",
				DCAM_MMU_RD(MMU_VPN_PAOR_WR), DCAM_MMU_RD(MMU_PPN_PAOR_WR));
		if (DCAM_MMU_RD(MMU_STS) & BIT_7)
			pr_err("fail to handle, MMU_VPN_PAOR_RD: %08x, MMU_PPN_PAOR_RD: %08x\n",
				DCAM_MMU_RD(MMU_VPN_PAOR_RD), DCAM_MMU_RD(MMU_PPN_PAOR_RD));
		if (DCAM_MMU_RD(MMU_STS) == 0) {
			for (addr = MMU_EN; addr <= MMU_PPN_RANGE2_SHAD; addr += 16)
				pr_info("addr 0x%x: 0x%x 0x%x 0x%x 0x%x\n", addr,
					DCAM_MMU_RD(addr), DCAM_MMU_RD(addr + 4),
					DCAM_MMU_RD(addr + 8), DCAM_MMU_RD(addr + 12));
		}

		for (i = 0; i < DCAM_ADDR_RECORD_FRAME_NUM; i++) {
			for (j = DCAM_RECORD_PORT_FULL; j < DCAM_RECORD_PORT_NR3; j += 5) {
				pr_err("fail to handle used frame_cnt%d, %s: %08x, %s: %08x, %s: %08x,"
					" %s: %08x, %s: %08x\n", dcam_hw_ctx->frame_addr[i][DCAM_RECORD_FRAME_CUR_COUNT],
					dcamint_addr_name_get(j), dcam_hw_ctx->frame_addr[i][j],
					dcamint_addr_name_get(j + 1), dcam_hw_ctx->frame_addr[i][j + 1],
					dcamint_addr_name_get(j + 2), dcam_hw_ctx->frame_addr[i][j + 2],
					dcamint_addr_name_get(j + 3), dcam_hw_ctx->frame_addr[i][j + 3],
					dcamint_addr_name_get(j + 4), dcam_hw_ctx->frame_addr[i][j + 4]);
			}
			pr_err("fail to handle used frame_cnt%d, %s: %08x\n", dcam_hw_ctx->frame_addr[i][DCAM_RECORD_FRAME_CUR_COUNT],
				dcamint_addr_name_get(j), dcam_hw_ctx->frame_addr[i][j]);
			if (dcam_hw_ctx->is_offline_proc) {
				pr_err("fail to handle used frame_cnt%d, offline fetch %08x\n",
					dcam_hw_ctx->frame_addr[i][DCAM_RECORD_FRAME_CUR_COUNT],
					dcam_hw_ctx->frame_addr[i][DCAM_RECORD_PORT_IMG_FETCH]);
				break;
			}
		}
		dcam_hw_ctx->err_count -= 1;
	}
}

struct dcam_irq_info dcam_int_mask_clr(uint32_t idx)
{
	struct dcam_irq_info irq_info = {0};

	irq_info.status = DCAM_REG_RD(idx, DCAM_INT_MASK);
	DCAM_REG_WR(idx, DCAM_INT_CLR, irq_info.status);

	return irq_info;
}

struct dcam_irq_info dcam_int_isr_handle(void *param)
{
	struct dcam_irq_info irq_info = {0};
	uint32_t line_mask = 0;
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	line_mask = DCAMINT_IRQ_LINE_MASK;

	irq_info = dcam_hw_ctx->irq_ops.mask_clr(dcam_hw_ctx->hw_ctx_id);
	irq_info.status &= line_mask;
	irq_info.irq_num = DCAM_IRQ_NUMBER;
	dcam_int_common_record(dcam_hw_ctx->hw_ctx_id, &irq_info);

	return irq_info;
}

void dcam_int_status_warning(void *param, uint32_t status, uint32_t status1)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	if (unlikely(status))
		pr_warn("warning: DCAM%u unhandled int 0x%x\n", dcam_hw_ctx->hw_ctx_id, status);
}

void dcam_int_irq_init(void *hw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	dcam_hw_ctx = (struct dcam_hw_context *)hw_ctx;
	dcam_hw_ctx->irq_ops.mask_clr = dcam_int_mask_clr;
	dcam_hw_ctx->irq_ops.isr_handle = dcam_int_isr_handle;
	dcam_hw_ctx->irq_ops.status_warning = dcam_int_status_warning;
	dcam_hw_ctx->irq_ops.iommu_regs_dump = dcam_int_iommu_regs_dump;
	dcam_hw_ctx->irq_ops.error_bit.all_error = DCAMINT_ALL_ERROR;
	dcam_hw_ctx->irq_ops.error_bit.fatal_error = DCAMINT_FATAL_ERROR;
	dcam_hw_ctx->irq_ops.error_bit.frm_err = BIT(DCAM_CAP_FRM_ERR);
	dcam_hw_ctx->irq_ops.error_bit.mmu_int = BIT(DCAM_MMU_INT);
	dcam_hw_ctx->irq_ops.error_bit.line_err = BIT(DCAM_CAP_LINE_ERR);
	dcam_hw_ctx->irq_ops.error_bit.overflow = BIT(DCAM_DCAM_OVF);
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
	case DCAM_FULL_PATH_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_DATA_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_FULL_OUT;
		break;
	case DCAM_PREV_PATH_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_DATA_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_BIN_OUT;
		break;
	case DCAM_AEM_TX_DONE:
		irq_desc->dcam_cb_type = CAM_CB_DCAM_STATIS_DONE;
		irq_desc->dcam_port_id = PORT_OFFLINE_AEM_OUT;
		break;
	default :
		pr_warn("Warning: not to support irq index BIT%d\n", index);
		ret = -1;
		break;
	}

	pr_debug("irq dcam port id %d\n", irq_desc->dcam_port_id);
	return ret;
}

