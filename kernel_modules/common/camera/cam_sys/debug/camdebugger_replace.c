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
#define pr_fmt(fmt) "CAMDEBUGGER_REPLACE: %d %d %s : " fmt, current->pid, __LINE__, __func__

struct cam_dbg_replace g_dbg_replace;

uint32_t g_dbg_replace_src = REPLACE_IMG_YUV;
uint32_t g_dbg_replace_switch = 0;

void camdebugger_replace_control_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	uint32_t val = 0;

	val = simple_strtol(param, NULL, 0);
	pr_info("set replace_control %u\n", val);

	cam_replace_node_set(&val);
}

void camdebugger_replace_control_read(struct seq_file *s, uint32_t idx)
{
	seq_printf(s, "\nUsage:\n");
	seq_printf(s, "  The different bits represent replace pipeline, node, port, state\n");
	seq_printf(s, "  Replace image form dump, need change image name \n");
	seq_printf(s, "      1.setenforce 0\n");
	seq_printf(s, "      2.echo replace_ctrl:val >debug_node\n");
	seq_printf(s, "      3.echo replace_file_fmt:replace img_fmt >debug_node\n");
	seq_printf(s, "  val = (pipeline_type << 3)|(node_type << 2)|(port_id << 1)|(state << 0)\n");
	seq_printf(s, "  replace img_fmt = character string\n");
	seq_printf(s, "\nExample:\n");
	seq_printf(s, "  replace capture_pipeline dcam_online_node full_port enable:\n");
	seq_printf(s, "      setenforce 0\n");
	seq_printf(s, "      echo replace_ctrl:2021 >debug_node\n");
	seq_printf(s, "      echo replace_file_fmt:yuv >debug_node\n");
	seq_printf(s, "  2->cap pipeline, 0->dcam online node, 2->full port, 1->work\n");
	seq_printf(s, "\nReplace Info:\n");
	seq_printf(s, "      Pipeline_type:        Preview = 0, Video = 1, Capture = 2, Zsl_Capture = 3...\n");
	seq_printf(s, "      Node_type:            Dcam_online = 0, Dcam_offline = 1, Offline_dec = 6...\n");
	seq_printf(s, "      Dcam_online port_id:  Bin = 1, Full = 2...\n");
	seq_printf(s, "      State:                Bypass = 0, Work = 1.\n");
	seq_printf(s, "      replace img_fmt:      yuv, mipiraw, raw...\n");
	seq_printf(s, "\nPS:\n");
	seq_printf(s, "      Replace PREV/VID pipeline need input replace_switch\n");
	seq_printf(s, "      Replace CAP pipeline need start capture\n");
	seq_printf(s, "      Reopen camera after input replace cmd\n");
}

void camdebugger_replace_file_fmt_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	uint32_t val = 0;

	if (strcmp(param, "yuv") == 0)
		val = REPLACE_IMG_YUV;
	else if (strcmp(param, "mipi") == 0)
		val = REPLACE_IMG_MIPIRAW;
	else if (strcmp(param, "raw") == 0)
		val = REPLACE_IMG_RAW;

	g_dbg_replace_src = val;
	pr_info("set replace src image type %u\n", g_dbg_replace_src);
}

void camdebugger_replace_switch_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_dbg_replace_switch = simple_strtol(param, NULL, 0);
	pr_info("set replaceswitch %u\n", g_dbg_replace_switch);
}

void camdebugger_replace_switch_read(struct seq_file *s, uint32_t idx)
{
	const char *desc = "PREV/VIDEO pipeline dump 0: disable, 1: enable\n";
	seq_printf(s, "%u\n\n%s\n", g_dbg_replace_switch, desc);
}

