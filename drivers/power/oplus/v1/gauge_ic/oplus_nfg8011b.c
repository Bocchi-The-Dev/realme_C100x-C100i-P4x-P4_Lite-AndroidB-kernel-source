// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2026 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) "[NFG8011B]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <asm/unaligned.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <oplus_gauge.h>
#include "oplus_bq27541.h"
#include "oplus_nfg8011b.h"

#define NFG8011B_BLOCK_SIZE 32

static u8 nfg8011b_calc_checksum(u8 *buf, int len)
{
	u8 checksum = 0;
	while (len--)
		checksum += buf[len];
	return 0xff - checksum;
}

static int nfg8011b_block_check_conditions(struct chip_bq27541 *chip, u8 *buf, int len, int offset, bool do_checksum)
{
	if (!chip || !buf || offset < 0 || offset >= NFG8011B_BLOCK_SIZE || len <= 0 ||
	    (len + do_checksum > NFG8011B_BLOCK_SIZE) || (offset + len + do_checksum > NFG8011B_BLOCK_SIZE)) {
		chg_err("%soffset %d or len %d invalid\n", buf ? "buf is null or " : "", offset, len);
		return -EINVAL;
	}

	if (atomic_read(&chip->suspended) || atomic_read(&chip->suspended))
		return -EINVAL;

	return 0;
}

int nfg8011b_read_block(struct chip_bq27541 *chip, int addr, u8 *buf, int len, int offset, bool do_checksum)
{
	int ret;
	int data_check;
	int try_count = NFG8011B_SUBCMD_TRY_COUNT;
	u8 extend_data[NFG8011B_BLOCK_SIZE + 2] = { 0 };
	u8 checksum = 0;

	ret = nfg8011b_block_check_conditions(chip, buf, len, offset, do_checksum);
	if (ret < 0)
		return ret;

try:
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = gauge_i2c_txsubcmd(chip, NFG8011B_DATAFLASHBLOCK, addr);
	if (ret < 0)
		goto error;
	usleep_range(1000, 1000);
	ret = gauge_read_i2c_block(chip, NFG8011B_DATAFLASHBLOCK, (offset + len + do_checksum + 2), extend_data);
	if (ret < 0)
		goto error;

	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != addr) {
		chg_err("0x%04x not match. try_count=%d extend_data[0]=0x%2x, extend_data[1]=0x%2x\n", addr, try_count,
			extend_data[0], extend_data[1]);
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		usleep_range(2000, 2000);
		goto try;
	}
	if (try_count < 0)
		goto error;

	if (do_checksum) {
		checksum = nfg8011b_calc_checksum(&extend_data[offset + 2], len);
		if (checksum != extend_data[offset + len + 2]) {
			chg_err("[%*ph]checksum not match. expect=0x%02x actual=0x%02x\n",
				offset + len + do_checksum + 2, extend_data, checksum, extend_data[offset + len + 2]);
			goto error;
		}
	}

	memcpy(buf, &extend_data[offset + 2], len);
	chg_info("addr=0x%04x offset=%d buf=[%*ph] do_checksum=%d read success\n", addr, offset, len, buf, do_checksum);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return 0;

error:
	chg_info("addr=0x%04x offset=%d buf=[%*ph] do_checksum=%d read fail\n", addr, offset, len, buf, do_checksum);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return -EINVAL;
}

int nfg8011b_write_block(struct chip_bq27541 *chip, int addr, u8 *buf, int len, int offset, bool do_checksum)
{
	int ret;
	int data_check;
	int try_count = NFG8011B_SUBCMD_TRY_COUNT;
	u8 extend_read_data[NFG8011B_BLOCK_SIZE + 2] = { 0 };
	u8 extend_write_data[NFG8011B_BLOCK_SIZE + 2] = { 0 };
	u8 checksum = 0;

	ret = nfg8011b_block_check_conditions(chip, buf, len, offset, do_checksum);
	if (ret < 0)
		return ret;

try:
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	ret = gauge_i2c_txsubcmd(chip, NFG8011B_DATAFLASHBLOCK, addr);
	if (ret < 0)
		goto error;
	usleep_range(1000, 1000);
	ret = gauge_read_i2c_block(chip, NFG8011B_DATAFLASHBLOCK, (NFG8011B_BLOCK_SIZE + 2), extend_read_data);
	if (ret < 0)
		goto error;

	data_check = (extend_read_data[1] << 0x8) | extend_read_data[0];
	if (try_count-- > 0 && data_check != addr) {
		chg_err("0x%04x not match. try_count=%d offset=%d extend_data[0]=0x%2x, extend_data[1]=0x%2x\n", addr,
			try_count, offset, extend_read_data[0], extend_read_data[1]);
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		usleep_range(2000, 2000);
		goto try;
	}
	if (try_count < 0)
		goto error;

	memcpy(extend_write_data, extend_read_data, NFG8011B_BLOCK_SIZE + 2);
	memcpy(&extend_write_data[offset + 2], buf, len);
	if (do_checksum)
		extend_write_data[offset + len + 2] = nfg8011b_calc_checksum(buf, len);
	ret = gauge_i2c_txsubcmd(chip, NFG8011B_DATAFLASHBLOCK, addr);
	if (ret < 0)
		goto error;
	ret = gauge_write_i2c_block(chip, NFG8011B_AUTHENDATA_1ST, NFG8011B_BLOCK_SIZE, extend_write_data + 2);
	if (ret < 0)
		goto error;
	checksum = nfg8011b_calc_checksum(extend_write_data, NFG8011B_BLOCK_SIZE + 2);
	ret = gauge_i2c_txsubcmd_onebyte(chip, NFG8011B_AUTHENCHECKSUM, checksum);
	if (ret < 0)
		goto error;
	ret = gauge_i2c_txsubcmd_onebyte(chip, NFG8011B_AUTHENLEN, 0x24);
	if (ret < 0)
		goto error;

	try_count = NFG8011B_SUBCMD_TRY_COUNT;
	do {
		data_check = true;
		memset(extend_read_data, 0, NFG8011B_BLOCK_SIZE + 2);
		usleep_range(15000, 15000);
		ret = gauge_i2c_txsubcmd(chip, NFG8011B_DATAFLASHBLOCK, addr);
		if (ret < 0)
			goto error;
		usleep_range(1000, 1000);
		ret = gauge_read_i2c_block(chip, NFG8011B_DATAFLASHBLOCK, NFG8011B_BLOCK_SIZE + 2, extend_read_data);
		if (memcmp(extend_read_data, extend_write_data, NFG8011B_BLOCK_SIZE + 2)) {
			chg_err("reg not match.extend_read_data =[%*ph]\n", NFG8011B_BLOCK_SIZE + 2, extend_read_data);
			chg_err("reg not match.extend_write_data=[%*ph]\n", NFG8011B_BLOCK_SIZE + 2, extend_write_data);
			data_check = false;
		}
	} while (!data_check && try_count--);
	if (!data_check)
		goto error;
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	chg_info("addr=0x%04x offset=%d buf=[%*ph] write success\n", addr, offset, len, buf);
	return 0;

error:
	chg_info("addr=0x%04x offset=%d buf=[%*ph] write fail\n", addr, offset, len, buf);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return -EINVAL;
}