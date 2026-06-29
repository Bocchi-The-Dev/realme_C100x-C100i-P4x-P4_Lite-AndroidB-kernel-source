/*
*SPDX-FileCopyrightText:2023 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier:GPL-2.0-only
*/

#include "vpu_drv.h"
#include "sprd_vpu.h"

const struct vpu_ops vsp_hevc_lite = {
	.get_eb_clk = get_eb_clk_lite,
	.clock_enable = clock_enable_lite,
	.clock_disable = clock_disable_lite,
	.check_pw_status = vsp_check_pw_status,
	.get_reset_mask = get_reset_mask,
};
/* for SHARKLe,SHARKL5,SHARKL5PRO,SHARKL6 */


