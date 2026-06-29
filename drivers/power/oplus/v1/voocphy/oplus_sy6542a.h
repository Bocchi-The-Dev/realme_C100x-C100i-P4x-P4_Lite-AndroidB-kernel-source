// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#ifndef _OPLUS_SY6542A_H_
#define _OPLUS_SY6542A_H_
#include "../oplus_chg_track.h"

#define UFCS_IC_NAME "SY6542A"

/********************** I2C Slave Addr **************************/
#define SY6542A_I2C_ADDR (0x2F)             /*update to 2F*/
#define SY6542A_MAX_REG (0x0264)
#define SY6542A_FLAG_NUM (3)

/********************** SY6542A UFCS reg map **********************/
#define SY6542A_ENABLE_REG_NUM (4)            /*update to 4, not need DPDM ctrl and INT_FLAG1*/

#define SY6542A_ADDR_DEVICE_ID (0x00)
#define SY6542A_ADDR_DEVICE_ID1 (0x01)

//#define SY6542A_ADDR_DPDM_CTRL (0x20)        /*Delete, not need*/
//#define SY6542A_CMD_DPDM_EN (0x01)           /*Delete, not need*/

#define SY6542A_ADDR_OTG_EN (0x3B)

#define SY6542A_ADDR_UFCS_CTRL0 (0x40)
#define SY6542A_SEND_SOURCE_HARDRESET BIT(0)
#define SY6542A_SEND_CABLE_HARDRESET BIT(1)
#define SY6542A_FLAG_BAUD_RATE_VALUE (BIT(4) | BIT(3))
#define SY6542A_FLAG_BAUD_NUM_SHIFT (3)
#define SY6542A_CMD_EN_CHIP (0x80)
#define SY6542A_CMD_DIS_CHIP (0X00)
#define SY6542A_MASK_EN_HANDSHAKE BIT(5)
#define SY6542A_CMD_EN_HANDSHAKE BIT(5)

#define SY6542A_ADDR_GENERAL_INT_FLAG1 (0x43)
//#define SY6542A_CMD_MASK_ACK_DISCARD (0x80)               /*no function*/

#define SY6542A_ADDR_GENERAL_INT_FLAG2 (0x43)
#define SY6542A_FLAG_ACK_RECEIVE_TIMEOUT BIT(0)
#define SY6542A_FLAG_HARD_RESET BIT(0)
#define SY6542A_FLAG_RX_BUFFER_BUSTY BIT(2)
#define SY6542A_FLAG_DATA_READY BIT(4)
#define SY6542A_FLAG_SENT_PACKET_COMPLETE BIT(5)
#define SY6542A_FLAG_CRC_ERROR BIT(1)
#define SY6542A_FLAG_BAUD_RATE_ERROR BIT(7)
#define SY6542A_FLAG_HANDSHAKE_SUCCESS BIT(6)
#define SY6542A_FLAG_HANDSHAKE_FAIL BIT(7)

#define SY6542A_ADDR_GENERAL_INT_FLAG3 (0x44)
#define SY6542A_FLAG_TRAINING_BYTE_ERROR BIT(6)
#define SY6542A_FLAG_MSG_TRANS_FAIL BIT(1)
#define SY6542A_FLAG_DATA_BYTE_TIMEOUT BIT(5)
#define SY6542A_FLAG_BAUD_RATE_CHANGE BIT(6)
#define SY6542A_FLAG_LENGTH_ERROR BIT(4)
#define SY6542A_FLAG_RX_OVERFLOW BIT(3)
#define SY6542A_FLAG_BUS_CONFLICT BIT(7)
#define SY6542A_FLAG_DATA_BIT_ERR BIT(5)

#define SY6542A_ADDR_UFCS_INT_MASK0 (0x46)              /* UPDATE to 0x46 */
#define SY6542A_CMD_MASK_ACK_TIMEOUT (0x01)

#define SY6542A_ADDR_UFCS_INT_MASK1 (0x47)              /* UPDATE to 0x47 */
#define SY6542A_MASK_TRANING_BYTE_ERROR (0x40)           /* bit 6 */

/*tx_buffer*/
#define SY6542A_ADDR_TX_LENGTH (0x49)

#define SY6542A_ADDR_TX_BUFFER0 (0x4A)

#define SY6542A_ADDR_TX_BUFFER35 (0x6D)

/*rx_buffer*/
#define SY6542A_ADDR_RX_LENGTH (0x6E)
#define SY6542A_ADDR_RX_BUFFER0 (0x6F)
#define SY6542A_ADDR_RX_BUFFER63 (0xAE)
#define SY6542A_LEN_MAX 64

#define SY6542A_ADDR_TXRX_BUFFER_CTRL (0x41)
#define SY6542A_CMD_CLR_TX_RX (0x30)
#define SY6542A_CMD_CLR_RX (0x10)
#define SY6542A_MASK_CLR_RX BIT(4)
#define SY6542A_CMD_CLR_TX (0x20)
#define SY6542A_MASK_CLR_TX BIT(5)

#define SY6542A_DEVICE_ID 0x67

/*********************command buffer**************/
/*avoid to rewrite the baudrate flags*/
#define SY6542A_CMD_SND_CMP BIT(2)
#define SY6542A_MASK_SND_CMP BIT(2)

/****************Message Construction Helper*********/
struct oplus_sy6542a_ufcs {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	atomic_t suspended;
	bool ufcs_enable;
};
#endif /*_OPLUS_SY6542A_H_*/