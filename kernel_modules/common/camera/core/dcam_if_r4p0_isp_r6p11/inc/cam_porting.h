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

#ifndef _CAM_PORT_H_
#define _CAM_PORT_H_

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <linux/pm_runtime.h>
#include <linux/ion.h>
#else
#include <video/sprd_mmsys_pw_domain.h>
#include "ion.h"
/* #include "ion_priv.h" */
#endif

#ifndef KERNEL_515_TIME_COMPAT
#define KERNEL_515_TIME_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
typedef struct timespec64 timespec;
#define ktime_get_ts ktime_get_ts64
typedef struct __kernel_old_timeval timeval;
#else
typedef struct timespec timespec;
typedef struct timeval timeval;
#endif
#endif // ifndef KERNEL_515_TIME_COMPAT

struct dma_buf * cam_ion_alloc(size_t len, unsigned int heap_id_mask,
					unsigned int flags);
void cam_ion_free(struct dma_buf *dmabuf);

struct file *cam_filp_open(const char *, int, umode_t);
int cam_filp_close(struct file *, fl_owner_t id);
ssize_t cam_kernel_read(struct file *, void *, size_t, loff_t *);
ssize_t cam_kernel_write(struct file *, void *, size_t, loff_t *);

void cam_kproperty_get(const char *key, char *value,
	const char *default_value);

int cam_syscon_get_args_by_name(struct device_node *np,
				const char *name,
				int arg_count,
				unsigned int *out_args);
struct regmap *cam_syscon_regmap_lookup_by_name(
					struct device_node *np,
					const char *name);
timespec cam_timespec_sub(timespec lhs, timespec rhs);


#endif
