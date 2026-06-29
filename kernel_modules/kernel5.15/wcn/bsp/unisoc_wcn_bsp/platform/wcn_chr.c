// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Communications Inc.
 *
 * Filename: chr.c
 * Abstract: This file is a implementation for wcn_chr.
 * Author: Wenjing He <wenjing.he@unisoc.com>
 */
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <uapi/linux/in.h>
#include <linux/net.h>
#include <linux/kernel.h>
#include <linux/inet.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <uapi/asm-generic/errno.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/completion.h>
#include <linux/proc_fs.h>

#include "sprd_wcn.h"
#include "wcn_chr.h"
#include "wcn_dbg.h"
#include "sysfs.h"

static struct sprdwcn_chr_desc *g_chr = {0};
static LIST_HEAD(sprdwcn_chr_event_list);

static void sprdwcn_chr_set_data(struct sprdwcn_chr_desc *chr)
{
	g_chr = chr;
}

static struct sprdwcn_chr_desc *sprdwcn_chr_get_data(void)
{
	return g_chr;
}

static void sprdwcn_get_chr_state(void)
{
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();

	WCN_INFO("chr %s\n", atomic_read(&chr->enable) ? "enabled" : "disabled");
}

bool sprdwcn_chr_get_minidump_state(void)
{
	int reset_prop = wcn_sysfs_get_reset_prop();
	struct wcn_match_data *g_match_config = get_wcn_match_config();
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();

	/* minidump donot support unisoc_wcn_integrated, unless open this and test it fully.*/
	if (!g_match_config)
		return false;
	if (wcn_source_ctl_get() && !g_match_config->unisoc_wcn_integrated)
		return true;
	if (reset_prop != WCN_ASSERT_ONLY_RESET || g_match_config->unisoc_wcn_integrated)
		return false;

	return chr->minidump;
}

static void
sprdwcn_chr_event_add(struct sprdwcn_chr_event_head *event_head)
{
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();

	mutex_lock(&chr->wr_mutex);
	list_add_tail(&event_head->list, &sprdwcn_chr_event_list);
	complete(&chr->wr_cmplte);
	mutex_unlock(&chr->wr_mutex);
}

static int sprdwcn_chr_flush(void)
{
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();
	unsigned long timeleft;

	WCN_INFO("flush chr event\n");

	mutex_lock(&chr->flush_mutex);

	if (kfifo_len(&chr->kfifo_chr) == 0) {
		mutex_unlock(&chr->flush_mutex);
		return 0;
	}

	chr->flush = true;
	wake_up(&chr->wait_queue);
	timeleft = wait_for_completion_timeout(&chr->rd_cmplte,
									msecs_to_jiffies(3000));

	if (!timeleft) {
		WCN_WARN("wait wcn_chr server read timeout\n");
		mutex_unlock(&chr->flush_mutex);
		return -ETIMEDOUT;
	}

	mutex_unlock(&chr->flush_mutex);
	return 0;
}

static void sprdwcn_chr_uniview_notify(uint32_t event_id, enum wcn_source_type type)
{
	char buff[256] = {0};
	char *envp[2] = {NULL};
	uint32_t uniview_id;
	uint32_t event_options;
	WCN_INFO("report uniview\n");

	//assert
	if (event_id == 0x9E000) {
		uniview_id = 109000003; //pack android.log/kernel.log/mdmp/chr.log
		event_options = 0x1 | (0x1 << 15) | (0x1 << 16);
		snprintf(buff, sizeof(buff),
		"kevent_begin:{\"event_id\":\"%d\",\"event_options\":%d,\"wcn_assert\":%d}:kevent_end",
			uniview_id, event_options, type);
	} else {
		event_options = 0x1 | (0x1 << 16);
		uniview_id = 109000004; //pack android.log/kernel.log/chr.log
		snprintf(buff, sizeof(buff),
		"kevent_begin:{\"event_id\":\"%d\",\"event_options\":%d,\"chr_event\":%d}:kevent_end",
			uniview_id, event_options, type);
	}

	envp[0] = buff;
	kobject_uevent_env(&wcn_misc_device.this_device->kobj, KOBJ_CHANGE, envp);
}

int sprdwcn_chr_report_event(enum wcn_source_type type,
								enum wcn_chr_loglevel loglevel,
									uint32_t event_id,
									void *params, uint8_t len)
{
	struct sprdwcn_chr_event_head *event_head;
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();
	uint8_t i = 0, *tmp;

	if (atomic_read(&chr->enable) == 0 || atomic_read(&chr->exit_flag))
		goto out;

	if (loglevel > atomic_read(&chr->loglevel))
		goto out;

	if (type > WCN_SOURCE_WCN)
		goto out;

	if (loglevel == WCN_CHR_UNIVIEW)
		sprdwcn_chr_uniview_notify(event_id, type);

	event_head = kzalloc(sizeof(*event_head), GFP_KERNEL);
	if (!event_head)
		goto out;

	event_head->ktime = ktime_get();
	event_head->event_id = event_id & 0xFFFFF;

	if (len == 0) {
		sprdwcn_chr_event_add(event_head);
		return 0;
	}

	event_head->len = len;
	event_head->params = kzalloc(len, GFP_KERNEL);
	if (!event_head->params) {
		kfree(event_head);
		event_head = NULL;
		goto out;
	}

	tmp = event_head->params;
	memcpy(event_head->params, params, len);

	while (len--)
		tmp[i++] ^= 0x5A;

	sprdwcn_chr_event_add(event_head);

	return 0;

out:
	WCN_INFO("ignore chr event 0x%x\n", event_id);
	return -EINVAL;
}

static void sprdwcn_chr_get_loglevel(void)
{
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();

	WCN_INFO("loglevel is %d\n", atomic_read(&chr->loglevel));
}

static int sprdwcn_chr_set_loglevel(char *cmd)
{
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();
	int ret;
	char *tmp;
	unsigned long loglevel;

	tmp = cmd += strlen(AT_CMD_SET_LOGLEVEL);

	ret = kstrtoul(tmp, 10, &loglevel);
	if (ret < 0) {
		WCN_ERR("set loglevel fail with %d,%s\n", ret, cmd);
		return ret;
	}

	if (loglevel > WCN_CHR_MAX)
		loglevel = WCN_CHR_MAX;

	WCN_INFO("set loglevel to %lu\n", loglevel);
	atomic_set(&chr->loglevel, loglevel);

	return 0;
}

static int sprdwcn_chr_parse_cmd(char *cmd)
{
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();
	int ret = 0;

	if (!strncmp(cmd, AT_CMD_FLUSH, min(strlen(cmd), strlen(AT_CMD_FLUSH))))
		ret = sprdwcn_chr_flush();
		else if (!strncmp(cmd, AT_CMD_ENABLE, min(strlen(cmd), strlen(AT_CMD_ENABLE))))
			atomic_set(&chr->enable, 1);
		else if (!strncmp(cmd, AT_CMD_DISABLE, min(strlen(cmd), strlen(AT_CMD_DISABLE))))
			atomic_set(&chr->enable, 0);
		else if (!strncmp(cmd, AT_CMD_STATE, min(strlen(cmd), strlen(AT_CMD_STATE))))
			sprdwcn_get_chr_state();
		else if (!strncmp(cmd, AT_CMD_GET_LOGLEVEL, min(strlen(cmd),
				strlen(AT_CMD_GET_LOGLEVEL))))
			sprdwcn_chr_get_loglevel();
		else if (!strncmp(cmd, AT_CMD_SET_LOGLEVEL, min(strlen(cmd),
				strlen(AT_CMD_SET_LOGLEVEL))))
			ret = sprdwcn_chr_set_loglevel(cmd);
		else if (!strncmp(cmd, AT_CMD_MINIDUMP_ENABLE, min(strlen(cmd),
				strlen(AT_CMD_MINIDUMP_ENABLE))))
			chr->minidump = true;
		else if (!strncmp(cmd, AT_CMD_MINIDUMP_DISABLE, min(strlen(cmd),
				strlen(AT_CMD_MINIDUMP_DISABLE))))
			chr->minidump = false;
		else if (!strncmp(cmd, AT_CMD_MINIDUMP_STATE, min(strlen(cmd),
				strlen(AT_CMD_MINIDUMP_STATE))))
			WCN_INFO("minidump %s\n", chr->minidump ? "enabled" : "disabled");
		else
			WCN_WARN("invalid cmd:%s\n", cmd);

	return ret;
}

/*
 * PROCFS interfaces:
 * /proc/mdbg/wcn_chr [rw] [userspace]
 */
static ssize_t sprdwcn_chr_proc_read(struct file *filp,
		char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret;
	unsigned int len, copied;
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();

	len = kfifo_len(&chr->kfifo_chr);
	ret = kfifo_to_user(&chr->kfifo_chr, user_buf, len, &copied);
	if (ret < 0 || copied != len)
		WCN_WARN("wcn_chr server read fail, ret=%d, copied=%u, expect=%u\n",
				ret, copied, len);

	complete(&chr->rd_cmplte);

	return len;
}

/*
 * PROCFS interfaces:
 * /proc/mdbg/wcn_chr [rw] [userspace]
 */
static ssize_t sprdwcn_chr_proc_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	char cmd[128] = {0};

	if (*ppos)
		return 0;

	if (copy_from_user(cmd, buf, sizeof(cmd)))
		return -EINVAL;

	cmd[127] = 0;
	sprdwcn_chr_parse_cmd(cmd);

	*ppos += count;
	return count;
}

static __poll_t sprdwcn_chr_proc_poll(struct file *file, poll_table *wait)
{
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();

	poll_wait(file, &chr->wait_queue, wait);

	if (chr->flush) {
		chr->flush = false;
		return EPOLLIN | EPOLLRDNORM;
	}

	return 0;
}

const struct proc_ops chr_fops = {
	.proc_read = sprdwcn_chr_proc_read,
	.proc_write = sprdwcn_chr_proc_write,
	.proc_poll = sprdwcn_chr_proc_poll,
};

/*
 * PROCFS interfaces:
 * /proc/mdbg/minidump [ro] [userspace]
 */
static ssize_t sprdwcn_chr_proc_mdmp_read(struct file *filp,
				char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret;
	char tmp[16] = {0};
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();

	if (*ppos)
		return 0;

	ret = snprintf(tmp, sizeof(tmp), "%d", chr->minidump);
	if (copy_to_user(user_buf, tmp, strlen(tmp)))
		return -EINVAL;

	*ppos += ret;
	return *ppos;
}

const struct proc_ops chr_mdmp_fops = {
	.proc_read = sprdwcn_chr_proc_mdmp_read,
};

static struct sprdwcn_chr_event_head *sprdwcn_chr_get_event(void)
{
	struct sprdwcn_chr_event_head *event_head = NULL;
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();

	mutex_lock(&chr->wr_mutex);
	if (list_empty(&sprdwcn_chr_event_list)) {
		mutex_unlock(&chr->wr_mutex);
		return NULL;
	}

	event_head = list_first_entry(&sprdwcn_chr_event_list,
					struct sprdwcn_chr_event_head, list);
	list_del(&event_head->list);
	mutex_unlock(&chr->wr_mutex);

	return event_head;
}

static void sprdwcn_chr_event_free(void)
{
	struct sprdwcn_chr_desc *chr = sprdwcn_chr_get_data();
	struct sprdwcn_chr_event_head *event_head = NULL;

	mutex_lock(&chr->wr_mutex);

	while (!list_empty(&sprdwcn_chr_event_list)) {
		event_head = list_first_entry(&sprdwcn_chr_event_list,
						struct sprdwcn_chr_event_head, list);
		list_del(&event_head->list);
		if (event_head->len) {
			kfree(event_head->params);
			event_head->params = NULL;
		}
		kfree(event_head);
		event_head = NULL;
	}

	mutex_unlock(&chr->wr_mutex);
}

static int sprdwcn_chr_thread(void *data)
{
	int ret;
	struct sprdwcn_chr_event_head *event_head;
	struct sprdwcn_chr_event chr_event;
	struct sprdwcn_chr_desc *chr = (struct sprdwcn_chr_desc *)data;
	unsigned long rem_nsec;
	uint64_t ktime;

	while (!kthread_should_stop()) {

		wait_for_completion(&chr->wr_cmplte);

		if (kfifo_len(&chr->kfifo_chr) >= SPRDWCN_CHR_KFIFO_SIZE / 4) {
			ret = sprdwcn_chr_flush();
			if (ret < 0)
				goto free;
		}

		event_head = sprdwcn_chr_get_event();
		if (!event_head)
			continue;

		ktime = (uint64_t)event_head->ktime;
		rem_nsec = do_div(ktime, 1000000000);

		chr_event.total_len = event_head->len + sizeof(chr_event);
		chr_event.sec = (uint32_t)ktime;
		chr_event.msec = (uint32_t)rem_nsec/1000000;
		chr_event.id = event_head->event_id;

		ret = kfifo_in(&chr->kfifo_chr, (char *)&chr_event, sizeof(chr_event));
		if (unlikely(ret != sizeof(chr_event))) {
			WCN_WARN("write 0x%x failed\n", event_head->event_id);
			goto flush;
		}

		if (event_head->len) {
			ret = kfifo_in(&chr->kfifo_chr, event_head->params, event_head->len);
			kfree(event_head->params);
			event_head->params = NULL;
			if (unlikely(ret != event_head->len)) {
				WCN_WARN("write 0x%x failed\n", event_head->event_id);
				goto flush;
			}
		}

		kfree(event_head);
		event_head = NULL;
	}

flush:
	atomic_set(&chr->exit_flag, 1);
	sprdwcn_chr_flush();
free:
	sprdwcn_chr_event_free();

	return 0;
}

static int sprdwcn_chr_data_init(struct sprdwcn_chr_desc *chr)
{
	int ret = 0;

	chr->lable = SPRDWCN_CHR_LABLE;

	init_waitqueue_head(&chr->wait_queue);
	init_completion(&chr->wr_cmplte);
	init_completion(&chr->rd_cmplte);
	atomic_set(&chr->enable, 1);
	atomic_set(&chr->loglevel, WCN_CHR_MAX);
	mutex_init(&chr->wr_mutex);
	mutex_init(&chr->flush_mutex);
	sprdwcn_chr_set_data(chr);

	ret = kfifo_alloc(&chr->kfifo_chr, SPRDWCN_CHR_KFIFO_SIZE, GFP_KERNEL);
	if (ret < 0)
		return -ENOMEM;

	chr->wr_thread = kthread_run(sprdwcn_chr_thread, chr,
								"sprdwcn_chr_thread");

	return 0;
}

static void sprdwcn_chr_data_deinit(struct sprdwcn_chr_desc *chr)
{
	atomic_set(&chr->enable, 0);
	atomic_set(&chr->loglevel, 0);
	complete_all(&chr->wr_cmplte);
	kthread_stop(chr->wr_thread);
	kfifo_free(&chr->kfifo_chr);

	sprdwcn_chr_set_data(NULL);
}

int sprdwcn_chr_init(void)
{
	int ret = 0;
	struct sprdwcn_chr_desc *chr = NULL;

	chr = kvzalloc(sizeof(*chr), GFP_KERNEL);
	if (!chr)
		return -ENOMEM;

	ret = sprdwcn_chr_data_init(chr);
	if (ret)
		goto chr_free;

	return 0;

chr_free:
	kvfree(chr);
	chr = NULL;
	return ret;
}

void sprdwcn_chr_deinit(struct sprdwcn_chr_desc *chr)
{
	sprdwcn_chr_data_deinit(chr);
	kvfree(chr);
}

