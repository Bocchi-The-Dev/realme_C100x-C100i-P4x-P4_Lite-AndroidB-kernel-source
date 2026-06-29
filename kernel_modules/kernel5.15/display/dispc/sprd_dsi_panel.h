/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SPRD_DSI_PANEL_H_
#define _SPRD_DSI_PANEL_H_

#include <linux/backlight.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define VREFRESH_CNT_MAX	20
#define SPRD_OLED_DEFAULT_BRIGHTNESS 25
#define MAX_CMD_SIZE 4096

enum {
	CMD_CODE_INIT = 0,
	CMD_CODE_SLEEP_IN,
	CMD_CODE_SLEEP_OUT,
	CMD_OLED_BRIGHTNESS,
	CMD_OLED_REG_LOCK,
	CMD_OLED_REG_UNLOCK,
	CMD_CODE_DOZE_IN,
	CMD_CODE_DOZE_OUT,
	CMD_CODE_BL_PREFIX,
	CMD_CODE_RESERVED3,
	CMD_CODE_RESERVED4,
	CMD_CODE_RESERVED5,
	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD  2023/01/30 add cabc func  start*/
	CMD_CODE_CABC_OFF,
	CMD_CODE_CABC_UI,
	CMD_CODE_CABC_STILL,
	CMD_CODE_CABC_MOVING,
	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD  2023/01/30 add cabc func  end*/
	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD, 2024/1/25, add for himax sequence in time , begin */
	HIMAX_CMD_CODE_SLEEP_IN_GESTURE_OFF,
	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD, 2024/1/25, add for himax sequence in time , end */
	CMD_CODE_MAX,
};

enum {
	SPRD_DSI_MODE_CMD = 0,
	SPRD_DSI_MODE_VIDEO_BURST,
	SPRD_DSI_MODE_VIDEO_SYNC_PULSE,
	SPRD_DSI_MODE_VIDEO_SYNC_EVENT,
	SPRD_DSI_MODE_CMD_DPI,
};

enum {
	SPRD_PANEL_TYPE_LCD = 0,
	SPRD_PANEL_TYPE_AMOLED,
};

enum {
	ESD_MODE_REG_CHECK,
	ESD_MODE_TE_CHECK,
	ESD_MODE_MIX_CHECK,
};

struct dsi_cmd_desc {
	u8 data_type;
	u8 wait;
	u8 wc_h;
	u8 wc_l;
	u8 payload[];
};

struct gpio_timing {
	u32 level;
	u32 delay;
};

struct reset_sequence {
	u32 items;
	struct gpio_timing *timing;
};

struct panel_info {
	/* common parameters */
	struct device_node *of_node;
	struct drm_display_mode mode;
	struct drm_display_mode *buildin_modes;
	int num_buildin_modes;
	int display_mode_count;
	struct gpio_desc *avdd_gpio;
	struct gpio_desc *avee_gpio;
	struct gpio_desc *reset_gpio;
	struct reset_sequence rst_on_seq;
	struct reset_sequence rst_off_seq;
	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD, 2024/1/15, add for pre_avdd_rst_on_seq reset_sequence */
	u32 ft8057_sequence_enable;
	const void *cmds[CMD_CODE_MAX];
	int cmds_len[CMD_CODE_MAX];
	const void *vrefresh_cmds[VREFRESH_CNT_MAX];
	int vrefresh_cmds_len[VREFRESH_CNT_MAX];
	bool vrefresh_change_send_cmd;
	int panel_type;
	u32 width_um;
	u32 height_um;
	/* add for hulk to fixed 51 value at fixed low brightness */
	u32 fixed_brightness;

	/* cmd mode vrr config */
	bool cmd_dpi_mode;
	u32 vrr_mode_count;
	u32 *vrr_mode_vrefresh;
	bool vrefresh_cmd_changed;
	int current_cmd_index;
	int max_vrefresh;

	u32 slice_width;
	u32 slice_height;
	u32 output_bpc;
	u32 dsc_en;
	u32 dual_dsi_en;
	u32 cabc_mode;

	/* esd check parameters*/
	bool esd_check_en;
	u8 esd_check_mode;
	u16 esd_check_period;
	u32 esd_check_reg;
	u32 esd_check_val;

	/* MIPI DSI specific parameters */
	u32 format;
	u32 lanes;
	u32 hs_rate;
	u32 lp_rate;
	u32 mode_flags;
	bool use_dcs;
	/*mipi lastfram delay*/
	bool mipi_need_delay;
	u32 mipi_delay_time;

	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD, 2024/1/25, add for himax sequence in time , begin */
	struct reset_sequence rst_off_avdd_seq;
	u32 himax_vsn_recovery;
	u32 himax_sequence;
	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD, 2024/1/25, add for himax sequence in time , end */
	/* pixelpll config parameters */
	bool dpi_clk_pixelpll;
	/*lcd use 51 register high 8bit*/
	u32 use_high_bit;
	/*add TP timing requirements*/
	bool tp_resume_by_ddic;
	bool tp_aod_support;
	bool ovt_tp_aod_support;
	bool ovt_tp_reset_gpio;
	bool need_delay;
	u32 low_brightness;
	u32 nairobi_low_brightness;
};

struct sprd_panel {
	struct device dev;
	struct drm_panel base;
	struct mipi_dsi_device *slave;
	struct panel_info info;
	char lcd_name[50];
	struct backlight_device *backlight;
	bool sprd_bl_mipi_type;
	struct regulator *supply;
	struct delayed_work esd_work;
	u64 esd_count;
	bool esd_work_pending;
	struct mutex lock;
	bool enabled;
	struct notifier_block lcd_vsn_reset_nb;
	bool is_esd_rst;
};

struct sprd_oled {
	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD  2024/1/24  add for max_backlight  start*/
	struct device oled_dev;
	/* xiazhiping@MULTIMEDIA.DISPLAY.LCD  2024/1/24  add for max_backlight  end*/
	struct backlight_device *bdev;
	struct sprd_panel *panel;
	struct dsi_cmd_desc *cmds[MAX_CMD_SIZE];
	int cmd_len;
	int cmds_total;
	int max_level;
};

struct oplus_brightness_alpha {
	u32 brightness;
	u32 alpha;
};

int sprd_panel_parse_lcddtb(struct device_node *lcd_node,
	struct sprd_panel *panel);
void  sprd_panel_enter_doze(struct drm_panel *p);
void  sprd_panel_exit_doze(struct drm_panel *p);
int sprd_panel_send_vrefresh_cmd(struct sprd_panel *panel, int index);
struct device_node *sprd_get_panel_node_by_name(void);
int   private_panel_cabc(unsigned int level);

#define to_sprd_panel(panel) \
	container_of(panel, struct sprd_panel, base)

#endif /* _SPRD_DSI_PANEL_H_ */
