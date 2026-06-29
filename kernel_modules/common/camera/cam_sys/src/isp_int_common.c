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
#define pr_fmt(fmt) "ISP_INT_COMMON: %d %d %s : " fmt, current->pid, __LINE__, __func__

//#define ISP_INT_RECORD 1
#ifdef ISP_INT_RECORD
#define INT_RCD_SIZE 0x10000
static uint32_t isp_int_recorder[ISP_CONTEXT_HW_NUM][32][INT_RCD_SIZE];
static uint32_t int_index[ISP_CONTEXT_HW_NUM][32];
#endif

uint32_t irq_done[ISP_CONTEXT_HW_NUM][32];
uint32_t irq_done_sw[ISP_CONTEXT_SW_NUM][32];

char *isp_dev_name[] = {"isp0", "isp1"};

static int ispint_common_error_done(
	enum isp_context_hw_id hw_idx, void *isp_handle, uint32_t is_reset, uint32_t irq_line)
{
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	int ret = 0;

	dev = (struct isp_pipe_dev *)isp_handle;
	pctx_hw = &dev->hw_ctx[hw_idx];

	pr_debug("cxt_id:%d ispint error done.\n", hw_idx);
	if (pctx_hw->postproc_func)
		pctx_hw->postproc_func(dev, hw_idx, POSTPROC_FRAME_ERROR_DONE, &is_reset);

	if (dev->sec_mode == SEC_UNABLE)
		ret = ispint_err_pre_proc(hw_idx, dev, irq_line);
	return ret;
}

static int ispintcommon_isr_root_unlikely(int c_id, struct isp_int_ctxs_com ctxs_com, void *priv)
{
	uint32_t val = 0;
	struct isp_pipe_dev *isp_handle = (struct isp_pipe_dev *)priv;
	struct isp_hw_context *pctx_hw = NULL;
	enum cam_en_status is_reset;

	pctx_hw = &isp_handle->hw_ctx[c_id];
	if (unlikely(ctxs_com.err_mask & ctxs_com.irq_line)) {
		pr_err("fail to get normal status %x\n", ctxs_com.irq_line);
		if (ctxs_com.irq_line & ISP_INT_LINE_MASK_MMU) {
			val = isp_int_get_mmu_irq_line(ctxs_com);

			if (val != pctx_hw->iommu_status) {
				pctx_hw->iommu_status = val;
				ispint_iommu_regs_dump();
			}
		}
		is_reset = (ctxs_com.irq_line & ISP_INT_FALAT_ERR_MASK) ? CAM_ENABLE : CAM_DISABLE;
		/*handle the error here*/
		if (ispint_common_error_done(c_id, isp_handle, is_reset, ctxs_com.irq_line)) {
			pr_err("fail to handle the error here c_id %d irq_line 0x%x\n", c_id, ctxs_com.irq_line);
			return IRQ_HANDLED;
		}
	}
	return 0;
}

static void ispintcommon_isr_root_handle(int c_id, struct isp_int_ctxs_com ctxs_com, void *priv)
{
	uint32_t k = 0;
	struct isp_pipe_dev *isp_handle = (struct isp_pipe_dev *)priv;

	for (k = 0; k < ctxs_com.irq_numbers; k++) {
		uint32_t irq_id = ctxs_com.irq_vect[k];

		if (ctxs_com.irq_line & (1 << irq_id)) {
			if (ctxs_com.isp_isr_handler[irq_id]) {
				ctxs_com.isp_isr_handler[irq_id](c_id, isp_handle);
			}
		}
		ctxs_com.irq_line &= ~(1 << irq_id);
		if (!ctxs_com.irq_line)
			break;
	}
}

static inline void ispintcommon_record(uint32_t cfg_id, enum isp_context_hw_id c_id, uint32_t irq_line)
{
	uint32_t k;

	for (k = 0; k < 32; k++) {
		if (irq_line & (1 << k))
			irq_done[c_id][k]++;
	}

	for (k = 0; k < 32; k++) {
		if (irq_line & (1 << k))
			irq_done_sw[cfg_id][k]++;
	}

#ifdef ISP_INT_RECORD
	{
		uint32_t cnt = 0, time = 0, int_no = 0;
		struct timespec cur_ts = {0};

		os_adapt_time_get_ts(&cur_ts);
		time = (uint32_t)(cur_ts.tv_sec & 0xffff);
		time <<= 16;
		time |= (uint32_t)((cur_ts.tv_nsec / (NSEC_PER_USEC * 100)) & 0xffff);
		for (int_no = 0; int_no < 32; int_no++) {
			if (irq_line & BIT(int_no)) {
				cnt = int_index[c_id][int_no];
				isp_int_recorder[c_id][int_no][cnt] = time;
				cnt++;
				int_index[c_id][int_no] = (cnt & (INT_RCD_SIZE - 1));
			}
		}
	}
#endif
}

static irqreturn_t ispintcommon_isr_root(int irq, void *priv)
{
	int ret = 0;
	uint32_t iid = 0, sid = 0;
	enum isp_context_hw_id pctx_hw_id;
	struct isp_pipe_dev *isp_handle = (struct isp_pipe_dev *)priv;
	struct isp_int_ctxs_com ctxs_com = {0};
	struct isp_hw_context *hw_ctx = NULL;

	if (!isp_handle) {
		pr_err("fail to get valid dev\n");
		return IRQ_HANDLED;
	}
	if (atomic_read(&isp_handle->enable) == 0) {
		pr_err("fail to get isp_handle enable\n");
		return IRQ_HANDLED;
	}
	if (irq == isp_handle->irq_no[0]) {
		iid = 0;
	} else if (irq == isp_handle->irq_no[1]) {
		iid = 1;
	} else {
		pr_err("fail to get irq %d mismatched\n", irq);
		return IRQ_NONE;
	}
	pr_debug("isp irq %d, priv %p, iid %d\n", irq, priv, iid);
	for (sid = 0; sid < 2; sid++) {
		pctx_hw_id = (sid << 1) | iid;

		/* read and clear the interrupt*/
		ctxs_com = isp_int_reg_handle(pctx_hw_id);

		if (unlikely((ctxs_com.irq_line == 0) && (ctxs_com.irq_line1 == 0)))
			continue;

		pr_debug("hw %d, irqno %d, irq_line: 0x%08x 0x%08x\n", pctx_hw_id, iid, ctxs_com.irq_line, ctxs_com.irq_line1);
		hw_ctx = &isp_handle->hw_ctx[pctx_hw_id];
		ispintcommon_record(hw_ctx->cfg_id, pctx_hw_id, ctxs_com.irq_line);

		/* isp interrupt unlikely */
		ret = ispintcommon_isr_root_unlikely(pctx_hw_id, ctxs_com, isp_handle);
		if (unlikely(ret != 0))
			return EFAULT;
		/* isp interrupt handle */
		ispintcommon_isr_root_handle(pctx_hw_id, ctxs_com, isp_handle);
	}
	return IRQ_HANDLED;
}

void isp_int_common_all_done(enum isp_context_hw_id hw_idx, void *isp_handle)
{
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;

	dev = (struct isp_pipe_dev *)isp_handle;

	pctx_hw = &dev->hw_ctx[hw_idx];
	if (pctx_hw->fmcu_handle) {
		pr_debug("fmcu started. skip all done, hw_idx %d\n ", hw_idx);
		return;
	}
	pr_debug("cxt_id:%d all done.\n", hw_idx);
	if (pctx_hw->valid_slc_num) {
		pr_debug("slice done. last %d\n", pctx_hw->is_last_slice);
		if (!pctx_hw->is_last_slice) {
			complete(&pctx_hw->slice_done);
			return;
		}
		complete(&pctx_hw->slice_done);
		pr_debug("frame done.\n");
	}

	if (pctx_hw->postproc_func)
		pctx_hw->postproc_func(dev, hw_idx, POSTPROC_FRAME_DONE, NULL);
}

void isp_int_common_shadow_done(enum isp_context_hw_id idx, void *isp_handle)
{
	pr_debug("cxt_id:%d shadow done.\n", idx);
}

void isp_int_common_dispatch_done(enum isp_context_hw_id idx, void *isp_handle)
{
	pr_debug("cxt_id:%d done.\n", idx);
}

void isp_int_common_pre_store_done(enum isp_context_hw_id idx, void *isp_handle)
{
	pr_debug("cxt_id:%d done.\n", idx);
}

void isp_int_common_vid_store_done(enum isp_context_hw_id idx, void *isp_handle)
{
	pr_debug("cxt_id:%d done.\n", idx);
}

void isp_int_common_thumb_store_done(enum isp_context_hw_id idx, void *isp_handle)
{
	pr_debug("cxt_id:%d done.\n", idx);
}

void isp_int_common_fmcu_store_done(enum isp_context_hw_id hw_idx, void *isp_handle)
{
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;

	dev = (struct isp_pipe_dev *)isp_handle;
	pctx_hw = &dev->hw_ctx[hw_idx];
	if (pctx_hw->fmcu_handle == NULL) {
		pr_warn("warning: no fmcu for hw %d\n", hw_idx);
		return;
	}

	pr_debug("cxt_id:%d fmcu store done.\n", hw_idx);

	if (pctx_hw->postproc_func)
		pctx_hw->postproc_func(dev, hw_idx, POSTPROC_FRAME_DONE, NULL);
	if (pctx_hw->enable_slowmotion == 1 && pctx_hw->postproc_func)
		pctx_hw->postproc_func(dev, hw_idx, POSTPROC_SLOWMOTION_FRAMEDONE, NULL);
}

void isp_int_common_fmcu_shadow_done(enum isp_context_hw_id hw_idx, void *isp_handle)
{
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;

	dev = (struct isp_pipe_dev *)isp_handle;
	pctx_hw = &dev->hw_ctx[hw_idx];
	if (pctx_hw->fmcu_handle == NULL) {
		pr_warn("warning: no fmcu for hw %d\n", hw_idx);
		return;
	}

	pr_debug("cxt_id:%d done.\n", hw_idx);
}

void isp_int_common_fmcu_load_done(enum isp_context_hw_id idx, void *isp_handle)
{
	pr_debug("cxt_id:%d done.\n", idx);
}

void isp_int_common_3dnr_all_done(enum isp_context_hw_id hw_idx, void *isp_handle)
{
	pr_debug("3dnr all done. cxt_id:%d\n", hw_idx);
}

void isp_int_common_3dnr_shadow_done(enum isp_context_hw_id hw_idx, void *isp_handle)
{
	pr_debug("3dnr shadow done. cxt_id:%d\n", hw_idx);
}

void isp_int_common_hist2_cal_done(enum isp_context_hw_id hw_idx, void *isp_handle)
{
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;

	dev = (struct isp_pipe_dev *)isp_handle;
	pctx_hw = &dev->hw_ctx[hw_idx];
	if (!pctx_hw->enable_slowmotion)
		return;

	if (pctx_hw->postproc_func)
		pctx_hw->postproc_func(dev, hw_idx, POSTPROC_HIST_CAL_DONE, NULL);
	return;
}

int isp_int_common_irq_hw_cnt_reset(int ctx_id)
{
	if (ctx_id < ISP_CONTEXT_HW_NUM)
		memset(irq_done[ctx_id], 0, sizeof(irq_done[ctx_id]));

#ifdef ISP_INT_RECORD
	if (ctx_id < ISP_CONTEXT_HW_NUM) {
		memset(isp_int_recorder[ctx_id][0], 0, sizeof(isp_int_recorder) / ISP_CONTEXT_HW_NUM);
		memset(int_index[ctx_id], 0, sizeof(int_index) / ISP_CONTEXT_HW_NUM);
	}
#endif
	return 0;
}

int isp_int_common_irq_hw_cnt_trace(int ctx_id)
{
	int i = 0;

	if (ctx_id >= ISP_CONTEXT_HW_NUM)
		return 0;

	for (i = 0; i < 32; i++)
		if(irq_done[ctx_id][i])
			pr_info("done %d %d :   %d\n", ctx_id, i, irq_done[ctx_id][i]);

#ifdef ISP_INT_RECORD
	{
		uint32_t cnt, j;
		int idx = ctx_id;
		for (cnt = 0; cnt < (uint32_t)irq_done[idx][ISP_INT_SHADOW_DONE]; cnt += 4) {
			j = (cnt & (INT_RCD_SIZE - 1)); //rolling
			pr_info("isp%u j=%d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d\n",
			idx, j, (uint32_t)isp_int_recorder[idx][ISP_INT_ISP_ALL_DONE][j] >> 16,
			(uint32_t)isp_int_recorder[idx][ISP_INT_ISP_ALL_DONE][j] & 0xffff,
			(uint32_t)isp_int_recorder[idx][ISP_INT_SHADOW_DONE][j] >> 16,
			(uint32_t)isp_int_recorder[idx][ISP_INT_SHADOW_DONE][j] & 0xffff,
			(uint32_t)isp_int_recorder[idx][ISP_INT_DISPATCH_DONE][j] >> 16,
			(uint32_t)isp_int_recorder[idx][ISP_INT_DISPATCH_DONE][j] & 0xffff,
			(uint32_t)isp_int_recorder[idx][ISP_INT_STORE_DONE_PRE][j] >> 16,
			(uint32_t)isp_int_recorder[idx][ISP_INT_STORE_DONE_PRE][j] & 0xffff,
			(uint32_t)isp_int_recorder[idx][ISP_INT_STORE_DONE_VID][j] >> 16,
			(uint32_t)isp_int_recorder[idx][ISP_INT_STORE_DONE_VID][j] & 0xffff,
			(uint32_t)isp_int_recorder[idx][ISP_INT_FMCU_CONFIG_DONE][j] >> 16,
			(uint32_t)isp_int_recorder[idx][ISP_INT_FMCU_CONFIG_DONE][j] & 0xffff);
		}
	}
#endif
	return 0;
}

int isp_int_common_irq_request(struct device *p_dev,
		uint32_t *irq_no, void *isp_handle)
{
	int ret = 0;
	uint32_t id = 0;
	struct isp_pipe_dev *ispdev = NULL;

	if (!p_dev || !isp_handle || !irq_no) {
		pr_err("fail to get valid input ptr p_dev %p isp_handle %p irq_no %p\n",
			p_dev, isp_handle, irq_no);
		return -EFAULT;
	}
	ispdev = (struct isp_pipe_dev *)isp_handle;

	for (id = 0; id < ISP_INT_LOGICAL_COUNT; id++) {
		ispdev->irq_no[id] = irq_no[id];
		ret = devm_request_irq(p_dev, ispdev->irq_no[id], ispintcommon_isr_root,
			IRQF_SHARED, isp_dev_name[id], (void *)ispdev);
		if (ret) {
			pr_err("fail to install isp%d irq_no %d\n", id, ispdev->irq_no[id]);
			if (id == 1)
				devm_free_irq(p_dev, ispdev->irq_no[0], (void *)ispdev);
			return -EFAULT;
		}
		pr_info("install isp%d irq_no %d\n", id, ispdev->irq_no[id]);
	}

	memset(irq_done, 0, sizeof(irq_done));
	memset(irq_done_sw, 0, sizeof(irq_done_sw));

	return ret;
}

int isp_int_common_irq_sw_cnt_reset(int ctx_id)
{
	if (ctx_id < ISP_CONTEXT_SW_NUM)
		memset(irq_done_sw[ctx_id], 0, sizeof(irq_done_sw[ctx_id]));

	return 0;
}

int isp_int_common_irq_sw_cnt_trace(int ctx_id)
{
	int i;

	if (ctx_id >= ISP_CONTEXT_SW_NUM)
		return 0;

	for (i = 0; i < 32; i++)
		if(irq_done_sw[ctx_id][i])
			pr_info("done %d %d :   %d\n", ctx_id, i, irq_done_sw[ctx_id][i]);
	return 0;
}

int isp_int_common_irq_free(struct device *p_dev, void *isp_handle)
{
	struct isp_pipe_dev *ispdev = NULL;

	ispdev = (struct isp_pipe_dev *)isp_handle;
	if (!ispdev) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	devm_free_irq(p_dev, ispdev->irq_no[0], (void *)ispdev);
	devm_free_irq(p_dev, ispdev->irq_no[1], (void *)ispdev);

	return 0;
}

