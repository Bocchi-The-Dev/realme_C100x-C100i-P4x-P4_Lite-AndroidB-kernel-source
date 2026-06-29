/*
*SPDX-FileCopyrightText: 2020 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier: GPL-2.0-only
*/

#include <linux/regmap.h>
#include "dw_dptx.h"

#define DP_TEST_PHY_PATTERN_CP2520_2			0x6
#define DP_TEST_PHY_PATTERN_CP2520_3_TPS4		0x7

#define DP_TEST_DYNAMIC_RANGE_SHIFT			3
#define DP_TEST_DYNAMIC_RANGE_MASK			BIT(3)

#define DP_TEST_YCBCR_COEFF_SHIFT			4
#define DP_TEST_YCBCR_COEFF_MASK			BIT(4)

static int handle_test_link_training(struct dptx *dptx)
{
	struct video_params *vparams;
	struct dtd *mdtd;
	int retval;
	int ret_dpcd;
	u8 lanes = 0;
	u8 rate = 0;
	u32 phyifctrl;

	dptx_enable_ssc(dptx);

	/* Move to P0 */
	phyifctrl = dptx_readl(dptx, DPTX_PHYIF_CTRL);
	phyifctrl &= ~DPTX_PHYIF_CTRL_LANE_PWRDOWN_MASK;
	dptx_writel(dptx, DPTX_PHYIF_CTRL, phyifctrl);

	ret_dpcd = drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_LINK_RATE, &rate);
	if (ret_dpcd < 0)
		return ret_dpcd;

	retval = dptx_bw_to_phy_rate(rate);
	if (retval < 0)
		return retval;

	rate = retval;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_LANE_COUNT, &lanes);
	if (ret_dpcd < 0)
		return ret_dpcd;

	DRM_DEBUG("%s: Strating link training rate=%d, lanes=%d\n",
		 __func__, rate, lanes);

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;

	retval = dptx_video_ts_calculate(dptx, lanes, rate,
					 vparams->bpc, vparams->pix_enc,
					 mdtd->pixel_clock);
	if (retval)
		return retval;

	retval = dptx_link_training(dptx, rate, lanes);
	if (retval)
		DRM_ERROR("Link training failed %d\n", retval);
	else
		DRM_DEBUG("Link training succeeded\n");

	return retval;
}

static int dptx_set_custom_pattern(struct dptx *dptx)
{
	int ret_dpcd;
	u8 pattern0, pattern1, pattern2, pattern3, pattern4, pattern5,
	    pattern6, pattern7, pattern8, pattern9;

	u32 custompat0;
	u32 custompat1;
	u32 custompat2;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_7_0,
			      &pattern0);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_15_8,
			      &pattern1);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_23_16,
			      &pattern2);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_31_24,
			      &pattern3);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_39_32,
			      &pattern4);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_47_40,
			      &pattern5);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_55_48,
			      &pattern6);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_63_56,
			      &pattern7);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_71_64,
			      &pattern8);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_80BIT_CUSTOM_PATTERN_79_72,
			      &pattern9);
	if (ret_dpcd < 0)
		return ret_dpcd;

	/*
	 * Calculate 30,30 and 20 bits custom patterns
	 * depending on TEST_80BIT_CUSTOM_PATTERN sequence
	 */
	custompat0 =
	    ((((((pattern3 & (0xff >> 2)) << 8) | pattern2) << 8) |
	     pattern1) << 8) | pattern0;
	custompat1 =
	    ((((((((pattern7 & (0xf)) << 8) | pattern6) << 8) | pattern5) << 8)
	      | pattern4) << 2) | ((pattern3 >> 6) & 0x3);
	custompat2 =
	    (((pattern9 << 8) | pattern8) << 4) | ((pattern7 >> 4) & 0xf);

	dptx_writel(dptx, DPTX_CUSTOMPAT0, custompat0);
	dptx_writel(dptx, DPTX_CUSTOMPAT1, custompat1);
	dptx_writel(dptx, DPTX_CUSTOMPAT2, custompat2);

	return 0;
}

static int adjust_vswing_and_preemphasis(struct dptx *dptx)
{
	int ret_dpcd, i;
	u8 lane_01 = 0;
	u8 lane_23 = 0;
	u8 pe = 0;
	u8 vs = 0;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_ADJUST_REQUEST_LANE0_1,
			      &lane_01);
	if (ret_dpcd < 0)
		return ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_ADJUST_REQUEST_LANE2_3,
			      &lane_23);
	if (ret_dpcd < 0)
		return ret_dpcd;

	for (i = 0; i < dptx->link.lanes; i++) {
		switch (i) {
		case 0:
			pe = (lane_01 & DP_ADJUST_PRE_EMPHASIS_LANE0_MASK)
			    >> DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT;
			vs = (lane_01 & DP_ADJUST_VOLTAGE_SWING_LANE0_MASK)
			    >> DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT;
			break;
		case 1:
			pe = (lane_01 & DP_ADJUST_PRE_EMPHASIS_LANE1_MASK)
			    >> DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT;
			vs = (lane_01 & DP_ADJUST_VOLTAGE_SWING_LANE1_MASK)
			    >> DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT;
			break;
		case 2:
			pe = (lane_23 & DP_ADJUST_PRE_EMPHASIS_LANE0_MASK)
			    >> DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT;
			vs = (lane_23 & DP_ADJUST_VOLTAGE_SWING_LANE0_MASK)
			    >> DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT;
			break;
		case 3:
			pe = (lane_23 & DP_ADJUST_PRE_EMPHASIS_LANE1_MASK)
			    >> DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT;
			vs = (lane_23 & DP_ADJUST_VOLTAGE_SWING_LANE1_MASK)
			    >> DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT;
			break;
		default:
			break;
		}

		dptx_phy_set_pre_emphasis(dptx, i, pe);
		dptx_phy_set_vswing(dptx, i, vs);
	}

	/*
	 * 0x8 bit7:PHY_REG_EN; bit0~15:PHY_REG_ADDR.
	 * 0xc bit0-31: PHY_REG_DATA.
	 */

	/* tuning override phy eq value */
	if (vs == 0 && pe == 0) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x570);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2000);
	}

	if (vs == 0 && pe == 1) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x5d0);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2300);
	}

	if (vs == 0 && pe == 2) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x620);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2580);
	}

	if (vs == 0 && pe == 3) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x6a0);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2980);
	}

	if (vs == 1 && pe == 0) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x620);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2000);
	}

	if (vs == 1 && pe == 1) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x6a0);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2400);
	}

	if (vs == 1 && pe == 2) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x700);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2700);
	}

	if (vs == 2 && pe == 0) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x6e0);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2000);
	}

	if (vs == 2 && pe == 1) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x760);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2400);
	}

	if (vs == 3 && pe == 0) {
		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19002);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x7e0);

		regmap_write(dptx->ipa_usb31_dp, 0x8, 0x19003);
		regmap_write(dptx->ipa_usb31_dp, 0xc, 0x2000);
	}

	return 0;
}

static int handle_test_edid_read(struct dptx *dptx)
{
	int test_resp = 0;

	if (drm_dp_dpcd_writeb(&dptx->aux_dev, DP_TEST_EDID_CHECKSUM,
			    dptx->sink_edid_checksum) <= 0) {
		DRM_ERROR("DPCD failed write DP_TEST_EDID_CHECKSUM register\n");
		return false;
	}

	test_resp = DP_TEST_ACK | DP_TEST_EDID_CHECKSUM_WRITE;

	if (drm_dp_dpcd_writeb(&dptx->aux_dev, DP_TEST_RESPONSE, test_resp) <= 0) {
		DRM_ERROR("DPCD failed write DP_TEST_RESPONSE register\n");
		return false;
	}

	return true;
}

static int handle_test_phy_pattern(struct dptx *dptx)
{
	u8 pattern = 0;
	int retval;
	int ret_dpcd;

	ret_dpcd =
	    drm_dp_dpcd_readb(&dptx->aux_dev, DP_PHY_TEST_PATTERN, &pattern);
	if (ret_dpcd < 0)
		return ret_dpcd;

	pattern &= DP_PHY_TEST_PATTERN_SEL_MASK;

	switch (pattern) {
	case DP_PHY_TEST_PATTERN_NONE:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		DRM_DEBUG("No test pattern selected\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);
		break;
	case DP_PHY_TEST_PATTERN_D10_2:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		DRM_DEBUG("D10.2 without scrambling test phy pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_1);
		break;
	case DP_PHY_TEST_PATTERN_ERROR_COUNT:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		DRM_DEBUG("Symbol error measurement count test phy pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_SYM_ERM);
		break;
	case DP_PHY_TEST_PATTERN_PRBS7:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		DRM_DEBUG("PRBS7 test phy pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_PRBS7);
		break;
	case DP_PHY_TEST_PATTERN_80BIT_CUSTOM:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		DRM_DEBUG("80-bit custom pattern transmitted test phy pattern\n");

		retval = dptx_set_custom_pattern(dptx);
		if (retval)
			return retval;
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_CUSTOM80);
		break;
	case DP_PHY_TEST_PATTERN_CP2520:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		DRM_DEBUG("CP2520_1 - HBR2 Compliance EYE pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_CP2520_1);
		break;
	case DP_TEST_PHY_PATTERN_CP2520_2:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		DRM_DEBUG("CP2520_2 - pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_CP2520_2);
		break;
	case DP_TEST_PHY_PATTERN_CP2520_3_TPS4:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		DRM_DEBUG("DP_TEST_PHY_PATTERN_CP2520_3_TPS4 - pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_4);
		break;
	default:
		DRM_DEBUG("Invalid TEST_PHY_PATTERN\n");
		return -EINVAL;
	}
	return retval;
}

int handle_automated_test_request(struct dptx *dptx)
{
	int retval;
	int ret_dpcd;
	u8 test = 0;

	ret_dpcd = drm_dp_dpcd_readb(&dptx->aux_dev, DP_TEST_REQUEST, &test);
	if (ret_dpcd < 0)
		return ret_dpcd;

	if (test & DP_TEST_LINK_TRAINING) {
		DRM_DEBUG("%s: DP_TEST_LINK_TRAINING\n", __func__);

		ret_dpcd =
		    drm_dp_dpcd_writeb(&dptx->aux_dev, DP_TEST_RESPONSE,
				       DP_TEST_ACK);
		if (ret_dpcd < 0)
			return ret_dpcd;

		retval = handle_test_link_training(dptx);
		if (retval)
			return retval;
	}

	if (test & DP_TEST_LINK_EDID_READ) {
		/* This should happen on HOTPLUG */
		DRM_DEBUG("%s:DP_TEST_LINK_EDID_READ\n", __func__);
		ret_dpcd =
		    drm_dp_dpcd_writeb(&dptx->aux_dev, DP_TEST_RESPONSE,
				       DP_TEST_ACK);
		if (ret_dpcd < 0)
			return ret_dpcd;

		retval = handle_test_edid_read(dptx);
		if (retval)
			return 0;
		else
			return -ENOTSUPP;
	}
	if (test & DP_TEST_LINK_PHY_TEST_PATTERN) {
		DRM_DEBUG("%s:DP_TEST_LINK_PHY_TEST_PATTERN\n", __func__);
		retval = handle_test_phy_pattern(dptx);
		if (retval)
			return retval;
	}
	return 0;
}

