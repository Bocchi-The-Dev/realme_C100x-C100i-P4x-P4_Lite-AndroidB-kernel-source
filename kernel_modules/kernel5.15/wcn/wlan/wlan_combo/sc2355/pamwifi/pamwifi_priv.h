/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _PAMWIFI_PRIV_H_
#define _PAMWIFI_PRIV_H_

#include <linux/alarmtimer.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/sipa.h>
#include "common/common.h"

#define PAMWIFI_TP_ENABLE

#define MSDU_HEADER_LEN 16
#define SPRDWL_TYPE_DATA 2

#define PAMWIFI_MAX_LUT_LEN 32
#define  PAMWIFI_DL_CMN_FIFO_DEPTH 1024
#define SIPA_TERM_WIFI 0x2

#define PAMWIFI_UL_FREE_INT_MASK       (BIT(0))
#define PAMWIFI_UL_FILL_INT_MASK       (BIT(1))
#define PAMWIFI_DL_FREE	               (BIT(2))
#define PAMWIFI_DL_FILL_4IN1	       (BIT(3))
#define PAMWIFI_DL_FILL_TYPE1	       (BIT(4))
#define PAMWIFI_DL_FILL_TYPE2	       (BIT(5))
#define PAMWIFI_DL_FILL_TYPE3	       (BIT(6))
#define PAMWIFI_DL_FILL_TYPE4	       (BIT(7))
#define PAMWIFI_DL_MISS_RX	           (BIT(8))
#define PAMWIFI_DL_MISS_TX	           (BIT(9))
#define PAMWIFI_4IN1_TYPE1_OVERFLOW    (BIT(10))
#define PAMWIFI_4IN1_TYPE2_OVERFLOW    (BIT(11))
#define PAMWIFI_4IN1_TYPE3_OVERFLOW    (BIT(12))
#define PAMWIFI_4IN1_TYPE4_OVERFLOW    (BIT(13))

#define PAMWIFI_TX_FIFO_DELAY_TIMER	(BIT(0))
#define PAMWIFI_TX_FIFO_THRESHOLD	(BIT(1))
#define PAMWIFI_TX_FIFO_INTR        (BIT(2))


#define PW_OVERFLOW_ENTER_INTR         (BIT(3))
#define PW_OVERFLOW_EXIT_INTR          (BIT(4))

enum pamwifi_suspend_stage {
	//pamwifi is enabled and resume
	PAMWIFI_NONE,
	//PAMWIFI_FORCE_SUSPEND = BIT(0),
	PAMWIFI_EB_SUSPEND = BIT(1),
	PAMWIFI_REG_SUSPEND = BIT(2),
	PAMWIFI_READY = BIT(3),
};

#define PAMWIFI_RESUME_STAGE (PAMWIFI_EB_SUSPEND |\
		PAMWIFI_REG_SUSPEND)


enum pamwifi_start_type {
	PW_START_ALL =1,
	PW_START_UL =2,
	PW_START_DL = 4,
};

enum pamwifi_hw_status {
	PWHW_STS_ALL_STARTED =1,
	PWHW_STS_UL_STARTED =2,
	PWHW_STS_DL_STARTED = 4,
	PWHW_STS_IDLE =8,
	PWHW_STS_UL_IDLE =16,
	PWHW_STS_DL_IDLE =32,
	PWHW_STS_PAUSED = 64,
};

enum pamwifi_cmn_fifo_index {
	CMNFIFO_TYPE_RF =0,
	CMNFIFO_TYPE_DL_TYPE1 =1,
	CMNFIFO_TYPE_DL_TYPE2 =2,
	CMNFIFO_TYPE_DL_TYPE3 =3,
	CMNFIFO_TYPE_DL_TYPE4 =4,
	CMNFIFO_TYPE_DL_FREE =5,
	CMNFIFO_TYPE_UL =6,
	CMNFIFO_TYPE_DL_MISS =7,
	CMNFIFO_TYPE_RAM1 =8,
	CMNFIFO_TYPE_RAM2 =9,
	PWFIFO_TYPE_MAX,
};

enum pamwifi_ipi_mode {
	PAMWIFI_IPI_MODE1 =1, // 1 ipi int
	PAMWIFI_IPI_MODE2 =2, //ul/dl 2 ipi int
	PAMWIFI_IPI_MODE4 =4, //ul free/ul fill/dl free/dl fill 4 ipi int
};

enum pamwifi_acax_mode {
	PAMWIFI_AX_MODE =0, // marlin3
	PAMWIFI_AC_MODE =1, //marlin5
};

enum pamwifi_overflow_mode {
	PAMWIFI_SW_OVERFLOW, //
	PAMWIFI_HW_OVERFLOW, //
};

enum pamwifi_dir_type {
	PAMWIFI_INTR_TO_CP, //interrupt to cp
	PAMWIFI_INTR_TO_AP, //interrupt to ap
};


struct pamwifi_cmn_fifo_tag {
	u32 depth;
	u32 wr;
	u32 rd;
	bool in_iram;

	u32 fifo_base_addr_l;
	u32 fifo_base_addr_h;

	void *virt_addr;
};

struct pamwifi_cmn_fifo_cfg_tag {
	const char *fifo_name;
	bool is_recv;
	bool is_pam;
	u32 state;
	u32 pending;
	u32 dst;
	u32 src;

	u32 irq_eb;

	u64 fifo_phy_addr;

	void __iomem *fifo_reg_base;

	struct pamwifi_cmn_fifo_tag rx_fifo;
	struct pamwifi_cmn_fifo_tag tx_fifo;

	u32 enter_flow_ctrl_cnt;
	u32 exit_flow_ctrl_cnt;
};

struct pamwifi_buffer_watermark {
	u32 dl_cp_filled;
	u32 dl_cp_miss;
	u32 dl_ap_filled;
	u32 dl_ap_free;
	u32 dl_cp_type1;
	u32 dl_cp_type2;
	u32 dl_cp_type3;
	u32 dl_cp_type4;
	u32 dl_cp_free;
	u32 ul_ap_free;
	u32 ul_cp_free;
	u32 ul_cp_filled;
	u32 ul_ap_filled;
};

struct pamwifi_buffer_type {
	bool node_fifo;
	bool index_fifo;
	bool type1_buffer;
	bool type2_buffer;
	bool type3_buffer;
	bool type4_buffer;
	bool cp_free_buffer;
	bool ap_filled_buffer;
	bool ap_free_buffer;
	bool ul_free_buffer;
	bool ul_filled_buffer;
};

struct pamwifi_ul_node {
	u8 msdu_length;
	u8 offset_before_msdu;
	u8 ul_net_id;
	u8 ul_dst_id;
	u8 ul_src_id;
};

struct pamwifi_pkt_cnt {
	u16 type1_cnt;
	u16 type2_cnt;
	u16 type3_cnt;
	u16 type4_cnt;
};

struct pamwifi_route_table_node {
	u8 sa[ETH_ALEN];
	u8 da[ETH_ALEN];
	u8 sta_lut_index;
	u8 ctx_id;
	u8 index;
};

struct pamwifi_fifo_phy_ops {
	int (*set_rx_addr)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u64 addr);
	int (*set_rx_depth)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 depth);
	int (*get_rx_depth)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base);
	int (*set_tx_depth)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base, u32 depth);
	int (*set_tx_addr)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u64 addr);
	int (*get_tx_depth)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base);
	int (*set_intr_timeout)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			bool enable, u32 time);
	int (*set_intr_thres)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			bool enable, u32 cnt);
	int (*enable_flowctrl_irq)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 enable, u32 irq_mode);
	int (*set_flowctrl_mode)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 work_mode,
			u32 tx_entry_watermark,
			u32 tx_exit_watermark,
			u32 rx_entry_watermark,
			u32 rx_exit_watermark);;
	void (*clr_tx_fifo_intr)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 src);
	int (*get_rx_ptr)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 *wr, u32 *rd);
	int (*get_tx_ptr)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 *wr, u32 *rd);
	int (*get_filled_depth)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 *rx, u32 *tx);
	int (*add_tx_fifo_rptr)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 tx_rd);
	int (*add_rx_fifo_wptr)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base,
			u32 rx_wr);
	int (*update_route_table)(void __iomem *reg_base,
			struct pamwifi_route_table_node *node,
			bool add);
	u32 (*get_fifo_int_sts)(enum pamwifi_cmn_fifo_index id,
			void __iomem *reg_base);
	void (*register_dump)(void __iomem *reg_base,
			enum pamwifi_cmn_fifo_index id);
};

struct pawwifi_glb_phy_ops {
	int (*start)(void __iomem *reg_base,enum pamwifi_start_type type);
	int (*stop)(void __iomem *reg_base,enum pamwifi_start_type type );
	enum pamwifi_start_type (*get_pamwifi_status)(void __iomem *reg_base);
	int (*pause)(void __iomem *reg_base);
	int (*resume)(void __iomem *reg_base);
	int (*system_enable)(void __iomem *subsys_reg_base, bool on);
	int (*set_dl_base_addr)(void __iomem *reg_base,
			u64 filled_addr, u64 free_addr);
	int (*set_ul_base_addr)(void __iomem *reg_base,
			u64 filled_addr, u64 free_addr);
	int (*set_dl_sts_addr)(void __iomem *reg_base,
			u64 filled_addr, u64 free_addr);
	int (*set_ul_sts_addr)(void __iomem *reg_base,
			u64 filled_addr, u64 free_addr);
	int (*set_dl_netid)(void __iomem *reg_base,
			u32 netid);
	int (*set_dl_dstid)(void __iomem *reg_base,
			u32 dstid);
	int (*set_buffer_watermark)(void __iomem *reg_base,
			struct pamwifi_buffer_watermark *value);
	int (*set_ul_free_mem_offset)(void __iomem *reg_base,
			u64 value);
	int (*set_ul_filled_mem_offset)(void __iomem *reg_base,
			u64 value);
	int (*set_dl_free_mem_offset)(void __iomem *reg_base,
			u64 value);
	int (*set_dl_filled_mem_offset)(void __iomem *reg_base,
			u64 value);
	int (*set_msdu_base_addr)(void __iomem *reg_base,
			u64 value);
	int (*set_ul_node)(void __iomem *reg_base,
			struct pamwifi_ul_node *node);
	enum pamwifi_hw_status (*get_status)(void __iomem *reg_base);
	int (*update_soft_table)(void __iomem *reg_base);
	int (*set_rf_timescale)(void __iomem *reg_base,u8 value);
	int (*set_ipi_mode)(void __iomem *reg_base,
			enum pamwifi_ipi_mode mode);
	int (*set_ac_ax_mode)(void __iomem *reg_base,
			enum pamwifi_acax_mode mode);
	int (*set_4in1_mode)(void __iomem *reg_base,
			bool enable);
	int (*set_4in1_threshold)(void __iomem *reg_base,
			u8 value);
	int (*set_overflow_mode)(void __iomem *reg_base,
			enum pamwifi_overflow_mode mode);
	int (*enable_flow_count)(void __iomem *reg_base,
			bool enable);
	int (*get_flow_count)(void __iomem *reg_base,
			struct pamwifi_pkt_cnt *pkt_cnt);
	int (*set_interrup_direction)(void __iomem *reg_base,
			u32 src, enum pamwifi_dir_type dir);
	int (*enable_interrup_src)(void __iomem *reg_base,
			u32 src, bool enable);
	int (*clr_interrup_src)(void __iomem *reg_base,
			u32 src);
	u32 (*get_interrup_status_src)(void __iomem *reg_base);
	u32 (*get_interrup_raw_status_src)(void __iomem *reg_base);
	int (*set_ipi_ul1_base_addr)(void __iomem *reg_base,
			u64 addr);
	int (*set_ipi_ul2_base_addr)(void __iomem *reg_base,
			u64 addr);
	int (*set_ipi_dl1_base_addr)(void __iomem *reg_base,
			u64 addr);
	int (*set_ipi_dl2_base_addr)(void __iomem *reg_base,
			u64 addr);
	int (*set_ipi_ul1_base_wdata)(void __iomem *reg_base,
			u32 wdata);
	int (*set_ipi_ul2_base_wdata)(void __iomem *reg_base,
			u32 wdata);
	int (*set_ipi_dl1_base_wdata)(void __iomem *reg_base,
			u32 wdata);
	int (*set_ipi_dl2_base_wdata)(void __iomem *reg_base,
			u32 wdata);
	int (*set_router_table_depth)(void __iomem *reg_base,
			u32 depth);
	bool (*lock_router_table)(void __iomem *reg_base);
	int (*unlock_router_table)(void __iomem *reg_base);
	void (*register_dump)(void __iomem *reg_base);
};

struct pamwifi_ops {
	struct pawwifi_glb_phy_ops *glb_ops;
	struct pamwifi_fifo_phy_ops  *fifo_ops;
};

/*Inform CP2 the pamwifi supported inof*/
struct pamwifi_cap_tlv {
	u8 ap_pam_wifi_support;
	u8 mux_tx_cmn_fifo_support;
	//0:mode 1; 1: mode 1/2; 2:mode 1/2/4
	u8 ipi_mode_support;
	//mux_tx_common_fifo should equal to it
	u16 dl_rx_cmn_fifo_depth;
}__packed;

struct pam_wifi_cap_cp {
	u8 cp_pam_wifi_support;
	//0:marlin3; 1:songshanw6
	u8 chip_ver;
	u8 mux_tx_common_fifo_support;
	u16 mux_tx_common_fifo_depth;
	//40bit
	u32 mux_tx_common_fifo_base_addr_l;
	u8 mux_tx_common_fifo_base_addr_h;
	//0:1 ipi; 1:2 ipi; 2:4 ipi
	u8 ipi_mode;
	u32 ipi_reg_addr_l[4];
	u8 ipi_reg_addr_h[4];
}__packed;

struct sprdwl_pamwifi_ul_status{
	u8 sub_type;
	u8 value;
};

struct ax_tx_msdu_dscr {
	struct {
		/*0:cmd, 1:event, 2:normal data,*/
		/*3:special data, 4:PCIE remote addr*/
		unsigned char type:3;
		/*direction of address buffer of cmd/event,*/
		/*0:Tx, 1:Rx*/
		unsigned char direction_ind:1;
		unsigned char need_rsp:1;
		/*ctxt_id*/
		unsigned char interface:3;
	} common;
	unsigned char offset:4;
	unsigned char reserved:4;
	struct {
		/*1:need HW to do checksum*/
		unsigned char checksum_offload:1;
		/*0:udp, 1:tcp*/
		unsigned char checksum_type:1;
		/*1:use SW rate,no aggregation 0:normal*/
		unsigned char sw_rate:1;
		/*WDS frame*/
		unsigned char wds:1;
		/*1:frame sent from SWQ to MH,
		 *0:frame sent from TXQ to MH,
		 default:0
		 */
		unsigned char swq_flag:1;
		unsigned char rsvd:1;
		/*used by PCIe address buffer, need set default:0*/
		unsigned char next_buffer_type:1;
		/*used by PCIe address buffer, need set default:0*/
		unsigned char pcie_mh_readcomp:1;
	} tx_ctrl;
	unsigned short pkt_len;
	struct {
		unsigned char msdu_tid:4;
		unsigned short sta_lut_index:10;
		unsigned char encryption_bypass:1;
		unsigned char ap_buf_flas:1;
	} buffer_info;
	unsigned char color_bit:2;
	unsigned char seq_num:8;
	unsigned char rsvd:6;
	unsigned short tcp_udp_header_offset;
};

struct pamwifi_miss_node_tx_dscr{
	u64 address : 34;
	u64 length : 15;
	u64 offset : 7;
	u64 src_id : 5;
	u64 tos : 2;
	u64 flag : 1;
};

struct pamwifi_miss_node_rx_dscr{
	u64 address : 34;
	u64 length : 15;
	u64 offset : 7;
	u64 src_id : 5;
	u64 tos : 2;
	u64 flag : 1;
};

struct sprdwl_pamwifi_msg_buf{
	struct list_head list;
	struct pamwifi_miss_node_tx_dscr dscr;
};

struct pamwifi_msglist{
	struct list_head freelist;
	struct list_head busylist;
	int max_num;
	atomic_t busylist_count;
};


struct pamwifi_intr_thres {
	bool cnt_enable;
	u32 cnt;
	bool timeout_enable;
	u32 timeout;

};

#ifdef PAMWIFI_TP_ENABLE
struct pamwifi_tp_info {
	unsigned long tx_bytes;
	unsigned long tx_count;
	unsigned long tx_last_time;
	unsigned long rx_bytes;
	unsigned long rx_last_time;
	unsigned long throughput_rx;
	unsigned long throughput_tx;
};
#endif
/**
 * struct pamwifi_t - PAMWIFI information
 * @pdev:		    Platform device
 * @status:              pamwifi system status
 * @irq:                   pamwifi interrupt handle
 * @tx_completed:	Used to signal pipeline clear transfer complete
 */
struct pamwifi_t{
	struct platform_device *pdev;
	struct sprd_priv *priv;
	struct pamwifi_route_table_node router_table[PAMWIFI_MAX_LUT_LEN];
	u32 suspend_stage;
	bool enabled;
	unsigned int irq;
	struct sipa_connect_params sipa_params;
	struct sipa_rm_register_params	ul_param;
	struct sipa_to_pam_info sipa_info;
	struct sk_buff_head buffer_list;
	struct delayed_work power_work;
	bool power_status;
	//requested: 1, released: 0
	u8 ul_resource_flag;
	struct pamwifi_msglist *msglist;
	bool tx_4in1_en;
	u32 fifo_depth;
	u32 search_table_depth;
	u32 timescale;
	struct pamwifi_intr_thres tx_thres;
	struct pamwifi_buffer_watermark watermark;
	struct pamwifi_ul_node ul_node;
	void __iomem *glb_base;
	void __iomem *subsys_base;
	struct pawwifi_glb_phy_ops *glb_ops;
	struct pamwifi_fifo_phy_ops *fifo_ops;
};
#endif /* _PAMWIFI_PRIV_H_ */
