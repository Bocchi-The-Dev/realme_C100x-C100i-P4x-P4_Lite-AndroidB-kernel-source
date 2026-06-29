// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <asm/cacheflush.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/dma-buf.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/sched/clock.h>

#include "sprd_crtc.h"
#include "sprd_plane.h"
#include "sprd_swdispc.h"

static uint16_t bgra888_to_rgb565(uint8_t b, uint8_t g, uint8_t r)
{
	uint16_t rgb565;

	rgb565 = (((r >> 3) & 0x1f) << 11) |
		(((g >> 2) & 0x3f) << 5) |
		(((b >> 3) & 0x1f) << 0);

	return rgb565;
}

static uint32_t spi_rgb_ctrl(u32 format)
{

	u32 rgb_f = 0;

	switch (format) {
		case DRM_FORMAT_ABGR8888:
		case DRM_FORMAT_RGBA8888:
		case DRM_FORMAT_RGBX8888:
		case DRM_FORMAT_XRGB8888:
			/* rb switch */
			rgb_f = 1;
			break;
		case DRM_FORMAT_BGRA8888:
			rgb_f = 2;
			break;
		case DRM_FORMAT_BGR888:
		case DRM_FORMAT_RGB888:
			pr_err("spi dispc not support rgb888 format\n");
			break;
		default:
			break;
	}

	return rgb_f;
}

static int abgr32_to_rgb16(struct swdispc_context *ctx,
			struct spi_panel_info *panel, void *base)
{
	int i = 0;
	uint32_t *prgb32;
	uint16_t *prgb16;
	uint32_t rgb32;
	uint16_t rgb16;
	uint8_t r, g, b;
	uint16_t width, height;
	uint16_t line_bytes;

	width = panel->mode.hdisplay;
	height = panel->mode.vdisplay;
	line_bytes = (width * panel->bpp) / 8;

	if (ctx->first_alloc) {
		ctx->pframe = kzalloc(line_bytes * height, GFP_KERNEL);
		if (!ctx->pframe)
			return -ENOMEM;
		ctx->first_alloc = false;
	}

	prgb32 = (uint32_t *)base;
	prgb16 = (uint16_t *)ctx->pframe;

	for (i = 0; i < width * height; i++) {
		rgb32 = *prgb32++;
		r = rgb32 & 0xff;
		g = (rgb32 >> 8) & 0xff;
		b = (rgb32 >> 16) & 0xff;
		rgb16 = bgra888_to_rgb565(b, g, r);
		*prgb16 = rgb16;
		prgb16++;
	}

	return 0;
}

static int argb32_to_rgb16(struct swdispc_context *ctx,
			struct spi_panel_info *panel, void *base)
{
	int i = 0;
	uint32_t *prgb32;
	uint16_t *prgb16;
	uint32_t rgb32;
	uint16_t rgb16;
	uint8_t r, g, b;
	uint16_t width, height;
	uint16_t line_bytes;

	width = panel->mode.hdisplay;
	height = panel->mode.vdisplay;
	line_bytes = (width * panel->bpp) / 8;
	if (ctx->first_alloc) {
		ctx->pframe = kzalloc(line_bytes * height, GFP_KERNEL);
		if (!ctx->pframe)
			return -ENOMEM;
		ctx->first_alloc = false;
	}

	prgb32 = (uint32_t *)base;
	prgb16 = (uint16_t *)ctx->pframe;
	for (i = 0; i < width * height; i++) {
		rgb32 = *prgb32++;
		b = rgb32 & 0xff;
		g = (rgb32 >> 8) & 0xff;
		r = (rgb32 >> 16) & 0xff;
		rgb16 = bgra888_to_rgb565(b, g, r);
		*prgb16 = rgb16;
		prgb16++;
	}

	return 0;
}

static int copy_to_pframe(struct swdispc_context *ctx, struct sprd_spi_panel *spi_panel,
			void *base, uint16_t stride, uint16_t line_bytes)
{
	int i = 0;
	uint8_t *pbase;
	uint8_t *ptemp;
	uint16_t height;
	struct spi_panel_info *panel = &spi_panel->info;

	pbase = base;
	height = panel->mode.vdisplay;
	if (ctx->first_alloc) {
		ctx->pframe = kzalloc(line_bytes * height, GFP_KERNEL);
		if (!ctx->pframe)
			return -ENOMEM;
		ctx->first_alloc = false;
	}
	ptemp = ctx->pframe;
	for (i = 0; i < height; i++)
		memcpy(ptemp + i * line_bytes,
			pbase + i * stride, line_bytes);

	return 0;
}

static int exchange_pixels(struct sprd_spi_panel *spi_panel, void *base)
{
	uint32_t i;
	uint16_t height;
	uint16_t width;
	uint16_t *pbase;
	struct spi_panel_info *panel = &spi_panel->info;

	pbase = (uint16_t *)base;
	height = panel->mode.vdisplay;
	width = panel->mode.hdisplay;

	DRM_DEBUG("exchange_pixels\n");
	for (i = 0; i < height * width; i += 2)
		swap(pbase[i], pbase[i+1]);

	return 0;
}

static int exchange_words(struct sprd_spi_panel *spi_panel, void *base)
{
	uint32_t i;
	uint16_t height;
	uint16_t width;
	uint8_t *pbase;
	struct spi_panel_info *panel = &spi_panel->info;

	pbase = (uint8_t *)base;
	height = panel->mode.vdisplay;
	width = panel->mode.hdisplay;

	DRM_DEBUG("exchange_words\n");
	for (i = 0; i < height * width * 2; i += 2)
		swap(pbase[i], pbase[i+1]);

	return 0;
}

static void sw_dispc_version(struct swdispc_context *ctx)
{
	ctx->version = "swdispc";
}

int32_t swdispc_wait_te(struct swdispc_context *ctx)
{
	int rc;

	ctx->evt_te = false;
	/*wait for reg update done interrupt*/
	rc = wait_event_interruptible_timeout(ctx->te_wq, ctx->evt_te,
					       msecs_to_jiffies(500));
	if (!rc) {
		/* time out */
		DRM_ERROR("swdispc wait for reg update done time out!\n");
		return -1;
	}

	return 0;
}

static u32 sw_dispc_isr(struct swdispc_context *ctx)
{
	struct sprd_swdispc *swdispc = (struct sprd_swdispc *)container_of(ctx, struct sprd_swdispc, ctx);
	u32 reg_val = 0;

	if (ctx->odd_vsync) {
		reg_val = 1;
		ctx->odd_vsync = false;
	} else
		ctx->odd_vsync = true;

	ctx->evt_te = true;

	wake_up_interruptible_all(&ctx->te_wq);

	drm_crtc_handle_vblank(&swdispc->crtc->base);

	return reg_val;
}

static void sw_dispc_stop(struct swdispc_context *ctx)
{
	pr_info("%s enter\n", __func__);

	return;
}

static void sw_dispc_run(struct swdispc_context *ctx)
{
	pr_info("%s enter\n", __func__);

	return;
}

static void sw_dispc_bg_color(struct swdispc_context *ctx, uint32_t color)
{
	struct sprd_swdispc *swdispc = (struct sprd_swdispc *)container_of(ctx, struct sprd_swdispc, ctx);
	struct sprd_spi_panel *panel = to_sprd_spi_panel(swdispc->spi->panel);

	pr_info("%s enter\n", __func__);
	if (!ctx->flag_mask_refresh) {
		u8 bg_flag = 1;

		swdispc_wait_te(ctx);
		sprd_spi_refresh(panel, NULL, bg_flag);
	}
}

static int sw_dispc_init(struct swdispc_context *ctx)
{
	if (ctx->frame_1st == FRAME_1ST_NONE) {
		ctx->frame_1st = FRAME_1ST_FLIP;
	}

	pr_info("swdispc init\n");

	return 0;
}

static void sw_dispc_fini(struct swdispc_context *ctx)
{
	pr_info("swdispc uninit\n");
}

enum {
	DPU_LAYER_FORMAT_YUV422_2PLANE,
	DPU_LAYER_FORMAT_YUV420_2PLANE,
	DPU_LAYER_FORMAT_YUV420_3PLANE,
	DPU_LAYER_FORMAT_ARGB8888,
	DPU_LAYER_FORMAT_RGB565,
	DPU_LAYER_FORMAT_XFBC_ARGB8888 = 8,
	DPU_LAYER_FORMAT_XFBC_RGB565,
	DPU_LAYER_FORMAT_XFBC_YUV420,
	DPU_LAYER_FORMAT_MAX_TYPES,
};

static uint8_t* swdispc_layer(struct swdispc_context *ctx,
				struct sprd_layer_state *hwlayer)
{
	unsigned int layer_base;
	void *layer_vbase;
	unsigned int layer_bpp;
	unsigned int rgbf;
	uint16_t width, height, line_bytes, stride;
	const struct drm_format_info *info;
	struct sprd_swdispc *swdispc = (struct sprd_swdispc *)container_of(ctx, struct sprd_swdispc, ctx);
	struct sprd_spi_panel *panel = to_sprd_spi_panel(swdispc->spi->panel);
	struct spi_panel_info *spi_panel_info = &panel->info;

	info = drm_format_info(hwlayer->format);
	layer_bpp = (info->cpp[0]) * 8;
	rgbf = spi_rgb_ctrl(hwlayer->format);

	height = spi_panel_info->mode.vdisplay;
	width = spi_panel_info->mode.hdisplay;
	layer_base = hwlayer->addr[0];

	if (!layer_base) {
		DRM_WARN("layer base is null\n");
		return NULL;
	}

	layer_vbase = phys_to_virt(layer_base);
	DRM_DEBUG("%s() layer_base:0x%x, layer_vbase:0x%p\n", __func__, layer_base, layer_vbase);

	line_bytes = (width * spi_panel_info->bpp) / 8;
	stride = hwlayer->pitch[0];
	DRM_DEBUG("%s() layer_bpp:%d,rgbf:%d,line_bytes:%d, stride:%d\n", __func__, layer_bpp, rgbf, line_bytes, stride);

	if (layer_bpp == 32) {
		if (rgbf == 1)
			abgr32_to_rgb16(ctx, spi_panel_info, layer_vbase);
		else if (rgbf == 2)
			argb32_to_rgb16(ctx, spi_panel_info, layer_vbase);
		else
			return NULL;

		if (!ctx->pframe)
			return NULL;

		if(spi_panel_info->spi_bits_1word == 32) {
			exchange_pixels(panel, ctx->pframe);
		} else if(spi_panel_info->spi_bits_1word == 8) {
			exchange_words(panel, ctx->pframe);
		}
	} else {
		copy_to_pframe(ctx, panel, layer_vbase, stride, line_bytes);

		if (!ctx->pframe)
			return NULL;

		if(spi_panel_info->spi_bits_1word == 32)
			exchange_pixels(panel, ctx->pframe);
		else if(spi_panel_info->spi_bits_1word == 8)
			exchange_words(panel, ctx->pframe);
	}

	return ctx->pframe;
}

static void sw_dispc_flip(struct swdispc_context *ctx, struct sprd_plane planes[], u8 count)
{
	struct sprd_plane_state *state = NULL;
	struct sprd_layer_state *hwlayer = NULL;
	uint8_t *frame_addr = NULL;
	struct sprd_swdispc *swdispc = (struct sprd_swdispc *)container_of(ctx, struct sprd_swdispc, ctx);
	struct sprd_spi_panel *panel = to_sprd_spi_panel(swdispc->spi->panel);
	u8 bg_flag = 0;

	state = to_sprd_plane_state(planes[0].base.state);
	hwlayer = &state->layer;
	ctx->hwlayer_lastest = hwlayer;

	frame_addr = swdispc_layer(ctx, hwlayer);

	if (!frame_addr)
		return;

	swdispc_wait_te(ctx);

	sprd_spi_refresh(panel, frame_addr, bg_flag);

	if (ctx->frame_1st == FRAME_1ST_FLIP) {
		swdispc_wait_te(ctx);
		ctx->frame_1st = FRAME_1ST_NONE;
	}
}

static int sw_dispc_context_init(struct swdispc_context *ctx, struct device_node *np)
{
	pr_info("sw dispc context init\n");

	return 0;
}

static void sw_dispc_ifconfig(struct swdispc_context *ctx)
{
	DRM_INFO("swdispc ifconfig inited");
}

static const u32 primary_fmts[] = {
	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBX8888, DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565,
};

static void sw_dispc_capability(struct swdispc_context *ctx,
			struct sprd_crtc_capability *cap)
{
	cap->max_layers = 1;
	cap->fmts_ptr = primary_fmts;
	cap->fmts_cnt = ARRAY_SIZE(primary_fmts);
}

const struct swdispc_core_ops sw_dispc_core_ops = {
	.version = sw_dispc_version,
	.init = sw_dispc_init,
	.ifconfig = sw_dispc_ifconfig,
	.fini = sw_dispc_fini,
	.run = sw_dispc_run,
	.stop = sw_dispc_stop,
	.isr = sw_dispc_isr,
	.capability = sw_dispc_capability,
	.flip = sw_dispc_flip,
	.bg_color = sw_dispc_bg_color,
	.context_init = sw_dispc_context_init,
};
