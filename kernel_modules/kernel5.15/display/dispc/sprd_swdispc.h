/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SPRD_SWDISPC_H_
#define _SPRD_SWDISPC_H_

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <video/videomode.h>

#include <uapi/drm/drm_mode.h>
#include <drm/drm_vblank.h>

#include "sprd_crtc.h"
#include "sprd_plane.h"
#include "disp_lib.h"
#include "sprd_drm_spi.h"
#include "sprd_spi_panel.h"

enum {
	SPRD_DPU_IF_DBI = 0,
	SPRD_DPU_IF_DPI,
	SPRD_DPU_IF_EDPI,
	SPRD_DPU_IF_LIMIT
};

enum spi_frame_1st {
	FRAME_1ST_NONE,
	FRAME_1ST_FLIP,
	FRAME_1ST_TE,
};

struct swdispc_context;

struct swdispc_core_ops {
	void (*version)(struct swdispc_context *ctx);
	int (*init)(struct swdispc_context *ctx);
	void (*fini)(struct swdispc_context *ctx);
	void (*run)(struct swdispc_context *ctx);
	void (*stop)(struct swdispc_context *ctx);
	u32 (*isr)(struct swdispc_context *ctx);
	void (*ifconfig)(struct swdispc_context *ctx);
	void (*flip)(struct swdispc_context *ctx,
		     struct sprd_plane planes[], u8 count);
	void (*capability)(struct swdispc_context *ctx,
			 struct sprd_crtc_capability *cap);
	void (*bg_color)(struct swdispc_context *ctx, u32 color);
	int (*context_init)(struct swdispc_context *ctx, struct device_node *np);
};

struct swdispc_clk_ops {
	int (*parse_dt)(struct swdispc_context *ctx,
			struct device_node *np);
	int (*init)(struct swdispc_context *ctx);
	int (*uinit)(struct swdispc_context *ctx);
	int (*enable)(struct swdispc_context *ctx);
	int (*disable)(struct swdispc_context *ctx);
	int (*update)(struct swdispc_context *ctx, int clk_id, int val);
};

struct swdispc_glb_ops {
	int (*parse_dt)(struct swdispc_context *ctx,
			struct device_node *np);
	void (*enable)(struct swdispc_context *ctx);
	void (*disable)(struct swdispc_context *ctx);
	void (*reset)(struct swdispc_context *ctx);
	void (*suspend_reset)(struct swdispc_context *ctx);
	void (*power)(struct swdispc_context *ctx, int enable);
};

struct swdispc_context {
	/* swdispc common parameters */
	void __iomem *base;
	u32 base_offset[2];
	const char *version;
	int irq;
	u8 if_type;
	struct videomode vm;
	struct semaphore lock;
	bool enabled;
	bool stopped;
	bool flip_pending;
	wait_queue_head_t wait_queue;
	bool evt_update;
	bool evt_stop;
	irqreturn_t (*swdispc_isr)(int irq, void *data);
	struct tasklet_struct dvfs_task;
	bool is_single_run;
	spinlock_t irq_lock;

	/* te check parameters */
	wait_queue_head_t te_wq;
	bool te_check_en;
	
	/* corner config parameters */
	u32 corner_size;
	int sprd_corner_radius;
	bool sprd_corner_support;
	bool panel_ready;

	/* spi frame refresh parameters */
	bool odd_vsync;
	bool evt_te;
	uint8_t *pframe;
	bool first_alloc;
	unsigned int flag_mask_refresh;
	struct sprd_layer_state *hwlayer_lastest;
	int frame_1st;
};

struct sprd_swdispc_ops {
	const struct swdispc_core_ops *core;
};

struct sprd_swdispc {
	struct device dev;
	struct sprd_crtc *crtc;
	struct swdispc_context ctx;
	const struct swdispc_core_ops *core;
	struct drm_display_mode mode;
	struct drm_display_mode actual_mode;
	struct sprd_drm_spi *spi;
};

void sprd_spi_swdispc_run(struct sprd_swdispc *swdispc);
void sprd_spi_swdispc_stop(struct sprd_swdispc *swdispc);
void sprd_spi_swdispc_resume(struct sprd_swdispc *swdispc);
void sprd_spi_swdispc_disable(struct sprd_swdispc *swdispc);

extern const struct swdispc_core_ops sw_dispc_core_ops;

#endif /* _SPRD_SWDISPC_H_ */
