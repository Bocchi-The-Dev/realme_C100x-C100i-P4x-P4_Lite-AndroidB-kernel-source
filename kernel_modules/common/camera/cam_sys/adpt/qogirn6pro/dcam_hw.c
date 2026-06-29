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

#define DCAMX_STOP_TIMEOUT             500
#define DCAM_AXI_STOP_TIMEOUT          2500
#define DCAM_AXIM_AQOS_MASK            (0xF030FFFF)
#define DCAM_LITE_AXIM_AQOS_MASK       (0x30FFFF)

#define IMG_TYPE_RAW10                 0x2B
#define IMG_TYPE_RAW8                  0x2A
#define IMG_TYPE_YUV                   0x1E

#define COEF_HOR_Y_SIZE                32
#define COEF_HOR_UV_SIZE               16
#define COEF_VOR_Y_SIZE                (32 * 8)
#define COEF_VOR_UV_SIZE               (32 * 8)
#define DCAM_BLOCK_SUM                 270
#define IS_DCAM_IF(idx)                ((idx) < 2)

static atomic_t clk_users;
extern uint32_t contr_cap_eof;
static int dcamhw_force_copy(void *handle, void *arg);

static struct qos_reg nic400_dcam_blk_mtx_qos_list[] = {
#if defined (PROJ_QOGIRN6PRO)
	/*nic400_dcam_blk_mtx_m0_qos_list*/
	{"REGU_OT_CTRL_AW_CFG", 0x30060004, 0xffffffff, 0x08010101},
	{"REGU_OT_CTRL_AR_CFG", 0x30060008, 0x3f3f3f3f, 0x08080808},
	{"REGU_OT_CTRL_Ax_CFG", 0x3006000C, 0x3f3fffff, 0x10100808},
	{"REGU_AXQOS_GEN_EN",   0x30060060, 0x80000003, 0x00000003},
	{"REGU_AXQOS_GEN_CFG",  0x30060064, 0x3fff3fff, 0x0dda0aaa},
	{"REGU_URG_CNT_CFG",    0x30060068, 0x00000701, 0x00000001},
	/*nic400_dcam_blk_mtx_m1_qos_list*/
	{"REGU_OT_CTRL_AW_CFG", 0x30060084, 0xffffffff, 0x08010101},
	{"REGU_OT_CTRL_AR_CFG", 0x30060088, 0x3f3f3f3f, 0x08080808},
	{"REGU_OT_CTRL_Ax_CFG", 0x3006008C, 0x3f3fffff, 0x10100808},
	{"REGU_AXQOS_GEN_EN",   0x300600E0, 0x80000003, 0x00000003},
	{"REGU_AXQOS_GEN_CFG",  0x300600E4, 0x3fff3fff, 0x0dda0aaa},
	{"REGU_URG_CNT_CFG",    0x300600E8, 0x00000701, 0x00000001},
#elif defined (PROJ_QOGIRN6L)
	/*nic400_dcam_blk_mtx_m0_qos_list*/
	{"REGU_LAT_W_CFG",      0x30060014, 0xffffffff, 0x00000000},
	{"REGU_LAT_R_CFG",      0x30060018, 0xffffffff, 0x00000000},
	{"REGU_AXQOS_GEN_EN",   0x30060060, 0x80000003, 0x00000003},
	{"REGU_AXQOS_GEN_CFG",  0x30060064, 0x3fff3fff, 0x0dda0aaa},
	{"REGU_URG_CNT_CFG",    0x30060068, 0x00000701, 0x00000001},
	/*nic400_dcam_blk_mtx_m1_qos_list*/
	{"REGU_LAT_W_CFG",      0x30060094, 0xffffffff, 0x00000000},
	{"REGU_LAT_R_CFG",      0x30060098, 0xffffffff, 0x00000000},
	{"REGU_AXQOS_GEN_EN",   0x300600E0, 0x80000003, 0x00000003},
	{"REGU_AXQOS_GEN_CFG",  0x300600E4, 0x3fff3fff, 0x0dda0aaa},
	{"REGU_URG_CNT_CFG",    0x300600E8, 0x00000701, 0x00000001},
#endif
};

static struct qos_reg glb_rf_dcam_qos_list[] = {
	{"MM_DCAM_BLK_M0_LPC_CTRL", 0x30000074, 0x00010000, 0x00000000},
	{"MM_DCAM_BLK_M0_LPC_CTRL", 0x30000074, 0x00010000, 0x00010000},
	{"MM_DCAM_BLK_M1_LPC_CTRL", 0x30000078, 0x00010000, 0x00000000},
	{"MM_DCAM_BLK_M1_LPC_CTRL", 0x30000078, 0x00010000, 0x00010000},
};

static int dcamhw_clk_eb(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_soc_info *soc_lite = NULL;

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
	soc_lite = hw->soc_dcam_lite;

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
	ret = clk_set_parent(soc->axi_clk, soc->axi_clk_parent);
	if (ret) {
		pr_err("fail to set axi_clk parent\n");
		clk_set_parent(soc->axi_clk, soc->axi_clk_parent);
		return ret;
	}
	ret = clk_prepare_enable(soc->axi_clk);
	if (ret) {
		pr_err(" fail to enable axi_clk\n");
		clk_set_parent(soc->axi_clk, soc->axi_clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc->core_eb);
	if (ret) {
		pr_err("fail to set eb\n");
		clk_disable_unprepare(soc->clk);
		return ret;
	}

	ret = clk_set_parent(soc_lite->clk, soc_lite->clk_parent);
	if (ret) {
		pr_err("fail to set clk parent\n");
		clk_set_parent(soc_lite->clk, soc_lite->clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc_lite->clk);
	if (ret) {
		pr_err("fail to enable clk\n");
		clk_set_parent(soc_lite->clk, soc_lite->clk_default);
		return ret;
	}
	ret = clk_set_parent(soc_lite->axi_clk, soc_lite->axi_clk_parent);
	if (ret) {
		pr_err("fail to set axi_clk parent\n");
		clk_set_parent(soc_lite->axi_clk, soc_lite->axi_clk_parent);
		return ret;
	}
	ret = clk_prepare_enable(soc_lite->axi_clk);
	if (ret) {
		pr_err(" fail to enable axi_clk\n");
		clk_set_parent(soc_lite->axi_clk, soc_lite->axi_clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc_lite->core_eb);
	if (ret) {
		pr_err("fail to set eb\n");
		clk_disable_unprepare(soc_lite->clk);
		return ret;
	}

	ret = clk_set_parent(soc->mtx_clk, soc->mtx_clk_parent);
	if (ret) {
		pr_err("fail to set clk parent\n");
		clk_set_parent(soc->mtx_clk, soc->mtx_clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc->mtx_clk);
	if (ret) {
		pr_err("fail to enable clk\n");
		clk_set_parent(soc->mtx_clk, soc->mtx_clk_default);
		return ret;
	}

	ret = clk_set_parent(soc->blk_cfg_clk, soc->blk_cfg_clk_parent);
	if (ret) {
		pr_err("fail to set clk parent\n");
		clk_set_parent(soc->blk_cfg_clk, soc->blk_cfg_clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc->blk_cfg_clk);
	if (ret) {
		pr_err("fail to enable clk\n");
		clk_set_parent(soc->blk_cfg_clk, soc->blk_cfg_clk_default);
		return ret;
	}

	ret = clk_prepare_enable(soc->mtx_en);
	if (ret) {
		pr_err("fail to set eb\n");
		clk_disable_unprepare(soc->mtx_clk);
		return ret;
	}

	ret = clk_prepare_enable(soc_lite->mtx_en);
	if (ret) {
		pr_err("fail to set eb\n");
		clk_disable_unprepare(soc->mtx_clk);
		return ret;
	}

	ret = clk_prepare_enable(soc->blk_cfg_en);
	if (ret) {
		pr_err("fail to set eb\n");
		clk_disable_unprepare(soc->blk_cfg_clk);
		return ret;
	}
	ret = clk_prepare_enable(soc->tck_en);

	return ret;
}

static int dcamhw_clk_dis(void *handle, void *arg)
{
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_soc_info *soc_lite = NULL;

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
	soc_lite = hw->soc_dcam_lite;

	clk_set_parent(soc->axi_clk, soc->axi_clk_default);
	clk_disable_unprepare(soc->axi_clk);
	clk_set_parent(soc_lite->axi_clk, soc_lite->axi_clk_default);
	clk_disable_unprepare(soc_lite->axi_clk);
	clk_set_parent(soc->clk, soc->clk_default);
	clk_disable_unprepare(soc->clk);
	clk_set_parent(soc_lite->clk, soc_lite->clk_default);
	clk_disable_unprepare(soc_lite->clk);
	clk_set_parent(soc->mtx_clk, soc->mtx_clk_default);
	clk_disable_unprepare(soc->mtx_clk);
	clk_set_parent(soc->blk_cfg_clk, soc->blk_cfg_clk_default);
	clk_disable_unprepare(soc->blk_cfg_clk);

	clk_disable_unprepare(soc->blk_cfg_en);
	clk_disable_unprepare(soc->mtx_en);
	clk_disable_unprepare(soc_lite->mtx_en);
	clk_disable_unprepare(soc->tck_en);
	clk_disable_unprepare(soc_lite->core_eb);
	clk_disable_unprepare(soc->core_eb);

	return 0;
}

static void dcamhw_axi_common_init(struct cam_hw_info *hw, struct cam_hw_soc_info *soc,
	uint32_t ctrl_reg, uint32_t dbg_reg, uint32_t hw_idx)
{
	unsigned long __flags;
	struct cam_hw_ip_info *ip = NULL;
	uint32_t time_out = 0, flag = 0;
	unsigned long lock_flag = 0;

	ip = hw->ip_dcam[hw_idx];
	/* firstly, stop AXI writing. */
	DCAM_AXIM_MWR(hw_idx, ctrl_reg, BIT_24 | BIT_23, (0x3 << 23));

	/* then wait for AHB busy cleared */
	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if (0 == (DCAM_AXIM_RD(hw_idx, dbg_reg) & 0x1F00F))
			break;
		os_adapt_time_udelay(1000);
	}
	write_lock_irqsave(&soc->cam_ahb_lock, lock_flag);
	if (time_out >= DCAM_AXI_STOP_TIMEOUT) {
		pr_info("dcam axim timeout status 0x%x\n", DCAM_AXIM_RD(hw_idx, dbg_reg));
	} else {
		flag = ip->syscon.all_rst_mask
			|ip->syscon.axi_rst_mask;
		/* reset dcam all (0/1/2/bus) */
		spin_lock_irqsave(&g_reg_wr_lock, __flags);
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst, flag, flag);
		os_adapt_time_udelay(10);
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst, flag, ~flag);
		spin_unlock_irqrestore(&g_reg_wr_lock, __flags);
	}
	write_unlock_irqrestore(&soc->cam_ahb_lock, lock_flag);

	hw->dcam_ioctl(hw, hw_idx, DCAM_HW_CFG_SET_QOS, &hw_idx);

	/* the end, enable AXI writing */
	DCAM_AXIM_MWR(hw_idx, ctrl_reg, BIT_24 | BIT_23, (0x0 << 23));
}

static int dcamhw_axi_init(void *handle, void *arg)
{
	uint32_t idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	uint32_t ctrl_reg = 0, dbg_reg = 0;

	if (!handle || !arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	idx = *(uint32_t *)arg;
	soc = hw->soc_dcam;
	ctrl_reg = AXIM_CTRL;
	dbg_reg = AXIM_DBG_STS;
	dcamhw_axi_common_init(hw, soc, ctrl_reg, dbg_reg, idx);

	return 0;
}

static int dcamhw_fmcu_reset(void *handle, void *arg)
{
	int ret = 0;
	uint32_t idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;
	#if defined (PROJ_QOGIRN6L)
	uint32_t flag = 0;
	#endif

	if (!handle || !arg) {
		pr_err("fail to get input arg %p, %p\n", handle, arg);
		return -EFAULT;
	}

	idx = *(uint32_t *)arg;
	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	ip = hw->ip_dcam[idx];

	#if defined (PROJ_QOGIRN6L)
	flag = ip->syscon.rst_fmcu_mask;
	pr_debug("DCAM%d, rst=0x%x, rst_fmcu_mask=0x%x\n",
		idx, ip->syscon.rst, ip->syscon.rst_fmcu_mask);

	regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.rst, flag, flag);
	os_adapt_time_udelay(10);
	regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.rst, flag, ~flag);

	pr_info("DCAM%d: fmcu end\n", idx);
	#else
	ret = hw->dcam_ioctl(hw, idx, DCAM_HW_CFG_INIT_AXI, &idx);
	#endif

	return ret;
}

static void dcamhw_ip_qos_set(void *handle, void *arg)
{
	uint32_t reg_val = 0, awqos_thrd = 0, axi_urgent = 0, awqos_ultra_high = 0, id = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_soc_info *soc_lite = NULL;

	id = *(uint32_t *)arg;
	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	soc_lite = hw->soc_dcam_lite;

	/* qos value need cfg from DT. Now 6pro DT not cfg dcam qos.
	* if only for test, can cfg specific value into dcam reg. Now, just
	* cfg default value for dcam and not consider dcam_lite first.
	*/
	soc->awqos_low = 0x1;
	soc->awqos_high = 0x6;
	soc->arqos_low = 0x6;
	awqos_thrd = 0x2;
	axi_urgent = 0x0;
	awqos_ultra_high = 0xF;
	if (id <= DCAM_ID_1) {
		reg_val = (awqos_ultra_high << 28) | (axi_urgent <<20) | ((soc->arqos_low & 0xF) << 12) | (awqos_thrd << 8) |
			((soc->awqos_high & 0xF) << 4) | (soc->awqos_low & 0xF);
		REG_MWR(soc->axi_reg_base + AXIM_CTRL, DCAM_AXIM_AQOS_MASK, reg_val);
	} else {
		reg_val = (0x0 <<20) | ((soc->arqos_low & 0xF) << 12) | (0x8 << 8) |
			((soc->awqos_high & 0xF) << 4) | (soc->awqos_low & 0xF);
		REG_MWR(soc_lite->axi_reg_base + AXIM_CTRL, DCAM_LITE_AXIM_AQOS_MASK, reg_val);
	}
}

static int dcamhw_qos_set(void *handle, void *arg)
{
	int i = 0, length_mtx = 0,length_list = 0;
	void __iomem *addr = NULL;
	uint32_t reg_val = 0, temp = 0;

	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EFAULT;
	}

	length_mtx = sizeof(nic400_dcam_blk_mtx_qos_list) /
		sizeof(nic400_dcam_blk_mtx_qos_list[0]);
	length_list = sizeof(glb_rf_dcam_qos_list) /
		sizeof(glb_rf_dcam_qos_list[0]);

	for(i = 0; i < length_mtx; i++) {
		addr = ioremap(nic400_dcam_blk_mtx_qos_list[i].base_addr, 4);
		temp = readl_relaxed(addr);
		reg_val = (temp & (~nic400_dcam_blk_mtx_qos_list[i].mask_value))
			| nic400_dcam_blk_mtx_qos_list[i].set_value;
		writel_relaxed(reg_val, addr);
		iounmap(addr);
	}

	for(i = 0; i < length_list; i++) {
		addr = ioremap(glb_rf_dcam_qos_list[i].base_addr, 4);
		temp = readl_relaxed(addr);
		reg_val = (temp & (~glb_rf_dcam_qos_list[i].mask_value))
			| glb_rf_dcam_qos_list[i].set_value;
		writel_relaxed(reg_val, addr);
		iounmap(addr);
	}

	dcamhw_ip_qos_set(handle, arg);
	return 0;
}

static int dcamhw_start(void *handle, void *arg)
{
	struct dcam_hw_start *parm = NULL;
	struct dcam_hw_force_copy copyarg = {0};
	uint32_t force_ids = DCAM_CTRL_ALL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	parm = (struct dcam_hw_start *)arg;

	DCAM_REG_WR(parm->idx, DCAM_INT0_CLR, 0xFFFFFFFF);
	DCAM_REG_WR(parm->idx, DCAM_INT1_CLR, 0xFFFFFFFF);
	/* see DCAM_PREVIEW_SOF in dcam_int.h for details */
	if (parm->raw_callback == 1 || parm->cap_info.cap_size.size_x > DCAM_TOTAL_LBUF ||
		parm->cap_info.ipg_skip_first_frm)
		DCAM_REG_WR(parm->idx, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL |
			BIT(DCAM_IF_IRQ_INT0_SENSOR_SOF));
	else
		DCAM_REG_WR(parm->idx, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL);

	DCAM_REG_WR(parm->idx, DCAM_INT1_EN, DCAMINT_IRQ_LINE_EN1_NORMAL);

	if (contr_cap_eof == 0)
		DCAM_REG_MWR(parm->idx, DCAM_INT0_EN, BIT_3, 0);
	else
		DCAM_REG_MWR(parm->idx, DCAM_INT0_EN, BIT_3, BIT_3);

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
	int ret = 0, time_out = DCAMX_STOP_TIMEOUT;
	uint32_t idx = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct cam_hw_reg_trace trace = {0};
	unsigned long flag = 0;
	struct dcam_dummy_param dummy_param = {0};

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw_ctx = (struct dcam_hw_context *)arg;
	idx = hw_ctx->hw_ctx_id;
	hw = (struct cam_hw_info *)handle;

	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_0, 0);
	DCAM_REG_WR(idx, DCAM_PATH_STOP, 0x7FFFF);

	if (!hw_ctx->is_offline_proc && hw_ctx->dummy_slave) {
		dummy_param.hw_ctx_id = hw_ctx->hw_ctx_id;
		dummy_param.enable = CAM_DISABLE;
		hw_ctx->dummy_slave->dummy_ops->dummy_enable(hw_ctx->dummy_slave, &dummy_param);
	}

	/* wait for AHB path busy cleared */
	while (time_out) {
		ret = DCAM_REG_RD(idx, DCAM_PATH_BUSY) & 0x7FFF;
		if (!ret)
			break;
		os_adapt_time_udelay(1000);
		time_out--;
	}

	if (time_out == 0) {
		pr_err("fail to normal stop, DCAM%d timeout for 2s\n", idx);
		trace.type = ABNORMAL_REG_TRACE;
		trace.idx = idx;
		hw->isp_ioctl(hw, ISP_HW_CFG_REG_TRACE, &trace);
	}

	time_out = 35;
	while (time_out) {
		spin_lock_irqsave(&hw_ctx->fbc_lock, flag);
		if (hw_ctx->prev_fbc_done && hw_ctx->cap_fbc_done) {
			spin_unlock_irqrestore(&hw_ctx->fbc_lock, flag);
			os_adapt_time_udelay(10);
			break;
		}
		spin_unlock_irqrestore(&hw_ctx->fbc_lock, flag);
		os_adapt_time_udelay(1000);
		time_out--;
	}

	if (time_out == 0)
		pr_debug("warning: DCAM%d wait fbc done %d %d timeout for 35ms\n", idx,
			hw_ctx->prev_fbc_done, hw_ctx->cap_fbc_done);
	else
		pr_debug("dcam%d time %d\n", idx, time_out);

	spin_lock_irqsave(&hw_ctx->fbc_lock, flag);
	hw_ctx->prev_fbc_done = 0;
	hw_ctx->cap_fbc_done = 0;
	spin_unlock_irqrestore(&hw_ctx->fbc_lock, flag);

	/* reset cap_en*/
	DCAM_REG_WR(idx, DCAM_INT0_EN, 0);
	DCAM_REG_WR(idx, DCAM_INT0_CLR, 0xFFFFFFFF);
	DCAM_REG_WR(idx, DCAM_INT1_EN, 0);
	DCAM_REG_WR(idx, DCAM_INT1_CLR, 0xFFFFFFFF);

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
		hw_ctx->frame_addr[count][DCAM_RECORD_PORT_IMG_FETCH] = DCAM_AXIM_RD(idx, IMG_FETCH_RADDR);
		hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM] += 1;
	} else {
		count = DCAM_REG_RD(idx, DCAM_CAP_FRM_CLR) & 0xFF;
		if (hw_ctx->slowmotion_count)
			count = count / hw_ctx->slowmotion_count;
		count = count % DCAM_ADDR_RECORD_FRAME_NUM;
		hw_ctx->frame_addr[count][DCAM_RECORD_FRAME_CUR_COUNT] = DCAM_REG_RD(idx, DCAM_CAP_FRM_CLR) & 0xFF;
		hw_ctx->frame_addr[0][DCAM_RECORD_FRAME_CNT_SUM] = count;
	}
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_FBC_BIN] = DCAM_REG_RD(idx, DCAM_YUV_FBC_SCAL_SLICE_PLOAD_BASE_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_FBC_FULL] = DCAM_REG_RD(idx, DCAM_FBC_RAW_SLICE_Y_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_FULL_Y] = DCAM_REG_RD(idx, DCAM_STORE4_SLICE_Y_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_FULL_U] = DCAM_REG_RD(idx, DCAM_STORE4_SLICE_U_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BIN_Y] = DCAM_REG_RD(idx, DCAM_STORE0_SLICE_Y_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BIN_U] = DCAM_REG_RD(idx, DCAM_STORE0_SLICE_U_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_RAW] = DCAM_REG_RD(idx, DCAM_RAW_PATH_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_DEC_L1_Y] = DCAM_REG_RD(idx, DCAM_STORE_DEC_L1_BASE + DCAM_STORE_DEC_SLICE_Y_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_DEC_L1_U] = DCAM_REG_RD(idx, DCAM_STORE_DEC_L1_BASE + DCAM_STORE_DEC_SLICE_U_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_DEC_L2_Y] = DCAM_REG_RD(idx, DCAM_STORE_DEC_L2_BASE + DCAM_STORE_DEC_SLICE_Y_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_DEC_L2_U] = DCAM_REG_RD(idx, DCAM_STORE_DEC_L2_BASE + DCAM_STORE_DEC_SLICE_U_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_DEC_L3_Y] = DCAM_REG_RD(idx, DCAM_STORE_DEC_L3_BASE + DCAM_STORE_DEC_SLICE_Y_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_DEC_L3_U] = DCAM_REG_RD(idx, DCAM_STORE_DEC_L3_BASE + DCAM_STORE_DEC_SLICE_U_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_DEC_L4_Y] = DCAM_REG_RD(idx, DCAM_STORE_DEC_L4_BASE + DCAM_STORE_DEC_SLICE_Y_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_DEC_L4_U] = DCAM_REG_RD(idx, DCAM_STORE_DEC_L4_BASE + DCAM_STORE_DEC_SLICE_U_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_PDAF] = DCAM_REG_RD(idx, DCAM_PDAF_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_VCH2] = DCAM_REG_RD(idx, DCAM_VCH2_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_VCH3] = DCAM_REG_RD(idx, DCAM_VCH3_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_LENS] = DCAM_REG_RD(idx, DCAM_LENS_BASE_RADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_LSCM] = DCAM_REG_RD(idx, DCAM_LSCM_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AEM] = DCAM_REG_RD(idx, DCAM_AEM_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BAYER_HIST] = DCAM_REG_RD(idx, DCAM_BAYER_HIST_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_PPE] = DCAM_REG_RD(idx, DCAM_PPE_RIGHT_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AFL_DDR_INIT] = DCAM_REG_RD(idx, ISP_AFL_DDR_INIT_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AFL_REGION] = DCAM_REG_RD(idx, ISP_AFL_REGION_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BPC_MAP] = DCAM_REG_RD(idx, DCAM_BPC_MAP_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_BPC_OUT] = DCAM_REG_RD(idx, DCAM_BPC_OUT_ADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_AFM] = DCAM_REG_RD(idx, DCAM_AFM_LUM_FV_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_NR3] = DCAM_REG_RD(idx, DCAM_NR3_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_HIST_ROI] = DCAM_REG_RD(idx, DCAM_HIST_ROI_BASE_WADDR);

	pr_debug("DCAM%u copy cnt %d addr done.\n", idx,
		hw_ctx->frame_addr[count][DCAM_RECORD_FRAME_CUR_COUNT]);
	return 0;
}

static int dcamhw_auto_copy(void *handle, void *arg)
{
	struct dcam_hw_auto_copy *copyarg = NULL;
	const uint32_t bitmap[] = {
		BIT_1, BIT_3, BIT_5, BIT_7, BIT_9, BIT_11, BIT_13
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
		for (j = 0; j < 7; j++) {
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
		BIT_0, BIT_2, BIT_4, BIT_6, BIT_8, BIT_10, BIT_12
	};
	uint32_t mask = 0, j = 0;
	unsigned long flags = 0;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	forcpy = (struct dcam_hw_force_copy *)arg;
	if (forcpy->idx < DCAM_ID_MAX) {
		for (j = 0; j < 7; j++) {
			if (forcpy->id & (1 << j))
				mask |= bitmap[j];
		}
	} else {
		mask = 0;
		pr_err("fail to get dev idx 0x%x exceed DCAM_ID_MAX\n", forcpy->idx);
	}

	pr_debug("DCAM%u: force copy 0x%0x, id 0x%x\n", forcpy->idx, mask, forcpy->id);
	if (mask == 0)
		return -EFAULT;

	spin_lock_irqsave(&forcpy->glb_reg_lock, flags);
	DCAM_REG_MWR(forcpy->idx, DCAM_CONTROL, mask, mask);
	spin_unlock_irqrestore(&forcpy->glb_reg_lock, flags);

	return 0;
}

void dcamhw_bypass_all(enum dcam_id idx, void *handle)
{
	struct cam_hw_info *hw = NULL;

	hw = (struct cam_hw_info *)handle;

	DCAM_REG_MWR(idx, DCAM_BIN_4IN1_CTRL0, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_BLC_PARA, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_RGBG_YRANDOM_PARAMETER0, BIT_0, 1);
	DCAM_REG_MWR(idx, ISP_PPI_PARAM, 0xF, 0x3);
	DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL0, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, 1);
	DCAM_REG_MWR(idx, ISP_AFL_PARAM0, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_BAYER_HIST_CTRL0, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_CROP0_CTRL, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_AWBC_GAIN0, BIT_31, 1 << 31);
	DCAM_REG_MWR(idx, DCAM_BPC_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_CROP1_CTRL, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_BWD1_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_NR3_FAST_ME_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_VST_PARA, BIT_0, 1);
	DCAM_REG_WR(idx, DCAM_NLM_PARA, 0x77);
	DCAM_REG_MWR(idx, DCAM_IVST_PARA, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_CROP3_CTRL, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_AFM_BIN_CTRL, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_AFM_GAMC_CTRL, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_AFM_FRM_CTRL, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_CFA_NEW_CFG0, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_CMC10_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_GTM_GLB_CTRL, BIT_0 | BIT_1, 3);
	DCAM_REG_MWR(idx, DCAM_FGAMMA10_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_HIST_ROI_CTRL0, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_CCE_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_YUV444TOYUV420_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_SCL0_CFG, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_CROP2_CTRL, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_DEC_ONLINE_PARAM, BIT_0 | BIT_1 | BIT_2, 0);
	DCAM_REG_MWR(idx, DCAM_STORE0_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_STORE4_PARAM, BIT_0, 1);
	DCAM_REG_MWR(idx, DCAM_VCH1_CONTROL, BIT_0, 0);
	DCAM_REG_MWR(idx, DCAM_VCH2_CONTROL, BIT_0, 0);
	DCAM_REG_MWR(idx, DCAM_VCH3_CONTROL, BIT_0, 0);
	if (hw->prj_id == QOGIRN6pro) {
		DCAM_REG_MWR(idx, DCAM_FBC_RAW_PARAM, BIT_0, 1);
		DCAM_REG_MWR(idx, DCAM_YUV_FBC_SCAL_PARAM, BIT_0, 1);
	}
	DCAM_REG_MWR(idx, DCAM_BUF_CTRL, BIT_0 | BIT_1 | BIT_2 | BIT_3 | BIT_4 | BIT_5, 0x3F);
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

	DCAM_REG_WR(idx, DCAM_INT0_EN, 0);
	DCAM_REG_WR(idx, DCAM_INT0_CLR, 0xFFFFFFFF);
	DCAM_REG_WR(idx, DCAM_INT1_EN, 0);
	DCAM_REG_WR(idx, DCAM_INT1_CLR, 0xFFFFFFFF);

	return 0;
}

static void dcamhw_axi_common_reset(struct cam_hw_info *hw, struct cam_hw_soc_info *soc,
	uint32_t flag, uint32_t ctrl_reg, uint32_t dbg_reg, uint32_t hw_idx)
{
	uint32_t time_out = 0;
	struct cam_hw_ip_info *ip = NULL;
	unsigned long lock_flag = 0;

	ip = hw->ip_dcam[hw_idx];
	DCAM_AXIM_MWR(DCAM_ID_0, ctrl_reg, BIT_24 | BIT_23, (0x3 << 23));

	/* then wait for AHB busy cleared */
	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if ((DCAM_AXIM_RD(DCAM_ID_0, dbg_reg) & 0x3F00F) == 0
			&& (DCAM_REG_RD(DCAM_ID_0, DCAM_PATH_BUSY) & 0x7FFF) == 0
			&& (DCAM_REG_RD(DCAM_ID_1, DCAM_PATH_BUSY) & 0x7FFF) == 0)
			break;
		os_adapt_time_udelay(1000);
	}
	write_lock_irqsave(&soc->cam_ahb_lock, lock_flag);
	if (time_out >= DCAM_AXI_STOP_TIMEOUT) {
		pr_err("fail to dcam axim timeout dbg_status 0x%x, dcam0/1 path_busy 0x%x, 0x%x\n",
			DCAM_AXIM_RD(DCAM_ID_0, dbg_reg),
			DCAM_REG_RD(DCAM_ID_0, DCAM_PATH_BUSY),
			DCAM_REG_RD(DCAM_ID_1, DCAM_PATH_BUSY));
	} else {
		/* reset dcam all (0/1/2/bus) */
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst, flag, flag);
		os_adapt_time_udelay(10);
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst, flag, ~flag);
	}

	write_unlock_irqrestore(&soc->cam_ahb_lock, lock_flag);
	hw->dcam_ioctl(hw, hw_idx, DCAM_HW_CFG_SET_QOS, &hw_idx);
	/* the end, enable AXI writing */
	DCAM_AXIM_MWR(hw_idx, ctrl_reg, BIT_24 | BIT_23, (0x0 << 23));
}

static int dcamhw_axi_reset(void *handle, void *arg)
{
	uint32_t idx = 0, flag = 0, ctrl_reg = 0, dbg_reg = 0, i = 0, mask = 0xffffffff;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;

	if (!handle || !arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	idx = *(uint32_t *)arg;
	soc = hw->soc_dcam;
	/* idx == DCAM_ID_MAX,dcam0 and dcam1 reset and axi reset*/
	ctrl_reg = AXIM_CTRL;
	dbg_reg = AXIM_DBG_STS;

	ip = hw->ip_dcam[DCAM_ID_0];
	flag = ip->syscon.all_rst_mask
		| ip->syscon.axi_rst_mask
		| ip->syscon.rst_mask
		| hw->ip_dcam[DCAM_ID_1]->syscon.rst_mask;
	dcamhw_axi_common_reset(hw, soc, flag, AXIM_CTRL, AXIM_DBG_STS, DCAM_ID_0);

	for(i = DCAM_ID_0; i <= DCAM_ID_1; i++) {
		DCAM_REG_MWR(i, DCAM_INT0_CLR, mask, mask);
		DCAM_REG_WR(i, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL);

		DCAM_REG_MWR(i, DCAM_INT1_CLR, mask, mask);
		DCAM_REG_WR(i, DCAM_INT1_EN, DCAMINT_IRQ_LINE_INT1_MASK);

		/* disable internal logic access sram */
		DCAM_REG_MWR(i, DCAM_APB_SRAM_CTRL, BIT_0, 0);
		DCAM_REG_WR(i, DCAM_MIPI_CAP_CFG, 0); /* disable all path */
		DCAM_REG_WR(i, DCAM_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);
	}

	pr_debug("dcam all & axi reset done\n");
	return 0;
}

static void dcamhw_common_reset(struct cam_hw_info *hw, struct cam_hw_soc_info *soc, uint32_t hw_idx)
{
	struct cam_hw_ip_info *ip = NULL;
	uint32_t flag = 0;

	ip = hw->ip_dcam[hw_idx];
	flag = ip->syscon.rst_mask;
	#if defined (PROJ_QOGIRN6L)
	flag |= ip->syscon.rst_mipi_mask;
	#endif
	pr_debug("DCAM%d, rst=0x%x, rst_mask=0x%x mipi_mask=0x%x\n",
		hw_idx, ip->syscon.rst, ip->syscon.rst_mask, ip->syscon.rst_mipi_mask);

	regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.rst, flag, flag);
	os_adapt_time_udelay(10);
	regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.rst, flag, ~flag);
}

static int dcamhw_reset(void *handle, void *arg)
{
	enum dcam_id idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;
	uint32_t time_out = 0;
	uint32_t mask = ~0;
	uint32_t sts_bit[DCAM_ID_MAX] = {
		BIT(12), BIT(13), BIT(14)
	};
	struct cam_hw_reg_trace trace;

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
		if (0 == (DCAM_AXIM_RD(idx, AXIM_DBG_STS) & (sts_bit[idx]))
			&& 0 == (DCAM_REG_RD(idx, DCAM_PATH_BUSY)))
			break;
		os_adapt_time_udelay(1000);
	}

	if (time_out >= DCAM_AXI_STOP_TIMEOUT) {
		pr_err("fail to reset DCAM%d timeout, axim status 0x%x, path_busy 0x%x\n", idx,
			DCAM_AXIM_RD(idx, AXIM_DBG_STS), DCAM_REG_RD(idx, DCAM_PATH_BUSY));
		trace.type = ABNORMAL_REG_TRACE;
		trace.idx = idx;
		hw->isp_ioctl(hw, ISP_HW_CFG_REG_TRACE, &trace);
	} else
		dcamhw_common_reset(hw, soc, idx);

	DCAM_REG_MWR(idx, DCAM_INT0_CLR, mask, mask);
	DCAM_REG_WR(idx, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL);

	DCAM_REG_MWR(idx, DCAM_INT1_CLR, mask, mask);
	DCAM_REG_WR(idx, DCAM_INT1_EN, DCAMINT_IRQ_LINE_INT1_MASK);

	/* disable internal logic access sram */
	DCAM_REG_MWR(idx, DCAM_APB_SRAM_CTRL, BIT_0, 0);
	DCAM_REG_WR(idx, DCAM_MIPI_CAP_CFG, 0); /* disable all path */
	DCAM_REG_WR(idx, DCAM_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);

	/* default bypass all blocks */
	dcamhw_bypass_all(idx, hw);
	sprd_iommu_restore(&hw->soc_dcam->pdev->dev);

	pr_info("DCAM%d: reset end\n", idx);
	return 0;
}

static int dcamhw_fetch_set(void *handle, void *arg)
{
	uint32_t val = 0, fetch_pitch = 0, bwu_shift = 0, is_pack = 0, data_bits = 0;
	struct dcam_hw_fetch_set *fetch = NULL;

	pr_debug("enter.\n");

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	fetch = (struct dcam_hw_fetch_set *)arg;
	is_pack = cam_is_pack(fetch->fetch_info->fmt);
	data_bits = cam_data_bits(fetch->fetch_info->fmt);
	fetch_pitch = cam_cal_hw_pitch(fetch->fetch_info->size.w, fetch->fetch_info->fmt);

	if (data_bits == CAM_10_BITS)
		bwu_shift = 0x4;
	else
		bwu_shift = 0;

	pr_info("size [%d %d], start %d, pitch %d, 0x%x fmt %s\n",
		fetch->fetch_info->trim.size_x, fetch->fetch_info->trim.size_y,
		fetch->fetch_info->trim.start_x, fetch_pitch, fetch->fetch_info->addr.addr_ch0,
		camport_fmt_name_get(fetch->fetch_info->fmt));
	/* (bitfile)unit 32b,(spec)64b */
	DCAM_REG_WR(fetch->idx, DCAM_INT0_CLR, 0xFFFFFFFF);
	if (fetch->virtualsensor_pre_sof)
		DCAM_REG_WR(fetch->idx, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL &
		(~BIT_2) | BIT(DCAM_IF_IRQ_INT0_PREIEW_SOF));
	else
		DCAM_REG_WR(fetch->idx, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL & (~BIT_2));

	DCAM_REG_WR(fetch->idx, DCAM_INT1_CLR, 0xFFFFFFFF);
	DCAM_REG_WR(fetch->idx, DCAM_INT1_EN, DCAMINT_IRQ_LINE_INT1_MASK);

	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG, BIT_12, 0x1 << 12);
	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG, BIT_1, 0x1 << 1);
	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG, BIT_3, 0x0 << 3);
	DCAM_REG_MWR(fetch->idx, DCAM_MIPI_CAP_CFG,
		BIT_5 | BIT_4, (fetch->fetch_info->pattern & 3) << 4);

	switch (fetch->fetch_info->fmt) {
	case CAM_RAW_PACK_10:
	case CAM_RAW_8:
		val = 0;
		break;
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		val = 1;
		break;
	case CAM_FULL_RGB14:
		val = FORMAT_FRGB_VAL;
		break;
	default:
		pr_err("fail to get valid fmt ,not support\n");
		break;
	}

	DCAM_AXIM_MWR(0, IMG_FETCH_CTRL, BIT_1 | BIT_0, val);
	DCAM_AXIM_MWR(0, IMG_FETCH_CTRL,
		BIT_3 | BIT_2, fetch->fetch_info->endian << 2);
	DCAM_AXIM_MWR(0, IMG_FETCH_CTRL, BIT_18 | BIT_17 | BIT_16, bwu_shift << 16);

	DCAM_AXIM_MWR(0, IMG_FETCH_CTRL, BIT_31, fetch->fetch_online_en << 31);

	DCAM_AXIM_WR(0, IMG_FETCH_SIZE,
		(fetch->fetch_info->trim.size_y << 16) | (fetch->fetch_info->trim.size_x & 0xffff));
	DCAM_AXIM_WR(0, IMG_FETCH_X,
		(fetch_pitch << 16) | (fetch->fetch_info->trim.start_x & 0x3fff));
	DCAM_AXIM_WR(0, IMG_FETCH_RADDR, fetch->fetch_info->addr.addr_ch0);

	DCAM_REG_WR(fetch->idx, DCAM_YUV444TO420_IMAGE_WIDTH, fetch->fetch_info->trim.size_x);
	DCAM_REG_MWR(fetch->idx, DCAM_YUV444TOYUV420_PARAM, BIT_0, 0);

	if (fetch->fetch_info->fmt == CAM_FULL_RGB14) {
		DCAM_REG_MWR(fetch->idx, DCAM_PATH_SEL, BIT_1, 1 << 1);
		DCAM_REG_MWR(fetch->idx, DCAM_GTM_GLB_CTRL, BIT_0, 1);
	}

	pr_info("done.\n");
	return 0;
}

static int dcamhw_mipi_cap_set(void *handle, void *arg)
{
	uint32_t idx = 0, reg_val = 0, image_vc = 0, image_mode = 1, image_data_type = IMG_TYPE_RAW10, bwu_shift = 4;
	struct dcam_mipi_info *cap_info = NULL;
	struct dcam_hw_mipi_cap *caparg = NULL;
	struct cam_hw_info *hw = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	caparg = (struct dcam_hw_mipi_cap *)arg;
	cap_info = &caparg->cap_info;
	idx = caparg->idx;

	if (idx > DCAM_ID_1){
		pr_err("fail to get invalid dcam index %d\n", idx);
		return 0;
	}
	/* set mipi interface  */
	if (cap_info->sensor_if != DCAM_CAP_IF_CSI2) {
		pr_err("fail to support sensor if : %d\n",
			cap_info->sensor_if);
		return -EINVAL;
	}

	/* data format */
	if (cap_info->format == DCAM_CAP_MODE_RAWRGB) {
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, 1 << 1);
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG,
			BIT_4 | BIT_5, cap_info->pattern << 4);
		/*raw 8:0 10:1 12:2 14:3*/
		reg_val = (cap_info->data_bits - 8) >> 1;
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG2, BIT_16 | BIT_17, reg_val << 16);
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG2, 0xFFFF,  cap_info->cap_size.size_x);
		if (cap_info->cap_size.size_x > DCAM_TOTAL_LBUF)
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG2, BIT_21, BIT_21);
	} else if (cap_info->format == DCAM_CAP_MODE_YUV) {
		if (unlikely(cap_info->data_bits != CAM_8_BITS)) {
			pr_err("fail to get valid data bits for yuv format %d\n",
				cap_info->data_bits);
			return -EINVAL;
		}

		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1,  0 << 1);
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_14 | BIT_15, cap_info->pattern << 14);

		/* x & y deci */
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG,
				BIT_16 | BIT_17 | BIT_18 | BIT_19,
				(cap_info->y_factor << 18)
				| (cap_info->x_factor << 16));
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_2 | BIT_3, BIT_2 | BIT_3);

		image_data_type = IMG_TYPE_YUV;
	} else {
		pr_err("fail to support capture format: %d\n",
			cap_info->format);
		return -EINVAL;
	}

	/* data mode */
#ifdef DEBUG_SINGLE_FRM
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_2, 0 << 2);
#else
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_2, cap_info->mode << 2);
#endif
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
		DCAM_REG_MWR(idx, DCAM_BIN_4IN1_CTRL0, BIT_1, 1 << 1);
		DCAM_REG_MWR(idx, DCAM_BIN_4IN1_CTRL0, BIT_2, 0 << 2);
	}
	DCAM_REG_MWR(idx, DCAM_BIN_4IN1_CTRL0, BIT_0, !cap_info->is_4in1);

	/* > 24M */
	if (cap_info->dcam_slice_mode) {
		DCAM_REG_MWR(idx, DCAM_BIN_4IN1_CTRL0, BIT_2, 1 << 2);
		DCAM_REG_MWR(idx, DCAM_BIN_4IN1_CTRL0, BIT_0, 1);
	}

	DCAM_REG_WR(idx, DCAM_YUV444TO420_IMAGE_WIDTH, cap_info->cap_size.size_x);
	DCAM_REG_MWR(idx, DCAM_YUV444TOYUV420_PARAM, BIT_0, 0);

	if (caparg->slowmotion_count) {
		/*slow motion enable*/
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_29, BIT_29);
		/*preview done mode*/
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_28, 0);
		DCAM_REG_MWR(idx, DCAM_BUF_CTRL, 0x3F << 10, caparg->slowmotion_count << 10);
	} else {
		/*slow motion disable*/
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_29, 0);
		/*preview done mode*/
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_28, BIT_28);
		DCAM_REG_MWR(idx, DCAM_BUF_CTRL, 0x3F << 10, 0);
	}

	reg_val = ((image_vc & 0x3) << 16) |
		((image_data_type & 0x3F) << 8) |
		((bwu_shift & 0x7) << 4) | (image_mode & 0x3);
	DCAM_REG_MWR(idx, DCAM_IMAGE_CONTROL, 0x33ff3, reg_val);

	pr_debug("cap size : %d %d %d %d\n",
		cap_info->cap_size.start_x, cap_info->cap_size.start_y,
		cap_info->cap_size.size_x, cap_info->cap_size.size_y);
	pr_debug("cap: frm %d, mode %d, bits %d, pattern %d, href %d\n",
		cap_info->format, cap_info->mode, cap_info->data_bits,
		cap_info->pattern, cap_info->href);
	pr_debug("cap: deci %d, skip %d, x %d, y %d, 4in1 %d, slowmotion count %d\n",
		cap_info->frm_deci, cap_info->frm_skip, cap_info->x_factor,
		cap_info->y_factor, cap_info->is_4in1, caparg->slowmotion_count);

	return 0;
}

static int dcamhw_path_start(void *handle, void *arg)
{
	uint32_t nr3_me_param = 0, image_data_type = IMG_TYPE_RAW10, val = 0, hwfmt = 0, data_bits = 0, is_pack = 0;
	struct isp_img_rect rect = {0};
	struct dcam_hw_path_start *patharg = NULL;

	pr_debug("enter.");

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	patharg = (struct dcam_hw_path_start *)arg;
	data_bits = cam_data_bits(patharg->out_fmt);
	is_pack = cam_is_pack(patharg->out_fmt);

	switch (patharg->path_id) {
	case DCAM_PATH_FULL:
		DCAM_REG_MWR(patharg->idx, DCAM_STORE4_PARAM, BIT_1, BIT_1);
		DCAM_REG_MWR(patharg->idx, DCAM_STORE4_PARAM,
			BIT_9 |  BIT_8, patharg->endian << 8);
		DCAM_REG_MWR(patharg->idx, DCAM_STORE4_PARAM, BIT_2, BIT_2);

		hwfmt = cal_dcamhw_format(patharg->out_fmt);
		DCAM_REG_MWR(patharg->idx, DCAM_STORE4_PARAM, 0x70, hwfmt << 4);

		val = (is_pack) ? 1 : 0;
		DCAM_REG_MWR(patharg->idx, DCAM_STORE4_PARAM, BIT_7, val << 7);
		val = (data_bits > CAM_8_BITS) ? 1 : 0;
		DCAM_REG_MWR(patharg->idx, DCAM_STORE4_PARAM, BIT_11, val << 11);

		if (data_bits == CAM_8_BITS) {
			/*bwd for yuv 8bit*/
			DCAM_REG_MWR(patharg->idx, DCAM_BWD2_PARAM, BIT_4, 1 << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_BWD2_PARAM, BIT_0, 0);
		} else
			DCAM_REG_MWR(patharg->idx, DCAM_BWD2_PARAM, BIT_0, 1);
		DCAM_REG_MWR(patharg->idx, DCAM_STORE4_PARAM, BIT_0, 0);
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(patharg->idx, DCAM_STORE0_PARAM, BIT_1, BIT_1);
		DCAM_REG_MWR(patharg->idx, DCAM_STORE0_PARAM,
			BIT_9 |  BIT_8, patharg->endian << 8);
		DCAM_REG_MWR(patharg->idx, DCAM_STORE0_PARAM, BIT_2, BIT_2);

		hwfmt = cal_dcamhw_format(patharg->out_fmt);
		DCAM_REG_MWR(patharg->idx, DCAM_STORE0_PARAM, 0x70, hwfmt << 4);

		val = (is_pack) ? 1 : 0;
		DCAM_REG_MWR(patharg->idx, DCAM_STORE0_PARAM, BIT_7, (is_pack) << 7);
		val = (data_bits > CAM_8_BITS) ? 1 : 0;
		DCAM_REG_MWR(patharg->idx, DCAM_STORE0_PARAM, BIT_11, val << 11);

		if (data_bits == CAM_8_BITS) {
			/*bwd for yuv 8bit*/
			DCAM_REG_MWR(patharg->idx, DCAM_BWD0_PARAM, BIT_4, 1 << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_BWD0_PARAM, BIT_0, 0);
		} else
			DCAM_REG_MWR(patharg->idx, DCAM_BWD0_PARAM, BIT_0, 1);

		DCAM_REG_MWR(patharg->idx, DCAM_STORE0_PARAM, BIT_0, 0);
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
		DCAM_REG_WR(patharg->idx, DCAM_NR3_FAST_ME_PARAM, nr3_me_param);
		dcam_k_3dnr_set_roi(rect, 0/* project_mode=0 */, patharg->idx);
		break;
	case DCAM_PATH_RAW:
		/* when dcam0 & dcam1 vch3 share mode eb, vch2_3 share mode shuould disable. */
		DCAM_REG_MWR(patharg->idx, DCAM_BUF_CTRL, BIT_6 | BIT_7, 0);
		/*max len sel: default 1*/
		DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_6 | BIT_7, 0x1 << 6);
		DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_9 | BIT_8, patharg->endian << 8);
		DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_14, 0 << 14);
		DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_15, 0 << 15);

		if (patharg->src_sel == ORI_RAW_SRC_SEL) {
			val = data_bits - 10;
			DCAM_REG_MWR(patharg->idx, DCAM_IMAGE_CONTROL, 0x70, val << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_13, BIT_13);
		} else if (patharg->src_sel == LSC_RAW_SRC_SEL) {
			val = data_bits - 10;
			DCAM_REG_MWR(patharg->idx, DCAM_IMAGE_CONTROL, 0x70, val << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_5 | BIT_4, 1 << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_13, 0 << 13);
			val = 14 - 10;
		} else if (patharg->src_sel == BPC_RAW_SRC_SEL) {
			val = data_bits - 10;
			DCAM_REG_MWR(patharg->idx, DCAM_IMAGE_CONTROL, 0x70, val << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_5 | BIT_4, 2 << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_13, 0 << 13);
			val = 14 - 10;
		} else if (patharg->src_sel == NLM_RAW_SRC_SEL) {
			val = data_bits - 10;
			DCAM_REG_MWR(patharg->idx, DCAM_IMAGE_CONTROL, 0x70, val << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_5 | BIT_4, 3 << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_13, 0 << 13);
			val = 14 - 10;
		} else if (patharg->src_sel == PROCESS_RAW_SRC_SEL) {
			val = data_bits - 10;
			DCAM_REG_MWR(patharg->idx, DCAM_IMAGE_CONTROL, 0x70, val << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_5 | BIT_4, 0 << 4);
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_13, 0 << 13);
		} else
			/*default 14bit down to 10bit*/
			val = 14 - 10;
		DCAM_REG_MWR(patharg->idx, DCAM_BWD1_PARAM, 0xE, val << 1);

		if (is_pack)
			val = 0;
		else {
			if (data_bits == CAM_10_BITS)
				val = 1;
			else if (data_bits == CAM_14_BITS)
				val = 2;
		}
		if (data_bits== CAM_8_BITS)
			val = 3;
		DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_2 | BIT_3, val << 2);
		val = (data_bits - CAM_8_BITS) >> 1;
		DCAM_REG_MWR(patharg->idx, DCAM_BWD1_PARAM, BIT_4 | BIT_5, val << 4);
		if (data_bits == CAM_14_BITS)
			val = 0;
		else
			val = 4;
		DCAM_REG_MWR(patharg->idx, DCAM_BWU1_PARAM, 0x70, val << 4);
		/* bwd for RAW 10bit */
		DCAM_REG_MWR(patharg->idx, DCAM_BWD1_PARAM, BIT_0, 0);
		if (patharg->cap_info.format ==  DCAM_CAP_MODE_YUV || patharg->cap_info.cap_size.size_x > DCAM_TOTAL_LBUF)
			DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_12, 1 << 12);
		DCAM_REG_MWR(patharg->idx, DCAM_RAW_PATH_CFG, BIT_0, 0);
		break;
	case DCAM_PATH_PDAF:
		/* pdaf path en */
		if (patharg->pdaf_path_eb) {
			if (patharg->pdaf_type == DCAM_PDAF_TYPE3)
				DCAM_REG_MWR(patharg->idx, DCAM_PPE_FRM_CTRL0, BIT_0, 1);
			else if (patharg->pdaf_type == DCAM_PDAF_TYPE2) {
				DCAM_REG_MWR(patharg->idx, DCAM_MIPI_CAP_CFG1, BIT_0, BIT_0);
				DCAM_REG_MWR(patharg->idx, DCAM_BUF_CTRL, BIT_6 | BIT_7, 0);
				DCAM_REG_MWR(patharg->idx, DCAM_VCH3_CONTROL, BIT_0, 1);
			} else
				DCAM_REG_MWR(patharg->idx, DCAM_VCH1_CONTROL, BIT_0, 1);
		}
		break;
	case DCAM_PATH_VCH2:
		if (data_bits == CAM_8_BITS)
			image_data_type = IMG_TYPE_RAW8;
		/* data type for raw picture */
		if (patharg->src_sel)
			DCAM_REG_WR(patharg->idx, DCAM_VCH2_CONTROL, image_data_type << 8 | 0x01 << 4);

		DCAM_REG_MWR(patharg->idx, DCAM_VCH2_CONTROL,
			BIT_21 |  BIT_20, patharg->endian << 20);

		DCAM_REG_MWR(patharg->idx, DCAM_MIPI_CAP_CFG1, BIT_0, BIT_0);
		DCAM_REG_MWR(patharg->idx, DCAM_BUF_CTRL, BIT_6 | BIT_7, 0);

		/*vch2 path en */
		DCAM_REG_MWR(patharg->idx, DCAM_VCH2_CONTROL, BIT_0, 1);
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_MWR(patharg->idx, DCAM_VCH3_CONTROL,
			BIT_21 | BIT_20, patharg->endian << 20);
		/*vch3 path en */
		DCAM_REG_MWR(patharg->idx, DCAM_VCH3_CONTROL, BIT_0, 1);
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
		DCAM_REG_MWR(pathctl->idx, DCAM_STORE4_PARAM, BIT_0, !pathctl->type);
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(pathctl->idx, DCAM_STORE0_PARAM, BIT_0, !pathctl->type);
		break;
	case DCAM_PATH_RAW:
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_0, !pathctl->type);
		break;
	case DCAM_PATH_PDAF:
		DCAM_REG_MWR(pathctl->idx, DCAM_PPE_FRM_CTRL0, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_VCH2:
		DCAM_REG_MWR(pathctl->idx, DCAM_VCH2_CONTROL, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_MWR(pathctl->idx, DCAM_VCH3_CONTROL, BIT_0, pathctl->type);
		break;
	default:
		break;
	}

	return 0;
}

static int dcamhw_fetch_start(void *handle, void *arg)
{
	DCAM_AXIM_WR(0, IMG_FETCH_START, 1);

	return 0;
}

static void dcamhw_path_scaler_coeff_set(uint32_t idx,uint32_t *coeff_buf)
{
	int k = 0, c = 0;
	uint32_t *tmp_h_coeff = NULL;
	uint32_t *tmp_h_chroma_coeff = NULL;
	struct coeff_arg arg = {0};

	arg.h_coeff = coeff_buf;
	arg.h_chroma_coeff = coeff_buf + (ISP_SC_COEFF_COEF_SIZE / 4);
	arg.v_coeff = coeff_buf + (ISP_SC_COEFF_COEF_SIZE * 2 / 4);
	arg.v_chroma_coeff = coeff_buf + (ISP_SC_COEFF_COEF_SIZE * 3 / 4);
	arg.v_coeff_addr = DCAM_SCL0_VER_COEF_Y;
	arg.v_chroma_coeff_addr = DCAM_SCL0_VER_COEF_UV;

	for (k = 0; k < 4; k++) {
		if (k == 0) {
			arg.h_coeff_addr = DCAM_SCL0_HOR_COEF0_Y;
			arg.h_chroma_coeff_addr= DCAM_SCL0_HOR_COEF0_UV;
		} else if (k == 1) {
			arg.h_coeff_addr = DCAM_SCL0_HOR_COEF1_Y;
			arg.h_chroma_coeff_addr= DCAM_SCL0_HOR_COEF1_UV;
		} else if (k == 2) {
			arg.h_coeff_addr = DCAM_SCL0_HOR_COEF2_Y;
			arg.h_chroma_coeff_addr= DCAM_SCL0_HOR_COEF2_UV;
		} else if (k == 3) {
			arg.h_coeff_addr = DCAM_SCL0_HOR_COEF3_Y;
			arg.h_chroma_coeff_addr= DCAM_SCL0_HOR_COEF3_UV;
		}

		/*h y*/
		tmp_h_coeff = arg.h_coeff + k * COEF_HOR_Y_SIZE;
		for (c = 0; c < COEF_HOR_Y_SIZE; c++) {
			DCAM_REG_WR(idx, arg.h_coeff_addr + c * 4, *(tmp_h_coeff));
			tmp_h_coeff++;
		}
		/*h uv*/
		tmp_h_chroma_coeff = arg.h_chroma_coeff + k * COEF_HOR_UV_SIZE;
		for (c = 0; c < COEF_HOR_UV_SIZE; c++) {
			DCAM_REG_WR(idx, arg.h_chroma_coeff_addr + c * 4, *(tmp_h_chroma_coeff));
			tmp_h_chroma_coeff++;
		}
	}

	for (k = 0; k < COEF_VOR_Y_SIZE; k++) {
		DCAM_REG_WR(idx, arg.v_coeff_addr, *arg.v_coeff);
		arg.v_coeff_addr += 4;
		arg.v_coeff++;
	}

	for (k = 0; k < COEF_VOR_UV_SIZE; k++) {
		DCAM_REG_WR(idx, arg.v_chroma_coeff_addr, *arg.v_chroma_coeff);
		arg.v_chroma_coeff_addr += 4;
		arg.v_chroma_coeff++;
	}

}

static int dcamhw_path_size_update(void *handle, void *arg)
{
	uint32_t idx = 0, reg_val = 0, fbc_nfull_level = 0;
	struct dcam_hw_path_size *sizearg = NULL;
	struct isp_img_rect rect = {0}; /* for 3dnr path */

	pr_debug("enter.");
	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	sizearg = (struct dcam_hw_path_size *)arg;
	idx = sizearg->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;
	pr_debug("sizearg->path_id:%d in_size:%d %d, out_size:%d %d in_trim:%d %d %d %d\n", sizearg->path_id,
		sizearg->in_size.w, sizearg->in_size.h, sizearg->out_size.w, sizearg->out_size.h,
		sizearg->in_trim.start_x, sizearg->in_trim.start_y, sizearg->in_trim.size_x, sizearg->in_trim.size_y);

	switch (sizearg->path_id) {
	case DCAM_PATH_FULL:
		if ((sizearg->in_size.w > sizearg->in_trim.size_x) ||
			(sizearg->in_size.h > sizearg->in_trim.size_y)) {

			DCAM_REG_MWR(idx, DCAM_CROP2_CTRL, BIT_0, 0);
			reg_val = (sizearg->in_trim.start_y << 16) |
						sizearg->in_trim.start_x;
			DCAM_REG_WR(idx, DCAM_CROP2_START, reg_val);
			reg_val = (sizearg->in_trim.size_y << 16) |
						sizearg->in_trim.size_x;
			DCAM_REG_WR(idx, DCAM_CROP2_SIZE, reg_val);
		} else
			DCAM_REG_MWR(idx, DCAM_CROP2_CTRL, BIT_0, 1);
		reg_val = (sizearg->out_size.h << 16) | sizearg->out_size.w;
		DCAM_REG_WR(idx, DCAM_STORE4_SLICE_SIZE, reg_val);
		if (sizearg->compress_info.is_compress) {
			DCAM_REG_WR(idx, DCAM_FBC_RAW_SLICE_SIZE, reg_val);
			DCAM_REG_WR(idx, DCAM_FBC_RAW_SLICE_TILE_PITCH, sizearg->compress_info.tile_col);
			/*recommended value from asic nfull = size_in_hor/8 - 80 for fbc is too slow*/
			fbc_nfull_level = (sizearg->out_size.w >> 3) - 80;
			reg_val = fbc_nfull_level > 0 ? ((fbc_nfull_level << 16) | fbc_nfull_level) : 0;
			DCAM_REG_WR(idx, DCAM_FBC_RAW_NFULL_LEVEL, reg_val);

		}

		DCAM_REG_WR(idx, DCAM_STORE4_Y_PITCH, sizearg->out_pitch);
		DCAM_REG_WR(idx, DCAM_STORE4_U_PITCH, sizearg->out_pitch);
		break;
	case DCAM_PATH_BIN:
		/* scaler0 cfg */
		reg_val = (sizearg->in_size.h << 16) | sizearg->in_size.w;
		DCAM_REG_WR(idx, DCAM_SCL0_SRC_SIZE, reg_val);

		reg_val = (sizearg->out_size.h << 16) | sizearg->out_size.w;
		DCAM_REG_WR(idx, DCAM_SCL0_DES_SIZE, reg_val);

		reg_val = (sizearg->in_trim.start_y << 16) | sizearg->in_trim.start_x;
		DCAM_REG_WR(idx, DCAM_SCL0_TRIM0_START, reg_val);

		reg_val = (sizearg->in_trim.size_y << 16) | sizearg->in_trim.size_x;
		DCAM_REG_WR(idx, DCAM_SCL0_TRIM0_SIZE, reg_val);

		/*set X/Y deci */
		DCAM_REG_MWR(idx, DCAM_SCL0_CFG, BIT_6, sizearg->deci.deci_x_eb << 6);
		DCAM_REG_MWR(idx, DCAM_SCL0_CFG, (BIT_4 | BIT_5), sizearg->deci.deci_x << 4);
		DCAM_REG_MWR(idx, DCAM_SCL0_CFG, BIT_9, sizearg->deci.deci_y_eb << 9);
		DCAM_REG_MWR(idx, DCAM_SCL0_CFG, (BIT_7 | BIT_8), sizearg->deci.deci_y << 7);
		DCAM_REG_MWR(idx, DCAM_SCL0_CFG, BIT_0, 0);

		/* PINGPONG BUF CONTROL  SW MODE*/
		DCAM_REG_MWR(idx, DCAM_BUF_CTRL, BIT_3, BIT_3);

		pr_debug("scaler_sel %d deci_x_eb %d deci_x %d deci_y_eb %d deci_y %d\n", sizearg->scaler_sel, sizearg->deci.deci_x_eb, sizearg->deci.deci_x, sizearg->deci.deci_y_eb, sizearg->deci.deci_y);
		if (sizearg->scaler_sel == PORT_SCALER_BYPASS) {
			DCAM_REG_MWR(idx, DCAM_SCL0_CFG, BIT_1, BIT_1);
			if ((sizearg->in_size.w == sizearg->out_size.w) && (sizearg->in_size.h == sizearg->out_size.h))
				DCAM_REG_MWR(idx, DCAM_SCL0_CFG, BIT_0, 1);
		} else if (sizearg->scaler_sel == PORT_SCALER_BY_YUVSCALER) {
			DCAM_REG_MWR(idx, DCAM_SCL0_CFG, BIT_1, 0 << 1);

			DCAM_REG_MWR(idx, DCAM_SCL0_CFG, 0x1f << 11, sizearg->scaler_info->scaler_uv_ver_tap << 11);
			DCAM_REG_MWR(idx, DCAM_SCL0_CFG, 0xf << 16, sizearg->scaler_info->scaler_y_ver_tap << 16);

			reg_val = ((sizearg->scaler_info->init_phase_info.scaler_init_phase_int[0][0] & 0x1F) << 16) |
				(sizearg->scaler_info->init_phase_info.scaler_init_phase_rmd[0][0] & 0x3FFF);
			DCAM_REG_WR(idx, DCAM_SCL0_IP, reg_val);

			reg_val = ((sizearg->scaler_info->init_phase_info.scaler_init_phase_int[0][1] & 0x1F) << 16) |
				(sizearg->scaler_info->init_phase_info.scaler_init_phase_rmd[0][1] & 0x3FFF);
			DCAM_REG_WR(idx, DCAM_SCL0_CIP, reg_val);

			reg_val = (sizearg->scaler_info->scaler_factor_in << 16) |
				sizearg->scaler_info->scaler_factor_out;
			DCAM_REG_WR(idx, DCAM_SCL0_FACTOR, reg_val);

			reg_val = (sizearg->scaler_info->scaler_ver_factor_in << 16) |
				sizearg->scaler_info->scaler_ver_factor_out;
			DCAM_REG_WR(idx, DCAM_SCL0_VER_FACTOR, reg_val);

			reg_val = ((sizearg->scaler_info->init_phase_info.scaler_init_phase_int[1][0] & 0x1F) << 16) |
				(sizearg->scaler_info->init_phase_info.scaler_init_phase_rmd[1][0] & 0x3FFF);
			DCAM_REG_WR(idx, DCAM_SCL0_VER_IP, reg_val);

			reg_val = ((sizearg->scaler_info->init_phase_info.scaler_init_phase_int[1][1] & 0x1F) << 16) |
				(sizearg->scaler_info->init_phase_info.scaler_init_phase_rmd[1][1] & 0x3FFF);
			DCAM_REG_WR(idx, DCAM_SCL0_VER_CIP, reg_val);

			dcamhw_path_scaler_coeff_set(idx, sizearg->scaler_info->coeff_buf);

			reg_val = DCAM_REG_RD(idx, DCAM_BUF_CTRL);
			reg_val = reg_val & BIT_19;
			DCAM_REG_MWR(idx, DCAM_BUF_CTRL, BIT_19, ~reg_val);
		} else
			pr_err("fail to get invalid scaler_sel %d\n", sizearg->scaler_sel);

		DCAM_REG_WR(idx, DCAM_SCL0_TRIM1_START, 0);
		reg_val = (sizearg->out_size.h << 16) | sizearg->out_size.w;
		DCAM_REG_WR(idx, DCAM_SCL0_TRIM1_SIZE, reg_val);

		DCAM_REG_MWR(idx, DCAM_SCL0_BWD_PARA, BIT_0, 1);

		/* yuv444to420 cfg */
		DCAM_REG_WR(idx, DCAM_YUV444TO420_IMAGE_WIDTH, sizearg->in_size.w);

		/* store0 cfg */
		reg_val = (sizearg->out_size.h<< 16) | sizearg->out_size.w;
		DCAM_REG_WR(idx, DCAM_STORE0_SLICE_SIZE, reg_val);

		/* fbc pitch */
		if (sizearg->compress_info.is_compress) {
			reg_val = (sizearg->out_size.h << 16) | sizearg->out_size.w;
			DCAM_REG_WR(idx, DCAM_YUV_FBC_SCAL_SLICE_SIZE, reg_val);
			DCAM_REG_WR(idx, DCAM_YUV_FBC_SCAL_TILE_PITCH, sizearg->compress_info.tile_col);
			/*recommended value from asic nfull = size_in_hor/8 - 80 for fbc is too slow*/
			fbc_nfull_level = (sizearg->out_size.w >> 3) - 80;
			reg_val = fbc_nfull_level > 0 ? ((fbc_nfull_level << 16) | fbc_nfull_level) : 0;
			DCAM_REG_WR(idx, DCAM_YUV_FBC_SCAL_NFULL_LEVEL, reg_val);
		}

		/* store0 pitch */
		DCAM_REG_WR(idx, DCAM_STORE0_Y_PITCH, sizearg->out_pitch);
		DCAM_REG_WR(idx, DCAM_STORE0_U_PITCH, sizearg->out_pitch);

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
	case DCAM_PATH_RAW:
		if ((sizearg->in_size.w > sizearg->in_trim.size_x) ||
			(sizearg->in_size.h > sizearg->in_trim.size_y)) {
			if (sizearg->compress_info.is_compress) {
				DCAM_REG_MWR(idx, DCAM_CROP1_CTRL, BIT_0, 0);
				DCAM_REG_MWR(idx, DCAM_RAW_PATH_CFG, BIT_1, 0);
				reg_val = (sizearg->in_trim.start_y << 16) | sizearg->in_trim.start_x;
				DCAM_REG_WR(idx, DCAM_CROP1_START, reg_val);
				reg_val = (sizearg->in_trim.size_y << 16) | sizearg->in_trim.size_x;
				DCAM_REG_WR(idx, DCAM_CROP1_SIZE, reg_val);
			} else {
				DCAM_REG_MWR(idx, DCAM_RAW_PATH_CFG, BIT_1, BIT_1);
				DCAM_REG_MWR(idx, DCAM_CROP1_CTRL, BIT_0, 0);
				reg_val = (sizearg->in_trim.start_y << 16) | sizearg->in_trim.start_x;
				DCAM_REG_WR(idx, DCAM_RAW_PATH_CROP_START, reg_val);
				reg_val = (sizearg->in_trim.size_y << 16) | sizearg->in_trim.size_x;
				DCAM_REG_WR(idx, DCAM_RAW_PATH_CROP_SIZE, reg_val);
				reg_val = (sizearg->in_trim.start_y << 16) | sizearg->in_trim.start_x;
				DCAM_REG_WR(idx, DCAM_CROP1_START, reg_val);
				reg_val = (sizearg->in_trim.size_y << 16) | sizearg->in_trim.size_x;
				DCAM_REG_WR(idx, DCAM_CROP1_SIZE, reg_val);
			}
		} else {
			DCAM_REG_MWR(idx, DCAM_RAW_PATH_CFG, BIT_1, 0);
			DCAM_REG_MWR(idx, DCAM_CROP1_CTRL, BIT_0, 1);
		}
		reg_val = (sizearg->out_size.h << 16) | sizearg->out_size.w;
		DCAM_REG_WR(idx, DCAM_RAW_PATH_SIZE, reg_val);
		if (sizearg->compress_info.is_compress) {
			DCAM_REG_WR(idx, DCAM_FBC_RAW_SLICE_SIZE, reg_val);
			DCAM_REG_WR(idx, DCAM_FBC_RAW_SLICE_TILE_PITCH, sizearg->compress_info.tile_row);
			DCAM_REG_WR(idx, DCAM_FBC_RAW_LOWBIT_PITCH, sizearg->compress_info.tile_row_lowbit);
			/*recommended value from asic nfull = size_in_hor/8 - 80 for fbc is too slow*/
			fbc_nfull_level = (sizearg->out_size.w >> 3) - 80;
			reg_val = fbc_nfull_level > 0 ? ((fbc_nfull_level << 16) | fbc_nfull_level) : 0;
			DCAM_REG_WR(idx, DCAM_FBC_RAW_NFULL_LEVEL, reg_val);
		}

		reg_val = sizearg->out_pitch * 8 / 128;
		DCAM_REG_MWR(idx, DCAM_RAW_PATH_CFG, 0x7FF << 20, reg_val << 20);
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
	/* dcam0, dcam1 */
		6528, 3840,
		6016, 4352,
		5696, 4672,
		5184, 5184,
		3840, 6528,
		4352, 6016,
		4672, 5696,
		5184, 5184,
	};
	struct cam_hw_lbuf_share *camarg = (struct cam_hw_lbuf_share *)arg;
	uint32_t dcam0_mipi_en = 0, dcam1_mipi_en = 0;

	dcam0_mipi_en = DCAM_REG_RD(0, DCAM_MIPI_CAP_CFG) & BIT_0;
	dcam1_mipi_en = DCAM_REG_RD(1, DCAM_MIPI_CAP_CFG) & BIT_0;
	pr_debug("dcam %d offline %d en0 %d en1 %d pdaf share %d\n", camarg->idx, camarg->offline_flag,
		dcam0_mipi_en, dcam1_mipi_en,camarg->pdaf_share_flag);
	if (camarg->pdaf_share_flag && camarg->idx == 0)
		DCAM_AXIM_MWR(camarg->idx, DCAM_LBUF_SHARE_MODE, BIT_12 | BIT_13, 1 << 12);
	if (camarg->pdaf_share_flag && camarg->idx == 1)
		DCAM_AXIM_MWR(camarg->idx, DCAM_LBUF_SHARE_MODE, BIT_12 | BIT_13, 2 << 12);
	if (!camarg->offline_flag && (dcam0_mipi_en || dcam1_mipi_en)) {
		pr_debug("dcam 0/1 already in working\n");
		return 0;
	}
	switch (camarg->idx) {
	case 0:
		if (camarg->width > tb_w[0]) {
			DCAM_AXIM_MWR(camarg->idx, DCAM_LBUF_SHARE_MODE, 0x7, 3);
			break;
		}
		for (i = 3; i >= 0; i--) {
			if (camarg->width <= tb_w[i * 2]) {
				DCAM_AXIM_MWR(camarg->idx, DCAM_LBUF_SHARE_MODE, 0x7, i);
				pr_debug("alloc dcam%d linebuf %d %d\n", camarg->idx, tb_w[i*2], tb_w[i*2 + 1]);
				break;
			}
		}
		break;
	case 1:
		if (camarg->width > tb_w[9]) {
			DCAM_AXIM_MWR(camarg->idx, DCAM_LBUF_SHARE_MODE, 0x7, 3);
			break;
		}
		for (i = 7; i >= 4; i--) {
			if (camarg->width <= tb_w[i * 2 + 1]) {
				DCAM_AXIM_MWR(camarg->idx, DCAM_LBUF_SHARE_MODE, 0x7, i);
				pr_debug("alloc dcam%d linebuf %d %d\n", camarg->idx, tb_w[i*2], tb_w[i*2 + 1]);
				break;
			}
		}
		break;
	default:
		pr_err("fail to get valid dcam id %d\n", camarg->idx);
		ret = 1;
	}
	DCAM_AXIM_MWR(camarg->idx, DCAM_LBUF_SHARE_MODE, 0x3 << 8, 0 << 8);

	return ret;
}

static int dcamhw_lbuf_share_get(void *handle, void *arg)
{
	int i = 0, idx = 0;
	uint32_t num = 0;
	struct cam_hw_lbuf_share *camarg = (struct cam_hw_lbuf_share *)arg;
	uint32_t tb_w[] = {
	/*     dcam0, dcam1 */
		6528, 3840,
		6016, 4352,
		5696, 4672,
		5184, 5184,
		3840, 6528,
		4352, 6016,
		4672, 5696,
		5184, 5184,
	};

	if (!arg)
		return -EFAULT;

	idx = camarg->idx;
	camarg->width = 3840;

	if (idx > DCAM_ID_1)
		goto exit;

	i = DCAM_AXIM_RD(idx, DCAM_LBUF_SHARE_MODE) & 7;
	num = sizeof(tb_w) / sizeof(tb_w[0]);
	if (i < num / 2)
		camarg->width = tb_w[i * 2 + idx];

exit:
	pr_debug("dcam%d, lbuf %d\n", idx, camarg->width);
	return 0;
}

static int dcamhw_slice_fetch_set(void *handle, void *arg)
{
	uint32_t idx = 0, fetch_pitch = 0, bwu_shift = 0, val = 0, data_bits = 0;
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
	data_bits = cam_data_bits(fetch->fmt);

	if (data_bits == CAM_10_BITS)
		bwu_shift = 0x4;
	else
		bwu_shift = 0;

	fetch_pitch = cam_cal_hw_pitch(fetch->size.w, fetch->fmt);

	switch (fetch->fmt) {
	case CAM_RAW_PACK_10:
	case CAM_RAW_8:
		val = 0;
		break;
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		val = 1;
		break;
	case CAM_FULL_RGB14:
		val = FORMAT_FRGB_VAL;
		break;
	default:
		pr_err("fail to get valid fmt ,not support\n");
		break;
	}

	DCAM_REG_WR(idx, DCAM_INT0_CLR, 0xFFFFFFFF);
	if (slicearg->virtualsensor_pre_sof)
		DCAM_REG_WR(idx, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL &
		(~BIT_2) | BIT(DCAM_IF_IRQ_INT0_PREIEW_SOF));
	else
		DCAM_REG_WR(idx, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL & (~BIT_2));
	DCAM_REG_WR(idx, DCAM_INT1_CLR, 0xFFFFFFFF);
	DCAM_REG_WR(idx, DCAM_INT1_EN, DCAMINT_IRQ_LINE_INT1_MASK);

	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_5 | BIT_4, (fetch->pattern & 3) << 4);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_28, 0x1 << 28);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_29, 0x1 << 29);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_30, slicearg->is_last_slice << 30);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_12, 0x1 << 12);
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, 0x1 << 1);

	DCAM_AXIM_WR(idx, IMG_FETCH_RADDR, fetch->addr.addr_ch0 + slicearg->fetch_offset);
	DCAM_AXIM_MWR(idx, IMG_FETCH_CTRL, BIT_1 | BIT_0, val);
	DCAM_AXIM_MWR(idx, IMG_FETCH_CTRL, BIT_3 | BIT_2, fetch->endian << 2);
	DCAM_AXIM_MWR(idx, IMG_FETCH_CTRL, BIT_18 | BIT_17 | BIT_16, bwu_shift << 16);
	DCAM_AXIM_WR(idx, IMG_FETCH_SIZE, (slicearg->fetch_size.h << 16) |
		(slicearg->fetch_size.w & 0x3fff));
	DCAM_AXIM_WR(idx, IMG_FETCH_X, (fetch_pitch << 16) | (0 & 0x3fff));

	return 0;
}

static int dcamhw_slice_store_set(void *handle, void *arg)
{
	uint32_t idx = 0, reg_val = 0, start_title_num = 0, addr = 0, addr_offset = 0, reg_header = 0, reg_addr = 0;
	struct img_trim *cur_slice = NULL;
	struct dcam_hw_slice_param *slicearg = NULL;
	struct dcam_hw_scaler *bin_scaler = NULL;
	struct dcam_hw_scaler *full_scaler = NULL;
	struct dcam_slice_store_param *bin_store = NULL;
	struct dcam_slice_store_param *full_store = NULL;

	if (!arg) {
		pr_err("fail to check param\n");
		return -1;
	}

	slicearg = (struct dcam_hw_slice_param *)arg;
	cur_slice = slicearg->cur_slice;
	idx = slicearg->idx;
	bin_scaler = &slicearg->bin_scaler;
	bin_store = &slicearg->bin_store;
	full_scaler = &slicearg->full_scaler;
	full_store = &slicearg->full_store;

	if (!slicearg->is_compress) {
		if (bin_store->bin_en) {
			DCAM_REG_WR(idx, DCAM_SCL0_SRC_SIZE,
				(bin_scaler->src_size.h << 16) | (bin_scaler->src_size.w & 0x3fff));
			DCAM_REG_WR(idx, DCAM_SCL0_DES_SIZE,
				(bin_scaler->dst_size.h << 16) | (bin_scaler->dst_size.w & 0x3fff));
			DCAM_REG_WR(idx, DCAM_SCL0_TRIM0_START,
				(bin_scaler->im_trim0.start_y << 16) | (bin_scaler->im_trim0.start_x & 0x3fff));
			DCAM_REG_WR(idx, DCAM_SCL0_TRIM0_SIZE,
				(bin_scaler->im_trim0.size_y << 16) | (bin_scaler->im_trim0.size_x & 0x3fff));
			DCAM_REG_WR(idx, DCAM_SCL0_TRIM1_START,
				(bin_scaler->im_trim1.start_y << 16) | (bin_scaler->im_trim1.start_x & 0x3fff));
			DCAM_REG_WR(idx, DCAM_SCL0_TRIM1_SIZE,
				(bin_scaler->im_trim1.size_y << 16) | (bin_scaler->im_trim1.size_x & 0x3fff));
			DCAM_REG_WR(idx, DCAM_STORE0_BORDER,
				(bin_store->out_border.right_border << 16) | (bin_store->out_border.left_border & 0xffff));
			DCAM_REG_MWR(idx, DCAM_STORE0_SLICE_SIZE, (0xFFFF << 16) | 0xFFFF,
				(bin_store->store_size.h << 16) | (bin_store->store_size.w & 0xffff));
			reg_val = DCAM_REG_RD(idx, DCAM_STORE0_SLICE_Y_ADDR);
			DCAM_REG_WR(idx, DCAM_STORE0_SLICE_Y_ADDR, reg_val + bin_store->store_offset[0]);
			reg_val = DCAM_REG_RD(idx, DCAM_STORE0_SLICE_U_ADDR);
			DCAM_REG_WR(idx, DCAM_STORE0_SLICE_U_ADDR, reg_val + bin_store->store_offset[1]);
		}

		DCAM_REG_MWR(idx, DCAM_CROP2_START, (0xFFFF << 16) | 0xFFFF,
			(full_scaler->im_trim0.start_y << 16) | (full_scaler->im_trim0.start_x & 0xffff));
		DCAM_REG_MWR(idx, DCAM_CROP2_SIZE, (0xFFFF << 16) | 0xFFFF,
			(full_scaler->im_trim0.size_y << 16) | (full_scaler->im_trim0.size_x & 0xffff));
		DCAM_REG_WR(idx, DCAM_STORE4_BORDER,
			(full_store->out_border.right_border << 16) | (full_store->out_border.left_border & 0xffff));
		DCAM_REG_MWR(idx, DCAM_STORE4_SLICE_SIZE, (0xFFFF << 16) | 0xFFFF,
			(full_store->store_size.h << 16) | (full_store->store_size.w & 0xffff));
		reg_val = DCAM_REG_RD(idx, DCAM_STORE4_SLICE_Y_ADDR);
		DCAM_REG_WR(idx, DCAM_STORE4_SLICE_Y_ADDR, reg_val + full_store->store_offset[0]);
		reg_val = DCAM_REG_RD(idx, DCAM_STORE4_SLICE_U_ADDR);
		DCAM_REG_WR(idx, DCAM_STORE4_SLICE_U_ADDR, reg_val + full_store->store_offset[1]);
	} else {
		start_title_num = (cur_slice->start_x - slicearg->relative_offset + 31) / 32;

		DCAM_REG_WR(idx, DCAM_FBC_RAW_BORDER, full_store->fbc_border.left_border << 16);
		DCAM_REG_MWR(idx, DCAM_FBC_RAW_SLICE_SIZE, (0x1FFF << 16) | 0x1FFF,
			(full_store->store_size.h << 16) | (full_store->store_size.w & 0x1fff));

		reg_header = DCAM_REG_RD(idx, DCAM_FBC_RAW_SLICE_Y_HEADER);
		addr = reg_header + FBC_STORE_ADDR_ALIGN * start_title_num;
		DCAM_REG_WR(idx, DCAM_FBC_RAW_SLICE_Y_HEADER, addr);

		reg_addr = DCAM_REG_RD(idx, DCAM_FBC_RAW_SLICE_Y_ADDR);
		addr = reg_addr + FBC_PAYLOAD_YUV10_BYTE_UNIT * start_title_num;
		DCAM_REG_WR(idx, DCAM_FBC_RAW_SLICE_Y_ADDR, addr);

		addr_offset = reg_addr - reg_header + FBC_PAYLOAD_YUV10_BYTE_UNIT * start_title_num;
		DCAM_REG_WR(idx, DCAM_FBC_RAW_SLICE_OFFSET, addr_offset);

		reg_addr = DCAM_REG_RD(idx, DCAM_FBC_RAW_SLCIE_LOWBIT_ADDR);
		addr = reg_addr + (cur_slice->start_x - slicearg->relative_offset) / 2;
		DCAM_REG_WR(idx, DCAM_FBC_RAW_SLCIE_LOWBIT_ADDR, addr);
	}

	DCAM_REG_WR(idx, DCAM_YUV444TO420_IMAGE_WIDTH, slicearg->fetch_size.w);
	DCAM_REG_MWR(idx, DCAM_YUV444TOYUV420_PARAM, BIT_0, 0);

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
		DCAM_REG_MWR(binning->idx, DCAM_BIN_4IN1_CTRL0, BIT_0, 0);
		DCAM_REG_MWR(binning->idx, DCAM_BIN_4IN1_CTRL0, BIT_1, 1 << 1);
		DCAM_REG_MWR(binning->idx, DCAM_BIN_4IN1_CTRL0, BIT_2, 0 << 2);
	} else {
		DCAM_REG_MWR(binning->idx, DCAM_BIN_4IN1_CTRL0, BIT_0, 1);
		DCAM_REG_MWR(binning->idx, DCAM_BIN_4IN1_CTRL0, BIT_1, 1 << 1);
		DCAM_REG_MWR(binning->idx, DCAM_BIN_4IN1_CTRL0, BIT_2, 0 << 2);
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

static int dcamhw_fbc_ctrl(void *handle, void *arg)
{
	struct dcam_hw_fbc_ctrl *fbc_arg = NULL;
	uint32_t addr = 0, val = 0, color_format = 0, afbc_mode = 0;

	fbc_arg = (struct dcam_hw_fbc_ctrl *)arg;

	if (fbc_arg->compress_en == 0)
		return 0;
	if ((fbc_arg->fmt == CAM_YUV420_2FRAME) || (fbc_arg->fmt == CAM_YVU420_2FRAME) ||
				(fbc_arg->fmt == CAM_YVU420_2FRAME_MIPI) || (fbc_arg->fmt == CAM_YVU420_2FRAME_MIPI)) {
		if (fbc_arg->data_bits == CAM_8_BITS)
			afbc_mode = 5;
		else
			afbc_mode = 7;
	} else if ((fbc_arg->fmt == CAM_YVU422_2FRAME) || (fbc_arg->fmt == CAM_YUV422_2FRAME)) {
		if (fbc_arg->data_bits == CAM_8_BITS)
			afbc_mode = 9;
		else
			afbc_mode = 0xC;
	}

	if ((fbc_arg->fmt == CAM_YVU420_2FRAME) || (fbc_arg->fmt == CAM_YVU420_2FRAME_MIPI))
		color_format = 4;
	else
		color_format = 5;

	if (fbc_arg->path_id == DCAM_PATH_BIN) {
		addr = DCAM_YUV_FBC_SCAL_PARAM;
		DCAM_REG_MWR(fbc_arg->idx, addr, 0x1F << 10, afbc_mode << 10);
		DCAM_REG_MWR(fbc_arg->idx, addr, 0xF0, color_format << 4);
		DCAM_REG_MWR(fbc_arg->idx, addr, BIT_0, 0);
		DCAM_REG_MWR(fbc_arg->idx, DCAM_PATH_SEL, BIT_6, BIT_6);
		DCAM_REG_MWR(fbc_arg->idx, DCAM_STORE0_PARAM, BIT_0, 1);
	} else {
		addr = DCAM_FBC_RAW_PARAM;
		DCAM_REG_MWR(fbc_arg->idx, addr, 0x1F00, afbc_mode << 8);
		val = (fbc_arg->data_bits - 8) >> 1;
		DCAM_REG_MWR(fbc_arg->idx, addr, BIT_6 | BIT_7, val << 6);
		DCAM_REG_MWR(fbc_arg->idx, addr, 0xF << 16, color_format << 16);
		DCAM_REG_MWR(fbc_arg->idx, addr, BIT_0, 0);
		DCAM_REG_MWR(fbc_arg->idx, DCAM_PATH_SEL, BIT_8, BIT_8);
		DCAM_REG_MWR(fbc_arg->idx, DCAM_STORE4_PARAM, BIT_0, 1);
	}

	/*recommended  from asic config bit_25 for fbc is too slow*/
	DCAM_REG_MWR(fbc_arg->idx, DCAM_PATH_SEL, BIT_25, BIT_25);
	return 0;
}

static int dcamhw_fbc_addr_set(void *handle, void *arg)
{
	struct dcam_hw_fbc_addr *fbcadr = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}
	fbcadr = (struct dcam_hw_fbc_addr *)arg;

	if (fbcadr->path_id == DCAM_PATH_BIN) {
		DCAM_REG_WR(fbcadr->idx, DCAM_YUV_FBC_SCAL_SLICE_HEADER_BASE_ADDR, fbcadr->frame_addr[0]);
		DCAM_REG_WR(fbcadr->idx, DCAM_YUV_FBC_SCAL_SLICE_PLOAD_BASE_ADDR, fbcadr->frame_addr[1]);
		DCAM_REG_WR(fbcadr->idx, DCAM_YUV_FBC_SCAL_SLICE_PLOAD_OFFSET_ADDR, fbcadr->frame_addr[1] - fbcadr->frame_addr[0]);
	} else {
		DCAM_REG_WR(fbcadr->idx, DCAM_FBC_RAW_SLICE_Y_HEADER, fbcadr->frame_addr[0]);
		DCAM_REG_WR(fbcadr->idx, DCAM_FBC_RAW_SLICE_Y_ADDR, fbcadr->frame_addr[1]);
		DCAM_REG_WR(fbcadr->idx, DCAM_FBC_RAW_SLICE_OFFSET, fbcadr->frame_addr[1] - fbcadr->frame_addr[0]);
	}

	return 0;
}

static int dcamhw_gtm_hist_bypass_get(void *handle, void *arg)
{
	int val = 0;
	struct dcam_isp_k_block *dcam_blkpm = NULL;

	dcam_blkpm = (struct dcam_isp_k_block *)arg;
	if (dcam_blkpm->rgb_gtm.rgb_gtm_info.bypass_info.gtm_hist_stat_bypass)
		val = 1;

	return val;
}

static int dcamhw_gtm_ltm_eb(void *handle, void *arg)
{
	struct cam_hw_gtm_ltm_eb *eb = NULL;
	struct dcam_isp_k_block *p = NULL;

	eb = (struct cam_hw_gtm_ltm_eb *)arg;
	if (eb->dcam_idx >= DCAM_ID_MAX) {
		pr_err("fail to get dcam_idx %d\n", eb->dcam_idx);
		return -EFAULT;
	}

	g_dcam_bypass[eb->dcam_idx] &= (~(1 << _E_GTM));

	p = (struct dcam_isp_k_block *)(eb->dcam_param);
	if (p)
		dcam_k_gtm_bypass(p, &p->rgb_gtm.rgb_gtm_info.bypass_info);
	else
		pr_err("fail to get gtm param\n");

	pr_debug("gtm enable, dcam hw ctx %d\n", eb->dcam_idx);

	return 0;
}

static int dcamhw_gtm_ltm_dis(void *handle, void *arg)
{
	struct cam_hw_gtm_ltm_dis *dis = NULL;

	dis = (struct cam_hw_gtm_ltm_dis *)arg;

	if (dis->dcam_idx >= DCAM_ID_MAX) {
		pr_err("fail to get dcam_idx %d\n", dis->dcam_idx);
		return -EFAULT;
	}

	g_dcam_bypass[dis->dcam_idx] |= (1 << _E_GTM);
	DCAM_REG_MWR(dis->dcam_idx, DCAM_GTM_GLB_CTRL, 0x3, 3);

	pr_debug("gtm disable, dcam hw ctx %d\n", dis->dcam_idx);

	return 0;
}

static int dcamhw_get_gtm_hist(void *handle, void *arg)
{
	struct dcam_hw_gtm_hist *param = NULL;
	uint32_t idx = 0, hist_index = 0, sum = 0;
	uint32_t *buf = NULL;

	if (!arg) {
		pr_err("fail to get gtm hist\n");
		return -1;
	}

	param = (struct dcam_hw_gtm_hist *)arg;
	idx = param->idx;
	buf = param->value;
	if (!buf) {
		pr_err("fail to get gtm buffer\n");
		return -1;
	}

	for (hist_index = 0; hist_index < GTM_HIST_ITEM_NUM; hist_index++) {
		buf[hist_index] = DCAM_REG_RD(idx, GTM_HIST_CNT + hist_index * 4);
		sum += buf[hist_index];
	}
	buf[GTM_HIST_ITEM_NUM] = sum;
	hist_index = GTM_HIST_ITEM_NUM + 2;
	buf[hist_index++] = DCAM_REG_RD(idx, DCAM_GTM_STATUS0) & 0x3FFF;
	buf[hist_index++] = (DCAM_REG_RD(idx, DCAM_GTM_STATUS0) >> 16) & 0x3FFF;
	buf[hist_index++] = DCAM_REG_RD(idx, DCAM_GTM_STATUS1) & 0x3FFF;
	buf[hist_index++] = (DCAM_REG_RD(idx, DCAM_GTM_STATUS1) >> 16) & 0xFFF;
	buf[hist_index++] = (DCAM_REG_RD(idx, DCAM_GTM_STATUS2) >> 16) & 0xFFFF;
	buf[hist_index++] = DCAM_REG_RD(idx, DCAM_GTM_STATUS3) & 0xFFFF;
	buf[hist_index++] = (DCAM_REG_RD(idx, DCAM_GTM_STATUS3) >> 16) & 0xFFFF;
	buf[hist_index++] = DCAM_REG_RD(idx, DCAM_GTM_STATUS4) & 0x1FFFFFFF;
	buf[hist_index++] = 0;
	buf[hist_index] = 0;

	return 0;
}

static int dcamhw_blocks_setall(void *handle, void *arg)
{
	uint32_t idx = 0;
	struct dcam_isp_k_block *p = NULL;

	if (!arg) {
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
	dcam_k_raw_gtm_block(p);
	/* simulator should set this block(random) carefully */
	dcam_k_rgb_dither_random_block(p);
	dcam_k_cmc10_block(p);
	dcam_k_nlm_block(p);
	dcam_k_nlm_imblance(p);
	dcam_k_cce_block(p);
	dcam_k_cfa_block(p);
	dcam_k_gamma_block(p);

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
	dcam_k_frgbhist_block(p);

	dcam_k_lscm_monitor(p);

	dcam_k_pdaf(p);
	dcam_k_3dnr_me(p);
	pr_info("dcam%d set statis done\n", idx);
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
	struct dcam_hw_cfg_store_addr *param = NULL;
	uint32_t path_id = 0, idx = 0, blk_x = 0, blk_y = 0;
	struct cam_hw_info *hw = NULL;

	hw = (struct cam_hw_info *)handle;
	param = (struct dcam_hw_cfg_store_addr *)arg;
	if (!hw || !param) {
		pr_err("fail to get valid handle:%p, %p\n", handle, arg);
		return -1;
	}

	path_id = param->path_id;
	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	switch (path_id) {
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_6, 0);
		DCAM_REG_WR(idx, DCAM_STORE0_SLICE_Y_ADDR, param->frame_addr[0]);
		if ((param->frame_addr[1] == 0) && (param->out_fmt & CAM_YUV_BASE))
			param->frame_addr[1] = param->frame_addr[0] + param->out_pitch * param->out_size.h;
		DCAM_REG_WR(idx, DCAM_STORE0_SLICE_U_ADDR, param->frame_addr[1]);
		break;
	case DCAM_PATH_FULL:
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_8, 0);
		DCAM_REG_WR(idx, DCAM_STORE4_SLICE_Y_ADDR, param->frame_addr[0]);
		if ((param->frame_addr[1] == 0) && (param->out_fmt & CAM_YUV_BASE))
			param->frame_addr[1] = param->frame_addr[0] + param->out_pitch * param->out_size.h;
		DCAM_REG_WR(idx, DCAM_STORE4_SLICE_U_ADDR, param->frame_addr[1]);
		pr_debug("store addr %08x, %08x, pitch %d fmt %d\n", param->frame_addr[0], param->frame_addr[1],
			param->out_pitch, param->out_fmt);
		break;
	case DCAM_PATH_RAW:
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_4, 0);
		DCAM_REG_WR(idx, DCAM_RAW_PATH_BASE_WADDR, param->frame_addr[0]);
		pr_debug("store addr %08x\n", param->frame_addr[0]);
		break;
	case DCAM_PATH_AEM:
		DCAM_REG_WR(idx, DCAM_AEM_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_HIST:
		DCAM_REG_WR(idx, DCAM_BAYER_HIST_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_AFM:
		DCAM_REG_WR(idx, DCAM_AFM_LUM_FV_BASE_WADDR, param->frame_addr[0]);
		if (param->blk_param && param->blk_param->afm.af_param.afm_enhanced_lum.afm_hist_en) {
			blk_x = param->blk_param->afm.win_parm.win_num.width;
			blk_y = param->blk_param->afm.win_parm.win_num.height;
			DCAM_REG_WR(idx, DCAM_AFM_HIST_BASE_WADDR, param->frame_addr[0] + blk_x * blk_y * 16);
		}
		break;
	case DCAM_PATH_PDAF:
		if (param->blk_param->pdaf.pdaf_type == DCAM_PDAF_TYPE2)
			DCAM_REG_WR(idx, DCAM_VCH3_BASE_WADDR, param->frame_addr[0]);
		else {
			DCAM_REG_WR(idx, DCAM_PDAF_BASE_WADDR, param->frame_addr[0]);
			DCAM_REG_WR(idx, DCAM_PPE_RIGHT_WADDR, param->frame_addr[0] + param->frame_size / 2);
		}
		break;
	case DCAM_PATH_AFL:
		DCAM_REG_WR(idx, ISP_AFL_DDR_INIT_ADDR, param->frame_addr[0]);
		DCAM_REG_WR(idx, ISP_AFL_REGION_WADDR, param->frame_addr[0] + STATIS_AFL_GBUF_SIZE);
		break;
	case DCAM_PATH_VCH2:
		DCAM_REG_WR(idx, DCAM_VCH2_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_WR(idx, DCAM_VCH3_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_FRGB_HIST:
		DCAM_REG_WR(idx, DCAM_HIST_ROI_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_3DNR:
		DCAM_REG_WR(idx, DCAM_NR3_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_BPC:
		DCAM_REG_WR(idx, DCAM_BPC_OUT_ADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_LSCM:
		DCAM_REG_WR(idx, DCAM_LSCM_BASE_WADDR, param->frame_addr[0]);
		break;
	case DCAM_PATH_GTM_HIST:
		break;
	default:
		pr_err("fail to get valid path id%d\n", path_id);
		break;
	}
	return 0;
}

static int dcamhw_csi_disconnect(void *handle, void *arg)
{
	uint32_t idx = 0, csi_id = 0, csi_controller = 0, csi_reg_bit = 0, val = 0xffffffff;
	struct cam_hw_info *hw = NULL;
	int time_out = DCAMX_STOP_TIMEOUT;
	struct dcam_switch_param *csi_switch = NULL;

	csi_switch = (struct dcam_switch_param *)arg;
	idx = csi_switch->dcam_id;
	csi_id = csi_switch->csi_id;
	hw = (struct cam_hw_info *)handle;

	switch (csi_id) {
		case 0:
			csi_controller = 0;
			break;
		case 1:
			csi_controller = 1;
			break;
		case 2:
		case 3:
			csi_controller = 2;
			break;
		case 4:
		case 5:
			csi_controller = 3;
			break;
		default:
			pr_err("fail to get right csi id\n");
			return -1;
	}
	/* read AHB DCAM_BLK_EN bit12~15: csi_en0~3 */
	csi_reg_bit = 1 << (csi_controller + 12);

	regmap_update_bits(hw->soc_dcam->cam_switch_gpr, idx * 4, BIT_31, BIT_31);
	while (time_out) {
		regmap_read(hw->soc_dcam->cam_ahb_gpr, 0x0c, &val);
		if (val & csi_reg_bit) {
			regmap_read(hw->soc_dcam->cam_switch_gpr, idx * 4, &val);
			if (val & BIT_30)
				break;
		} else {
			pr_warn("csi%d disabled already. DCAM_BLK_EN 0x%x\n", csi_id, val);
			break;
		}
		os_adapt_time_udelay(1000);
		time_out--;
	}

	if (time_out != 0)
		pr_debug("disconnect dcam%d from csi%d, cam_switch_gpr:0x%x\n", idx, csi_id, val);
	else
		pr_err("fail to disconnect DCAM%d csi%d\n", idx, csi_id);

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
	uint32_t idx = 0, csi_id = 0, val = 0xffffffff;
	struct cam_hw_info *hw = NULL;
	struct dcam_switch_param *csi_switch = NULL;
	int time_out = DCAMX_STOP_TIMEOUT;
	char chip_type[64] = { 0 };

	csi_switch = (struct dcam_switch_param *)arg;
	hw = (struct cam_hw_info *)handle;
	csi_id = csi_switch->csi_id;
	idx = csi_switch->dcam_id;

	cam_kernel_adapt_kproperty_get("auto/chipid", chip_type, "-1");
	if (strncmp(chip_type, "UMS9620-AA", strlen("UMS9620-AA")) == 0)
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, 0xf00, 1 << 8);
	regmap_update_bits(hw->soc_dcam->cam_switch_gpr, idx * 4, 0x7 << 13, csi_id << 13);
	regmap_update_bits(hw->soc_dcam->cam_switch_gpr, idx * 4, BIT_31, ~BIT_31);
	while (time_out) {
		regmap_read(hw->soc_dcam->cam_switch_gpr, idx * 4, &val);
		if (!(val & BIT_30))
			break;
		os_adapt_time_udelay(1000);
		time_out--;
	}

	if (time_out != 0)
		pr_debug("connect dcam%d to csi%d, cam_switch_gpr:0x%x\n", idx, csi_id, val);
	else
		pr_err("fail to connect DCAM%d csi%d\n", idx, csi_id);

	return 0;
}

static int dcamhw_csi_force_enable(void *handle, void *arg)
{
	uint32_t idx = 0, csi_id = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_switch_param *csi_switch = NULL;

	csi_switch = (struct dcam_switch_param *)arg;
	hw = (struct cam_hw_info *)handle;
	csi_id = csi_switch->csi_id;
	idx = csi_switch->dcam_id;

	pr_debug("Enter dcam%d to csi%d\n", idx, csi_id);

	regmap_update_bits(hw->soc_dcam->cam_switch_gpr, idx * 4, BIT_5, BIT_5);
	regmap_update_bits(hw->soc_dcam->cam_switch_gpr, idx * 4, BIT_31, BIT_31);
	os_adapt_time_udelay(1000);
	regmap_update_bits(hw->soc_dcam->cam_switch_gpr, idx * 4, 0x7 << 13, csi_id << 13);
	regmap_update_bits(hw->soc_dcam->cam_switch_gpr, idx * 4, BIT_31, ~BIT_31);
	os_adapt_time_udelay(1000);
	return 0;
}

static int dcamhw_fmcu_cmd_set(void *handle, void *arg)
{
	struct dcam_hw_fmcu_cmd *cmdarg = NULL;
	uint32_t cmd_num = 0;

	cmdarg = (struct dcam_hw_fmcu_cmd *)arg;
	cmd_num = cmdarg->cmd_num / 2;

	DCAM_FMCU_WR(DCAM_FMCU_DDR_ADR, cmdarg->hw_addr);
	DCAM_FMCU_MWR(DCAM_FMCU_CTRL, 0xFFFF0000, cmd_num << 16);
	DCAM_FMCU_WR(DCAM_FMCU_CMD_READY, 1);

	return 0;
}

static int dcamhw_fmcu_start(void *handle, void *arg)
{
	struct dcam_hw_fmcu_start *startarg = NULL;
	uint32_t cmd_num = 0;

	startarg = (struct dcam_hw_fmcu_start *)arg;
	cmd_num = startarg->cmd_num / 2;

	DCAM_REG_WR(startarg->idx, DCAM_INT0_CLR, 0xFFFFFFFF);
	DCAM_REG_WR(startarg->idx, DCAM_INT1_CLR, 0xFFFFFFFF);
	DCAM_REG_WR(startarg->idx, DCAM_INT0_EN, DCAMINT_IRQ_LINE_EN0_NORMAL);
	DCAM_REG_MWR(startarg->idx, DCAM_INT0_EN, 0x6cff0002, 0);
	DCAM_REG_WR(startarg->idx, DCAM_INT1_EN, DCAMINT_IRQ_LINE_EN1_NORMAL);
	DCAM_REG_MWR(startarg->idx, DCAM_INT1_EN, BIT_0 , 0);

	DCAM_FMCU_WR(DCAM_FMCU_DDR_ADR, startarg->hw_addr);
	DCAM_FMCU_MWR(DCAM_FMCU_CTRL, 0xFFFF0000, cmd_num << 16);
	DCAM_FMCU_WR(DCAM_FMCU_ISP_REG_REGION, DCAM_OFFSET_RANGE);
	DCAM_FMCU_WR(DCAM_FMCU_START, 1);

	return 0;
}

static int dcamhw_fmcu_enable(void *handle, void *arg)
{
	struct dcam_fmcu_enable *param =  NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EINVAL;
	}

	param = (struct dcam_fmcu_enable *)arg;

	pr_debug("dcam%d enable fmcu %d\n", param->idx,param->enable);
	if (param->enable) {
		DCAM_REG_MWR(param->idx, DCAM_PATH_SEL, BIT_31, BIT_31);
		if (param->idx == 0)
			DCAM_REG_MWR(DCAM_ID_1, DCAM_PATH_SEL, BIT_31, 0);
		else
			DCAM_REG_MWR(DCAM_ID_0, DCAM_PATH_SEL, BIT_31, 0);
	} else
		DCAM_REG_MWR(param->idx, DCAM_PATH_SEL, BIT_31, 0);

	return 0;
}

static int dcamhw_slw_fmcu_cmds(void *handle, void *arg)
{
	int i = 0;
	uint32_t addr = 0, cmd = 0;
	struct dcam_fmcu_ctx_desc *fmcu = NULL;
	struct dcam_hw_slw_fmcu_cmds *slw = NULL;
	slw = (struct dcam_hw_slw_fmcu_cmds *)arg;

	if (!slw) {
		pr_err("fail to get valid input ptr, slw %p\n", slw);
		return -EFAULT;
	}
	fmcu = slw->fmcu_handle;

	if (!fmcu) {
		pr_err("fail to get valid input ptr, fmcu %p\n", fmcu);
		return -EFAULT;
	}

	for (i = 0; i < DCAM_PATH_MAX; i++) {
		if (slw->store_info[i].store_addr.addr_ch0 == 0)
			continue;
		switch (i) {
		case DCAM_PATH_BIN:
			if (!slw->store_info[i].is_compressed) {
				addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_STORE0_SLICE_Y_ADDR);
				cmd = slw->store_info[i].store_addr.addr_ch0;
				DCAM_FMCU_PUSH(fmcu, addr, cmd);
				addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_STORE0_SLICE_U_ADDR);
				cmd = slw->store_info[i].store_addr.addr_ch1;
				DCAM_FMCU_PUSH(fmcu, addr, cmd);
			} else {
				addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_YUV_FBC_SCAL_SLICE_HEADER_BASE_ADDR);
				DCAM_FMCU_PUSH(fmcu, addr, slw->store_info[i].store_addr.addr_ch0);
				addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_YUV_FBC_SCAL_SLICE_PLOAD_BASE_ADDR);
				DCAM_FMCU_PUSH(fmcu, addr, slw->store_info[i].store_addr.addr_ch1);
				addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_YUV_FBC_SCAL_SLICE_PLOAD_OFFSET_ADDR);
				DCAM_FMCU_PUSH(fmcu, addr, slw->store_info[i].store_addr.addr_ch1 - slw->store_info[i].store_addr.addr_ch0);
			}
			break;
		case DCAM_PATH_FULL:
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_STORE4_SLICE_Y_ADDR);
			cmd = slw->store_info[i].store_addr.addr_ch0;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_STORE4_SLICE_U_ADDR);
			cmd = slw->store_info[i].store_addr.addr_ch1;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			break;
		case DCAM_PATH_AFL:
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, ISP_AFL_DDR_INIT_ADDR);
			cmd = slw->store_info[i].store_addr.addr_ch0;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, ISP_AFL_REGION_WADDR);
			cmd = slw->store_info[i].store_addr.addr_ch1;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			break;
		case DCAM_PATH_AEM:
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_AEM_BASE_WADDR);
			cmd = slw->store_info[i].store_addr.addr_ch0;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			break;
		case DCAM_PATH_HIST:
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_BAYER_HIST_BASE_WADDR);
			cmd = slw->store_info[i].store_addr.addr_ch0;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			break;
		case DCAM_PATH_AFM:
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_AFM_LUM_FV_BASE_WADDR);
			cmd = slw->store_info[i].store_addr.addr_ch0;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_AFM_HIST_BASE_WADDR);
			cmd = slw->store_info[i].store_addr.addr_ch1;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			break;
		case DCAM_PATH_FRGB_HIST:
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_HIST_ROI_BASE_WADDR);
			cmd = slw->store_info[i].store_addr.addr_ch0;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			break;
		case DCAM_PATH_LSCM:
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_LSCM_BASE_WADDR);
			cmd = slw->store_info[i].store_addr.addr_ch0;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			break;
		case DCAM_PATH_PDAF:
			addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_PDAF_BASE_WADDR);
			cmd = slw->store_info[i].store_addr.addr_ch0;
			DCAM_FMCU_PUSH(fmcu, addr, cmd);
			break;
		case DCAM_PATH_GTM_HIST:
			break;
		default :
			pr_err("fail to get valid path id%d\n", i);
			break;
		}
	}

	addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_CONTROL);
	if (slw->is_first_cycle)
		/*force copy*/
		cmd = 0x55555555;
	else
		/*auto copy*/
		cmd = 0xaaaaaaaa;
	DCAM_FMCU_PUSH(fmcu, addr, cmd);

	if (slw->is_first_cycle) {
		uint32_t val;
		val = DCAM_REG_RD(fmcu->hw_ctx_id, DCAM_MIPI_CAP_CFG);
		addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_MIPI_CAP_CFG);
		cmd = val | BIT_0;
		DCAM_FMCU_PUSH(fmcu, addr, cmd);
	}

	addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_FMCU_CMD);
	cmd = DCAM0_POF3_DISPATCH;
	DCAM_FMCU_PUSH(fmcu, addr, cmd);

	if (slw->slw_id == slw->slw_cnt - 1) {
		addr = DCAM_GET_REG(fmcu->hw_ctx_id, DCAM_FMCU_CMD);
		cmd = DCAM0_PREVIEW_DONE;
		DCAM_FMCU_PUSH(fmcu, addr, cmd);
	}

	return 0;
}

static int dcamhw_dec_online(void *handle, void *arg)
{
	uint32_t idx = 0, val = 0;
	struct dcam_hw_dec_online_cfg *param = NULL;

	if (!handle || !arg) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}

	param = (struct dcam_hw_dec_online_cfg *)arg;
	idx = param->idx;

	val = (param->layer_num & 0x7) | ((param->chksum_clr_mode & 0x1) << 3)
		| ((param->chksum_work_mode & 0x1) << 4);
	DCAM_REG_WR(idx, DCAM_DEC_ONLINE_PARAM, val);

	if(param->path_sel == DACM_DEC_PATH_DEC)
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_20|BIT_21|BIT_22|BIT_23, 0);
	else if (param->path_sel == DCAM_DEC_PATH_YUV_DECI)
		DCAM_REG_MWR(idx, DCAM_PATH_SEL, BIT_20|BIT_21|BIT_22|BIT_23, 0xF << 20);
	else {
		pr_err("fail to support path_sel %d\n", param->path_sel);
		return -1;
	}

	val = (param->hor_padding_en & 0x1) | ((param->hor_padding_num & 0xefff) << 1)
		| ((param->ver_padding_en & 0x1) << 16) | ((param->ver_padding_num & 0x7f) << 17);
	DCAM_REG_WR(idx, DCAM_DEC_ONLINE_PARAM1, val);
	val = (param->flust_width & 0xffff) | ((param->flush_hblank_num & 0xffff) << 16);
	DCAM_REG_WR(idx, DCAM_IMG_SIZE, val);
	val = (param->flush_line_num << 8) | 0x50 | (0x50 << 16);
	DCAM_REG_WR(idx, DCAM_FLUSH_CTRL, val);

	return 0;
}

static int dcamhw_dec_bypass(void *handle, void *arg)
{
	uint32_t idx = 0, base = 0;
	uint32_t reg_base[4] = {
		DCAM_STORE_DEC_L1_BASE,
		DCAM_STORE_DEC_L2_BASE,
		DCAM_STORE_DEC_L3_BASE,
		DCAM_STORE_DEC_L4_BASE
	};
	struct dcam_hw_dec_store_cfg *param = NULL;

	if (!handle || !arg) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}

	param = (struct dcam_hw_dec_store_cfg *)arg;
	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	base = reg_base[param->layer_num];
	DCAM_REG_MWR(idx, base + DCAM_STORE_DEC_PARAM, BIT_0, 1);

	return 0;
}

static int dcamhw_dec_store_addr(void *handle, void *arg)
{
	uint32_t idx = 0, base = 0;
	uint32_t reg_base[4] = {
		DCAM_STORE_DEC_L1_BASE,
		DCAM_STORE_DEC_L2_BASE,
		DCAM_STORE_DEC_L3_BASE,
		DCAM_STORE_DEC_L4_BASE
	};
	struct dcam_hw_dec_store_cfg *param = NULL;

	if (!handle || !arg) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}

	param = (struct dcam_hw_dec_store_cfg *)arg;
	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	base = reg_base[param->cur_layer];

	DCAM_REG_WR(idx, base + DCAM_STORE_DEC_SLICE_Y_ADDR, param->addr[0]);
	DCAM_REG_WR(idx, base + DCAM_STORE_DEC_SLICE_U_ADDR, param->addr[1]);

	return 0;
}

static int dcamhw_dec_size_update(void *handle, void *arg)
{
	uint32_t idx = 0, base = 0, val = 0;
	uint32_t reg_base[4] = {
		DCAM_STORE_DEC_L1_BASE,
		DCAM_STORE_DEC_L2_BASE,
		DCAM_STORE_DEC_L3_BASE,
		DCAM_STORE_DEC_L4_BASE
	};
	struct dcam_hw_dec_store_cfg *param = NULL;

	if (!handle || !arg) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}

	param = (struct dcam_hw_dec_store_cfg *)arg;
	idx = param->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	base = reg_base[param->cur_layer];

	DCAM_REG_MWR(idx, base + DCAM_STORE_DEC_PARAM, BIT_0, param->bypass);
	if (param->bypass)
		return 0;

	switch (param->color_format) {
	case CAM_FULL_RGB14:
		val = 0;
		break;
	case CAM_YUV422_2FRAME:
		val = 1;
		break;
	case CAM_YVU422_2FRAME:
		val = 2;
		break;
	case CAM_YUV420_2FRAME:
	case CAM_YUV420_2FRAME_MIPI:
		val = 4;
		break;
	case CAM_YVU420_2FRAME:
	case CAM_YVU420_2FRAME_MIPI:
		val = 5;
		break;
	default:
		pr_err("fail to get right color_format %d\n", param->color_format);
		break;
	}

	val = ((param->burst_len & 1) << 1) | ((param->speed2x & 1) << 2) |
		((param->mirror_en & 1) << 3) | ((val & 0xF) << 4) |
		((param->endian & 3) << 8) | ((param->mono_en & 1) << 10)|
		((param->data_10b & 1) << 11)| ((param->flip_en & 1) << 12) |
		((param->last_frm_en & 3) << 13);
	DCAM_REG_MWR(idx, base + DCAM_STORE_DEC_PARAM, 0x3FFE, val);
	DCAM_REG_MWR(idx, base + DCAM_STORE_DEC_PARAM, BIT_7, 1 << 7);

	val = (param->width & 0xffff) | ((param->height & 0xffff) << 16);
	DCAM_REG_WR(idx, base + DCAM_STORE_DEC_SLICE_SIZE, val);

	val = (param->border_up & 0xff) | ((param->border_down & 0xff) << 8) |
		((param->border_left & 0xff) << 16) | ((param->border_right & 0xff) << 24);
	DCAM_REG_WR(idx, base + DCAM_STORE_DEC_BORDER, val);

	DCAM_REG_WR(idx, base + DCAM_STORE_DEC_Y_PITCH, param->pitch[0]);
	DCAM_REG_WR(idx, base + DCAM_STORE_DEC_U_PITCH, param->pitch[1]);

	val = (param->rd_ctrl & 0x3) | ((param->store_res & 0x3fffffff) << 2);
	DCAM_REG_WR(idx, base + DCAM_STORE_DEC_READ_CTRL, val);
	DCAM_REG_MWR(idx, base + DCAM_STORE_DEC_SHADOW_CLR_SEL, BIT_1, 1 << 1);
	DCAM_REG_MWR(idx, base + DCAM_STORE_DEC_SHADOW_CLR, BIT_0, 1);

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
	int time_out = 0, wait_time_flag = 0;
	uint32_t hw_ctx_id = 0, dbg_sts_reg = 0;

	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}

	hw_ctx_id = *((uint32_t *)arg);
	if (hw_ctx_id >= DCAM_HW_CONTEXT_MAX)
		return time_out;
	wait_time_flag = *((int *)handle);
	if (hw_ctx_id <= DCAM_ID_1)
		dbg_sts_reg = AXIM_DBG_STS;
	else
		dbg_sts_reg = DCAM_LITE_AXIM_DBG_STS;

	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if (0 == (DCAM_AXIM_RD(hw_ctx_id, dbg_sts_reg) & BIT(16)))
			break;
		if (wait_time_flag == WAIT_IN_NORMAL_SCENE)
			os_adapt_time_udelay(1000);
		else
			os_adapt_time_udelay(80);
	}

	return time_out;
}

static int dcamhw_dummy_set(void *handle, void *arg)
{
	struct dcam_hw_dummy_param *param = NULL;

	if (!arg) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}
	param = (struct dcam_hw_dummy_param *)arg;
	DCAM_AXIM_MWR(param->idx, DCAM_DUMMY_SLAVE, BIT_30, param->clr_mode << 30);
	if (param->clr_mode == DCAM_DUMMY_MODE_HW_AUTO)
		DCAM_AXIM_WR(param->idx, DCAM_DUMMY_SLAVE_CFG, param->skip_num);
	if (param->dfifo_lvl || param->cfifo_lvl) {
		DCAM_AXIM_MWR(param->idx, DCAM_DUMMY_SLAVE, BIT_29, 0x1 << 29);
		if (param->dfifo_lvl)
			DCAM_AXIM_MWR(param->idx, DCAM_DUMMY_SLAVE, 0x1fff, param->dfifo_lvl);
		if (param->cfifo_lvl)
			DCAM_AXIM_MWR(param->idx, DCAM_DUMMY_SLAVE, 0x7ff << 16, param->cfifo_lvl << 16);
	}
	return 0;
}

static int dcamhw_dummy_enable(void *handle, void *arg)
{
	struct dcam_hw_dummy_param *param = NULL;

	if (!arg) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}
	param = (struct dcam_hw_dummy_param *)arg;
	DCAM_AXIM_MWR(param->idx, DCAM_DUMMY_SLAVE, BIT_28, param->enable << 28);
	return 0;
}

static int dcamhw_disable_sn_sof(void *handle, void *arg)
{
	uint32_t hw_ctx_id = 0;

	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}

	hw_ctx_id = *((uint32_t *)arg);
	DCAM_REG_MWR(hw_ctx_id, DCAM_INT0_EN, BIT(DCAM_IF_IRQ_INT0_SENSOR_SOF), 0);
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
	DCAM_REG_MWR(hw_ctx_id, DCAM_INT0_EN, BIT(DCAM_IF_IRQ_INT0_SENSOR_EOF), 0);
	return 0;
}

static int dcamhw_slice_imblance_set(void *handle, void *arg)
{
	uint32_t idx = 0;
	struct dcam_slice_imblance_info *slice_imblance = NULL;

	if (!handle || !arg) {
		pr_err("fail to get valid handle or arg\n");
		return -EFAULT;
	}

	slice_imblance = (struct dcam_slice_imblance_info *)arg;
	idx = slice_imblance->idx;
	if (idx >= DCAM_HW_CONTEXT_MAX)
		return 0;

	DCAM_REG_WR(idx, DCAM_NLM_IMBLANCE_PARA30,
		((slice_imblance->global_y_start & 0xFFFF) << 16) |
		(slice_imblance->global_x_start & 0xFFFF));
	return 0;
}

static int dcamhw_block_param_config(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_isp_k_block *pm_ctx = NULL;
	struct isp_pipeline_param_n6pro *blkpm_ptr = NULL;

	if (!handle || !arg) {
		pr_err("fail to get input arg :%p, %p.\n", handle, arg);
		return -EFAULT;
	}

	pm_ctx = (struct dcam_isp_k_block *)arg;
	blkpm_ptr = (struct isp_pipeline_param_n6pro *)handle;

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
		memcpy(&pm_ctx->bpc_n6pro.bpc_param_n6pro.bpc_info,
			&blkpm_ptr->bpc_param, sizeof(struct dcam_dev_bpc_info_v1));

	if (blkpm_ptr->ppi_param.update_flag)
		memcpy(&pm_ctx->bpc.bpc_ppi_info,
			&blkpm_ptr->ppi_param, sizeof(struct dcam_bpc_ppi_info));

	if (blkpm_ptr->gain_param.update_flag)
		memcpy(&pm_ctx->rgb.gain_info,
			&blkpm_ptr->gain_param, sizeof(struct dcam_dev_rgb_gain_info));

	if (blkpm_ptr->gtm_param.update_flag)
		memcpy(&pm_ctx->rgb_gtm.rgb_gtm_info,
			&blkpm_ptr->gtm_param, sizeof(struct dcam_dev_rgb_gtm_block_info));

	if (blkpm_ptr->dither_param.update_flag)
		memcpy(&pm_ctx->rgb.rgb_dither,
			&blkpm_ptr->dither_param, sizeof(struct dcam_dev_rgb_dither_info));

	if (blkpm_ptr->cmc10_param.update_flag)
		memcpy(&pm_ctx->cmc10_info,
			&blkpm_ptr->cmc10_param, sizeof(struct isp_dev_cmc10_info));

	if (blkpm_ptr->nlm_param.update_flag) {
		memcpy(&pm_ctx->nlm_info_base,
			&blkpm_ptr->nlm_param, sizeof(struct isp_dev_nlm_info_v2));
		ret = dcam_k_save_vst_ivst(pm_ctx);
		if (ret)
			pm_ctx->nlm_info_base.bypass = 1;
	}
	if (blkpm_ptr->imbalance_param.update_flag)
		memcpy(&pm_ctx->imbalance_info_base2,
			&blkpm_ptr->imbalance_param, sizeof(struct isp_dev_nlm_imblance_v2));

	if (blkpm_ptr->cce_param.update_flag)
		memcpy(&pm_ctx->cce_info,
			&blkpm_ptr->cce_param, sizeof(struct isp_dev_cce_info));

	if (blkpm_ptr->cfa_param.update_flag)
		memcpy(&pm_ctx->cfa_info_v1,
			&blkpm_ptr->cfa_param, sizeof(struct isp_dev_cfa_info_v1));

	if (blkpm_ptr->gamma_param.update_flag)
		memcpy(&pm_ctx->gamma_info_v1.gamma_info,
			&blkpm_ptr->gamma_param, sizeof(struct isp_dev_gamma_info_v1));

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

static int dcamhw_raw_path_src_sel(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_hw_path_ctrl *pathctl = NULL;

	pathctl = (struct dcam_hw_path_ctrl *)arg;
	switch (pathctl->raw_sel) {
	case ORI_RAW_SRC_SEL:
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_13, 1 << 13);
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_4 | BIT_5, 3 << 4);
		break;
	case LSC_RAW_SRC_SEL:
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_13, 0 << 13);
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_4 | BIT_5, 1<< 4);
		break;
	case BPC_RAW_SRC_SEL:
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_13, 0 << 13);
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_4 | BIT_5, 2 << 4);
		break;
	case PROCESS_RAW_SRC_SEL:
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_13, 0);
		DCAM_REG_MWR(pathctl->idx, DCAM_RAW_PATH_CFG, BIT_4 | BIT_5, 0 << 4);
		break;
	default:
		pr_err("fail to support src_sel %d\n", pathctl->raw_sel);
		ret = -EINVAL;
		break;
	}
	pr_debug("DCAM%d: raw path sel %d set done\n", pathctl->idx, pathctl->raw_sel);

	return ret;
}

static int dcamhw_csi_reg_trace(void *handle, void *arg)
{
	csi_api_reg_trace();

	return 0;
}

static struct hw_io_ctrl_fun dcam_ioctl_fun_tab[] = {
	{DCAM_HW_CFG_ENABLE_CLK,            dcamhw_clk_eb},
	{DCAM_HW_CFG_DISABLE_CLK,           dcamhw_clk_dis},
	{DCAM_HW_CFG_INIT_AXI,              dcamhw_axi_init},
	{DCAM_HW_CFG_FMCU_RESET,            dcamhw_fmcu_reset},
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
	{DCAM_HW_CFG_FBC_CTRL,              dcamhw_fbc_ctrl},
	{DCAM_HW_CFG_FBC_ADDR_SET,          dcamhw_fbc_addr_set},
	{DCAM_HW_CFG_GTM_LTM_EB,            dcamhw_gtm_ltm_eb},
	{DCAM_HW_CFG_GTM_LTM_DIS,           dcamhw_gtm_ltm_dis},
	{DCAM_HW_CFG_BLOCKS_SETALL,         dcamhw_blocks_setall},
	{DCAM_HW_CFG_BLOCKS_SETSTATIS,      dcamhw_blocks_setstatis},
	{DCAM_HW_CFG_BAYER_HIST_ROI_UPDATE, dcamhw_bayer_hist_roi_update},
	{DCAM_HW_CFG_STORE_ADDR,            dcamhw_set_store_addr},
	{DCAM_HW_CFG_DISCONECT_CSI,         dcamhw_csi_disconnect},
	{DCAM_HW_CFG_CONECT_CSI,            dcamhw_csi_connect},
	{DCAM_HW_CFG_FORCE_EN_CSI,          dcamhw_csi_force_enable},
	{DCAM_HW_CFG_FMCU_CMD,              dcamhw_fmcu_cmd_set},
	{DCAM_HW_CFG_FMCU_START,            dcamhw_fmcu_start},
	{DCAM_HW_CFG_FMCU_EBABLE,           dcamhw_fmcu_enable},
	{DCAM_HW_CFG_SLW_FMCU_CMDS,         dcamhw_slw_fmcu_cmds},
	{DCAM_HW_CFG_DEC_ONLINE,            dcamhw_dec_online},
	{DCAM_HW_CFG_BYPASS_DEC,            dcamhw_dec_bypass},
	{DCAM_HW_CFG_DEC_STORE_ADDR,        dcamhw_dec_store_addr},
	{DCAM_HW_CFG_DEC_SIZE_UPDATE,       dcamhw_dec_size_update},
	{DCAM_HW_CFG_GTM_HIST_GET,          dcamhw_get_gtm_hist},
	{DCAM_HW_CFG_ALL_RESET,             dcamhw_axi_reset},
	{DCAM_HW_CFG_IRQ_DISABLE,           dcamhw_irq_disable},
	{DCAM_HW_CFG_SLW_ADDR,              dcamhw_set_slw_addr},
	{DCAM_HW_CFG_FETCH_STATUS_GET,      dcamhw_fetch_sts_get},
	{DCAM_HW_CFG_DUMMY_SET,             dcamhw_dummy_set},
	{DCAM_HW_CFG_DUMMY_ENABLE,          dcamhw_dummy_enable},
	{DCAM_HW_CFG_GTM_HIST_BYPASS_GET,   dcamhw_gtm_hist_bypass_get},
	{DCAM_HW_CFG_DIS_SN_SOF,            dcamhw_disable_sn_sof},
	{DCAM_HW_CFG_DIS_SN_EOF,            dcamhw_disable_sn_eof},
	{DCAM_HW_CFG_SLICE_IMBLANCE_SET,    dcamhw_slice_imblance_set},
	{DCAM_HW_CFG_NONZSL_BLOCK_PARAM,    dcamhw_block_param_config},
	{DCAM_HW_CFG_MIPI_CAP_FETCH_RESET,  dcamhw_reset_mipicap_fetch},
	{DCAM_HW_CFG_PATH_SRC_SEL,          dcamhw_raw_path_src_sel},
	{DCAM_HW_CFG_CSI_REG_TRACE,         dcamhw_csi_reg_trace},
};

static hw_ioctl_fun dcamhw_ioctl_fun_get(enum dcam_hw_cfg_cmd cmd)
{
	hw_ioctl_fun hw_ctrl = NULL;
	uint32_t i = 0, total_num = 0;

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
