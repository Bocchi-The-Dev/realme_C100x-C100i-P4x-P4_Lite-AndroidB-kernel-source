/**
 * SPDX-FileContributor: vdsp driver debug function.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "vdsp_debugfs.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-vdsp: debugfs %d: %d %s:" \
	fmt, current->pid, __LINE__, __func__

struct xrp_debug_para {
	unsigned int vdsp_log_mode;
	unsigned int vdsp_log_level;
	unsigned int vdsp_dvfs_level;
	unsigned int vdsp_trace_mem;
	unsigned int vdsp_trace_iommu;
	unsigned int vdsp_trace_firmware;
	unsigned int vdsp_trace_hw;
	unsigned int vdsp_cmd_timeout;
};

static struct dentry *root_d;
static struct xrp_debug_para xrp_para;

int vdsp_debugfs_init(void)
{
	pr_debug("vdsp_debugfs: initialing...\n");
	memset(&xrp_para, 0, sizeof(struct xrp_debug_para));

	root_d = debugfs_create_dir("sprd_vdsp", NULL);
	if (!root_d) {
		pr_err("vdsp_debugfs: error create root dir\n");
		return -ENOMEM;
	}
	debugfs_create_u32("log_mode", 0664, root_d, &xrp_para.vdsp_log_mode);
	debugfs_create_u32("log_level", 0664, root_d, &xrp_para.vdsp_log_level);
	debugfs_create_u32("dvfs_level", 0664, root_d, &xrp_para.vdsp_dvfs_level);
	debugfs_create_u32("trace_mem", 0664, root_d, &xrp_para.vdsp_trace_mem);
	debugfs_create_u32("trace_iommu", 0664, root_d, &xrp_para.vdsp_trace_iommu);
	debugfs_create_u32("trace_hw", 0664, root_d, &xrp_para.vdsp_trace_hw);
	debugfs_create_u32("trace_firmware", 0664, root_d, &xrp_para.vdsp_trace_firmware);
	debugfs_create_u32("cmd_timeout", 0664, root_d, &xrp_para.vdsp_cmd_timeout);

	return 0;
}

unsigned int vdsp_debugfs_log_mode(void)
{
	return xrp_para.vdsp_log_mode;
}

unsigned int vdsp_debugfs_log_level(void)
{
	return xrp_para.vdsp_log_level;
}

unsigned int vdsp_debugfs_dvfs_level(void)
{
	return xrp_para.vdsp_dvfs_level;
}

unsigned int vdsp_debugfs_trace_mem(void)
{
	return xrp_para.vdsp_trace_mem;
}

unsigned int vdsp_debugfs_trace_iommu(void)
{
	return xrp_para.vdsp_trace_iommu;
}

unsigned int vdsp_debugfs_trace_hw(void)
{
	return xrp_para.vdsp_trace_hw;
}

unsigned int vdsp_debugfs_trace_firmware(void)
{
	return xrp_para.vdsp_trace_firmware;
}

unsigned int vdsp_debugfs_cmd_timeout(void)
{
	return xrp_para.vdsp_cmd_timeout;
}

void vdsp_debugfs_exit(void)
{
	pr_debug("vdsp_debugfs: exiting...\n");
	if (root_d)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
		debugfs_remove(root_d);
#else
		debugfs_remove_recursive(root_d);
#endif
}

