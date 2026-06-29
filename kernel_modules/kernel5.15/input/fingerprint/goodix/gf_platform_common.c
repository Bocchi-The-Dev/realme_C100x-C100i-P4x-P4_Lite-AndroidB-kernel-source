/* Goodix's fingerprint sensor linux driver for TEE
 *
 * 2010 - 2015 Goodix Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "gf_spi_tee.h"
#include "gf_platform.h"
#include <linux/notifier.h>
#include <linux/fb.h>
#ifdef GF_SUPPORT_QCOM_DRM
#include <drm/drm_panel.h>
#endif
/* align=2, 2 bytes align */
/* align=4, 4 bytes align */
/* align=8, 8 bytes align */
#define ROUND_UP(x, align)      ((x+(align-1))&~(align-1))
#define GF_NETLINK_ENABLE
#ifdef GF_POWER_GPIO
static int gf_power_on_by_gpio(struct gf_device *gf_dev);
static int gf_power_off_by_gpio(struct gf_device *gf_dev);
#elif defined(GF_POWER_EXT_LDO)
static int gf_power_on_by_exldo(struct gf_device *gf_dev);
static int gf_power_off_by_exldo(struct gf_device *gf_dev);
#elif defined(GF_POWER_PMIC_LDO)
static int gf_power_on_by_ldo(struct gf_device *gf_dev);
static int gf_power_off_by_ldo(struct gf_device *gf_dev);
#endif
extern void gf_netlink_send(struct gf_device *gf_dev, const int command);

 void gf_spi_clk_enable(struct gf_device *gf_dev)
 {
    gf_debug(INFO_LOG, "%s no need for this platform\n", __func__);
    return;
 }

 void gf_spi_clk_disable(struct gf_device *gf_dev)
 {
    gf_debug(INFO_LOG, "%s no need for this platform\n", __func__);
    return;
 }

/* -------------------------------------------------------------------- */
/* screen on/off callback and suspend/resume functions          */
/* -------------------------------------------------------------------- */
#ifdef GF_SUPPORT_QCOM_DRM
static void goodix_panel_state_chg_callback(enum panel_event_notifier_tag tag,
                                struct panel_event_notification *notification,
                                        void *pvt_data)
{
    struct gf_device *gf_dev;

    if (!notification || !notification->notif_data.early_trigger)
        return ;
    gf_debug(INFO_LOG,"[info] %s go to the goodix_fb_state_chg_callback type = %d\n",
            __func__, (int)notification->notif_type);
    gf_dev = pvt_data;
    if (gf_dev) {
        switch (notification->notif_type) {
        case DRM_PANEL_EVENT_BLANK:
#if defined(GF_NETLINK_ENABLE)
                gf_netlink_send(gf_dev, GF_NETLINK_SCREEN_OFF);
#elif defined(GF_FASYNC)
                if (gf_dev->async)
                    kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
#endif
            break;
        case DRM_PANEL_EVENT_UNBLANK:
#if defined(GF_NETLINK_ENABLE)
                gf_netlink_send(gf_dev, GF_NETLINK_SCREEN_ON);
#elif defined(GF_FASYNC)
                if (gf_dev->async)
                    kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
#endif
            break;
        default:
            gf_debug(INFO_LOG,"%s default\n", __func__);
            break;
        }
    }
    return ;
}
#else
static int gf_fb_notifier_callback(struct notifier_block *self,
            unsigned long event, void *data)
{
    struct gf_device *gf_dev = NULL;
    struct fb_event *evdata = data;
    unsigned int blank;
    int retval = 0;
    FUNC_ENTRY();

    /* If we aren't interested in this event, skip it immediately ... */
    if (event != FB_EVENT_BLANK /* FB_EARLY_EVENT_BLANK */)
        return 0;

    gf_dev = container_of(self, struct gf_device, notifier);
    blank = *(int *)evdata->data;

    gf_debug(INFO_LOG, "[%s] : enter, blank=0x%x\n", __func__, blank);

    switch (blank) {
    case FB_BLANK_UNBLANK:
        gf_debug(INFO_LOG, "[%s] : lcd on notify\n", __func__);
        gf_netlink_send(gf_dev, GF_NETLINK_SCREEN_ON);
        break;

    case FB_BLANK_POWERDOWN:
        gf_debug(INFO_LOG, "[%s] : lcd off notify\n", __func__);
        gf_netlink_send(gf_dev, GF_NETLINK_SCREEN_OFF);
        break;

    default:
        gf_debug(INFO_LOG, "[%s] : other notifier, ignore\n", __func__);
        break;
    }
    FUNC_EXIT();
    return retval;
}
#endif
#ifdef GF_SUPPORT_QCOM_DRM
int check_drm_dt(struct gf_device *gf_dev)
{
    int count,i = 0;
    struct device *dev = &gf_dev->pldev->dev;
    struct device_node *np = dev->of_node;
    struct device_node *node = NULL;
    struct drm_panel *panel = NULL;

    count = of_count_phandle_with_args(np, "panel", NULL);
    if (count <= 0) {
        gf_debug(ERR_LOG,"find drm_panel count(%d) fail", count);
        return -ENODEV;
    }

    for (i = 0; i < count; i++) {
        node = of_parse_phandle(np, "panel", i);
        panel = of_drm_find_panel(node);
        of_node_put(node);
        if (!IS_ERR(panel)) {
            gf_debug(ERR_LOG,"find drm_panel successfully");
            gf_dev->active_panel = panel;
            return 0;
        }
    }
    gf_debug(ERR_LOG,"goodix fp can not find drm_panel");

    return -ENODEV;
}
#endif
void gf_reg_screen_notify(struct gf_device *gf_dev)
{
#ifdef GF_SUPPORT_QCOM_DRM
    int status = 0;
#endif
    if (!gf_dev) {
        gf_debug(ERR_LOG, "%s: Invalid device pointer\n", __func__);
        return;
    }
#ifdef GF_SUPPORT_QCOM_DRM
    status = check_drm_dt(gf_dev);
    if(status)
        gf_debug(ERR_LOG,"goodix fp parse drm-panel fail\n");
    gf_dev->panel_notifier = goodix_panel_state_chg_callback;
    if(gf_dev->active_panel){
        gf_dev->cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
            PANEL_EVENT_NOTIFIER_CLIENT_ECM,
        gf_dev->active_panel,gf_dev->panel_notifier, gf_dev);
        if(!gf_dev->cookie)
            gf_debug(ERR_LOG,"goodix fp drm_panel_notifier_register fail\n");
    }
#else
    gf_dev->notifier.notifier_call = gf_fb_notifier_callback;
    fb_register_client(&gf_dev->notifier);
#endif
}

void gf_unreg_screen_notify(struct gf_device *gf_dev)
{
    if (!gf_dev) {
        gf_debug(ERR_LOG, "%s: Invalid device pointer\n", __func__);
        return;
    }
    
#ifdef GF_SUPPORT_QCOM_DRM
    if (gf_dev->cookie) {
        panel_event_notifier_unregister(gf_dev->cookie);
        gf_dev->cookie = NULL;
    }
#else
    fb_unregister_client(&gf_dev->notifier);
#endif
}

void gf_reg_spi_driver(void)
{
    gf_debug(INFO_LOG, "%s no need for this platform\n", __func__);
    return;
}

void gf_un_reg_spi_driver(void)
{
    gf_debug(INFO_LOG, "%s no need for this platform\n", __func__);
    return;
}


void gf_power_on(struct gf_device *gf_dev)
{
    int rc = 0;
#ifdef GF_POWER_GPIO
    rc = gf_power_on_by_gpio(gf_dev);
#elif defined(GF_POWER_EXT_LDO)
    rc = gf_power_on_by_exldo(gf_dev);
#elif defined(GF_POWER_PMIC_LDO)
    rc = gf_power_on_by_ldo(gf_dev);
#else
    gf_debug(DEBUG_LOG, "---- No power control method selected----\n");
#endif
    mdelay(10);
    gf_hw_reset(gf_dev, 3);
    if (rc) {
        gf_debug(DEBUG_LOG, "---- power on failed rc = %d ----\n", rc);
    } else {
        gf_debug(DEBUG_LOG, "---- power on ok  ----\n");
    }
}

void gf_power_off(struct gf_device *gf_dev)
{
    int rc = 0;
#ifdef GF_POWER_GPIO
    rc = gf_power_off_by_gpio(gf_dev);
#elif defined(GF_POWER_EXT_LDO)
    rc = gf_power_off_by_exldo(gf_dev);
#elif defined(GF_POWER_PMIC_LDO)
    rc = gf_power_off_by_ldo(gf_dev);
#else
    gf_debug(DEBUG_LOG, "---- No power control method selected----\n");
#endif
    if (rc) {
        gf_debug(DEBUG_LOG, "---- power off failed rc = %d ----\n", rc);
    } else {
        gf_debug(DEBUG_LOG, "---- power off ok  ----\n");
    }
}

#ifdef GF_POWER_GPIO
static int gf_power_on_by_gpio(struct gf_device *gf_dev)
    int rc = 0;
    #ifndef GF_SUPPORT_PINCTRL
        rc = gpio_direction_output(gf_dev->avdd_gpio, 1);
    #else
        if (!IS_ERR(gf_dev->pins_vdd_low))
            rc = pinctrl_select_state(gf_dev->pinctrl, gf_dev->pins_vdd_low);
    #endif
    gf_debug(DEBUG_LOG, "set avdd_gpio on,ret = %d",rc);
    return rc;
}
static int gf_power_off_by_gpio(struct gf_device *gf_dev)
{
    int rc = 0;
    #ifndef GF_SUPPORT_PINCTRL
        gpio_direction_output(gf_dev->avdd_gpio, 0);
    #else
        if (!IS_ERR(gf_dev->pins_vdd_high))
            pinctrl_select_state(gf_dev->pinctrl, gf_dev->pins_vdd_high);
    #endif
    gf_debug(DEBUG_LOG, "set avdd_gpio off,ret = %d",rc);
    return rc;
}
#elif defined(GF_POWER_EXT_LDO)
static int gf_power_on_by_exldo(struct gf_device *gf_dev)
{
    int rc = 0;
    rc = wl2868c_set_ldo_enable(LDO4, 3000);
    gf_debug(DEBUG_LOG, "---- power_on external ldo ---- rc = %d\n", rc);
    return rc;
}
static int gf_power_off_by_exldo(struct gf_device *gf_dev)
{
    int rc = 0;
    rc = wl2868c_set_ldo_disable(LDO4);
    gf_debug(DEBUG_LOG, "---- power_off external ldo ---- rc = %d\n", rc);
    return rc;
}
#elif defined(GF_POWER_PMIC_LDO)
static int gf_power_on_by_ldo(struct gf_device *gf_dev)
{
    int rc = 0;
    if (NULL == gf_dev->vdd) {
        //gf_dev->vdd = regulator_get(&gf_dev->pldev->dev, "vdd");
        gf_dev->vdd = devm_regulator_get(&gf_dev->pldev->dev, "vdd");
        if (IS_ERR(gf_dev->vdd)) {
            gf_debug(DEBUG_LOG, "---- unable to get vdd ----\n");
            return -1;
        } else {
            gf_debug(DEBUG_LOG, "---- get vdd ok  ----\n");
        }
    }
    if (NULL != gf_dev->vdd) {
        if (regulator_is_enabled(gf_dev->vdd)) {
            gf_debug(DEBUG_LOG, "---- vdd is already enabled  ----\n");
            return 0;
        }
        rc = regulator_set_load(gf_dev->vdd, 600000);
        if (rc < 0) {
            gf_debug(DEBUG_LOG, "---- unable to regulator_set_load ----\n");
        }
        if (regulator_count_voltages(gf_dev->vdd) > 0) {
            rc = regulator_set_voltage(gf_dev->vdd, 3000000, 3000000);
            if (rc) {
                gf_debug(DEBUG_LOG, "---- unable to regulator_set_voltage ----\n");
                //return;
            }
        }
        rc = regulator_enable(gf_dev->vdd);
    }
    return rc;
}
static int gf_power_off_by_ldo(struct gf_device *gf_dev)
{
    int rc = 0;
    if (NULL != gf_dev->vdd) {
        rc = regulator_set_load(gf_dev->vdd, 0);
        if (rc < 0) {
            gf_debug(DEBUG_LOG, "---- unable to regulator_set_load 0 ----\n");
        }
        if (regulator_is_enabled(gf_dev->vdd)) {
            regulator_disable(gf_dev->vdd);
            gf_debug(DEBUG_LOG, "---- regulator_disable  ----\n");
        }
    }
    return rc;
}
#endif
