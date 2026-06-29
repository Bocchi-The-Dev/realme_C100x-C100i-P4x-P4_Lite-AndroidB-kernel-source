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
#define pr_fmt(fmt) "CAMDEBUGGER_CORE: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define DEBUG_CONTROL_KEY   "*#781#*"
#define WORK_MODE_SLEN      2
#define LBUF_LEN_SLEN       8

uint32_t g_dbg_debug_control = 0;
static struct dentry *s_p_dentry;
static struct debug_cmd g_dbg_readinfo = {NULL, NULL, NULL, HW_CTX_MAX, IP_MAX};

static struct debug_cmd debug_cmd_tab[] = {
	{"bypass_dcam0",       NULL,                                 camdebugger_dcam_bypass_read,        HW_CTX_0,   IP_DCAM},
	{"bypass_dcam1",       NULL,                                 camdebugger_dcam_bypass_read,        HW_CTX_1,   IP_DCAM},
	{"reg_dcam0",          NULL,                                 camdebugger_dcam_reg_read,           HW_CTX_0,   IP_DCAM},
	{"reg_dcam1",          NULL,                                 camdebugger_dcam_reg_read,           HW_CTX_1,   IP_DCAM},
	{"reg_dcam2",          NULL,                                 camdebugger_dcam_reg_read,           HW_CTX_2,   IP_DCAM},
	{"reg_fetch",          NULL,                                 camdebugger_dcam_reg_read,           HW_CTX_3,   IP_DCAM},
	{"pre0_buf",           NULL,                                 camdebugger_reg_buf_read,            HW_CTX_0,   IP_ISP},
	{"cap0_buf",           NULL,                                 camdebugger_reg_buf_read,            HW_CTX_1,   IP_ISP},
	{"pre1_buf",           NULL,                                 camdebugger_reg_buf_read,            HW_CTX_2,   IP_ISP},
	{"cap1_buf",           NULL,                                 camdebugger_reg_buf_read,            HW_CTX_3,   IP_ISP},
	{"bypass_pre0",        NULL,                                 camdebugger_isp_bypass_read,         HW_CTX_0,   IP_ISP},
	{"bypass_cap0",        NULL,                                 camdebugger_isp_bypass_read,         HW_CTX_1,   IP_ISP},
	{"bypass_pre1",        NULL,                                 camdebugger_isp_bypass_read,         HW_CTX_2,   IP_ISP},
	{"bypass_cap1",        NULL,                                 camdebugger_isp_bypass_read,         HW_CTX_3,   IP_ISP},
	{"zoom_mode",          camdebugger_zoom_mode_write,          camdebugger_zoom_mode_read,          HW_CTX_MAX, IP_MAX},
	{"full_size",          camdebugger_preview_full_size_write,
		camdebugger_preview_full_size_read,  HW_CTX_MAX, IP_MAX},
	{"zoom_rds_limit",      camdebugger_rds_limit_write,
		camdebugger_rds_limit_read,  HW_CTX_MAX, IP_MAX},
	{"fbc_ctrl",           camdebugger_fbc_control_write,        camdebugger_fbc_control_read,        HW_CTX_MAX, IP_MAX},
	{"recovery_bypass",    camdebugger_recovery_bypass_write,    camdebugger_recovery_bypass_read,    HW_CTX_MAX, IP_MAX},
	{"replace_ctrl",       camdebugger_replace_control_write,    camdebugger_replace_control_read,    HW_CTX_MAX, IP_MAX},
	{"replace_file_fmt",   camdebugger_replace_file_fmt_write,   NULL,                                HW_CTX_MAX, IP_MAX},
	{"replace_switch",     camdebugger_replace_switch_write,     camdebugger_replace_switch_read,     HW_CTX_MAX, IP_MAX},
	{"dump_switch",        camdebugger_dump_switch_write,        camdebugger_dump_switch_read,        HW_CTX_MAX, IP_MAX},
	{"dump_ctrl",          camdebugger_dump_control_write,       camdebugger_dump_control_read,       HW_CTX_MAX, IP_MAX},
	{"dump_count",         camdebugger_dump_count_write,         NULL,                                HW_CTX_MAX, IP_MAX},
	{"pyr_dec_bypass",     camdebugger_pyr_dec_bypass_write,     camdebugger_pyr_dec_bypass_read,     HW_CTX_MAX, IP_MAX},
	{"dcam_cap_eof",       camdebugger_dcamint_eof_write,        camdebugger_dcamint_eof_read,        HW_CTX_MAX, IP_MAX},
	{"rawcap_frgb",        camdebugger_rawcap_frgb_write,        camdebugger_rawcap_frgb_read,        HW_CTX_MAX, IP_MAX},
	{"debug_log_switch",   camdebugger_pipeline_log_write,       camdebugger_pipeline_log_read,       HW_CTX_MAX, IP_MAX},
	{"iommu_mode",         camdebugger_iommu_mode_write,         camdebugger_iommu_mode_read,         HW_CTX_MAX, IP_MAX},
	{"line_buf_len",       camdebugger_lbuf_len_write,           camdebugger_lbuf_len_read,           HW_CTX_MAX, IP_MAX},
	{"offline_dec_bypass", camdebugger_isp_pyr_dec_bypass_write, camdebugger_isp_pyr_dec_bypass_read, HW_CTX_MAX, IP_MAX},
};

static int camdebugger_strtok(char *buf, struct debug_ops *debug_data)
{
	uint32_t j = 0;
	uint32_t k = 0;
	uint32_t i = 0;

	for (i = 0; i < sizeof(debug_data->name) - 1; i++) {
		if ('\0' == buf[i] || ' ' == buf[i] || ':' == buf[i] || '\n' == buf[i])
			break;
		debug_data->name[i] = buf[i];
	}
	debug_data->name[i] = '\0';
	i++;

	for (j = 0; j < sizeof(debug_data->val) - 1; i++, j++) {
		if ('\0' == buf[i] || ' ' == buf[i] || ':' == buf[i] || '\n' == buf[i])
			break;
		debug_data->val[j] = buf[i];
	}

	if(j == 0)
		return -1;
	debug_data->val[j] = '\0';
	i++;

	for (k = 0; k < sizeof(debug_data->tag) - 1; i++, k++) {
		if ('\0' == buf[i] || ' ' == buf[i] || ':' == buf[i] || '\n' == buf[i])
			break;
		debug_data->tag[k] = buf[i];
	}
	debug_data->tag[k] = '\0';

	return 0;
}

struct debug_cmd debug_fun_get(char *name, char *tag)
{
	uint32_t total_num = 0;
	uint32_t i = 0;
	struct debug_cmd debug_info = {NULL, NULL, NULL, HW_CTX_MAX, IP_MAX};

	total_num = sizeof(debug_cmd_tab) / sizeof(struct debug_cmd);
	for (i = 0; i < total_num; i++) {
		if (strcmp(name, debug_cmd_tab[i].name) == 0) {
			debug_info.write_fun = debug_cmd_tab[i].write_fun;
			debug_info.read_fun = debug_cmd_tab[i].read_fun;
			debug_info.hw_ctx = debug_cmd_tab[i].hw_ctx;
			debug_info.ip_info = debug_cmd_tab[i].ip_info;
			break;
		}
		else {
			if (tag && strcmp(tag, debug_cmd_tab[i].name) == 0) {
				debug_info.hw_ctx = debug_cmd_tab[i].hw_ctx;
				debug_info.ip_info = debug_cmd_tab[i].ip_info;
				debug_info.read_fun = debug_cmd_tab[i].read_fun;
			}
		}
	}

	pr_info("idx %d ,ip_info %d\n", debug_info.hw_ctx, debug_info.ip_info);
	return debug_info;
}

static ssize_t camdebugger_input_write(struct file *filp,
	const char __user *buffer, size_t count, loff_t *ppos)
{
	struct seq_file *p = (struct seq_file *)filp->private_data;
	struct cam_hw_info *hw = (struct cam_hw_info *)p->private;
	struct debug_cmd debug_info = {0};
	struct debug_ops debug_data = {0};
	char buf[256];
	uint32_t i = 0;
	int ret = 0;

	memset(buf, 0x00, sizeof(buf));
	i = count;
	if (i >= sizeof(buf))
		i = sizeof(buf) - 1; /* last one for \0 */

	CAM_DEBUGGER_IF_GET_CORRECT_KEY(g_dbg_debug_control);

	if (copy_from_user(buf, buffer, i)) {
		pr_err("fail to get user info\n");
		return -EFAULT;
	}
	buf[i] = '\0';

	ret = camdebugger_strtok(buf, &debug_data);

	if (ret < 0) {
		g_dbg_readinfo = debug_fun_get(&debug_data.name[0], NULL);
		return count;
	}
	debug_info = debug_fun_get(&debug_data.name[0], &debug_data.tag[0]);
	if (!debug_info.write_fun) {
		if (debug_info.ip_info == IP_DCAM)
			debug_info.write_fun = camdebugger_dcam_bypass_write;
		else if (debug_info.ip_info == IP_ISP)
			debug_info.write_fun = camdebugger_isp_bypass_write;
		else {
			pr_info("please input right cmd\n");
			return count;
		}
	}
	debug_info.write_fun(hw, &debug_data.name[0], &debug_data.val[0], debug_info.hw_ctx);
	g_dbg_readinfo = debug_info;

	return count;
}

static int camdebugger_input_read(struct seq_file *s, void *unused)
{
	CAM_DEBUGGER_IF_GET_CORRECT_KEY(g_dbg_debug_control);

	if (g_dbg_readinfo.read_fun)
		g_dbg_readinfo.read_fun(s, g_dbg_readinfo.hw_ctx);
	else {
		seq_printf(s, "\n bypass|work sub_block \n");
		seq_printf(s, " echo sub_block:1(bypass)|0(work):target >debug_node \n");
		seq_printf(s, " target - DCAM:(bypass_dcam0|bypass_dcam1) ISP:(bypass_pre0|bypass_cap0|bypass_pre1|bypass_cap1) \n");
		seq_printf(s, " Example: bypass dcam0 blc\n");
		seq_printf(s, " echo blc:1:bypass_dcam0 >debug_node\n");
		seq_printf(s, "\n check sub_block bypass status\n");
		seq_printf(s, " 1.echo target >debug_node \n");
		seq_printf(s, " 2.cat debug_node \n");
		seq_printf(s, "\n check dcam register value\n");
		seq_printf(s, " 1.echo reg_dcam0|reg_dcam1|reg_dcam2|reg_fetch >debug_node \n");
		seq_printf(s, " 2.cat debug_node \n");
		seq_printf(s, "\n check isp cfg_buf value\n");
		seq_printf(s, " 1.echo pre0_buf|cap0_buf|pre1_buf|cap1_buf >debug_node \n");
		seq_printf(s, " 2.cat debug_node \n");
		seq_printf(s, "\n other debug_cmd \n");
		seq_printf(s, " zoom_mode|fbc_ctrl|recovery_bypass|replace_ctrl|replace_file_fmt|replace_switch \n");
		seq_printf(s, " dump_switch|dump_ctrl|dump_count|pyr_dec_bypass|dcam_cap_eof|mem_leak_ctrl \n");
		seq_printf(s, " rawcap_frgb|debug_log_switch|iommu_mode|line_buf_len|offline_dec_bypass \n");
		seq_printf(s, "\n Example: use zoom_mode\n");
		seq_printf(s, " 1.write echo zoom_mode:1 >debug_node \n");
		seq_printf(s, " 2.read  cat debug_node \n");
		seq_printf(s, "\n for specific operation methods of each debug function, please\n");
		seq_printf(s, " 1.echo debug_cmd >debug_node \n");
		seq_printf(s, " 2.cat debug_node \n");
	}

	return 0;
}

static int camdebugger_input_open(struct inode *inode, struct file *file)
{
	return single_open(file, camdebugger_input_read, inode->i_private);
}

static const struct file_operations debug_node_ops = {
	.owner = THIS_MODULE,
	.open = camdebugger_input_open,
	.read = seq_read,
	.write = camdebugger_input_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t camdebugger_debug_control_write(struct file *filp,
	const char __user *buffer, size_t count, loff_t *ppos)
{
	int ret = 0;
	char msg[8] = {0};

	if (count > 8 || count < 1)
		return -EINVAL;
	ret = copy_from_user(msg, (void __user *)buffer, count);
	if (ret) {
		pr_err("fail to copy_from_user\n");
		return -EFAULT;
	}

	msg[count - 1] = '\0';
	if (strcmp(msg, DEBUG_CONTROL_KEY) == 0) {
		g_dbg_debug_control = 1;
	} else {
		pr_err("fail to get correct key\n");
		return -EFAULT;
	}

	pr_info("set debug_control %u\n", g_dbg_debug_control);
	return count;
}

static const struct file_operations debug_control_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = camdebugger_debug_control_write,
};

static int camdebugger_cam_init(struct cam_hw_info *hw)
{
	/* folder in /sys/kernel/debug/ */
	const char tb_folder[] = {"sprd_cam"};
	struct dentry *pd = NULL;
	int ret = 0;

	if (!hw) {
		pr_err("fail to get valid para\n");
		return -EFAULT;
	}

	s_p_dentry = debugfs_create_dir(tb_folder, NULL);
	pd = s_p_dentry;
	if (pd == NULL)
		return -ENOMEM;
	/* sub block bypass */
	if (!debugfs_create_file("debug_ctrl", 0664, pd, NULL, &debug_control_ops))
		ret |= BIT(1);
	if (!debugfs_create_file("debug_node", 0664, pd, hw, &debug_node_ops))
		ret |= BIT(2);
	if (ret)
		ret = -ENOMEM;
	pr_info("cam debugfs init ok\n");

	return ret;
}

static int camdebugger_cam_deinit(void)
{
	if (s_p_dentry)
		debugfs_remove_recursive(s_p_dentry);
	s_p_dentry = NULL;
	return 0;
}

int cam_debugger_init(struct cam_hw_info *hw)
{
	int ret = 0;

	ret = camdebugger_cam_init(hw);
	if (ret)
		pr_err("fail to init cam debugfs\n");

	return ret;
}

int cam_debugger_deinit(void)
{
	camdebugger_cam_deinit();

	return 0;
}

/*
This function implements uvent information reporting.Do not add frequent information,
as it can increase the load and power consumption of uvent.uvent information requires
adherence to a fixed format and cannot be modified arbitrarily.
*/
void cam_debugger_uevent_notify(struct platform_device *pdev, char *str)
{
	char event[256] = { 0 };
	char *envp[2] = {event, NULL };

	if (str) {
		snprintf(event, ARRAY_SIZE(event),
			"kevent_begin:{\"event_id\":\"107000003\",\"event_time\":%lld,%s}:kevent_end",
			ktime_get(), str);
		kobject_uevent_env(&pdev->dev.kobj, KOBJ_CHANGE, envp);
	} else
		pr_err("fail to get invalid str\n");
}

void cam_debugger_logfile_init(struct cam_debug_log_ctx *log_ctx, const char *filename)
{
	char *tmp = NULL;
	uint32_t size = 0;

	if ((log_ctx->ip_info == IP_DCAM && g_dcam_block_dump == 0)
		|| (log_ctx->ip_info == IP_ISP && g_isp_block_dump == 0)) {
		pr_debug("no support file init \n");
		return;
	}

	log_ctx->log_file = cam_kernel_adapt_filp_open(filename, O_CREAT | O_RDWR | O_APPEND, 0666);
	if (IS_ERR_OR_NULL(log_ctx->log_file)) {
		log_ctx->log_file = NULL;
		return;
	}

	pr_info("open file %s\n", filename);
	size = CAM_DEBUG_LOG_BLOCK_LOG_BUF_SIZE;
	if (in_interrupt())
		tmp = cam_buf_kernel_sys_kzalloc(size, GFP_ATOMIC);
	else
		tmp = cam_buf_kernel_sys_vzalloc(size);

	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("fail to alloc log buf\n");
		return;
	}

	log_ctx->log_buf = tmp;
	log_ctx->write_pos = 0;
	log_ctx->read_pos = 0;
	log_ctx->size = size;
	log_ctx->empty_size = size;
}

void cam_debug_logfile_deinit(struct cam_debug_log_ctx *log_ctx)
{
	if (!log_ctx || !log_ctx->log_file || !log_ctx->log_buf)
		return;

	if ((log_ctx->ip_info == IP_DCAM && g_dcam_block_dump == 0)
		|| (log_ctx->ip_info == IP_ISP && g_isp_block_dump == 0)) {
		pr_debug("no support file deinit \n");
		return;
	}

	cam_kernel_adapt_filp_close(log_ctx->log_file, NULL);

	log_ctx->log_file = NULL;

	if (is_vmalloc_addr(log_ctx->log_buf))
		cam_buf_kernel_sys_vfree(log_ctx->log_buf);
	else
		cam_buf_kernel_sys_kfree(log_ctx->log_buf);
	log_ctx->log_buf = NULL;

	memset(log_ctx, 0, sizeof(struct cam_debug_log_ctx));
}

