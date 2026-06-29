/*
 *
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _WCN_DUMP_H
#define _WCN_DUMP_H

int mdbg_dump_mem(enum wcn_source_type type);
int mdbg_minidump_mem(enum wcn_source_type type);

struct wcn_dump_mem_reg {
	u32 addr;
	u32 len;
	u32 offset;
	u32 domain;
	char name[64];
};

struct wcn_dump_section_info {
	/* cp load start addr */
	__le32 start;
	/* cp load end addr */
	__le32 end;
	/* load from file offset */
	__le32 off;
	__le32 reserv;
} __packed;

struct wcn_dump_head_info {
	/* WCN_DUMP_VERSION_NAME */
	u8 version[16];
	/* WCN_DUMP_VERSION_SUB_NAME */
	u8 sub_version[16];
	/* numbers of wcn_dump_section_info */
	__le32 n_sec;
	/* used to check if dump is full */
	__le32 file_size;
	u8 reserv[8];
	struct wcn_dump_section_info section[];
} __packed;

#define MAX_DUMP_REG	0x30

#define	BSP			(1<<0)
#define	WIFI		(1<<1)
#define	BT			(1<<2)
#define	FM			(1<<3)
#define	GNSS		(1<<4)
#define	SDIO		(1<<5)
#define	PCIE		(1<<6)
#define	BT_BUF		(1<<7)
#define	BTWF		(1<<8)

#define	CP			(1<<30)
#define	AP			(1<<31)

extern int btwf_reg_cnt;
extern struct wcn_dump_mem_reg btwf_reg[MAX_DUMP_REG];
extern int btwf_mem_cnt;
extern struct wcn_dump_mem_reg btwf_minidump_reg[MAX_DUMP_REG];

//id=0 btwf,  id=1 gnss
int dump_arm_reg(u32 id);

void sprdwcn_bus_armreg_write(u32 id, u32 reg_index, u32 value);
int gnss_dump_data(void *start_addr, int len, u32 skip);
void gnss_dump_str(char *str, int str_len);

#endif
