// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022-2023 Unisoc(Shanghai) Technologies Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include "sprd_img.h"
#include "flash_drv.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "FLASH_OCP81375: %d %d %s : " fmt, current->pid, __LINE__, __func__

/*flash-idx usage:
 *1: LED1 used for back, LED2 not installed;
 *2: LED1 used for front,LED2 not installed;
 *3: LED1 used for back, LED2 used for front;
 *4: LED1 used for front,LED2 used for back;
 *5: LED1 and LED2 used for back;
 *6: LED1 and LED2 used for front;
 *7: LED2 used for back, LED1 not installed;
 *8: LED2 used for front,LED1 not installed;
 */

enum {
	LED1_BK            = 0x01,
	LED1_FRONT         = 0x02,
	LED1_BK_LED2_FRONT = 0x03,
	LED1_FRONT_LED2_BK = 0x04,
	LED1_LED2_BK       = 0x05,
	LED1_LED2_FRONT    = 0x06,
	LED2_FRONT         = 0x07,
	LED2_BK            = 0x08,
};

enum {
	LED_BACK     = 0x01,
	LED_FRONT    = 0x02,
	LED_BK_AND_FRONT = 0x03,
};


#define FLASH_IC_DRIVER_NAME       "flash-ocp81375"
#define FLASH_IC_REG_ENABLE        0x01
#define FLASH_IC_REG_IVFM          0x02
#define REG_FLASH_CURRENT_LED1     0x03
#define REG_FLASH_CURRENT_LED2     0x04
#define REG_TORCH_CURRENT_LED1     0x05
#define REG_TORCH_CURRENT_LED2     0x06
#define REG_BOOST_CONFIG           0x07

#define REG_DEVICE_ID              0x0C
#define OCP81375_DEVICE_ID         0x3A

#define FLASH_IC_REG_TIMEOUT       0x08
#define REG_FLAGS_1                0x0A
#define REG_FLAGS_2                0x0B
#define OCP81375_SOFTWARE_TIMEOUT   400 /* : ms  */

static int ocp81375_reg_value[10] = {0};

#define I2C_SLAVEADDR	0x63

#define OCP81375_LEVEL_NUM 16   /* for tuning*/

#define OCP81375_MAX_TORCH_CURRENT 500   /* MAX output current of ocp81375*/
#define OCP81375_MAX_FLASH_CURRENT 2000   /* MAX output current of ocp81375*/

static int led1_level_torch[OCP81375_LEVEL_NUM] = {0};
static int led1_level_flash[OCP81375_LEVEL_NUM] = {0};

static int led2_level_torch[OCP81375_LEVEL_NUM] = {0};
static int led2_level_flash[OCP81375_LEVEL_NUM] = {0};

/* [ITD] For PR651E/PR652C only! Don't use for other
 * projects.Add by bingfeng.li 20210630 add start
 */
#ifdef USING_AW36515_FLASH_LEVEL_TRANSSION
static int led1_aw36515_level_torch[OCP81375_LEVEL_NUM] = {
	3, 3, 10, 14, 18, 22, 25, 30, 33, 37, 41, 45, 49, 52, 56, 60};
static int led2_aw36515_level_torch[OCP81375_LEVEL_NUM] = {
	6, 14, 22, 30, 38, 46, 53, 61, 69, 77, 85, 93, 101, 108, 116, 124};
static int led1_aw36515_level_flash[OCP81375_LEVEL_NUM] = {
	1, 3, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 27, 29};
static int led2_aw36515_level_flash[OCP81375_LEVEL_NUM] = {
	6, 14, 21, 29, 37, 44, 52, 59, 67, 75, 82, 90, 97, 105, 112, 120};
#endif
/* [ITD] For PR651E/PR652C only! Don't use for other projects. Add by bingfeng.li 20210630 add end*/

struct flash_ic_cfg {
	unsigned int flash_idx; /* refer to "flash-idx usage" */
	unsigned int led1_max_torch;   /* MAX torch current from LED datasheet  :mA  */
	unsigned int led1_max_flash;   /* MAX flash current from LED datasheet  :mA  */
	unsigned int led2_max_torch;   /* MAX torch current from LED datasheet  :mA  */
	unsigned int led2_max_flash;   /* MAX flash current from LED datasheet  :mA  */
};

struct ocp81375_flash_driver_data {
	struct i2c_client *i2c_info;
	struct mutex i2c_lock;
	void *priv;
	struct flash_ic_cfg flash_cfg;
};

static int highlight_open;

static int flash_ocp81375_driver_reg_write(struct i2c_client *i2c, u8 reg, u8 value)
{
	int ret;

	/* pr_info("flash ic reg write %x   %x\n", reg, value);*/
	/* smbus_xfer is empty! Will call i2c_transfer, using i2c ,not smbus protocol */
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	return ret;
}

static int flash_ocp81375_driver_reg_read(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	int ret;

	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret < 0) {
		pr_info("%s:%s reg(0x%x), ret(%d)\n", FLASH_IC_DRIVER_NAME, __func__, reg, ret);
		return ret;
	}

	ret &= 0xff;
	*dest = ret;
	return 0;
}

/* add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add start*/
static void sprd_flash_ocp81375_clear_flag(void *drvd)
{
	u8 data = 0;
	struct ocp81375_flash_driver_data *drv_data = (struct ocp81375_flash_driver_data *)drvd;

	if (drv_data->i2c_info) {
		pr_info("clear flag start\n");
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, REG_FLAGS_1, &data);
		if (((data >> 6) & 0x1) | ((data >> 5) & 0x1) | ((data >> 4) & 0x1))
			pr_err("Error::VOUT Short (Vout = %d, Vled1 = %d, Vled2 = %d)!!!\n",
				((data >> 6) & 1), ((data >> 5) & 1), ((data >> 4) & 1));
		if (data & 0x1)
			pr_err("Error::Flash_time_out!!!\n");
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, REG_FLAGS_2, &data);
		if ((data >> 2) & 0x1)
			pr_err("Error::IVFM is Trigger,In Voltage too low!!!\n");
		if ((data >> 1) & 0x1)
			pr_err("Error::OVP is Trigger,out Voltage too high!!!\n");
	}
	pr_info("clear flag end\n");
}
/* add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add end*/
/* add read all flag. ping.luan@transsion.com, 2021-7-7 add start*/
#ifdef OCP81375_REG_CHECK
static void sprd_flash_ocp81375_read_flag(void *drvd)
{
	u8 test_data = 0;
	struct ocp81375_flash_driver_data *drv_data = (struct ocp81375_flash_driver_data *)drvd;

	if (drv_data->i2c_info) {
		pr_info("read flag start-----------------------\n");

		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x01, &test_data);
		pr_info("systemUI reg 0x01 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x02, &test_data);
		pr_info("systemUI reg 0x02 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x03, &test_data);
		pr_info("systemUI reg 0x03 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x04, &test_data);
		pr_info("systemUI reg 0x04 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x05, &test_data);
		pr_info("systemUI reg 0x05 = %d\n\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x06, &test_data);
		pr_info("systemUI reg 0x06 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x07, &test_data);
		pr_info("systemUI reg 0x07 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x06, &test_data);
		pr_info("systemUI reg 0x06 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x07, &test_data);
		pr_info("systemUI reg 0x07 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x08, &test_data);
		pr_info("systemUI reg 0x08 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x09, &test_data);
		pr_info("systemUI reg 0x09 = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x0a, &test_data);
		pr_info("systemUI reg 0x0a = %d\n\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x0b, &test_data);
		pr_info("systemUI reg 0x0b = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x0c, &test_data);
		pr_info("systemUI reg 0x0c = %d\n", test_data);
		flash_ocp81375_driver_reg_read(drv_data->i2c_info, 0x0d, &test_data);
		pr_info("systemUI reg 0x0d = %d\n", test_data);
	}
	pr_info("read flag end-----------------------\n");
}
#endif
/* add read all flag. ping.luan@transsion.com, 2021-7-7 add end*/

static struct ocp81375_flash_driver_data *drv_data_tmp;
#ifdef OCP81375_SOFT_TIMEOUT
#include <linux/hrtimer.h>
#include <linux/ktime.h>
static int sprd_flash_ocp81375_close_highlight(void *drvd, uint8_t idx);
static struct hrtimer g_timeOutTimer;
static struct work_struct workTimeOut;
static int g_timer_flag;
static void work_timeOutFunc(struct work_struct *data);
static enum hrtimer_restart ledTimeOutCallback(struct hrtimer *timer);
static int sprd_flash_ocp81375_time_close(void *drvd)
{
	int ret = 0;
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;

	pr_info("time_close.\n");

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		ret = flash_ocp81375_driver_reg_write(
			drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x00);
		pr_info("ret= %d\n", ret);
	}
	return ret;
}

static void work_timeOutFunc(struct work_struct *data)
{
	if (drv_data_tmp != NULL)
		sprd_flash_ocp81375_time_close(drv_data_tmp);

	pr_info("ocp81375 ledTimeOut_callback\n");
}
static enum hrtimer_restart ledTimeOutCallback(struct hrtimer *timer)
{
	schedule_work(&workTimeOut);
	return HRTIMER_NORESTART;
}
static void timerInit(void)
{
	static int init_flag;

	if (init_flag == 0) {
		init_flag = 1;
		INIT_WORK(&workTimeOut, work_timeOutFunc);
		hrtimer_init(&g_timeOutTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		g_timeOutTimer.function = ledTimeOutCallback;
	}
}
#endif

static int sprd_flash_ocp81375_init(void *drvd)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
	usleep_range(2000, 3000);
	if (drv_data->i2c_info) {
		/* bit[7]IVFM, [6]TXI, [5]Flash_pin, [4]Torch_pin, [1:0]
		 * 00:Shutdown  01: Indicator 10:Torch mode 11: Flash mode
		 */
		flash_ocp81375_driver_reg_write(
			drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x00);

		/* Flash Ramp time=1ms, Flash timeout=600ms*/
		flash_ocp81375_driver_reg_write(
			drv_data->i2c_info, FLASH_IC_REG_TIMEOUT, 0x19);

		/* Flash IVFM vdd=3.2v, Flash selection Stop and Hold Mode*/
		flash_ocp81375_driver_reg_write(drv_data->i2c_info, FLASH_IC_REG_IVFM, 0x59);
		/*Flash Boost Frequency = 4M*/
		flash_ocp81375_driver_reg_write(drv_data->i2c_info, REG_BOOST_CONFIG, 0x0B);

		pr_info("flash ic init  %d\n", __LINE__);
	}
	return 0;
}

static int ocp81375_open_torch_bk(void *drvd, uint8_t idx)
{
	u8 data = 0;
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
// add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add start
	sprd_flash_ocp81375_clear_flag(drv_data);
// add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add end
	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_BK ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
/*add by bingfeng.li for sprd platform support 360flash systemUI flash device node. add start*/
			data = ocp81375_reg_value[FLASH_IC_REG_ENABLE];
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED1,
				ocp81375_reg_value[REG_TORCH_CURRENT_LED1]);
			if (idx != 0xff) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x09 | data);
				ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x09 | data;
				pr_info("enable idx = %d!!!\n", drv_data->flash_cfg.flash_idx);
			}
		} else if (drv_data->flash_cfg.flash_idx == LED2_BK ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			data = ocp81375_reg_value[FLASH_IC_REG_ENABLE];
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED1, 0);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED2,
				ocp81375_reg_value[REG_TORCH_CURRENT_LED2]);
			if (idx != 0xff) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0a | data);
				ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0a | data;
				pr_info("enable idx = %d!!!\n", drv_data->flash_cfg.flash_idx);
			}
/*add by bingfeng.li for sprd platform support 360flash systemUI flash device node. add end*/
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_BK) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED1,
				ocp81375_reg_value[REG_TORCH_CURRENT_LED1]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED2,
				ocp81375_reg_value[REG_TORCH_CURRENT_LED2]);
			if (idx != 0xff) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0b);
				ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0b;
				pr_info("enable idx = %d!!!\n", drv_data->flash_cfg.flash_idx);
			}
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;
}

static int ocp81375_open_torch_front(void *drvd, uint8_t idx)
{
	u8 data = 0;
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
// add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add start
	sprd_flash_ocp81375_clear_flag(drv_data);
// add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add end
	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			data = ocp81375_reg_value[FLASH_IC_REG_ENABLE];
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED1,
				ocp81375_reg_value[REG_TORCH_CURRENT_LED1]);
			if (idx != 0xff) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x09 | data);
				ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x09 | data;
				pr_info("enable idx = %d!!!\n", drv_data->flash_cfg.flash_idx);
			}
		} else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
			data = ocp81375_reg_value[FLASH_IC_REG_ENABLE];
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED1, 0);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED2,
				ocp81375_reg_value[REG_TORCH_CURRENT_LED2]);
			if (idx != 0xff) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0a | data);
				ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0a | data;
				pr_info("enable idx = %d!!!\n", drv_data->flash_cfg.flash_idx);
			}
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED1,
				ocp81375_reg_value[REG_TORCH_CURRENT_LED1]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_TORCH_CURRENT_LED2,
				ocp81375_reg_value[REG_TORCH_CURRENT_LED2]);
			if (idx != 0xff) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0b);
				ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0b;
				pr_info("enable idx = %d!!!\n", drv_data->flash_cfg.flash_idx);
			}
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;

}

static int sprd_flash_ocp81375_open_torch_bk(void *drvd, uint8_t idx)
{
	ocp81375_open_torch_bk(drvd, idx);
	pr_info("idx = %d\n", idx);
	return 0;
}

static int sprd_flash_ocp81375_open_torch_front(void *drvd, uint8_t idx)
{
	ocp81375_open_torch_front(drvd, idx);
	pr_info("idx = %d\n", idx);
	return 0;
}

static int sprd_flash_ocp81375_open_preflash_bk(void *drvd, uint8_t idx)
{
	ocp81375_open_torch_bk(drvd, idx);
	pr_info("idx = %d\n", idx);
	return 0;
}

static int sprd_flash_ocp81375_open_preflash_front(void *drvd, uint8_t idx)
{
	ocp81375_open_torch_front(drvd, idx);
	pr_info("idx = %d\n", idx);
	return 0;
}

static int ocp81375_close_torch_bk(void *drvd, uint8_t idx)
{
	u8 data = 0;
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info)
		flash_ocp81375_driver_reg_write(
			drv_data->i2c_info, REG_BOOST_CONFIG, 0x0B);

	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_BK ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
			data = ocp81375_reg_value[FLASH_IC_REG_ENABLE];
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, ((~0x01)&data));
			ocp81375_reg_value[FLASH_IC_REG_ENABLE] = (~0x01)&data;
		} else if (drv_data->flash_cfg.flash_idx ==  LED2_BK ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			data = ocp81375_reg_value[FLASH_IC_REG_ENABLE];
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, ((~0x02)&data));
			ocp81375_reg_value[FLASH_IC_REG_ENABLE] = (~0x02)&data;
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_BK) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0);
			ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0;
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

	}
	return 0;
}

static int ocp81375_close_torch_front(void *drvd, uint8_t idx)
{
	u8 data = 0;
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info)
		flash_ocp81375_driver_reg_write(
			drv_data->i2c_info, REG_BOOST_CONFIG, 0x0B);

	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			data = ocp81375_reg_value[FLASH_IC_REG_ENABLE];
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, ((~0x01)&data));
			ocp81375_reg_value[FLASH_IC_REG_ENABLE] = (~0x01)&data;
		} else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
			data = ocp81375_reg_value[FLASH_IC_REG_ENABLE];
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, ((~0x02)&data));
			ocp81375_reg_value[FLASH_IC_REG_ENABLE] = (~0x02)&data;
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0);
			ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0;
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

		pr_info("idx = %d\n", idx);
	}
	return 0;
}

static int sprd_flash_ocp81375_close_torch_bk(void *drvd, uint8_t idx)
{
	ocp81375_close_torch_bk(drvd, idx);
	pr_info("idx = %d\n", idx);
	return 0;
}

static int sprd_flash_ocp81375_close_torch_front(void *drvd, uint8_t idx)
{
	ocp81375_close_torch_front(drvd, idx);
	pr_info("idx = %d\n", idx);
	return 0;
}

static int sprd_flash_ocp81375_close_preflash(void *drvd, uint8_t idx)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	int ret = 0;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		ret = flash_ocp81375_driver_reg_write(
			drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x00);
		ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0;
		pr_info("ret= %d,idx = %d\n", ret, idx);
	}
	if (drv_data->i2c_info) {
		flash_ocp81375_driver_reg_write(
			drv_data->i2c_info, REG_BOOST_CONFIG, 0x0B);
	}
	return 0;
}

static int sprd_flash_ocp81375_open_highlight_bk(void *drvd, uint8_t idx)
{
#ifdef OCP81375_SOFT_TIMEOUT
	ktime_t ktime;
#endif

	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	pr_info("idx = %d\n", idx);
// add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add start
	sprd_flash_ocp81375_clear_flag(drv_data);
// add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add end
	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		if (highlight_open == 1) {
			if (drv_data->i2c_info) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x00);
				ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0;
			}
			if (drv_data->i2c_info)
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, REG_BOOST_CONFIG, 0x0B);

			udelay(500);
			pr_info("last time is open and not close,reset all done\n");
		}
		if (drv_data->flash_cfg.flash_idx == LED1_BK ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED1,
				ocp81375_reg_value[REG_FLASH_CURRENT_LED1]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0d);
			highlight_open = 1;
		} else if (drv_data->flash_cfg.flash_idx == LED2_BK ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED1, 0);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED2,
				ocp81375_reg_value[REG_FLASH_CURRENT_LED2]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0e);
			highlight_open = 1;
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_BK) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED1,
				ocp81375_reg_value[REG_FLASH_CURRENT_LED1]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED2,
				ocp81375_reg_value[REG_FLASH_CURRENT_LED2]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0f);
			highlight_open = 1;
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

#ifdef OCP81375_SOFT_TIMEOUT
		if (highlight_open == 1) {
			if (g_timer_flag == 1) {
				hrtimer_cancel(&g_timeOutTimer);
				g_timer_flag = 0;
				pr_info("not close last time,cancel g_timeOutTimer\n");
			}
			timerInit();
			// param1:s, param2:ms
			ktime = ktime_set(0, OCP81375_SOFTWARE_TIMEOUT * 1000000);
			hrtimer_start(&g_timeOutTimer, ktime, HRTIMER_MODE_REL);
			g_timer_flag = 1;
		}
#endif
#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;
}

static int sprd_flash_ocp81375_open_highlight_front(void *drvd, uint8_t idx)
{
#ifdef OCP81375_SOFT_TIMEOUT
	ktime_t ktime;
#endif

	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
// add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add start
	sprd_flash_ocp81375_clear_flag(drv_data);
// add search&clear all flag. ping.luan@transsion.com, 2021-6-24 add end
	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		if (highlight_open == 1) {
			if (drv_data->i2c_info) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x00);
				ocp81375_reg_value[FLASH_IC_REG_ENABLE] = 0x0;
			}
			if (drv_data->i2c_info)
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, REG_BOOST_CONFIG, 0x0B);

			udelay(500);
			pr_info("last time is open and not close,reset all done\n");
		}
		if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED1,
				ocp81375_reg_value[REG_FLASH_CURRENT_LED1]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0d);
			highlight_open = 1;
		} else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED2,
				ocp81375_reg_value[REG_FLASH_CURRENT_LED2]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0e);
			highlight_open = 1;
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED1,
				ocp81375_reg_value[REG_FLASH_CURRENT_LED1]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, REG_FLASH_CURRENT_LED2,
				ocp81375_reg_value[REG_FLASH_CURRENT_LED2]);
			flash_ocp81375_driver_reg_write(
				drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x0f);
			highlight_open = 1;
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

#ifdef OCP81375_SOFT_TIMEOUT
		if (highlight_open == 1) {
			if (g_timer_flag == 1) {
				hrtimer_cancel(&g_timeOutTimer);
				g_timer_flag = 0;
				pr_info("not close last time,cancel g_timeOutTimer\n");
			}
			timerInit();
			// param1:s, param2:ms
			ktime = ktime_set(0, OCP81375_SOFTWARE_TIMEOUT * 1000000);
			hrtimer_start(&g_timeOutTimer, ktime, HRTIMER_MODE_REL);
			g_timer_flag = 1;
		}
#endif
#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
		pr_info("idx = %d\n", idx);
	}
	return 0;
}

static int sprd_flash_ocp81375_close_highlight(void *drvd, uint8_t idx)
{
	int ret = 0;

	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	pr_info("idx %d\n", idx);
	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		ret = flash_ocp81375_driver_reg_write(
			drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x00);
		pr_info("ret= %d,idx = %d\n", ret, idx);
	}
#ifdef OCP81375_SOFT_TIMEOUT
	if (g_timer_flag == 1) {
		hrtimer_cancel(&g_timeOutTimer);
		g_timer_flag = 0;
	}
#endif
	highlight_open = 0;
	return ret;
}

static int sprd_flash_ocp81375_cfg_value_torch_bk(void *drvd, uint8_t idx,
					  struct sprd_flash_element *element)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	int index = element->index;

	pr_info("idx = %d,  index= %d\n",
		idx, index);
	if (index >= OCP81375_LEVEL_NUM)
		index = OCP81375_LEVEL_NUM - 1;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_BK ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT)
			ocp81375_reg_value[REG_TORCH_CURRENT_LED1] =
				led1_level_torch[index];
		else if (drv_data->flash_cfg.flash_idx == LED2_BK ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK)
			ocp81375_reg_value[REG_TORCH_CURRENT_LED2] =
				led2_level_torch[index];
		else if (drv_data->flash_cfg.flash_idx == LED1_LED2_BK) {
			ocp81375_reg_value[REG_TORCH_CURRENT_LED1] =
				led1_level_torch[index];
			ocp81375_reg_value[REG_TORCH_CURRENT_LED2] =
				led2_level_torch[index];
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;
}

static int sprd_flash_ocp81375_cfg_value_torch_front(
	void *drvd, uint8_t idx, struct sprd_flash_element *element)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	int index = element->index;
	uint32_t Current = 0, Current2 = 0;

	pr_info("idx = %d,  index= %d, val=0x%x\n",
		idx, index, element->val);
// add torch percent lev ctrl. hong.zhang@transsion.com, 2021-4-14
	if (element->val != 0xFFFF && index >= OCP81375_LEVEL_NUM)
		index = OCP81375_LEVEL_NUM - 1;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
// add torch percent lev ctrl. hong.zhang@transsion.com, 2021-4-14
		if (element->val == 0xFFFF) {
			if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
				drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK)
				Current = index;
			else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
				drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT)
				Current = index;
			else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
				Current = index;
				Current2 = index;
			} else
				pr_err("Error::torch percent ctrl idx = %d!!!\n",
					drv_data->flash_cfg.flash_idx);

			if (Current > 0) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, REG_TORCH_CURRENT_LED1, Current);
				pr_info("torch percent led1 ctrl reg=0x%x", Current);
			}

			if (Current2 > 0) {
				flash_ocp81375_driver_reg_write(
					drv_data->i2c_info, REG_TORCH_CURRENT_LED2, Current2);
				pr_info("torch percent led2 ctrl reg=0x%x", Current2);
			}
		} else {
// add end. hong.zhang@transsion.com, 2021-4-14
			if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
				drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK)
				ocp81375_reg_value[REG_TORCH_CURRENT_LED1] =
					led1_level_torch[index];
			else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
				drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT)
				ocp81375_reg_value[REG_TORCH_CURRENT_LED2] =
					led2_level_torch[index];
			else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
				ocp81375_reg_value[REG_TORCH_CURRENT_LED1] =
					led1_level_torch[index];
				ocp81375_reg_value[REG_TORCH_CURRENT_LED2] =
					led2_level_torch[index];
			} else
				pr_err("Error: idx = %d!!!\n",
					drv_data->flash_cfg.flash_idx);

		}
#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;
}

static int sprd_flash_ocp81375_cfg_value_preflash_bk(
	void *drvd, uint8_t idx, struct sprd_flash_element *element)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	int index = element->index;

	pr_info("idx = %d,  index= %d\n",
		idx, index);
	if (index >= OCP81375_LEVEL_NUM)
		index = OCP81375_LEVEL_NUM - 1;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_BK ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT)
			ocp81375_reg_value[REG_TORCH_CURRENT_LED1] = led1_level_torch[index];
		else if (drv_data->flash_cfg.flash_idx == LED2_BK ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK)
			ocp81375_reg_value[REG_TORCH_CURRENT_LED2] = led2_level_torch[index];
		else if (drv_data->flash_cfg.flash_idx == LED1_LED2_BK) {
			ocp81375_reg_value[REG_TORCH_CURRENT_LED1] = led1_level_torch[index];
			ocp81375_reg_value[REG_TORCH_CURRENT_LED2] = led2_level_torch[index];
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;
}

static int sprd_flash_ocp81375_cfg_value_preflash_front(
	void *drvd, uint8_t idx, struct sprd_flash_element *element)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	int index = element->index;

	pr_info("idx = %d,  index= %d\n",
		idx, index);
	if (index >= OCP81375_LEVEL_NUM)
		index = OCP81375_LEVEL_NUM - 1;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK)
			ocp81375_reg_value[REG_TORCH_CURRENT_LED1] = led1_level_torch[index];
		else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT)
			ocp81375_reg_value[REG_TORCH_CURRENT_LED2] = led2_level_torch[index];
		else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
			ocp81375_reg_value[REG_TORCH_CURRENT_LED1] = led1_level_torch[index];
			ocp81375_reg_value[REG_TORCH_CURRENT_LED2] = led2_level_torch[index];
		} else
			pr_err("Error: idx = %d!!!\n",
				idx);

#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;
}

static int sprd_flash_ocp81375_cfg_value_highlight_bk(
	void *drvd, uint8_t idx, struct sprd_flash_element *element)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	int index = element->index;

	pr_info("idx = %d,  index= %d\n",
		idx, index);
	if (index >= OCP81375_LEVEL_NUM)
		index = OCP81375_LEVEL_NUM - 1;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_BK ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT)
			ocp81375_reg_value[REG_FLASH_CURRENT_LED1] = led1_level_flash[index];
		else if (drv_data->flash_cfg.flash_idx == LED2_BK ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK)
			ocp81375_reg_value[REG_FLASH_CURRENT_LED2] = led2_level_flash[index];
		else if (drv_data->flash_cfg.flash_idx == LED1_LED2_BK) {
			ocp81375_reg_value[REG_FLASH_CURRENT_LED1] = led1_level_flash[index];
			ocp81375_reg_value[REG_FLASH_CURRENT_LED2] = led2_level_flash[index];
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;
}

static int sprd_flash_ocp81375_cfg_value_highlight_front(
	void *drvd, uint8_t idx, struct sprd_flash_element *element)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	int index = element->index;

	pr_info("idx = %d,  index= %d\n",
		idx, index);
	if (index >= OCP81375_LEVEL_NUM)
		index = OCP81375_LEVEL_NUM - 1;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			ocp81375_reg_value[REG_FLASH_CURRENT_LED1] = led1_level_flash[index];
		} else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
			ocp81375_reg_value[REG_FLASH_CURRENT_LED2] = led2_level_flash[index];
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
			ocp81375_reg_value[REG_FLASH_CURRENT_LED1] = led1_level_flash[index];
			ocp81375_reg_value[REG_FLASH_CURRENT_LED2] = led2_level_flash[index];
		} else
			pr_err("Error: idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);

#ifdef OCP81375_REG_CHECK
		sprd_flash_ocp81375_read_flag(drv_data);
#endif
	}
	return 0;
}

/*add by ping.luan for sprd platform support 360flash systemUI flash device node. add start*/
#ifdef TRAN_CAMERA_FLASH_SYSTEMUI
static int get_current_torch_state_bk(void *drvd, uint8_t *state)
{
	u8 data = 0;
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		flash_ocp81375_driver_reg_read(
			drv_data->i2c_info, FLASH_IC_REG_ENABLE, &data);
		if (drv_data->flash_cfg.flash_idx == LED1_BK ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
			if (0x1 == (data & 0x1))
				*state = 1;
		} else if (drv_data->flash_cfg.flash_idx == LED2_BK ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			if (0x1 == ((data >> 1) & 0x1))
				*state = 1;
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_BK) {
			if (0x3 == (data & 0x3))
				*state = 1;
		} else {
			pr_err("Error::get_default_torch_level_front  idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);
			return -1;
		}
	}
	return 0;
}

static int get_current_torch_state_front(void *drvd, uint8_t *state)
{
	u8 data = 0;
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
	if (drv_data->i2c_info) {
		flash_ocp81375_driver_reg_read(
			drv_data->i2c_info, FLASH_IC_REG_ENABLE, &data);
		if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
			if (0x1 == (data & 0x1))
				*state = 1;
		} else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
			drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
			if (0x1 == ((data >> 1) & 0x1))
				*state = 1;
		} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
			if (0x3 == (data & 0x3))
				*state = 1;
		} else {
			pr_err("Error::get_default_torch_level_front  idx = %d!!!\n",
				drv_data->flash_cfg.flash_idx);
			return -1;
		}
	}
	return 0;
}

#endif
/*add by ping.luan for sprd platform support 360flash systemUI flash device node. add end*/

//[ITD] modify transsion flash param by ping.luan@transsion.com 22/07/21 start
#ifdef TRAN_FLASH_PARAM

static int set_torch_current_front(void *drvd, int cur)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	uint8_t currentReg = 0;
	int ret = -1;

	currentReg = (cur * 100 - 390) / 390;  //from spec torch reg table
	if (drv_data->flash_cfg.flash_idx == LED1_FRONT ||
		drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK) {
		ocp81375_reg_value[REG_TORCH_CURRENT_LED1] = currentReg;
		pr_info("torch percent led1 ctrl reg=0x%x", currentReg);
	} else if (drv_data->flash_cfg.flash_idx == LED2_FRONT ||
		drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
		ocp81375_reg_value[REG_TORCH_CURRENT_LED2] = currentReg;
		pr_info("torch percent led2 ctrl reg=0x%x", currentReg);
	} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_FRONT) {
		ocp81375_reg_value[REG_TORCH_CURRENT_LED1] = currentReg;
		ocp81375_reg_value[REG_TORCH_CURRENT_LED2] = currentReg;
		pr_info("torch percent led1&led2 ctrl reg=0x%x", currentReg);
	} else {
		pr_err("Error::torch percent ctrl idx = %d!!!\n", drv_data->flash_cfg.flash_idx);
		goto exit;
	}

	ret = 0;

exit:
	return ret;
}

static int set_torch_current_bk(void *drvd, int cur)
{
	struct ocp81375_flash_driver_data *drv_data =
		(struct ocp81375_flash_driver_data *)drvd;
	uint8_t currentReg = 0;
	int ret = -1;

	currentReg = (cur * 100 - 390)  / 390;  //from spec torch reg table
	if (drv_data->flash_cfg.flash_idx == LED1_BK ||
		drv_data->flash_cfg.flash_idx == LED1_BK_LED2_FRONT) {
		ocp81375_reg_value[REG_TORCH_CURRENT_LED1] = currentReg;
		pr_info("torch percent led1 ctrl reg=0x%x", currentReg);
	} else if (drv_data->flash_cfg.flash_idx == LED1_FRONT_LED2_BK ||
		drv_data->flash_cfg.flash_idx == LED2_BK) {
		ocp81375_reg_value[REG_TORCH_CURRENT_LED2] = currentReg;
		pr_info("torch percent led2 ctrl reg=0x%x", currentReg);
	} else if (drv_data->flash_cfg.flash_idx == LED1_LED2_BK) {
		ocp81375_reg_value[REG_TORCH_CURRENT_LED1] = currentReg;
		ocp81375_reg_value[REG_TORCH_CURRENT_LED2] = currentReg;
		pr_info("torch percent led1&led2 ctrl reg=0x%x", currentReg);
	} else {
		pr_err("Error::torch percent ctrl idx = %d!!!\n", drv_data->flash_cfg.flash_idx);
		goto exit;
	}

	ret = 0;

exit:
	return ret;
}
#endif
//[ITD] modify transsion flash param by ping.luan@transsion.com 22/07/21 end
//[ITD] modify transsion flash common by ping.luan@transsion.com 22/07/19 start
#ifdef FLASH_USE_ONE_KO_TRANSSION
static int init_param_front(void *drvd, int torch_max_current, int flash_max_current)
{
	struct ocp81375_flash_driver_data *pdata = (struct ocp81375_flash_driver_data *)drvd;
	int step_torch = 0, step_flash = 0;
	int torch_current = 0, flash_current = 0;
	unsigned int j;
	int flash_index = pdata->flash_cfg.flash_idx;
	//pr_info("systemUI  flash-idx %d\n",flash_index);

	if ((flash_index == LED1_FRONT) ||
		(flash_index == LED1_LED2_FRONT) || (flash_index == LED1_FRONT_LED2_BK)) {
		pdata->flash_cfg.led1_max_torch = torch_max_current;
		pdata->flash_cfg.led1_max_flash = flash_max_current;

		step_torch = (pdata->flash_cfg.led1_max_torch * 100) / OCP81375_LEVEL_NUM;
		step_flash = (pdata->flash_cfg.led1_max_flash * 100) / OCP81375_LEVEL_NUM;
		for (j = 0; j < OCP81375_LEVEL_NUM; j++) {
#ifdef USING_AW36515_FLASH_LEVEL_TRANSSION
			led1_level_torch[j] = led1_aw36515_level_torch[j];
			led1_level_flash[j] = led1_aw36515_level_flash[j];
#else
			led1_level_torch[j] = (step_torch * (j + 1) - 390) / 390;
			led1_level_flash[j] = (step_flash * (j + 1) - 1563) / 1563;
#endif
			if (led1_level_torch[j] <= 0)
				led1_level_torch[j] = 1;

			if (led1_level_flash[j] <= 0)
				led1_level_flash[j] = 1;

			torch_current = (led1_level_torch[j] * 390 + 390)  / 100,
				flash_current = (led1_level_flash[j] * 1563 + 1563) / 100;
			pr_info("level%d : torch current = %d,flash current = %d\n",
				j, torch_current, flash_current);
		}
	}
	if ((flash_index == LED1_BK_LED2_FRONT) ||
		(flash_index == LED1_LED2_FRONT) || (flash_index == LED2_FRONT)) {
		pdata->flash_cfg.led2_max_torch = torch_max_current;
		pdata->flash_cfg.led2_max_flash = flash_max_current;

		step_torch = (pdata->flash_cfg.led2_max_torch * 100) / OCP81375_LEVEL_NUM;
		step_flash = (pdata->flash_cfg.led2_max_flash * 100) / OCP81375_LEVEL_NUM;
		for (j = 0; j < OCP81375_LEVEL_NUM; j++) {
#ifdef USING_AW36515_FLASH_LEVEL_TRANSSION
			led2_level_torch[j] = led2_aw36515_level_torch[j];
			led2_level_flash[j] = led2_aw36515_level_flash[j];
#else
			led2_level_torch[j] = (step_torch * (j + 1) - 390) / 390;
			led2_level_flash[j] = (step_flash * (j + 1) - 1563) / 1563;
#endif
			if (led2_level_torch[j] <= 0)
				led2_level_torch[j] = 1;

			if (led2_level_flash[j] <= 0)
				led2_level_flash[j] = 1;

			torch_current = (led2_level_torch[j] * 390 + 390) / 100,
				flash_current = (led2_level_flash[j] * 1563 + 1563) / 100;
			pr_info("level%d : torch current = %d,flash current = %d\n",
				j, torch_current, flash_current);
		}
	}
	return 0;
}
static int init_param_bk(void *drvd, int torch_max_current, int flash_max_current)
{
	struct ocp81375_flash_driver_data *pdata = (struct ocp81375_flash_driver_data *)drvd;
	unsigned int j;
	int step_torch = 0, step_flash = 0;
	int torch_current = 0, flash_current = 0;
	int flash_index = pdata->flash_cfg.flash_idx;
	//pr_info("systemUI  flash-idx %d\n",flash_index);

	if ((flash_index == LED1_BK) || (flash_index == LED1_BK_LED2_FRONT) ||
		(flash_index == LED1_LED2_BK)) {
		pdata->flash_cfg.led1_max_torch = torch_max_current;
		pdata->flash_cfg.led1_max_flash = flash_max_current;

		step_torch = (pdata->flash_cfg.led1_max_torch * 100) / OCP81375_LEVEL_NUM;
		step_flash = (pdata->flash_cfg.led1_max_flash * 100) / OCP81375_LEVEL_NUM;
		for (j = 0; j < OCP81375_LEVEL_NUM; j++) {
#ifdef USING_AW36515_FLASH_LEVEL_TRANSSION
			led1_level_torch[j] = led1_aw36515_level_torch[j];
			led1_level_flash[j] = led1_aw36515_level_flash[j];
#else
			led1_level_torch[j] = (step_torch * (j + 1) - 390) / 390;
			led1_level_flash[j] = (step_flash * (j + 1) - 1563) / 1563;
#endif
			if (led1_level_torch[j] <= 0)
				led1_level_torch[j] = 1;

			if (led1_level_flash[j] <= 0)
				led1_level_flash[j] = 1;

			torch_current = (led1_level_torch[j] * 390 + 390) / 100,
				flash_current = (led1_level_flash[j] * 1563 + 1563) / 100;
			pr_info("level%d : torch current = %d,flash current = %d\n",
				j, torch_current, flash_current);
		}
	}
	if ((flash_index == LED1_FRONT_LED2_BK) ||
		(flash_index == LED1_LED2_BK) || (flash_index == LED2_BK)) {
		pdata->flash_cfg.led2_max_torch = torch_max_current;
		pdata->flash_cfg.led2_max_flash = flash_max_current;

		step_torch = (pdata->flash_cfg.led2_max_torch * 100) / OCP81375_LEVEL_NUM;
		step_flash = (pdata->flash_cfg.led2_max_flash * 100) / OCP81375_LEVEL_NUM;
		for (j = 0; j < OCP81375_LEVEL_NUM; j++) {
#ifdef USING_AW36515_FLASH_LEVEL_TRANSSION
			led2_level_torch[j] = led2_aw36515_level_torch[j];
			led2_level_flash[j] = led2_aw36515_level_flash[j];
#else
			led2_level_torch[j] = (step_torch * (j + 1) - 390) / 390;
			led2_level_flash[j] = (step_flash * (j + 1) - 1563) / 1563;
#endif
			if (led2_level_torch[j] <= 0)
				led2_level_torch[j] = 1;

			if (led2_level_flash[j] <= 0)
				led2_level_flash[j] = 1;

			torch_current = (led2_level_torch[j] * 390 + 390) / 100,
				flash_current = (led2_level_flash[j] * 1563 + 1563) / 100;
			pr_info("level%d : torch current = %d,flash current = %d\n",
				j, torch_current, flash_current);
		}
	}
	return 0;
}
#endif
//ITD: add FLSYQPUB-43 360 systemUI Torch by ping.luan 20220119 end.


static const struct of_device_id sprd_flash_ic_of_match_table[] = {
	{.compatible = "sprd,flash-ocp81375"},
	{/* MUST end with empty struct */},
};

static const struct i2c_device_id sprd_flash_ic_ids[] = {
	{}
};
static const struct sprd_flash_driver_ops flash_ic_ops_bk = {
	.open_torch = sprd_flash_ocp81375_open_torch_bk,
	.close_torch = sprd_flash_ocp81375_close_torch_bk,
	.open_preflash = sprd_flash_ocp81375_open_preflash_bk,
	.close_preflash = sprd_flash_ocp81375_close_preflash,
	.open_highlight = sprd_flash_ocp81375_open_highlight_bk,
	.close_highlight = sprd_flash_ocp81375_close_highlight,
	.cfg_value_torch = sprd_flash_ocp81375_cfg_value_torch_bk,
	.cfg_value_preflash = sprd_flash_ocp81375_cfg_value_preflash_bk,
	.cfg_value_highlight = sprd_flash_ocp81375_cfg_value_highlight_bk,
/*add by bingfeng.li for sprd platform support 360flash systemUI flash device node. add start*/
#ifdef TRAN_CAMERA_FLASH_SYSTEMUI
	.get_current_torch_state = get_current_torch_state_bk,
#endif
/*add by bingfeng.li for sprd platform support 360flash systemUI flash device node. add end*/
//[ITD] modify transsion flash common by ping.luan@transsion.com 22/07/19 start
#ifdef FLASH_USE_ONE_KO_TRANSSION
	.init_param = init_param_bk,
#endif
//[ITD] modify transsion flash common by ping.luan@transsion.com 22/07/19 end
//[ITD] modify transsion flash param by ping.luan@transsion.com 22/07/21 start
#ifdef TRAN_FLASH_PARAM
	.set_torch_current = set_torch_current_bk,
#endif
//[ITD] modify transsion flash param by ping.luan@transsion.com 22/07/21 end
};

static const struct sprd_flash_driver_ops flash_ic_ops_front = {
	.open_torch = sprd_flash_ocp81375_open_torch_front,
	.close_torch = sprd_flash_ocp81375_close_torch_front,
	.open_preflash = sprd_flash_ocp81375_open_preflash_front,
	.close_preflash = sprd_flash_ocp81375_close_preflash,
	.open_highlight = sprd_flash_ocp81375_open_highlight_front,
	.close_highlight = sprd_flash_ocp81375_close_highlight,
	.cfg_value_torch = sprd_flash_ocp81375_cfg_value_torch_front,
	.cfg_value_preflash = sprd_flash_ocp81375_cfg_value_preflash_front,
	.cfg_value_highlight = sprd_flash_ocp81375_cfg_value_highlight_front,
/*add by bingfeng.li for sprd platform support 360flash systemUI flash device node. add start*/
#ifdef TRAN_CAMERA_FLASH_SYSTEMUI
	.get_current_torch_state = get_current_torch_state_front,
#endif
/*add by bingfeng.li for sprd platform support 360flash systemUI flash device node. add end*/
//[ITD] modify transsion flash common by ping.luan@transsion.com 22/07/19 start
#ifdef FLASH_USE_ONE_KO_TRANSSION
	.init_param = init_param_front,
#endif
//[ITD] modify transsion flash common by ping.luan@transsion.com 22/07/19 end
//[ITD] modify transsion flash param by ping.luan@transsion.com 22/07/21 start
#ifdef TRAN_FLASH_PARAM
	.set_torch_current = set_torch_current_front,
#endif
//[ITD] modify transsion flash param by ping.luan@transsion.com 22/07/21 end
};

static int sprd_flash_ocp81375_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	u8 device_id = 0;
	struct device *dev = &client->dev;
	struct ocp81375_flash_driver_data *pdata = NULL;
	unsigned int j;
	int flash_index = 0;

	pr_info("flash-ic-driver ocp81375 probe\n");
	if (!dev->of_node) {
		pr_err("no device node %s", __func__);
		return -ENODEV;
	}
	ret = of_property_read_u32(dev->of_node, "flash-ic", &j);
	if (ret || j != 81375)
		return -ENODEV;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	client->dev.platform_data = (void *)pdata;
	pdata->i2c_info = client;
	mutex_init(&pdata->i2c_lock);
	pdata->i2c_info->addr = (I2C_SLAVEADDR);

	flash_ocp81375_driver_reg_read(client, REG_DEVICE_ID, &device_id);
	if (device_id  != OCP81375_DEVICE_ID) {
		pr_err("device id not match!,id = %d\n", device_id);
		goto exit;
	} else {
		pr_info("flash-ic-driver ocp81375 probe	device_id= %d\n", device_id);
	}
	ret = of_property_read_u32(dev->of_node, "flash-idx", &pdata->flash_cfg.flash_idx);
	if (ret) {
		pdata->flash_cfg.flash_idx = 0;
		pr_err("flash-idx no cfg\n");
		goto exit;
	}
	flash_index = pdata->flash_cfg.flash_idx;

	ret = sprd_flash_ocp81375_init(pdata);
	if (ret) {
		pdata->flash_cfg.led1_max_flash = 0;
		pr_info("sprd_flash_ocp81375_init failed\n");
		goto exit;
	}
	if ((flash_index == LED1_BK) || (flash_index == LED2_BK) ||
		((flash_index >= LED1_BK_LED2_FRONT) && (flash_index <= LED1_LED2_BK))) {
		sprd_flash_register(&flash_ic_ops_bk, pdata, 0);
	}
	if ((flash_index == LED1_LED2_FRONT) || (flash_index == LED2_FRONT) ||
		((flash_index >= LED1_FRONT) && (flash_index <= LED1_FRONT_LED2_BK))) {
		sprd_flash_register(&flash_ic_ops_front, pdata, 1);
	}
	drv_data_tmp = pdata;
	pr_info("flash-ic-driver ocp81375 probe succeed\n");
	return 0;
exit:
	pr_info("flash-ic-driver ocp81375 probe failed\n");
	return ret;

}
static int sprd_flash_ocp81375_driver_remove(struct i2c_client *client)
{
	struct ocp81375_flash_driver_data *pdata = NULL;

	pr_info("sprd_flash_ocp81375_driver_remove");
	if (drv_data_tmp) {
		sprd_flash_ocp81375_close_torch_front(drv_data_tmp, SPRD_FLASH_LED0);
		sprd_flash_ocp81375_close_torch_bk(drv_data_tmp, SPRD_FLASH_LED0);
	}
	pdata = (struct ocp81375_flash_driver_data *)client->dev.platform_data;
	if (pdata)
		devm_kfree(&client->dev, pdata);

	pdata = NULL;
	client->dev.platform_data = NULL;
	return 0;
}

static void sprd_flash_ocp81375_driver_shutdown(struct i2c_client *client)
{
	struct ocp81375_flash_driver_data *drv_data = NULL;

	drv_data = (struct ocp81375_flash_driver_data *)client->dev.platform_data;
	if (!drv_data)
		return;
	if (drv_data->i2c_info)
		flash_ocp81375_driver_reg_write(drv_data->i2c_info, FLASH_IC_REG_ENABLE, 0x00);

}

static struct i2c_driver sprd_flash_ocp81375_driver = {
	.driver = {
		.of_match_table = of_match_ptr(sprd_flash_ic_of_match_table),
		.name = FLASH_IC_DRIVER_NAME,
	},
	.probe = sprd_flash_ocp81375_driver_probe,
	.remove = sprd_flash_ocp81375_driver_remove,
	.id_table = sprd_flash_ic_ids,
	.shutdown = sprd_flash_ocp81375_driver_shutdown,
};

static int sprd_flash_ocp81375_register_driver(void)
{
	int ret = 0;

	ret = i2c_add_driver(&sprd_flash_ocp81375_driver);
	pr_info("register sprd_flash_ocp81375_driver:%d\n", ret);
	return ret;
}

static void sprd_flash_ocp81375_unregister_driver(void)
{
	i2c_del_driver(&sprd_flash_ocp81375_driver);
}


int __init ocp81375_flash_init(void)
{
	int ret = 0;

	ret = sprd_flash_ocp81375_register_driver();
	return ret;
}
void __exit ocp81375_flash_exit(void)
{
	sprd_flash_ocp81375_unregister_driver();
}
module_init(ocp81375_flash_init);
module_exit(ocp81375_flash_exit);
MODULE_DESCRIPTION("Sprd ocp81375 Flash Driver");
MODULE_LICENSE("GPL");
