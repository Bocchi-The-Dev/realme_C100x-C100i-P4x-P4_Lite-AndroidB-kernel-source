// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/component.h>
#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/mm.h>
#include <linux/sprd_iommu.h>
#include <linux/memblock.h>

#include <linux/gpio.h>
#include <linux/io.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "sprd_crtc.h"
#include "sprd_swdispc.h"
#include "sprd_drm.h"
#include "sprd_gem.h"
#include "sprd_iommu.h"
#include "sprd_plane.h"
#include "sysfs/sysfs_display.h"

static void sprd_swdispc_enable(struct sprd_swdispc *swdispc);
void sprd_swdispc_disable(struct sprd_swdispc *swdispc);

static void sprd_swdispc_prepare_fb(struct sprd_crtc *crtc,
				struct drm_plane_state *new_state)
{
	DRM_DEBUG("%s()\n", __func__);
}

static void sprd_swdispc_cleanup_fb(struct sprd_crtc *crtc,
				struct drm_plane_state *old_state)
{
	DRM_DEBUG("%s()\n", __func__);
}

static void sprd_swdispc_mode_set_nofb(struct sprd_crtc *crtc)
{
	struct drm_display_mode *mode = &crtc->base.state->adjusted_mode;

	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));
}

static enum drm_mode_status sprd_swdispc_mode_valid(struct sprd_crtc *crtc,
					const struct drm_display_mode *mode)
{
	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));

	return MODE_OK;
}

static void sprd_swdispc_atomic_enable(struct sprd_crtc *crtc)
{
	struct sprd_swdispc *swdispc = crtc->priv;
	static bool is_enable = true;

	DRM_INFO("%s()\n", __func__);
	if (is_enable)
		is_enable = false;

	sprd_spi_swdispc_resume(swdispc);
}

static void sprd_swdispc_atomic_disable(struct sprd_crtc *crtc)
{
	struct sprd_swdispc *swdispc = crtc->priv;

	DRM_INFO("%s()\n", __func__);

	sprd_crtc_wait_last_commit_complete(&crtc->base);

	disable_irq(swdispc->ctx.irq);

	sprd_spi_swdispc_disable(swdispc);
}

static void sprd_swdispc_atomic_begin(struct sprd_crtc *crtc)
{
	struct sprd_swdispc *swdispc = crtc->priv;

	DRM_DEBUG("%s()\n", __func__);

	down(&swdispc->ctx.lock);

	crtc->pending_planes = 0;
}

static void sprd_swdispc_atomic_flush(struct sprd_crtc *crtc)

{
	struct sprd_swdispc *swdispc = crtc->priv;

	DRM_DEBUG("%s()\n", __func__);

	if (crtc->pending_planes && !swdispc->ctx.flip_pending) {
		swdispc->core->flip(&swdispc->ctx, crtc->planes, crtc->pending_planes);
	}

	up(&swdispc->ctx.lock);
}

static int sprd_swdispc_enable_vblank(struct sprd_crtc *crtc)
{
	DRM_INFO("%s()\n", __func__);

	return 0;
}

static void sprd_swdispc_disable_vblank(struct sprd_crtc *crtc)
{
	DRM_INFO("%s()\n", __func__);
}

static int sprd_swdispc_atomic_get_property(struct sprd_crtc *crtc,
					const struct drm_crtc_state *crtc_state,
					struct drm_property *property, uint64_t *val)
{
	if (property == crtc->blend_limit_property) {
		*val = 1;
	} else if (property == crtc->vrr_enabled_property) {
		*val = 0;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static const struct sprd_crtc_ops sprd_swdispc_ops = {
	.mode_set_nofb	= sprd_swdispc_mode_set_nofb,
	.mode_valid	= sprd_swdispc_mode_valid,
	.atomic_begin	= sprd_swdispc_atomic_begin,
	.atomic_flush	= sprd_swdispc_atomic_flush,
	.atomic_enable	= sprd_swdispc_atomic_enable,
	.atomic_disable	= sprd_swdispc_atomic_disable,
	.enable_vblank	= sprd_swdispc_enable_vblank,
	.disable_vblank	= sprd_swdispc_disable_vblank,
	.prepare_fb = sprd_swdispc_prepare_fb,
	.cleanup_fb = sprd_swdispc_cleanup_fb,
	.atomic_get_property = sprd_swdispc_atomic_get_property,
};

void sprd_spi_swdispc_run(struct sprd_swdispc *swdispc)
{
	struct swdispc_context *ctx = &swdispc->ctx;

	down(&ctx->lock);
	if (!ctx->enabled) {
		DRM_ERROR("swdispc is not initialized\n");
		up(&ctx->lock);
		return;
	}

	if (!ctx->stopped) {
		up(&ctx->lock);
		return;
	}

	if (swdispc->core->run)
		swdispc->core->run(ctx);

	up(&ctx->lock);

	drm_crtc_vblank_on(&swdispc->crtc->base);
}

void sprd_spi_swdispc_stop(struct sprd_swdispc *swdispc)
{
	struct swdispc_context *ctx = &swdispc->ctx;

	down(&ctx->lock);

	if (!ctx->enabled) {
		DRM_ERROR("swdispc is not initialized\n");
		up(&ctx->lock);
		return;
	}

	if (ctx->stopped) {
		up(&ctx->lock);
		return;
	}

	if (swdispc->core->stop)
		swdispc->core->stop(ctx);

	up(&ctx->lock);

	drm_crtc_handle_vblank(&swdispc->crtc->base);
	drm_crtc_vblank_off(&swdispc->crtc->base);
}

static void sprd_swdispc_enable(struct sprd_swdispc *swdispc)
{
	struct swdispc_context *ctx = &swdispc->ctx;

	down(&ctx->lock);

	if (ctx->enabled) {
		up(&ctx->lock);
		return;
	}

	if (swdispc->core->init)
		swdispc->core->init(ctx);
	if (swdispc->core->ifconfig)
		swdispc->core->ifconfig(ctx);

	ctx->enabled = true;

	up(&ctx->lock);
}

void sprd_spi_swdispc_resume(struct sprd_swdispc *swdispc)
{
	sprd_swdispc_enable(swdispc);
	enable_irq(swdispc->ctx.irq);
	DRM_INFO("swdispc resume OK\n");
}

void sprd_spi_swdispc_disable(struct sprd_swdispc *swdispc)
{
	struct swdispc_context *ctx = &swdispc->ctx;

	down(&ctx->lock);
	if (!ctx->enabled) {
		up(&ctx->lock);
		return;
	}

	if (swdispc->core->fini)
		swdispc->core->fini(ctx);

	ctx->enabled = false;
	up(&ctx->lock);
}

static irqreturn_t sprd_swdispc_isr(int irq, void *data)
{
	struct sprd_swdispc *swdispc = data;
	struct swdispc_context *ctx = &swdispc->ctx;
	u32 int_mask = 0;

	spin_lock(&ctx->irq_lock);
	int_mask = swdispc->core->isr(ctx);

	spin_unlock(&ctx->irq_lock);

	return IRQ_HANDLED;
}

static int sprd_swdispc_irq_request(struct sprd_swdispc *swdispc)
{
	struct swdispc_context *ctx = &swdispc->ctx;
	struct gpio_desc *te_gpiod;
	int irq_num;
	int ret;

	te_gpiod = devm_gpiod_get(swdispc->dev.parent, "te", GPIOD_IN);
	if (IS_ERR(te_gpiod)) {
		DRM_ERROR("failed to get te detection GPIO %p\n", te_gpiod);
		return PTR_ERR(te_gpiod);
	}

	irq_num = gpiod_to_irq(te_gpiod);
	if (irq_num < 0) {
		DRM_ERROR("sprd: swdispc failed to get gpiod_to_irq! %d\n", irq_num);
		return -EINVAL;
	}

	irq_set_status_flags(irq_num, IRQ_NOAUTOEN);
	ret =  devm_request_threaded_irq(&swdispc->dev, irq_num, NULL, sprd_swdispc_isr,
			IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
			"SW-DISPC-TE", swdispc);
	if (ret) {
		DRM_ERROR("sprd: swdispc failed to request irq!\n");
		return -EINVAL;
	}

	ctx->irq = irq_num;
	ctx->swdispc_isr = sprd_swdispc_isr;

	return 0;
}

static struct sprd_drm_spi *sprd_swdispc_spi_attach(struct sprd_swdispc *swdispc)
{
	struct device *dev;
	struct sprd_drm_spi *spi;

	DRM_INFO("swdispc attach spi\n");
	dev = sprd_disp_pipe_get_output(&swdispc->dev);
	if (!dev) {
		DRM_ERROR("swdispc pipe get output failed\n");
		return NULL;
	}

	spi = dev_get_drvdata(dev);
	if (!spi) {
		DRM_ERROR("swdispc attach spi failed\n");
		return NULL;
	}

 	spi->swdispc = swdispc;

	return spi;
}

static int sprd_swdispc_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct sprd_swdispc *swdispc = dev_get_drvdata(dev);
	struct sprd_crtc_capability cap = {};
	struct sprd_plane *planes;
	int ret;

	DRM_INFO("%s()\n", __func__);

	swdispc->core->version(&swdispc->ctx);
	swdispc->core->capability(&swdispc->ctx, &cap);

	planes = sprd_plane_init(drm, &cap, 0xff);
	if (IS_ERR_OR_NULL(planes))
		return PTR_ERR(planes);

	swdispc->crtc = sprd_crtc_init(drm, planes, SPRD_DISPLAY_TYPE_DPI,
				&sprd_swdispc_ops, swdispc->ctx.version, swdispc->ctx.corner_size, "dispc2", swdispc);
	if (IS_ERR(swdispc->crtc))
		return PTR_ERR(swdispc->crtc);

	ret = sprd_swdispc_irq_request(swdispc);
	if (ret) {
		DRM_ERROR("request te interrupt failed\n");
		return ret;
	}

	swdispc->spi = sprd_swdispc_spi_attach(swdispc);

	return 0;
}

static void sprd_swdispc_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct sprd_swdispc *swdispc = dev_get_drvdata(dev);

	DRM_INFO("%s()\n", __func__);

	drm_crtc_cleanup(&swdispc->crtc->base);
}

static const struct component_ops swdispc_component_ops = {
	.bind = sprd_swdispc_bind,
	.unbind = sprd_swdispc_unbind,
};

static int sprd_swdispc_device_create(struct sprd_swdispc *swdispc,
				struct device *parent)
{
	int ret;

	swdispc->dev.class = display_class;
	swdispc->dev.parent = parent;
	swdispc->dev.of_node = parent->of_node;
	dev_set_name(&swdispc->dev, "sw-dispc");
	dev_set_drvdata(&swdispc->dev, swdispc);

	ret = device_register(&swdispc->dev);
	if (ret) {
		DRM_ERROR("swdispc device register failed\n");
		return ret;
	}

	return 0;
}

static int sprd_swdispc_context_init(struct sprd_swdispc *swdispc,
				struct device_node *np)
{
	struct swdispc_context *ctx = &swdispc->ctx;
	int ret;

	if (swdispc->core->context_init) {
		ret = swdispc->core->context_init(ctx, np);
		if (ret)
			return ret;
	}

	if (of_property_read_bool(np, "sprd,initial-stop-state")) {
		DRM_WARN("swdispc is not initialized before entering kernel\n");
		ctx->stopped = true;
	}

	sema_init(&ctx->lock, 1);
	spin_lock_init(&ctx->irq_lock);
	init_waitqueue_head(&ctx->wait_queue);

	ctx->panel_ready = true;
	ctx->odd_vsync = true;
	ctx->first_alloc = true;
	ctx->frame_1st = FRAME_1ST_NONE;

	init_waitqueue_head(&swdispc->ctx.te_wq);

	return 0;
}

static const struct sprd_swdispc_ops sw_dispc = {
	.core = &sw_dispc_core_ops,
};

static const struct of_device_id swdispc_match_table[] = {
	{ .compatible = "sprd,sw-dpu",
	  .data = &sw_dispc },
	{ /* sentinel */ },
};

static int sprd_swdispc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct sprd_swdispc_ops *pdata;
	struct sprd_swdispc *swdispc;
	int ret;

	swdispc = devm_kzalloc(&pdev->dev, sizeof(*swdispc), GFP_KERNEL);
	if (!swdispc)
		return -ENOMEM;

	pdata = of_device_get_match_data(&pdev->dev);
	if (pdata) {
		swdispc->core = pdata->core;
	} else {
		DRM_ERROR("No matching driver data found\n");
		return -EINVAL;
	}

	ret = sprd_swdispc_context_init(swdispc, np);
	if (ret)
		return ret;

	ret = sprd_swdispc_device_create(swdispc, &pdev->dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, swdispc);

	return component_add(&pdev->dev, &swdispc_component_ops);
}

static int sprd_swdispc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &swdispc_component_ops);
	return 0;
}

struct platform_driver sprd_spi_swdispc_driver = {
	.probe = sprd_swdispc_probe,
	.remove = sprd_swdispc_remove,
	.driver = {
		.name = "sprd-spi-swdispc-drv",
		.of_match_table = swdispc_match_table,
	},
};

MODULE_AUTHOR("Pony Wu <pony.wu@unisoc.com>");
MODULE_DESCRIPTION("Unisoc Display Controller Driver");
MODULE_LICENSE("GPL v2");
