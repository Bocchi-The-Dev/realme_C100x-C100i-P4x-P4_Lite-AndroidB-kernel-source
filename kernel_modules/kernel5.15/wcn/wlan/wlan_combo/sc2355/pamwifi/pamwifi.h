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

#ifndef _PAM_WIFI_API_H
#define _PAM_WIFI_API_H

#include <linux/io.h>
#include <linux/sipa.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip.h>
#include <linux/kthread.h>
#include <sc2355/cmdevt.h>
#include <common/common.h>

#define PAMWIFI_OK 0
#define PAMWIFI_ERROR -1
#define PAMWIFI_DISABLED -2
#define PAMWIFI_BUSY -3

int sprd_pamwifi_init(struct platform_device *pdev,
	                   struct sprd_priv *priv);

void sprd_pamwifi_uninit(struct platform_device *pdev);

void sprd_pamwifi_enable(struct sprd_vif *vif);

void sprd_pamwifi_disable(struct sprd_vif *vif);

int sprd_pamwifi_pause_chip(void);

int sprd_pamwifi_resume_chip(void);

int sprd_pamwifi_xmit_to_ipa(struct sk_buff *skb,
	                     struct net_device *ndev);

void sprd_pamwifi_update_router_table(struct sprd_priv *priv,
        void  *data,	u8 vif_mode, u32 index, int add);

void sprd_pamwifi_ul_resource_event(struct sprd_vif *vif,
	 u8 *data, u16 len);

int  sprd_pamwifi_send_ul_res_cmd(struct sprd_priv *priv,
	                     u8 vif_ctx_id,
				void *data, u16 len);

bool sprd_pamwifi_using_ap(void);

int sprd_pamwifi_settlv_cmd(u8 *addr, u16 max_len);

int sprd_pamwifi_save_capability(void *cap);

int sprd_pamwifi_get_captlv_size(void);

bool sprd_pamwifi_hw_supported(struct platform_device *pdev);

bool sprd_pamwifi_supported(struct platform_device *pdev);


#endif
