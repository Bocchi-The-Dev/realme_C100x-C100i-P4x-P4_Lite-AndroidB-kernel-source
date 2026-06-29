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

#include "isp_int.h"
#include "isp_int_common.h"
#include "isp_reg.h"
#include "isp_fmcu.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_INT: %d %d %s : " fmt, current->pid, __LINE__, __func__

static const uint32_t isp_irq_process[] = {
	ISP_INT_SHADOW_DONE,
	ISP_INT_DISPATCH_DONE,
	ISP_INT_STORE_DONE_PRE,
	ISP_INT_STORE_DONE_VID,
	ISP_INT_NR3_ALL_DONE,
	ISP_INT_NR3_SHADOW_DONE,
	ISP_INT_STORE_DONE_THUMBNAIL,
	ISP_INT_RGB_LTMHISTS_DONE,
	ISP_INT_FMCU_LOAD_DONE,
	ISP_INT_FMCU_SHADOW_DONE,
	ISP_INT_ISP_ALL_DONE,
	ISP_INT_FMCU_STORE_DONE,
};

int ispint_err_pre_proc(enum isp_context_hw_id hw_idx, void *isp_handle, uint32_t irq_line)
{
	uint32_t idx = 0, cmd_num = 0;
	unsigned long addr = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	struct isp_fmcu_ctx_desc *fmcu_ctx = NULL;
	uint32_t isp_int_base[ISP_CONTEXT_HW_NUM] = {
		ISP_P0_INT_BASE, ISP_P1_INT_BASE,
		ISP_C0_INT_BASE, ISP_C1_INT_BASE,
	};

	dev = (struct isp_pipe_dev *)isp_handle;
	pctx_hw = &dev->hw_ctx[hw_idx];

	fmcu_ctx = (struct isp_fmcu_ctx_desc *)pctx_hw->fmcu_handle;
	idx = pctx_hw->cfg_id;
	if (idx >= ISP_CONTEXT_SW_NUM) {
		pr_err("fail to get sw_id:%d for hw_idx=%d\n", idx, hw_idx);
		return 0;
	}

	pr_info("isp hw %d, cfg id:%d\n", hw_idx, pctx_hw->cfg_id);
	pr_info("ISP: INT Register list\n");
	for (addr = isp_int_base[hw_idx]; addr <= isp_int_base[hw_idx] + 0x3C; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
	}
	pr_info("ISP: Common Register list\n");
	for (addr = 0x700; addr <= 0xA2C; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
	}

	pr_info("ISP: pyr_rec Register list\n");
	for (addr = 0x9310; addr <= 0x9334; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
	}
	for (addr = 0x9510; addr <= 0x9534; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
	}
	for (addr = 0x9610; addr <= 0x963C; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
	}
	for (addr = 0x9B10; addr <= 0x9B14; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
	}

	pr_info("ISP fetch: register list\n");
	for (addr = (ISP_FETCH_BASE + ISP_FETCH_PARAM0); addr <= (ISP_FETCH_BASE + ISP_FETCH_MIPI_PARAM_UV); addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));

		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	pr_info("ISP fbd fetch:reg list\n");
	for (addr = (ISP_YUV_AFBD_FETCH_BASE + ISP_AFBD_FETCH_SEL); addr <= (ISP_YUV_AFBD_FETCH_BASE + ISP_AFBD_FETCH_PARAM2); addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));

		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	pr_info("ISP dispatch: register list\n");
	for (addr = (ISP_DISPATCH_BASE + ISP_DISPATCH_CH0_BAYER); addr <= (ISP_DISPATCH_BASE + ISP_DISPATCH_CHK_SUM);
		addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	pr_info("ISP axi: register list\n");
	for (addr = 0x510; addr <= 0x55C; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
	}

	for (addr = (ISP_SCALER_PRE_CAP_BASE + ISP_SCALER_CFG); addr <= (ISP_SCALER_PRE_CAP_BASE + ISP_SCALER_RES);
			addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	for (addr = (ISP_SCALER_VID_BASE + ISP_SCALER_CFG); addr <= (ISP_SCALER_VID_BASE + ISP_SCALER_RES);
		addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	pr_info("ISP store: register list\n");
	for (addr = (ISP_STORE_PRE_CAP_BASE + ISP_STORE_PARAM); addr <= (ISP_STORE_PRE_CAP_BASE + ISP_STORE_SHADOW_CLR);
			addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	pr_info("ISP store: register list\n");
	for (addr = (ISP_STORE_VID_BASE + ISP_STORE_PARAM); addr <= (ISP_STORE_VID_BASE + ISP_STORE_SHADOW_CLR);
			addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	pr_info("ISP 3dnr: register list\n");
	for (addr = 0x2010; addr <= 0x204C; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	pr_info("ISP 3dnr store: register list\n");
	for (addr = 0x2210; addr <= 0x223C; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	pr_info("ISP 3dnr crop: register list\n");
	for (addr = 0x2310; addr <= 0x231C; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(idx, addr),
			ISP_REG_RD(idx, addr + 4),
			ISP_REG_RD(idx, addr + 8),
			ISP_REG_RD(idx, addr + 12));
	}

	if ((irq_line & (1 << ISP_INT_FMCU_CMD_ERROR)) && fmcu_ctx) {
		addr = (unsigned long)fmcu_ctx->cmd_buf[!fmcu_ctx->cur_buf_id];
		cmd_num = (uint32_t)fmcu_ctx->cmdq_pos[!fmcu_ctx->cur_buf_id] / 2;
		pr_info("cmd num %d\n", cmd_num);
		for (idx = 0; idx <= cmd_num; idx += 2) {
			pr_info("a:0x%08x c: 0x%08x | a:0x%08x c: 0x%08x\n",
				*(uint32_t *)(addr + 4),
				*(uint32_t *)(addr),
				*(uint32_t *)(addr + 12),
				*(uint32_t *)(addr + 8));
			addr += 16;
		}
	}

	return 0;
}

void ispint_iommu_regs_dump(void)
{
	uint32_t reg = 0;
	uint32_t val[4];

	pr_err("fail to handle, isp mmu int sts:%d", ISP_MMU_RD(ISP_MMU_INT_STS));
	for (reg = ISP_MMU_VERSION; reg <= ISP_MMU_INT_RAW; reg += 16) {
		val[0] = ISP_MMU_RD(reg);
		val[1] = ISP_MMU_RD(reg + 4);
		val[2] = ISP_MMU_RD(reg + 8);
		val[3] = ISP_MMU_RD(reg + 12);
		pr_err("fail to handle, offset=0x%04x: %08x %08x %08x %08x\n",
			reg, val[0], val[1], val[2], val[3]);
	}

	pr_err("fail to handle, fetch y %08x u %08x pyr_rec_fetch y %08x u %08x pyr_dec_fetch y %08x u %08x\n",
		ISP_HREG_RD(ISP_FETCH_BASE + ISP_FETCH_SLICE_Y_ADDR),
		ISP_HREG_RD(ISP_FETCH_BASE + ISP_FETCH_SLICE_U_ADDR),
		ISP_HREG_RD(PYR_REC_CUR_FETCH_BASE + ISP_FETCH_SLICE_Y_ADDR),
		ISP_HREG_RD(PYR_REC_CUR_FETCH_BASE + ISP_FETCH_SLICE_U_ADDR),
		ISP_HREG_RD(PYR_DEC_FETCH_BASE + ISP_FETCH_SLICE_Y_ADDR),
		ISP_HREG_RD(PYR_DEC_FETCH_BASE + ISP_FETCH_SLICE_U_ADDR));

	pr_err("fail to handle, afbd head normal/rec frame %08x slice %08x dec frame %08x slice %08x\n",
		ISP_HREG_RD(ISP_YUV_AFBD_FETCH_BASE + ISP_AFBD_FETCH_PARAM1),
		ISP_HREG_RD(ISP_YUV_AFBD_FETCH_BASE + ISP_AFBD_FETCH_PARAM2),
		ISP_HREG_RD(ISP_YUV_DEC_AFBD_FETCH_BASE + ISP_AFBD_FETCH_PARAM1),
		ISP_HREG_RD(ISP_YUV_DEC_AFBD_FETCH_BASE + ISP_AFBD_FETCH_PARAM2));

	pr_err("fail to handle, store pre cap y %08x u %08x afbc head y %08x y %08x offset %08x\n",
		ISP_HREG_RD(ISP_STORE_PRE_CAP_BASE + ISP_STORE_SLICE_Y_ADDR),
		ISP_HREG_RD(ISP_STORE_PRE_CAP_BASE + ISP_STORE_SLICE_U_ADDR),
		ISP_HREG_RD(ISP_CAP_FBC_STORE_BASE + ISP_FBC_STORE_SLICE_HEADER_BASE_ADDR),
		ISP_HREG_RD(ISP_CAP_FBC_STORE_BASE + ISP_FBC_STORE_SLICE_PAYLOAD_BASE_ADDR),
		ISP_HREG_RD(ISP_CAP_FBC_STORE_BASE + ISP_FBC_STORE_SLICE_PAYLOAD_OFFSET_ADDR));

	pr_err("fail to handle, store vid y %08x u %08x fbc head y %08x y %08x offset %08x\n",
		ISP_HREG_RD(ISP_STORE_VID_BASE + ISP_STORE_SLICE_Y_ADDR),
		ISP_HREG_RD(ISP_STORE_VID_BASE + ISP_STORE_SLICE_U_ADDR),
		ISP_HREG_RD(ISP_VID_FBC_STORE_BASE  + ISP_FBC_STORE_SLICE_HEADER_BASE_ADDR),
		ISP_HREG_RD(ISP_VID_FBC_STORE_BASE  + ISP_FBC_STORE_SLICE_PAYLOAD_BASE_ADDR),
		ISP_HREG_RD(ISP_VID_FBC_STORE_BASE  + ISP_FBC_STORE_SLICE_PAYLOAD_OFFSET_ADDR));

	pr_err("fail to handle, store thumb y %08x u %08x\n",
		ISP_HREG_RD(ISP_STORE_THUMB_BASE + ISP_STORE_SLICE_Y_ADDR),
		ISP_HREG_RD(ISP_STORE_THUMB_BASE + ISP_STORE_SLICE_U_ADDR));
}

isp_int_isr isp_isr_handler[32] = {
	[ISP_INT_ISP_ALL_DONE] = isp_int_common_all_done,
	[ISP_INT_SHADOW_DONE] = isp_int_common_shadow_done,
	[ISP_INT_DISPATCH_DONE] = isp_int_common_dispatch_done,
	[ISP_INT_STORE_DONE_PRE] = isp_int_common_pre_store_done,
	[ISP_INT_STORE_DONE_VID] = isp_int_common_vid_store_done,
	[ISP_INT_NR3_ALL_DONE] = isp_int_common_3dnr_all_done,
	[ISP_INT_NR3_SHADOW_DONE] = isp_int_common_3dnr_shadow_done,
	[ISP_INT_STORE_DONE_THUMBNAIL] = isp_int_common_thumb_store_done,
	[ISP_INT_FMCU_LOAD_DONE] = isp_int_common_fmcu_load_done,
	[ISP_INT_FMCU_SHADOW_DONE] = isp_int_common_fmcu_shadow_done,
	[ISP_INT_FMCU_STORE_DONE] = isp_int_common_fmcu_store_done,
};

struct isp_int_ctxs_com isp_int_ctxs[4] = {
	{ /* P0 */
		ISP_P0_INT_BASE,
		ISP_INT_LINE_MASK_ERR | ISP_INT_LINE_MASK_MMU,
		(uint32_t)ARRAY_SIZE(isp_irq_process),
		isp_irq_process,
	},
	{ /* P1 */
		ISP_P1_INT_BASE,
		ISP_INT_LINE_MASK_ERR | ISP_INT_LINE_MASK_MMU,
		(uint32_t)ARRAY_SIZE(isp_irq_process),
		isp_irq_process,
	},
	{ /* C0 */
		ISP_C0_INT_BASE,
		ISP_INT_LINE_MASK_ERR | ISP_INT_LINE_MASK_MMU,
		(uint32_t)ARRAY_SIZE(isp_irq_process),
		isp_irq_process,
	},
	{ /* C1 */
		ISP_C1_INT_BASE,
		ISP_INT_LINE_MASK_ERR | ISP_INT_LINE_MASK_MMU,
		(uint32_t)ARRAY_SIZE(isp_irq_process),
		isp_irq_process,
	},
};

struct isp_int_ctxs_com isp_int_reg_handle(int c_id)
{
	struct isp_int_ctxs_com ctxs_com = {0};

	ctxs_com = isp_int_ctxs[c_id];
	ctxs_com.irq_line = ISP_HREG_RD(ctxs_com.irq_offset + ISP_INT_INT0);
	ctxs_com.irq_line1 = ISP_HREG_RD(ctxs_com.irq_offset + ISP_INT_INT1);
	ISP_HREG_WR(ctxs_com.irq_offset + ISP_INT_CLR0, ctxs_com.irq_line);
	ISP_HREG_WR(ctxs_com.irq_offset + ISP_INT_CLR1, ctxs_com.irq_line1);

	ctxs_com.isp_isr_handler = isp_isr_handler;

	return ctxs_com;
}

uint32_t isp_int_get_mmu_irq_line(struct isp_int_ctxs_com ctxs_com)
{
	ctxs_com.mmu_irq_line = ISP_MMU_RD(ISP_MMU_INT_STS);
	return ctxs_com.mmu_irq_line;
}

