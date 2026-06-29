// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mode.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include "disp_lib.h"
#include "sprd_crtc.h"
#include "sprd_swdispc.h"
#include "sprd_drm_spi.h"
#include "sprd_spi_panel.h"
#include "sysfs/sysfs_display.h"

#define encoder_to_spi(encoder) \
	container_of(encoder, struct sprd_drm_spi, encoder)
#define connector_to_spi(connector) \
	container_of(connector, struct sprd_drm_spi, connector)

static void sprd_spi_encoder_enable(struct drm_encoder *encoder)
{
	struct sprd_drm_spi *spi = encoder_to_spi(encoder);
	struct sprd_crtc *crtc = to_sprd_crtc(encoder->crtc);
	struct sprd_swdispc *swdispc = crtc->priv;
	static bool is_enabled = true;

	DRM_INFO("%s()\n", __func__);

	if (is_enabled) {
		is_enabled = false;
		return;
	}

	if (spi->panel) {
		drm_panel_prepare(spi->panel);
		drm_panel_enable(spi->panel);
	}

	sprd_spi_swdispc_run(swdispc);
}

static void sprd_spi_encoder_disable(struct drm_encoder *encoder)
{
	struct sprd_drm_spi *spi = encoder_to_spi(encoder);
	struct sprd_crtc *crtc = to_sprd_crtc(encoder->crtc);
	struct sprd_swdispc *swdispc = crtc->priv;

	DRM_INFO("%s()\n", __func__);

	sprd_spi_swdispc_stop(swdispc);

	if (spi->panel) {
		drm_panel_disable(spi->panel);
		drm_panel_unprepare(spi->panel);
	}
}

static void sprd_spi_encoder_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));
}

static int sprd_spi_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	DRM_INFO("%s()\n", __func__);

	return 0;
}

static const struct drm_encoder_helper_funcs sprd_encoder_helper_funcs = {
	.atomic_check	= sprd_spi_encoder_atomic_check,
	.mode_set	= sprd_spi_encoder_mode_set,
	.enable		= sprd_spi_encoder_enable,
	.disable	= sprd_spi_encoder_disable
};

static const struct drm_encoder_funcs sprd_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int sprd_spi_encoder_init(struct drm_device *drm,
			       struct sprd_drm_spi *spi)
{
	struct drm_encoder *encoder = &spi->encoder;
	int ret;

	ret = drm_encoder_init(drm, encoder, &sprd_encoder_funcs,
			       DRM_MODE_ENCODER_DPI, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize spi encoder\n");
		return ret;
	}

	ret = sprd_drm_set_possible_crtcs(encoder, SPRD_DISPLAY_TYPE_DPI);
	if (ret) {
		DRM_ERROR("failed to find possible crtc\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &sprd_encoder_helper_funcs);

	return 0;
}

static int sprd_spi_connector_get_modes(struct drm_connector *connector)
{
	struct sprd_drm_spi *spi = connector_to_spi(connector);

	DRM_INFO("%s()\n", __func__);

	return drm_panel_get_modes(spi->panel, connector);
}

static enum drm_mode_status
sprd_spi_connector_mode_valid(struct drm_connector *connector,
			 struct drm_display_mode *mode)
{
	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));

	return MODE_OK;
}

static struct drm_encoder *
sprd_spi_connector_best_encoder(struct drm_connector *connector)
{
	struct sprd_drm_spi *spi = connector_to_spi(connector);

	DRM_INFO("%s()\n", __func__);
	return &spi->encoder;
}

static struct drm_connector_helper_funcs sprd_spi_connector_helper_funcs = {
	.get_modes = sprd_spi_connector_get_modes,
	.mode_valid = sprd_spi_connector_mode_valid,
	.best_encoder = sprd_spi_connector_best_encoder,
};

static enum drm_connector_status
sprd_spi_connector_detect(struct drm_connector *connector, bool force)
{
	struct sprd_drm_spi *spi = connector_to_spi(connector);

	DRM_INFO("%s()\n", __func__);

	if (spi->panel) {
		//drm_panel_attach(spi->panel, connector);
		return connector_status_connected;
	}

	return connector_status_disconnected;
}

static void sprd_spi_connector_destroy(struct drm_connector *connector)
{
	DRM_INFO("%s()\n", __func__);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int sprd_spi_atomic_get_property(struct drm_connector *connector,
					const struct drm_connector_state *state,
					struct drm_property *property,
					uint64_t *val)
{
	struct sprd_drm_spi *spi = connector_to_spi(connector);

	DRM_DEBUG("%s()\n", __func__);

	if (property == spi->edid_prop) {
		memcpy(spi->edid_blob->data, &spi->edid_info, sizeof(struct edid));
		*val = spi->edid_blob->base.id;
		DRM_INFO("%s() val = %d\n", __func__, spi->edid_blob->base.id);
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
 		return -EINVAL;
	}

	return 0;
}

static const struct drm_connector_funcs sprd_spi_atomic_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = sprd_spi_connector_detect,
	.destroy = sprd_spi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_get_property = sprd_spi_atomic_get_property,
};

static int sprd_spi_connector_init(struct drm_device *drm, struct sprd_drm_spi *spi)
{
	struct drm_encoder *encoder = &spi->encoder;
	struct drm_connector *connector = &spi->connector;
	struct drm_property *prop;
	int ret;

	ret = drm_connector_init(drm, connector,
				 &sprd_spi_atomic_connector_funcs,
				 DRM_MODE_CONNECTOR_DPI);
	if (ret) {
		DRM_ERROR("drm_connector_init() failed\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &sprd_spi_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	spi->edid_blob = drm_property_create_blob(drm, (sizeof(struct edid) + 1), &spi->edid_info);
	if (IS_ERR(spi->edid_blob)) {
		DRM_ERROR("drm_property_create_blob edid blob failed\n");
		return PTR_ERR(spi->edid_blob);
	}

	prop = drm_property_create(drm, DRM_MODE_PROP_BLOB, "EDID INFO", 0);
	if (!prop) {
		DRM_ERROR("drm_property_create dpu version failed\n");
		return -ENOMEM;
	}

	drm_object_attach_property(&connector->base, prop, spi->edid_blob->base.id);
	spi->edid_prop = prop;

	DRM_INFO("spi->edid_blob->base.id:%d\n", spi->edid_blob->base.id);

	return 0;
}

static int sprd_spi_panel_attach(struct sprd_drm_spi *spi)
{
	struct device_node *lcds_node;
	struct drm_panel *panel;

	lcds_node = sprd_panel_find_node_by_name();
	panel = of_drm_find_panel(lcds_node);
	if (panel) {
		spi->panel = panel;
	}

	if (!spi->panel) {
		DRM_ERROR("of_drm_find_panel() failed\n");
		return -ENODEV;
	}

	return 0;
}

static int sprd_spi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct sprd_drm_spi *spi = dev_get_drvdata(dev);
	int ret;

	ret = sprd_spi_encoder_init(drm, spi);
	if (ret)
		goto cleanup;

	ret = sprd_spi_connector_init(drm, spi);
	if (ret)
		goto cleanup_encoder;

	ret = sprd_spi_panel_attach(spi);
	if (ret)
		goto cleanup_connector;

	return 0;

cleanup_connector:
	drm_connector_cleanup(&spi->connector);
cleanup_encoder:
	drm_encoder_cleanup(&spi->encoder);
cleanup:
	return ret;
}

static void sprd_spi_unbind(struct device *dev,
			struct device *master, void *data)
{
	/* do nothing */
	DRM_INFO("%s()\n", __func__);
}

static const struct component_ops spi_component_ops = {
	.bind	= sprd_spi_bind,
	.unbind	= sprd_spi_unbind,
};

static unsigned char kEdid0[128] = {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x1c, 0xec, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x1b, 0x10, 0x01, 0x03, 0x80, 0x50, 0x2d, 0x78,
        0x0a, 0x0d, 0xc9, 0xa0, 0x57, 0x47, 0x98, 0x27, 0x12, 0x48, 0x4c, 0x00,
        0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38,
        0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xfc, 0x00, 0x45, 0x4d, 0x55, 0x5f, 0x64, 0x69, 0x73,
        0x70, 0x6c, 0x61, 0x79, 0x5f, 0x30, 0x00, 0x4b
};

static void sprd_edid_set_default_prop(struct edid *edid_info)
{
	memcpy(edid_info, kEdid0, sizeof(struct edid));
}

static int sprd_spi_probe(struct platform_device *pdev)
{
	struct sprd_drm_spi *spi;
	int ret;

	DRM_INFO("%s enter\n", __func__);
	spi = devm_kzalloc(&pdev->dev, sizeof(*spi), GFP_KERNEL);
	if (!spi) {
		DRM_ERROR("failed to allocate spi data.\n");
		return -ENOMEM;
	}
	spi->dev = &pdev->dev;

	ret = devm_of_platform_populate(&pdev->dev);
	if (ret)
		return ret;

	sprd_edid_set_default_prop(&spi->edid_info);

	platform_set_drvdata(pdev, spi);

	return component_add(&pdev->dev, &spi_component_ops);
}

static int sprd_spi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &spi_component_ops);

	return 0;
}

static const struct of_device_id spi_match_table[] = {
	{ .compatible = "sprd,spi-interface" },
	{ /* sentinel */ },
};

struct platform_driver sprd_spi_driver = {
	.probe = sprd_spi_probe,
	.remove = sprd_spi_remove,
	.driver = {
		.name = "sprd-spi-drv",
		.of_match_table = spi_match_table,
	},
};

MODULE_AUTHOR("Pony Wu <pony.wu@unisoc.com>");
MODULE_DESCRIPTION("Unisoc SPI BUS Device Driver");
MODULE_LICENSE("GPL v2");
