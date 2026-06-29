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

#ifndef _ISP_HW_H_
#define _ISP_HW_H_

#include "dcam_hw_adpt.h"
#include "isp_hw_adpt.h"
#include "sprd_isp_2v6.h"

#define SHRINK_Y_UP_TH                  235
#define SHRINK_Y_DN_TH                  16
#define SHRINK_UV_UP_TH                 240
#define SHRINK_UV_DN_TH                 16
#define SHRINK_Y_OFFSET                 16
#define SHRINK_Y_RANGE                  3
#define SHRINK_C_OFFSET                 16
#define SHRINK_C_RANGE                  6

enum isp_afbd_data_bits {
	AFBD_FETCH_8BITS = 5,
	AFBD_FETCH_10BITS = 7,
};

#endif
