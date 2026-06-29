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
#define pr_fmt(fmt) "CAMDEBUGGER_FBC_CONTROL: %d %d %s : " fmt, current->pid, __LINE__, __func__

uint32_t g_dbg_fbc_control = 0;

void camdebugger_fbc_control_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_dbg_fbc_control = simple_strtol(param, NULL, 0);
	pr_info("set fbc_control %u\n", g_dbg_fbc_control);
}

void camdebugger_fbc_control_read(struct seq_file *s, uint32_t idx)
{
	const char *desc = "bit 0:bin 1:full 2:raw\n";

	seq_printf(s, "%u\n\n%s\n", g_dbg_fbc_control, desc);
	seq_printf(s, "\nUsage:\n");
	seq_printf(s, "         echo fbc_ctrl:val >debug_node\n");
	seq_printf(s, "The different bits represent fbc switch in different path\n");
	seq_printf(s, "\nExample:\n");
	seq_printf(s, "         echo fbc_ctrl:6 >debug_node   // bit 110, bypass full and raw path fbc\n");
	seq_printf(s, "         echo fbc_ctrl:3 >debug_node   // bit 011, bypass bin and full path fbc\n");
}

