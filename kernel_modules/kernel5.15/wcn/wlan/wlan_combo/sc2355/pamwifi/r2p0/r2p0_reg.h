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

#ifndef _PAM_WIFI_R2P0_REG_H
#define _PAM_WIFI_R2P0_REG_H

#define    ROFF_UL_FILLED_FIFO_ADDRL		       0x0000
#define    ROFF_UL_FREE_FIFO_ADDRL				0x0004
#define    ROFF_DL_FILLED_FIFO_ADDRL			0x0008
#define    ROFF_DL_FREE_FIFO_ADDRL				0x000C
#define    ROFF_DL_UL_FIFO_ADDRH			       0x0010
#define    ROFF_UL_FREE_STS_ADDRL				0x0014
#define    ROFF_UL_FILLED_STS_ADDRL			0x0018
#define    ROFF_DL_FREE_STS_ADDRL				0x001C
#define    ROFF_DL_FILLED_STS_ADDRL			0x0020
#define    ROFF_DL_UL_STS_ADDRH			       0x0024
#define    ROFF_DL_FILLED_BUFFER_CTRL			0x0028
#define    ROFF_CFG_BUFFER_WATERMARK			0x002C
#define    ROFF_CFG_BUFFER_CTRL					0x0030
#define    ROFF_UL_FREE_DDR_MAPPING_OFFSETL	0x0034
#define    ROFF_UL_FILLED_DDR_MAPPING_OFFSETL	0x0038
#define    ROFF_DL_FREE_DDR_MAPPING_OFFSETL	0x003C
#define    ROFF_DL_FILLED_DDR_MAPPING_OFFSETL	0x0040
#define    ROFF_DDR_MAPPING_OFFSETH			0x0044
#define    ROFF_MSDU_ADDRL						0x0048
#define    ROFF_UL_NODE_INFO_CONFIG			0x004C
#define    ROFF_CFG_START						0x0050
#define    ROFF_RF_DL_CFG						0x0054
#define    ROFF_RF_DL_FLOW_THRESHOLD			0x0058
#define    ROFF_RF_DL_FLOW_PKT_NUM_CNT1		0x005C
#define    ROFF_RF_DL_FLOW_PKT_NUM_CNT2		0x0060
#define    ROFF_AXI_MST_CFG						0x0064
#define    ROFF_CH_WR_PRIO						0x0068
#define    ROFF_CH_RD_PRIO						0x006C
#define    ROFF_BUFFER_TIMEOUT_VAL				0x0070
#define    ROFF_PAMWIFIIP_VER					0x0074
#define    ROFF_INT_EN							0x0078
#define    ROFF_INT_CLR							0x007C
#define    ROFF_INT_STS							0x0080
#define    ROFF_IPI_UL1_ADDRL				       0x0084
#define    ROFF_IPI_UL2_ADDRL				       0x0088
#define    ROFF_IPI_DL1_ADDRL					0x008C
#define    ROFF_IPI_DL2_ADDRL					0x0090
#define    ROFF_IPI_ADDRH						0x0094
#define    ROFF_IPI_UL1_WDATA					0x0098
#define    ROFF_IPI_UL2_WDATA					0x009C
#define    ROFF_IPI_DL1_WDATA					0x00A0
#define    ROFF_IPI_DL2_WDATA					0x00A4
#define    ROFF_COMMON_FIFO_STS_UPDATE		0x00A8
#define    ROFF_TOS_PRIO							0x00B0
#define    ROFF_SW_DEBUG_MEM_ADDR				0x00B4
#define    ROFF_INDEX_SEARCH_DEPTH				0x00B8
#define    ROFF_INDEX_MISS_ADDRL				0x00BC
#define    ROFF_COMMON_FIFO_OFFSET			0x00C0
#define    ROFF_SW_DEBUG_CURT_STS				0x00C4
#define    ROFF_SW_DEBUG_RESP_STS				0x00C8
#define    ROFF_DUMMY_REG						0x00D0

/*Common fifo reg*/
/*base address offset*/
//[14:8] == 7'b000_0000
#define    tPSEL_RF				(0x00<<8)
//[14:7] == 8'b000_0001_1
#define    tPSEL_DL_TYPE1			(0x180)
//[14:7] == 8'b000_0010_0
#define    tPSEL_DL_TYPE2			(0x200)
//[14:7] == 8'b000_0010_1
#define    tPSEL_DL_TYPE3			(0x280)
//[14:7] == 8'b000_0011_0
#define   tPSEL_DL_TYPE4			(0x300)
//[14:7] == 8'b000_0011_1
#define    tPSEL_DL_FREE			(0x380)
//[14:7] == 8'b000_0100_0
#define    tPSEL_UL				(0x400)
//[14:7] == 8'b000_0101_0
#define    tPSEL_DL_MISS			(0x500)

//[14:11] == 4'b000_1   //Wrong in mail for 6:DA
#define    tPSEL_RAM1				(0x800)
//[14:11] == 4'b001_0   //Wrong in mail for 6
#define    tPSEL_RAM2				 (0x1000)
/*common fifo offset*/
#define ROFF_COMMON_RX_FIFO_DEPTH			0x00l
#define ROFF_COMMON_RX_FIFO_WR				0x04l
#define ROFF_COMMON_RX_FIFO_RD				0x08l
#define ROFF_COMMON_TX_FIFO_DEPTH			0x0Cl
#define ROFF_COMMON_TX_FIFO_WR				0x10l
#define ROFF_COMMON_TX_FIFO_RD				0x14l
#define ROFF_COMMON_RX_FIFO_ADDRL			0x18l
#define ROFF_COMMON_RX_FIFO_ADDRH			0x1Cl
#define ROFF_COMMON_TX_FIFO_ADDRL			0x20l
#define ROFF_COMMON_TX_FIFO_ADDRH			0x24l
#define ROFF_PERFETCH_FIFO_CTL				0x28l
#define ROFF_GEN_CTL_TX_FIFO_VALUE			0x2Cl
#define ROFF_GEN_CTL_EN						0x30l
#define ROFF_FLOW_CTL_CFG						0x38l
#define ROFF_GEN_CTL_CLR						0x48l

#define PW_NET_ID_MASK  0x1FE0000
#define PW_DST_ID_MASK  0x1F000

#define PW_UL_FREE_ADDRH_MASK     0xFF000000
#define PW_UL_FILLED_ADDRH_MASK  0x00FF0000
#define PW_DL_FREE_ADDRH_MASK     0x0000FF00
#define PW_DL_FILLED_ADDRH_MASK  0x000000FF
#define PW_MSDU_ADDRH_MASK          0xFF000000
#define PW_RF_TIMESCALE_MASK        0x00FF0000
#define PW_RF_IPI_MODE_MASK         0x0000000E0
#define PW_RF_AC_AX_SEL_MASK       0x000000004
#define PW_FLOW_COUNT_EN_MASK   0x000000010

#define PW_SUBSYS_POWER_MASK    0x000000040

#define PAM_WIFI_HW_LOCK_TO                 (20*1000)

#define	BIT_DL_CP_FILLED_WATERMARK(_X_)		(((_X_) << 4) & 0x000000F0)
#define	BIT_DL_CP_MISS_WATERMARK(_X_)		((_X_) & 0x0000000F)
#define	BIT_DL_AP_FILLED_WATERMARK(_X_)		(((_X_) << 28) & 0xF0000000)
#define	BIT_DL_AP_FREE_WATERMARK(_X_)		(((_X_) << 24) & 0x0F000000)
#define	BIT_DL_CP_TYPE1_WATERMARK(_X_)		(((_X_) << 16) & 0x00030000)
#define	BIT_DL_CP_TYPE2_WATERMARK(_X_)		(((_X_) << 18) & 0x000C0000)
#define	BIT_DL_CP_TYPE3_WATERMARK(_X_)		(((_X_) << 20) & 0x00300000)
#define	BIT_DL_CP_TYPE4_WATERMARK(_X_)		(((_X_) << 22) & 0x00C00000)
#define	BIT_UL_CP_FREE_WATERMARK(_X_)		(((_X_) << 12) & 0x0000F000)
#define	BIT_CFG_UL_AP_FREE_WATERMARK(_X_)	(((_X_) << 8) & 0x00000F00)
#define	BIT_UL_CP_FILLED_WATERMARK(_X_)		(((_X_) << 4) & 0x000000F0)
#define	BIT_UL_AP_FILLED_WATERMARK(_X_)		((_X_) & 0x0000000F)

#define	BIT_UL_SRC_ID(_X_)						((_X_) & 0x0000001F) /*[4:0]*/
#define	BIT_UL_DST_ID(_X_)						(((_X_) <<5) & 0x00003E00) /*[9:5]*/
#define	BIT_UL_NET_ID(_X_)						(((_X_) <<10) & 0x0003FC00) /*[17:10]*/
#define	BIT_OFFSET_BEFOR_MSDU(_X_)			(((_X_) <<18) & 0x03FC0000) /*[25:18]**/
#define	BIT_LENGTH_MSDU(_X_)					(((_X_) <<26) & 0xFC000000) /*[31:26]*/
#define   BIT_RF_TIMESCALE(_X_)                              (((_X_) <<16) & 0x00FF0000) /*[23:16]*/
#define   BIT_RF_IPI_MODE(_X_)                               (((_X_) <<5) & 0x0000000E0) /*[7:5]*/
#define   BIT_RF_AC_AX_SEL(_X_)                             (((_X_) <<2) & 0x000000004) /*[2]*/
#define   BIT_RF_FLOW_COUNT_EN(_X_)                   (((_X_) <<4) & 0x000000010) /*[4]*/


#define    BIT_TABLE_RD_STOPED                                   (BIT(17))
#define    BIT_SOFT_TABLE_UPDATE_REQ                      (BIT(16))

#endif
