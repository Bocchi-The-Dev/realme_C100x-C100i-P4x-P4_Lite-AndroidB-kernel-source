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

#ifdef CAM_HW_ADPT_LAYER

static atomic_t clk_users;

static int dcamlitehw_axi_init(void *handle, void *arg)
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
	soc = hw->soc_dcam_lite;
	ctrl_reg = DCAM_LITE_AXIM_CTRL;
	dbg_reg = DCAM_LITE_AXIM_DBG_STS;
	dcamhw_axi_common_init(hw, soc, ctrl_reg, dbg_reg, idx);

	return 0;
}

static int dcamlitehw_qos_set(void *handle, void *arg)
{
	//TODO
	return 0;
}

static int dcamlitehw_reset(void *handle, void *arg)
{
	int ret = 0;
	enum dcam_id idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;
	uint32_t mask = ~0;

	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}

	idx = *(uint32_t *)arg;
	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	ip = hw->ip_dcam[idx];

	pr_info("DCAM%d: reset.\n", idx);
	dcamhw_common_reset(hw, soc, idx);

	DCAM_REG_MWR(idx, DCAM_LITE_INT_CLR, mask, mask);
	DCAM_REG_WR(idx,  DCAM_LITE_INT_EN, DCAMLITE_IRQ_LINE_EN_NORMAL);

	DCAM_REG_WR(idx, DCAM_LITE_IMAGE_DT_VC_CONTROL, 0x2b << 8 | 0x01);
	DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_0, 0);
	sprd_iommu_restore(&hw->soc_dcam_lite->pdev->dev);

	pr_info("DCAM%d: reset end\n", idx);
	return ret;
}

static int dcamlitehw_force_copy(void *handle, void *arg)
{
	unsigned long flags = 0;
	struct dcam_hw_force_copy *forcpy = NULL;

	forcpy = (struct dcam_hw_force_copy *)arg;
	spin_lock_irqsave(&forcpy->glb_reg_lock, flags);
	DCAM_REG_MWR(forcpy->idx, DCAM_LITE_CONTROL, BIT_4, BIT_4);
	spin_unlock_irqrestore(&forcpy->glb_reg_lock, flags);
	return 0;
}

static int dcamlitehw_start(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_hw_start *parm = NULL;
	struct dcam_hw_force_copy copyarg;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	parm = (struct dcam_hw_start *)arg;
	DCAM_REG_WR(parm->idx, DCAM_LITE_INT_CLR, 0xFFFFFFFF);
	DCAM_REG_WR(parm->idx, DCAM_LITE_INT_EN, DCAMLITE_IRQ_LINE_EN_NORMAL);

	if (parm->raw_callback == 1)
		DCAM_REG_WR(parm->idx, DCAM_LITE_INT_EN, DCAMLITE_IRQ_LINE_EN_NORMAL | BIT(DCAM_LITE_IRQ_INT_SENSOR_SOF));
	else
		DCAM_REG_WR(parm->idx, DCAM_LITE_INT_EN, DCAMLITE_IRQ_LINE_EN_NORMAL);

	if (contr_cap_eof == 0)
		DCAM_REG_MWR(parm->idx, DCAM_LITE_INT_EN, BIT_3, 0);
	else
		DCAM_REG_MWR(parm->idx, DCAM_LITE_INT_EN, BIT_3, BIT(DCAM_LITE_IRQ_INT_CAP_EOF));

	copyarg.idx = parm->idx;
	copyarg.glb_reg_lock = parm->glb_reg_lock;
	dcamlitehw_force_copy(handle, &copyarg);
	/* trigger cap_en*/
	DCAM_REG_MWR(parm->idx, DCAM_LITE_CFG, BIT_0, 1);
	pr_info("dcam lite start\n");
	return ret;
}

static int dcamlitehw_stop(void *handle, void *arg)
{
	int ret = 0;
	int time_out = DCAMX_STOP_TIMEOUT;
	uint32_t idx = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_hw_context *hw_ctx = NULL;
	struct cam_hw_reg_trace trace;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw_ctx = (struct dcam_hw_context *)arg;
	idx = hw_ctx->hw_ctx_id;
	hw = (struct cam_hw_info *)handle;

	/* reset  cap_en*/
	DCAM_REG_MWR(idx, DCAM_LITE_CFG, BIT_0, 0);
	DCAM_REG_WR(idx, DCAM_LITE_PATH_STOP, 0x3);

	/* wait for AHB path busy cleared */
	while (time_out) {
		ret = DCAM_REG_RD(idx, DCAM_LITE_PATH_BUSY) & 0x3;
		if (!ret)
			break;
		udelay(1000);
		time_out--;
	}

	if (time_out == 0) {
		pr_err("fail to normal stop, DCAM%d timeout for 2s\n", idx);
		trace.type = ABNORMAL_REG_TRACE;
		trace.idx = idx;
		hw->isp_ioctl(hw, ISP_HW_CFG_REG_TRACE, &trace);
	}

	DCAM_REG_WR(idx, DCAM_LITE_INT_EN, 0);
	DCAM_REG_WR(idx, DCAM_LITE_INT_CLR, 0xFFFFFFFF);

	pr_info("dcam%d stop\n", idx);
	return ret;
}

static int dcamlitehw_record_addr(void *handle, void *arg)
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
	count = (DCAM_REG_RD(idx, DCAM_LITE_MIPI_CAP_FRM_CLR) >> 4) & 0x3F;

	count = count % DCAM_ADDR_RECORD_FRAME_NUM;
	hw_ctx->frame_addr[count][DCAM_RECORD_FRAME_CUR_COUNT] = (DCAM_REG_RD(idx, DCAM_LITE_MIPI_CAP_FRM_CLR) >> 4) & 0x3F;
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_FULL_Y] = DCAM_REG_RD(idx, DCAM_LITE_PATH0_BASE_WADDR);
	hw_ctx->frame_addr[count][DCAM_RECORD_PORT_FULL_U] = DCAM_REG_RD(idx, DCAM_LITE_PATH1_BASE_WADDR0);

	pr_debug("DCAM%u copy addr done.\n", idx);
	return 0;
}

static int dcamlitehw_auto_copy(void *handle, void *arg)
{
	struct dcam_hw_auto_copy *copyarg = NULL;
	unsigned long flags = 0;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}
	copyarg = (struct dcam_hw_auto_copy *)arg;
	spin_lock_irqsave(&copyarg->glb_reg_lock, flags);
	DCAM_REG_MWR(copyarg->idx, DCAM_LITE_CONTROL, BIT_5, BIT_5);
	spin_unlock_irqrestore(&copyarg->glb_reg_lock, flags);

	return 0;
}

static int dcamlitehw_path_start(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_hw_path_start *patharg = NULL;
	uint32_t image_data_type = IMG_TYPE_RAW10, data_bits = 0, is_pack = 0;
	pr_debug("enter.");

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	patharg = (struct dcam_hw_path_start *)arg;
	data_bits = cam_data_bits(patharg->out_fmt);
	is_pack = cam_is_pack(patharg->out_fmt);
	if (data_bits == CAM_8_BITS)
		image_data_type = IMG_TYPE_RAW8;

	switch (patharg->path_id) {
	case  DCAM_PATH_FULL:
		DCAM_REG_MWR(patharg->idx, DCAM_LITE_PATH_ENDIAN,
			BIT_1 |  BIT_0, patharg->endian);

		DCAM_REG_MWR(patharg->idx, DCAM_LITE_CFG, BIT_1, BIT_1);
		if (patharg->out_fmt & CAM_YUV_BASE) {
			DCAM_REG_MWR(patharg->idx, DCAM_LITE_PATH_ENDIAN,
				BIT_3 |  BIT_2, patharg->endian << 2);

			DCAM_REG_MWR(patharg->idx, DCAM_LITE_CFG, BIT_2, BIT_2);
		}
		break;
	default:
		pr_err("invalid path for dcam lite\n");
		break;
	}

	pr_info("done\n");
	return ret;
}

static int dcamlitehw_path_ctrl(void *handle, void *arg)
{
	struct dcam_hw_path_ctrl *pathctl = NULL;

	pathctl = (struct dcam_hw_path_ctrl *)arg;

	switch (pathctl->path_id) {
	case DCAM_PATH_FULL:
		DCAM_REG_MWR(pathctl->idx, DCAM_LITE_CFG, BIT_1, pathctl->type);
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(pathctl->idx, DCAM_LITE_CFG, BIT_2, pathctl->type);
		break;
	default:
		break;
	}

	return 0;
}

static int dcamlitehw_mipi_cap_set(void *handle, void *arg)
{
	int ret = 0;
	uint32_t idx = 0, reg_val = 0;
	struct dcam_mipi_info *cap_info = NULL;
	struct dcam_hw_mipi_cap *caparg = NULL;
	uint32_t image_vc = 0, image_mode = 1;
	uint32_t image_data_type = IMG_TYPE_RAW10;

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
		DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_1 | BIT_0, 1);

		/*raw 8:0 10:1 12:2 */
		if (cap_info->data_bits == CAM_14_BITS) {
			pr_err("dcam lite can't support bit:%d\n", cap_info->data_bits);
			return -EINVAL;
		}

		reg_val = (cap_info->data_bits - 8) >> 1;
		DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_5 | BIT_4, reg_val << 4);
	} else if (cap_info->format == DCAM_CAP_MODE_YUV) {
		if (unlikely(cap_info->data_bits != DCAM_CAP_8_BITS)) {
			pr_err("fail to get valid data bits for yuv format %d\n",
				cap_info->data_bits);
			return -EINVAL;
		}
		reg_val = (cap_info->data_bits - 8) >> 1;
		DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_5 | BIT_4, reg_val << 4);

		DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_1 | BIT_0, 0);
		DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_FRM_CTRL, BIT_1 | BIT_0, cap_info->pattern << 0);

		/* x & y deci */
		DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_FRM_CTRL, BIT_9 | BIT_8 | BIT_5 | BIT_4,
			(cap_info->y_factor << 8) | (cap_info->x_factor << 4));
		image_data_type = IMG_TYPE_YUV;
	} else {
		pr_err("fail to support capture format: %d\n",
			cap_info->format);
		return -EINVAL;
	}

	DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_2, cap_info->mode << 2);
	/* href */
	DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_3, cap_info->href << 3);
	/* frame deci */
	DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_7 | BIT_6, cap_info->frm_deci << 6);
	/* MIPI capture start */
	reg_val = (cap_info->cap_size.start_y << 16);
	reg_val |= cap_info->cap_size.start_x;
	DCAM_REG_WR(idx, DCAM_LITE_MIPI_CAP_START, reg_val);

	/* MIPI capture end */
	reg_val = (cap_info->cap_size.start_y
			+ cap_info->cap_size.size_y - 1) << 16;
	reg_val |= (cap_info->cap_size.start_x
			+ cap_info->cap_size.size_x - 1);
	DCAM_REG_WR(idx, DCAM_LITE_MIPI_CAP_END, reg_val);

	DCAM_REG_MWR(idx, DCAM_LITE_MIPI_CAP_CFG, BIT_8 | BIT_9 | BIT_10 | BIT_11, cap_info->frm_skip << 8);

	reg_val = ((image_vc & 0x3) << 16) |
		((image_data_type & 0x3F) << 8) | (image_mode & 0x3);
	DCAM_REG_MWR(idx, DCAM_LITE_IMAGE_DT_VC_CONTROL, 0x33f03, reg_val);

	pr_info("cap size : %d %d %d %d\n",
		cap_info->cap_size.start_x, cap_info->cap_size.start_y,
		cap_info->cap_size.size_x, cap_info->cap_size.size_y);
	pr_info("cap: frm %d, mode %d,  image_mode %d, bits %d, pattern %d, href %d\n",
		cap_info->format, cap_info->mode, image_mode, cap_info->data_bits,
		cap_info->pattern, cap_info->href);
	pr_info("cap: deci %d, skip %d, x %d, y %d, 4in1 %d, slowmotion count %d\n",
		cap_info->frm_deci, cap_info->frm_skip, cap_info->x_factor,
		cap_info->y_factor, cap_info->is_4in1, caparg->slowmotion_count);

	return ret;
}

static int dcamlitehw_set_store_addr(void *handle, void *arg)
{
	struct dcam_hw_cfg_store_addr *param = NULL;
	uint32_t path_id = 0, idx = 0;
	struct cam_hw_info *hw = NULL;

	hw = (struct cam_hw_info *)handle;
	param = (struct dcam_hw_cfg_store_addr *)arg;
	if (!hw || !param) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}

	path_id = param->path_id;
	idx = param->idx;

	switch (path_id) {

	case DCAM_PATH_FULL:
		DCAM_REG_WR(idx, DCAM_LITE_PATH0_BASE_WADDR, param->frame_addr[0]);
		if ((param->frame_addr[1] == 0) && (param->out_fmt & CAM_YUV_BASE))
			param->frame_addr[1] = param->frame_addr[0] + param->out_pitch * param->out_size.h;
		DCAM_REG_WR(idx, DCAM_LITE_PATH1_BASE_WADDR0, param->frame_addr[1]);
		break;
	default:
		pr_err("invalid path for dcam lite\n");
		break;
	}
	return 0;
}

static int dcamlitehw_axi_reset(void *handle, void *arg)
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
	soc = hw->soc_dcam_lite;
	ctrl_reg = DCAM_LITE_AXIM_CTRL;
	dbg_reg = DCAM_LITE_AXIM_DBG_STS;

	ip = hw->ip_dcam[DCAM_ID_2];
	flag = ip->syscon.all_rst_mask
		| ip->syscon.axi_rst_mask
		| ip->syscon.rst_mask
		| hw->ip_dcam[DCAM_ID_3]->syscon.rst_mask;
	dcamhw_axi_common_reset(hw, soc, flag, AXIM_CTRL, AXIM_DBG_STS, DCAM_ID_2);

	for(i = DCAM_ID_2; i <= DCAM_ID_3; i++) {
		DCAM_REG_MWR(i, DCAM_LITE_INT_CLR, mask, mask);
		DCAM_REG_WR(i,  DCAM_LITE_INT_EN, DCAMLITE_IRQ_LINE_EN_NORMAL);

		DCAM_REG_WR(i, DCAM_LITE_IMAGE_DT_VC_CONTROL, 0x2b << 8 | 0x01);
		DCAM_REG_MWR(i, DCAM_LITE_MIPI_CAP_CFG, BIT_0, 0);
		sprd_iommu_restore(&soc->pdev->dev);
	}

	pr_debug("dcam all & axi reset done\n");
	return 0;
}

static int dcamlitehw_irq_disable(void *handle, void *arg)
{
	struct cam_hw_info *hw = NULL;
	uint32_t idx = 0;
	if (!handle || !arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	idx = *(uint32_t *)arg;

	DCAM_REG_WR(idx, DCAM_LITE_INT_EN, 0);
	DCAM_REG_WR(idx, DCAM_LITE_INT_CLR, 0xFFFFFFFF);

	return 0;
}

static struct hw_io_ctrl_fun dcamlite_ioctl_fun_tab[] = {
	{DCAM_HW_CFG_INIT_AXI,              dcamlitehw_axi_init},
	{DCAM_HW_CFG_SET_QOS,               dcamlitehw_qos_set},
	{DCAM_HW_CFG_RESET,                 dcamlitehw_reset},
	{DCAM_HW_CFG_START,                 dcamlitehw_start},
	{DCAM_HW_CFG_STOP,                  dcamlitehw_stop},
	{DCAM_HW_CFG_RECORD_ADDR,           dcamlitehw_record_addr},
	{DCAM_HW_CFG_AUTO_COPY,             dcamlitehw_auto_copy},
	{DCAM_HW_CFG_FORCE_COPY,            dcamlitehw_force_copy},
	{DCAM_HW_CFG_PATH_START,            dcamlitehw_path_start},
	{DCAM_HW_CFG_PATH_CTRL,             dcamlitehw_path_ctrl},
	{DCAM_HW_CFG_MIPI_CAP_SET,          dcamlitehw_mipi_cap_set},
	{DCAM_HW_CFG_STORE_ADDR,            dcamlitehw_set_store_addr},
	{DCAM_HW_CFG_DISCONECT_CSI,         dcamhw_csi_disconnect},
	{DCAM_HW_CFG_CONECT_CSI,            dcamhw_csi_connect},
	{DCAM_HW_CFG_FORCE_EN_CSI,          dcamhw_csi_force_enable},
	{DCAM_HW_CFG_ALL_RESET,             dcamlitehw_axi_reset},
	{DCAM_HW_CFG_IRQ_DISABLE,           dcamlitehw_irq_disable},
};

static hw_ioctl_fun dcamlitehw_ioctl_fun_get(enum dcam_hw_cfg_cmd cmd)
{
	hw_ioctl_fun hw_ctrl = NULL;
	uint32_t total_num = 0;
	uint32_t i = 0;

	total_num = sizeof(dcamlite_ioctl_fun_tab) / sizeof(struct hw_io_ctrl_fun);
	for (i = 0; i < total_num; i++) {
		if (cmd == dcamlite_ioctl_fun_tab[i].cmd) {
			hw_ctrl = dcamlite_ioctl_fun_tab[i].hw_ctrl;
			break;
		}
	}

	return hw_ctrl;
}
#endif

