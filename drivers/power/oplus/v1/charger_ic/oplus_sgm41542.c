// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/iio/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#ifdef CONFIG_OPLUS_CHARGER_UNISOC
#include <linux/hardware_info_lux.h>
#endif

#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_pps.h"
#include "../oplus_short.h"
#include "../oplus_wireless.h"
#include "../oplus_adapter.h"
#include "../charger_ic/oplus_short_ic.h"
#include "../gauge_ic/oplus_bq27541.h"
#include <soc/oplus/system/boot_mode.h>
#include "../oplus_chg_ops_manager.h"
#include "../voocphy/oplus_voocphy.h"
#include "oplus_discrete_charger.h"
#include "oplus_sgm41542.h"
#include "oplus_sgm41515d.h"
#include "oplus_sgm41512sd.h"
#include <linux/pm_wakeup.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include "oplus_battery_ums9230.h"
#include <linux/sched/clock.h>

//extern void oplus_notify_device_mode(bool enable);qcom
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))

extern int sprd_hsphy_set_dpdm_high_impedance_state(void);
extern int sprd_hsphy_cancel_dpdm_high_impedance_state(void);
#endif

extern struct charger_manager *g_cm;

extern int oplus_battery_meter_get_battery_voltage(void);
extern int oplus_get_rtc_ui_soc(void);
extern int oplus_set_rtc_ui_soc(int value);
extern int get_rtc_spare_oplus_fg_value(void);
extern int set_rtc_spare_oplus_fg_value(int value);
extern void oplus_ums9230_usbtemp_set_cc_open(void);
extern void oplus_ums9230_usbtemp_set_typec_sinkonly(void);
extern void mt_set_chargerid_switch_val(int value);
extern int mt_get_chargerid_switch_val(void);
extern void  oplus_chg_set_otg_online(bool online);
extern bool oplus_chg_get_otg_online(void);
extern int qpnp_get_battery_voltage(void);
extern int oplus_chg_set_pd_config(void);
extern int oplus_chg_enable_qc_detect(void);
extern void oplus_get_usbtemp_volt(struct oplus_chg_chip *chip);
extern void oplus_set_typec_sinkonly(void);
extern bool oplus_usbtemp_condition(void);
extern void oplus_set_typec_cc_open(void);
extern void oplus_set_pd_active(int active);
extern int oplus_get_adapter_svid(void);
extern void oplus_wake_up_usbtemp_thread(void);
extern int qpnp_get_prop_charger_voltage_now(void);
extern int oplus_chg_get_battery_btb_temp_cal(void);
extern void oplus_chg_input_current_recheck_work(void);

extern void oplus_sgm41542_enable_gpio(bool enable);
extern void sgm41542_really_suspend_charger(bool en);
extern int sprd_get_pd_type(void);
extern int oplus_get_adapter_svid(void);
int sgm41542_get_vbus_stat(void);
extern void oplus_chg_set_input_current_limit(struct oplus_chg_chip *chip);
extern int oplus_chg_get_tbatt_normal_charging_current(struct oplus_chg_chip *chip);
extern int oplus_gauge_get_batt_current(void);
extern void voocphy_set_otg_en(bool);
static void oplus_notify_hvdcp_detach_stat(void);
#if IS_ENABLED(CONFIG_USB_SC27XX_TYPEC)
extern bool oplus_chg_typec_get_usb_present(void);
#endif

#define AICL_POINT_VOL_9V 		7600
#define AICL_POINT_VOL_5V_HIGH		4250
#define AICL_POINT_VOL_5V_MID		4150
#define AICL_POINT_VOL_5V_LOW		4100
#define HW_AICL_POINT_VOL_5V_PHASE1 	4400
#define HW_AICL_POINT_VOL_5V_PHASE2 	4500
#define SW_AICL_POINT_VOL_5V_PHASE1 	4500
#define SW_AICL_POINT_VOL_5V_PHASE2 	4550


#define PD_AUTHENTICATION_LIMIT_CUR_100MA	100000
#define PD_AUTHENTICATION_LIMIT_CUR_0MA		0

#define SGM41542_QC_RETRY_CNT	3

struct chip_sgm41542 {
	struct device		*dev;
	struct i2c_client	*client;
	int			irq_gpio;
	int			enable_gpio;
	struct pinctrl 		*pinctrl;
	struct pinctrl_state 	*enable_gpio_active;
	struct pinctrl_state 	*enable_gpio_sleep;
	enum power_supply_type	oplus_charger_type;
	atomic_t		charger_suspended;
	atomic_t		is_suspended;
	struct mutex		i2c_lock;
	struct mutex		dpdm_lock;
	struct regulator	*dpdm_reg;
	bool			dpdm_enabled;
	bool			is_sgm41542;
	bool			is_bq25601d;
	bool			bc12_done;
	int			hw_aicl_point;
	int			sw_aicl_point;
	int			reg_access;
	int			before_suspend_icl;
	int			before_unsuspend_icl;
	bool                    batfet_reset_disable;
	bool			use_voocphy;
	int			charger_current_pre;
	struct delayed_work	init_work;
	struct delayed_work	vindpm_recovery_work;
	struct delayed_work	qc_detect_work;
	struct delayed_work	chg_protection_work;
	struct delayed_work otg_iindpm_work;
	struct delayed_work	update_chg_type_work;
	struct notifier_block fchg_notify;
	bool notifier_registered;
	bool support_hvdcp;
	bool is_hvdcp;
	bool pre_is_hvdcp;
	bool qc_chging;
	int part_id;
	struct wakeup_source *suspend_ws;
	/*fix chgtype identify error*/
	struct wakeup_source *keep_resume_ws;
	wait_queue_head_t wait;
	int ap_charge_current;
	struct delayed_work	slave_charger_work;
	bool slave_chg_en;
	bool hvdcp_cfg_9v_done;
	int hvdcp_exit_stat;
	unsigned long long hvdcp_detect_time;
	unsigned long long hvdcp_detach_time;
	bool			force_qc_5v;
	const char 		*buck_ic_name;
	bool			support_vreg_ft;
	int bc12_retry_count;
};

static bool sgm41542_chg_is_usb_present(void);
void oplus_chg_sgm41542_set_bc12_init(bool enable);
bool sgm41542_check_really_suspend_charger(void);
static struct chip_sgm41542 *charger_ic = NULL;
static int aicl_result = 500;
static bool btb_detect_over;

static const unsigned int SGM41515D_IPRECHG_CURRENT_STABLE[IPRECHG_CURRENT_STABLE_LEN] = {
	5, 10, 15, 20, 30, 40, 50, 60,
	80, 100, 120, 140, 160, 180, 200, 240
};
static const unsigned int SGM41515D_ITERM_CURRENT_STABLE[ITERM_CURRENT_STABLE_LEN] = {
	5, 10, 15, 20, 30, 40, 50, 60,
	80, 100, 120, 140, 160, 180, 200, 240
};

int __attribute__((weak)) oplus_chg_enable_qc_detect(void)
{
	return 0;
}
int oplus_sgm41542_get_pd_type(void)
{
    return sprd_get_pd_type();
}

static int oplus_get_boot_reason(void)
{
	return 0;
}

bool oplus_sgm41542_get_bc12_done(void)
{
//	int vbus_stat = 0;

	if(!charger_ic)
	    return false;
/*
	vbus_stat = sgm41542_get_vbus_stat();
	chg_err("%s-line=%d, bc12_done=%d\n", __func__, __LINE__, charger_ic->bc12_done);

	if (!charger_ic->bc12_done &&
		(vbus_stat == REG08_SGM41542_VBUS_STAT_SDP ||
		vbus_stat == REG08_SGM41542_VBUS_STAT_CDP)) {
		charger_ic->oplus_charger_type = POWER_SUPPLY_TYPE_USB;
		return true;
	}*/

	return charger_ic->bc12_done;
}
EXPORT_SYMBOL(oplus_sgm41542_get_bc12_done);

int oplus_sgm41542_get_charger_type_read_only(void)
{
	if(!charger_ic)
		return -1;

	return charger_ic->oplus_charger_type;
}

int oplus_sgm41542_get_charger_type(void)
{
	if(!charger_ic)
	    return -1;

	return charger_ic->oplus_charger_type;
}
EXPORT_SYMBOL(oplus_sgm41542_get_charger_type);

void oplus_sgm41542_set_ap_current(int value)
{
	struct chip_sgm41542 *chip = charger_ic;

	if(!chip)
	    return ;

	chip->ap_charge_current = value;

	chg_err("ap_charge_current: %d\n", value);
}
EXPORT_SYMBOL(oplus_sgm41542_set_ap_current);

bool oplus_sgm41542_get_slave_chg_en(void)
{
	if(!charger_ic)
	    return false;

	return charger_ic->slave_chg_en;
}
EXPORT_SYMBOL(oplus_sgm41542_get_slave_chg_en);

void oplus_sgm41542_set_chargerid_switch_val(int value)
{
	mt_set_chargerid_switch_val(value);
}

int oplus_sgm41542_get_chargerid_switch_val(void)
{
	return mt_get_chargerid_switch_val();
}

int oplus_sgm41542_get_chargerid_volt(void)
{
	return 0;
}

static int sgm41542_request_dpdm(struct chip_sgm41542 *chip, bool enable);
static void sgm41542_get_bc12_init(struct chip_sgm41542 *chip);
#define READ_REG_CNT_MAX	20
#define USLEEP_5000MS	5000

static void oplus_chg_wakelock(struct chip_sgm41542 *chip, bool awake);

static int __sgm41542_read_reg(struct chip_sgm41542 *chip, int reg, int *data)
{
	s32 ret = 0;
	int retry = READ_REG_CNT_MAX;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		while(retry > 0 && atomic_read(&chip->is_suspended) == 0) {
			usleep_range(USLEEP_5000MS, USLEEP_5000MS);
			ret = i2c_smbus_read_byte_data(chip->client, reg);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		chg_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	} else {
		*data = ret;
	}

	return 0;
}

static int __sgm41542_write_reg(struct chip_sgm41542 *chip, int reg, int val)
{
	s32 ret = 0;
	int retry = 3;

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_write_byte_data(chip->client, reg, val);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		chg_err("i2c write fail: can't write %02x to %02x: %d\n", val, reg, ret);
		return ret;
	}

	return 0;
}

static int sgm41542_read_reg(struct chip_sgm41542 *chip, int reg, int *data)
{
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41542_read_reg(chip, reg, data);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static __maybe_unused int sgm41542_write_reg(struct chip_sgm41542 *chip, int reg, int data)
{
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41542_write_reg(chip, reg, data);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static __maybe_unused int sgm41542_config_interface(struct chip_sgm41542 *chip, int reg, int data, int mask)
{
	int ret;
	int tmp;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41542_read_reg(chip, reg, &tmp);
	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sgm41542_write_reg(chip, reg, tmp);
	if (ret)
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

#define VINDPM_RECOVERY_DELAY_MS 100
int sgm41542_set_vindpm_vol(int vindpm)
{
	int rc;
	int tmp = 0;
	int offset = 0, offset_val = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct charger_manager *cm = g_cm;

	if (!chip)
		return 0;

	if (!cm)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("vindpm = %d\n", vindpm);

	if((cm->vchg_info->usb_limit >= PD_AUTHENTICATION_LIMIT_CUR_0MA &&
		cm->vchg_info->usb_limit <= PD_AUTHENTICATION_LIMIT_CUR_100MA &&
		(cm->desc->limit_status & CM_CHARGE_USB_LIMIT_CMD) &&
		!(cm->desc->limit_status & CM_CHARGE_PD_LIMIT_CMD)) ||
		(cm->desc->rp_limit_current >= PD_AUTHENTICATION_LIMIT_CUR_0MA &&
		cm->desc->rp_limit_current <= PD_AUTHENTICATION_LIMIT_CUR_100MA &&
		(cm->desc->limit_status & CM_CHARGE_RP_LIMIT_CMD)) ||
		(cm->desc->pd_req_cur_ua >= PD_AUTHENTICATION_LIMIT_CUR_0MA &&
		cm->desc->pd_req_cur_ua <= PD_AUTHENTICATION_LIMIT_CUR_100MA &&
		(cm->desc->limit_status & CM_CHARGE_PD_LIMIT_CMD)))
	{
		vindpm = REG06_SGM41542_VINDPM_MAX;
		chg_err("Is still in PD test, force set vindpm =%d \n",vindpm);
	}

	if(vindpm < SGM41542_VINDPM_THRESHOLD_5900MV) {
		offset = VINDPM_OS_3900mV;
		offset_val = SGM41542_VINDPM_THRESHOLD_3900MV;
	} else if (vindpm < SGM41542_VINDPM_THRESHOLD_7500MV) {
		offset = VINDPM_OS_5900mV;
		offset_val = SGM41542_VINDPM_THRESHOLD_5900MV;
	} else if (vindpm < SGM41542_VINDPM_THRESHOLD_10500MV) {
		offset = VINDPM_OS_7500mV;
		offset_val = SGM41542_VINDPM_THRESHOLD_7500MV;
	} else if (vindpm <= SGM41542_VINDPM_THRESHOLD_MAX) {
		offset = VINDPM_OS_10500mV;
		offset_val = SGM41542_VINDPM_THRESHOLD_10500MV;
	}

	/*set input os*/
	rc = sgm41542_config_interface(chip, REG0F_SGM41542_ADDRESS,
			offset,
			REG0F_SGM41542_VINDPM_THRESHOLD_OFFSET_MASK);

	/*set input vindpm*/
	tmp = (vindpm - offset_val) / REG06_SGM41542_VINDPM_STEP_MV;
	/* Adjust vindpm to add 100mv when less than 100mv,solve the ic sleep issue when vindpm is close to the Vbat */
	if (((vindpm - offset_val) % REG06_SGM41542_VINDPM_STEP_MV) && (offset_val == SGM41542_VINDPM_THRESHOLD_3900MV))
		tmp++;

	rc = sgm41542_config_interface(chip, REG06_SGM41542_ADDRESS,
			tmp << REG06_SGM41542_VINDPM_SHIFT,
			REG06_SGM41542_VINDPM_MASK);

	if (rc < 0)
		chg_err("%s failed, rc=%d\n", __func__, rc);

	if (vindpm == REG06_SGM41542_VINDPM_MAX)
		schedule_delayed_work(&chip->vindpm_recovery_work, msecs_to_jiffies(VINDPM_RECOVERY_DELAY_MS));


	return rc;
}

int sgm41542_usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 2200, 2500, 2750, 3000,
};

static int sgm41542_get_usb_icl(void)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	rc = sgm41542_read_reg(chip, REG00_SGM41542_ADDRESS, &tmp);
	if (rc) {
		chg_err("Couldn't read REG00_SGM41542_ADDRESS rc = %d\n", rc);
		return 0;
	}
	tmp = (tmp & REG00_SGM41542_INPUT_CURRENT_LIMIT_MASK) >> REG00_SGM41542_INPUT_CURRENT_LIMIT_SHIFT;
	return (tmp * REG00_SGM41542_INPUT_CURRENT_LIMIT_STEP + REG00_SGM41542_INPUT_CURRENT_LIMIT_OFFSET);
}

int sgm41542_input_current_limit_without_aicl(int current_ma)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("in suspend\n");
		return 0;
	}

	if (current_ma > REG00_SGM41542_INPUT_CURRENT_LIMIT_MAX)
		current_ma = REG00_SGM41542_INPUT_CURRENT_LIMIT_MAX;

	if (current_ma < REG00_SGM41542_INPUT_CURRENT_LIMIT_OFFSET)
		current_ma = REG00_SGM41542_INPUT_CURRENT_LIMIT_OFFSET;

	tmp = (current_ma - REG00_SGM41542_INPUT_CURRENT_LIMIT_OFFSET) / REG00_SGM41542_INPUT_CURRENT_LIMIT_STEP;
	chg_err("tmp current [%d]ma\n", current_ma);
	rc = sgm41542_config_interface(chip, REG00_SGM41542_ADDRESS,
			tmp << REG00_SGM41542_INPUT_CURRENT_LIMIT_SHIFT,
			REG00_SGM41542_INPUT_CURRENT_LIMIT_MASK);

	if (rc < 0) {
		chg_err("Couldn't set aicl rc = %d\n", rc);
	}

	if (current_ma <= 100)
		sgm41542_set_vindpm_vol(REG06_SGM41542_VINDPM_MAX);
	else
		sgm41542_set_vindpm_vol(chip->hw_aicl_point);

	return rc;
}

int sgm41542_chg_get_dyna_aicl_result(void)
{
	return aicl_result;
}

#define BATT_VOL_4V14	4140
#define BATT_VOL_4V30	4300
#define SGM41542_INP_VOL_4V44	4440
#define SGM41542_INP_VOL_4V5	4500
#define SGM41542_INP_VOL_4V52	4520
#define SGM41542_INP_VOL_4V535	4535
#define SGM41542_INP_VOL_4V65	4650
#define SGM41542_INP_VOL_4V665	4665
static void sgm41545d_set_aicl_point(int vbatt)
{
	struct chip_sgm41542 *chip = charger_ic;
	static int pre_hw_aicl_point = 0;
	if (!chip)
		return;
	if (vbatt > BATT_VOL_4V14) {
		chip->hw_aicl_point = SGM41542_INP_VOL_4V52;
		chip->sw_aicl_point = SGM41542_INP_VOL_4V535;
	} else {
		chip->hw_aicl_point = SGM41542_INP_VOL_4V44;
		chip->sw_aicl_point = SGM41542_INP_VOL_4V5;
	}
	if (chip->hw_aicl_point != pre_hw_aicl_point) {
		pre_hw_aicl_point = chip->hw_aicl_point;
		sgm41542_set_vindpm_vol(chip->hw_aicl_point);
		chg_err("pre_hw_aicl_point:%d, hw_aicl_point:%d\n", pre_hw_aicl_point, chip->hw_aicl_point);
	}
}
void sgm41542_set_aicl_point(int vbatt)
{
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip)
		return;

	if (chip->part_id == REG0B_SGM41515D_PART_ID
		|| chip->part_id == REG0B_SGM41512SD_PART_ID) {
		sgm41545d_set_aicl_point(vbatt);
		return;
	}

	if (chip->hw_aicl_point == 4440 && vbatt > 4140) {
		chip->hw_aicl_point = 4520;
		chip->sw_aicl_point = 4535;
		sgm41542_set_vindpm_vol(chip->hw_aicl_point);
	} else if (chip->hw_aicl_point == 4520 && vbatt < 4000) {
		chip->hw_aicl_point = 4440;
		chip->sw_aicl_point = 4500;
		sgm41542_set_vindpm_vol(chip->hw_aicl_point);
	}

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->set_aicl_point)
			g_oplus_chip->sub_chg_ops->set_aicl_point(vbatt);
	}
}

int sgm41542_get_charger_vol(void)
{
	return qpnp_get_prop_charger_voltage_now();
}

int sgm41542_get_vbus_voltage(void)
{
	return qpnp_get_prop_charger_voltage_now();
}

int sgm41542_input_current_limit_write(int current_ma)
{
	int i = 0, rc = 0;
	int chg_vol = 0;
	int sw_aicl_point = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();
	int pre_icl_index = 0, pre_icl = 0;
	int main_cur = 0;
	int slave_cur = 0;

	if (!chip || !g_oplus_chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (atomic_read(&chip->is_suspended) == 1) {
		chg_err("suspend,ignore set current=%dmA\n", current_ma);
		return 0;
	}

	if (chip->charger_current_pre == current_ma) {
		chg_err("charger_current_pre = %d.\n", current_ma);
		return 0;
	} else {
		chip->charger_current_pre = current_ma;
	}

	for (pre_icl_index = ARRAY_SIZE(sgm41542_usb_icl) - 1; pre_icl_index > 0; pre_icl_index--) {
		if (sgm41542_usb_icl[pre_icl_index] <= current_ma) {
			break;
		}
	}
	i = pre_icl_index;
	chg_err("sgm41542_usb_icl[%d]: %d, current_ma=%d, is_hvdcp=%d\n", i, sgm41542_usb_icl[i], current_ma, chip->is_hvdcp);

	if ((g_oplus_chip->temperature < 0 || g_oplus_chip->temperature > 420) && chip->slave_chg_en) {
		chip->slave_chg_en = false;
	}


	/*first: icl down to 500mA, step from pre icl*/
	pre_icl = sgm41542_get_usb_icl();
	for (pre_icl_index = ARRAY_SIZE(sgm41542_usb_icl) - 1; pre_icl_index >= 0; pre_icl_index--) {
		if (sgm41542_usb_icl[pre_icl_index] < pre_icl) {
			break;
		}
	}
	chg_err("icl_set: %d, pre_icl: %d, pre_icl_index: %d\n", current_ma, pre_icl, pre_icl_index);

	for (i = pre_icl_index; i > 1; i--) {
		rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
		if (rc) {
			chg_err("icl_down: set icl to %d mA fail, rc=%d\n", sgm41542_usb_icl[i], rc);
		} else {
			chg_err("icl_down: set icl to %d mA\n", sgm41542_usb_icl[i]);
		}
		msleep(50);
	}

	/*second: aicl process, step from 500ma*/
	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	/*if (g_oplus_chip->dual_charger_support) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->charging_disable)
		rc = g_oplus_chip->sub_chg_ops->charging_disable();
		if (rc < 0) {
			chg_err("disable sub charging failed");
		}
		chg_debug("disable subchg ");
	}*/

	chg_debug("usb input max current limit=%d", current_ma);
	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		chg_vol = sgm41542_get_vbus_voltage();
		if (chg_vol > AICL_POINT_VOL_9V) {
			sw_aicl_point = AICL_POINT_VOL_9V;
		} else {
			if (g_oplus_chip->batt_volt > AICL_POINT_VOL_5V_LOW)
				sw_aicl_point = SW_AICL_POINT_VOL_5V_PHASE2;
			else
				sw_aicl_point = SW_AICL_POINT_VOL_5V_PHASE1;
		}
	} else {
		sw_aicl_point = chip->sw_aicl_point;
	}

	i = 1; /* 500 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		chg_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1350) {
		i = i - 1; /*We use 1.2A here*/
		goto aicl_end;
	}

	i = 5; /* 1500 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; /*We use 1.2A here*/
		goto aicl_end;
	} else if (current_ma < 2000) {
		goto aicl_end;
	}

	i = 6; /* 1750 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /*1.2*/
		goto aicl_pre_step;
	}

	i = 7; /* 2000 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*1.5*/
		goto aicl_pre_step;
	} else if (current_ma < 2200) {
		goto aicl_end;
	}

	i = 8; /* 2200 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*1.75*/
		goto aicl_pre_step;
	} else if (current_ma < 2500) {
		goto aicl_end;
	}

	i = 9; /* 2500 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*2.0*/
		goto aicl_pre_step;
	} else if (current_ma < 2750) {
		goto aicl_end;
	}

	i = 10; /* 2750 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*2.2*/
		goto aicl_pre_step;
	} else if (current_ma < 3000) {
		goto aicl_end;
	}

	i = 11; /* 3000 */
	rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41542_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2;/*2.5*/
		goto aicl_pre_step;
	} else if (current_ma >= 3000) {
		goto aicl_end;
	}
aicl_pre_step:
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, sgm41542_usb_icl[i], sw_aicl_point);
	goto aicl_rerun;
aicl_end:
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, sgm41542_usb_icl[i], sw_aicl_point);
	goto aicl_rerun;
aicl_rerun:

	if (g_oplus_chip->dual_charger_support
		&& (g_oplus_chip->slave_charger_enable || chip->slave_chg_en)) {
		slave_cur = (sgm41542_usb_icl[i] * g_oplus_chip->slave_pct)/100;
		main_cur = sgm41542_usb_icl[i] - slave_cur;

		if (g_oplus_chip && g_oplus_chip->mmi_chg == 0){
			chg_err("set icl 100 when mmi_chg = %d\n",g_oplus_chip->mmi_chg);
			rc = sgm41542_input_current_limit_without_aicl(100);
			chip->charger_current_pre = 100;
		}else{
			rc = sgm41542_input_current_limit_without_aicl(main_cur);
			chip->charger_current_pre = main_cur;
		}
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->input_current_write) {
			g_oplus_chip->sub_chg_ops->input_current_write(slave_cur);
		}

		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->charging_enable) {
			g_oplus_chip->sub_chg_ops->charging_enable();
		}

		chg_err("aicl: main_cur: %d, slave_cur: %d, slave_pct:%d", main_cur, slave_cur, g_oplus_chip->slave_pct);
	} else {
		aicl_result = sgm41542_usb_icl[i];
		if (g_oplus_chip && g_oplus_chip->mmi_chg == 0){
			chg_err("set icl 100 when mmi_chg = %d\n",g_oplus_chip->mmi_chg);
			rc = sgm41542_input_current_limit_without_aicl(100);
			chip->charger_current_pre = 100;
		}else{
			rc = sgm41542_input_current_limit_without_aicl(sgm41542_usb_icl[i]);
			chip->charger_current_pre = sgm41542_usb_icl[i];
		}
		if (g_oplus_chip != NULL && g_oplus_chip->dual_charger_support
			&& (!g_oplus_chip->slave_charger_enable && !chip->slave_chg_en)) {
			if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->charging_disable) {
				g_oplus_chip->sub_chg_ops->charging_disable();
			}
		}
	}

	if (!g_oplus_chip->cool_down_force_5v && chip->is_hvdcp && (sgm41542_get_vbus_voltage() > AICL_POINT_VOL_9V)) {
		chip->sw_aicl_point = AICL_POINT_VOL_9V;
		rc = sgm41542_set_vindpm_vol(chip->sw_aicl_point);
	} else {
		if (chip->part_id == REG0B_SGM41515D_PART_ID || chip->part_id == REG0B_SGM41512SD_PART_ID)
			sgm41542_set_aicl_point(oplus_chg_get_batt_volt());
		else
			rc = sgm41542_set_vindpm_vol(chip->hw_aicl_point);
	}

	return rc;
}

int sgm41542_input_current_limit_ctrl_by_vooc_write(int current_ma)
{
	int rc = 0;
	int cur_usb_icl  = 0;
	int temp_curr = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (atomic_read(&chip->is_suspended) == 1) {
		chg_err("suspend,ignore set current=%dmA\n", current_ma);
		return 0;
	}

	cur_usb_icl = sgm41542_get_usb_icl();
	chg_err(" get cur_usb_icl = %d\n", cur_usb_icl);

	if (current_ma > cur_usb_icl) {
		for (temp_curr = cur_usb_icl; temp_curr < current_ma; temp_curr += 500) {
			msleep(35);
			rc = sgm41542_input_current_limit_without_aicl(temp_curr);
			chg_err("[up] set input_current = %d\n", temp_curr);
		}
	} else {
		for (temp_curr = cur_usb_icl; temp_curr > current_ma; temp_curr -= 500) {
			msleep(35);
			rc = sgm41542_input_current_limit_without_aicl(temp_curr);
			chg_err("[down] set input_current = %d\n", temp_curr);
		}
	}

	rc = sgm41542_input_current_limit_without_aicl(current_ma);
	return rc;
}

static ssize_t sgm41542_access_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct chip_sgm41542 *chip = dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}
	return sprintf(buf, "0x%02x\n", chip->reg_access);
}

static ssize_t sgm41542_access_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct chip_sgm41542 *chip = dev_get_drvdata(dev);
	int ret = 0;
	char *pvalue = NULL, *addr, *val;
	unsigned int reg_value = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	if (buf != NULL && size != 0) {
		pr_info("[%s] buf is %s and size is %zu\n", __func__, buf, size);

		pvalue = (char *)buf;
		if (size > 3) {
			addr = strsep(&pvalue, " ");
			ret = kstrtou32(addr, 16, (unsigned int *)&chip->reg_access);
		} else
			ret = kstrtou32(pvalue, 16, (unsigned int *)&chip->reg_access);

		if (size > 3) {
			val = strsep(&pvalue, " ");
			ret = kstrtou32(val, 16, (unsigned int *)&reg_value);
			pr_err("[%s] write sgm41542 reg 0x%02x with value 0x%02x !\n",
					__func__, (unsigned int) chip->reg_access, reg_value);
			ret = sgm41542_write_reg(chip, chip->reg_access, reg_value);
		} else {
			ret = sgm41542_read_reg(chip, chip->reg_access, &reg_value);
			pr_err("[%s] read sgm41542 reg 0x%02x with value 0x%02x !\n",
					__func__, (unsigned int) chip->reg_access, reg_value);
		}
	}
	return size;
}

static DEVICE_ATTR(sgm41542_access, 0660, sgm41542_access_show, sgm41542_access_store);

void sgm41542_dump_registers(void)
{
	int ret = 0;
	int addr = 0;
	int val_buf[SGM41542_REG_NUMBER] = {0x0};
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip || !g_oplus_chip)
		return;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return;
	}

	for(addr = SGM41542_FIRST_REG; addr <= SGM41542_LAST_REG; addr++) {
		ret = sgm41542_read_reg(chip, addr, &val_buf[addr]);
		if (ret) {
			chg_err("Couldn't read 0x%02x ret = %d\n", addr, ret);
		}
	}

	chg_err("[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
			"[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x]\n",
			val_buf[0], val_buf[1], val_buf[2], val_buf[3],
			val_buf[4], val_buf[5], val_buf[6], val_buf[7],
			val_buf[8], val_buf[9], val_buf[10], val_buf[11],
			val_buf[12], val_buf[13], val_buf[14], val_buf[15]
			);

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->dump_registers) {
			g_oplus_chip->sub_chg_ops->dump_registers();
		}
	}

}

int sgm41542_kick_wdt(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG01_SGM41542_ADDRESS,
			REG01_SGM41542_WDT_TIMER_RESET,
			REG01_SGM41542_WDT_TIMER_RESET_MASK);
	if (rc) {
		chg_err("Couldn't sgm41542 kick wdt rc = %d\n", rc);
	}

	return rc;
}

int sgm41542_set_wdt_timer(int reg)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG05_SGM41542_ADDRESS,
			reg,
			REG05_SGM41542_WATCHDOG_TIMER_MASK);
	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}

	return 0;
}

static void sgm41542_wdt_enable(bool wdt_enable)
{
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return;

	if (wdt_enable)
		sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_40S);
	else
		sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_DISABLE);

	chg_err("sgm41542_wdt_enable[%d]\n", wdt_enable);
}

int sgm41542_set_stat_dis(bool enable)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG00_SGM41542_ADDRESS,
			enable ? REG00_SGM41542_STAT_DIS_ENABLE : REG00_SGM41542_STAT_DIS_DISABLE,
			REG00_SGM41542_STAT_DIS_MASK);
	if (rc) {
		chg_err("Couldn't sgm41542 set_stat_dis rc = %d\n", rc);
	}

	return rc;
}

int sgm41542_set_int_mask(int val)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG0A_SGM41542_ADDRESS,
			val,
			REG0A_SGM41542_VINDPM_INT_MASK | REG0A_SGM41542_IINDPM_INT_MASK);
	if (rc) {
		chg_err("Couldn't sgm41542 set_int_mask rc = %d\n", rc);
	}

	return rc;
}

int sgm41542_set_chg_timer(bool enable)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG05_SGM41542_ADDRESS,
			enable ? REG05_SGM41542_CHG_SAFETY_TIMER_ENABLE : REG05_SGM41542_CHG_SAFETY_TIMER_DISABLE,
			REG05_SGM41542_CHG_SAFETY_TIMER_MASK);
	if (rc) {
		chg_err("Couldn't sgm41542 set_chg_timer rc = %d\n", rc);
	}

	return rc;
}

bool sgm41542_get_bus_gd(void)
{
	int rc = 0;
	int reg_val = 0;
	bool bus_gd = false;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_read_reg(chip, REG0A_SGM41542_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read regeister, rc = %d\n", rc);
		return false;
	}

	bus_gd = ((reg_val & REG0A_SGM41542_BUS_GD_MASK) == REG0A_SGM41542_BUS_GD_YES) ? 1 : 0;
	return bus_gd;
}

bool sgm41542_get_iindpm_stat(void)
{
	int rc = 0;
	int reg_val = 0;
	bool iindpm_stat = false;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_read_reg(chip, REG0A_SGM41515D_ADDRESS, &reg_val);
	if (rc) {
		chg_err("%s,Couldn't read regeister, rc = %d\n", __func__,rc);
		return false;
	}

	iindpm_stat = ((reg_val & REG0A_SGM41515D_IINDPM_STAT_MASK) == REG0A_SGM41515D_IINDPM_STAT_DET) ? 1 : 0;
	return iindpm_stat;
}

bool sgm41542_get_power_gd(void)
{
	int rc = 0;
	int reg_val = 0;
	bool power_gd = false;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_read_reg(chip, REG08_SGM41542_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't get_power_gd rc = %d\n", rc);
		return false;
	}

	power_gd = ((reg_val & REG08_SGM41542_POWER_GOOD_STAT_MASK) == REG08_SGM41542_POWER_GOOD_STAT_GOOD) ? 1 : 0;
	return power_gd;
}

static bool sgm41542_chg_is_usb_present(void)
{
	if (oplus_vooc_get_fastchg_started() == true || oplus_chg_get_wait_for_ffc_flag() == true) {
		chg_err("[%s]:svooc/vooc already started!\n", __func__);
		return true;
	} else {
#if IS_ENABLED(CONFIG_USB_SC27XX_TYPEC)
		if (oplus_chg_typec_get_usb_present() && sgm41542_get_bus_gd())
			return true;
		else
			return false;
#else
		return sgm41542_get_bus_gd();
#endif
	}
}

#define REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_STEP_HALF (REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_STEP/2)
int sgm41542_set_charging_current(int chg_cur)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41542 *chip = charger_ic;

	chg_debug("mian ichg = %d\n", chg_cur);

	tmp = chg_cur - REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_OFFSET;
	tmp = (tmp / REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_STEP) +
		((tmp%REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_STEP > REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_STEP_HALF)?1:0);

	rc = sgm41542_config_interface(chip, REG02_SGM41542_ADDRESS,
			tmp << REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_SHIFT,
			REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_MASK);

	return rc;
}

#define MAIN_ICHG_MAX 2500
#define MAIN_ICHG_OFFSET 250
int sgm41542_fast_offset_ma[] = {
	0, 150, 200, 250, 300, 350, 370,
};
int sgm41542_fast_limit_ma[] = {
	1000, 1700, 2100, 2500, 2900, 3200, 3300,
};
int sgm45142_fastcharge_offset_select(int chg_cur)
{
	int limit_ma_index = 0;
	int offset_ma = 0;

	for (limit_ma_index = 0; limit_ma_index <= (ARRAY_SIZE(sgm41542_fast_limit_ma) - 1); limit_ma_index++) {
		if (sgm41542_fast_limit_ma[limit_ma_index] >= chg_cur) {
			break;
		}
	}

	if((limit_ma_index >= 0) && (limit_ma_index < (ARRAY_SIZE(sgm41542_fast_limit_ma)))){
		offset_ma = sgm41542_fast_offset_ma[limit_ma_index];
	}

	chg_err("offset_ma= %d, limit_ma_index=%d",offset_ma, limit_ma_index);
	return offset_ma;
}

int sgm41542_charging_current_write_fast(int chg_cur)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();
	int main_cur = 0;
	int slave_cur = 0;
	int expect_cur = 0;
	int offset_ma = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	expect_cur = chg_cur < chip->ap_charge_current ? chg_cur : chip->ap_charge_current;

	chg_err("set expect_cur=%d, chg_cur=%d, ap_charge_current=%d\n", expect_cur, chg_cur, chip->ap_charge_current);

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)
		&& (g_oplus_chip->slave_charger_enable || chip->slave_chg_en)) {
		/* main && slave ichg offset select*/
		offset_ma = sgm45142_fastcharge_offset_select(expect_cur);
		if(offset_ma >= MAIN_ICHG_OFFSET) {
			expect_cur = expect_cur + offset_ma;
			offset_ma = 0;
		}

		/* main ichg */
		main_cur = (expect_cur * (100 - g_oplus_chip->slave_pct))/100;
		rc = sgm41542_set_charging_current(main_cur + offset_ma);
		if (rc < 0) {
			chg_err("set main_cur:%d fail\n", main_cur);
		}

		/* slave ichg */
		slave_cur = expect_cur - main_cur;
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->charging_current_write_fast) {
			rc = g_oplus_chip->sub_chg_ops->charging_current_write_fast(slave_cur);
			if (rc) {
				chg_err("set slave_cur:%d failed", slave_cur);
			}
		}
		chg_err("set expect_cur:%d offset_ma:%d main_cur:%d slave_cur:%d\n", expect_cur, offset_ma, main_cur, slave_cur);
	} else {
		/* main ichg */
	if (chip->part_id == REG0B_SGM41515D_PART_ID || chip->part_id == REG0B_SGM41512SD_PART_ID) {
		chg_err("sgm41515d set charge current = %d\n", chg_cur);
		if (chg_cur <= SGM41515D_SET_CURRENT_40MA)
			tmp = chg_cur / SGM41515D_SET_CURRENT_STEP_5MA;
		else if (chg_cur <= SGM41515D_SET_CURRENT_110MA)
			tmp = SGM41515D_SET_CURRENT_REG_VAL_40MA +
			      (chg_cur - SGM41515D_SET_CURRENT_40MA) /
			      SGM41515D_SET_CURRENT_STEP_10MA;
		else if (chg_cur <= SGM41515D_SET_CURRENT_270MA)
			tmp = SGM41515D_SET_CURRENT_REG_VAL_110MA +
			      (chg_cur - SGM41515D_SET_CURRENT_110MA) /
			      SGM41515D_SET_CURRENT_STEP_20MA;
		else if (chg_cur <= SGM41515D_SET_CURRENT_540MA)
			tmp = SGM41515D_SET_CURRENT_REG_VAL_270MA +
			      (chg_cur - SGM41515D_SET_CURRENT_270MA) /
			      SGM41515D_SET_CURRENT_STEP_30MA;
		else if (chg_cur <= SGM41515D_SET_CURRENT_1500MA)
			tmp = SGM41515D_SET_CURRENT_REG_VAL_540MA +
			      (chg_cur - SGM41515D_SET_CURRENT_540MA) /
			      SGM41515D_SET_CURRENT_STEP_60MA;
		else if (chg_cur <= SGM41515D_SET_CURRENT_2940MA)
			tmp = SGM41515D_SET_CURRENT_REG_VAL_1500MA +
			      (chg_cur - SGM41515D_SET_CURRENT_1500MA) /
			      SGM41515D_SET_CURRENT_STEP_120MA;
		else
			tmp = SGM41515D_SET_CURRENT_REG_VAL_3000MA;
	} else {
		chg_err("set charge current = %d\n", chg_cur);
		tmp = chg_cur - REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_OFFSET;
		tmp = tmp / REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_STEP;
	}
	rc = sgm41542_config_interface(chip, REG02_SGM41542_ADDRESS,
			tmp << REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_SHIFT,
			REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_MASK);
	}

	return rc;
}

#define VREG_FINE_TUNING_20_MV	20
#define VREG_FINE_TUNING_4_MV	4
int sgm41542_vreg_ft_write(int ft_val)
{
	int tmp_delta = 0, tmp = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (ft_val > (REG04_SGM41542_CHG_VOL_LIMIT_STEP
		- VREG_FINE_TUNING_4_MV)) { /*over29~31mv, vreg=step+1*/
		tmp_delta = 1;
		tmp = 0;
	} else if (ft_val > VREG_FINE_TUNING_20_MV) { /*over21~28mv,vreg=(step+1)-8mv*/
		tmp_delta = 1;
		tmp = 2;
	} else if (ft_val > (REG04_SGM41542_CHG_VOL_LIMIT_STEP
		- VREG_FINE_TUNING_20_MV)) { /*over13~20mv,vreg=(step+1)-16mv*/
		tmp_delta = 1;
		tmp = 3;
	} else if (ft_val > VREG_FINE_TUNING_4_MV) { /*over5~12mv,vreg+8mv*/
		tmp_delta = 0;
		tmp = 1;
	} else { /*over1~4mv,vreg unchanged*/
		tmp_delta = 0;
		tmp = 0;
	}

	chg_err("get ft_val=%d, tmp_delta=%d, tmp=%d\n", ft_val, tmp_delta, tmp);
	/*set vreg-ft*/
	sgm41542_config_interface(chip, REG0F_SGM41542_ADDRESS,
		tmp << REG0F_SGM41542_VREG_FT_LIMIT_SHIFT,
		REG0F_SGM41542_VREG_FT_LIMIT_MASK);

	return tmp_delta;
}

int sgm41542_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	int tmp = 0, tmp_delta = 0;
	int ft_val = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("vfloat_mv = %d\n", vfloat_mv);

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->float_voltage_write)
		g_oplus_chip->sub_chg_ops->float_voltage_write(vfloat_mv);
	}

	if (chip->is_bq25601d) {
		tmp = vfloat_mv - REG04_BQ25601D_CHG_VOL_LIMIT_OFFSET;
	} else {
		tmp = vfloat_mv - REG04_SGM41542_CHG_VOL_LIMIT_OFFSET;
	}
	ft_val = tmp % REG04_SGM41542_CHG_VOL_LIMIT_STEP;
	tmp = tmp / REG04_SGM41542_CHG_VOL_LIMIT_STEP;

	if ((chip->support_vreg_ft == true) && (ft_val > 0)) {
		tmp_delta = sgm41542_vreg_ft_write(ft_val);
		tmp = tmp + tmp_delta;
	}
	chg_err("final tmp = %d\n", tmp);
	rc = sgm41542_config_interface(chip, REG04_SGM41542_ADDRESS,
			tmp << REG04_SGM41542_CHG_VOL_LIMIT_SHIFT,
			REG04_SGM41542_CHG_VOL_LIMIT_MASK);

	return rc;
}

int sgm41542_set_termchg_current(int term_curr)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->term_current_set)
		g_oplus_chip->sub_chg_ops->term_current_set(term_curr + 200);
	}
	if (chip->part_id == REG0B_SGM41515D_PART_ID || chip->part_id == REG0B_SGM41512SD_PART_ID) {
		if(term_curr > SGM41515D_ITERM_CURRENT_STABLE[ITERM_CURRENT_STABLE_LEN - 1])
			term_curr = SGM41515D_ITERM_CURRENT_STABLE[ITERM_CURRENT_STABLE_LEN - 1];
		for (tmp = 1; tmp < 16 && term_curr >= SGM41515D_ITERM_CURRENT_STABLE[tmp]; tmp++) {
			chg_err("%s:tmp = %d check next !\n", __func__, tmp);
		}
		tmp--;
		chg_err("sgm41515d term_current = %d tmp = %d\n", term_curr, tmp);
	} else {
		chg_err("term_current = %d\n", term_curr);
		tmp = term_curr - REG03_SGM41542_TERM_CHG_CURRENT_LIMIT_OFFSET;
		tmp = tmp / REG03_SGM41542_TERM_CHG_CURRENT_LIMIT_STEP;
	}
	rc = sgm41542_config_interface(chip, REG03_SGM41542_ADDRESS,
			tmp << REG03_SGM41542_TERM_CHG_CURRENT_LIMIT_SHIFT,
			REG03_SGM41542_TERM_CHG_CURRENT_LIMIT_MASK);
	return 0;
}

int sgm41542_otg_ilim_set(int ilim)
{
	int rc;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG02_SGM41542_ADDRESS,
			ilim,
			REG02_SGM41542_OTG_CURRENT_LIMIT_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_otg_ilim_set  rc = %d\n", rc);
	}

	return rc;
}

int sgm41542_otg_enable(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_DISABLE);
	if (chip->part_id == REG0B_SGM41515D_PART_ID
		|| chip->part_id == REG0B_SGM41512SD_PART_ID)
		rc = sgm41542_otg_ilim_set(REG02_SGM41515D_OTG_CURRENT_LIMIT_1200MA);
	else
		rc = sgm41542_otg_ilim_set(REG02_SGM41542_OTG_CURRENT_LIMIT_1200MA);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_otg_ilim_set rc = %d\n", rc);
	}

	rc = sgm41542_config_interface(chip, REG01_SGM41542_ADDRESS,
			REG01_SGM41542_OTG_ENABLE,
			REG01_SGM41542_OTG_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_otg_enable  rc = %d\n", rc);
	}

	if (chip->part_id == REG0B_SGM41515D_PART_ID
		|| chip->part_id == REG0B_SGM41512SD_PART_ID) {
		oplus_chg_set_otg_online(true);
	}

	chg_err("sgm41542_otg_enable\n");

	schedule_delayed_work(&chip->otg_iindpm_work, round_jiffies_relative(msecs_to_jiffies(300)));

	return rc;
}

int sgm41542_otg_disable(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG01_SGM41542_ADDRESS,
			REG01_SGM41542_OTG_DISABLE,
			REG01_SGM41542_OTG_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_otg_disable rc = %d\n", rc);
	}

	if (chip->part_id == REG0B_SGM41515D_PART_ID || chip->part_id == REG0B_SGM41512SD_PART_ID) {
		oplus_chg_set_otg_online(false);
	}

	chg_err("sgm41542_otg_disable\n");

	sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_DISABLE);
	cancel_delayed_work_sync(&chip->otg_iindpm_work);

	return rc;
}

static int sgm41542_enable_gpio(struct chip_sgm41542 *chip, bool enable)
{
	if (enable == true) {
		if (chip->enable_gpio > 0)
			gpio_direction_output(chip->enable_gpio, 0);
	} else {
		if (chip->enable_gpio > 0)
			gpio_direction_output(chip->enable_gpio, 1);
	}
	return 0;
}
int sgm41542_enable_charging(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41542_enable_gpio(chip, true);

	sgm41542_otg_disable();
	rc = sgm41542_config_interface(chip, REG01_SGM41542_ADDRESS,
			REG01_SGM41542_CHARGING_ENABLE,
			REG01_SGM41542_CHARGING_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_enable_charging rc = %d\n", rc);
	}
	chg_err("sgm41542_enable_charging\n");

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)
		&& g_oplus_chip->slave_charger_enable) {
			chg_err("enable slave charger.\n");
			if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->charging_enable) {
				rc = g_oplus_chip->sub_chg_ops->charging_enable();
			}
	}
    if (g_oplus_chip != NULL)
    {
		chg_err("slave_charger_enable=%d\n", g_oplus_chip->slave_charger_enable);
	}

	return rc;
}

int sgm41542_disable_charging(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41542_enable_gpio(chip, false);

	rc = sgm41542_config_interface(chip, REG01_SGM41542_ADDRESS,
			REG01_SGM41542_CHARGING_DISABLE,
			REG01_SGM41542_CHARGING_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_disable_charging rc = %d\n", rc);
	}

	chg_err("sgm41542_disable_charging \n");

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->charging_disable) {
			rc = g_oplus_chip->sub_chg_ops->charging_disable();
		}
	}

	return rc;
}

int sgm41542_check_charging_enable(void)
{
	int rc = 0;
	int reg_val = 0;
	struct chip_sgm41542 *chip = charger_ic;
	bool charging_enable = false;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_read_reg(chip, REG01_SGM41542_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG01_SGM41542_ADDRESS rc = %d\n", rc);
		return 0;
	}

	charging_enable = ((reg_val & REG01_SGM41542_CHARGING_MASK) == REG01_SGM41542_CHARGING_ENABLE) ? 1 : 0;

	return charging_enable;
}

int sgm41542_suspend_charger(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	atomic_set(&chip->is_suspended, 1);

	chip->before_suspend_icl = sgm41542_get_usb_icl();
	/*sgm41542_input_current_limit_without_aicl(100);*/
	rc = sgm41542_disable_charging();
	if ((g_oplus_chip != NULL) && (g_oplus_chip->mmi_chg == 0)){
		chg_err("set icl 100 when mmi_chg = %d\n",g_oplus_chip->mmi_chg);
		rc = sgm41542_input_current_limit_without_aicl(100);
		}

	chg_err("before_suspend_icl=%d\n", chip->before_suspend_icl);

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->input_current_write_without_aicl) {
			/*rc = g_oplus_chip->sub_chg_ops->input_current_write_without_aicl(100);*/
		}
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->charging_disable) {
			rc = g_oplus_chip->sub_chg_ops->charging_disable();
		}
		g_oplus_chip->slave_charger_enable = false;
		chip->slave_chg_en = false;
	}

	return rc;
}

int sgm41542_unsuspend_charger(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	atomic_set(&chip->is_suspended, 0);

	if(oplus_pps_get_ffc_started() != true){
		rc = sgm41542_enable_charging();
	}
	sgm41542_really_suspend_charger(false);

	chip->before_unsuspend_icl = sgm41542_get_usb_icl();
	if ((chip->before_unsuspend_icl == 0)
			|| (chip->before_suspend_icl == 0)
			|| (chip->before_unsuspend_icl != 100)
			|| (chip->before_unsuspend_icl == chip->before_suspend_icl)) {
		chg_err("ignore set icl [%d %d]\n", chip->before_suspend_icl, chip->before_unsuspend_icl);
	} else {
		if (g_oplus_chip->mmi_chg != 0){
			rc = sgm41542_input_current_limit_without_aicl(chip->before_suspend_icl);
			chip->charger_current_pre = chip->before_suspend_icl;
		} else {
			chg_err("set icl 100 when mmi_chg = %d\n",g_oplus_chip->mmi_chg);
			rc = sgm41542_input_current_limit_without_aicl(100);
			chip->charger_current_pre = 100;
		}
	}

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)
		&& g_oplus_chip->slave_charger_enable) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->charging_enable) {
			rc = g_oplus_chip->sub_chg_ops->charging_enable();
		}
	}

	return rc;
}

bool sgm41542_check_suspend_charger(void)
{
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	return atomic_read(&chip->is_suspended);
}

void sgm41542_really_suspend_charger(bool en)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip) {
		return;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return;
	}

	rc = sgm41542_config_interface(chip, REG00_SGM41542_ADDRESS,
			en ? REG00_SGM41542_SUSPEND_MODE_ENABLE : REG00_SGM41542_SUSPEND_MODE_DISABLE,
			REG00_SGM41542_SUSPEND_MODE_MASK);

	if (rc < 0) {
		chg_err("fail en=%d rc = %d\n", en, rc);
	}

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->really_suspend_charger) {
			g_oplus_chip->sub_chg_ops->really_suspend_charger(en);
		}
	}
}

bool sgm41542_check_really_suspend_charger(void)
{
	int rc = 0;
	int reg_val = 0;
	struct chip_sgm41542 *chip = charger_ic;
	bool really_suspend = false;

	if (!chip)
		return 0;

	rc = sgm41542_read_reg(chip, REG00_SGM41542_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG00_SGM41542_ADDRESS rc = %d\n", rc);
		return 0;
	}

	really_suspend = ((reg_val & REG00_SGM41542_SUSPEND_MODE_MASK) == REG00_SGM41542_SUSPEND_MODE_ENABLE) ? 1 : 0;

	return really_suspend;
}

int sgm41542_set_rechg_voltage(int recharge_mv)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG04_SGM41542_ADDRESS,
			recharge_mv,
			REG04_SGM41542_RECHG_THRESHOLD_VOL_MASK);

	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}

	return rc;
}

int sgm41542_reset_charger(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG0B_SGM41542_ADDRESS,
			REG0B_SGM41542_REG_RST_RESET,
			REG0B_SGM41542_REG_RST_MASK);

	if (rc) {
		chg_err("Couldn't sgm41542_reset_charger rc = %d\n", rc);
	}

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->reset_charger) {
			rc = g_oplus_chip->sub_chg_ops->reset_charger();
		}
	}

	return rc;
}

int sgm41542_registers_read_full(void)
{
	int rc = 0;
	int reg_full = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_read_reg(chip, REG08_SGM41542_ADDRESS, &reg_full);
	if (rc) {
		chg_err("Couldn't read REG08_SGM41542_ADDRESS rc = %d\n", rc);
		return 0;
	}

	reg_full = ((reg_full & REG08_SGM41542_CHG_STAT_MASK) == REG08_SGM41542_CHG_STAT_CHG_TERMINATION) ? 1 : 0;
	if (reg_full) {
		chg_err("the sgm41542 is full");
		sgm41542_dump_registers();
	}

	return rc;
}

int sgm41542_set_chging_term_disable(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG05_SGM41542_ADDRESS,
			REG05_SGM41542_TERMINATION_DISABLE,
			REG05_SGM41542_TERMINATION_MASK);
	if (rc) {
		chg_err("Couldn't set chging term disable rc = %d\n", rc);
	}

	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->set_charging_term_disable) {
			rc = g_oplus_chip->sub_chg_ops->set_charging_term_disable();
		}
	}

	return rc;
}

bool sgm41542_check_charger_resume(void)
{
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return false;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return false;
	}

	return true;
}

bool sgm41542_need_to_check_ibatt(void)
{
	return false;
}

int sgm41542_get_chg_current_step(void)
{
	return REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_STEP;
}

int sgm41542_set_prechg_voltage_threshold(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41542_config_interface(chip, REG01_SGM41542_ADDRESS,
			REG01_SGM41542_SYS_VOL_LIMIT_3400MV,
			REG01_SGM41542_SYS_VOL_LIMIT_MASK);

	return rc;
}

int sgm41542_set_prechg_current(int ipre_mA)
{
	int tmp = 0;
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	if (chip->part_id == REG0B_SGM41515D_PART_ID || chip->part_id == REG0B_SGM41512SD_PART_ID) {
		if(ipre_mA > SGM41515D_IPRECHG_CURRENT_STABLE[IPRECHG_CURRENT_STABLE_LEN - 1])
			ipre_mA = SGM41515D_IPRECHG_CURRENT_STABLE[IPRECHG_CURRENT_STABLE_LEN - 1];
		for (tmp = 1; tmp < 16 && ipre_mA > SGM41515D_IPRECHG_CURRENT_STABLE[tmp]; tmp++) {
			chg_err("%s:tmp = %d check next !\n", __func__, tmp);
		}
		tmp--;
		chg_err("sgm41515d prechg_current = %d tmp = %d\n", ipre_mA, tmp);
	} else {
		chg_err("prechg_current = %d\n", ipre_mA);
		tmp = ipre_mA - REG03_SGM41542_PRE_CHG_CURRENT_LIMIT_OFFSET;
		tmp = tmp / REG03_SGM41542_PRE_CHG_CURRENT_LIMIT_STEP;
	}
	rc = sgm41542_config_interface(chip, REG03_SGM41542_ADDRESS,
			(tmp + 1) << REG03_SGM41542_PRE_CHG_CURRENT_LIMIT_SHIFT,
			REG03_SGM41542_PRE_CHG_CURRENT_LIMIT_MASK);

	return 0;
}

int sgm41542_set_otg_voltage(void)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG06_SGM41542_ADDRESS,
			REG06_SGM41542_OTG_VLIM_5150MV,
			REG06_SGM41542_OTG_VLIM_MASK);

	return rc;
}

int sgm41542_set_ovp(int val)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG06_SGM41542_ADDRESS,
			val,
			REG06_SGM41542_OVP_MASK);

	return rc;
}

int sgm41542_get_vbus_stat(void)
{
	int rc = 0;
	int vbus_stat = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_read_reg(chip, REG08_SGM41542_ADDRESS, &vbus_stat);
	if (rc) {
		chg_err("Couldn't read REG08_SGM41542_ADDRESS rc = %d\n", rc);
		return 0;
	}

	vbus_stat = vbus_stat & REG08_SGM41542_VBUS_STAT_MASK;

	return vbus_stat;
}

int sgm41542_set_iindet(void)
{
	int rc;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_config_interface(chip, REG07_SGM41542_ADDRESS,
			REG07_SGM41542_IINDET_EN_MASK,
			REG07_SGM41542_IINDET_EN_FORCE_DET);
	if (rc < 0) {
		chg_err("Couldn't set REG07_SGM41542_IINDET_EN_MASK rc = %d\n", rc);
	}

	return rc;
}

int sgm41542_get_iindet(void)
{
	int rc = 0;
	int reg_val = 0;
	bool is_complete = false;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_read_reg(chip, REG07_SGM41542_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG07_SGM41542_ADDRESS rc = %d\n", rc);
		return false;
	}

	is_complete = ((reg_val & REG07_SGM41542_IINDET_EN_MASK) == REG07_SGM41542_IINDET_EN_DET_COMPLETE) ? 1 : 0;
	return is_complete;
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
				__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
				CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
				CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 110) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
				tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
				tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

void sgm41542_vooc_timeout_callback(bool vbus_rising)
{
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return;
	}

	if (!vbus_rising) {
		sgm41542_request_dpdm(chip, false);
		sgm41542_really_suspend_charger(false);
		chip->bc12_done = false;
          	oplus_chg_wakelock(chip, false);
	}
	sgm41542_dump_registers();
}

static int sgm41542_batfet_reset_disable(struct chip_sgm41542 *chip, bool enable)
{
	int rc = 0;
	int val = 0;

	if(enable) {
		val = SGM41542_BATFET_RST_DISABLE << REG07_SGM41542_BATFET_RST_EN_SHIFT;
	} else {
		val = SGM41542_BATFET_RST_ENABLE << REG07_SGM41542_BATFET_RST_EN_SHIFT;
	}

	rc = sgm41542_config_interface(chip, REG07_SGM41542_ADDRESS, val, REG07_SGM41542_BATFET_RST_EN_MASK);

	return rc;
}

int sgm41542_set_shipmode(bool enable)
{
	int rc = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (chip == NULL)
		return rc;

	if (enable) {
		rc = sgm41542_config_interface(chip, REG07_SGM41542_ADDRESS,
			REG07_SGM41542_BATFET_DIS_ON,
			REG07_SGM41542_BATFET_DIS_MASK);
		if (rc < 0) {
			chg_err("Couldn't set REG07_SGM41542_BATFET_DIS_ON rc = %d\n", rc);
		}
	} else {
		rc = sgm41542_config_interface(chip, REG07_SGM41542_ADDRESS,
			REG07_SGM41542_BATFET_DIS_OFF,
			REG07_SGM41542_BATFET_DIS_MASK);
		if (rc < 0) {
			chg_err("Couldn't set REG07_SGM41542_BATFET_DIS_OFF rc = %d\n", rc);
		}
	}

	return rc;
}


bool sgm41542_get_deivce_online(void)
{
	int rc = 0;
	int reg_val = 0;
	// int part_id = 0x00;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41542_read_reg(chip, REG0B_SGM41542_ADDRESS, &reg_val);
	if (rc) {
		rc = sgm41542_read_reg(chip, REG0B_SGM41542_ADDRESS, &reg_val);
		if (rc) {
			chg_err("Couldn't read REG0B_SGM41542_ADDRESS rc = %d\n", rc);
			return false;
		}
	}

	chip->part_id = (reg_val & REG0B_SGM41542_PN_MASK) >> SGM41542_DEVID_SHIFT;
	chg_err("sgm41542 part_id=0x%02X\n", chip->part_id);
/*
part id:
1100=SGM41541
1101=SGM41542
0001=SGM41515D
1110=SGM41512SD
*/
#ifdef CONFIG_OPLUS_CHARGER_UNISOC
	if (chip->part_id == 0x0c) {
		chip->buck_ic_name = "SGM41541_BUCK_CHG_IC";
	} else if (chip->part_id == 0x0d) {
		chip->buck_ic_name = "SGM41542_BUCK_CHG_IC";
	} else if (chip->part_id == REG0B_SGM41515D_PART_ID) {
		chip->buck_ic_name = "SGM41515D_BUCK_CHG_IC";
	} else if (chip->part_id == REG0B_SGM41512SD_PART_ID) {
		chip->buck_ic_name = "SGM41512SD_BUCK_CHG_IC";
	} else {
		chip->buck_ic_name = "not found";
	}
#endif
	if (chip->part_id == 0x0c || chip->part_id == 0x0d ||
		chip->part_id == REG0B_SGM41515D_PART_ID ||
		chip->part_id == REG0B_SGM41512SD_PART_ID)
		return true;

	return false;
}

int sgm41542_hardware_init(void)
{
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	chg_err("init sgm41542 hardware! \n");

	if (!chip) {
		return false;
	}

	/*must be before set_vindpm_vol and set_input_current*/
	chip->hw_aicl_point = 4440;
	chip->sw_aicl_point = 4500;


	sgm41542_set_stat_dis(false);
	sgm41542_set_int_mask(REG0A_SGM41542_VINDPM_INT_NOT_ALLOW | REG0A_SGM41542_IINDPM_INT_NOT_ALLOW);

	sgm41542_set_chg_timer(false);

	sgm41542_disable_charging();

	sgm41542_set_ovp(REG06_SGM41542_OVP_14P0V);

	sgm41542_set_chging_term_disable();

	sgm41542_float_voltage_write(WPC_TERMINATION_VOLTAGE);
	if (chip->part_id == REG0B_SGM41515D_PART_ID || chip->part_id == REG0B_SGM41512SD_PART_ID)
		sgm41542_otg_ilim_set(REG02_SGM41515D_OTG_CURRENT_LIMIT_1200MA);
	else
		sgm41542_otg_ilim_set(REG02_SGM41542_OTG_CURRENT_LIMIT_1200MA);

	sgm41542_set_prechg_voltage_threshold();

	sgm41542_set_prechg_current(WPC_PRECHARGE_CURRENT);

	sgm41542_charging_current_write_fast(REG02_SGM41542_FAST_CHG_CURRENT_LIMIT_2000MA);

	sgm41542_set_termchg_current(WPC_TERMINATION_CURRENT);

	sgm41542_input_current_limit_without_aicl(REG00_SGM41542_INIT_INPUT_CURRENT_LIMIT_500MA);

	chip->charger_current_pre = REG00_SGM41542_INIT_INPUT_CURRENT_LIMIT_500MA;
	sgm41542_set_rechg_voltage(WPC_RECHARGE_VOLTAGE_OFFSET);
	if (chip->part_id == REG0B_SGM41515D_PART_ID || chip->part_id == REG0B_SGM41512SD_PART_ID)
		sgm41542_set_aicl_point(oplus_chg_get_batt_volt());
	else
		sgm41542_set_vindpm_vol(chip->hw_aicl_point);
	sgm41542_set_otg_voltage();

	sgm41542_batfet_reset_disable(chip, chip->batfet_reset_disable);

	sgm41542_unsuspend_charger();

	sgm41542_enable_charging();

	sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_40S);


	if ((g_oplus_chip != NULL) && (g_oplus_chip->dual_charger_support)) {
		if (g_oplus_chip->sub_chg_ops && g_oplus_chip->sub_chg_ops->hardware_init) {
			g_oplus_chip->sub_chg_ops->hardware_init();
		}
	}


	return true;
}

static int sgm41542_get_charger_type(void)
{
	struct chip_sgm41542 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip || !g_oplus_chip)
		return POWER_SUPPLY_TYPE_UNKNOWN;

	return chip->oplus_charger_type;
}

static void sgm41542_update_chg_type_work_handler(struct work_struct *work)
{
	struct chip_sgm41542 *chip = container_of(to_delayed_work(work),
		struct chip_sgm41542, update_chg_type_work);

	if (!chip || !chip->notifier_registered)
		return;

	if ((chip->oplus_charger_type == POWER_SUPPLY_TYPE_USB ||
		chip->oplus_charger_type == POWER_SUPPLY_TYPE_USB_CDP) &&
		sprd_get_pd_type() == PD_ACTIVE) {
		oplus_chg_input_current_recheck_work();
		chg_info("update charging type");
	}
	return;
}

static int sgm41542_pd_notifier_call(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct chip_sgm41542 *chip = container_of(nb, struct chip_sgm41542, fchg_notify);
	struct power_supply *psy = data;

	if (!chip || !chip->notifier_registered || !psy || !psy->desc || !psy->desc->name)
		return NOTIFY_DONE;

	if (strcmp(psy->desc->name, SPRD_FCHG_TCPM_PD_NAME) == 0 &&
		event == PSY_EVENT_PROP_CHANGED) {
		schedule_delayed_work(&chip->update_chg_type_work, msecs_to_jiffies(20));
	}
	return NOTIFY_DONE;
}

static int oplus_get_max_current_from_fixed_pdo(int volt)
{
	struct sprd_tcpm_port *port;
	struct power_supply *psy;
	struct adapter_power_cap pd_source_cap;
	int i;
	int current_ma = 0;

	psy = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy) {
		chg_err("failed to get tcpm psy!\n");
		return 0;
	}

	port = power_supply_get_drvdata(psy);
	if (!port) {
		chg_err("failed to get tcpm port!\n");
		power_supply_put(psy);
		return 0;
	}

	sprd_tcpm_get_source_capabilities(port, &pd_source_cap);
	if (!pd_source_cap.nr_source_caps) {
		chg_err("failed to obtain the PD power supply capacity\n");
		power_supply_put(psy);
		return 0;
	}

	for (i = 0; i < pd_source_cap.nr_source_caps; i++) {
		if ((pd_source_cap.type[i] == SPRD_PDO_TYPE_FIXED) &&
			(pd_source_cap.max_mv[i] <= volt)) {
			current_ma = pd_source_cap.ma[i];
			break;
		}
	}

	power_supply_put(psy);
	return current_ma;
}

static int sgm41542_parse_dt(struct chip_sgm41542 *chip)
{
	int ret = 0;

	chip->irq_gpio = of_get_named_gpio(chip->client->dev.of_node, "sgm41542-irq-gpio", 0);
	if (!gpio_is_valid(chip->irq_gpio)) {
		chg_err("gpio_is_valid fail irq-gpio[%d]\n", chip->irq_gpio);
		return -EINVAL;
	}

	/*charger not need bc1.2 and irq service.*/
	ret = devm_gpio_request(chip->dev, chip->irq_gpio, "sgm41542-irq-gpio");
	if (ret) {
		chg_err("unable to request irq-gpio[%d]\n", chip->irq_gpio);
		return -EINVAL;
	}

	chg_err("irq-gpio[%d]\n", chip->irq_gpio);

	/*Get the charger enable gpio.*/
	chip->enable_gpio = of_get_named_gpio(chip->client->dev.of_node, "qcom,charger_enable-gpio", 0);
	if (!gpio_is_valid(chip->enable_gpio)) {
                chg_err("gpio_is_valid fail enable-gpio[%d]\n", chip->enable_gpio);
        } else {
		chg_err("enable-gpio[%d]\n", chip->enable_gpio);
		ret = gpio_request(chip->enable_gpio,
			                  "sgm41542-enable-gpio");
			if (ret) {
				chg_err("unable to request gpio [%d]\n",
				         chip->enable_gpio);
			}
	}
	chip->use_voocphy = of_property_read_bool(chip->client->dev.of_node, "qcom,use_voocphy");
	chg_err("use_voocphy=%d\n", chip->use_voocphy);

	chip->force_qc_5v = of_property_read_bool(chip->client->dev.of_node, "qcom,force_qc_5v");
	chg_err("force_qc_5v=%d\n", chip->force_qc_5v);
	chip->support_vreg_ft = of_property_read_bool(chip->client->dev.of_node, "oplus,support_vreg_ft");
	chg_err("support_vreg_ft=%d\n", chip->support_vreg_ft);

	return ret;
}

void sgm41542_force_dpdm(void)
{
	int val;
	struct chip_sgm41542 *chip = charger_ic;

	if (chip == NULL)
		return ;

    val = 0;
    sgm41542_config_interface(chip, REG0D_SGM41542_ADDRESS,
				  val, REG0D_SGM41542_DPDM_VSEL_MASK); /*dp/dm Hiz*/
}

int sgm41542_enable_qc20_hvdcp_9v(void)
{
	int ret = 0;
	int val, dp_val;
	struct chip_sgm41542 *chip = charger_ic;

	if (chip == NULL)
		return ret;

	/*dp and dm connected,dp 0.6V dm 0.6V*/
	dp_val = 0x2<<3;
	ret = sgm41542_config_interface(chip, REG0D_SGM41542_ADDRESS,
				  dp_val, REG0D_SGM41542_DP_VSEL_MASK); /*dp 0.6V*/
	if (ret)
		return ret;

	msleep(2500);
	sgm41542_input_current_limit_write(500);
	msleep(20);
	if ((chip->oplus_charger_type != POWER_SUPPLY_TYPE_USB_DCP)
		|| (chip->bc12_done == false))
		return 0;

	val = 0xe << 1;
	ret = sgm41542_config_interface(chip, REG0D_SGM41542_ADDRESS,
				  val, REG0D_SGM41542_DPDM_VSEL_MASK); /*dp 3.3v dm 0.6v*/

	return ret;
}

static int sgm41542_disable_hvdcp(void)
{
	int ret = 0, val;
	struct chip_sgm41542 *chip = charger_ic;

	if (chip == NULL)
		return 0;

	chg_debug("disable hvdcp\n");

	/*dp and dm connected,dp 0.6V dm Hiz*/
	val = 0x8 << 1;
	ret = sgm41542_config_interface(chip, REG0D_SGM41542_ADDRESS,
			val, REG0D_SGM41542_DPDM_VSEL_MASK);

	return ret;
}

static bool sgm41542_is_hvdcp(void)
{
	int ret = 0;
	bool hvdcp = false;
	int vol_mv = 0;
	struct chip_sgm41542 *chip = charger_ic;

	if (chip == NULL)
		return ret;

	vol_mv = sgm41542_get_vbus_voltage();

	if (vol_mv > 7500 && sprd_get_pd_type() != CHARGER_SUBTYPE_PD && oplus_vooc_get_fastchg_started() == false)
		hvdcp = true;

	chg_err("finn vbus = %dmV, hvdcp = %d\n", vol_mv, hvdcp);

	chip->is_hvdcp = hvdcp;
	return hvdcp;
}

#define SGM4154x_FAST_CHARGER_VOLTAGE		9000
#define SGM4154x_NORMAL_CHARGER_VOLTAGE		5000
static int sgm41542_adjust_qc_voltage(u32 value)
{
	int i = 0, cnt = 0, ret = 0;
	int val = 0xe << 1;
	struct chip_sgm41542 *chip = charger_ic;

	if (chip == NULL)
		return ret;

	if (value == SGM4154x_NORMAL_CHARGER_VOLTAGE) {
		ret = sgm41542_disable_hvdcp();
		chip->is_hvdcp = false;
		chg_err("adjust 5v success\n");
		return ret;
	} else if (value == SGM4154x_FAST_CHARGER_VOLTAGE) {
		for (i = 0; i < 10; i++) {
			if (sgm41542_is_hvdcp())
				break;
			msleep(100);
		}

		ret = sgm41542_config_interface(chip, REG0D_SGM41542_ADDRESS,
			  val, REG0D_SGM41542_DPDM_VSEL_MASK); /*dp 3.3v dm 0.6v*/

		if (!sgm41542_is_hvdcp())
		{
			do {
				sgm41542_force_dpdm();
				msleep(100);
				ret = sgm41542_enable_qc20_hvdcp_9v();
				msleep(200);

				if (sgm41542_is_hvdcp())
					break;

				if ((chip->oplus_charger_type != POWER_SUPPLY_TYPE_USB_DCP)
					|| (chip->bc12_done == false))
					break;

			} while (cnt++ < SGM41542_QC_RETRY_CNT);
		}

		if (chip->is_hvdcp) {
			chg_err("adjust 9v success\n");
		} else {
			oplus_chg_input_current_recheck_work();
			chg_err("adjust 9v failed\n");
		}

		return ret;
	}

	return ret;
}


static int sgm41542_set_qc_config(void)
{
	int ret = -1;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip || !g_oplus_chip) {
		chg_debug("oplus_chip is null\n");
		return ret;
	}

	if (!g_oplus_chip->calling_on && !g_oplus_chip->camera_on && g_oplus_chip->charger_volt < 6500 && g_oplus_chip->soc < 90
			&& g_oplus_chip->temperature <= 400 && g_oplus_chip->cool_down_force_5v == false && !btb_detect_over && chip->force_qc_5v == false) {
		printk(KERN_ERR "%s: set qc to 9V", __func__);
		ret = sgm41542_adjust_qc_voltage(9000);
	} else {
		if (g_oplus_chip->charger_volt > 7500 &&
				(g_oplus_chip->calling_on || g_oplus_chip->camera_on || g_oplus_chip->soc >= 90 || chip->force_qc_5v == true /* || g_oplus_chip->batt_volt >= 4424*/
				|| g_oplus_chip->temperature > 420 || g_oplus_chip->temperature < 0 || g_oplus_chip->cool_down_force_5v == true || btb_detect_over)) {
			printk(KERN_ERR "%s: set qc to 5V", __func__);
			sgm41542_set_vindpm_vol(SW_AICL_POINT_VOL_5V_PHASE1);
			ret = sgm41542_adjust_qc_voltage(5000);
			/*g_oplus_chip->slave_charger_enable = false;*/
			chip->slave_chg_en = false;
		}
	}

	if((chip->pre_is_hvdcp != chip->is_hvdcp) && chip->is_hvdcp)
		ret = 0;
	chip->pre_is_hvdcp = chip->is_hvdcp;

    return ret;
}

static void btb_temp_detect_over(void)
{
	int temp;
	int charger_current;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	temp = oplus_chg_get_battery_btb_temp_cal();

	if (!btb_detect_over && (temp >= 80)) {
		btb_detect_over = true;
		sgm41542_set_qc_config();
		oplus_chg_set_input_current_limit(g_oplus_chip);
		charger_current = oplus_chg_get_tbatt_normal_charging_current(g_oplus_chip);
		sgm41542_charging_current_write_fast(charger_current);
	}

	chg_err("btb:temp:%d, over:%d\n",temp, btb_detect_over);
}

int sgm41542_get_charger_subtype(void);

static void sgm41542_chg_protection_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sgm41542 *chip = container_of(dwork, struct chip_sgm41542, chg_protection_work);
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!g_oplus_chip) {
		chg_debug("oplus_chip is null\n");
		return;
	}

	if (!g_oplus_chip->charger_exist)
		return;

	btb_temp_detect_over();

	if (chip->use_voocphy == false) {
		if (!chip->qc_chging && (g_oplus_chip->soc < 90) && (chip->support_hvdcp == false) && (sgm41542_get_charger_subtype() != CHARGER_SUBTYPE_PD)) {
			chip->qc_chging = true;
			if (HVDCP_EXIT_ABNORMAL == chip->hvdcp_exit_stat) {
				chg_err("%s:HVDCP_EXIT_ABNORMAL not enable hvdcp \n", __func__);
				return;
			}
			schedule_delayed_work(&chip->qc_detect_work, round_jiffies_relative(msecs_to_jiffies(100)));
		}
	}

	schedule_delayed_work(&chip->chg_protection_work, round_jiffies_relative(msecs_to_jiffies(5000)));
}

static void sgm41542_otg_iindpm_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sgm41542 *chip = container_of(dwork, struct chip_sgm41542, otg_iindpm_work);
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	bool iindpm_stat = false;

	chg_debug("sgm41542_otg_iindpm_work 1\n");

	if (!g_oplus_chip) {
		chg_debug("oplus_chip is null\n");
		return;
	}

	if (!g_oplus_chip->charger_exist)
		return;

	iindpm_stat = sgm41542_get_iindpm_stat();

	if(iindpm_stat == true){
		sgm41542_otg_disable();
		chg_debug("iindpm_stat !!! sgm41542_otg_disable\n");
	}


	msleep(10);
	if(oplus_chg_get_otg_online() == true){
		schedule_delayed_work(&chip->otg_iindpm_work, round_jiffies_relative(msecs_to_jiffies(300)));
	}

	chg_debug("sgm41542_otg_iindpm_work 2\n");
}

static void sgm41542_slave_charger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sgm41542 *chip = container_of(dwork, struct chip_sgm41542, slave_charger_work);
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();
	static int enable_slave_cnt = 0;
	int charger_current = 0;
	int icharging = 0;

	if (!g_oplus_chip) {
		chg_debug("oplus_chip is null\n");
		return;
	}

	if (!g_oplus_chip->dual_charger_support) {
		return;
	}

	if (!chip->is_hvdcp) {
		enable_slave_cnt = 0;
		chip->slave_chg_en = false;
		return;
	}

	icharging = oplus_gauge_get_batt_current();
	if (icharging < 0 && (icharging * -1) > g_oplus_chip->slave_chg_enable_ma) {
		enable_slave_cnt++;
	} else {
		enable_slave_cnt = 0;
	}

	chg_err("icharging=%d, temperature=%d, slave_chg_enable_ma=%d, enable_slave_cnt=%d\n",
			icharging, g_oplus_chip->temperature, g_oplus_chip->slave_chg_enable_ma, enable_slave_cnt);

	if (enable_slave_cnt >= 3 && chip->pre_is_hvdcp == true) {
		if (g_oplus_chip->temperature >= 0 && g_oplus_chip->temperature <= 420) {
			chg_err("Enable slave charger!!\n");
			chip->slave_chg_en = true;
			oplus_chg_set_input_current_limit(g_oplus_chip);
			charger_current = oplus_chg_get_tbatt_normal_charging_current(g_oplus_chip);
			sgm41542_charging_current_write_fast(charger_current);
			power_supply_changed(g_oplus_chip->usb_psy);
		}
		enable_slave_cnt = 0;
	} else {
		chg_err("icharging check not complete delay 1000ms\n");
		schedule_delayed_work(&chip->slave_charger_work, round_jiffies_relative(msecs_to_jiffies(1000)));
	}
}

static void oplus_notify_hvdcp_detach_stat(void)
{
	if(!charger_ic) {
		return;
	}
	charger_ic->hvdcp_detach_time = cpu_clock(smp_processor_id()) / CONV_DETACH_TIME;
	chg_err("the hvdcp_detach_time:%lu %lu %d %d\n",
		charger_ic->hvdcp_detach_time, charger_ic->hvdcp_detect_time,
		OPLUS_HVDCP_DETECT_TO_DETACH_TIME, charger_ic->hvdcp_cfg_9v_done);
	if (charger_ic->hvdcp_cfg_9v_done &&
		(charger_ic->hvdcp_detach_time - charger_ic->hvdcp_detect_time
		<= OPLUS_HVDCP_DETECT_TO_DETACH_TIME)) {
		charger_ic->hvdcp_exit_stat = HVDCP_EXIT_ABNORMAL;
	} else {
		charger_ic->hvdcp_exit_stat = HVDCP_EXIT_NORMAL;
	}
	charger_ic->hvdcp_detect_time = 0;
	charger_ic->hvdcp_detach_time = 0;
	charger_ic->hvdcp_cfg_9v_done = false;
}

static void oplus_notify_hvdcp_detect_stat(void)
{
	if(!charger_ic) {
		return;
	}
	charger_ic->hvdcp_cfg_9v_done = true;
	charger_ic->hvdcp_detect_time = cpu_clock(smp_processor_id()) / CONV_DETACH_TIME;
	chg_err("HVDCP2 detect: %d, the detect time: %lu\n",
		charger_ic->hvdcp_cfg_9v_done, charger_ic->hvdcp_detect_time);
}

static void sgm41542_qc_detect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sgm41542 *chip = container_of(dwork, struct chip_sgm41542, qc_detect_work);
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();
	int count = 0, ret = 0;

	chg_err("fchg qc start detect, oplus_charger_type=%d\n", chip->oplus_charger_type);

	if (chip->oplus_charger_type != POWER_SUPPLY_TYPE_USB_DCP)
		return;

	if (g_oplus_chip->soc >= 90) {
		chg_err("return when soc>=90");
		return;
	}

	if (!sgm41542_is_hvdcp())
	{
		do {
			sgm41542_force_dpdm();
			msleep(100);
			oplus_notify_hvdcp_detect_stat();
			ret = sgm41542_enable_qc20_hvdcp_9v();
			msleep(200);

			if (sgm41542_is_hvdcp())
				break;

			if ((chip->oplus_charger_type != POWER_SUPPLY_TYPE_USB_DCP)
				|| (chip->bc12_done == false))
				break;

		} while (count++ < SGM41542_QC_RETRY_CNT);
	}

	chg_err("count=%d\n", count);

	if (chip->is_hvdcp) {
		chip->support_hvdcp = true;
		chg_err("detect 9v success\n");
		/*sgm41542_disable_hvdcp();*/
		schedule_delayed_work(&chip->slave_charger_work, round_jiffies_relative(msecs_to_jiffies(1000)));
	} else {
		chip->support_hvdcp = false;
		oplus_chg_input_current_recheck_work();
		chg_err("detect 9v failed\n");
	}

}

static void sgm41542_start_qc_detect(struct chip_sgm41542 *chip)
{
	if (!chip)
		return;

	/*sgm41542_set_iindet();*/
	if (chip->is_sgm41542) {
		schedule_delayed_work(&chip->qc_detect_work, round_jiffies_relative(msecs_to_jiffies(100)));
	}
}

int sgm41545_chg_enable_qc_detect(void)
{
	struct chip_sgm41542 *chip = charger_ic;

	chg_err("\n");
	if (chip != NULL) {
		if (HVDCP_EXIT_ABNORMAL == chip->hvdcp_exit_stat) {
			chg_err("HVDCP_EXIT_ABNORMAL not enable hvdcp \n");
			return 0;
		}
		sgm41542_start_qc_detect(chip);
		return 0;
	} else {
		chg_err("chip is NULL !\n");
		return 0;
	}
}

int sgm41542_get_charger_subtype(void)
{
	int charg_subtype = CHARGER_SUBTYPE_DEFAULT;
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip) {
		return charg_subtype;
	}

	if (sprd_get_pd_type() == PD_PPS_ACTIVE) {
		charg_subtype = CHARGER_SUBTYPE_PPS;
	} else if (sprd_get_pd_type() == PD_ACTIVE) {
		charg_subtype = CHARGER_SUBTYPE_PD;
	} else if (sgm41542_is_hvdcp()) {
		charg_subtype = CHARGER_SUBTYPE_QC;
    }

	return charg_subtype;
}

static int sgm41542_request_dpdm(struct chip_sgm41542 *chip, bool enable)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
 	static int (*func)(void);
#else
    int ret;
#endif

	if (!chip)
		return 0;
	if(enable){
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
		func = (int (*)(void))
			module_kallsyms_lookup_name("sprd_hsphy_set_dpdm_high_impedance_state");
		if (func){
           (*func)();
           	chg_err("sgm41542_request_dpdm=%d\n", enable);
          }
#else
		ret = sprd_hsphy_set_dpdm_high_impedance_state();
#endif
	}else{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
			func = (int (*)(void))
				module_kallsyms_lookup_name("sprd_hsphy_cancel_dpdm_high_impedance_state");
		if (func){
           (*func)();
           	chg_err("sgm41542_request_dpdm=%d\n", enable);
          }
#else
		ret = sprd_hsphy_cancel_dpdm_high_impedance_state();

#endif
        }

		chg_err("sgm41542_request_dpdm enable = %d\n", enable);

#if 0


	int ret = 0;

	if (!chip)
		return 0;

	/* fetch the DPDM regulator */
	if (!chip->dpdm_reg && of_get_property(chip->dev->of_node,
				"dpdm-supply", NULL)) {
		chip->dpdm_reg = devm_regulator_get(chip->dev, "dpdm");
		if (IS_ERR(chip->dpdm_reg)) {
			ret = PTR_ERR(chip->dpdm_reg);
			chg_err("Couldn't get dpdm regulator ret=%d\n", ret);
			chip->dpdm_reg = NULL;
			return ret;
		}
	}

	mutex_lock(&chip->dpdm_lock);
	if (enable) {
		if (chip->dpdm_reg && !chip->dpdm_enabled) {
			chg_err("enabling DPDM regulator\n");
			ret = regulator_enable(chip->dpdm_reg);
			if (ret < 0)
				chg_err("Couldn't enable dpdm regulator ret=%d\n", ret);
			else
				chip->dpdm_enabled = true;
		}
	} else {
		if (chip->dpdm_reg && chip->dpdm_enabled) {
			chg_err("disabling DPDM regulator\n");
			ret = regulator_disable(chip->dpdm_reg);
			if (ret < 0)
				chg_err("Couldn't disable dpdm regulator ret=%d\n", ret);
			else
				chip->dpdm_enabled = false;
		}
	}
	mutex_unlock(&chip->dpdm_lock);
	return ret;
 #endif
 return 0 ;
}

static void oplus_chg_awake_init(struct chip_sgm41542 *chip)
{
	if (!chip) {
		pr_err("[%s]chip is null\n", __func__);
		return;
	}
	chip->suspend_ws = NULL;
	chip->suspend_ws = wakeup_source_register(NULL, "split chg wakelock");
	return;
}

static void oplus_chg_wakelock(struct chip_sgm41542 *chip, bool awake)
{
	static bool pm_flag = false;
	if (!chip || !chip->suspend_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->suspend_ws);
		pr_err("[%s] true\n", __func__);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->suspend_ws);
		pm_flag = false;
		pr_err("[%s] false\n", __func__);
	}
	return;
}

static void oplus_keep_resume_awake_init(struct chip_sgm41542 *chip)
{
	if (!chip) {
		pr_err("[%s]chip is null\n", __func__);
		return;
	}
	chip->keep_resume_ws = NULL;
	chip->keep_resume_ws = wakeup_source_register(NULL, "split_chg_keep_resume");
	return;
}

static void oplus_keep_resume_wakelock(struct chip_sgm41542 *chip, bool awake)
{
	static bool pm_flag = false;

	if (!chip || !chip->keep_resume_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->keep_resume_ws);
		pr_err("[%s] true\n", __func__);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->keep_resume_ws);
		pm_flag = false;
		pr_err("[%s] false\n", __func__);
	}
	return;
}


static irqreturn_t sgm41542_irq_handler(int irq, void *data)
{
	struct chip_sgm41542 *chip = (struct chip_sgm41542 *) data;
	bool curr_pg = false;

	if (!chip)
		return IRQ_HANDLED;

	curr_pg = sgm41542_get_bus_gd();

	if(curr_pg) {
		oplus_chg_wakelock(chip, true);
	} else {
		oplus_chg_wakelock(chip, false);
	}
	oplus_keep_resume_wakelock(chip, true);
	chg_err("chg irq not stablity that irq do nothing, use oplus_chg_typec_event_notifier_call\n");

	oplus_keep_resume_wakelock(chip, false);
	return IRQ_HANDLED;
}

static int sgm41542_enable_gpio_init(struct chip_sgm41542 *chip)
{
	if (NULL == chip) {
		return -EINVAL;
	}

	sgm41542_enable_gpio(chip, true);

	return 0;
}

static int sgm41542_irq_register(struct chip_sgm41542 *chip)
{
	int ret = 0;

	ret = devm_request_threaded_irq(chip->dev, gpio_to_irq(chip->irq_gpio), NULL,
			sgm41542_irq_handler,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"sgm41542-eint",
			chip);
	if (ret < 0) {
		chg_err("sgm41542 request_irq fail!");
		return -EFAULT;
	}

	/*In Pikachu(21291), sgm41542 is slave charger, not need the BC1.2 and irq service*/
	ret = enable_irq_wake(gpio_to_irq(chip->irq_gpio));
	if (ret != 0) {
		chg_err("enable_irq_wake: irq_gpio failed %d\n", ret);
	}

	return ret;
}

static void sgm41542_get_bc12_init(struct chip_sgm41542 *chip)
{
	int vbus_stat = 0, bc12_retry = 0, bc12_retry_count = 10;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip)
		return;

	bc12_retry_count = (chip->bc12_retry_count > bc12_retry_count) ? chip->bc12_retry_count : bc12_retry_count;
	chg_err("bc12_retry_count=%d\n", bc12_retry_count);
	if (!chip->bc12_done) {
		sgm41542_request_dpdm(chip, true);
		sgm41542_set_iindet();
		while (bc12_retry < bc12_retry_count){
			vbus_stat = sgm41542_get_vbus_stat();
			chg_err("vbus_stat=%d bc12_retry=%d\n", vbus_stat, bc12_retry);
			if (vbus_stat == 0 || sgm41542_get_iindet() == 0) {
				msleep(100);
				bc12_retry++;
			} else {
				switch (vbus_stat) {
				case REG08_SGM41542_VBUS_STAT_SDP:
						chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB;
						chip->bc12_done = true;
						break;
				case REG08_SGM41542_VBUS_STAT_CDP:
						chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB_CDP;
						chip->bc12_done = true;
						break;
				case REG08_SGM41542_VBUS_STAT_DCP:
				case REG08_SGM41542_VBUS_STAT_OCP:
				case REG08_SGM41542_VBUS_STAT_FLOAT:
						chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB_DCP;
						chip->bc12_done = true;
						break;
				case REG08_SGM41542_VBUS_STAT_OTG_MODE:
				default:
						chg_err("bc1.2 unknown\n");
					break;
				}

				break;//while break
			}
		}
		if (g_oplus_chip && g_oplus_chip->usb_psy)
			power_supply_changed(g_oplus_chip->usb_psy);
		sgm41542_request_dpdm(chip, false);
		if (bc12_retry >= bc12_retry_count) {
			chg_err("BC1.2 not complete delay_cnt to max\n");
		}
	}
	if (chip->bc12_done) {
		cancel_delayed_work_sync(&chip->chg_protection_work);
		schedule_delayed_work(&chip->chg_protection_work, round_jiffies_relative(msecs_to_jiffies(20000)));
	}
	oplus_chg_wake_update_work();
	return;
}

void oplus_chg_sgm41542_set_bc12_init(bool enable)
{
	struct chip_sgm41542 *chip = charger_ic;

	if (!chip)
		return;
	if (enable) {
		chip->bc12_done = false;
		sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_40S);
		sgm41542_get_bc12_init(chip);
	} else {
		sgm41542_set_vindpm_vol(SW_AICL_POINT_VOL_5V_PHASE1);
		oplus_notify_hvdcp_detach_stat();
		chip->pre_is_hvdcp = false;
		chip->bc12_done = false;
		chip->charger_current_pre = -1;
		chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		chip->support_hvdcp = false;
		chip->is_hvdcp = false;
		chip->slave_chg_en = false;
		btb_detect_over = false;
		chip->qc_chging = false;
	}

	sgm41542_dump_registers();

	return;
}

#define BYPASS_BC12_RETRY_COUNT 50
static void sgm41542_init_work_handler(struct work_struct *work)
{
	struct chip_sgm41542 *chip = NULL;

	if (charger_ic) {
		chip = charger_ic;

		sgm41542_irq_handler(0, chip);
		chip->bc12_retry_count = BYPASS_BC12_RETRY_COUNT;
		if (sgm41542_chg_is_usb_present()) {
			sgm41542_irq_handler(0, chip);
			if (chip->oplus_charger_type == POWER_SUPPLY_TYPE_USB ||
				chip->oplus_charger_type == POWER_SUPPLY_TYPE_USB_CDP) {
				chip->bc12_done = false;
				chg_err("sdp or cdp, need bc1.2 retry\n");
				sgm41542_get_bc12_init(chip);
			}
		}
	}

	return;
}

struct oplus_chg_operations  sgm41542_chg_ops = {
	.dump_registers = sgm41542_dump_registers,
	.kick_wdt = sgm41542_kick_wdt,
	.hardware_init = sgm41542_hardware_init,
	.charging_current_write_fast = sgm41542_charging_current_write_fast,
	.set_aicl_point = sgm41542_set_aicl_point,
	.input_current_write = sgm41542_input_current_limit_write,
	.input_current_ctrl_by_vooc_write = sgm41542_input_current_limit_ctrl_by_vooc_write,
	.float_voltage_write = sgm41542_float_voltage_write,
	.term_current_set = sgm41542_set_termchg_current,
	.charging_enable = sgm41542_enable_charging,
	.charging_disable = sgm41542_disable_charging,
	.get_charging_enable = sgm41542_check_charging_enable,
	.charger_suspend = sgm41542_suspend_charger,
	.charger_unsuspend = sgm41542_unsuspend_charger,
	.charger_suspend_check = sgm41542_check_suspend_charger,
	.set_rechg_vol = sgm41542_set_rechg_voltage,
	.reset_charger = sgm41542_reset_charger,
	.read_full = sgm41542_registers_read_full,
	.otg_enable = sgm41542_otg_enable,
	.otg_disable = sgm41542_otg_disable,
	.set_charging_term_disable = sgm41542_set_chging_term_disable,
	.check_charger_resume = sgm41542_check_charger_resume,
	.get_chargerid_volt = oplus_sgm41542_get_chargerid_volt,
	.set_chargerid_switch_val = oplus_sgm41542_set_chargerid_switch_val,
	.get_chargerid_switch_val = oplus_sgm41542_get_chargerid_switch_val,
	.need_to_check_ibatt = sgm41542_need_to_check_ibatt,
	.get_chg_current_step = sgm41542_get_chg_current_step,
	.get_charger_type = sgm41542_get_charger_type,
	.get_real_charger_type = NULL,
	.get_charger_volt = sgm41542_get_vbus_voltage,
	/*.get_charger_current = sgm41542_get_ibus_current,*/
	.check_chrdet_status = sgm41542_chg_is_usb_present,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = oplus_get_boot_reason,
    .get_rtc_soc = get_rtc_spare_oplus_fg_value,
    .set_rtc_soc = set_rtc_spare_oplus_fg_value,

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = sgm41542_chg_get_dyna_aicl_result,
#endif
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
	.get_charger_subtype = sgm41542_get_charger_subtype,
	.set_qc_config = sgm41542_set_qc_config,
	.enable_qc_detect = sgm41545_chg_enable_qc_detect,
	.oplus_chg_pd_setup = oplus_chg_set_pd_config,
	.oplus_chg_get_pd_type = oplus_sgm41542_get_pd_type,
	.input_current_write_without_aicl = sgm41542_input_current_limit_without_aicl,
	.oplus_chg_wdt_enable = sgm41542_wdt_enable,
	.get_usbtemp_volt = oplus_get_usbtemp_volt,
	.set_typec_cc_open = oplus_ums9230_usbtemp_set_cc_open,
	.set_typec_sinkonly = oplus_ums9230_usbtemp_set_typec_sinkonly,

	.check_really_suspend_charger = sgm41542_check_really_suspend_charger,
	.really_suspend_charger = sgm41542_really_suspend_charger,
	.oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
	.vooc_timeout_callback = sgm41542_vooc_timeout_callback,
	.enable_shipmode = sgm41542_set_shipmode,
	.get_bc12_done = oplus_sgm41542_get_bc12_done,
	//.get_charger_type = oplus_sgm41542_get_charger_type_read_only,
	.set_bc12_init = oplus_chg_sgm41542_set_bc12_init,
	.get_fpdo_max_current = oplus_get_max_current_from_fixed_pdo,
};

static void sgm41542_vindpm_recovery_work(struct work_struct *work)
{
	struct chip_sgm41542 *chip = NULL;

	if (charger_ic) {
		chip = charger_ic;
		sgm41542_set_vindpm_vol(chip->hw_aicl_point);
		chg_err("%s: vindpm = %d\n", __func__, chip->hw_aicl_point);
	} else {
		chg_err("chip_sgm41542 is null, return\n");
	}

	return;
}

int sgm41542_otg_enable_charger(struct regulator_dev *dev)
{
	int rc = 0;
	struct chip_sgm41542 *chip = rdev_get_drvdata(dev);
	chg_err("sgm41542_otg_enable_charger\n");

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (chip->part_id == REG0B_SGM41515D_PART_ID
		|| chip->part_id == REG0B_SGM41512SD_PART_ID) {
		if (chip->use_voocphy) {
			voocphy_set_otg_en(true);
		}
		sgm41542_really_suspend_charger(false);
	}

	sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_DISABLE);

	if (chip->part_id == REG0B_SGM41515D_PART_ID
		|| chip->part_id == REG0B_SGM41512SD_PART_ID)
		rc = sgm41542_otg_ilim_set(REG02_SGM41515D_OTG_CURRENT_LIMIT_1200MA);
	else
		rc = sgm41542_otg_ilim_set(REG02_SGM41542_OTG_CURRENT_LIMIT_1200MA);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_otg_ilim_set rc = %d\n", rc);
	}
	chg_err("sgm41542_otg_enable_charger\n");

	rc = sgm41542_config_interface(chip, REG01_SGM41542_ADDRESS,
			REG01_SGM41542_OTG_ENABLE,
			REG01_SGM41542_OTG_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_otg_enable  rc = %d\n", rc);
	}

	if (chip->part_id == REG0B_SGM41515D_PART_ID) {
		rc = sgm41542_config_interface(chip, REG0D_SGM41515D_ADDRESS,
			REG0D_SGM41515D_OTG_FREQ_1500KHZ, REG0D_SGM41515D_FREQUENCY_SELECT);
		if(rc < 0) {
			chg_err("Couldn't set SGM41515D OTG_FREQ to 1.5MHZ rc = %d\n", rc);
		}
	} else if (chip->part_id == REG0B_SGM41512SD_PART_ID) {
		rc = sgm41542_config_interface(chip, REG10_SGM41512SD_ADDRESS,
			REG10_SGM41512SD_OTG_FREQ_1500KHZ, REG10_SGM41512SD_FREQUENCY_SELECT);
		if(rc < 0) {
			chg_err("Couldn't set SGM41512SD OTG_FREQ to 1.5MHZ rc = %d\n", rc);
		}
	}
	oplus_chg_set_otg_online(true);

	return rc;
}

int sgm41542_otg_disable_charger(struct regulator_dev *dev)
{
	int rc = 0;
	struct chip_sgm41542 *chip = rdev_get_drvdata(dev);
	chg_err("sgm41542_otg_disable_charger\n");

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if ((chip->part_id == REG0B_SGM41515D_PART_ID
		|| chip->part_id == REG0B_SGM41512SD_PART_ID) && chip->use_voocphy) {
		voocphy_set_otg_en(false);
	}

	rc = sgm41542_config_interface(chip, REG01_SGM41542_ADDRESS,
			REG01_SGM41542_OTG_DISABLE,
			REG01_SGM41542_OTG_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41542_otg_disable rc = %d\n", rc);
	}

	if (chip->part_id == REG0B_SGM41515D_PART_ID) {
		rc = sgm41542_config_interface(chip, REG0D_SGM41515D_ADDRESS,
			REG0D_SGM41515D_OTG_FREQ_500KHZ, REG0D_SGM41515D_FREQUENCY_SELECT);
		if(rc < 0) {
			chg_err("Couldn't set SGM41515D OTG_FREQ to 500MHZ rc = %d\n", rc);
		}
	} else if (chip->part_id == REG0B_SGM41512SD_PART_ID) {
		rc = sgm41542_config_interface(chip, REG10_SGM41512SD_ADDRESS,
			REG10_SGM41512SD_OTG_FREQ_500KHZ, REG10_SGM41512SD_FREQUENCY_SELECT);
		if(rc < 0) {
			chg_err("Couldn't set SGM41512SD OTG_FREQ to 500MHZ rc = %d\n", rc);
		}
	}
	oplus_chg_set_otg_online(false);

	sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_DISABLE);

	return rc;
}

static int sgm41542_charger_vbus_is_enabled(struct regulator_dev *dev)
{
	struct chip_sgm41542 *info = rdev_get_drvdata(dev);
	int ret;
	int val;

	ret = sgm41542_read_reg(info, REG01_SGM41542_ADDRESS, &val);
	if (ret) {
		dev_err(info->dev, "failed to get sgm41542 otg status\n");
		return ret;
	}

	val &= REG01_SGM41542_OTG_MASK;
	chg_err("sgm41542_charger_vbus_is_enabled %d\n", val);

	return val;
}

static const struct regulator_ops sgm41542_charger_vbus_ops = {
	.enable = sgm41542_otg_enable_charger,
	.disable = sgm41542_otg_disable_charger,
	.is_enabled = sgm41542_charger_vbus_is_enabled,
};

static const struct regulator_desc sgm41542_charger_vbus_desc = {
	.name = "otg-vbus",
	.of_match = "otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &sgm41542_charger_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int sgm41542_charger_register_vbus_regulator(struct chip_sgm41542 *info)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;

	cfg.dev = info->dev;
	cfg.driver_data = info;
	reg = devm_regulator_register(info->dev,
				      &sgm41542_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(info->dev, "Can't register regulator:%d\n", ret);
	}

	return ret;
}
#define INIT_WORK_NORMAL_DELAY 8000
#define INIT_WORK_OTHER_DELAY 1000

static int sgm41542_charger_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct chip_sgm41542 *chip = NULL;
	int ret = 0;

	chg_err("sgm41542_charger_probe enter\n");

	chip = devm_kzalloc(&client->dev, sizeof(struct chip_sgm41542), GFP_KERNEL);
	if (!chip) {
		return -ENOMEM;
	}

	charger_ic = chip;
	chip->dev = &client->dev;
	chip->client = client;

	i2c_set_clientdata(client, chip);
	mutex_init(&chip->i2c_lock);
	mutex_init(&chip->dpdm_lock);
	INIT_DELAYED_WORK(&chip->qc_detect_work, sgm41542_qc_detect_work);
	INIT_DELAYED_WORK(&chip->slave_charger_work, sgm41542_slave_charger_work);
	INIT_DELAYED_WORK(&chip->chg_protection_work, sgm41542_chg_protection_work);
	INIT_DELAYED_WORK(&chip->otg_iindpm_work, sgm41542_otg_iindpm_work);

	atomic_set(&chip->charger_suspended, 0);
	atomic_set(&chip->is_suspended, 0);
	oplus_chg_awake_init(chip);
	init_waitqueue_head(&chip->wait);
	oplus_keep_resume_awake_init(chip);
	chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->before_suspend_icl = 0;
	chip->before_unsuspend_icl = 0;
	chip->is_sgm41542 = true;
	chip->is_bq25601d = false;
	chip->bc12_done = false;
	chip->batfet_reset_disable = true;
	chip->charger_current_pre = -1;
	chip->support_hvdcp = false;
	chip->is_hvdcp = false;
	chip->slave_chg_en = false;
	btb_detect_over = false;
	chip->qc_chging = false;
	chip->ap_charge_current = 3300;
	chip->hvdcp_detect_time = 0;
	chip->hvdcp_detach_time = 0;
	chip->hvdcp_cfg_9v_done = false;
	chip->hvdcp_exit_stat = HVDCP_EXIT_NORMAL;
	chip->notifier_registered = false;
	chip->bc12_retry_count = 0;
	ret = sgm41542_get_deivce_online();
	if (ret < 0) {
		chg_err("!!!sgm41542 is not detected\n");
		/*goto err_parse_dt;*/
	}

	ret = sgm41542_parse_dt(chip);
	if (ret) {
		chg_err("Couldn't parse device tree ret=%d\n", ret);
		goto err_parse_dt;
	}

	if (!chip->is_sgm41542) {
		chg_err("not support sgm41542\n");
		ret = -ENOTSUPP;
		goto err_parse_dt;
	}

	sgm41542_enable_gpio_init(chip);

	sgm41542_dump_registers();
	sgm41542_hardware_init();

	ret = device_create_file(chip->dev, &dev_attr_sgm41542_access);
	if (ret) {
		chg_err("create sgm41542_access file fail ret=%d\n", ret);
		goto err_create_file;
	}

	ret = sgm41542_charger_register_vbus_regulator(chip);
	if (ret) {
		chg_err("failed to register vbus regulator ret=%d\n", ret);
	}

	sgm41542_irq_register(chip);
	if (ret) {
		chg_err("Failed to register irq ret=%d\n", ret);
	}

	INIT_DELAYED_WORK(&chip->init_work, sgm41542_init_work_handler);
	INIT_DELAYED_WORK(&chip->vindpm_recovery_work, sgm41542_vindpm_recovery_work);
	INIT_DELAYED_WORK(&chip->update_chg_type_work, sgm41542_update_chg_type_work_handler);

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (NORMAL_BOOT == get_boot_mode())
#else
	if (MSM_BOOT_MODE__NORMAL == get_boot_mode())
#endif
		schedule_delayed_work(&chip->init_work, msecs_to_jiffies(INIT_WORK_NORMAL_DELAY));
	else
		schedule_delayed_work(&chip->init_work, msecs_to_jiffies(INIT_WORK_OTHER_DELAY));

	set_charger_ic(SGM41542);

#ifdef CONFIG_OPLUS_CHARGER_UNISOC
	hardwareinfo_set_prop(HARDWARE_CHARGER_IC, chip->buck_ic_name);
#endif
	chg_err("SGM41542 probe success.");

	chip->fchg_notify.notifier_call = sgm41542_pd_notifier_call;
	ret = power_supply_reg_notifier(&chip->fchg_notify);
	if (ret) {
		chg_err("failed to register fchg notifier, ret = %d\n", ret);
		chip->notifier_registered = false;
	} else {
		chip->notifier_registered = true;
	}

	if (oplus_chg_typec_get_usb_present() && charger_ic->bc12_done == false) {
		sgm41542_get_bc12_init(chip);
	}

	return 0;

err_create_file:
	device_remove_file(chip->dev, &dev_attr_sgm41542_access);
err_parse_dt:
	mutex_destroy(&chip->dpdm_lock);
	mutex_destroy(&chip->i2c_lock);
	return ret;
}

static int sgm41542_charger_remove(struct i2c_client *client)
{
	struct chip_sgm41542 *chip = i2c_get_clientdata(client);
	bool notifier_registered_flag = chip->notifier_registered;

	mutex_destroy(&chip->dpdm_lock);
	mutex_destroy(&chip->i2c_lock);
	cancel_delayed_work_sync(&chip->qc_detect_work);
	cancel_delayed_work_sync(&chip->slave_charger_work);
	cancel_delayed_work_sync(&chip->chg_protection_work);
	cancel_delayed_work_sync(&chip->otg_iindpm_work);
	chip->notifier_registered = false;
	cancel_delayed_work_sync(&chip->update_chg_type_work);
	if (notifier_registered_flag) {
		power_supply_unreg_notifier(&chip->fchg_notify);
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int sgm41542_pm_resume(struct device *dev)
{
	struct chip_sgm41542 *chip = NULL;
	struct i2c_client *client = to_i2c_client(dev);

	pr_err("+++ complete %s: enter +++\n", __func__);
	if (client) {
		chip = i2c_get_clientdata(client);
		if (chip) {
			wake_up_interruptible(&charger_ic->wait);
			atomic_set(&chip->charger_suspended, 0);
		}
	}

	return 0;
}

static int sgm41542_pm_suspend(struct device *dev)
{
	struct chip_sgm41542 *chip = NULL;
	struct i2c_client *client = to_i2c_client(dev);

	if (client) {
		chip = i2c_get_clientdata(client);
		if (chip)
			atomic_set(&chip->charger_suspended, 1);
	}

	return 0;
}

static const struct dev_pm_ops sgm41542_pm_ops = {
	.resume			= sgm41542_pm_resume,
	.suspend		= sgm41542_pm_suspend,
};
#else
static int sgm41542_resume(struct i2c_client *client)
{
	struct chip_sgm41542 *chip = i2c_get_clientdata(client);

	if(!chip) {
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);

	return 0;
}

static int sgm41542_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct chip_sgm41542 *chip = i2c_get_clientdata(client);

	if(!chip) {
		return 0;
	}

	atomic_set(&chip->charger_suspended, 1);

	return 0;
}
#endif

static void sgm41542_charger_shutdown(struct i2c_client *client)
{
	struct chip_sgm41542 *chip = i2c_get_clientdata(client);
	bool notifier_registered_flag = false;

	if(!chip) {
		return;
	}
	notifier_registered_flag = chip->notifier_registered;
	sgm41542_force_dpdm();  //sgm41542_disable_hvdcp();
	sgm41542_set_vindpm_vol(SW_AICL_POINT_VOL_5V_PHASE1);
	sgm41542_set_wdt_timer(REG05_SGM41542_WATCHDOG_TIMER_DISABLE);
	sgm41542_charging_current_write_fast(360);
	sgm41542_disable_charging();
	chip->notifier_registered = false;
	if (notifier_registered_flag) {
		power_supply_unreg_notifier(&chip->fchg_notify);
	}
}

static struct of_device_id sgm41542_charger_match_table[] = {
	{.compatible = "oplus,sgm41542", },
	{},
};

MODULE_DEVICE_TABLE(of, sgm41542_charger_match_table);

static const struct i2c_device_id sgm41542_i2c_device_id[] = {
	{ "sgm41542", 0x3b },
	{ },
};

MODULE_DEVICE_TABLE(i2c, sgm41542_i2c_device_id);

static struct i2c_driver sgm41542_charger_driver = {
	.driver = {
		.name = "sgm41542-charger",
		.owner = THIS_MODULE,
		.of_match_table = sgm41542_charger_match_table,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm 	= &sgm41542_pm_ops,
#endif
	},

	.probe = sgm41542_charger_probe,
	.remove = sgm41542_charger_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= sgm41542_resume,
	.suspend	= sy6974_suspend,
#endif
	.shutdown = sgm41542_charger_shutdown,
	.id_table = sgm41542_i2c_device_id,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(sgm41542_charger_driver);
#else
void sgm41542_charger_exit(void)
{
	i2c_del_driver(&sgm41542_charger_driver);
}

int sgm41542_charger_init(void)
{
	int ret = 0;
	chg_err(" init start\n");

	/*SGM41542 is slave charger in Pikachu(21291) */
	oplus_chg_ops_register("ext-sgm41542", &sgm41542_chg_ops);

	if (i2c_add_driver(&sgm41542_charger_driver) != 0) {
		chg_err(" failed to register sgm41542 i2c driver.\n");
	} else {
		chg_debug(" Success to register sgm41542 i2c driver.\n");
	}
	return ret;
}
#endif

MODULE_DESCRIPTION("Driver for charge IC.");
MODULE_LICENSE("GPL v2");
