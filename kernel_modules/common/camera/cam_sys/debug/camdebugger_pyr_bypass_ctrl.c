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

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <os_adapt_common.h>
#include <sprd_mm.h>

#include "cam_debugger.h"
#include "dcam_reg.h"
#include "isp_cfg.h"
#include "isp_reg.h"
#include "cam_pipeline.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAMDEBUGGER_PYR_BYPASS_CTRL: %d %d %s : " fmt, current->pid, __LINE__, __func__

uint32_t g_pyr_dec_online_bypass = 0;
uint32_t g_pyr_dec_offline_bypass = 0;

void camdebugger_pyr_dec_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_pyr_dec_online_bypass = simple_strtol(param, NULL, 0);
	pr_info("set pyr_dec_online_bypass %u\n", g_pyr_dec_online_bypass);
}

void camdebugger_pyr_dec_bypass_read(struct seq_file *s, uint32_t idx)
{
	const char *desc = "0: work, 1: bybass";
	char *str = "Example: echo pyr_dec_bypass:1 >debug_node";
	seq_printf(s, "%u\n\n%s\n\n%s\n\n", g_pyr_dec_online_bypass, desc ,str);
}

void camdebugger_isp_pyr_dec_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_pyr_dec_offline_bypass = simple_strtol(param, NULL, 0);
	pr_info("set pyr_dec_offline_bypass %u\n", g_pyr_dec_offline_bypass);
}

void camdebugger_isp_pyr_dec_bypass_read(struct seq_file *s, uint32_t idx)
{
	const char *desc = "0: work, 1: bybass";
	char *str = "Example: echo 1 > offline_dec_bypass";
	seq_printf(s, "%u\n\n%s\n\n%s\n\n", g_pyr_dec_offline_bypass, desc ,str);
}

