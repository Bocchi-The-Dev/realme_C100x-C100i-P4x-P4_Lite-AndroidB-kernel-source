/* SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2020 Unisoc Communications Inc.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef __WCN_FW_DUMP_H__
#define __WCN_FW_DUMP_H__
void fw_ring_reset(void);
unsigned long fw_ring_free_space(void);
int fw_dump_write(void *buf, int len);
int wcn_fw_dump_init(void);
void wcn_fw_dump_exit(void);
#endif

