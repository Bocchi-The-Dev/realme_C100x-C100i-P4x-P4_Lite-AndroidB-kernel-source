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

#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <sprd_mm.h>

#include "dcam_core.h"
#include "dcam_reg.h"
#include "cam_debugger.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "LSC: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define LENS_LOAD_TIMEOUT 1000

int dcam_init_lsc_slice(void *in, uint32_t online)
{
	return 0;
}

int dcam_init_lsc(void *in, uint32_t online)
{
	int ret = 0;
	uint32_t idx, i = 0;
	uint32_t dst_w_num = 0;
	uint32_t val, lens_load_flag;
	uint32_t buf_sel, offset, hw_addr;
	uint16_t *w_buff = NULL, *gain_tab = NULL;
	struct dcam_dev_lsc_info *info = NULL;
	struct dcam_dev_lsc_param *param = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct dcam_isp_k_block *blk_dcam_pm = NULL;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_force_copy copyarg;

	blk_dcam_pm = (struct dcam_isp_k_block *)in;
	dev = (struct dcam_pipe_dev *)blk_dcam_pm->dev;
	hw = dev->hw;
	param = &blk_dcam_pm->lsc;
	hw_ctx = &dev->hw_ctx[blk_dcam_pm->idx];

	copyarg.id = DCAM_CTRL_BIN;
	copyarg.idx = hw_ctx->hw_ctx_id;
	copyarg.glb_reg_lock = hw_ctx->glb_reg_lock;

	idx = hw_ctx->hw_ctx_id;
	if (idx == DCAM_ID_1 && (blk_dcam_pm->raw_fetch_count == 1)) {
		pr_debug("lsc no need init\n");
		return 0;
	}

	info = &param->lens_info;

	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	/* debugfs bypass, not return, need force copy */
	if (g_dcam_bypass[idx] & (1 << _E_LSC))
		info->bypass = 1;
	if (info->bypass) {
		pr_debug("bypass\n");
		DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, 1);
		hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_FORCE_COPY, &copyarg);
		return 0;
	}

	pr_info("w %d,  grid len %d grid %d  num_t %d (%d, %d)\n",
		info->weight_num, info->gridtab_len, info->grid_width,
		info->grid_num_t, info->grid_x_num, info->grid_y_num);

	w_buff = (uint16_t *)param->weight_tab;
	gain_tab = (uint16_t *)param->buf.addr_k;
	hw_addr = (uint32_t)param->buf.iova[CAM_BUF_IOMMUDEV_DCAM];
	if (!w_buff || !gain_tab || !hw_addr || info->grid_width >= LSC_WEI_TABLE_MAX_NUM) {
		pr_err("fail to get buf, null buf %p %p %x grid_width %x\n",
			w_buff, gain_tab, hw_addr, info->grid_width);
		ret = -EPERM;
		goto exit;
	}

	/* step1:  load weight tab */
	dst_w_num = (info->grid_width >> 1) + 1;
	offset = LSC_WEI_TABLE_START;
	for (i = 0; i < dst_w_num; i++) {
		val = (((uint32_t)w_buff[i * 3 + 0]) & 0xFFFF) |
			((((uint32_t)w_buff[i * 3 + 1]) & 0xFFFF) << 16);
		DCAM_REG_WR(idx, offset, val);
		offset += 4;
		val = (((uint32_t)w_buff[i * 3 + 2]) & 0xFFFF);
		DCAM_REG_WR(idx, offset, val);
		offset += 4;
	}
	pr_debug("write weight tab done\n");

	/* enable internal access sram */
	DCAM_REG_MWR(idx, DCAM_APB_SRAM_CTRL, BIT_0, 1);

	for (i = 0; i < info->grid_num_t * 4; i += 8) {
		pr_debug("gain %04x %04x %04x %04x %04x %04x %04x %04x\n",
			gain_tab[0], gain_tab[1], gain_tab[2], gain_tab[3],
			gain_tab[4], gain_tab[5], gain_tab[6], gain_tab[7]);
		gain_tab += 8;
	}

	/* step2: load grid table */
	DCAM_REG_WR(idx, DCAM_LENS_BASE_RADDR, hw_addr);
	DCAM_REG_MWR(idx, DCAM_LENS_GRID_NUMBER, 0x7FF,
			info->grid_num_t & 0x7FF);

	/* load eb */
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_CLR, BIT_0, 1);

	/* lens_load_buf_sel toggle */
	val = DCAM_REG_RD(idx, DCAM_LENS_LOAD_ENABLE);
	buf_sel = !((val & BIT_1) >> 1);
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_1, buf_sel << 1);
	pr_info("buf_sel %d\n", buf_sel);

	/* configure lens enable and grid param...*/
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, 0);

	/* force copy for init */
	hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_FORCE_COPY, &copyarg);

	/* step3: config grid x y */
	val = ((info->grid_width & 0x1ff) << 16) |
			((info->grid_y_num & 0xff) << 8) |
			(info->grid_x_num & 0xff);
	DCAM_REG_WR(idx, DCAM_LENS_GRID_SIZE, val);

	/* step 4: if initialized config, polling lens_load_flag done. */
	i = 0;
	while (i++ < LENS_LOAD_TIMEOUT) {
		val = DCAM_REG_RD(idx, DCAM_LENS_LOAD_ENABLE);
		lens_load_flag = (val & BIT_2);
		if (lens_load_flag)
			break;
	}
	if (i >= LENS_LOAD_TIMEOUT) {
		pr_err("fail to load, lens grid table load timeout.\n");
		ret = -EPERM;
		goto exit;
	}
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_CLR, BIT_1, (1 << 1));

	/* lens_load_buf_sel toggle */
	buf_sel = !buf_sel;
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_1, buf_sel << 1);
	pr_info("buf_sel %d\n", buf_sel);

	hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_FORCE_COPY, &copyarg);

	return 0;

exit:
	/* bypass lsc if there is exception */
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, 1);
	hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_FORCE_COPY, &copyarg);

	return ret;
}

int dcam_update_lsc(void *in)
{
	int ret = 0;
	uint32_t idx, i = 0;
	uint32_t dst_w_num = 0;
	uint32_t val, lens_load_flag;
	uint32_t buf_sel, offset, hw_addr;
	uint16_t *w_buff = NULL, *gain_tab = NULL;
	struct dcam_dev_lsc_info *info = NULL;
	struct dcam_dev_lsc_param *param = NULL;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct dcam_isp_k_block *blk_dcam_pm = NULL;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_auto_copy copyarg;

	blk_dcam_pm = (struct dcam_isp_k_block *)in;
	dev = (struct dcam_pipe_dev *)blk_dcam_pm->dev;
	hw = dev->hw;
	param = &blk_dcam_pm->lsc;
	hw_ctx = &dev->hw_ctx[blk_dcam_pm->idx];

	idx = hw_ctx->hw_ctx_id;
	info = &param->lens_info;

	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	if (g_dcam_bypass[idx] & (1 << _E_LSC))
		info->bypass = 1;
	if (info->bypass) {
		pr_debug("bypass\n");
		DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, 1);
		return 0;
	}

	w_buff = (uint16_t *)param->weight_tab;
	gain_tab = (uint16_t *)param->buf.addr_k;
	hw_addr = (uint32_t)param->buf.iova[CAM_BUF_IOMMUDEV_DCAM];
	if (!w_buff || !gain_tab || !hw_addr || info->grid_width >= LSC_WEI_TABLE_MAX_NUM) {
		pr_err("fail to get buf, null buf %p %p %x grid_width %x\n",
			w_buff, gain_tab, hw_addr, info->grid_width);
		ret = -EPERM;
		goto exit;
	}

	/* step1:  load weight tab */
	dst_w_num = (info->grid_width >> 1) + 1;
	offset = LSC_WEI_TABLE_START;
	for (i = 0; i < dst_w_num; i++) {
		val = (((uint32_t)w_buff[i * 3 + 0]) & 0xFFFF) |
			((((uint32_t)w_buff[i * 3 + 1]) & 0xFFFF) << 16);
		DCAM_REG_WR(idx, offset, val);
		offset += 4;
		val = (((uint32_t)w_buff[i * 3 + 2]) & 0xFFFF);
		DCAM_REG_WR(idx, offset, val);
		offset += 4;
	}
	pr_debug("update weight tab done\n");

	/* step2: load grid table */
	DCAM_REG_WR(idx, DCAM_LENS_BASE_RADDR, hw_addr);
	DCAM_REG_MWR(idx, DCAM_LENS_GRID_NUMBER, 0x7FF,
			info->grid_num_t & 0x7FF);
	/* load eb */
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_CLR, BIT_0, 1);

	/*lens_bypass*/
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, 0);

	/* step3: config grid x y */
	val = ((info->grid_width & 0x1ff) << 16) |
			((info->grid_y_num & 0xff) << 8) |
			(info->grid_x_num & 0xff);
	DCAM_REG_WR(idx, DCAM_LENS_GRID_SIZE, val);
	pr_debug("update grid %d x %d y %d\n", info->grid_width,
			info->grid_x_num, info->grid_y_num);

	/* step 4:  polling lens_load_flag done. */
	i = 0;
	while (i++ < LENS_LOAD_TIMEOUT) {
		val = DCAM_REG_RD(idx, DCAM_LENS_LOAD_ENABLE);
		lens_load_flag = (val & BIT_2);
		if (lens_load_flag)
			break;
	}
	if (i >= LENS_LOAD_TIMEOUT) {
		pr_debug("fail to load, lens grid table load timeout.\n");
		ret = -EPERM;
	} else {
		/* lens_load_buf_sel toggle */
		val = DCAM_REG_RD(idx, DCAM_LENS_LOAD_ENABLE);
		buf_sel = !((val & BIT_1) >> 1);
		DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_1, buf_sel << 1);

		/*  auto cpy lens registers next sof */
		copyarg.id = DCAM_CTRL_BIN;
		copyarg.idx = idx;
		copyarg.glb_reg_lock = hw_ctx->glb_reg_lock;
		hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_AUTO_COPY, &copyarg);
	}

	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_CLR, BIT_1, (1 << 1));

	param->load_trigger = 0;
	pr_debug("done\n");
	return 0;

exit:
	/* bypass lsc if there is exception */
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, 1);
	return ret;
}

static void dcam_k_lsc_param_dump(struct dcam_dev_lsc_info *lsc_info,
		struct cam_debug_cfg *debug)
{
	struct cam_debug_log_ctx *log_ctx = NULL;

	if (!debug) {
		pr_err("fail to get valid debug file\n");
		return;
	}

	if (!debug->block_param)
		return;

	log_ctx = &debug->block_param_log;

	CAM_DEBUG_LOG_WRITE(log_ctx, "lsc: bypass %d update %d\n",
		lsc_info->bypass, lsc_info->update_flag);
	CAM_DEBUG_LOG_WRITE(log_ctx, "grid num %dx%d, size %dx%d, total num %d, grid tab len %d\n",
		lsc_info->grid_x_num, lsc_info->grid_y_num, lsc_info->grid_width_x,
		lsc_info->grid_width_y, lsc_info->grid_num_t, lsc_info->gridtab_len);
	CAM_DEBUG_LOG_PRINT(&debug->block_param_log);
}

int dcam_k_lsc_block(struct dcam_isp_k_block *p)
{
	int ret = 0;
	uint32_t idx = 0;
	uint16_t *w_buff = NULL, *gain_tab = NULL;
	struct dcam_dev_lsc_info *info;
	struct dcam_dev_lsc_param *param;
	unsigned long wtab_uaddr, gtab_uaddr;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	param = &p->lsc;
	info = &param->lens_info;
	dev = p->dev;

	if (dev && idx < DCAM_HW_CONTEXT_MAX) {
		hw_ctx = &dev->hw_ctx[idx];
		if (g_dcam_block_dump & (1 << _E_LSC)) {
			hw_ctx->debug.idx = idx;
			hw_ctx->debug.fid = hw_ctx->fid;
			hw_ctx->debug.block_param = 1;
		}
		dcam_k_lsc_param_dump(&param->lens_info, &hw_ctx->debug);
	}

	if (param->weight_tab == NULL) {
		pr_err("fail to get weight_tab buf\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (info->weight_num > DCAM_LSC_WEIGHT_TAB_SIZE) {
		pr_err("fail to get weight length wrong %d\n", info->weight_num);
		ret = -EPERM;
		goto exit;
	}

	gain_tab = (uint16_t *)param->buf.addr_k;
	if (IS_ERR_OR_NULL(gain_tab) || (info->gridtab_len > DCAM_LSC_BUF_SIZE)) {
		pr_err("fail to get gain tab, length 0x%x\n", info->gridtab_len);
		ret = -EPERM;
		goto exit;
	}

	w_buff = (uint16_t *)param->weight_tab;
	wtab_uaddr = (unsigned long)info->weight_tab_addr;
	ret = copy_from_user((void *)w_buff,
			(void __user *)wtab_uaddr, info->weight_num);
	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		ret = -EPERM;
		goto exit;
	}

	gtab_uaddr = (unsigned long)info->grid_tab_addr;
	ret = copy_from_user((void *)gain_tab,
			(void __user *)gtab_uaddr, info->gridtab_len);
	if (ret != 0) {
		pr_err("fail to copy from user, ret = %d\n", ret);
		ret = -EPERM;
		goto exit;
	}
exit:
	return ret;
}

int dcam_k_cfg_lsc(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;

	switch (param->property) {
	case DCAM_PRO_LSC_BLOCK:
		pr_debug("DCAM_PRO_LSC_BLOCK\n");
		break;
	default:
		pr_err("fail to support property %d\n",
			param->property);
		return -EINVAL;
	}

	ret = copy_from_user(&p->lsc.lens_info,
			param->property_param, sizeof(p->lsc.lens_info));
	if (ret) {
		pr_err("fail to copy from user. ret %d\n", ret);
		return ret;
	}
	pr_debug("update all\n");

	ret = dcam_k_lsc_block(p);

	return ret;
}
