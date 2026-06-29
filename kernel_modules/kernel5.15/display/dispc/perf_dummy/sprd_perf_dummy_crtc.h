/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SPRD_PERF_DUMMY_CRTC_
#define _SPRD_PERF_DUMMY_CRTC_

#include <drm/drm_fourcc.h>
#include "../sprd_drm.h"
#include "../sprd_crtc.h"
#include "sprd_perf_dummy_connector.h"

#define SPRD_DUMMY_PLANE_CNT	10

#define VSYNC_GAP_60HZ	16666666
#define VSYNC_GAP_90HZ	11111111
#define VSYNC_GAP_120HZ	8333333

struct sprd_perf_dummy_crtc {
	struct drm_crtc crtc;
	struct drm_pending_vblank_event *event;
	struct hrtimer vsync_timer;
	u32 vsync_timegap;
	struct device dev;
	struct sprd_plane *planes;
	struct sprd_perf_dummy_connector *perf_conn;
	u8 pending_planes;
	void *priv;
	bool fps_mode_changed;
	bool sr_mode_changed;
	bool mode_change_pending;
	struct sprd_crtc_capability cap;

	struct drm_property *resolution_property;
	struct drm_property *frame_rate_property;
	struct drm_property *blend_limit_property;
	struct drm_property *vrr_enabled_property;
};

static const u32 primary_fmts[] = {
	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBX8888, DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12, DRM_FORMAT_NV21,
	DRM_FORMAT_NV16, DRM_FORMAT_NV61,
	DRM_FORMAT_YUV420, DRM_FORMAT_YVU420,
};

#endif
