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
#define pr_fmt(fmt) "CAMDEBUGGER_ISP_BYPASS_CTRL: %d %d %s : " fmt, current->pid, __LINE__, __func__

uint64_t g_isp_bypass[ISP_CONTEXT_SW_NUM] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
uint64_t g_isp_block_dump = {0};

void camdebugger_isp_bypass_write(struct cam_hw_info *hw, char *name, char *param, uint32_t idx)
{
	struct cam_hw_bypass_data data = {0};
	struct isp_hw_ltm_3dnr_param parm = {0};
	uint32_t type ={0};
	uint32_t bypass_cnt = 0;
	uint32_t val = 0;
	uint32_t bypass_all = 0;
	uint64_t bypass_val = 1;
	uint64_t i = 0;

	if (!hw->s_isp_dev_debug) { /* isp not working */
		pr_err(" fail to enable isp Hardware\n");
		return;
	}

	if (idx == HW_CTX_MAX) {
		pr_err("fail to get right hw_ctx\n");
		return;
	}

	val = simple_strtol(param, NULL, 0);
	val = val != 0 ? 1 : 0;
	/* find */
	if (strcmp(name, "isp_all") == 0) {
		bypass_all = 1;
	} else { /* check special: ltm, nr3 */
		if (strcmp(name, "ltm") == 0) {
			parm.idx = idx;
			parm.val = val;
			hw->isp_ioctl(hw, ISP_HW_CFG_LTM_PARAM, &parm);
			bypass_val = (bypass_val << _EISP_LTM);
			g_isp_bypass[idx] &= (~bypass_val);
			if (val)
				g_isp_bypass[idx] |= bypass_val;
			return;
		} else if (strcmp(name, "nr3") == 0 ||
			strcmp(name, "3dnr") == 0) {
			bypass_val = (bypass_val << _EISP_NR3);
			g_isp_bypass[idx] &= (~bypass_val);
			if (val)
				g_isp_bypass[idx] |= bypass_val;
			parm.idx = idx;
			parm.val = val;
			hw->isp_ioctl(hw, ISP_HW_CFG_3DNR_PARAM, &parm);
			/* ISP_HREG_MWR(ISP_FBC_3DNR_STORE_PARAM, BIT_0, val); */
			return;
		}
	}
	type = ISP_BYPASS_TYPE;
	bypass_cnt = hw->isp_ioctl(hw, ISP_HW_CFG_BYPASS_COUNT_GET, &type);
	data.type = ISP_BYPASS_TYPE;

	if (strcmp(name, "dump_all") == 0) {
		printk("dump all isp block\n");
		for (i = 0; i < bypass_cnt; i++) {
			g_isp_block_dump &= (~((uint64_t)1 << i));
			g_isp_block_dump |= ((uint64_t)1 << i);
		}
		return;
	}
	if (strcmp(name, "stop_dump") == 0) {
		printk("stop dump all isp block\n");
		g_isp_block_dump = 0;
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
				g_isp_block_dump &= (~(1 << i));
				g_isp_block_dump |= (1 << i);
				if (!bypass_all)
					break;
			}
		}
		/* not opreate */
		if ((!bypass_all) && i >= bypass_cnt)
			pr_info("Not operate, isp%d,name:%s val:%d\n", idx, name, val);
	} else {
		for (i = 0; i < bypass_cnt; i++) {
			data.i = i;
			hw->isp_ioctl(hw, ISP_HW_CFG_BYPASS_DATA_GET, &data);
			if (data.tag == NULL) {
				continue;
			}
			if (data.tag->p == NULL)
				continue;
			if (strcmp(data.tag->p, name) == 0 || bypass_all) {
				pr_debug("set isp addr 0x%x, bit %d val %d\n",
					data.tag->addr, data.tag->bpos, val);
				if (i < _EISP_TOTAL) {
					bypass_val = val;
					bypass_val = bypass_val << i;
					g_isp_bypass[idx] &= (~bypass_val);
					g_isp_bypass[idx] |= bypass_val;
				}
				if (bypass_all && (data.tag->all == 0))
					continue;
				ISP_REG_MWR(idx, data.tag->addr, 1 << data.tag->bpos,
					val << data.tag->bpos);

				if (!bypass_all)
					break;
			}
		}
			/* not opreate */
		if ((!bypass_all) && i >= bypass_cnt)
			pr_info("Not operate, isp%d,name:%s val:%d\n", idx, name, val);

	}

}

void camdebugger_isp_bypass_read(struct seq_file *s, uint32_t idx)
{
	uint32_t addr = 0, val = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_bypass_data data = {0};
	uint32_t type = 0;
	uint32_t bypass_cnt = 0;
	uint32_t i = 0;

	if (!s) {
		pr_err("fail to get valid input para\n");
		return;
	}
	hw = (struct cam_hw_info *)s->private;

	if (idx == HW_CTX_MAX) {
		pr_err("fail to get right hw_ctx\n");
		return;
	}

	if (!hw->s_isp_dev_debug) { /* isp not working */
		seq_printf(s, "isp hardware not working, can't read\n");
		return;
	}
	seq_printf(s, "===========isp context %d=============\n", idx);
	type = ISP_BYPASS_TYPE;
	bypass_cnt = hw ->isp_ioctl(hw , ISP_HW_CFG_BYPASS_COUNT_GET, &type);
	data.type = ISP_BYPASS_TYPE;
	for (i = 0; i < bypass_cnt; i++) {
		data.i = i;
		hw ->isp_ioctl(hw , ISP_HW_CFG_BYPASS_DATA_GET, &data);
		if (data.tag == NULL) {
			continue;
		}
		if (data.tag->p == NULL)
			continue;

		addr = data.tag->addr;
		val = ISP_REG_RD(idx, addr) & (1 << data.tag->bpos);
		if (val)
			seq_printf(s, "%s:%d  bypass\n", data.tag->p, val);
		else
			seq_printf(s, "%s:%d  work\n", data.tag->p, val);
	}
	seq_puts(s, "\n isp_all:1:bypass_pre0 //bypass all except preview path\n");
	seq_puts(s, "\nltm:1:bypass_pre0 //(ltm-hist,ltm-map)\n");
	seq_puts(s, "\nnr3:1:bypass_pre0 //or 3dnr:1:bypass_pre0(all of 3dnr block)\n");
}

