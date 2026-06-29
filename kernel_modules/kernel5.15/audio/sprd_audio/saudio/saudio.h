/*
 * SPDX-FileCopyrightText: 2015 Spreadtrum Communications (Shanghai) Co., Ltd
 * SPDX-FileCopyrightText: 2016 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */
#ifndef __SAUDIO_H
#define  __SAUDIO_H

#define SAUDIO_DEV_MAX 3

struct saudio_init_data {
	char	*name;
	uint8_t	dst;
	uint8_t	ctrl_channel;
	uint8_t	playback_channel[SAUDIO_DEV_MAX];
	uint8_t	capture_channel;
	uint8_t	monitor_channel;
	uint8_t	device_num;
	uint32_t	ap_addr_to_cp;
};

#endif
