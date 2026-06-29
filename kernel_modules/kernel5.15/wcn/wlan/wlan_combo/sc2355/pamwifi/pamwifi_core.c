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
#include <linux/kernel.h>
#include "sc2355/tx.h"
#include "common/iface.h"
#include "common/hif.h"
#include "sc2355_intf.h"
#include "sc2355/mm.h"
#include "sc2355/cmdevt.h"
#include "common/delay_work.h"
#include "sc2355/rx.h"
#include "sc2355/qos.h"
#include "chip_ops.h"
#include "pamwifi/pamwifi_log.h"
#include "pamwifi/pamwifi.h"
#include "pamwifi/pamwifi_priv.h"
#include "pamwifi/pamwifi_phy.h"

dma_addr_t dl_type1_phy_addr;
dma_addr_t dl_type2_phy_addr;
dma_addr_t dl_type3_phy_addr;
dma_addr_t dl_type4_phy_addr;
dma_addr_t dl_free_phy_addr;
dma_addr_t miss_tx_phy_addr;
dma_addr_t miss_rx_phy_addr;
//dma_addr_t dl_4in1_phy_addr;
dma_addr_t ul_tx_phy_addr;
dma_addr_t ul_rx_phy_addr;

void *dl_type1_virt_addr;
void * dl_type2_virt_addr;
void * dl_type3_virt_addr;
void * dl_type4_virt_addr;
void * dl_free_virt_addr;
void * miss_tx_virt_addr;
void * miss_rx_virt_addr;
//void * dl_4in1_virt_addr;
void * ul_tx_virt_addr;
void * ul_rx_virt_addr;

u32 * pam_wifi_msdu_header_info;
dma_addr_t term_pam_wifi_msdu_header_buf;
struct pamwifi_t *g_pamwifi =NULL;
struct workqueue_struct *g_power_wq =NULL;

struct pam_wifi_cap_cp g_cp_cap;
static enum sipa_nic_id g_nic_id;

#ifdef PAMWIFI_TP_ENABLE
struct pamwifi_tp_info g_tp_info;
#endif

static bool  g_power_status = false;
static bool g_hw_supported = false;

int g_pw_debug_level = PW_DBG;

static struct pamwifi_ops pamwifi_r2p0 = {
	.glb_ops = &g_pamwifi_r2p0_glb_ops,
	.fifo_ops= &g_pamwifi_r2p0_fifo_ops
};

static struct of_device_id pamwifi_match_table[] = {
	{  .compatible = "sprd,pamwifi-r2p0",
	    .data = &pamwifi_r2p0 },
	{},
};

static int load_pamwifi_cfg(struct pamwifi_t *pamwifi);
static u32 check_pamwifi_ipa_fifo_status(struct pamwifi_t *pamwifi);
static int __pamwifi_xmit_to_ipa(struct sk_buff *skb, struct net_device *ndev);
static void miss_tx_handler(struct work_struct *work);

static DEFINE_MUTEX(pamwifi_mutex);
static DECLARE_WORK(miss_tx_worker, miss_tx_handler);
static DEFINE_SPINLOCK(pamwifi_spinlock);
/*TODO*/
static void config_ipa(struct pamwifi_t *pamwifi)
{
	u32 depth;

	if(!pamwifi || !pamwifi->glb_ops){
		return;
	}
	sipa_get_ep_info(SIPA_EP_WIFI, &pamwifi->sipa_info);

	depth = pamwifi->sipa_info.dl_fifo.fifo_depth;
	pamwifi->sipa_params.recv_param.tx_enter_flowctrl_watermark = depth - depth / 4;
	pamwifi->sipa_params.recv_param.tx_leave_flowctrl_watermark = depth / 2;
	pamwifi->sipa_params.recv_param.flow_ctrl_cfg = 1;
	pamwifi->sipa_params.send_param.flow_ctrl_irq_mode = 2;
	pamwifi->sipa_params.send_param.tx_intr_threshold = 64;
	pamwifi->sipa_params.send_param.tx_intr_delay_us = 200;
	pamwifi->sipa_params.recv_param.tx_intr_threshold = 64;
	pamwifi->sipa_params.recv_param.tx_intr_delay_us = 200;
	pamwifi->sipa_params.id = SIPA_EP_WIFI;

	/*Setting IPA PAM WIFI fifo base addr*/
	if(pamwifi->glb_ops->set_ul_base_addr)
		pamwifi->glb_ops->set_ul_base_addr(pamwifi->glb_base, pamwifi->sipa_info.ul_fifo.rx_fifo_base_addr,
				pamwifi->sipa_info.ul_fifo.tx_fifo_base_addr);
	if(pamwifi->glb_ops->set_dl_base_addr)
		pamwifi->glb_ops->set_dl_base_addr(pamwifi->glb_base,  pamwifi->sipa_info.dl_fifo.tx_fifo_base_addr,
				pamwifi->sipa_info.dl_fifo.rx_fifo_base_addr);
	/*Setting IPA PAM WIFI fifo sts*/
	if(pamwifi->glb_ops->set_ul_sts_addr)
		pamwifi->glb_ops->set_ul_sts_addr(pamwifi->glb_base,  pamwifi->sipa_info.ul_fifo.fifo_sts_addr,
				pamwifi->sipa_info.ul_fifo.fifo_sts_addr);
	if(pamwifi->glb_ops->set_dl_sts_addr)
		pamwifi->glb_ops->set_dl_sts_addr(pamwifi->glb_base, pamwifi->sipa_info.dl_fifo.fifo_sts_addr,
				pamwifi->sipa_info.dl_fifo.fifo_sts_addr);
	if(pamwifi->glb_ops->register_dump)
		pamwifi->glb_ops->register_dump(pamwifi->glb_base);
}

static void config_ipi(struct pamwifi_t *pamwifi)
{
	if(!pamwifi || !pamwifi->glb_ops){
		return;
	}

	if (g_cp_cap.ipi_mode == 0) {
		pamwifi->glb_ops->set_ipi_mode(pamwifi->glb_base, PAMWIFI_IPI_MODE1);
		if(pamwifi->glb_ops->set_ipi_ul1_base_addr){
			pamwifi->glb_ops->set_ipi_ul1_base_addr(pamwifi->glb_base,
					g_cp_cap.ipi_reg_addr_l[0]|((u64)g_cp_cap.ipi_reg_addr_h[0] << 32));
		}

	} else if (g_cp_cap.ipi_mode == 1) {
		if(pamwifi->glb_ops->set_ipi_mode)
			pamwifi->glb_ops->set_ipi_mode(pamwifi->glb_base, PAMWIFI_IPI_MODE2);
		if(pamwifi->glb_ops->set_ipi_dl1_base_addr){
			pamwifi->glb_ops->set_ipi_dl1_base_addr(pamwifi->glb_base,
					g_cp_cap.ipi_reg_addr_l[0]|((u64)g_cp_cap.ipi_reg_addr_h[0] << 32));
		}
		if(pamwifi->glb_ops->set_ipi_ul1_base_addr){
			pamwifi->glb_ops->set_ipi_ul1_base_addr(pamwifi->glb_base,
					g_cp_cap.ipi_reg_addr_l[1]|((u64)g_cp_cap.ipi_reg_addr_h[1] << 32));
		}
	} else if (g_cp_cap.ipi_mode == 2) {
		if(pamwifi->glb_ops->set_ipi_mode)
			pamwifi->glb_ops->set_ipi_mode(pamwifi->glb_base, PAMWIFI_IPI_MODE4);
		if(pamwifi->glb_ops->set_ipi_dl1_base_addr){
			pamwifi->glb_ops->set_ipi_dl1_base_addr(pamwifi->glb_base,
					(u64)g_cp_cap.ipi_reg_addr_l[0]|((u64)g_cp_cap.ipi_reg_addr_h[0] << 32));
		}
		if(pamwifi->glb_ops->set_ipi_ul1_base_addr){
			pamwifi->glb_ops->set_ipi_ul1_base_addr(pamwifi->glb_base,
					g_cp_cap.ipi_reg_addr_l[1]|((u64)g_cp_cap.ipi_reg_addr_h[1] << 32));
		}
		if(pamwifi->glb_ops->set_ipi_dl2_base_addr){
			pamwifi->glb_ops->set_ipi_dl2_base_addr(pamwifi->glb_base,
					g_cp_cap.ipi_reg_addr_l[2]|((u64)g_cp_cap.ipi_reg_addr_h[2] << 32));
		}
		if(pamwifi->glb_ops->set_ipi_ul2_base_addr){
			pamwifi->glb_ops->set_ipi_ul2_base_addr(pamwifi->glb_base,
					g_cp_cap.ipi_reg_addr_l[3]|((u64)g_cp_cap.ipi_reg_addr_h[3] << 32));
		}
	}

	/*REG_PAM_WIFI_IPI_UL1_BASE_WDATA*/
	if(pamwifi->glb_ops->set_ipi_ul1_base_wdata)
		pamwifi->glb_ops->set_ipi_ul1_base_wdata(pamwifi->glb_base, 2);
	if(pamwifi->glb_ops->set_ipi_ul2_base_wdata)
		pamwifi->glb_ops->set_ipi_ul2_base_wdata(pamwifi->glb_base, 2);
	if(pamwifi->glb_ops->set_ipi_dl1_base_wdata)
		pamwifi->glb_ops->set_ipi_dl1_base_wdata(pamwifi->glb_base, 2);
	if(pamwifi->glb_ops->set_ipi_dl2_base_wdata)
		pamwifi->glb_ops->set_ipi_dl2_base_wdata(pamwifi->glb_base, 2);
}

static void poweron(struct pamwifi_t *pamwifi, bool enable)
{
	if(!pamwifi || !pamwifi->glb_ops){
		return;
	}
	pw_alert("poweron enable %d", enable);
	if(pamwifi->glb_ops->system_enable)
		pamwifi->glb_ops->system_enable(pamwifi->subsys_base, enable);
}

static int config_type1_4_common_fifo(struct pamwifi_t *pamwifi)
{
	if(!pamwifi || !pamwifi->fifo_ops || !pamwifi->glb_ops){
		return  -ENOMEM;
	}

	/*set DL TYPe1 TX FIFO Addr and depth, threshold*/
	if (!dl_type1_virt_addr)
		dl_type1_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth*2*sizeof(dma_addr_t),
				&dl_type1_phy_addr, GFP_KERNEL);
	if (!dl_type1_virt_addr) {
		pw_err("dl_type1_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	/*set DL TYPe1 TX FIFO Addr and depth, threshold*/
	if(pamwifi->fifo_ops->set_tx_addr)
		pamwifi->fifo_ops->set_tx_addr(CMNFIFO_TYPE_DL_TYPE1,pamwifi->glb_base,
				dl_type1_phy_addr);
	if(pamwifi->fifo_ops->set_tx_depth)
		pamwifi->fifo_ops->set_tx_depth(CMNFIFO_TYPE_DL_TYPE1,pamwifi->glb_base,
				pamwifi->fifo_depth);

	if(pamwifi->fifo_ops->set_intr_thres)
		pamwifi->fifo_ops->set_intr_thres(CMNFIFO_TYPE_DL_TYPE1,pamwifi->glb_base,
				pamwifi->tx_thres.cnt_enable, pamwifi->tx_thres.cnt);
	if(pamwifi->fifo_ops->set_intr_timeout)
		pamwifi->fifo_ops->set_intr_timeout(CMNFIFO_TYPE_DL_TYPE1,pamwifi->glb_base,
				pamwifi->tx_thres.timeout_enable, pamwifi->tx_thres.timeout);

	/*set DL TYPe2 TX FIFO Addr and depth, threshold*/
	if (!dl_type2_virt_addr)
		dl_type2_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth*2*sizeof(dma_addr_t),
				&dl_type2_phy_addr, GFP_KERNEL);
	if (!dl_type2_virt_addr) {
		pw_err("dl_type2_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->fifo_ops->set_tx_addr)
		pamwifi->fifo_ops->set_tx_addr(CMNFIFO_TYPE_DL_TYPE2,pamwifi->glb_base,
				dl_type2_phy_addr);
	if(pamwifi->fifo_ops->set_tx_depth)
		pamwifi->fifo_ops->set_tx_depth(CMNFIFO_TYPE_DL_TYPE2,pamwifi->glb_base,
				pamwifi->fifo_depth);

	if(pamwifi->fifo_ops->set_intr_thres)
		pamwifi->fifo_ops->set_intr_thres(CMNFIFO_TYPE_DL_TYPE2,pamwifi->glb_base,
				pamwifi->tx_thres.cnt_enable, pamwifi->tx_thres.cnt);
	if(pamwifi->fifo_ops->set_intr_timeout)
		pamwifi->fifo_ops->set_intr_timeout(CMNFIFO_TYPE_DL_TYPE2,pamwifi->glb_base,
				pamwifi->tx_thres.timeout_enable, pamwifi->tx_thres.timeout);
	/*set DL TYPe3 TX FIFO Addr and depth, threshold*/
	if (!dl_type3_virt_addr)
		dl_type3_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth*2 * sizeof(dma_addr_t),
				&dl_type3_phy_addr, GFP_KERNEL);
	if (!dl_type3_virt_addr) {
		pw_err("dl_type3_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->fifo_ops->set_tx_addr)
		pamwifi->fifo_ops->set_tx_addr(CMNFIFO_TYPE_DL_TYPE3,pamwifi->glb_base,
				dl_type3_phy_addr);
	if(pamwifi->fifo_ops->set_tx_depth)
		pamwifi->fifo_ops->set_tx_depth(CMNFIFO_TYPE_DL_TYPE3,pamwifi->glb_base,
				pamwifi->fifo_depth);
	if(pamwifi->fifo_ops->set_intr_thres)
		pamwifi->fifo_ops->set_intr_thres(CMNFIFO_TYPE_DL_TYPE3,pamwifi->glb_base,
				pamwifi->tx_thres.cnt_enable, pamwifi->tx_thres.cnt);
	if(pamwifi->fifo_ops->set_intr_timeout)
		pamwifi->fifo_ops->set_intr_timeout(CMNFIFO_TYPE_DL_TYPE3,pamwifi->glb_base,
				pamwifi->tx_thres.timeout_enable, pamwifi->tx_thres.timeout);
	/*set DL TYPe4 TX FIFO Addr and depth, threshold*/
	if (!dl_type4_virt_addr)
		dl_type4_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth*2*sizeof(dma_addr_t),
				&dl_type4_phy_addr, GFP_KERNEL);
	if (!dl_type4_virt_addr) {
		pw_err("dl_type4_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->fifo_ops->set_tx_addr)
		pamwifi->fifo_ops->set_tx_addr(CMNFIFO_TYPE_DL_TYPE4,pamwifi->glb_base,
				dl_type4_phy_addr);
	if(pamwifi->fifo_ops->set_tx_depth)
		pamwifi->fifo_ops->set_tx_depth(CMNFIFO_TYPE_DL_TYPE4,pamwifi->glb_base,
				pamwifi->fifo_depth);

	if(pamwifi->fifo_ops->set_intr_thres)
		pamwifi->fifo_ops->set_intr_thres(CMNFIFO_TYPE_DL_TYPE4,pamwifi->glb_base,
				pamwifi->tx_thres.cnt_enable, pamwifi->tx_thres.cnt);
	if(pamwifi->fifo_ops->set_intr_timeout)
		pamwifi->fifo_ops->set_intr_timeout(CMNFIFO_TYPE_DL_TYPE4,pamwifi->glb_base,
				pamwifi->tx_thres.timeout_enable, pamwifi->tx_thres.timeout);

	/*set intr dir*/
	if(pamwifi->glb_ops->set_interrup_direction)
		pamwifi->glb_ops->set_interrup_direction(pamwifi->glb_base,
				PAMWIFI_DL_FILL_TYPE1 | PAMWIFI_DL_FILL_TYPE2
				|PAMWIFI_DL_FILL_TYPE3 | PAMWIFI_DL_FILL_TYPE4,
				PAMWIFI_INTR_TO_CP);
	/*enable type1-type4 dl fill interrupt*/
	if(pamwifi->glb_ops->enable_interrup_src)
		pamwifi->glb_ops->enable_interrup_src(pamwifi->glb_base,
				PAMWIFI_DL_FILL_TYPE1 | PAMWIFI_DL_FILL_TYPE2
				|PAMWIFI_DL_FILL_TYPE3 | PAMWIFI_DL_FILL_TYPE4,
				true);

	return 0;
}

/*init dl free/ul filled/ul free common fifo*/
static int config_others_common_fifo(struct pamwifi_t *pamwifi)
{
	u32 tx_wrptr =0, tx_rdptr =0;

	if(!pamwifi || !pamwifi->fifo_ops || !pamwifi->glb_ops){
		return  -ENOMEM;
	}

	/*dl free*/
	if (!dl_free_virt_addr)
		dl_free_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth*2*sizeof(dma_addr_t),
				&dl_free_phy_addr, GFP_KERNEL);
	if (!dl_free_virt_addr) {
		pw_err("dl_free_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->fifo_ops->set_rx_addr)
		pamwifi->fifo_ops->set_rx_addr(CMNFIFO_TYPE_DL_FREE,pamwifi->glb_base,
				dl_free_phy_addr);
	if(pamwifi->fifo_ops->set_rx_depth)
		pamwifi->fifo_ops->set_rx_depth(CMNFIFO_TYPE_DL_FREE,pamwifi->glb_base,
				pamwifi->fifo_depth);

	/*config miss tx*/
	if (!miss_tx_virt_addr)
		miss_tx_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth*2 * sizeof(struct pamwifi_miss_node_tx_dscr),
				&miss_tx_phy_addr, GFP_KERNEL);
	if (!miss_tx_virt_addr) {
		pw_err("miss_tx_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->fifo_ops->set_tx_addr)
		pamwifi->fifo_ops->set_tx_addr(CMNFIFO_TYPE_DL_MISS,pamwifi->glb_base,
				miss_tx_phy_addr);
	if(pamwifi->fifo_ops->set_tx_depth)
		pamwifi->fifo_ops->set_tx_depth(CMNFIFO_TYPE_DL_MISS,pamwifi->glb_base,
				pamwifi->fifo_depth);
	/*set miss tx threshold*/
	if(pamwifi->fifo_ops->set_intr_thres)
		pamwifi->fifo_ops->set_intr_thres(CMNFIFO_TYPE_DL_MISS,pamwifi->glb_base,
				pamwifi->tx_thres.cnt_enable, pamwifi->tx_thres.cnt*4);
	if(pamwifi->fifo_ops->set_intr_timeout)
		pamwifi->fifo_ops->set_intr_timeout(CMNFIFO_TYPE_DL_MISS,pamwifi->glb_base,
				pamwifi->tx_thres.timeout_enable, pamwifi->tx_thres.timeout*100);
	/*miss rx*/
	if (!miss_rx_virt_addr)
		miss_rx_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth *2* sizeof(struct pamwifi_miss_node_rx_dscr),
				&miss_rx_phy_addr, GFP_KERNEL);
	if (!miss_rx_virt_addr) {
		pw_err("miss_rx_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->fifo_ops->set_rx_addr)
		pamwifi->fifo_ops->set_rx_addr(CMNFIFO_TYPE_DL_MISS,pamwifi->glb_base,
				miss_rx_phy_addr);
	if(pamwifi->fifo_ops->set_rx_depth)
		pamwifi->fifo_ops->set_rx_depth(CMNFIFO_TYPE_DL_MISS,pamwifi->glb_base,
				pamwifi->fifo_depth);
	/*ul filled*/
	if (!ul_rx_virt_addr)
		ul_rx_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth*2 * sizeof(dma_addr_t),
				&ul_rx_phy_addr, GFP_KERNEL);
	if (!ul_rx_virt_addr) {
		pw_err("ul_rx_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->fifo_ops->set_rx_addr)
		pamwifi->fifo_ops->set_rx_addr(CMNFIFO_TYPE_UL,pamwifi->glb_base,
				ul_rx_phy_addr);
	if(pamwifi->fifo_ops->set_rx_depth)
		pamwifi->fifo_ops->set_rx_depth(CMNFIFO_TYPE_UL,pamwifi->glb_base,
				pamwifi->fifo_depth);
	/*ul free*/
	if (!ul_tx_virt_addr)
		ul_tx_virt_addr = dma_alloc_coherent(&pamwifi->pdev->dev,
				pamwifi->fifo_depth *2* sizeof(dma_addr_t),
				&ul_tx_phy_addr, GFP_KERNEL);
	if (!ul_tx_virt_addr) {
		pw_err("ul_tx_virt_addr alloc failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->fifo_ops->set_tx_addr)
		pamwifi->fifo_ops->set_tx_addr(CMNFIFO_TYPE_UL,pamwifi->glb_base,
				ul_tx_phy_addr);
	if(pamwifi->fifo_ops->set_tx_depth)
		pamwifi->fifo_ops->set_tx_depth(CMNFIFO_TYPE_UL,pamwifi->glb_base,
				pamwifi->fifo_depth);

	/*to reset  UL tx rdptr equal with wrptr*/
	if(pamwifi->fifo_ops->get_tx_ptr)
		pamwifi->fifo_ops->get_tx_ptr(CMNFIFO_TYPE_UL, pamwifi->glb_base,
				&tx_wrptr, &tx_rdptr);
	if(pamwifi->fifo_ops->add_tx_fifo_rptr)
		pamwifi->fifo_ops->add_tx_fifo_rptr(CMNFIFO_TYPE_UL, pamwifi->glb_base,
				tx_wrptr);
	/*to reset tx rdptr*/
	if(pamwifi->fifo_ops->get_tx_ptr)
		pamwifi->fifo_ops->get_tx_ptr(CMNFIFO_TYPE_DL_FREE, pamwifi->glb_base,
				&tx_wrptr, &tx_rdptr);
	if(pamwifi->fifo_ops->add_tx_fifo_rptr)
		pamwifi->fifo_ops->add_tx_fifo_rptr(CMNFIFO_TYPE_DL_FREE,
				pamwifi->glb_base,tx_wrptr);
	/*set UL threshold and timeout*/
	if(pamwifi->fifo_ops->set_intr_thres)
		pamwifi->fifo_ops->set_intr_thres(CMNFIFO_TYPE_UL,pamwifi->glb_base,
				pamwifi->tx_thres.cnt_enable, pamwifi->tx_thres.cnt);
	if(pamwifi->fifo_ops->set_intr_timeout)
		pamwifi->fifo_ops->set_intr_timeout(CMNFIFO_TYPE_UL,pamwifi->glb_base,
				pamwifi->tx_thres.timeout_enable, pamwifi->tx_thres.timeout);

	/*set DL FREE, UL FILL, UL FREE intr dir to CP2*/
	if(pamwifi->glb_ops->set_interrup_direction)
		pamwifi->glb_ops->set_interrup_direction(pamwifi->glb_base,
				PAMWIFI_DL_FREE | PAMWIFI_UL_FILL_INT_MASK |PAMWIFI_UL_FREE_INT_MASK,
				PAMWIFI_INTR_TO_CP);
	/*set MISS TX/RX intr dir to AP*/
	if(pamwifi->glb_ops->set_interrup_direction)
		pamwifi->glb_ops->set_interrup_direction(pamwifi->glb_base,
				PAMWIFI_DL_MISS_TX | PAMWIFI_DL_MISS_RX,
				PAMWIFI_INTR_TO_AP);
	/*diable DL FREE, UL FILL,  DL MISS RX  interrupt*/
	if(pamwifi->glb_ops->enable_interrup_src)
		pamwifi->glb_ops->enable_interrup_src(pamwifi->glb_base,
				PAMWIFI_UL_FILL_INT_MASK | PAMWIFI_DL_FREE
				|PAMWIFI_DL_MISS_RX,
				false);
	/*disable MISS TX, UL FREE interrupt */
	if(pamwifi->glb_ops->enable_interrup_src)
		pamwifi->glb_ops->enable_interrup_src(pamwifi->glb_base,
				PAMWIFI_UL_FREE_INT_MASK
				|PAMWIFI_DL_MISS_TX,
				true);
	return 0;
}

static int config_4in1_comm_fifo(struct pamwifi_t *pamwifi)
{
	u64 addr;
	if(!pamwifi || !pamwifi->fifo_ops)
		return -1;

	/*set DL Free Common_Tx_FIFO_Address*/
	if(pamwifi->fifo_ops->set_tx_addr){
		addr = g_cp_cap.mux_tx_common_fifo_base_addr_l
			+ ((u64)g_cp_cap.mux_tx_common_fifo_base_addr_h <<32);
		pamwifi->fifo_ops->set_tx_addr(CMNFIFO_TYPE_DL_FREE, pamwifi->glb_base,addr);
	}
	/*set DL Free Common_Tx_FIFO depath*/
	if(pamwifi->fifo_ops->set_tx_depth)
		pamwifi->fifo_ops->set_tx_depth(CMNFIFO_TYPE_DL_FREE, pamwifi->glb_base,
				g_cp_cap.mux_tx_common_fifo_depth);
	/*DL freee tx threshold*/
	if(pamwifi->fifo_ops->set_intr_thres)
		pamwifi->fifo_ops->set_intr_thres(CMNFIFO_TYPE_DL_FREE,pamwifi->glb_base,
				pamwifi->tx_thres.cnt_enable, pamwifi->tx_thres.cnt);
	if(pamwifi->fifo_ops->set_intr_timeout)
		pamwifi->fifo_ops->set_intr_timeout(CMNFIFO_TYPE_DL_FREE,pamwifi->glb_base,
				pamwifi->tx_thres.timeout_enable, pamwifi->tx_thres.timeout);
	/*set intr dir*/
	if(pamwifi->glb_ops && pamwifi->glb_ops->set_interrup_direction)
		pamwifi->glb_ops->set_interrup_direction(pamwifi->glb_base,
				PAMWIFI_DL_FILL_4IN1,PAMWIFI_INTR_TO_CP);
	/*enable dl_fill_4in1 interrupt */
	if(pamwifi->glb_ops && pamwifi->glb_ops->enable_interrup_src)
		pamwifi->glb_ops->enable_interrup_src(pamwifi->glb_base,PAMWIFI_DL_FILL_4IN1,true);
	return 0;
}

static void config_4in1_overflow(struct pamwifi_t *pamwifi)
{
	if(!pamwifi || !pamwifi->fifo_ops || !pamwifi->glb_ops)
		return;

	/*default disable DL overflow*/
	if(pamwifi->glb_ops->enable_flow_count)
		pamwifi->glb_ops->enable_flow_count(pamwifi->glb_base,false);
	/*set 4in1 threshold to 0xff*/
	if(pamwifi->glb_ops->set_4in1_threshold)
		pamwifi->glb_ops->set_4in1_threshold(pamwifi->glb_base,0xFF);
	/*set 4in timescale*/
	if(pamwifi->glb_ops->set_rf_timescale)
		pamwifi->glb_ops->set_rf_timescale(pamwifi->glb_base,pamwifi->timescale);
}

static u32 config_common_fifo(struct pamwifi_t *pamwifi)
{
	if(!pamwifi){
		return -1;
	}
	if(pamwifi->tx_4in1_en){
		config_4in1_comm_fifo(pamwifi);
		config_4in1_overflow(pamwifi);
	}else{
		config_type1_4_common_fifo(pamwifi);
	}
	config_others_common_fifo(pamwifi);
	return 0;
}


static u32 config_pamwifi(struct pamwifi_t *pamwifi)
{
	if(!pamwifi || !pamwifi->glb_ops){
		return -1;
	}
	/*config 4in1 or type1-4 mode*/
	if(pamwifi->glb_ops->set_4in1_mode)
		pamwifi->glb_ops->set_4in1_mode(pamwifi->glb_base, pamwifi->tx_4in1_en);
	/*index search depth*/
	/*config index search table depth*/
	if(pamwifi->glb_ops->set_router_table_depth)
		pamwifi->glb_ops->set_router_table_depth(pamwifi->glb_base,pamwifi->search_table_depth);
	/*set dl net_id/dst_id*/
	if(pamwifi->glb_ops->set_dl_netid)
		pamwifi->glb_ops->set_dl_netid(pamwifi->glb_base,0x000);
	if(pamwifi->glb_ops->set_dl_dstid)
		pamwifi->glb_ops->set_dl_dstid(pamwifi->glb_base,SIPA_TERM_WIFI);
	/*set watermark*/
	if(pamwifi->glb_ops->set_buffer_watermark)
		pamwifi->glb_ops->set_buffer_watermark(pamwifi->glb_base,&pamwifi->watermark);
	/*set ddr mapping offset addr, match REG_PAM_WIFI_CFG_DL_FILLED_BUFFER_CTRL*/
	if(pamwifi->glb_ops->set_ul_free_mem_offset)
		pamwifi->glb_ops->set_ul_free_mem_offset(pamwifi->glb_base,0x8000000000);
	if(pamwifi->glb_ops->set_ul_filled_mem_offset)
		pamwifi->glb_ops->set_ul_filled_mem_offset(pamwifi->glb_base,0x8000000000);
	if(pamwifi->glb_ops->set_dl_free_mem_offset)
		pamwifi->glb_ops->set_dl_free_mem_offset(pamwifi->glb_base,0x8000000000);
	if(pamwifi->glb_ops->set_dl_filled_mem_offset)
		pamwifi->glb_ops->set_dl_filled_mem_offset(pamwifi->glb_base,0x8000000000);
	/*Configure the msdu_header_buf table*/
	if (!pam_wifi_msdu_header_info)
		pam_wifi_msdu_header_info = dma_alloc_coherent(&pamwifi->pdev->dev, 8*4*16,
				&term_pam_wifi_msdu_header_buf,
				GFP_KERNEL);
	if (!pam_wifi_msdu_header_info) {
		pw_err("term_pam_wifi_msdu_header_buf alloc buffer failed!\n");
		return -ENOMEM;
	}
	if(pamwifi->glb_ops->set_msdu_base_addr)
		pamwifi->glb_ops->set_msdu_base_addr(pamwifi->glb_base, term_pam_wifi_msdu_header_buf);
	/*set ul ipa node info*/
	if(pamwifi->glb_ops->set_ul_node)
		pamwifi->glb_ops->set_ul_node(pamwifi->glb_base, &pamwifi->ul_node);
	return 0;
}

//baokun: TODO 128*128, da?,
/**
 *  sprd_pamwifi_update_router_table - setup pamwifi's route table(search index table)
 * 	@priv: wifi driver information and private struct,
 *    @sta_lut: the route table information get from cp2
 *	@vif_mode: wifi mode as: ap, p2p, station, etc.
 *    @index: the index of table
 *    @add: 1: update or add, 0:delete a node of route table
 *
 *	Note : Only after this function be called, the data transmit from ipa to pamwifi
 */
void sprd_pamwifi_update_router_table(struct sprd_priv *priv,
		void  *data,	u8 vif_mode, u32 index, int add)
{
	struct sprd_vif *vif;
	struct pamwifi_route_table_node node;
	struct evt_sta_lut_ind *sta_lut;

	if(!g_hw_supported ||  !g_cp_cap.cp_pam_wifi_support){
              pw_alert("%s pamwifi disalbed !\n", __func__);
		return;
	}
	mutex_lock(&pamwifi_mutex);
	if(!data || !g_pamwifi || !g_pamwifi->glb_ops || !g_pamwifi->fifo_ops){
		mutex_unlock(&pamwifi_mutex);
		return;
	}
	vif = sprd_mode_to_vif(priv, vif_mode);
	if (!vif) {
		pw_err("%s cant't get vif", __func__);
		return;
	}

	/*check ctx_id*/
	sta_lut = (struct evt_sta_lut_ind *)data;
	if (vif->ctx_id != sta_lut->ctx_id) {
		pw_err("ctx_id do not match!\n");
		mutex_unlock(&pamwifi_mutex);
		sprd_put_vif(vif);
		return;
	}
	sprd_put_vif(vif);
	if(sta_lut->sta_lut_index <6){
		mutex_unlock(&pamwifi_mutex);
		return;
	}
	poweron(g_pamwifi,true);
	if(g_pamwifi->glb_ops->lock_router_table(g_pamwifi->glb_base))
		g_pamwifi->glb_ops->lock_router_table(g_pamwifi->glb_base);


	memset(&node, 0x00, sizeof(node));
	node.sta_lut_index = sta_lut->sta_lut_index;
	node.ctx_id = sta_lut->ctx_id;
	node.index = index;
	pw_info("sprdwl_pamwifi_update_router_table vif_mode %d index %d add %d \
		    ctx_id %d sta_lut_index =%d ra: %x:%x:%x:%x:%x:%x\n",
			vif_mode, index, add,node.ctx_id, node.sta_lut_index,
			sta_lut->ra[0], sta_lut->ra[1], sta_lut->ra[2],
			sta_lut->ra[3], sta_lut->ra[4], sta_lut->ra[5]);

	if (add == 1) {
		/*DL set sa =1, marlin can distinguish pice address*/
		//node.sa[0] =1;
		memcpy(node.da, sta_lut->ra, sizeof(node.da));
		memcpy(&g_pamwifi->router_table[node.sta_lut_index -6], &node, sizeof(g_pamwifi->router_table[node.sta_lut_index-6]));
	}else{
		/*delete router table*/
		memset(&g_pamwifi->router_table[node.sta_lut_index-6], 0x00, sizeof(g_pamwifi->router_table[node.sta_lut_index-6]));
	}

	if(g_pamwifi->fifo_ops->update_route_table)
		g_pamwifi->fifo_ops->update_route_table(g_pamwifi->glb_base, &node, add);
	//dump_pamwifiram_seachtable();
	if(g_pamwifi->glb_ops->unlock_router_table(g_pamwifi->glb_base))
		g_pamwifi->glb_ops->unlock_router_table(g_pamwifi->glb_base);
	mutex_unlock(&pamwifi_mutex);
}

static void ac_msdu_init(struct pamwifi_t *pamwifi, u8 msdu_index)
{
	struct tx_msdu_dscr  *tmp_msdu_header;
	unsigned char *msdu_dscr;
	int i = 0, j = 0;

	if(!pamwifi || !pamwifi->glb_ops){
		return;
	}
	if (!pam_wifi_msdu_header_info) {
		pw_err("pamwifi msdu dma_alloc_coherent err\n");
		return;
	}

	if(pamwifi->glb_ops->set_ac_ax_mode)
		pamwifi->glb_ops->set_ac_ax_mode(pamwifi->glb_base, PAMWIFI_AC_MODE);

	msdu_dscr = (unsigned char *)pam_wifi_msdu_header_info;
	memset((u8 *)msdu_dscr + MSDU_HEADER_LEN * msdu_index, 0x00, MSDU_HEADER_LEN);
	tmp_msdu_header = (struct tx_msdu_dscr *)(msdu_dscr + 5);

	((struct tx_msdu_dscr  *)((char *)tmp_msdu_header + (i * 4 + j) * MSDU_HEADER_LEN))->common.type = SPRDWL_TYPE_DATA;
	((struct tx_msdu_dscr  *)((char *)tmp_msdu_header + (i * 4 + j) * MSDU_HEADER_LEN))->offset = DSCR_LEN;
	((struct tx_msdu_dscr  *)((char *)tmp_msdu_header + (i * 4 + j) * MSDU_HEADER_LEN))->tx_ctrl.pcie_mh_readcomp = 1;
}

static void ax_msdu_init(struct pamwifi_t *pamwifi, u8 msdu_index)
{
	struct ax_tx_msdu_dscr  *tmp_msdu_header;
	unsigned char *msdu_dscr;
	int i = 0, j = 0;

	if (!pam_wifi_msdu_header_info) {
		pw_err("pamwifi msdu dma_alloc_coherent err\n");
		return;
	}
	if(!pamwifi || !pamwifi->glb_ops){
		return;
	}

	if(pamwifi->glb_ops->set_ac_ax_mode)
		pamwifi->glb_ops->set_ac_ax_mode(pamwifi->glb_base, PAMWIFI_AX_MODE);

	msdu_dscr = (unsigned char *)pam_wifi_msdu_header_info;
	memset((u8 *)msdu_dscr + MSDU_HEADER_LEN * msdu_index, 0x00, MSDU_HEADER_LEN);
	tmp_msdu_header = (struct ax_tx_msdu_dscr *)(msdu_dscr + 5);

	((struct ax_tx_msdu_dscr  *)((char *)tmp_msdu_header + (i * 4 + j) * MSDU_HEADER_LEN))->common.type = SPRDWL_TYPE_DATA;
	((struct ax_tx_msdu_dscr  *)((char *)tmp_msdu_header + (i * 4 + j) * MSDU_HEADER_LEN))->offset = DSCR_LEN;
	((struct ax_tx_msdu_dscr  *)((char *)tmp_msdu_header + (i * 4 + j) * MSDU_HEADER_LEN))->tx_ctrl.pcie_mh_readcomp = 1;

}

/**
 *  read_miss_tx_fifodscr - read dl miss tx common fifo decriptor
 *
 *	Note :This function will read all of dl miss tx common fifo decriptor
 *             to busy_list.
 *             After read, this decriptors must be write to dl miss tx common fifo
 *
 */
static int read_miss_tx_fifodscr(struct pamwifi_t *pamwifi, u32 fifo_depth)
{
	u32 wrptr=0, rdptr=0, read_count=0, oldwptr=0;
	int i;
	struct sprdwl_pamwifi_msg_buf *pamwifi_msg_buf = NULL;
	unsigned long flags = 0;

	if(!pamwifi || !pamwifi->fifo_ops){
		return -1;
	}
	if(pamwifi->fifo_ops->get_tx_ptr)
		pamwifi->fifo_ops->get_tx_ptr(CMNFIFO_TYPE_DL_MISS,
				pamwifi->glb_base, &wrptr, &rdptr);
	oldwptr = wrptr;
	wrptr = wrptr % fifo_depth;
	rdptr = rdptr % fifo_depth;
	/*read actions: read sections not exceed wirte sections, read sections:
	  between rdptr and wrptr*/
	if (wrptr >= rdptr) {
		read_count = wrptr - rdptr;
		for (i = 0; i < read_count; i++) {
			spin_lock_irqsave(&pamwifi_spinlock, flags);
			if (!list_empty(&pamwifi->msglist->freelist)) {
				pamwifi_msg_buf = list_first_entry(&pamwifi->msglist->freelist,
						struct sprdwl_pamwifi_msg_buf, list);
				list_del(&pamwifi_msg_buf->list);
			} else {
				pw_err("no more miss buffer\n");
				BUG_ON(1);
			}
			spin_unlock_irqrestore(&pamwifi_spinlock, flags);
			memcpy(&pamwifi_msg_buf->dscr, (struct pamwifi_miss_node_tx_dscr*)miss_tx_virt_addr + rdptr + i,
					sizeof(struct pamwifi_miss_node_tx_dscr));
			spin_lock_irqsave(&pamwifi_spinlock, flags);
			if (list_empty(&pamwifi->msglist->busylist))
				INIT_LIST_HEAD(&pamwifi->msglist->busylist);
			list_add_tail(&pamwifi_msg_buf->list, &pamwifi->msglist->busylist);
			atomic_inc(&pamwifi->msglist->busylist_count);
			spin_unlock_irqrestore(&pamwifi_spinlock, flags);
		}
	}else if (wrptr < rdptr) {
		/*wptr from fifo bottom to fifo header, so read sections include:
		  1)between rptr and fifo bottom
		  2) between fifo header and wptr location*/

		/*first read sections  1)between rptr and fifo bottom*/
		read_count = fifo_depth - rdptr;
		for (i = 0; i < read_count; i++) {
			spin_lock_irqsave(&pamwifi_spinlock, flags);
			if (!list_empty(&pamwifi->msglist->freelist)) {
				pamwifi_msg_buf = list_first_entry(&pamwifi->msglist->freelist,
						struct sprdwl_pamwifi_msg_buf, list);
				list_del(&pamwifi_msg_buf->list);
			} else {
				pw_err("no more miss buffer\n");
				BUG_ON(1);
			}
			spin_unlock_irqrestore(&pamwifi_spinlock, flags);
			memcpy(&pamwifi_msg_buf->dscr, (struct pamwifi_miss_node_tx_dscr*)miss_tx_virt_addr + rdptr + i,
					sizeof(struct pamwifi_miss_node_tx_dscr));
			spin_lock_irqsave(&pamwifi_spinlock, flags);
			if (list_empty(&pamwifi->msglist->freelist))
				INIT_LIST_HEAD(&pamwifi->msglist->busylist);
			list_add_tail(&pamwifi_msg_buf->list, &pamwifi->msglist->busylist);
			atomic_inc(&pamwifi->msglist->busylist_count);
			spin_unlock_irqrestore(&pamwifi_spinlock, flags);
		}

		/*Then read sections 2) between fifo header and wptr */
		read_count = wrptr;
		for (i = 0; i < read_count; i++) {
			spin_lock_irqsave(&pamwifi_spinlock, flags);
			if (!list_empty(&pamwifi->msglist->freelist)) {
				pamwifi_msg_buf = list_first_entry(&pamwifi->msglist->freelist,
						struct sprdwl_pamwifi_msg_buf, list);
				list_del(&pamwifi_msg_buf->list);
			} else {
				pw_err("no more miss buffer\n");
				//BUG_ON(1);
			}
			spin_unlock_irqrestore(&pamwifi_spinlock, flags);
			memcpy(&pamwifi_msg_buf->dscr, (struct pamwifi_miss_node_tx_dscr*)miss_tx_virt_addr + i,
					sizeof(struct pamwifi_miss_node_tx_dscr));
			spin_lock_irqsave(&pamwifi_spinlock, flags);
			if (list_empty(&pamwifi->msglist->freelist))
				INIT_LIST_HEAD(&pamwifi->msglist->busylist);
			list_add_tail(&pamwifi_msg_buf->list, &pamwifi->msglist->busylist);
			atomic_inc(&pamwifi->msglist->busylist_count);
			spin_unlock_irqrestore(&pamwifi_spinlock, flags);
		}
	}

	/*rellcate  rptr to wptr: rptr = wptr*/
	rdptr = oldwptr % (2 * fifo_depth);
	if(pamwifi->fifo_ops->get_tx_ptr)
		pamwifi->fifo_ops->add_tx_fifo_rptr(CMNFIFO_TYPE_DL_MISS, pamwifi->glb_base,
				rdptr);
	return 0;

}

irqreturn_t pamwifi_irq_handle(int irq, void *dev)
{
	u32 int_sts =0, cmn_fifo_intr_sts =0, src;
	u32 int_sts_log =0, cmn_fifo_intr_sts_log  =0;

	if(!g_pamwifi || !g_pamwifi->glb_ops || !g_pamwifi->fifo_ops){
		return IRQ_HANDLED;
	}
	if(g_pamwifi->glb_ops->get_interrup_status_src)
		int_sts = g_pamwifi->glb_ops->get_interrup_status_src(g_pamwifi->glb_base);
	int_sts_log = int_sts;
	int_sts = int_sts & BIT(9);
	if(g_pamwifi->fifo_ops->get_fifo_int_sts)
		cmn_fifo_intr_sts = g_pamwifi->fifo_ops->get_fifo_int_sts(CMNFIFO_TYPE_DL_MISS ,g_pamwifi->glb_base);
	cmn_fifo_intr_sts_log = cmn_fifo_intr_sts;
	cmn_fifo_intr_sts = cmn_fifo_intr_sts & 0x300l;
	pw_err("%s, %d,  sts 0x%x :0x%x common sts 0x%x : 0x%x enter!\n", __func__, __LINE__, int_sts_log,int_sts ,cmn_fifo_intr_sts_log, cmn_fifo_intr_sts);
	if (int_sts != 0 &&  cmn_fifo_intr_sts != 0) {
		//get_fifonode_dscr(g_pamwifi, 1024);
		read_miss_tx_fifodscr(g_pamwifi, PAMWIFI_COMN_FIFO_DEPTH);
		schedule_work(&miss_tx_worker);
		/*Tx_FIFO_interrupt_threshold_clr, Tx_FIFO_interrupt_delay_timer_clr*/
		if(g_pamwifi->fifo_ops->clr_tx_fifo_intr)
			g_pamwifi->fifo_ops->clr_tx_fifo_intr(CMNFIFO_TYPE_DL_MISS,
					g_pamwifi->glb_base,
					PAMWIFI_TX_FIFO_DELAY_TIMER
					|PAMWIFI_TX_FIFO_THRESHOLD);
		/*clear miss rx, miss rx interrupt sts*/
		src = PAMWIFI_DL_MISS_TX | PAMWIFI_4IN1_TYPE1_OVERFLOW
		         |PAMWIFI_4IN1_TYPE2_OVERFLOW
		         |PAMWIFI_4IN1_TYPE3_OVERFLOW
		         |PAMWIFI_4IN1_TYPE1_OVERFLOW;
		if(g_pamwifi->glb_ops->clr_interrup_src)
			g_pamwifi->glb_ops->clr_interrup_src(g_pamwifi->glb_base, src);
	} else {
		pw_err("int_sts:%lu, cmn_fifo_intr_sts:%lu, intr err!\n", int_sts, cmn_fifo_intr_sts);
	}
	return IRQ_HANDLED;
}


struct sprd_vif *find_ul_vif(struct sk_buff *skb)
{
	struct sprd_hif *intf = sc2355_get_hif();
	struct net_device *net;
	int i, j;

	for (i = 0; i < MAX_LUT_NUM; i++) {
		if (0 == memcmp(intf->peer_entry[i].tx.sa, skb->data, ETH_ALEN)) {
			return intf->peer_entry[i].vif;
		} else if (0 == memcmp(intf->peer_entry[i].tx.da, skb->data + 6, ETH_ALEN)) {
			net = intf->peer_entry[i].vif->ndev;
			if(!net)
				return NULL;
			for (j = 0; j < MAX_LUT_NUM; j++) {
				if (0 == memcmp(intf->peer_entry[j].tx.da, skb->data, ETH_ALEN)) {
					__pamwifi_xmit_to_ipa(skb, net);
					return NULL;
				}
			}
			return intf->peer_entry[i].vif;
		}
	}
	//sprdwl_hex_dump("vif is null, dump skb:", skb->data, 100);
	dev_kfree_skb(skb);
	return NULL;
}

/*retrieve miss node buffer to ipa*/
static int retrieve_misstxbuf2ipa(struct pamwifi_miss_node_rx_dscr *dscr, u32 index,
		u32 wrptr, u32 rdptr, u32 fifo_depth)
{
	if ((wrptr + index + 1) > fifo_depth){
		memcpy((struct pamwifi_miss_node_rx_dscr*)miss_rx_virt_addr + wrptr + index - fifo_depth, dscr,
				sizeof(struct pamwifi_miss_node_rx_dscr));
	}
	else{
		memcpy((struct pamwifi_miss_node_rx_dscr*)miss_rx_virt_addr + wrptr + index, dscr,
				sizeof(struct pamwifi_miss_node_rx_dscr));
	}
	return 0;
}

/**
 *  process_miss_tx_fifodscr - write the miss tx and overflow tx node to dl miss rx common fifo
 *
 *	Note :This function will write all of common fifo decriptors in busy_list to dl miss rx common fifo.
 *             After write, thelse decriptors will be return to ipa.
 *
 */
static void process_miss_tx_fifodscr(struct pamwifi_t *pamwifi)
{
	struct pamwifi_miss_node_rx_dscr rx_node;
	struct sprdwl_pamwifi_msg_buf *pamwifi_msg_buf = NULL;
	unsigned long i;
	int num, free_num=0;
	u32 rdptr=0, wrptr=0,oldwrptr=0;

	unsigned long flags = 0;
	if(!pamwifi || !pamwifi->fifo_ops){
		return;
	}
	if (pamwifi->suspend_stage & PAMWIFI_EB_SUSPEND) {
		pw_err("%s, Pam wifi already disabled!", __func__);
		return;
	}
	num = atomic_read(&pamwifi->msglist->busylist_count);
	if(pamwifi->fifo_ops->get_rx_ptr)
		pamwifi->fifo_ops->get_rx_ptr(CMNFIFO_TYPE_DL_MISS, pamwifi->glb_base,
				&wrptr, &rdptr);
	else
		return;

	pw_debug(" %s wrptr %d rdptr %d num %d\n",__func__,wrptr, rdptr, num);
	if (num <= 0)
		return;
	oldwrptr = wrptr;
	wrptr = wrptr % pamwifi->fifo_depth;
	rdptr = rdptr % pamwifi->fifo_depth;

	/*r*/
	if (wrptr >= rdptr) {
		free_num = rdptr + pamwifi->fifo_depth - wrptr;
	} else if (wrptr < rdptr) {
		free_num = rdptr - wrptr;
	}
	if (free_num <= 0){
		pw_err(" %s free_num =%d no free space in fifo !!\n",__func__, free_num);
		return;
	}
	if (num < free_num)
		free_num = num;

	for(i = 0; i < free_num; i++){
		spin_lock_irqsave(&pamwifi_spinlock, flags);
		if (!list_empty(&pamwifi->msglist->busylist)) {
			pamwifi_msg_buf = list_first_entry(&pamwifi->msglist->busylist,
					struct sprdwl_pamwifi_msg_buf, list);
			list_del(&pamwifi_msg_buf->list);
			atomic_dec(&pamwifi->msglist->busylist_count);
			list_add_tail(&pamwifi_msg_buf->list, &pamwifi->msglist->freelist);
			spin_unlock_irqrestore(&pamwifi_spinlock, flags);
			rx_node.address = pamwifi_msg_buf->dscr.address;
			rx_node.length = pamwifi_msg_buf->dscr.length;
			rx_node.offset = pamwifi_msg_buf->dscr.offset;
			rx_node.src_id = pamwifi_msg_buf->dscr.src_id;
			rx_node.tos = pamwifi_msg_buf->dscr.tos;
			rx_node.flag = pamwifi_msg_buf->dscr.flag;
		        retrieve_misstxbuf2ipa(&rx_node, i, wrptr, rdptr, pamwifi->fifo_depth);
		}else
			spin_unlock_irqrestore(&pamwifi_spinlock, flags);
		/*free miss node*/
		//retrieve_misstxbuf2ipa(&rx_node, i, wrptr, rdptr, pamwifi->fifo_depth);
	}
	wrptr = (oldwrptr + free_num) % ( pamwifi->fifo_depth * 2);
	if(pamwifi->fifo_ops->add_rx_fifo_wptr)
		pamwifi->fifo_ops->add_rx_fifo_wptr(CMNFIFO_TYPE_DL_MISS,
				pamwifi->glb_base,wrptr);

	if (num > free_num) {
		mdelay(100);
		pw_debug(" %s reentry num %d free_num  %d\n",__func__, num, free_num);
		process_miss_tx_fifodscr(pamwifi);
	}
}


//baokun TODO:ÎªÊ²Ã´ÒªpamwifiÀ´×öå£?
int sprdwl_pamwifi_recv_skb(struct notifier_block *nb,
		unsigned long data, void *ptr)
{
	struct sprd_vif *vif;
	struct iphdr *iph;
	struct ipv6hdr *ipv6h;
	struct ethhdr *ethh;
	struct net_device *net;
	unsigned int real_len = 0;
	unsigned int payload_len = 0;
	bool ip_arp = true;
	struct sk_buff *skb = (struct sk_buff *)ptr;


	vif = find_ul_vif(skb);
	if (!vif)
		return -1;
	net = vif->ndev;
	if (!net) {
		//sprdwl_hex_dump("net_dev is null, dump skb:", skb->data, 100);
		dev_kfree_skb(skb);
		return -1;
	}

	skb_reset_mac_header(skb);
	ethh = eth_hdr(skb);

	skb->protocol = eth_type_trans(skb, net);
	skb_reset_network_header(skb);

	switch (ntohs(ethh->h_proto)) {
		case ETH_P_IP:
			iph = ip_hdr(skb);
			real_len = ntohs(iph->tot_len);
			break;
		case ETH_P_IPV6:
			ipv6h = ipv6_hdr(skb);
			payload_len = ntohs(ipv6h->payload_len);
			real_len = payload_len + sizeof(struct ipv6hdr);
			break;
		case ETH_P_ARP:
			real_len = arp_hdr_len(net);
			break;
		default:
			ip_arp = false;
			pw_debug("skb %p is neither v4 nor v6 nor arp\n", skb);
			break;
	}

	/* resize the skb->len to a real one */
	if (ip_arp)
		skb_trim(skb, real_len);

	/* TODO chechsum ... */
	skb->ip_summed = CHECKSUM_NONE;

	/*count rx packets*/
	net->stats.rx_packets++;
	net->stats.rx_bytes += skb->len;

	netif_receive_skb(skb);
	//pw_info("%s, skb->len %d total len %lu \n", __func__,skb->len, net->stats.rx_bytes);
	return 0;
}

static struct notifier_block wifi_recv_skb = {
	.notifier_call = sprdwl_pamwifi_recv_skb,
};
static void dl_flowctrl_handler(int flowctrl)
{
	struct sprd_hif *intf = sc2355_get_hif();
	int i;

	pw_info("dl_flowctrl_handler intf 0x%llx, flowctrl %d\n", intf, flowctrl);
	if(intf == NULL){
		pw_err("%s null \n", __func__);
		return;
	}
	for (i = 0; i < MAX_LUT_NUM; i++) {
		if (!intf->peer_entry[i].vif)
			continue;

		if (flowctrl) {
			netif_stop_queue(intf->peer_entry[i].vif->ndev);
		} else if (netif_queue_stopped(intf->peer_entry[i].vif->ndev)) {
			netif_wake_queue(intf->peer_entry[i].vif->ndev);
		}
	}
}

void pamwifi_dl_rm_notify_cb(void *priv, enum sipa_evt_type evt,
		unsigned long data)
{
	pr_alert("priv= 0x%llx, eve %d, data 0x%lld \n", priv, evt, data);
	switch (evt) {
		case SIPA_LEAVE_FLOWCTRL:
			pw_info("sipa_wifi SIPA LEAVE FLOWCTRL\n");
			dl_flowctrl_handler(0);
			break;
		case SIPA_ENTER_FLOWCTRL:
			pw_info("sipa_wifi SIPA ENTER FLOWCTRL\n");
			dl_flowctrl_handler(1);
			break;
		default:
			break;
	}
}

static void ul_res_add_wq(struct sprd_vif *vif, u8 flag)
{

	struct sprdwl_pamwifi_ul_status ul_sts;

	pw_info("%s, %d, flag: %u\n", __func__, __LINE__, flag);
	ul_sts.sub_type = flag;
	ul_sts.value = 1;
	sc2355_rx_send_cmd(&vif->priv->hif, (void*)(&ul_sts), sizeof(ul_sts), SPRD_WORK_UL_RES_STS_CMD, vif->ctx_id);
}

/**
 *  sprd_pamwifi_ul_resource_event - accept the cp2 EVT_PAMWIFI_UL_RESOURCE_EVENT and send response to cp2
 * 	@vif: wifi driver information and private struct,
 *    @data: 1: keep request; 0:relase request
 *	@len: data len
 *
 *	Note : UL resource management, when request, it cannot suspend.
 */
void sprd_pamwifi_ul_resource_event(struct sprd_vif *vif, u8 *data, u16 len)
{
	//struct sprd_priv *priv = vif->priv;
	u8 flag;
	int ret = 0;

	if(!g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
              pw_alert("%s pamwifi disalbed !\n", __func__);
		return;
	}
	pw_info("%s, %d \n", __func__, __LINE__);
	if (vif->state != VIF_STATE_OPEN)
		return;

	mutex_lock(&pamwifi_mutex);

	memcpy(&flag, data, sizeof(u8));
	if (flag == 1 && g_pamwifi->ul_resource_flag == 0){
		mutex_unlock(&pamwifi_mutex);

		ret = sipa_rm_request_resource(SIPA_RM_RES_CONS_WIFI_UL);
		mutex_lock(&pamwifi_mutex);
	}
	else if (flag == 0 && g_pamwifi->ul_resource_flag == 1){
		mutex_unlock(&pamwifi_mutex);
		ret = sipa_rm_release_resource(SIPA_RM_RES_CONS_WIFI_UL);
		mutex_lock(&pamwifi_mutex);
	}

	if (!ret && flag == 1) {
		pw_err("%s, %d\n", __func__, __LINE__);
		g_pamwifi->ul_resource_flag = flag;
		ul_res_add_wq(vif, flag);
	}
	mutex_unlock(&pamwifi_mutex);
}

/**
 *  sprd_pamwifi_send_ul_res_cmd - send resource mgt response to cp2
 * 	@priv: wifi driver information and private struct,
 *    @vif_ctx_id: content id from cp2
 *    @data: 1: keep request; 0:relase request
 *	@len: data len
 *
 *	Note : UL resource management, when request, it cannot suspend.
 */
int sprd_pamwifi_send_ul_res_cmd(struct sprd_priv *priv, u8 vif_ctx_id,
		void *data, u16 len)
{
	struct sprd_msg *msg = NULL;
	struct sprd_vif *vif;

	if(!g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
              pw_alert("%s pamwifi disalbed !\n", __func__);
		return PAMWIFI_DISABLED;
	}
	vif = sc2355_ctxid_to_vif(priv, vif_ctx_id);
	if (!vif) {
		pw_err("%s, can't get vif\n", __func__);
		return -EIO;
	}

	msg = sc2355_get_cmdbuf(priv, vif, len, CMD_UL_RES_STS, SPRD_HEAD_RSP, GFP_KERNEL);
	sprd_put_vif(vif);
	if (!msg)
		return -ENOMEM;
	memcpy(msg->data, data, len);
	return sc2355_send_cmd_recv_rsp(priv, msg, NULL, NULL, CMD_WAIT_TIMEOUT);
}

static void pamwifi_ul_rm_notify_cb(void *user_data,
		enum sipa_rm_event event,
		unsigned long data)
{
	struct sprd_vif *vif = (struct sprd_vif *)user_data;

	pw_info("%s: event %d\n", __func__, event);

	if (!g_pamwifi){
		return;
	}

	if (!vif || !(vif->priv)){
		return;
	}

	//if(vif->state != VIF_STATE_OPEN) return;

	switch (event) {
		case SIPA_RM_EVT_GRANTED:
			if (g_pamwifi->ul_resource_flag == 0) {
				g_pamwifi->ul_resource_flag = 1;
				ul_res_add_wq(vif, 1);
			}
			break;
		case SIPA_RM_EVT_RELEASED:
			if (g_pamwifi->ul_resource_flag == 1) {
				g_pamwifi->ul_resource_flag = 0;
				ul_res_add_wq(vif, 0);
			}
			break;
		default:
			pw_info("%s: unknown event %d\n", __func__, event);
			break;
	}
}

static void prepare_suspend(struct pamwifi_t *pamwifi)
{
	u32 value, timeout = 500;

	if (!pamwifi)
		return;

	if (pamwifi->suspend_stage & PAMWIFI_EB_SUSPEND) {
		pw_err("%s, Pam wifi already disabled!", __func__);
		return;
	}
	pw_info("%s, enter!\n", __func__);
	if (!(pamwifi->suspend_stage & PAMWIFI_REG_SUSPEND)) {
		sipa_disconnect(SIPA_EP_WIFI, SIPA_DISCONNECT_START);
		value = check_pamwifi_ipa_fifo_status(pamwifi);
		pw_info("%s, Start to close Pam wifi!\n", __func__);
		while(value) {
			if (!timeout--) {
				pw_err("Pam wifi close fail!\n");
				break;
			}
			pw_err("Pam wifi closing!\n");
			usleep_range(10, 15);
			value = check_pamwifi_ipa_fifo_status(pamwifi);
		}
		/*stop pam wifi*/
		if(pamwifi->glb_ops->stop){
			pamwifi->glb_ops->stop(pamwifi->glb_base, PW_START_ALL);
			pamwifi->suspend_stage |= PAMWIFI_REG_SUSPEND;
		}
		sipa_disconnect( SIPA_EP_WIFI, SIPA_DISCONNECT_END);
		pamwifi->suspend_stage |= PAMWIFI_REG_SUSPEND;
		/*pamwifi not idle, can not power off*/
		if (value)
			return;
	}

	poweron(pamwifi, false);
	pamwifi->suspend_stage |= PAMWIFI_EB_SUSPEND;

}

static int reinit(struct pamwifi_t *pamwifi)
{
	int ret =-1,i;
	/*init ipa for pamwifi*/
	if(!pamwifi){
		return ret;
	}

	/*init ipa for pamwifi*/
	config_ipa(pamwifi);
	ret = sipa_pam_connect(&pamwifi->sipa_params);
	if (ret) {
		pw_err("%s, pamwifi connect ipa fail\n", __func__);
		return ret;
	}
	//load_pamwifi_cfg(g_pamwifi);
	config_pamwifi(pamwifi);
	config_common_fifo(pamwifi);

	/*init msdu dscr*/
	if (!g_cp_cap.chip_ver)
		ac_msdu_init(pamwifi, 0);
	else
		ax_msdu_init(pamwifi, 0);

	/*try 2 ipi mode, because marlin3 only support 1ipi*/
	config_ipi(pamwifi);

	/*recovery router table, now max_lut_index 32, so search depth is 16*/
	for (i = 0; i < PAMWIFI_MAX_LUT_LEN; i++) {
		if(pamwifi->fifo_ops->update_route_table)
			pamwifi->fifo_ops->update_route_table(pamwifi->glb_base, &pamwifi->router_table[i],true);
	}

	return ret;
}

static int prepare_resume(struct pamwifi_t *pamwifi)
{
	int ret = 0;

	if(!pamwifi || !pamwifi->glb_ops){
		goto out;
	}
	pw_err("%s, %d, stage: %u\n", __func__, __LINE__, pamwifi->suspend_stage);

	if (!(pamwifi->suspend_stage & PAMWIFI_EB_SUSPEND))
		goto out;

	if (pamwifi->suspend_stage & PAMWIFI_EB_SUSPEND) {
		poweron(pamwifi, true);
		pamwifi->suspend_stage &= ~PAMWIFI_EB_SUSPEND;
	}

	if (pamwifi->suspend_stage & PAMWIFI_REG_SUSPEND) {
		ret = reinit(pamwifi);
		if (ret)
			goto out;
		if(pamwifi->glb_ops->start)
			pamwifi->glb_ops->start(pamwifi->glb_base, PW_START_ALL);

		pamwifi->suspend_stage &= ~PAMWIFI_REG_SUSPEND;
	}

out:
	//pw_err("%s, %d, stage: %u\n", __func__, __LINE__, pamwifi->suspend_stage);
	sipa_rm_notify_completion(SIPA_RM_EVT_GRANTED,
			SIPA_RM_RES_PROD_PAM_WIFI);

	return ret;
}

static void miss_tx_handler(struct work_struct *work)
{
	process_miss_tx_fifodscr(g_pamwifi);
}

void sprdwl_pamwifi_power_work(struct work_struct *work)
{
	mutex_lock(&pamwifi_mutex);

	if(g_pamwifi){
		if (g_power_status) {
			/*pam_wifi resume*/
			if (prepare_resume(g_pamwifi)) {
				pw_err("%s, pamwifi resume fail, resume again\n");
				queue_delayed_work(g_power_wq,
						&g_pamwifi->power_work, msecs_to_jiffies(200));
			}
		} else {
			/*pam_wifi suspend*/
			prepare_suspend(g_pamwifi);
		}
	}
	mutex_unlock(&pamwifi_mutex);
}

static int pamwifi_req_res(void *vif)
{
	if(!g_pamwifi){

		return -1;
	}
	g_power_status = true;
	pw_err("%s, %d\n", __func__, __LINE__);
	cancel_delayed_work(&g_pamwifi->power_work);
	//flush_workqueue(g_power_wq);
	queue_delayed_work(g_power_wq, &g_pamwifi->power_work, 0);
	return 0;
}

static int pamwifi_rel_res(void *vif)
{
#if 0
	if(!g_pamwifi){
		return -1;
	}
	if(!g_pamwifi->enabled){
		pw_err("%s, pamwifi not enable\n", __func__);
		return 0;
	}
	g_power_status = false;
	pw_err("%s, %d\n", __func__, __LINE__);
	cancel_delayed_work(&g_pamwifi->power_work);
	//flush_workqueue(g_power_wq);
	queue_delayed_work(g_power_wq, &g_pamwifi->power_work, 0);
#endif
	return 0;
}


static int sprdwl_pamwifi_res_init(struct pamwifi_t *pamwifi, struct sprd_vif *vif)
{
	struct sipa_rm_create_params rm_params;
	int ret;

	/*UL*/
	if(!pamwifi){
		return -1;
	}

	pamwifi->ul_param.user_data = vif;
	pamwifi->ul_param.notify_cb = pamwifi_ul_rm_notify_cb;
	ret = sipa_rm_register(SIPA_RM_RES_CONS_WIFI_UL, &pamwifi->ul_param);
	if (ret) {
		pw_err("UL res register failed\n");
		return ret;
	}

	/*create prod*/
	rm_params.name = SIPA_RM_RES_PROD_PAM_WIFI;
	rm_params.floor_voltage = 0;
	rm_params.reg_params.notify_cb = NULL;
	rm_params.reg_params.user_data = vif;
	rm_params.request_resource = pamwifi_req_res;
	rm_params.release_resource = pamwifi_rel_res;
	ret = sipa_rm_create_resource(&rm_params);
	if (ret) {
		pw_err("res create failed\n");
		return ret;
	}

	/*add dependencys*/
	ret = sipa_rm_add_dependency(SIPA_RM_RES_CONS_WIFI_UL,
			SIPA_RM_RES_PROD_PAM_WIFI);
	if (ret < 0 && ret != -EINPROGRESS) {
		pw_err("pam ipa add_dependency WIFI_UL fail.\n");
		sipa_rm_delete_resource(SIPA_RM_RES_PROD_PAM_WIFI);
		return ret;
	}

	ret = sipa_rm_add_dependency(SIPA_RM_RES_CONS_WIFI_DL,
			SIPA_RM_RES_PROD_PAM_WIFI);
	if (ret < 0 && ret != -EINPROGRESS) {
		pw_err("pam ipa add_dependency WIFI_DL fail.\n");
		sipa_rm_delete_dependency(SIPA_RM_RES_CONS_WIFI_UL,
				SIPA_RM_RES_PROD_IPA);
		sipa_rm_delete_resource(SIPA_RM_RES_PROD_PAM_WIFI);
		return ret;
	}

	ret = sipa_rm_add_dependency(SIPA_RM_RES_CONS_WWAN_UL,
			SIPA_RM_RES_PROD_PAM_WIFI);
	if (ret < 0 && ret != -EINPROGRESS) {
		pw_err("pam ipa add_dependency WWAN_UL fail.\n");
		sipa_rm_delete_dependency(SIPA_RM_RES_CONS_WIFI_UL,
				SIPA_RM_RES_PROD_IPA);
		sipa_rm_delete_dependency(SIPA_RM_RES_CONS_WIFI_DL,
				SIPA_RM_RES_PROD_IPA);
		sipa_rm_delete_resource(SIPA_RM_RES_PROD_PAM_WIFI);
		return ret;
	}

	ret = sipa_rm_add_dependency(SIPA_RM_RES_CONS_WWAN_DL,
			SIPA_RM_RES_PROD_PAM_WIFI);
	if (ret < 0 && ret != -EINPROGRESS) {
		pw_err("pam ipa add_dependency WWAN_DL fail.\n");
		sipa_rm_delete_dependency(SIPA_RM_RES_CONS_WIFI_UL,
				SIPA_RM_RES_PROD_IPA);
		sipa_rm_delete_dependency(SIPA_RM_RES_CONS_WIFI_DL,
				SIPA_RM_RES_PROD_IPA);
		sipa_rm_delete_dependency(SIPA_RM_RES_CONS_WWAN_UL,
				SIPA_RM_RES_PROD_IPA);
		sipa_rm_delete_resource(SIPA_RM_RES_PROD_PAM_WIFI);
		return ret;
	}

	return ret;
}

static void sprdwl_pamwifi_res_uninit(struct pamwifi_t *pamwifi)
{
	sipa_rm_release_resource(SIPA_RM_RES_CONS_WIFI_UL);
	sipa_rm_delete_dependency(SIPA_RM_RES_CONS_WIFI_UL,
			SIPA_RM_RES_PROD_PAM_WIFI);
	sipa_rm_delete_dependency(SIPA_RM_RES_CONS_WIFI_DL,
			SIPA_RM_RES_PROD_PAM_WIFI);
	sipa_rm_delete_resource(SIPA_RM_RES_PROD_PAM_WIFI);
	if(pamwifi)
		sipa_rm_deregister(SIPA_RM_RES_CONS_WIFI_UL, &pamwifi->ul_param);
}

/**
 *  sprd_pamwifi_enable - start pamwifi and open/connect ipa to work
 * 	@vif: wifi network informations
 *
 *	Note :This function must be called after sprd_pamwifi_init()
 *
 */
void sprd_pamwifi_enable(struct sprd_vif *vif)
{
	int ret;

	if( !g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
	      pw_alert("%s not supproted \n", __func__);
             return;
	}
	mutex_lock(&pamwifi_mutex);

	if(!g_pamwifi || !g_pamwifi->glb_ops){
		mutex_unlock(&pamwifi_mutex);
		return;
	}

	sprdwl_pamwifi_res_init(g_pamwifi, vif);
	ret = sipa_pam_connect(&g_pamwifi->sipa_params);
	if (ret) {
		pw_err("%s, pamwifi connect ipa fail\n", __func__);
		g_pamwifi->suspend_stage = PAMWIFI_REG_SUSPEND;
	}

	ret = sipa_nic_open(
			SIPA_TERM_WIFI,
			-1,
			pamwifi_dl_rm_notify_cb,
			NULL);
	pw_info("%s, nic_id: %d\n", __func__, ret);
	g_nic_id = ret;

	if(g_pamwifi->glb_ops->start)
		g_pamwifi->glb_ops->start(g_pamwifi->glb_base, PW_START_ALL);
	g_pamwifi->suspend_stage = PAMWIFI_READY;
	g_pamwifi->enabled = 1;
	mutex_unlock(&pamwifi_mutex);
}

void sprd_pamwifi_disable(struct sprd_vif *vif)
{
	u32 value = 0;
	int timeout = 500;

	if( !g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
             return;
	}
	mutex_lock(&pamwifi_mutex);
	g_pamwifi->enabled = 0;
	if(!g_pamwifi || !g_pamwifi->glb_ops){
		mutex_unlock(&pamwifi_mutex);
		return;
	}

	sipa_nic_close(g_nic_id);
	sprdwl_pamwifi_res_uninit(g_pamwifi);
	if(!g_pamwifi->suspend_stage){
		mutex_unlock(&pamwifi_mutex);
		return;
	}
	if (!(g_pamwifi->suspend_stage & PAMWIFI_EB_SUSPEND)) {
		if (!(g_pamwifi->suspend_stage & PAMWIFI_REG_SUSPEND)) {
			sipa_disconnect(SIPA_EP_WIFI, SIPA_DISCONNECT_START);
			if (!(g_pamwifi->suspend_stage & PAMWIFI_EB_SUSPEND)){
				value = check_pamwifi_ipa_fifo_status(g_pamwifi);
				pw_info("%s, Start to close Pam wifi!\n", __func__);
				while(value) {
					pw_info("Pam wifi closing!\n");
					value = check_pamwifi_ipa_fifo_status(g_pamwifi);
					if (!timeout--) {
						pw_err("Pam wifi close fail!\n");
						break;
					}
					usleep_range(10, 15);
				}
				/*stop pam wifi*/
				if(g_pamwifi->glb_ops->stop){
					g_pamwifi->glb_ops->stop(g_pamwifi->glb_base, PW_START_ALL);
					g_pamwifi->suspend_stage |= PAMWIFI_REG_SUSPEND;
				}
			}
			sipa_disconnect( SIPA_EP_WIFI, SIPA_DISCONNECT_END);
		}
		if(!(g_pamwifi->suspend_stage &PAMWIFI_REG_SUSPEND)){
			g_pamwifi->suspend_stage |= PAMWIFI_REG_SUSPEND;
			pw_info("%d,Pam wifi close success!!\n", __LINE__);
			/*stop pam wifi*/
			if(g_pamwifi->glb_ops->stop)
				g_pamwifi->glb_ops->stop(g_pamwifi->glb_base, PW_START_ALL);
		}
		if (!(g_pamwifi->suspend_stage & PAMWIFI_EB_SUSPEND)){
			poweron(g_pamwifi, false);
			g_pamwifi->suspend_stage |= PAMWIFI_EB_SUSPEND;
		}
	}

	mutex_unlock(&pamwifi_mutex);

}

/**
 *  sprd_pamwifi_pause_chip - pause the pamwifi hardwire, not stop
 *
 *  It always be used when some cp2 hang event
 */
int sprd_pamwifi_pause_chip(void)
{
	int ret = PAMWIFI_ERROR;

	if(!g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
		pw_alert("%s pamwifi disalbed !\n", __func__);
		return PAMWIFI_DISABLED;
	}
	mutex_lock(&pamwifi_mutex);

	if(!g_pamwifi || !g_pamwifi->glb_ops){
		mutex_unlock(&pamwifi_mutex);
		return PAMWIFI_ERROR;
	}
	if(g_pamwifi->glb_ops->pause)
		ret = g_pamwifi->glb_ops->pause(g_pamwifi->glb_base);

	mutex_unlock(&pamwifi_mutex);
	return ret;
}

/**
 *  sprdwl_pamwifi_resume - resume the paused pamwifi
 *
 *  It always be used when after cp2 hang
 */
int sprd_pamwifi_resume_chip(void)
{
	int  ret = PAMWIFI_ERROR;

	if( !g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
		pw_alert("%s pamwifi disalbed !\n", __func__);
		return PAMWIFI_DISABLED;
	}
	mutex_lock(&pamwifi_mutex);

	if(!g_pamwifi || !g_pamwifi->glb_ops){
		mutex_unlock(&pamwifi_mutex);
		return PAMWIFI_ERROR;
	}
	if(g_pamwifi->glb_ops->pause)
		ret = g_pamwifi->glb_ops->resume(g_pamwifi->glb_base);

	mutex_unlock(&pamwifi_mutex);
	return ret;
}

/**
 *  sprd_pamwifi_using_ap - check pamwifi can be suspend
 *
 *  When pamwifi was request resource by ipa, it would not permited to suspend.
 */
bool sprd_pamwifi_using_ap(void)
{
	bool ret;

	if( !g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
		pw_alert("%s pamwifi disalbed !\n", __func__);
		return false;
	}
	mutex_lock(&pamwifi_mutex);

	if(!g_pamwifi){
		mutex_unlock(&pamwifi_mutex);
		return false;
	}
	ret = (g_pamwifi->ul_resource_flag?true:false);
	mutex_unlock(&pamwifi_mutex);
	return ret;
}

static int sprdwl_pamwifi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device_node *dev_node = NULL;
	struct  of_device_id *match = NULL;
	struct pamwifi_ops *pdata;

	if(!g_pamwifi){
		return -1;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pamwifi_subsys_base");
	if (!res) {
		g_pamwifi->subsys_base =  ioremap((phys_addr_t)0x25000004l, 0x10);
		pw_err("wifi get  pamwifi_subsys_base res fail using default! subsys_base 0x%llx \n", g_pamwifi->subsys_base);
	}else{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		g_pamwifi->subsys_base = devm_ioremap(&pdev->dev,
				res->start, resource_size(res));
#else
		g_pamwifi->subsys_base = devm_ioremap_nocache(&pdev->dev,
				res->start, resource_size(res));

#endif
		pw_info("%s subsys base start 0x%llx size =%d, ioremap 0x%llx \n", __func__,
				res->start, resource_size(res),
				g_pamwifi->subsys_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pam_wifi_reg_base_remap");
	if (!res) {
		g_pamwifi->glb_base = ioremap((phys_addr_t)0x25200000l, 0x3000l);
		pw_err("wifi get  pam_wifi_reg_base_remap res fail using default glb_base 0x%llx! dev %s\n", g_pamwifi->glb_base, dev_name(&pdev->dev));

	}else{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
                g_pamwifi->glb_base = devm_ioremap(&pdev->dev,
				res->start, resource_size(res));
#else
		g_pamwifi->glb_base = devm_ioremap_nocache(&pdev->dev,
				res->start, resource_size(res));
#endif
		pw_info("%s glb_base start 0x%llx size =%d, ioremap 0x%llx \n", __func__,
				res->start, resource_size(res),
				g_pamwifi->glb_base);
	}

	dev_node = of_get_child_by_name(pdev->dev.of_node, "pamwifi");
	if(dev_node){
		match = (struct of_device_id *)of_match_node(pamwifi_match_table, dev_node);
		if(match && match->data){
			pw_info("find match data \n");
			pdata =(struct pamwifi_ops *)match->data;
			g_pamwifi->glb_ops = pdata->glb_ops;
			g_pamwifi->fifo_ops = pdata->fifo_ops;
		}
	}
	if(!match){
		pw_err("cannot find matched data using default!\n");
		g_pamwifi->glb_ops = &g_pamwifi_r2p0_glb_ops;
		g_pamwifi->fifo_ops = &g_pamwifi_r2p0_fifo_ops;
	}
	g_pamwifi->pdev = pdev;

	return 0;
}

/*check pamwifi idle sts*/
static u32 check_pamwifi_ipa_fifo_status(struct pamwifi_t *pamwifi)
{
	enum pamwifi_start_type status =0;
	u32 ul_rx_wrptr=0, ul_rx_rdptr=0, ul_tx_wrptr=0, ul_tx_rdptr=0,
	    dl_rx_wrptr =0, dl_rx_rdptr =0, dl_tx_wrptr=0, dl_tx_rdptr=0;


	if(!pamwifi || !pamwifi->glb_ops || !pamwifi->fifo_ops){
		return 0;
	}

	if(pamwifi->glb_ops->get_pamwifi_status)
		status = pamwifi->glb_ops->get_pamwifi_status(pamwifi->glb_base);
	/*get UL rx and tx wrptr/rdptr*/
	if(pamwifi->fifo_ops->get_rx_ptr)
		pamwifi->fifo_ops->get_rx_ptr(CMNFIFO_TYPE_UL, pamwifi->glb_base,
				&ul_rx_wrptr, &ul_rx_rdptr);
	if(pamwifi->fifo_ops->get_tx_ptr)
		pamwifi->fifo_ops->get_tx_ptr(CMNFIFO_TYPE_UL, pamwifi->glb_base,
				&ul_tx_wrptr, &ul_tx_rdptr);
	/*get DL Free  rx and tx wrptr/rdptr*/
	if(pamwifi->fifo_ops->get_rx_ptr)
		pamwifi->fifo_ops->get_rx_ptr(CMNFIFO_TYPE_DL_FREE, pamwifi->glb_base,
				&dl_rx_wrptr, &dl_rx_rdptr);
	if(pamwifi->fifo_ops->get_tx_ptr)
		pamwifi->fifo_ops->get_tx_ptr(CMNFIFO_TYPE_DL_FREE, pamwifi->glb_base,
				&dl_tx_wrptr, &dl_tx_rdptr);

	if (!status ||
			((status & BIT(4)) && (ul_rx_wrptr == ul_tx_wrptr) &&
			 status & BIT(5) && (dl_rx_wrptr == dl_tx_wrptr))) {
		pw_err("%s, dl_idle_sts:0x%x, wrptr:(ul rx) %u, (ul tx) %u,(dlrx) %u,(dl tx) %u, rdptr:(ul rx) %u, (ul tx) %u, (dl rx )%u, (dl tx) %u\n",
				__func__,status, ul_rx_wrptr, ul_tx_wrptr, dl_rx_wrptr, dl_tx_wrptr,
				ul_rx_rdptr,ul_tx_rdptr, dl_rx_rdptr,dl_tx_rdptr);
		return 0;
	} else{
		pw_err("%s, dl_idle_sts:0x%x, wrptr:(ul rx) %u, (ul tx) %u,(dlrx) %u,(dl tx) %u, rdptr:(ul rx) %u, (ul tx) %u, (dl rx )%u, (dl tx) %u\n",
				__func__,status, ul_rx_wrptr, ul_tx_wrptr, dl_rx_wrptr, dl_tx_wrptr,
				ul_rx_rdptr,ul_tx_rdptr, dl_rx_rdptr,dl_tx_rdptr);
		return 1;
	}
}

/**
 *  sprdwl_deinit_pamwifi_fifo - release all memory when
 * 	@pdev: wifi driver platform device
 *
 *	Note :This function must be called before close softap or wifi exit
 *
 */
static void sprdwl_deinit_pamwifi_fifo(struct platform_device *pdev, u32 fifo_depth)
{
	if(dl_type1_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(dma_addr_t),
				dl_type1_virt_addr, dl_type1_phy_addr);
		dl_type1_virt_addr = NULL;
	}
	if(dl_type2_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(dma_addr_t),
				dl_type2_virt_addr, dl_type2_phy_addr);
		dl_type2_virt_addr = NULL;
	}
	if(dl_type3_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(dma_addr_t),
				dl_type3_virt_addr, dl_type3_phy_addr);
		dl_type3_virt_addr = NULL;
	}
	if(dl_type4_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(dma_addr_t),
				dl_type4_virt_addr, dl_type4_phy_addr);
		dl_type4_virt_addr = NULL;
	}
	if(dl_free_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(dma_addr_t),
				dl_free_virt_addr, dl_free_phy_addr);
		dl_free_virt_addr = NULL;
	}
	if(miss_tx_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(struct pamwifi_miss_node_tx_dscr),
				miss_tx_virt_addr, miss_tx_phy_addr);
		miss_tx_virt_addr = NULL;
	}
	if(miss_rx_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(struct pamwifi_miss_node_rx_dscr),
				miss_rx_virt_addr, miss_rx_phy_addr);
		miss_rx_virt_addr = NULL;
	}
	//dma_free_coherent(&pdev->dev, fifo_depth*sizeof(dma_addr_t),
	//				 dl_4in1_virt_addr, dl_4in1_phy_addr);
	//dl_4in1_virt_addr = NULL;
	if(ul_tx_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(dma_addr_t),
				ul_tx_virt_addr, ul_tx_phy_addr);
		ul_tx_virt_addr = NULL;
	}
	if(ul_rx_virt_addr){
		dma_free_coherent(&pdev->dev, fifo_depth*2*sizeof(dma_addr_t),
				ul_rx_virt_addr, ul_rx_phy_addr);
		ul_rx_virt_addr = NULL;
	}
	if(pam_wifi_msdu_header_info){
		dma_free_coherent(&pdev->dev,  8*4*16, pam_wifi_msdu_header_info,
				term_pam_wifi_msdu_header_buf);
		pam_wifi_msdu_header_info = NULL;
	}
	pw_info("%d,Pam wifi close success!!\n", __LINE__);
}

static int load_pamwifi_cfg(struct pamwifi_t *pamwifi)
{
	int ret = -1;
	if(pamwifi){
		pamwifi->tx_4in1_en =
			g_cp_cap.mux_tx_common_fifo_support ? true:false;
		pamwifi->tx_thres.cnt_enable = true;
		pamwifi->tx_thres.cnt = 16;
		pamwifi->tx_thres.timeout_enable = true;
		pamwifi->tx_thres.timeout = 10;
		pamwifi->fifo_depth = PAMWIFI_COMN_FIFO_DEPTH;
		pamwifi->search_table_depth = 16;
		pamwifi->watermark.dl_cp_filled =1;
		pamwifi->watermark.dl_cp_miss =1;
		pamwifi->watermark.dl_ap_filled =1;
		pamwifi->watermark.dl_ap_free =1;
		pamwifi->watermark.dl_cp_type1 =1;
		pamwifi->watermark.dl_cp_type2 =1;
		pamwifi->watermark.dl_cp_type3 =1;
		pamwifi->watermark.dl_cp_type4 =1;
		pamwifi->watermark.dl_cp_type4 =1;
		pamwifi->watermark.ul_cp_free=1;
		pamwifi->watermark.ul_cp_filled=1;
		pamwifi->watermark.ul_ap_filled=1;
		pamwifi->watermark.ul_ap_free=1;
		pamwifi->ul_node.ul_net_id = 0;
		pamwifi->ul_node.ul_src_id = SIPA_TERM_WIFI;
		pamwifi->timescale = 0xa;
		ret =0;
	}

	return ret;
}

/**
 *  sprd_pamwifi_supported -pamwifi supported by hardware and wcn firmware
 * 	@pdev: wifi driver platform device
 *
 *	Note :This function return this board supported pamwifi feature or not.
 */
bool sprd_pamwifi_supported(struct platform_device *pdev)
{
	return g_cp_cap.cp_pam_wifi_support && sprd_pamwifi_hw_supported(pdev) ;
}

/**
 *  sprd_pamwifi_hw_supported -this AP main chip suppored pamwifi hardware or not
 * 	@pdev: wifi driver platform device
 *
 *	Note :This function return hardware supported pamwifi or not according
 *             the hardware dts defined "pamwifi" node or not.
 */
bool sprd_pamwifi_hw_supported(struct platform_device *pdev)
{
      static bool firsttime = true;
      struct device_node *dev_node = NULL;

	if(firsttime){
		firsttime = false;
		if(pdev)
			dev_node = of_get_child_by_name(pdev->dev.of_node, "pamwifi");
		if(dev_node)
			g_hw_supported = true;
		else
			g_hw_supported = false;
	}
	//pw_alert("%s g_hw_supported = %d \n", __func__, g_hw_supported);
	return g_hw_supported;
}


/**
 *  sprd_pamwifi_init - initialize of pamwifi system and config pamwifi hardware
 * 	@pdev: wifi driver platform device
 *	@priv: wifi driver imported struct, it will be used to get some wifi driver list
 *              and some information
 *
 *	Note :This function must be called before used pamwifi system, sometimes
 *              be called when a softap opened
 */
int sprd_pamwifi_init(struct platform_device *pdev, struct sprd_priv *priv)
{
	struct sprdwl_pamwifi_msg_buf *pamwifi_msg_buf;
	int i, ret =0;
	if( !g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
		pw_alert("%s pamwifi disalbed !\n", __func__);
		return PAMWIFI_DISABLED;
	}
#ifdef PAMWIFI_TP_ENABLE
	g_tp_info.tx_last_time  = jiffies;
	g_tp_info.rx_last_time =jiffies;
	g_tp_info.tx_bytes = 0;
	g_tp_info.tx_count =0;
	g_tp_info.rx_bytes = 0;
#endif
	mutex_lock(&pamwifi_mutex);

	g_pamwifi = kzalloc(sizeof(struct pamwifi_t), GFP_KERNEL);
	if (!g_pamwifi) {
		pw_err("g_pamwifi alloc fail!\n");
		goto err;
	}
	memset(g_pamwifi, 0x00, sizeof(*g_pamwifi));
       g_pamwifi->suspend_stage = PAMWIFI_NONE;
	ret = sprdwl_pamwifi_probe(pdev);
	if (ret) {
		pw_err("%s pamwifi probe fail\n",__func__);
		goto err;
	}
	g_pamwifi->priv = priv;
	/*enalbe pamwifi system*/
	if(g_pamwifi->glb_ops->system_enable){
		g_pamwifi->glb_ops->system_enable(g_pamwifi->subsys_base,true);
	}

	/*check cp2 cap*/
	/*if (!g_pamwifi->cp_cap.cp_pam_wifi_support) {
	  wl_err("cp2 do not support pam_wifi!\n");
	  return;
	  }*/

	/*init ipa for pamwifi*/
	config_ipa(g_pamwifi);
	load_pamwifi_cfg(g_pamwifi);
	config_pamwifi(g_pamwifi);
	config_common_fifo(g_pamwifi);

	/*init msdu dscr*/
	if (!g_cp_cap.chip_ver)
		ac_msdu_init(g_pamwifi, 0);
	else
		ax_msdu_init(g_pamwifi, 0);

	/*try 2 ipi mode, because marlin3 only support 1ipi*/
	config_ipi(g_pamwifi);

	/*register pamwifi irq*/
	g_pamwifi->irq = platform_get_irq_byname(pdev, "pam-wifi-irq");
	ret = request_irq(g_pamwifi->irq , pamwifi_irq_handle,
			IRQF_NO_SUSPEND, "pam_wifi_irq", NULL);
	pw_info("pam_wifi_irq-%d , ret: %d!!!\n", g_pamwifi->irq, ret);

	/*init miss buf*/
	g_pamwifi->msglist = kzalloc(sizeof(struct pamwifi_msglist), GFP_KERNEL);
	g_pamwifi->msglist->max_num = g_pamwifi->fifo_depth;

	INIT_LIST_HEAD(&g_pamwifi->msglist->freelist);
	INIT_LIST_HEAD(&g_pamwifi->msglist->busylist);
	for (i = 0; i < max(g_pamwifi->fifo_depth, g_pamwifi->sipa_info.dl_fifo.fifo_depth); i++) {
		pamwifi_msg_buf = kzalloc(sizeof(struct sprdwl_pamwifi_msg_buf), GFP_KERNEL);
		if (pamwifi_msg_buf) {
			INIT_LIST_HEAD(&pamwifi_msg_buf->list);
			list_add_tail(&pamwifi_msg_buf->list, &g_pamwifi->msglist->freelist);
		} else {
			pw_err("miss buffer alloc failed!\n");
			ret = -ENOMEM;
			goto err;
		}
	}

	sipa_dummy_register_wifi_recv_handler(&wifi_recv_skb);

	/*create power workqueue*/
	/*create power workqueue*/
	INIT_DELAYED_WORK(&g_pamwifi->power_work, sprdwl_pamwifi_power_work);
	if(g_power_wq == NULL)
		g_power_wq = create_workqueue("pamwifi_power_wq");
	if (!g_power_wq) {
		pw_err("pamwifi power wq create failed\n");
		ret= -ENOMEM;
		goto err;
	}
	mutex_unlock(&pamwifi_mutex);
	return ret;
err:
	mutex_unlock(&pamwifi_mutex);
	if(g_pamwifi){
		pw_alert("%s call sprd_pamwifi_uninit!\n", __func__);
		sprd_pamwifi_uninit(pdev);
	}
	return ret;
}

void sprd_pamwifi_uninit(struct platform_device *pdev)
{
	struct sprdwl_pamwifi_msg_buf *msgbuf = NULL, *tmp;
	struct pamwifi_msglist *msglist = NULL;

	if( !g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
	      pw_alert("%s not supproted \n", __func__);
             return;
	}
	mutex_lock(&pamwifi_mutex);

	if(!g_pamwifi){
		mutex_unlock(&pamwifi_mutex);
		return;
	}
	msglist = g_pamwifi->msglist;
	if(g_pamwifi->irq){
		disable_irq(g_pamwifi->irq);
		free_irq(g_pamwifi->irq, NULL);
	}
	sprdwl_deinit_pamwifi_fifo(pdev, 1024);
	if(g_pamwifi->glb_ops->system_enable)
		g_pamwifi->glb_ops->system_enable(g_pamwifi->subsys_base, false);
	//g_pamwifi->suspend_stage |= PWSYS_STS_DISABLE
	cancel_work_sync(&miss_tx_worker);
	if( g_pamwifi->msglist){
		if (!list_empty(&g_pamwifi->msglist->freelist)) {
			list_for_each_entry_safe(msgbuf, tmp,
				               &g_pamwifi->msglist->freelist, list){
				list_del(&msgbuf->list);
				kfree(msgbuf);
		    }
		}
		if (!list_empty(&g_pamwifi->msglist->busylist)) {
			list_for_each_entry_safe(msgbuf, tmp,
				               &g_pamwifi->msglist->busylist, list){
				list_del(&msgbuf->list);
				kfree(msgbuf);
		    }
		}
		kfree(msglist);
	}
	cancel_delayed_work(&g_pamwifi->power_work);
	kfree(g_pamwifi);
	g_pamwifi = NULL;
	mutex_unlock(&pamwifi_mutex);
	sipa_dummy_unregister_wifi_recv_handler(&wifi_recv_skb);
}

/**
 *  sprdwl_pamwifi_AddCapCmd - add pamwifi get capabiity cmd
 * 	@addr: the memory address to save pamwifi get capabiity cmd
 *    @max_len: max data len
 *
 *	This function used to init get capability cmd with pamwifi enabled
 *
 */
int sprd_pamwifi_settlv_cmd(u8 *addr, u16 max_len){
	struct pamwifi_cap_tlv tlv;
	struct tlv_data *p;
	u16 offset = sizeof(struct tlv_data);
	u16 data_len = sizeof(tlv);

	if(addr == NULL||  offset +data_len > max_len){
		return -1;
	}
	tlv.ap_pam_wifi_support = 1;
	tlv.mux_tx_cmn_fifo_support = 1;
	tlv.ipi_mode_support = PAMWIFI_IPI_MODE2;
	tlv.dl_rx_cmn_fifo_depth = PAMWIFI_DL_CMN_FIFO_DEPTH;

	p = (struct tlv_data *)(addr + offset);
	p->type = PAM_WIFI_AP_CAP_TLV_TYPE;
	p->len = data_len;
	memcpy(p->data, &tlv, data_len);
	return 0;

}

/**
 *  sprd_pamwifi_save_capability - save the pamwifi capability
 * 	@cap: pamwifi capabilty from cp2
 *
 */
int sprd_pamwifi_save_capability(void *cap){
	if(cap){
		memcpy(&g_cp_cap, cap, sizeof(g_cp_cap));
		pw_err("pam cap, support:%u, ver:%u, mux_support:%u, depth:%u, mux_addrl:%u,\
				mux_addrh:%u, ipi_mode:%u, ipi_addr:%u %u %u %u, %u, %u, %u, %u\n",
				g_cp_cap.cp_pam_wifi_support,
				g_cp_cap.chip_ver, g_cp_cap.mux_tx_common_fifo_support,
				g_cp_cap.mux_tx_common_fifo_depth, g_cp_cap.mux_tx_common_fifo_base_addr_l,
				g_cp_cap.mux_tx_common_fifo_base_addr_h, g_cp_cap.ipi_mode,
				g_cp_cap.ipi_reg_addr_l[0], g_cp_cap.ipi_reg_addr_l[1],
				g_cp_cap.ipi_reg_addr_l[2], g_cp_cap.ipi_reg_addr_l[3],
				g_cp_cap.ipi_reg_addr_h[0], g_cp_cap.ipi_reg_addr_h[1],
				g_cp_cap.ipi_reg_addr_h[2], g_cp_cap.ipi_reg_addr_h[3]);

		return 0;
	}else
		return -1;
}


/**
 *  sprd_pamwifi_get_captlv_size - return pamwifi capability cmd size
 * 	this pamwifi capabilty cmd send to cp2
 *
 */
int sprd_pamwifi_get_captlv_size(void){
	return sizeof(struct pamwifi_cap_tlv);
}

static int pkt_checksum(struct sk_buff *skb, struct net_device *ndev)
{
	struct udphdr *udphdr;
	struct tcphdr *tcphdr;
	struct iphdr *iphdr;
	struct ipv6hdr *ipv6hdr;
	__sum16 checksum = 0;
	unsigned char iphdrlen = 0;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;

	if (ethhdr->h_proto == htons(ETH_P_IPV6)) {
		ipv6hdr = (struct ipv6hdr *)(skb->data + ETHER_HDR_LEN);
		iphdrlen = sizeof(*ipv6hdr);
	} else if (ethhdr->h_proto == htons(ETH_P_IP)) {
		iphdr = (struct iphdr *)(skb->data + ETHER_HDR_LEN);
		iphdrlen = ip_hdrlen(skb);
	}
	udphdr = (struct udphdr *)(skb->data + ETHER_HDR_LEN + iphdrlen);
	tcphdr = (struct tcphdr *)(skb->data + ETHER_HDR_LEN + iphdrlen);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		checksum =
			(__force __sum16)sc2355_tx_do_csum(
					skb->data + ETHER_HDR_LEN + iphdrlen,
					skb->len - ETHER_HDR_LEN - iphdrlen);
		if ((ethhdr->h_proto == htons(ETH_P_IPV6) && ipv6hdr->nexthdr == IPPROTO_UDP) ||
				(ethhdr->h_proto == htons(ETH_P_IP) && iphdr->protocol != IPPROTO_UDP)) {
			udphdr->check = ~checksum;
			pw_info("csum:%x,udp check:%x\n",
					checksum, udphdr->check);
		} else if ((ethhdr->h_proto == htons(ETH_P_IPV6) && ipv6hdr->nexthdr == IPPROTO_TCP) ||
				(ethhdr->h_proto == htons(ETH_P_IP) && iphdr->protocol != IPPROTO_TCP)) {
			tcphdr->check = ~checksum;
			pw_info("csum:%x,tcp check:%x\n",
					checksum, tcphdr->check);
		} else
			return 1;

		skb->ip_summed = CHECKSUM_NONE;
		return 0;
	}
	return 1;
}

/**
 *  sprd_pamwifi_xmit_to_ipa - transmit dl data to ipa, then to cp2
 * 	@skb: wifi driver platform device
 *    @ndev: net device, used to send data to tcpip
 *
 *	Note :This function was called when first tx data to cp2 as not route table setup,
 *             or some data missed by pamwifi.
 *             we need setup the connect with ipa and pamwifi.
 *    The route of tx : tcpip or application send data ->wifi driver ->ipa ->pamwifi ->cp2
 *
 */
int sprd_pamwifi_xmit_to_ipa(struct sk_buff *skb, struct net_device *ndev){
	int ret = 0;

	if(!g_hw_supported  || !g_cp_cap.cp_pam_wifi_support){
	     pw_alert("%s not supported \n", __func__);
             return PAMWIFI_DISABLED;
	}
	ret = __pamwifi_xmit_to_ipa(skb, ndev);
	return ret;
}
static int __pamwifi_xmit_to_ipa(struct sk_buff *skb, struct net_device *ndev)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_hif *intf = (struct sprd_hif *)&(vif->priv->hif);
	unsigned char lut_index;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	int ret = 0;
	unsigned int qos_index = 0;
	struct sprd_peer_entry *peer_entry = NULL;
	unsigned char tid = 0, tos = 0;

	lut_index = sc2355_find_lut_index(intf, vif, skb->data);
	/*filter pkt to pam wifi*/
	if ((ethhdr->h_proto == htons(ETH_P_IPV6) ||ethhdr->h_proto == htons(ETH_P_IP)) &&
			lut_index > 5) {
		/*add tx ba*/
		intf->tx_num[lut_index]++;
		qos_index = sc2355_qos_get_tid_index(skb, MSDU_DSCR_RSVD + DSCR_LEN, &tid, &tos);
		peer_entry = &intf->peer_entry[lut_index];
		sc2355_tx_prepare_addba(intf, lut_index, peer_entry, tid);
		if (skb_headroom(skb) < (32 + NET_IP_ALIGN)) {
			struct sk_buff *tmp_skb = skb;

			skb = skb_realloc_headroom(skb, 32 + NET_IP_ALIGN);
			dev_kfree_skb(tmp_skb);
			if (!skb) {
				netdev_err(ndev, "%s send to pam wifi, skb_realloc_headroom failed\n",
						__func__);
				return NETDEV_TX_OK;
			}
		}
		ret = sipa_nic_tx(g_nic_id, SIPA_TERM_WIFI, -1, skb);
		if (unlikely(ret != 0)) {
			pw_err("sipa_wifi fail to send skb, ret %d\n", ret);
			if (ret == -ENOMEM || ret == -EAGAIN) {
				ndev->stats.tx_fifo_errors++;
				if (sipa_nic_check_flow_ctrl(g_nic_id)) {
					netif_stop_queue(ndev);
					pw_err("stop queue on dev %s\n", ndev->name);
				}
				sipa_nic_trigger_flow_ctrl_work(g_nic_id, ret);
				return NETDEV_TX_BUSY;
			}else{
				dev_kfree_skb(skb);
				return NETDEV_TX_OK;
			}
		}
		vif->ndev->stats.tx_bytes += skb->len;
		vif->ndev->stats.tx_packets++;
#ifdef PAMWIFI_TP_ENABLE
		g_tp_info.tx_bytes +=  skb->len;
		g_tp_info.tx_count ++;
		if (time_after(jiffies, g_tp_info.tx_last_time +  msecs_to_jiffies(1000))){
			pw_debug("%s, succeed to send to ipa  pkt count:%d  tp: %d Mbps\n", __func__, g_tp_info.tx_count ,(g_tp_info.tx_bytes/1024/128));
			g_tp_info.tx_bytes = 0;
			g_tp_info.tx_count =0;
			g_tp_info.tx_last_time = jiffies;
		}
#endif
		return NETDEV_TX_OK;
	}

	pkt_checksum(skb, ndev);
	//sprdwl_hex_dump("sprdwl xmit dump:", skb->data, 100);
	sc2355_xmit_data2cmd_wq(skb, ndev);
	return NETDEV_TX_OK;
}


