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

#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include "cam_kernel_adapt.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "cam_kernel_adapt: %d %d %s : " fmt, current->pid, __LINE__, __func__

/*kernal 5.15 file operation need import namespace*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#ifdef CONFIG_SPRD_DCAM_DEBUG_RAW
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))

struct file *cam_kernel_adapt_filp_open(const char *filename, int flags, umode_t mode)
{
#ifdef CONFIG_SPRD_DCAM_DEBUG_RAW
	return filp_open(filename, flags, mode);
#else
	return NULL;
#endif


}

int cam_kernel_adapt_filp_close(struct file *filp, fl_owner_t id)
{
#ifdef CONFIG_SPRD_DCAM_DEBUG_RAW
	return filp_close(filp, id);
#else
	return 0;
#endif
}

ssize_t cam_kernel_adapt_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
#ifdef CONFIG_SPRD_DCAM_DEBUG_RAW
	return kernel_read(file, buf, count, pos);
#else
	return 0;
#endif
}

ssize_t cam_kernel_adapt_write(struct file *file, void *buf, size_t count, loff_t *pos)
{
#ifdef CONFIG_SPRD_DCAM_DEBUG_RAW
	return kernel_write(file, buf, count, pos);
#else
	return 0;
#endif
}

void cam_kernel_adapt_kproperty_get(const char *key, char *value,
	const char *default_value)
{
}

int cam_kernel_adapt_syscon_get_args_by_name(struct device_node *np,
				const char *name,
				int arg_count,
				unsigned int *out_args)
{
	struct regmap *reg_map = NULL;

	reg_map = syscon_regmap_lookup_by_phandle_args(np, name, arg_count, out_args);
	if (IS_ERR_OR_NULL(reg_map)) {
		pr_err("fail to get syscon %s %d, %p\n", name, arg_count, out_args);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(cam_kernel_adapt_syscon_get_args_by_name);

struct regmap *cam_kernel_adapt_syscon_regmap_lookup_by_name(
					struct device_node *np,
					const char *name)
{
	return syscon_regmap_lookup_by_phandle(np, name);
}
EXPORT_SYMBOL(cam_kernel_adapt_syscon_regmap_lookup_by_name);
#else
extern void sprd_kproperty_get(const char *key, char *value,
	const char *default_value);

struct file *cam_kernel_adapt_filp_open(const char *filename, int flags, umode_t mode)
{
	return filp_open(filename, flags, mode);
}

int cam_kernel_adapt_filp_close(struct file *filp, fl_owner_t id)
{
	return filp_close(filp, id);
}

ssize_t cam_kernel_adapt_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
	return kernel_read(file, buf, count, &file->f_pos);
}

ssize_t cam_kernel_adapt_write(struct file *file, void *buf, size_t count, loff_t *pos)
{
	return kernel_write(file, buf, count, &file->f_pos);
}

void cam_kernel_adapt_kproperty_get(const char *key, char *value,
	const char *default_value)
{
#if defined(PROJ_SHARKL5PRO) || defined(PROJ_QOGIRL6) || defined(PROJ_QOGIRN6PRO)
	sprd_kproperty_get(key, value, default_value);
#endif
}

int cam_kernel_adapt_syscon_get_args_by_name(struct device_node *np,
				const char *name,
				int arg_count,
				unsigned int *out_args)
{
	int args_count = 0;

	args_count = syscon_get_args_by_name(np, name, arg_count, out_args);
	if (args_count != arg_count) {
		pr_err("fail to get syscon %s %d, %p\n", name, arg_count, out_args);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(cam_kernel_adapt_syscon_get_args_by_name);

struct regmap *cam_kernel_adapt_syscon_regmap_lookup_by_name(
					struct device_node *np,
					const char *name)
{
	return syscon_regmap_lookup_by_name(np, name);
}
EXPORT_SYMBOL(cam_kernel_adapt_syscon_regmap_lookup_by_name);
#endif

