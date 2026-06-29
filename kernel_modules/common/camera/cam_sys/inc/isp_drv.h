/*
 * Copyright (C) 2021-2022 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _ISP_DRIVER_H_
#define _ISP_DRIVER_H_

int isp_drv_hw_init(void *arg);
int isp_drv_hw_deinit(void *arg);
int isp_drv_dt_parse(struct device_node *dn, struct cam_hw_info *hw_info);
int isp_drv_trim_deci_info_cal(uint32_t src, uint32_t dst, uint32_t *trim, uint32_t *deci);
uint32_t isp_drv_deci_factor_cal(uint32_t deci);
#endif
