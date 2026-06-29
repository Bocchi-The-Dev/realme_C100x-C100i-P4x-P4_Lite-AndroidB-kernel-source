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

#include <linux/uaccess.h>
#include <sprd_mm.h>

#include "cam_debugger.h"
#include "dcam_core.h"
#include "dcam_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "RDS: %d %d %s : " fmt, current->pid, __LINE__, __func__

int dcam_k_cfg_rds(struct isp_io_param *param, struct dcam_isp_k_block *p)
{
	int ret = 0;
	void *pcpy = NULL;
	struct dcam_dev_rds_param *rds = NULL;

	pcpy = (void *)&(p->rds);

	mutex_lock(&p->param_lock);
	ret = copy_from_user(pcpy, param->property_param,
		sizeof(struct dcam_dev_rds_param));
	if (ret) {
		mutex_unlock(&p->param_lock);
		pr_err("fail to copy from user ret=0x%x\n", (unsigned int)ret);
		return -EPERM;
	}
	rds = &(p->rds);
	pr_debug("rds mode %d, B %d, R %d", rds->mode, rds->B, rds->R);
	mutex_unlock(&p->param_lock);

	return ret;
}
