/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sprdwcn2.0/include/chr.h
 */
#ifndef __SPRD_WCN_CHR_H__
#define __SPRD_WCN_CHR_H__

#include <linux/ktime.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>

#include "wcn_bus.h"

#define BUF_SIZE	512
#define MIN(a, b)		(a > b ? b : a)

#define SPRDWCN_CHR_LABLE			"wcn_chr_v2"
#define SPRDWCN_CHR_MDMP_LABLE		"minidump"
#define SPRDWCN_CHR_KFIFO_SIZE		(4*1024)

#define AT_CMD_FLUSH				"AT+FLUSH"
#define AT_CMD_ENABLE				"AT+ENABLE"
#define AT_CMD_DISABLE				"AT+DISABLE"
#define AT_CMD_STATE				"AT+STATE"
#define AT_CMD_GET_LOGLEVEL			"AT+LOGLEVEL?"
#define AT_CMD_SET_LOGLEVEL			"AT+LOGLEVEL="
#define AT_CMD_MINIDUMP_ENABLE		"AT+MINIDUMP=1"
#define AT_CMD_MINIDUMP_DISABLE		"AT+MINIDUMP=0"
#define AT_CMD_MINIDUMP_STATE		"AT+MINIDUMP=?"

extern struct miscdevice wcn_misc_device;

enum sprdwcn_chr_event_type {
	SPRDWCN_CHR_EVENT,
	SPRDWCN_CHR_MINIDUMP,
};

struct sprdwcn_chr_event {
	uint8_t total_len;
	uint32_t sec:22;
	uint32_t msec:10;
	uint32_t rsrv:4;
	uint32_t id:20;
	uint8_t params[];
} __packed;

struct sprdwcn_chr_event_head {
	ktime_t ktime;
	uint32_t event_id;
	uint8_t *params;
	uint8_t len;
	struct list_head list;
};

struct sprdwcn_chr_desc {
	char *lable;
	atomic_t enable;
	atomic_t loglevel;
	atomic_t exit_flag;
	bool minidump;
	wait_queue_head_t wait_queue;
	struct completion wr_cmplte;
	struct completion rd_cmplte;
	struct mutex wr_mutex;
	struct mutex flush_mutex;

	struct task_struct *wr_thread;
	struct kfifo kfifo_chr;

	bool flush;
};

struct sprdwcn_chr_assert_event {
	uint8_t sys;
	uint8_t reason[256];
} __packed;

int sprdwcn_chr_init(void);
void sprdwcn_chr_deinit(struct sprdwcn_chr_desc *chr);
bool sprdwcn_chr_get_minidump_state(void);
int sprdwcn_chr_report_event(enum wcn_source_type type,
								enum wcn_chr_loglevel loglevel,
									uint32_t event_id,
									void *params, uint8_t len);

#endif	//__SPRD_WCN_CHR_H__
