/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __WCN_GNSS_DUMP_H__
#define __WCN_GNSS_DUMP_H__

void gnss_ring_reset(void);
unsigned long gnss_ring_free_space(void);

int gnss_dump_write(char *buf, int len);
int wcn_gnss_dump_init(void);
void wcn_gnss_dump_exit(void);

#endif
