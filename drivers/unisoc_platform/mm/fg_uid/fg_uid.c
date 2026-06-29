// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 UNSOC. All rights reserved.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <../fs/proc/internal.h>
#include "fg_uid.h"


#define MAX_ARRAY_LEN         256
#define FS_FG_INFO_PATH       "fg_info"
#define FS_FG_UIDS            "fg_uids"
#define FG_RW                 (S_IWUSR|S_IRUSR|S_IWGRP|S_IRGRP|S_IWOTH|S_IROTH)

static struct fg_info fginfo = {
	.fg_num = 0,
	.fg_uids = -1,
};

static struct proc_dir_entry *fg_dir __read_mostly;

bool is_fg(int uid)
{
	bool ret = false;

	if (uid == fginfo.fg_uids)
		ret = true;
	return ret;
}

EXPORT_SYMBOL_GPL(is_fg);

static int fg_uids_show(struct seq_file *m, void *v)
{
	seq_printf(m, "fg_uids: %d\n", fginfo.fg_uids);
	return 0;
}

static int fg_uids_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, fg_uids_show, inode);
}

static ssize_t fg_uids_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	char buffer[MAX_ARRAY_LEN];
	int err = 0;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user((void *)buffer, buf, count)) {
		err = -EFAULT;
		goto out;
	}
	fginfo.fg_uids = simple_strtol(buffer, NULL, 0);
	fginfo.fg_num = 1;
out:
	return err < 0 ? err : count;
}

static const struct proc_ops fg_uids_ops = {
	.proc_open = fg_uids_open,
	.proc_read = seq_read,
	.proc_write = fg_uids_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void uids_proc_fs_init(struct proc_dir_entry *p_parent)
{
	struct proc_dir_entry *p_temp;

	if (!p_parent)
		goto out_p_temp;
	p_temp = proc_create(FS_FG_UIDS, FG_RW, p_parent, &fg_uids_ops);
	if (!p_temp)
		goto out_p_temp;

out_p_temp:
	return;
}

static int __init fg_uids_init(void)
{
	struct proc_dir_entry *p_parent;

	p_parent = proc_mkdir(FS_FG_INFO_PATH, fg_dir);
	if (!p_parent)
		return -ENOMEM;
	uids_proc_fs_init(p_parent);
	return 0;
}

static void __exit fg_uids_exit(void)
{
	if (!fg_dir)
		return;
	remove_proc_entry(FS_FG_UIDS, fg_dir);
}

module_init(fg_uids_init);
module_exit(fg_uids_exit);
MODULE_LICENSE("GPL v2");

