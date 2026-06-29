/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SPRD_SPI_PANEL_H_
#define _SPRD_SPI_PANEL_H_

#include <linux/backlight.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/spi/spi.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

enum {
	SPI_CMD_CODE_INIT = 0,
	SPI_CMD_CODE_SLEEP_IN,
	SPI_CMD_CODE_SLEEP_OUT,
	SPI_CMD_CODE_RESERVED1,
	SPI_CMD_CODE_RESERVED2,
	SPI_CMD_CODE_RESERVED3,
	SPI_CMD_CODE_RESERVED4,
	SPI_CMD_CODE_RESERVED5,
	SPI_CMD_CODE_MAX,
};

struct spi_cmd_desc {
	u8 data_type;
	u8 wait;
	u8 wc_h;
	u8 wc_l;
	u8 payload[];
};

struct spi_reset_sequence {
	u32 items;
	struct gpio_timing *timing;
};

struct spi_panel_info {
	/* common parameters */
	struct device_node *of_node;
	struct drm_display_mode mode;
	struct drm_display_mode *buildin_modes;
	int num_buildin_modes;
	int display_mode_count;
	struct gpio_desc *avdd_gpio;
	struct gpio_desc *avee_gpio;
	struct gpio_desc *reset_gpio;
	struct spi_reset_sequence rst_on_seq;
	struct spi_reset_sequence rst_off_seq;
	const void *cmds[SPI_CMD_CODE_MAX];
	int cmds_len[SPI_CMD_CODE_MAX];
	char lcd_name[50];

	/* esd check parameters*/
	bool esd_check_en;
	u8 esd_check_mode;
	u16 esd_check_period;
	u32 esd_check_reg;
	u32 esd_check_val;

	uint8_t bpp;
    uint16_t spi_bus_mode;
    uint16_t spi_te_pol;
    uint16_t spi_mode;
    struct gpio_desc *cd_gpio;
    uint16_t spi_cd_gpio;
    uint16_t spi_te_gpio;
    uint32_t spi_freq;
    uint32_t spi_sync_delay;
    uint8_t spi_bus_num;
    uint8_t spi_pol_mode;
    uint8_t spi_cs;
    uint8_t spi_endian;
    uint8_t spi_2data_en;
    uint8_t spi_bits_1word;
    uint32_t spi_word_swap;
};

struct sprd_spi_panel {
	struct device dev;
	struct drm_panel base;
	struct spi_device *spi_dev;
	struct spi_panel_info info;
	//char lcd_name[50];
	//struct drm_display_mode mode;
	struct device_node *lcd_node;
	struct backlight_device *backlight;
	struct completion spi_complete;
	struct semaphore transfer_lock;
	void *current_base;
	uint32_t refresh_len;
	struct delayed_work esd_work;
	bool esd_work_pending;
	bool is_enabled;
	struct spi_transfer sprd_refresh_transfers[5];
	unsigned char sprd_refresh_cmd[3];
	unsigned char sprd_refresh_data[2][4];
	struct spi_message sprd_refresh_msg;
	struct spi_transfer sprd_refresh_xfer;
};

struct device_node *sprd_panel_find_node_by_name(void);
int sprd_spi_refresh(struct sprd_spi_panel *spi_panel, uint8_t *pframe, uint8_t bg_flag);
#define to_sprd_spi_panel(panel) container_of(panel, struct sprd_spi_panel, base)

#endif /* _SPRD_DSI_PANEL_H_ */
