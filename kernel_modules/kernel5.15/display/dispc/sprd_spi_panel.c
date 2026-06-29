/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_encoder.h>
#include <linux/component.h>
#include <drm/drm_connector.h>
#include <drm/drm_bridge.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/random.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>
#include "sprd_spi_panel.h"
#include <disp_lib.h>
#include "sprd_swdispc.h"
#include "sysfs/sysfs_display.h"

#include <linux/kernel.h>
#include <linux/of_fdt.h>
#include <linux/sysfs.h>

#include <asm/uaccess.h>
#include <linux/fs.h>

#define SPI_CD_BIT			(0x80000000)
#define SPI_CD_SET_HIGH(x)		((x) |= SPI_CD_BIT)
#define SPI_CD_SET_LOW(x)		((x) &= ~SPI_CD_BIT)
#define SPI_MAX_SPEED_6MHZ    (6000000)
#define SPI_MAX_SPEED_48MHZ   (48000000)

#define SPI_2DATA_LINE_EN()		(spi_panel->spi_dev->mode |= BIT(8))
#define SPI_2DATA_LINE_DISEN()		(spi_panel->spi_dev->mode &= ~(BIT(8)))

/*
static inline struct sprd_spi_panel *to_sprd_spi_panel(struct drm_panel *panel)
{
	return container_of(panel, struct sprd_spi_panel, base);
}*/

static int sprd_spi_config(struct sprd_spi_panel *spi_panel)
{
	uint16_t width, height, bpp;
	struct spi_panel_info *info = &spi_panel->info;

	bpp = info->bpp;
	width = info->mode.hdisplay;
	height = info->mode.vdisplay;
	spi_panel->refresh_len = width * height * bpp / 8;
	spi_panel->sprd_refresh_xfer.len = spi_panel->refresh_len;
	spi_panel->sprd_refresh_xfer.bits_per_word = info->spi_bits_1word;
	spi_panel->sprd_refresh_data[0][2] = ((width - 1) >> 8) & 0xff;
	spi_panel->sprd_refresh_data[0][3] = (width - 1) & 0xff;
	spi_panel->sprd_refresh_data[1][2] = ((height - 1) >> 8) & 0xff;
	spi_panel->sprd_refresh_data[1][3] = (height - 1) & 0xff;

	return 0;
}

static void sprd_spi_cmd(struct sprd_spi_panel *spi_panel, int n)
{
	struct spi_message msg;
	struct spi_panel_info *panel = &spi_panel->info;

	spi_message_init(&msg);

	if (panel->cd_gpio)
		gpiod_direction_output(panel->cd_gpio, 0);
	else if (IS_ERR(panel->cd_gpio))
		SPI_CD_SET_LOW(spi_panel->sprd_refresh_transfers[n].len);

	spi_message_add_tail(&(spi_panel->sprd_refresh_transfers[n]), &msg);
	spi_sync(spi_panel->spi_dev, &msg);
}

static void sprd_spi_data(struct sprd_spi_panel *spi_panel, int n)
{
	struct spi_message msg;
	struct spi_panel_info *panel = &spi_panel->info;

	spi_message_init(&msg);
	if (panel->cd_gpio)
		gpiod_direction_output(panel->cd_gpio, 1);
	else if (IS_ERR(panel->cd_gpio))
		SPI_CD_SET_HIGH(spi_panel->sprd_refresh_transfers[n].len);

	spi_message_add_tail(&(spi_panel->sprd_refresh_transfers[n]), &msg);
	spi_sync(spi_panel->spi_dev, &msg);
}

static void sprd_spi_flip_layer(struct sprd_spi_panel *spi_panel, uint8_t *pframe)
{
	struct spi_panel_info *panel;
	unsigned char i = 0;
	int ret;

	panel = &spi_panel->info;

	spi_panel->sprd_refresh_xfer.tx_buf = pframe;
	spi_message_init(&spi_panel->sprd_refresh_msg);

	if (panel->cd_gpio)
		gpiod_direction_output(panel->cd_gpio, 1);
	else if (IS_ERR(panel->cd_gpio)) {
		SPI_CD_SET_HIGH(spi_panel->sprd_refresh_xfer.len);
	}

	spi_message_add_tail(&spi_panel->sprd_refresh_xfer, &spi_panel->sprd_refresh_msg);

	do {
		ret = spi_sync(spi_panel->spi_dev, &spi_panel->sprd_refresh_msg);
		i++;
	} while ((ret < 0) && (i < 6));
}

static int sprd_spi_transfer(struct sprd_spi_panel *spi_panel, uint8_t *pframe, uint8_t bg_flag)
{
	struct spi_panel_info *panel = &spi_panel->info;

	if (panel->spi_2data_en) {
		SPI_2DATA_LINE_EN();
	} else {
		DRM_DEBUG("sprd_spi_transfer set window\n");
		sprd_spi_cmd(spi_panel, 0);
		sprd_spi_data(spi_panel, 1);
		sprd_spi_cmd(spi_panel, 2);
		sprd_spi_data(spi_panel, 3);
		sprd_spi_cmd(spi_panel, 4);
	}

	if (!bg_flag)
		sprd_spi_flip_layer(spi_panel, pframe);
	//else
		//sprd_spi_flip_bg();

	if (panel->spi_2data_en) {
		SPI_2DATA_LINE_DISEN();
	}

	return 0;
}

int sprd_spi_refresh(struct sprd_spi_panel *spi_panel, uint8_t *pframe, uint8_t bg_flag)
{
	int ret;

	ret = sprd_spi_transfer(spi_panel, pframe, bg_flag);
	if (ret)
		DRM_ERROR("sprd:sprd_spi_refresh spi_async error\n");

	return ret;
}

static void spi_write_data(struct sprd_spi_panel *spi_panel, struct spi_transfer *xfer)
{
	struct spi_device *spi_dev = spi_panel->spi_dev;
	struct spi_message msg;
	struct spi_panel_info *panel = &spi_panel->info;

	if (panel->cd_gpio)
		gpiod_direction_output(panel->cd_gpio, 1);
	else if (IS_ERR(panel->cd_gpio))
		SPI_CD_SET_HIGH(xfer->len);

	spi_message_init(&msg);
	spi_message_add_tail(xfer, &msg);
	spi_sync(spi_dev, &msg);
}

static void spi_write_cmd(struct sprd_spi_panel *spi_panel, struct spi_transfer *xfer)
{
	struct spi_device *spi_dev = spi_panel->spi_dev;
	struct spi_message msg;
	struct spi_panel_info *panel = &spi_panel->info;

	if (panel->cd_gpio)
		gpiod_direction_output(panel->cd_gpio, 0);
	else if (IS_ERR(panel->cd_gpio))
		SPI_CD_SET_LOW(xfer->len);

	spi_message_init(&msg);
	spi_message_add_tail(xfer, &msg);
	spi_sync(spi_dev, &msg);
}

static void spi_send_cmds(struct sprd_spi_panel *spi_panel, const u8 *data, uint16_t len)
{
	struct spi_transfer xfer;

	memset(&xfer, 0, sizeof(xfer));
	xfer.len = 1;
	xfer.tx_buf = &(data[0]);
	xfer.bits_per_word = 8;
	DRM_INFO("spi_write_cmd:0x%x\n", data[0]);
	spi_write_cmd(spi_panel, &xfer);

	if (len == 1)
		return;

	xfer.len = len - 1;
	xfer.tx_buf = &(data[1]);
	xfer.bits_per_word = 8;
	DRM_INFO("spi_write_data:0x%x\n", data[1]);
	spi_write_data(spi_panel, &xfer);
}

static int spi_panel_send_cmds(struct sprd_spi_panel *spi_panel, const void *data, int size)
{
	const struct spi_cmd_desc *cmds = data;
	u16 len;
	if ((cmds == NULL) || (spi_panel == NULL))
		return -1;

	while (size > 0) {
		len = (cmds->wc_h << 8) | cmds->wc_l;

		spi_send_cmds(spi_panel, cmds->payload, len);
		if (cmds->wait)
			msleep(cmds->wait);
		cmds = (const struct spi_cmd_desc *)(cmds->payload + len);
		size -= (len + 4);
	}

	return 0;
}

static int sprd_panel_disable(struct drm_panel *p)
{
	struct sprd_spi_panel *panel = to_sprd_spi_panel(p);

	DRM_INFO("%s()\n", __func__);

	/*
	 * FIXME:
	 * The cancel work should be executed before DPU stop,
	 * otherwise the esd check will be failed if the DPU
	 * stopped in video mode and the DSI has not change to
	 * CMD mode yet. Since there is no VBLANK timing for
	 * LP cmd transmission.
	 */
	if (panel->esd_work_pending) {
		cancel_delayed_work_sync(&panel->esd_work);
		panel->esd_work_pending = false;
	}


	if (panel->backlight) {
		panel->backlight->props.power = FB_BLANK_POWERDOWN;
		panel->backlight->props.state |= BL_CORE_FBBLANK;
		backlight_update_status(panel->backlight);
	}
	spi_panel_send_cmds(panel,
			     panel->info.cmds[SPI_CMD_CODE_SLEEP_IN],
			     panel->info.cmds_len[SPI_CMD_CODE_SLEEP_IN]);

	return 0;
}

static int sprd_panel_enable(struct drm_panel *p)
{
	struct sprd_spi_panel *panel = to_sprd_spi_panel(p);

	DRM_INFO("%s()\n", __func__);

	spi_panel_send_cmds(panel,
			     panel->info.cmds[SPI_CMD_CODE_INIT],
			     panel->info.cmds_len[SPI_CMD_CODE_INIT]);

	if (panel->backlight) {
		panel->backlight->props.power = FB_BLANK_UNBLANK;
		panel->backlight->props.state &= ~BL_CORE_FBBLANK;
		backlight_update_status(panel->backlight);
	}

	if (panel->info.esd_check_en) {
		schedule_delayed_work(&panel->esd_work,
				      msecs_to_jiffies(1000));
		panel->esd_work_pending = true;
	}

	return 0;
}

static int sprd_panel_get_modes(struct drm_panel *p, struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct sprd_spi_panel *spi_panel = to_sprd_spi_panel(p);
	struct spi_panel_info *panel = &spi_panel->info;

	DRM_INFO("%s()-spi\n", __func__);

	mode = drm_mode_duplicate(connector->dev,  &spi_panel->info.mode);
	if (!mode) {
		DRM_ERROR("failed to add mode %ux%ux\n",
			  panel->mode.hdisplay,
			  panel->mode.vdisplay);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = panel->mode.width_mm;
	connector->display_info.height_mm = panel->mode.width_mm;

	return 1;
}

static const struct drm_panel_funcs sprd_panel_funcs = {
	.enable = sprd_panel_enable,
	.disable = sprd_panel_disable,
	.get_modes = sprd_panel_get_modes,
};

static int sprd_panel_gpio_request(struct device *dev,
			struct spi_panel_info *panel)
{

	panel->reset_gpio = devm_gpiod_get_optional(dev,
					"reset", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->reset_gpio))
		DRM_WARN("can't get panel reset gpio: %ld\n",
				 PTR_ERR(panel->reset_gpio));

	panel->cd_gpio = devm_gpiod_get(dev, "cd", GPIOD_ASIS);
	if (IS_ERR(panel->cd_gpio))
		DRM_WARN("failed to get te detection GPIO %p\n", panel->cd_gpio);

	return 0;
}

struct device_node *sprd_panel_find_node_by_name(void)
{
	int rc;
	struct device_node *lcd_node, *cmdline_node;
	const char *cmd_line, *lcd_name_p;
	char lcd_name[50];
	char lcd_path[60];

	DRM_INFO("%s()\n", __func__);

	cmdline_node = of_find_node_by_path("/chosen");
	rc = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
	if (!rc) {
		lcd_name_p = strstr(cmd_line, "spi_panel_name=");
		if (lcd_name_p) {
			sscanf(lcd_name_p, "spi_panel_name=%s", lcd_name);
			DRM_INFO("spi lcd name: %s\n", lcd_name);
		}
	} else {
		DRM_ERROR("can't not parse bootargs property\n");
		return NULL;
	}

	sprintf(lcd_path, "/lcds/%s", lcd_name);
	lcd_node = of_find_node_by_path(lcd_path);

	return lcd_node;
}

static int sprd_panel_parse_dt(struct device_node *np, struct sprd_spi_panel *spi_panel)
{
	int rc;
	struct device_node *lcd_node;
	struct spi_panel_info *panel = &spi_panel->info;
	const void *p;
	int bytes;
	u32 temp;

	DRM_INFO("%s()\n", __func__);

	lcd_node = sprd_panel_find_node_by_name();
  	if (!lcd_node) {
  		DRM_ERROR("%pOF: could not find %s node\n", np, panel->lcd_name);
  		return -ENODEV;
  	}

	spi_panel->lcd_node = lcd_node;
	spi_panel->info.of_node = lcd_node;

	rc = of_property_read_u32(lcd_node, "sprd,width-mm", &temp);
	if (!rc)
		panel->mode.width_mm = temp;
	else
		panel->mode.width_mm = 68;

	rc = of_property_read_u32(lcd_node, "sprd,height-mm", &temp);
	if (!rc)
		panel->mode.height_mm = temp;
	else
		panel->mode.height_mm = 121;

	rc = of_get_drm_display_mode(lcd_node, &panel->mode, 0,
				     OF_USE_NATIVE_MODE);
	if (rc) {
		DRM_ERROR("get display timing failed\n");
		return rc;
	}

	rc = of_property_read_u32(lcd_node, "bpp", &temp);
	if (!rc)
		panel->bpp = temp;
	else
		panel->bpp = 24;

	rc = of_property_read_u32(lcd_node, "spi_bus_num", &temp);
	if (!rc)
		panel->spi_bus_num = temp;
	pr_info("spi_bus_num = %d\n", panel->spi_bus_num);

	rc = of_property_read_u32(lcd_node, "spi_cs", &temp);
	if (!rc)
		panel->spi_cs = temp;
	pr_info("spi_cs = %d\n", panel->spi_cs);

	rc = of_property_read_u32(lcd_node, "spi_mode", &temp);
	if (!rc)
		panel->spi_mode = temp;
	pr_info("spi_mode = %d\n", panel->spi_mode);

	rc = of_property_read_u32(lcd_node, "spi_te_gpio", &temp);
	if (!rc)
		panel->spi_te_gpio = temp;
	pr_info("spi_te_gpio = %d\n", panel->spi_te_gpio);

	rc = of_property_read_u32(lcd_node, "spi_pol_mode", &temp);
	if (!rc)
		panel->spi_pol_mode = temp;
	pr_info("spi_pol_mode = %d\n", panel->spi_pol_mode);

	rc = of_property_read_u32(lcd_node, "spi_bits_1word", &temp);
	if (!rc)
		panel->spi_bits_1word = temp;
	else
		panel->spi_bits_1word = 32;
	pr_info("spi_bits_1word = %d\n", panel->spi_bits_1word);

	rc = of_property_read_u32(lcd_node, "spi_word_swap", &temp);
	if (!rc)
		panel->spi_word_swap = temp;
	else
		panel->spi_word_swap = 0;
	pr_info("spi_word_swap = %d\n", panel->spi_word_swap);

	rc = of_property_read_u32(lcd_node, "spi_2data_en", &temp);
	if (!rc)
		panel->spi_2data_en = temp;
	else
		panel->spi_2data_en = 0;
	pr_info("spi_2data_en = %d\n", panel->spi_2data_en);


	rc = of_property_read_u32(lcd_node, "sprd,esd-check-enable", &temp);
	if (!rc)
		panel->esd_check_en = temp;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-mode", &temp);
	if (!rc)
		panel->esd_check_mode = temp;
	else
		panel->esd_check_mode = 1;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-period", &temp);
	if (!rc)
		panel->esd_check_period = temp;
	else
		panel->esd_check_period = 1000;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-register", &temp);
	if (!rc)
		panel->esd_check_reg = temp;
	else
		panel->esd_check_reg = 0x0A;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-value", &temp);
	if (!rc)
		panel->esd_check_val = temp;
	else
		panel->esd_check_val = 0x9C;


	p = of_get_property(lcd_node, "init-data", &bytes);
	if (p) {
		panel->cmds[SPI_CMD_CODE_INIT] = p;
		panel->cmds_len[SPI_CMD_CODE_INIT] = bytes;
	} else
		DRM_ERROR("can't find sprd,initial-command property\n");

	p = of_get_property(lcd_node, "sleep-in", &bytes);
	if (p) {
		panel->cmds[SPI_CMD_CODE_SLEEP_IN] = p;
		panel->cmds_len[SPI_CMD_CODE_SLEEP_IN] = bytes;
	} else
		DRM_ERROR("can't find sprd,sleep-in-command property\n");

	p = of_get_property(lcd_node, "sleep-out", &bytes);
	if (p) {
		panel->cmds[SPI_CMD_CODE_SLEEP_OUT] = p;
		panel->cmds_len[SPI_CMD_CODE_SLEEP_OUT] = bytes;
	} else
		DRM_ERROR("can't find sprd,sleep-out-command property\n");

	rc = of_property_read_u32(lcd_node, "spi_cd_gpio", &temp);
	if (!rc) {
		panel->spi_cd_gpio = temp;
	} else
		panel->spi_cd_gpio = 0;

	rc = of_property_read_u32(lcd_node, "spi_endian", &temp);
	if (!rc)
		panel->spi_endian = temp;

	rc = of_property_read_u32(lcd_node, "spi_freq", &temp);
	if (!rc)
		panel->spi_freq = temp;

	return 0;
}

static int sprd_panel_bind(struct device *dev,
		struct device *master, void *data)
{
	/* do nothing */
	DRM_INFO("%s()\n", __func__);
	return 0;
}

static void sprd_panel_unbind(struct device *dev,
			struct device *master, void *data)
{
	/* do nothing */
	DRM_INFO("%s()\n", __func__);
}

static const struct component_ops panel_component_ops = {
	.bind	= sprd_panel_bind,
	.unbind	= sprd_panel_unbind,
};

static int sprd_panel_device_create(struct device *parent,
				    struct sprd_spi_panel *panel)
{
	panel->dev.class = display_class;
 	panel->dev.parent = parent;
	panel->dev.of_node = panel->info.of_node;
 	dev_set_name(&panel->dev, "panel2");
 	dev_set_drvdata(&panel->dev, panel);

 	return device_register(&panel->dev);
}

static void sprd_spi_panel_context_init(struct sprd_spi_panel *spi_panel)
{
	spi_panel->sprd_refresh_cmd[0] = 0x2a;
	spi_panel->sprd_refresh_cmd[1] = 0x2b;
	spi_panel->sprd_refresh_cmd[2] = 0x2c;

	spi_panel->sprd_refresh_data[0][0] = 0x00;
	spi_panel->sprd_refresh_data[0][1] = 0x00;
	spi_panel->sprd_refresh_data[0][2] = 0x00;
	spi_panel->sprd_refresh_data[0][3] = 0xef;

	spi_panel->sprd_refresh_data[1][0] = 0x00;
	spi_panel->sprd_refresh_data[1][1] = 0x00;
	spi_panel->sprd_refresh_data[1][2] = 0x01;
	spi_panel->sprd_refresh_data[1][3] = 0x3f;

	spi_panel->sprd_refresh_transfers[0].len = 1;
	spi_panel->sprd_refresh_transfers[0].tx_buf = &(spi_panel->sprd_refresh_cmd[0]);
	spi_panel->sprd_refresh_transfers[0].bits_per_word = 8;

	spi_panel->sprd_refresh_transfers[1].len = 4;
	spi_panel->sprd_refresh_transfers[1].tx_buf = &(spi_panel->sprd_refresh_data[0][0]);
	spi_panel->sprd_refresh_transfers[1].bits_per_word = 8;

	spi_panel->sprd_refresh_transfers[2].len = 1;
	spi_panel->sprd_refresh_transfers[2].tx_buf = &(spi_panel->sprd_refresh_cmd[1]);
	spi_panel->sprd_refresh_transfers[2].bits_per_word = 8;

	spi_panel->sprd_refresh_transfers[3].len = 4;
	spi_panel->sprd_refresh_transfers[3].tx_buf = &(spi_panel->sprd_refresh_data[1][0]);
	spi_panel->sprd_refresh_transfers[3].bits_per_word = 8;

	spi_panel->sprd_refresh_transfers[4].len = 1;
	spi_panel->sprd_refresh_transfers[4].tx_buf = &(spi_panel->sprd_refresh_cmd[2]);
	spi_panel->sprd_refresh_transfers[4].bits_per_word = 8;
}

static int sprd_spi_panel_probe(struct spi_device *spi_dev)
{
	int ret;
	struct device_node *bl_node;
	struct sprd_spi_panel *spi_panel;

	DRM_INFO("%s enter\n", __func__);
	spi_panel = devm_kzalloc(&spi_dev->dev, sizeof(*spi_panel), GFP_KERNEL);
	if (!spi_panel)
		return -ENOMEM;

	bl_node = of_parse_phandle(spi_dev->dev.of_node,
					"sprd,backlight", 0);
	if (bl_node) {
		spi_panel->backlight = of_find_backlight_by_node(bl_node);
		of_node_put(bl_node);

		if (spi_panel->backlight) {
			spi_panel->backlight->props.state &= ~BL_CORE_FBBLANK;
			spi_panel->backlight->props.power = FB_BLANK_UNBLANK;
			backlight_update_status(spi_panel->backlight);
			DRM_WARN("backlight set ok\n");
		} else {
			DRM_WARN("backlight is not ready, panel probe deferred\n");
			return -EPROBE_DEFER;
		}
	} else
		DRM_WARN("backlight node not found\n");

	sprd_spi_panel_context_init(spi_panel);

	ret = sprd_panel_parse_dt(spi_dev->dev.of_node, spi_panel);
	if (ret) {
		DRM_ERROR("parse panel info failed\n");
		goto err;
	}

	ret = sprd_panel_gpio_request(&spi_dev->dev, &spi_panel->info);
	if (ret) {
		DRM_WARN("gpio is not ready, panel probe deferred\n");
		return -EPROBE_DEFER;
 	}

	ret = sprd_panel_device_create(&spi_dev->dev, spi_panel);
	if (ret) {
		DRM_ERROR("panel device create failed\n");
		goto err;
	}

	spi_panel->base.dev = &spi_panel->dev;
	spi_panel->base.funcs = &sprd_panel_funcs;
	drm_panel_init(&spi_panel->base, &spi_panel->dev, &sprd_panel_funcs, DRM_MODE_ENCODER_DPI);

	drm_panel_add(&spi_panel->base);

	spi_panel->spi_dev = spi_dev;
	spi_dev->max_speed_hz = spi_panel->info.spi_freq;
	spi_dev->mode = spi_panel->info.spi_mode;
	spi_dev->chip_select = spi_panel->info.spi_cs;
	sprd_spi_config(spi_panel);

	ret = spi_setup(spi_dev);
	if (ret) {
		DRM_ERROR("spi init failed\n");
		goto err;
	}

	device_init_wakeup(&spi_dev->dev, 1);
	spi_set_drvdata(spi_dev, spi_panel);

	if (spi_panel->info.esd_check_en) {
		schedule_delayed_work(&spi_panel->esd_work,
				      msecs_to_jiffies(2000));
		spi_panel->esd_work_pending = true;
	}

	component_add(&spi_dev->dev, &panel_component_ops);
	sema_init(&spi_panel->transfer_lock, 1);
	DRM_INFO("%s success!\n", __func__);

	return 0;

err:
	kfree(spi_panel);
	return -ENODEV;
}

static int sprd_spi_panel_remove(struct spi_device *spi)
{
 	struct sprd_spi_panel *spi_panel = spi_get_drvdata(spi);

 	DRM_INFO("%s()\n", __func__);

 	sprd_panel_disable(&spi_panel->base);
	drm_panel_remove(&spi_panel->base);

	spi_panel->spi_dev = NULL;
	spi_set_drvdata(spi, NULL);

 	return 0;
}

static const struct spi_device_id spi_panel_id[] = {
 	{"sprd-spi-panel", 0 },
 	{ }
};

static const struct of_device_id rgb_panel_of_match[] = {
	{ .compatible = "sprd,generic-spi-panel", },
	{ }
};
MODULE_DEVICE_TABLE(of, rgb_panel_of_match);

struct spi_driver sprd_spi_panel_driver = {
	.driver = {
		.name = "sprd-spi-panel-drv",
		.of_match_table = rgb_panel_of_match,
	},
	.probe = sprd_spi_panel_probe,
	.remove = sprd_spi_panel_remove,
	.id_table = spi_panel_id,
};

MODULE_AUTHOR("Pony Wu <Pony.Wu@unisoc.com>");
MODULE_DESCRIPTION("SPRD SPI Panel Driver");
MODULE_LICENSE("GPL v2");
