 /*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef _WCN_PROCFS
#define _WCN_PROCFS

extern const struct proc_ops chr_fops;
extern const struct proc_ops chr_mdmp_fops;

#define MDBG_SNAP_SHOOT_SIZE		(32*1024)
#define MDBG_WRITE_SIZE			(64)
#define MDBG_ASSERT_SIZE		(1024)
#define MDBG_LOOPCHECK_SIZE		(128)
#define MDBG_AT_CMD_SIZE		(128)

void mdbg_fs_channel_init(void);
void mdbg_fs_channel_destroy(void);
int proc_fs_init(void);
int mdbg_memory_alloc(void);

void proc_fs_exit(void);
int get_loopcheck_status(void);
void wakeup_loopcheck_int(void);
void loopcheck_ready_clear(void);
void loopcheck_ready_set(void);
void mdbg_assert_interface(char *str);
int wcn_chr_write(char *buf, size_t len);
int wcn_chr_read(void);
void wcn_set_powerdown_flag(u8 flag);
void wcn_silent_reset(void);
int mdbg_assert_flag(void);
void mdbg_wcn_source_ctl(enum wcn_source_type type, char *str);
#endif
