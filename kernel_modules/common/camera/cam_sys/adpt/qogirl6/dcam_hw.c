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

#ifdef CAM_HW_ADPT_LAYER

#define DCAMX_STOP_TIMEOUT 500
#define DCAM_AXI_STOP_TIMEOUT 2000
#define DCAM_AXIM_AQOS_MASK (0x30FFFF)
#define IMG_TYPE_RAW10        0x2B
#define IMG_TYPE_RAW8         0x2A
#define IMG_TYPE_YUV          0x1E

static atomic_t clk_users;
static int dcamhw_force_copy(void *handle, void *arg);

static int dcamhw_clk_eb(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

	pr_debug("enter\n");
	if (atomic_inc_return(&clk_users) != 1) {
		pr_info("clk has enabled, users: %d\n",
			atomic_read(&clk_users));
		return 0;
	}

	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EINVAL;
	}

	hw = (struct cam_hw_info *)handle;

	soc = hw->soc_dcam;
	ret = clk_set_parent(soc->clk, soc->clk_parent);
	if (ret) {
		pr_err("fail to set clk parent\n");
		clk_set_parent(soc->clk, soc->clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc->clk);
	if (ret) {
		pr_err("fail to enable clk\n");
		clk_set_parent(soc->clk, soc->clk_default);
		return ret;
	}

	ret = clk_prepare_enable(soc->core_eb);
	if (ret) {
		pr_err("fail to set eb\n");
		clk_disable_unprepare(soc->clk);
		return ret;
	}

	return ret;
}

static int dcamhw_clk_dis(void *handle, void *arg)
{
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

	pr_debug("enter\n");
	if (atomic_dec_return(&clk_users) != 0) {
		pr_info("Other using, users: %d\n",
			atomic_read(&clk_users));
		return 0;
	}

	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EINVAL;
	}

	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	clk_set_parent(soc->clk, soc->clk_default);
	clk_disable_unprepare(soc->clk);
	clk_disable_unprepare(soc->core_eb);

	return 0;
}

static int dcamhw_axi_init(void *handle, void *arg)
{
	uint32_t time_out = 0, idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;
	unsigned long flag = 0;

	if (!handle || !arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	idx = *(uint32_t *)arg;
	ip = hw->ip_dcam[idx];
	soc = hw->soc_dcam;
	/* firstly, stop AXI writing. */
	DCAM_AXIM_MWR(AXIM_CTRL, BIT_24 | BIT_23, (0x3 << 23));

	/* then wait for AHB busy cleared */
	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if (0 == (DCAM_AXIM_RD(AXIM_DBG_STS) & 0x1F00F))
			break;
		os_adapt_time_udelay(1000);
	}
	write_lock_irqsave(&soc->cam_ahb_lock, flag);
	if (time_out >= DCAM_AXI_STOP_TIMEOUT) {
		pr_info("dcam axim timeout status 0x%x\n",
			DCAM_AXIM_RD(AXIM_DBG_STS));
	} else {
		/* reset dcam all (0/1/2/bus) */
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst,
			ip->syscon.all_rst_mask, ip->syscon.all_rst_mask);
		os_adapt_time_udelay(10);
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst,
			ip->syscon.all_rst_mask, ~(ip->syscon.all_rst_mask));
	}
	write_unlock_irqrestore(&soc->cam_ahb_lock, flag);
	hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_SET_QOS, NULL);
	/* the end, enable AXI writing */
	DCAM_AXIM_MWR(AXIM_CTRL, BIT_24 | BIT_23, (0x0 << 23));

	return 0;
}

static int dcamhw_irq_disable(void *handle, void *arg)
{
	struct cam_hw_info *hw = NULL;
	uint32_t idx = 0;
	if (!handle || !arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	idx = *(uint32_t *)arg;

	DCAM_REG_WR(idx, DCAM_INT_EN, 0);
	DCAM_REG_WR(idx, DCAM_INT_CLR, 0xFFFFFFFF);
	return 0;
}

static int dcamhw_axi_reset(void *handle, void *arg)
{
	uint32_t time_out = 0, idx = 0, flag = 0, i = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;
	unsigned long lock_flag = 0;

	if (!handle || !arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	idx = *(uint32_t *)arg;
	soc = hw->soc_dcam;
	/* firstly, stop AXI writing. */
	DCAM_AXIM_MWR(AXIM_CTRL, BIT_24 | BIT_23, (0x3 << 23));

	/* then wait for AHB busy cleared */
	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if (0 == (DCAM_AXIM_RD(AXIM_DBG_STS) & 0x1F00F))
			break;
		os_adapt_time_udelay(1000);
	}
	write_lock_irqsave(&soc->cam_ahb_lock, lock_flag);
	if (time_out >= DCAM_AXI_STOP_TIMEOUT) {
		pr_info("fail to dcam axim timeout status 0x%x\n",
			DCAM_AXIM_RD(AXIM_DBG_STS));
	} else {
		ip = hw->ip_dcam[DCAM_ID_0];
		flag = ip->syscon.all_rst_mask
			| ip->syscon.rst_mask
			| hw->ip_dcam[DCAM_ID_1]->syscon.rst_mask;
		/* reset dcam all (0/1/2/bus) */
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst,
			flag, flag);
		os_adapt_time_udelay(10);
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst,
			flag, ~flag);
	}
	write_unlock_irqrestore(&soc->cam_ahb_lock, lock_flag);
	hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_SET_QOS, NULL);
	/* the end, enable AXI writing */
	DCAM_AXIM_MWR(AXIM_CTRL, BIT_24 | BIT_23, (0x0 << 23));

	for(i = DCAM_ID_0; i <= DCAM_ID_1; i++) {
		DCAM_REG_MWR(i, DCAM_INT_CLR, DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
		DCAM_REG_MWR(i, DCAM_INT_EN, DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
		/* disable internal logic access sram */
		DCAM_REG_MWR(i, DCAM_APB_SRAM_CTRL, BIT_0, 0);

		DCAM_REG_WR(i, DCAM_MIPI_CAP_CFG, 0); /* disable all path */
		DCAM_REG_WR(i, DCAM_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);
	}

	pr_debug("dcam all & axi reset done\n");
	return 0;
}

static int dcamhw_qos_set(void *handle, void *arg)
{
	uint32_t reg_val = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	reg_val = (0x0 << 20) | ((soc->arqos_low & 0xF) << 12) | (0x8 << 8) |
		((soc->awqos_high & 0xF) << 4) | (soc->awqos_low & 0xF);
	REG_MWR(soc->axi_reg_base + AXIM_CTRL, DCAM_AXIM_AQOS_MASK, reg_val);

	return 0;
}

static int dcamhw_start(void *handle, void *arg)
{
	struct dcam_hw_start *parm = NULL;
	struct dcam_hw_force_copy copyarg;
	uint32_t reg_val = 0;
	uint32_t image_vc = 0;
	uint32_t image_data_type = IMG_TYPE_RAW10;
	uint32_t image_mode = 1;
	uint32_t force_ids = DCAM_CTRL_ALL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	parm = (struct dcam_hw_start *)arg;

	if (parm->cap_info.format == DCAM_CAP_MODE_YUV)
		image_data_type = IMG_TYPE_YUV;
	if (parm->cap_info.format == CAM_8_BITS)
		image_data_type = IMG_TYPE_RAW8;
	if (parm->raw_callback == 1)
		image_data_type = 0;
	DCAM_REG_WR(parm->idx, DCAM_INT_CLR, 0xFFFFFFFF);
	/* see DCAM_PREVIEW_SOF in dcam_int.h for details */
	if (parm->raw_callback == 1 || parm->cap_info.ipg_skip_first_frm)
		DCAM_REG_WR(parm->idx, DCAM_INT_EN, DCAMINT_IRQ_LINE_EN_NORMAL | BIT(DCAM_SENSOR_SOF));
	else
		DCAM_REG_WR(parm->idx, DCAM_INT_EN, DCAMINT_IRQ_LINE_EN_NORMAL);

	reg_val = ((image_vc & 0x3) << 16) |
		((image_data_type & 0x3F) << 8) | (image_mode & 0x3);
	DCAM_REG_WR(parm->idx, DCAM_IMAGE_CONTROL, reg_val);

	copyarg.id = force_ids;
	copyarg.idx = parm->idx;
	copyarg.glb_reg_lock = parm->glb_reg_lock;
	dcamhw_force_copy(handle, &copyarg);
	/* trigger cap_en*/
	DCAM_REG_MWR(parm->idx, DCAM_MIPI_CAP_CFG, BIT_0, 1);

	return 0;
}

static int dcamhw_stop(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_hw_context *hw_ctx = NULL;
	int time_out = DCAMX_STOP_TIMEOUT;
	uint32_t idx = 0;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw_ctx = (struct dcam_hw_context *)arg;
	idx = hw_ctx->hw_ctx_id;

	/* reset  cap_en*/
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_0, 0);
	DCAM_REG_WR(idx, DCAM_PATH_STOP, 0x3DFF);

	DCAM_REG_WR(idx, DCAM_INT_EN, 0);
	DCAM_REG_WR(idx, DCAM_INT_CLR, 0xFFFFFFFF);

	/* wait for AHB path busy cleared */
	while (time_out) {
		ret = DCAM_REG_RD(idx, DCAM_PATH_BUSY) & 0x3FFF;
		if (!ret)
			break;
		os_adapt_time_udelay(1000);
		time_out--;
	}

	if (time_out == 0)
		pr_err("fail to normal stop PATH 0x%x, DCAM%d timeout for 2s\n", ret, idx);

	pr_info("dcam%d stop\n", idx);
	return ret;
}

static int dcamhw_record_addr(void *handle, void *arg)
{
	struct dcam_hw_context *hw_ctx = NULL;
	uint32_t count = 0, idx = DCAM_HW_CONTEXT_MAX;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw_ctx= (struct dcam_hw_context *)arg;
	idx = hw_ctx->hw_ctx_id;
	if (idx >= DCAM_HW_CONTEXT_MAX) {
		pr_err("fail to get dev idx 0x%x exceed DCAM_ID_MAX\n", idx);
		return -EFAULT;
	}

	if (hw_ctx->is_offline_proc) {
		count = hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM];
		count = count % DCAM_ADDR_RECORD_FRAME_NUM;
		hw_ctx->frame_addr[count][DCAM_RECORD_FRAME_CUR_COUNT] = hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM];
		hw_ctx->frame_addr[count][DCAM_RECORD_PORT_IMG_FETCH] = DCAM_AXIM_RD(IMG_FETCH_RADDR);
		hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM] += 1;
	} else {
		count = DCAM_REG_RD(idx, DCAM_CAP_FRM_CLR) & 0xFF;
		if (hw_ctx->slowmotion_count)
			count = count / hw_ctx->slowmotion_count;
		count = count % DCAM_ADDR_RECORD_FRAME_NUM;
		hw_ctx->frame_addr[count][DCAM_RECORD_FRAME_CUR_COUNT] = DCAM_REG_RD(idx, DCAM_CAP_FRM_CLR) & 0xFF;
		hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM] = count;
	}
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_FULL] = DCAM_REG_RD(idx, DCAM_FULL_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BIN] = DCAM_REG_RD(idx, DCAM_BIN_BASE_WADDR0);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BIN_1] = DCAM_REG_RD(idx, DCAM_BIN_BASE_WADDR1);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BIN_2] = DCAM_REG_RD(idx, DCAM_BIN_BASE_WADDR2);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BIN_3] = DCAM_REG_RD(idx, DCAM_BIN_BASE_WADDR3);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_PDAF] = DCAM_REG_RD(idx, DCAM_PDAF_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_VCH2] = DCAM_REG_RD(idx, DCAM_VCH2_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_VCH3] = DCAM_REG_RD(idx, DCAM_VCH3_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_PPE] = DCAM_REG_RD(idx, DCAM_PPE_RIGHT_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_LENS] = DCAM_REG_RD(idx, DCAM_LENS_BASE_RADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_LSCM] = DCAM_REG_RD(idx, DCAM_LSCM_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AEM] = DCAM_REG_RD(idx, DCAM_AEM_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AEM_1] = DCAM_REG_RD(idx, DCAM_AEM_BASE_WADDR1);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AEM_2] = DCAM_REG_RD(idx, DCAM_AEM_BASE_WADDR2);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AEM_3] = DCAM_REG_RD(idx, DCAM_AEM_BASE_WADDR3);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_HIST] = DCAM_REG_RD(idx, DCAM_HIST_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_HIST_1] = DCAM_REG_RD(idx, DCAM_HIST_BASE_WADDR1);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_HIST_2] = DCAM_REG_RD(idx, DCAM_HIST_BASE_WADDR2);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_HIST_3] = DCAM_REG_RD(idx, DCAM_HIST_BASE_WADDR3);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AFL_GLB] = DCAM_REG_RD(idx, ISP_AFL_GLB_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AFL_REGION] = DCAM_REG_RD(idx, ISP_AFL_REGION_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BPC_MAP] = DCAM_REG_RD(idx, ISP_BPC_MAP_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BPC_OUT] = DCAM_REG_RD(idx, ISP_BPC_OUT_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AFM] = DCAM_REG_RD(idx, ISP_AFM_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_NR3] = DCAM_REG_RD(idx, ISP_NR3_WADDR);

	pr_debug("DCAM%u copy cnt %d addr done.\n", idx,
		hw_ctx->frame_addr[count][DCAM_RECORD_FRAME_CUR_COUNT]);
	return 0;
}

static int dcamhw_auto_copy(void *handle, void *arg)
{
	struct dcam_hw_auto_copy *copyarg = NULL;
	const uint32_t bitmap[] = {
		BIT_1, BIT_5, BIT_7, BIT_9, BIT_11, BIT_13, BIT_15, BIT_17
	};
	uint32_t mask = 0, j = 0, id = 0;
	unsigned long flags = 0;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	copyarg = (struct dcam_hw_auto_copy *)arg;
	id = copyarg->id;
	if (copyarg->idx < DCAM_ID_MAX) {
		for (j = 0; j < 8; j++) {
			if (id & (1 << j))
				mask |= bitmap[j];
		}
	} else {
		mask = 0;
		pr_err("fail to get dev idx 0x%x exceed DCAM_ID_MAX\n",
			copyarg->idx);
	}

	pr_debug("DCAM%u: auto copy 0x%0x, id 0x%x\n", copyarg->idx, mask, id);
	if (mask == 0)
		return -EFAULT;

	spin_lock_irqsave(&copyarg->glb_reg_lock, flags);
	DCAM_REG_MWR(copyarg->idx, DCAM_CONTROL, mask, mask);
	spin_unlock_irqrestore(&copyarg->glb_reg_lock, flags);

	return 0;
}

static int dcamhw_force_copy(void *handle, void *arg)
{
	struct dcam_hw_force_copy *forcpy = NULL;
	const uint32_t bitmap[] = {
		BIT_0, BIT_4, BIT_6, BIT_8, BIT_10, BIT_12, BIT_14, BIT_16
	};
	uint32_t mask = 0, j = 0;
	unsigned long flags = 0;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	forcpy = (struct dcam_hw_force_copy *)arg;
	if (forcpy->idx < DCAM_ID_MAX) {
		for (j = 0; j < 8; j++) {
			if (forcpy->id & (1 << j))
				mask |= bitmap[j];
		}
	} else {
		mask = 0;
		pr_err("fail to get dev idx 0x%x exceed DCAM_ID_MAX\n",
			forcpy->idx);
	}

	pr_debug("DCAM%u: force copy 0x%0x, id 0x%x\n", forcpy->idx, mask, forcpy->id);
	if (mask == 0)
		return -EFAULT;

	spin_lock_irqsave(&forcpy->glb_reg_lock, flags);
	DCAM_REG_MWR(forcpy->idx, DCAM_CONTROL, mask, mask);
	spin_unlock_irqrestore(&forcpy->glb_reg_lock, flags);

	return 0;
}

static int dcamhw_reset(void *handle, void *arg)
{
	enum dcam_id idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;
	uint32_t time_out = 0, flag = 0, bypass = 1, eb = 0;
	uint32_t reset_bit[DCAM_ID_MAX] = {
		BIT(5),
		BIT(4),
		BIT(3)
	};
	uint32_t sts_bit[DCAM_ID_MAX] = {
		BIT(12), BIT(13), BIT(14)
	};

	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}

	idx = *(uint32_t *)arg;
	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	ip = hw->ip_dcam[idx];

	pr_info("DCAM%d: reset.\n", idx);
	/* then wait for AXIM cleared */
	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if (0 == (DCAM_AXIM_RD(AXIM_DBG_STS) & sts_bit[idx]))
			break;
		os_adapt_time_udelay(1000);
	}

	if (time_out >= DCAM_AXI_STOP_TIMEOUT) {
		pr_info("DCAM%d: reset timeout, axim status 0x%x\n", idx,
			DCAM_AXIM_RD(AXIM_DBG_STS));
	} else {
		flag = reset_bit[idx];
		pr_debug("DCAM%d, rst=0x%x, rst_mask=0x%x flag=0x%x\n",
			idx, ip->syscon.rst, ip->syscon.rst_mask, flag);
		regmap_update_bits(soc->cam_ahb_gpr,
			ip->syscon.rst, ip->syscon.rst_mask, ip->syscon.rst_mask);
		os_adapt_time_udelay(10);
		regmap_update_bits(soc->cam_ahb_gpr,
			ip->syscon.rst, ip->syscon.rst_mask, ~(ip->syscon.rst_mask));
	}

	DCAM_REG_MWR(idx, DCAM_INT_CLR,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	DCAM_REG_MWR(idx, DCAM_INT_EN,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);

	/* disable internal logic access sram */
	DCAM_REG_MWR(idx, DCAM_APB_SRAM_CTRL, BIT_0, 0);

	DCAM_REG_WR(idx, DCAM_MIPI_CAP_CFG, 0); /* disable all path */
	DCAM_REG_WR(idx, DCAM_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);

	DCAM_REG_MWR(idx, DCAM_PDAF_CONTROL, BIT_1 | BIT_0, eb);
	DCAM_REG_MWR(idx, DCAM_CROP0_START, BIT_31, eb << 31);
	DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_0, eb);
	DCAM_REG_MWR(idx, DCAM_FULL_CFG, BIT_0, eb);

	/* default bypass all blocks */
	DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_BLC_PARA_R_B, BIT_31, bypass<< 31);
	DCAM_REG_MWR(idx, ISP_RGBG_YRANDOM_PARAMETER0,
		BIT_1, bypass << 1);
	DCAM_REG_MWR(idx, ISP_RGBG_YRANDOM_PARAMETER0,
		BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_PPI_PARAM, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_HIST_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_AFL_PARAM0, BIT_1, bypass << 1);
	DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_CROP0_START, BIT_31, 0 << 31);
	DCAM_REG_MWR(idx, ISP_AWBC_GAIN0, BIT_31, bypass << 31);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_AFM_FRM_CTRL, BIT_0, bypass);
	DCAM_REG_WR(idx, NR3_FAST_ME_PARAM, 0x109);

	pr_info("DCAM%d: reset end\n", idx);
	sprd_iommu_restore(&hw->soc_dcam->pdev->dev);

	return 0;
}

static int dcamhw_fetch_set(void *handle, void *arg)
{
	uint32_t fetch_pitch = 0, val = 0;
	struct dcam_hw_fetch_set *fetch = NULL;
	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	fetch = (struct dcam_hw_fetch_set *)arg;
	fetch_pitch = cam_cal_hw_pitch(fetch->fetch_info->size.w, fetch->fetch_info->fmt);
	pr_info("size [%d %d], start %d, pitch %d, 0x%x fmt %s\n",
		fetch->fetch_info->trim.size_x, fetch->fetch_info->trim.size_y,
		fetch->fetch_info->trim.start_x, fetch_pitch, fetch->fetch_info->addr.addr_ch0,
		camport_fmt_name_get(fetch->fetch_info->fmt));

	switch (fetch->fetch_info->fmt) {
	case CAM_RAW_PACK_10:
		val = 0;
		break;
	case CAM_RAW_HALFWORD_10:
		val = 1;
		break;
	case CAM_RAW_14:
		val = 2;
		break;
	default:
		pr_err("fail to get fetch->fetch_info->fmt:%s, val:%d\n", camport_fmt_name_get(fetch->fetch_info->fmt), val);
		break;
	}

	DCAM_REG_MWR(fetch->idx, DCAM_INT_CLR,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	if (fetch->virtualsensor_pre_sof) {
		DCAM_REG_WR(fetch->idx, DCAM_INT_EN,
			DCAMINT_IRQ_LINE_EN_NORMAL & (~BIT_2) | BIT(DCAM_PREVIEW_SOF));
	} else
		DCAM_REG_MWR(fetch->idx, DCAM_INT_EN,
			DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG, BIT_12, 0x1 << 12);
	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG, BIT_1, 0x1 << 1);
	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG, BIT_3, 0x0 << 3);
	DCAM_REG_MWR(fetch->idx, DCAM_BAYER_INFO_CFG,
		BIT_5 | BIT_4, (fetch->fetch_info->pattern & 3) << 4);

	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_1 | BIT_0, val);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_3 | BIT_2, fetch->fetch_info->endian << 2);
	DCAM_AXIM_WR(IMG_FETCH_SIZE,
		(fetch->fetch_info->trim.size_y << 16) | (fetch->fetch_info->trim.size_x & 0xffff));
	DCAM_AXIM_WR(IMG_FETCH_X,
		(fetch_pitch << 16) | (fetch->fetch_info->trim.start_x & 0xffff));
	DCAM_AXIM_WR(IMG_FETCH_RADDR, fetch->fetch_info->addr.addr_ch0);

	pr_info("done.\n");

	return 0;
}

static int dcamhw_mipi_cap_set(void *handle, void *arg)
{
	uint32_t idx = 0, reg_val = 0;
	struct dcam_mipi_info *cap_info = NULL;
	struct dcam_hw_mipi_cap *caparg = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	caparg = (struct dcam_hw_mipi_cap *)arg;
	cap_info = &caparg->cap_info;
	idx = caparg->idx;

	/* set mipi interface  */
	if (cap_info->sensor_if != DCAM_CAP_IF_CSI2) {
		pr_err("fail to support sensor if : %d\n",
			cap_info->sensor_if);
		return -EINVAL;
	}

	/* data format */
	if (cap_info->format == DCAM_CAP_MODE_RAWRGB) {
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, 1 << 1);
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG,
			BIT_4 | BIT_5, cap_info->pattern << 4);
	} else if (cap_info->format == DCAM_CAP_MODE_YUV) {
		if (unlikely(cap_info->data_bits != CAM_8_BITS)) {
			pr_err("fail to get valid data bits for yuv format %d\n",
				cap_info->data_bits);
			return -EINVAL;
		}

		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1,  0 << 1);
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG,
			BIT_14 | BIT_15, cap_info->pattern << 14);

		/* x & y deci*/
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG,
				BIT_16 | BIT_17 | BIT_18 | BIT_19,
				(cap_info->y_factor << 18)
				| (cap_info->x_factor << 16));
	} else {
		pr_err("fail to support capture format: %d\n",
			cap_info->format);
		return -EINVAL;
	}

	/* data mode */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_3, cap_info->mode << 3);
	/* href */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_13, cap_info->href << 13);
	/* frame deci */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_7 | BIT_6,
			cap_info->frm_deci << 6);
	/* MIPI capture start */
	reg_val = (cap_info->cap_size.start_y << 16);
	reg_val |= cap_info->cap_size.start_x;
	DCAM_REG_WR(idx, DCAM_MIPI_CAP_START, reg_val);

	/* MIPI capture end */
	reg_val = (cap_info->cap_size.start_y
			+ cap_info->cap_size.size_y - 1) << 16;
	reg_val |= (cap_info->cap_size.start_x
			+ cap_info->cap_size.size_x - 1);
	DCAM_REG_WR(idx, DCAM_MIPI_CAP_END, reg_val);

	/* frame skip before capture */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG,
			BIT_8 | BIT_9 | BIT_10 | BIT_11,
				cap_info->frm_skip << 8);

	/* for C-phy */
	if (cap_info->is_cphy == 1)
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_31, BIT_31);
	DCAM_REG_MWR(idx,
		DCAM_MIPI_CAP_CFG, BIT_12, 0x0 << 12);
	DCAM_REG_MWR(idx,
		DCAM_MIPI_CAP_CFG, BIT_28, 0x0 << 28);
	/* bypass 4in1 */
	if (cap_info->is_4in1) { /* 4in1 use sum, not avrg */
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_1,
						(1) << 1);
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_2, 0 << 2);
	}
	DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_0, !cap_info->is_4in1);

	/* > 24M */
	if (cap_info->dcam_slice_mode) {
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_2, 1 << 2);
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_0, 0);
	}

	if (cap_info->fetch_online_en)
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_4, 1 << 4);

	pr_debug("cap size : %d %d %d %d\n",
		cap_info->cap_size.start_x, cap_info->cap_size.start_y,
		cap_info->cap_size.size_x, cap_info->cap_size.size_y);
	pr_debug("cap: frm %d, mode %d, bits %d, pattern %d, href %d\n",
		cap_info->format, cap_info->mode, cap_info->data_bits,
		cap_info->pattern, cap_info->href);
	pr_debug("cap: deci %d, skip %d, x %d, y %d, 4in1 %d fetch online %d\n",
		cap_info->frm_deci, cap_info->frm_skip, cap_info->x_factor,
		cap_info->y_factor, cap_info->is_4in1, cap_info->fetch_online_en);

	return 0;
}

static int dcamhw_path_start(void *handle, void *arg)
{
	uint32_t nr3_me_param = 0, data_bits = 0, hwfmt = 0, image_data_type = IMG_TYPE_RAW10;
	struct isp_img_rect rect = {0}; /* for 3dnr */
	struct dcam_hw_path_start *patharg = NULL;

	pr_debug("enter.");

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	patharg = (struct dcam_hw_path_start *)arg;
	data_bits = cam_data_bits(patharg->out_fmt);

	patharg->src_sel = patharg->src_sel ? PROCESS_RAW_SRC_SEL : ORI_RAW_SRC_SEL;

	hwfmt = cal_dcamhw_format(patharg->out_fmt);
	switch (patharg->path_id) {
	case DCAM_PATH_FULL:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN, BIT_17 | BIT_16, patharg->endian << 16);
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_2 | BIT_3, hwfmt << 2);
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_4, patharg->src_sel << 4);

		if (data_bits == CAM_10_BITS) {
			/*10bit*/
			DCAM_REG_MWR(patharg->idx, DCAM_BAYER_INFO_CFG, 0x7 << 18, 4 << 18);
			DCAM_REG_MWR(patharg->idx, DCAM_BAYER_INFO_CFG, 0x3 << 16, 1 << 16);
		} else if (data_bits == CAM_14_BITS) {
			/*14bit*/
			DCAM_REG_MWR(patharg->idx, DCAM_BAYER_INFO_CFG, 0x7 << 18, 4 << 18);
			DCAM_REG_MWR(patharg->idx, DCAM_BAYER_INFO_CFG, 0x3 << 16, 3 << 16);
		} else if (data_bits == CAM_12_BITS) {
			/*12bit*/
			DCAM_REG_MWR(patharg->idx, DCAM_BAYER_INFO_CFG, 0x7 << 18, 2 << 18);
			DCAM_REG_MWR(patharg->idx, DCAM_BAYER_INFO_CFG, 0x3 << 16, 2 << 16);
		} else if (data_bits == CAM_8_BITS) {
			/*8bit*/
			DCAM_REG_MWR(patharg->idx, DCAM_BAYER_INFO_CFG, 0x3 << 16, 0);
		} else {
			pr_debug("full path data_bits %d\n", data_bits);
		}

		/* full_path_en */
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_0, (0x1));
		DCAM_REG_MWR(patharg->idx, NR3_FAST_ME_PARAM, BIT_7 | BIT_6,
			(patharg->bayer_pattern & 0x3) << 6);

		if (patharg->cap_info.format == DCAM_CAP_MODE_YUV) {
			hwfmt = CAM_RAW8;
			DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_2 | BIT_3, hwfmt << 2);
			DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG, BIT_0, 0x1);
		}
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN, BIT_19 | BIT_18,
			patharg->endian << 18);
		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG, BIT_2 | BIT_3, hwfmt << 2);
		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG,
				BIT_16, !!patharg->slowmotion_count << 16);
		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG,
				BIT_19 | BIT_18 | BIT_17, (patharg->slowmotion_count & 7) << 17);
		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG, BIT_0, 0x1);
		DCAM_REG_MWR(patharg->idx, NR3_FAST_ME_PARAM, BIT_7 | BIT_6,
			(patharg->bayer_pattern & 0x3) << 6);

		/*
		 * set default value for 3DNR
		 * nr3_mv_bypass: 0
		 * nr3_channel_sel: 0
		 * nr3_project_mode: 0
		 * nr3_sub_me_bypass: 1
		 * nr3_out_en: 0
		 * nr3_ping_pong_en: 0
		 * nr3_bypass: 0
		 */
		 /* slowmotion use pingpong, nr3_ping_pong_en:1 */
		if (patharg->slowmotion_count)
			nr3_me_param = 0xa;
		else
			nr3_me_param = 0x8;

		if (patharg->cap_info.cap_size.size_x == 0 ||
			patharg->cap_info.cap_size.size_y == 0)
			break;
		rect.x = patharg->in_trim.start_x;
		rect.y = patharg->in_trim.start_y;
		rect.w = patharg->in_trim.size_x;
		rect.h = patharg->in_trim.size_y;
		if (patharg->cap_info.cap_size.size_x < (rect.x + rect.w) ||
			patharg->cap_info.cap_size.size_y < (rect.y + rect.h)) {
			pr_err("fail to get valid dcam 3dnr input rect [%d %d %d %d]\n",
				rect.x, rect.y, rect.w, rect.h);
			break;
		}
		DCAM_REG_WR(patharg->idx, NR3_FAST_ME_PARAM, nr3_me_param);
		dcam_k_3dnr_set_roi(rect, 0/* project_mode=0 */, patharg->idx);
		break;
	case DCAM_PATH_PDAF:
		/* pdaf path en */
		if (patharg->pdaf_path_eb)
			DCAM_REG_MWR(patharg->idx, DCAM_PPE_FRM_CTRL0, BIT_0, 1);
		break;
	case DCAM_PATH_VCH2:
		if (data_bits == CAM_8_BITS)
			image_data_type = IMG_TYPE_RAW8;
		/* data type for raw picture */
		if (patharg->src_sel)
			DCAM_REG_WR(patharg->idx, DCAM_VC2_CONTROL, image_data_type << 8 | 0x01 << 4);

		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN, BIT_23 | BIT_22, patharg->endian << 22);

		/*vch2 path en */
		DCAM_REG_MWR(patharg->idx, DCAM_VC2_CONTROL, BIT_0, 1);
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN, BIT_25 | BIT_24, patharg->endian << 24);
		/*vch3 path en */
		DCAM_REG_MWR(patharg->idx, DCAM_VC3_CONTROL, BIT_0, 1);
		break;
	default:
		break;
	}

	pr_debug("done path_id %d, fmt %d\n", patharg->path_id, hwfmt);
	return 0;
}

static int dcamhw_path_ctrl(void *handle, void *arg)
{
	struct dcam_hw_path_ctrl *pathctl = NULL;

	pathctl = (struct dcam_hw_path_ctrl *)arg;
	switch (pathctl->path_id) {
	case DCAM_PATH_FULL:
		DCAM_REG_MWR(pathctl->idx, DCAM_FULL_CFG, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(pathctl->idx, DCAM_CAM_BIN_CFG, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_PDAF:
		DCAM_REG_MWR(pathctl->idx, DCAM_PPE_FRM_CTRL0, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_VCH2:
		DCAM_REG_MWR(pathctl->idx, DCAM_VC2_CONTROL, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_MWR(pathctl->idx, DCAM_VC3_CONTROL, BIT_0, pathctl->type);
		break;
	default:
		break;
	}

	return 0;
}

static int dcamhw_fetch_start(void *handle, void *arg)
{
	DCAM_AXIM_WR(IMG_FETCH_START, 1);
	return 0;
}

static int dcamhw_path_size_update(void *handle, void *arg)
{
	uint32_t idx = 0, reg_val = 0;
	struct dcam_hw_path_size *sizearg = NULL;
	struct isp_img_rect rect = {0}; /* for 3dnr path */

	pr_debug("enter.");
	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	sizearg = (struct dcam_hw_path_size *)arg;
	idx = sizearg->idx;
	pr_debug("sizearg->path_id:%d in_size:%d %d, out_size:%d %d in_trim:%d %d %d %d\n", sizearg->path_id,
		sizearg->in_size.w, sizearg->in_size.h, sizearg->out_size.w, sizearg->out_size.h,
		sizearg->in_trim.start_x, sizearg->in_trim.start_y, sizearg->in_trim.size_x, sizearg->in_trim.size_y);

	switch (sizearg->path_id) {
	case DCAM_PATH_FULL:
		if ((sizearg->in_size.w > sizearg->in_trim.size_x) ||
			(sizearg->in_size.h > sizearg->in_trim.size_y)) {

			DCAM_REG_MWR(idx, DCAM_FULL_CFG, BIT_1, 1 << 1);
			reg_val = (sizearg->in_trim.start_y << 16) |
						sizearg->in_trim.start_x;
			DCAM_REG_WR(idx, DCAM_FULL_CROP_START, reg_val);
			reg_val = (sizearg->in_trim.size_y << 16) |
						sizearg->in_trim.size_x;
			DCAM_REG_WR(idx, DCAM_FULL_CROP_SIZE, reg_val);
		} else {
			DCAM_REG_MWR(idx, DCAM_FULL_CFG, BIT_1, 0 << 1);
		}
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_6, sizearg->bin_ratio << 6);
		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_5 | BIT_4, (sizearg->scaler_sel & 3) << 4);
		/* set size to path[DCAM_PATH_3DNR]
		 * because, 3dnr set roi need know bin path crop size
		 * 3dnr end_y should <= bin crop.end_y
		 */
		if ((sizearg->in_size.w > sizearg->in_trim.size_x) ||
			(sizearg->in_size.h > sizearg->in_trim.size_y)) {

			reg_val = (sizearg->in_trim.start_y << 16) |
						sizearg->in_trim.start_x;
			DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_START, reg_val);

			reg_val = (sizearg->in_trim.size_y << 16) |
						sizearg->in_trim.size_x;
			DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_SIZE, reg_val);
			DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_1, 1 << 1);
		} else {
			/* bypass trim */
			DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_1, 0 << 1);
		}

		if (sizearg->scaler_sel == PORT_SCALER_RAW_DOWNSISER) {
			uint32_t cnt = 0;
			uint32_t *ptr = (uint32_t *)sizearg->rds_coeff_buf;
			unsigned long addr = RDS_COEF_TABLE_START;

			for (cnt = 0; cnt < sizearg->rds_coeff_size;
				cnt += 4, addr += 4)
				DCAM_REG_WR(idx, addr, *ptr++);

			reg_val = ((sizearg->out_size.h & 0x1fff) << 16) |
						(sizearg->out_size.w & 0x1fff);
			pr_debug("output = 0x%x\n", reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_DES_SIZE, reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_SLICE_CTRL1, reg_val);

			reg_val = ((sizearg->in_trim.size_y & 0x1fff) << 16) |
						(sizearg->in_trim.size_x & 0x1fff);
			pr_debug("input = 0x%x\n", reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_SLICE_CTRL0, reg_val);

			reg_val = (sizearg->rds_init_phase_int1 << 16) | sizearg->rds_init_phase_int0;
			pr_debug("phase_init = 0x%x\n", reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_SLICE_CTRL2, reg_val);
			reg_val = (sizearg->rds_init_phase_rdm1 << 16) | sizearg->rds_init_phase_rdm0;
			pr_debug("rdm0_init = 0x%x\n", reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_SLICE_CTRL3, reg_val);
		}

		/* 3dnr reset when zoom */
		if (sizearg->size_x == 0 || sizearg->size_y == 0)
			break;
		rect.x = sizearg->in_trim.start_x;
		rect.y = sizearg->in_trim.start_y;
		rect.w = sizearg->in_trim.size_x;
		rect.h = sizearg->in_trim.size_y;
		if (sizearg->size_x < (rect.x + rect.w) ||
			sizearg->size_y < (rect.y + rect.h)) {
			pr_err("fail to get valid dcam 3dnr input rect[%d %d %d %d]\n",
				rect.x, rect.y, rect.w, rect.h);
			break;
		}
		dcam_k_3dnr_set_roi(rect, 0/* project_mode=0 */, idx);
		break;
	default:
		break;
	}

	pr_debug("done\n");
	return 0;
}

static int dcamhw_lbuf_share_set(void *handle, void *arg)
{
	int i = 0, ret = 0;
	uint32_t tb_w[] = {
	/*     dcam0, dcam1 */
		5664, 3648,
		5184, 4160,
		4672, 4672,
		4160, 5184,
		3648, 5664,
	};
	char buf[64]= { 0 };
	struct cam_hw_lbuf_share *camarg = (struct cam_hw_lbuf_share *)arg;
	uint32_t dcam0_mipi_en = 0, dcam1_mipi_en = 0;

	cam_kernel_adapt_kproperty_get("auto/efuse",buf, "-1");
	/*0: UMS9230 1:UMS9230T*/
	if(strncmp(buf, "UMS9230T", strlen("UMS9230T")) == 0) {
		if (camarg->width > DCAM_48M_WIDTH)
			pr_debug("UMS9230T, size larger than 48M , w %d\n", camarg->width);
	} else if (strncmp(buf, "UMS9230", strlen("UMS9230")) == 0) {
		if (camarg->width > DCAM_24M_WIDTH)
			pr_debug("UMS9230, size larger than 24M,  w %d\n", camarg->width);
	} else {
		pr_debug("product name %s, w %d\n", buf,camarg->width);
	}

	dcam0_mipi_en = DCAM_REG_RD(0, DCAM_MIPI_CAP_CFG) & BIT_0;
	dcam1_mipi_en = DCAM_REG_RD(1, DCAM_MIPI_CAP_CFG) & BIT_0;
	pr_debug("dcam %d offline %d en0 %d en1 %d\n", camarg->idx, camarg->offline_flag,
		dcam0_mipi_en, dcam1_mipi_en);
	if (!camarg->offline_flag && (dcam0_mipi_en || dcam1_mipi_en)) {
		pr_debug("dcam 0/1 already in working\n");
		return 0;
	}

	switch (camarg->idx) {
	case 0:
		if (camarg->width > tb_w[0]) {
			DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x7, 2);
			break;
		}
		for (i = 2; i >= 0; i--) {
			if (camarg->width <= tb_w[i * 2]) {
				DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x7, i);
				pr_info("alloc dcam%d linebuf %d %d\n", camarg->idx, tb_w[i*2], tb_w[i*2 + 1]);
				break;
			}
		}
		break;
	case 1:
		if (camarg->width > tb_w[9]) {
			DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x7, 2);
			break;
		}
		for (i = 2; i <= 4; i++) {
			if (camarg->width <= tb_w[i * 2 + 1]) {
				DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x7, i);
				pr_info("alloc dcam%d linebuf %d %d\n", camarg->idx, tb_w[i*2], tb_w[i*2 + 1]);
				break;
			}
		}
		break;
	default:
		pr_err("fail to get valid dcam id %d\n", camarg->idx);
		ret = -EFAULT;
		break;
	}
	DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x3 << 8, 0 << 8);

	return ret;
}

static int dcamhw_lbuf_share_get(void *handle, void *arg)
{
	int i = 0, idx = 0;
	struct cam_hw_lbuf_share *camarg = (struct cam_hw_lbuf_share *)arg;
	uint32_t tb_w[] = {
	/*     dcam0, dcam1 */
		5664, 3648,
		5184, 4160,
		4672, 4672,
		4160, 5184,
		3648, 5664,
	};

	if (!arg)
		return -EFAULT;

	idx = camarg->idx;
	camarg->width = 3264;

	if (idx > DCAM_ID_1)
		goto exit;

	i = DCAM_AXIM_RD(DCAM_LBUF_SHARE_MODE) & 7;
	if (i < 5)
		camarg->width = tb_w[i * 2 + idx];

exit:
	pr_debug("dcam%d, lbuf %d\n", idx, camarg->width);
	return 0;
}

static int dcamhw_slice_fetch_set(void *handle, void *arg)
{
	uint32_t idx = 0, fetch_pitch = 0, val = 0;
	struct img_trim *cur_slice = NULL;
	struct dcam_fetch_info *fetch = NULL;
	struct dcam_hw_slice_param *slicearg = NULL;

	if (!arg) {
		pr_err("fail to check param\n");
		return -1;
	}

	slicearg = (struct dcam_hw_slice_param *)arg;
	fetch = slicearg->fetch;
	cur_slice = slicearg->cur_slice;
	idx = slicearg->idx;

	fetch_pitch = cam_cal_hw_pitch(fetch->size.w, fetch->fmt);

	DCAM_REG_MWR(idx, DCAM_INT_CLR, DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	if (slicearg->virtualsensor_pre_sof)
		DCAM_REG_WR(idx, DCAM_INT_EN, DCAMINT_IRQ_LINE_EN_NORMAL & (~BIT_2) | BIT(DCAM_PREVIEW_SOF));
	else
		DCAM_REG_MWR(idx, DCAM_INT_EN, DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);

	switch (fetch->fmt) {
	case CAM_RAW_PACK_10:
		val = 0;
		break;
	case CAM_RAW_HALFWORD_10:
		val = 1;
		break;
	case CAM_RAW_14:
		val = 2;
		break;
	default:
		pr_err("fail to get fetch->fetch_info->fmt:%s, val:%d\n", camport_fmt_name_get(fetch->fmt), val);
		break;
	}

	DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_5 | BIT_4, (fetch->pattern & 3) << 4);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_1 | BIT_0, val);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_3 | BIT_2, fetch->endian << 2);

	/* cfg mipicap */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_28, 0x1 << 28);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_29, 0x1 << 29);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_30, slicearg->is_last_slice << 30);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_12, 0x1 << 12);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_3, 0x0 << 3);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, 0x1 << 1);

	DCAM_AXIM_WR(IMG_FETCH_RADDR, fetch->addr.addr_ch0 + slicearg->fetch_offset);
	DCAM_AXIM_WR(IMG_FETCH_SIZE, (slicearg->fetch_size.h << 16) | (slicearg->fetch_size.w & 0x1fff));
	DCAM_AXIM_WR(IMG_FETCH_X, (fetch_pitch << 16) | (0 & 0x1fff));

	return 0;
}

static int dcamhw_slice_store_set(void *handle, void *arg)
{
	uint32_t idx = 0, reg_val = 0;
	struct img_trim *cur_slice = NULL;
	struct dcam_hw_slice_param *slicearg = NULL;
	struct dcam_slice_store_param *bin_store = NULL;
	struct dcam_slice_store_param *full_store = NULL;

	if (!arg) {
		pr_err("fail to check param\n");
		return -1;
	}

	slicearg = (struct dcam_hw_slice_param *)arg;
	cur_slice = slicearg->cur_slice;
	idx = slicearg->idx;
	bin_store = &slicearg->bin_store;
	full_store = &slicearg->full_store;

	/* cfg full path */
	DCAM_REG_MWR(idx, DCAM_FULL_CFG, (0x7FF << 20) | (1 << 1), (full_store->pitch << 20) | (1 << 1));
	DCAM_REG_WR(idx, DCAM_FULL_CROP_START,
		((full_store->crop.start_y & 0x1fff) << 16) | (full_store->crop.start_x & 0x1fff));
	DCAM_REG_WR(idx, DCAM_FULL_CROP_SIZE,
		((full_store->crop.size_y & 0x1fff) << 16) | (full_store->crop.size_x & 0x1fff));
	reg_val = DCAM_REG_RD(idx, DCAM_FULL_BASE_WADDR);
	DCAM_REG_WR(idx, DCAM_FULL_BASE_WADDR, reg_val + full_store->store_offset[0]);

	/* cfg bin path */
	if (bin_store->bin_en == 0)
		return 0;
	DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG,
		0x3ff00000 | (1 << 1), (bin_store->pitch << 20) | (1 << 1));
	DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_START,
		((bin_store->crop.start_y & 0x1fff) << 16) | (bin_store->crop.start_x & 0x1fff));
	DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_SIZE,
		((bin_store->crop.size_y & 0x1fff) << 16) | (bin_store->crop.size_x & 0x1fff));

	reg_val = DCAM_REG_RD(idx, DCAM_BIN_BASE_WADDR0);
	DCAM_REG_WR(idx, DCAM_BIN_BASE_WADDR0, reg_val + bin_store->store_offset[0]);

	return 0;
}

static int dcamhw_binning_4in1_set(void *handle, void *arg)
{
	struct dcam_hw_binning_4in1 *binning = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	binning = (struct dcam_hw_binning_4in1 *)arg;
	if (binning->binning_4in1_en) {
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_0, 0);
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_1, 1 << 1);
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_2, 0 << 2);
	} else {
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_0, 1);
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_1, 1 << 1);
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_2, 0 << 2);
	}
	return 0;
}

static int dcamhw_sram_ctrl_set(void *handle, void *arg)
{
	struct dcam_hw_sram_ctrl *sramarg = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	sramarg = (struct dcam_hw_sram_ctrl *)arg;
	if (sramarg->sram_ctrl_en)
		DCAM_REG_MWR(sramarg->idx, DCAM_APB_SRAM_CTRL, BIT_0, 1);
	else
		DCAM_REG_MWR(sramarg->idx, DCAM_APB_SRAM_CTRL, BIT_0, 0);

	return 0;
}

static int dcamhw_blocks_setall(void *handle, void *arg)
{
	uint32_t idx = 0;
	struct dcam_isp_k_block *p = NULL;

	if (arg == NULL) {
		pr_err("fail to get ptr %p\n", arg);
		return -EFAULT;
	}
	p = (struct dcam_isp_k_block *)arg;
	idx = p->idx;
	dcam_k_awbc_block(p);
	dcam_k_blc_block(p);
	dcam_k_bpc_block(p);
	dcam_k_bpc_ppi_param(p);
	dcam_k_rgb_gain_block(p);
	/* simulator should set this block(random) carefully */
	dcam_k_rgb_dither_random_block(p);

	pr_info("dcam%d set all\n", idx);

	return 0;
}

static int dcamhw_blocks_setstatis(void *handle, void *arg)
{
	uint32_t idx = 0;
	struct dcam_isp_k_block *p = NULL;

	if (arg == NULL) {
		pr_err("fail to get ptr %p\n", arg);
		return -EFAULT;
	}
	p = (struct dcam_isp_k_block *)arg;
	idx = p->idx;
	if (p->idx == DCAM_HW_CONTEXT_MAX)
		return 0;

	dcam_k_aem_block(p);
	dcam_k_aem_win(p);

	dcam_k_afm_block(p);
	dcam_k_afm_mode(p);
	dcam_k_afm_skipnum(p);
	dcam_k_afm_win_info(p);
	dcam_k_afm_bypass(p);

	dcam_k_afl_block(p);

	dcam_k_bayerhist_block(p);

	dcam_k_lscm_monitor(p);

	dcam_k_pdaf(p);
	dcam_k_3dnr_me(p);

	pr_info("dcam%d set statis done\n", idx);
	return 0;
}

static int dcamhw_csi_disconnect(void *handle, void *arg)
{
	uint32_t idx = 0, csi_id = 0, val = 0xffffffff;
	struct cam_hw_info *hw = NULL;
	int time_out = DCAMX_STOP_TIMEOUT;
	struct dcam_switch_param *csi_switch = NULL;

	csi_switch = (struct dcam_switch_param *)arg;
	idx = csi_switch->dcam_id;
	csi_id = csi_switch->csi_id;
	hw = (struct cam_hw_info *)handle;

	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, 0x600, 3 << 9);
	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_0, BIT_0);
	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_0, ~BIT_0);

	while (time_out) {
		regmap_read(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, &val);
		val &= BIT_25;
		if (val)
			break;
		os_adapt_time_udelay(1000);
		time_out--;
	}

	if (time_out != 0) {
		pr_debug("disconnect dcam%d from csi%d\n", idx, csi_id);
	} else {
		regmap_read(hw->soc_dcam->cam_ahb_gpr, 0x0, &val);
		if((val >> (6 - csi_id)) & BIT_0)
			pr_err("fail to disconnect DCAM%d from csi%d, timeout\n", idx, csi_id);
		else
			pr_warn("warning: csi%d is already disabled. AHB_EN: 0x%x\n", csi_id, val);
	}

	time_out = DCAMX_STOP_TIMEOUT;
	while (time_out) {
		val = DCAM_REG_RD(idx, DCAM_PATH_BUSY) & 0xFFF;
		if (!val)
			break;
		os_adapt_time_udelay(1000);
		time_out--;
	}

	if (time_out == 0)
		pr_err("fail to stop:DCAM%d: stop timeout for 2s\n", idx);

	return 0;
}

static int dcamhw_csi_connect(void *handle, void *arg)
{
	uint32_t idx = 0, csi_idx = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_switch_param *csi_switch = NULL;

	csi_switch = (struct dcam_switch_param *)arg;
	hw = (struct cam_hw_info *)handle;
	csi_idx = csi_switch->csi_id;
	idx = csi_switch->dcam_id;

	pr_info("DCAM%d connect to csi%d\n", idx, csi_idx);

	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, 0xf00, 1 << 8);
	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_9 | BIT_10, csi_idx << 9);/* select csi for dcam */
	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_0, BIT_0);
	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_0, ~BIT_0);

	return 0;
}

static int dcamhw_csi_force_enable(void *handle, void *arg)
{
	uint32_t idx = 0, csi_idx = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_switch_param *csi_switch = NULL;

	csi_switch = (struct dcam_switch_param *)arg;
	hw = (struct cam_hw_info *)handle;
	csi_idx = csi_switch->csi_id;
	idx = csi_switch->dcam_id;

	pr_debug("DCAM%d force to csi%d\n", idx, csi_idx);

	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_9 | BIT_10, csi_idx << 9);/* select csi for dcam */
	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_11, BIT_11);
	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_8, BIT_8);
	regmap_update_bits(hw->soc_dcam->cam_ahb_gpr, 0x48 + idx *4, BIT_8, ~BIT_8);

	return 0;
}

static int dcamhw_bayer_hist_roi_update(void *handle, void *arg)
{
	struct dcam_isp_k_block *blk_dcam_pm = NULL;

	blk_dcam_pm = (struct dcam_isp_k_block *)arg;
	dcam_k_bayerhist_roi(blk_dcam_pm);
	return 0;
}

static int dcamhw_set_store_addr(void *handle, void *arg)
{
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_cfg_store_addr *param = NULL;
	uint32_t path_id = 0, idx = 0;

	param = (struct dcam_hw_cfg_store_addr *)arg;
	if (!param) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}

	hw = (struct cam_hw_info *)handle;
	path_id = param->path_id;
	idx = param->idx;

	switch (path_id) {
	case DCAM_PATH_FULL:
		if (param->in_fmt == DCAM_CAP_MODE_YUV) {
			param->frame_addr[1] = param->frame_addr[0] + param->out_size.h * param->out_size.w;
			DCAM_REG_WR(idx, DCAM_BIN_BASE_WADDR0, param->frame_addr[1]);
		}
		DCAM_REG_WR(idx, DCAM_FULL_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_WR(idx, DCAM_BIN_BASE_WADDR0, param->frame_addr[0]);
		break;
	case DCAM_PATH_AEM:
		DCAM_REG_WR(idx, DCAM_AEM_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_HIST:
		DCAM_REG_WR(idx, DCAM_HIST_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_PDAF:
		if (param->blk_param->pdaf.pdaf_type == DCAM_PDAF_TYPE3) {
			param->frame_addr[1] = param->frame_addr[0] + param->frame_size / 2;
			DCAM_REG_WR(idx, DCAM_PPE_RIGHT_WADDR, param->frame_addr[1]);
		}
		DCAM_REG_WR(idx, DCAM_PDAF_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_VCH2:
		DCAM_REG_WR(idx, DCAM_VCH2_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_WR(idx, DCAM_VCH3_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_AFM:
		DCAM_REG_WR(idx, ISP_AFM_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_3DNR:
		DCAM_REG_WR(idx, ISP_NR3_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_BPC:
		DCAM_REG_WR(idx, ISP_BPC_OUT_ADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_LSCM:
		DCAM_REG_WR(idx, DCAM_LSCM_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_AFL:
		DCAM_REG_WR(idx, ISP_AFL_GLB_WADDR, param->frame_addr[0]);
		DCAM_REG_WR(idx, ISP_AFL_REGION_WADDR, param->frame_addr[0] + STATIS_AFL_GBUF_SIZE);
		break;
	default:
		pr_err("fail to get valid path id%d\n", path_id);
		break;
	}
	return 0;
}

static int dcamhw_set_slw_addr(void *handle, void *arg)
{
	struct dcam_hw_cfg_store_addr *param = NULL;

	param = (struct dcam_hw_cfg_store_addr *)arg;
	if (!param) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}
	DCAM_REG_WR(param->idx, param->reg_addr, param->frame_addr[0]);

	return 0;
}

static int dcamhw_fetch_sts_get(void *handle, void *arg)
{
	int time_out = 0;
	uint32_t hw_ctx_id = 0, dbg_sts_reg = 0;

	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}

	hw_ctx_id = *((uint32_t *)arg);
	if (hw_ctx_id >= DCAM_HW_CONTEXT_MAX)
		return time_out;
	if (hw_ctx_id <= DCAM_ID_1)
		dbg_sts_reg = AXIM_DBG_STS;

	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if (0 == (DCAM_AXIM_RD(dbg_sts_reg) & BIT(16)))
			break;
		os_adapt_time_udelay(1000);
	}

	return time_out;
}

static int dcamhw_disable_sn_sof(void *handle, void *arg)
{
	uint32_t hw_ctx_id = 0;

	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}

	hw_ctx_id = *((uint32_t *)arg);
	DCAM_REG_MWR(hw_ctx_id, DCAM_INT_EN, BIT(DCAM_SENSOR_SOF), 0);
	return 0;
}

static int dcamhw_disable_sn_eof(void *handle, void *arg)
{
	uint32_t hw_ctx_id = 0;

	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}

	hw_ctx_id = *((uint32_t *)arg);
	DCAM_REG_MWR(hw_ctx_id, DCAM_INT_EN, BIT(DCAM_SENSOR_EOF), 0);
	return 0;
}

static int dcamhw_block_param_config(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_isp_k_block *pm_ctx = NULL;
	struct isp_pipeline_param_l6 *blkpm_ptr = NULL;

	if (!handle || !arg) {
		pr_err("fail to get input arg :%p, %p.\n", handle, arg);
		return -EFAULT;
	}

	pm_ctx = (struct dcam_isp_k_block *)arg;
	blkpm_ptr = (struct isp_pipeline_param_l6 *)handle;

	if (blkpm_ptr->lsc_param.update_flag) {
		memcpy(&pm_ctx->lsc.lens_info,
			&blkpm_ptr->lsc_param, sizeof(struct dcam_dev_lsc_info));
		ret = dcam_k_lsc_block(pm_ctx);
		if (ret)
			pm_ctx->lsc.lens_info.bypass = 1;
	}

	if (blkpm_ptr->awbc_param.update_flag)
		memcpy(&pm_ctx->awbc.awbc_info,
			&blkpm_ptr->awbc_param, sizeof(struct dcam_dev_awbc_info));

	if (blkpm_ptr->blc_param.update_flag)
		memcpy(&pm_ctx->blc.blc_info,
			&blkpm_ptr->blc_param, sizeof(struct dcam_dev_blc_info));

	if (blkpm_ptr->bpc_param.update_flag)
		memcpy(&pm_ctx->bpc.bpc_param.bpc_info,
			&blkpm_ptr->bpc_param, sizeof(struct dcam_dev_bpc_info));

	if (blkpm_ptr->ppi_param.update_flag)
		memcpy(&pm_ctx->bpc.bpc_ppi_info,
			&blkpm_ptr->ppi_param, sizeof(struct dcam_bpc_ppi_info));

	if (blkpm_ptr->gain_param.update_flag)
		memcpy(&pm_ctx->rgb.gain_info,
			&blkpm_ptr->gain_param, sizeof(struct dcam_dev_rgb_gain_info));

	if (blkpm_ptr->dither_param.update_flag)
		memcpy(&pm_ctx->rgb.rgb_dither,
			&blkpm_ptr->dither_param, sizeof(struct dcam_dev_rgb_dither_info));

	return 0;
}

static int dcamhw_csi_reg_trace(void *handle, void *arg)
{
	csi_api_reg_trace();

	return 0;
}

static int dcamhw_reset_mipicap_fetch(void *handle, void *arg)
{
	enum dcam_id idx = 0;

	if (!arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}
	idx = *(uint32_t *)arg;
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_12, 0 << 12);
	pr_debug("DCAM%d: reset mipi cap fetch.\n", idx);
	return 1;
}

static int dcamhw_full_path_src_sel(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_hw_path_ctrl *patharg = NULL;

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	patharg = (struct dcam_hw_path_ctrl *)arg;
	switch (patharg->raw_sel) {
	case ORI_RAW_SRC_SEL:
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT(4), 0 << 4);
		break;
	case PROCESS_RAW_SRC_SEL:
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT(4), BIT(4));
		break;
	default:
		pr_err("fail to support src_sel %d\n", patharg->raw_sel);
		ret = -EINVAL;
		break;
	}
	pr_debug("full path raw sel %d\n", patharg->raw_sel);

	return ret;
}

static struct hw_io_ctrl_fun dcam_ioctl_fun_tab[] = {
	{DCAM_HW_CFG_ENABLE_CLK,            dcamhw_clk_eb},
	{DCAM_HW_CFG_DISABLE_CLK,           dcamhw_clk_dis},
	{DCAM_HW_CFG_INIT_AXI,              dcamhw_axi_init},
	{DCAM_HW_CFG_SET_QOS,               dcamhw_qos_set},
	{DCAM_HW_CFG_RESET,                 dcamhw_reset},
	{DCAM_HW_CFG_START,                 dcamhw_start},
	{DCAM_HW_CFG_STOP,                  dcamhw_stop},
	{DCAM_HW_CFG_FETCH_START,           dcamhw_fetch_start},
	{DCAM_HW_CFG_RECORD_ADDR,           dcamhw_record_addr},
	{DCAM_HW_CFG_AUTO_COPY,             dcamhw_auto_copy},
	{DCAM_HW_CFG_FORCE_COPY,            dcamhw_force_copy},
	{DCAM_HW_CFG_PATH_START,            dcamhw_path_start},
	{DCAM_HW_CFG_PATH_CTRL,             dcamhw_path_ctrl},
	{DCAM_HW_CFG_PATH_SIZE_UPDATE,      dcamhw_path_size_update},
	{DCAM_HW_CFG_MIPI_CAP_SET,          dcamhw_mipi_cap_set},
	{DCAM_HW_CFG_FETCH_SET,             dcamhw_fetch_set},
	{DCAM_HW_CFG_BINNING_4IN1_SET,      dcamhw_binning_4in1_set},
	{DCAM_HW_CFG_SRAM_CTRL_SET,         dcamhw_sram_ctrl_set},
	{DCAM_HW_CFG_LBUF_SHARE_SET,        dcamhw_lbuf_share_set},
	{DCAM_HW_CFG_LBUF_SHARE_GET,        dcamhw_lbuf_share_get},
	{DCAM_HW_CFG_SLICE_FETCH_SET,       dcamhw_slice_fetch_set},
	{DCAM_HW_CFG_SLICE_STORE_SET,       dcamhw_slice_store_set},
	{DCAM_HW_CFG_BLOCKS_SETALL,         dcamhw_blocks_setall},
	{DCAM_HW_CFG_BLOCKS_SETSTATIS,      dcamhw_blocks_setstatis},
	{DCAM_HW_CFG_DISCONECT_CSI,         dcamhw_csi_disconnect},
	{DCAM_HW_CFG_CONECT_CSI,            dcamhw_csi_connect},
	{DCAM_HW_CFG_FORCE_EN_CSI,          dcamhw_csi_force_enable},
	{DCAM_HW_CFG_BAYER_HIST_ROI_UPDATE, dcamhw_bayer_hist_roi_update},
	{DCAM_HW_CFG_STORE_ADDR,            dcamhw_set_store_addr},
	{DCAM_HW_CFG_SLW_ADDR,              dcamhw_set_slw_addr},
	{DCAM_HW_CFG_FETCH_STATUS_GET,      dcamhw_fetch_sts_get},
	{DCAM_HW_CFG_ALL_RESET,             dcamhw_axi_reset},
	{DCAM_HW_CFG_IRQ_DISABLE,           dcamhw_irq_disable},
	{DCAM_HW_CFG_DIS_SN_SOF,            dcamhw_disable_sn_sof},
	{DCAM_HW_CFG_DIS_SN_EOF,            dcamhw_disable_sn_eof},
	{DCAM_HW_CFG_NONZSL_BLOCK_PARAM,    dcamhw_block_param_config},
	{DCAM_HW_CFG_CSI_REG_TRACE,         dcamhw_csi_reg_trace},
	{DCAM_HW_CFG_MIPI_CAP_FETCH_RESET,  dcamhw_reset_mipicap_fetch},
	{DCAM_HW_CFG_PATH_SRC_SEL,          dcamhw_full_path_src_sel},
};

static hw_ioctl_fun dcamhw_ioctl_fun_get(enum dcam_hw_cfg_cmd cmd)
{
	hw_ioctl_fun hw_ctrl = NULL;
	uint32_t  i = 0, total_num = 0;

	total_num = sizeof(dcam_ioctl_fun_tab) / sizeof(struct hw_io_ctrl_fun);
	for (i = 0; i < total_num; i++) {
		if (cmd == dcam_ioctl_fun_tab[i].cmd) {
			hw_ctrl = dcam_ioctl_fun_tab[i].hw_ctrl;
			break;
		}
	}

	return hw_ctrl;
}
#endif
