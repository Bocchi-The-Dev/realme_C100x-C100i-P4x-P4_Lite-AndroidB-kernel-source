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
#define pr_fmt(fmt) "CAMDEBUGGER_DUMP: %d %d %s : " fmt, current->pid, __LINE__, __func__

struct cam_dbg_dump g_dbg_dump[DUMP_NUM_MAX];

uint32_t g_dbg_dumpswitch = 0;
uint32_t g_dbg_dumpcount = 0;

void camdebugger_dump_switch_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_dbg_dumpswitch = simple_strtol(param, NULL, 0);
	pr_info("set dumpswitch %u\n", g_dbg_dumpswitch);
}

void camdebugger_dump_switch_read(struct seq_file *s, uint32_t idx)
{
	seq_printf(s, "g_dbg_dumpswitch %u\n", g_dbg_dumpswitch);
	seq_printf(s, "PREV/VIDEO pipeline dump 0: disable, 1: enable\n");
}

void camdebugger_dump_control_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	uint32_t val = 0;

	val = simple_strtol(param, NULL, 16);
	pr_info("set dump_control %x\n", val);

	cam_dump_node_set(&val);
}

void camdebugger_dump_control_read(struct seq_file *s, uint32_t idx)
{
	seq_printf(s, "\nUsage:\n");
	seq_printf(s, "  The different bits represent dump pipeline, node, port, state\n");
	seq_printf(s, "      1.setenforce 0\n");
	seq_printf(s, "      2.echo dump_ctrl:val >debug_node\n");
	seq_printf(s, "  val = (pipeline_type << 3)|(node_type << 2)|(port_id << 1)|(state << 0)\n");
	seq_printf(s, "\nExample:\n");
	seq_printf(s, "  Dump capture_pipeline dcam_online_node full_port enable:\n");
	seq_printf(s, "      setenforce 0\n");
	seq_printf(s, "      echo dump_ctrl:2021 >debug_node\n");
	seq_printf(s, "  2->cap pipeline, 0->dcam online node, 2->full port, 1->work\n");
	seq_printf(s, "\nDump Info:\n");
	seq_printf(s, "      Pipeline_type:        Preview = 0, Video = 1, Capture = 2, Zsl_Capture = 3, Online_normal2yuv_or_raw2user2yuv = b...\n");
	seq_printf(s, "      Node_type:            Dcam_online = 0, Dcam_offline = 1, Offline_dec = 8...\n");
	seq_printf(s, "      Dcam_online port_id:  Bin = 1, Full = 2...\n");
	seq_printf(s, "      State:                Bypass = 0, Work = 1.\n");
	seq_printf(s, "\nPS:\n");
	seq_printf(s, "      Dump PREV/VID pipeline need input dump_switch\n");
	seq_printf(s, "      Dump CAP pipeline need start capture\n");
	seq_printf(s, "      Reopen camera after input dump cmd\n");
}

void camdebugger_dump_count_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_dbg_dumpcount = simple_strtol(param, NULL, 0);
	pr_info("set dump_count %u\n", g_dbg_dumpcount);
}

