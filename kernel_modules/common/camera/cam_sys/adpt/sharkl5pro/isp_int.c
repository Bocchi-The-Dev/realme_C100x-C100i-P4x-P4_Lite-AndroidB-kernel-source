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
	ISP_INT_HIST_CAL_DONE,
	ISP_INT_ISP_ALL_DONE,
	ISP_INT_FMCU_STORE_DONE,
};

int ispint_err_pre_proc(enum isp_context_hw_id hw_idx, void *isp_handle, uint32_t irq_line)
{
	unsigned long addr = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;

	dev = (struct isp_pipe_dev *)isp_handle;
	pctx_hw = &dev->hw_ctx[hw_idx];

	pr_info("isp hw %d, cfg id:%d\n", hw_idx, pctx_hw->cfg_id);
	pr_info("ISP: sw cfg Register list\n");
	/* TBD: should add necessary cfg register print */
	for (addr = 0xc010; addr <= 0xc150; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_REG_RD(pctx_hw->cfg_id, addr),
			ISP_REG_RD(pctx_hw->cfg_id, addr + 4),
			ISP_REG_RD(pctx_hw->cfg_id, addr + 8),
			ISP_REG_RD(pctx_hw->cfg_id, addr + 12));
	}

	pr_info("ISP: hw Register list\n");
	/* TBD: should add necessary hw register print */
	for (addr = 0xc010; addr <= 0xc150; addr += 16) {
		pr_info("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			addr,
			ISP_HREG_RD(addr),
			ISP_HREG_RD(addr + 4),
			ISP_HREG_RD(addr + 8),
			ISP_HREG_RD(addr + 12));
	}

	return 0;
}

void ispint_iommu_regs_dump(void)
{
	uint32_t reg = 0;
	uint32_t val[4];

	for (reg = ISP_MMU_VERSION; reg <= ISP_MMU_INT_RAW; reg += 16) {
		val[0] = ISP_MMU_RD(reg);
		val[1] = ISP_MMU_RD(reg + 4);
		val[2] = ISP_MMU_RD(reg + 8);
		val[3] = ISP_MMU_RD(reg + 12);
		pr_err("fail to handle, offset=0x%04x: %08x %08x %08x %08x\n",
			reg, val[0], val[1], val[2], val[3]);
	}

	pr_err("fail to handle, fetch y %08x u %08x v %08x \n",
		ISP_HREG_RD(ISP_FETCH_SLICE_Y_ADDR),
		ISP_HREG_RD(ISP_FETCH_SLICE_U_ADDR),
		ISP_HREG_RD(ISP_FETCH_SLICE_V_ADDR));

	pr_err("fail to handle, fetch 3dnr y %08x uv %08x\n",
		ISP_HREG_RD(ISP_3DNR_MEM_CTRL_FT_CUR_LUMA_ADDR),
		ISP_HREG_RD(ISP_3DNR_MEM_CTRL_FT_CUR_CHROMA_ADDR));

	pr_err("fail to handle, store pre cap y %08x u %08x v %08x \n",
		ISP_HREG_RD(ISP_STORE_PRE_CAP_BASE + ISP_STORE_SLICE_Y_ADDR),
		ISP_HREG_RD(ISP_STORE_PRE_CAP_BASE + ISP_STORE_SLICE_U_ADDR),
		ISP_HREG_RD(ISP_STORE_PRE_CAP_BASE + ISP_STORE_SLICE_V_ADDR));

	pr_err("fail to handle, store vid y %08x u %08x v %08x\n",
		ISP_HREG_RD(ISP_STORE_VID_BASE + ISP_STORE_SLICE_Y_ADDR),
		ISP_HREG_RD(ISP_STORE_VID_BASE + ISP_STORE_SLICE_U_ADDR),
		ISP_HREG_RD(ISP_STORE_VID_BASE + ISP_STORE_SLICE_V_ADDR));

	pr_err("fail to handle, store thumb y %08x u %08x v %08x\n",
		ISP_HREG_RD(ISP_STORE_THUMB_BASE + ISP_STORE_SLICE_Y_ADDR),
		ISP_HREG_RD(ISP_STORE_THUMB_BASE + ISP_STORE_SLICE_U_ADDR),
		ISP_HREG_RD(ISP_STORE_THUMB_BASE + ISP_STORE_SLICE_V_ADDR));
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
	[ISP_INT_HIST_CAL_DONE] = isp_int_common_hist2_cal_done,
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
	ISP_HREG_WR(ctxs_com.irq_offset + ISP_INT_CLR0, ctxs_com.irq_line);

	ctxs_com.isp_isr_handler = isp_isr_handler;

	return ctxs_com;
}

uint32_t isp_int_get_mmu_irq_line(struct isp_int_ctxs_com ctxs_com)
{
	ctxs_com.mmu_irq_line = ISP_MMU_RD(ISP_MMU_INT_STS);
	return ctxs_com.mmu_irq_line;
}

