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
#define pr_fmt(fmt) "CAMDEBUGGER_DCAM_BYPASS_CTRL: %d %d %s : " fmt, current->pid, __LINE__, __func__

uint32_t g_dcam_bypass[DCAM_HW_CONTEXT_MAX] = {0};
uint32_t g_dcam_block_dump = {0};

void camdebugger_dcam_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	struct cam_hw_bypass_data data = {0};
	uint32_t type = 0;
	uint32_t bypass_cnt = 0;
	uint32_t i = 0;
	int bypass_all = 0;
	uint32_t val = 0;

	if (!hw->s_dcam_dev_debug) {
		pr_err(" fail to enable dcam Hardware\n");
		return;
	}

	if (idx == HW_CTX_MAX) {
		pr_err("fail to get right hw_ctx\n");
		return;
	}

	val = simple_strtol(param, NULL, 0);
	val = val != 0 ? 1 : 0;
	/* find */
	if (strcmp(name, "dcam_all") == 0)
		bypass_all = 1;

	type = DCAM_BYPASS_TYPE;
	bypass_cnt = hw->isp_ioctl(hw, ISP_HW_CFG_BYPASS_COUNT_GET, &type);
	data.type = DCAM_BYPASS_TYPE;

	if (strcmp(name, "dump_all") == 0) {
		printk("dump all dcam block\n");
		for (i = 0; i < bypass_cnt; i++) {
			g_dcam_block_dump &= (~(1 << i));
			g_dcam_block_dump |= (1 << i);
		}
		return;
	}
	if (strcmp(name, "stop_dump") == 0) {
		printk("stop dump all dcam block\n");
		g_dcam_block_dump = 0;
		return;
	}

	if (strcmp(param, "dump") == 0) {
		for (i = 0; i < bypass_cnt; i++) {
			data.i = i;
			hw->isp_ioctl(hw, ISP_HW_CFG_BYPASS_DATA_GET, &data);
			if (data.tag == NULL)
				continue;
			if (data.tag->p == NULL)
				continue;
			if (strcmp(data.tag->p, name) == 0 || bypass_all){
				printk("set dcam%d i %d, addr 0x%x, bit %d val %d\n",
					idx, i, data.tag->addr, data.tag->bpos, val);
				g_dcam_block_dump &= (~(1 << i));
				g_dcam_block_dump |= (1 << i);
				if (!bypass_all)
					break;
			}
		}
		/* not opreate */
		if ((!bypass_all) && i >= bypass_cnt)
			pr_info("Not operate, dcam%d,name:%s val:%d\n", idx, name, val);
	} else {
		for (i = 0; i < bypass_cnt; i++) {
			data.i = i;
			hw->isp_ioctl(hw, ISP_HW_CFG_BYPASS_DATA_GET, &data);
			if (data.tag == NULL)
				continue;
			if (data.tag->p == NULL)
				continue;
			if (strcmp(data.tag->p, name) == 0 || bypass_all){
				printk("set dcam%d i %d, addr 0x%x, bit %d val %d\n",
					idx, i, data.tag->addr, data.tag->bpos, val);
				g_dcam_bypass[idx] &= (~(1 << i));
				g_dcam_bypass[idx] |= (val << i);

				os_adapt_time_msleep(20); /* If PM writing,wait little time */

				DCAM_REG_MWR(idx, data.tag->addr, 1 << data.tag->bpos,
					val << data.tag->bpos);
				/* afl need rgb2y work */
				if (strcmp(name, "afl") == 0)
					DCAM_REG_MWR(idx, ISP_AFL_PARAM0, BIT_0, val);
				if (!bypass_all)
					break;
			}
		}
		/* not opreate */
		if ((!bypass_all) && i >= bypass_cnt)
			pr_info("Not operate, dcam%d,name:%s val:%d\n", idx, name, val);
	}
}


void camdebugger_dcam_bypass_read(struct seq_file *s, uint32_t idx)
{
	uint32_t addr = 0, val = 0;
	struct cam_hw_bypass_data data = {0};
	uint32_t type = 0;
	uint32_t bypass_cnt = 0;
	uint32_t i = 0;
	struct cam_hw_info *hw = NULL;

	if (idx == HW_CTX_MAX) {
		pr_err("fail to get right hw_ctx\n");
		return;
	}

	hw = (struct cam_hw_info *)s->private;
	data.type = DCAM_BYPASS_TYPE;
	type = DCAM_BYPASS_TYPE;
	bypass_cnt = hw->isp_ioctl(hw, ISP_HW_CFG_BYPASS_COUNT_GET, &type);
	seq_printf(s, "-----dcam%d-----\n", idx);
	if (!hw->s_dcam_dev_debug) {
		seq_puts(s, "Hardware not enable\n");
	} else {
		for (i = 0; i < bypass_cnt; i++) {
			data.i = i;
			hw->isp_ioctl(hw, ISP_HW_CFG_BYPASS_DATA_GET, &data);
			if (data.tag == NULL)
				continue;
			if (data.tag->p == NULL)
				continue;
			addr = data.tag->addr;
			val = DCAM_REG_RD(idx, addr) & (1 << data.tag->bpos);
			if (val)
				seq_printf(s, "%s:bit%d=1 bypass\n",
					data.tag->p, data.tag->bpos);
			else
				seq_printf(s, "%s:bit%d=0  work\n",
					data.tag->p, data.tag->bpos);
		}
		seq_puts(s, "\ndcam_all:1:bypass_dcam0 #to bypass all\n\n");
		seq_puts(s, "Example: make 4in1 module bypass(1) or work(0)\n");
		seq_puts(s, "bypass: echo 4in1:1:bypass_dcam0 > debug_node\n");
		seq_puts(s, "work: echo 4in1:0:bypass_dcam0 > debug_node\n\n");
	}
}

