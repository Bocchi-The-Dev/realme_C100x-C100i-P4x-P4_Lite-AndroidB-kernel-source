// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author Wy Chuang<wy.chuang@mediatek.com>
 */

#include <linux/cdev.h>		/* cdev */
#include <linux/err.h>	/* IS_ERR, PTR_ERR */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>	/*irq_to_desc*/
#include <linux/kernel.h>
#include <linux/kthread.h>	/* For Kthread_run */
#include <linux/math64.h>
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/netlink.h>	/* netlink */
#include <linux/of_fdt.h>	/*of_dt API*/
#include <linux/of.h>
#include <linux/platform_device.h>	/* platform device */
#include <linux/proc_fs.h>
#include <linux/reboot.h>	/*kernel_power_off*/
#include <linux/sched.h>	/* For wait queue*/
#include <linux/skbuff.h>	/* netlink */
#include <linux/socket.h>	/* netlink */
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>		/* For wait queue*/
#include <net/sock.h>		/* netlink */
#include <linux/suspend.h>
#include "unisoc_battery.h"
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/hardware_info.h>
#include <linux/hardware_info_lux.h>
#include <linux/power/charger-manager.h>

#define OPLUS_FEATURE_CHG_BASIC 1
struct sprd_fgu_data *gauge_data;

#ifdef OPLUS_FEATURE_CHG_BASIC
#include "../oplus/v1/oplus_gauge.h"
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define LOWEST_TEMP_FOR_NTC_DISCONNECT 10000000
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/iio/consumer.h>
//extern void fg_int_event(struct mtk_battery *gm, enum gauge_event evt);
extern struct iio_channel *iio_channel_get(struct device *dev,const char *channel_name);
extern void oplus_gauge_init(struct oplus_gauge_chip *chip);
extern int oplus_chg_get_voocphy_support(void);
extern int oplus_force_get_subboard_temp(void);

struct iio_channel	*batt_id = NULL;
int fuelgauge_apply = 0;
int batt_id_fast_chcek = 0;
static int batt_id_number = -1;
int g_ntc_switch_not_use = 0;
static int g_switch_ntc = 0;
int is_subboard_temp_support = 0;
int enable_is_force_full;
bool last_full = false;
static int first_get_ui_soc = false;

#define BAT_LIWEI_BATT0_ID  0
#define BAT_LIWEI_BATT_ID  1
#define BAT_LIWEI_BATT1_ID  2
#define BAT_GUANYU_BATT_ID  1
#define BAT_GUANYU_BATT1_ID  0
#define BAT_ATL_BATT_ID  0
#define BAT_ATL_BATT1_ID 1
#define BAT_ATL_BATT2_ID 2
#define BAT_ATL_BATT3_ID 3

/*hulk start*/
#define BAT_TYPE__LIWEI_4450mV_NTC_MIN 70
#define BAT_TYPE__LIWEI_4450mV_NTC_MAX 270
#define BAT_TYPE__GUANYU_4450mV_NTC_MIN 310
#define BAT_TYPE__GUANYU_4450mV_NTC_MAX 510
#define BAT_TYPE__ATL_4450mV_NTC_MIN 630
#define BAT_TYPE__ATL_4450mV_NTC_MAX 810

#define BAT_TYPE__LIWEI_4480mV_NTC_MIN 70
#define BAT_TYPE__LIWEI_4480mV_NTC_MAX 270
#define BAT_TYPE__GUANYU_4480mV_NTC_MIN 310
#define BAT_TYPE__GUANYU_4480mV_NTC_MAX 510
#define BAT_TYPE__ATL_4480mV_NTC_MIN 630
#define BAT_TYPE__ATL_4480mV_NTC_MAX 810
/*latte end*/

/*latte start*/
#define BAT_TYPE__ATL1_4480mV_NTC_MIN 310
#define BAT_TYPE__ATL1_4480mV_NTC_MAX 500
#define BAT_TYPE__ATL_4500mV_NTC_MIN 630
#define BAT_TYPE__ATL_4500mV_NTC_MAX 810
/*latte end*/

/*dhaka start*/
#define BAT_TYPE__ATL2_4480mV_NTC_MIN 70
#define BAT_TYPE__ATL2_4480mV_NTC_MAX 270
/*dhaka end*/

/*nairobi/cruiserB4 start*/
#define BAT_TYPE__GUANYU1_4480mV_NTC_MIN 310
#define BAT_TYPE__GUANYU1_4480mV_NTC_MAX 470
#define BAT_TYPE__LIWEI1_4480mV_NTC_MIN 470
#define BAT_TYPE__LIWEI1_4480mV_NTC_MAX 630
#define BAT_TYPE__ATL_4530mV_NTC_MIN 640
#define BAT_TYPE__ATL_4530mV_NTC_MAX 840
/*nairobi/cruiserB4 end*/

enum {
	BAT_TYPE__UNKNOWN,
	BAT_TYPE__SDI_4350mV, //50mV~290mV
	BAT_TYPE__SDI_4400mV, //300mV~520mV
	BAT_TYPE__LG_4350mV, //NO use
	BAT_TYPE__LG_4400mV, //530mV~780mV
	BAT_TYPE__ATL_4350mV, //1110mV~1450mV
	BAT_TYPE__ATL_4400mV, //790mV~1100mV
	BAT_TYPE__TWS_4400mV,
	BAT_TYPE__LIWEI_4450mV,
	BAT_TYPE__GUANYU_4450mV,
	BAT_TYPE__ATL_4450mV, //550mV~790mV
	BAT_TYPE__XWD_4450mV, //50mv~200mv
	BAT_TYPE__LIWEI_4480mV,
	BAT_TYPE__GUANYU_4480mV,
	BAT_TYPE__ATL_4480mV,
	BAT_TYPE__ATL_4500mV,
	BAT_TYPE__ATL_4530mV,
};

bool is_fuelgauge_apply(void)
{
	return fuelgauge_apply;
}
//EXPORT_SYMBOL(is_fuelgauge_apply);

int is_batt_id_check(void)
{
	return batt_id_fast_chcek;
}

bool prj_is_subboard_temp_support(void)
{
	return is_subboard_temp_support;
}

int bat_get_debug_level(void)
{
	return BMLOG_DEBUG_LEVEL;
}

int battery_type_check(int *battery_type)
{
	int value = 0;
	int ret = 0;
	int ret_value = 0;
	int battery_id = 0, choose_project = 0;
	struct device_node *battery_np;
	if (batt_id == NULL) {
		bm_err("[battery_type_check]: batt_id is null\n");
		*battery_type = BAT_TYPE__ATL_4400mV;
		battery_id = -1;
		return battery_id;
	}

	ret = iio_read_channel_processed(batt_id, &ret_value);
	if (ret < 0) {
		bm_err( "[battery_type_check] read channel err = %d,\n", ret);
	}
	value = ret_value;
	choose_project = is_batt_id_check();
	bm_info( "[battery_type_check]: batt_id_voltage = %d, choose_project = %d\n", value, choose_project);

	switch (choose_project) {
		case 0:
			/****hulk-A/D start****/
			if(is_fuelgauge_apply() == true){
				if (value >= BAT_TYPE__LIWEI_4450mV_NTC_MIN && value <= BAT_TYPE__LIWEI_4450mV_NTC_MAX) {
					*battery_type = BAT_TYPE__LIWEI_4450mV;
					battery_id = BAT_LIWEI_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"LIWEI_5000MA_4P45V");
				} else if (value >= BAT_TYPE__GUANYU_4450mV_NTC_MIN && value < BAT_TYPE__GUANYU_4450mV_NTC_MAX) {
					*battery_type = BAT_TYPE__GUANYU_4450mV;
					battery_id = BAT_GUANYU_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"GUANYU_5000MA_4P45V");
				} else if (value >= BAT_TYPE__ATL_4450mV_NTC_MIN && value < BAT_TYPE__ATL_4450mV_NTC_MAX){
					*battery_type = BAT_TYPE__ATL_4450mV;
					battery_id = BAT_ATL_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"ALT_5000MA_4P45V");
				} else {
					*battery_type = BAT_TYPE__UNKNOWN;
					battery_id = -1;
					get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
				}
			} else {
				*battery_type = BAT_TYPE__UNKNOWN;
				battery_id = -1;
				get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
			}
			/****hulk-A/D/ end****/
			break;
		case 1:
			/****hulk-B/C/M start****/
			if (is_fuelgauge_apply() == true) {
				if (value >= BAT_TYPE__LIWEI_4480mV_NTC_MIN && value <= BAT_TYPE__LIWEI_4480mV_NTC_MAX) {
					*battery_type = BAT_TYPE__LIWEI_4480mV;
					battery_id = BAT_LIWEI_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"LIWEI_5000MA_4P48V");
				} else if (value >= BAT_TYPE__GUANYU_4480mV_NTC_MIN && value < BAT_TYPE__GUANYU_4480mV_NTC_MAX) {
					*battery_type = BAT_TYPE__GUANYU_4480mV;
					battery_id = BAT_GUANYU_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"GUANYU_5000MA_4P48V");
				} else if (value >= BAT_TYPE__ATL_4480mV_NTC_MIN && value < BAT_TYPE__ATL_4480mV_NTC_MAX){
					*battery_type = BAT_TYPE__ATL_4480mV;
					battery_id = BAT_ATL_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"ALT_5000MA_4P48V");
				} else {
					*battery_type = BAT_TYPE__UNKNOWN;
					battery_id = -1;
					get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
				}
			} else {
				*battery_type = BAT_TYPE__UNKNOWN;
				battery_id = -1;
				get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
			}
			/****hulk-B/C/M end****/
			break;
		case 2:
			/****hulk-K start****/
			if(is_fuelgauge_apply() == true){
				if (value >= BAT_TYPE__ATL_4450mV_NTC_MIN && value < BAT_TYPE__ATL_4450mV_NTC_MAX){
					*battery_type = BAT_TYPE__ATL_4450mV;
					battery_id = BAT_ATL_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"ALT_5000MA_4P45V");
				} else {
					*battery_type = BAT_TYPE__UNKNOWN;
					battery_id = -1;
					get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
				}
			} else {
				*battery_type = BAT_TYPE__UNKNOWN;
				battery_id = -1;
				get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
			}
			/****hulk-K end****/
			break;
		case 3:
			/****latte-A/B/C/D start****/
			if(is_fuelgauge_apply() == true){
				if (value >= BAT_TYPE__ATL_4500mV_NTC_MIN && value <= BAT_TYPE__ATL_4500mV_NTC_MAX) {
					*battery_type = BAT_TYPE__ATL_4500mV;
					battery_id = BAT_ATL_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"ATL_5000MA_4P5V");
				} else if (value >= BAT_TYPE__ATL1_4480mV_NTC_MIN && value < BAT_TYPE__ATL1_4480mV_NTC_MAX){
					*battery_type = BAT_TYPE__ATL_4480mV;
					battery_id = BAT_ATL_BATT1_ID;
					get_hardware_info_data(HWID_BATERY_ID,"ATL_5000MA_4P48V");
				} else {
					*battery_type = BAT_TYPE__UNKNOWN;
					battery_id = -1;
					get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
				}
			} else {
				*battery_type = BAT_TYPE__UNKNOWN;
				battery_id = -1;
				get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
			}
			/****latte-A/B/C/D end****/
			break;
		case 4:
			/****dhaka-start****/
			if(is_fuelgauge_apply() == true){
				if (value >= BAT_TYPE__ATL2_4480mV_NTC_MIN && value <= BAT_TYPE__ATL2_4480mV_NTC_MAX) {
					*battery_type = BAT_TYPE__ATL_4480mV;
					battery_id = BAT_ATL_BATT1_ID;
					get_hardware_info_data(HWID_BATERY_ID,"ATL_6300MA_4P48V");
					battery_np = of_parse_phandle(gauge_data->battery->of_node, "monitored-battery", battery_id);
					of_property_read_u32(battery_np, "comp-resistance-mohm",&gauge_data->comp_resistance);
				} else if (value >= BAT_TYPE__ATL1_4480mV_NTC_MIN && value < BAT_TYPE__ATL1_4480mV_NTC_MAX){
					*battery_type = BAT_TYPE__ATL_4480mV;
					battery_id = BAT_ATL_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"ATL_6000MA_4P48V");
					battery_np = of_parse_phandle(gauge_data->battery->of_node, "monitored-battery", battery_id);
					of_property_read_u32(battery_np, "comp-resistance-mohm",&gauge_data->comp_resistance);
				} else {
					*battery_type = BAT_TYPE__UNKNOWN;
					battery_id = -1;
					get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
				}
			} else {
				*battery_type = BAT_TYPE__UNKNOWN;
				battery_id = -1;
				get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
			}
			/****dhaka-end****/
			break;
		case 5:
			/****nairobi-start****/
			if(is_fuelgauge_apply() == true){
				if (value >= BAT_TYPE__LIWEI_4480mV_NTC_MIN && value <= BAT_TYPE__LIWEI_4480mV_NTC_MAX) {
					*battery_type = BAT_TYPE__LIWEI_4480mV;
					battery_id = BAT_LIWEI_BATT_ID;
					hardwareinfo_set_prop(HARDWARE_BATTERY_ID,"LIWEI_7000MA_4P48V");
					battery_np = of_parse_phandle(gauge_data->battery->of_node, "monitored-battery", battery_id);
					of_property_read_u32(battery_np, "comp-resistance-mohm",&gauge_data->comp_resistance);
				} else if (value >= BAT_TYPE__GUANYU1_4480mV_NTC_MIN && value < BAT_TYPE__GUANYU1_4480mV_NTC_MAX){
					*battery_type = BAT_TYPE__GUANYU_4480mV;
					battery_id = BAT_GUANYU_BATT1_ID;
					hardwareinfo_set_prop(HARDWARE_BATTERY_ID,"GUANYU_7000MA_4P48V");
					battery_np = of_parse_phandle(gauge_data->battery->of_node, "monitored-battery", battery_id);
					of_property_read_u32(battery_np, "comp-resistance-mohm",&gauge_data->comp_resistance);
				} else if (value >= BAT_TYPE__LIWEI1_4480mV_NTC_MIN && value < BAT_TYPE__LIWEI1_4480mV_NTC_MAX){
					*battery_type = BAT_TYPE__LIWEI_4480mV;
					battery_id = BAT_LIWEI_BATT1_ID;
					hardwareinfo_set_prop(HARDWARE_BATTERY_ID,"LIWEI_6600MA_4P48V");
					battery_np = of_parse_phandle(gauge_data->battery->of_node, "monitored-battery", battery_id);
					of_property_read_u32(battery_np, "comp-resistance-mohm",&gauge_data->comp_resistance);
				} else if (value >= BAT_TYPE__ATL_4530mV_NTC_MIN && value < BAT_TYPE__ATL_4530mV_NTC_MAX){//just for trial production
					*battery_type = BAT_TYPE__ATL_4530mV;
					battery_id = BAT_ATL_BATT3_ID;
					hardwareinfo_set_prop(HARDWARE_BATTERY_ID,"ATL_8000MA_4P53V");
					battery_np = of_parse_phandle(gauge_data->battery->of_node, "monitored-battery", battery_id);
					of_property_read_u32(battery_np, "comp-resistance-mohm",&gauge_data->comp_resistance);
				}else {
					*battery_type = BAT_TYPE__UNKNOWN;
					battery_id = -1;
					hardwareinfo_set_prop(HARDWARE_BATTERY_ID,"UNKNOWN");
				}
			} else {
				*battery_type = BAT_TYPE__UNKNOWN;
				battery_id = -1;
				hardwareinfo_set_prop(HARDWARE_BATTERY_ID,"UNKNOWN");
			}
			/****nairobi-end****/
			break;
		case 6:
			/****cruiserB4-start****/
			if(is_fuelgauge_apply() == true){
				if (value >= BAT_TYPE__LIWEI_4480mV_NTC_MIN && value <= BAT_TYPE__LIWEI_4480mV_NTC_MAX) {
					*battery_type = BAT_TYPE__LIWEI_4480mV;
					battery_id = BAT_LIWEI_BATT0_ID;
					get_hardware_info_data(HWID_BATERY_ID,"LIWEI_7000MA_4P48V");
					battery_np = of_parse_phandle(gauge_data->battery->of_node, "monitored-battery", battery_id);
					of_property_read_u32(battery_np, "comp-resistance-mohm",&gauge_data->comp_resistance);
				} else if (value >= BAT_TYPE__LIWEI1_4480mV_NTC_MIN && value < BAT_TYPE__LIWEI1_4480mV_NTC_MAX){
					*battery_type = BAT_TYPE__LIWEI_4480mV;
					battery_id = BAT_LIWEI_BATT_ID;
					get_hardware_info_data(HWID_BATERY_ID,"LIWEI_6600MA_4P48V");
					battery_np = of_parse_phandle(gauge_data->battery->of_node, "monitored-battery", battery_id);
					of_property_read_u32(battery_np, "comp-resistance-mohm",&gauge_data->comp_resistance);
				} else {
					*battery_type = BAT_TYPE__UNKNOWN;
					battery_id = -1;
					get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
				}
			} else {
				*battery_type = BAT_TYPE__UNKNOWN;
				battery_id = -1;
				get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
			}
			/****cruiserB4-end****/
			break;
		default:
			*battery_type = BAT_TYPE__UNKNOWN;
			battery_id = -1;
			get_hardware_info_data(HWID_BATERY_ID,"UNKNOWN");
			break;
	}

	printk(KERN_ERR "[battery_type_check]: adc_value[%d], battery_type[%d], g_fg_battery_id[%d]\n", value, *battery_type, battery_id);
	batt_id_number = battery_id;
	return battery_id;
}

int fgauge_get_profile_id(void)
{
	int battery_id = 0;
	int battery_type = BAT_TYPE__UNKNOWN;

	battery_id = battery_type_check(&battery_type);

	return battery_id;
}
EXPORT_SYMBOL_GPL(fgauge_get_profile_id);

#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define OPLUS_BATON_NTC_01 10
#endif

static int gauge_get_property(enum power_supply_property psp, int *temp)
{
	struct power_supply *fuel_gauge;
	int ret;
	int64_t temp_val;

	fuel_gauge = power_supply_get_by_name("sc27xx-fgu");
	if (!fuel_gauge)
		return -ENODEV;
	ret = power_supply_get_property(fuel_gauge, psp, (union power_supply_propval *)&temp_val);
	power_supply_put(fuel_gauge);

	if (ret == 0)
		*temp = (int)temp_val;
	return ret;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
#define OPLUS_TEMP_25 250

static bool battery_type_is_4450mv(void)
{
	int battery_type = BAT_TYPE__UNKNOWN;
	int retry_flag = 0;

try_again:
	battery_type_check(&battery_type);
	if (battery_type == BAT_TYPE__ATL_4450mV || battery_type == BAT_TYPE__GUANYU_4450mV) {
		return true;
	} else {
		if (retry_flag == 0) {
			retry_flag = 1;
			goto try_again;
		}
		return false;
	}
}

static bool battery_type_is_4480mv(void)
{
	int battery_type = BAT_TYPE__UNKNOWN;
	int retry_flag = 0;

try_again:
	battery_type_check(&battery_type);
	if (battery_type == BAT_TYPE__ATL_4480mV || battery_type == BAT_TYPE__LIWEI_4480mV || battery_type == BAT_TYPE__GUANYU_4480mV || battery_type == BAT_TYPE__ATL_4530mV) {
		return true;
	} else {
		if (retry_flag == 0) {
			retry_flag = 1;
			goto try_again;
		}
		return false;
	}
}

static bool battery_type_is_4500mv(void)
{
	int battery_type = BAT_TYPE__UNKNOWN;
	int retry_flag = 0;

try_again:
	battery_type_check(&battery_type);
	if (battery_type == BAT_TYPE__ATL_4500mV || battery_type == BAT_TYPE__ATL_4480mV || battery_type == BAT_TYPE__LIWEI_4480mV) {
		return true;
	} else {
		if (retry_flag == 0) {
			retry_flag = 1;
			goto try_again;
		}
		return false;
	}
}

bool is_battery_init_done(void)
{
	return gauge_data->is_probe_done;
}

int oplus_battery_get_bat_temperature(void)
{
	if (is_battery_init_done()) {
		//return (force_get_tbat(oplus_gm, true) * 10);
		return OPLUS_TEMP_25;
	} else {
		return -1270;
	}
}

static int unisoc_get_battery_mvolts(void)
{
	int bat_volt_mv = 0;
	int ret;

	ret = gauge_get_property(POWER_SUPPLY_PROP_VOLTAGE_AVG, &bat_volt_mv);
	if (ret)
		bm_err("%s, unisoc_get_battery_mvolts failed, ret=%d\n", __func__, ret);

	bat_volt_mv = bat_volt_mv / 1000;
	return bat_volt_mv;
}

static int unisoc_get_battery_temperature(void)
{
	int bat_temperature = 0;
	int ret;

	ret = gauge_get_property(POWER_SUPPLY_PROP_TEMP, &bat_temperature);
	if (ret)
		bm_err("%s, unisoc_get_battery_temperature failed, ret=%d\n", __func__, ret);

	return bat_temperature;
}

#define TEMP_WITHOUT_BAT_ID	-400
static int unisoc_get_battery_soc(void)
{
	int fuel_cap = 0;
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;
	int fuel_cap_init = 50;

	fuel_gauge = power_supply_get_by_name("sc27xx-fgu");
	if (!fuel_gauge)
		return -ENODEV;
	if(first_get_ui_soc) {
		bm_err("%s,  first_get_ui_soc=%d\n", __func__,first_get_ui_soc);
		val.intval = CM_BOOT_CAPACITY;
		first_get_ui_soc = false;
	}

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CAPACITY, &val);
	power_supply_put(fuel_gauge);

	if (ret == 0){
		fuel_cap = val.intval;

	fuel_cap = DIV_ROUND_CLOSEST(fuel_cap, 10);

	if (fuel_cap > 100)
		fuel_cap = 100;
	else if (fuel_cap < 0)
		fuel_cap = 0;
}
	if (TEMP_WITHOUT_BAT_ID == gauge_data->bat_temp) {
		fuel_cap = fuel_cap_init;
		bm_err("%s, bat_temp is low than temp threshold, return 50 percent\n", __func__);
	}

	//bm_info("%s, eddyhua, fuel_cap = %d\n", __func__, fuel_cap);

	return fuel_cap;
}

static int unisoc_get_batt_remaining_capacity_uah(void)
{
	int fuel_cap = 0;
	//int ret;

	fuel_cap = unisoc_get_battery_soc();
	if (fuel_cap < 0) {
		bm_err("%s unisoc_get_battery_soc fail \n", __func__);
		fuel_cap = 0;
	}

	bm_err("%s, eddyhua, remain cap = %d\n", __func__, gauge_data->total_mah * fuel_cap * 1000);

	return (int64_t)gauge_data->total_mah * fuel_cap * 10;
}

static int unisoc_get_average_current(void)
{
	int bat_curr_ma = 0;
	int ret;

	ret = gauge_get_property(POWER_SUPPLY_PROP_CURRENT_AVG, &bat_curr_ma);
	if (ret)
		bm_err("%s, unisoc_get_average_current failed, ret=%d\n", __func__, ret);

	bat_curr_ma = (-1*(bat_curr_ma / 1000));


	//bm_info("%s, eddyhua, bat_curr_ma = %d\n", __func__, bat_curr_ma);

	return bat_curr_ma;
}

static int unisoc_get_prev_battery_fcc(void)
{
	bm_info("%s, eddyhua, total_mah = %d\n", __func__, gauge_data->total_mah);
	return gauge_data->total_mah;
}

static int unisoc_get_battery_fcc(void)
{
	return gauge_data->total_mah;
}

static int unisoc_get_battery_cc(void)
{
	return gauge_data->charge_cycle;
}

static int unisoc_get_battery_soh(void)
{
	return -1;
}

static int unisoc_get_prev_batt_remaining_capacity_uah(void)
{
	int fuel_cap = 0;
	//int ret;

	fuel_cap = unisoc_get_battery_soc();
	if (fuel_cap < 0) {
		bm_err("%s unisoc_get_battery_soc fail \n", __func__);
		fuel_cap = 0;
	}

	return (int64_t)gauge_data->total_mah * fuel_cap * 1000;
}

static int unisoc_modify_dod0(void)
{
	return -1;
}

static int unisoc_update_soc_smooth_parameter(void)
{
	return -1;
}

static int unisoc_get_bat_id_number(void)
{
	return batt_id_number;
}

static int adjust_fuel_cap(int cap)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name("sc27xx-fgu");
	if (!fuel_gauge)
		return -ENODEV;

	val.intval = cap * 10;
	ret = power_supply_set_property(fuel_gauge,
					POWER_SUPPLY_PROP_CALIBRATE, &val);
	power_supply_put(fuel_gauge);

	return ret;
}


static void unisoc_set_battery_full(bool full)
{
	int ret = -1;
	if(full)
		ret = adjust_fuel_cap(100);
	bm_err("last full = %d, full = %d ret = %d\n", last_full, full,ret);
}

bool unisoc_get_battery_authenticate(void)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
		int choose_project = 0;

		choose_project = is_batt_id_check();
		switch (choose_project) {
			case 0:
				return battery_type_is_4450mv();
				break;
			case 1:
				return battery_type_is_4480mv();
				break;
			case 2:
				return battery_type_is_4450mv();
				break;
			case 3:
				return battery_type_is_4500mv();
				break;
			case 4:
				return battery_type_is_4480mv();
				break;
			case 5:
			case 6:
				return battery_type_is_4480mv();
				break;
			default:
				return false;
				break;
		}
#endif
}

#if IS_ENABLED(CONFIG_OF)
static int fg_read_dts_val(const struct device_node *np,
		const char *node_srting,
		int *param, int unit)
{
	static unsigned int val;

	if (!of_property_read_u32(np, node_srting, &val)) {
		*param = (int)val * unit;
		bm_debug("Get %s: %d\n",
			 node_srting, *param);
	} else {
		bm_debug("Get %s no data\n", node_srting);
		return -1;
	}
	return 0;
}
#endif

static int unisoc_get_dec_fg_type(void)
{
	bm_debug(" %d\n", UNISOC_FG);

	return UNISOC_FG;
}

static int unisoc_get_qmax(int *qmax1, int *qmax2)
{
	int design_capacity = 0;
	int ret;

	if (!qmax1 || !qmax2)
		return -1;

	ret = gauge_get_property(POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, &design_capacity);
	if (ret)
		bm_err("%s, unisoc_get_battery_temperature failed, ret=%d\n", __func__, ret);

	*qmax1 = design_capacity;
	*qmax2 = design_capacity;

	return 0;
}

static struct oplus_gauge_operations oplus_battery_gauge = {
	.get_battery_mvolts 		= unisoc_get_battery_mvolts,
	.get_battery_temperature		= unisoc_get_battery_temperature,
	.get_batt_remaining_capacity	= unisoc_get_batt_remaining_capacity_uah,
	.get_battery_soc				= unisoc_get_battery_soc,
	.get_average_current			= unisoc_get_average_current,
	.get_battery_fcc				= unisoc_get_battery_fcc,
	.get_battery_cc 			= unisoc_get_battery_cc,
	.get_battery_soh				= unisoc_get_battery_soh,
	.get_battery_authenticate		= unisoc_get_battery_authenticate,
	.set_battery_full				= unisoc_set_battery_full,
	.get_prev_battery_mvolts		= unisoc_get_battery_mvolts,
	.get_prev_battery_temperature	= unisoc_get_battery_temperature,
	.get_prev_battery_soc			= unisoc_get_battery_soc,
	.get_prev_average_current		= unisoc_get_average_current,
	.get_prev_batt_remaining_capacity	= unisoc_get_prev_batt_remaining_capacity_uah,
	.get_battery_mvolts_2cell_max		= unisoc_get_battery_mvolts,
	.get_battery_mvolts_2cell_min		= unisoc_get_battery_mvolts,
	.get_prev_battery_mvolts_2cell_max	= unisoc_get_battery_mvolts,
	.get_prev_battery_mvolts_2cell_min	= unisoc_get_battery_mvolts,
	.get_prev_batt_fcc				= unisoc_get_prev_battery_fcc,
	.update_battery_dod0				= unisoc_modify_dod0,
	.update_soc_smooth_parameter		= unisoc_update_soc_smooth_parameter,
	.get_bat_id_number                      = unisoc_get_bat_id_number,
	.get_dec_fg_type 			= unisoc_get_dec_fg_type,
	.get_batt_qmax				= unisoc_get_qmax,
};
#endif

int battery_init(struct platform_device *pdev)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_gauge_chip *chip = NULL;
#endif
	gauge_data = dev_get_drvdata(&pdev->dev);

#ifdef OPLUS_FEATURE_CHG_BASIC
	fg_read_dts_val(pdev->dev.of_node, "FUELGAGUE_APPLY", &(fuelgauge_apply), 1);
	bm_err("%s, fuelgauge_apply:%d\n", __func__, fuelgauge_apply);

	fg_read_dts_val(pdev->dev.of_node, "IS_SUBBOARD_TEMP_SUPPORT", &(is_subboard_temp_support), 1);
	bm_err("%s, is_subboard_temp_support:%d\n", __func__, is_subboard_temp_support);

	fg_read_dts_val(pdev->dev.of_node, "Enable_Is_Force_Full", &(enable_is_force_full), 1);
	bm_err("%s, enable_is_force_full:%d\n", __func__, enable_is_force_full);

	fg_read_dts_val(pdev->dev.of_node, "BATT_ID_FAST_CHECK", &(batt_id_fast_chcek), 1);
	bm_err("%s, batt_id_fast_chcek:%d\n", __func__, batt_id_fast_chcek);

	fg_read_dts_val(pdev->dev.of_node, "NTC_SWITCH_NOT_USE", &(g_ntc_switch_not_use), 1);
	bm_err("%s, g_ntc_switch_not_use:%d\n", __func__, g_ntc_switch_not_use);

	g_switch_ntc = of_get_named_gpio(pdev->dev.of_node, "ntc_switch_gpio", 0);
	if (g_switch_ntc < 0) {
		bm_err("ntc_switch_gpio < 0 !!!\r\n");
		g_switch_ntc = 0;
	}

	if(gpio_request(g_switch_ntc, "NTC_SWITCH_GPIO") < 0) {
		bm_err("ntc_switch_gpio gpio_request fail\r\n");
	}

	if(is_fuelgauge_apply() == true) {
		batt_id = devm_iio_channel_get(&pdev->dev, "batt_id");
		if (IS_ERR(batt_id)){
			bm_err("battery ID CHANNEL ERR\n");
			batt_id = NULL;
		}
	}

#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	if(is_fuelgauge_apply() == true) {
		chip = (struct oplus_gauge_chip*) kzalloc(sizeof(struct oplus_gauge_chip),
					GFP_KERNEL);
		if (!chip) {
			bm_err("oplus_gauge_chip devm_kzalloc failed.\n");
			return -ENOMEM;
		}
	}
	if(is_fuelgauge_apply() == true) {
		chip->gauge_ops = &oplus_battery_gauge;
		oplus_gauge_init(chip);
		chip->gauge_ops->get_battery_authenticate();
		chip->gauge_init_completed = true;
	}
#endif
	first_get_ui_soc = true;

	return 0;
}
EXPORT_SYMBOL_GPL(battery_init);

