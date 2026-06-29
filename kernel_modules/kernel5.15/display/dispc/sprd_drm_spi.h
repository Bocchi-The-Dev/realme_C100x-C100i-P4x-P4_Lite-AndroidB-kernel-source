/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SPRD_DRM_SPI_H_
#define _SPRD_DRM_SPI_H_

#include <linux/of.h>
#include <linux/device.h>
#include <video/videomode.h>

#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_print.h>
#include <drm/drm_panel.h>

#include "disp_lib.h"

struct sprd_drm_spi {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_panel *panel;
	struct device *dev;
	struct sprd_swdispc *swdispc;

	/* edid releated information for repoting display device HW info to framework */
	struct edid edid_info;
	struct drm_property *edid_prop;
	struct drm_property_blob *edid_blob;
};

#endif /* _SPRD_DRM_SPI_H_ */
