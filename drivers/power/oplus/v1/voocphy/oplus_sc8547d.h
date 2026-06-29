// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SC8547D_H_
#define _OPLUS_SC8547D_H_

#define UFCS_BTB_TEMP_MAX 80
#define UFCS_USB_TEMP_MAX 80
#define UFCS_BTB_USB_OVER_CNTS 9

#define UFCS_UCT_POWER_VOLT 5000
#define UFCS_UCT_POWER_CURR 1000

#define UFCS_HARDRESET_RETRY_CNTS 3
#define UFCS_HANDSHAKE_RETRY_CNTS 1

/************************Timer***********************************/
#define UFCS_HANDSHAKE_TIMEOUT (300)
#define UFCS_PING_TIMEOUT (300)
#define UFCS_ACK_TIMEOUT (50)
#define UFCS_MSG_TIMEOUT (300)
#define UFCS_POWER_RDY_TIMEOUT (550)
/********************** I2C Slave Addr **************************/
#define SC8547D_I2C_ADDR (0x6A)
#define SC8547D_MAX_REG (0x0264)
#define SC8547D_FLAG_NUM (3)

/********************** SC2201 I2C reg map **********************/
#define SC8547D_ENABLE_REG_NUM (6)

#define SC8547D_ADDR_DEVICE_ID (0x00)
#define SC8547D_ADDR_DEVICE_ID1 (0x01)

#define SC8547D_ADDR_DPDM_CTRL (0x21)
#define SC8547D_CMD_DPDM_EN (0x01)

#define SC8547D_ADDR_OTG_EN (0x3B)

#define SC8547D_ADDR_UFCS_CTRL0 (0x40)
#define SEND_SOURCE_HARDRESET BIT(0)
#define SEND_CABLE_HARDRESET BIT(1)
#define FLAG_BAUD_RATE_VALUE (BIT(4) | BIT(3))
#define FLAG_BAUD_NUM_SHIFT (3)
#define SC8547D_CMD_EN_CHIP (0x80)
#define SC8547D_CMD_DIS_CHIP (0X00)
#define SC8547D_MASK_EN_HANDSHAKE BIT(5)
#define SC8547D_CMD_EN_HANDSHAKE BIT(5)

#define SC8547D_ADDR_GENERAL_INT_FLAG1 (0x41)
#define SC8547D_CMD_MASK_ACK_DISCARD (0x80)

#define SC8547D_ADDR_GENERAL_INT_FLAG2 (0x42)
#define SC8547D_FLAG_ACK_RECEIVE_TIMEOUT BIT(0)
#define SC8547D_FLAG_HARD_RESET BIT(1)
#define SC8547D_FLAG_DATA_READY BIT(2)
#define SC8547D_FLAG_SENT_PACKET_COMPLETE BIT(3)
#define SC8547D_FLAG_CRC_ERROR BIT(4)
#define SC8547D_FLAG_BAUD_RATE_ERROR BIT(5)
#define SC8547D_FLAG_HANDSHAKE_SUCCESS BIT(6)
#define SC8547D_FLAG_HANDSHAKE_FAIL BIT(7)

#define SC8547D_ADDR_GENERAL_INT_FLAG3 (0x43)
#define SC8547D_FLAG_TRAINING_BYTE_ERROR BIT(0)
#define SC8547D_FLAG_MSG_TRANS_FAIL BIT(1)
#define SC8547D_FLAG_DATA_BYTE_TIMEOUT BIT(3)
#define SC8547D_FLAG_BAUD_RATE_CHANGE BIT(4)
#define SC8547D_FLAG_LENGTH_ERROR BIT(5)
#define SC8547D_FLAG_RX_OVERFLOW BIT(6)
#define SC8547D_FLAG_BUS_CONFLICT BIT(7)

#define SC8547D_ADDR_UFCS_INT_MASK0 (0x44)
#define SC8547D_CMD_MASK_ACK_TIMEOUT (0x01)

#define SC8547D_ADDR_UFCS_INT_MASK1 (0x45)
#define SC8547D_MASK_TRANING_BYTE_ERROR (0x01)

/*tx_buffer*/
#define SC8547D_ADDR_TX_LENGTH (0x47)

#define SC8547D_ADDR_TX_BUFFER0 (0x48)

#define SC8547D_ADDR_TX_BUFFER35 (0x6B)

/*rx_buffer*/
#define SC8547D_ADDR_RX_LENGTH (0x6C)
#define SC8547D_ADDR_RX_BUFFER0 (0x6D)
#define SC8547D_ADDR_RX_BUFFER124 (0xE9)
#define SC8547D_LEN_MAX 64

#define SC8547D_ADDR_TXRX_BUFFER_CTRL (0xEA)
#define SC8547D_CMD_CLR_TX_RX (0xC0)

#define SC8547D_DEVICE_ID 0x49

//C0PY SC8547.h
/* Register 00h */
#define SC8547_REG_00					0x00
#define	SC8547_BAT_OVP_DIS_MASK			0x80
#define	SC8547_BAT_OVP_DIS_SHIFT		7
#define	SC8547_BAT_OVP_ENABLE			0
#define	SC8547_BAT_OVP_DISABLE			1

#define SC8547_BAT_OVP_MASK				0x3F
#define SC8547_BAT_OVP_SHIFT			0
#define SC8547_BAT_OVP_BASE				3500
#define SC8547_BAT_OVP_LSB				25

/* Register 01h */
#define SC8547_REG_01					0x01
#define SC8547_BAT_OCP_MASK			    0x3F
#define SC8547_BAT_OCP_SHIFT		    0
#define SC8547_BAT_OCP_BASE			    2000
#define SC8547_BAT_OCP_LSB			    100

/* Register 02h */
#define SC8547_REG_02					0x02
#define	SC8547_AC_OVP_STAT_MASK		    0x20
#define	SC8547_AC_OVP_STAT_SHIFT		5

#define	SC8547_AC_OVP_FLAG_MASK		    0x10
#define	SC8547_AC_OVP_FALG_SHIFT		4

#define	SC8547_AC_OVP_MASK_MASK		    0x08
#define	SC8547_AC_OVP_MASK_SHIFT		3

#define SC8547_AC_OVP_MASK				0x07
#define SC8547_AC_OVP_SHIFT				0
#define SC8547_AC_OVP_BASE				11000
#define SC8547_AC_OVP_LSB				1000
#define SC8547_AC_OVP_6P5V				6500

/* Register 03h */
#define SC8547_REG_03					0x03
#define SC8547_VDROP_OVP_DIS_MASK		0x80
#define SC8547_VDROP_OVP_DIS_SHIFT	    7
#define SC8547_VDROP_OVP_ENABLE		    0
#define SC8547_VDROP_OVP_DISABLE		1

#define	SC8547_VDROP_OVP_STAT_MASK		0x10
#define	SC8547_VDROP_OVP_STAT_SHIFT		4

#define	SC8547_VDROP_OVP_FLAG_MASK      0x08
#define	SC8547_VDROP_OVP_FALG_SHIFT		3

#define	SC8547_VDROP_OVP_MASK_MASK      0x04
#define	SC8547_VDROP_OVP_MASK_SHIFT     2

#define SC8547_VDROP_OVP_THRESHOLD_MASK     0x02
#define SC8547_VDROP_OVP_THRESHOLD_SHIFT    1
#define SC8547_VDROP_OVP_THRESHOLD_300MV    0
#define SC8547_VDROP_OVP_THRESHOLD_400MV    1

#define SC8547_VDROP_DEGLITCH_SET_MASK      0x01
#define SC8547_VDROP_DEGLITCH_SET_SHIFT     0
#define SC8547_VDROP_DEGLITCH_SET_5MS       0
#define SC8547_VDROP_DEGLITCH_SET_8US       1

/* Register 04h */
#define SC8547_REG_04                       0x04
#define	SC8547_VBUS_OVP_DIS_MASK            0x80
#define	SC8547_VBUS_OVP_DIS_SHIFT           7
#define	SC8547_VBUS_OVP_ENABLE              0
#define	SC8547_VBUS_OVP_DISABLE             1

#define	SC8547_VBUS_OVP_MASK                0x7F
#define	SC8547_VBUS_OVP_SHIFT               0
#define	SC8547_VBUS_OVP_BASE                6000
#define	SC8547_VBUS_OVP_LSB                 50

/* Register 05h */
#define SC8547_REG_05                       0x05
#define	SC8547_IBUS_UCP_DIS_MASK            0x80
#define	SC8547_IBUS_UCP_DIS_SHIFT           7
#define	SC8547_IBUS_UCP_ENABLE              0
#define	SC8547_IBUS_UCP_DISABLE             1

#define	SC8547_IBUS_OCP_DIS_MASK            0x40
#define	SC8547_IBUS_OCP_DIS_SHIFT           6
#define	SC8547_IBUS_OCP_ENABLE              0
#define	SC8547_IBUS_OCP_DISABLE             1

#define SC8547_IBUS_UCP_FALL_DEGLITCH_SET_MASK   0x20
#define SC8547_IBUS_UCP_FALL_DEGLITCH_SET_SHIFT  5
#define SC8547_IBUS_UCP_FALL_DEGLITCH_SET_10US   0
#define SC8547_IBUS_UCP_FALL_DEGLITCH_SET_5MS    1

#define	SC8547_IBUS_OCP_MASK                0x0F
#define	SC8547_IBUS_OCP_SHIFT               0
#define	SC8547_IBUS_OCP_BASE                1200
#define	SC8547_IBUS_OCP_LSB                 300

/* Register 06h */
#define SC8547_REG_06                       0x06
#define SC8547_TSHUT_FLAG_MASK              0x80
#define SC8547_TSHUT_FLAG_SHIFT             7

#define SC8547_TSHUT_STAT_MASK              0x40
#define SC8547_TSHUT_STAT_SHIFT             6

#define SC8547_VBUS_ERRORLO_STAT_MASK       0x20
#define SC8547_VBUS_ERRORLO_STAT_SHIFT      5

#define SC8547_VBUS_ERRORHI_STAT_MASK       0x10
#define SC8547_VBUS_ERRORHI_STAT_SHIFT      4

#define SC8547_SS_TIMEOUT_FLAG_MASK         0x08
#define SC8547_SS_TIMEOUT_FLAG_SHIFT        3

#define SC8547_CP_SWITCHING_STAT_MASK       0x04
#define SC8547_CP_SWITCHING_STAT_SHIFT      2

#define SC8547_REG_TIMEOUT_FLAG_MASK        0x02
#define SC8547_REG_TIMEOUT_FLAG_SHIFT       1

#define SC8547_PIN_DIAG_FALL_FLAG_MASK      0x01
#define SC8547_PIN_DIAG_FALL_FLAG_SHIFT     0

/* Register 07h */
#define SC8547_REG_07                       0x07
#define SC8547_CHG_EN_MASK                  0x80
#define SC8547_CHG_EN_SHIFT                 7
#define SC8547_CHG_ENABLE                   1
#define SC8547_CHG_DISABLE                  0

#define SC8547_REG_RESET_MASK               0x40
#define SC8547_REG_RESET_SHIFT              6
#define SC8547_NO_REG_RESET                 0
#define SC8547_RESET_REG                    1

#define SC8547_FREQ_SHIFT_MASK				0x18
#define SC8547_FREQ_SHIFT_SHIFT				3
#define SC8547_FREQ_SHIFT_NORMINAL			0
#define SC8547_FREQ_SHIFT_POSITIVE10		1
#define SC8547_FREQ_SHIFT_NEGATIVE10		2
#define SC8547_FREQ_SHIFT_SPREAD_SPECTRUM	3

#define SC8547_FSW_SET_MASK                 0x70
#define SC8547_FSW_SET_SHIFT                4
#define SC8547_FSW_SET_300KHZ               0
#define SC8547_FSW_SET_350KHZ               1
#define SC8547_FSW_SET_400KHZ               2
#define SC8547_FSW_SET_450KHZ               3
#define SC8547_FSW_SET_500KHZ               4
#define SC8547_FSW_SET_550KHZ               5
#define SC8547_FSW_SET_600KHZ               6
#define SC8547_FSW_SET_750KHZ               7

/* Register 08h */
#define SC8547_REG_08                       0x08
#define SC8547_SS_TIMEOUT_SET_MASK          0xE0
#define SC8547_SS_TIMEOUT_SET_SHIFT         5
#define SC8547_SS_TIMEOUT_DISABLE           0
#define SC8547_SS_TIMEOUT_40MS              1
#define SC8547_SS_TIMEOUT_80MS              2
#define SC8547_SS_TIMEOUT_320MS             3
#define SC8547_SS_TIMEOUT_1280MS            4
#define SC8547_SS_TIMEOUT_5120MS            5
#define SC8547_SS_TIMEOUT_20480MS           6
#define SC8547_SS_TIMEOUT_81920MS           7

#define SC8547_REG_TIMEOUT_DIS_MASK         0x10
#define SC8547_REG_TIMEOUT_DIS_SHIFT        4
#define SC8547_650MS_REG_TIMEOUT_ENABLE     0
#define SC8547_650MS_REG_TIMEOUT_DISABLE    1

#define SC8547_VOUT_OVP_DIS_MASK            0x08
#define SC8547_VOUT_OVP_DIS_SHIFT           3
#define SC8547_VOUT_OVP_ENABLE              0
#define SC8547_VOUT_OVP_DISABLE             1

#define SC8547_SET_IBAT_SNS_RES_MASK        0x04
#define SC8547_SET_IBAT_SNS_RES_SHIFT       2
#define SC8547_SET_IBAT_SNS_RES_5MHM        0
#define SC8547_SET_IBAT_SNS_RES_2MHM        1

#define SC8547_VBUS_PD_EN_MASK              0x02
#define SC8547_VBUS_PD_EN_SHIFT             1
#define SC8547_VBUS_PD_ENABLE               1
#define SC8547_VBUS_PD_DISABLE              0

/* Register 09h */
#define SC8547_REG_09                       0x09
#define SC8547_CHARGE_MODE_MASK             0x80
#define SC8547_CHARGE_MODE_SHIFT            7
#define SC8547_CHARGE_MODE_2_1              0
#define SC8547_CHARGE_MODE_1_1              1

#define SC8547_POR_FLAG_MASK                0x40
#define SC8547_POR_FLAG_SHIFT               6

#define SC8547_IBUS_UCP_RISE_FLAG_MASK      0x20
#define SC8547_IBUS_UCP_RISE_FLAG_SHIFT     5

#define SC8547_IBUS_UCP_RISE_MASK_MASK      0x10
#define SC8547_IBUS_UCP_RISE_MASK_SHIFT     4
#define SC8547_IBUS_UCP_RISE_MASK           1
#define SC8547_IBUS_UCP_RISE_NOT_MAST       0

#define SC8547_WD_TIMEOUT2_FLAG_MASK        0x08
#define SC8547_WD_TIMEOUT2_FLAG_SHIFT       3

#define SC8547_WATCHDOG_MASK                0x07
#define SC8547_WATCHDOG_SHIFT               0
#define SC8547_WATCHDOG_DIS                 0
#define SC8547_WATCHDOG_200MS               1
#define SC8547_WATCHDOG_500MS               2
#define SC8547_WATCHDOG_1S                  3
#define SC8547_WATCHDOG_5S                  4
#define SC8547_WATCHDOG_30S                 5

/* Register 0Ah */
#define SC8547_REG_0A                       0x0A
#define SC8547_VBAT_REG_EN_MASK             0x80
#define SC8547_VBAT_REG_EN_SHIFT            7
#define SC8547_VBAT_REG_ENABLE              1
#define SC8547_VBAT_REG_DISABLE             0

#define SC8547_VBATREG_ACTIVE_STAT_MASK     0x10
#define SC8547_VBATREG_ACTIVE_STAT_SHIFT    4

#define SC8547_VBATREG_ACTIVE_FLAG_MASK     0x08
#define SC8547_VBATREG_ACTIVE_FLAG_SHIFT    3

#define SC8547_VBATREG_ACTIVE_MASK_MASK     0x04
#define SC8547_VBATREG_ACTIVE_MASK_SHIFT    2
#define SC8547_VBATREG_ACTIVE_MASK          1
#define SC8547_VBATREG_ACTIVE_NOT_MAST      0

#define SC8547_SET_VBATREG_MASK             0x03
#define SC8547_SET_VBATREG_SHIFT            0
#define SC8547_SET_VBATREG_50MV             0
#define SC8547_SET_VBATREG_100MV            1
#define SC8547_SET_VBATREG_150MV            2
#define SC8547_SET_VBATREG_200MV            3

/* Register 0Bh */
#define SC8547_REG_0B                       0x0B
#define SC8547_IBAT_REG_EN_MASK             0x80
#define SC8547_IBAT_REG_EN_SHIFT            7
#define SC8547_IBAT_REG_ENABLE              1
#define SC8547_IBAT_REG_DISABLE             0

#define SC8547_IBATREG_ACTIVE_STAT_MASK     0x10
#define SC8547_IBATREG_ACTIVE_STAT_SHIFT    4

#define SC8547_IBATREG_ACTIVE_FLAG_MASK     0x08
#define SC8547_IBATREG_ACTIVE_FLAG_SHIFT    3

#define SC8547_IBATREG_ACTIVE_MASK_MASK     0x04
#define SC8547_IBATREG_ACTIVE_MASK_SHIFT    2
#define SC8547_IBATREG_ACTIVE_MASK          1
#define SC8547_IBATREG_ACTIVE_NOT_MAST      0

#define SC8547_SET_IBATREG_MASK             0x03
#define SC8547_SET_IBATREG_SHIFT            0
#define SC8547_SET_IBATREG_200MA            0
#define SC8547_SET_IBATREG_300MA            1
#define SC8547_SET_IBATREG_400MA            2
#define SC8547_SET_IBATREG_500MA            3


/* Register 0Ch */
#define SC8547_REG_0C                       0x0C
#define SC8547_IBUS_REG_EN_MASK             0x80
#define SC8547_IBUS_REG_EN_SHIFT            7
#define SC8547_IBUS_REG_ENABLE              1
#define SC8547_IBUS_REG_DISABLE             0

#define SC8547_IBUSREG_ACTIVE_STAT_MASK     0x40
#define SC8547_IBUSREG_ACTIVE_STAT_SHIFT    6

#define SC8547_IBUSREG_ACTIVE_FLAG_MASK     0x20
#define SC8547_IBUSREG_ACTIVE_FLAG_SHIFT    5

#define SC8547_IBUSREG_ACTIVE_MASK_MASK     0x10
#define SC8547_IBUSREG_ACTIVE_MASK_SHIFT    4
#define SC8547_IBUSREG_ACTIVE_MASK          1
#define SC8547_IBUSREG_ACTIVE_NOT_MAST      0

#define SC8547_SET_IBUSREG_MASK             0x0F
#define SC8547_SET_IBUSREG_SHIFT            0
#define	SC8547_SET_IBUSREG_BASE             1200
#define	SC8547_SET_IBUSREG_LSB              300

/* Register 0Dh */
#define SC8547_REG_0D                       0x0D
#define SC8547_PMID2OUT_UVP_MASK            0xC0
#define SC8547_PMID2OUT_UVP_SHIFT           6
#define SC8547_PMID2OUT_UVP_U_50MV          0
#define SC8547_PMID2OUT_UVP_U_100MV         1
#define SC8547_PMID2OUT_UVP_U_150MV         2
#define SC8547_PMID2OUT_UVP_U_200MV         3

#define SC8547_PMID2OUT_OVP_MASK            0x30
#define SC8547_PMID2OUT_OVP_SHIFT           4
#define SC8547_PMID2OUT_OVP_200MV           0
#define SC8547_PMID2OUT_OVP_300MV           1
#define SC8547_PMID2OUT_OVP_400MV           2
#define SC8547_PMID2OUT_OVP_500MV           3

#define SC8547_PMID2OUT_UVP_FLAG_MASK       0x08
#define SC8547_PMID2OUT_UVP_FLAG_SHIFT      3

#define SC8547_PMID2OUT_OVP_FLAG_MASK       0x04
#define SC8547_PMID2OUT_OVP_FLAG_SHIFT      2

#define SC8547_PMID2OUT_UVP_STAT_MASK       0x01
#define SC8547_PMID2OUT_UVP_STAT_SHIFT      1

#define SC8547_PMID2OUT_OVP_STAT_MASK       0x01
#define SC8547_PMID2OUT_OVP_STAT_SHIFT      0

/* Register 0Eh */
#define SC8547_REG_0E                       0x0E
#define SC8547_VOUT_OVP_STAT_MASK           0x80
#define SC8547_VOUT_OVP_STAT_SHIFT          7

#define SC8547_VBAT_OVP_STAT_MASK           0x40
#define SC8547_VBAT_OVP_STAT_SHIFT          6

#define SC8547_IBAT_OCP_STAT_MASK           0x20
#define SC8547_IBAT_OCP_STAT_SHIFT          5

#define SC8547_VBUS_OVP_STAT_MASK           0x10
#define SC8547_VBUS_OVP_STAT_SHIFT          4

#define SC8547_IBUS_OCP_STAT_MASK           0x08
#define SC8547_IBUS_OCP_STAT_SHIFT          3

#define SC8547_IBUS_UCP_FALL_STAT_MASK      0x04
#define SC8547_IBUS_UCP_FALL_STAT_SHIFT     2

#define SC8547_ADAPTER_INSERT_STAT_MASK     0x02
#define SC8547_ADAPTER_INSERT_STAT_SHIFT    1

#define SC8547_VBAT_INSERT_STAT_MASK        0x01
#define SC8547_VBAT_INSERT_STAT_SHIFT       0

/* Register 0Fh */
#define SC8547_REG_0F                       0x0F
#define SC8547_VOUT_OVP_FLAG_MASK           0x80
#define SC8547_VOUT_OVP_FLAG_SHIFT          7

#define SC8547_VBAT_OVP_FLAG_MASK           0x40
#define SC8547_VBAT_OVP_FLAG_SHIFT          6

#define SC8547_IBAT_OCP_FLAG_MASK           0x20
#define SC8547_IBAT_OCP_FLAG_SHIFT          5

#define SC8547_VBUS_OVP_FLAG_MASK           0x10
#define SC8547_VBUS_OVP_FLAG_SHIFT          4

#define SC8547_IBUS_OCP_FLAG_MASK           0x08
#define SC8547_IBUS_OCP_FLAG_SHIFT          3

#define SC8547_IBUS_UCP_FALL_FLAG_MASK      0x04
#define SC8547_IBUS_UCP_FALL_FLAG_SHIFT     2

#define SC8547_ADAPTER_INSERT_FLAG_MASK     0x02
#define SC8547_ADAPTER_INSERT_FLAG_SHIFT    1

#define SC8547_VBAT_INSERT_FLAG_MASK        0x01
#define SC8547_VBAT_INSERT_FLAG_SHIFT       0

/* Register 10h */
#define SC8547_REG_10                       0x10
#define SC8547_VOUT_OVP_MASK_MASK           0x80
#define SC8547_VOUT_OVP_MASK_SHIFT          7

#define SC8547_VBAT_OVP_MASK_MASK           0x40
#define SC8547_VBAT_OVP_MASK_SHIFT          6

#define SC8547_IBAT_OCP_MASK_MASK           0x20
#define SC8547_IBAT_OCP_MASK_SHIFT          5

#define SC8547_VBUS_OVP_MASK_MASK           0x10
#define SC8547_VBUS_OVP_MASK_SHIFT          4

#define SC8547_IBUS_OCP_MASK_MASK           0x08
#define SC8547_IBUS_OCP_MASK_SHIFT          3

#define SC8547_IBUS_UCP_FALL_MASK_MASK      0x04
#define SC8547_IBUS_UCP_FALL_MASK_SHIFT     2

#define SC8547_ADAPTER_INSERT_MASK_MASK     0x02
#define SC8547_ADAPTER_INSERT_MASK_SHIFT    1

#define SC8547_VBAT_INSERT_MASK_MASK        0x01
#define SC8547_VBAT_INSERT_MASK_SHIFT       0

/* Register 11h */
#define SC8547_REG_11                       0x11
#define SC8547_ADC_EN_MASK                  0x80
#define SC8547_ADC_EN_SHIFT                 7
#define SC8547_ADC_ENABLE                   1
#define SC8547_ADC_DISABLE                  0

#define SC8547_ADC_RATE_MASK                0x40
#define SC8547_ADC_RATE_SHIFT               6
#define SC8547_ADC_RATE_CONTINOUS           0
#define SC8547_ADC_RATE_ONESHOT             1

#define SC8547_ADC_DONE_STAT_MASK           0x04
#define SC8547_ADC_DONE_STAT_SHIFT          2

#define SC8547_ADC_DONE_FLAG_MASK           0x02
#define SC8547_ADC_DONE_FLAG_SHIFT          1

#define SC8547_ADC_DONE_MASK_MASK           0x01
#define SC8547_ADC_DONE_MASK_SHIFT          0

/* Register 12h */
#define SC8547_REG_12                       0x12
#define SC8547_VBUS_ADC_DIS_MASK            0x40
#define SC8547_VBUS_ADC_DIS_SHIFT           6
#define SC8547_VBUS_ADC_ENABLE              0
#define SC8547_VBUS_ADC_DISABLE             1

#define SC8547_VAC_ADC_DIS_MASK             0x20
#define SC8547_VAC_ADC_DIS_SHIFT            5
#define SC8547_VAC_ADC_ENABLE               0
#define SC8547_VAC_ADC_DISABLE              1

#define SC8547_VOUT_ADC_DIS_MASK            0x10
#define SC8547_VOUT_ADC_DIS_SHIFT           4
#define SC8547_VOUT_ADC_ENABLE              0
#define SC8547_VOUT_ADC_DISABLE             1

#define SC8547_VBAT_ADC_DIS_MASK            0x08
#define SC8547_VBAT_ADC_DIS_SHIFT           3
#define SC8547_VBAT_ADC_ENABLE              0
#define SC8547_VBAT_ADC_DISABLE             1

#define SC8547_IBAT_ADC_DIS_MASK            0x04
#define SC8547_IBAT_ADC_DIS_SHIFT           2
#define SC8547_IBAT_ADC_ENABLE              0
#define SC8547_IBAT_ADC_DISABLE             1

#define SC8547_IBUS_ADC_DIS_MASK            0x02
#define SC8547_IBUS_ADC_DIS_SHIFT           1
#define SC8547_IBUS_ADC_ENABLE              0
#define SC8547_IBUS_ADC_DISABLE             1

#define SC8547_TDIE_ADC_DIS_MASK            0x01
#define SC8547_TDIE_ADC_DIS_SHIFT           0
#define SC8547_TDIE_ADC_ENABLE              0
#define SC8547_TDIE_ADC_DISABLE             1

/* Register 13h */
#define SC8547_REG_13                       0x13
#define SC8547_IBUS_POL_H_MASK              0x0F
#define SC8547_IBUS_ADC_LSB                 1875/1000

/* Register 14h */
#define SC8547_REG_14                       0x14
#define SC8547_IBUS_POL_L_MASK              0xFF

/* Register 15h */
#define SC8547_REG_15                       0x15
#define SC8547_VBUS_POL_H_MASK              0x0F
#define SC8547_VBUS_ADC_LSB                 375/100

/* Register 16h */
#define SC8547_REG_16                       0x16
#define SC8547_VBUS_POL_L_MASK              0xFF

/* Register 17h */
#define SC8547_REG_17                       0x17
#define SC8547_VAC_POL_H_MASK               0x0F
#define SC8547_VAC_ADC_LSB                  5

/* Register 18h */
#define SC8547_REG_18                       0x18
#define SC8547_VAC_POL_L_MASK               0xFF

/* Register 19h */
#define SC8547_REG_19                       0x19
#define SC8547_VOUT_POL_H_MASK              0x0F
#define SC8547_VOUT_ADC_LSB                 125/100

/* Register 1Ah */
#define SC8547_REG_1A                       0x1A
#define SC8547_VOUT_POL_L_MASK              0xFF

/* Register 1Bh */
#define SC8547_REG_1B                       0x1B
#define SC8547_VBAT_POL_H_MASK              0x0F
#define SC8547_VBAT_ADC_LSB                 125/100

/* Register 1Ch */
#define SC8547_REG_1C                       0x1C
#define SC8547_VBAT_POL_L_MASK              0xFF

/* Register 1Dh */
#define SC8547_REG_1D                       0x1D
#define SC8547_IBAT_POL_H_MASK              0x0F
#define SC8547_IBAT_ADC_LSB                 125/100

/* Register 1Eh */
#define SC8547_REG_1E                       0x1E
#define SC8547_IBAT_POL_L_MASK              0xFF

/* Register 1Fh */
#define SC8547_REG_1F                       0x1F
#define SC8547_TDIE_POL_H_MASK              0x01
#define SC8547_TDIE_ADC_LSB                 5/10

/* Register 20h */
#define SC8547_REG_20                       0x20
#define SC8547_TDIE_POL_L_MASK              0xFF
#define SC8547_TDIE_MIN 0
#define SC8547_TDIE_MAX 200

/* Register 21h */
#define SC8547_REG_21                       0x21
#define SC8547_DM_500K_PD_EN_MASK           0x80
#define SC8547_DM_500K_PD_EN_SHIFT          7
#define SC8547_DM_500K_PD_ENABLE            1
#define SC8547_DM_500K_PD_DISABLE           0

#define SC8547_DP_500K_PD_EN_MASK           0x40
#define SC8547_DP_500K_PD_EN_SHIFT          6
#define SC8547_DP_500K_PD_ENABLE            1
#define SC8547_DP_500K_PD_DISABLE           0

#define SC8547_DM_20K_PD_EN_MASK            0x20
#define SC8547_DM_20K_PD_EN_SHIFT           5
#define SC8547_DM_20K_PD_ENABLE             1
#define SC8547_DM_20K_PD_DISABLE            0

#define SC8547_DP_20K_PD_EN_MASK            0x10
#define SC8547_DP_20K_PD_EN_SHIFT           4
#define SC8547_DP_20K_PD_ENABLE             1
#define SC8547_DP_20K_PD_DISABLE            0

#define SC8547_DM_SINK_EN_MASK              0x08
#define SC8547_DM_SINK_EN_SHIFT             3
#define SC8547_DM_SINK_ENABLE               1
#define SC8547_DM_SINK_DISABLE              0

#define SC8547_DP_SINK_EN_MASK              0x04
#define SC8547_DP_SINK_EN_SHIFT             2
#define SC8547_DP_SINK_ENABLE               1
#define SC8547_DP_SINK_DISABLE              0

#define SC8547_DP_SRC_10UA_MASK             0x02
#define SC8547_DP_SRC_10UA_SHIFT            1
#define SC8547_DP_SRC_250UA                 0
#define SC8547_DP_SRC_10UA                  1

#define SC8547_DPDM_EN_MASK                 0x01
#define SC8547_DPDM_EN_SHIFT                0
#define SC8547_DPDM_ENABLE                  1
#define SC8547_DPDM_DISABLE                 0


/* Register 22h */
#define SC8547_REG_22                       0x22
#define SC8547_DPDM_OVP_DIS_MASK            0x40
#define SC8547_DPDM_OVP_DIS_SHIFT           6
#define SC8547_DPDM_OVP_ENABLE              0
#define SC8547_DPDM_OVP_DISABLE             1

#define SC8547_DM_BUF_MASK                  0x30
#define SC8547_DM_BUF_SHIFT                 4
#define SC8547_DM_BUF_600MV                 0
#define SC8547_DM_BUF_2000MV                1
#define SC8547_DM_BUF_2700MV                2
#define SC8547_DM_BUF_3300MV                3

#define SC8547_DP_BUF_MASK                  0x0C
#define SC8547_DP_BUF_SHIFT                 2
#define SC8547_DP_BUF_600MV                 0
#define SC8547_DP_BUF_2000MV                1
#define SC8547_DP_BUF_2700MV                2
#define SC8547_DP_BUF_3300MV                3

#define SC8547_DM_BUF_EN_MASK               0x02
#define SC8547_DM_BUF_EN_SHIFT              1
#define SC8547_DM_BUF_ENABLE                1
#define SC8547_DM_BUF_DISABLE               0

#define SC8547_DP_BUF_EN_MASK               0x01
#define SC8547_DP_BUF_EN_SHIFT              0
#define SC8547_DP_BUF_ENABLE                1
#define SC8547_DP_BUF_DISABLE               0

/* Register 23h */
#define SC8547_REG_23                       0x23
#define SC8547_VDM_RD_MASK                  0x38
#define SC8547_VDM_RD_SHIFT                 3
#define SC8547_VDM_RD_0MV_325MV             0
#define SC8547_VDM_RD_325MV_1000MV          1
#define SC8547_VDM_RD_1000MV_1350MV         2
#define SC8547_VDM_RD_1350MV_2200MV         3
#define SC8547_VDM_RD_2200MV_3000MV         4
#define SC8547_VDM_RD_3000MV_3300MV         5

#define SC8547_VDP_RD_MASK                  0x07
#define SC8547_VDP_RD_SHIFT                 3
#define SC8547_VDP_RD_0MV_325MV             0
#define SC8547_VDP_RD_325MV_1000MV          1
#define SC8547_VDP_RD_1000MV_1350MV         2
#define SC8547_VDP_RD_1350MV_2200MV         3
#define SC8547_VDP_RD_2200MV_3000MV         4
#define SC8547_VDP_RD_3000MV_3300MV         5

/* Register 24h */
#define SC8547_REG_24                       0x24
#define SC8547_DM_LOW_MASK_MASK             0x40
#define SC8547_DM_LOW_MASK_SHIFT            6

#define SC8547_DM_LOW_FLAG_MASK             0x20
#define SC8547_DM_LOW_FLAG_SHIFT            5

#define SC8547_DP_LOW_MASK_MASK             0x10
#define SC8547_DP_LOW_MASK_SHIFT            4

#define SC8547_DP_LOW_FLAG_MASK             0x08
#define SC8547_DP_LOW_FLAG_SHIFT            3

#define SC8547_DPDM_OVP_MASK_MASK           0x04
#define SC8547_DPDM_OVP_MASK_SHIFT          2

#define SC8547_DPDM_OVP_FLAG_MASK           0x02
#define SC8547_DPDM_OVP_FLAG_SHIFT          1

#define SC8547_DPDM_OVP_STAT_MASK           0x01
#define SC8547_DPDM_OVP_STAT_SHIFT          0

/* Register 2Bh */
#define SC8547_REG_2B                       0x2B
#define SC8547_VOOC_EN_MASK                 0x80
#define SC8547_VOOC_EN_SHIFT                7
#define SC8547_VOOC_ENABLE                  1
#define SC8547_VOOC_DISABLE                 0

#define SC8547_SOFT_RESET_MASK              0x02
#define SC8547_SOFT_RESET_SHIFT             1
#define SC8547_SOFT_RESET                   1

#define SC8547_SEND_SEQ_MASK                0x01
#define SC8547_SEND_SEQ_SHIFT               0
#define SC8547_SEND_SEQ                     1

/* Register 2Ch */
#define SC8547_REG_2C                       0x2C
#define SC8547_TX_WDATA_POL_H_MASK          0x03

/* Register 2Dh */
#define SC8547_REG_2D                       0x2D
#define SC8547_TX_WDATA_POL_L_MASK          0xFF

/* Register 2Eh */
#define SC8547_REG_2E                       0x2E
#define SC8547_RX_RDATA_POL_H_MASK          0xFF

/* Register 2Fh */
#define SC8547_REG_2F                       0x2F
#define SC8547_PULSE_FILTERED_STAT_MASK     0x80
#define SC8547_PULSE_FILTERED_STAT_SHIFT    7

#define SC8547_NINTH_CLK_ERR_FALG_MASK      0x40
#define SC8547_NINTH_CLK_ERR_FALG_SHIFT     6

#define SC8547_TXSEQ_DONE_FLAG_MASK         0x20
#define SC8547_TXSEQ_DONE_FLAG_SHIFT        5

#define SC8547_ERR_TRANS_DET_FLAG_MASK      0x10
#define SC8547_ERR_TRANS_DET_FLAG_SHIFT     4

#define SC8547_TXDATA_WR_FAIL_FLAG_MASK     0x08
#define SC8547_TXDATA_WR_FAIL_FLAG_SHIFT    3

#define SC8547_RX_START_FLAG_MASK           0x04
#define SC8547_RX_START_FLAG_SHIFT          2

#define SC8547_RXDATA_DONE_FLAG_MASK        0x02
#define SC8547_RXDATA_DONE_FLAG_SHIFT       1

#define SC8547_TXDATA_DONE_FLAG_MASK        0x01
#define SC8547_TXDATA_DONE_FLAG_SHIFT       0

/* Register 30h */
#define SC8547_REG_30                       0x30
#define SC8547_NINTH_CLK_ERR_MASK_MASK      0x40
#define SC8547_NINTH_CLK_ERR_MASK_SHIFT     6

#define SC8547_TXSEQ_DONE_MASK_MASK         0x20
#define SC8547_TXSEQ_DONE_MASK_SHIFT        5

#define SC8547_ERR_TRANS_DET_MASK_MASK      0x10
#define SC8547_ERR_TRANS_DET_MASK_SHIFT     4

#define SC8547_TXDATA_WR_FAIL_MASK_MASK     0x08
#define SC8547_TXDATA_WR_FAIL_MASK_SHIFT    3

#define SC8547_RX_START_MASK_MASK           0x04
#define SC8547_RX_START_MASK_SHIFT          2

#define SC8547_RXDATA_DONE_MASK_MASK        0x02
#define SC8547_RXDATA_DONE_MASK_SHIFT       1

#define SC8547_TXDATA_DONE_MASK_MASK        0x01
#define SC8547_TXDATA_DONE_MASK_SHIFT       0

/* Register 31h */
#define SC8547_REG_31                       0x31
#define SC8547_PRE_WDATA_POL_H_MASK         0x03

/* Register 32h */
#define SC8547_REG_32                       0x32
#define SC8547_PRE_WDATA_POL_L_MASK         0xFF

/* Register 33h */
#define SC8547_REG_33                       0x33
#define SC8547_LOOSE_DET_MASK               0x80
#define SC8547_LOOSE_DET_SHIFT              7
#define SC8547_LOOSE_WINDOW_HIGH            0
#define SC8547_LOOSE_WINDOW_LOW             1

#define SC8547_LOW_CHECK_EN_MASK            0x40
#define SC8547_LOW_CHECK_EN_SHIFT           6

#define SC8547_END_TIME_SET_MASK            0x30
#define SC8547_END_TIME_SET_SHIFT           4
#define SC8547_NINE_5_5MS_AFTER_NINE_1_5MS  0
#define SC8547_NINE_6MS_AFTER_NINE_2MS      1
#define SC8547_NINE_6_5MS_AFTER_NINE_2_5MS  2
#define SC8547_NINE_7MS_AFTER_NINE_3MS      3

#define SC8547_RX_SAMPLE_TIME_MASK          0x03
#define SC8547_RX_SAMPLE_TIME_SHIFT         4
#define SC8547_RX_SAMPLE_TIME_5US           0
#define SC8547_RX_SAMPLE_TIME_150US         1
#define SC8547_RX_SAMPLE_TIME_300US         2
#define SC8547_RX_SAMPLE_TIME_450US         3

/* Register 36h */
#define SC8547_REG_36                       0x36
#define SC8547_DEVICE_ID_MASK               0xFF


#define SC8547_REG_3A						0x3A
#define SC8547_REG_34						0x34
#define SC8547_ADC_FREEZE_MASK              0x20

/* Register 3Ch */
#define SC8547_REG_3C                       0x3C
#define SC8547_VBUS_IN_RANGE_DIS_MASK       0x40
#define SC8547_VBUS_IN_RANGE_DIS_SHIFT      6
#define SC8547_VBUS_EN_RANGE_ENABLE         0
#define SC8547_VBUS_EN_RANGE_DISABLE        1

// #define SC8547D_DEVICE_ID        			0x49
//copy sc8547.h

/*********************command buffer**************/
/*avoid to rewrite the baudrate flags*/
#define SC8547D_CMD_SND_CMP BIT(2)
#define SC8547D_MASK_SND_CMP BIT(2)

/*********************message defination**********/
enum UFCS_MACHINE_STATE {
	STATE_NULL = 0,
	STATE_HANDSHAKE,
	STATE_PING,
	STATE_IDLE,
	STATE_WAIT_ACK,
	STATE_WAIT_MSG,
};

enum UFCS_ADDR {
	UFCS_ADDR_SRC = 1,
	UFCS_ADDR_SNK,
	UFCS_ADDR_CABLE,
};

enum UFCS_HEADER_TYPE {
	HEAD_TYPE_CTRL = 0,
	HEAD_TYPE_DATA = 1,
	HEAD_TYPE_VENDER = 2,
	HEAD_TYPE_NULL = 3,
};

enum UFCS_CTRL_MSG_TYPE {
	UFCS_CTRL_PING = 0,
	UFCS_CTRL_ACK,
	UFCS_CTRL_NCK,
	UFCS_CTRL_ACCEPT,
	UFCS_CTRL_SOFT_RESET,
	UFCS_CTRL_POWER_READY,
	UFCS_CTRL_GET_OUTPUT_CAP,
	UFCS_CTRL_GET_SOURCE_INFO,
	UFCS_CTRL_GET_SINK_INFO,
	UFCS_CTRL_GET_CABLE_INFO,
	UFCS_CTRL_GET_DEVICE_INFO,
	UFCS_CTRL_GET_ERROR_INFO,
	UFCS_CTRL_DETECT_CABLE_INFO,
	UFCS_CTRL_START_CABLE_DETECT,
	UFCS_CTRL_END_CABLE_DETECT,
	UFCS_CTRL_EXIT_UFCS_MODE,
	UFCS_CTRL_MAX,
};

enum UFCS_DATA_MSG_TYPE {
	UFCS_DATA_OUTPUT_CAP = 1,
	UFCS_DATA_REQUEST,
	UFCS_DATA_SOURCE_INFO,
	UFCS_DATA_SINK_INFO,
	UFCS_DATA_CABLE_INFO,
	UFCS_DATA_DEVICE_INFO,
	UFCS_DATA_ERROR_INFO,
	UFCS_DATA_CONFIG_WATCHDOG,
	UFCS_DATA_REFUSE,
	UFCS_DATA_VERIFY_REQUEST,
	UFCS_DATA_VERIFY_RESPONSE,
	UFCS_DATA_POWER_CHANGE,
	UFCS_DATA_UCT_REQUEST = 0xFF,
	UFCS_DATA_MAX,
};

enum UFCS_VND_MSG_TYPE {
	UFCS_VND_CHECK_POWER = 1,
	UFCS_VND_MAX,
};

enum UFCS_BAUD_RATE {
	UFCS_BAUD_115200 = 0,
	UFCS_BAUD_57600,
	UFCS_BAUD_38400,
	UFCS_BAUD_19200,
};

enum UFCS_REFUSE_REASON {
	REASON_CMD_NOT_DETECT = 1,
	REASON_ID_NOT_SUPPORT = 2,
	REASON_DEVICE_BUSY = 3,
	REASON_OVER_LIMIT = 4,
	REASON_OTHERS = 5,
};

struct hardware_error {
	bool dp_ovp;
	bool dm_ovp;
	bool temp_shutdown;
	bool wtd_timeout;
	bool hardreset;
};

struct receive_error {
	bool sent_cmp;
	bool msg_trans_fail;
	bool ack_rcv_timeout;
	bool data_rdy;
};

struct communication_error {
	bool baud_error;
	bool training_error;
	bool start_fail;
	bool byte_timeout;
	bool rx_len_error;
	bool rx_overflow;
	bool crc_error;
	bool stop_error;
	bool baud_change;
	bool bus_conflict;
};

struct sc8574d_error_flag {
	struct hardware_error hd_error;
	struct receive_error rcv_error;
	struct communication_error commu_error;
};

struct comm_msg {
	u16 header;
	u8 command;
	u16 vender;
	u8 len;
	u8 *buf;
};

struct source_cap {
	u64 min_cur : 8;
	u64 max_cur : 16;
	u64 min_vol : 16;
	u64 max_vol : 16;
	u64 vol_step : 1;
	u64 cur_step : 3;
	u64 id : 4;
};

struct request_pdo {
	u64 cur : 16;
	u64 vol : 16;
	u64 reserve : 28;
	u64 index : 4;
};

struct charger_power {
	u64 vnd_cmd : 16;
	u64 vnd_p1 : 16;
	u64 vnd_p2 : 16;
	u64 vnd_p3 : 16;
};

struct verify_request {
	u8 id;
	u8 phone_random[16];
};

struct verify_response {
	u8 encrypt_result[32];
	u8 adapter_random[16];
};

struct device_info {
	u64 software_ver : 16;
	u64 hardware_ver : 16;
	u64 ic_vender : 16;
	u64 dev_vender : 16;
};

struct src_info {
	u64 cur : 16;
	u64 vol : 16;
	u64 usb_temp : 8;
	u64 dev_temp : 8;
	u64 reserve : 16;
};

struct sink_info {
	u64 cur : 16;
	u64 vol : 16;
	u64 usb_temp : 8;
	u64 dev_temp : 8;
	u64 reserve : 16;
};

struct error_info {
	u32 output_ovp : 1;
	u32 output_uvp : 1;
	u32 output_ocp : 1;
	u32 output_scp : 1;
	u32 usb_otp : 1;
	u32 batt_otp : 1;
	u32 cc_ovp : 1;
	u32 dm_ovp : 1;
	u32 dp_ovp : 1;
	u32 input_ovp : 1;
	u32 input_uvp : 1;
	u32 drain_ovp : 1;
	u32 input_lost : 1;
	u32 crc : 1;
	u32 watchdog : 1;
	u32 reserve : 16;
};

struct config_watchdog {
	u16 timer;
};

struct refuse_msg {
	u32 reason : 8;
	u32 cmd : 8;
	u32 msg_type : 3;
	u32 reserve2 : 5;
	u32 msg_id : 4;
	u32 reserve1 : 4;
};

struct uct_mode {
	u16 msg_num : 8;
	u16 msg_type : 3;
	u16 device_addr : 3;
	u16 voltage_mode : 1;
	u16 reserved : 1;
};

/****************Message Construction Helper*********/
#define UFCS_HEADER_REV 0x01
#define UFCS_KEY_VER 0x01
#define OPLUS_DEV_VENDOR 0x22d9
#define OPLUS_IC_VENDOR 0x0

#define SC8547D_VENDOR_ID 0x0006
#define SC8547D_HARDWARE_VER 0x0001
#define SC8547D_SOFTWARE_VER 0x0001
#define DATA_LENGTH_BLANK 0x0
#define UCT_MODE_WATCHDOG 3000
#define UCT_MODE_IC_VEND 0xFFFF
#define UCT_MODE_DEV_VEND 0xFFFF

#define ABNORMAL_HARDWARE_VER_A_MIN 0x1006
#define ABNORMAL_HARDWARE_VER_A_MAX 0x1008
#define ABNORMAL_SOFTWARE_VER_A 0x0006
#define ABNORMAL_IC_VEND 0x0
#define ABNORMAL_DEV_VEND 0x0

#define VND_LENGTH_BLANK 0x2

#define UFCS_HEADER_DEV_TYPE_SHIFT 13
#define UFCS_HEADER_DEV_TYPE_MASK 0x7
#define UFCS_HEADER_MSG_ID_SHIFT 9
#define UFCS_HEADER_MSG_ID_MASK 0xF
#define UFCS_HEADER_REV_SHIFT 3
#define UFCS_HEADER_REV_MASK 0x3F
#define UFCS_HEADER_MSG_TYPE_SHIFT 0
#define UFCS_HEADER_MSG_TYPE_MASK 0x7

#define UFCS_HEADER(dev_type, msg_seq, msg_type)                               \
	((u16)(((dev_type)&UFCS_HEADER_DEV_TYPE_MASK)                          \
	       << UFCS_HEADER_DEV_TYPE_SHIFT) |                                \
	 (((msg_seq)&UFCS_HEADER_MSG_ID_MASK) << UFCS_HEADER_MSG_ID_SHIFT) |   \
	 (((UFCS_HEADER_REV)&UFCS_HEADER_REV_MASK) << UFCS_HEADER_REV_SHIFT) | \
	 (((msg_type)&UFCS_HEADER_MSG_TYPE_MASK)                               \
	  << UFCS_HEADER_MSG_TYPE_SHIFT))

#define UFCS_HEADER_ADDR(header) (header >> 13)
#define UFCS_HEADER_ID(header) ((header >> 9) & 0x0F)
#define UFCS_HEADER_VER(header) ((header >> 3) & 0x3F)
#define UFCS_HEADER_TYPE(header) (header & 0x07)

/*Protocol Data Length*/
#define COM_DATA_HEADER_POS 0
#define COM_DATA_CMD_TYPE_POS 2
#define COM_DATA_DATA_SIZE_POS 3
#define COM_DATA_DATA_POS 4

#define COM_HEADER_LEN 2
#define COM_CMD_ID_LEN 1
#define COM_VENDER_ID_LEN 2
#define COM_DATA_SIZE_LEN 1

#define COM_FIXED_FIELD_LEN                                                    \
	(COM_HEADER_LEN + COM_CMD_ID_LEN + COM_DATA_SIZE_LEN)

#define WRT_DATA_BGN_POS 4

#define READ_DATA_LEN_POS 3
#define READ_DATA_BGN_POS 4

#define READ_VENDER_LEN_POS 4
#define READ_VENDER_BGN_POS 5

#define MAX_RCV_BUFFER_SIZE 124
#define MAX_TX_BUFFER_SIZE 35
#define MAX_TX_MSG_SIZE (MAX_TX_BUFFER_SIZE - COM_FIXED_FIELD_LEN)

#define REQUEST_PDO_LENGTH 8
#define SINK_INFO_LENGTH 8
#define CONFIG_WTD_LENGTH 2
#define REFUSE_MSG_LENGTH 4
#define DEVICE_INFO_LENGTH 8
#define VERIFY_REQUEST_LENGTH 17
#define PHONE_RANDOM_LENGTH 16
#define VERIFY_RESPONSE_LENGTH 48
#define ADAPTER_RANDOM_LENGTH 16
#define ADAPTER_ENCRYPT_LENGTH 32
#define REQUEST_POWER_LENGTH 3

#define ATTRIBUTE_NOT_SUPPORTED 0
/***************************message parse helper**************************/
#define SWAP64(val)                                                            \
	((((val)&0xff00000000000000) >> 56) |                                  \
	 (((val)&0x00ff000000000000) >> 40) |                                  \
	 (((val)&0x0000ff0000000000) >> 24) |                                  \
	 (((val)&0x000000ff00000000) >> 8) |                                   \
	 (((val)&0x00000000ff000000) << 8) |                                   \
	 (((val)&0x0000000000ff0000) << 24) |                                  \
	 (((val)&0x000000000000ff00) << 40) |                                  \
	 (((val)&0x00000000000000ff) << 56))

#define SWAP32(val)                                                            \
	((((val)&0xff000000) >> 24) | (((val)&0x00ff0000) >> 8) |              \
	 (((val)&0x0000ff00) << 8) | (((val)&0x000000ff) << 24))

#define SWAP16(val) ((((val)&0xff00) >> 8) | (((val)&0x00ff) << 8))

#define POWER_EXCHANGE_GET_VOLT(val)                                           \
	(((val)&0x0000ff0000) >> 8) | (((val)&0x00ff000000) >> 24)

#define POWER_EXCHANGE_GET_CURR(val)                                           \
	(((val)&0x00000000ff) << 8) | (((val)&0x000000ff00) >> 8)

#define PDO_LENGTH 8
#define POWER_CHANGE_LENGTH 3
#define MAX_PDO_NUM 7
#define MIN_PDO_NUM 1
#define ERROR_INFO_NUM 16
#define ERROR_INFO_LEN 4
#define VALID_ERROR_INFO_BITS 0x7fff

static const char *adapter_error_info[16] = {
	"adapter output OVP!",
	"adapter outout UVP!",
	"adapter output OCP!",
	"adapter output SCP!",
	"adapter USB OTP!",
	"adapter inside OTP!",
	"adapter CCOVP!",
	"adapter D-OVP!",
	"adapter D+OVP!",
	"adapter input OVP!",
	"adapter input UVP!",
	"adapter drain over current!",
	"adapter input current loss!",
	"adapter CRC error!",
	"adapter watchdog timeout!",
	"invalid msg!",
};

struct oplus_sc8574d_ufcs {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;

	struct mutex chip_lock;
	atomic_t suspended;
	atomic_t i2c_err_count;
	struct kthread_worker *wq;
	struct kthread_work rcv_work;
	struct work_struct reply_work;
	struct delayed_work ufcs_service_work;
	struct completion rcv_cmp;
	struct wakeup_source *chip_ws;

	enum UFCS_MACHINE_STATE state;
	struct comm_msg dev_msg;
	struct comm_msg rcv_msg;
	struct oplus_ufcs_src_cap src_cap;
	struct sc8574d_error_flag flag;
	struct oplus_ufcs_error error;
	struct oplus_ufcs_sink_info info;
	u8 dev_buffer[MAX_TX_BUFFER_SIZE];
	u8 rcv_buffer[MAX_RCV_BUFFER_SIZE];
	int msg_send_id;
	int msg_recv_id;

	int get_flag_failed;
	int handshake_success;
	int ack_received;
	int msg_received;
	int soft_reset;
	int uct_mode;
	bool oplus_id;
	bool ufcs_enable;
	bool abnormal_id;
};

#define GET_VBAT_PREDATA_DEFAULT (0x64 << SC8547_DATA_H_SHIFT) | 0x02;
#define EXEC_TIME_THR	1500
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
int sc8547_subsys_init(void);
void sc8547_subsys_exit(void);
int sc8547_slave_subsys_init(void);
void sc8547_slave_subsys_exit(void);
#endif
#endif /*_OPLUS_SC8547D_H_*/
