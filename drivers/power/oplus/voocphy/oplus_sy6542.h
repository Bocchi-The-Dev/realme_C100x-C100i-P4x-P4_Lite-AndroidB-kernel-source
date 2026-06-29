#ifndef __SY6542_HEADER__
#define __SY6542_HEADER__

#define SY6542_REG_B2  0xb2
#define SY6542_REG_40  0x40

/* Register 00h */
#define SY6542_REG_00					0x00
#define	SY6542_BAT_OVP_DIS_MASK			0x80
#define	SY6542_BAT_OVP_DIS_SHIFT		7
#define	SY6542_BAT_OVP_ENABLE			0
#define	SY6542_BAT_OVP_DISABLE			1

#define SY6542_BAT_OVP_MASK				0x3F
#define SY6542_BAT_OVP_SHIFT			0
#define SY6542_BAT_OVP_BASE				3500
#define SY6542_BAT_OVP_LSB				25

/* Register 01h */
#define SY6542_REG_01					0x01
#define SY6542_BAT_OCP_MASK			    0x3F
#define SY6542_BAT_OCP_SHIFT		    0
#define SY6542_BAT_OCP_BASE			    2000
#define SY6542_BAT_OCP_LSB			    100

/* Register 02h */
#define SY6542_REG_02					0x02
#define	SY6542_AC_OVP_STAT_MASK		    0x20
#define	SY6542_AC_OVP_STAT_SHIFT		5

#define	SY6542_AC_OVP_FLAG_MASK		    0x10
#define	SY6542_AC_OVP_FALG_SHIFT		4

#define	SY6542_AC_OVP_MASK_MASK		    0x08
#define	SY6542_AC_OVP_MASK_SHIFT		3

#define SY6542_AC_OVP_MASK				0x07
#define SY6542_AC_OVP_SHIFT				0
#define SY6542_AC_OVP_BASE				11000
#define SY6542_AC_OVP_LSB				1000
#define SY6542_AC_OVP_6P5V				6500

/* Register 03h */
#define SY6542_REG_03					0x03
#define SY6542_VDROP_OVP_DIS_MASK		0x80
#define SY6542_VDROP_OVP_DIS_SHIFT	    7
#define SY6542_VDROP_OVP_ENABLE		    0
#define SY6542_VDROP_OVP_DISABLE		1

#define	SY6542_VDROP_OVP_STAT_MASK		0x10
#define	SY6542_VDROP_OVP_STAT_SHIFT		4

#define	SY6542_VDROP_OVP_FLAG_MASK      0x08
#define	SY6542_VDROP_OVP_FALG_SHIFT		3

#define	SY6542_VDROP_OVP_MASK_MASK      0x04
#define	SY6542_VDROP_OVP_MASK_SHIFT     2

#define SY6542_VDROP_OVP_THRESHOLD_MASK     0x02
#define SY6542_VDROP_OVP_THRESHOLD_SHIFT    1
#define SY6542_VDROP_OVP_THRESHOLD_300MV    0
#define SY6542_VDROP_OVP_THRESHOLD_400MV    1

#define SY6542_VDROP_DEGLITCH_SET_MASK      0x01
#define SY6542_VDROP_DEGLITCH_SET_SHIFT     0
#define SY6542_VDROP_DEGLITCH_SET_5MS       0
#define SY6542_VDROP_DEGLITCH_SET_8US       1

/* Register 04h */
#define SY6542_REG_04                       0x04
#define	SY6542_VBUS_OVP_DIS_MASK            0x80
#define	SY6542_VBUS_OVP_DIS_SHIFT           7
#define	SY6542_VBUS_OVP_ENABLE              0
#define	SY6542_VBUS_OVP_DISABLE             1

#define	SY6542_VBUS_OVP_MASK                0x7F
#define	SY6542_VBUS_OVP_SHIFT               0
#define	SY6542_VBUS_OVP_BASE                6000
#define	SY6542_VBUS_OVP_LSB                 50

/* Register 05h */
#define SY6542_REG_05                       0x05
#define	SY6542_IBUS_UCP_DIS_MASK            0x80
#define	SY6542_IBUS_UCP_DIS_SHIFT           7
#define	SY6542_IBUS_UCP_ENABLE              0
#define	SY6542_IBUS_UCP_DISABLE             1

#define	SY6542_IBUS_OCP_DIS_MASK            0x40
#define	SY6542_IBUS_OCP_DIS_SHIFT           6
#define	SY6542_IBUS_OCP_ENABLE              0
#define	SY6542_IBUS_OCP_DISABLE             1

#define SY6542_IBUS_UCP_FALL_DEGLITCH_SET_MASK   0x20
#define SY6542_IBUS_UCP_FALL_DEGLITCH_SET_SHIFT  5
#define SY6542_IBUS_UCP_FALL_DEGLITCH_SET_10US   0
#define SY6542_IBUS_UCP_FALL_DEGLITCH_SET_5MS    1

#define SY6542A_IBUS_UCP_FALL_DEGLITCH_SET_MASK   0x30
#define SY6542A_IBUS_UCP_FALL_DEGLITCH_SET_SHIFT  4
#define SY6542A_IBUS_UCP_FALL_DEGLITCH_SET_10US   0
#define SY6542A_IBUS_UCP_FALL_DEGLITCH_SET_5MS    1
#define SY6542A_IBUS_UCP_FALL_DEGLITCH_SET_50MS   2
#define SY6542A_IBUS_UCP_FALL_DEGLITCH_SET_100MS  3

#define	SY6542_IBUS_OCP_MASK                0x0F
#define	SY6542_IBUS_OCP_SHIFT               0
#define	SY6542_IBUS_OCP_BASE                1200
#define	SY6542_IBUS_OCP_LSB                 300

#define	SY6542A_IBUS_OCP_MASK                SY6542_IBUS_OCP_MASK

/* Register 06h */
#define SY6542_REG_06                       0x06
#define SY6542_TSHUT_FLAG_MASK              0x80
#define SY6542_TSHUT_FLAG_SHIFT             7

#define SY6542_TSHUT_STAT_MASK              0x40
#define SY6542_TSHUT_STAT_SHIFT             6

#define SY6542_VBUS_ERRORLO_STAT_MASK       0x20
#define SY6542_VBUS_ERRORLO_STAT_SHIFT      5

#define SY6542_VBUS_ERRORHI_STAT_MASK       0x10
#define SY6542_VBUS_ERRORHI_STAT_SHIFT      4

#define SY6542_SS_TIMEOUT_FLAG_MASK         0x08
#define SY6542_SS_TIMEOUT_FLAG_SHIFT        3

#define SY6542_CP_SWITCHING_STAT_MASK       0x04
#define SY6542_CP_SWITCHING_STAT_SHIFT      2

#define SY6542_REG_TIMEOUT_FLAG_MASK        0x02
#define SY6542_REG_TIMEOUT_FLAG_SHIFT       1

#define SY6542_NOT_WORK_FLAG_MASK      0x04
#define SY6542_NOT_WORK_FLAG_SHIFT     2

#define SY6542_CFLY_CDRV_FLAG_MASK      0x01
#define SY6542_CFLY_CDRV_FLAG_SHIFT     0

/* Register 07h */
#define SY6542_REG_07                       0x07
#define SY6542_CHG_EN_MASK                  0x80
#define SY6542_CHG_EN_SHIFT                 7
#define SY6542_CHG_ENABLE                   1
#define SY6542_CHG_DISABLE                  0

#define SY6542_REG_RESET_MASK               0x80
#define SY6542_REG_RESET_SHIFT              7
#define SY6542_NO_REG_RESET                 0
#define SY6542_RESET_REG                    1

#define SY6542_FREQ_SHIFT_MASK				0x18
#define SY6542_FREQ_SHIFT_SHIFT				3
#define SY6542_FREQ_SHIFT_NORMINAL			0
#define SY6542_FREQ_SHIFT_POSITIVE10		1
#define SY6542_FREQ_SHIFT_NEGATIVE10		2
#define SY6542_FREQ_SHIFT_SPREAD_SPECTRUM	3

#define SY6542_FSW_SET_MASK                 0x70
#define SY6542_FSW_SET_SHIFT                4
#define SY6542_FSW_SET_300KHZ               0
#define SY6542_FSW_SET_350KHZ               1
#define SY6542_FSW_SET_400KHZ               2
#define SY6542_FSW_SET_450KHZ               3
#define SY6542_FSW_SET_500KHZ               4
#define SY6542_FSW_SET_550KHZ               5
#define SY6542_FSW_SET_600KHZ               6
#define SY6542_FSW_SET_750KHZ               7

/* Register 08h */
#define SY6542_REG_08                       0x08
#define SY6542_SS_TIMEOUT_SET_MASK          0xE0
#define SY6542_SS_TIMEOUT_SET_SHIFT         5
#define SY6542_SS_TIMEOUT_DISABLE           0
#define SY6542_SS_TIMEOUT_40MS              1
#define SY6542_SS_TIMEOUT_80MS              2
#define SY6542_SS_TIMEOUT_320MS             3
#define SY6542_SS_TIMEOUT_1280MS            4
#define SY6542_SS_TIMEOUT_5120MS            5
#define SY6542_SS_TIMEOUT_20480MS           6
#define SY6542_SS_TIMEOUT_81920MS           7

#define SY6542_IBUS_UCP_TIMEOUT_FLAG_MASK   0x10
#define SY6542_IBUS_UCP_TIMEOUT_FLAG_SHIFT  4

#define SY6542_REG_TIMEOUT_DIS_MASK         0x10
#define SY6542_REG_TIMEOUT_DIS_SHIFT        4
#define SY6542_650MS_REG_TIMEOUT_ENABLE     0
#define SY6542_650MS_REG_TIMEOUT_DISABLE    1

#define SY6542_VOUT_OVP_DIS_MASK            0x08
#define SY6542_VOUT_OVP_DIS_SHIFT           3
#define SY6542_VOUT_OVP_ENABLE              0
#define SY6542_VOUT_OVP_DISABLE             1

#define SY6542_SET_IBAT_SNS_RES_MASK        0x04
#define SY6542_SET_IBAT_SNS_RES_SHIFT       2
#define SY6542_SET_IBAT_SNS_RES_5MHM        0
#define SY6542_SET_IBAT_SNS_RES_2MHM        1

#define SY6542_VBUS_PD_EN_MASK              0x02
#define SY6542_VBUS_PD_EN_SHIFT             1
#define SY6542_VBUS_PD_ENABLE               1
#define SY6542_VBUS_PD_DISABLE              0

/* Register 09h */
#define SY6542_REG_09                       0x09
#define SY6542_CHARGE_MODE_MASK             0x80
#define SY6542_CHARGE_MODE_SHIFT            7
#define SY6542_CHARGE_MODE_2_1              0
#define SY6542_CHARGE_MODE_1_1              1

#define SY6542_POR_FLAG_MASK                0x40
#define SY6542_POR_FLAG_SHIFT               6

#define SY6542_IBUS_UCP_RISE_FLAG_MASK      0x20
#define SY6542_IBUS_UCP_RISE_FLAG_SHIFT     5

#define SY6542_IBUS_UCP_RISE_MASK_MASK      0x10
#define SY6542_IBUS_UCP_RISE_MASK_SHIFT     4
#define SY6542_IBUS_UCP_RISE_MASK           1
#define SY6542_IBUS_UCP_RISE_NOT_MAST       0

#define SY6542_WD_TIMEOUT2_FLAG_MASK        0x08
#define SY6542_WD_TIMEOUT2_FLAG_SHIFT       3

#define SY6542_WATCHDOG_MASK                0x07
#define SY6542_WATCHDOG_SHIFT               0
#define SY6542_WATCHDOG_DIS                 0
#define SY6542_WATCHDOG_200MS               1
#define SY6542_WATCHDOG_500MS               2
#define SY6542_WATCHDOG_1S                  3
#define SY6542_WATCHDOG_5S                  4
#define SY6542_WATCHDOG_30S                 5

/* Register 0Ah */
#define SY6542_REG_0A                       0x0A
#define SY6542_VBAT_REG_EN_MASK             0x80
#define SY6542_VBAT_REG_EN_SHIFT            7
#define SY6542_VBAT_REG_ENABLE              1
#define SY6542_VBAT_REG_DISABLE             0

#define SY6542_IBUS_RCP_FLAG_MASK     0x10
#define SY6542_IBUS_RCP_FLAG_SHIFT    4

#define SY6542_TDIE_OTP_FLAG_MASK      0x08
#define SY6542_TDIE_OTP_FLAG_SHIFT     3

#define SY6542_VBUS_LOW_ERR_FLAG_MASK      0x04
#define SY6542_VBUS_LOW_ERR_FLAG_SHIFT     2

#define SY6542_VBUS_HIGH_ERR_FLAG_MASK      0x02
#define SY6542_VBUS_HIGH_ERR_FLAG_SHIFT     1

#define SY6542_VBATREG_ACTIVE_STAT_MASK     0x10
#define SY6542_VBATREG_ACTIVE_STAT_SHIFT    4

#define SY6542_VBATREG_ACTIVE_FLAG_MASK     0x08
#define SY6542_VBATREG_ACTIVE_FLAG_SHIFT    3

#define SY6542_VBATREG_ACTIVE_MASK_MASK     0x04
#define SY6542_VBATREG_ACTIVE_MASK_SHIFT    2
#define SY6542_VBATREG_ACTIVE_MASK          1
#define SY6542_VBATREG_ACTIVE_NOT_MAST      0

#define SY6542_SET_VBATREG_MASK             0x03
#define SY6542_SET_VBATREG_SHIFT            0
#define SY6542_SET_VBATREG_50MV             0
#define SY6542_SET_VBATREG_100MV            1
#define SY6542_SET_VBATREG_150MV            2
#define SY6542_SET_VBATREG_200MV            3

/* Register 0Bh */
#define SY6542_REG_0B                       0x0B
#define SY6542_IBAT_REG_EN_MASK             0x80
#define SY6542_IBAT_REG_EN_SHIFT            7
#define SY6542_IBAT_REG_ENABLE              1
#define SY6542_IBAT_REG_DISABLE             0

#define SY6542_IBATREG_ACTIVE_STAT_MASK     0x10
#define SY6542_IBATREG_ACTIVE_STAT_SHIFT    4

#define SY6542_IBATREG_ACTIVE_FLAG_MASK     0x08
#define SY6542_IBATREG_ACTIVE_FLAG_SHIFT    3

#define SY6542_IBATREG_ACTIVE_MASK_MASK     0x04
#define SY6542_IBATREG_ACTIVE_MASK_SHIFT    2
#define SY6542_IBATREG_ACTIVE_MASK          1
#define SY6542_IBATREG_ACTIVE_NOT_MAST      0

#define SY6542_SET_IBATREG_MASK             0x03
#define SY6542_SET_IBATREG_SHIFT            0
#define SY6542_SET_IBATREG_200MA            0
#define SY6542_SET_IBATREG_300MA            1
#define SY6542_SET_IBATREG_400MA            2
#define SY6542_SET_IBATREG_500MA            3


/* Register 0Ch */
#define SY6542_REG_0C                       0x0C
#define SY6542_IBUS_REG_EN_MASK             0x80
#define SY6542_IBUS_REG_EN_SHIFT            7
#define SY6542_IBUS_REG_ENABLE              1
#define SY6542_IBUS_REG_DISABLE             0

#define SY6542_IBUSREG_ACTIVE_STAT_MASK     0x40
#define SY6542_IBUSREG_ACTIVE_STAT_SHIFT    6

#define SY6542_WDT_TIMEOUT_FLAG_MASK        0x20
#define SY6542_WDT_TIMEOUT_FLAG_SHIFT       5

#define SY6542_IBUSREG_ACTIVE_FLAG_MASK     0x20
#define SY6542_IBUSREG_ACTIVE_FLAG_SHIFT    5

#define SY6542_IBUSREG_ACTIVE_MASK_MASK     0x10
#define SY6542_IBUSREG_ACTIVE_MASK_SHIFT    4
#define SY6542_IBUSREG_ACTIVE_MASK          1
#define SY6542_IBUSREG_ACTIVE_NOT_MAST      0

#define SY6542_SET_IBUSREG_MASK             0x0F
#define SY6542_SET_IBUSREG_SHIFT            0
#define	SY6542_SET_IBUSREG_BASE             1200
#define	SY6542_SET_IBUSREG_LSB              300

/* Register 0Dh */
#define SY6542_REG_0D                       0x0D
#define SY6542_PMID2OUT_UVP_MASK            0xC0
#define SY6542_PMID2OUT_UVP_SHIFT           6
#define SY6542_PMID2OUT_UVP_U_50MV          0
#define SY6542_PMID2OUT_UVP_U_100MV         1
#define SY6542_PMID2OUT_UVP_U_150MV         2
#define SY6542_PMID2OUT_UVP_U_200MV         3

#define SY6542_PMID2OUT_OVP_MASK            0x30
#define SY6542_PMID2OUT_OVP_SHIFT           4
#define SY6542_PMID2OUT_OVP_200MV           0
#define SY6542_PMID2OUT_OVP_300MV           1
#define SY6542_PMID2OUT_OVP_400MV           2
#define SY6542_PMID2OUT_OVP_500MV           3

#define SY6542_PMID2OUT_UVP_FLAG_MASK       0x08
#define SY6542_PMID2OUT_UVP_FLAG_SHIFT      3

#define SY6542_PMID2OUT_OVP_FLAG_MASK       0x04
#define SY6542_PMID2OUT_OVP_FLAG_SHIFT      2

#define SY6542_PMID2OUT_UVP_STAT_MASK       0x01
#define SY6542_PMID2OUT_UVP_STAT_SHIFT      1

#define SY6542_PMID2OUT_OVP_STAT_MASK       0x01
#define SY6542_PMID2OUT_OVP_STAT_SHIFT      0

/* Register 0Eh */
#define SY6542_REG_0E                       0x0E
#define SY6542_VOUT_OVP_STAT_MASK           0x80
#define SY6542_VOUT_OVP_STAT_SHIFT          7

#define SY6542_VBAT_OVP_STAT_MASK           0x40
#define SY6542_VBAT_OVP_STAT_SHIFT          6

#define SY6542_IBAT_OCP_STAT_MASK           0x20
#define SY6542_IBAT_OCP_STAT_SHIFT          5

#define SY6542_VBUS_OVP_STAT_MASK           0x10
#define SY6542_VBUS_OVP_STAT_SHIFT          4

#define SY6542_IBUS_OCP_STAT_MASK           0x08
#define SY6542_IBUS_OCP_STAT_SHIFT          3

#define SY6542_IBUS_UCP_FALL_STAT_MASK      0x04
#define SY6542_IBUS_UCP_FALL_STAT_SHIFT     2

#define SY6542_ADAPTER_INSERT_STAT_MASK     0x02
#define SY6542_ADAPTER_INSERT_STAT_SHIFT    1

#define SY6542_VBAT_INSERT_STAT_MASK        0x01
#define SY6542_VBAT_INSERT_STAT_SHIFT       0

/* Register 0Fh */
#define SY6542_REG_0F                       0x0F
#define SY6542_VOUT_OVP_FLAG_MASK           0x20
#define SY6542_VOUT_OVP_FLAG_SHIFT          5

#define SY6542_VBAT_OVP_FLAG_MASK           0x80
#define SY6542_VBAT_OVP_FLAG_SHIFT          7

#define SY6542_IBAT_OCP_FLAG_MASK           0x40
#define SY6542_IBAT_OCP_FLAG_SHIFT          6

#define SY6542_VBUS_OVP_FLAG_MASK           0x08
#define SY6542_VBUS_OVP_FLAG_SHIFT          3

#define SY6542_IBUS_OCP_FLAG_MASK           0x04
#define SY6542_IBUS_OCP_FLAG_SHIFT          2

#define SY6542_IBUS_UCP_FALL_FLAG_MASK      0x01
#define SY6542_IBUS_UCP_FALL_FLAG_SHIFT     2

#define SY6542_ADAPTER_INSERT_FLAG_MASK     0x02
#define SY6542_ADAPTER_INSERT_FLAG_SHIFT    1

#define SY6542_CONV_OCP_FLAG_MASK        0x01
#define SY6542_CONV_OCP_FLAG_SHIFT       0

/* Register 10h */
#define SY6542_REG_10                       0x10
#define SY6542_VOUT_OVP_MASK_MASK           0x80
#define SY6542_VOUT_OVP_MASK_SHIFT          7

#define SY6542_VBAT_OVP_MASK_MASK           0x40
#define SY6542_VBAT_OVP_MASK_SHIFT          6

#define SY6542_IBAT_OCP_MASK_MASK           0x20
#define SY6542_IBAT_OCP_MASK_SHIFT          5

#define SY6542_VBUS_OVP_MASK_MASK           0x10
#define SY6542_VBUS_OVP_MASK_SHIFT          4

#define SY6542_IBUS_OCP_MASK_MASK           0x08
#define SY6542_IBUS_OCP_MASK_SHIFT          3

#define SY6542_IBUS_UCP_FALL_MASK_MASK      0x04
#define SY6542_IBUS_UCP_FALL_MASK_SHIFT     2

#define SY6542_ADAPTER_INSERT_MASK_MASK     0x02
#define SY6542_ADAPTER_INSERT_MASK_SHIFT    1

#define SY6542_VBAT_INSERT_MASK_MASK        0x01
#define SY6542_VBAT_INSERT_MASK_SHIFT       0

/* Register 11h */
#define SY6542_REG_11                       0x11
#define SY6542_ADC_EN_MASK                  0x80
#define SY6542_ADC_EN_SHIFT                 7
#define SY6542_ADC_ENABLE                   1
#define SY6542_ADC_DISABLE                  0

#define SY6542_ADC_RATE_MASK                0x40
#define SY6542_ADC_RATE_SHIFT               6
#define SY6542_ADC_RATE_CONTINOUS           0
#define SY6542_ADC_RATE_ONESHOT             1

#define SY6542_ADC_DONE_STAT_MASK           0x04
#define SY6542_ADC_DONE_STAT_SHIFT          2

#define SY6542_ADC_DONE_FLAG_MASK           0x02
#define SY6542_ADC_DONE_FLAG_SHIFT          1

#define SY6542_ADC_DONE_MASK_MASK           0x01
#define SY6542_ADC_DONE_MASK_SHIFT          0

/* Register 12h */
#define SY6542_REG_12                       0x12
#define SY6542_VBUS_ADC_DIS_MASK            0x40
#define SY6542_VBUS_ADC_DIS_SHIFT           6
#define SY6542_VBUS_ADC_ENABLE              0
#define SY6542_VBUS_ADC_DISABLE             1

#define SY6542_VAC_ADC_DIS_MASK             0x20
#define SY6542_VAC_ADC_DIS_SHIFT            5
#define SY6542_VAC_ADC_ENABLE               0
#define SY6542_VAC_ADC_DISABLE              1

#define SY6542_VOUT_ADC_DIS_MASK            0x10
#define SY6542_VOUT_ADC_DIS_SHIFT           4
#define SY6542_VOUT_ADC_ENABLE              0
#define SY6542_VOUT_ADC_DISABLE             1

#define SY6542_VBAT_ADC_DIS_MASK            0x08
#define SY6542_VBAT_ADC_DIS_SHIFT           3
#define SY6542_VBAT_ADC_ENABLE              0
#define SY6542_VBAT_ADC_DISABLE             1

#define SY6542_IBAT_ADC_DIS_MASK            0x04
#define SY6542_IBAT_ADC_DIS_SHIFT           2
#define SY6542_IBAT_ADC_ENABLE              0
#define SY6542_IBAT_ADC_DISABLE             1

#define SY6542_IBUS_ADC_DIS_MASK            0x02
#define SY6542_IBUS_ADC_DIS_SHIFT           1
#define SY6542_IBUS_ADC_ENABLE              0
#define SY6542_IBUS_ADC_DISABLE             1

#define SY6542_TDIE_ADC_DIS_MASK            0x01
#define SY6542_TDIE_ADC_DIS_SHIFT           0
#define SY6542_TDIE_ADC_ENABLE              0
#define SY6542_TDIE_ADC_DISABLE             1

/* Register 13h */
#define SY6542_REG_13                       0x13
#define SY6542_IBUS_POL_H_MASK              0x0F
#define SY6542_IBUS_ADC_LSB                 4069/10000

/* Register 14h */
#define SY6542_REG_14                       0x14
#define SY6542_IBUS_POL_L_MASK              0xFF

/* Register 15h */
#define SY6542_REG_15                       0x15
#define SY6542_VBUS_POL_H_MASK              0x0F
#define SY6542_VBUS_ADC_LSB                 732/1000

/* Register 16h */
#define SY6542_REG_16                       0x16
#define SY6542_VBUS_POL_L_MASK              0xFF

/* Register 17h */
#define SY6542_REG_17                       0x17
#define SY6542_VAC_POL_H_MASK               0x0F
#define SY6542_VAC_ADC_LSB                  5

/* Register 18h */
#define SY6542_REG_18                       0x18
#define SY6542_VAC_POL_L_MASK               0xFF

/* Register 19h */
#define SY6542_REG_19                       0x19
#define SY6542_VOUT_POL_H_MASK              0x0F
#define SY6542_VOUT_ADC_LSB                 2197/10000

/* Register 1Ah */
#define SY6542_REG_1A                       0x1A
#define SY6542_VOUT_POL_L_MASK              0xFF

/* Register 1Bh */
#define SY6542_REG_1B                       0x1B
#define SY6542_VBAT_POL_H_MASK              0x0F
#define SY6542_VBAT_ADC_LSB                 125/100

/* Register 1Ch */
#define SY6542_REG_1C                       0x1C
#define SY6542_VBAT_POL_L_MASK              0xFF

/* Register 1Dh */
#define SY6542_REG_1D                       0x1D
#define SY6542_IBAT_POL_H_MASK              0x0F
#define SY6542_IBAT_ADC_LSB                 125/100

/* Register 1Eh */
#define SY6542_REG_1E                       0x1E
#define SY6542_IBAT_POL_L_MASK              0xFF

/* Register 1Fh */
#define SY6542_REG_1F                       0x1F
#define SY6542_TDIE_POL_H_MASK              0x01
#define SY6542_TDIE_ADC_LSB                 5/10

/* Register 20h */
#define SY6542_REG_20                       0x20
#define SY6542_TDIE_POL_L_MASK              0xFF
#define SY6542_TDIE_MIN 0
#define SY6542_TDIE_MAX 200


/* Register 21h */
#define SY6542_REG_21                       0x21
#define SY6542_DM_500K_PD_EN_MASK           0x80
#define SY6542_DM_500K_PD_EN_SHIFT          7
#define SY6542_DM_500K_PD_ENABLE            1
#define SY6542_DM_500K_PD_DISABLE           0

#define SY6542_DP_500K_PD_EN_MASK           0x40
#define SY6542_DP_500K_PD_EN_SHIFT          6
#define SY6542_DP_500K_PD_ENABLE            1
#define SY6542_DP_500K_PD_DISABLE           0

#define SY6542_DM_20K_PD_EN_MASK            0x20
#define SY6542_DM_20K_PD_EN_SHIFT           5
#define SY6542_DM_20K_PD_ENABLE             1
#define SY6542_DM_20K_PD_DISABLE            0

#define SY6542_DP_20K_PD_EN_MASK            0x10
#define SY6542_DP_20K_PD_EN_SHIFT           4
#define SY6542_DP_20K_PD_ENABLE             1
#define SY6542_DP_20K_PD_DISABLE            0

#define SY6542_DM_SINK_EN_MASK              0x08
#define SY6542_DM_SINK_EN_SHIFT             3
#define SY6542_DM_SINK_ENABLE               1
#define SY6542_DM_SINK_DISABLE              0

#define SY6542_DP_SINK_EN_MASK              0x04
#define SY6542_DP_SINK_EN_SHIFT             2
#define SY6542_DP_SINK_ENABLE               1
#define SY6542_DP_SINK_DISABLE              0

#define SY6542_DP_SRC_10UA_MASK             0x02
#define SY6542_DP_SRC_10UA_SHIFT            1
#define SY6542_DP_SRC_250UA                 0
#define SY6542_DP_SRC_10UA                  1

#define SY6542_DPDM_EN_MASK                 0x01
#define SY6542_DPDM_EN_SHIFT                0
#define SY6542_DPDM_ENABLE                  1
#define SY6542_DPDM_DISABLE                 0


/* Register 22h */
#define SY6542_REG_22                       0x22
#define SY6542_DPDM_OVP_DIS_MASK            0x40
#define SY6542_DPDM_OVP_DIS_SHIFT           6
#define SY6542_DPDM_OVP_ENABLE              0
#define SY6542_DPDM_OVP_DISABLE             1

#define SY6542_DM_BUF_MASK                  0x30
#define SY6542_DM_BUF_SHIFT                 4
#define SY6542_DM_BUF_600MV                 0
#define SY6542_DM_BUF_2000MV                1
#define SY6542_DM_BUF_2700MV                2
#define SY6542_DM_BUF_3300MV                3

#define SY6542_DP_BUF_MASK                  0x0C
#define SY6542_DP_BUF_SHIFT                 2
#define SY6542_DP_BUF_600MV                 0
#define SY6542_DP_BUF_2000MV                1
#define SY6542_DP_BUF_2700MV                2
#define SY6542_DP_BUF_3300MV                3

#define SY6542_DM_BUF_EN_MASK               0x02
#define SY6542_DM_BUF_EN_SHIFT              1
#define SY6542_DM_BUF_ENABLE                1
#define SY6542_DM_BUF_DISABLE               0

#define SY6542_DP_BUF_EN_MASK               0x01
#define SY6542_DP_BUF_EN_SHIFT              0
#define SY6542_DP_BUF_ENABLE                1
#define SY6542_DP_BUF_DISABLE               0

/* Register 23h */
#define SY6542_REG_23                       0x23
#define SY6542_VDM_RD_MASK                  0x38
#define SY6542_VDM_RD_SHIFT                 3
#define SY6542_VDM_RD_0MV_325MV             0
#define SY6542_VDM_RD_325MV_1000MV          1
#define SY6542_VDM_RD_1000MV_1350MV         2
#define SY6542_VDM_RD_1350MV_2200MV         3
#define SY6542_VDM_RD_2200MV_3000MV         4
#define SY6542_VDM_RD_3000MV_3300MV         5

#define SY6542_VDP_RD_MASK                  0x07
#define SY6542_VDP_RD_SHIFT                 3
#define SY6542_VDP_RD_0MV_325MV             0
#define SY6542_VDP_RD_325MV_1000MV          1
#define SY6542_VDP_RD_1000MV_1350MV         2
#define SY6542_VDP_RD_1350MV_2200MV         3
#define SY6542_VDP_RD_2200MV_3000MV         4
#define SY6542_VDP_RD_3000MV_3300MV         5

/* Register 24h */
#define SY6542_REG_24                       0x24
#define SY6542_DM_LOW_MASK_MASK             0x40
#define SY6542_DM_LOW_MASK_SHIFT            6

#define SY6542_DM_LOW_FLAG_MASK             0x20
#define SY6542_DM_LOW_FLAG_SHIFT            5

#define SY6542_DP_LOW_MASK_MASK             0x10
#define SY6542_DP_LOW_MASK_SHIFT            4

#define SY6542_DP_LOW_FLAG_MASK             0x08
#define SY6542_DP_LOW_FLAG_SHIFT            3

#define SY6542_DPDM_OVP_MASK_MASK           0x04
#define SY6542_DPDM_OVP_MASK_SHIFT          2

#define SY6542_DPDM_OVP_FLAG_MASK           0x02
#define SY6542_DPDM_OVP_FLAG_SHIFT          1

#define SY6542_DPDM_OVP_STAT_MASK           0x01
#define SY6542_DPDM_OVP_STAT_SHIFT          0

/* Register 2Bh */
#define SY6542_REG_2B                       0x2B
#define SY6542_VOOC_EN_MASK                 0x80
#define SY6542_VOOC_EN_SHIFT                7
#define SY6542_VOOC_ENABLE                  1
#define SY6542_VOOC_DISABLE                 0

#define SY6542_SOFT_RESET_MASK              0x02
#define SY6542_SOFT_RESET_SHIFT             1
#define SY6542_SOFT_RESET                   1

#define SY6542_SEND_SEQ_MASK                0x01
#define SY6542_SEND_SEQ_SHIFT               0
#define SY6542_SEND_SEQ                     1

/* Register 2Ch */
#define SY6542_REG_2C                       0x2C
#define SY6542_TX_WDATA_POL_H_MASK          0x03

/* Register 2Dh */
#define SY6542_REG_2D                       0x2D
#define SY6542_TX_WDATA_POL_L_MASK          0xFF

/* Register 2Eh */
#define SY6542_REG_2E                       0x2E
#define SY6542_RX_RDATA_POL_H_MASK          0xFF

/* Register 2Fh */
#define SY6542_REG_2F                       0x2F
#define SY6542_PULSE_FILTERED_STAT_MASK     0x80
#define SY6542_PULSE_FILTERED_STAT_SHIFT    7

#define SY6542_NINTH_CLK_ERR_FALG_MASK      0x40
#define SY6542_NINTH_CLK_ERR_FALG_SHIFT     6

#define SY6542_TXSEQ_DONE_FLAG_MASK         0x20
#define SY6542_TXSEQ_DONE_FLAG_SHIFT        5

#define SY6542_ERR_TRANS_DET_FLAG_MASK      0x10
#define SY6542_ERR_TRANS_DET_FLAG_SHIFT     4

#define SY6542_TXDATA_WR_FAIL_FLAG_MASK     0x08
#define SY6542_TXDATA_WR_FAIL_FLAG_SHIFT    3

#define SY6542_RX_START_FLAG_MASK           0x04
#define SY6542_RX_START_FLAG_SHIFT          2

#define SY6542_RXDATA_DONE_FLAG_MASK        0x02
#define SY6542_RXDATA_DONE_FLAG_SHIFT       1

#define SY6542_TXDATA_DONE_FLAG_MASK        0x01
#define SY6542_TXDATA_DONE_FLAG_SHIFT       0

/* Register 30h */
#define SY6542_REG_30                       0x30
#define SY6542_NINTH_CLK_ERR_MASK_MASK      0x40
#define SY6542_NINTH_CLK_ERR_MASK_SHIFT     6

#define SY6542_TXSEQ_DONE_MASK_MASK         0x20
#define SY6542_TXSEQ_DONE_MASK_SHIFT        5

#define SY6542_ERR_TRANS_DET_MASK_MASK      0x10
#define SY6542_ERR_TRANS_DET_MASK_SHIFT     4

#define SY6542_TXDATA_WR_FAIL_MASK_MASK     0x08
#define SY6542_TXDATA_WR_FAIL_MASK_SHIFT    3

#define SY6542_RX_START_MASK_MASK           0x04
#define SY6542_RX_START_MASK_SHIFT          2

#define SY6542_RXDATA_DONE_MASK_MASK        0x02
#define SY6542_RXDATA_DONE_MASK_SHIFT       1

#define SY6542_TXDATA_DONE_MASK_MASK        0x01
#define SY6542_TXDATA_DONE_MASK_SHIFT       0

/* Register 31h */
#define SY6542_REG_31                       0x31
#define SY6542_PRE_WDATA_POL_H_MASK         0x03

/* Register 32h */
#define SY6542_REG_32                       0x32
#define SY6542_PRE_WDATA_POL_L_MASK         0xFF

/* Register 33h */
#define SY6542_REG_33                       0x33
#define SY6542_LOOSE_DET_MASK               0x80
#define SY6542_LOOSE_DET_SHIFT              7
#define SY6542_LOOSE_WINDOW_HIGH            0
#define SY6542_LOOSE_WINDOW_LOW             1

#define SY6542_LOW_CHECK_EN_MASK            0x40
#define SY6542_LOW_CHECK_EN_SHIFT           6

#define SY6542_END_TIME_SET_MASK            0x30
#define SY6542_END_TIME_SET_SHIFT           4
#define SY6542_NINE_5_5MS_AFTER_NINE_1_5MS  0
#define SY6542_NINE_6MS_AFTER_NINE_2MS      1
#define SY6542_NINE_6_5MS_AFTER_NINE_2_5MS  2
#define SY6542_NINE_7MS_AFTER_NINE_3MS      3

#define SY6542_RX_SAMPLE_TIME_MASK          0x03
#define SY6542_RX_SAMPLE_TIME_SHIFT         4
#define SY6542_RX_SAMPLE_TIME_5US           0
#define SY6542_RX_SAMPLE_TIME_150US         1
#define SY6542_RX_SAMPLE_TIME_300US         2
#define SY6542_RX_SAMPLE_TIME_450US         3

/* Register 36h */
#define SY6542_REG_36                       0x36
#define SY6542_DEVICE_ID_MASK               0xFF


#define SY6542_REG_3A						0x3A
#define SY6542_REG_AE 0xAE


/* Register 3Ch */
#define SY6542_REG_3C                       0x3C
#define SY6542_VBUS_IN_RANGE_DIS_MASK       0x40
#define SY6542_VBUS_IN_RANGE_DIS_SHIFT      6
#define SY6542_VBUS_EN_RANGE_ENABLE         0
#define SY6542_VBUS_EN_RANGE_DISABLE        1

#define SY6542A_DEVICE_ID        			0x67

#define GET_VBAT_PREDATA_DEFAULT (0x64 << SY6542_DATA_H_SHIFT) | 0x02;
#define EXEC_TIME_THR	1500
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
int sc8547_subsys_init(void);
void sc8547_subsys_exit(void);
int sc8547_slave_subsys_init(void);
void sc8547_slave_subsys_exit(void);
#endif
#endif
