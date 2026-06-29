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
#define pr_fmt(fmt) "CAMDEBUGGER_IOMMU_MODE: %d %d %s : " fmt, current->pid, __LINE__, __func__

int g_dbg_iommu_mode = IOMMU_AUTO;
int g_dbg_set_iommu_mode = IOMMU_AUTO;

static uint8_t iommu_mode_string[4][32] = {
	"IOMMU_AUTO",
	"IOMMU_OFF",
	"IOMMU_ON_RESERVED",
	"IOMMU_ON"
};

void camdebugger_iommu_mode_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	int val = 0;

	val = simple_strtol(param, NULL, 0);
	if (val == 0)
		g_dbg_set_iommu_mode = IOMMU_AUTO;
	else if (val == 1)
		g_dbg_set_iommu_mode = IOMMU_OFF;
	else if (val == 2)
		g_dbg_set_iommu_mode = IOMMU_ON_RESERVED;
	else if (val == 3)
		g_dbg_set_iommu_mode = IOMMU_ON;
	else
		pr_err("fail to get valid work mode: %d\n", val);

	pr_info("set_iommu_mode : %d(%s)\n",
		g_dbg_set_iommu_mode,
		iommu_mode_string[g_dbg_set_iommu_mode&3]);
}

void camdebugger_iommu_mode_read(struct seq_file *s, uint32_t idx)
{
	seq_printf(s, "cur: %d(%s), next: %d(%s)\n",
		g_dbg_iommu_mode,
		iommu_mode_string[g_dbg_iommu_mode&3],
		g_dbg_set_iommu_mode,
		iommu_mode_string[g_dbg_set_iommu_mode&3]);
}

