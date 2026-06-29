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
#define pr_fmt(fmt) "CAMDEBUGGER_REG_READ: %d %d %s : " fmt, current->pid, __LINE__, __func__

/* read dcamx register once */
void camdebugger_dcam_reg_read(struct seq_file *s, uint32_t idx)
{
	uint32_t addr = 0;
	struct cam_hw_info *hw = NULL;
	const uint32_t addr_end[] = {0x400, 0x400, 0x110, 0x110, 0x100, 0x100};

	hw = (struct cam_hw_info *)s->private;
	if (!hw->s_dcam_dev_debug) {
		seq_puts(s, "Hardware not enable\n");
		return;
	}

	if (idx >= HW_CTX_MAX) {
		pr_err("fail to get right hw_ctx\n");
		return;
	}

	if (idx >= 3 && idx < 5) {
		seq_printf(s, "-----dcam axi%d and fetch----\n", idx - 3);
		for (addr = 0; addr < addr_end[idx]; addr += 4)
			seq_printf(s, "0x%04x: 0x%08x\n",
				addr, REG_RD(g_dcam_aximbase[idx - 3] + addr));

		seq_puts(s, "--------------------\n");
	} else {
		seq_printf(s, "-----dcam%d----------\n", idx);
		for (addr = 0; addr < addr_end[idx]; addr += 4)
			seq_printf(s, "0x%04x: 0x%08x\n",
				addr, DCAM_REG_RD(idx, addr));

		seq_puts(s, "--------------------\n");
	}
}

void camdebugger_reg_buf_read(struct seq_file *s, uint32_t idx)
{
	if (idx == HW_CTX_MAX) {
		pr_err("fail to get right hw_ctx\n");
		return;
	}

	isp_cfg_ctx_reg_buf_debug_show((void *)s, idx);
}

