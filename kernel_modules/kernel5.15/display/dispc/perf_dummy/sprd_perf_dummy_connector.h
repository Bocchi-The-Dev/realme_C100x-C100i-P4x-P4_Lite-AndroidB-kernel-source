/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */
#ifndef _SPRD_PERF_DUMMY_CONN_
#define _SPRD_PERF_DUMMY_CONN_

#include <linux/of.h>
#include <linux/device.h>
#include <video/videomode.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_print.h>
#include <drm/drm_panel.h>

enum perf_output_fmt {
	PERF_OUTPUT_FMT_RGB565 = 0,
	PERF_OUTPUT_FMT_RGB666,
	PERF_OUTPUT_FMT_RGB666_PACKED,
	PERF_OUTPUT_FMT_RGB888,
	PERF_OUTPUT_FMT_RGB101010,
	PERF_OUTPUT_FMT_DSC
};

enum perf_work_mode {
	PERF_WORK_MODE_CMD = 0,
	PERF_WORK_MODE_VIDEO
};

struct sprd_dummy_panel_info {
	/* common parameters */
	struct device_node *of_node;
	struct drm_display_mode mode;
	struct drm_display_mode *buildin_modes;
	int num_buildin_modes;
	int display_mode_count;
	int panel_type;
	int work_mode;
	u32 format;

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
	u32 dual_output_en;

	/* pixelpll config parameters */
	bool dpi_clk_pixelpll;
	bool use_sysfs_mode;
};

struct sprd_perf_dummy_connector {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct device dev;
	struct sprd_perf_dummy_crtc *dummy_crtc;
	//struct drm_display_mode mode;
	struct sprd_dummy_panel_info panel_info;
	struct edid edid_info;
	struct drm_property *edid_prop;
	struct drm_property_blob *edid_blob;
};

#endif /* _SPRD_PERF_CONN_ */
