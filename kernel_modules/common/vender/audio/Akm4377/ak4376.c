/*
 * ak4376.c  --  audio driver for AK4376A and AK4377A
 *
 * Copyright (C) 2017 Asahi Kasei Microdevices Corporation
 *  Author                Date        Revision      DS ver.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  K.Tsubota           15/06/12	    1.0           00
 *  K.Tsubota           16/06/17	    1.1           00
 *  K.Tsubota           16/10/31	    1.2           01
 *  K.Tsubota           16/11/24	    1.3           01
 *  K.Tsubota           17/08/09	    1.4           01
 *  J.Wakasugi          18/05/30        2.0      AK4376A 00  // for AK4376A/AK4377A & 4.9.XX 
 *                                               AK4377A 01
 *  J.Wakasugi          18/06/06        2.1
 *  J.Wakasugi          18/06/06        2.11
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  kernel version: 4.9
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/regulator/consumer.h>

#include "ak4376.h"

//#define KERNEL_4_9_XX

#define AK4376_DEBUG				//used at debug mode
#define CONFIG_DEBUG_FS_CODEC		//used at debug mode

#ifdef AK4376_DEBUG
#define akdbgprt printk
//#define akdbgprt pr_info
#else
#define akdbgprt(format, arg...) do {} while (0)
#endif

/* AK4376 Codec Private Data */
struct ak4376_priv {
	struct mutex mutex;
	int priv_pdn_en;			//PDN GPIO pin
	int pdn1;							//PDN control, 0:Off, 1:On, 2:No use(assume always On)
	int on;							//PDN control for kcontrol
	int pdn_cnt;
	int pdn_set;
	int fs1;
	int rclk;							//Master Clock
	int nBickFreq;						//0:32fs, 1:48fs, 2:64fs
	int nPllMode;						//1slave 2master
	int nPllMCKI;						//0:9.6MHz, 1:11.2896MHz, 2:12.288MHz, 3:19.2MHz
	int deviceID;						//0x21:AK4376A, 0x22:AK4377A
	int lpmode;							//0:High Performance, 1:Low power mode
	int xtalfreq;						//0:12.288MHz, 1:11.2896MHz
	int nDACOn;
	int trigger_cnt;
	struct i2c_client *i2c;
	struct regmap *regmap;
	struct regulator *ldo1_regu;
	u32 ldo1_vol;
	struct clk *clk_48m;
	struct clk *clk_parent;
	struct clk *clk_enable;

};


static struct reg_default ak4376a_patch[] = {
	{ 0x0B, 0x99 },      /* 0x0B AK4376A_0B_LCH_OUTPUT_VOLUME */
	{ 0x26, 0x20 },      /* 0x26  AK4376A_26_DAC32_DEBUG2		*/
	{ 0x2A, 0x07 },      /* 0x2A  AK4376A_2A_LDO3_DEBUG1		*/
};

static struct reg_default ak4377a_patch[] = { 
	{ 0x0B, 0x99 },      /* 0x0B  AK4377A_0B_LCH_OUTPUT_VOLUME */
	{ 0x25, 0x69 },      /* 0x25  AK4377A_25_DAC32_DEBUG2		*/
	{ 0x26, 0x70 },      /* 0x26  AK4377A_26_DAC32_DEBUG2		*/
    { 0x2A, 0x07 },      /* 0x2A  AK4377A_2A_LDO3_DEBUG1		*/
};

static struct reg_default *ak437x_patch;
static int ak437x_ptsize = 0;
static bool exist_chip;

/* ak4376 register cache & default register settings */
static const struct reg_default ak4376_reg[] = {
	{ 0x0, 0x00 },	/*	0x00	AK4376_00_POWER_MANAGEMENT1		*/
	{ 0x1, 0x00 },	/*	0x01	AK4376_01_POWER_MANAGEMENT2		*/
	{ 0x2, 0x00 },	/*	0x02	AK4376_02_POWER_MANAGEMENT3		*/
	{ 0x3, 0x00 },	/*	0x03	AK4376_03_POWER_MANAGEMENT4		*/
	{ 0x4, 0x00 },	/*	0x04	AK4376_04_OUTPUT_MODE_SETTING	*/
	{ 0x5, 0x00 },	/*	0x05	AK4376_05_CLOCK_MODE_SELECT		*/
	{ 0x6, 0x00 },	/*	0x06	AK4376_06_DIGITAL_FILTER_SELECT	*/
	{ 0x7, 0x00 },	/*	0x07	AK4376_07_DAC_MONO_MIXING		*/
	{ 0x8, 0x00 },	/*	0x08	AK4376_08_RESERVED				*/
	{ 0x9, 0x00 },	/*	0x09	AK4376_09_RESERVED				*/
	{ 0xA, 0x00 },	/*	0x0A	AK4376_0A_RESERVED				*/
	{ 0xB, 0x19 },	/*	0x0B	AK4376_0B_LCH_OUTPUT_VOLUME		*/
	{ 0xC, 0x19 },	/*	0x0C	AK4376_0C_RCH_OUTPUT_VOLUME		*/
	{ 0xD, 0x0B },	/*	0x0D	AK4376_0D_HP_VOLUME_CONTROL		*/
	{ 0xE, 0x00 },	/*	0x0E	AK4376_0E_PLL_CLK_SOURCE_SELECT	*/
	{ 0xF, 0x00 },	/*	0x0F	AK4376_0F_PLL_REF_CLK_DIVIDER1	*/
	{ 0x10, 0x00 },	/*	0x10	AK4376_10_PLL_REF_CLK_DIVIDER2	*/
	//{ 0x10, 0x18 },	/*	0x10	AK4376_10_PLL_REF_CLK_DIVIDER2	*///yht D == 24, D + 1 == 25
	{ 0x11, 0x00 },	/*	0x11	AK4376_11_PLL_FB_CLK_DIVIDER1	*/
	{ 0x12, 0x00 },	/*	0x12	AK4376_12_PLL_FB_CLK_DIVIDER2	*/
//	{ 0x12, 0xff },	/*	0x12	AK4376_12_PLL_FB_CLK_DIVIDER2	*///yht M == 255, M + 1 == 256
	{ 0x13, 0x00 },	/*	0x13	AK4376_13_DAC_CLK_SOURCE		*/
	{ 0x14, 0x00 },	/*	0x14	AK4376_14_DAC_CLK_DIVIDER		*/
	{ 0x15, 0x40 },	/*	0x15	AK4376_15_AUDIO_IF_FORMAT		*/
	{ 0x21, 0x00 },	/*	0x21	AK4376_21_CHIPID                */
	{ 0x24, 0x00 },	/*	0x24	AK4376_24_MODE_CONTROL			*/
};

static unsigned int ak4376_reg_read(struct snd_soc_component *codec, unsigned int reg);
static bool ak4376_readable(struct device *dev, unsigned int reg);
static int ak4376_i2c_write(struct snd_soc_component *codec, unsigned int reg, unsigned int value);

int read_component(struct snd_soc_component *component, int reg)
{
#if defined ( CONFIG_SND_SOC_AKM4377_K54 )
	return snd_soc_component_read32(component, reg);
#else
	return snd_soc_component_read(component, reg);
#endif
}

static int ak4376_update_register(struct snd_soc_component *codec)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	int i;

	if ( ak4376->deviceID == -1 )
		return 0;

	for(i=0; i< ak437x_ptsize; i++){
		ak4376_i2c_write(codec,ak437x_patch[i].reg,ak437x_patch[i].def);
	}

	return 0;
}

static int ak4376_vddldo1_en(struct snd_soc_component *codec, bool on)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	struct regulator *regu = ak4376->ldo1_regu;
	int ret;

	/*
	if (!regu) {
		pr_info("typec switch supply is NULL.\n");
		return 0;
	}
	*/

	akdbgprt("%s %d on=%d\n", __func__, __LINE__, on);
	if (!on)
		return regulator_disable(regu);

	ret = regulator_set_voltage(regu, ak4376->ldo1_vol, ak4376->ldo1_vol);
	if (ret < 0) {
		pr_err("failed(%d) to set vddldo1 voltage at %duV\n",
			   ret, ak4376->ldo1_vol);
		return ret;
	}

	return regulator_enable(regu);
}

static int ak4376_clk_enable(struct ak4376_priv *ak4376, bool enable)
{
	int ret;

	if (enable) {
		//clk_set_rate(ak4376->clk_48m, 12000000);//call it before calling clk_prepare_enable()
		ret = clk_prepare_enable(ak4376->clk_48m);
		ret = clk_prepare_enable(ak4376->clk_enable);
		pr_info("%s ret %d\n", __func__, ret);
	} else {
		clk_disable_unprepare(ak4376->clk_enable);
		clk_disable_unprepare(ak4376->clk_48m);
		ret = 0;
	}

	return ret;
}

static int ak4376_pdn_control(struct snd_soc_component *codec, int pdn)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	int ret;

	akdbgprt("%s pdn1 %d, pdn = %d, pdn_cnt = %d\n",__func__, ak4376->pdn1, pdn, ak4376->pdn_cnt);

	if ((ak4376->pdn1 == 0) && (pdn == 1)) {
		if(ak4376->pdn_cnt == 0){
			ret = ak4376_vddldo1_en(codec, true);
			if (ret) {
				pr_err("vddldo1 power on fail %d\n", ret);
				return ret;
			}
			gpio_direction_output(ak4376->priv_pdn_en, 1);
			pr_info("%s Turn on priv_pdn_en\n", __func__);
			ak4376->pdn1 = 1;
			ak4376_clk_enable(ak4376, true);
			udelay(800);

			regcache_sync(ak4376->regmap);
			ak4376_update_register(codec);
		}

	} else if ((ak4376->pdn1 == 1) && (pdn == 0)) {
		if(ak4376->pdn_cnt == 1){
			i2c_lock_bus(ak4376->i2c->adapter, I2C_LOCK_ROOT_ADAPTER);
			ak4376_clk_enable(ak4376, false);
			gpio_direction_output(ak4376->priv_pdn_en, 0);
			pr_info("%s Turn off priv_pdn_en\n", __func__);
			ak4376->pdn1 = 0;
			udelay(800);

			i2c_unlock_bus(ak4376->i2c->adapter, I2C_LOCK_ROOT_ADAPTER);
			regcache_mark_dirty(ak4376->regmap);
			ret = ak4376_vddldo1_en(codec, false);
			if (ret) {
				pr_err("vddldo1 power down fail %d\n", ret);
				return ret;
			}
		}
	}

	if(pdn){
		ak4376->pdn_cnt++;
	} else {
		if(ak4376->pdn_cnt > 0)
			ak4376->pdn_cnt--;
	}
	akdbgprt("%s After pdn1(pdn_status) %d, pdn = %d, pdn_cnt = %d\n",__func__, ak4376->pdn1, pdn, ak4376->pdn_cnt);

	return 0;
}

#ifdef CONFIG_DEBUG_FS_CODEC
static struct mutex io_lock;
static struct snd_soc_component *ak4376_codec;
static unsigned int ak4376_i2c_read(struct snd_soc_component *codec, unsigned int reg);
static bool ak4376_writeable(struct device *dev, unsigned int reg);

static ssize_t reg_data_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret, i, fpt, pdn;
	int n;
	int rx[30];

	pdn = ak4376_pdn_control(ak4376_codec, 1);
	if(pdn){
		pr_err("%s set pdn failed!", __func__);
		return pdn;
	}

	n = ARRAY_SIZE(ak4376_reg);
	mutex_lock(&io_lock);
	for (i = 0; i < n; i++) {
		ret = ak4376_reg_read(ak4376_codec, ak4376_reg[i].reg);
		if (ret < 0) {
			pr_err("%s: read register error.\n", __func__);
			break;

		} else {
			rx[i] = ret;
		}
	}

	if ( ret >= 0 ) {
		for(i=0; i< ak437x_ptsize; i++){
			ret = ak4376_i2c_read(ak4376_codec, ak437x_patch[i].reg);
			if (ret < 0) {
				pr_err("%s: read register error.\n", __func__);
				break;

			} else {
				rx[n++] = ret;
			}
		}
	}
	mutex_unlock(&io_lock);

	pdn = ak4376_pdn_control(ak4376_codec, 0);
	if(pdn){
		pr_err("%s drop pdn failed!", __func__);
		return pdn;
	}

	if ( ret >= 0 ) {
		ret = fpt = 0;

		n = ARRAY_SIZE(ak4376_reg);
		for (i = 0; i < n; i++, fpt += 6) {
			ret += sprintf(buf + fpt, "%02x,%02x\n", ak4376_reg[i].reg, rx[i]);
		}
		for(i=0; i< ak437x_ptsize; i++, fpt += 6){
			ret += sprintf(buf + fpt, "%02x,%02x\n", ak437x_patch[i].reg, rx[n++]);
		};
		return ret;

	} else {

		return sprintf(buf, "read error");
	}

}

static ssize_t reg_data_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	char	*ptr_data = (char *)buf;
	char	*p;
	int	i, pt_count = 0;
	int j, pdn;
	u16 val[20];

	akdbgprt("%s %d enter\n",__func__,__LINE__);
	pdn = ak4376_pdn_control(ak4376_codec, 1);
	if(pdn){
		pr_err("%s set pdn failed!", __func__);
		return pdn;
	}

	while ((p = strsep(&ptr_data, ","))) {
		if (!*p)
			break;

		if (pt_count >= 20)
			break;

		val[pt_count] = simple_strtoul(p, NULL, 16);

		pt_count++;
	}
	mutex_lock(&io_lock);

	for (i = 0; i < pt_count; i+=2) {
		if ( ak4376_writeable(NULL, (unsigned int)val[i]) ) {
			snd_soc_component_write(ak4376_codec, (unsigned int)val[i], (unsigned int)val[i+1]);
			pr_info("%s: write add=%d, val=%d.\n", __func__, val[i], val[i+1]);
		} else {
			for ( j = 0 ; j < ak437x_ptsize ; j ++ ){
				if ( ak437x_patch[j].reg == val[i] ) {
					ak437x_patch[j].def = val[i+1];
					ak4376_i2c_write(ak4376_codec,ak437x_patch[j].reg,ak437x_patch[j].def);
				}
			}
		}
	}
	mutex_unlock(&io_lock);

	pdn = ak4376_pdn_control(ak4376_codec, 0);
	if(pdn){
		pr_err("%s drop pdn failed!", __func__);
		return pdn;
	}

	return count;
}

static DEVICE_ATTR(reg_data, 0644, reg_data_show, reg_data_store);

#endif

/* Output Digital volume control:
 * from -12.5 to 3 dB in 0.5 dB steps (mute instead of -12.5 dB) */
static DECLARE_TLV_DB_SCALE(ovl_tlv, -1250, 50, 0);
static DECLARE_TLV_DB_SCALE(ovr_tlv, -1250, 50, 0);

/* HP-Amp Analog volume control:
 * from -22 to 6 dB in 2 dB steps (mute instead of -42 dB) */
static DECLARE_TLV_DB_SCALE(hpg_tlv, -2200, 200, 0);

static const char *ak4376_ovolcn_select_texts[] = {"Dependent", "Independent"};
static const char *ak4376_mdacl_select_texts[] = {"x1", "x1/2"};
static const char *ak4376_mdacr_select_texts[] = {"x1", "x1/2"};
static const char *ak4376_invl_select_texts[] = {"Normal", "Inverting"};
static const char *ak4376_invr_select_texts[] = {"Normal", "Inverting"};
static const char *ak4376_cpmod_select_texts[] =
		{"Automatic Switching", "+-VDD Operation", "+-1/2VDD Operation"};
static const char *ak4376_hphl_select_texts[] = {"9ohm", "Hi-Z"};
static const char *ak4376_hphr_select_texts[] = {"9ohm", "Hi-Z"};
static const char *ak4376_dacfil_select_texts[]  =
		{"Sharp Roll-Off", "Slow Roll-Off", "Short Delay Sharp Roll-Off", "Short Delay Slow Roll-Off"};
static const char *ak4376_bcko_select_texts[] = {"64fs", "32fs"};
static const char *ak4376_dfthr_select_texts[] = {"Digital Filter", "Bypass"};
static const char *ak4376_ngate_select_texts[] = {"On", "Off"};
static const char *ak4376_ngatet_select_texts[] = {"Short", "Long"};

static const struct soc_enum ak4376_dac_enum[] = {
	SOC_ENUM_SINGLE(AK4376_0B_LCH_OUTPUT_VOLUME, 7,
			ARRAY_SIZE(ak4376_ovolcn_select_texts), ak4376_ovolcn_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 2,
			ARRAY_SIZE(ak4376_mdacl_select_texts), ak4376_mdacl_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 6,
			ARRAY_SIZE(ak4376_mdacr_select_texts), ak4376_mdacr_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 3,
			ARRAY_SIZE(ak4376_invl_select_texts), ak4376_invl_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 7,
			ARRAY_SIZE(ak4376_invr_select_texts), ak4376_invr_select_texts),
	SOC_ENUM_SINGLE(AK4376_03_POWER_MANAGEMENT4, 2,
			ARRAY_SIZE(ak4376_cpmod_select_texts), ak4376_cpmod_select_texts),
	SOC_ENUM_SINGLE(AK4376_04_OUTPUT_MODE_SETTING, 0,
			ARRAY_SIZE(ak4376_hphl_select_texts), ak4376_hphl_select_texts),
	SOC_ENUM_SINGLE(AK4376_04_OUTPUT_MODE_SETTING, 1,
			ARRAY_SIZE(ak4376_hphr_select_texts), ak4376_hphr_select_texts),
    SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 6,
    		ARRAY_SIZE(ak4376_dacfil_select_texts), ak4376_dacfil_select_texts), 
	SOC_ENUM_SINGLE(AK4376_15_AUDIO_IF_FORMAT, 3,
			ARRAY_SIZE(ak4376_bcko_select_texts), ak4376_bcko_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 3,
			ARRAY_SIZE(ak4376_dfthr_select_texts), ak4376_dfthr_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 0,
			ARRAY_SIZE(ak4376_ngate_select_texts), ak4376_ngate_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 1,
			ARRAY_SIZE(ak4376_ngatet_select_texts), ak4376_ngatet_select_texts),
};

static const char *bickfreq_on_select[] = {"32fs", "48fs", "64fs"};
static const char *pllmcki_on_select[] = {"9.6MHz", "11.2896MHz", "12.288MHz", "19.2MHz"};
static const char *lpmode_on_select[] = {"High Performance", "Low Power"};
static const char *xtalfreq_on_select[] = {"12.288MHz", "11.2896MHz"};
static const char *pdn_on_select[] = {"Off", "On"};
static const char *pllmode_on_select[] = {"PLL_OFF", "PLL_BICK_MODE", "PLL_MCKI_MODE", "XTAL_MODE"};

static const struct soc_enum ak4376_bitset_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(bickfreq_on_select), bickfreq_on_select), 
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pllmcki_on_select), pllmcki_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(lpmode_on_select), lpmode_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(xtalfreq_on_select), xtalfreq_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pdn_on_select), pdn_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pllmode_on_select), pllmode_on_select),
};

static int get_bickfs(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("ak4376 %s %d\n",__func__,__LINE__);

    ucontrol->value.enumerated.item[0] = ak4376->nBickFreq;

    return 0;
}

static int ak4376_set_bickfs(struct snd_soc_component *codec)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	pr_info("%s nBickFreq=%d\n",__func__,ak4376->nBickFreq);

	if ( ak4376->nBickFreq == 0 ) { 	//32fs
		snd_soc_component_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x01);	//DL1-0=01(16bit, >=32fs)
	}
	else if( ak4376->nBickFreq == 1 ) {	//48fs
		snd_soc_component_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x00);	//DL1-0=00(24bit, >=48fs)
	}
	else {								//64fs
		snd_soc_component_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x02);	//DL1-0=1x(32bit, >=64fs)
	}
	
	return 0;
}

static int set_bickfs(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("ak4376 %s %d\n",__func__,__LINE__);
    
	ak4376->nBickFreq = ucontrol->value.enumerated.item[0];

	ak4376_set_bickfs(codec);
	
    return 0;
}

static int get_pllmcki(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("ak4376 %s %d\n",__func__,__LINE__);

    ucontrol->value.enumerated.item[0] = ak4376->nPllMCKI;

    return 0;
}

static int set_pllmcki(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	ak4376->nPllMCKI = ucontrol->value.enumerated.item[0];
	pr_info("ak4376 %s  nPllMCKI %d\n",__func__, ak4376->nPllMCKI);

    return 0;
}

static int get_lpmode(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("ak4376 %s %d\n",__func__,__LINE__);
    ucontrol->value.enumerated.item[0] = ak4376->lpmode;

    return 0;
}

static int ak4376_set_lpmode(struct snd_soc_component *codec)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("ak4376 %s %d, lpmode %d, fs1 %d\n",__func__,__LINE__, ak4376->lpmode, ak4376->fs1);
	if ( ak4376->lpmode == 0 ) { 	//High Performance Mode
		snd_soc_component_update_bits(codec, AK4376_02_POWER_MANAGEMENT3, 0x10, 0x00);	//LPMODE=0(High Performance Mode)
			if ( ak4376->fs1 <= 12000 ) {
				snd_soc_component_update_bits(codec, AK4376_24_MODE_CONTROL, 0x40, 0x40);	//DSMLP=1
			}
			else {
				snd_soc_component_update_bits(codec, AK4376_24_MODE_CONTROL, 0x40, 0x00);	//DSMLP=0
			}
	}
	else {							//Low Power Mode
		snd_soc_component_update_bits(codec, AK4376_02_POWER_MANAGEMENT3, 0x10, 0x10);	//LPMODE=1(Low Power Mode)
		snd_soc_component_update_bits(codec, AK4376_24_MODE_CONTROL, 0x40, 0x40);			//DSMLP=1
	}
	
	return 0;
}

static int set_lpmode(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
    
	ak4376->lpmode = ucontrol->value.enumerated.item[0];
	akdbgprt("ak4376 %s %d, lpmode %d\n",__func__,__LINE__, ak4376->lpmode);

	ak4376_set_lpmode(codec);
	
    return 0;
}

static int get_xtalfreq(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

    ucontrol->value.enumerated.item[0] = ak4376->xtalfreq;
	akdbgprt("ak4376 %s %d, xtalfreq %d\n",__func__,__LINE__, ak4376->xtalfreq);

    return 0;
}

static int set_xtalfreq(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
    
	ak4376->xtalfreq = ucontrol->value.enumerated.item[0];
	akdbgprt("ak4376 %s %d, xtalfreq %d\n",__func__,__LINE__, ak4376->xtalfreq);

    return 0;
}

static int get_pdn(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("ak4376 %s %d, on %d\n",__func__,__LINE__, ak4376->on);

    ucontrol->value.enumerated.item[0] = ak4376->on;

    return 0;
}

static int set_pdn(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	int ret;

	ak4376->on = ucontrol->value.enumerated.item[0];

	akdbgprt("ak4376 %s %d on=%d, pdn1 %d\n",__func__,__LINE__,ak4376->on, ak4376->pdn1);

	if(ak4376->pdn_set == 0 && ak4376->on == 1) {
		ret = ak4376_pdn_control(codec, ak4376->on);
		if(ret){
			pr_err("%s set pdn failed!", __func__);
			return ret;
		}
		ak4376->pdn_set = 1;
		akdbgprt("%s PDN Set",__func__);
	} else if(ak4376->pdn_set == 1 && ak4376->on == 0) {
		ret = ak4376_pdn_control(codec, ak4376->on);
		if(ret){
			pr_err("%s drop pdn failed!", __func__);
			return ret;
		}
		ak4376->pdn_set = 0;
		akdbgprt("%s PDN Drop",__func__);
	} else {
		akdbgprt("%s PDN Already Set or Drop",__func__);
		ret = 0;
	}

	return ret;
}

static int get_pllmode(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("%s %d, nPllMode %d\n",__func__,__LINE__, ak4376->nPllMode);

    ucontrol->value.enumerated.item[0] = ak4376->nPllMode;

    return 0;
}

static int set_pllmode(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
    
	ak4376->nPllMode = ucontrol->value.enumerated.item[0];

	akdbgprt("%s %d nPllMode=%d\n",__func__,__LINE__,ak4376->nPllMode);

    return 0;
}

#ifdef AK4376_DEBUG

static const char *test_reg_select[]   = 
{
    "read AK4376 Reg 00:24",
};

static const struct soc_enum ak4376_enum[] = 
{
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(test_reg_select), test_reg_select),
};

static int nTestRegNo = 0;

static int get_test_reg(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    /* Get the current output routing */
    ucontrol->value.enumerated.item[0] = nTestRegNo;
	akdbgprt("%s %d nTestRegNo %d\n",__func__,__LINE__,nTestRegNo);

    return 0;
}

static int set_test_reg(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
    u32    currMode = ucontrol->value.enumerated.item[0];
	int i, value;
	int regs, rege;
	int pdn;

	nTestRegNo = currMode;

	pdn = ak4376_pdn_control(codec, 1);
	if(pdn){
		pr_err("%s set pdn failed!", __func__);
		return pdn;
	}

	regs = 0x00;
	rege = 0x24;

	for ( i = regs ; i <= rege ; i++ ){
		if ( ak4376_readable(NULL, i) ) {
			value = read_component(codec, i);
			pr_info("%s %d, Addr,Reg=(%x, %x)\n", __func__, __LINE__, i, value);
		}
	}

	if ( ak4376->deviceID == 0x22 ) {
		i = 0x25;
		value = ak4376_i2c_read(codec, i);
		pr_info("%s %d, Addr,Reg=(%x, %x)\n", __func__, __LINE__, i, value);
	}
	i = 0x26;
	value = ak4376_i2c_read(codec, i);
	pr_info("%s %d, Addr,Reg=(%x, %x)\n", __func__, __LINE__, i, value);
	i = 0x2A;
	value = ak4376_i2c_read(codec, i);
	pr_info("%s %d, Addr,Reg=(%x, %x)\n", __func__, __LINE__, i, value);

	pdn = ak4376_pdn_control(codec, 0);
	if(pdn){
		pr_err("%s drop pdn failed!", __func__);
		return pdn;
	}

	return 0;
}
#endif

static const struct snd_kcontrol_new ak4376_snd_controls[] = {
	SOC_SINGLE_TLV("AK4376 Digital Output VolumeL",
			AK4376_0B_LCH_OUTPUT_VOLUME, 0, 0x1F, 0, ovl_tlv),
	SOC_SINGLE_TLV("AK4376 Digital Output VolumeR",
			AK4376_0C_RCH_OUTPUT_VOLUME, 0, 0x1F, 0, ovr_tlv),
	SOC_SINGLE_TLV("AK4376 HP-Amp Analog Volume",
			AK4376_0D_HP_VOLUME_CONTROL, 0, 0x0F, 0, hpg_tlv),

	SOC_ENUM("AK4376 Digital Volume Control", ak4376_dac_enum[0]), 
	SOC_ENUM("AK4376 DACL Signal Level", ak4376_dac_enum[1]), 
	SOC_ENUM("AK4376 DACR Signal Level", ak4376_dac_enum[2]), 
	SOC_ENUM("AK4376 DACL Signal Invert", ak4376_dac_enum[3]), 
	SOC_ENUM("AK4376 DACR Signal Invert", ak4376_dac_enum[4]), 
	SOC_ENUM("AK4376 Charge Pump Mode", ak4376_dac_enum[5]), 
	SOC_ENUM("AK4376 HPL Power-down Resistor", ak4376_dac_enum[6]),
	SOC_ENUM("AK4376 HPR Power-down Resistor", ak4376_dac_enum[7]),
	SOC_ENUM("AK4376 DAC Digital Filter Mode", ak4376_dac_enum[8]),
	SOC_ENUM("AK4376 BICK Output Frequency", ak4376_dac_enum[9]),
	SOC_ENUM("AK4376 Digital Filter Mode", ak4376_dac_enum[10]),
	SOC_ENUM("AK4376 Noise Gate", ak4376_dac_enum[11]),
	SOC_ENUM("AK4376 Noise Gate Time", ak4376_dac_enum[12]),

	SOC_ENUM_EXT("AK4376 BICK Frequency Select", ak4376_bitset_enum[0], get_bickfs, set_bickfs),
	SOC_ENUM_EXT("AK4376 PLL MCKI Frequency", ak4376_bitset_enum[1], get_pllmcki, set_pllmcki),
	SOC_ENUM_EXT("AK4376 Low Power Mode", ak4376_bitset_enum[2], get_lpmode, set_lpmode),
	SOC_ENUM_EXT("AK4376 Xtal Frequency", ak4376_bitset_enum[3], get_xtalfreq, set_xtalfreq),
	SOC_ENUM_EXT("AK4376 PDN Control", ak4376_bitset_enum[4], get_pdn, set_pdn),
	SOC_ENUM_EXT("AK4376 PLL Mode", ak4376_bitset_enum[5], get_pllmode, set_pllmode),

#ifdef AK4376_DEBUG
	SOC_ENUM_EXT("Reg Read", ak4376_enum[0], get_test_reg, set_test_reg),
#endif

};


/* DAC control */
static int ak4376_dac_event2(struct snd_soc_component *codec, int event) 
{
	u8 MSmode;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	MSmode = read_component(codec, AK4376_15_AUDIO_IF_FORMAT);
	akdbgprt("%s event 0x%x, nPllMode %d, MSmode 0x%x\n",__func__, event, ak4376->nPllMode, MSmode);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:	/* before widget power up */
		ak4376->nDACOn = 1;
		snd_soc_component_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x01,0x01);		//PMCP1=1
		mdelay(6);																//wait 6ms
		udelay(500);															//wait 0.5ms
		snd_soc_component_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x30,0x30);		//PMLDO1P/N=1
		mdelay(1);																//wait 1ms
		break;
	case SND_SOC_DAPM_POST_PMU:	/* after widget power up */
		snd_soc_component_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x02,0x02);		//PMCP2=1
		mdelay(4);																//wait 4ms
		udelay(500);															//wait 0.5ms
		break;
	case SND_SOC_DAPM_PRE_PMD:	/* before widget power down */
		snd_soc_component_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x02,0x00);		//PMCP2=0
		break;
	case SND_SOC_DAPM_POST_PMD:	/* after widget power down */
		snd_soc_component_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x30,0x00);		//PMLDO1P/N=0
		snd_soc_component_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x01,0x00);		//PMCP1=0

	if (ak4376->nPllMode == 0) {
		if (MSmode & 0x10) {	//Master mode
			snd_soc_component_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x10,0x00);	//MS bit = 0
		}
	}

		ak4376->nDACOn = 0;
		akdbgprt("%s %d, \n",__func__,__LINE__);

		break;
	}
	return 0;
}

static int ak4376_dac_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);

	akdbgprt("%s wname %s, event 0x%x\n",__func__, w->name, event);

	ak4376_dac_event2(codec, event);

	return 0;
}

/* PLL control */
static int ak4376_pll_event2(struct snd_soc_component *codec, int event)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	int pdn;

	akdbgprt("%s event 0x%x, nPllMode %d\n",__func__, event, ak4376->nPllMode);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:/* before widget power up */
	case SND_SOC_DAPM_POST_PMU:/* after widget power up */
		pdn = ak4376_pdn_control(codec, 1);
		if(pdn){
			pr_err("%s set pdn failed!", __func__);
			return pdn;
		}
		if ((ak4376->nPllMode == 1) || (ak4376->nPllMode == 2)) {	//3.18
		snd_soc_component_update_bits(codec, AK4376_00_POWER_MANAGEMENT1, 0x01,0x01);	//PMPLL=1
		}
		else if (ak4376->nPllMode == 3) {	//3.18
		snd_soc_component_update_bits(codec, AK4376_00_POWER_MANAGEMENT1, 0x10,0x10);	//PMOSC=1
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:	/* before widget power down */
	case SND_SOC_DAPM_POST_PMD:	/* after widget power down */
		if ((ak4376->nPllMode == 1) || (ak4376->nPllMode == 2)) {	//3.18
		snd_soc_component_update_bits(codec, AK4376_00_POWER_MANAGEMENT1, 0x01,0x00);	//PMPLL=0
		}
		else if (ak4376->nPllMode == 3) {
		snd_soc_component_update_bits(codec, AK4376_00_POWER_MANAGEMENT1, 0x10,0x00);	//PMOSC=0
		}
		pdn = ak4376_pdn_control(codec, 0);
		if(pdn){
			pr_err("%s drop pdn failed!", __func__);
			return pdn;
		}
		break;
	}

	return 0;
}

static int ak4376_pll_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);

	akdbgprt("%s wname %s, event 0x%x\n",__func__, w->name, event);
	ak4376_pll_event2(codec, event);
	return 0;
}

static int ak4376_widget_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	int ret = 0;

	pr_info("wname %s 0x%x\n", w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
	case SND_SOC_DAPM_POST_PMD:
		break;
	default:
		WARN_ON(1);
		ret = -EINVAL;
	}

	return ret;
}

/* HPL Mixer */
static const struct snd_kcontrol_new ak4376_hpl_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACL", AK4376_07_DAC_MONO_MIXING, 0, 1, 0), 
	SOC_DAPM_SINGLE("RDACL", AK4376_07_DAC_MONO_MIXING, 1, 1, 0), 
};

/* HPR Mixer */
static const struct snd_kcontrol_new ak4376_hpr_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACR", AK4376_07_DAC_MONO_MIXING, 4, 1, 0), 
	SOC_DAPM_SINGLE("RDACR", AK4376_07_DAC_MONO_MIXING, 5, 1, 0), 
};


/* ak4376 dapm widgets */
static const struct snd_soc_dapm_widget ak4376_dapm_widgets[] = {
// DAC
	SND_SOC_DAPM_DAC_E("AK4376 DAC", NULL, AK4376_02_POWER_MANAGEMENT3, 0, 0, 
			ak4376_dac_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD 
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),

// PLL, OSC
	SND_SOC_DAPM_SUPPLY_S("AK4376 PLL", 0, SND_SOC_NOPM, 0, 0, 
			ak4376_pll_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD 
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),

	SND_SOC_DAPM_AIF_IN_E("AK4376 AP01", "HIFI-Playback-AP01", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 AP23", "HIFI-Playback-AP23", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 FAST", "HIFI-Playback-FAST", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 OFFLOAD", "HIFI-Playback-OFFLOAD", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 VOICE", "HIFI-Playback-VOICE", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 VOIP", "HIFI-Playback-VOIP", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 FM", "HIFI-Playback-FM", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 LOOP", "HIFI-Playback-LOOP", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 DSP", "HIFI-FM-DSP", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 TEST", "HIFI-Playback-TEST", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("AK4376 192K DYNAMIC", "HIFI-Playback-192K-DYNAMIC", 0,
			      SND_SOC_NOPM, 0, 0, ak4376_widget_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

// Analog Output
	SND_SOC_DAPM_OUTPUT("AK4376 HPL"),
	SND_SOC_DAPM_OUTPUT("AK4376 HPR"),

	SND_SOC_DAPM_MIXER("AK4376 HPR Mixer", AK4376_03_POWER_MANAGEMENT4, 1, 0,
			&ak4376_hpr_mixer_controls[0], ARRAY_SIZE(ak4376_hpr_mixer_controls)),

	SND_SOC_DAPM_MIXER("AK4376 HPL Mixer", AK4376_03_POWER_MANAGEMENT4, 0, 0,
			&ak4376_hpl_mixer_controls[0], ARRAY_SIZE(ak4376_hpl_mixer_controls)),

};

static const struct snd_soc_dapm_route ak4376_intercon[] =
{

	{"AK4376 DAC", NULL, "AK4376 PLL"},
	{"AK4376 DAC", NULL, "AK4376 AP01"},
	{"AK4376 DAC", NULL, "AK4376 AP23"},
	{"AK4376 DAC", NULL, "AK4376 FAST"},
	{"AK4376 DAC", NULL, "AK4376 OFFLOAD"},
	{"AK4376 DAC", NULL, "AK4376 VOICE"},
	{"AK4376 DAC", NULL, "AK4376 VOIP"},
	{"AK4376 DAC", NULL, "AK4376 FM"},
	{"AK4376 DAC", NULL, "AK4376 LOOP"},
	{"AK4376 DAC", NULL, "AK4376 DSP"},
	{"AK4376 DAC", NULL, "AK4376 TEST"},
	{"AK4376 DAC", NULL, "AK4376 192K DYNAMIC"},

	{"AK4376 HPL Mixer", "LDACL", "AK4376 DAC"},
	{"AK4376 HPL Mixer", "RDACL", "AK4376 DAC"},
	{"AK4376 HPR Mixer", "LDACR", "AK4376 DAC"},
	{"AK4376 HPR Mixer", "RDACR", "AK4376 DAC"},

	{"AK4376 HPL", NULL, "AK4376 HPL Mixer"},
	{"AK4376 HPR", NULL, "AK4376 HPR Mixer"},

};

static int ak4376_set_mcki(struct snd_soc_component *codec, int fs, int rclk)
{
	u8 mode;
	u8 mode2;
	int mcki_rate;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("%s fs=%d rclk=%d, nPllMode %d\n",__func__, fs, rclk, ak4376->nPllMode);

	if ((fs != 0)&&(rclk != 0)) {
		if (rclk > 28800000)
			return -EINVAL;

		if (ak4376->nPllMode == 0) {	//PLL_OFF
			mcki_rate = rclk/fs;
		} else {		//XTAL_MODE
			if ( ak4376->xtalfreq == 0 ) {		//12.288MHz
				mcki_rate = 12288000/fs;
			} else {	//11.2896MHz
				mcki_rate = 11289600/fs;
			}
		}

		mode = read_component(codec, AK4376_05_CLOCK_MODE_SELECT);
		akdbgprt("%s %d, mode 0x%x\n",__func__, __LINE__, mode);
		mode &= ~AK4376_CM;
		akdbgprt("%s %d, mode %d, lpmode %d, mcki_rate %d\n",__func__,__LINE__, mode, ak4376->lpmode, mcki_rate);

		if (ak4376->lpmode == 0) {				//High Performance Mode
			switch (mcki_rate) {
			case 32:
				mode |= AK4376_CM_0;
				break;
			case 64:
				mode |= AK4376_CM_1;
				break;
			case 128:
				mode |= AK4376_CM_3;
				break;
			case 256:
				mode |= AK4376_CM_0;
				mode2 = read_component(codec, AK4376_24_MODE_CONTROL);
				if ( fs <= 12000 ) {
					mode2 |= 0x40;	//DSMLP=1
					snd_soc_component_write(codec, AK4376_24_MODE_CONTROL, mode2);
				} else {
					mode2 &= ~0x40;	//DSMLP=0
					snd_soc_component_write(codec, AK4376_24_MODE_CONTROL, mode2);
				}
				break;
			case 512:
				mode |= AK4376_CM_1;
				break;
			case 1024:
				mode |= AK4376_CM_2;
				break;
			default:
				akdbgprt("%s %d, return error\n",__func__, __LINE__);
				return -EINVAL;
			}
		} else {					//Low Power Mode (LPMODE == DSMLP == 1)
			switch (mcki_rate) {
			case 32:
				mode |= AK4376_CM_0;
				break;
			case 64:
				mode |= AK4376_CM_1;
				break;
			case 128:
				mode |= AK4376_CM_3;
				break;
			case 256:
				mode |= AK4376_CM_0;
				break;
			case 512:
				mode |= AK4376_CM_1;
				break;
			case 1024:
				mode |= AK4376_CM_2;
				break;
			default:
				akdbgprt("%s %d, return error\n",__func__, __LINE__);
				return -EINVAL;
			}
		}
			
		snd_soc_component_write(codec, AK4376_05_CLOCK_MODE_SELECT, mode);
	}
		
	return 0;
}

static int ak4376_set_pllblock(struct snd_soc_component *codec, int fs)
{
	u8 mode;
	int nMClk, nPLLClk, nRefClk;
	int PLDbit, PLMbit, MDIVbit;
	int PLLMCKI;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	
	akdbgprt("%s %d, fs %d\n",__func__,__LINE__, fs);

	mode = read_component(codec, AK4376_05_CLOCK_MODE_SELECT);
	mode &= ~AK4376_CM;

		if ( fs <= 24000 ) {
			mode |= AK4376_CM_1;
			nMClk = 512 * fs;
		}
		else if ( fs <= 96000 ) {
			mode |= AK4376_CM_0;
			nMClk = 256 * fs;
		}
		else if ( fs <= 192000 ) {
			mode |= AK4376_CM_3;
			nMClk = 128 * fs;
		}
		else {		//fs > 192kHz
			mode |= AK4376_CM_1;
			nMClk = 64 * fs;
		}
	
	snd_soc_component_write(codec, AK4376_05_CLOCK_MODE_SELECT, mode);
	
	if ( (fs % 8000) == 0 ) {
		nPLLClk = 122880000;
	} else if ( (fs == 11025 ) && ( ak4376->nBickFreq == 1 ) && ( ak4376->nPllMode == 1 )) {	//3.18
		nPLLClk = 101606400;
	} else {
		nPLLClk = 112896000;
	}

	akdbgprt("%s %d, nPllMode %d, nBickFreq %d\n",__func__,__LINE__, ak4376->nPllMode, ak4376->nBickFreq);

	if (ak4376->nPllMode == 1) {		//BICK_PLL (Slave)
		if (ak4376->nBickFreq == 0) {		//32fs
			if ( fs <= 96000 ) PLDbit = 1;
			else if ( fs <= 192000 ) PLDbit = 2;
			else PLDbit = 4;
			nRefClk = 32 * fs / PLDbit;
		} else if (ak4376->nBickFreq == 1) {	//48fs
			if ( fs <= 16000 ) PLDbit = 1;
			else if ( fs <= 192000 ) PLDbit = 3;
			else PLDbit = 6;
			nRefClk = 48 * fs / PLDbit;
		} else {  									// 64fs
			if ( fs <= 48000 ) PLDbit = 1;
			else if ( fs <= 96000 ) PLDbit = 2;
			else if ( fs <= 192000 ) PLDbit = 4;
			else PLDbit = 8;
			nRefClk = 64 * fs / PLDbit;
		}
	} else {		//MCKI_PLL (Master)
				if ( ak4376->nPllMCKI == 0 ) { //9.6MHz
					PLLMCKI = 9600000;
					if ( (fs % 4000) == 0) nRefClk = 1920000;
					else nRefClk = 384000;
				}
				else if ( ak4376->nPllMCKI == 1 ) { //11.2896MHz
					PLLMCKI = 11289600;
					if ( (fs % 4000) == 0) return -EINVAL;
					else nRefClk = 2822400;
				}
				else if ( ak4376->nPllMCKI == 2 ) { //12.288MHz
					PLLMCKI = 12288000;
					if ( (fs % 4000) == 0) nRefClk = 3072000;
					else nRefClk = 768000;
				}
				else {								//19.2MHz
					PLLMCKI = 19200000;
					if ( (fs % 4000) == 0) nRefClk = 1920000;
					else nRefClk = 384000;
				}
				PLDbit = PLLMCKI / nRefClk;
			}

	PLMbit = nPLLClk / nRefClk;
	MDIVbit = nPLLClk / nMClk;
	
	PLDbit--;
	PLMbit--;
	MDIVbit--;
	
	//PLD15-0
	snd_soc_component_write(codec, AK4376_0F_PLL_REF_CLK_DIVIDER1, ((PLDbit & 0xFF00) >> 8));
	snd_soc_component_write(codec, AK4376_10_PLL_REF_CLK_DIVIDER2, ((PLDbit & 0x00FF) >> 0));
	//PLM15-0
	snd_soc_component_write(codec, AK4376_11_PLL_FB_CLK_DIVIDER1, ((PLMbit & 0xFF00) >> 8));
	snd_soc_component_write(codec, AK4376_12_PLL_FB_CLK_DIVIDER2, ((PLMbit & 0x00FF) >> 0));

//	if (ak4376_data->pdata->nPllMode == 1 ) {	//BICK_PLL (Slave)
	if (ak4376->nPllMode == 1 ) {	//BICK_PLL (Slave)
		snd_soc_component_update_bits(codec, AK4376_0E_PLL_CLK_SOURCE_SELECT, 0x03, 0x01);	//PLS=1(BICK)
	}
	else {										//MCKI PLL (Slave/Master)
		snd_soc_component_update_bits(codec, AK4376_0E_PLL_CLK_SOURCE_SELECT, 0x03, 0x00);	//PLS=0(MCKI)
	}

	//MDIV7-0
	snd_soc_component_write(codec, AK4376_14_DAC_CLK_DIVIDER, MDIVbit);

	return 0;
}

static int ak4376_set_timer(struct snd_soc_component *codec)
{
	int ret, curdata;
	int count, tm, nfs;
	int lvdtm, vddtm, hptm;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	lvdtm = 0;
	vddtm = 0;
	hptm = 0;
	
	nfs = ak4376->fs1;

	//LVDTM2-0 bits set
	ret = read_component(codec, AK4376_03_POWER_MANAGEMENT4);
	curdata = (ret & 0x70) >> 4;	//Current data Save
	ret &= ~0x70;
	do {
       count = 1000 * (64 << lvdtm);
       tm = count / nfs;
       if ( tm > LVDTM_HOLD_TIME ) break;
       lvdtm++;
    } while ( lvdtm < 7 );			//LVDTM2-0 = 0~7
	if ( curdata != lvdtm) {
			snd_soc_component_write(codec, AK4376_03_POWER_MANAGEMENT4, (ret | (lvdtm << 4)));
	}

	//VDDTM3-0 bits set
	ret = read_component(codec, AK4376_04_OUTPUT_MODE_SETTING);
	curdata = (ret & 0x3C) >> 2;	//Current data Save
	ret &= ~0x3C;
	do {
       count = 1000 * (1024 << vddtm);
       tm = count / nfs;
       if ( tm > VDDTM_HOLD_TIME ) break;
       vddtm++;
    } while ( vddtm < 8 );			//VDDTM3-0 = 0~8
	if ( curdata != vddtm) {
			snd_soc_component_write(codec, AK4376_04_OUTPUT_MODE_SETTING, (ret | (vddtm<<2)));
	}

	akdbgprt("%s %d, \n",__func__,__LINE__);

	return 0;
}

static int ak4376_hw_params_set(struct snd_soc_component *codec, int nfs1)
{
	u8 fs;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	int ret;

	fs = read_component(codec, AK4376_05_CLOCK_MODE_SELECT);
	akdbgprt("%s %d, fs 0x%x\n",__func__,__LINE__, fs);
	fs &= ~AK4376_FS;
	akdbgprt("%s %d, fs 0x%x, nfs1 %d\n",__func__,__LINE__, fs, nfs1);

	switch (nfs1) {
	case 8000:
		fs |= AK4376_FS_8KHZ;
		break;
	case 11025:
		fs |= AK4376_FS_11_025KHZ;
		break;
	case 16000:
		fs |= AK4376_FS_16KHZ;
		break;
	case 22050:
		fs |= AK4376_FS_22_05KHZ;
		break;
	case 32000:
		fs |= AK4376_FS_32KHZ;
		break;
	case 44100:
		fs |= AK4376_FS_44_1KHZ;
		break;
	case 48000:
		fs |= AK4376_FS_48KHZ;
		break;
	case 88200:
		fs |= AK4376_FS_88_2KHZ;
		break;
	case 96000:
		fs |= AK4376_FS_96KHZ;
		break;
	case 176400:
		fs |= AK4376_FS_176_4KHZ;
		break;
	case 192000:
		fs |= AK4376_FS_192KHZ;
		break;
	case 352800:
		fs |= AK4376_FS_352_8KHZ;
		break;
	case 384000:
		fs |= AK4376_FS_384KHZ;
		break;
	default:
		pr_err("%s %d, error, return, fs 0x%x\n",__func__,__LINE__, fs);
		return -EINVAL;
	}
	pr_info("%s, fs 0x%x, nPllMode %d, nfs1 %d\n",__func__, fs, ak4376->nPllMode, nfs1);
	ret = snd_soc_component_write(codec, AK4376_05_CLOCK_MODE_SELECT, fs);
	if (ret)
		pr_info("%s %d write reg fail %d\n",__func__, __LINE__, ret);

	if (ak4376->nPllMode == 2) {
		ret = snd_soc_component_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x10, 0x10);//add by weifa.lin, 20191209
	} else if  (ak4376->nPllMode == 1) {
		ret = snd_soc_component_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x10, 0x0);
	}
	if (ret)
		pr_info("%s %d write reg fail %d\n",__func__, __LINE__, ret);

	#ifdef PLL_MCKI_MODE
	snd_soc_component_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x10, 0x10);//add by weifa.lin, 20191209
	#endif

	if ( ak4376->nPllMode == 0 ) {		//PLL Off
		snd_soc_component_update_bits(codec, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x00);	//DACCKS=0
		ak4376_set_mcki(codec, nfs1, ak4376->rclk);
	} else if ( ak4376->nPllMode == 3 ) {	//XTAL MODE
		snd_soc_component_update_bits(codec, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x02);	//DACCKS=2
		ak4376_set_mcki(codec, nfs1, ak4376->rclk);
	} else {											//PLL mode
		snd_soc_component_update_bits(codec, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x01);	//DACCKS=1
		akdbgprt("%s %d, nfs1 %d\n",__func__,__LINE__, nfs1);
		ak4376_set_pllblock(codec, nfs1);
	}

	ak4376_set_timer(codec);

	return 0;
}

static int ak4376_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_component *codec = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	//ak4376->fs1 = params_rate(params);//error, getting 0 when playing mp3
	if (ak4376->nPllMode == 2) {//#ifdef PLL_MCKI_MODE
			ak4376->fs1 = params_rate(params);
			if (ak4376->fs1 == 0)
				ak4376->fs1 = 48000;//192000;
	} else {//#else
			ak4376->fs1 = 48000;
	}//#endif


	pr_info("%s dai->name=%s, fs1 %d, params_rate %d\n", __func__, dai->name, ak4376->fs1, params_rate(params));

	ak4376_hw_params_set(codec, ak4376->fs1);

	return 0;
}

static int ak4376_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	struct snd_soc_component *codec = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	int pdn;

	akdbgprt("%s %d freq=%dHz\n",__func__, __LINE__, freq);

	pdn = ak4376_pdn_control(codec, 1);
	if(pdn){
		pr_err("%s set pdn failed!", __func__);
		return pdn;
	}

	ak4376->rclk = freq;
	akdbgprt("%s %d, dai->name=%s, nPllMode %d, fs1 %d, freq %d\n",__func__, __LINE__, dai->name, ak4376->nPllMode, ak4376->fs1, freq);

	if ((ak4376->nPllMode == 0) || (ak4376->nPllMode == 3)) {	//Not PLL mode
		ak4376_set_mcki(codec, ak4376->fs1, freq);
	}

	pdn = ak4376_pdn_control(codec, 0);
	if(pdn){
		pr_err("%s drop pdn failed!", __func__);
		return pdn;
	}

	return 0;
}

static int ak4376_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{

	struct snd_soc_component *codec = dai->component;
//	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	u8 format;
	int pdn;

	akdbgprt("%s %d\n",__func__,__LINE__);

	pdn = ak4376_pdn_control(codec, 1);
	if(pdn){
		pr_err("%s set pdn failed!", __func__);
		return pdn;
	}

	/* set master/slave audio interface */
	format = read_component(codec, AK4376_15_AUDIO_IF_FORMAT);
	akdbgprt("%s %d, format 0x%x\n",__func__,__LINE__, format);
	format &= ~AK4376_DIF;
	akdbgprt("%s %d, dai->name=%s, format 0x%x, fmt 0x%x\n",__func__,__LINE__, dai->name, format, fmt);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		format |= AK4376_SLAVE_MODE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		format |= AK4376_MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
	default:
		dev_err(codec->dev, "Clock mode unsupported");
		pdn = ak4376_pdn_control(codec, 0);
		if(pdn){
			pr_err("%s drop pdn failed!", __func__);
			return pdn;
		}
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format |= AK4376_DIF_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format |= AK4376_DIF_MSB_MODE;
		break;
	default:
		dev_err(codec->dev, "Dai format unsupported");
		pdn = ak4376_pdn_control(codec, 0);
		if(pdn){
			pr_err("%s drop pdn failed!", __func__);
			return pdn;
		}
		return -EINVAL;
	}
	akdbgprt("%s %d, format 0x%x\n",__func__,__LINE__, format);

	/* set format */
	snd_soc_component_write(codec, AK4376_15_AUDIO_IF_FORMAT, format);
	pdn = ak4376_pdn_control(codec, 0);
	if(pdn){
		pr_err("%s drop pdn failed!", __func__);
		return pdn;
	}

	return 0;
}

static bool ak4376_readable(struct device *dev, unsigned int reg)
{
	int	ret;

	if (reg <=  AK4376_MAX_REGISTERS) {
		ret = true;
	} else if ((reg == AK4376_21_CHIPID) || (reg == AK4376_24_MODE_CONTROL)) {
		ret = true;
	} else {
		ret = false;
	}

	return ret;

}


//static bool ak4376_volatile(struct device *dev, unsigned int reg)
static bool ak4376_volatile(struct device *dev, unsigned int reg)
{
	int	ret;

	switch (reg) {
		case AK4376_15_AUDIO_IF_FORMAT:
		case AK4376_21_CHIPID:
			ret = true;
			break;
		default:
#ifdef AK4376_DEBUG
			ret = true; 
#else
			ret = false;
#endif
			break;
	}

	return ret;
}

static bool ak4376_writeable(struct device *dev, unsigned int reg)
{
	bool ret;

	if (  reg <=  AK4376_MAX_REGISTERS ) {
		ret = true;
	}
	else if (  reg ==  AK4376_24_MODE_CONTROL ) {
		ret = true;
	}
	else {
		ret = false;
	}

	return ret;
}

static int ak4376_i2c_write(struct snd_soc_component *codec, unsigned int reg,
	unsigned int value)
{
	int ret;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	unsigned char tx[2];
	size_t	len = 2;

	tx[0] = (unsigned char)(0xFF & reg);
	tx[1] = (unsigned char)value;

	ret = i2c_master_send(ak4376->i2c, tx, len);

	if( ret < 0 ){
		pr_err("%s error ret = %d \n", __func__, ret );
		ret = -EIO;
	} else {
		akdbgprt("%s addr, data =(0x%X, 0x%X)\n",__func__, reg, (int)value);
		ret = 0;
	}

	return(ret);

}

static unsigned int ak4376_i2c_read(struct snd_soc_component *codec, unsigned int reg)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	int ret;
	unsigned char tx[1], rx[1];

	struct i2c_msg xfer[2];
	struct i2c_client *client = ak4376->i2c;

	tx[0] = reg;
	rx[0] = 0;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = tx;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 1;
	xfer[1].buf = rx;

	ret = i2c_transfer(client->adapter, xfer, 2);

	if( ret != 2 ){
		pr_err("%s error ret = %d \n", __func__, ret );
	}
	pr_info("%s rx 0x%x\n", __func__, (unsigned int)rx[0]);

	return (unsigned int)rx[0];

}

static unsigned int ak4376_reg_read(struct snd_soc_component *codec, unsigned int reg)
{
	unsigned char tx[1];
	int	wlen, rlen;
	int ret = 0;
	int rdata = 0;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	if (ak4376->pdn1 == 0) {
		rdata = read_component(codec, reg);
		dev_err(codec->dev,"%s Read cache\n",__func__);
	} else if ((ak4376->pdn1 == 1) || (ak4376->pdn1 == 2)) {
		wlen = 1;
		rlen = 1;
		tx[0] = reg;
		ret = ak4376_i2c_read(codec, reg);
		dev_err(codec->dev,"%s, reg=0x%x val=0x%x\n", __func__, reg, ret);
		if (ret < 0) {
			dev_err(codec->dev,"%s error ret = %d\n",__func__,ret);
			rdata = -EIO;
		} else {
			rdata = ret;
			dev_err(codec->dev,"%s ret = %d\n",__func__,rdata);
		}
	}

	return rdata;

}

// * for AK4376
static int ak4376_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *codec_dai)
{
	int ret = 0;
	struct snd_soc_component *codec = codec_dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("%s %d, dai->name=%s, cmd %d\n",__func__,__LINE__, codec_dai->name, cmd);
	akdbgprt("%s trigger_cnt %d", __func__, ak4376->trigger_cnt);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if(ak4376->trigger_cnt == 0) {
			ret = ak4376_pdn_control(codec, 1);
		}
		ak4376->trigger_cnt++;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if(ak4376->trigger_cnt == 1){
			ret = ak4376_pdn_control(codec, 0);
		}
		ak4376->trigger_cnt--;
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	}
	return ret;
}

#if 0
static int ak4376_set_bias_level(struct snd_soc_component *codec,
		enum snd_soc_bias_level level)
{
	int level1 = level;
#ifdef KERNEL_4_9_XX
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);

	akdbgprt("%s %d snd_soc_codec_get_bias_level=%d\n",__func__,__LINE__,snd_soc_codec_get_bias_level(codec));
#else
	akdbgprt("%s %d codec->dapm.bias_level=%d\n",__func__,__LINE__,codec->dapm.bias_level);
#endif


	akdbgprt("%s %d level=%d\n",__func__,__LINE__,level1);

	switch (level1) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
#ifdef KERNEL_4_9_XX
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_STANDBY)
#else
		if (codec->dapm.bias_level == SND_SOC_BIAS_STANDBY)
#endif
			akdbgprt("%s %d codec->dapm.bias_level == SND_SOC_BIAS_STANDBY\n",__func__,__LINE__);

#ifdef KERNEL_4_9_XX
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_ON)
#else
		if (codec->dapm.bias_level == SND_SOC_BIAS_ON)
#endif
			akdbgprt("%s %d codec->dapm.bias_level >= SND_SOC_BIAS_ON\n",__func__,__LINE__);
		break;
	case SND_SOC_BIAS_STANDBY:
#ifdef KERNEL_4_9_XX
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_PREPARE) {
#else
		if (codec->dapm.bias_level == SND_SOC_BIAS_PREPARE) {
#endif
			akdbgprt("%s %d codec->dapm.bias_level == SND_SOC_BIAS_PREPARE\n",__func__,__LINE__);
			ak4376_pdn_control(codec, 0);
#ifdef KERNEL_4_9_XX
		} if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF)
#else
		} if (codec->dapm.bias_level == SND_SOC_BIAS_OFF)
#endif
			akdbgprt("%s %d codec->dapm.bias_level == SND_SOC_BIAS_OFF\n",__func__,__LINE__);
		break;
	case SND_SOC_BIAS_OFF:
#ifdef KERNEL_4_9_XX
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_STANDBY) {
#else
		if (codec->dapm.bias_level == SND_SOC_BIAS_STANDBY) {
#endif
			akdbgprt("%s %d codec->dapm.bias_level == SND_SOC_BIAS_STANDBY\n",__func__,__LINE__);
			ak4376_pdn_control(codec, 0);
		}
		break;
	}

#ifdef KERNEL_4_9_XX
	dapm->bias_level = level;
#else
	codec->dapm.bias_level = level;
#endif
	return 0;
}
#endif

#if defined(DCONFIG_SND_SOC_AKM4377_K54)
static int ak4376_set_dai_mute(struct snd_soc_dai *dai, int mute) 
{
	u8 MSmode;
	struct snd_soc_component *codec = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

#ifdef KERNEL_4_9_XX
	akdbgprt("%s %d snd_soc_codec_get_bias_level(codec)=%d\n",__func__,__LINE__,snd_soc_codec_get_bias_level(codec));
#else
	akdbgprt("%s %d snd_soc_codec_get_bias_level(codec)=%d\n",__func__,__LINE__,codec->dapm.bias_level);

#endif

	pr_info("%s %d, nPllMode %d, nDACOn %d\n", __func__, __LINE__, ak4376->nPllMode, ak4376->nDACOn);
	if (ak4376->nPllMode == 0) {
		if ( ak4376->nDACOn == 0 ) {
			MSmode = read_component(codec, AK4376_15_AUDIO_IF_FORMAT);
			if (MSmode & 0x10) {	//Master mode
				pr_info("%s %d\n", __func__, __LINE__);
				snd_soc_component_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x10,0x00);	//MS bit = 0
			}
		}
	}

#ifdef KERNEL_4_9_XX
	if (snd_soc_codec_get_bias_level(codec) <= SND_SOC_BIAS_STANDBY) {
#else
	if (codec->dapm.bias_level <= SND_SOC_BIAS_STANDBY) {
#endif
		akdbgprt("%s %d codec->dapm.bias_level <= SND_SOC_BIAS_STANDBY\n",__func__,__LINE__);

	}

	return 0;
}
#endif

#define AK4376_RATES		(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
				SNDRV_PCM_RATE_192000)

#define AK4376_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)


static struct snd_soc_dai_ops ak4376_dai_ops = {
	.hw_params	= ak4376_hw_params,
	.set_sysclk	= ak4376_set_dai_sysclk,
	.set_fmt	= ak4376_set_dai_fmt,
	.trigger = ak4376_trigger,
#if defined(DCONFIG_SND_SOC_AKM4377_K54)
	.digital_mute = ak4376_set_dai_mute,
#endif
};

struct snd_soc_dai_driver ak4376_dai[] = {
	/* 0: NORMAL_AP01 */
	{										 
		.name = "ak4376-AP01",
		.playback = {
		       .stream_name = "HIFI-Playback-AP01",
		       .channels_min = 1,
		       .channels_max = 2,
		       .rates = AK4376_RATES,
		       .formats = AK4376_FORMATS,
		},
		.ops = &ak4376_dai_ops,
	},
	/* 1: NORMAL_AP23 */
	{
		.name = "ak4376-AP23",
		.playback = {
		       .stream_name = "HIFI-Playback-AP23",
		       .channels_min = 1,
		       .channels_max = 2,
		       .rates = AK4376_RATES,
		       .formats = AK4376_FORMATS,
		},
		.ops = &ak4376_dai_ops,
	},
	/* 2: FAST_P */
	{
		.name = "ak4376-FAST",
		.playback = {
			.stream_name = "HIFI-Playback-FAST",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AK4376_RATES,
			.formats = AK4376_FORMATS,
		},
	.ops = &ak4376_dai_ops,
	},
	/* 3: OFFLOAD */
	{
	   .name = "ak4376-OFFLOAD",
	   .playback = {
		   .stream_name = "HIFI-Playback-OFFLOAD",
		   .channels_min = 1,
		   .channels_max = 2,
		   .rates = AK4376_RATES,
		   .formats = AK4376_FORMATS,
	   },
	.ops = &ak4376_dai_ops,
	},
	/* 4: VOICE */
	{
		.name = "ak4376-VOICE",
		.playback = {
			.stream_name = "HIFI-Playback-VOICE",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AK4376_RATES,
			.formats = AK4376_FORMATS,
		},
	.ops = &ak4376_dai_ops,
	},
	/* 5: VOIP */
	{
	   .name = "ak4376-VOIP",
	   .playback = {
		   .stream_name = "HIFI-Playback-VOIP",
		   .channels_min = 1,
		   .channels_max = 2,
		   .rates = AK4376_RATES,
		   .formats = AK4376_FORMATS,
	   },
	.ops = &ak4376_dai_ops,
	},
	/* 6: FM */
	{
		.name = "ak4376-FM",
		.playback = {
			.stream_name = "HIFI-Playback-FM",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AK4376_RATES,
			.formats = AK4376_FORMATS,
		},
	.ops = &ak4376_dai_ops,
	},
	/* 7: loopback(dsp voice) */
	{
	   .name = "ak4376-LOOP",
	   .playback = {
		   .stream_name = "HIFI-Playback-LOOP",
		   .channels_min = 1,
		   .channels_max = 2,
		   .rates = AK4376_RATES,
		   .formats = AK4376_FORMATS,
	   },
	.ops = &ak4376_dai_ops,
	},
	/* 8: FM dsp*/
	{
		.name = "ak4376-FM-DSP",
		.playback = {
			.stream_name = "HIFI-FM-DSP",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AK4376_RATES,
			.formats = AK4376_FORMATS,
		},
	.ops = &ak4376_dai_ops,
	},
	/* 9: codec test */
	{
	   .name = "ak4376-TEST",
	   .playback = {
		   .stream_name = "HIFI-Playback-TEST",
		   .channels_min = 1,
		   .channels_max = 2,
		   .rates = AK4376_RATES,
		   .formats = AK4376_FORMATS,
	   },
	.ops = &ak4376_dai_ops,
	},
};

static int ak4376_init_reg(struct snd_soc_component *codec)
{
	u8 devID;
	u8 chipID;
	int pdn;

	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);

	akdbgprt("%s %d\n",__func__,__LINE__);

	ak4376->deviceID = -1;
	pdn = ak4376_pdn_control(codec, 1);
	if(pdn){
		pr_err("%s set pdn failed!", __func__);
		return pdn;
	}

	devID = ak4376_reg_read(codec, AK4376_15_AUDIO_IF_FORMAT);
	pr_info("%s devID 0x%x\n", __func__, devID);
	devID &= 0xE0;
	devID >>= 1;

	if ( devID == 0x20 ) {
		chipID = ak4376_reg_read(codec, AK4376_21_CHIPID);
		chipID &= 0x07;
	} else {
		chipID = 0;
	}

	ak4376->deviceID = devID + chipID;

	pr_info("%s Device/ChipID=%X\n", __func__, ak4376->deviceID );

	switch (ak4376->deviceID) {
	case 0:
		pr_info("ak4376_init_reg AK4375 is connecting.\n");
		break;
	case 0x10:
		pr_info("ak4376_init_reg AK4375A is connecting.\n");
		break;
	case 0x20:
		pr_info("ak4376_init_reg AK4376 is connecting.\n");
		break;
	case 0x21:
		pr_info("ak4376_init_reg AK4376A is connecting.\n");
		break;
	case 0x22:
		pr_info("ak4376_init_reg AK4377A is connecting.\n");
		break;
	case 0x30:
		pr_info("ak4376_init_reg AK4377 is connecting.\n");
		break;
	case 0x70:
		pr_info("ak4376_init_reg AK4331 is connecting.\n");
		break;
	default:
		pr_info("ak4376_init_reg This device is not AK437X\n");
		break;
	}

	if ( ak4376->deviceID == 0x22 ) {
		ak437x_patch = ak4377a_patch;
		ak437x_ptsize = ARRAY_SIZE(ak4377a_patch);
		exist_chip = true;
	} else {
		ak437x_patch = ak4376a_patch;
		ak437x_ptsize = ARRAY_SIZE(ak4376a_patch);
	}
	ak4376_update_register(codec);
	akdbgprt("%s exit\n", __func__);
	pdn = ak4376_pdn_control(codec, 0);
	if(pdn){
		pr_err("%s drop pdn failed!", __func__);
		return pdn;
	}

	return 0;
}

static int ak4376_parse_dt(struct ak4376_priv *ak4376)
{
	struct device *dev;
	struct device_node *np;
	int ret;

	dev = &(ak4376->i2c->dev);

	np = dev->of_node;

	if (!np)
		return -1;

	pr_info("%s enter\n", __func__);

	ak4376->priv_pdn_en = of_get_named_gpio(np, "ak4377,pdn-gpio", 0);
	if (ak4376->priv_pdn_en < 0) {
		//ak4376->priv_pdn_en = -1;
		pr_err("%s parse 'ak4377,pdn-gpio' fail %d\n", __func__, ak4376->priv_pdn_en);
		return -1;
	}

	if( !gpio_is_valid(ak4376->priv_pdn_en) ) {
		pr_err("%s pdn pin(%u) is invalid\n", __func__, ak4376->priv_pdn_en);
		return -1;
	}

	ak4376->ldo1_regu = devm_regulator_get(dev, "vddldo1");
	if (IS_ERR_OR_NULL(ak4376->ldo1_regu)) {
		pr_warn("get vddldo1 supply failed(%ld)!\n",
			PTR_ERR(ak4376->ldo1_regu));
		//ak4376->ldo1_regu = NULL;
		return PTR_ERR(ak4376->ldo1_regu);
	}
	ret = of_property_read_u32(np, "sprd,vddldo1-voltage",
				   &ak4376->ldo1_vol);
	if (ret < 0) {
		pr_warn("parse 'sprd,vddldo1-voltage' failed(%d).\n",
			ret);
		ak4376->ldo1_vol = 1800000;
	}
	pr_info("%s priv_pdn_en %d, ldo1_vol %d, successful\n",
		__func__, ak4376->priv_pdn_en, ak4376->ldo1_vol);

	ak4376->clk_48m = devm_clk_get(dev, "clk_48M");
	if (IS_ERR(ak4376->clk_48m)) {
		dev_err(dev, "can't get clock dts config: clk_48m\n");
		return -EINVAL;
	}

	ak4376->clk_parent = devm_clk_get(dev, "source");
	if (IS_ERR(ak4376->clk_parent)) {
		dev_err(dev, "can't get clock dts config: source\n");
		return -EINVAL;
	}
	clk_set_parent(ak4376->clk_48m, ak4376->clk_parent);

	ak4376->clk_enable = devm_clk_get(dev, "enable");
	if (IS_ERR(ak4376->clk_enable)) {
		dev_err(dev, "can't get clock dts config: enable\n");
		return -EINVAL;
	}

	return 0;
}

static int ak4376_probe(struct snd_soc_component *codec)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(codec);
	int ret = 0;

	pr_info("%s enter\n",__func__);

	ret = ak4376_parse_dt(ak4376);
	if ( ret < 0 )
		ak4376->pdn1 = 2;	//No use GPIO control

	ret = gpio_request(ak4376->priv_pdn_en, "ak4376 pdn");
	if (ret) {
		pr_info("%s cannot get ak4376 pdn gpio, ret %d\n",__func__, ret);
		ak4376->pdn1 = 2;	//No use GPIO control
	}

	ret = gpio_direction_output(ak4376->priv_pdn_en, 0);
	if (ret) {
		pr_err("%s set gpio output fail %d\n", __func__, ret);
		gpio_free(ak4376->priv_pdn_en);
		ak4376->pdn1 = 2;
	} else {
		akdbgprt("%s %d pdn_en=0\n", __func__,__LINE__);
	}

#ifdef CONFIG_DEBUG_FS_CODEC
	mutex_init(&io_lock);
	ak4376_codec = codec;
#endif
	
	ak4376_init_reg(codec);

	akdbgprt("%s %d\n",__func__,__LINE__);

	ak4376->fs1 = 48000;
	ak4376->rclk = 0;
	ak4376->nBickFreq = 0;		//0:32fs, 1:48fs, 2:64fs
	ak4376->nPllMCKI = 0;		//0:9.6MHz, 1:11.2896MHz, 2:12.288MHz, 3:19.2MHz
	ak4376->lpmode = 0;			//0:High Performance mode, 1:Low Power Mode
	ak4376->xtalfreq = 0;		//0:12.288MHz, 1:11.2896MHz
	ak4376->nDACOn = 0;

#ifdef PLL_BICK_MODE	//at ak4376_pdata.h
	ak4376->nPllMode = 1;
	akdbgprt("%s %d, nPllMode %d\n",__func__,__LINE__, ak4376->nPllMode);
#else
	#ifdef PLL_MCKI_MODE
		ak4376->nPllMode = 2;
	#else
		#ifdef XTAL_MODE
			ak4376->nPllMode = 3;
		#endif
			ak4376->nPllMode = 0;
			akdbgprt("%s %d, nPllMode %d\n",__func__,__LINE__, ak4376->nPllMode);
	#endif
#endif

	dapm->idle_bias_off = true;
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-AP01");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-AP23");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-FAST");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-OFFLOAD");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-VOICE");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-VOIP");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-FM");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-LOOP");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-FM-DSP");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-TEST");
	snd_soc_dapm_ignore_suspend(dapm, "HIFI-Playback-192K-DYNAMIC");
	snd_soc_dapm_ignore_suspend(dapm, "AK4376 HPL");
	snd_soc_dapm_ignore_suspend(dapm, "AK4376 HPR");

	return ret;
}

static void ak4376_remove(struct snd_soc_component *codec)
{

	akdbgprt("%s %d\n",__func__,__LINE__);

	//ak4376_set_bias_level(codec, SND_SOC_BIAS_OFF);
}

static int ak4376_suspend(struct snd_soc_component *codec)
{

	akdbgprt("%s %d\n",__func__,__LINE__);

	//ak4376_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int ak4376_resume(struct snd_soc_component *codec)
{

	akdbgprt("%s %d\n",__func__,__LINE__);

	//ak4376_init_reg(codec);

	return 0;
}

struct snd_soc_component_driver soc_codec_dev_ak4376 = {
	.probe = ak4376_probe,
	.remove = ak4376_remove,
	.suspend = ak4376_suspend,
	.resume = ak4376_resume,

	//.idle_bias_off = true,
	//.set_bias_level = ak4376_set_bias_level,

#ifdef KERNEL_4_9_XX
	.component_driver = {
#endif
	.controls = ak4376_snd_controls,
	.num_controls = ARRAY_SIZE(ak4376_snd_controls),
	.dapm_widgets = ak4376_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ak4376_dapm_widgets),
	.dapm_routes = ak4376_intercon,
	.num_dapm_routes = ARRAY_SIZE(ak4376_intercon),
#ifdef KERNEL_4_9_XX
	},
#endif

};

static const struct regmap_config ak4376_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = AK4376_MAX_REGISTERS,
	.readable_reg = ak4376_readable,
	.volatile_reg = ak4376_volatile,
	.writeable_reg = ak4376_writeable,

	.reg_defaults = ak4376_reg,
	.num_reg_defaults = ARRAY_SIZE(ak4376_reg),
	.cache_type = REGCACHE_RBTREE,

};

static struct of_device_id ak4376_i2c_dt_ids[] = {
	{ .compatible = "akm,ak4376"},
    { }
};
MODULE_DEVICE_TABLE(of, ak4376_i2c_dt_ids);

static ssize_t ak4376_chip_exist_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buff)
{
	return sprintf(buff, "%d\n", exist_chip);
}

static int ak4376_sysfs_create_chip_exist(void)
{
	int ret;
	static struct kobj_attribute chip_exit =
		__ATTR(hifi_exist, 0644,
		ak4376_chip_exist_show,
		NULL);

	ret = sysfs_create_file(kernel_kobj, &chip_exit.attr);
	if (ret) {
		pr_err("create sysfs '%s' failed. ret = %d\n",
		       chip_exit.attr.name, ret);
		return ret;
	}

	return ret;
}

static int ak4376_i2c_probe(struct i2c_client *i2c,
                            const struct i2c_device_id *id)
{
	struct ak4376_priv *ak4376;
	int ret=0;
	
	pr_info("%s \n",__func__);

	ak4376 = kzalloc(sizeof(struct ak4376_priv), GFP_KERNEL);
	if (ak4376 == NULL)
		return -ENOMEM;

	ak4376->regmap = devm_regmap_init_i2c(i2c, &ak4376_regmap);
	if (IS_ERR(ak4376->regmap)) {
		ret = PTR_ERR(ak4376->regmap);
		kfree(ak4376);
		pr_err("%s regmap error\n",__func__);
		return ret;
	}

#ifdef  CONFIG_DEBUG_FS_CODEC
	ret = device_create_file(&i2c->dev, &dev_attr_reg_data);
	if (ret) {
		pr_err("%s: Error to create reg_data\n", __func__);
	}
#endif

	i2c_set_clientdata(i2c, ak4376);

	ak4376->i2c = i2c;
	ak4376->pdn1 = 0;
	ak4376->on = 0;
	ak4376->priv_pdn_en = 0;
	ak4376->pdn_cnt = 0;
	ak4376->pdn_set = 0;
	ak4376->trigger_cnt = 0;

	ret = snd_soc_register_component(&i2c->dev,
			&soc_codec_dev_ak4376, &ak4376_dai[0], ARRAY_SIZE(ak4376_dai));
	if (ret < 0){
		/* kfree(ak4376); */ /* may lead to null pointer crash when call kcontrols in DUT which don't have akm4377 chip */
		pr_err("%s register codec fail %d\n",__func__, ret);
	}

	akdbgprt("%s pdn1=%d, exit ret=%d\n", __func__,ak4376->pdn1,ret);

	ak4376_sysfs_create_chip_exist();

	return ret;
}

static int ak4376_i2c_remove(struct i2c_client *client)
{

	akdbgprt("%s %d\n",__func__,__LINE__);

#ifdef CONFIG_DEBUG_FS_CODEC
	device_remove_file(&client->dev, &dev_attr_reg_data);
#endif

	snd_soc_unregister_component(&client->dev);
	kfree(i2c_get_clientdata(client));

	return 0;
}

static const struct i2c_device_id ak4376_i2c_id[] = {
	{ "ak4376", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ak4376_i2c_id);

static struct i2c_driver ak4376_i2c_driver = {
	.driver = {
		.name = "ak4376",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ak4376_i2c_dt_ids),	//3.18
	},
	.probe = ak4376_i2c_probe,
	.remove = ak4376_i2c_remove,
	.id_table = ak4376_i2c_id,
};

static int __init ak4376_modinit(void)
{

	akdbgprt("%s %d\n", __func__,__LINE__);

	return i2c_add_driver(&ak4376_i2c_driver);
}

module_init(ak4376_modinit);

static void __exit ak4376_exit(void)
{
	i2c_del_driver(&ak4376_i2c_driver);
}
module_exit(ak4376_exit);

MODULE_DESCRIPTION("ASoC ak4376 codec driver");
MODULE_LICENSE("GPL v2");
