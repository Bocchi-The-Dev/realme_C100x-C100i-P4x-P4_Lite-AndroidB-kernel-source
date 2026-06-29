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
#include <linux/memblock.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "sprd_crtc.h"
#include "sprd_dpu.h"
#include "sprd_drm.h"
#include "sprd_gem.h"
#include "sprd_iommu.h"
#include "sprd_plane.h"
#include "sprd_perf_connector.h"
#include "disp_lib.h"
#include "sysfs/sysfs_display.h"

int dpu_wait_te_flush(struct dpu_context *ctx)
{
	int rc;

	spin_lock_irq(&ctx->irq_lock);
	ctx->evt_te = false;
	spin_unlock_irq(&ctx->irq_lock);

	/* wait for reg update done interrupt */
	rc = wait_event_interruptible_timeout(ctx->te_wq, ctx->evt_te,
		msecs_to_jiffies(20));

	if (!rc) {
		/* time out */
		pr_err("dpu wait for te time out!\n");
		return -1;
	}

	return 0;
}

static int sprd_perf_dpu_prepare_fb(struct sprd_crtc *crtc,
				struct drm_plane_state *new_state)
{
	struct drm_gem_object *obj;
	struct sprd_gem_obj *sprd_gem;
	struct sprd_dpu *dpu = crtc->priv;
	int i, ret = 0;

	if (!dpu->ctx.enabled) {
		DRM_WARN("dpu has already powered off\n");
		return 0;
	}

	for (i = 0; i < new_state->fb->format->num_planes; i++) {
		obj = drm_gem_fb_get_obj(new_state->fb, i);
		sprd_gem = to_sprd_gem_obj(obj);
		if (sprd_gem->need_iommu) {
			ret = sprd_crtc_iommu_map(&dpu->dev, sprd_gem);
			if (ret)
				break;
		}
	}

	return ret;
}

static unsigned long sprd_free_reserved_area(void *start, void *end, int poison, const char *s)
{
	void *pos;
	unsigned long pages = 0;

	start = (void *)PAGE_ALIGN((unsigned long)start);
	end = (void *)((unsigned long)end & PAGE_MASK);
	for (pos = start; pos < end; pos += PAGE_SIZE, pages++) {
		struct page *page = virt_to_page(pos);
		void *direct_map_addr;

		/*
		 * 'direct_map_addr' might be different from 'pos'
		 * because some architectures' virt_to_page()
		 * work with aliases.  Getting the direct map
		 * address ensures that we get a _writeable_
		 * alias for the memset().
		 */
		direct_map_addr = page_address(page);
		if ((unsigned int)poison <= 0xFF)
			memset(direct_map_addr, poison, PAGE_SIZE);

		free_reserved_page(page);
	}

	if (pages && s)
		pr_info("Freeing %s memory: %ldK\n",
			s, pages << (PAGE_SHIFT - 10));

	return pages;
}

static void sprd_perf_dpu_cleanup_fb(struct sprd_crtc *crtc,
				struct drm_plane_state *old_state)
{
	struct device_node *np = NULL;
	struct drm_gem_object *obj;
	struct sprd_gem_obj *sprd_gem;
	struct sprd_dpu *dpu = crtc->priv;
	static atomic_t logo2animation = { -1 };
	struct resource r;
	bool keep_buf_reserved = false;
	int i;

	if (!dpu->ctx.enabled) {
		DRM_WARN("dpu has already powered off\n");
		return;
	}

	for (i = 0; i < old_state->fb->format->num_planes; i++) {
		obj = drm_gem_fb_get_obj(old_state->fb, i);
		sprd_gem = to_sprd_gem_obj(obj);
		if (sprd_gem->need_iommu)
			sprd_crtc_iommu_unmap(&dpu->dev, sprd_gem);
	}

	/* FIXME:
	 * while sysdump, some thread information will be written to an area
	 * of memory that may overlap with the logo_reserved, resulting in logo data
	 * overwriting the thread information when restart.
	 * Workaround:
	 * keep_buf_reserved = true: do not free logo_reserved
	 * keep_buf_reserved = false: free logo_reserved
	 */
	if (unlikely(atomic_inc_not_zero(&logo2animation)) &&
		dpu->ctx.logo_addr) {
		np = of_find_node_by_name(NULL, "sysdump-uboot");
		if (np) {
			if ((!of_address_to_resource(np, 0, &r)) && (resource_size(&r) != 0)) {
				DRM_INFO("sysdump-uboot reserved is not 0, do not free logo_reserved\n");
				keep_buf_reserved = true;
			} else {
				DRM_INFO("sysdump-uboot reserved is 0, free logo_reserved\n");
			}
		} else {
			DRM_INFO("cannot find node by name sysdump-uboot, free logo_reserved\n");
		}

		if (keep_buf_reserved)
			return;

		DRM_INFO("free logo memory addr:0x%lx size:0x%lx\n",
			dpu->ctx.logo_addr, dpu->ctx.logo_size);
		sprd_free_reserved_area(phys_to_virt(dpu->ctx.logo_addr),
			phys_to_virt(dpu->ctx.logo_addr + dpu->ctx.logo_size),
			-1, "logo");
	}
}

void sprd_drm_mode_copy(struct drm_display_mode *dst, const struct drm_display_mode *src)
{
	struct list_head head = dst->head;

	*dst = *src;
	dst->head = head;
}

/*
static void sprd_perf_dpu_mode_set_nofb(struct sprd_crtc *crtc)
{
	struct sprd_dpu *dpu = crtc->priv;
	struct drm_display_mode *mode = &crtc->base.state->adjusted_mode;

	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));

	dpu->ctx.if_type = SPRD_DPU_IF_DPI;


	sprd_drm_mode_copy(&dpu->mode, mode);
	sprd_drm_mode_copy(&dpu->actual_mode, mode);

	drm_display_mode_to_videomode(&dpu->actual_mode, &dpu->ctx.vm);

	if (dpu->core->modeset && crtc->base.state->mode_changed)
		dpu->core->modeset(&dpu->ctx, mode);
}
*/
static void sprd_perf_dpu_mode_set_nofb(struct sprd_crtc *crtc)
{
	struct sprd_dpu *dpu = crtc->priv;
	struct drm_display_mode *mode = &crtc->base.state->adjusted_mode;
	struct sprd_perf_connector *perf_conn = dpu->perf_conn;
	struct sprd_dummy_panel_info *info = &perf_conn->panel_info;
	int i;

	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));

	if ((info->work_mode == DSI_MODE_VIDEO) || dpu->ctx.cmd_dpi_mode)
		dpu->ctx.if_type = SPRD_DPU_IF_DPI;
	else
		dpu->ctx.if_type = SPRD_DPU_IF_EDPI;

	sprd_drm_mode_copy(&dpu->mode, mode);
	sprd_drm_mode_copy(&dpu->actual_mode, mode);

	if (dpu->ctx.cmd_dpi_mode) {
		for (i = 0; i < info->display_mode_count; i++) {
			if ((info->buildin_modes[i].hdisplay == info->mode.hdisplay) &&
			    (info->buildin_modes[i].vdisplay == info->mode.vdisplay) &&
			    (drm_mode_vrefresh(&info->buildin_modes[i]) == DPI_VREFRESH_120)) {
				sprd_drm_mode_copy(&dpu->actual_mode, &(info->buildin_modes[i]));
				drm_display_mode_to_videomode(&dpu->actual_mode, &dpu->ctx.vm);
				break;
			}
		}
	} else {
		if (mode->type & DRM_MODE_TYPE_USERDEF) {
			drm_display_mode_to_videomode(&info->mode, &dpu->ctx.vm);
		} else {
			if ((mode->hdisplay != info->mode.hdisplay) || (mode->vdisplay != info->mode.vdisplay)) {
				for (i = 0; i < info->display_mode_count; i++) {
					if ((info->buildin_modes[i].hdisplay == info->mode.hdisplay) &&
					    (info->buildin_modes[i].vdisplay == info->mode.vdisplay) &&
					    (drm_mode_vrefresh(&info->buildin_modes[i]) == drm_mode_vrefresh(mode))) {
						sprd_drm_mode_copy(&dpu->actual_mode, &(info->buildin_modes[i]));
						break;
					}
				}
			}
			drm_display_mode_to_videomode(&dpu->actual_mode, &dpu->ctx.vm);
		}
	}

	if (dpu->core->modeset && crtc->base.state->mode_changed)
		dpu->core->modeset(&dpu->ctx, mode);
}

static enum drm_mode_status sprd_perf_dpu_mode_valid(struct sprd_crtc *crtc,
					const struct drm_display_mode *mode)
{
	DRM_INFO("%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));

	return MODE_OK;
}

static void sprd_perf_dpu_atomic_enable(struct sprd_crtc *crtc)
{
	struct sprd_dpu *dpu = crtc->priv;
	static bool is_enable = true;

	DRM_INFO("%s()\n", __func__);
	if (is_enable) {
		is_enable = false;
	}
	else
		pm_runtime_get_sync(dpu->dev.parent);

	sprd_perf_dpu_resume(dpu);
}

static void sprd_perf_dpu_atomic_disable(struct sprd_crtc *crtc)
{
	struct sprd_dpu *dpu = crtc->priv;

	DRM_INFO("%s()\n", __func__);

	sprd_crtc_wait_last_commit_complete(&crtc->base);

	disable_irq(dpu->ctx.irq);

	sprd_perf_dpu_disable(dpu);

	pm_runtime_put(dpu->dev.parent);
}

void sprd_perf_dpu_atomic_disable_force(struct drm_crtc *crtc)
{
	struct sprd_crtc *sprd_crtc = container_of(crtc, struct sprd_crtc, base);
	struct sprd_dpu *dpu = sprd_crtc->priv;
	struct sprd_dummy_panel_info *info = &dpu->perf_conn->panel_info;

	DRM_INFO("%s()\n", __func__);

	/* dpu is not initialized,it should enable first! */
	if (!dpu->ctx.enabled) {
		drm_display_mode_to_videomode(&info->mode, &dpu->ctx.vm);
		sprd_perf_dpu_enable(dpu);
		enable_irq(dpu->ctx.irq);
	}

	disable_irq(dpu->ctx.irq);
	sprd_perf_dpu_disable(dpu);

	pm_runtime_put(dpu->dev.parent);
}

static void sprd_perf_dpu_atomic_begin(struct sprd_crtc *crtc)
{
	struct sprd_dpu *dpu = crtc->priv;

	down(&dpu->ctx.lock);

	crtc->pending_planes = 0;
}

static void sprd_perf_dpu_atomic_flush(struct sprd_crtc *crtc)
{
	struct sprd_dpu *dpu = crtc->priv;
	struct time_fifo *tf = &dpu->ctx.tf;

	DRM_DEBUG("%s()\n", __func__);

	if (crtc->pending_planes && !dpu->ctx.flip_pending) {
		dpu->core->flip(&dpu->ctx, crtc->planes, crtc->pending_planes);
		dpu->ctx.frame_count++;
		ktime_get_real_ts64(&tf->ts[tf->head++]);
		if (tf->head == sizeof(tf->ts) / sizeof(struct timespec64))
			tf->head = 0;
	}
	up(&dpu->ctx.lock);
}

static int sprd_perf_dpu_enable_vblank(struct sprd_crtc *crtc)
{
	struct sprd_dpu *dpu = crtc->priv;
	struct dpu_context *ctx = &dpu->ctx;

	DRM_INFO("%s()\n", __func__);

	if (dpu->core->enable_vsync && ctx->enabled)
		dpu->core->enable_vsync(ctx);

	return 0;
}

static void sprd_perf_dpu_disable_vblank(struct sprd_crtc *crtc)
{
	struct sprd_dpu *dpu = crtc->priv;
	struct dpu_context *ctx = &dpu->ctx;

	DRM_INFO("%s()\n", __func__);

	if (dpu->core->disable_vsync && ctx->enabled)
		dpu->core->disable_vsync(&dpu->ctx);
}

static int sprd_perf_dpu_atomic_get_property(struct sprd_crtc *crtc,
					const struct drm_crtc_state *crtc_state,
					struct drm_property *property, uint64_t *val)
{
	struct sprd_dpu *dpu = crtc->priv;

	 if (property == crtc->vrr_enabled_property) {
		*val = dpu->ctx.vrr_enabled;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static const struct sprd_crtc_ops sprd_perf_dpu_ops = {
	.mode_set_nofb	= sprd_perf_dpu_mode_set_nofb,
	.mode_valid	= sprd_perf_dpu_mode_valid,
	.atomic_begin	= sprd_perf_dpu_atomic_begin,
	.atomic_flush	= sprd_perf_dpu_atomic_flush,
	.atomic_enable	= sprd_perf_dpu_atomic_enable,
	.atomic_disable	= sprd_perf_dpu_atomic_disable,
	.enable_vblank	= sprd_perf_dpu_enable_vblank,
	.disable_vblank	= sprd_perf_dpu_disable_vblank,
	.prepare_fb = sprd_perf_dpu_prepare_fb,
	.cleanup_fb = sprd_perf_dpu_cleanup_fb,
	.atomic_get_property = sprd_perf_dpu_atomic_get_property,
};

void sprd_dpu_run(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;

	down(&ctx->lock);
	if (ctx->wb_configed) {
		ctx->need_wb_work = true;
	}

	if (!ctx->enabled) {
		DRM_ERROR("dpu is not initialized\n");
		up(&ctx->lock);
		return;
	}

	if (!ctx->stopped) {
		up(&ctx->lock);
		return;
	}

	if (dpu->core->run)
		dpu->core->run(ctx);

	up(&ctx->lock);

	drm_crtc_vblank_on(&dpu->crtc->base);
}

void sprd_dpu_stop(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;

	down(&ctx->lock);

	if (!ctx->enabled) {
		DRM_ERROR("dpu is not initialized\n");
		up(&ctx->lock);
		return;
	}

	if (ctx->stopped) {
		up(&ctx->lock);
		return;
	}

	if (dpu->core->stop)
		dpu->core->stop(ctx);

	up(&ctx->lock);

	drm_crtc_handle_vblank(&dpu->crtc->base);
	drm_crtc_vblank_off(&dpu->crtc->base);
}

void sprd_perf_dpu_enable(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;
	static bool first_init = true;

	down(&ctx->lock);

	if (ctx->enabled) {
		up(&ctx->lock);
		return;
	}

	if (first_init) {
		if (dpu->core->stop)
			dpu->core->stop(ctx);
		if (dpu->core->fini)
			dpu->core->fini(ctx);
		if (dpu->clk->disable)
			dpu->clk->disable(ctx);
		if (dpu->glb->suspend_reset)
			dpu->glb->suspend_reset(ctx);
		if (dpu->glb->disable)
			dpu->glb->disable(ctx);
		if (dpu->glb->power)
			dpu->glb->power(ctx, false);

		ctx->enabled = false;
		first_init = false;
		mdelay(20);
	}

	if (dpu->glb->power)
		dpu->glb->power(ctx, true);
	if (dpu->glb->enable)
		dpu->glb->enable(ctx);

	if (ctx->stopped && dpu->glb->reset)
		dpu->glb->reset(ctx);

	if (dpu->clk->init)
		dpu->clk->init(ctx);
	if (dpu->clk->enable)
		dpu->clk->enable(ctx);

	if (dpu->core->init)
		dpu->core->init(ctx);
	if (dpu->core->ifconfig)
		dpu->core->ifconfig(ctx);
	if (dpu->core->run)
		dpu->core->run(ctx);

	ctx->enabled = true;

	up(&ctx->lock);
}

void sprd_perf_dpu_resume(struct sprd_dpu *dpu)
{
	sprd_perf_dpu_enable(dpu);
	enable_irq(dpu->ctx.irq);
	sprd_iommu_restore(&dpu->dev);
	DRM_INFO("dpu resume OK\n");
}

void sprd_perf_dpu_disable(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;

	down(&ctx->lock);
	down(&ctx->cabc_lock);
	if (!ctx->enabled) {
		up(&ctx->lock);
		up(&ctx->cabc_lock);
		return;
	}

	if (dpu->core->fini)
		dpu->core->fini(ctx);
	if (dpu->clk->disable)
		dpu->clk->disable(ctx);
	if (dpu->glb->suspend_reset)
		dpu->glb->suspend_reset(ctx);
	if (dpu->glb->disable)
		dpu->glb->disable(ctx);
	if (dpu->glb->power)
		dpu->glb->power(ctx, false);

	ctx->enabled = false;
	up(&ctx->cabc_lock);
	up(&ctx->lock);
}

static irqreturn_t sprd_perf_dpu_isr(int irq, void *data)
{
	struct sprd_dpu *dpu = data;
	struct dpu_context *ctx = &dpu->ctx;
	u32 int_mask = 0;

	int_mask = dpu->core->isr(ctx);

	if (int_mask & BIT_DPU_INT_ERR)
		DRM_WARN("Warning: dpu underflow!\n");

	return IRQ_HANDLED;
}

void sprd_dpu_dsc_reset(struct sprd_dpu *dpu)
{
}

static int sprd_perf_dpu_irq_request(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;
	int irq_num;
	struct cpumask mask;
	int ret;

	irq_num = irq_of_parse_and_map(dpu->dev.of_node, 0);
	if (!irq_num) {
		DRM_ERROR("error: dpu parse irq num failed\n");
		return -EINVAL;
	}

	DRM_INFO("dpu irq_num = %d\n", irq_num);

	irq_set_status_flags(irq_num, IRQ_NOAUTOEN);
	ret = devm_request_irq(&dpu->dev, irq_num, sprd_perf_dpu_isr, 0, "DISPC", dpu);
	if (ret) {
		DRM_ERROR("error: dpu request irq failed\n");
		return -EINVAL;
	}

	if (ctx->cmd_dpi_mode) {
		cpumask_set_cpu(5, &mask);
		cpumask_set_cpu(6, &mask);
		cpumask_set_cpu(7, &mask);
		ret = irq_set_affinity(irq_num, &mask);
		if (ret)
			DRM_ERROR("irq_set_affinity() on CPU %d failed\n", irq_num);
	}

	ctx->irq = irq_num;
	ctx->dpu_isr = sprd_perf_dpu_isr;

	return 0;
}

static struct sprd_perf_connector *sprd_perf_dpu_conn_attach(struct sprd_dpu *dpu)
{
	struct device *dev;
	struct sprd_perf_connector *perf_conn;

	DRM_INFO("dpu attach perf conn\n");
	dev = sprd_disp_pipe_get_output(&dpu->dev);
	if (!dev) {
		DRM_ERROR("dpu pipe get output failed\n");
		return NULL;
	}

	perf_conn = dev_get_drvdata(dev);
	if (!perf_conn) {
		DRM_ERROR("dpu attach connector failed\n");
		return NULL;
	}

	perf_conn->dpu = dpu;

	return perf_conn;
}

static int sprd_perf_dpu_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct sprd_dpu *dpu = dev_get_drvdata(dev);
	struct sprd_crtc_capability cap = {};
	struct sprd_plane *planes;

	DRM_INFO("%s()\n", __func__);

	dpu->core->version(&dpu->ctx);
	dpu->core->capability(&dpu->ctx, &cap);
	dpu->ctx.max_cap_layers = cap.max_layers;

	planes = sprd_plane_init(drm, &cap, 1);
	if (IS_ERR_OR_NULL(planes))
		return PTR_ERR(planes);

	dpu->crtc = sprd_crtc_init(drm, planes, SPRD_DISPLAY_TYPE_LCD,
				&sprd_perf_dpu_ops, dpu->ctx.version, dpu->ctx.corner_size, "dispc0", dpu);
	if (IS_ERR(dpu->crtc))
		return PTR_ERR(dpu->crtc);

	sprd_perf_dpu_irq_request(dpu);

	dpu->perf_conn = sprd_perf_dpu_conn_attach(dpu);

	return 0;
}

static void sprd_perf_dpu_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct sprd_dpu *dpu = dev_get_drvdata(dev);

	DRM_INFO("%s()\n", __func__);

	drm_crtc_cleanup(&dpu->crtc->base);
}

static const struct component_ops dpu_component_ops = {
	.bind = sprd_perf_dpu_bind,
	.unbind = sprd_perf_dpu_unbind,
};

static int sprd_perf_dpu_device_create(struct sprd_dpu *dpu,
				struct device *parent)
{
	int ret;

	dpu->dev.class = display_class;
	dpu->dev.parent = parent;
	dpu->dev.of_node = parent->of_node;
	dev_set_name(&dpu->dev, "dispc0");
	dev_set_drvdata(&dpu->dev, dpu);

	ret = device_register(&dpu->dev);
	if (ret) {
		DRM_ERROR("dpu device register failed\n");
		return ret;
	}

	return 0;
}

static int of_get_logo_memory_info(struct sprd_dpu *dpu,
	struct device_node *np)
{
	struct device_node *node;
	struct resource r;
	int ret;
	struct dpu_context *ctx = &dpu->ctx;

	node = of_parse_phandle(np, "sprd,logo-memory", 0);
	if (!node) {
		DRM_INFO("no sprd,logo-memory specified\n");
		return 0;
	}

	ret = of_address_to_resource(node, 0, &r);
	of_node_put(node);
	if (ret) {
		DRM_ERROR("invalid logo reserved memory node!\n");
		return -EINVAL;
	}

	ctx->logo_addr = r.start;
	ctx->logo_size = resource_size(&r);

	return 0;
}

static int sprd_perf_dpu_context_init(struct sprd_dpu *dpu,
				struct device *dev)
{
	struct resource r;
	struct dpu_context *ctx = &dpu->ctx;
	struct device_node *np = dev->of_node;
	int ret;

	if (dpu->core->context_init) {
		ret = dpu->core->context_init(ctx, dev);
		if (ret)
			return ret;
	}

	if (dpu->clk->parse_dt)
		dpu->clk->parse_dt(ctx, np);
	if (dpu->glb->parse_dt)
		dpu->glb->parse_dt(ctx, np);

	if (of_address_to_resource(np, 0, &r)) {
		DRM_ERROR("parse dt base address failed\n");
		return -ENODEV;
	}
	ctx->base = ioremap(r.start, resource_size(&r));
	if (!ctx->base) {
		DRM_ERROR("ioremap base address failed\n");
		return -EFAULT;
	}

	if (of_property_read_bool(np, "sprd,initial-stop-state")) {
		DRM_WARN("DPU is not initialized before entering kernel\n");
		ctx->stopped = true;
	}

	/*
	 * FIXME:
	 * When gsp is busy, dpu executes dpu_reset.
	 * Check gsp_busy status and add lock.
	 * Just handle HDCP scence, power key lock/unlock.
	 * When gsp is busy, dpu doesn't execute dpu reset request until gsp finishes operation.
	 */
	//if (dpu->core->get_gsp_base)
	//	dpu->core->get_gsp_base(ctx, np);

	of_get_logo_memory_info(dpu, np);

	sema_init(&ctx->lock, 1);
	sema_init(&ctx->cabc_lock, 1);
	init_waitqueue_head(&ctx->wait_queue);
	mutex_init(&ctx->vrr_lock);
	spin_lock_init(&ctx->irq_lock);

	ctx->panel_ready = true;
	ctx->time = 5000;
	ctx->secure_debug = false;

	init_waitqueue_head(&dpu->ctx.te_wq);
	init_waitqueue_head(&dpu->ctx.te_update_wq);

	return 0;
}

struct device_node *sprd_get_perf_conn_node_by_name(void)
{
	struct device_node *conn_node;

	conn_node = of_find_node_by_name(NULL, "perf_connector");
	if (!conn_node) {
		DRM_ERROR("failed to get perf conn node\n");
		return NULL;
	}

	DRM_INFO("find perf connector node successfully\n");

	return conn_node;
}

static int sprd_parse_vrr_config(struct sprd_dpu *dpu)
{
	struct device_node *perf_conn_node;
	u32 val;
	int rc;

	perf_conn_node = sprd_get_perf_conn_node_by_name();
	if (!perf_conn_node) {
		DRM_INFO("can not find perf conn node, skip other action\n");
		return 0;
	}

	if (of_property_read_bool(perf_conn_node, "sprd,vrr-enabled")) {
		DRM_INFO("vrr supported\n");
		dpu->ctx.vrr_enabled = true;
	} else {
		dpu->ctx.vrr_enabled = false;
	}

	if (of_property_read_bool(perf_conn_node, "sprd,hw-vrr-en")) {
		DRM_INFO("use dpu hw vrr\n");
		dpu->ctx.hw_vrr_en = true;
	} else {
		DRM_INFO("use dpu sw vrr\n");
		dpu->ctx.hw_vrr_en = false;
	}

	rc = of_property_read_u32(perf_conn_node, "sprd,dsi-work-mode", &val);
	if (!rc) {
		if (val == SPRD_DSI_MODE_CMD_DPI) {
			DRM_INFO("cmd panel vrr supported\n");
			dpu->ctx.cmd_dpi_mode = true;
		}
	}

	rc = of_property_read_u32(perf_conn_node, "sprd,dpi-actual-pclk", &val);
	if (!rc) {
		DRM_INFO("set dpi actual clock to %u\n", val);
		dpu->ctx.actual_dpi_clk = val;
	} else {
		DRM_INFO("no dpi actual clock config found\n");
		dpu->ctx.actual_dpi_clk = 0;
	}

	return 0;
}

static const struct sprd_dpu_ops qogirn6pro_dpu = {
	.core = &dpu_r6p0_core_ops,
	.clk = &qogirn6pro_dpu_clk_ops,
	.glb = &qogirn6pro_dpu_glb_ops,
};

static const struct of_device_id dpu_match_table[] = {
	{ .compatible = "sprd,perf_dpu",
	  .data = &qogirn6pro_dpu },
	{ /* sentinel */ },
};

static int sprd_perf_dpu_probe(struct platform_device *pdev)
{
	//struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	const struct sprd_dpu_ops *pdata;
	struct sprd_dpu *dpu;
	int ret;

	dpu = devm_kzalloc(&pdev->dev, sizeof(*dpu), GFP_KERNEL);
	if (!dpu)
		return -ENOMEM;

	/*
	 * FIXME:
	 * When gsp is busy, dpu executes dpu_reset.
	 * Check gsp_busy status and add lock.
	 * Just handle HDCP scence, power key lock/unlock.
	 * When gsp is busy, dpu doesn't execute dpu reset request until gsp finishes operation.
	 */
	mutex_init(&dpu->dpu_gsp_lock);

	pdata = of_device_get_match_data(&pdev->dev);
	if (pdata) {
		dpu->core = pdata->core;
		dpu->clk = pdata->clk;
		dpu->glb = pdata->glb;
	} else {
		DRM_ERROR("No matching driver data found\n");
		return -EINVAL;
	}

	ret = sprd_perf_dpu_context_init(dpu, dev);
	if (ret) {
		DRM_ERROR("dpu context init failed\n");
		return ret;
	}

	ret = sprd_perf_dpu_device_create(dpu, dev);
	if (ret) {
		DRM_ERROR("dpu device create failed\n");
		return ret;
	}

	ret = sprd_dpu_sysfs_init(&dpu->dev);
	if (ret) {
		DRM_ERROR("dpu sysfs init failed\n");
	 	return ret;
	}

	ret = sprd_parse_vrr_config(dpu);
	if (ret)
	 	return ret;

	platform_set_drvdata(pdev, dpu);

	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);

	return component_add(dev, &dpu_component_ops);
}

static int sprd_perf_dpu_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dpu_component_ops);
	return 0;
}

struct platform_driver sprd_perf_dpu_driver = {
	.probe = sprd_perf_dpu_probe,
	.remove = sprd_perf_dpu_remove,
	.driver = {
		.name = "sprd-perf-dpu-drv",
		.of_match_table = dpu_match_table,
	},
};

MODULE_AUTHOR("Pony Wu <pony.wu@unisoc.com>");
MODULE_DESCRIPTION("Unisoc Perf DPU Driver");
MODULE_LICENSE("GPL v2");
