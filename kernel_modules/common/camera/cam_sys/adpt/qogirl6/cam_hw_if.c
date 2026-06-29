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

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <sprd_mm.h>

#include "cam_trusty.h"
#include "csi_api.h"
#include "dcam_core.h"
#include "dcam_dummy.h"
#include "dcam_reg.h"
#include "isp_cfg.h"
#include "isp_reg.h"
#include "isp_slice.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_HW_IF_L6: %d %d %s : "\
	fmt, current->pid, __LINE__, __func__

#define CAM_HW_ADPT_LAYER
#include "dcam_hw.c"
#include "isp_hw.c"

static int camhw_get_isp_dts_clk(void *handle, void *arg)
{
	struct cam_hw_soc_info *soc_isp = NULL;
	struct device_node *isp_node = (struct device_node *)arg;
	struct cam_hw_info *hw = NULL;

	hw = (struct cam_hw_info *)handle;
	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EINVAL;
	}

	soc_isp = hw->soc_isp;

	soc_isp->core_eb = of_clk_get_by_name(isp_node, "isp_eb");
	if (IS_ERR_OR_NULL(soc_isp->core_eb)) {
		pr_err("fail to read dts isp eb\n");
		return -EFAULT;
	}
	soc_isp->axi_eb = of_clk_get_by_name(isp_node, "isp_axi_eb");
	if (IS_ERR_OR_NULL(soc_isp->axi_eb)) {
		pr_err("fail to read dts isp axi eb\n");
		return -EFAULT;
	}
	soc_isp->clk = of_clk_get_by_name(isp_node, "isp_clk");
	if (IS_ERR_OR_NULL(soc_isp->clk)) {
		pr_err("fail to read dts isp clk\n");
		return -EFAULT;
	}
	soc_isp->clk_parent = of_clk_get_by_name(isp_node, "isp_clk_parent");
	if (IS_ERR_OR_NULL(soc_isp->clk_parent)) {
		pr_err("fail to read dts isp clk parent\n");
		return -EFAULT;
	}
	soc_isp->clk_default = clk_get_parent(soc_isp->clk);
	return 0;
}

static int camhw_get_dcam_dts_clk(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;
	struct device_node *dn = (struct device_node *)arg;
	struct cam_hw_soc_info *soc_dcam;

	hw = (struct cam_hw_info *)handle;
	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EINVAL;
	}

	soc_dcam = hw->soc_dcam;

	soc_dcam->core_eb = of_clk_get_by_name(dn, "dcam_eb");
	if (IS_ERR_OR_NULL(soc_dcam->core_eb)) {
		pr_err("fail to read clk, dcam_eb\n");
		ret = 1;
	}

	soc_dcam->clk = of_clk_get_by_name(dn, "dcam_clk");
	if (IS_ERR_OR_NULL(soc_dcam->clk)) {
		pr_err("fail to read clk, dcam_clk\n");
		ret = 1;
	}

	soc_dcam->clk_parent = of_clk_get_by_name(dn, "dcam_clk_parent");
	if (IS_ERR_OR_NULL(soc_dcam->clk_parent)) {
		pr_err("fail to read clk, dcam_clk_parent\n");
		ret = 1;
	}
	soc_dcam->clk_default = clk_get_parent(soc_dcam->clk);

	return ret;
}

static int camhw_get_all_rst(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;
	uint32_t args[2] = {0};
	struct device_node *dn = (struct device_node *)arg;
	struct cam_hw_ip_info *dcam_info = NULL;

	int i = 0;
	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EINVAL;
	}

	hw = (struct cam_hw_info *)handle;

	ret = cam_kernel_adapt_syscon_get_args_by_name(dn, "dcam_all_reset", ARRAY_SIZE(args), args);
	if (ret) {
		pr_err("fail to get dcam all reset syscon\n");
		return -EINVAL;
	}

	for (i = 0; i < DCAM_ID_MAX; i++) {
		dcam_info = hw->ip_dcam[i];
		dcam_info->syscon.all_rst = args[0];
		dcam_info->syscon.all_rst_mask= args[1];
	}

	return ret;
}

static int camhw_get_axi_base(void *handle, void *arg)
{
	struct cam_hw_info *hw = NULL;
	struct device_node *dn = (struct device_node *)arg;
	int pos = 0;
	uint32_t count = 0;
	struct resource reg_res = {0};
	void __iomem *reg_base = NULL;
	struct cam_hw_soc_info *soc_dcam;
	int i = 0;
	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EINVAL;
	}

	hw = (struct cam_hw_info *)handle;
	soc_dcam = hw->soc_dcam;
	if (of_property_read_u32(dn, "sprd,dcam-count", &count)) {
		pr_err("fail to parse the property of sprd,dcam-count\n");
		return -EINVAL;
	}

	pos = count;
	if (of_address_to_resource(dn, pos, &reg_res)) {
		pr_err("fail to get AXIM phy addr\n");
		goto err_axi_iounmap;
	}

	reg_base = ioremap(reg_res.start, reg_res.end - reg_res.start + 1);
	if (!reg_base) {
		pr_err("fail to map AXIM reg base\n");
		goto err_axi_iounmap;
	}

	for (i = 0; i < count; i++)
		g_dcam_aximbase[i] = (unsigned long)reg_base;

	soc_dcam->axi_reg_base = (unsigned long)reg_base;

	return 0;

err_axi_iounmap:
	for (i = 0; i < DCAM_ID_MAX; i++) {
		g_dcam_aximbase[i] = 0;
	}
	return -1;
}

static int camhw_get_block_type(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_info *hw = (struct cam_hw_info *)handle;
	struct cam_cfg_block *blk_type = (struct cam_cfg_block *)arg;

	if (hw->ip_isp->isphw_abt->rgb_gtm_support && (blk_type->sub_block == DCAM_BLOCK_GTM))
		blk_type->sub_block = ISP_BLOCK_RGB_GTM;

	switch (blk_type->sub_block) {
	case DCAM_BLOCK_BLC:
	case DCAM_BLOCK_RGBG:
	case DCAM_BLOCK_RGBG_DITHER:
	case DCAM_BLOCK_PDAF:
	case DCAM_BLOCK_LSC:
	case DCAM_BLOCK_BAYERHIST:
	case DCAM_BLOCK_AEM:
	case DCAM_BLOCK_AFL:
	case DCAM_BLOCK_AWBC:
	case DCAM_BLOCK_BPC:
	case DCAM_BLOCK_LSCM:
	case DCAM_BLOCK_AFM:
	case DCAM_BLOCK_3DNR_ME:
	case DCAM_BLOCK_GTM:
	case DCAM_BLOCK_RDS:
		blk_type->block_type = DCAM_BLOCK_TYPE;
		break;
	case ISP_BLOCK_RGB_GTM:
	case ISP_BLOCK_RGB_LTM:
	case ISP_BLOCK_GAMMA:
	case ISP_BLOCK_CMC:
	case ISP_BLOCK_CFA:
	case ISP_BLOCK_NLM:
	case ISP_BLOCK_CCE:
	case ISP_BLOCK_HIST2:
	case ISP_BLOCK_BCHS:
	case ISP_BLOCK_CNR_H:
	case ISP_BLOCK_POST_CNR_H:
	case ISP_BLOCK_CDN:
	case ISP_BLOCK_EDGE:
	case ISP_BLOCK_3DLUT:
	case ISP_BLOCK_UVD:
	case ISP_BLOCK_YGAMMA:
	case ISP_BLOCK_YRANDOM:
	case ISP_BLOCK_YNR:
	case ISP_BLOCK_HSV:
	case ISP_BLOCK_3DNR:
	case ISP_BLOCK_PRE_CDN:
	case ISP_BLOCK_POST_CDN:
	case ISP_BLOCK_IIRCNR:
	case ISP_BLOCK_YUV_LTM:
	case ISP_BLOCK_HIST:
	case ISP_BLOCK_NOISEFILTER:
		blk_type->block_type = ISP_BLOCK_TYPE;
		break;
	default:
		pr_err("fail to get valid block %d\n", blk_type->sub_block);
		ret = -EFAULT;
		break;
	}
	return ret;
}

static struct cam_cfg_entry cam_hw_cfg_func_tab[ISP_BLOCK_TOTAL] = {
	[DCAM_BLOCK_BLC]         = {DCAM_BLOCK_BLC,         dcam_k_cfg_blc},
	[DCAM_BLOCK_AEM]         = {DCAM_BLOCK_AEM,         dcam_k_cfg_aem},
	[DCAM_BLOCK_AWBC]        = {DCAM_BLOCK_AWBC,        dcam_k_cfg_awbc},
	[DCAM_BLOCK_AFM]         = {DCAM_BLOCK_AFM,         dcam_k_cfg_afm},
	[DCAM_BLOCK_AFL]         = {DCAM_BLOCK_AFL,         dcam_k_cfg_afl},
	[DCAM_BLOCK_LSC]         = {DCAM_BLOCK_LSC,         dcam_k_cfg_lsc},
	[DCAM_BLOCK_BPC]         = {DCAM_BLOCK_BPC,         dcam_k_cfg_bpc},
	[DCAM_BLOCK_RGBG]        = {DCAM_BLOCK_RGBG,        dcam_k_cfg_rgb_gain},
	[DCAM_BLOCK_RGBG_DITHER] = {DCAM_BLOCK_RGBG_DITHER, dcam_k_cfg_rgb_dither},
	[DCAM_BLOCK_PDAF]        = {DCAM_BLOCK_PDAF,        dcam_k_cfg_pdaf},
	[DCAM_BLOCK_BAYERHIST]   = {DCAM_BLOCK_BAYERHIST,   dcam_k_cfg_bayerhist},
	[DCAM_BLOCK_3DNR_ME]     = {DCAM_BLOCK_3DNR_ME,     dcam_k_cfg_3dnr_me},
	[DCAM_BLOCK_LSCM]        = {DCAM_BLOCK_LSCM,        dcam_k_cfg_lscm},
	[DCAM_BLOCK_RDS]         = {DCAM_BLOCK_RDS,         dcam_k_cfg_rds},
	[ISP_BLOCK_CCE]          = {ISP_BLOCK_CCE,          isp_k_cfg_cce},
	[ISP_BLOCK_NLM]          = {ISP_BLOCK_NLM,          isp_k_cfg_nlm},
	[ISP_BLOCK_CFA]          = {ISP_BLOCK_CFA,          isp_k_cfg_cfa},
	[ISP_BLOCK_YGAMMA]       = {ISP_BLOCK_YGAMMA,       isp_k_cfg_ygamma},
	[ISP_BLOCK_GAMMA]        = {ISP_BLOCK_GAMMA,        isp_k_cfg_gamma},
	[ISP_BLOCK_CMC]          = {ISP_BLOCK_CMC,          isp_k_cfg_cmc10},
	[ISP_BLOCK_PRE_CDN]      = {ISP_BLOCK_PRE_CDN,      isp_k_cfg_pre_cdn},
	[ISP_BLOCK_YNR]          = {ISP_BLOCK_YNR,          isp_k_cfg_ynr},
	[ISP_BLOCK_UVD]          = {ISP_BLOCK_UVD,          isp_k_cfg_uvd},
	[ISP_BLOCK_CDN]          = {ISP_BLOCK_CDN,          isp_k_cfg_cdn},
	[ISP_BLOCK_EDGE]         = {ISP_BLOCK_EDGE,         isp_k_cfg_edge},
	[ISP_BLOCK_POST_CDN]     = {ISP_BLOCK_POST_CDN,     isp_k_cfg_post_cdn},
	[ISP_BLOCK_IIRCNR]       = {ISP_BLOCK_IIRCNR,       isp_k_cfg_iircnr},
	[ISP_BLOCK_YRANDOM]      = {ISP_BLOCK_YRANDOM,      isp_k_cfg_yrandom},
	[ISP_BLOCK_BCHS]         = {ISP_BLOCK_BCHS,         isp_k_cfg_bchs},
	[ISP_BLOCK_HIST2]        = {ISP_BLOCK_HIST2,        isp_k_cfg_hist2},
	[ISP_BLOCK_RGB_LTM]      = {ISP_BLOCK_RGB_LTM,      isp_k_cfg_rgb_ltm},
	[ISP_BLOCK_YUV_LTM]      = {ISP_BLOCK_YUV_LTM,      isp_k_cfg_yuv_ltm},
	[ISP_BLOCK_RGB_GTM]      = {ISP_BLOCK_RGB_GTM,      isp_k_cfg_rgb_gtm},
	[ISP_BLOCK_HSV]          = {ISP_BLOCK_HSV,          isp_k_cfg_hsv},
};

static int camhw_get_block_fun(void *handle, void *arg)
{
	void *block_func = NULL;
	struct cam_hw_block_func_get *fucarg = NULL;

	fucarg = (struct cam_hw_block_func_get *)arg;
	if (fucarg->index < ISP_BLOCK_TOTAL) {
		block_func = (struct cam_cfg_entry *)&cam_hw_cfg_func_tab[fucarg->index];
		fucarg->cam_entry = block_func;
	}

	if (block_func == NULL)
		pr_err("fail to get valid block func %d\n", fucarg->index);

	return 0;
}

static struct hw_io_ctrl_fun cam_ioctl_fun_tab[] = {
	{CAM_HW_GET_ALL_RST,            camhw_get_all_rst},
	{CAM_HW_GET_AXI_BASE,           camhw_get_axi_base},
	{CAM_HW_GET_DCAM_DTS_CLK,       camhw_get_dcam_dts_clk},
	{CAM_HW_GET_ISP_DTS_CLK,        camhw_get_isp_dts_clk},
	{CAM_HW_GET_BLOCK_TYPE,         camhw_get_block_type},
	{CAM_HW_GET_BLK_FUN,            camhw_get_block_fun},
};

static hw_ioctl_fun camhw_ioctl_fun_get(enum cam_hw_cfg_cmd cmd)
{
	hw_ioctl_fun hw_ctrl = NULL;
	uint32_t total_num = 0;
	uint32_t i = 0;

	total_num = sizeof(cam_ioctl_fun_tab) / sizeof(struct hw_io_ctrl_fun);
	for (i = 0; i < total_num; i++) {
		if (cmd == cam_ioctl_fun_tab[i].cmd) {
			hw_ctrl = cam_ioctl_fun_tab[i].hw_ctrl;
			break;
		}
	}

	return hw_ctrl;
}

#undef CAM_HW_ADPT_LAYER

const unsigned long slowmotion_store_addr[3][4] = {
	{
		DCAM_BIN_BASE_WADDR0,
		DCAM_BIN_BASE_WADDR1,
		DCAM_BIN_BASE_WADDR2,
		DCAM_BIN_BASE_WADDR3
	},
	{
		DCAM_AEM_BASE_WADDR,
		DCAM_AEM_BASE_WADDR1,
		DCAM_AEM_BASE_WADDR2,
		DCAM_AEM_BASE_WADDR3
	},
	{
		DCAM_HIST_BASE_WADDR,
		DCAM_HIST_BASE_WADDR1,
		DCAM_HIST_BASE_WADDR2,
		DCAM_HIST_BASE_WADDR3
	}
};

static uint32_t isp_ctx_fmcu_support[ISP_CONTEXT_HW_NUM] = {
	[ISP_CONTEXT_HW_P0] = 1,
	[ISP_CONTEXT_HW_C0] = 1,
	[ISP_CONTEXT_HW_P1] = 1,
	[ISP_CONTEXT_HW_C1] = 1,
};

static int camhwif_dcam_ioctl(void *handle, enum dcam_hw_context_id hw_idx, enum dcam_hw_cfg_cmd cmd, void *arg)
{
	int ret = 0;
	hw_ioctl_fun hw_ctrl = NULL;

	hw_ctrl = dcamhw_ioctl_fun_get(cmd);
	if (hw_ctrl != NULL)
		ret = hw_ctrl(handle, arg);
	else
		pr_debug("hw_core_ctrl_fun is null, cmd %d\n", cmd);

	return ret;
}

static int camhwif_isp_ioctl(void *handle,
	enum isp_hw_cfg_cmd cmd, void *arg)
{
	int ret = 0;
	hw_ioctl_fun hw_ctrl = NULL;

	hw_ctrl = isphw_ioctl_fun_get(cmd);
	if (hw_ctrl != NULL)
		ret = hw_ctrl(handle, arg);
	else
		pr_debug("hw_core_ctrl_fun is null, cmd %d\n", cmd);

	return ret;
}

static int camhwif_cam_ioctl(void *handle,
	enum cam_hw_cfg_cmd cmd, void *arg)
{
	int ret = 0;
	hw_ioctl_fun hw_ctrl = NULL;

	hw_ctrl = camhw_ioctl_fun_get(cmd);
	if (hw_ctrl != NULL)
		ret = hw_ctrl(handle, arg);
	else
		pr_debug("hw_core_ctrl_fun is null, cmd %d\n", cmd);

	return ret;
}

static struct cam_hw_soc_info dcam_soc_info;
static struct cam_hw_soc_info isp_soc_info;
static struct path_hw_abt dcam_path_abt[DCAM_PATH_ID_MAX] = {
	[DCAM_SENSOR_IN_PATH] = {
		.enable = 1,
		.fbc_sup = 0,
		.fbd_sup = 0,
		.type = CAM_PATH_INPUT,
		.full_src_sel_sup = 0,
		.scaler_sup = 0,
		.format[0] = CAM_RAW_14,
		.format[1] = CAM_RAW_8,
		.format[2] = CAM_RAW_HALFWORD_10,
		.format[3] = CAM_RAW_PACK_10,
		.format[4] = CAM_FORMAT_MAX,
	},
	[DCAM_FETCH_IN_PATH] = {
		.enable = 1,
		.fbc_sup = 0,
		.fbd_sup = 0,
		.type = CAM_PATH_INPUT,
		.full_src_sel_sup = 0,
		.scaler_sup = 0,
		.format[0] = CAM_RAW_14,
		.format[1] = CAM_RAW_8,
		.format[2] = CAM_RAW_HALFWORD_10,
		.format[3] = CAM_RAW_PACK_10,
		.format[4] = CAM_FORMAT_MAX,
	},
	[DCAM_RAW_OUT_PATH] = {
		.enable = 0,
	},
	[DCAM_BIN_OUT_PATH] = {
		.enable = 1,
		.fbc_sup = 0,
		.fbd_sup = 0,
		.type = CAM_PATH_OUTPUT,
		.full_src_sel_sup = 0,
		.scaler_sup = 1,
		.scaler_max_ratio = DCAM_RDS_UP_MAX,
		.scaler_min_ratio = 1 / DCAM_RDS_DOWN_MAX,
		.max_in_size_w = DCAM_RDS_MAX_IN_WIDTH,
		.max_out_size_w = DCAM_RDS_MAX_OUT_WIDTH,
		.format[0] = CAM_RAW_14,
		.format[1] = CAM_RAW_8,
		.format[2] = CAM_RAW_HALFWORD_10,
		.format[3] = CAM_RAW_PACK_10,
		.format[4] = CAM_FORMAT_MAX,
	},
	[DCAM_FULL_OUT_PATH] = {
		.enable = 1,
		.fbc_sup = 0,
		.fbd_sup = 0,
		.type = CAM_PATH_OUTPUT,
		.full_src_sel_sup = 1,
		.full_src_sel_type[0] = ORI_RAW_SRC_SEL,
		.full_src_sel_type[1] = PROCESS_RAW_SRC_SEL,
		.scaler_sup = 0,
		.format[0] = CAM_RAW_14,
		.format[1] = CAM_RAW_8,
		.format[2] = CAM_RAW_HALFWORD_10,
		.format[3] = CAM_RAW_PACK_10,
		.format[4] = CAM_FORMAT_MAX,
	},
};

static struct dcam_hw_abt dcamhw_ability = {
	.slm_path = BIT(PORT_BIN_OUT) | BIT(PORT_AEM_OUT) | BIT(PORT_BAYER_HIST_OUT),
	.aux_dcam_path = DCAM_PATH_FULL,
	.dcam_raw_path_id = DCAM_PATH_FULL,
	.sensor_raw_path_id = DCAM_PATH_VCH2,
	.dcam_scaling_path = DCAM_PATH_BIN,
	.fmcu_support = CAM_DISABLE,
	.pyramid_support = CAM_DISABLE,
	.bpc_raw_support = CAM_DISABLE,
	.superzoom_support = CAM_ENABLE,
	.lbuf_share_support = CAM_ENABLE,
	.recovery_support = DCAMINT_ALL_ERROR,
	.dummy_slave_support = DCAM_DUMMY_SW_CLOSE,
	.offline_slice_support = CAM_ENABLE,
	.dcam_offline_fbc_support = CAM_DISABLE,
	.vch3_output_pdaf_support = CAM_DISABLE,
	.mul_raw_output_support = CAM_ENABLE,
	.output_yuv_support = CAM_DISABLE,
	.sensor_32M_support = CAM_DISABLE,
	.sensor_raw_fmt = CAM_RAW_14,
	.store_pyr_fmt = CAM_FORMAT_MAX,
	.store_3dnr_fmt[0] = CAM_YUV420_2FRAME,
	.dcam_zoom_mode = ZOOM_BINNING2,
	.preview_full_size = CAM_DISABLE,
	.save_band_for_bigsize = 0,
	.output_strategy = IMG_POWER_CONSUM_PRI,
	.rgb_gtm_support = CAM_DISABLE,
	.dcam_block[0] = DCAM_BLOCK_BLC,
	.dcam_block[1] = DCAM_BLOCK_RGBG,
	.dcam_block[2] = DCAM_BLOCK_RGBG_DITHER,
	.dcam_block[3] = DCAM_BLOCK_PDAF,
	.dcam_block[4] = DCAM_BLOCK_LSCM,
	.dcam_block[5] = DCAM_BLOCK_BAYERHIST,
	.dcam_block[6] = DCAM_BLOCK_AFL,
	.dcam_block[7] = DCAM_BLOCK_AEM,
	.dcam_block[8] = DCAM_BLOCK_LSC,
	.dcam_block[9] = DCAM_BLOCK_AWBC,
	.dcam_block[10] = DCAM_BLOCK_BPC,
	.dcam_block[11] = DCAM_BLOCK_AFM,
	.dcam_block[12] = DCAM_BLOCK_3DNR_ME,
};

static struct dcamlite_hw_abt dcamlitehw_ability = {
	.sensor_raw_fmt = CAM_RAW_PACK_10,
	.raw_fmt_support[0] = CAM_RAW_HALFWORD_10,
	.raw_fmt_support[1] = CAM_RAW_PACK_10,
	.raw_fmt_support[2] = CAM_FORMAT_MAX,
	.dcam_output_fmt[0] = CAM_RAW_BASE,
};

static struct cam_hw_ip_info dcam[DCAM_ID_MAX] = {
	[DCAM_ID_0] = {
		.dcampath_abt[DCAM_SENSOR_IN_PATH]= &dcam_path_abt[DCAM_SENSOR_IN_PATH],
		.dcampath_abt[DCAM_FETCH_IN_PATH]= &dcam_path_abt[DCAM_FETCH_IN_PATH],
		.dcampath_abt[DCAM_RAW_OUT_PATH]= &dcam_path_abt[DCAM_RAW_OUT_PATH],
		.dcampath_abt[DCAM_BIN_OUT_PATH]= &dcam_path_abt[DCAM_BIN_OUT_PATH],
		.dcampath_abt[DCAM_FULL_OUT_PATH]= &dcam_path_abt[DCAM_FULL_OUT_PATH],
		.dcamhw_abt = &dcamhw_ability,
	},
	[DCAM_ID_1] = {
		.dcampath_abt[DCAM_SENSOR_IN_PATH]= &dcam_path_abt[DCAM_SENSOR_IN_PATH],
		.dcampath_abt[DCAM_FETCH_IN_PATH]= &dcam_path_abt[DCAM_FETCH_IN_PATH],
		.dcampath_abt[DCAM_RAW_OUT_PATH]= &dcam_path_abt[DCAM_RAW_OUT_PATH],
		.dcampath_abt[DCAM_BIN_OUT_PATH]= &dcam_path_abt[DCAM_BIN_OUT_PATH],
		.dcampath_abt[DCAM_FULL_OUT_PATH]= &dcam_path_abt[DCAM_FULL_OUT_PATH],
		.dcamhw_abt = &dcamhw_ability,
	},
	[DCAM_ID_2] = {
		.dcamlitehw_abt = &dcamlitehw_ability,
	},
};

static struct path_hw_abt isp_path_abt[ISP_PATH_ID_MAX] = {
	[ISP_FETCH_IN_PATH] = {
		.enable = 1,
		.fbc_sup = 0,
		.fbd_sup = 0,
		.type = CAM_PATH_INPUT,
		.full_src_sel_sup = 0,
		.scaler_sup = 0,
		.format[0] = CAM_RAW_PACK_10,
		.format[1] = CAM_RAW_HALFWORD_10,
		.format[2] = CAM_RAW_14,
	},
	[ISP_PRECAP_OUT_PATH] = {
		.enable = 1,
		.fbc_sup = 0,
		.fbd_sup = 0,
		.type = CAM_PATH_OUTPUT,
		.full_src_sel_sup = 0,
		.scaler_sup = 1,
		.scaler_max_ratio = ISP_SCALER_UP_MAX,
		.scaler_min_ratio = 1 / ISP_SCALER_UP_MAX,
		.format[0] = CAM_YUV420_2FRAME,
		.format[1] = CAM_YVU420_2FRAME,
	},
	[ISP_VID_OUT_PATH] = {
		.enable = 1,
		.fbc_sup = 0,
		.fbd_sup = 0,
		.type = CAM_PATH_OUTPUT,
		.full_src_sel_sup = 0,
		.scaler_sup = 1,
		.scaler_max_ratio = ISP_SCALER_UP_MAX,
		.scaler_min_ratio = 1 / ISP_SCALER_UP_MAX,
		.format[0] = CAM_YUV420_2FRAME,
		.format[1] = CAM_YVU420_2FRAME,
	},
	[ISP_THUMB_OUT_PATH] = {
		.enable = 1,
		.fbc_sup = 0,
		.fbd_sup = 0,
		.type = CAM_PATH_OUTPUT,
		.full_src_sel_sup = 0,
		.scaler_sup = 1,
		.scaler_max_ratio = ISP_THUMB_SCL_UP_MAX,
		.scaler_min_ratio = 1 / ISP_SCALER_UP_MAX,
		.max_out_size_w = ISP_THUMB_SCL_WIDTH_MAX,
		.max_out_size_h = ISP_THUMB_SCL_HEIGHT_MAX,
		.format[0] = CAM_YUV420_2FRAME,
		.format[1] = CAM_YVU420_2FRAME,
	},
};

static struct isp_hw_abt isphw_ability = {
	.slm_cfg_support = CAM_ENABLE,
	.ctx_fmcu_support = isp_ctx_fmcu_support,
	.fetch_raw_support = CAM_ENABLE,
	.pyr_rec_support = CAM_DISABLE,
	.pyr_dec_support = CAM_DISABLE,
	.rgb_ltm_support = CAM_ENABLE,
	.rgb_gtm_support = CAM_ENABLE,
	.frbg_hist_support = CAM_DISABLE,
	.static_map_support = CAM_DISABLE,
	.nr3_mv_alg_version = ALG_NR3_MV_VER_0,
	.dyn_overlap_version = ALG_ISP_OVERLAP_VER_1,
	.thumb_scaler_cal_version = ISP_THUMB_SCL_VER_0,
	.scaler_coeff_ex = 1,
	.scaler_bypass_ctrl = 1,
	.max_size_w = ISP_WIDTH_MAX,
	.max_size_h = ISP_HEIGHT_MAX,
	.isp_scaling_path_id = ISP_SPATH_CP,
	.reserved_iommu_map_support = CAM_DISABLE,
	.isp_block[0] = ISP_BLOCK_NLM,
	.isp_block[1] = ISP_BLOCK_RGB_GTM,
	.isp_block[2] = ISP_BLOCK_CFA,
	.isp_block[3] = ISP_BLOCK_RGB_LTM,
	.isp_block[4] = ISP_BLOCK_GAMMA,
	.isp_block[5] = ISP_BLOCK_HSV,
	.isp_block[6] = ISP_BLOCK_CMC,
	.isp_block[7] = ISP_BLOCK_CCE,
	.isp_block[8] = ISP_BLOCK_UVD,
	.isp_block[9] = ISP_BLOCK_PRE_CDN,
	.isp_block[10] = ISP_BLOCK_CDN,
	.isp_block[11] = ISP_BLOCK_POST_CDN,
	.isp_block[12] = ISP_BLOCK_YNR,
	.isp_block[13] = ISP_BLOCK_EDGE,
	.isp_block[14] = ISP_BLOCK_YGAMMA,
	.isp_block[15] = ISP_BLOCK_IIRCNR,
	.isp_block[16] = ISP_BLOCK_BCHS,
	.isp_block[17] = ISP_BLOCK_YRANDOM,
};

static struct cam_hw_ip_info isp = {
	.isppath_abt[ISP_FETCH_IN_PATH] = &isp_path_abt[ISP_FETCH_IN_PATH],
	.isppath_abt[ISP_PRECAP_OUT_PATH] = &isp_path_abt[ISP_PRECAP_OUT_PATH],
	.isppath_abt[ISP_VID_OUT_PATH] = &isp_path_abt[ISP_VID_OUT_PATH],
	.isppath_abt[ISP_THUMB_OUT_PATH] = &isp_path_abt[ISP_THUMB_OUT_PATH],
	.isphw_abt = &isphw_ability,
};

struct cam_hw_info qogirl6_hw_info = {
	.prj_id = QOGIRL6,
	.pdev = NULL,
	.soc_dcam = &dcam_soc_info,
	.soc_isp = &isp_soc_info,
	.soc_dcam_lite = NULL,
	.ip_dcam[DCAM_ID_0] = &dcam[DCAM_ID_0],
	.ip_dcam[DCAM_ID_1] = &dcam[DCAM_ID_1],
	.ip_dcam[DCAM_ID_2] = &dcam[DCAM_ID_2],
	.ip_isp = &isp,
	.dcam_ioctl = camhwif_dcam_ioctl,
	.isp_ioctl = camhwif_isp_ioctl,
	.cam_ioctl = camhwif_cam_ioctl,
	.csi_connect_type = DCAM_BIND_DYNAMIC,
};
