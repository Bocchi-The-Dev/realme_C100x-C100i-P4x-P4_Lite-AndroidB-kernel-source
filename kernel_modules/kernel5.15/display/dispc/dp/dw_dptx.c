/*
*SPDX-FileCopyrightText: 2020 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier: GPL-2.0-only
*/

#include <linux/platform_device.h>
#include <linux/regmap.h>
#include "sprd_dp.h"
#include "dw_dptx.h"
#include <linux/extcon-provider.h>
#include <dt-bindings/soc/sprd,qogirn6pro-mask.h>
#include <dt-bindings/soc/sprd,qogirn6pro-regs.h>
#include <drm/drm_probe_helper.h>
#include <uapi/linux/sched/types.h>

static struct dptx *handle;

static const unsigned int dptx_extcon_cable[] = {
	EXTCON_DISP_HDMI,
	EXTCON_NONE,
};

struct dptx *dptx_get_handle(void)
{
	return handle;
}

static int hotplug_handle_sink_request(struct dptx *dptx)
{
	int retval;
	int ret_dpcd;
	u8 vector = 0;

	retval = dptx_link_check_status(dptx);
	if (retval)
		return retval;

	ret_dpcd = drm_dp_dpcd_readb(&dptx->aux_dev,
		DP_DEVICE_SERVICE_IRQ_VECTOR, &vector);
	if (ret_dpcd < 0)
		return ret_dpcd;

	DRM_INFO("%s: IRQ_VECTOR: 0x%02x\n", __func__, vector);

	/* TODO handle sink interrupts */
	if (!vector)
		return 0;

	if (drm_dp_dpcd_writeb(&dptx->aux_dev, DP_DEVICE_SERVICE_IRQ_VECTOR,
			      vector) < 0) {
		DRM_DEBUG("DPCD failed write DP_DEVICE_SERVICE_IRQ_VECTOR register\n");
	}

	if (vector & DP_AUTOMATED_TEST_REQUEST) {
		DRM_INFO("%s: DP_AUTOMATED_TEST_REQUEST\n", __func__);
		retval = handle_automated_test_request(dptx);
		if (retval) {
			if (retval == -ENOTSUPP) {
				DRM_ERROR("Automated test request failed\n");
				ret_dpcd =
				    drm_dp_dpcd_writeb(&dptx->aux_dev,
						       DP_TEST_RESPONSE,
						       DP_TEST_NAK);
				if (ret_dpcd < 0)
					return ret_dpcd;
			}
		}
	}

	return retval;
}

static int handle_sink_request(struct dptx *dptx)
{
	int retval;
	int ret_dpcd;
	u8 vector = 0;
	u8 bytes[14];
	u32 reg;
	struct drm_connector *connector = &dptx->connector;
	struct edid *edid;

	retval = dptx_link_check_status(dptx);
	if (retval)
		return retval;

	ret_dpcd = drm_dp_dpcd_read(&dptx->aux_dev,
				DP_SINK_COUNT_ESI, bytes, 14);
	if (ret_dpcd < 0)
		return ret_dpcd;

	if (dptx->read_edid_flag && dptx->link.trained) {
		dptx_disable_default_video_stream(dptx, DEFAULT_STREAM);
		edid = drm_get_edid(connector, &dptx->aux_dev.ddc);
		dptx_video_core_config(dptx, DEFAULT_STREAM);
		dptx_enable_default_video_stream(dptx, DEFAULT_STREAM);
	}

	ret_dpcd = drm_dp_dpcd_readb(&dptx->aux_dev,
		DP_DEVICE_SERVICE_IRQ_VECTOR, &vector);
	if (ret_dpcd < 0)
		return ret_dpcd;

	DRM_INFO("%s: IRQ_VECTOR: 0x%02x\n", __func__, vector);

	/* TODO handle sink interrupts */
	if (!vector)
		return 0;

	if (drm_dp_dpcd_writeb(&dptx->aux_dev, DP_DEVICE_SERVICE_IRQ_VECTOR,
			      vector) < 0) {
		DRM_DEBUG("DPCD failed write DP_DEVICE_SERVICE_IRQ_VECTOR register\n");
	}

	if (vector & DP_REMOTE_CONTROL_COMMAND_PENDING) {
		/* TODO */
		DRM_WARN("%s: DP_REMOTE_CONTROL_COMMAND_PENDING: Not yet implemented",
			  __func__);
	}

	if (vector & DP_AUTOMATED_TEST_REQUEST) {
		DRM_INFO("%s: DP_AUTOMATED_TEST_REQUEST\n", __func__);
		retval = handle_automated_test_request(dptx);
		if (retval) {
			if (retval == -ENOTSUPP) {
				DRM_ERROR("Automated test request failed\n");
				ret_dpcd =
				    drm_dp_dpcd_writeb(&dptx->aux_dev,
						       DP_TEST_RESPONSE,
						       DP_TEST_NAK);
				if (ret_dpcd < 0)
					return ret_dpcd;
			}
		}
	}

	if (vector & DP_CP_IRQ) {
		DRM_WARN("%s: DP_CP_IRQ", __func__);
		ret_dpcd =
		    drm_dp_dpcd_writeb(&dptx->aux_dev,
				       DP_DEVICE_SERVICE_IRQ_VECTOR, DP_CP_IRQ);
		reg = dptx_readl(dptx, DPTX_HDCP_CONFIG);
		reg |= DPTX_CFG_CP_IRQ;
		dptx_writel(dptx, DPTX_HDCP_CONFIG, reg);
		reg = dptx_readl(dptx, DPTX_HDCP_CONFIG);
		reg &= ~DPTX_CFG_CP_IRQ;
		dptx_writel(dptx, DPTX_HDCP_CONFIG, reg);
		if (ret_dpcd < 0)
			return ret_dpcd;
	}

	if (vector & DP_MCCS_IRQ) {
		/* TODO */
		DRM_WARN("%s: DP_MCCS_IRQ: Not yet implemented",
					__func__);
		retval = -ENOTSUPP;
	}

	if (vector & DP_DOWN_REP_MSG_RDY) {
		/* TODO */
		DRM_WARN("%s: DP_DOWN_REP_MSG_RDY: Not yet implemented",
			  __func__);
		retval = -ENOTSUPP;
	}

	if (vector & DP_UP_REQ_MSG_RDY) {
		/* TODO */
		DRM_WARN("%s: DP_UP_REQ_MSG_RDY: Not yet implemented",
			  __func__);
		retval = -ENOTSUPP;
	}

	if (vector & DP_SINK_SPECIFIC_IRQ) {
		/* TODO */
		DRM_WARN("%s: DP_SINK_SPECIFIC_IRQ: Not yet implemented",
			  __func__);
		retval = -ENOTSUPP;
	}

	return retval;
}

int dptx_check_power(struct dptx *dptx)
{
	u32 reg = 0;
	u32 val;

	regmap_read(dptx->force_shutdown, REG_PMU_APB_PWR_STATUS_DBG_22, &reg);
	val = reg & MASK_PMU_APB_PD_IPA_STATE;
	if (val) {
		DRM_ERROR("%s:ipa sys power off, ipa_sys = %d\n", __func__, val);
		return -1;
	}

	regmap_read(dptx->ipa_dispc1_glb_apb, REG_DISPC1_GLB_APB_APB_EB, &reg);
	val = reg & MASK_DISPC1_GLB_APB_DPU1_EB;
	if (!val) {
		DRM_ERROR("%s:dpu1 eb disable, dpu1_eb = %d\n", __func__, val);
		return -1;
	}

	val = reg & MASK_DISPC1_GLB_APB_DPTX_EB;
	if (!val) {
		DRM_ERROR("%s:dptx eb disable, dptx_eb = %d\n", __func__, val);
		return -1;
	}

	return 0;
}

static int handle_hotunplug(struct dptx *dptx)
{
	u32 phyifctrl;
	u8 retval;

	/* Clear xmit enables */
	phyifctrl = dptx_readl(dptx, DPTX_PHYIF_CTRL);
	phyifctrl &= ~DPTX_PHYIF_CTRL_XMIT_EN_MASK;

	/* Move PHY to P3 state */
	phyifctrl |= (3 << DPTX_PHYIF_CTRL_LANE_PWRDOWN_SHIFT);
	dptx_writel(dptx, DPTX_PHYIF_CTRL, phyifctrl);

	retval = dptx_phy_wait_busy(dptx, dptx->link.lanes);
	if (retval) {
		DRM_ERROR("Timed out waiting for PHY BUSY\n");
		return retval;
	}

	/* Power down all lanes */

	dptx_writel(dptx, DPTX_PHYIF_CTRL, phyifctrl);
	atomic_set(&dptx->sink_request, 0);
	dptx->link.trained = false;
	dptx->num_modes = 0;
	dptx->has_edid = false;

	dptx->active = false;

	return 0;
}

static int handle_hotplug(struct dptx *dptx)
{
	int retval, ret_dpcd;
	u32 phyifctrl;
	u8 byte = 0;
	struct video_params *vparams;
	u8 rev;

	vparams = &dptx->vparams;

	// dptx_enable_ssc(dptx);

	dptx_soft_reset(dptx, DPTX_SRST_CTRL_AUX);

	/* Move PHY to P0 */
	phyifctrl = dptx_readl(dptx, DPTX_PHYIF_CTRL);
	phyifctrl &= ~DPTX_PHYIF_CTRL_LANE_PWRDOWN_MASK;
	dptx_writel(dptx, DPTX_PHYIF_CTRL, phyifctrl);

	ret_dpcd = drm_dp_dpcd_readb(&dptx->aux_dev, DP_MSTM_CAP, &byte);
	if (ret_dpcd < 0) {
		DRM_ERROR("read DP_MSTM_CAP err %d\n", ret_dpcd);
		return ret_dpcd;
	}

	if (byte & DP_MST_CAP) {
		ret_dpcd = drm_dp_dpcd_writeb(&dptx->aux_dev, DP_MSTM_CTRL, 0);
		if (ret_dpcd < 0) {
			DRM_ERROR("write DP_MSTM_CTRL err %d\n", ret_dpcd);
			return ret_dpcd;
		}
	}

	if (!dptx->mst)
		dptx->streams = 1;

	memset(dptx->rx_caps, 0, DPTX_RECEIVER_CAP_SIZE);
	ret_dpcd = drm_dp_dpcd_read(&dptx->aux_dev, DP_DPCD_REV,
					   dptx->rx_caps,
					   DPTX_RECEIVER_CAP_SIZE);
	if (ret_dpcd < 0) {
		DRM_ERROR("read rx_caps err %d\n", ret_dpcd);
		return ret_dpcd;
	}

	if (dptx->rx_caps[DP_TRAINING_AUX_RD_INTERVAL] &
	    DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT) {
		ret_dpcd = drm_dp_dpcd_read(&dptx->aux_dev, 0x2200,
						   dptx->rx_caps,
						   DPTX_RECEIVER_CAP_SIZE);
		if (ret_dpcd < 0) {
			DRM_ERROR("read rx EXTENDED caps err %d\n",
				 ret_dpcd);
			return ret_dpcd;
		}
	}

	rev = dptx->rx_caps[0];
	DRM_DEBUG("DP vision %x.%x\n", (rev & 0xf0) >> 4, rev & 0xf);

	retval = dptx_link_training(dptx, dptx->max_rate, dptx->max_lanes);
	if (retval)
		return retval;

	dptx->active = true;

	return 0;
}

irqreturn_t dptx_threaded_irq(int irq, void *dev)
{
	int retval = 0;
	struct dptx *dptx = dev;
	struct sprd_dp *dp = NULL;
	u32 hpdsts;
	struct sched_param param = { .sched_priority = 48};

	DRM_DEBUG("%s:\n", __func__);

	sched_setscheduler(current, SCHED_FIFO, &param);

	spin_lock(&dptx->irq_lock);
	dptx->irq_flag = true;
	spin_unlock(&dptx->irq_lock);

	if (dptx_check_power(dptx)) {
		spin_lock(&dptx->irq_lock);
		dptx->irq_flag = false;
		spin_unlock(&dptx->irq_lock);
		DRM_ERROR("power or clk disabled, irq exit\n");

		return IRQ_NONE;
	}

	if (dptx->dev) {
		dp = dev_get_drvdata(dptx->dev);
	} else {
		DRM_DEBUG("%s: param invalid\n", __func__);
		return IRQ_NONE;
	}

	hpdsts = dptx_readl(dptx, DPTX_HPDSTS);
	DRM_DEBUG("%s: HPDSTS = 0x%08x\n", __func__, hpdsts);

	/*
	 * TODO this should be set after all AUX transactions that are
	 * queued are aborted.
	 */
	atomic_set(&dptx->aux.abort, 0);

	if (atomic_read(&dptx->c_connect)) {
		atomic_set(&dptx->c_connect, 0);

		hpdsts = dptx_readl(dptx, DPTX_HPDSTS);
		if (hpdsts & DPTX_HPDSTS_STATUS) {
			handle_hotplug(dptx);
		} else {
			handle_hotunplug(dptx);
			if (dp->sink_has_audio) {
				DRM_DEBUG("%s: audio hdmi disconnect\n", __func__);
				extcon_set_state_sync(dptx->hdmi_edev, EXTCON_DISP_HDMI, 0);
			}
		}

		if (dptx->drm_dev)
			drm_helper_hpd_irq_event(dptx->drm_dev);
	}

	if (atomic_read(&dptx->sink_request)) {
		atomic_set(&dptx->sink_request, 0);
		if (atomic_read(&dptx->handle_sink_flag))
			retval = hotplug_handle_sink_request(dptx);
		else
			retval = handle_sink_request(dptx);

		if (retval)
			DRM_ERROR("Unable to handle sink request %d\n",
				 retval);
	}

	spin_lock(&dptx->irq_lock);
	dptx->irq_flag = false;
	spin_unlock(&dptx->irq_lock);
	DRM_DEBUG("%s: DONE\n", __func__);

	return IRQ_HANDLED;
}

static void handle_hpd_irq(struct dptx *dptx)
{
	DRM_DEBUG("%s: HPD_IRQ\n", __func__);
	atomic_set(&dptx->sink_request, 1);
}

irqreturn_t dptx_irq(int irq, void *dev)
{
	irqreturn_t retval = IRQ_HANDLED;
	struct dptx *dptx = dev;
	u32 ists, reg;

	if (dptx_check_power(dptx)) {
		DRM_ERROR("power or clk disabled, irq exit\n");
		return IRQ_NONE;
	}

	ists = dptx_readl(dptx, DPTX_ISTS);
	DRM_DEBUG("%s: ISTS=0x%08x\n", __func__, ists);

	if (!(ists & DPTX_ISTS_ALL_INTR)) {
		DRM_DEBUG("%s: IRQ_NONE\n", __func__);
		return IRQ_NONE;
	}

	if (ists & (DPTX_ISTS_AUX_REPLY | DPTX_ISTS_AUX_CMD_INVALID))
		DRM_DEBUG("%s: Should not receive AUX interrupts\n",
					__func__);

	if (ists & DPTX_ISTS_SDP) {
		DRM_DEBUG("%s: DPTX_ISTS_SDP\n", __func__);
		dptx_writel(dptx, DPTX_ISTS, DPTX_ISTS_SDP);
	}

	if (ists & DPTX_ISTS_AUDIO_FIFO_OVERFLOW) {
		DRM_DEBUG("%s: DPTX_ISTS_AUDIO_FIFO_OVERFLOW\n", __func__);
		dptx_writel(dptx, DPTX_ISTS, DPTX_ISTS_AUDIO_FIFO_OVERFLOW);
	}

	if (ists & DPTX_ISTS_VIDEO_FIFO_OVERFLOW) {
		DRM_DEBUG("%s: DPTX_ISTS_VIDEO_FIFO_OVERFLOW\n", __func__);
		dptx_writel(dptx, DPTX_ISTS, DPTX_ISTS_VIDEO_FIFO_OVERFLOW);
	}

	if (ists & DPTX_ISTS_TYPE_C) {
		DRM_INFO("%s: DPTX_ISTS_TYPE_C\n", __func__);
		dptx_writel(dptx, DPTX_ISTS, DPTX_ISTS_TYPE_C);

		reg = dptx_readl(dptx, DPTX_TYPE_C_CTRL);

		if (reg & 0x2)
			dptx_writel(dptx, DPTX_TYPE_C_CTRL, 0x5);
		else
			dptx_writel(dptx, DPTX_TYPE_C_CTRL, 0x4);

	}

	if (ists & DPTX_ISTS_HPD) {
		u32 hpdsts;

		DRM_INFO("%s: DPTX_ISTS_HPD\n", __func__);
		hpdsts = dptx_readl(dptx, DPTX_HPDSTS);

		DRM_DEBUG("%s: HPDSTS = 0x%08x\n", __func__, hpdsts);

		if (hpdsts & DPTX_HPDSTS_IRQ) {
			DRM_DEBUG("%s: DPTX_HPDSTS_IRQ\n", __func__);
			dptx_writel(dptx, DPTX_HPDSTS, DPTX_HPDSTS_IRQ);
			handle_hpd_irq(dptx);
			atomic_set(&dptx->handle_sink_flag, 0);
			retval = IRQ_WAKE_THREAD;
		}

		if (hpdsts & DPTX_HPDSTS_HOT_PLUG) {
			DRM_INFO("%s: DPTX_HPDSTS_HOT_PLUG\n", __func__);
			dptx_writel(dptx, DPTX_HPDSTS, DPTX_HPDSTS_HOT_PLUG);
			atomic_set(&dptx->aux.abort, 1);
			atomic_set(&dptx->c_connect, 1);
			atomic_set(&dptx->sink_request, 1);
			atomic_set(&dptx->handle_sink_flag, 1);
			retval = IRQ_WAKE_THREAD;
		}

		if (hpdsts & DPTX_HPDSTS_HOT_UNPLUG) {
			DRM_INFO("%s: DPTX_HPDSTS_HOT_UNPLUG\n",
				 __func__);
			dptx_writel(dptx, DPTX_HPDSTS, DPTX_HPDSTS_HOT_UNPLUG);
			atomic_set(&dptx->aux.abort, 1);
			atomic_set(&dptx->c_connect, 1);
			retval = IRQ_WAKE_THREAD;
		}
	}

	return retval;
}

static ssize_t dptx_dpaux_transfer(struct drm_dp_aux *aux,
				   struct drm_dp_aux_msg *msg)
{
	struct dptx *dp = container_of(aux, struct dptx, aux_dev);

	return dptx_aux_transfer(dp, msg);
}

struct dptx *dptx_init(struct device *dev, struct drm_device *drm_dev)
{
	struct dptx *dptx;
	int retval;
	struct sprd_dp *dp = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	struct device_type *new_device_type = devm_kzalloc(dev, sizeof(*new_device_type), GFP_KERNEL);
	if (!new_device_type)
		return ERR_PTR(-ENOMEM);

	dptx = devm_kzalloc(dev, sizeof(*dptx), GFP_KERNEL);
	if (!dptx)
		return ERR_PTR(-ENOMEM);

	dptx->dev = dev;
	dptx->base = dp->ctx.base;
	dptx->connector = dp->connector;
	dptx->ipa_usb31_dp = dp->ctx.ipa_usb31_dp;
	dptx->ipa_dispc1_glb_apb = dp->ctx.ipa_dispc1_glb_apb;
	dptx->dpu1_dpi_ctrl_reg = dp->ctx.dpu1_dpi_reg;
	dptx->force_shutdown = dp->ctx.force_shutdown;
	dptx->irq_flag = false;

	if (IS_ERR(dptx->base)) {
		dev_err(dev, "Failed to map memory resource\n");
		return ERR_PTR(-EFAULT);
	}

	dptx->irq = platform_get_irq(pdev, 0);
	if (dptx->irq < 0) {
		dev_err(dev, "No IRQ\n");
		return ERR_PTR(dptx->irq);
	}

	spin_lock_init(&dptx->irq_lock);

	dptx_misc_reset(dptx);
	dptx_video_params_reset(dptx);
	dptx_audio_params_reset(&dptx->aparams);
	atomic_set(&dptx->sink_request, 0);
	atomic_set(&dptx->c_connect, 0);

	dptx_global_intr_dis(dptx);

	retval = devm_request_threaded_irq(dev,
					   dptx->irq,
					   dptx_irq,
					   dptx_threaded_irq,
					   IRQF_SHARED, "dw_dptx", dptx);
	if (retval) {
		dev_err(dev, "Request for irq %d failed\n", dptx->irq);
		return ERR_PTR(retval);
	}

	/* Allocate extcon device */
	dptx->hdmi_edev = devm_extcon_dev_allocate(dptx->dev, dptx_extcon_cable);
	if (IS_ERR(dptx->hdmi_edev)) {
		dev_err(dev, "failed to allocate memory for extcon\n");
		retval = -ENOMEM;
	};

	/* Register extcon device */
	retval = devm_extcon_dev_register(dptx->dev, dptx->hdmi_edev);
	if (retval) {
		dev_err(dev, "failed to register extcon device\n");
		goto fail;
	}

	/**
	 * change sys/class/extcon/extcon3 to sys/class/extcon/audio3.
	 * the index of "audio[x]" must in the same as "extcon[x]",
	 * index is 3 in this case.
	*/
	memcpy(new_device_type, dptx->hdmi_edev->dev.type, sizeof(struct device_type));
	new_device_type->name = "audio3";
	if(IS_ERR(new_device_type->name)) {
		dev_err(dev, "failed to get new_device_type->name\n");
		return ERR_PTR(-EFAULT);
	}
	dptx->hdmi_edev->dev.type = new_device_type;

	device_rename(&dptx->hdmi_edev->dev, new_device_type->name);

	dptx_init_hwparams(dptx);

	retval = dptx_core_cap_init(dptx);
	if (retval)
		goto fail;

	dptx->drm_dev = drm_dev;
	dptx->aux_dev.name = "DP-AUX";
	dptx->aux_dev.transfer = dptx_dpaux_transfer;
	dptx->aux_dev.dev = &pdev->dev;
	dptx->aux_dev.drm_dev = drm_dev;

	retval = drm_dp_aux_register(&dptx->aux_dev);
	if (retval)
		goto fail;

	handle = dptx;
	return dptx;

fail:
	return ERR_PTR(retval);
}
