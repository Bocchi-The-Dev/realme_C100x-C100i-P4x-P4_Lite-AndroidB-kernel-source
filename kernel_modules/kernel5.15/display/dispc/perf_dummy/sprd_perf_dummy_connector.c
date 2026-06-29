// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/of_display_timing.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include "../sprd_crtc.h"
#include "../disp_lib.h"
#include "../sysfs/sysfs_display.h"
#include "sprd_perf_dummy_connector.h"

#define encoder_to_perf_encoder(encoder) \
	container_of(encoder, struct sprd_perf_dummy_connector, encoder)

#define connector_to_perf_conn(connector) \
	container_of(connector, struct sprd_perf_dummy_connector, connector)

static void sprd_perf_encoder_enable(struct drm_encoder *encoder)
{
}

static void sprd_perf_encoder_disable(struct drm_encoder *encoder)
{
}

static const struct drm_encoder_helper_funcs sprd_perf_encoder_helper_funcs = {
	.enable = sprd_perf_encoder_enable,
	.disable = sprd_perf_encoder_disable
};

static const struct drm_encoder_funcs sprd_perf_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};


static int sprd_perf_encoder_init(struct drm_device *drm,
			       struct sprd_perf_dummy_connector *perf_conn)
{
	struct drm_encoder *encoder = &perf_conn->encoder;
	int ret;

	encoder->possible_crtcs = 0x01;
	ret = drm_encoder_init(drm, encoder, &sprd_perf_encoder_funcs,
				DRM_MODE_ENCODER_DPI, NULL);

	if (ret) {
		DRM_ERROR("failed to initialize dummy encoder\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &sprd_perf_encoder_helper_funcs);

	return 0;
}
static struct drm_display_mode *sprd_dummy_panel_create_sr_mode(struct drm_device *drm,
							struct drm_display_mode *original_mode,
							u32 sr_width, u32 sr_height)
{
	struct drm_display_mode *new_mode;
	struct videomode vm = {};

	new_mode = drm_mode_create(drm);
	drm_display_mode_to_videomode(original_mode, &vm);
	vm.hactive = sr_width;
	vm.vactive = sr_height;

	drm_display_mode_from_videomode(&vm, new_mode);
	new_mode->clock = new_mode->htotal * new_mode->vtotal *
				drm_mode_vrefresh(original_mode) / 1000;

	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(new_mode));

	return new_mode;
}
static int sprd_perf_dummy_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct sprd_perf_dummy_connector *perf_conn = connector_to_perf_conn(connector);
	struct sprd_dummy_panel_info *info = &perf_conn->panel_info;
	int i, mode_count = 0;
	u32 sr_width = 0, sr_height = 0;

	mode = drm_mode_duplicate(connector->dev, &info->mode);
	if (!mode) {
		DRM_ERROR("failed to alloc mode %s\n", info->mode.name);
		return 0;
	}

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
	mode_count++;

	if (!info->use_sysfs_mode) {
		for (i = 1; i < info->num_buildin_modes; i++) {
			mode = drm_mode_duplicate(connector->dev,
				&(info->buildin_modes[i]));
			if (!mode) {
				DRM_ERROR("failed to alloc mode %s\n", info->buildin_modes[i].name);
				return 0;
			}

			mode->type = DRM_MODE_TYPE_DRIVER;
			drm_mode_probed_add(connector, mode);
			mode_count++;
		}

		/* parse and create sr mode config */
		of_property_read_u32(info->of_node, "sprd,sr-width", &sr_width);
		of_property_read_u32(info->of_node, "sprd,sr-height", &sr_height);
		if (sr_width && sr_height) {
			DRM_INFO("find sr config: width:%d, height:%d\n", sr_width, sr_height);
			for (i = 0; i < info->num_buildin_modes; i++) {
				mode = sprd_dummy_panel_create_sr_mode(connector->dev,
						&info->buildin_modes[i], sr_width, sr_height);
				mode->type = DRM_MODE_TYPE_DRIVER;
				mode->width_mm = info->mode.width_mm;
				mode->height_mm = info->mode.height_mm;
				drm_mode_probed_add(connector, mode);
				mode_count++;
			}
		}
	}

	connector->display_info.width_mm = info->mode.width_mm;
	connector->display_info.height_mm = info->mode.height_mm;
	info->display_mode_count = mode_count;

	return mode_count;
}

static enum drm_mode_status
sprd_perf_dummy_connector_mode_valid(struct drm_connector *connector,
			 struct drm_display_mode *mode)
{

	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));

	return MODE_OK;
}

static struct drm_connector_helper_funcs sprd_perf_dummy_connector_helper_funcs = {
	.get_modes = sprd_perf_dummy_connector_get_modes,
	.mode_valid = sprd_perf_dummy_connector_mode_valid,
};

static void sprd_perf_dummy_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int sprd_perf_dummy_conn_atomic_get_property(struct drm_connector *connector,
					const struct drm_connector_state *state,
					struct drm_property *property,
					uint64_t *val)
{
	struct sprd_perf_dummy_connector *perf_conn = connector_to_perf_conn(connector);


	if (property == perf_conn->edid_prop) {
		memcpy(perf_conn->edid_blob->data, &perf_conn->edid_info, sizeof(struct edid));
		*val = perf_conn->edid_blob->base.id;
		DRM_INFO("%s() val = %d\n", __func__, perf_conn->edid_blob->base.id);
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_connector_funcs sprd_perf_dummy_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = sprd_perf_dummy_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_get_property = sprd_perf_dummy_conn_atomic_get_property,
};

static int sprd_perf_dummy_connector_init(struct drm_device *drm,
				     struct sprd_perf_dummy_connector *perf_conn)
{
	struct drm_encoder *encoder = &perf_conn->encoder;
	struct drm_connector *connector = &perf_conn->connector;
	struct drm_property *prop;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;
	ret = drm_connector_init(drm, connector,
				 &sprd_perf_dummy_connector_funcs,
				 DRM_MODE_CONNECTOR_DPI);
	if (ret) {
		DRM_ERROR("drm_connector_init() failed\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &sprd_perf_dummy_connector_helper_funcs);
	drm_connector_attach_encoder(connector, encoder);

	perf_conn->edid_blob = drm_property_create_blob(drm, (sizeof(struct edid) + 1),
							&perf_conn->edid_info);
	if (IS_ERR(perf_conn->edid_blob)) {
		DRM_ERROR("drm_property_create_blob edid blob failed\n");
		return PTR_ERR(perf_conn->edid_blob);
	}

	prop = drm_property_create(drm, DRM_MODE_PROP_BLOB, "EDID INFO", 0);
	if (!prop) {
		DRM_ERROR("drm_property_create dpu version failed\n");
		return -ENOMEM;
	}

	drm_object_attach_property(&connector->base, prop, perf_conn->edid_blob->base.id);
	perf_conn->edid_prop = prop;

	DRM_INFO("perf_conn->edid_blob->base.id:%d\n", perf_conn->edid_blob->base.id);

	return 0;
}

static int sprd_perf_dummy_connector_bind(struct device *dev, struct device *master,
				   void *data)
{
	struct drm_device *drm = data;
	struct sprd_perf_dummy_connector *perf_conn = dev_get_drvdata(dev);
	int ret;

	ret = sprd_perf_encoder_init(drm, perf_conn);
	if (ret)
		return ret;

	ret = sprd_perf_dummy_connector_init(drm, perf_conn);
	if (ret) {
		drm_encoder_cleanup(&perf_conn->encoder);
		return ret;
	}

	return 0;
}
static const struct component_ops sprd_perf_dummy_connector_component_ops = {
	.bind = sprd_perf_dummy_connector_bind,
};

static int of_parse_dummy_buildin_modes(struct sprd_dummy_panel_info *info,
	struct device_node *conn_node)
{
	int i, rc, num_timings;
	struct device_node *timings_np;

	timings_np = of_get_child_by_name(conn_node, "display-timings");
	if (!timings_np) {
		DRM_ERROR("%s: can not find display-timings node\n",
			conn_node->name);
		return -ENODEV;
	}

	num_timings = of_get_child_count(timings_np);
	if (num_timings == 0) {
		/* should never happen, as entry was already found above */
		DRM_ERROR("%s: no timings specified\n", conn_node->name);
		goto done;
	}

	info->buildin_modes = kzalloc(sizeof(struct drm_display_mode) *
				num_timings, GFP_KERNEL);
	for (i = 0; i < num_timings; i++) {
		rc = of_get_drm_display_mode(conn_node,
			&info->buildin_modes[i], NULL, i);
		if (rc) {
			DRM_ERROR("get display timing failed\n");
			goto entryfail;
		}
		info->buildin_modes[i].width_mm = info->mode.width_mm;
		info->buildin_modes[i].height_mm = info->mode.height_mm;
	}

	info->num_buildin_modes = num_timings;
	DRM_INFO("info->num_buildin_modes = %d\n", num_timings);
	goto done;

entryfail:
	kfree(info->buildin_modes);

done:
	of_node_put(timings_np);

	return 0;
}

static int sprd_perf_dummy_connector_parse_dt(struct device_node *np,
				struct sprd_perf_dummy_connector *perf_conn)
{
	int rc;
	const char *str;
	u32 val;
	struct sprd_dummy_panel_info *info = &perf_conn->panel_info;

	info->of_node = np;
	rc = of_get_drm_display_mode(np, &info->mode, 0,
					OF_USE_NATIVE_MODE);
	if (rc) {
		DRM_ERROR("get display timing failed\n");
		return rc;
	}

	of_parse_dummy_buildin_modes(info, np);

	rc = of_property_read_u32(np, "sprd,width-mm", &val);
	if (!rc)
		info->mode.width_mm = val;
	else
		info->mode.width_mm = 68;

	rc = of_property_read_u32(np, "sprd,height-mm", &val);
	if (!rc)
		info->mode.height_mm = val;
	else
		info->mode.height_mm = 121;

	rc = of_property_read_u32(np, "sprd,dummy-panel-work-mode", &val);
	if (!rc) {
		info->work_mode = val;
	} else {
		DRM_ERROR("dummy panel work mode is not found! use video mode\n");
		info->work_mode = PERF_WORK_MODE_VIDEO;
	}

	if (of_property_read_bool(np, "sprd,dpi-clk-pixelpll")) {
		info->dpi_clk_pixelpll = true;
		pr_info("read sprd,dpi-clk-pixelpll success, dpi_clk_pixelpll = true\n");
	} else {
		info->dpi_clk_pixelpll = false;
		pr_info("read sprd,dpi-clk-pixelpll failed, dpi_clk_pixelpll = false\n");
	}

	rc = of_property_read_u32(np, "sprd,slice-width", &val);
	if (!rc)
		info->slice_width = val;
	else
		DRM_DEBUG("slice-width is not found!\n");

	rc = of_property_read_u32(np, "sprd,slice-height", &val);
	if (!rc)
		info->slice_height = val;
	else
		DRM_DEBUG("slice-height is not found!\n");

	rc = of_property_read_u32(np, "sprd,output-bpc", &val);
	if (!rc)
		info->output_bpc = val;
	else
		DRM_DEBUG("output-bpc is not found!\n");

	rc = of_property_read_u32(np, "sprd,dsc-enable", &val);
	if (!rc)
		info->dsc_en = val;
	else
		DRM_DEBUG("dsc-enable is not found!\n");

	rc = of_property_read_u32(np, "sprd,dual-output-enable", &val);
	if (!rc)
		info->dual_output_en = val;
	else
		DRM_DEBUG("dual-output-enable is not found!\n");

	rc = of_property_read_string(np, "sprd,dummy-panel-color-format", &str);
	if (rc)
		info->format = PERF_OUTPUT_FMT_RGB888;
	else if (!strcmp(str, "rgb888"))
		info->format = PERF_OUTPUT_FMT_RGB888;
	else if (!strcmp(str, "rgb666"))
		info->format = PERF_OUTPUT_FMT_RGB666;
	else if (!strcmp(str, "rgb666_packed"))
		info->format = PERF_OUTPUT_FMT_RGB666_PACKED;
	else if (!strcmp(str, "rgb565"))
		info->format = PERF_OUTPUT_FMT_RGB565;
	else if (!strcmp(str, "dsc"))
		info->format = PERF_OUTPUT_FMT_DSC;
	else
		DRM_ERROR("output-color-format (%s) is not supported\n", str);

	return 0;
}

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

static int sprd_perf_dummy_connector_device_create(struct sprd_perf_dummy_connector *perf_conn,
				struct device *parent)
{
	int ret;

	perf_conn->dev.class = display_class;
	perf_conn->dev.parent = parent;
	perf_conn->dev.of_node = parent->of_node;
	dev_set_name(&perf_conn->dev, "perf connector");
	dev_set_drvdata(&perf_conn->dev, perf_conn);

	ret = device_register(&perf_conn->dev);
	if (ret)
		DRM_ERROR("perf_conn device register failed\n");

	return ret;
}

static int sprd_perf_dummy_connector_probe(struct platform_device *pdev)
{
	struct sprd_perf_dummy_connector *perf_conn;
	int ret;

	perf_conn = devm_kzalloc(&pdev->dev, sizeof(*perf_conn), GFP_KERNEL);
	if (!perf_conn) {
		DRM_ERROR("failed to allocate perf_conn data.\n");
		return -ENOMEM;
	}

	ret = sprd_perf_dummy_connector_parse_dt(pdev->dev.of_node, perf_conn);
	if (ret) {
		DRM_ERROR("conn parse dt failed\n");
		return ret;
	}

	sprd_edid_set_default_prop(&perf_conn->edid_info);

	sprd_perf_dummy_connector_device_create(perf_conn, &pdev->dev);

	platform_set_drvdata(pdev, perf_conn);

	return component_add(&pdev->dev, &sprd_perf_dummy_connector_component_ops);
}

static int sprd_perf_dummy_connector_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sprd_perf_dummy_connector_component_ops);

	return 0;
}

static const struct of_device_id sprd_perf_dummy_connector_of_match[] = {
	{ .compatible = "sprd,perf-dummy-connector" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sprd_perf_dummy_connector_of_match);

struct platform_driver sprd_perf_dummy_connector_driver = {
	.probe = sprd_perf_dummy_connector_probe,
	.remove = sprd_perf_dummy_connector_remove,
	.driver = {
		.name = "sprd-perf-dummy-connector-drv",
		.of_match_table = sprd_perf_dummy_connector_of_match,
	},
};

MODULE_AUTHOR("Pony Wu <pony.wu@unisoc.com>");
MODULE_DESCRIPTION("Perf Dummy Connector Driver for Unisoc");
MODULE_LICENSE("GPL");
