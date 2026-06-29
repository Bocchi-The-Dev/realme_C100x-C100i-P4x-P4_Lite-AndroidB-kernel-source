/*
 *
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Filename : gnss_common_ctl.c
 * Abstract : This file is a implementation for driver of gnss
 *
 * Authors  : zhaohui.chen  cheng.liu
 *
 */

#include <linux/bug.h>
#include <linux/delay.h>
#include "marlin_platform.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/notifier.h>
#include <linux/version.h>
#include <linux/pinctrl/consumer.h>
#include "wcn_bus.h"
#include <linux/platform_data/sprd_ump96xx_tsensor.h>
#include "gnss_common.h"

#define GNSS_DATA_BASE_TYPE_H  16
#define GNSS_MAX_STRING_LEN	10
#define GNSS_DUMP_DATA_START_UP 1
#define FIRMWARE_FILEPATHNAME_LENGTH_MAX 256

/*ums9230_4h10*/
#define QOGIRL6_UART_INF4_SYS_SEL	"uart_matrix_mtx_cfg"
#define QOGIRL6_TSX_OSC_SEL		"tsx_osc_sel"
#define QOGIRL6_T_DIG			"t_dig"
#define QOGIRL6_FM_CLK			"fm_clk"
#define QOGIRL6_FM_DATA			"fm_data"
/*ums9620*/
#define QOGIRN6PRO_UART_INF4_SYS_SEL	"uart_matrix_mtx_cfg"
#define QOGIRN6PRO_U4TXD		"u4txd"
#define QOGIRN6PRO_U4RXD		"u4rxd"
#define QOGIRN6PRO_U4CTS		"u4cts"
#define QOGIRN6PRO_U4RTS		"u4rts"
/*ums9621*/
#define QOGIRN6LITE_UART_INF4_SYS_SEL	"uart_matrix_mtx_cfg"
#define QOGIRN6LITE_U4TXD		"u4txd"
#define QOGIRN6LITE_U4RXD		"u4rxd"
#define QOGIRN6LITE_U4CTS		"u4cts"
#define QOGIRN6LITE_U4RTS		"u4rts"

struct gnss_common_ctl {
	struct device *dev;
	struct pinctrl *pctrl;
	unsigned long chip_ver;
	unsigned int gnss_status;
	unsigned int gnss_subsys;
	unsigned int board_platform;
	char firmware_path[FIRMWARE_FILEPATHNAME_LENGTH_MAX];
};

static struct gnss_common_ctl gnss_common_ctl_dev;

enum gnss_status_e {
	GNSS_STATUS_POWEROFF = 0,
	GNSS_STATUS_POWERON,
	GNSS_STATUS_ASSERT,
	GNSS_STATUS_POWEROFF_GOING,
	GNSS_STATUS_POWERON_GOING,
	GNSS_STATUS_MAX,
};

struct gnss_exception_chr_event {
	char *namestr;
	enum wcn_chr_loglevel loglevel;
	uint32_t event_id;
};

/**
 * GNSS CHR Event ID range:
 *     HAL     : 0xB0000 ~ 0xBFFFF
 *     Kernel  : 0xC0000 ~ 0xCFFFF
 *     Firmware: 0xD0000 ~ 0xDFFFF
 */
static const struct gnss_exception_chr_event gnss_exception_chr_event_table[] = {
	/* Exception String      CHR Level        Event ID */
	{"WCN-CP2-EXCEPTION",    WCN_CHR_ERROR,   0xD0001},
	{"heartcount-trigger",   WCN_CHR_ERROR,   0xD0002},
	{"mem-dump-trigger",     WCN_CHR_ERROR,   0xD0003},
	{"getversion-trigger",   WCN_CHR_ERROR,   0xD0004},
	{"idleon-trigger",       WCN_CHR_ERROR,   0xD0005},
	{"idleoff-trigger",      WCN_CHR_ERROR,   0xD0006},
	{"wakeup-trigger",       WCN_CHR_ERROR,   0xD0007},
	{"sleep-trigger",        WCN_CHR_ERROR,   0xD0008},
	{"firmware-trigger",     WCN_CHR_ERROR,   0xD0009},
	{"donwload-trigger",     WCN_CHR_ERROR,   0xC0000},
};

enum gnss_cp_status_subtype {
	GNSS_CP_STATUS_CALI = 1,
	GNSS_CP_STATUS_INIT = 2,
	GNSS_CP_STATUS_INIT_DONE = 3,
	GNSS_CP_STATUS_IDLEOFF = 4,
	GNSS_CP_STATUS_IDLEON = 5,
	GNSS_CP_STATUS_SLEEP = 6,
	GNSS_CP_STATUS__MAX,
};

enum gnss_board_platform {
	GNSS_PLTF_L6_MERLION = 49,
	GNSS_PLTF_L6_MARLIN3LITE,
	GNSS_PLTF_N6PRO,
	GNSS_PLTF_N6LITE,
};

static unsigned int gnssver = 0x22;
static const struct of_device_id gnss_common_ctl_of_match[] = {
	{.compatible = "sprd,gnss", .data = (void *)&gnssver},
	{},
};

static const struct of_device_id pmic_of_match[] = {
	{ .compatible = "sprd,sc27xx-syscon",  },
	{ .compatible = "sprd,ump9622-syscon", },
	{ .compatible = "sprd,ump9652-syscon", },
	{},
};

static int isM3lite(void)
{
	int iRet = FALSE;
	struct wcn_match_data *ptr_match_config = get_wcn_match_config();

	if (ptr_match_config && ptr_match_config->unisoc_wcn_m3lite)
		iRet = TRUE;

	return iRet;
}

static int isQogirl6(void)
{
	int iRet = FALSE;

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6)
		iRet = TRUE;

	return iRet;
}

static void pmic_sc27xx_tsen_enable(struct regmap *regmap,
					unsigned int base, int type)
{
	unsigned int value = 0, temp = 0;
	struct device *dev = gnss_common_ctl_dev.dev;

	dev_err(dev, "%s sc27xx-syscon base 0x%x\n", __func__, base);
	regmap_read(regmap, (REGS_ANA_APB_BASE + XTL_WAIT_CTRL0), &value);
	dev_err(dev, "%s XTL_WAIT_CTRL0 value read 0x%x\n", __func__, value);
	temp = value | BIT_XTL_EN;
	regmap_write(regmap, (REGS_ANA_APB_BASE + XTL_WAIT_CTRL0), temp);
	regmap_read(regmap, (REGS_ANA_APB_BASE + XTL_WAIT_CTRL0), &value);
	dev_err(dev, "%s 2nd read 0x%x\n", __func__, value);

	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL0), &value);
	dev_err(dev, "%s TSEN_CTRL0 value read 0x%x\n", __func__, value);
	temp = value | BIT_TSEN_CLK_SRC_SEL | BIT_TSEN_ADCLDO_EN;
	regmap_write(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL0), temp);
	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL0), &value);
	dev_err(dev, "%s 2nd read 0x%x\n", __func__, value);

	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL1), &value);
	dev_err(dev, "%s TSEN_CTRL1 value read 0x%x\n", __func__, value);
	temp = value | BIT_TSEN_SDADC_EN | BIT_TSEN_CLK_EN | BIT_TSEN_UGBUF_EN;
	regmap_write(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL1), temp);
	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL1), &value);
	dev_err(dev, "%s 2nd read 0x%x\n", __func__, value);

	regmap_read(regmap, (SC2730_PIN_REG_BASE + PTEST0), &value);
	dev_err(dev, "%s PTEST0 value read 0x%x\n", __func__, value);
	temp = (value & (~PTEST0_MASK)) | PTEST0_sel(2);
	regmap_write(regmap, (SC2730_PIN_REG_BASE + PTEST0), temp);
	regmap_read(regmap, (SC2730_PIN_REG_BASE + PTEST0), &value);
	dev_err(dev, "%s 2nd read 0x%x\n", __func__, value);

	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL3), &value);
	dev_err(dev, "%s TSEN_CTRL3 value read 0x%x\n", __func__, value);
	temp = (value | BIT_TSEN_EN) & (~BIT_TSEN_SEL_EN);
	temp = (temp & (~BIT_TSEN_TIME_SEL_MASK)) | BIT_TSEN_TIME_sel(2);
	regmap_write(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL3), temp);
	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL3), &value);
	dev_err(dev, "%s 2nd read 0x%x\n", __func__, value);

	if (type == TSEN_EXT)
		regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL4), &value);
	else
		regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL5), &value);
	dev_err(dev, "%s 0x%x read 0x%x\n", __func__, TSEN_CTRL4, value);

}

static void pmic_sc27xx_tsen_disable(struct regmap *regmap,
				unsigned int base, int type)
{
	unsigned int value, temp;
	struct device *dev = gnss_common_ctl_dev.dev;

	dev_err(dev, "%s sc27xx-syscon base 0x%x\n", __func__, base);

	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL0), &value);
	dev_err(dev, "%s TSEN_CTRL0 value read 0x%x\n", __func__, value);
	temp = BIT_TSEN_CLK_SRC_SEL | BIT_TSEN_ADCLDO_EN;
	temp = value & (~temp);
	regmap_write(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL0), temp);
	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL0), &value);
	dev_err(dev, "%s 2nd read 0x%x\n", __func__, value);

	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL1), &value);
	dev_err(dev, "%s TSEN_CTRL1 value read 0x%x\n", __func__, value);
	temp = BIT_TSEN_SDADC_EN | BIT_TSEN_CLK_EN | BIT_TSEN_UGBUF_EN;
	temp = value & (~temp);
	regmap_write(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL1), temp);
	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL1), &value);
	dev_err(dev, "%s 2nd read 0x%x\n", __func__, value);

	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL3), &value);
	dev_err(dev, "%s TSEN_CTRL3 value read 0x%x\n", __func__, value);
	temp = (value & (~BIT_TSEN_EN)) | BIT_TSEN_SEL_EN;
	regmap_write(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL3), temp);
	regmap_read(regmap, (REGS_ANA_APB_BASE + TSEN_CTRL3), &value);
	dev_err(dev, "%s 2nd read 0x%x\n", __func__, value);
}

static int gnss_tsen_enable(int type)
{
	struct platform_device *pdev_regmap;
	static struct device_node *regmap_np;
	static struct regmap *regmap;
	static unsigned int base;
	int ret;
	struct device *dev = gnss_common_ctl_dev.dev;

	dev_err(dev, "%s M3l=[%d],L6=[%d]\n", __func__, isM3lite(), isQogirl6());

	if ((isM3lite() == FALSE) && (isQogirl6() == FALSE)) {
		dev_err(dev, "%s not M3lite or L6\n", __func__);
		return -EINVAL;
	}

	if (base == 0) {
		regmap_np = of_find_matching_node(NULL,
						    pmic_of_match);
		if (!regmap_np) {
			dev_err(dev, "%s, error np\n", __func__);
			return -EINVAL;
		}
		pdev_regmap = of_find_device_by_node(regmap_np);
		if (!pdev_regmap) {
			dev_err(dev, "%s, error regmap\n", __func__);
			of_node_put(regmap_np);
			return -EINVAL;
		}
		regmap = dev_get_regmap(pdev_regmap->dev.parent, NULL);
		ret = of_property_read_u32_index(regmap_np, "reg", 0, &base);
		if (ret) {
			dev_err(dev, "%s, error base\n", __func__);
			of_node_put(regmap_np);
			return -EINVAL;
		}
		of_node_put(regmap_np);
	}

	if (of_device_is_compatible(regmap_np, "sprd,sc27xx-syscon"))
		pmic_sc27xx_tsen_enable(regmap, base, type);
	if (of_device_is_compatible(regmap_np, "sprd,ump9622-syscon"))
		gnss_tsen_control(regmap, base + UMP9622_BASE_OFFSET, UMP9622_ENABLE);
	if (of_device_is_compatible(regmap_np, "sprd,ump9652-syscon"))
		gnss_tsen_control(regmap, base + UMP9652_BASE_OFFSET, UMP9652_ENABLE);

	of_node_put(regmap_np);
	return 0;
}

static int gnss_tsen_disable(int type)
{
	struct platform_device *pdev_regmap;
	static struct device_node *regmap_np;
	static struct regmap *regmap;
	static unsigned int base;
	int ret;
	struct device *dev = gnss_common_ctl_dev.dev;

	dev_err(dev, "%s M3l=[%d],L6=[%d]\n", __func__, isM3lite(), isQogirl6());

	if ((isM3lite() == FALSE) && (isQogirl6() == FALSE)) {
		dev_err(dev, "%s not M3lite or L6\n", __func__);
		return -EINVAL;
	}

	if (base == 0) {
		regmap_np = of_find_matching_node(NULL,
						    pmic_of_match);
		if (!regmap_np) {
			dev_err(dev, "%s, error np\n", __func__);
			return -EINVAL;
		}
		pdev_regmap = of_find_device_by_node(regmap_np);
		if (!pdev_regmap) {
			dev_err(dev, "%s, error regmap\n", __func__);
			of_node_put(regmap_np);
			return -EINVAL;
		}
		regmap = dev_get_regmap(pdev_regmap->dev.parent, NULL);
		ret = of_property_read_u32_index(regmap_np, "reg", 0, &base);
		if (ret) {
			dev_err(dev, "%s, error base\n", __func__);
			of_node_put(regmap_np);
			return -EINVAL;
		}
		of_node_put(regmap_np);
	}
	if (of_device_is_compatible(regmap_np, "sprd,sc27xx-syscon"))
		pmic_sc27xx_tsen_disable(regmap, base, type);
	if (of_device_is_compatible(regmap_np, "sprd,ump9622-syscon"))
		gnss_tsen_control(regmap, base + UMP9622_BASE_OFFSET, UMP9622_DISABLE);
	if (of_device_is_compatible(regmap_np, "sprd,ump9652-syscon"))
		gnss_tsen_control(regmap, base + UMP9652_BASE_OFFSET, UMP9652_DISABLE);

	of_node_put(regmap_np);
	return 0;
}

static void gnss_tcxo_enable(void)
{
	u32 val_buf = 0;
	u32 val_ctl = 0;
	struct device *dev = gnss_common_ctl_dev.dev;

	dev_err(dev, "%s M3l=[%d],L6=[%d]\n", __func__, isM3lite(), isQogirl6());

	if ((isM3lite() == FALSE) || (isQogirl6() == FALSE)) {
		dev_err(dev, "%s not M3lite or L6\n", __func__);
		return;
	}

	wcn_read_data_from_phy_addr(XTLBUF3_REL_CFG_ADDR, (void *)&val_buf, 4);
	wcn_read_data_from_phy_addr(WCN_XTL_CTRL_ADDR, (void *)&val_ctl, 4);
	dev_info(dev, "tcxo_en 1read buf=%x ctl=%x\n", val_buf, val_ctl);

	/* XTLBUF3_REL_CF bit[1:0]=01 */
	val_buf &= ~(0x1 << 1);
	val_buf |=  (0x1 << 0);
	/* WCN_XTL_CTRL bit[2:1]=01 */
	val_ctl &= ~(0x1 << 2);
	val_ctl |=  (0x1 << 1);
	dev_info(dev, "tcxo_en write buf=%x ctl=%x\n", val_buf, val_ctl);

	wcn_write_data_to_phy_addr(XTLBUF3_REL_CFG_ADDR, (void *)&val_buf, 4);
	wcn_write_data_to_phy_addr(WCN_XTL_CTRL_ADDR, (void *)&val_ctl, 4);

	wcn_read_data_from_phy_addr(XTLBUF3_REL_CFG_ADDR, (void *)&val_buf, 4);
	wcn_read_data_from_phy_addr(WCN_XTL_CTRL_ADDR, (void *)&val_ctl, 4);
	dev_info(dev, "tcxo_en 2read buf=%x ctl=%x\n", val_buf, val_ctl);
}
static void gnss_tcxo_disable(void)
{
	u32 val_buf = 0;
	u32 val_ctl = 0;
	struct device *dev = gnss_common_ctl_dev.dev;

	dev_err(dev, "%s M3l=[%d],L6=[%d]\n", __func__, isM3lite(), isQogirl6());

	if ((isM3lite() == FALSE) || (isQogirl6() == FALSE)) {
		dev_err(dev, "%s not M3lite or L6\n", __func__);
		return;
	}

	wcn_read_data_from_phy_addr(XTLBUF3_REL_CFG_ADDR, (void *)&val_buf, 4);
	wcn_read_data_from_phy_addr(WCN_XTL_CTRL_ADDR, (void *)&val_ctl, 4);
	dev_info(dev, "tcxo_dis 1read buf=%x ctl=%x\n", val_buf, val_ctl);

	/* bit[1:0] */
	val_buf &= ~(0x1 << 1);
	val_buf &= ~(0x1 << 0);
	/* bit[2:1] */
	val_ctl &= ~(0x1 << 2);
	val_ctl &= ~(0x1 << 1);
	dev_info(dev, "tcxo_dis write buf=%x ctl=%x\n", val_buf, val_ctl);

	wcn_write_data_to_phy_addr(XTLBUF3_REL_CFG_ADDR, (void *)&val_buf, 4);
	wcn_write_data_to_phy_addr(WCN_XTL_CTRL_ADDR, (void *)&val_ctl, 4);

	wcn_read_data_from_phy_addr(XTLBUF3_REL_CFG_ADDR, (void *)&val_buf, 4);
	wcn_read_data_from_phy_addr(WCN_XTL_CTRL_ADDR, (void *)&val_ctl, 4);
	dev_info(dev, "tcxo_dis 2read buf=%x ctl=%x\n", val_buf, val_ctl);
}

static int gnss_pinctrl_set_state(char *name)
{
	struct pinctrl_state *state;
	int ret = 0;
	struct device *dev = gnss_common_ctl_dev.dev;

	state = pinctrl_lookup_state(gnss_common_ctl_dev.pctrl, name);
	if (state){
		ret =  pinctrl_select_state(gnss_common_ctl_dev.pctrl, state);
		if (ret != 0) {
			dev_err(dev, "%s pinctrl select:%s faile\n", __func__, name);
			return -EINVAL;
		}
	} else {
		dev_err(dev, "%s pinctrl lookup:%s faile\n", __func__, name);
		return -EINVAL;
	}
	return ret;
}

static int gnss_uart_pinctrl(void)
{
	static int first_boot = 1;
	int ret = 0;

	if (!first_boot)
		return ret;

	first_boot = 0;

	if (GNSS_PLTF_L6_MARLIN3LITE == gnss_common_ctl_dev.board_platform) {
		ret += gnss_pinctrl_set_state(QOGIRL6_UART_INF4_SYS_SEL);
		ret += gnss_pinctrl_set_state(QOGIRL6_TSX_OSC_SEL);
		ret += gnss_pinctrl_set_state(QOGIRL6_T_DIG);
		ret += gnss_pinctrl_set_state(QOGIRL6_FM_CLK);
		ret += gnss_pinctrl_set_state(QOGIRL6_FM_DATA);
	} else if (GNSS_PLTF_N6PRO == gnss_common_ctl_dev.board_platform) {
		ret += gnss_pinctrl_set_state(QOGIRN6PRO_UART_INF4_SYS_SEL);
		ret += gnss_pinctrl_set_state(QOGIRN6PRO_U4TXD);
		ret += gnss_pinctrl_set_state(QOGIRN6PRO_U4RXD);
		ret += gnss_pinctrl_set_state(QOGIRN6PRO_U4CTS);
		ret += gnss_pinctrl_set_state(QOGIRN6PRO_U4RTS);
	} else if (GNSS_PLTF_N6LITE == gnss_common_ctl_dev.board_platform) {
		ret += gnss_pinctrl_set_state(QOGIRN6LITE_UART_INF4_SYS_SEL);
		ret += gnss_pinctrl_set_state(QOGIRN6LITE_U4TXD);
		ret += gnss_pinctrl_set_state(QOGIRN6LITE_U4RXD);
		ret += gnss_pinctrl_set_state(QOGIRN6LITE_U4CTS);
		ret += gnss_pinctrl_set_state(QOGIRN6LITE_U4RTS);
	}
	return ret;
}

static void gnss_io_channel_config(void)
{
	int ret = 0;
	struct device *dev = gnss_common_ctl_dev.dev;

	if (GNSS_PLTF_L6_MERLION == gnss_common_ctl_dev.board_platform)
		wcn_set_host_direct_gnss(TRUE);
	else {
		ret = gnss_uart_pinctrl();
		if (ret != 0)
		dev_err(dev, "%s gnss uart pinctrl failed ret=%d\n",
			__func__, ret);
	}
}

static void gnss_power_on(bool enable)
{
	int ret;
	struct device *dev = gnss_common_ctl_dev.dev;

	dev_info(dev, "%s en=%d,status=%d v3.2\n", __func__,
		 enable, gnss_common_ctl_dev.gnss_status);

	dev_info(dev, "%s clktp=%d\n", __func__, wcn_get_xtal_26m_clk_type());

	if (enable && gnss_common_ctl_dev.gnss_status == GNSS_STATUS_POWEROFF) {
		gnss_common_ctl_dev.gnss_status = GNSS_STATUS_POWERON_GOING;

		/* only marlin3lite and qogirl6 need */
		if (wcn_get_xtal_26m_clk_type() == WCN_CLOCK_TYPE_TSX)
			gnss_tsen_enable(TSEN_EXT);
		else if (wcn_get_xtal_26m_clk_type() == WCN_CLOCK_TYPE_TCXO)
			gnss_tcxo_enable();
		else
			dev_info(dev, "%s: unknown clk_type\n", __func__);

		if(gnss_common_ctl_dev.board_platform)
			gnss_io_channel_config();

		ret = start_marlin(gnss_common_ctl_dev.gnss_subsys);
		if (ret != 0) {
			dev_err(dev, "%s start marlin failed ret=%d\n",
				__func__, ret);
			gnss_common_ctl_dev.gnss_status = GNSS_STATUS_POWEROFF;
		}
		else {
			gnss_common_ctl_dev.gnss_status = GNSS_STATUS_POWERON;
		}
	} else if (!enable && ((gnss_common_ctl_dev.gnss_status == GNSS_STATUS_POWERON) ||
			(gnss_common_ctl_dev.gnss_status == GNSS_STATUS_ASSERT))) {
		gnss_common_ctl_dev.gnss_status = GNSS_STATUS_POWEROFF_GOING;

		/* only marlin3lite and qogirl6 need */
		if (wcn_get_xtal_26m_clk_type() == WCN_CLOCK_TYPE_TSX)
			gnss_tsen_disable(TSEN_EXT);
		else if (wcn_get_xtal_26m_clk_type() == WCN_CLOCK_TYPE_TCXO)
			gnss_tcxo_disable();
		else
			dev_info(dev, "%s: unknown clk_type\n", __func__);

		ret = stop_marlin(gnss_common_ctl_dev.gnss_subsys);
		if (ret != 0) {
			dev_err(dev, "%s stop marlin failed ret=%d\n",
				__func__, ret);
			gnss_common_ctl_dev.gnss_status = GNSS_STATUS_POWERON;
		}
		else
			gnss_common_ctl_dev.gnss_status = GNSS_STATUS_POWEROFF;
	} else {
		dev_err(dev, "%s status is not match\n", __func__);
	}
}

static ssize_t gnss_power_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long set_value;
	ssize_t ret = count;

	if (kstrtoul(buf, GNSS_MAX_STRING_LEN, &set_value)) {
		dev_err(dev, "%s Maybe store string is too long\n", __func__);
		return -EINVAL;
	}
	dev_info(dev, "%s set_value[%lu]\n", __func__, set_value);
	if (set_value == 1)
		gnss_power_on(1);
	else if (set_value == 0)
		gnss_power_on(0);
	else {
		ret = -EINVAL;
		dev_info(dev, "%s unknown control\n", __func__);
	}

	return ret;
}

static DEVICE_ATTR_WO(gnss_power_enable);

static ssize_t gnss_subsys_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long set_value;
	ssize_t ret = count;
	struct wcn_match_data *ptr_match_config = get_wcn_match_config();
	if (kstrtoul(buf, GNSS_MAX_STRING_LEN, &set_value))
		return -EINVAL;

	dev_info(dev, "%s setVal=%lu\n", __func__, set_value);
	if (ptr_match_config && ptr_match_config->unisoc_wcn_integrated) {
		if (set_value == WCN_GNSS)
			gnss_common_ctl_dev.gnss_subsys = WCN_GNSS;
		else if (set_value == WCN_GNSS_BD)
			gnss_common_ctl_dev.gnss_subsys  = WCN_GNSS_BD;
		else if (set_value == WCN_GNSS_GAL)
			gnss_common_ctl_dev.gnss_subsys  = WCN_GNSS_GAL;
		else if (set_value == GNSS_PLTF_L6_MERLION){
			gnss_common_ctl_dev.board_platform = GNSS_PLTF_L6_MERLION;
			gnss_common_ctl_dev.gnss_subsys = WCN_GNSS;
		}
		else
			ret = -EINVAL;
	} else {
		gnss_common_ctl_dev.gnss_subsys = MARLIN_GNSS;
		if (set_value == GNSS_PLTF_L6_MARLIN3LITE)
			gnss_common_ctl_dev.board_platform = GNSS_PLTF_L6_MARLIN3LITE;
		if (set_value == GNSS_PLTF_N6PRO)
			gnss_common_ctl_dev.board_platform = GNSS_PLTF_N6PRO;
		if (set_value == GNSS_PLTF_N6LITE)
			gnss_common_ctl_dev.board_platform = GNSS_PLTF_N6LITE;
	}
	return ret;
}

static ssize_t gnss_subsys_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int i = 0;

	dev_info(dev, "%s\n", __func__);
	if (gnss_common_ctl_dev.gnss_status == GNSS_STATUS_POWERON) {
		memset(gnss_common_ctl_dev.firmware_path, 0x0,
		       FIRMWARE_FILEPATHNAME_LENGTH_MAX);
		strncpy(&gnss_common_ctl_dev.firmware_path[0],
		       gnss_firmware_path_get(), FIRMWARE_FILEPATHNAME_LENGTH_MAX - 1);

		dev_info(dev, "%s fpath=[%s]\n", __func__,
			 gnss_common_ctl_dev.firmware_path);

		i += scnprintf(buf + i, PAGE_SIZE - i, "%d:%s\n",
				gnss_common_ctl_dev.gnss_subsys,
				&gnss_common_ctl_dev.firmware_path[0]);
	} else {
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				gnss_common_ctl_dev.gnss_subsys);
	}

	return i;
}

static DEVICE_ATTR_RW(gnss_subsys);

static int gnss_status_get(void)
{
	phys_addr_t phy_addr;
	phys_addr_t addr_offset;
	u32 magic_value = 0;
	struct device *dev = gnss_common_ctl_dev.dev;

	if (isQogirl6() == TRUE)
		addr_offset = GNSS_QOGIRL6_STATUS_OFFSET;
	else
		addr_offset = GNSS_STATUS_OFFSET;

	phy_addr = wcn_get_gnss_base_addr() + addr_offset;

	wcn_read_data_from_phy_addr(phy_addr, &magic_value, sizeof(u32));
	dev_info(dev, "%s magic_value=%d\n", __func__, magic_value);

	return magic_value;
}

static void gnss_dump_mem_ctrl_co(char *trigStr)
{
	char flag = 0; /* 0: default, all, 1: only data, pmu, aon */
	unsigned int temp_status = 0;
	static char dump_flag;
	char triggerStr[64];
	struct device *dev = gnss_common_ctl_dev.dev;

	memset(triggerStr, 0, 64);
	strncpy(triggerStr, trigStr, 63);

	dev_info(dev, "%s flag[%d],str[%s]\n", __func__, dump_flag, triggerStr);

	if (dump_flag == 1)
		return;

	dump_flag = 1;

	temp_status = gnss_common_ctl_dev.gnss_status;
	if ((temp_status == GNSS_STATUS_POWERON_GOING) ||
		((temp_status == GNSS_STATUS_POWERON) &&
		(gnss_status_get() != GNSS_CP_STATUS_SLEEP))) {
		flag = (temp_status == GNSS_STATUS_POWERON) ? 0 : 1;
		wcn_assert_interface(WCN_SOURCE_GNSS, triggerStr);
		gnss_common_ctl_dev.gnss_status = GNSS_STATUS_ASSERT;
	}
	dump_flag = 0;
}

static void gnss_dump_mem_ctrl(char *trigStr)
{
	static char dump_flag;
	char triggerStr[64];
	struct device *dev = gnss_common_ctl_dev.dev;

	memset(triggerStr, 0, 64);
	strncpy(triggerStr, trigStr, 63);

	dev_info(dev, "%s flag[%d],str[%s]\n", __func__, dump_flag, triggerStr);

	if (dump_flag == 1)
		return;

	dump_flag = 1;

	if (gnss_common_ctl_dev.gnss_status == GNSS_STATUS_POWERON) {
		wcn_assert_interface(WCN_SOURCE_GNSS, triggerStr);
		gnss_common_ctl_dev.gnss_status = GNSS_STATUS_ASSERT;
	}
	dump_flag = 0;

}

static void gnss_reset_ctrl_co(char *trigStr)
{
	char flag = 0; /* 0: default, all, 1: only data, pmu, aon */
	unsigned int temp_status = 0;
	static char reset_flag;
	char triggerStr[64];
	struct device *dev = gnss_common_ctl_dev.dev;

	memset(triggerStr, 0, 64);
	strncpy(triggerStr, trigStr, 63);

	dev_info(dev, "%s flag[%d],str[%s]\n", __func__, reset_flag, triggerStr);

	if (reset_flag == 1)
		return;

	reset_flag = 1;

	temp_status = gnss_common_ctl_dev.gnss_status;
	if ((temp_status == GNSS_STATUS_POWERON_GOING) ||
		((temp_status == GNSS_STATUS_POWERON) &&
		(gnss_status_get() != GNSS_CP_STATUS_SLEEP))) {
		flag = (temp_status == GNSS_STATUS_POWERON) ? 0 : 1;
		mdbg_wcn_source_ctl(WCN_SOURCE_GNSS, triggerStr);
		gnss_common_ctl_dev.gnss_status = GNSS_STATUS_ASSERT;
	}
	reset_flag = 0;
}

static void gnss_reset_ctrl(char *trigStr)
{
	static char reset_flag;
	char triggerStr[64];
	struct device *dev = gnss_common_ctl_dev.dev;

	memset(triggerStr, 0, 64);
	strncpy(triggerStr, trigStr, 63);

	dev_info(dev, "%s flag[%d],str[%s]\n", __func__, reset_flag, triggerStr);

	if (reset_flag == 1)
		return;

	reset_flag = 1;

	if (gnss_common_ctl_dev.gnss_status == GNSS_STATUS_POWERON) {
		mdbg_wcn_source_ctl(WCN_SOURCE_GNSS, triggerStr);
		gnss_common_ctl_dev.gnss_status = GNSS_STATUS_ASSERT;
	}
	reset_flag = 0;
}

static void gnss_chr_report(enum wcn_chr_loglevel loglevel,
						uint32_t event_id, void *params, uint8_t len)
{
	sprdwcn_bus_chr_report(WCN_SOURCE_GNSS, loglevel, event_id, params, len);
}

static void gnss_exception_chr_report(const char *exception)
{
	int evt_num = ARRAY_SIZE(gnss_exception_chr_event_table);
	int i;

	if (exception == NULL)
		return;

	for (i = 0; i < evt_num; i++) {
		if (gnss_exception_chr_event_table[i].namestr == NULL)
			continue;

		if (strcmp(exception, gnss_exception_chr_event_table[i].namestr) == 0) {
			gnss_chr_report(gnss_exception_chr_event_table[i].loglevel,
					gnss_exception_chr_event_table[i].event_id, NULL, 0);
		}
	}
}

static void gnss_dump_proc(const char *buf, size_t count)
{
	struct wcn_match_data *ptr_match_config = get_wcn_match_config();
	char triggerStr[64] = {0};
	struct device *dev = gnss_common_ctl_dev.dev;

	if ((count - 2) > sizeof(triggerStr))
		return;

	memset(triggerStr, 0, sizeof(triggerStr));
	memcpy(triggerStr, buf+2, count-2);

	dev_info(dev, "%s triggerStr=%s\n", __func__, triggerStr);

	gnss_exception_chr_report(triggerStr);
	if (ptr_match_config && ptr_match_config->unisoc_wcn_integrated)
		gnss_dump_mem_ctrl_co(triggerStr);
	else
		gnss_dump_mem_ctrl(triggerStr);
}

static void gnss_reset_proc(const char *buf, size_t count)
{
	struct wcn_match_data *ptr_match_config = get_wcn_match_config();
	char triggerStr[64] = {0};
	struct device *dev = gnss_common_ctl_dev.dev;

	if ((count - 2) > sizeof(triggerStr))
		return;

	memset(triggerStr, 0, sizeof(triggerStr));
	memcpy(triggerStr, buf+2, count-2);

	dev_info(dev, "%s triggerStr=%s\n", __func__, triggerStr);

	//gnss_exception_chr_report(triggerStr);
	if (ptr_match_config && ptr_match_config->unisoc_wcn_integrated)
		gnss_reset_ctrl_co(triggerStr);
	else
		gnss_reset_ctrl(triggerStr);
}

static ssize_t gnss_dump_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long set_value;
	int ret = -1;
	int strlen = 0;
	char triggerStr[64];
	int subsys_status = 0;
	struct wcn_match_data *ptr_match_config = get_wcn_match_config();

	set_value = buf[0] - '0';

	dev_info(dev, "%s V2.0 val[%lu]\n", __func__, set_value);

	memset(triggerStr, 0, 64);
	strlen = ((count - 2) > 63) ? 63 : (count - 2);
	memcpy(triggerStr, &buf[2], strlen);

	if (ptr_match_config && ptr_match_config->unisoc_wcn_integrated) {
		subsys_status  = wcn_check_module_status(WCN_GNSS);
		subsys_status |= wcn_check_module_status(WCN_GNSS_BD);
		subsys_status |= wcn_check_module_status(WCN_GNSS_GAL);
	} else {
		subsys_status = wcn_check_module_status(MARLIN_GNSS);
	}

	dev_info(dev, "%s subsys status[%d], trigStr=%s\n", __func__, subsys_status, triggerStr);

	if (count < 2)
		return ret;

	if (set_value == 1) {
		gnss_dump_proc(buf, count);
		ret = GNSS_DUMP_DATA_START_UP;
	} else if((set_value == 4) && subsys_status && !sprdwcn_bus_get_carddump_status()) {
		wcn_assert_interface(WCN_SOURCE_SP_RESET, triggerStr);
		ret = GNSS_DUMP_DATA_START_UP;
	} else if((set_value == 5) && subsys_status && !sprdwcn_bus_get_carddump_status()) {
		gnss_hold_cpu();
		ret = 0;
	} else if (set_value == 2) {
		gnss_reset_proc(buf, count);
		ret = GNSS_DUMP_DATA_START_UP;
	} else
		ret = -EINVAL;

	return ret;
}

static DEVICE_ATTR_WO(gnss_dump);

static ssize_t gnss_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int i = 0;
	unsigned int status = gnss_common_ctl_dev.gnss_status;

	dev_info(dev, "%s status[%d]\n", __func__, status);
	i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", status);

	return i;
}
static DEVICE_ATTR_RO(gnss_status);

static ssize_t gnss_clktype_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int i = 0;
	enum wcn_clock_type clktype = WCN_CLOCK_TYPE_UNKNOWN;

	clktype = wcn_get_xtal_26m_clk_type();
	dev_info(dev, "%s clktyp[%d]\n", __func__, clktype);
	i = scnprintf(buf, PAGE_SIZE, "%d\n", clktype);

	return i;
}
static DEVICE_ATTR_RO(gnss_clktype);

static ssize_t gnss_pmic_chipid_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	int i = 0;
	static struct device_node *regmap_np;

	if (wcn_get_xtal_26m_clk_type() != WCN_CLOCK_TYPE_TSX)
		return -EINVAL;

	regmap_np = of_find_matching_node(NULL, pmic_of_match);
	if (!regmap_np) {
		dev_err(dev, "%s, error np\n", __func__);
		return -EINVAL;
	}

	if (of_device_is_compatible(regmap_np, "sprd,sc27xx-syscon"))
		i = scnprintf(buf, PAGE_SIZE, "%x\n", PMIC_CHIPID_SC27XX);
	else if (of_device_is_compatible(regmap_np, "sprd,ump9622-syscon"))
		i = scnprintf(buf, PAGE_SIZE, "%x\n", PMIC_CHIPID_UMP9622);
	else if (of_device_is_compatible(regmap_np, "sprd,ump9652-syscon"))
		i = scnprintf(buf, PAGE_SIZE, "%x\n", PMIC_CHIPID_UMP9652);
	else
		return -EINVAL;

	return i;
}
static DEVICE_ATTR_RO(gnss_pmic_chipid);

static unsigned int gnss_op_reg;
static unsigned int gnss_indirect_reg_offset;
static ssize_t gnss_regr_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	unsigned int op_reg = gnss_op_reg;
	unsigned int buffer = 0;
	int ret = 0;

	dev_info(dev, "%s, register is 0x%x\n", __func__, gnss_op_reg);
	if (op_reg == GNSS_INDIRECT_OP_REG) {
		int set_value;

		set_value = gnss_indirect_reg_offset + 0x80000000;
		ret = sprdwcn_bus_reg_write(op_reg, &set_value, 4);
		if (ret < 0) {
			dev_err(dev, "%s, bus reg 0x%x write error\n", __func__, op_reg);
			return ret;
		}
	}
	ret = sprdwcn_bus_reg_read(op_reg, &buffer, 4);
	if (ret < 0) {
		dev_err(dev, "%s, bus reg 0x%x read error\n", __func__, op_reg);
		return ret;
	}
	dev_info(dev, "%s,temp value is %d\n", __func__, buffer);

	ret = scnprintf((char *)buf, PAGE_SIZE, "show: 0x%x\n", buffer);

	return ret;
}

static DEVICE_ATTR_RO(gnss_regr);

static ssize_t gnss_regaddr_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long set_addr;

	if (kstrtoul(buf, GNSS_DATA_BASE_TYPE_H, &set_addr)) {
		dev_err(dev, "%s, input error\n", __func__);
		return -EINVAL;
	}
	dev_info(dev, "%s,0x%lx\n", __func__, set_addr);
	gnss_op_reg = (unsigned int)set_addr;

	return count;
}
static DEVICE_ATTR_WO(gnss_regaddr);

static ssize_t gnss_regspaddr_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned long set_addr;

	if (kstrtoul(buf, GNSS_DATA_BASE_TYPE_H, &set_addr)) {
		dev_err(dev, "%s, input error\n", __func__);
		return -EINVAL;
	}
	dev_info(dev, "%s,0x%lx\n", __func__, set_addr);
	gnss_op_reg = GNSS_INDIRECT_OP_REG;
	gnss_indirect_reg_offset = (unsigned int)set_addr;
	return count;
}
static DEVICE_ATTR_WO(gnss_regspaddr);

static ssize_t gnss_regw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long set_value;
	unsigned int op_reg = gnss_op_reg;
	int ret = 0;

	if (kstrtoul(buf, GNSS_DATA_BASE_TYPE_H, &set_value)) {
		dev_err(dev, "%s, input error\n", __func__);
		return -EINVAL;
	}
	if (op_reg == GNSS_INDIRECT_OP_REG)
		set_value = gnss_indirect_reg_offset + set_value;
	dev_info(dev, "%s,0x%lx\n", __func__, set_value);
	ret = sprdwcn_bus_reg_write(op_reg, &set_value, 4);
	if (ret < 0) {
		dev_err(dev, "%s, bus reg 0x%x write error\n", __func__, op_reg);
		return ret;
	}
	return count;
}
static DEVICE_ATTR_WO(gnss_regw);

bool gnss_delay_ctl(void)
{
	return (gnss_common_ctl_dev.gnss_status == GNSS_STATUS_POWERON);
}
EXPORT_SYMBOL_GPL(gnss_delay_ctl);

static struct miscdevice gnss_common_ctl_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gnss_common_ctl",
	.fops = NULL,
};

static int gnss_reset(struct notifier_block *this, unsigned long ev, void *ptr)
{
	struct device *dev = gnss_common_ctl_dev.dev;

 	char *envp[3] = {
 		[0] = "SOURCE=unisocgnss",
 		[1] = "EVENT=FW_ERROR",
 		[2] = NULL,
 	};

	dev_info(dev, "%s: reset callback coming\n", __func__);
	kobject_uevent_env(&gnss_common_ctl_miscdev.this_device->kobj, KOBJ_CHANGE, envp);

	return NOTIFY_OK;
}

static struct notifier_block gnss_reset_block = {
	.notifier_call = gnss_reset,
};

static ssize_t gnss_debug_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long set_value;
	ssize_t ret = count;
	char *envp[3] = {
				[0] = "SOURCE=unisocgnss",
				[1] = "EVENT=GNSS_ERROR",
				[2] = NULL,
	};

	if (kstrtoul(buf, GNSS_MAX_STRING_LEN, &set_value)) {
		dev_err(dev, "%s Maybe store string is too long\n", __func__);
		return -EINVAL;
	}
	dev_info(dev, "%s set_value[%lu]\n", __func__, set_value);

	if (set_value == 1) {
		kobject_uevent_env(&gnss_common_ctl_miscdev.this_device->kobj, KOBJ_CHANGE, envp);
	} else if ((set_value == 2) && isQogirl6()) {
		gnss_clear_outbox_fifo();
	} else {
		ret = -EINVAL;
		dev_info(dev, "%s unknown control\n", __func__);
	}

	return ret;
}
static DEVICE_ATTR_WO(gnss_debug);

static struct attribute *gnss_common_ctl_attrs[] = {
	&dev_attr_gnss_power_enable.attr,
	&dev_attr_gnss_dump.attr,
	&dev_attr_gnss_status.attr,
	&dev_attr_gnss_subsys.attr,
	&dev_attr_gnss_clktype.attr,
	&dev_attr_gnss_pmic_chipid.attr,
	&dev_attr_gnss_regr.attr,
	&dev_attr_gnss_regaddr.attr,
	&dev_attr_gnss_regspaddr.attr,
	&dev_attr_gnss_regw.attr,
	&dev_attr_gnss_debug.attr,
	NULL,
};

static struct attribute_group gnss_common_ctl_group = {
	.name = NULL,
	.attrs = gnss_common_ctl_attrs,
};

static int gnss_common_ctl_probe(struct platform_device *pdev)
{
	int ret;
	const struct of_device_id *of_id;
	struct wcn_match_data *ptr_match_config = get_wcn_match_config();

	dev_info(&pdev->dev, "%s V3.2 entry\n", __func__);
	gnss_common_ctl_dev.dev = &pdev->dev;
	gnss_common_ctl_dev.board_platform = 0;
	gnss_common_ctl_dev.gnss_status = GNSS_STATUS_POWEROFF;
	memset(gnss_common_ctl_dev.firmware_path, 0x0,
	       FIRMWARE_FILEPATHNAME_LENGTH_MAX);
	if (ptr_match_config && ptr_match_config->unisoc_wcn_integrated)
		gnss_common_ctl_dev.gnss_subsys = WCN_GNSS;
	else
		gnss_common_ctl_dev.gnss_subsys = MARLIN_GNSS;
	/* considering backward compatibility, it's not use now  start */
	of_id = of_match_node(gnss_common_ctl_of_match,
		pdev->dev.of_node);
	if (!of_id) {
		dev_err(&pdev->dev,
			"get gnss_common_ctl of device id failed!\n");
		return -ENODEV;
	}
	gnss_common_ctl_dev.chip_ver = (unsigned long)(of_id->data);
	/* considering backward compatibility, it's not use now  end */

	platform_set_drvdata(pdev, &gnss_common_ctl_dev);
	ret = misc_register(&gnss_common_ctl_miscdev);
	if (ret) {
		dev_err(&pdev->dev, "%s failed to register\n",
			__func__);
		return ret;
	}

	ret = sysfs_create_group(&gnss_common_ctl_miscdev.this_device->kobj,
			&gnss_common_ctl_group);
	if (ret) {
		dev_err(&pdev->dev, "%s failed to create attributes\n",
			__func__);
		goto err_attr_failed;
	}
	atomic_notifier_chain_register(&wcn_reset_notifier_list,&gnss_reset_block);
	gnss_common_ctl_dev.pctrl = devm_pinctrl_get(gnss_common_ctl_dev.dev);

#ifdef GNSS_SINGLE_MODULE
	dev_err(&pdev->dev, "%s single ko\n", __func__);

	ret = gnss_pnt_ctl_init();
	if (ret != 0)
		dev_err(&pdev->dev, "%s gnss_pnt_ctl_init[%d]\n",
			__func__, ret);

	ret = gnss_gdb_init();
	if (ret != 0)
		dev_err(&pdev->dev, "%s gnss_gdb_init[%d]\n",
			__func__, ret);
#else
	dev_err(&pdev->dev, "%s multiple  ko\n", __func__);
#endif

	return 0;

err_attr_failed:
	misc_deregister(&gnss_common_ctl_miscdev);
	return ret;
}

static int gnss_common_ctl_remove(struct platform_device *pdev)
{
	atomic_notifier_chain_unregister(&wcn_reset_notifier_list, &gnss_reset_block);
	sysfs_remove_group(&gnss_common_ctl_miscdev.this_device->kobj,
				&gnss_common_ctl_group);

	misc_deregister(&gnss_common_ctl_miscdev);
#ifdef GNSS_SINGLE_MODULE
	gnss_pnt_ctl_cleanup();
	gnss_gdb_exit();
#endif
	return 0;
}
static struct platform_driver gnss_common_ctl_drv = {
	.driver = {
		   .name = "gnss_common_ctl",
		   .of_match_table = of_match_ptr(gnss_common_ctl_of_match),
		   },
	.probe = gnss_common_ctl_probe,
	.remove = gnss_common_ctl_remove
};

static int __init gnss_common_ctl_init(void)
{
	return platform_driver_register(&gnss_common_ctl_drv);
}

static void __exit gnss_common_ctl_exit(void)
{
	platform_driver_unregister(&gnss_common_ctl_drv);
}

module_init(gnss_common_ctl_init);
module_exit(gnss_common_ctl_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Spreadtrum Gnss Driver");
MODULE_AUTHOR("Jun.an<jun.an@spreadtrum.com>");
