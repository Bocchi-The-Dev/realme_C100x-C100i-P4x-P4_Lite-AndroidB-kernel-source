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

#define DCAMX_STOP_TIMEOUT              2000
#define DCAM_AXI_STOP_TIMEOUT           2000
#define DCAM_AXIM_AQOS_MASK             0x30FFFF
#define IMG_TYPE_RAW10                  0x2B
#define IMG_TYPE_RAW8                   0x2A
#define IMG_TYPE_YUV                    0x1E

/*
 * pdaf bypass is bit3 of DCAM_CFG
.* 4in1 bypass is bit12 of DCAM_MIPI_CAP_CFG
.* blc bypass is bit18 of DCAM_MIPI_CAP_CFG
 */
#define DCAM_PDAF_BYPASS_CTRL          DCAM_CFG
#define DCAM_4IN1_BYPASS_CTRL          DCAM_MIPI_CAP_CFG
#define DCAM_BLC_BYPASS_CTRL           DCAM_MIPI_CAP_CFG

static atomic_t clk_users;
static int dcamhw_force_copy(void *handle, void *arg);

static int dcamhw_clk_eb(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

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
	ret = clk_set_parent(soc->bpc_clk, soc->bpc_clk_parent);
	if (ret) {
		pr_err("fail to set bpc_clk parent\n");
		clk_set_parent(soc->bpc_clk, soc->bpc_clk_parent);
		return ret;
	}
	ret = clk_prepare_enable(soc->bpc_clk);
	if (ret) {
		pr_err("fail to enable bpc_clk\n");
		clk_set_parent(soc->bpc_clk, soc->bpc_clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc->core_eb);
	if (ret) {
		pr_err("fail to set eb\n");
		clk_disable_unprepare(soc->clk);
		return ret;
	}
	ret = clk_prepare_enable(soc->axi_eb);
	if (ret) {
		pr_err("fail to set dcam axi clk\n");
		clk_disable_unprepare(soc->clk);
		clk_disable_unprepare(soc->core_eb);
	}

	return ret;
}

static int dcamhw_clk_dis(void *handle, void *arg)
{
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

	pr_debug("Enter\n");
	if (atomic_dec_return(&clk_users) != 0) {
		pr_info("Other using, users: %d\n",
			atomic_read(&clk_users));
		return 0;
	}

	if (!handle) {
		pr_err("fail to get invalid hw\n");
		return -EINVAL;
	}

	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	clk_set_parent(soc->bpc_clk, soc->bpc_clk_default);
	clk_disable_unprepare(soc->bpc_clk);
	clk_set_parent(soc->clk, soc->clk_default);
	clk_disable_unprepare(soc->clk);
	clk_disable_unprepare(soc->axi_eb);
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
		pr_warn("warning: dcam axim timeout status 0x%x\n",
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

static int dcamhw_qos_set(void *handle, void *arg)
{
	uint32_t reg_val = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

	if (!handle) {
		pr_err("fail to get invalid hw\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	reg_val = (0x0 << 20) | ((soc->arqos_low & 0xF) << 12) | (0x8 << 8) |
		((soc->awqos_high & 0xF) << 4) | (soc->awqos_low & 0xF);
	REG_MWR(soc->axi_reg_base + AXIM_CTRL, DCAM_AXIM_AQOS_MASK, reg_val);

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
	uint32_t time_out = 0, idx = 0, flag = 0, i = 0, line_mask = 0;
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
	for (i = DCAM_ID_0; i <= DCAM_ID_2; i++)
		DCAM_REG_WR(i, DCAM_CFG, 0);/* disable all path */
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
		pr_warn("warning: dcam axim timeout status 0x%x\n",
			DCAM_AXIM_RD(AXIM_DBG_STS));
	} else {
		ip = hw->ip_dcam[DCAM_ID_0];
		flag = ip->syscon.all_rst_mask
			| ip->syscon.rst_mask
			| hw->ip_dcam[DCAM_ID_1]->syscon.rst_mask
			| hw->ip_dcam[DCAM_ID_2]->syscon.rst_mask;
		/* reset dcam all (0/1/2/bus) */
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst,
			ip->syscon.all_rst_mask, ip->syscon.all_rst_mask);
		os_adapt_time_udelay(10);
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst,
			ip->syscon.all_rst_mask, ~(ip->syscon.all_rst_mask));
	}
	write_unlock_irqrestore(&soc->cam_ahb_lock, lock_flag);
	hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_SET_QOS, NULL);

	/* the end, enable AXI writing */
	DCAM_AXIM_MWR(AXIM_CTRL, BIT_24 | BIT_23, (0x0 << 23));

	for (i = DCAM_ID_0; i <= DCAM_ID_2; i++) {
		if (i != DCAM_ID_2)
			line_mask = DCAMINT_IRQ_LINE_MASK;
		else
			line_mask = DCAM2INT_IRQ_LINE_MASK;
		DCAM_REG_MWR(i, DCAM_INT_CLR, line_mask, line_mask);
		DCAM_REG_MWR(i, DCAM_INT_EN, line_mask, line_mask);

		/* disable internal logic access sram */
		DCAM_REG_MWR(i, DCAM_APB_SRAM_CTRL, BIT_0, 0);

		DCAM_REG_WR(i, DCAM_CFG, 0);/* disable all path */
		if (i != DCAM_ID_2)
			DCAM_REG_WR(i, DCAM_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);
		else
			DCAM_REG_WR(i, DCAM2_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);
	}

	pr_debug("dcam all & axi reset done\n");
	return 0;
}

static int dcamhw_start(void *handle, void *arg)
{
	struct dcam_hw_start *parm = NULL;
	struct dcam_hw_force_copy copyarg = {0};
	uint32_t force_ids = DCAM_CTRL_ALL, reg_val = 0, image_vc = 0, image_data_type = IMG_TYPE_RAW10, image_mode = 1, line_en = 0;

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

	reg_val = ((image_vc & 0x3) << 16) |
		((image_data_type & 0x3F) << 8) | (image_mode & 0x3);

	if (parm->idx != DCAM_ID_2)
		DCAM_REG_WR(parm->idx, DCAM_IMAGE_CONTROL, reg_val);
	else
		DCAM_REG_WR(parm->idx, DCAM2_IMAGE_CONTROL, reg_val);

	DCAM_REG_WR(parm->idx, DCAM_INT_CLR, 0xFFFFFFFF);
	/* see DCAM_PREVIEW_SOF in dcam_int.h for details */
	if (parm->idx != DCAM_ID_2)
		if (parm->raw_callback == 1 || parm->cap_info.ipg_skip_first_frm)
			line_en = DCAMINT_IRQ_LINE_EN_NORMAL | BIT(DCAM_SENSOR_SOF);
		else
			line_en = DCAMINT_IRQ_LINE_EN_NORMAL;
	else
		line_en = DCAM2INT_IRQ_LINE_EN_NORMAL;
	DCAM_REG_WR(parm->idx, DCAM_INT_EN, line_en);

	copyarg.id = force_ids;
	copyarg.idx = parm->idx;
	copyarg.glb_reg_lock = parm->glb_reg_lock;
	dcamhw_force_copy(handle, &copyarg);
	/* trigger cap_en*/
	DCAM_REG_MWR(parm->idx, DCAM_CFG, BIT_0, 1);

	return 0;
}

static int dcamhw_stop(void *handle, void *arg)
{
	int ret = 0, time_out = DCAMX_STOP_TIMEOUT;
	struct dcam_hw_context *hw_ctx = NULL;
	uint32_t idx = 0;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw_ctx = (struct dcam_hw_context *)arg;
	idx = hw_ctx->hw_ctx_id;

	DCAM_REG_MWR(idx, DCAM_PATH_STOP, 0x3F, 0x3F);
	os_adapt_time_udelay(1000);
	DCAM_REG_MWR(idx, DCAM_CFG, 0x3F, 0);

	DCAM_REG_WR(idx, DCAM_INT_EN, 0);
	DCAM_REG_WR(idx, DCAM_INT_CLR, 0xFFFFFFFF);

	/* wait for AHB path busy cleared */
	while (time_out) {
		ret = DCAM_REG_RD(idx, DCAM_PATH_BUSY) & 0xFFF;
		if (!ret)
			break;
		os_adapt_time_udelay(1000);
		time_out--;
	}

	if (time_out == 0)
		pr_err("fail to stop:DCAM%d: stop timeout for 2s\n", idx);

	pr_info("dcam%d stop end\n", idx);
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
		count = DCAM_REG_RD(idx, DCAM_CAP_FRM_CLR) & 0x3F;
		if (hw_ctx->slowmotion_count)
			count = count / hw_ctx->slowmotion_count;
		count = count % DCAM_ADDR_RECORD_FRAME_NUM;
		hw_ctx->frame_addr[count][DCAM_RECORD_FRAME_CUR_COUNT] = DCAM_REG_RD(idx, DCAM_CAP_FRM_CLR) & 0x3F;
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
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_LENS] = DCAM_REG_RD(idx, DCAM_LENS_BASE_RADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AEM] = DCAM_REG_RD(idx, DCAM_AEM_BASE_WADDR);
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
		BIT_5, BIT_7, BIT_9, BIT_11, BIT_13, BIT_15, BIT_17, BIT_19
	};
	const uint32_t bitmap2 = BIT_5;
	uint32_t mask = 0, j = 0, id = 0;
	unsigned long flags = 0;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	copyarg = (struct dcam_hw_auto_copy *)arg;
	id = copyarg->id;
	if (copyarg->idx < 2) {
		for (j = 0; j < 8; j++) {
			if (id & (1 << j))
				mask |= bitmap[j];
		}
	} else if (id && DCAM_CTRL_CAP) {
		mask = bitmap2;
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
	struct dcam_hw_force_copy *copyarg = NULL;
	const uint32_t bitmap[] = {
		BIT_4, BIT_6, BIT_8, BIT_10, BIT_12, BIT_14, BIT_16, BIT_18
	};
	const uint32_t bitmap2 = BIT_4;
	uint32_t mask = 0, j = 0;
	unsigned long flags = 0;

	if (unlikely(!arg)) {
		pr_warn("warning: invalid param dev\n");
		return -EFAULT;
	}

	copyarg = (struct dcam_hw_force_copy *)arg;
	if (copyarg->idx < 2) {
		for (j = 0; j < 8; j++) {
			if (copyarg->id & (1 << j))
				mask |= bitmap[j];
		}
	} else if (copyarg->id && DCAM_CTRL_CAP) {
		mask = bitmap2;
	}
	pr_debug("DCAM%u: force copy 0x%0x, id 0x%x\n", copyarg->idx, mask, copyarg->id);
	if (mask == 0)
		return -EFAULT;

	spin_lock_irqsave(&copyarg->glb_reg_lock, flags);
	DCAM_REG_MWR(copyarg->idx, DCAM_CONTROL, mask, mask);
	spin_unlock_irqrestore(&copyarg->glb_reg_lock, flags);

	return 0;
}

static int dcamhw_reset(void *handle, void *arg)
{
	int i = 0;
	enum dcam_id idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;
	uint32_t bypass = 1, eb = 0, line_mask = 0;

	if (!handle || !arg) {
		pr_err("fail to get input para\n");
		return -EFAULT;
	}

	idx = *(uint32_t *)arg;
	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	ip = hw->ip_dcam[idx];

	regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.rst,
		ip->syscon.rst_mask, ip->syscon.rst_mask);
	os_adapt_time_udelay(1);
	regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.rst,
		ip->syscon.rst_mask, ~(ip->syscon.rst_mask));

	for (i = 0x200; i < 0x400; i += 4)
		DCAM_REG_WR(idx, i, 0);

	if (idx != DCAM_ID_2)
		line_mask = DCAMINT_IRQ_LINE_MASK;
	else
		line_mask = DCAM2INT_IRQ_LINE_MASK;

	DCAM_REG_MWR(idx, DCAM_INT_CLR, line_mask, line_mask);
	DCAM_REG_MWR(idx, DCAM_INT_EN, line_mask, line_mask);

	/* disable internal logic access sram */
	DCAM_REG_MWR(idx, DCAM_APB_SRAM_CTRL, BIT_0, 0);

	DCAM_REG_WR(idx, DCAM_CFG, 0);/* disable all path */
	if (idx != DCAM_ID_2)
		DCAM_REG_WR(idx, DCAM_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);
	else
		DCAM_REG_WR(idx, DCAM2_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);

	DCAM_REG_MWR(idx, DCAM_PDAF_CONTROL, BIT_1 | BIT_0, eb);
	DCAM_REG_MWR(idx, DCAM_CROP0_START, BIT_31, eb << 31);

	/* default bypass all blocks */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_18, bypass << 18);
	DCAM_REG_MWR(idx, ISP_RGBG_YRANDOM_PARAMETER0, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_AWBC_PARAM, BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, 0xF, 0xF);/*bpc bypass all */
	DCAM_REG_MWR(idx, ISP_AFL_PARAM0, BIT_1, bypass << 1);/*bayer2y*/
	DCAM_REG_MWR(idx, ISP_BPC_GC_CFG, 0x7, 6);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, 0x0F, 0xF);
	DCAM_REG_MWR(idx, DCAM_GRGB_CTRL, BIT_0, 1);
	DCAM_REG_MWR(idx, ISP_RGBG_PARAM, BIT_0, 1);

	/* 3A statistic */
	DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_AFM_FRM_CTRL, BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL, BIT_0, bypass);
	DCAM_REG_MWR(idx, NR3_FAST_ME_PARAM, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_CFG, 0x3F, 0);

	pr_info("DCAM%d: reset end\n", idx);
	sprd_iommu_restore(&hw->soc_dcam->pdev->dev);

	return 0;
}

static int dcamhw_fetch_set(void *handle, void *arg)
{
	uint32_t fetch_pitch = 0,  val = 0;
	struct dcam_hw_fetch_set *fetch = NULL;

	pr_debug("enter.\n");

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	fetch = (struct dcam_hw_fetch_set *)arg;
	fetch_pitch = cam_cal_hw_pitch(fetch->fetch_info->size.w, fetch->fetch_info->fmt);

	pr_info("size [%d %d], start %d, pitch %d, 0x%x fmt %s\n",
		fetch->fetch_info->trim.size_x, fetch->fetch_info->trim.size_y,
		fetch->fetch_info->trim.start_x, fetch_pitch, fetch->fetch_info->addr.addr_ch0, camport_fmt_name_get(fetch->fetch_info->fmt));
	/* (bitfile)unit 32b,(spec)64b */

	switch (fetch->fetch_info->fmt) {
	case CAM_RAW_PACK_10:
	case CAM_RAW_8:
		val = 0;
		break;
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		val = 1;
		break;
	default:
		pr_err("fail to get valid fmt ,not support\n");
		break;
	}


	DCAM_REG_MWR(fetch->idx, DCAM_INT_CLR,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	DCAM_REG_MWR(fetch->idx, DCAM_INT_EN,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, 0x0F << 12, 0x1 << 12);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, 0xFF << 4, 0x1 << 4);
	/* HW LIMIT, bypass lsc normal for 9 - FF, abnormal 1 - 8 */
	if (fetch->simu_mode)
		DCAM_AXIM_MWR(IMG_FETCH_CTRL, 0xFF << 4, 0x24 << 4);
	fetch_pitch /= 4;
	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG, 0x7, 0x3);
	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG,
		BIT_17 | BIT_16, (fetch->fetch_info->pattern & 3) << 16);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_1, val << 1);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_3 | BIT_2, fetch->fetch_info->endian << 2);
	DCAM_AXIM_WR(IMG_FETCH_SIZE,
		(fetch->fetch_info->trim.size_y << 16) | (fetch->fetch_info->trim.size_x & 0xffff));
	DCAM_AXIM_WR(IMG_FETCH_X,
		(fetch_pitch << 16) | (fetch->fetch_info->trim.start_x & 0xffff));
	DCAM_REG_WR(fetch->idx, DCAM_MIPI_CAP_START, 0);
	DCAM_REG_WR(fetch->idx, DCAM_MIPI_CAP_END,
		((fetch->fetch_info->trim.size_y - 1) << 16) | (fetch->fetch_info->trim.size_x - 1));
	DCAM_AXIM_WR(IMG_FETCH_RADDR, fetch->fetch_info->addr.addr_ch0);

	return 0;
}

static int dcamhw_slice_fetch_set(void *handle, void *arg)
{
	uint32_t fetch_pitch = 0, reg_val = 0, val = 0;
	struct dcam_hw_slice_param *slicearg = NULL;
	struct dcam_fetch_info *fetch = NULL;
	struct img_trim *cur_slice = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	slicearg = (struct dcam_hw_slice_param *)arg;
	fetch = slicearg->fetch;
	cur_slice = slicearg->cur_slice;
	fetch_pitch = cam_cal_hw_pitch(fetch->size.w, fetch->fmt);
	pr_debug("size [%d %d], start [%d %d], pitch %d, 0x%x\n",
		fetch->trim.size_x, fetch->trim.size_y,
		fetch->trim.start_x, fetch->trim.start_y,
		fetch_pitch, fetch->addr.addr_ch0);
	fetch_pitch /= 4;
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_16, BIT_16);
	DCAM_REG_MWR(slicearg->idx, DCAM_MIPI_CAP_CFG, 0x7, 0x3);
	DCAM_REG_MWR(slicearg->idx, DCAM_MIPI_CAP_CFG,
		BIT_17 | BIT_16, (fetch->pattern & 3) << 16);

	switch (fetch->fmt) {
	case CAM_RAW_PACK_10:
	case CAM_RAW_8:
		val = 0;
		break;
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		val = 1;
		break;
	default:
		pr_err("fail to get valid fmt ,not support\n");
		break;
	}

	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_1, val << 1);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_3 | BIT_2, fetch->endian << 2);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, 0xFF << 8, 0x01 << 8);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, 0x0F << 12, 0x01 << 12);

	DCAM_AXIM_WR(IMG_FETCH_SIZE,
		(fetch->trim.size_y << 16) | (fetch->trim.size_x & 0x3fff));
	DCAM_AXIM_WR(IMG_FETCH_X,
		(fetch_pitch << 16) | (fetch->trim.start_x & 0x3fff));

	DCAM_REG_WR(slicearg->idx, DCAM_MIPI_CAP_START, 0);
	DCAM_REG_WR(slicearg->idx, DCAM_MIPI_CAP_END,
		((fetch->trim.size_y - 1) << 16) | (fetch->trim.size_x - 1));

	DCAM_AXIM_WR(IMG_FETCH_RADDR, fetch->addr.addr_ch0);

	DCAM_REG_WR(slicearg->idx, DCAM_CAM_BIN_CFG, BIT_5 | BIT_4);

	reg_val = (0 << 16) | cur_slice->start_x;
	DCAM_REG_WR(slicearg->idx, DCAM_CROP0_START, reg_val);
	reg_val = (cur_slice->size_y << 17) | cur_slice->size_x;
	DCAM_REG_WR(slicearg->idx, DCAM_CROP0_X, reg_val);

	return 0;
}

static int dcamhw_mipi_cap_set(void *handle, void *arg)
{
	uint32_t idx = 0, reg_val = 0;
	struct dcam_hw_mipi_cap *caparg = NULL;
	struct dcam_mipi_info *cap_info = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	caparg = (struct dcam_hw_mipi_cap *)arg;
	cap_info = &caparg->cap_info;
	idx = caparg->idx;

	/* set mipi interface  */
	if (cap_info->sensor_if != DCAM_CAP_IF_CSI2) {
		pr_err("fail to supported sensor if : %d\n",
			cap_info->sensor_if);
		return -EINVAL;
	}

	/* data format */
	if (cap_info->format == DCAM_CAP_MODE_RAWRGB) {
		if (idx != DCAM_ID_2) {
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, BIT_1);
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_17 | BIT_16,
				cap_info->pattern << 16);
		} else
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1 | BIT_0, BIT_0);
	} else if (cap_info->format == DCAM_CAP_MODE_YUV) {
		if (unlikely(cap_info->data_bits != CAM_8_BITS)) {
			pr_err("fail to get valid %d bits for yuv format\n",
				cap_info->data_bits);
			return -EINVAL;
		}

		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, 0 << 1);
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_FRM_CTRL,
				BIT_1 | BIT_0, cap_info->pattern);

		/* x & y deci */
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_FRM_CTRL,
				BIT_9 | BIT_8 | BIT_5 | BIT_4,
				(cap_info->y_factor << 8)
				| (cap_info->x_factor << 4));
	} else {
		pr_err("fail to supported capture format: %d\n",
			cap_info->format);
		return -EINVAL;
	}

	/* data mode */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_2, cap_info->mode << 2);

	/* href */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_3, cap_info->href << 3);

	/* data bits */
	if (cap_info->data_bits == CAM_12_BITS) {
		reg_val = 2;
	} else if (cap_info->data_bits == CAM_10_BITS) {
		reg_val = 1;
	} else if (cap_info->data_bits == CAM_8_BITS) {
		reg_val = 0;
	} else {
		pr_err("fail to supported data bits: %d\n",
			cap_info->data_bits);
		return -EINVAL;
	}
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_5 | BIT_4, reg_val << 4);

	/* frame deci */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_7 | BIT_6,
			cap_info->frm_deci << 6);

	/* MIPI capture start */
	reg_val = (cap_info->cap_size.start_y << 16);
	reg_val |= cap_info->cap_size.start_x;
	if (idx != DCAM_ID_2)
		DCAM_REG_WR(idx, DCAM_MIPI_CAP_START, reg_val);
	else
		DCAM_REG_WR(idx, DCAM2_MIPI_CAP_START, reg_val);

	/* MIPI capture end */
	reg_val = (cap_info->cap_size.start_y
			+ cap_info->cap_size.size_y - 1) << 16;
	reg_val |= (cap_info->cap_size.start_x
			+ cap_info->cap_size.size_x - 1);
	if (idx != DCAM_ID_2)
		DCAM_REG_WR(idx, DCAM_MIPI_CAP_END, reg_val);
	else
		DCAM_REG_WR(idx, DCAM2_MIPI_CAP_END, reg_val);

	/* frame skip before capture */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG,
			BIT_8 | BIT_9 | BIT_10 | BIT_11,
				cap_info->frm_skip << 8);

	/* bypass 4in1 */
	if (cap_info->is_4in1) /* 4in1 use sum, not avrg */
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_13,
				(!!cap_info->is_4in1) << 13);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_12,
			(!cap_info->is_4in1) << 12);

	pr_info("mipi_cap: size %d, %d, %d, %d\n",
		cap_info->cap_size.start_x, cap_info->cap_size.start_y,
		cap_info->cap_size.size_x, cap_info->cap_size.size_y);
	pr_info("mipi_cap: frm %d, mode %d, bits %d, pattern %d, href %d\n",
		cap_info->format, cap_info->mode, cap_info->data_bits,
		cap_info->pattern, cap_info->href);
	pr_info("mipi_cap: deci %d, skip %d, x %d, y %d, 4in1 %d\n",
		cap_info->frm_deci, cap_info->frm_skip, cap_info->x_factor,
		cap_info->y_factor, cap_info->is_4in1);

	return 0;
}

static int dcamhw_path_start(void *handle, void *arg)
{
	uint32_t value = 0, nr3_me_param = 0, image_data_type = IMG_TYPE_RAW10, data_bits = 0, hwfmt = 0;
	struct dcam_hw_path_start *patharg = NULL;
	struct isp_img_rect rect = {0};/* for 3dnr */

	pr_debug("enter.");

	if (!arg) {
		pr_err("fail to get input ptr.\n");
		return -EFAULT;
	}

	patharg = (struct dcam_hw_path_start *)arg;
	data_bits = cam_data_bits(patharg->out_fmt);
	hwfmt = cal_dcamhw_format(patharg->out_fmt);

	patharg->src_sel = patharg->src_sel ? PROCESS_RAW_SRC_SEL : ORI_RAW_SRC_SEL;
	switch (patharg->path_id) {
	case DCAM_PATH_FULL:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN,
			BIT_17 | BIT_16, patharg->endian << 16);

		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_0, hwfmt);
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_2, patharg->src_sel << 2);

		/* full_path_en */
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_1, 1 << 1);
		if (patharg->cap_info.format == DCAM_CAP_MODE_YUV) {
			hwfmt = CAM_RAW10PACK_RAW;
			DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_0, hwfmt);
			DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_2, 0 << 2);
			DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_2, 1 << 2);
		}
		break;

	case DCAM_PATH_BIN:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN,
			BIT_19 |  BIT_18, patharg->endian << 18);

		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG, BIT_0, hwfmt);

		value = (patharg->slowmotion_count << 1) | !!patharg->slowmotion_count;
		DCAM_REG_WR(patharg->idx, DCAM_MODE, value & 0xf);

		/* bin_path_en */
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_2, (1 << 2));
		/*
		 * set default value for 3DNR
		 * nr3_channel_sel: 0
		 * nr3_project_mode: 0
		 * nr3_ping_pong_en: 0
		 * nr3_bypass: 0
		 */
		 /* slowmotion use pingpong, nr3_ping_pong_en:1 */
		if (patharg->slowmotion_count)
			nr3_me_param = 2;
		else
			nr3_me_param = 0;

		if (patharg->cap_info.cap_size.size_x == 0 ||
			patharg->cap_info.cap_size.size_y == 0)
			break;
		rect.x = patharg->in_trim.start_x;
		rect.y = patharg->in_trim.start_y;
		rect.w = patharg->in_trim.size_x;
		rect.h = patharg->in_trim.size_y;
		if (patharg->cap_info.cap_size.size_x < (rect.x + rect.w) ||
			patharg->cap_info.cap_size.size_y < (rect.y + rect.h)) {
			pr_err("fail to get dcam 3dnr input rect [%d %d %d %d]\n",
				rect.x, rect.y, rect.w, rect.h);
			break;
		}
		DCAM_REG_WR(patharg->idx, NR3_FAST_ME_PARAM, nr3_me_param);
		dcam_k_3dnr_set_roi(rect, 0/* project_mode=0 */, patharg->idx);
		break;
	case DCAM_PATH_PDAF:
		/* pdaf path en */
		if (patharg->pdaf_path_eb)
			DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_3, (1 << 3));
		break;

	case DCAM_PATH_VCH2:
		if (data_bits == CAM_8_BITS)
			image_data_type = IMG_TYPE_RAW8;
		/* data type for raw picture */
		if (patharg->src_sel)
			DCAM_REG_WR(patharg->idx, DCAM_VC2_CONTROL, image_data_type << 8 | 0x01);

		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN,
			BIT_8 |  BIT_9, patharg->endian << 8);

		/*vch2 path en */
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_4, (1 << 4));
		break;

	case DCAM_PATH_VCH3:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN,
			BIT_25 |  BIT_24, patharg->endian << 24);
		/*vch3 path en */
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_5, (1 << 5));
		break;
	default:
		break;
	}

	pr_debug("done path_id %d, fmt %d\n", patharg->path_id, hwfmt);
	return 0;
}

static int dcamhw_path_ctrl(void *handle, void *arg)
{
	struct dcam_hw_path_ctrl *patharg = NULL;

	patharg = (struct dcam_hw_path_ctrl *)arg;

	switch (patharg->path_id) {
	case DCAM_PATH_FULL:
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_1, (patharg->type << 1));
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_2, (patharg->type << 2));
		break;
	case DCAM_PATH_PDAF:
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_3, (patharg->type << 3));
		break;
	case DCAM_PATH_VCH2:
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_4, (patharg->type << 4));
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_MWR(patharg->idx, DCAM_CFG, BIT_5, (patharg->type << 5));
		break;
	default:
		break;
	}

	return 0;
}

static int dcamhw_fetch_start(void *handle, void *arg)
{
	DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_16, 0 << 16);
	DCAM_AXIM_WR(IMG_FETCH_START, 1);

	return 0;
}

static int dcamhw_path_size_update(void *handle, void *arg)
{
	uint32_t idx = 0, reg_val = 0;
	struct dcam_hw_path_size *sizearg = NULL;
	struct isp_img_rect rect = {0}; /* for 3dnr path */

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	sizearg = (struct dcam_hw_path_size *)arg;
	idx = sizearg->idx;

	if (sizearg->in_size.w > DCAM_PATH_WMAX ||
		sizearg->in_size.h > DCAM_PATH_HMAX) {
		pr_err("fail to get valid in_size\n");
		return -EFAULT;
	}
	pr_debug("sizearg->path_id:%d in_size:%d %d, out_size:%d %d in_trim:%d %d %d %d\n", sizearg->path_id,
		sizearg->in_size.w, sizearg->in_size.h, sizearg->out_size.w, sizearg->out_size.h,
		sizearg->in_trim.start_x, sizearg->in_trim.start_y, sizearg->in_trim.size_x, sizearg->in_trim.size_y);

	switch (sizearg->path_id) {
	case  DCAM_PATH_FULL:
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
	case  DCAM_PATH_BIN:

		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG,
			BIT_2, sizearg->bin_ratio << 2);
		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_3,
			(sizearg->scaler_sel & 1) << 3);
		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_5,
			(sizearg->scaler_sel >> 1) << 5);
		/* set size to path[DCAM_PATH_3DNR]
		 * because, 3dnr set roi need know bin path crop size
		 * 3dnr end_y should <= bin crop.end_y
		 */
		if ((sizearg->in_size.w > sizearg->in_trim.size_x) ||
			(sizearg->in_size.h > sizearg->in_trim.size_y)) {

			reg_val = (sizearg->in_trim.start_y << 16) |
						sizearg->in_trim.start_x;
			DCAM_REG_WR(idx, DCAM_BIN_CROP_START, reg_val);
			DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_1, 1 << 1);

			reg_val = (sizearg->in_trim.size_y << 16) |
						sizearg->in_trim.size_x;
			DCAM_REG_WR(idx, DCAM_BIN_CROP_SIZE, reg_val);
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

			reg_val = ((sizearg->out_size.h & 0xfff) << 16) |
						(sizearg->out_size.w & 0x1fff);
			DCAM_REG_WR(idx, DCAM_RDS_DES_SIZE, reg_val);
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
			pr_err("fail to get dcam 3dnr input rect[%d %d %d %d]\n",
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

static int dcamhw_binning_4in1_set(void *handle, void *arg)
{
	struct dcam_hw_binning_4in1 *binning = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	binning = (struct dcam_hw_binning_4in1 *)arg;
	if (binning->binning_4in1_en) {
		DCAM_REG_MWR(binning->idx, DCAM_MIPI_CAP_CFG, BIT_13, BIT_13);
		DCAM_REG_MWR(binning->idx, DCAM_MIPI_CAP_CFG, BIT_12, 0 << 12);
	} else {
		DCAM_REG_MWR(binning->idx, DCAM_MIPI_CAP_CFG, BIT_13, BIT_13);
		DCAM_REG_MWR(binning->idx, DCAM_MIPI_CAP_CFG, BIT_12, BIT_12);
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

static int dcamhw_k_null_cfg(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	return 0;
}

static int dcamhw_blocks_setall(void *handle, void *arg)
{
	uint32_t idx = 0;
	struct dcam_isp_k_block *p = (struct dcam_isp_k_block *)arg;

	if (arg == NULL) {
		pr_err("fail to get ptr %p\n", arg);
		return -EFAULT;
	}

	idx = p->idx;
	dcam_k_awbc_block(p);
	dcam_k_blc_block(p);
	dcam_k_bpc_block(p);
	dcam_k_rgb_gain_block(p);
	/* simulator should set this block(random) carefully */
	dcam_k_rgb_dither_random_block(p);
	dcam_k_grgb_block(p);
	pr_debug("dcam%d set all\n", idx);

	return 0;
}

static int dcamhw_blocks_setstatis(void *handle, void *arg)
{
	struct dcam_isp_k_block *p = (struct dcam_isp_k_block *)arg;

	if (arg == NULL) {
		pr_err("fail to get ptr %p\n", arg);
		return -EFAULT;
	}

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

	dcam_k_pdaf(p);
	dcam_k_3dnr_me(p);

	pr_debug("dcam%d set statis done\n", p->idx);
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
	case DCAM_PATH_AEM:
		DCAM_REG_WR(idx, DCAM_AEM_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_AFL:
		DCAM_REG_WR(idx, ISP_AFL_GLB_WADDR, param->frame_addr[0]);
		DCAM_REG_WR(idx, ISP_AFL_REGION_WADDR, param->frame_addr[0] + STATIS_AFL_GBUF_SIZE3);
		break;
	case DCAM_PATH_PDAF:
		DCAM_REG_WR(idx, DCAM_PDAF_BASE_WADDR, param->frame_addr[0]);
		if (!param->blk_param->pdaf.bypass && param->blk_param->pdaf.pdaf_type == DCAM_PDAF_TYPE3)
			DCAM_REG_WR(idx, DCAM_VCH2_BASE_WADDR, param->frame_addr[0] + param->frame_size / 2);
		break;
	case DCAM_PATH_3DNR:
		DCAM_REG_WR(idx, ISP_NR3_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_BPC:
		DCAM_REG_WR(idx, ISP_BPC_OUT_ADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_VCH2:
		DCAM_REG_WR(idx, DCAM_VCH2_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_WR(idx, DCAM_VCH3_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_WR(idx, DCAM_BIN_BASE_WADDR0, param->frame_addr[0]);
		break;
	case DCAM_PATH_AFM:
		DCAM_REG_WR(idx, ISP_AFM_BASE_WADDR, param->frame_addr[0]);
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
	int time_out = 0, hw_ctx_id = 0, dbg_sts_reg = 0, i = 0, cnt = 0;
	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}
	hw_ctx_id = *((uint32_t *)arg);
	if (hw_ctx_id >= DCAM_HW_CONTEXT_MAX)
		return time_out;
	if (hw_ctx_id <= DCAM_ID_1)
		dbg_sts_reg = IMG_FETCH_CTRL;

	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		for (i = 0; i < 5; i++) {
			if (0 == (DCAM_AXIM_RD(dbg_sts_reg) & BIT(17)))
				cnt++;
			else {
				pr_debug("debug read cnt %d\n",cnt);
				cnt = 0;
			}
		}
		if (cnt == 5)
			break;
		os_adapt_time_udelay(1000);
	}

	pr_debug("debug end fetch status time_out %d\n",time_out);
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
	struct isp_pipeline_param_l3 *blkpm_ptr = NULL;

	if (!handle || !arg) {
		pr_err("fail to get input arg :%p, %p.\n", handle, arg);
		return -EFAULT;
	}

	pm_ctx = (struct dcam_isp_k_block *)arg;
	blkpm_ptr = (struct isp_pipeline_param_l3 *)handle;

	if (blkpm_ptr->lsc_param.update_flag) {
		memcpy(&pm_ctx->lsc.lens_info, &blkpm_ptr->lsc_param,
			sizeof(struct dcam_dev_lsc_info));
		ret = dcam_k_lsc_block(pm_ctx);
		if (ret)
			pm_ctx->lsc.lens_info.bypass = 1;
	}
	if (blkpm_ptr->awbc_param.update_flag) {
		memcpy(&pm_ctx->awbc.awbc_info, &blkpm_ptr->awbc_param,
			sizeof(struct dcam_dev_awbc_info));
	}
	if (blkpm_ptr->blc_param.update_flag) {
		memcpy(&pm_ctx->blc.blc_info, &blkpm_ptr->blc_param,
			sizeof(struct dcam_dev_blc_info));
	}
	if (blkpm_ptr->bpc_param.update_flag) {
		memcpy(&pm_ctx->bpc.bpc_param.bpc_info_l3, &blkpm_ptr->bpc_param,
			sizeof(struct dcam_dev_bpc_info_l3));
	}
	if (blkpm_ptr->gain_param.update_flag) {
		memcpy(&pm_ctx->rgb.gain_info, &blkpm_ptr->gain_param,
			sizeof(struct dcam_dev_rgb_gain_info));
	}
	if (blkpm_ptr->dither_param.update_flag) {
		memcpy(&pm_ctx->rgb.rgb_dither, &blkpm_ptr->dither_param,
			sizeof(struct dcam_dev_rgb_dither_info));
	}

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
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_0, 0);
	pr_debug("DCAM%d: reset mipi cap fetch.\n", idx);
	return 1;
}

static struct hw_io_ctrl_fun dcam_hw_ioctl_fun_tab[] = {
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
	{DCAM_HW_CFG_SLICE_FETCH_SET,       dcamhw_slice_fetch_set},
	{DCAM_HW_CFG_BLOCKS_SETALL,         dcamhw_blocks_setall},
	{DCAM_HW_CFG_BLOCKS_SETSTATIS,      dcamhw_blocks_setstatis},
	{DCAM_HW_CFG_STORE_ADDR,            dcamhw_set_store_addr},
	{DCAM_HW_CFG_SLW_ADDR,              dcamhw_set_slw_addr},
	{DCAM_HW_CFG_FETCH_STATUS_GET,      dcamhw_fetch_sts_get},
	{DCAM_HW_CFG_IRQ_DISABLE,           dcamhw_irq_disable},
	{DCAM_HW_CFG_ALL_RESET,             dcamhw_axi_reset},
	{DCAM_HW_CFG_DIS_SN_SOF,            dcamhw_disable_sn_sof},
	{DCAM_HW_CFG_DIS_SN_EOF,            dcamhw_disable_sn_eof},
	{DCAM_HW_CFG_NONZSL_BLOCK_PARAM,    dcamhw_block_param_config},
	{DCAM_HW_CFG_CSI_REG_TRACE,         dcamhw_csi_reg_trace},
	{DCAM_HW_CFG_MIPI_CAP_FETCH_RESET,  dcamhw_reset_mipicap_fetch},
};

static hw_ioctl_fun dcamhw_ioctl_fun_get(
	enum dcam_hw_cfg_cmd cmd)
{
	hw_ioctl_fun hw_ctrl = NULL;
	uint32_t i = 0, total_num = 0;

	total_num = sizeof(dcam_hw_ioctl_fun_tab) / sizeof(struct hw_io_ctrl_fun);
	for (i = 0; i < total_num; i++) {
		if (cmd == dcam_hw_ioctl_fun_tab[i].cmd) {
			hw_ctrl = dcam_hw_ioctl_fun_tab[i].hw_ctrl;
			break;
		}
	}

	return hw_ctrl;
}
#endif
