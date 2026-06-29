/* 
 *
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef _WCN_BOOT
#define _WCN_BOOT

#include "marlin_platform.h"

#include "rf/rf.h"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "WCN BOOT: " fmt

extern uint GNSS_CP_START_ADDR;
extern uint GNSS_FIRMWARE_MAX_SIZE;
extern uint GNSS_DUMP_PACKET_SIZE;
extern uint GNSS_SHARE_MEMORY_SIZE;
extern uint GNSS_DUMP_IRAM_START_ADDR;
extern uint GNSS_CP_IRAM_DATA_NUM;
extern uint GNSS_DUMP_REG_NUMBER;
extern unsigned char  flag_download_done;
extern unsigned char is_ums9620;

#define REG_AON_APB_RESERVED 0x4083C38C
#define REG_RF_CONTRLLER 0x40150000
#define REG_BBPLL_CTRL			0x27AC
#define DEBUG_BBPLL_CTRL		0x27A8
#define REG_LDO_ENABLE1			0x29A4
#define DEBUD_LDO_ENABLE1		0x29A0
#define REG_WF_5G_PRI_RX_RF_ENABLE	0x2944
#define DEBUG_WF_5G_PRI_RX_RF_ENABLE	0x2940
#define REG_WF_5G_DIV_RX_RF_ENABLE	0x2974
#define DEBUG_WF_5G_DIV_RX_RF_ENABLE	0x2970
#define REG_LDO_FC_PULSE1		0x29B4
#define DEBUG_LDO_FC_PULSE1		0x29B0

int wifi_read_rf_reg(unsigned int addr, unsigned int *data);
struct wifi_rf_reg *get_wifi_rf_reg(size_t *array_size);

struct wifi_rf_reg {
	unsigned int reg_addr;
	unsigned int reg_bit;
	bool bit_val;
};

struct wcn_sync_info_t {
	unsigned int init_status;
	unsigned int mem_pd_bt_start_addr;
	unsigned int mem_pd_bt_end_addr;
	unsigned int mem_pd_wifi_start_addr;
	unsigned int mem_pd_wifi_end_addr;
	unsigned int prj_type;
	unsigned short tsx_dac_data;
	/* bit[0]:pcie, bit[1]:sdio, bit[2]:sipc, bit[3];usb */
	unsigned char push_not_allow;
	unsigned char rsved;
} __packed;

struct tsx_data {
	u32 flag; /* cali flag ref */
	u16 dac; /* AFC cali data */
	u16 reserved;
};

struct tsx_cali {
	u32 init_flag;
	struct tsx_data tsxdata;
};

#define WCN_BOUND_CONFIG_NUM	4
struct wcn_pmic_config {
	bool enable;
	char name[32];
	/* index [0]:addr [1]:mask [2]:unboudval [3]boundval */
	u32 config[WCN_BOUND_CONFIG_NUM];
};

struct wcn_clock_info {
	enum wcn_clock_type type;
	enum wcn_clock_mode mode;
	/*
	 * xtal-26m-clk-type-gpio config in the dts.
	 * if xtal-26m-clk-type config in the dts,this gpio unvalid.
	 */
	int gpio;
};

struct marlin_device {
	struct wcn_clock_info clk_xtal_26m;
	int wakeup_ap;
	int reset;
	int chip_en;
	int int_ap;
	unsigned char  crystal_type;

	/* pmic config */
	struct regmap *syscon_pmic;

	/* sharkl5 vddgen1 */
	struct wcn_pmic_config avdd12_parent_bound_chip;
	struct wcn_pmic_config avdd12_bound_wbreq;
	struct wcn_pmic_config avdd33_bound_wbreq;

	bool bound_avdd12;
	bool bound_dcxo18;
	/* power sequence */
	/* VDDIO->DVDD12->chip_en->rst_N->AVDD12->AVDD33 */
	struct regulator *dvdd12;
	struct regulator *avdd12;
	struct regulator *vddwcn_parent;
	/* for PCIe */
	struct regulator *avdd18;
	/* for wifi PA, BT TX RX */
	struct regulator *avdd33;
	/* for internal 26M clock */
	struct regulator *dcxo18;
	struct regulator *refout_wcn;
	struct regulator *dcxo18_xtl2;
	struct regulator *refout_wcn_xtl2;
	struct regulator *vddrf0v9;
	struct regulator *vddrf1v8;
	struct regulator *vddrf1v1;
	struct regulator *vddldo0;
	struct regulator *vddldo4;
	struct regulator *refout_rf;
	struct clk *clk_32k;

	struct clk *clk_parent;
	struct clk *clk_enable;
	struct device *dev;
	struct device_node *np;
	struct mutex power_lock;
	struct completion carddetect_done;
	struct completion download_done;
	struct completion gnss_download_done;
	unsigned long power_state;
	char *write_buffer;
	struct delayed_work power_wq;
	struct work_struct download_wq;
	struct work_struct gnss_dl_wq;
	bool keep_power_on;
	bool wait_ge2;
	bool is_btwf_in_sysfs;
	bool is_gnss_in_sysfs;
	bool need_to_check_ufs;
	bool btwf_wakeup_lock;
	bool n79_mode_support;
	bool dcxo18_status;
	int wifi_need_download_ini_flag;
	int first_power_on_ready;
	atomic_t download_finish_flag;
	unsigned char gnss_dl_finish_flag;
	int loopcheck_status_change;
	struct wcn_sync_info_t sync_f;
	struct tsx_cali tsxcali;
	char *btwf_path;
	char *gnss_path;
	char *emmc_ufs;
	phys_addr_t	base_addr_btwf;
	u32	maxsz_btwf;
	phys_addr_t	base_addr_gnss;
	u32	maxsz_gnss;
};
int marlin_avdd18_dcxo_enable(bool enable);
int sprdwcn_chr_init(void);
int wcn_firmware_ready_close(u8 not_allow_map);

#endif
