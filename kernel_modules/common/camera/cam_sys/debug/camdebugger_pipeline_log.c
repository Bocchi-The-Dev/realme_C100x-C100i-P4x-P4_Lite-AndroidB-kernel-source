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
#define pr_fmt(fmt) "CAMDEBUGGER_PIPELINE_LOG: %d %d %s : " fmt, current->pid, __LINE__, __func__

uint32_t g_pipeline_type = CAM_PIPELINE_TYPE_MAX;

void camdebugger_pipeline_log_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	g_pipeline_type = simple_strtol(param, NULL, 0);
	pr_info("set pipeline log type %d\n", g_pipeline_type);
}

void camdebugger_pipeline_log_read(struct seq_file *s, uint32_t idx)
{
	seq_printf(s, "pipeline num %u\n", g_pipeline_type);
	seq_printf(s, "\n Example: open Preview Pipeline Log\n");
	seq_printf(s, "\n echo debug_log_switch:0 >debug_node\n");
	seq_printf(s, "\n Pipeline Type ID\n");
	seq_printf(s, " 0: CAM_PIPELINE_PREVIEW\n");
	seq_printf(s, " 1: CAM_PIPELINE_VIDEO\n");
	seq_printf(s, " 2: CAM_PIPELINE_CAPTURE\n");
	seq_printf(s, " 3: CAM_PIPELINE_ZSL_CAPTURE\n");
	seq_printf(s, " 4: CAM_PIPELINE_SENSOR_RAW\n");
	seq_printf(s, " 5: CAM_PIPELINE_SCALER_YUV\n");
	seq_printf(s, " 6: CAM_PIPELINE_OFFLINE_RAW2YUV\n");
	seq_printf(s, " 7: CAM_PIPELINE_ONLINERAW_2_OFFLINEYUV\n");
	seq_printf(s, " 8: CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV\n");
	seq_printf(s, " 9: CAM_PIPELINE_ONLINERAW_2_BPCRAW_2_USER_2_OFFLINEYUV\n");
	seq_printf(s, " 10: CAM_PIPELINE_ONLINERAW_2_USER_2_BPCRAW_2_USER_2_OFFLINEYUV\n");
	seq_printf(s, " 11: CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_RAW2USER2YUV\n");
	seq_printf(s, " 12: CAM_PIPELINE_ONLINE_NORMALZSLCAPTURE_OR_RAW2USER2YUV\n");
	seq_printf(s, " 13: Close Pipeline Log\n");
}

