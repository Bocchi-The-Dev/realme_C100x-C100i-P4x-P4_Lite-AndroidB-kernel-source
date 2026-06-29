// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */
#include <linux/component.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/time.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "sprd_perf_dummy_crtc.h"
#include "../sprd_plane.h"
#include "../disp_lib.h"
#include "../sysfs/sysfs_display.h"


static inline struct sprd_perf_dummy_crtc *crtc_to_dummy(struct drm_crtc *crtc)
{
	return crtc ? container_of(crtc, struct sprd_perf_dummy_crtc, crtc) : NULL;
}

static enum hrtimer_restart vsync_timer_func(struct hrtimer *timer)
{
	struct sprd_perf_dummy_crtc *dummy = container_of(timer, struct sprd_perf_dummy_crtc,
						vsync_timer);
	drm_crtc_handle_vblank(&dummy->crtc);

	hrtimer_forward_now(timer, ns_to_ktime(dummy->vsync_timegap));

	return HRTIMER_RESTART;
}

static void sprd_perf_dummy_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_atomic_state *old_state)
{
	drm_crtc_vblank_on(crtc);
}

static void sprd_perf_dummy_crtc_atomic_disable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	drm_crtc_vblank_off(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void sprd_perf_dummy_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_atomic_state *old_state)

{
	struct sprd_perf_dummy_crtc *dummy = crtc_to_dummy(crtc);
	struct drm_device *drm = dummy->crtc.dev;

	spin_lock_irq(&drm->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&drm->event_lock);
}

static int sprd_perf_dummy_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct sprd_perf_dummy_crtc *dummy = crtc_to_dummy(crtc);

	hrtimer_start(&dummy->vsync_timer, ns_to_ktime(dummy->vsync_timegap),
		      HRTIMER_MODE_REL);

	return 0;
}

static void sprd_perf_dummy_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct sprd_perf_dummy_crtc *dummy = crtc_to_dummy(crtc);

	hrtimer_cancel(&dummy->vsync_timer);
}

static void sprd_perf_dummy_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct sprd_perf_dummy_crtc *dummy = crtc_to_dummy(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;

	if (drm_mode_vrefresh(mode) == 60)
		dummy->vsync_timegap = VSYNC_GAP_60HZ;
	else if (drm_mode_vrefresh(mode) == 90)
		dummy->vsync_timegap = VSYNC_GAP_90HZ;
	else if (drm_mode_vrefresh(mode) == 120)
		dummy->vsync_timegap = VSYNC_GAP_120HZ;
	else
		dummy->vsync_timegap = VSYNC_GAP_60HZ;

	DRM_INFO("crefresh rate changed, vsync timegap set to %u ns\n", dummy->vsync_timegap);
}

static enum drm_mode_status sprd_perf_dummy_crtc_mode_valid(struct drm_crtc *crtc,
		const struct drm_display_mode *mode)
{
	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));

	return MODE_OK;
}

static const struct drm_crtc_helper_funcs sprd_perf_dummy_crtc_helper_funcs = {
	.mode_set_nofb	= sprd_perf_dummy_crtc_mode_set_nofb,
	.atomic_enable	= sprd_perf_dummy_crtc_atomic_enable,
	.atomic_disable	= sprd_perf_dummy_crtc_atomic_disable,
	.atomic_flush = sprd_perf_dummy_crtc_atomic_flush,
	.mode_valid	= sprd_perf_dummy_crtc_mode_valid,
};

static int sprd_perf_dummy_crtc_atomic_get_property(struct drm_crtc *drm_crtc,
					const struct drm_crtc_state *crtc_state,
					struct drm_property *property,
					uint64_t *val)
{
	struct sprd_perf_dummy_crtc *dummy = crtc_to_dummy(drm_crtc);

	DRM_DEBUG("%s() name = %s\n", __func__, property->name);

	if (property == dummy->resolution_property) {
		*val = true;
	} else if (property == dummy->frame_rate_property) {
		*val = true;
	} else if (property == dummy->vrr_enabled_property) {
		*val = true;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static int sprd_perf_dummy_crtc_atomic_set_property(struct drm_crtc *drm_crtc,
					struct drm_crtc_state *crtc_state,
					struct drm_property *property,
					uint64_t val)
{
	struct sprd_perf_dummy_crtc *dummy = crtc_to_dummy(drm_crtc);

	DRM_DEBUG("%s() name = %s, val = %llu\n",
		  __func__, property->name, val);

	if (property == dummy->resolution_property)
		DRM_DEBUG("userspace set resolution change property.\n");
	else if (property == dummy->frame_rate_property)
		DRM_DEBUG("userspace set frame rate change property.\n");
	else if (property == dummy->blend_limit_property)
		DRM_DEBUG("do not allow change blend limit property value.\n");
	else if (property == dummy->vrr_enabled_property)
		DRM_DEBUG("do not allow change vrr enabled property value.\n");
	else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_crtc_funcs sprd_perf_dummy_crtc_funcs = {
	.destroy	= drm_crtc_cleanup,
	.set_config	= drm_atomic_helper_set_config,
	.page_flip	= drm_atomic_helper_page_flip,
	.reset		= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank	= sprd_perf_dummy_crtc_enable_vblank,
	.disable_vblank	= sprd_perf_dummy_crtc_disable_vblank,
	.atomic_set_property = sprd_perf_dummy_crtc_atomic_set_property,
	.atomic_get_property = sprd_perf_dummy_crtc_atomic_get_property,
};

static int sprd_perf_dummy_crtc_create_properties(struct sprd_perf_dummy_crtc *crtc,
						const char *version)
{
	struct drm_property *prop;
	struct drm_property_blob *blob;
	size_t blob_size;
	struct drm_mode_config *config;

	blob_size = strlen(version) + 1;

	blob = drm_property_create_blob(crtc->crtc.dev, blob_size, version);
	if (IS_ERR(blob)) {
		DRM_ERROR("drm_property_create_blob dpu version failed\n");
		return PTR_ERR(blob);
	}

	/* create dpu version property */
	prop = drm_property_create(crtc->crtc.dev,
		DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_BLOB,
		"dpu version", 0);
	if (!prop) {
		DRM_ERROR("drm_property_create dpu version failed\n");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->crtc.base, prop, blob->base.id);

	/* create resolution change property */
	prop = drm_property_create_range(crtc->crtc.dev, 0,
			"resolution change", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&crtc->crtc.base, prop, 0);
	crtc->resolution_property = prop;

	/* create frame rate change property */
	prop = drm_property_create_range(crtc->crtc.dev, 0,
			"frame rate change", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&crtc->crtc.base, prop, 0);
	crtc->frame_rate_property = prop;

	/* create vrr enabled property */
	prop = drm_property_create_range(crtc->crtc.dev, 0,
				"vrr enabled", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&crtc->crtc.base, prop, 0);
	crtc->vrr_enabled_property = prop;

	config = &crtc->crtc.dev->mode_config;
	drm_object_attach_property(&crtc->crtc.base, config->ctm_property, 0);

	return 0;
}

static int sprd_perf_dummy_crtc_init(struct drm_device *drm, struct drm_crtc *crtc)
{
	struct device_node *port;
	struct sprd_plane *planes;
	struct drm_plane *primary = NULL;
	struct sprd_perf_dummy_crtc *dummy = crtc_to_dummy(crtc);
	const char *version = "perf-dummy-crtc";
	int err;

	dummy->cap.max_layers = SPRD_DUMMY_PLANE_CNT;
	dummy->cap.fmts_ptr = primary_fmts;
	dummy->cap.fmts_cnt = ARRAY_SIZE(primary_fmts);
	/*
	 * set crtc port so that drm_of_find_possible_crtcs call works
	 */
	port = of_parse_phandle(drm->dev->of_node, "perf_dummy_ports", 0);
	if (!port) {
		DRM_ERROR("find 'ports' phandle of %s failed\n",
			  drm->dev->of_node->full_name);
		return -EINVAL;
	}
	of_node_put(port);
	crtc->port = port;

	planes = sprd_plane_init(drm, &dummy->cap, 1);
	primary = &planes[0].base;

	err = drm_crtc_init_with_planes(drm, crtc, primary, NULL,
					&sprd_perf_dummy_crtc_funcs, "sprd-perf-dummy-crtc");
	if (err) {
		DRM_ERROR("failed to init crtc.\n");
		return err;
	}

	drm_crtc_helper_add(crtc, &sprd_perf_dummy_crtc_helper_funcs);

	sprd_perf_dummy_crtc_create_properties(dummy, version);

	return 0;
}

static struct sprd_perf_dummy_connector *sprd_dummy_crtc_conn_attach(
					struct sprd_perf_dummy_crtc *dummy)
{
	struct device *dev;
	struct sprd_perf_dummy_connector *perf_conn;

	DRM_INFO("dpu attach dsi\n");
	dev = sprd_disp_pipe_get_output(&dummy->dev);
	if (!dev) {
		DRM_ERROR("dpu pipe get output failed\n");
		return NULL;
	}

	perf_conn = dev_get_drvdata(dev);
	if (!perf_conn) {
		DRM_ERROR("dpu attach dsi failed\n");
		return NULL;
	}

	perf_conn->dummy_crtc = dummy;

	return perf_conn;
}

static int sprd_perf_dummy_crtc_bind(struct device *dev, struct device *master,
				void *data)
{
	struct drm_device *drm = data;
	struct sprd_perf_dummy_crtc *dummy = dev_get_drvdata(dev);
	int err;

	err = sprd_perf_dummy_crtc_init(drm, &dummy->crtc);
	if (err)
		return err;

	dummy->perf_conn = sprd_dummy_crtc_conn_attach(dummy);

	return 0;
}

static void sprd_perf_dummy_crtc_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct sprd_perf_dummy_crtc *dummy = dev_get_drvdata(dev);

	drm_crtc_cleanup(&dummy->crtc);
}

static const struct component_ops dummy_component_ops = {
	.bind = sprd_perf_dummy_crtc_bind,
	.unbind = sprd_perf_dummy_crtc_unbind,
};

static int sprd_perf_dummy_crtc_device_create(struct sprd_perf_dummy_crtc *dummy,
				struct device *parent)
{
	int ret;

	dummy->dev.class = display_class;
	dummy->dev.parent = parent;
	dummy->dev.of_node = parent->of_node;
	dev_set_name(&dummy->dev, "perf_dummy_crtc");
	dev_set_drvdata(&dummy->dev, dummy);

	ret = device_register(&dummy->dev);
	if (ret) {
		DRM_ERROR("perf dummy crtc device register failed\n");
		return ret;
	}

	return 0;
}

static int sprd_perf_dummy_crtc_probe(struct platform_device *pdev)
{
	struct sprd_perf_dummy_crtc *dummy;

	dummy = devm_kzalloc(&pdev->dev, sizeof(*dummy), GFP_KERNEL);
	if (!dummy)
		return -ENOMEM;

	hrtimer_init(&dummy->vsync_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	dummy->vsync_timer.function = vsync_timer_func;
	dummy->vsync_timegap = VSYNC_GAP_60HZ;

	sprd_perf_dummy_crtc_device_create(dummy, &pdev->dev);

	sprd_dummy_crtc_sysfs_init(&dummy->dev);

	platform_set_drvdata(pdev, dummy);

	return component_add(&pdev->dev, &dummy_component_ops);
}

static int sprd_perf_dummy_crtc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dummy_component_ops);
	return 0;
}

static const struct of_device_id sprd_perf_dummy_crtc_match_table[] = {
	{ .compatible = "sprd,perf-dummy-crtc", },
	{ /* sentinel */ },
};

struct platform_driver sprd_perf_dummy_crtc_driver = {
	.probe = sprd_perf_dummy_crtc_probe,
	.remove = sprd_perf_dummy_crtc_remove,
	.driver = {
		.name = "sprd-perf-dummy-crtc-drv",
		.of_match_table = sprd_perf_dummy_crtc_match_table,
	},
};

MODULE_AUTHOR("Pony Wu <pony.wu@unisoc.com>");
MODULE_DESCRIPTION("Dummy Perf CRTC Driver for Unisoc");
MODULE_LICENSE("GPL");
