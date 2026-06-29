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
#include "marlin_platform.h"
#include "wcn_bus.h"
#include <linux/uaccess.h>
#include "bufring.h"
#include "edma_engine.h"
//#include "rdc_debug.h"
#include "wcn_txrx.h"
#include "wcn_log.h"
#include "wcn_swd_dap.h"
#include "../include/wcn_glb_reg.h"
#include "mdbg_type.h"
#include "../include/wcn_dbg.h"
#include "gnss_dump.h"
#include "wcn_gnss_dump.h"
#include "wcn_fw_dump.h"
#include "wcn_dump.h"
#include "wcn_chr.h"

#define DUMP_PACKET_SIZE		(32 * 1024)

static int smp_calc_chsum(unsigned short *buf, unsigned int size)
{
	unsigned long int cksum = 0;
	unsigned short data;

	while (size > 1) {
		data = *buf;
		buf++;
		cksum += data;
		size -= sizeof(unsigned short);
	}

	if (size)
		cksum += *buf & 0xff;

	while (cksum >> 16)
		cksum = (cksum >> 16) + (cksum & 0xffff);

	return (unsigned short)(~cksum);
}

static int mdbg_write_smp_head(unsigned int len)
{
	struct smp_head *smp;
	unsigned char *smp_buf, *tmp;
	unsigned short *buf_tmp;
	int smp_len;

	smp_len = sizeof(struct smp_head) + sizeof(struct sme_head_tag);
	smp_buf = kmalloc(smp_len, GFP_KERNEL);
	if (!smp_buf)
		return -ENOMEM;

	/* Smp header */
	smp = (struct smp_head *)smp_buf;
	smp->sync_code = SMP_HEADERFLAG;
	smp->length = smp_len + len - SYSNC_CODE_LEN;
	smp->channel_num = SMP_DSP_CHANNEL_NUM;
	smp->packet_type = SMP_DSP_TYPE;
	smp->reserved = SMP_RESERVEDFLAG;
	buf_tmp = &smp->length;
	smp->check_sum = smp_calc_chsum(buf_tmp, sizeof(struct smp_head)
		- SYSNC_CODE_LEN - CHKSUM_LEN);

	/*
	 * Diag header: Needs use these bytes for ARM log tool,
	 * And it need't 0x7e head and without 0x7e tail
	 */
	tmp = smp_buf + sizeof(struct smp_head);
	((struct sme_head_tag *)tmp)->seq_num = 0;
	((struct sme_head_tag *)tmp)->len = smp_len
		+ len - sizeof(struct smp_head);
	((struct sme_head_tag *)tmp)->type = SMP_DSP_TYPE;
	((struct sme_head_tag *)tmp)->subtype = SMP_DSP_DUMP_TYPE;

	mdbg_ring_write(mdbg_dev->ring_dev->ring, smp_buf, smp_len);

	kfree(smp_buf);
	smp_buf = NULL;

	return 0;
}

static int mdbg_dump_data(unsigned int start_addr,
			  char *str, int len, int str_len, size_t skip)
{
	unsigned char *buf, *temp_buf;
	int count, trans_size, err = 0, i, prin_temp = 2;
	int temp_len;

	if (unlikely(!mdbg_dev->ring_dev)) {
		WCN_ERR(" mdbg_dump ring_dev is NULL\n");
		return -1;
	}
	str = NULL;
	if (str) {
		WCN_INFO("mdbg str_len:%d\n", str_len);
		if (mdbg_dev->ring_dev->flag_smp == 1)
			mdbg_write_smp_head(str_len);

		if ((mdbg_ring_free_space(mdbg_dev->ring_dev->ring) - 1)
			 < str_len) {
			wake_up_log_wait();
			temp_len
			= mdbg_ring_free_space(mdbg_dev->ring_dev->ring)
						- 1;
			if (temp_len > 0)
				mdbg_ring_write(mdbg_dev->ring_dev->ring,
						str, temp_len);
			if (temp_len < 0) {
				WCN_ERR("ringbuf str error\n");
				return 0;
			}
			str += temp_len;
			str_len -= temp_len;
			wake_up_log_wait();
		}

		while ((mdbg_ring_free_space(mdbg_dev->ring_dev->ring)
			- 1 == 0) && (mdbg_dev->open_count != 0)) {
			WCN_ERR("no space to write mem, sleep...\n");
			wake_up_log_wait();
			msleep(20);
		}

		mdbg_ring_write(mdbg_dev->ring_dev->ring, str, str_len);
		wake_up_log_wait();
	}

	if (len == 0)
		return 0;

	buf = kzalloc(DUMP_PACKET_SIZE, GFP_KERNEL);
	temp_buf = buf;
	if (!buf)
		return -ENOMEM;

	count = 0;
	while (count < len) {
		trans_size = (len - count) > DUMP_PACKET_SIZE ?
			DUMP_PACKET_SIZE : (len - count);
		temp_buf = buf;

		if (likely(!skip))
			err = sprdwcn_bus_direct_read(start_addr + count, buf,
					      trans_size);
		if (err < 0) {
			WCN_ERR("%s dump memory error:%d\n", __func__, err);
			goto out;
		}
		if (prin_temp == 0) {
			prin_temp = 1;
			for (i = 0; i < 5; i++)
				WCN_ERR("mdbg *****buf[%d]:0x%x\n",
				       i, buf[i]);
		}
		if (mdbg_dev->ring_dev->flag_smp == 1)
			mdbg_write_smp_head(trans_size);

		temp_len
			= mdbg_ring_free_space(mdbg_dev->ring_dev->ring) - 1;
		if (temp_len < trans_size) {
			wake_up_log_wait();

			if (temp_len > 0)
				mdbg_ring_write(mdbg_dev->ring_dev->ring,
						temp_buf, temp_len);
			if (temp_len < 0) {
				WCN_ERR("ringbuf data error\n");
				goto out;
			}
			temp_buf += temp_len;
			trans_size -= temp_len;
			count += temp_len;
			wake_up_log_wait();
		}
		while ((mdbg_ring_free_space(mdbg_dev->ring_dev->ring) - 1 == 0)
			&& (mdbg_dev->open_count != 0)) {
			WCN_ERR("no space buf to write mem, sleep...\n");
			wake_up_log_wait();
			msleep(20);
		}

		mdbg_ring_write(mdbg_dev->ring_dev->ring, temp_buf, trans_size);
		count += trans_size;
		wake_up_log_wait();
	}

out:
	kfree(buf);

	return count;
}

static int mdbg_minidump_data(unsigned int start_addr,
			  char *str, int len, int str_len, size_t skip)
{
	unsigned char *buf;
	int count = 0, trans_size = 0, err = 0;

	if (len == 0)
		return 0;

	buf = kzalloc(DUMP_PACKET_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (count < len) {
		memset(buf, 0, DUMP_PACKET_SIZE);
		trans_size = (len - count) > DUMP_PACKET_SIZE ?
			DUMP_PACKET_SIZE : (len - count);

		if (likely(!skip))
			err = sprdwcn_bus_direct_read(start_addr + count, buf, trans_size);
		if (err < 0) {
			WCN_ERR("%s minidump memory error:%d\n", __func__, err);
			goto out;
		}

		fw_dump_write(buf, trans_size);
		count += trans_size;
	}

out:
	kfree(buf);
	buf = NULL;

	return count;
}

static int dump_ring_free_space(void)
{
	if (sprdwcn_chr_get_minidump_state())// add reset adj todo
		return (int)fw_ring_free_space();

	return (int)gnss_ring_free_space();
}

int gnss_dump_data(void *start_addr, int len, u32 skip)
{
	char *buf, *temp_buf;
	int count, trans_size;

	if (len == 0)
		return 0;

	buf = kmalloc(DUMP_PACKET_SIZE, GFP_KERNEL);
	temp_buf = buf;
	if (!buf) {
		WCN_ERR("%s kmalloc failed\n", __func__);
		return -ENOMEM;
	}

	count = 0;
	while (count < len) {
		memset(buf, 0, DUMP_PACKET_SIZE);
		trans_size = (len - count) > DUMP_PACKET_SIZE ?
			DUMP_PACKET_SIZE : (len - count);
		temp_buf = buf;
		if (likely(!skip))
			memcpy_fromio(buf, start_addr+count, trans_size);
		while (dump_ring_free_space() - 1 <= 0) {
			WCN_ERR("no space to write mem,sleep...\n");
			msleep(20);
		}
		WCN_DBG("gnss_dump_write:%d", trans_size);

		if (sprdwcn_chr_get_minidump_state())
			fw_dump_write(temp_buf, trans_size);
		else
			gnss_dump_write(temp_buf, trans_size);
		if (trans_size >= 0x4000) {
			msleep(10);
		}
		count += trans_size;
	}
	kfree(buf);

	return count;
}

static void mdbg_clear_log(void)
{
	if (mdbg_dev->ring_dev->ring->rp
		!= mdbg_dev->ring_dev->ring->wp) {
		WCN_INFO("log:%ld left in ringbuf not read\n",
			 (long)(mdbg_dev->ring_dev->ring->wp
		- mdbg_dev->ring_dev->ring->rp));
		mdbg_ring_clear(mdbg_dev->ring_dev->ring);
	}
}

#define WCN_DUMP_END_STRING "marlin_memdump_finish"
/* magic number, not change it */
#define WCN_DUMP_VERSION_NAME "WCN_DUMP_HEAD__"
/* SUB_NAME len not more than 15 bytes */
#define WCN_DUMP_VERSION_SUB_NAME "SDIO_23xx"

#define WCN_DUMP_ALIGN(x) (((x) + 3) & ~3)

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
		strlen(WCN_DUMP_VERSION_NAME));
	strncpy(head->sub_version, WCN_DUMP_VERSION_SUB_NAME,
		strlen(WCN_DUMP_VERSION_SUB_NAME));
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
		mdbg_ring_write(mdbg_dev->ring_dev->ring, head, head_len);
		wake_up_log_wait();
	}
	kfree(head);

	return 0;
}

static void mdbg_dump_str(char *str, int str_len)
{
	if (!str)
		return;
	if (sprdwcn_chr_get_minidump_state()) {
		fw_dump_write(str, str_len);
	} else {
		mdbg_ring_write(mdbg_dev->ring_dev->ring, str, str_len);
		wake_up_log_wait();
	}
	WCN_INFO("dump str finish!");
}

void gnss_dump_str(char *str, int str_len)
{
	//mm_segment_t fs;
	if (!str)
		return;
	//fs = get_fs();
	//set_fs(KERNEL_DS);

	if (sprdwcn_chr_get_minidump_state()) {
		fw_dump_write(str, str_len);
		WCN_INFO("dump str finish!");
		return;
	}

	gnss_dump_write(str, str_len);
	//set_fs(fs);
	WCN_INFO("dump str finish!");
}


#define  CACHE_STATUS_OFFSET  32
#define  CACHE_START_OFFSET    36
#define  CACHE_END_OFFSET       40
#define  DCACHE_BLOCK_NUM       7

struct cache_block_config {
	unsigned int reg_addr;
	unsigned int reg_value;
};

static struct cache_block_config s_cache_block_config[] = {
		{DCACHE_REG_BASE+4, 0x100000},
		{DCACHE_REG_BASE+8, 0x1E6000},
		{DCACHE_REG_BASE+12, 0x200000},
		{DCACHE_REG_BASE+16, 0x230000},
		{DCACHE_REG_BASE+20, 0x240000},
		{DCACHE_REG_BASE+24, 0x250000},
		{DCACHE_REG_BASE+28, 0x260000},
};

static int cp_dcache_clean_invalid_all(void)
{
	int ret;
	unsigned int cp_cache_status = 0;
	unsigned int reg_val = 0;
	int i;

	/*
	 * 1.AP write DCACHE REG CMD by sdio dt mode
	 * 2.delay little time for dcache clean excuting and polling done raw
	 * 3.clear done raw
	 * 4.if sdio dt mode is breaked,
	 *   cp cpu reset and dcache REG is default.
	 *   cache_debug mode must be set normal mode.
	 *   cache_size set 32K
	 */
	ret =  sprdwcn_bus_reg_read(get_sync_addr() + CACHE_STATUS_OFFSET,
				    &cp_cache_status, 4);
	if (!(ret == 0)) {
		pr_info("Marlin3_Dcache status sdiohal_dt_read error !\n");
		return ret;
	}
	ret = sprdwcn_bus_reg_read(DCACHE_REG_ENABLE, &reg_val, 4);
	if (!(ret == 0)) {
		pr_info("Marlin3_Dcache REG sdiohal_dt_read error !\n");
		return ret;
	}
	if (!(reg_val & DCACHE_ENABLE_MASK) && !cp_cache_status) {
		WCN_INFO("CP DCACHE DISENABLE\n");
		return ret;
	}

	if (cp_cache_status && !(reg_val & DCACHE_ENABLE_MASK)) {
		/* need config cache as resetpin */
		WCN_INFO("Config cache as pull reset pin");
		ret = sprdwcn_bus_reg_read(get_sync_addr() + CACHE_START_OFFSET,
					  &(s_cache_block_config[0].reg_value),
					  4);
		if (!(ret == 0)) {
			pr_info("Marlin3_Dcache startaddr sdiohal_dt_read error !\n");
			return ret;
		}
		ret = sprdwcn_bus_reg_read(get_sync_addr() + CACHE_END_OFFSET,
					  &(s_cache_block_config[1].reg_value),
					  4);
		if (!(ret == 0)) {
			pr_info("Marlin3_Dcache endaddr sdiohal_dt_read error !\n");
			return ret;
		}
		ret = sprdwcn_bus_reg_read(DCACHE_CFG0, &reg_val, 4);
		if (!(ret == 0)) {
			pr_info("Marlin3_Dcache REG sdiohal_dt_read error !\n");
			return ret;
		}
		reg_val |= 0x30000002;
		/* cache set 32k, write allocate mode */
		ret = sprdwcn_bus_reg_write(DCACHE_CFG0, &reg_val, 4);
		if (!ret)
			pr_info("Marlin3_Dcache REG sdiohal_dt_read error !\n");
		/* config block addr */
		for (i = 0; i < DCACHE_BLOCK_NUM; i++)
			sprdwcn_bus_reg_write(s_cache_block_config[i].reg_addr,
					   &(s_cache_block_config[i].reg_value),
					   4);
		/* enable dcache block 1 */
		reg_val = 0x2;
		sprdwcn_bus_reg_write(DCACHE_REG_ENABLE, &reg_val, 4);
	}
	if (!cp_cache_status && (reg_val & DCACHE_ENABLE_MASK))
		WCN_INFO("cp_cache_status is not the same with reg status\n");
	WCN_INFO("CP DCACHE ENABLE\n");
	ret = sprdwcn_bus_reg_read(DCACHE_CFG0, &reg_val, 4);
	if (!(ret == 0)) {
		pr_info("Marlin3_Dcache REG sdiohal_dt_read error !\n");
		return ret;
	}
	if (reg_val & DCACHE_DEBUG_EN) {
		reg_val &= ~(DCACHE_DEBUG_EN);
		/* dcache set normal mode */
		ret = sprdwcn_bus_reg_write(DCACHE_CFG0, &reg_val, 4);
		if (!(ret == 0)) {
			pr_info("Marlin3_Dcache REG sdiohal_dt_write error !\n");
			return ret;
		}
	}
	ret = sprdwcn_bus_reg_read(DCACHE_CFG0, &reg_val, 4);
	if ((reg_val & DCACHE_SIZE_SEL_MASK) != DCACHE_SIZE_SEL_MASK) {
		reg_val |= ((DCACHE_SIZE_32K<<28)&DCACHE_SIZE_SEL_MASK);
		/* cache size set 32K */
		ret = sprdwcn_bus_reg_write(DCACHE_CFG0, &reg_val, 4);
	}
	reg_val = (
		(DCACHE_CMD_ISSUE_START | DCACHE_CMD_CLEAN_INVALID_ALL)&
		DCACHE_CMD_CFG2_MASK);
	ret = sprdwcn_bus_reg_write(DCACHE_CMD_CFG2, &reg_val, 4);
	if (!ret)
		pr_info("Marlin3_Dcache REG sdiohal_dt_write error !\n");
	/* cmd excuting */
	udelay(200);
	ret = sprdwcn_bus_reg_read(DCACHE_INT_RAW_STS, &reg_val, 4);
	if (!ret)
		pr_info("Marlin3_Dcache REG sdiohal_dt_read error !\n");
	/* read raw */
	if ((reg_val & 0X00000001) == 0) {
		pr_info("Marlin3_Dcache clear cost time not enough !\n");
		return ret;
	}
	reg_val = (DCACHE_CMD_IRQ_CLR);
	/* clear raw */
	ret = sprdwcn_bus_reg_write(DCACHE_INT_CLR, &reg_val, 4);

	return ret;
}

/* select UMW2652 aon_apb_dap DAP(Debug Access Port) */
static void btwf_dap_sel_lite(void)
{
	int ret;
	unsigned int reg_val = 0;

	ret = sprdwcn_bus_reg_read(M3L_BTWF_DAP_CTRL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read DJTAG_DAP_SEL error:%d\n", __func__, ret);
		WCN_INFO("read DJTAG_DAP_SEL fail,start reset pin!\n");
		ret = marlin_reset_reg();
		if (ret < 0) {
			WCN_ERR("marlin_reset_reg fail,reset pin fail!\n");
			return;
		}
		ret = sprdwcn_bus_reg_read(M3L_BTWF_DAP_CTRL, &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("after reset,read DJTAG_DAP_SEL still fail!\n");
			return;
		}
	}
	WCN_LOG("%s DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);

	reg_val |= M3L_CM4_DAP_SEL_BTWF;
	ret = sprdwcn_bus_reg_write(M3L_BTWF_DAP_CTRL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write DJTAG_DAP_SEL error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);

	ret = sprdwcn_bus_reg_read(M3L_BTWF_DAP_CTRL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read2 DJTAG_DAP_SEL error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s 2:DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);
}

/* enable UMW2652 aon_apb_dap_en */
static void btwf_apb_eb_lite(void)
{
	int ret;
	unsigned int reg_val = 0;

	ret = sprdwcn_bus_reg_read(M3L_BTWF_APB_EB1, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s APB_EB:0x%x\n", __func__, reg_val);

	reg_val |= M3L_BTWF_DBG_CM4_EB;
	ret = sprdwcn_bus_reg_write(M3L_BTWF_APB_EB1, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s APB_EB:0x%x\n", __func__, reg_val);

	ret = sprdwcn_bus_reg_read(M3L_BTWF_APB_EB1, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read2 APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s 2:APB_EB:0x%x\n", __func__, reg_val);
}


/* select UMW2652 aon_apb_dap DAP(Debug Access Port) */
void btwf_dap_sel_default_lite(void)
{
	int ret;
	unsigned int reg_val;

	reg_val = 0;
	ret = sprdwcn_bus_reg_write(M3L_BTWF_DAP_CTRL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write DJTAG_DAP_SEL error:%d\n", __func__, ret);
		return;
	}
}

/* select UMW2652 aon_apb_dap DAP(Debug Access Port) */
static void gnss_dap_sel_lite(void)
{
	int ret;
	unsigned int reg_val = 0;

	ret = sprdwcn_bus_reg_read(M3L_GNSS_CM4_DBG_SEL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read DJTAG_DAP_SEL error:%d\n", __func__, ret);
		WCN_INFO("read DJTAG_DAP_SEL fail,start reset pin!\n");
		ret = marlin_reset_reg();
		if (ret < 0) {
			WCN_ERR("marlin_reset_reg fail,reset pin fail!\n");
			return;
		}
		ret = sprdwcn_bus_reg_read(M3L_GNSS_CM4_DBG_SEL, &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("after reset,read DJTAG_DAP_SEL still fail!\n");
			return;
		}
	}
	WCN_LOG("%s DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);

	reg_val |= M3L_CM4_DAP_SEL_GNSS;
	ret = sprdwcn_bus_reg_write(M3L_GNSS_CM4_DBG_SEL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write DJTAG_DAP_SEL error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);

	ret = sprdwcn_bus_reg_read(M3L_GNSS_CM4_DBG_SEL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read2 DJTAG_DAP_SEL error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s 2:DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);
}

/* enable UMW2652 aon_apb_dap_en */
static void gnss_apb_eb_lite(void)
{
	int ret;
	unsigned int reg_val = 0;

	ret = sprdwcn_bus_reg_read(M3L_GNSS_PERI_EB, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s APB_EB:0x%x\n", __func__, reg_val);

	reg_val |= M3L_GNSS_DBG_CM4_EB;
	ret = sprdwcn_bus_reg_write(M3L_GNSS_PERI_EB, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s APB_EB:0x%x\n", __func__, reg_val);

	ret = sprdwcn_bus_reg_read(M3L_GNSS_PERI_EB, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read2 APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s 2:APB_EB:0x%x\n", __func__, reg_val);
}

/* select UMW2652 aon_apb_dap DAP(Debug Access Port) */
void gnss_dap_sel_default_lite(void)
{
	int ret;
	unsigned int reg_val;

	reg_val = 0;
	ret = sprdwcn_bus_reg_write(M3L_GNSS_CM4_DBG_SEL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write DJTAG_DAP_SEL error:%d\n", __func__, ret);
		return;
	}
}


/* select aon_apb_dap DAP(Debug Access Port) */
static void dap_sel(void)
{
	int ret;
	unsigned int reg_val = 0;

	ret = sprdwcn_bus_reg_read(DJTAG_DAP_SEL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read DJTAG_DAP_SEL error:%d\n", __func__, ret);
		WCN_ERR("dt fail,start reset pin!\n");
		ret = marlin_reset_reg();
		if (ret < 0) {
			WCN_ERR("dt fail,reset pin fail!\n");
			return;
		}
		ret = sprdwcn_bus_reg_read(DJTAG_DAP_SEL, &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("after reset,dt read still fail!\n");
			return;
		}
	}
	WCN_LOG("%s DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);

	reg_val |= CM4_DAP_SEL_BTWF | CM4_DAP_SEL_GNSS;
	ret = sprdwcn_bus_reg_write(DJTAG_DAP_SEL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write DJTAG_DAP_SEL error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);

	ret = sprdwcn_bus_reg_read(DJTAG_DAP_SEL, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read2 DJTAG_DAP_SEL error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s 2:DJTAG_DAP_SEL:0x%x\n", __func__, reg_val);
}

/* select aon_apb_dap DAP(Debug Access Port) */
static void dap_sel_default(void)
{
	int ret;
	unsigned int reg_val;

	reg_val = 0;
	ret = sprdwcn_bus_reg_write(DJTAG_DAP_SEL, &reg_val, 4);
	if (ret < 0)
		WCN_ERR("%s write DJTAG_DAP_SEL error:%d\n", __func__, ret);
}

/* disable aon_apb_dap_rst */
static void apb_rst(void)
{
	int ret;
	unsigned int reg_val = 0;

	ret = sprdwcn_bus_reg_read(APB_RST, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read APB_RST error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s APB_RST:0x%x\n", __func__, reg_val);

	reg_val &= ~CM4_DAP0_SOFT_RST & ~CM4_DAP1_SOFT_RST;
	ret = sprdwcn_bus_reg_write(APB_RST, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write APB_RST error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s APB_RST:0x%x\n", __func__, reg_val);

	ret = sprdwcn_bus_reg_read(APB_RST, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read2 APB_RST error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s 2:APB_RST:0x%x\n", __func__, reg_val);
}

/* enable aon_apb_dap_en */
static void apb_eb(void)
{
	int ret;
	unsigned int reg_val = 0;

	ret = sprdwcn_bus_reg_read(APB_EB, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s APB_EB:0x%x\n", __func__, reg_val);

	reg_val |= CM4_DAP0_EB | CM4_DAP1_EB;
	ret = sprdwcn_bus_reg_write(APB_EB, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s write APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s APB_EB:0x%x\n", __func__, reg_val);

	ret = sprdwcn_bus_reg_read(APB_EB, &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read2 APB_EB error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s 2:APB_EB:0x%x\n", __func__, reg_val);
}

static void check_dap_is_ok(u32 id)
{
	int ret;
	unsigned int reg_val = 0;

	ret = sprdwcn_bus_reg_read(get_arm_dap_status_reg(id), &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read error:%d\n", __func__, ret);
		return;
	}
	WCN_LOG("%s :0x%x\n", __func__, reg_val);

	if (reg_val == CM4_DAP_OK_VALUE)
		WCN_INFO("dap[%u] is ready\n", id);
	else
		WCN_ERR("dap[%u] is error, 0x%x\n", id, reg_val);
}

/*
 * Debug Halting Control status Register
 * (0xe000edf0) = 0xa05f0003
 */
static void hold_arm_core(u32 id)
{
	int ret, i;
	unsigned int reg_val;
	unsigned int a[][2] = {
			{get_arm_dap_reg1(id), 0x22000012},
			{get_arm_dap_reg2(id), 0xe000edf0},
			{get_arm_dap_reg3(id), 0xa05f0003} }; /* 0xa05f0007 try */

	for (i = 0; i < 3; i++) {
		reg_val = a[i][1];
		ret = sprdwcn_bus_reg_write(a[i][0], &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("%s  error:%d\n", __func__, ret);
			return;
		}
	}
}

/*
 * Debug Halting Control status Register
 * (0xe000edf0) = 0xa05f0003
 */
static void release_arm_core(u32 id)
{
	int ret, i;
	unsigned int reg_val;
	unsigned int a[][2] = {
			{get_arm_dap_reg1(id), 0x22000012},
			{get_arm_dap_reg2(id), 0xe000edf0},
			{get_arm_dap_reg3(id), 0xa05f0000} }; /* 0xa05f is a key */

	for (i = 0; i < 3; i++) {
		reg_val = a[i][1];
		ret = sprdwcn_bus_reg_write(a[i][0], &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("%s  error:%d\n", __func__, ret);
			return;
		}
	}
}

/* Debug Exception and Monitor Control Register */
static void set_debug_mode(u32 id)
{
	int ret, i;
	unsigned int reg_val;
	unsigned int a[][2] = {
			{get_arm_dap_reg1(id), 0x22000012},
			{get_arm_dap_reg2(id), 0xe000edfC},
			{get_arm_dap_reg3(id), 0x010007f1} };

	for (i = 0; i < 3; i++) {
		reg_val = a[i][1];
		ret = sprdwcn_bus_reg_write(a[i][0], &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("%s  error:%d\n", __func__, ret);
			return;
		}
	}
}

/*
 * Debug core Register Selector Register
 * The index R0 is 0, R1 is 1
 */
static void set_core_reg(u32 id, u32 index)
{
	int ret, i;
	unsigned int reg_val;
	unsigned int a[][2] = {
			{get_arm_dap_reg1(id), 0x22000012},
			{get_arm_dap_reg2(id), 0xe000edf4},
			{get_arm_dap_reg3(id), index} };

	for (i = 0; i < 3; i++) {
		reg_val = a[i][1];
		ret = sprdwcn_bus_reg_write(a[i][0], &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("%s  error:%d\n", __func__, ret);
			return;
		}
	}
}

/*
 * write_core_reg_value - write arm reg = value.
 * Example: write PC(R15)=0x12345678
 * reg_index = 15, value = 0x12345678
 */
static void write_core_reg_value(u32 id, u32 reg_index, u32 value)
{
	int ret, i;
	unsigned int reg_val;
	unsigned int a[][3] = {
			{get_arm_dap_reg1(id), 0x22000012, 0x22000012},
			{get_arm_dap_reg2(id), 0xe000edf8, 0xe000edf4},
			{get_arm_dap_reg3(id), value, 0x10000+reg_index} };

	for (i = 0; i < 3; i++) {
		reg_val = a[i][1];
		ret = sprdwcn_bus_reg_write(a[i][0], &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("%s  error:%d\n", __func__, ret);
			return;
		}
	}

	for (i = 0; i < 2; i++) {
		reg_val = a[i][1];
		ret = sprdwcn_bus_reg_write(a[i][0], &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("%s  error:%d\n", __func__, ret);
			return;
		}
	}

	ret = sprdwcn_bus_reg_read(a[2][0], &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s  error:%d\n", __func__, ret);
		return;
	}

	WCN_LOG("%s value: 0x%x, reg_value:0x%x\n", __func__, value, reg_val);

	for (i = 0; i < 3; i++) {
		reg_val = a[i][2];
		ret = sprdwcn_bus_reg_write(a[i][0], &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("%s  error:%d\n", __func__, ret);
			return;
		}
	}
}

void sprdwcn_bus_armreg_write(u32 id, u32 reg_index, u32 value)
{
	struct wcn_match_data *g_match_config = get_wcn_match_config();

	if (g_match_config && g_match_config->unisoc_wcn_m3lite) {
		if (id == 1) {
			gnss_dap_sel_lite();
			gnss_apb_eb_lite();
		} else {
			btwf_dap_sel_lite();
			btwf_apb_eb_lite();
		}
	} else {
		dap_sel();
		apb_rst();
		apb_eb();
	}

	check_dap_is_ok(id);
	hold_arm_core(id);
	set_debug_mode(id);
	write_core_reg_value(id, reg_index, value);

	/* make sure btwf core can run */
	release_arm_core(id);

	if (g_match_config && (g_match_config->unisoc_wcn_m3lite == false)) {
		/* make sure JTAG can connect dap */
		dap_sel_default();
	}
}

/* Debug Core register Data Register */
static void read_core_reg(u32 id, u32 value, u32 *p)
{
	int ret, i;
	unsigned int reg_val;
	unsigned int a[][2] = {
			{get_arm_dap_reg1(id), 0x22000012},
			{get_arm_dap_reg2(id), 0xe000edf8},
			{get_arm_dap_reg3(id), 0x00000000} };

	for (i = 0; i < 2; i++) {
		reg_val = a[i][1];
		ret = sprdwcn_bus_reg_write(a[i][0], &reg_val, 4);
		if (ret < 0) {
			WCN_ERR("%s  error:%d\n", __func__, ret);
			return;
		}
	}

	ret = sprdwcn_bus_reg_read(a[2][0], &reg_val, 4);
	if (ret < 0) {
		WCN_ERR("%s read error:%d\n", __func__, ret);
		return;
	}

	p[value] = reg_val;

	WCN_LOG("%s ****R[%d]: 0x%x****\n", __func__, value, reg_val);
}

//id=0 btwf,  id=1 gnss
int dump_arm_reg(u32 id)
{
	unsigned int i;
	static const char *core_reg_name[19] = {
		"R0 ", "R1 ", "R2 ", "R3 ", "R4 ", "R5 ", "R6 ", "R7 ", "R8 ",
		"R9 ", "R10", "R11", "R12", "R13", "R14", "R15", "PSR", "MSP",
		"PSP",
	};
	unsigned int *p;
	struct wcn_match_data *g_match_config = get_wcn_match_config();

	p = kzalloc(19 * 4, GFP_KERNEL);
	if (!p) {
		WCN_ERR("Can not allocate ARM REG Buffer\n");
		return -ENOMEM;
	}

	memset(p, 0, 19 * 4);
	if (g_match_config && g_match_config->unisoc_wcn_m3lite) {

		if (id == 1) {
			gnss_dap_sel_lite();
			gnss_apb_eb_lite();
		} else {
			btwf_dap_sel_lite();
			btwf_apb_eb_lite();
		}
	} else {
		dap_sel();
		apb_rst();
		apb_eb();
	}
	check_dap_is_ok(id);
	hold_arm_core(id);
	set_debug_mode(id);
	for (i = 0; i < 19; i++) {
		set_core_reg(id, i);
		read_core_reg(id, i, p);
	}
	WCN_INFO("------------[ ARM REG ]------------\n");
	for (i = 0; i < 19; i++)
		WCN_INFO("[%s] = \t0x%08x\n", core_reg_name[i], p[i]);

	WCN_INFO("------------[ ARM END ]------------\n");
	kfree(p);

	if (g_match_config && (g_match_config->unisoc_wcn_m3lite == false)) {
		/* make sure JTAG can connect dap */
		dap_sel_default();
	}

	return 0;
}

static int check_bt_buffer_rw(void)
{
	int ret = -1;
	unsigned int temp = 0;

	ret = sprdwcn_bus_reg_read(HCI_ARM_WR_RD_MODE, &temp, 4);
	if (ret < 0) {
		WCN_ERR("read HCI_ARM_WR_RD_MODE reg error:%d\n", ret);
		return BT_BUF;
	}
	WCN_INFO("%s HCI_ARM_WR_RD_MODE reg val:0x%x\n", __func__, temp);

	temp = HCI_ARM_WR_RD_VALUE;
	ret = sprdwcn_bus_reg_write(HCI_ARM_WR_RD_MODE, &temp, 4);

	return ret;
}

static int enable_cp_pll(void)
{
	int ret;
	unsigned int temp = 0;

	ret = sprdwcn_bus_reg_read(CLK_CTRL0, &temp, 4);
	if (ret < 0) {
		WCN_ERR("%s read CLK_CTRL0 reg error:%d\n", __func__, ret);
		return ret;
	}
	WCN_INFO("%s rd CLK_CTRL0 reg val:0x%x\n", __func__, temp);

	temp = temp | APLL_PDN;
	ret = sprdwcn_bus_reg_write(CLK_CTRL0, &temp, 4);
	if (ret < 0) {
		WCN_ERR("%s write CLK_CTRL0 reg error:%d\n", __func__, ret);
		return ret;
	}
	udelay(200);
	temp = temp | APLL_PDN | BPLL_PDN;
	WCN_INFO("%s enable CLK_CTRL0 val:0x%x\n", __func__, temp);
	ret = sprdwcn_bus_reg_write(CLK_CTRL0, &temp, 4);
	if (ret < 0) {
		WCN_ERR("%s write CLK_CTRL0 reg err:%d\n", __func__, ret);
		return ret;
	}
	udelay(200);

	return ret;
}

static int check_bt_power_clk_ison(void)
{
	int ret;
	unsigned int temp = 0;

	ret = sprdwcn_bus_reg_read(AHB_EB0, &temp, 4);
	if (ret < 0) {
		WCN_ERR("%s read AHB_EB0 reg error:%d\n", __func__, ret);
		return BT;
	}
	WCN_INFO("%s AHB_EB0 reg val:0x%x\n", __func__, temp);
	if ((temp & BT_EN) != BT_EN) {
		WCN_INFO("bt_en not enable\n");
		temp = temp | BT_EN;
		ret = sprdwcn_bus_reg_write(AHB_EB0, &temp, 4);
	}

	ret = sprdwcn_bus_reg_read(CLK_CTRL3, &temp, 4);
	if (ret < 0) {
		WCN_ERR("%s read CLK_CTRL3 reg error:%d\n", __func__, ret);
		return BT;
	}
	WCN_INFO("%s CLK_CTRL3(bit18,19 need 1)val:0x%x\n", __func__, temp);
	if (((temp & CGM_BT_32M_EN) != CGM_BT_32M_EN) ||
	    ((temp & CGM_BT_64M_EN) != CGM_BT_64M_EN)) {
		WCN_INFO("bt clk not enable\n");
		temp = temp | CGM_BT_32M_EN | CGM_BT_64M_EN;
		ret = sprdwcn_bus_reg_write(CLK_CTRL3, &temp, 4);
	}

	return 0;
}


static int check_wifi_power_domain_ison(void)
{
	int ret = 0;
	unsigned int temp = 0;

	ret = enable_cp_pll();
	if (ret < 0) {
		WCN_ERR("wifi enable cp pll err\n");
		return WIFI;
	}

	ret = sprdwcn_bus_reg_read(CHIP_SLP, &temp, 4);
	if (ret < 0) {
		WCN_ERR("%s read CHIP_SLP reg error:%d\n", __func__, ret);
		return WIFI;
	}
	WCN_INFO("%s CHIP_SLP reg val:0x%x\n", __func__, temp);

	if ((temp & WIFI_ALL_PWRON) != WIFI_ALL_PWRON) {
		/* WIFI WRAP */
		if ((temp & WIFI_WRAP_PWRON) != WIFI_WRAP_PWRON) {
			WCN_INFO("WIFI WRAP have power down\n");
			/* WRAP power on */
			WCN_INFO("WIFI WRAP start power on\n");
			ret = sprdwcn_bus_reg_read(PD_WIFI_AON_CFG4, &temp, 4);
			temp = temp & (~WIFI_WRAP_PWR_DOWN);
			ret = sprdwcn_bus_reg_write(PD_WIFI_AON_CFG4, &temp, 4);
			udelay(200);
			/* MAC power on */
			WCN_INFO("WIFI MAC start power on\n");
			ret = sprdwcn_bus_reg_read(PD_WIFI_MAC_AON_CFG4,
						   &temp, 4);
			temp = temp & (~WIFI_MAC_PWR_DOWN);
			ret = sprdwcn_bus_reg_write(PD_WIFI_MAC_AON_CFG4,
						    &temp, 4);
			udelay(200);
			/* PHY power on */
			WCN_INFO("WIFI PHY start power on\n");
			ret = sprdwcn_bus_reg_read(PD_WIFI_PHY_AON_CFG4,
						   &temp, 4);
			temp = temp & (~WIFI_PHY_PWR_DOWN);
			ret = sprdwcn_bus_reg_write(PD_WIFI_PHY_AON_CFG4,
						    &temp, 4);
			/* retention */
			WCN_INFO("WIFI retention start power on\n");
			ret = sprdwcn_bus_reg_read(PD_WIFI_AON_CFG4, &temp, 4);
			temp = temp | WIFI_RETENTION;
			ret = sprdwcn_bus_reg_write(PD_WIFI_AON_CFG4, &temp, 4);
		}
		/* WIFI MAC */
		else if ((temp & WIFI_MAC_PWRON) != WIFI_MAC_PWRON) {
			WCN_INFO("WIFI MAC have power down\n");
			/* MAC_AON_WIFI_DOZE_CTL [bit1 =0] */
			ret = sprdwcn_bus_reg_read(DUMP_WIFI_AON_MAC_ADDR,
						   &temp, 4);
			temp = temp & (~(1 << 1));
			ret = sprdwcn_bus_reg_write(DUMP_WIFI_AON_MAC_ADDR,
						    &temp, 4);
			udelay(300);
			/* WIFI_MAC_RTN_SLEEPPS_CTL [bit0] =0 */
			ret = sprdwcn_bus_reg_read(WIFI_MAC_RTN_SLEEPPS_CTL,
						   &temp, 4);
			temp = temp & (~(1 << 0));
			ret = sprdwcn_bus_reg_write(WIFI_MAC_RTN_SLEEPPS_CTL,
						    &temp, 4);
		}

	}

	ret = sprdwcn_bus_reg_read(AHB_EB0, &temp, 4);
	if (ret < 0) {
		WCN_ERR("%s read AHB_EB0 reg error:%d\n", __func__, ret);
		return WIFI;
	}
	WCN_INFO("%s AHB_EB0 reg val:0x%x\n", __func__, temp);

	if ((temp & WIFI_ALL_EN) == WIFI_ALL_EN)
		return 0;

	WCN_INFO("WIFI_en and wifi_mac_en is disable\n");
	ret = sprdwcn_bus_reg_read(AHB_EB0, &temp, 4);
	temp = temp | WIFI_EN;
	temp = temp | WIFI_MAC_EN;
	ret = sprdwcn_bus_reg_write(AHB_EB0, &temp, 4);

	return 0;
}

static int btwf_dump_mem(size_t skip)
{
	int i;

	for (i = 0; i < btwf_reg_cnt; i++) {
		mdbg_dump_data(btwf_reg[i].addr + btwf_reg[i].offset,
						NULL,
						btwf_reg[i].len,
						0,
						btwf_reg[i].domain & skip);
	}

	return 0;
}

static int btwf_minidump_mem(size_t skip)
{
	int i;

	for (i = 0; i < btwf_mem_cnt; i++) {
		mdbg_minidump_data(btwf_minidump_reg[i].addr + btwf_minidump_reg[i].offset,
						NULL,
						btwf_minidump_reg[i].len,
						0,
						btwf_minidump_reg[i].domain & skip);
	}

	return 0;
}

/*
 * 0x400F0000 - 0x400F0108 MAC AON
 * check 1:
 * AON APB status Reg(0x4083C00C
 * AON APB Control Reg(0x4083C088   bit1 wrap pwr on(0)/down(1))
 * AON APB Control Reg(0x4083C0A8  bit2 Mac Pwr on(0)/dwn(1))
 * AON APB Control Reg(0x4083C0B8 bit2 Phy pwr on(0)/dwn (1))
 * check 2:
 * Wifi EB : 0x40130004 Wifi EB(bit5)  wifi mac  enable:1
 *
 * 0x40300000 - 0x40358000  wifi 352k share RAM
 * 0x400f1000 - 0x400fe100  wifi reg
 */
int mdbg_dump_mem(enum wcn_source_type type)
{
	struct wcn_match_data *g_match_config = get_wcn_match_config();
	int i, btfm_dump_cnt = 0;
	size_t skip_modules = 0;
	u32 id = 0;
	struct wcn_dump_mem_reg *btfm_dump_reg;

	if (g_match_config && g_match_config->unisoc_wcn_pcie) {
		edma_dump_glb_reg();
		for (i = 0; i < 16; i++)
			edma_dump_chn_reg(i);
	}

	mdbg_dev->ring_dev->ring->is_mem = 1;

	if (g_match_config && !g_match_config->unisoc_wcn_m3e) {
		/* DUMP ARM REG */
		id = (type == WCN_SOURCE_GNSS ? 1 : 0);
		dump_arm_reg(id);
	}

	if (g_match_config && g_match_config->unisoc_wcn_swd)
		swd_dump_arm_reg();

	mdbg_clear_log();
	/* mdbg_atcmd_clean(); */
	cp_dcache_clean_invalid_all();

	if (type == WCN_SOURCE_GNSS)
		goto dump_gnss;

	if (sprdwcn_chr_get_minidump_state()) {
		btfm_dump_cnt = btwf_mem_cnt;
		btfm_dump_reg = btwf_minidump_reg;
	} else {
		btfm_dump_cnt = btwf_reg_cnt;
		btfm_dump_reg = btwf_reg;
	}
	if (wcn_fill_dump_head_info(btfm_dump_reg, btfm_dump_cnt))
		return -1;

	/* for dump wifi reg */
	skip_modules |= check_wifi_power_domain_ison();
	if (skip_modules & WIFI) {
		WCN_WARN("********:-) :-) :-) :-)*********\n");
		WCN_WARN("!!!mdbg wifi power domain is down!!\n");
	}

	skip_modules |= check_bt_power_clk_ison();
	if (skip_modules & BT) {
		WCN_WARN("********:-) :-) :-) :-)*********\n");
		WCN_WARN("!!!mdbg bt power domain is down!!\n");
	}

	skip_modules |= check_bt_buffer_rw();

	btwf_dump_mem(skip_modules);
	msleep(40);
	/* Make sure only string "marlin_memdump_finish" to slog one time */
	mdbg_dump_str(WCN_DUMP_END_STRING, strlen(WCN_DUMP_END_STRING));
	goto out;

dump_gnss:
	/*check the status of gnss*/
	if (!(marlin_get_power() < 80)) {
	WCN_INFO("need to dump gnss!\n");
	/* dump gnss */
	gnss_dump_mem(0);
	}
out:
	WCN_INFO("mdbg dump memory finish\n");
#ifdef WCN_RDCDBG
	/*rdc_debug.c*/
	if ((functionmask[7] & CP2_FLAG_YLOG) == 1)
		complete(&dumpmem_complete);
#endif
	return 0;
}

int mdbg_minidump_mem(enum wcn_source_type type)
{
	size_t skip_modules = 0;

	cp_dcache_clean_invalid_all();

	if (type == WCN_SOURCE_GNSS)
		goto dump_gnss;

	if (wcn_fill_dump_head_info(btwf_minidump_reg, btwf_mem_cnt))
		return -1;

	/* for dump wifi reg */
	skip_modules |= check_wifi_power_domain_ison();
	if (skip_modules & WIFI) {
		WCN_WARN("********:-) :-) :-) :-)*********\n");
		WCN_WARN("!!!mdbg wifi power domain is down!!\n");
	}

	skip_modules |= check_bt_power_clk_ison();
	if (skip_modules & BT) {
		WCN_WARN("********:-) :-) :-) :-)*********\n");
		WCN_WARN("!!!mdbg bt power domain is down!!\n");
	}

	skip_modules |= check_bt_buffer_rw();

	btwf_minidump_mem(skip_modules);
	mdbg_dump_str(WCN_DUMP_END_STRING, strlen(WCN_DUMP_END_STRING));
	return 0;

dump_gnss:
	/*check the status of gnss*/
	if (!(marlin_get_power() < 80)) {
		WCN_INFO("need to dump gnss!\n");
		/* dump gnss */
		gnss_dump_mem(0);
	}

	WCN_INFO("mdbg minidump memory finish\n");

	return 0;
}
