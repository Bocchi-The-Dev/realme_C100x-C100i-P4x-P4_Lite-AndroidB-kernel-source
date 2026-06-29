/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef __GNSS_DUMP_H__
#define __GNSS_DUMP_H__

#include "sprd_wcn_glb.h"
#include "wcn_dump.h"

#define GNSS_DUMP_IRAM_START_ADDR_SIPC	0x88240000
#define SIPC_BUFFER_DATA_NUM		0x40000

#define GNSS_DUMP_IRAM_START_ADDR_PCHANNEL 0x40e40000
#define GNSS_PCHANNEL_IRAM_DATA_NUM	32768

/* ap aon registers start */
#define DUMP_REG_PMU_SLEEP_CTRL		0x402B00CC
#define DUMP_REG_PMU_SLEEP_STATUS	0x402B00D4

#define DUMP_REG_SYS_EN_STATUS		0x402e057c   /* sys_en */
#define DUMP_REG_WCN_SYS_CFG		0x402e0578   /* wcn_sys_cfg */
#define DUMP_REG_GNSS_CLK_STATUS	0x402d02d4   /* clk */

#define DUMP_REG_WCN_PD_STATUS		0x402b0100   /* wcn_pd */
#define DUMP_REG_BT_WIFI_PD		0x402b0104   /* bt_wifi_pd */
#define DUMP_REG_GNSS_STATUS		0x402b0108   /* gnss */
/* ap aon registers end */

/* cp reg start */
/* APB */
#define DUMP_REG_GNSS_APB_CTRL_ADDR	0xA0060000
#define DUMP_REG_GNSS_APB_CTRL_LEN	0x400
/* AHB */
#define DUMP_REG_GNSS_AHB_CTRL_ADDR	0xC0300000
#define DUMP_REG_GNSS_AHB_CTRL_LEN	0x400
/* Com_sys */
#define DUMP_COM_SYS_CTRL_ADDR		0xD0020800
#define DUMP_COM_SYS_CTRL_LEN		0x10

/* wcn_cp_clk */
#define DUMP_WCN_CP_CLK_CORE_ADDR	0xD0020000
#define DUMP_WCN_CP_CLK_LEN		0x100
/* cp reg end */

/* qogirl6 cpu hold start */
#define GNSS_CPU_RESET			1
#define GNSS_CPU_RESET_RELEASE		0
#define GNSS_CACHE_FLAG_ADDR_L6		0x158000
#define MCU_AP_RST_ADDR			0x40bc8280
#define CMSTAR_BOOT_CTRL_ADDR		0x4088000c
#define GNSS_SYS_BB_EN			0x40b18004
#define GNSS_SYS_CGM_GNSS_FAKE_SEL	0x40bd8020
#define GNSS_AP_ACCESS_CP_OFFSET	0x11000000
/* qogirl6 cpu hold end*/

/* qogirl6 cp reg start */
#define CP_REG_NUM	4

#define REG_AON_WCN_GNSS_CLK	0x40bd8000
#define AON_WCN_GNSS_CLK_LEN	0x54
#define REG_AON_APB_PERI	0x40bc8000
#define AON_APB_PERI_LEN	0x2e4
#define REG_AON_AHB_SYS		0x40b18000
#define AON_AHB_SYS_LEN		0x42c
#define REG_AON_CONTROL		0x40c00000
#define AON_CONTROL_LEN		0x64
/* qogirl6 cp reg end */

#define ANLG_WCN_WRITE_ADDR 0XFF4
#define ANLG_WCN_READ_ADDR 0XFFC

#define GNSS_DRAM_ADDR      0x40a80000
#define GNSS_DRAM_SIZE      0x30000
#define GNSS_TE_MEM         0x40e40000
#define GNSS_TE_MEM_SIZE    0x30000
#define GNSS_BASE_AON_APB   0x4083c000
#define GNSS_BASE_AON_APB_SIZE 0x354
#define CTL_BASE_AON_CLOCK  0x40844200
#define CTL_BASE_AON_CLOCK_SIZE  0x144

enum DEBUGBUS_QOGIRL6 {
	DEBUGBUS_CGM_GNSS_MTX_GATE_EN,
	DEBUGBUS_GNSS_IP_CURRENT_STATE,
	DEBUGBUS_RESERVE,
};
#define CGM_GNSS_MTX_GATE_EN	0x2
#define GNSS_IP_CURRENT_STATE	(0x7 << 28)
extern int gnss_reg_cnt;
extern struct wcn_dump_mem_reg gnss_reg[MAX_DUMP_REG];
extern int gnss_mem_cnt;
extern struct wcn_dump_mem_reg gnss_minidump_reg[MAX_DUMP_REG];

int gnss_dump_mem(char flag);
void debugbus_set_value(u32 flag_debugbus, enum DEBUGBUS_QOGIRL6 flag, int val);
u32 debugbus_get_value(enum DEBUGBUS_QOGIRL6 flag);
#endif
