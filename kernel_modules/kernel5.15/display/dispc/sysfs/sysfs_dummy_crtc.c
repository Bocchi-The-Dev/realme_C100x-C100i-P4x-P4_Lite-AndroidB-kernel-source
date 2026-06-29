// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>

#include <drm/drm_sysfs.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <linux/device.h>

#include "../disp_lib.h"
#include "../sprd_drm.h"
#include "sysfs_display.h"

#include "../perf_dummy/sprd_perf_dummy_connector.h"
#include "../perf_dummy/sprd_perf_dummy_crtc.h"

struct dummy_crtc_sysfs {
	u32 type;
	u32 input_param[64];
};

static struct dummy_crtc_sysfs *sysfs;

static ssize_t name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	const char *node_name = "sprd perf dummy crtc";
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%s\n", node_name);

	return ret;
}
static DEVICE_ATTR_RO(name);

static ssize_t display_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	//struct sprd_dummy_crtc *dummy_crtc = dev_get_drvdata(dev);
	int ret = 0;

	return ret;
}

void sprd_sysfs_mode_copy(struct drm_display_mode *dst, const struct drm_display_mode *src)
{
	struct list_head head = dst->head;

	*dst = *src;
	dst->head = head;
}

static void sprd_drm_sysfs_hotplug_event(struct drm_device *dev)
{
	char *event_string = "HOTPLUG=2";
	char *envp[2] = { event_string, NULL };

	DRM_INFO("generating sysfs hotplug event\n");

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}

static ssize_t display_mode_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct sprd_perf_dummy_crtc *dummy_crtc = dev_get_drvdata(dev);
	struct sprd_perf_dummy_connector *perf_conn = dummy_crtc->perf_conn;
	struct drm_connector *connector = &perf_conn->connector;
	struct drm_display_mode *new_mode;
	struct sprd_dummy_panel_info *info = &perf_conn->panel_info;
	struct drm_device *drm;
	struct videomode vm = {};
	u32 frame_rate = 0;
	struct drm_display_mode *old_pmode, *next;

	drm = dummy_crtc->crtc.dev;
	str_to_u32_array(buf, 10, sysfs->input_param, 9);

	new_mode = drm_mode_create(drm);
	drm_display_mode_to_videomode(new_mode, &vm);

	/* hactive/hfp/hbp/hsync/vactive/vfp/vbp/vsync/frame_rate */
	vm.hactive = sysfs->input_param[0];
	vm.hfront_porch = sysfs->input_param[1];
	vm.hback_porch = sysfs->input_param[2];
	vm.hsync_len = sysfs->input_param[3];
	vm.vactive = sysfs->input_param[4];
	vm.vfront_porch = sysfs->input_param[5];
	vm.vback_porch = sysfs->input_param[6];
	vm.vsync_len = sysfs->input_param[7];
	frame_rate = sysfs->input_param[8];

	drm_display_mode_from_videomode(&vm, new_mode);
	new_mode->clock = new_mode->htotal * new_mode->vtotal * frame_rate / 1000;

	new_mode->type = DRM_MODE_TYPE_DRIVER;
	new_mode->width_mm = 68;
	new_mode->height_mm = 152;

	/* remove all previous display modes */
	mutex_lock(&drm->mode_config.mutex);
	list_for_each_entry_safe(old_pmode, next, &connector->modes, head) {
		list_del(&old_pmode->head);
		drm_mode_destroy(connector->dev, old_pmode);
	}

	new_mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(new_mode));

	drm_mode_probed_add(connector, new_mode);

	sprd_sysfs_mode_copy(&info->mode, new_mode);
	memset(info->buildin_modes + sizeof(struct drm_display_mode), 0,
		(info->num_buildin_modes - 1) * sizeof(struct drm_display_mode));
	info->num_buildin_modes = 1;
	info->use_sysfs_mode = true;

	mutex_unlock(&drm->mode_config.mutex);

	sprd_drm_sysfs_hotplug_event(drm);

	return count;
}
static DEVICE_ATTR_RW(display_mode);

static struct attribute *dummy_crtc_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_display_mode.attr,
	NULL,
};

static const struct attribute_group dummy_crtc_group = {
	.attrs = dummy_crtc_attrs,
};

int sprd_dummy_crtc_sysfs_init(struct device *dev)
{
	int rc;

	sysfs = kzalloc(sizeof(*sysfs), GFP_KERNEL);

	rc = sysfs_create_group(&(dev->kobj), &dummy_crtc_group);
	if (rc)
		pr_err("create dpu attr node failed, rc=%d\n", rc);

	return rc;
}
EXPORT_SYMBOL(sprd_dummy_crtc_sysfs_init);

MODULE_AUTHOR("Pony Wu <pony.wu@unisoc.com>");
MODULE_DESCRIPTION("Add perf dummy crtc for userspace");
MODULE_LICENSE("GPL");

