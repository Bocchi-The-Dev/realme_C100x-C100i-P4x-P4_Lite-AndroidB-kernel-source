/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Unisoc Co., Ltd.
 * Changhua.Zhang <Changhua.Zhang@unisoc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/power/sprd_battery_info.h>

enum sprd_fgu_int_command {
	SPRD_FGU_VOLT_LOW_INT_CMD = 0,
	SPRD_FGU_VOLT_HIGH_INT_CMD,
	SPRD_FGU_CLBCNT_DELTA_INT_CMD,
	SPRD_FGU_POWER_LOW_CNT_INT_CMD,
	SPRD_FGU_RELAX_CNT_INT_CMD,
};

enum sprd_fgu_int_event {
	SPRD_FGU_VOLT_LOW_INT_EVENT = 0,
	SPRD_FGU_VOLT_HIGH_INT_EVENT,
	SPRD_FGU_CLBCNT_DELTA_INT_EVENT,
	SPRD_FGU_POWER_LOW_CNT_INT_EVENT,
	SPRD_FGU_RELAX_CNT_INT_EVENT,
};

enum sprd_fgu_sts_command {
	SPRD_FGU_CURT_LOW_STS_CMD = 0,
	SPRD_FGU_POWER_LOW_STS_CMD,
	SPRD_FGU_INVALID_POCV_STS_CMD,
	SPRD_FGU_BATTERY_FLAG_STS_CMD,
	SPRD_FGU_CLK_SEL_FGU_STS_CMD,
};

enum sprd_fgu_dump_fgu_info_level {
	DUMP_FGU_INFO_LEVEL_0 = 0,
	DUMP_FGU_INFO_LEVEL_1,
	DUMP_FGU_INFO_LEVEL_2,
	DUMP_FGU_INFO_LEVEL_3,
	DUMP_FGU_INFO_LEVEL_4,
	DUMP_FGU_INFO_LEVEL_MAX,
};

struct sprd_fgu_variant_data {
	u32 module_en;
	u32 clk_en;
	u32 fgu_cal;
	u32 fgu_cal_shift;
};

struct sprd_fgu_sleep_capacity_calibration {
	bool support_slp_calib;
	int suspend_ocv_uv;
	int resume_ocv_uv;
	int suspend_cc_uah;
	int resume_cc_uah;
	s64 suspend_time;
	s64 resume_time;
	int resume_ocv_cap;

	/* UMP518 relax cnt function define */
	int relax_cur_threshold;
	int relax_state_time_threshold;
	int power_low_counter_threshold;
	bool power_low_cnt_int_ocurred;

	/* UMP96XX/SC27XX relax cnt function define */
	int relax_cnt_threshold;
	bool relax_cnt_int_ocurred;
};

/*
 * struct sprd_fgu_info: describe the sprd fgu device
 * @regmap: regmap for register access
 * @base: the base offset for the controller
 * @dev: platform device
 * @cur_1mv_adc: ADC0 value corresponding to 1 mV
 * @cur_1000ma_adc: ADC0 value corresponding to 1000 mA
 * @cur_1code_lsb: ADC0 current value corresponding to 1 code
 * @vol_1000mv_adc: ADC1 value corresponding to 1000 mV
 * @cur_zero_point_adc: ADC0 current zero point adc value
 * @calib_resist: the real resistance of coulomb counter chip in uOhm
 * @standard_calib_resist: the standard resistance of coulomb counter chip in uOhm
 * @lock: protect the structure
 * @ops: pointer of sprd fgu device
 * @slp_cap_calib: struct of sleep capacity calib
 * @pdata: struct of pdata
 */
struct sprd_fgu_info {
	struct regmap *regmap;
	u32 base;
	struct device *dev;
	int cur_1mv_adc;
	int cur_1000ma_adc;
	int cur_1code_lsb;
	int vol_1000mv_adc;
	int cur_zero_point_adc;
	int calib_resist;
	int standard_calib_resist;
	struct mutex lock;
	struct sprd_fgu_device_ops *ops;
	struct sprd_fgu_sleep_capacity_calibration slp_cap_calib;
	const struct sprd_fgu_variant_data *pdata;
};

struct sprd_fgu_device_ops {
	int (*enable_fgu_module)(struct sprd_fgu_info *info, bool enable);
	int (*clr_fgu_int)(struct sprd_fgu_info *info);
	int (*clr_fgu_int_bit)(struct sprd_fgu_info *info, enum sprd_fgu_int_command int_cmd);
	int (*get_fgu_sts)(struct sprd_fgu_info *info,
			   enum sprd_fgu_sts_command sts_cmd, int *fgu_sts);
	int (*enable_fgu_int)(struct sprd_fgu_info *info,
			      enum sprd_fgu_int_command int_cmd, bool enable);
	int (*get_fgu_int)(struct sprd_fgu_info *info, int *int_sts);
	int (*enable_relax_cnt_mode)(struct sprd_fgu_info *info);
	int (*suspend_calib_check_relax_counter_sts)(struct sprd_fgu_info *info);
	int (*cap2mah)(struct sprd_fgu_info *info, int total_mah, int cap);
	int (*set_low_overload)(struct sprd_fgu_info *info, int vol);
	int (*set_high_overload)(struct sprd_fgu_info *info, int vol);
	int (*get_vbat_now)(struct sprd_fgu_info *info, int *val);
	int (*get_vbat_avg)(struct sprd_fgu_info *info, int *val);
	int (*get_vbat_buf)(struct sprd_fgu_info *info, int index, int *val);
	int (*get_current_now)(struct sprd_fgu_info *info, int *val);
	int (*get_current_avg)(struct sprd_fgu_info *info, int *val);
	int (*get_current_buf)(struct sprd_fgu_info *info, int index, int *val);
	int (*reset_cc_mah)(struct sprd_fgu_info *info, int total_mah, int init_cap);
	int (*get_cc_uah)(struct sprd_fgu_info *info, int *cc_uah, bool is_adjust);
	int (*adjust_cap)(struct sprd_fgu_info *info, int cap);
	int (*set_cap_delta_thre)(struct sprd_fgu_info *info, int total_mah, int cap);
	int (*get_relax_cur_low)(struct sprd_fgu_info *info, int *cur_sts);
	int (*get_relax_power_low)(struct sprd_fgu_info *info, int *power_sts);
	int (*get_power_low_cnt_int)(struct sprd_fgu_info *info, int *int_sts);
	int (*relax_mode_config)(struct sprd_fgu_info *info);
	int (*fgu_calibration)(struct sprd_fgu_info *info);
	int (*get_poci)(struct sprd_fgu_info *info, int *val);
	int (*get_pocv)(struct sprd_fgu_info *info, int *val);
	bool (*is_first_poweron)(struct sprd_fgu_info *info);
	int (*save_boot_mode)(struct sprd_fgu_info *info, int boot_mode);
	int (*read_last_cap)(struct sprd_fgu_info *info, int *cap);
	int (*save_last_cap)(struct sprd_fgu_info *info, int cap);
	int (*read_normal_temperature_cap)(struct sprd_fgu_info *info, int *cap);
	int (*save_normal_temperature_cap)(struct sprd_fgu_info *info, int cap);
	int (*get_reg_val)(struct sprd_fgu_info *info, int offset, int *reg_val);
	int (*set_reg_val)(struct sprd_fgu_info *info, int offset, int reg_val);
	void (*hw_init)(struct sprd_fgu_info *info, struct power_supply *psy);
	void (*dump_fgu_info)(struct sprd_fgu_info *info,
			      enum sprd_fgu_dump_fgu_info_level dump_level);
	void (*remove)(struct sprd_fgu_info *info);
	void (*shutdown)(struct sprd_fgu_info *info);
};
#if IS_ENABLED(CONFIG_FUEL_GAUGE_SC27XX)
struct sprd_fgu_info *sc27xx_fgu_info_register(struct device *dev);
#elif IS_ENABLED(CONFIG_FUEL_GAUGE_UMP96XX)
struct sprd_fgu_info *ump96xx_fgu_info_register(struct device *dev);
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
/* rtc reg default value */
#define SPRD_FGU_DEFAULT_CAP				GENMASK(11, 0)
#define SPRD_FGU_NORMAL_POWERON				0x5
#define SPRD_FGU_RTC2_RESET_VALUE			0xA05
/* uusoc vbat */
#define SPRD_FGU_LOW_VBAT_REC_REGION			3450
#define SPRD_FGU_LOW_VBAT_REGION			3400
#define SPRD_FGU_LOW_VBAT_REGION0			3400
#define SPRD_FGU_LOW_VBAT_REGION1			3350
#define SPRD_FGU_ABSOLUTE_LOW_VBAT_REGION		3300
#define SPRD_FGU_LOW_TEMP_LOW_VBAT_REGION		3200
#define SPRD_FGU_LOW_VBAT_REC_REGION			3450

#define SPRD_FGU_LOW_VBAT_UUSOC_STEP			7
#define SPRD_FGU_LOW_VBAT_UUSOC_STEP0			7
#define SPRD_FGU_LOW_VBAT_UUSOC_STEP1			4
#define SPRD_FGU_ABSOLUTE_LOW_VBAT_UUSOC_STEP		2
/* sleep calib */
#define SPRD_FGU_SLP_CAP_CALIB_SLP_TIME			300
#define SPRD_FGU_CAP_CALIB_TEMP_LOW			100
#define SPRD_FGU_CAP_CALIB_TEMP_HI			450
#define SPRD_FGU_SR_ARRAY_LEN				100
#define SPRD_FGU_SR_STOP_CHARGE_TIMES			(30 * 60)
#define SPRD_FGU_SR_SLEEP_MIN_TIME_S			(10 * 60)
#define SPRD_FGU_SR_AWAKE_MAX_TIME_S			90
#define SPRD_FGU_SR_AWAKE_BIG_CUR_MAX_TIME_S		30
#define SPRD_FGU_SR_SLEEP_AVG_CUR_MA			30
#define SPRD_FGU_SR_AWAKE_AVG_CUR_MA			200
#define SPRD_FGU_SR_LAST_SLEEP_TIME_S			(4 * 60)
#define SPRD_FGU_SR_LAST_AWAKE_TIME_S			30
#define SPRD_FGU_SR_DUTY_RATIO				95
#define SPRD_FGU_SR_TOTAL_TIME_S			(30 * 60)
#define SPRD_FGU_SR_VALID_VOL_CNT			3
#define SPRD_FGU_SR_MAX_VOL_MV				4500
#define SPRD_FGU_SR_MIN_VOL_MV				3400
/* discharing calibration */
#define SPRD_FGU_CALIB_CAP30				30
#define SPRD_FGU_CAP_CALIB_ALARM_CAP			30
/* track cap */
#define SPRD_FGU_TRACK_CAP_START_VOLTAGE		3650
#define SPRD_FGU_TRACK_CAP_START_CURRENT		50
#define SPRD_FGU_TRACK_HIGH_TEMP_THRESHOLD		450
#define SPRD_FGU_TRACK_LOW_TEMP_THRESHOLD		150
#define SPRD_FGU_TRACK_TIMEOUT_THRESHOLD		108000
#define SPRD_FGU_TRACK_NEW_OCV_VALID_THRESHOLD		(SPRD_FGU_TRACK_TIMEOUT_THRESHOLD / 60)
#define SPRD_FGU_TRACK_START_CAP_HTHRESHOLD		200
#define SPRD_FGU_TRACK_START_CAP_LTHRESHOLD		10
#define SPRD_FGU_TRACK_START_CAP_SWOCV_HTHRESHOLD	100
#define SPRD_FGU_TRACK_WAKE_UP_MS			16000
#define SPRD_FGU_TRACK_UPDATING_WAKE_UP_MS		200
#define SPRD_FGU_TRACK_DONE_WAKE_UP_MS			6000
#define SPRD_FGU_TRACK_OCV_VALID_TIME			15
#define SPRD_FGU_TRACK_CHARGE_CYCLE_DIFF		200000
#define SPRD_FGU_CAPACITY_TRACK_0S			0
#define SPRD_FGU_CAPACITY_TRACK_3S			3
#define SPRD_FGU_CAPACITY_TRACK_15S			15
#define SPRD_FGU_CAPACITY_TRACK_100S			100
#define SPRD_FGU_WORK_MS				msecs_to_jiffies(15000)
#define SPRD_FGU_PROBE_TIMEOUT				msecs_to_jiffies(3000)

/* unuse cap */
#define SPRD_FGU_RESIST_ALG_REIST_CNT			40
#define SPRD_FGU_RESIST_ALG_OCV_GAP_UV			20000
#define SPRD_FGU_RESIST_ALG_OCV_CNT			10
#define SPRD_FGU_RBAT_CMP_MOH				10
/* RTC OF 2021-08-06 15 : 44*/
#define SPRD_FGU_MISCDATA_RTC_TIME			(1621355101)
#define SPRD_FGU_SHUTDOWN_TIME				(15 * 60)
/* debug value config */
#define SPRD_FGU_DEBUG_TEMP_CELSIUS			200
#define SPRD_FGU_DEBUG_VBAT_NOW_UV			4000000
#define SPRD_FGU_DEBUG_CUR_NOW_UV			1000000
#define SPRD_FGU_DEBUG_VBUS_UV				5000000
#define SPRD_FGU_DEBUG_OCV_UV				4000000
/* others define */
#define SPRD_FGU_NORMAL_WORK_15S			15
#define SPRD_FGU_QUICKEN_WORK_8S			8
#define SPRD_FGU_CAP_CALC_WORK_8S			8
#define SPRD_FGU_CAP_CALC_WORK_15S			15
#define SPRD_FGU_CAP_CALC_WORK_LOW_TEMP			50
#define SPRD_FGU_CAP_CALC_WORK_LOW_CAP			50
#define SPRD_FGU_CAP_CALC_WORK_BIG_CURRENT		3000
#define SPRD_FGU_POCV_VOLT_THRESHOLD			3400
#define SPRD_FGU_TEMP_BUFF_CNT				10
#define SPRD_FGU_LOW_TEMP_REGION			100
#define SPRD_FGU_CURRENT_BUFF_CNT			8
#define SPRD_FGU_DISCHG_CNT				4
#define SPRD_FGU_VOLTAGE_BUFF_CNT			8
#define SPRD_FGU_MAGIC_NUMBER				0x5a5aa5a5
#define SPRD_FGU_DEBUG_EN_CMD				0x5a5aa5a5
#define SPRD_FGU_DEBUG_DIS_CMD				0x5a5a5a5a
#define SPRD_FGU_GOOD_HEALTH_CMD			0x7f7f7f7f
#define SPRD_FGU_FCC_PERCENT				1000
#define SPRD_FGU_RESERVE_SOC				20
#define SPRD_FGU_EXTCON_SINK				3
#define SPRD_FGU_GET_CHG_TYPE_RETRY_CNT			12
#define SPRD_FGU_CALIB_LOW_DENS_CAP_DIFF		30
#define SPRD_FGU_CALIB_HIGH_DENS_CAP_DIFF		150
#define SPRD_FGU_IS_SWITCH_BAT_PARA_VOL_THRES		4100
#define SPRD_FGU_REG_MAX				0x260
#define SPRD_FGU_FULL_PERCENT				100
#define SPRD_FGU_RESUME_TEMP_BUFF			1

#define OCV_ALARM_NUM	(5)
#define OCV_ERR			BIT(0)
#define VBAT_ERR		BIT(1)
#define VBAT_AVG_ERR		BIT(2)
#define VBAT_CUR_ERR		BIT(3)
#define VBAT_CUR_AVG_ERR	BIT(4)

#define interpolate(x, x1, y1, x2, y2) \
	((y1) + ((((y2) - (y1)) * ((x) - (x1))) / ((x2) - (x1))))

struct power_supply_vol_temp_table {
	int vol;	/* microVolts */
	int temp;	/* celsius */
};

struct power_supply_capacity_temp_table {
	int temp;	/* celsius */
	int cap;	/* capacity percentage */
};

enum sprd_fgu_track_state {
	CAP_TRACK_INIT,
	CAP_TRACK_IDLE,
	CAP_TRACK_UPDATING,
	CAP_TRACK_DONE,
	CAP_TRACK_ERR,
};

enum sprd_fgu_track_mode {
	CAP_TRACK_MODE_UNKNOWN,
	CAP_TRACK_MODE_SW_OCV,
	CAP_TRACK_MODE_POCV,
	CAP_TRACK_MODE_LP_OCV,
};

struct sprd_fgu_ocv_info {
	s64 ocv_time_stamp;
	int ocv_uv;
	bool valid;
//	bool is_low_density;
};

struct sprd_fgu_track_capacity {
	enum sprd_fgu_track_state state;
	bool clear_cap_flag;
	int start_cc_mah;
	int start_cap;
	int end_vol;
	int end_cur;
	s64 start_time;
	bool cap_tracking;
	int learned_mah;
//	int start_aging_bat_id;
	struct sprd_fgu_ocv_info lpocv_info;
	struct sprd_fgu_ocv_info pocv_info;
	density_ocv_table *dens_ocv_table;
	int dens_ocv_table_len;
	enum sprd_fgu_track_mode mode;
};

struct sprd_fgu_debug_info {
	bool temp_debug_en;
	bool vbat_now_debug_en;
	bool ocv_debug_en;
	bool cur_now_debug_en;
	bool batt_present_debug_en;
	bool chg_vol_debug_en;
	bool batt_health_debug_en;

	int debug_temp;
	int debug_vbat_now;
	int debug_ocv;
	int debug_cur_now;
	bool debug_batt_present;
	int debug_chg_vol;
	int debug_batt_health;

	int sel_reg_id;
};

struct sprd_fgu_sysfs {
	char *name;
	struct attribute_group attr_g;
	struct device_attribute attr_sprd_fgu_dump_info;
	struct device_attribute attr_sprd_fgu_sel_reg_id;
	struct device_attribute attr_sprd_fgu_reg_val;
	struct device_attribute attr_sprd_fgu_enable_sleep_calib;
	struct device_attribute attr_sprd_fgu_relax_cnt_th;
	struct device_attribute attr_sprd_fgu_relax_cur_th;
	struct device_attribute attr_sprd_fgu_charge_now;
	struct attribute *attrs[8];

	struct sprd_fgu_data *data;
};

/*
 * struct sprd_fgu_cap_remap_table
 * @cnt: record the counts of battery capacity of this scope
 * @lcap: the lower boundary of the capacity scope before transfer
 * @hcap: the upper boundary of the capacity scope before transfer
 * @lb: the lower boundary of the capacity scope after transfer
 * @hb: the upper boundary of the capacity scope after transfer
 */
struct sprd_fgu_cap_remap_table {
	int cnt;
	int lcap;
	int hcap;
	int lb;
	int hb;
};

/*
struct ocv_alarm {
	unsigned int calib_cap;
	int ocv_alarm_cap;
};

struct sprd_fgu_vbat_info {
	struct ocv_alarm ocv_alarm_info[OCV_ALARM_NUM];
	int ocv_uv;
	int vbatt_mv;
	int vbat_avg_mv;
	int vbat_cur_ma;
	int vbat_cur_avg_ma;
	unsigned int vbat_err;
	bool absolute_charger_mode;
};
*/

/*
 * struct sprd_fgu_data: describe the FGU device
 * @regmap: regmap for register access
 * @dev: platform device
 * @battery: battery power supply
 * @base: the base offset for the controller
 * @lock: protect the structure
 * @gpiod: GPIO for battery detection
 * @channel: IIO channel to get battery temperature
 * @charge_chan: IIO channel to get charge voltage
 * @internal_resist: the battery internal resistance in mOhm
 * @total_mah: the total capacity of the battery in mAh
 * @init_cap: the initial capacity of the battery in mAh
 * @alarm_cap: the alarm capacity
 * @normal_temp_cap: the normal temperature capacity
 * @max_volt_uv: the maximum constant input voltage in millivolt
 * @min_volt_uv: the minimum drained battery voltage in microvolt
 * @boot_volt_uv: the voltage measured during boot in microvolt
 * @table_len: the capacity table length
 * @temp_table_len: temp_table length
 * @cap_table_len£ºthe capacity temperature table length
 * @resist_table_len: the resistance table length
 * @comp_resistance: the coulomb counter internal and the board ground resistance
 * @index: record temp_buff array index
 * @temp_buff: record the battery temperature for each measurement
 * @bat_temp: the battery temperature
 * @cap_table: capacity table with corresponding ocv
 * @temp_table: the NTC voltage table with corresponding battery temperature
 * @cap_temp_table: the capacity table with corresponding temperature
 * @resist_table: resistance percent table with corresponding temperature
 */

struct sprd_fgu_data {
	struct regmap *regmap;
	struct device *dev;
	struct power_supply *battery;
	u32 base;
	struct mutex lock;
	struct mutex discharge_lock;
	struct gpio_desc *gpiod;
	struct iio_channel *channel;
	struct iio_channel *charge_chan;
//	struct sprd_fgu_vbat_info vbat_info;
	bool bat_present;
	int internal_resist;
	int total_mah;
	int design_mah;
//	int charge_full_mah;
	int init_cap;
	int alarm_cap;
	int boot_cap;
	int boot_ibat;
	int boot_vbat;
	int normal_temp_cap;
//	int temp_cap;
//	int smooth_soc;
//	int smooth_soc_decimal;
//	int normal_cap_diff;
	int bat_soc;
	int uusoc_mah;
	int init_mah;
	int cc_uah;
	int max_volt_uv;
	int min_volt_uv;
	int boot_volt_uv;
	int table_len;
	int temp_table_len;
	int cap_table_len;
	int resist_table_len;
//	int fullcap_table_len;
	int cap_calib_dens_ocv_table_len;
	int first_calib_volt;
	int first_calib_cap;
	int uusoc_vbat;
	unsigned int comp_resistance;
	int batt_ovp_threshold;
	int index;
	int ocv_uv;
	int batt_mv;
	int temp_buff[SPRD_FGU_TEMP_BUFF_CNT];
	int cur_now_buff[SPRD_FGU_CURRENT_BUFF_CNT];
	bool dischg_trend[SPRD_FGU_DISCHG_CNT];
	int bat_temp;
	bool online;
	bool is_first_poweron;
	bool is_ovp;
	bool invalid_pocv;
	bool sw_pocv_flag;
	u32 chg_type;
	int cap_remap_total_cnt;
	int cap_remap_full_percent;
	int cap_remap_table_len;
	struct sprd_fgu_cap_remap_table *cap_remap_table;
	struct sprd_fgu_track_capacity track;
	struct power_supply_battery_ocv_table *cap_table;
	struct power_supply_vol_temp_table *temp_table;
	struct power_supply_capacity_temp_table *cap_temp_table;
//	struct sprd_battery_temp_fullcap_table *temp_fullcap_table;
	struct power_supply_resistance_temp_table *resist_table;
	struct usb_phy *usb_phy;
	struct notifier_block usb_notify;
	int chg_sts;
	struct sprd_fgu_debug_info debug_info;
	density_ocv_table *cap_calib_dens_ocv_table;
//	struct sprd_battery_cycles_fcc_table *battery_cycles_fcc_table;

	struct sprd_fgu_sysfs *sysfs;
	struct delayed_work cap_track_work;
	struct delayed_work fgu_work;
	struct sprd_fgu_info *fgu_info;

	/* typec extcon */
	struct extcon_dev *edev;
	struct notifier_block pd_swap_notify;
	struct notifier_block extcon_nb;
	struct work_struct typec_extcon_work;
	bool is_sink;
	bool use_typec_extcon;

	/* multi resistance */
	int *target_rbat_table;
	int **rbat_table;
	int rabat_table_len;
	int *rbat_temp_table;
	int rbat_temp_table_len;
	int *rbat_ocv_table;
	int rbat_ocv_table_len;
	bool support_multi_resistance;
	bool support_debug_log;

	/* boot capacity calibration */
	bool support_boot_calib;
	s64 shutdown_rtc_time;

	/* charge cycle */
	int charge_cycle;

	/* basp */
	bool support_basp;
	int basp_volt_uv;
	struct sprd_battery_ocv_table **basp_ocv_table;
	int basp_ocv_table_len;
	int *basp_full_design_table;
	int basp_full_design_table_len;
	int *basp_voltage_max_table;
	int basp_voltage_max_table_len;

	/* battery aging function */
//	bool support_bat_aging;
//	int last_aging_bat_id;
//	bool dynamic_update_bat_para_flag;

	int work_enter_cc_uah;
	int work_exit_cc_uah;
	int last_cc_uah;
	s64 work_enter_times;
	s64 work_exit_times;

	/* sleep resume calibration */
	s64 awake_times;
	s64 sleep_times;
	s64 stop_charge_times;
	int sleep_cc_uah;
	int awake_cc_uah;
	int awake_avg_cur_ma;
	int sr_time_sleep[SPRD_FGU_SR_ARRAY_LEN];
	int sr_time_awake[SPRD_FGU_SR_ARRAY_LEN];
	int sr_index_sleep;
	int sr_index_awake;
	int sr_ocv_uv;

	struct completion probe_init;
	bool probe_initialized;
	bool is_probe_done;
};
#endif
