/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2022-2023 Unisoc(Shanghai) Technologies Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _CAM_KERNEL_ADAPT_H_
#define _CAM_KERNEL_ADAPT_H_

#include <linux/version.h>
#if (KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE)
#include <linux/pm_runtime.h>
#if (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
#include <linux/dma-heap.h>
#endif
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
#include <soc/unisoc/sprd_dmabuf.h>
#else
#include <linux/sprd_ion.h>
#include <uapi/linux/sprd_dmabuf.h>
#endif
#else
#include <video/sprd_mmsys_pw_domain.h>
#include "ion.h"
#endif

struct file *cam_kernel_adapt_filp_open(const char *filename, int, umode_t);
int cam_kernel_adapt_filp_close(struct file *filp, fl_owner_t id);
ssize_t cam_kernel_adapt_read(struct file *file, void *buf, size_t count, loff_t *pos);
ssize_t cam_kernel_adapt_write(struct file *file, void *buf, size_t count, loff_t *pos);
void cam_kernel_adapt_kproperty_get(const char *key, char *value, const char *default_value);
int cam_kernel_adapt_syscon_get_args_by_name(struct device_node *np,
	const char *name, int arg_count, unsigned int *out_args);
struct regmap *cam_kernel_adapt_syscon_regmap_lookup_by_name
	(struct device_node *np, const char *name);

#endif
