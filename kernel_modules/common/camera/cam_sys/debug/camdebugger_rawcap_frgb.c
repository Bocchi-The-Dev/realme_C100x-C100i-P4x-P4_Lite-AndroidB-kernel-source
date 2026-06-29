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
#define pr_fmt(fmt) "CAMDEBUGGER_RAWCAP_FRGB: %d %d %s : " fmt, current->pid, __LINE__, __func__

uint32_t g_dbg_raw2frgb_switch = DEBUG_RAWCAP_MODE;

void camdebugger_rawcap_frgb_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_dbg_raw2frgb_switch = simple_strtol(param, NULL, 0);
	pr_info("set raw2frgb_switch %u\n", g_dbg_raw2frgb_switch);
}

void camdebugger_rawcap_frgb_read(struct seq_file *s, uint32_t idx)
{
	const char *desc = "0: rawcap mode, 1: frgb mode";
	char *str = "Example: echo rawcap_frgb_switch:1 >debug_node";
	seq_printf(s, "%u\n\n%s\n\n%s\n\n", g_dbg_raw2frgb_switch, desc ,str);
}

