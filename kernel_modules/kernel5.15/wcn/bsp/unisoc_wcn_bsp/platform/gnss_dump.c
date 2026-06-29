/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/file.h>
#include <linux/fs.h>
#include "wcn_integrate.h"
#include "gnss.h"
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/sipc.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include "marlin_platform.h"
#include "wcn_bus.h"
#include "wcn_integrate_platform.h"
#include <linux/uaccess.h>
#include "bufring.h"
#include "wcn_log.h"
#include "wcn_dump.h"
#include "gnss_common.h"
#include "gnss_dump.h"
#include "wcn_gnss_dump.h"
#include "sprd_wcn.h"
#include "wcn_debug_bus.h"
#include "wcn_chr.h"

#define DUMP_PACKET_SIZE	(1024)

#define GNSS_DUMP_END_STRING "gnss_memdump_finish"
#define GNSS_SET_OFFSET      0x1000
#define GNSS_APB_BASE              0x40bc8000
#define REG_GNSS_APB_MCU_AP_RST        (GNSS_APB_BASE + 0x0280) /* s/c */
#define BIT_GNSS_APB_MCU_AP_RST_SOFT (1<<0)/*BIT 0*/

#define GNSS_AHB_BASE			   0x40b18000
#define GNSS_ARCH_EB_REG		   (GNSS_AHB_BASE + 0x084)
#define GNSS_ARCH_EB_REG_BYPASS    (1<<1)

#define GNSSDUMP_INFO(format, arg...) pr_info("gnss_dump: " format, ## arg)
#define GNSSDUMP_ERR(format, arg...) pr_err("gnss_dump: " format, ## arg)
#define GNSSDUMP_DBG(format, arg...) pr_debug("gnss_dump: " format, ## arg)
#define GNSS_DUMP_ADDR_OFFSET 0x00000004

struct gnss_mem_dump {
	uint address;
	uint length;
};

struct regmap_dump {
	int regmap_type;
	uint reg;
};

struct cp_reg_dump {
	u32 addr;
	u32 len;
};

static u32 debugbus_qogirl6[DEBUGBUS_RESERVE] = {0};
static char gnss_dump_level; /* 0: default, all, 1: only data, pmu, aon */
static char gnss_pll_switch_flag = 1;/*0:switch fail, 1:switch suc*/
extern struct wcn_device_manage s_wcn_device;

void debugbus_set_value(u32 flag_debugbus, enum DEBUGBUS_QOGIRL6 flag, int val)
{
	if ((flag_debugbus & val) == val)
		debugbus_qogirl6[flag] = true;
	else
		debugbus_qogirl6[flag] = false;
}

u32 debugbus_get_value(enum DEBUGBUS_QOGIRL6 flag)
{
	GNSSDUMP_INFO("%s=%x\n", __func__, debugbus_qogirl6[flag]);
	return debugbus_qogirl6[flag];
}

static void gnss_write_data_to_phy_addr(phys_addr_t phy_addr,
					      void *src_data, u32 size)
{
	void *virt_addr;
	GNSSDUMP_ERR("gnss_write_data_to_phy_addr entry\n");
	virt_addr = shmem_ram_vmap_nocache(SIPC_ID_GNSS, phy_addr, size);
	if (virt_addr) {
		memcpy(virt_addr, src_data, size);
		shmem_ram_unmap(SIPC_ID_GNSS, virt_addr);
	} else
		GNSSDUMP_ERR("%s shmem_ram_vmap_nocache fail\n", __func__);
}

static void gnss_read_data_from_phy_addr(phys_addr_t phy_addr,
					  void *tar_data, u32 size)
{
	void *virt_addr;

	GNSSDUMP_ERR("gnss_read_data_from_phy_addr\n");
	virt_addr = shmem_ram_vmap_nocache(SIPC_ID_GNSS, phy_addr, size);
	if (virt_addr) {
		memcpy(tar_data, virt_addr, size);
		shmem_ram_unmap(SIPC_ID_GNSS, virt_addr);
	} else
		GNSSDUMP_ERR("%s shmem_ram_vmap_nocache fail\n", __func__);
}


u32 gnss_check_stab_dump_status(void)
{
	phys_addr_t base_addr;
	u32 value1 = 0, value2 = 0;

	if (debugbus_get_value(DEBUGBUS_GNSS_IP_CURRENT_STATE)) {
		GNSSDUMP_INFO("%s GNSS IP is shutdown!\n", __func__);
		return FALSE;
	}
	if ((!debugbus_get_value(DEBUGBUS_CGM_GNSS_MTX_GATE_EN)
			 && gnss_sys_is_deepsleep_status(s_wcn_device.gnss_device))) {
		GNSSDUMP_INFO("%s GNSS error in cgm_gnss_mtx_en!\n", __func__);
		return FALSE;
	}
	base_addr = GNSS_SYS_CGM_GNSS_FAKE_SEL + GNSS_AP_ACCESS_CP_OFFSET;
	gnss_read_data_from_phy_addr(base_addr, (void *)&value1, 4);
	base_addr = GNSS_SYS_BB_EN + GNSS_AP_ACCESS_CP_OFFSET;
	gnss_read_data_from_phy_addr(base_addr, (void *)&value2, 4);
	GNSSDUMP_INFO("%s cgm_gnss_fake_en=%x, gnss_sys_bb_en=%x!\n", __func__, value1, value2);
	if ((value1 & 0x2) && (!(value2 & 0x8))) {
		value2 |= 1 << 3;
		gnss_write_data_to_phy_addr(base_addr, (void *)&value2, 4);
		GNSSDUMP_INFO("%s after set val=%x\n", __func__, value2);
	} else if (!(value1 & 0x2) && !(value2 & 0x8))
		return FALSE;

	return TRUE;
}

static void get_gnss_pll_switch_state(void)
{
	struct wcn_device *wcn_dev;
	phys_addr_t phy_addr;
	struct wcn_dfs_sync_info dfs_info;

	wcn_dev = s_wcn_device.gnss_device;
	phy_addr = wcn_dev->base_addr - WCN_GNSS_DDR_OFFSET
				+ WCN_SYS_DFS_SYNC_ADDR_OFFSET;
	gnss_read_data_from_phy_addr(phy_addr, &dfs_info,
				sizeof(struct wcn_dfs_sync_info));
	GNSSDUMP_INFO("gnss_dfs_info: 0x%x-0x%x-0x%x\n",
			dfs_info.gnss_dfs_info, dfs_info.debugdfs0,
			dfs_info.debugdfs1);
	/*debugdfs1[27]:dfs_done*/
	if (dfs_info.debugdfs1 & (1 << 27)) {
		gnss_pll_switch_flag = 1;
		return;
	}
	/*gnss not switch pll*/
	GNSSDUMP_INFO("gnss not switch pll");
	gnss_pll_switch_flag = 0;
}

static void gnss_soft_reset_release_cpu(u32 type)
{
	struct regmap *regmap;
	phys_addr_t base_addr;
	u32 value = 0;
	u32 value_tmp = 0;
	u32 platform_type = wcn_platform_chip_type();
	if (platform_type == WCN_PLATFORM_TYPE_QOGIRL6) {
		base_addr = MCU_AP_RST_ADDR + GNSS_AP_ACCESS_CP_OFFSET;
		if (type == GNSS_CPU_RESET) {
			/* reset gnss cpu */
			gnss_read_data_from_phy_addr(base_addr,
				(void *)&value_tmp, 4);
			GNSSDUMP_INFO("before rst val=%x\n", value_tmp);
			/* BIT0:rst set */
			value_tmp |= 1;
			gnss_write_data_to_phy_addr(base_addr,
				(void *)&value_tmp, 4);
			GNSSDUMP_INFO("rst val=%x\n", value_tmp);
		} else if (type == GNSS_CPU_RESET_RELEASE) {
			/* release gnss cpu */
			gnss_read_data_from_phy_addr(base_addr,
				(void *)&value_tmp, 4);
			GNSSDUMP_INFO("before rls val=%x\n", value_tmp);
			/* BIT0:rst clear */
			value_tmp &= ~(1);
			gnss_write_data_to_phy_addr(base_addr,
				(void *)&value_tmp, 4);
			GNSSDUMP_INFO("rls val=%x\n", value_tmp);
		}
		return;
	}

	if (platform_type == WCN_PLATFORM_TYPE_SHARKL3) {
		GNSSDUMP_INFO("regmap SHARKL3");
		regmap = wcn_get_gnss_regmap(REGMAP_WCN_REG);
	} else {
		GNSSDUMP_INFO("regmap non SHARKL3");
		regmap = wcn_get_gnss_regmap(REGMAP_ANLG_WRAP_WCN);
	}
	if (type == GNSS_CPU_RESET) {
		/* reset gnss cm4 */
		wcn_regmap_read(regmap, 0X20, &value);
		value |= 1 << 2;
		wcn_regmap_raw_write_bit(regmap, 0X20, value);

		wcn_regmap_read(regmap, 0X24, &value);
		value |= 1 << 3;
		wcn_regmap_raw_write_bit(regmap, 0X24, value);
	} else if (type == GNSS_CPU_RESET_RELEASE) {
		value = 0;
		/* release gnss cm4 */
		wcn_regmap_raw_write_bit(regmap, 0X20, value);
		wcn_regmap_raw_write_bit(regmap, 0X24, value);
	}
}

void gnss_hold_cpu(void)
{
	u32 value;
	phys_addr_t base_addr;
	phys_addr_t ph_addr;
	int i = 0;
	GNSSDUMP_INFO("%s enter\n", __func__);
	/* reset cpu */
	gnss_soft_reset_release_cpu(GNSS_CPU_RESET);
	/* set cache flag */
	value = GNSS_CACHE_FLAG_VALUE;
	base_addr = wcn_get_gnss_base_addr();
	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6)
		ph_addr = base_addr + GNSS_CACHE_FLAG_ADDR_L6;
	else
		ph_addr = base_addr + GNSS_CACHE_FLAG_ADDR;
	GNSSDUMP_INFO("val=%x bs=%llx ph=%llx\n", value, base_addr, ph_addr);
	gnss_write_data_to_phy_addr(ph_addr, (void *)&value, 4);
	/* release cpu */
	gnss_soft_reset_release_cpu(GNSS_CPU_RESET_RELEASE);

	while (i < 3) {
		gnss_read_data_from_phy_addr(ph_addr, (void *)&value, 4);
		GNSSDUMP_INFO("%s gnss cache value %x\n", __func__, value);
		if (value == GNSS_CACHE_END_VALUE)
			break;
		i++;
		msleep(50);
	}
	if (value != GNSS_CACHE_END_VALUE)
		GNSSDUMP_ERR("%s gnss cache failed value %x\n", __func__,
			value);
	msleep(200);
}
EXPORT_SYMBOL_GPL(gnss_hold_cpu);

static int wcn_integrated_dump_data_regmap(u32 addr, u32 len, u32 skip)
{
	u32 chip_tp, i;
	struct regmap *map;
	void *buf = NULL;
	u8 *tmp;

	chip_tp = wcn_platform_chip_type();

	buf = vmalloc(len);
	if (!buf) {
		GNSSDUMP_ERR("%s vmalloc buf error\n", __func__);
		return -ENOMEM;
	}
	memset(buf, 0, len);

	if (unlikely(skip))
		goto next;

	if (chip_tp == WCN_PLATFORM_TYPE_SHARKL3)
		map = wcn_get_gnss_regmap(REGMAP_WCN_REG);
	else
		map = wcn_get_gnss_regmap(REGMAP_ANLG_WRAP_WCN);

	wcn_regmap_raw_write_bit(map, ANLG_WCN_WRITE_ADDR, addr);
	for (i = 0; i < len / 4; i++) {
		tmp = (u8 *) (buf + i * 4);
		wcn_regmap_read(map, ANLG_WCN_READ_ADDR, (u32 *)tmp);
	}

next:

	gnss_dump_data(buf, len, skip);

	vfree(buf);
	return 0;
}

static int wcn_integrated_dump_data_generic(u32 addr, u32 len, u32 skip)
{
	void *virt_addr;
	unsigned int cnt;

	virt_addr = wcn_mem_ram_vmap_nocache(addr, len, &cnt);
	if (!virt_addr) {
		GNSSDUMP_ERR("wcn_mem_ram_vmap_nocache fail\n");
		return -1;
	}

	/* copy data from ddr to ring buf  */
	gnss_dump_data(virt_addr, len, skip);

	wcn_mem_ram_unmap(virt_addr, cnt);
	GNSSDUMP_INFO("%s dump 0x%x finish!\n", __func__, addr);
	return 0;
}

static int gnss_integrated_dump_mem(void)
{
	int i, ret = 0;
	int skip = 0, gnss_dump_cnt = 0;
	uint gnss_stab_dump_flag = 0;
	struct wcn_dump_mem_reg *gnss_dump_reg;

	GNSSDUMP_INFO("gnss_dump_mem entry\n");

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6) {
		get_gnss_pll_switch_state();
		/*gnss dont dump unless pll switch done*/
		debug_bus_show("GNSS DUMP READ DEBUGBUS");
		if (gnss_pll_switch_flag == 0)
			return ret;
		gnss_stab_dump_flag = gnss_check_stab_dump_status();
		if (gnss_stab_dump_flag)
			gnss_hold_cpu();
		else
			GNSSDUMP_INFO("%s : gnss only dump DDR/SIPC data!!!\n", __func__);
	}
	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_SHARKL3)
		gnss_hold_cpu();

	if (sprdwcn_chr_get_minidump_state()) {
		gnss_dump_cnt = gnss_mem_cnt;
		gnss_dump_reg = gnss_minidump_reg;
	} else {
		gnss_dump_cnt = gnss_reg_cnt;
		gnss_dump_reg = gnss_reg;
	}

	for (i = 0; i < gnss_dump_cnt; i++) {
		if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6) {
			if (!gnss_stab_dump_flag && i == 2)
				skip = 1;
		}
		if (gnss_dump_reg[i].domain & CP)
			wcn_integrated_dump_data_regmap(gnss_dump_reg[i].addr
					+ gnss_dump_reg[i].offset, gnss_dump_reg[i].len, skip);
		else
			wcn_integrated_dump_data_generic(gnss_dump_reg[i].addr
					+ gnss_dump_reg[i].offset, gnss_dump_reg[i].len, skip);
	}

	GNSSDUMP_INFO("%s finish\n", __func__);
	return ret;
}

static int gnss_ext_hold_cpu(void)
{
	uint temp = 0;
	int ret = 0;

	GNSSDUMP_INFO("%s entry\n", __func__);
	temp = BIT_GNSS_APB_MCU_AP_RST_SOFT;
	ret = sprdwcn_bus_reg_write(REG_GNSS_APB_MCU_AP_RST + GNSS_SET_OFFSET,
		&temp, 4);
	if (ret < 0) {
		GNSSDUMP_ERR("%s write reset reg error:%d\n", __func__, ret);
		return ret;
	}
	temp = GNSS_ARCH_EB_REG_BYPASS;
	ret = sprdwcn_bus_reg_write(GNSS_ARCH_EB_REG + GNSS_SET_OFFSET,
				    &temp, 4);
	if (ret < 0)
		GNSSDUMP_ERR("%s write bypass reg error:%d\n", __func__, ret);

	return ret;
}

#define ALLOC_SIZE 4096

static int gnss_ext_dump_data(unsigned int start_addr, int len)
{
	u8 *buf = NULL;
	int i = 0, ret = 0, count = 0,
	    end_len = 0, write_len = 0;
	unsigned int base_addr;

	buf = kzalloc(ALLOC_SIZE, GFP_KERNEL);
	if (!buf) {
		GNSSDUMP_ERR("%s kzalloc buf error\n", __func__);
		return -ENOMEM;
	}

	count = DIV_ROUND_UP(len, ALLOC_SIZE);
	end_len = len % ALLOC_SIZE;
	for (i = 0; i < count; i++) {
		base_addr = start_addr + i * ALLOC_SIZE;
		if (end_len == 0) {
			write_len = ALLOC_SIZE;
		} else {
			if (i == count - 1)
				write_len = end_len;
			else
				write_len = ALLOC_SIZE;
		}
		ret = sprdwcn_bus_direct_read(base_addr, buf, write_len);
		print_hex_dump(KERN_INFO, "GNSS_DUMP_DATE:", DUMP_PREFIX_OFFSET, 32, 4,
			buf, write_len > 64 ? 64 : write_len, true);
		GNSSDUMP_DBG("%s, write_times:%d, addr:%x, len:%d, total_len:%d\n",
			__func__, i, base_addr, write_len, len);
		if (ret < 0) {
			GNSSDUMP_ERR("%s read error:%d\n", __func__, ret);
			goto dump_data_done;
		}

		ret = gnss_dump_data(buf, write_len, 0);
		if (ret != write_len) {
			GNSSDUMP_ERR("%s failed size is %d, ret %d\n", __func__,
				     write_len, ret);
			goto dump_data_done;
		}
	}
	GNSSDUMP_INFO("%s finish %d, count: %d\n", __func__, len, count);
	ret = 0;

dump_data_done:
	kfree(buf);
	return ret;
}

static int gnss_ext_dump_mem(void)
{
	int ret = 0;
	int i = 0, gnss_dump_cnt = 0;
	struct wcn_dump_mem_reg *gnss_dump_reg;

	GNSSDUMP_INFO("%s entry\n", __func__);
	gnss_ext_hold_cpu();
	gnss_ring_reset();

	if (sprdwcn_chr_get_minidump_state()) {
		gnss_dump_cnt = gnss_mem_cnt;
		gnss_dump_reg = gnss_minidump_reg;
	} else {
		gnss_dump_cnt = gnss_reg_cnt;
		gnss_dump_reg = gnss_reg;
	}

	for (i = 0; i < gnss_dump_cnt; i++)
		if (gnss_ext_dump_data(gnss_dump_reg[i].addr + gnss_dump_reg[i].offset,
			gnss_dump_reg[i].len)) {
			GNSSDUMP_ERR("%s dumpdata i %d error\n", __func__, i);
			break;
		}
	return ret;
}

int gnss_dump_mem(char flag)
{
	int ret = 0;
	struct wcn_match_data *g_match_config = get_wcn_match_config();

	if (g_match_config && g_match_config->unisoc_wcn_integrated) {
		if (wcn_get_gnss_power_status() == WCN_POWER_STATUS_OFF) {
			GNSSDUMP_INFO("gnss power status off:can not dump gnss\n");
			return -1;
		}
		GNSSDUMP_INFO("need dump gnss\n");
		gnss_dump_level = flag;
		ret = gnss_integrated_dump_mem();
	} else {
		ret = gnss_ext_dump_mem();
	}
	msleep(40);
	gnss_dump_str(GNSS_DUMP_END_STRING, strlen(GNSS_DUMP_END_STRING));
	return ret;
}
EXPORT_SYMBOL_GPL(gnss_dump_mem);

