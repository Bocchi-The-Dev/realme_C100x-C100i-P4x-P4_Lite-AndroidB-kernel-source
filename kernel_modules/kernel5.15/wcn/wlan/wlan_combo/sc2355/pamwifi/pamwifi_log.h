/*
 * SPDX-FileCopyrightText: 2015-2022 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2015-2022 Unisoc (Shanghai) Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef _PAM_WIFI_LOG_H__
#define _PAM_WIFI_LOG_H__

#include <linux/io.h>
#include <linux/printk.h>

#define TAG "pamwifi:"
enum PW_LOG_LEVEL {
	PW_ERR = 0, /*LEVEL_ERR*/
	PW_WARN, /*LEVEL_WARNING*/
	PW_INFO,/*LEVEL_INFO*/
	PW_DBG, /*LEVEL_DEBUG*/
};

extern int g_pw_debug_level;

#define pw_debug(fmt, args...) \
	do { \
		if (g_pw_debug_level >= PW_DBG) { \
			pr_alert(TAG fmt, ##args); \
		} \
	} while (0)

#define pw_err(fmt, args...) \
	do { \
		if (g_pw_debug_level >= PW_ERR) \
		pr_alert(TAG fmt, ##args); \
	} while (0)

#define pw_warn(fmt, args...) \
	do { \
		if (g_pw_debug_level >= PW_WARN) \
		pr_alert(TAG fmt, ##args); \
	} while (0)

#define pw_info(fmt, args...) \
	do { \
		if (g_pw_debug_level >= PW_INFO) { \
			pr_alert(TAG fmt, ##args); \
		} \
	} while (0)

#define pw_err_ratelimited(fmt, args...) \
	do { \
		if (g_pw_debug_level >= L_ERR) \
		printk_ratelimited(TAG fmt, ##args); \
	} while (0)

#define pw_alert(fmt, args...) \
	do { \
		pr_alert(TAG fmt, ##args); \
	} while (0)

#endif
