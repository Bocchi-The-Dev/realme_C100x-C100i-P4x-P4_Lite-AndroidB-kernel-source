/*
 * SPDX-FileCopyrightText: 2015-2022 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2015-2022 Unisoc (Shanghai) Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */
#include <linux/dma-direction.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include "pamwifi/pamwifi.h"
#include "pamwifi/pamwifi_priv.h"
#include "pamwifi/pamwifi_log.h"
#include "r2p0_reg.h"

u32 fifo_offsets[] ={
	tPSEL_RF, //CMNFIFO_TYPE_RF =0
	tPSEL_DL_TYPE1, //CMNFIFO_TYPE_DL_TYPE1=1
	tPSEL_DL_TYPE2, //CMNFIFO_TYPE_DL_TYPE2=2
	tPSEL_DL_TYPE3, //CMNFIFO_TYPE_DL_TYPE3=3
	tPSEL_DL_TYPE4, //CMNFIFO_TYPE_DL_TYPE4=4
	tPSEL_DL_FREE, //CMNFIFO_TYPE_DL_FREE=5
	tPSEL_UL, //CMNFIFO_TYPE_UL=6
	tPSEL_DL_MISS, //CMNFIFO_TYPE_DL_MISS=7
	tPSEL_RAM1, //CMNFIFO_TYPE_RAM1=8
	tPSEL_RAM2 //CMNFIFO_TYPE_RAM2=9
};

#define CMN_FIFO_OFFSET(id, __offset) do { \
	if(id < PWFIFO_TYPE_MAX) \
	__offset = fifo_offsets[id]; \
	else \
	__offset = 0; \
}while(0)

//////////////////////////////////////////////////////////////////
//             common fifo operations
/////////////////////////////////////////////////////////////////
static int set_rx_depth(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u32 depth)
{
	u32 old_value;
	u32 addr_offset;
	if(!reg_base || depth > 0xFFFFl){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);

	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_DEPTH));
	old_value  &= 0x0000FFFFl;
	old_value |= (depth << 16);
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_DEPTH));
	return 0;
}
static int get_rx_depth(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base)
{
	u32 addr_offset;
	if(!reg_base){
		return 0;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	return readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_DEPTH)) >> 16;
}
static int set_tx_depth(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base, u32 depth)
{
	u32 old_value;
	u32 addr_offset;
	if(!reg_base || depth > 0xFFFFl){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	old_value = readl_relaxed((void *)(reg_base +addr_offset +ROFF_COMMON_TX_FIFO_DEPTH));
	old_value  &= 0x0000FFFFl;
	old_value |= (depth << 16);
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_DEPTH));
	return 0;
}

static int get_tx_depth(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base)
{
	u32 addr_offset;
	if(!reg_base){
		return 0;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	return readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_DEPTH)) >> 16;
}

static int set_intr_timeout(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		bool enable, u32 time)
{
	u32 old_value;
	u32 addr_offset;

	if(!reg_base){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	/*[31:16] :Tx_FIFO_interrupt_threshold [15:0]:Tx_FIFO_interrupt_delay_timer*/
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_GEN_CTL_TX_FIFO_VALUE)) & 0xFFFF0000;
	old_value |= time;
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_GEN_CTL_TX_FIFO_VALUE));
	/*disable/enable threhold intr [0]:Tx_FIFO_interrupt_delay_timer_En*/
	old_value = readl_relaxed( (void *)(reg_base +addr_offset + ROFF_GEN_CTL_EN))&(~0x00000001);
	old_value |= (enable ? 1:0) ;
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_GEN_CTL_EN));
	return 0;
}
static int set_intr_thres(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		bool enable, u32 cnt)
{
	u32 old_value;
	u32 addr_offset;

	if(!reg_base){
		return -1;
	}

	CMN_FIFO_OFFSET(id, addr_offset);
	/*[31:16] :Tx_FIFO_interrupt_threshold [15:0]:Tx_FIFO_interrupt_delay_timer*/
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_GEN_CTL_TX_FIFO_VALUE)) & 0x0000FFFF;
	old_value |= cnt << 16 ;
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_GEN_CTL_TX_FIFO_VALUE));
	/*disable/enable threhold delay intr  [1]:Tx_FIFO_interrupt_threshold_En*/
	old_value = readl_relaxed( (void *)(reg_base +addr_offset + ROFF_GEN_CTL_EN))&(~0x00000002);
	old_value |= (enable ? 1:0) << 1;
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_GEN_CTL_EN));
	return 0;
}

static int enable_flowctrl_irq(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u32 enable, u32 irq_mode)
{
	u32 old_value;
	u32 addr_offset;

	if(!reg_base){
		return -1;
	}
	if(irq_mode != PW_OVERFLOW_ENTER_INTR
			&& irq_mode != PW_OVERFLOW_EXIT_INTR){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	/*[3]:Rx_FIFO_interrupt_enter_flow_ctrl_en [4]:Rx_FIFO_interrupt_exit_flow_ctrl_en*/
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_GEN_CTL_EN));
	if(enable)
		old_value |=irq_mode;
	else
		old_value &= ~irq_mode;

	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_GEN_CTL_EN));
	return 0;
}
static int set_flowctrl_mode(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u32 work_mode,
		u32 tx_entry_watermark,
		u32 tx_exit_watermark,
		u32 rx_entry_watermark,
		u32 rx_exit_watermark)
{
	u32 old_value;
	u32 addr_offset;

	if(!reg_base){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	/*set workmode : [1:0]:00: flow ctrl by Rx FIFO empty; 01:flow ctrl by Tx FIFO full
11: flow ctrl by Rx FIFO empty or y Tx FIFO full*/
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_FLOW_CTL_CFG))
		& 0xFFFFFFFC; //clean bit 0 and bit 1
	old_value |= work_mode &0x3;
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_FLOW_CTL_CFG));
	return 0;
}
static void clr_tx_fifo_intr(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u32 src)
{
	u32 old_value;
	u32 addr_offset;

	if(!reg_base){
		return;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_GEN_CTL_CLR));
	old_value |=src;
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_GEN_CTL_CLR));
	old_value &= ~src;
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_GEN_CTL_CLR));
}

static int get_rx_ptr(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u32 *wr, u32 *rd)
{
	u32 addr_offset;

	if(!reg_base){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	if(wr)
		*wr = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_WR)) >> 16;
	if(rd)
		*rd = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_RD)) >> 16;
	return 0;
}
static int get_tx_ptr(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u32 *wr, u32 *rd)
{
	u32 addr_offset;

	if(!reg_base){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	if(wr)
		*wr = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_WR)) >> 16;
	if(rd)
		*rd = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_RD)) >> 16;
	return 0;
}
static int add_tx_fifo_rptr(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u32 tx_rd)
{
	u32 addr_offset;
	u32 old_value;

	if(!reg_base){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_RD)) &0xFFFF; //clean high 16 bits
	old_value |= tx_rd << 16;
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_RD));
	return 0;
}
static int add_rx_fifo_wptr(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u32 rx_wr)
{
	u32 addr_offset;
	u32 old_value;

	if(!reg_base){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_WR)) &0xFFFF; //clean high 16 bits
	old_value |= (u32)(rx_wr << 16);
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_WR));
	return 0;
}

static int set_rx_addr(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u64 addr)
{
	u32 addr_offset;
	u32 old_value;

	if(!reg_base){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	/*low 32 bits*/
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_ADDRL));
	old_value = (u32)(addr &0xFFFFFFFF);
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_ADDRL));
	/*high 8 bits*/
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_ADDRH)) & 0xFFFFFF00; //clean low 8 bits
	old_value |= (u32)(addr >> 32);
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_COMMON_RX_FIFO_ADDRH));
	return 0;
}

/**
 *  set_tx_addr - Initialize tx common fifo  memory addr
 * 	@id: tx common fifo type, include : DL, type1-type4 etc.
 * 	@reg_base: based register address
 *	@addr: tx common fifo  memory addr, it must by physic address.
 */
static int set_tx_addr(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base,
		u64 addr)
{
	u32 addr_offset;
	u32 old_value;

	if(!reg_base){
		return -1;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	/*low 32 bits*/
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_ADDRL));
	old_value = (u32)(addr &0xFFFFFFFF);
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_ADDRL));
	/*high 8 bits*/
	old_value = readl_relaxed((void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_ADDRH)) & 0xFFFFFF00; //clean low 8 bits
	old_value |= (u32)(addr >> 32);
	writel_relaxed(old_value, (void *)(reg_base +addr_offset + ROFF_COMMON_TX_FIFO_ADDRH));
	return 0;
}

/**
 *  update_route_table - update pamwifi search index table
 * 	@node: route table's node.
 *	@add: true: update or add new, false: clean this index
 *
 *	The node of route table as follows:
 *		[47:0]: mac_src_addr;[95:48]: mac_dst_addr;
 *		[105:96]: lut_index;[107:106]: ctxt_id;
 *		[119: 108]: reserved;[127:120]: Index
 *
 *	consider uc w2w pkt, sa maybe not be marlin3 self mac addr, so only set da
 */
static int update_route_table(void __iomem *reg_base,
		struct pamwifi_route_table_node *node,
		bool add)
{
	u32 addr_offset;
	u32 value;
	u32 index, router_lut;

	if(!reg_base || !node || node->sta_lut_index < 6){
		return -1;
	}
	router_lut = node->sta_lut_index -6;
	index = router_lut/2;
	if(!(node->sta_lut_index % 2)){
		/*save to ram1 */
		CMN_FIFO_OFFSET(CMNFIFO_TYPE_RAM1, addr_offset);
	}
	else{
		/*save to ram2 */
		CMN_FIFO_OFFSET(CMNFIFO_TYPE_RAM2, addr_offset);
	}

	pw_info(" %s add %d ctx_id%d,lut%d,\
			index %d router_lut %d ,sd: %x:%x:%x:%x:%x:%x da: %x:%x:%x:%x:%x:%x \n",
			 __func__, add,
			node->ctx_id, node->sta_lut_index, index,router_lut,
			node->sa[0], node->sa[1], node->sa[2],
			node->sa[3], node->sa[4], node->sa[5],
			node->da[0], node->da[1], node->da[2],
			node->da[3], node->da[4], node->da[5]);
	if(add){
		/*[31:0] : sa[0] ~  sa[3]*/
		value = (u32)(node->sa[0] | node->sa[1] << 8 | node->sa[2] << 16 |  node->sa[3] <<24);
		writel_relaxed(value, (void *)(reg_base +addr_offset + 16 *index));
		/*[47:32]: sa[4]~sa[5]; [63:48]: da[0]~da[1]*/
		value =  (u32)(node->sa[4] | node->sa[5] << 8 | node->da[0] << 16 |  node->da[1] <<24);
		writel_relaxed(value, (void *)(reg_base +addr_offset + 0x04 + 16 *index));
		/*[95:64]: da[2] ~ da[5]*/
		value =  (u32)(node->da[2] | node->da[3] << 8 | node->da[4] << 16 |  node->da[5] <<24);
		writel_relaxed(value, (void *)(reg_base +addr_offset + 0x08 + 16 *index));
		/*[105:96]: lut_index;[107:106]: ctxt_id;[107:106]: ctxt_id [119: 108]: reserved;[127:120]: Index */
		value =  (node->sta_lut_index& 0x3FF) | ((node->ctx_id & 0x7) << 10)  | ((node->index & 0xFF) << 24);
		writel_relaxed(value, (void *)(reg_base +addr_offset + 0x0C + 16 *index));
	}else{
		writel_relaxed(0x0, (void *)(reg_base +addr_offset +  16 *index));
		writel_relaxed(0x0, (void *)(reg_base +addr_offset + 0x04 + 16 *index));
		writel_relaxed(0x0, (void *)(reg_base +addr_offset + 0x08 + 16 *index));
		writel_relaxed(0x0, (void *)(reg_base +addr_offset + 0x0C + 16 *index));
	}
	return 0;
}

static u32 get_fifo_int_sts(enum pamwifi_cmn_fifo_index id,
		void __iomem *reg_base)
{
	u32 addr_offset;

	if(!reg_base){
		return 0;
	}
	CMN_FIFO_OFFSET(id, addr_offset);
	return readl_relaxed((void *)(reg_base +addr_offset + ROFF_GEN_CTL_EN));
}

void fifo_register_dump(void __iomem *reg_base, enum pamwifi_cmn_fifo_index id){
#if 0
	u32 addr_offset;

	if(!reg_base){
		return;
	}

	if(id == CMNFIFO_TYPE_RAM1 || id == CMNFIFO_TYPE_RAM2){
		u32 ram1_offset, ram2_offset;
		CMN_FIFO_OFFSET(CMNFIFO_TYPE_RAM1, ram1_offset);
		CMN_FIFO_OFFSET(CMNFIFO_TYPE_RAM2, ram2_offset);
		dump_pamwifiram_seachtable(reg_base +ram1_offset,
				reg_base +ram2_offset);
	}else{
		CMN_FIFO_OFFSET(id, addr_offset);
		dump_commonfifo_register(reg_base+addr_offset);
	}
#endif
}

//////////////////////////////////////////////////////////////////
//             pamwifi common operations
/////////////////////////////////////////////////////////////////

static int start(void __iomem *reg_base,enum pamwifi_start_type type)
{
	u32 value;
	if(!reg_base){
		return -1;
	}
	value = readl_relaxed((void *)(reg_base + ROFF_CFG_START));
	/*[0]:pam_wifi_all_start [1]:pam_wifi_ul_start [2]:pam_wifi_dl_start*/
	if(type & PW_START_ALL){
		value |= 0x1;
	}else{
		if(type & PW_START_UL)
			value |=0x2;
		if(type & PW_START_DL)
			value |=0x4;
	}
	writel_relaxed(value, (void *)(reg_base + ROFF_CFG_START));
	return 0;
}
static int stop(void __iomem *reg_base,enum pamwifi_start_type type )
{
	u32 value;
	if(!reg_base){
		return -1;
	}
	value = readl_relaxed((void *)(reg_base + ROFF_CFG_START));
	/*[0]:pam_wifi_all_start [1]:pam_wifi_ul_start [2]:pam_wifi_dl_start*/
	if(type & PW_START_ALL){
		value &= ~0x1;
	}else{
		if(type & PW_START_UL)
			value &= ~0x2;
		if(type & PW_START_DL)
			value &= ~0x4;
	}
	writel_relaxed(value, (void *)(reg_base + ROFF_CFG_START));
	return 0;
}

static enum pamwifi_start_type get_pamwifi_status(void __iomem *reg_base)
{
	if(!reg_base){
		return -1;
	}
	return readl_relaxed((void *)(reg_base + ROFF_CFG_START));
}
static int pause(void __iomem *reg_base)
{
	u32 value;

	if(!reg_base){
		return -1;
	}
	value =readl_relaxed((void *)(reg_base + ROFF_CFG_START));
	/*[6]:pam_wifi_pause_req*/
	value |=BIT(6);
	writel_relaxed(value, (void *)(reg_base + ROFF_CFG_START));
	return 0;
}
static int resume(void __iomem *reg_base){
	u32 value;

	if(!reg_base){
		return -1;
	}
	value =readl_relaxed((void *)(reg_base + ROFF_CFG_START));
	/*[6]:pam_wifi_pause_req*/
	value &= ~BIT(6);
	writel_relaxed(value, (void *)(reg_base + ROFF_CFG_START));
	return 0;
}
static int set_dl_base_addr(void __iomem *reg_base,
		u64 filled_addr, u64 free_addr)
{
	u32 value, high_value;

	if(!reg_base){
		return -1;
	}
	value = (u32)(filled_addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_FILLED_FIFO_ADDRL));
	value = (u32)(free_addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_FREE_FIFO_ADDRL));
	/*DL_AP_Free_FIFO_Base_AddrH [7:0]:DL_AP_Free_FIFO_Base_AddrH
	  [15:8]:DL_AP_Filled_FIFO_Base_AddrH*/
	value = readl_relaxed((void *)(reg_base + ROFF_DL_UL_FIFO_ADDRH))&0xFFFF0000;
	high_value = (u32)(filled_addr >> 32);
	value |=  (u32)((free_addr >>32)&0xFF) | ((u32)(high_value << 8)&0xFF00);
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_UL_FIFO_ADDRH));
	return 0;
}
static int set_ul_base_addr(void __iomem *reg_base,
		u64 filled_addr, u64 free_addr)
{
	u32 value,hfree_value, hfill_value;

	if(!reg_base){
		return -1;
	}
	value = (u32)(filled_addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_UL_FILLED_FIFO_ADDRL));
	value = (u32)(free_addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_UL_FREE_FIFO_ADDRL));
	/*DL_AP_Free_FIFO_Base_AddrH [23:16]:UL_AP_Free_FIFO_Base_AddrH
	  [31:24]:UL_AP_Filled_FIFO_Base_AddrH*/
	value = readl_relaxed((void *)(reg_base + ROFF_DL_UL_FIFO_ADDRH))&0xFFFF;
	hfill_value = (u32)(filled_addr >> 32);
	hfree_value = (u32)(free_addr >> 32);
	value |=  ((hfree_value << 16)&0xFF0000) | ((hfill_value <<24)&0xFF000000);
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_UL_FIFO_ADDRH));

	return 0;
}
static int set_dl_sts_addr(void __iomem *reg_base,
		u64 filled_addr, u64 free_addr){

	u32 value, high_value;

	if(!reg_base){
		return -1;
	}
	value = (u32)(filled_addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_FILLED_STS_ADDRL));
	value = (u32)(free_addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_FREE_STS_ADDRL));
	/*DL_AP_Filled_STS_Base_AddrH [7:0]:DL_AP_Filled_STS_Base_AddrH
	  [15:8]:DL_AP_Free_STS_Base_AddrH*/
	value = readl_relaxed((void *)(reg_base + ROFF_DL_UL_STS_ADDRH))&0xFFFF0000;
	high_value = (u32)(free_addr >> 32);
	value |=  (u32)((filled_addr >>32)&0xFF) | ((high_value << 8)&0xFF00);
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_UL_STS_ADDRH));
	return 0;
}
static int set_ul_sts_addr(void __iomem *reg_base,
		u64 filled_addr, u64 free_addr){
	u32 value,hfree_value, hfill_value;

	if(!reg_base){
		return -1;
	}
	value = (u32)(filled_addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_UL_FILLED_STS_ADDRL));
	value = (u32)(free_addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_UL_FREE_STS_ADDRL));
	/*DL_AP_Filled_STS_Base_AddrH [23:16]:UL_AP_Filled_STS_Base_AddrH
	  [31:24]:UL_AP_Free_STS_Base_AddrH*/
	value = readl_relaxed((void *)(reg_base + ROFF_DL_UL_STS_ADDRH))&0xFFFF;
	hfill_value = (u32)(filled_addr >> 32);
	hfree_value = (u32)(free_addr >> 32);
	value |=  ((hfill_value << 16)&0xFF0000) | ((hfree_value <<24)&0xFF000000);
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_UL_STS_ADDRH));

	return 0;
}
static int set_dl_netid(void __iomem *reg_base,
		u32 netid)
{
	u32 value;
	if(!reg_base){
		return -1;
	}
	/*[24:17]:dl_net_id*/
	value = readl_relaxed((void *)(reg_base + ROFF_DL_FILLED_BUFFER_CTRL)) & (~PW_NET_ID_MASK);
	value |= (netid <<17)&PW_NET_ID_MASK;
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_FILLED_BUFFER_CTRL));
	return 0;
}
static int set_dl_dstid(void __iomem *reg_base,
		u32 dstid)
{
	u32 value;
	if(!reg_base){
		return -1;
	}
	/*[16:12]:dl_dst_id*/
	value = readl_relaxed((void *)(reg_base + ROFF_DL_FILLED_BUFFER_CTRL)) & (~PW_DST_ID_MASK);
	value |= (dstid << 12)&PW_DST_ID_MASK;
	writel_relaxed(value, (void *)(reg_base + ROFF_DL_FILLED_BUFFER_CTRL));
	return 0;
}

/**
 *  set_buffer_watermark - set fifo watermark
 * 	@reg_base: base register
 *	@value: all types of watermark
 *
 *	The value  follows:in cfg_buffer_watermark
 *	[31:28]	cfg_dl_ap_filled_buffer_watermark
 *	[27:24]	cfg_dl_ap_free_buffer_watermark
 *	[23:22]	cfg_dl_cp_type4_buffer_watermark
 *	[21:20]	cfg_dl_cp_type3_buffer_watermark
 *	[19:18]	cfg_dl_cp_type2_buffer_watermark
 *	[17:16]	cfg_dl_cp_type1_buffer_watermark
 *	[15:12]	cfg_ul_cp_free_buffer_watermark
 *	[11:8]	cfg_ul_ap_free_buffer_watermark
 *	[7:4]	cfg_ul_cp_filled_buffer_watermark
 *	[3:0]	cfg_ul_ap_filled_buffer_watermark
 *
 *	The value  follows:in cfg_dl_filled_buffer_ctrl
 *	[7:4]	cfg_dl_cp_filled_buffer_watermark
 *	[3:0]	cfg_dl_cp_miss_buffer_watermark
 *
 */
static int set_buffer_watermark(void __iomem *reg_base,
		struct pamwifi_buffer_watermark *value)
{
	u32 set_val, value_mask;

	if(!reg_base || !value){
		return -1;
	}
	/*set [7:4] :cfg_dl_cp_filled_buffer_watermark  [3:0]:cfg_dl_cp_miss_buffer_watermark*/
	value_mask = BIT_DL_CP_MISS_WATERMARK(value->dl_cp_miss)
		|BIT_DL_CP_FILLED_WATERMARK(value->dl_cp_filled);
	set_val =  readl_relaxed((void *)(reg_base + ROFF_DL_FILLED_BUFFER_CTRL)) &(~0xFF) ;
	set_val |= value_mask;
	writel_relaxed(set_val, (void *)(reg_base + ROFF_DL_FILLED_BUFFER_CTRL));
	/*set cfg_buffer_watermark*/
	set_val = BIT_UL_AP_FILLED_WATERMARK(value->ul_ap_filled)
		| BIT_UL_CP_FILLED_WATERMARK(value->ul_cp_filled)
		| BIT_CFG_UL_AP_FREE_WATERMARK(value->ul_ap_free)
		| BIT_UL_CP_FREE_WATERMARK(value->ul_cp_free)
		| BIT_DL_CP_TYPE1_WATERMARK(value->dl_cp_type1)
		| BIT_DL_CP_TYPE2_WATERMARK(value->dl_cp_type2)
		| BIT_DL_CP_TYPE3_WATERMARK(value->dl_cp_type3)
		| BIT_DL_CP_TYPE4_WATERMARK(value->dl_cp_type4)
		| BIT_DL_AP_FREE_WATERMARK(value->dl_ap_free)
		| BIT_DL_AP_FILLED_WATERMARK(value->dl_ap_filled);
 	writel_relaxed(set_val, (void *)(reg_base + ROFF_CFG_BUFFER_WATERMARK));
	return 0;
}

static int set_ul_free_mem_offset(void __iomem *reg_base,
		u64 value)
{
	u32 set_val;
	if(!reg_base){
		return -1;
	}
	/*set dl free low 32bits*/
	writel_relaxed((u32)value,(void *)(reg_base + ROFF_UL_FREE_DDR_MAPPING_OFFSETL));
	/*set high 8 bits: ddr_mapping_offset_h[31:24] ul_free_ddr_mapping_offset_h*/
	set_val = readl_relaxed((void *)(reg_base + ROFF_DDR_MAPPING_OFFSETH))& (~PW_UL_FREE_ADDRH_MASK);
	set_val |= (u32)((value >> 32)&0xFF) <<24;
	writel_relaxed(set_val, (void *)(reg_base + ROFF_DDR_MAPPING_OFFSETH));
	return 0;
}
static int set_ul_filled_mem_offset(void __iomem *reg_base,
		u64 value)
{
	u32 set_val;
	if(!reg_base){
		return -1;
	}
	/*set dl filled low 32bits*/
	writel_relaxed((u32)value,(void *)(reg_base + ROFF_UL_FILLED_DDR_MAPPING_OFFSETL));
	/*set high 8 bits: ddr_mapping_offset_h[23:16] ul_fill_ddr_mapping_offset_h*/
	set_val = readl_relaxed((void *)(reg_base + ROFF_DDR_MAPPING_OFFSETH))& (~PW_UL_FILLED_ADDRH_MASK);
	set_val |= (u32)((value >> 32)&0xFF) << 16;
	writel_relaxed(set_val, (void *)(reg_base + ROFF_DDR_MAPPING_OFFSETH));
	return 0;
}
static int set_dl_free_mem_offset(void __iomem *reg_base,
		u64 value)
{
	u32 set_val;
	if(!reg_base){
		return -1;
	}
	/*set dl filled low 32bits*/
	writel_relaxed((u32)value,(void *)(reg_base + ROFF_DL_FREE_DDR_MAPPING_OFFSETL));
	/*set high 8 bits: ddr_mapping_offset_h[31:24] dl_filled_ddr_mapping_offset_h*/
	set_val = readl_relaxed((void *)(reg_base + ROFF_DDR_MAPPING_OFFSETH))& (~PW_DL_FREE_ADDRH_MASK);
	set_val |= (u32)((value >> 32)&0xFF) << 8;
	writel_relaxed(set_val, (void *)(reg_base + ROFF_DDR_MAPPING_OFFSETH));
	return 0;
}
static int set_dl_filled_mem_offset(void __iomem *reg_base,
		u64 value)
{
	u32 set_val;
	if(!reg_base){
		return -1;
	}
	/*set dl filled low 32bits*/
	writel_relaxed((u32)value,(void *)(reg_base + ROFF_DL_FILLED_DDR_MAPPING_OFFSETL));
	/*set high 8 bits: ddr_mapping_offset_h[31:24] dl_filled_ddr_mapping_offset_h*/
	set_val = readl_relaxed((void *)(reg_base + ROFF_DDR_MAPPING_OFFSETH))& (~PW_DL_FILLED_ADDRH_MASK);
	set_val |= (u32)((value >> 32)&0xFF);
	writel_relaxed(set_val, (void *)(reg_base + ROFF_DDR_MAPPING_OFFSETH));
	return 0;;
}
static int set_msdu_base_addr(void __iomem *reg_base,
		u64 value)
{
	u32 set_val;

	if(!reg_base){
		return -1;
	}
	/*set msdu address low 32bits*/
	writel_relaxed((u32)value,(void *)(reg_base + ROFF_MSDU_ADDRL));
	/*set msdu address high 8 bits :rf_dl_cfg[31:24]: MSDU_Base_Addr_h*/
	set_val =  readl_relaxed((void *)(reg_base + ROFF_RF_DL_CFG))& (~PW_MSDU_ADDRH_MASK);
	set_val |= (u32)((value >>32)&0xFF) << 24;
	writel_relaxed((u32)set_val,(void *)(reg_base + ROFF_RF_DL_CFG));
	return 0;
}

/**
 *  set_ul_node - set ul node
 * 	@reg_base: base register
 *	@node: node value
 *
 *	The value  follows:in UL_node_info_config
 *	[31:26]	msdu_length
 *	[25:18]	offset_before_msdu
 *	[17:10]	ul_net_id
 *	[9:5]	ul_dst_id
 *	[4:0]	ul_src_id
 *
 */
static int set_ul_node(void __iomem *reg_base,
		struct pamwifi_ul_node *node)
{
	u32 set_val;

	if(!reg_base || !node){
		return -1;
	}
	set_val =  readl_relaxed((void *)(reg_base + ROFF_UL_NODE_INFO_CONFIG))& 0xFFFC0000;
	set_val |= BIT_UL_SRC_ID(node->ul_src_id) |BIT_UL_DST_ID(node->ul_dst_id)
		|BIT_UL_NET_ID(node->ul_net_id);
	writel_relaxed(set_val, (void *)(reg_base + ROFF_UL_NODE_INFO_CONFIG));
	return 0;
}
static enum pamwifi_hw_status get_status(void __iomem *reg_base){
	/*TODO: now not used*/
	return 0;
}
static int update_soft_table(void __iomem *reg_base){
	/*TODO: now not used*/
	return 0;
}
static int set_rf_timescale(void __iomem *reg_base,u8 value){
	u32 set_val;

	if(!reg_base){
		return -1;
	}
	set_val =  readl_relaxed((void *)(reg_base + ROFF_RF_DL_CFG))& (~PW_RF_TIMESCALE_MASK);
	set_val |= BIT_RF_TIMESCALE(value);
	writel_relaxed(set_val, (void *)(reg_base + ROFF_RF_DL_CFG));
	return 0;
}
static int set_ipi_mode(void __iomem *reg_base,
		enum pamwifi_ipi_mode mode)
{
	u32 set_val;

	if(!reg_base){
		return -1;
	}
	set_val =  readl_relaxed((void *)(reg_base + ROFF_RF_DL_CFG))& (~PW_RF_IPI_MODE_MASK);
	set_val |= BIT_RF_IPI_MODE(mode);
	writel_relaxed(set_val, (void *)(reg_base + ROFF_RF_DL_CFG));
	return 0;
}
static int set_ac_ax_mode(void __iomem *reg_base,
		enum pamwifi_acax_mode mode)
{
	u32 set_val;

	if(!reg_base){
		return -1;
	}
	set_val =  readl_relaxed((void *)(reg_base + ROFF_RF_DL_CFG))& (~PW_RF_AC_AX_SEL_MASK);
	set_val |= BIT_RF_AC_AX_SEL(mode);
 	writel_relaxed(set_val, (void *)(reg_base + ROFF_RF_DL_CFG));
	return 0;
}
static int set_4in1_mode(void __iomem *reg_base,
		bool enable)
{
	u32 set_val;

	if(!reg_base){
		return -1;
	}
	set_val =  readl_relaxed((void *)(reg_base + ROFF_RF_DL_CFG))& (~0x01);
	set_val |= (u32)(enable & 0x01);
	writel_relaxed(set_val, (void *)(reg_base + ROFF_RF_DL_CFG));
	return 0;
}
static int set_overflow_mode(void __iomem *reg_base,
		enum pamwifi_overflow_mode mode){
	/*TODO :no using now*/
	return 0;
}
static int enable_flow_count(void __iomem *reg_base,
		bool enable)
{
	u32 set_val;

	if(!reg_base){
		return -1;
	}
	set_val =  readl_relaxed((void *)(reg_base + ROFF_RF_DL_CFG))
		& (~PW_FLOW_COUNT_EN_MASK);
	set_val |= BIT_RF_FLOW_COUNT_EN(enable);
	writel_relaxed(set_val, (void *)(reg_base + ROFF_RF_DL_CFG));
	return 0;
}
static int get_flow_count(void __iomem *reg_base,
		struct pamwifi_pkt_cnt *pkt_cnt)
{
	u32 value;

	if(!reg_base || !pkt_cnt){
		return -1;
	}
	/*[31:16]:rf_type1_pkt_num_cnt, [15:0]:rf_type2_pkt_num_cnt*/
	value = readl_relaxed((void *)(reg_base + ROFF_RF_DL_FLOW_PKT_NUM_CNT1));
	pkt_cnt->type1_cnt = (u16)((value >>16)&0xFFFF);
	pkt_cnt->type2_cnt = (u16)((value)&0xFFFF);
	/*[31:16]:rf_type3_pkt_num_cnt [15:0]:rf_type4_pkt_num_cnt*/
	value = readl_relaxed((void *)(reg_base + ROFF_RF_DL_FLOW_PKT_NUM_CNT2));
	pkt_cnt->type3_cnt = (u16)((value >>16)&0xFFFF);
	pkt_cnt->type4_cnt = (u16)((value)&0xFFFF);
	return 0;
}
static int set_interrup_direction(void __iomem *reg_base, u32 src,
		enum pamwifi_dir_type dir)
{
	u32 value;

	if(!reg_base){
		return -1;
	}
	/*pam_wifi_int_en [29:16]*/
	value = readl_relaxed((void *)(reg_base + ROFF_INT_EN)) &(~(src <<16));
	if(PAMWIFI_INTR_TO_AP == dir){
		value |= (src <<16);
	}
	writel_relaxed(value, (void *)(reg_base + ROFF_INT_EN));
	return 0;
}
static int enable_interrup_src(void __iomem *reg_base,
		u32 src, bool enable)
{
	u32 value;

	if(!reg_base){
		return -1;
	}
	/*pam_wifi_int_en [13:0]*/
	value = readl_relaxed((void *)(reg_base + ROFF_INT_EN)) &(~src);
	if(enable){
		value |= (src);
	}
	writel_relaxed(value, (void *)(reg_base + ROFF_INT_EN));
	return 0;
}
static int clr_interrup_src(void __iomem *reg_base,
		u32 src)
{
	u32 value;

	if(!reg_base){
		return -1;
	}
	/*pam_wifi_int_cls[13:0]*/
	value = readl_relaxed((void *)(reg_base + ROFF_INT_CLR)) &(~src);
	value |= (src);
	writel_relaxed(value, (void *)(reg_base + ROFF_INT_CLR));
	value &= ~src;;
	writel_relaxed(value, (void *)(reg_base + ROFF_INT_CLR));
	return 0;
}
static u32 get_interrup_status_src(void __iomem *reg_base){

	if(!reg_base){
		return -1;
	}
	/*pam_wifi_int_sts[13:0]*/
	return readl_relaxed((void *)(reg_base + ROFF_INT_STS));
}
static u32 get_interrup_raw_status_src(void __iomem *reg_base){
	u32 value;

	if(!reg_base){
		return -1;
	}
	/*pam_wifi_int_sts[29:16]*/
	value = readl_relaxed((void *)(reg_base + ROFF_INT_STS));
	return value >> 16;
}
static int set_ipi_ul1_base_addr(void __iomem *reg_base,
		u64 addr)
{
	u32 value, high_value;

	if(!reg_base){
		return -1;
	}
	value = (u32)(addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_IPI_UL1_ADDRL));
	/*ROFF_IPI_ADDRH: [[31:24]:ipi_ul1_base_addrh*/
	value = readl_relaxed((void *)(reg_base + ROFF_IPI_ADDRH))&(~0xFF000000);
	high_value = (u32)(addr >> 32);
	value |=  (high_value << 24)&0xFF000000;
	writel_relaxed(value, (void *)(reg_base + ROFF_IPI_ADDRH));
	return 0;
}
static int set_ipi_ul2_base_addr(void __iomem *reg_base,
		u64 addr)
{
	u32 value, high_value;

	if(!reg_base){
		return -1;
	}
	value = (u32)(addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_IPI_UL2_ADDRL));
	/*ROFF_IPI_ADDRH: [23:16]:ipi_ul2_base_addrh*/
	value = readl_relaxed((void *)(reg_base + ROFF_IPI_ADDRH))&(~0x00FF0000);
	high_value = (u32)(addr >> 32);
	value |=  (high_value << 16)&0x00FF0000;
	writel_relaxed(value, (void *)(reg_base + ROFF_IPI_ADDRH));
	return 0;
}
static int set_ipi_dl1_base_addr(void __iomem *reg_base,
		u64 addr)
{
	u32 value, high_value;

	if(!reg_base){
		return -1;
	}
	value = (u32)(addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_IPI_DL1_ADDRL));
	/*ROFF_IPI_ADDRH: [15:8]:ipi_dl1_base_addrh*/
	value = readl_relaxed((void *)(reg_base + ROFF_IPI_ADDRH))& (~0x0000FF00);
	high_value = (u32)(addr >> 32);
	value |=  (high_value << 8)&0x0000FF00;
	writel_relaxed(value, (void *)(reg_base + ROFF_IPI_ADDRH));
	return 0;
}
static int set_ipi_dl2_base_addr(void __iomem *reg_base,
		u64 addr)
{
	u32 value, high_value;

	if(!reg_base){
		return -1;
	}
	value = (u32)(addr&0xFFFFFFFF);
	writel_relaxed(value, (void *)(reg_base + ROFF_IPI_DL2_ADDRL));
	/*ROFF_IPI_ADDRH: [15:8]:ipi_dl1_base_addrh*/
	value = readl_relaxed((void *)(reg_base + ROFF_IPI_ADDRH))&(~0x000000FF);
	high_value = (u32)(addr >> 32);
	value |=  (high_value)&0x000000FF;
	writel_relaxed(value, (void *)(reg_base + ROFF_IPI_ADDRH));
	return 0;
}
static int set_ipi_ul1_base_wdata(void __iomem *reg_base,
		u32 wdata)
{
	if(!reg_base){
		return -1;
	}
	writel_relaxed(wdata, (void *)(reg_base + ROFF_IPI_UL1_WDATA));
	return 0;
}
static int set_ipi_ul2_base_wdata(void __iomem *reg_base,
		u32 wdata)
{
	if(!reg_base){
		return -1;
	}
	writel_relaxed(wdata, (void *)(reg_base + ROFF_IPI_UL2_WDATA));
	return 0;
}
static int set_ipi_dl1_base_wdata(void __iomem *reg_base,
		u32 wdata)
{
	if(!reg_base){
		return -1;
	}
	writel_relaxed(wdata, (void *)(reg_base + ROFF_IPI_DL1_WDATA));
	return 0;
}
static int set_ipi_dl2_base_wdata(void __iomem *reg_base,
		u32 wdata)
{
	if(!reg_base){
		return -1;
	}
	writel_relaxed(wdata, (void *)(reg_base + ROFF_IPI_DL2_WDATA));
	return 0;
}

static int system_enable(void __iomem *subsys_reg_base,
		bool on)
{
	u32 value;

	if(!subsys_reg_base){
		return -1;
	}
	value = __raw_readl(subsys_reg_base);
	if(on){
		value |= PW_SUBSYS_POWER_MASK;
	}else{
		value &= ~(PW_SUBSYS_POWER_MASK);
	}
	__raw_writel(value, (void *)(subsys_reg_base));
	return 0;
}

static int set_router_table_depth(void __iomem *reg_base,
		u32 depth)
{
	u32 value;
	if(!reg_base){
		return -1;
	}
	/*[6:0]	index_search_depth*/
	value = readl_relaxed((void *)(reg_base + ROFF_INDEX_SEARCH_DEPTH))&~0x7F;
	value |= depth;
	writel_relaxed(value, (void *)(reg_base + ROFF_INDEX_SEARCH_DEPTH));
	return 0;
}

static int set_4in1_threshold(void __iomem *reg_base,
		u8 value){

	u32 set_val;

	if(!reg_base){
		return -1;
	}
	set_val = (u32) (value) | (u32) (value) << 8 | (u32) (value) << 16 | (u32) (value) <<24;
	writel_relaxed(set_val, (void *)(reg_base + ROFF_RF_DL_FLOW_THRESHOLD));
	return 0;
}

static bool wait_route_table_done(void __iomem *reg_base)
{

	u32 timeout = 100000;/*worst timeout count =10^5 , 10us per one, total 1s*/

	u32 value = 0;
	if(!reg_base){
		return false;
	}
	do{
		value = readl_relaxed((void *)(reg_base + ROFF_CFG_START))
			& BIT_TABLE_RD_STOPED;

	}while(value != BIT_TABLE_RD_STOPED && timeout--);
	if(timeout)
		return false;

	return true;
}

static bool lock_router_table(void __iomem *reg_base)
{
	u32 value;

	if(!reg_base){
		return false;
	}
	value = readl_relaxed((void *)(reg_base + ROFF_CFG_START));
	value |= BIT_SOFT_TABLE_UPDATE_REQ;
	writel_relaxed(value, (void *)(reg_base + ROFF_CFG_START));
	return wait_route_table_done(reg_base);
}
static int unlock_router_table(void __iomem *reg_base)
{
	u32 value;

	if(!reg_base){
		return -1;
	}
	value = readl_relaxed((void *)(reg_base + ROFF_CFG_START)) & ~BIT_SOFT_TABLE_UPDATE_REQ;
	writel_relaxed(value, (void *)(reg_base + ROFF_CFG_START));
	return 0;
}

void pamwifi_register_dump(void __iomem *reg_base){
	//dump_pamwifi_register(reg_base);
}

struct pamwifi_fifo_phy_ops g_pamwifi_r2p0_fifo_ops = {
	.set_rx_addr = set_rx_addr,
	.set_tx_addr = set_tx_addr,
	.set_rx_depth = set_rx_depth,
	.get_rx_depth = get_rx_depth,
	.set_tx_depth =set_tx_depth,
	.get_tx_depth = get_tx_depth,
	.set_intr_timeout=set_intr_timeout,
	.set_intr_thres=set_intr_thres,
	.enable_flowctrl_irq=enable_flowctrl_irq,
	.set_flowctrl_mode=set_flowctrl_mode,
	.clr_tx_fifo_intr = clr_tx_fifo_intr,
	.get_rx_ptr = get_rx_ptr,
	.get_tx_ptr= get_tx_ptr,
	.add_tx_fifo_rptr=add_tx_fifo_rptr,
	.add_rx_fifo_wptr=add_rx_fifo_wptr,
	.update_route_table = update_route_table,
	.get_fifo_int_sts = get_fifo_int_sts,
	.register_dump = fifo_register_dump,
};

struct pawwifi_glb_phy_ops g_pamwifi_r2p0_glb_ops = {

	.start=start,
	.stop=stop,
	.pause=pause,
	.resume = resume,
	.system_enable= system_enable,
	.get_pamwifi_status = get_pamwifi_status,
	.set_dl_base_addr=set_dl_base_addr,
	.set_ul_base_addr=set_ul_base_addr,
	.set_dl_sts_addr=set_dl_sts_addr,
	.set_ul_sts_addr=set_ul_sts_addr,
	.set_dl_netid=set_dl_netid,
	.set_dl_dstid=set_dl_dstid,
	.set_buffer_watermark =set_buffer_watermark,
	.set_ul_free_mem_offset= set_ul_free_mem_offset,
	.set_ul_filled_mem_offset = 	set_ul_filled_mem_offset,
	.set_dl_free_mem_offset=	set_dl_free_mem_offset,
	.set_dl_filled_mem_offset=set_dl_filled_mem_offset,
	.set_msdu_base_addr=set_msdu_base_addr,
	.set_ul_node = set_ul_node,
	.get_status= get_status,
	.update_soft_table =update_soft_table,
	.set_rf_timescale = set_rf_timescale,
	.set_ipi_mode=set_ipi_mode,
	.set_ac_ax_mode=set_ac_ax_mode,
	.set_4in1_mode=set_4in1_mode,
	.set_4in1_threshold = set_4in1_threshold,
	.set_overflow_mode= set_overflow_mode,
	.enable_flow_count=enable_flow_count,
	.get_flow_count=get_flow_count,
	.set_interrup_direction =set_interrup_direction,
	.enable_interrup_src= enable_interrup_src,
	.clr_interrup_src= clr_interrup_src,
	.get_interrup_status_src= get_interrup_status_src,
	.get_interrup_raw_status_src= get_interrup_raw_status_src,
	.set_ipi_ul1_base_addr= set_ipi_ul1_base_addr,
	.set_ipi_ul2_base_addr= set_ipi_ul2_base_addr,
	.set_ipi_dl1_base_addr=set_ipi_dl1_base_addr,
	.set_ipi_dl2_base_addr= set_ipi_dl2_base_addr,
	.set_ipi_ul1_base_wdata=set_ipi_ul1_base_wdata,
	.set_ipi_ul2_base_wdata =set_ipi_ul2_base_wdata,
	.set_ipi_dl1_base_wdata =set_ipi_dl1_base_wdata,
	.set_ipi_dl2_base_wdata=  set_ipi_dl2_base_wdata,
	.set_router_table_depth = set_router_table_depth,
	.lock_router_table = lock_router_table,
	.unlock_router_table = unlock_router_table,
	.register_dump = pamwifi_register_dump,
};

