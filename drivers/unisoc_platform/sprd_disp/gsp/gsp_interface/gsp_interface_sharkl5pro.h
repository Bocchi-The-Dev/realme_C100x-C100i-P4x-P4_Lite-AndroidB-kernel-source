/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _GSP_INTERFACE_SHARKL5PRO_H
#define _GSP_INTERFACE_SHARKL5PRO_H

#include <linux/of.h>
#include <linux/regmap.h>
#include "../gsp_interface.h"


#define GSP_SHARKL5PRO "sharkl5pro"
/*
 * qogirl6 (ums9230) is the same r8p0 GSP silicon and uses this same
 * interface.  Its live device tree says compatible = "sprd,gsp-r8p0-qogirl6",
 * and gsp_interface_copy_name() strips two dash-separated tokens, so the name
 * it dispatches on is "qogirl6" -- it must be accepted here or the driver
 * bails out with "no match interface for gsp".
 */
#define GSP_QOGIRL6 "qogirl6"

#define SHARKL5PRO_AP_AHB_DISP_EB_NAME	  ("clk_ap_ahb_disp_eb")

struct gsp_interface_sharkl5pro {
	struct gsp_interface common;

	struct clk *clk_ap_ahb_disp_eb;
	struct regmap *module_en_regmap;
	struct regmap *reset_regmap;
};

int gsp_interface_sharkl5pro_parse_dt(struct gsp_interface *intf,
				  struct device_node *node);

int gsp_interface_sharkl5pro_init(struct gsp_interface *intf);
int gsp_interface_sharkl5pro_deinit(struct gsp_interface *intf);

int gsp_interface_sharkl5pro_prepare(struct gsp_interface *intf);
int gsp_interface_sharkl5pro_unprepare(struct gsp_interface *intf);

int gsp_interface_sharkl5pro_reset(struct gsp_interface *intf);

void gsp_interface_sharkl5pro_dump(struct gsp_interface *intf);

#endif
