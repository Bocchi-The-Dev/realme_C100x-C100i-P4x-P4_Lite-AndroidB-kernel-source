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
#define pr_fmt(fmt) "CAMDEBUGGER_LBUF_LEN: %d %d %s : " fmt, current->pid, __LINE__, __func__

extern uint32_t s_dbg_linebuf_len;

void camdebugger_lbuf_len_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	s_dbg_linebuf_len = simple_strtol(param, NULL, 0);
	pr_info("set line buf len %d.  %s\n", s_dbg_linebuf_len, param);
}

void camdebugger_lbuf_len_read(struct seq_file *s, uint32_t idx)
{
	seq_printf(s, "%d\n", s_dbg_linebuf_len);
}

