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

#ifdef CAM_HW_ADPT_LAYER
#include <linux/uaccess.h>
#include <sprd_mm.h>

#include "pyr_dec_node.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "PYRDEC_HW: %d %d %s : "fmt, current->pid, __LINE__, __func__

static pyrdec_k_blk_func pyrdec_hw_k_blk_func_tab[PYR_DEC_K_BLK_MAX] = {
	[PYR_DEC_K_BLK_IRQ_FUNC] = pyr_dec_irq_func,
	[PYR_DEC_K_BLK_CFG] = pyr_dec_config,
	[PYR_DEC_K_BLK_DCT_UPDATE] = isp_k_update_dct,
};

static int pyrdechw_k_blk_func_get(void *handle, void *arg)
{
	struct pyrdec_hw_k_blk_func *func_arg = NULL;

	func_arg = (struct pyrdec_hw_k_blk_func *)arg;

	if (func_arg->index < PYR_DEC_K_BLK_MAX)
		func_arg->k_blk_func = pyrdec_hw_k_blk_func_tab[func_arg->index];

	return 0;
}

static struct hw_io_ctrl_fun pyrdec_ioctl_fun_tab[] = {
	{PYRDEC_HW_CFG_K_BLK_FUNC_GET,          pyrdechw_k_blk_func_get},
};

static hw_ioctl_fun pyrdechw_ioctl_fun_get(enum pyrdec_hw_cfg_cmd cmd)
{
	hw_ioctl_fun hw_ctrl = NULL;
	uint32_t total_num = 0;
	uint32_t i = 0;

	total_num = sizeof(pyrdec_ioctl_fun_tab) / sizeof(struct hw_io_ctrl_fun);
	for (i = 0; i < total_num; i++) {
		if (cmd == pyrdec_ioctl_fun_tab[i].cmd) {
			hw_ctrl = pyrdec_ioctl_fun_tab[i].hw_ctrl;
			break;
		}
	}

	return hw_ctrl;
}
#endif
