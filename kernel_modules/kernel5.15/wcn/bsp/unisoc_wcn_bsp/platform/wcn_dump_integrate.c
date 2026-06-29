/*
 *
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include "wcn_integrate_platform.h"

#include "bufring.h"
#include "wcn_glb.h"
#include "wcn_glb_reg.h"
#include "wcn_log.h"
#include "wcn_misc.h"
#include "mdbg_type.h"
#include "loopcheck.h"
#include "../include/wcn_dbg.h"
#include "gnss_dump.h"
#include "wcn_fw_dump.h"
#include "wcn_debug_bus.h"
#include "wcn_chr.h"

/* SUB_NAME len not more than 15 bytes */
#define UMW2631_WCN_DUMP_VERSION_SUB_NAME "SIPC_26xx"
#define WCN_DUMP_VERSION_SUB_NAME "SIPC_23xx"

#define WCN_DUMP_END_STRING "marlin_memdump_finish"
#define WCN_CP2_STATUS_DUMP_REG	0x6a6b6c6d

#define DUMP_PACKET_SIZE	(1024)

/* units is ms, 2500ms */
#define WCN_DUMP_TIMEOUT 2500

/* use for umw2631_integrate only */
#define WCN_AON_ADDR_OFFSET 0x11000000
#define WCN_DUMP_ADDR_OFFSET 0x00000004

/* magic number, not change it */
#define WCN_DUMP_VERSION_NAME "WCN_DUMP_HEAD__"
#define WCN_DUMP_ALIGN(x) (((x) + 3) & ~3)
extern int wcn_sipc_mdbg_debug_show(void);

static struct mdbg_ring_t	*mdev_ring;
gnss_dump_callback gnss_dump_handle;

static int wcn_fill_dump_head_info(struct wcn_dump_mem_reg *mem_cfg, size_t cnt)
{
	unsigned int i, len, head_len;
	struct wcn_dump_mem_reg *mem;
	struct wcn_dump_head_info *head;
	struct wcn_dump_section_info *sec;

	head_len = sizeof(*head) + sizeof(*sec) * cnt;
	head = kzalloc(head_len, GFP_KERNEL);
	if (unlikely(!head)) {
		WCN_ERR("system has no mem for dump mem\n");
		return -1;
	}

	strncpy(head->version, WCN_DUMP_VERSION_NAME,
		strlen(WCN_DUMP_VERSION_NAME) + 1);
	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6) {
		strncpy(head->sub_version, UMW2631_WCN_DUMP_VERSION_SUB_NAME,
			strlen(UMW2631_WCN_DUMP_VERSION_SUB_NAME) + 1);
	} else {
		strncpy(head->sub_version, WCN_DUMP_VERSION_SUB_NAME,
			strlen(WCN_DUMP_VERSION_SUB_NAME) + 1);
	}

	head->n_sec = cpu_to_le32(cnt);
	len = head_len;
	for (i = 0; i < cnt; i++) {
		sec = head->section + i;
		mem = mem_cfg + i;
		sec->off = cpu_to_le32(WCN_DUMP_ALIGN(len));
		sec->start = cpu_to_le32(mem->addr);
		sec->end = cpu_to_le32(sec->start + mem->len - 1);
		len += mem->len;
		WCN_INFO("section[%d] [0x%x 0x%x 0x%x]\n",
			 i, le32_to_cpu(sec->start),
			 le32_to_cpu(sec->end), le32_to_cpu(sec->off));
	}
	head->file_size = cpu_to_le32(len + strlen(WCN_DUMP_END_STRING));

	if (sprdwcn_chr_get_minidump_state()) {
		fw_dump_write(head, head_len);
	} else {
		mdbg_ring_write(mdev_ring, head, head_len);
		wake_up_log_wait();
	}

	kfree(head);
	head = NULL;

	return 0;
}

static void mdbg_dump_str(char *str, int str_len)
{
	if (!str)
		return;
	if (sprdwcn_chr_get_minidump_state()) {
		fw_dump_write(str, str_len);
	} else {
		mdbg_ring_write_timeout(mdev_ring, str, str_len, WCN_DUMP_TIMEOUT);
		wake_up_log_wait();
	}
	WCN_INFO("dump str finish!");
}

u32 mdbg_check_wcn_sys_exit_sleep(void)
{
	u32 need_dump_status = 0;
	u32 wcn_sys_domain_power = ((0x6<<28));
	struct wcn_device *wcn_dev;

	wcn_dev = s_wcn_device.btwf_device;
	wcn_regmap_read(wcn_dev->rmap[REGMAP_PMU_APB],
				 0x0860, &need_dump_status);
	WCN_INFO("REG 0x64020860:val=0x%x!\n", need_dump_status);

	return (need_dump_status & wcn_sys_domain_power) ? 0 : BSP;

}

u32 mdbg_check_btwf_sys_exit_sleep(void)
{
	u32 need_dump_status = 0;
	u32 btwf_sys_domain_power = ((0x6<<7));
	struct wcn_device *wcn_dev;

	wcn_dev = s_wcn_device.btwf_device;
	wcn_regmap_read(wcn_dev->rmap[REGMAP_AON_APB],
			 0x0364, &need_dump_status);
	WCN_INFO("REG 0x64000364:val=0x%x!\n", need_dump_status);

	return (need_dump_status & btwf_sys_domain_power) ? 0 : BTWF;
}

u32 mdbg_check_wifi_ip_status(void)
{
	u32 need_dump_status = 0;
	u32 btwf_wrap_mac_mask = (0x2);
	u32 btwf_wrap_phy_mask = (0x4);
	u32 btwf_arm_domain_power = (0x1);
	u32 btwf_arm_wrap_mask = (0x8);
	struct wcn_device *wcn_dev;

	wcn_dev = s_wcn_device.btwf_device;
	wcn_regmap_read(wcn_dev->rmap[REGMAP_WCN_AON_APB],
					0x03b0, &need_dump_status);
	WCN_INFO("%s:0x03b0=0x%x\n", __func__, need_dump_status);

	return ((need_dump_status & btwf_wrap_mac_mask) &&
			(need_dump_status & btwf_wrap_phy_mask) &&
			(need_dump_status & btwf_arm_wrap_mask) &&
			(need_dump_status & btwf_arm_domain_power)) ? 0 : WIFI;
}

u32 mdbg_check_bt_poweron(void)
{
	u32 need_dump_status = 0;
	u32 btwf_bt_domain_power = (0x1<<4);
	struct wcn_device *wcn_dev;

	wcn_dev = s_wcn_device.btwf_device;
	wcn_regmap_read(wcn_dev->rmap[REGMAP_WCN_AON_APB],
					0x03b0, &need_dump_status);
	WCN_INFO("%s:0x03b0=0x%x\n", __func__, need_dump_status);

	return (need_dump_status & btwf_bt_domain_power) ? 0 : BT;
}

u32 mdbg_check_gnss_poweron(void)
{
	u32 need_dump_status = 0;
	u32 gnss_ss_poweron_mask = (0x4<<4);
	u32 gnss_poweron_mask = (0x2<<4);
	struct wcn_device *wcn_dev;

	wcn_dev = s_wcn_device.gnss_device;
	wcn_regmap_read(wcn_dev->rmap[REGMAP_WCN_AON_APB],
					0x03b0, &need_dump_status);
	WCN_INFO("%s:0x03b0=0x%x\n", __func__, need_dump_status);

	return ((need_dump_status & gnss_poweron_mask) &&
			(need_dump_status & gnss_ss_poweron_mask)) ? 0 : GNSS;

}

static int mdbg_dump_mem_regmap(u32 addr, u32 len, u32 skip)
{
	u32 chip_tp, count, trans_size, i;
	struct regmap *map;
	void *buf = NULL;
	u8 *tmp;

	chip_tp = wcn_platform_chip_type();

	buf = vmalloc(len);
	if (!buf) {
		WCN_ERR("%s vmalloc buf error\n", __func__);
		return -ENOMEM;
	}
	memset(buf, 0, len);

	if (unlikely(skip)) {
		WCN_WARN("%s: skip addr:0x%x\n", __func__, addr);
		goto next;
	}

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_SHARKL3)
		map = wcn_get_btwf_regmap(REGMAP_WCN_REG);
	else
		map = wcn_get_btwf_regmap(REGMAP_ANLG_WRAP_WCN);

	wcn_regmap_raw_write_bit(map, 0XFF4, addr);
	for (i = 0; i < len / 4; i++) {
		tmp = (u8 *) (buf + i * 4);
		wcn_regmap_read(map, 0XFFC, (u32 *)tmp);
	}

next:

	count = 0;
	while (count < len) {
		trans_size = (len - count) > DUMP_PACKET_SIZE ?
			DUMP_PACKET_SIZE : (len - count);
		mdbg_ring_write_timeout(mdev_ring, buf + count,
				trans_size, WCN_DUMP_TIMEOUT);

		count += trans_size;
		wake_up_log_wait();
	}

	WCN_INFO("%s dump 0x%x finish! total count %u\n", __func__, addr, count);

	vfree(buf);
	return 0;
}

static int mdbg_minidump_mem_regmap(u32 addr, u32 len, u32 skip)
{
	u32 chip_tp, count, trans_size, i;
	struct regmap *map;
	void *buf = NULL;
	u8 *tmp;

	chip_tp = wcn_platform_chip_type();

	buf = vmalloc(len);
	if (!buf) {
		WCN_ERR("%s vmalloc buf error\n", __func__);
		return -ENOMEM;
	}
	memset(buf, 0, len);

	if (unlikely(skip)) {
		WCN_WARN("%s: skip addr:0x%x\n", __func__, addr);
		goto next;
	}

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_SHARKL3)
		map = wcn_get_btwf_regmap(REGMAP_WCN_REG);
	else
		map = wcn_get_btwf_regmap(REGMAP_ANLG_WRAP_WCN);

	wcn_regmap_raw_write_bit(map, 0XFF4, addr);
	for (i = 0; i < len / 4; i++) {
		tmp = (u8 *) (buf + i * 4);
		wcn_regmap_read(map, 0XFFC, (u32 *)tmp);
	}

next:

	count = 0;
	while (count < len) {
		trans_size = (len - count) > DUMP_PACKET_SIZE ?
			DUMP_PACKET_SIZE : (len - count);
		fw_dump_write(buf + count, trans_size);

		count += trans_size;
	}

	WCN_INFO("%s minidump 0x%x finish! total count %u\n", __func__, addr, count);

	vfree(buf);
	buf = NULL;
	return 0;
}

static int mdbg_dump_mem_generic(u32 addr, u32 len, u32 skip)
{
	u32 count, trans_size;
	void *virt_addr;
	unsigned int cnt;
	const char dummy[DUMP_PACKET_SIZE] = {0};
	unsigned long timeout;

	if (unlikely(!mdbg_dev->ring_dev)) {
		WCN_ERR("ring_dev is NULL\n");
		return -1;
	}

	WCN_INFO("ring->pbuff=%p, ring->end=%p.\n",
		 mdev_ring->pbuff, mdev_ring->end);

	virt_addr = wcn_mem_ram_vmap_nocache(addr, len, &cnt);
	if (!virt_addr) {
		WCN_ERR("wcn_mem_ram_vmap_nocache fail\n");
		return -1;
	}

	count = 0;
	/* 20s timeout */
	timeout = jiffies + msecs_to_jiffies(20000);
	while (count < len) {
		trans_size = (len - count) > DUMP_PACKET_SIZE ?
			DUMP_PACKET_SIZE : (len - count);

		/* copy data from ddr to ring buf  */
		if (unlikely(skip)) {
			WCN_WARN("%s: skip dump addr:0x%x\n", __func__, addr);
			mdbg_ring_write_timeout(mdev_ring, (void *)dummy,
					trans_size, WCN_DUMP_TIMEOUT);
		} else {
			mdbg_ring_write_timeout(mdev_ring, virt_addr + count,
					trans_size, WCN_DUMP_TIMEOUT);
		}

		count += trans_size;
		wake_up_log_wait();
		if (time_after(jiffies, timeout)) {
			WCN_ERR("Dump share mem timeout:count:%u\n", count);
			return -1;
		}
	}
	wcn_mem_ram_unmap(virt_addr, cnt);
	WCN_INFO("%s dump 0x%x finish! total count %u\n", __func__, addr, count);

	return 0;
}

static int mdbg_minidump_mem_generic(u32 addr, u32 len, u32 skip)
{
	u32 count, trans_size;
	void *virt_addr;
	unsigned int cnt;
	const char dummy[DUMP_PACKET_SIZE] = {0};
	unsigned long timeout;

	virt_addr = wcn_mem_ram_vmap_nocache(addr, len, &cnt);
	if (!virt_addr) {
		WCN_ERR("wcn_mem_ram_vmap_nocache fail\n");
		return -1;
	}

	count = 0;
	/* 20s timeout */
	timeout = jiffies + msecs_to_jiffies(20000);
	while (count < len) {
		trans_size = (len - count) > DUMP_PACKET_SIZE ?
			DUMP_PACKET_SIZE : (len - count);

		/* copy data from ddr to ring buf  */
		if (unlikely(skip)) {
			WCN_WARN("%s: skip minidump addr:0x%x\n", __func__, addr);
			fw_dump_write((void *)dummy, trans_size);
		} else {
			fw_dump_write(virt_addr + count, trans_size);
		}

		count += trans_size;
		if (time_after(jiffies, timeout)) {
			WCN_ERR("Minidump share mem timeout:count:%u\n", count);
			return -1;
		}
	}
	wcn_mem_ram_unmap(virt_addr, cnt);
	WCN_INFO("%s dump 0x%x finish! total count %u\n", __func__, addr, count);

	return 0;
}

static int btwf_dump_mem(enum wcn_source_type type)
{
	u32 cp2_status = 0, i, skip = 0, ret = 0;
	phys_addr_t sleep_addr;

	if (wcn_get_btwf_power_status() == WCN_POWER_STATUS_OFF) {
		WCN_INFO("wcn power status off:can not dump btwf!\n");
		return -1;
	}

	mdbg_send_atcmd("at+sleep_switch=0\r",
			strlen("at+sleep_switch=0\r"),
			WCN_ATCMD_KERNEL);
	msleep(500);
	sleep_addr = wcn_get_btwf_sleep_addr();
	wcn_read_data_from_phy_addr(sleep_addr, &cp2_status, sizeof(u32));
	mdev_ring = mdbg_dev->ring_dev->ring;

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6) {
		if (btwf_sys_force_exit_deep_sleep(s_wcn_device.btwf_device)) {
			WCN_INFO("Dump need prerequisite!\n");
			return 0;
		}
	}

	if (type == WCN_SOURCE_GNSS) {
		mdbg_cpu_reset();//only soft reset not reset release
		return 0;
	}

	mdbg_hold_cpu(MDBG_CACHE_FLAG_VALUE);
	msleep(100);
	ret = mdbg_check_clean_cache_done(MDBG_CACHE_CLEAN_DONE);
	mdbg_ring_reset(mdev_ring);
	mdbg_atcmd_clean();

	if (wcn_fill_dump_head_info(btwf_reg, btwf_reg_cnt))
		return -1;

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6) {
		skip |= mdbg_check_wifi_ip_status();
		skip |= mdbg_check_gnss_poweron();
		skip |= mdbg_check_wcn_sys_exit_sleep();
		skip |= mdbg_check_btwf_sys_exit_sleep();
		skip |= mdbg_check_bt_poweron();
	} else if (cp2_status != WCN_CP2_STATUS_DUMP_REG)
		skip |= BT | FM | WIFI | BSP | BTWF;

	for (i = 0; i < btwf_reg_cnt; i++) {
		if (btwf_reg[i].domain & CP) {
			if ((wcn_platform_chip_type() == WCN_PLATFORM_TYPE_SHARKL3) && ret)
				mdbg_dump_mem_regmap(btwf_reg[i].addr + btwf_reg[i].offset,
						btwf_reg[i].len,
						1);
			else
				mdbg_dump_mem_regmap(btwf_reg[i].addr + btwf_reg[i].offset,
						btwf_reg[i].len,
						btwf_reg[i].domain & skip & ~CP);
                } else
			mdbg_dump_mem_generic(btwf_reg[i].addr + btwf_reg[i].offset,
						btwf_reg[i].len,
						btwf_reg[i].domain & skip & ~CP);
	}

	mdbg_dump_str(WCN_DUMP_END_STRING, strlen(WCN_DUMP_END_STRING));

	return 0;
}

static int btwf_minidump_mem(enum wcn_source_type type)
{
	u32 cp2_status = 0, i, skip = 0;
	phys_addr_t sleep_addr;

	wcn_get_btwf_base_addr();

	if (wcn_get_btwf_power_status() == WCN_POWER_STATUS_OFF) {
		WCN_INFO("wcn power status off:can not dump btwf!\n");
		return -1;
	}

	mdbg_send_atcmd("at+sleep_switch=0\r",
			strlen("at+sleep_switch=0\r"),
			WCN_ATCMD_KERNEL);
	msleep(500);
	sleep_addr = wcn_get_btwf_sleep_addr();
	wcn_read_data_from_phy_addr(sleep_addr, &cp2_status, sizeof(u32));

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6) {
		if (btwf_sys_force_exit_deep_sleep(s_wcn_device.btwf_device)) {
			WCN_INFO("Dump need prerequisite!\n");
			return 0;
		}
	}

	if (type == WCN_SOURCE_GNSS) {
		mdbg_cpu_reset();//only soft reset not reset release
		return 0;
	}

	mdbg_hold_cpu(MDBG_CACHE_FLAG_VALUE);
	msleep(100);
	mdbg_check_clean_cache_done(MDBG_CACHE_CLEAN_DONE);
	mdbg_ring_reset(mdev_ring);
	mdbg_atcmd_clean();

	if (wcn_fill_dump_head_info(btwf_minidump_reg, btwf_mem_cnt))
		return -1;

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6) {
		skip |= mdbg_check_wifi_ip_status();
		skip |= mdbg_check_gnss_poweron();
		skip |= mdbg_check_wcn_sys_exit_sleep();
		skip |= mdbg_check_btwf_sys_exit_sleep();
		skip |= mdbg_check_bt_poweron();
	} else if (cp2_status != WCN_CP2_STATUS_DUMP_REG)
		skip |= BT | FM | WIFI | BSP | BTWF;

	for (i = 0; i < btwf_mem_cnt; i++) {
		if (btwf_minidump_reg[i].domain & CP)
			mdbg_minidump_mem_regmap(btwf_minidump_reg[i].addr
						+ btwf_minidump_reg[i].offset,
						btwf_minidump_reg[i].len,
						btwf_minidump_reg[i].domain & skip & ~CP);
		else
			mdbg_minidump_mem_generic(btwf_minidump_reg[i].addr
						+ btwf_minidump_reg[i].offset,
						btwf_minidump_reg[i].len,
						btwf_minidump_reg[i].domain & skip & ~CP);
	}

	mdbg_dump_str(WCN_DUMP_END_STRING, strlen(WCN_DUMP_END_STRING));

	return 0;
}

/*
 * 9863a dump,if gnss and btwf both hold cpu,the dump will very slowly.
 * this time kernel log will has scheedule_timeout
 * now dump only dump the source type
 *
 */
void mdbg_dump_mem_integ(enum wcn_source_type type)
{
	/* print debugbus information beforce dump for debugging */
	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6)
		debug_bus_show("WCN Assert");

	if (type == WCN_SOURCE_GNSS) {
		/* dump gnss */
		gnss_dump_mem(0);
		/* dump btwf */
		btwf_dump_mem(type);//only send sleep ,not dump btwf
	} else {
		/* dump btwf */
		btwf_dump_mem(type);
	}

	wcn_sipc_mdbg_debug_show();
}

void mdbg_minidump_mem_integ(enum wcn_source_type type)
{
	if (type == WCN_SOURCE_GNSS) {
		/* dump gnss */
		gnss_dump_mem(0);
	}
	/* dump btwf */
	btwf_minidump_mem(type);//type == WCN_SOURCE_GNSS, only send sleep, not dump btwf
}

int dump_arm_reg_integ(void)
{
	mdbg_hold_cpu(MDBG_CACHE_FLAG_VALUE);

	return 0;
}

static int mdbg_snap_shoot_iram_data(void *buf, u32 addr, u32 len)
{
	struct regmap *regmap;
	u32 i;
	u32 cp_access_type;
	phys_addr_t phy_addr;
	u8 *ptr = NULL;

	WCN_INFO("start snap_shoot iram data!addr:%x,len:%d", addr, len);
	if (marlin_get_module_status() == 0) {
		WCN_ERR("module status off:can not get iram data!\n");
		return -1;
	}

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6)
		cp_access_type = 1;
	else
		cp_access_type = 0;
	if (cp_access_type) {
		/* direct map */
		phy_addr =  addr + WCN_AON_ADDR_OFFSET;
		ptr = buf;
		wcn_read_data_from_phy_addr(phy_addr, ptr, len);
	} else {
		/* aon funcdma tlb way */
		if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_SHARKL3)
			regmap = wcn_get_btwf_regmap(REGMAP_WCN_REG);
		else
			regmap = wcn_get_btwf_regmap(REGMAP_ANLG_WRAP_WCN);
		wcn_regmap_raw_write_bit(regmap, 0XFF4, addr);
		for (i = 0; i < len / 4; i++) {
			ptr = buf + i * 4;
			wcn_regmap_read(regmap, 0XFFC, (u32 *)ptr);
		}
	}
	WCN_INFO("snap_shoot iram data success\n");

	return 0;
}

int mdbg_snap_shoot_iram(void *buf)
{
	u32 ret;

	if (wcn_platform_chip_type() == WCN_PLATFORM_TYPE_QOGIRL6)
		return -EINVAL;

	ret = mdbg_snap_shoot_iram_data(buf,
			0x18000000, 1024 * 32);

	return ret;
}
