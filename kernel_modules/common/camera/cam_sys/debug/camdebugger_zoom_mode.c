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
#define pr_fmt(fmt) "CAMDEBUGGER_ZOOM_MODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static char zoom_mode_strings[5][8] = {
	"bypass", "bin2", "bin4", "scaler", "rds"
};

void camdebugger_zoom_mode_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	int val = 0;

	val = simple_strtol(param, NULL, 0);
	if (val == 0)
		g_camctrl.dcam_zoom_mode = ZOOM_DEBUG_DEFAULT;
	else if (val == 1)
		g_camctrl.dcam_zoom_mode = ZOOM_DEBUG_BINNING2;
	else if (val == 2)
		g_camctrl.dcam_zoom_mode = ZOOM_DEBUG_BINNING4;
	else if (val == 3)
		g_camctrl.dcam_zoom_mode = ZOOM_DEBUG_SCALER;
	else if (val == 4)
		g_camctrl.dcam_zoom_mode = ZOOM_DEBUG_RDS;
	else
		pr_err("fail to get valid zoom mode: %d\n", val);

	if (g_camctrl.dcam_zoom_mode >= ZOOM_DEBUG_DEFAULT)
		pr_info("set zoom mode %d(%s)\n", g_camctrl.dcam_zoom_mode - ZOOM_DEBUG_DEFAULT,
			zoom_mode_strings[g_camctrl.dcam_zoom_mode - ZOOM_DEBUG_DEFAULT]);
	else
		pr_info("set zoom mode %d(%s)\n", g_camctrl.dcam_zoom_mode,
			zoom_mode_strings[g_camctrl.dcam_zoom_mode]);
}

void camdebugger_zoom_mode_read(struct seq_file *s, uint32_t idx)
{
	const char *desc = "0: bypass, 1: bin2, 2: bin4, 3: scaler, 4: rds";

	if (g_camctrl.dcam_zoom_mode >= ZOOM_DEBUG_DEFAULT)
		seq_printf(s, "%d(%s)\n%s\n", g_camctrl.dcam_zoom_mode - ZOOM_DEBUG_DEFAULT,
			zoom_mode_strings[g_camctrl.dcam_zoom_mode - ZOOM_DEBUG_DEFAULT], desc);
	else
		seq_printf(s, "%d(%s)\n%s\n", g_camctrl.dcam_zoom_mode,
			zoom_mode_strings[g_camctrl.dcam_zoom_mode], desc);
}

void camdebugger_preview_full_size_write(struct cam_hw_info *hw,
	char *name, char *param, uint32_t idx)
{
	long val = 0;
	g_camctrl.preview_full_size = kstrtol(param, 0, &val);
	pr_info("set preview_full_size: %d, value %ld\n", g_camctrl.preview_full_size, val);
}

void camdebugger_preview_full_size_read(struct seq_file *s, uint32_t idx)
{
	seq_printf(s, "preview_full_size: %d\n", g_camctrl.preview_full_size);
}

void camdebugger_rds_limit_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_camctrl.dcam_rds_limit = simple_strtol(param, NULL, 0);
	pr_info("set rds limit %d\n", g_camctrl.dcam_rds_limit);
}

void camdebugger_rds_limit_read(struct seq_file *s, uint32_t idx)
{
	seq_printf(s, "rds limit %d\n", g_camctrl.dcam_rds_limit);
}

