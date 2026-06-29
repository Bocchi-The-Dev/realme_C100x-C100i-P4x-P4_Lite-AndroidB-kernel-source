// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[8547a]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>

#include <trace/events/sched.h>
#include <linux/ktime.h>
#include <uapi/linux/sched/types.h>

#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"

#include "../charger_ic/oplus_switching.h"
#include "../oplus_ufcs.h"
#include "../oplus_chg_track.h"
#include "../oplus_chg_module.h"
#include "../voocphy/oplus_sy6542.h"
#include "oplus_sy6542a.h"
#include "../voocphy/oplus_voocphy.h"
#include "../voocphy/oplus_cp_intf.h"
#include "../oplus_pps.h"

static struct oplus_sy6542a_ufcs *sy6542a_ufcs = NULL;
static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;

static bool error_reported = false;
extern void oplus_chg_sc8547_error(int report_flag, int *buf, int len);
static irqreturn_t sy6542_protect_interrupt_handler(struct oplus_voocphy_manager *chip);

#define DEFUALT_VBUS_LOW 150
#define DEFUALT_VBUS_HIGH 250
#define I2C_ERR_NUM 10
#define MAIN_I2C_ERROR (1 << 0)

static int sy6542_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data);
static int sy6542_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip, int err_type, u8 reg);
static int sy6542_track_upload_cp_err_info(
	struct oplus_voocphy_manager *chip, int err_type, u8 flt_flag_0, u8 flt_flag_1, u8 flt_flag_2);
static u8 sy6542_get_int_value(struct oplus_voocphy_manager *chip);

static void sy6542_i2c_error(bool happen)
{
	int report_flag = 0;
	if (!oplus_voocphy_mg || error_reported)
		return;

	if (happen) {
		oplus_voocphy_mg->voocphy_iic_err = true;
		oplus_voocphy_mg->voocphy_iic_err_num++;
		if (oplus_voocphy_mg->voocphy_iic_err_num >= I2C_ERR_NUM) {
			report_flag |= MAIN_I2C_ERROR;
			oplus_chg_sc8547_error(report_flag, NULL, 0);
			error_reported = true;
		}
	} else {
		oplus_voocphy_mg->voocphy_iic_err_num = 0;
		oplus_chg_sc8547_error(0, NULL, 0);
	}
}

/************************************************************************/
static int __sy6542_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		sy6542_i2c_error(true);
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		sy6542_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		return ret;
	}
	sy6542_i2c_error(false);
	*data = (u8)ret;

	return 0;
}

static int __sy6542_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		sy6542_i2c_error(true);
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
		sy6542_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		return ret;
	}
	sy6542_i2c_error(false);
	return 0;
}

static int sy6542_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sy6542_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sy6542_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sy6542_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sy6542_update_bits(struct i2c_client *client, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sy6542_read_byte(client, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sy6542_write_byte(client, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}



static s32 sy6542_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		oplus_pps_notify_master_cp_error();
		sy6542_i2c_error(true);
		pr_err("i2c write word fail: can't write 0x%02X to reg:0x%02X \n", val, reg);
		sy6542_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	sy6542_i2c_error(false);
	mutex_unlock(&i2c_rw_lock);
	return 0;
}

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int sy6542_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int sy6542_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip, int err_type, u8 reg)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	curr_time = sy6542_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->debug_force_i2c_err)
		err_type = -chip->debug_force_i2c_err;

	mutex_lock(&chip->track_i2c_err_lock);
	if (chip->i2c_err_uploading) {
		pr_info("i2c_err_uploading, should return\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->i2c_err_load_trigger)
		kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->i2c_err_load_trigger) {
		pr_err("i2c_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->i2c_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->i2c_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_CP_ABNORMAL;
	chip->i2c_err_uploading = true;
	upload_count++;
	pre_upload_time = sy6542_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "sy6542a");
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_I2C_ERR);

	oplus_chg_track_get_i2c_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$access_reg@@0x%02x", reg);
	schedule_delayed_work(&chip->i2c_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void sy6542_track_i2c_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip =
		container_of(dwork, struct oplus_voocphy_manager, i2c_err_load_trigger_work);

	if (!chip->i2c_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = NULL;
	chip->i2c_err_uploading = false;
}

static int sy6542_track_upload_cp_err_info(
				struct oplus_voocphy_manager *chip, int err_type, u8 flt_flag_0, u8 flt_flag_1, u8 flt_flag_2)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	curr_time = sy6542_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (err_type == TRACK_CP_ERR_DEFAULT) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	mutex_lock(&chip->track_cp_err_lock);
	if (chip->cp_err_uploading) {
		pr_info("cp_err_uploading, should return\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->cp_err_load_trigger)
		kfree(chip->cp_err_load_trigger);
	chip->cp_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->cp_err_load_trigger) {
		pr_err("cp_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->cp_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->cp_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_CP_ABNORMAL;
	chip->cp_err_uploading = true;
	upload_count++;
	pre_upload_time = sy6542_track_get_local_time_s();
	mutex_unlock(&chip->track_cp_err_lock);

	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "sy6542");
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_CP_ERR);

	oplus_chg_track_get_cp_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$reg_info@@0x%02x=0x%02x,0x%02x=0x%02x,0x%02x=0x%02x",
			  SY6542_REG_08, flt_flag_0, SY6542_REG_0A, flt_flag_1, SY6542_REG_0C, flt_flag_2);
	schedule_delayed_work(&chip->cp_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void sy6542_track_cp_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip =
		container_of(dwork, struct oplus_voocphy_manager, cp_err_load_trigger_work);

	if (!chip->cp_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->cp_err_load_trigger));
	kfree(chip->cp_err_load_trigger);
	chip->cp_err_load_trigger = NULL;
	chip->cp_err_uploading = false;
}

static int sy6542_track_debugfs_init(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_sy6542;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_sy6542 = debugfs_create_dir("sy6542", debugfs_root);
	if (!debugfs_sy6542) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_cp_err = TRACK_WLS_TRX_ERR_DEFAULT;
	debugfs_create_u32("debug_force_i2c_err", 0644, debugfs_sy6542, &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_cp_err", 0644, debugfs_sy6542, &(chip->debug_force_cp_err));

	return ret;
}

static int sy6542_track_init(struct oplus_voocphy_manager *chip)
{
	int rc;

	if (!chip)
		return -EINVAL;

	mutex_init(&chip->track_i2c_err_lock);
	mutex_init(&chip->track_cp_err_lock);
	mutex_init(&chip->track_upload_lock);
	chip->i2c_err_uploading = false;
	chip->i2c_err_load_trigger = NULL;
	chip->cp_err_uploading = false;
	chip->cp_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work, sy6542_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->cp_err_load_trigger_work, sy6542_track_cp_err_load_trigger_work);
	rc = sy6542_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("sy6542 debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int sy6542_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	ret = sy6542_write_byte(chip->client, SY6542_REG_31, (u8)(val & 0xff));
	if (ret < 0) {
		pr_err("failed: write predata\n");
		return -1;
	}

	ret = sy6542_write_byte(chip->client, SY6542_REG_32, (u8)((val >> 8) & 0xff));
	if (ret < 0) {
		pr_err("failed: write predata\n");
		return -1;
	}

	pr_info("write predata 0x%0x\n", val);
	return ret;
}

static int sy6542_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	ret = sy6542_write_word(chip->client, SY6542_REG_2C, val);
	if (ret < 0) {
		pr_err("write txbuff\n");
		return -1;
	}

	return ret;
}

static int sy6542_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	u8 data = 0;
	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	sy6542_read_byte(chip->client, SY6542_REG_2E, &data);
	chip->voocphy_rx_buff = data;
	sy6542_read_byte(chip->client, SY6542_REG_2F, &data);
	chip->vooc_flag = data;

	pr_info("vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n", chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static void sy6542_update_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[4] = { 0 };
	int i = 0;
	s32 ret = 0;

	/*int_flag*/
	sy6542_get_int_value(chip);
	chip->int_flag = 0;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SY6542_REG_13, 2, data_block);
	if (ret < 0) {
		sy6542_i2c_error(true);
		chg_err("sy6542_update_data read vsys vbat error \n");
	} else {
		sy6542_i2c_error(false);
	}
	for (i = 0; i < 2; i++) {
		pr_info("read vsys vbat data_block[%d] = %u\n", i,
			data_block[i]);
	}
	chip->cp_vbat = (((data_block[0] & 0x7f) << 8) | data_block[1]) * SY6542_VOUT_ADC_LSB; //19 20

	memset(data_block, 0, sizeof(u8) * 4);
	ret = i2c_smbus_read_i2c_block_data(chip->client, SY6542_REG_11, 2, data_block);
	if (ret < 0) {
		sy6542_i2c_error(true);
		chg_err("sy6542_update_data read vsys vbat error \n");
	} else {
		sy6542_i2c_error(false);
	}
	for (i = 0; i < 2; i++)
		pr_info("read ichg ichg data_block[%d] = %u\n", i, data_block[i]);
	chip->cp_ichg = (((data_block[0] & 0x7f) << 8) | data_block[1]) * SY6542_IBUS_ADC_LSB;

	memset(data_block, 0, sizeof(u8) * 4);
	ret = i2c_smbus_read_i2c_block_data(chip->client, SY6542_REG_0F, 2, data_block);
	if (ret < 0) {
		sy6542_i2c_error(true);
		chg_err("sy6542_update_data read vsys vbat error \n");
	} else {
		sy6542_i2c_error(false);
	}
	for (i = 0; i < 2; i++)
		pr_info("read ichg vbus data_block[%d] = %u\n", i, data_block[i]);
	chip->cp_vbus = (((data_block[0] & 0x7f) << 8) | data_block[1]) * SY6542_VBUS_ADC_LSB;
	chip->cp_vsys = chip->cp_vbus / 2;


	pr_info("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d cp_vac = "
		"%d int_flag = %d", chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat,
		chip->cp_vac, chip->int_flag);

}

static int sy6542_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	int cp_ichg = 0;
	u8 cp_enable = 0;
	s32 ret = 0;

	sy6542_get_chg_enable(chip, &cp_enable);

	if (cp_enable == 0)
		return 0;
	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SY6542_REG_11, 2, data_block);
	if (ret < 0) {
		sy6542_i2c_error(true);
		pr_err("sy6542 read ichg error \n");
	} else {
		sy6542_i2c_error(false);
	}

	cp_ichg = (((data_block[0] & 0x7f) << 8) | data_block[1]) * SY6542_IBUS_ADC_LSB;

	return cp_ichg;
}

static int sy6542_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	s32 ret = 0;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SY6542_REG_13, 2, data_block);
	if (ret < 0) {
		sy6542_i2c_error(true);
		pr_err("sy6542 read vbat error \n");
	} else {
		sy6542_i2c_error(false);
	}

	chip->cp_vbat = (((data_block[0] & 0x7f) << 8) | data_block[1]) * SY6542_VOUT_ADC_LSB;

	return chip->cp_vbat;
}

static int sy6542_get_cp_vbus(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	s32 ret = 0;

	/* parse data_block for improving time of interrupt */
	ret = i2c_smbus_read_i2c_block_data(chip->client, SY6542_REG_0F, 2, data_block);
	if (ret < 0) {
		sy6542_i2c_error(true);
		pr_err("sy6542 read vbat error \n");
	} else {
		sy6542_i2c_error(false);
	}

	chip->cp_vbus = (((data_block[0] & 0x7f) << 8) | data_block[1]) * SY6542_VBUS_ADC_LSB;

	return chip->cp_vbus;
}

/*********************************************************************/
static int sy6542_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 val;
	if (enable)
		val = SY6542_RESET_REG;
	else
		val = SY6542_NO_REG_RESET;

	val <<= SY6542_REG_RESET_SHIFT;

	pr_err("sy6542_reg_reset:%d \n", val);
	ret = sy6542_update_bits(chip->client, SY6542_REG_00,
				 SY6542_REG_RESET_MASK, val);

	return ret;
}

static bool sy6542a_hw_version_check(struct oplus_voocphy_manager *chip)
{
	int ret;
	u8 val;

	return true;
	ret = sy6542_read_byte(chip->client, SY6542_REG_36, &val);
	if (val == SY6542A_DEVICE_ID)
		return true;
	else
		return false;
}

static int sy6542_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sy6542_read_byte(chip->client, SY6542_REG_00, data);
	if (ret < 0) {
		pr_err("SY6542_REG_05\n");
		return -1;
	}

	*data &= 0x20;
	*data = *data >> 5;

	return ret;
}

static int sy6542_get_voocphy_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sy6542_read_byte(chip->client, SY6542_REG_2B, data);
	if (ret < 0) {
		pr_err("SY6542_REG_2B\n");
		return -1;
	}

	return ret;
}

static void sy6542_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	int i = 0, p = 0;

	if (!chip) {
		chg_err("!!!!! oplus_voocphy_manager chip NULL");
		return;
	}

	for (i = 0; i < 35; i++) {
		p = p + 1;
		sy6542_read_byte(chip->client, i, &chip->reg_dump[p]);
		chg_err("addr[0x%2x], data[0x%2x]\n", i, chip->reg_dump[p]);
	}
	for (i = 0; i < 9; i++) {
		p = p + 1;
		sy6542_read_byte(chip->client, 40 + i, &chip->reg_dump[p]);
		chg_err("addr[0x%2x], data[0x%2x]\n", 40 + i, chip->reg_dump[p]);
	}
	p = p + 1;
	sy6542_read_byte(chip->client, SY6542_REG_AE, &chip->reg_dump[p]);
	chg_err("addr[0x%2x], data[0x%2x]\n", SY6542_REG_AE, chip->reg_dump[p]);
}

static int sy6542_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sy6542_read_byte(chip->client, SY6542_REG_0E, data);
	if (ret < 0) {
		pr_err("SY6542_REG_11\n");
		return -1;
	}

	*data = *data >> 7;

	return ret;
}

static u8 sy6542_match_err_value(
				struct oplus_voocphy_manager *chip, u8 flt_flag_0, u8 flt_flag_1, u8 flt_flag_2)
{
	int err_type = TRACK_CP_ERR_DEFAULT;

	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -EINVAL;
	}

	if (flt_flag_2 & SY6542_CFLY_CDRV_FLAG_MASK)
		err_type = TRACK_CP_ERR_CFLY_CDRV_FAULT;
	else if (flt_flag_1 & SY6542_CONV_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_CONV_OCP;
	else if (flt_flag_1 & SY6542_TDIE_OTP_FLAG_MASK)
		err_type = TRACK_CP_ERR_TDIE_OTP;
	else if (flt_flag_1 & SY6542_VBAT_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBAT_OVP;
	else if (flt_flag_0 & SY6542_VBUS_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS_OVP;
	else if (flt_flag_0 & SY6542_IBUS_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBUS_OCP;
	else if (flt_flag_1 & SY6542_VOUT_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VOUT_OVP;
	else if (flt_flag_1 & SY6542_IBUS_RCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBUS_RCP;
	else if (flt_flag_2 & SY6542_WDT_TIMEOUT_FLAG_MASK)
		err_type = TRACK_CP_ERR_WD_TIMEOUT;
	else if (flt_flag_0 & SY6542_IBUS_UCP_TIMEOUT_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBUS_UCP_TIMEOUT;
	else if (flt_flag_1 & SY6542_IBAT_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBAT_OCP;
	else if (flt_flag_1 & SY6542_VBUS_HIGH_ERR_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS2OUT_ERRORHI;
	else if (flt_flag_1 & SY6542_VBUS_LOW_ERR_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS2OUT_ERRORLO;

	if (chip->debug_force_cp_err){
		err_type = chip->debug_force_cp_err;
		chip->debug_force_cp_err = TRACK_WLS_TRX_ERR_DEFAULT;
	}
	if (err_type != TRACK_CP_ERR_DEFAULT)
		sy6542_track_upload_cp_err_info(chip, err_type, flt_flag_0, flt_flag_1, flt_flag_2);

	return 0;
}

static u8 sy6542_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data_0 = 0;
	u8 data_1 = 0;
	u8 data_2 = 0;

	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -1;
	}

	ret = sy6542_read_byte(chip->client, SY6542_REG_08, &data_0);
	if (ret < 0) {
		pr_err(" read SY6542_REG_08 failed\n");
		return -1;
	}

	ret = sy6542_read_byte(chip->client, SY6542_REG_0A, &data_1);
	if (ret < 0) {
		pr_err(" read SY6542_REG_0A failed\n");
		return -1;
	}

	ret = sy6542_read_byte(chip->client, SY6542_REG_0C, &data_2);
	if (ret < 0) {
		pr_err(" read SY6542_REG_0C failed\n");
		return -1;
	}
	ufcs_err("REG08 = 0x%x REG0a = 0x%x REG0C = 0x%x\n", data_0, data_1, data_2);

	sy6542_match_err_value(chip, data_0, data_1, data_2);

	return 0;
}

static int sy6542_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	pr_err("sy6542_set_chg_enable:%d\n", enable);
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}


	if (enable)
		return sy6542_update_bits(chip->client, SY6542_REG_00,0x20, 0x20);
	else
		return sy6542_update_bits(chip->client, SY6542_REG_00,0x20, 0x00);
}

static void sy6542_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	u8 reg_data = 0;
	if (!chip) {
		pr_err("Failed\n");
		return;
	}

	if (enable) {
		reg_data = (0x20 | 0x14);
		sy6542_write_byte(chip->client, SY6542_REG_05, reg_data);
		sy6542_write_byte(chip->client, SY6542_REG_09, 0x82);
		sy6542_update_bits(chip->client, SY6542_REG_00,0x0F, 0x02);
	} else {
		reg_data = (0xa0 | 0x14);
		sy6542_write_byte(chip->client, SY6542_REG_05, reg_data);
	}

	ret = sy6542_read_byte(chip->client, SY6542_REG_05, &reg_data);
	if (ret < 0) {
		chg_err("SY6542_REG_05\n");
		return;
	}

	chg_err("pd_svooc config SY6542_REG_05 = %d\n", reg_data);
}

static bool sy6542_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;

	if (!chip) {
		pr_err("Failed\n");
		return false;
	}

	ret = sy6542_read_byte(chip->client, SY6542_REG_05, &data);
	if (ret < 0) {
		pr_err("SY6542_REG_05\n");
		return false;
	}

	pr_err("SY6542_REG_05 = 0x%0x\n", data);

	data = data >> 7;
	if (data == 1)
		return true;
	else
		return false;
}

static int sy6542_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	if (enable)
		return sy6542_write_byte(chip->client, SY6542_REG_0E, 0x80);
	else
		return sy6542_write_byte(chip->client, SY6542_REG_0E, 0x00);
}

static void sy6542_send_handshake(struct oplus_voocphy_manager *chip)
{
	sy6542_write_byte(chip->client, SY6542_REG_2B, 0x81);
}

static int sy6542_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	/* turn off mos */
	sy6542_write_byte(chip->client, SY6542_REG_01, 0x20);
	sy6542_write_byte(chip->client, SY6542_REG_00, 0x0B);
	sy6542_write_byte(chip->client, SY6542_REG_06, 0xF6);
	sy6542_write_byte(chip->client, SY6542_REG_03, 0x18);
	sy6542_write_byte(chip->client, SY6542_REG_04, 0xBC);
	sy6542_write_byte(chip->client, SY6542_REG_05, 0xAD);

	/* clear tx data */
	sy6542_write_byte(chip->client, SY6542_REG_1E, 0xC0);
	sy6542_write_byte(chip->client, SY6542_REG_0E, 0x00);

	/* disable vooc phy irq */
	sy6542_write_byte(chip->client, SY6542_REG_2C, 0x00);

	/* set D+ HiZ */
	sy6542_write_byte(chip->client, SY6542_REG_2D, 0x00);

	/* select big bang mode */

	/* disable vooc */
	sy6542_write_byte(chip->client, SY6542_REG_30, 0xFF);

	/* set predata */
	sy6542_write_word(chip->client, SY6542_REG_20, 0xD8);

	/* mask insert irq */
	sy6542_write_byte(chip->client, SY6542_REG_2B, 0x00);
	sy6542_write_byte(chip->client, SY6542_REG_31, 0x00);
	sy6542_write_byte(chip->client, SY6542_REG_32, 0x00);
	sy6542_write_byte(chip->client, SY6542_REG_0D, 0x80);
	sy6542_write_byte(chip->client, SY6542_REG_0E, 0x00);
	chg_err("oplus_vooc_reset_voocphy done");


	return VOOCPHY_SUCCESS;
}

static int sy6542_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	/*set predata to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us*/
	sy6542_write_word(chip->client, SY6542_REG_31, 0x0);

	/*dpdm*/
	sy6542_write_byte(chip->client, SY6542_REG_20, 0x04);
	sy6542_write_byte(chip->client, SY6542_REG_33, 0xD1);

	/*clear tx data*/
	sy6542_write_byte(chip->client, SY6542_REG_2C, 0x00);
	sy6542_write_byte(chip->client, SY6542_REG_2D, 0x00);

	/*vooc*/
	sy6542_write_byte(chip->client, SY6542_REG_30, 0x85);
	sy6542_send_handshake(chip);

	pr_info("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static irqreturn_t sy6542a_charger_interrupt(int irq, void *dev_id)
{
	irqreturn_t ret = IRQ_HANDLED;
	struct oplus_voocphy_manager *chip = dev_id;

	if (!chip) {
		return IRQ_HANDLED;
	}

	switch (chip->cp_work_mode) {
	case CP_WORKMODE_VOOCPHY:
		ret = oplus_voocphy_interrupt_handler(chip);
		break;
	case CP_WORKMODE_PPS:
		ret = sy6542_protect_interrupt_handler(chip);
		break;
	default:
		sy6542_reset_voocphy(chip);
		break;
	}
	return ret;

}

static int sy6542_init_device(struct oplus_voocphy_manager *chip)
{
	sy6542_write_byte(chip->client, SY6542_REG_0E, 0x00); /* ADC disable */
	sy6542_write_byte(chip->client, SY6542_REG_03, 0x18); /* VAC OVP:12V */
	sy6542_write_byte(chip->client, SY6542_REG_04, 0xbc); /* VBUS_OVP:10V */
	sy6542_write_byte(chip->client, SY6542_REG_05, 0xad); /* IBUS_OCP_UCP:3.75A */
	sy6542_write_byte(chip->client, SY6542_REG_06, 0xf6); /* VBAT_OVP:4.85V */
	sy6542_write_byte(chip->client, SY6542_REG_1E, 0xc0); /*UCP Falling = 640ms UCP_timeout dis*/
	sy6542_write_byte(chip->client, SY6542_REG_07, 0x34); /* IBAT OCP:disable */
	sy6542_write_byte(chip->client, SY6542_REG_2B, 0x00);
	sy6542_write_byte(chip->client, SY6542_REG_09, 0x82); /*Mask UCP rising*/
	sy6542_write_byte(chip->client, SY6542_REG_00, 0x0b);
	sy6542_write_byte(chip->client, SY6542_REG_0D, 0x80); /* mask insert irq */

	chip->cp_work_mode = CP_WORKMODE_VOOCPHY;
	pr_err("sy6542_init_device done");

	return 0;
}

static int sy6542_init_vooc(struct oplus_voocphy_manager *chip)
{
	pr_err(" >>>>start init vooc\n");

	sy6542_reg_reset(chip, true);
	sy6542_init_device(chip);

	sy6542_write_word(chip->client, SY6542_REG_31, 0x0);
	sy6542_write_byte(chip->client, SY6542_REG_20, 0x04);
	sy6542_write_byte(chip->client, SY6542_REG_32, 0x00);
	sy6542_write_byte(chip->client, SY6542_REG_33, 0xD1);
	sy6542_write_byte(chip->client, SY6542_REG_30, 0x85);

	return 0;
}

static int sy6542_irq_gpio_init(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->irq_gpio = of_get_named_gpio(node, "qcom,irq_gpio", 0);
	if (chip->irq_gpio < 0) {
		pr_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->irq_gpio)) {
			rc = gpio_request(chip->irq_gpio, "irq_gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n", chip->irq_gpio);
			}
		}
		pr_err("chip->irq_gpio =%d\n", chip->irq_gpio);
	}
	chip->irq = gpio_to_irq(chip->irq_gpio);
	pr_err("irq chip->irq = %d\n", chip->irq);

	/* set voocphy pinctrl*/
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->charging_inter_active = pinctrl_lookup_state(chip->pinctrl, "charging_inter_active");
	if (IS_ERR_OR_NULL(chip->charging_inter_active)) {
		chg_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	chip->charging_inter_sleep = pinctrl_lookup_state(chip->pinctrl, "charging_inter_sleep");
	if (IS_ERR_OR_NULL(chip->charging_inter_sleep)) {
		chg_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	gpio_direction_input(chip->irq_gpio);
	pinctrl_select_state(chip->pinctrl, chip->charging_inter_active); /* no_PULL */

	rc = gpio_get_value(chip->irq_gpio);
	pr_err("irq chip->irq_gpio input =%d irq_gpio_stat = %d\n", chip->irq_gpio, rc);

	return 0;
}

static int sy6542_irq_register(struct oplus_voocphy_manager *chip)
{
	int ret = 0;

	sy6542_irq_gpio_init(chip);
	pr_err(" sy6542 chip->irq = %d\n", chip->irq);
	if (chip->irq) {
		ret = request_threaded_irq(chip->irq, NULL, sy6542a_charger_interrupt,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "sy6542_charger_irq", chip);
		if (ret < 0) {
			pr_debug("request irq for irq=%d failed, ret =%d\n", chip->irq, ret);
			return ret;
		}
		enable_irq_wake(chip->irq);
	}
	pr_debug("request irq ok\n");

	return ret;
}

static int sy6542_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sy6542_write_byte(chip->client, SY6542_REG_03, 0x18); /*VAC_OVP:12v*/
	sy6542_write_byte(chip->client, SY6542_REG_04, 0xbc); /*VBUS_OVP:10v*/
	sy6542_write_byte(chip->client, SY6542_REG_05, 0xb4); /*IBUS_OCP_UCP:3.6A*/
	sy6542_write_byte(chip->client, SY6542_REG_1E, 0xc0); /*WD:1000ms*/
	sy6542_write_byte(chip->client, SY6542_REG_09, 0x82); /*ADC_CTRL:ADC_EN*/
	sy6542_write_byte(chip->client, SY6542_REG_00, 0x02);
	sy6542_write_byte(chip->client, SY6542_REG_0E, 0x80); /*VOOC_CTRL,send handshake*/
	sy6542_write_byte(chip->client, SY6542_REG_02, 0xF3);
	sy6542_write_byte(chip->client, SY6542_REG_33, 0xd1); /*Loose_det=1*/
	sy6542_write_byte(chip->client, SY6542_REG_B2, 0x40);

	pr_err("sy6542_svooc_hw_setting done");

	return 0;
}

static int sy6542_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sy6542_write_byte(chip->client, SY6542_REG_03, 0x13); /*VAC_OVP:*/
	sy6542_write_byte(chip->client, SY6542_REG_04, 0xbc); /*VBUS_OVP:*/
	sy6542_write_byte(chip->client, SY6542_REG_05, 0xb2); /*IBUS_OCP_UCP:*/
	sy6542_write_byte(chip->client, SY6542_REG_1E, 0xC0); /*IBUS_OCP_UCP:*/
	sy6542_write_byte(chip->client, SY6542_REG_00, 0x12); /*WD:1000ms*/
	sy6542_write_byte(chip->client, SY6542_REG_09, 0x82); /*ADC_CTRL:*/
	sy6542_write_byte(chip->client, SY6542_REG_0E, 0x80); /*Loose_det*/
	sy6542_write_byte(chip->client, SY6542_REG_33, 0xD1);
	sy6542_write_byte(chip->client, SY6542_REG_B2, 0x40);

	pr_err("sy6542_vooc_hw_setting done");
	return 0;
}

static int sy6542_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	sy6542_write_byte(chip->client, SY6542_REG_03, 0x13); /*VAC_OVP:*/
	sy6542_write_byte(chip->client, SY6542_REG_04, 0xbc); /*VBUS_OVP:*/
	sy6542_update_bits(chip->client, SY6542_REG_00, 0x20, 0);
	sy6542_update_bits(chip->client, SY6542_REG_00, 0x08, 1 << 3); /*WD:DISABLE*/

	sy6542_write_byte(chip->client, SY6542_REG_0E, 0x00); /*ADC_CTRL:*/
	sy6542_write_byte(chip->client, SY6542_REG_2B, 0x00); /*VOOC_CTRL*/

	pr_err("sy6542_5v2a_hw_setting done");
	return 0;
}

static int sy6542_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sy6542_write_byte(chip->client, SY6542_REG_06, 0xf6); /*VAC_OV:*/
	sy6542_write_byte(chip->client, SY6542_REG_03, 0x18); /*VAC_OVP:*/
	sy6542_write_byte(chip->client, SY6542_REG_04, 0xbc); /*VBUS_OVP*/
	sy6542_write_byte(chip->client, SY6542_REG_00, 0x03);
	sy6542_write_byte(chip->client, SY6542_REG_09, 0x82); /*WD:DISABLE*/
	sy6542_write_byte(chip->client, SY6542_REG_0E, 0x00); /*ADC_CTRL*/
	sy6542_write_byte(chip->client, SY6542_REG_2B, 0x00); /*VOOC_CTRL*/
	sy6542_write_byte(chip->client, SY6542_REG_40, 0x00); /*VOOC_CTRL*/

	pr_err("sy6542_pdqc_hw_setting done");
	return 0;
}

static int sy6542_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		pr_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		sy6542_init_device(chip);
		pr_info("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		sy6542_svooc_hw_setting(chip);
		pr_info("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		sy6542_vooc_hw_setting(chip);
		pr_info("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		sy6542_5v2a_hw_setting(chip);
		pr_info("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		sy6542_pdqc_hw_setting(chip);
		pr_info("SETTING_REASON_PDQC\n");
		break;
	default:
		pr_err("do nothing\n");
		break;
	}
	return 0;
}

static ssize_t sy6542_show_registers(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr, val, tmpbuf[300];
	int len = 0, idx = 0, ret = 0;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sy6542");
	for (addr = 0x0; addr <= 0x35; addr++) {
		if ((addr < 0x24) || (addr > 0x2B && addr < 0x35)) {
			ret = sy6542_read_byte(chip->client, addr, &val);
			if (ret == 0) {
				len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, val);
				memcpy(&buf[idx], tmpbuf, len);
				idx += len;
			}
		}
	}

	return idx;
}

static ssize_t sy6542_store_register(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x35)
		sy6542_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sy6542_show_registers, sy6542_store_register);

static void sy6542_create_device_node(struct device *dev)
{
	int ret = 0;
	ret = device_create_file(dev, &dev_attr_registers);
	if (ret)
		chg_err("create_device_node fail\n");
}

static int sy6542_gpio_init(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get chargerid_switch_gpio pinctrl fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_high = pinctrl_lookup_state(chip->pinctrl, "switch1_act_switch2_act");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_high)) {
		chg_err("get switch1_act_switch2_act fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_low = pinctrl_lookup_state(chip->pinctrl, "switch1_sleep_switch2_sleep");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_low)) {
		chg_err("get switch1_sleep_switch2_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);

	chip->slave_charging_inter_default = pinctrl_lookup_state(chip->pinctrl, "slave_charging_inter_default");
	if (IS_ERR_OR_NULL(chip->slave_charging_inter_default)) {
		chg_err("get slave_charging_inter_default fail\n");
	} else {
		pinctrl_select_state(chip->pinctrl, chip->slave_charging_inter_default);
	}

	printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip is ready!\n", __func__);
	return 0;
}

static int sy6542_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = NULL;

	if (!chip) {
		pr_debug("chip null\n");
		return -1;
	}

	/* Parsing gpio switch gpio47*/
	node = chip->dev->of_node;
	chip->switch1_gpio = of_get_named_gpio(node, "qcom,charging_switch1-gpio", 0);
	if (chip->switch1_gpio < 0) {
		pr_debug("chip->switch1_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->switch1_gpio)) {
			rc = gpio_request(chip->switch1_gpio, "charging-switch1-gpio");
			if (rc) {
				pr_debug("unable to request gpio [%d]\n", chip->switch1_gpio);
			} else {
				rc = sy6542_gpio_init(chip);
				if (rc)
					chg_err("unable to init "
						"charging_sw_ctrl2-gpio:%d\n",
						chip->switch1_gpio);
			}
		}
		pr_debug("chip->switch1_gpio =%d\n", chip->switch1_gpio);
	}

	rc = of_property_read_u32(node, "ovp_reg", &chip->ovp_reg);
	if (rc) {
		chip->ovp_reg = 0xE;
	} else {
		chg_err("ovp_reg is %d\n", chip->ovp_reg);
	}

	rc = of_property_read_u32(node, "ocp_reg", &chip->ocp_reg);
	if (rc) {
		chip->ocp_reg = 0x8;
	} else {
		chg_err("ocp_reg is %d\n", chip->ocp_reg);
	}

	rc = of_property_read_u32(node, "oplus,pps_ocp_max", &chip->pps_ocp_max);
	if (rc)
		chip->pps_ocp_max = 3600;
	else
		chg_err("pps_ocp_max is %d\n", chip->pps_ocp_max);

	rc = of_property_read_u32(node, "qcom,voocphy_vbus_low", &chip->voocphy_vbus_low);
	if (rc) {
		chip->voocphy_vbus_low = DEFUALT_VBUS_LOW;
	}
	chg_err("voocphy_vbus_high is %d\n", chip->voocphy_vbus_low);

	rc = of_property_read_u32(node, "qcom,voocphy_vbus_high", &chip->voocphy_vbus_high);
	if (rc) {
		chip->voocphy_vbus_high = DEFUALT_VBUS_HIGH;
	}
	chg_err("voocphy_vbus_high is %d\n", chip->voocphy_vbus_high);

	return 0;
}

static void sy6542_set_switch_fast_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("sy6542_set_switch_fast_charger chip null\n");
		return;
	}

	if (chip->switch1_gpio < 0) {
		chg_err("miss sy6542_set_switch_normal_charger gpio\n");
		return;
	}

	mutex_lock(&chip->voocphy_pinctrl_mutex);
	gpio_direction_output(chip->switch1_gpio, 1); /* out 1*/
	mutex_unlock(&chip->voocphy_pinctrl_mutex);

	pr_err("switch switch2 %d to fast finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void sy6542_set_switch_normal_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("sy6542_set_switch_normal_charger chip null\n");
		return;
	}

	if (chip->switch1_gpio < 0) {
		chg_err("miss sy6542_set_switch_normal_charger gpio\n");
		return;
	}

	mutex_lock(&chip->voocphy_pinctrl_mutex);
	gpio_direction_output(chip->switch1_gpio, 0); /* out 1*/
	mutex_unlock(&chip->voocphy_pinctrl_mutex);

	pr_err("switch switch2 %d to normal finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void sy6542_set_switch_mode(struct oplus_voocphy_manager *chip, int mode)
{
	switch (mode) {
	case VOOC_CHARGER_MODE:
		sy6542_set_switch_fast_charger(chip);
		break;
	case NORMAL_CHARGER_MODE:
	default:
		sy6542_set_switch_normal_charger(chip);
		break;
	}

	return;
}

static struct oplus_voocphy_operations oplus_sy6542_ops = {
	.hw_setting = sy6542_hw_setting,
	.init_vooc = sy6542_init_vooc,
	.set_predata = sy6542_set_predata,
	.set_txbuff = sy6542_set_txbuff,
	.get_adapter_info = sy6542_get_adapter_info,
	.update_data = sy6542_update_data,
	.get_chg_enable = sy6542_get_chg_enable,
	.set_chg_enable = sy6542_set_chg_enable,
	.reset_voocphy = sy6542_reset_voocphy,
	.reactive_voocphy = sy6542_reactive_voocphy,
	.set_switch_mode = sy6542_set_switch_mode,
	.send_handshake = sy6542_send_handshake,
	.get_cp_vbat = sy6542_get_cp_vbat,
	.get_cp_vbus = sy6542_get_cp_vbus,
	.get_int_value = sy6542_get_int_value,
	.get_adc_enable = sy6542_get_adc_enable,
	.set_adc_enable = sy6542_set_adc_enable,
	.get_ichg = sy6542_get_cp_ichg,
	.set_pd_svooc_config = sy6542_set_pd_svooc_config,
	.get_pd_svooc_config = sy6542_get_pd_svooc_config,
	.get_voocphy_enable = sy6542_get_voocphy_enable,
	.dump_voocphy_reg = sy6542_dump_reg_in_err_issue,
};

static int sy6542_charger_choose(struct oplus_voocphy_manager *chip)
{
	int ret;

	if (!oplus_voocphy_chip_is_null()) {
		pr_err("oplus_voocphy_chip already exists!");
		return 0;
	} else {
		ret = i2c_smbus_read_byte_data(chip->client, 0x07);
		pr_err("0x07 = %d\n", ret);
		if (ret < 0) {
			pr_err("i2c communication fail");
			return -EPROBE_DEFER;
		} else
			return 1;
	}
}

static bool sy6542a_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static int sy6542a_read_byte(struct oplus_sy6542a_ufcs *chip, u8 addr, u8 *data)
{
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = __sy6542_read_byte(chip->client, addr, data);
	if (rc < 0)
		goto error;
	mutex_unlock(&i2c_rw_lock);
	return 0;
error:
	mutex_unlock(&i2c_rw_lock);
	return rc;
}

static int sy6542a_write_byte(struct oplus_sy6542a_ufcs *chip, u8 addr, u8 data)
{
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = __sy6542_write_byte(chip->client, addr, data);
	if (rc < 0)
		goto error;
	mutex_unlock(&i2c_rw_lock);
	return 0;

error:
	mutex_unlock(&i2c_rw_lock);
	return rc;
}

static int sy6542a_dump_registers(void)
{
	int rc = 0;
	u8 addr;
	u8 val_buf[6] = { 0x0 };
	struct oplus_sy6542a_ufcs *chip = sy6542a_ufcs;
	if (atomic_read(&chip->suspended) == 1) {
		ufcs_err("sy6542a is suspend!\n");
		return -ENODEV;
	}

	for (addr = 0; addr <= 5; addr++) {
		rc = sy6542a_read_byte(chip, addr, &val_buf[addr]);
		if (rc < 0) {
			ufcs_err("sy6542a_dump_registers Couldn't read 0x%02x rc = %d\n", addr, rc);
			break;
		}
	}
	ufcs_err(":[0~5][0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n", val_buf[0], val_buf[1], val_buf[2], val_buf[3],
		 val_buf[4], val_buf[5]);

	return 0;
}


static int sy6542a_chip_disable(void)
{
	int rc = 0;
	if (IS_ERR_OR_NULL(sy6542a_ufcs)) {
		ufcs_err("sy6542a_ufcs null \n");
		return -ENODEV;
	}

	sy6542a_write_byte(sy6542a_ufcs, SY6542A_ADDR_UFCS_CTRL0, 0x00);
	rc = sy6542_update_bits(sy6542a_ufcs->client, SY6542_REG_00, 0x08, 1 << 3);	/* dsiable wdt */
	if (rc < 0) {
		ufcs_err("write i2c failed\n");
		return rc;
	}
	sy6542a_ufcs->ufcs_enable = false;
	return 0;
}

static int sy6542a_hardware_init(struct oplus_sy6542a_ufcs *chip)
{
	return 0;
}

static int sy6542a_parse_dt(struct oplus_sy6542a_ufcs *chip)
{
	struct device_node *node = NULL;

	if (!chip) {
		return -1;
	}
	node = chip->dev->of_node;
	return 0;
}

static void register_ufcs_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int rc = 0;
	char *version;
	char *manufacture;

	version = "sy6542a";
	manufacture = "SouthChip";

	rc = register_device_proc("sy6542a", version, manufacture);
	if (rc)
		ufcs_err("register_ufcs_devinfo fail\n");
#endif
}

static struct regmap_config sy6542a_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = SY6542A_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = sy6542a_is_volatile_reg,
};

static int sy6542_cp_hardware_init(struct i2c_client *client)
{
	int ret = 0;
	if (!oplus_voocphy_mg)
		return -ENODEV;

	ret = sy6542_init_device(oplus_voocphy_mg);
	return ret;
}

static int sy6542_cp_reg_reset(struct i2c_client *client)
{
	int ret = 0;
	if (!oplus_voocphy_mg)
		return -ENODEV;

	ret = sy6542_reg_reset(oplus_voocphy_mg, true);
	oplus_voocphy_mg->cp_work_mode = CP_WORKMODE_DEFAULT;
	pr_info("sy6542 reset ret=%d", ret);

	return ret;
}

static int sy6542_cp_config_sc_mode(struct i2c_client *client)
{
	if (!oplus_voocphy_mg)
		return -ENODEV;

	if (!client)
		return -EINVAL;

	sy6542_write_byte(client, SY6542_REG_06, 0xf6); /* VBAT_OVP:4.65V */
	sy6542_write_byte(client, SY6542_REG_03, 0x18); /*VAC_OVP:12v*/
	sy6542_write_byte(client, SY6542_REG_04, 0xc6); /*VBUS_OVP:11v*/
	sy6542_write_byte(client, SY6542_REG_05, 0x1c); /* IBUS_OCP_UCP:5.0A */
	sy6542_write_byte(client, SY6542_REG_00, 0x22); /* WD:1s bit7[0]-->2:1 */
	sy6542_write_byte(client, SY6542_REG_09, 0x82);
	sy6542_write_byte(client, SY6542_REG_0E, 0x80); /*ADC_CTRL:ADC_EN*/
	sy6542_write_byte(client, SY6542_REG_02, 0xF3); /* PMID2OUT_UVP_OVP */
	sy6542_write_byte(client, SY6542_REG_2B, 0x00);
	oplus_voocphy_mg->cp_work_mode = CP_WORKMODE_PPS;
	return 0;
}

static int sy6542_cp_config_bypass_mode(struct i2c_client *client)
{
	u8 reg_data;

	if (!oplus_voocphy_mg)
		return -ENODEV;

	if (!client)
		return -EINVAL;

	reg_data = 0x20 | (oplus_voocphy_mg->ovp_reg & 0x1f);
	sy6542_write_byte(client, SY6542_REG_06, 0xf6); /* VBAT_OVP:4.65V */
	sy6542_write_byte(client, SY6542_REG_03, 0x18); /*VAC_OVP:12v*/
	sy6542_write_byte(client, SY6542_REG_04, 0x99); /*VBUS_OVP:6.5v*/
	sy6542_write_byte(client, SY6542_REG_05, 0x1c); /* IBUS_OCP_UCP:5.0A */
	sy6542_write_byte(client, SY6542_REG_00, 0x32); /* WD:1s bit7[0]-->1:1 */
	sy6542_write_byte(client, SY6542_REG_09, 0x82);
	sy6542_write_byte(client, SY6542_REG_0E, 0x80); /*ADC_CTRL:ADC_EN*/
	sy6542_read_byte(client, SY6542_REG_2B, 0x00);
	oplus_voocphy_mg->cp_work_mode = CP_WORKMODE_PPS;
	pr_info("sy6542 configed bypass mode");
	return 0;
}

static int sy6542_cp_get_ucp_flag(struct i2c_client *client)
{
 	int ret = 0;
	u8 value;
	int ucp_fail = 0;
	if (!client)
		return -EINVAL;

	ret = sy6542_read_byte(client, SY6542_REG_08, &value);
	if (ret < 0) {
		pr_err("SY6542_REG_08 failed ret=%d\n", ret);
		return 0;
	}
	ucp_fail = (value & SY6542_IBUS_UCP_FALL_FLAG_MASK);
	pr_info("SY6542_REG_08[0x%x] ucp_fail = %d\n", value, ucp_fail);
	return ucp_fail;
}

static int sy6542_set_cp_enable(struct i2c_client *client, int enable)
{
	int ret = 0;
	if (!oplus_voocphy_mg)
		return -ENODEV;

	ret = sy6542_set_chg_enable(oplus_voocphy_mg, enable);
	if (ret < 0)
		pr_err("sy6542 set_cp_enable(%d) failed", enable);

	pr_info("sy6542 set cp %sabled", enable ? "en" : "dis");
	return ret;
}

static int sy6542_cp_get_vbus(struct i2c_client *client)
{
	int vbus = 0;
	if (!oplus_voocphy_mg)
		return -ENODEV;

	vbus = sy6542_get_cp_vbus(oplus_voocphy_mg);
	return vbus;
}

static int sy6542_cp_get_ibus(struct i2c_client *client)
{
	int ibus = 0;
	if (!oplus_voocphy_mg)
		return -ENODEV;

	ibus = sy6542_get_cp_ichg(oplus_voocphy_mg);
	return ibus;
}

static int sy6542_cp_get_vac(struct i2c_client *client)
{
	return TRUE;
}

static int sy6542_cp_get_vout(struct i2c_client *client)
{
	return TRUE;
}

static int sy6542_cp_get_vbat(struct i2c_client *client)
{
	int vbat = 0;
	if (!oplus_voocphy_mg)
		return -ENODEV;

	vbat = sy6542_get_cp_vbat(oplus_voocphy_mg);
	return vbat;
}

static int sy6542_cp_get_tdie(struct i2c_client *client)
{
	int ret = 0;
	u8 value;
	int cp_tdie = 0;
	if (!client)
		return -EINVAL;

	ret = sy6542_read_byte(client, SY6542_REG_17, &value);
	if (ret < 0) {
		pr_err("SY6542_REG_17 failed ret=%d\n", ret);
		return 0;
	}
	cp_tdie = (value * 2) - 273;
	pr_info("SY6542_REG_17[0x%x] cp_tdie = %d\n", value, cp_tdie);
	return cp_tdie;
}

static irqreturn_t sy6542_protect_interrupt_handler(struct oplus_voocphy_manager *chip)
{
	if (!chip)
		return IRQ_HANDLED;

	return IRQ_HANDLED;
}

static struct oplus_pps_cp_device_operations sy6542_cp_pps_ops = {
	.oplus_cp_hardware_init = sy6542_cp_hardware_init,
	.oplus_cp_reset         = sy6542_cp_reg_reset,
	.oplus_cp_cfg_sc        = sy6542_cp_config_sc_mode,
	.oplus_cp_cfg_bypass    = sy6542_cp_config_bypass_mode,
	.oplus_get_ucp_flag     = sy6542_cp_get_ucp_flag,
	.oplus_set_cp_enable    = sy6542_set_cp_enable,
	.oplus_get_cp_vbus      = sy6542_cp_get_vbus,
	.oplus_get_cp_ibus      = sy6542_cp_get_ibus,
	.oplus_get_cp_vac       = sy6542_cp_get_vac,
	.oplus_get_cp_vout      = sy6542_cp_get_vout,
	.oplus_get_cp_vbat      = sy6542_cp_get_vbat,
	.oplus_get_cp_tdie      = sy6542_cp_get_tdie,
};

static int sy6542_pps_check_and_register(struct oplus_voocphy_manager *chip)
{
	PPS_CP_DEVICE_NUM index = CP_MAX;
	struct chargepump_device *sy6542_cp;
	struct device_node * node = NULL;

	if (!chip) {
		pr_err("chip null\n");
		return -EINVAL;
	}

	node = chip->dev->of_node;

	if (of_property_read_bool(node, "oplus,pps_role_master"))
		index = CP_MASTER;
	else if (of_property_read_bool(node, "oplus,pps_role_slave_a"))
		index = CP_SLAVE_A;
	else if (of_property_read_bool(node, "oplus,pps_role_slave_b"))
		index = CP_SLAVE_B;

	if (index < CP_MAX && index >= CP_MASTER) {
		sy6542_cp = devm_kzalloc(chip->dev, sizeof(*sy6542_cp), GFP_KERNEL);
		if (!sy6542_cp) {
			dev_err(chip->dev, "Couldn't allocate sy6542_cp memory\n");
			return -ENOMEM;
		}
		sy6542_cp->client = chip->client;
		sy6542_cp->dev_ops = &sy6542_cp_pps_ops;
		if (of_property_read_string(node, "oplus,pps_dev-name", &sy6542_cp->dev_name)) {
			if (index != CP_MASTER) {
				chg_err("Warn: there is a nameless non-master cp");
				sy6542_cp->dev_name = "default_nameless_cp";
			} else {
				chg_err("Can't register a nameless master cp");
				return -ENODEV;
			}
		}
		oplus_cp_device_register(index, sy6542_cp);
	}
	return 0;
}

static int sy6542a_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	 struct oplus_sy6542a_ufcs *chip_ic;
	int ret;

	client->addr = SY6542A_I2C_ADDR;
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	mutex_init(&i2c_rw_lock);
	mutex_init(&chip->voocphy_pinctrl_mutex);

	i2c_set_clientdata(client, chip);
	if (!sy6542a_hw_version_check(chip)) {
		pr_err("sy6542 dont use ufcs\n");
		return -ENOMEM;
	}

	ret = sy6542_charger_choose(chip);
	if (ret <= 0)
		return ret;

	sy6542_create_device_node(&(client->dev));

	ret = sy6542_parse_dt(chip);

	sy6542_reg_reset(chip, true);

	sy6542_init_device(chip);

	ret = sy6542_irq_register(chip);
	if (ret < 0)
		goto err_1;

	chip->ops = &oplus_sy6542_ops;

	oplus_voocphy_init(chip);

	oplus_voocphy_mg = chip;
	sy6542_track_init(chip);

	init_proc_voocphy_debug();

	ufcs_err("sy6542a_parse_dt successfully!\n");
	chip_ic = devm_kzalloc(&client->dev, sizeof(struct oplus_sy6542a_ufcs), GFP_KERNEL);
	if (!chip_ic) {
		ufcs_err("failed to allocate oplus_sy6542a_ufcs\n");
		return -ENOMEM;
	}

	chip_ic->regmap = devm_regmap_init_i2c(client, &sy6542a_regmap_config);
	if (!chip_ic->regmap) {
		ret = -ENODEV;
		goto regmap_init_err;
	}

	chip_ic->dev = &client->dev;
	chip_ic->client = client;
	i2c_set_clientdata(client, chip_ic);
	sy6542a_ufcs = chip_ic;

	sy6542a_hardware_init(chip_ic);
	sy6542a_dump_registers();
	sy6542a_parse_dt(chip_ic);
	chip_ic->ufcs_enable = false;
	register_ufcs_devinfo();
	sy6542_pps_check_and_register(chip);
 	return 0;
regmap_init_err:
	devm_kfree(&client->dev, chip_ic);
	return ret;
err_1:
	pr_err("sy6542 probe err_1\n");
	return ret;
}

static int sy6542a_pm_resume(struct device *dev_chip)
{
	struct i2c_client *client = container_of(dev_chip, struct i2c_client, dev);
	struct oplus_sy6542a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 0);
	ufcs_err(" %d!\n", chip->suspended);

	return 0;
}

static int sy6542a_pm_suspend(struct device *dev_chip)
{
	struct i2c_client *client = container_of(dev_chip, struct i2c_client, dev);
	struct oplus_sy6542a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 1);
	ufcs_err(" %d!\n", chip->suspended);

	return 0;
}

static const struct dev_pm_ops sy6542a_pm_ops = {
	.resume = sy6542a_pm_resume,
	.suspend = sy6542a_pm_suspend,
};

static int sy6542a_driver_remove(struct i2c_client *client)
{
	struct oplus_sy6542a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return -ENODEV;

	devm_kfree(&client->dev, chip);

	return 0;
}

static void sy6542a_shutdown(struct i2c_client *client)
{
	sy6542_write_byte(client, SY6542_REG_11, 0x00);
	sy6542_write_byte(client, SY6542_REG_21, 0x00);
	sy6542a_chip_disable();
	return;
}

static const struct of_device_id sy6542a_match[] = {
	{.compatible = "oplus,sy6542a-ufcs" },
	{},
};

static const struct i2c_device_id sy6542a_id[] = {
	{ "oplus,sy6542a-ufcs", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sy6542a_id);

static struct i2c_driver sy6542a_i2c_driver = {
	.driver =
		{
			.name = "sy6542a-ufcs",
			.owner = THIS_MODULE,
			.of_match_table = sy6542a_match,
			.pm = &sy6542a_pm_ops,
		},
	.probe = sy6542a_driver_probe,
	.remove = sy6542a_driver_remove,
	.id_table = sy6542a_id,
	.shutdown = sy6542a_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(sy6542a_i2c_driver);
#else
static __init int sy6542a_i2c_driver_init(void)
{
	return i2c_add_driver(&sy6542a_i2c_driver);
}

static __exit void sy6542a_i2c_driver_exit(void)
{
	i2c_del_driver(&sy6542a_i2c_driver);
}

oplus_chg_module_register(sy6542a_i2c_driver);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("SC SY6542A VOOCPHY Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");