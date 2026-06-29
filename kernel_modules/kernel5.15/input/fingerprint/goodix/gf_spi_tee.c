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

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/fb.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/hardware_info.h>
#if IS_ENABLED(CONFIG_OF)
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#endif

#if IS_ENABLED(CONFIG_COMPAT)
#include <linux/compat.h>
#endif

#include "gf_spi_tee.h"
#include "gf_platform.h"
#include <linux/version.h>
/**************************defination******************************/
#define GF_NETLINK_ROUTE 29   /* for GF test temporary */
#define MAX_NL_MSG_LEN 16

#define WAKELOCK_HOLD_TIME 1000 /* in ms */
/*************************************************************/


/*************************************************************/
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gf_of_match[] = {
    {.compatible = "goodix,plat-driver"},
    {},
};
MODULE_DEVICE_TABLE(of, gf_of_match);
#endif

/* for netlink use */
static int pid = 0;

static u8 g_vendor_id = 0;
static ssize_t gf_debug_show(struct device *dev,
            struct device_attribute *attr, char *buf);

static ssize_t gf_debug_store(struct device *dev,
            struct device_attribute *attr, const char *buf, size_t count);
//#ifdef GF_SUPPORT_PINCTRL
static int gf_get_dts_info_pinctrl(struct gf_device *gf_dev);
//#endif
static void gf_irq_gpio_cfg(struct gf_device *gf_dev);
static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR, gf_debug_show, gf_debug_store);
static struct attribute *gf_debug_attrs[] = {
    &dev_attr_debug.attr,
    NULL};

static const struct attribute_group gf_debug_attr_group = {
    .attrs = gf_debug_attrs,
    .name = "debug"};

/* -------------------------------------------------------------------- */
/* timer function                               */
/* -------------------------------------------------------------------- */
#define TIME_START     0
#define TIME_STOP      1

static long int prev_time, cur_time;

long int kernel_time(unsigned int step)
{
    cur_time = ktime_to_us(ktime_get());
    if (step == TIME_START) {
        prev_time = cur_time;
        return 0;
    } else if (step == TIME_STOP) {
        gf_debug(DEBUG_LOG, "%s, use: %ld us\n", __func__, (cur_time - prev_time));
        return cur_time - prev_time;
    }
    prev_time = cur_time;
    return -1;
}

int gf_get_dts_info(struct gf_device *gf_dev)
{
	int rc = 0;
#ifndef GF_SUPPORT_PINCTRL
	struct device *dev = gf_dev->device;
#endif
	struct device_node *np = NULL;
	np = of_find_compatible_node(NULL, NULL, "goodix,plat-driver");
    gf_debug(DEBUG_LOG, "%s, get gpio\n", __func__);
    gf_dev->irq_gpio = of_get_named_gpio(np, "gfint-gpios", 0);
//#ifdef GF_SUPPORT_PINCTRL
    rc = gf_get_dts_info_pinctrl(gf_dev);//for set cs gpio mode befor power on
//#else
    if (rc) {
        gf_debug(DEBUG_LOG, "failed to get_dts_info_pinctrl, rc = %d\n", rc);
        return -1;
    }
    gf_dev->reset_gpio = of_get_named_gpio(np, "gfrst-gpios", 0);

    if (gf_dev->reset_gpio < 0) {
        gf_debug(DEBUG_LOG, "falied to get reset gpio!\n");
        return gf_dev->reset_gpio;
    }
    rc = devm_gpio_request(dev, gf_dev->reset_gpio, "gf_reset");
    if (rc) {
        gf_debug(DEBUG_LOG, "failed to request reset gpio, rc = %d\n", rc);
        return -1;
    }
    gpio_direction_output(gf_dev->reset_gpio, 0);
    gpio_set_value(gf_dev->reset_gpio, 0);

#ifdef GF_POWER_GPIO
    gf_dev->avdd_gpio = of_get_named_gpio(np, "gfvdd-gpios", 0);
    if (gf_dev->avdd_gpio < 0) {
        gf_debug(DEBUG_LOG, "falied to get avdd gpio!\n");
        return gf_dev->avdd_gpio;
    }

    rc = devm_gpio_request(dev, gf_dev->avdd_gpio, "gf_avdd");
    if (rc) {
        gf_debug(DEBUG_LOG, "failed to request vdd gpio, rc = %d\n", rc);
        goto err_avdd;
    }
#endif
    gf_irq_gpio_cfg(gf_dev);

    return rc;
}

void gf_cleanup_gpio(struct gf_device *gf_dev)
{
    gf_debug(DEBUG_LOG,"[info] %s\n", __func__);
    if (gpio_is_valid(gf_dev->irq_gpio)) {
        gpio_free(gf_dev->irq_gpio);
        gf_debug(DEBUG_LOG,"remove irq_gpio success\n");
    }
#ifndef GF_SUPPORT_PINCTRL
    if (gpio_is_valid(gf_dev->reset_gpio)) {
        gpio_free(gf_dev->reset_gpio);
        gf_debug(DEBUG_LOG,"remove reset_gpio success\n");
    }
    if (gpio_is_valid(gf_dev->avdd_gpio)) {
        gpio_free(gf_dev->avdd_gpio);
        gf_debug(DEBUG_LOG,"remove avdd_gpio success\n");
    }
#endif
    return;
}
int gf_hw_reset(struct gf_device *gf_dev, unsigned int delay_ms)
{
    if (gf_dev == NULL) {
        gf_debug(DEBUG_LOG,"Input buff is NULL.\n");
        return -1;
    }
#ifndef GF_SUPPORT_PINCTRL
    gpio_direction_output(gf_dev->reset_gpio, 0);
    gpio_set_value(gf_dev->reset_gpio, 0);
    mdelay(3);
    gpio_set_value(gf_dev->reset_gpio, 1);
    mdelay(delay_ms);
#else
    if (!IS_ERR(gf_dev->pins_reset_low))
        pinctrl_select_state(gf_dev->pinctrl, gf_dev->pins_reset_low);
    mdelay(3);
    if (!IS_ERR(gf_dev->pins_reset_high))
        pinctrl_select_state(gf_dev->pinctrl, gf_dev->pins_reset_high);
    mdelay(delay_ms);
#endif
    return 0;
}
/* -------------------------------------------------------------------- */
/* fingerprint chip hardware configuration                                */
/* -------------------------------------------------------------------- */

static int gf_get_dts_info_pinctrl(struct gf_device *gf_dev)
{
    //int rc = 0;
    struct device_node *node = NULL;
    struct platform_device *pdev = NULL;
    gf_dev->pinctrl = NULL;
    gf_dev->pstate_default = NULL;
    gf_dev->pstate_cs_func = NULL;
    
    node = of_find_compatible_node(NULL, NULL, "goodix,plat-driver");
    if (node) {
        pdev = of_find_device_by_node(node);
        if (pdev == NULL) {
            gf_debug(DEBUG_LOG,"[err] %s can not find device by node \n", __func__);
            return -1;
        }
    } else {
        gf_debug(DEBUG_LOG,"[err] %s can not find compatible node \n", __func__);
        return -1;
    }

    /*get pinctrl resource*/
    gf_dev->pinctrl = devm_pinctrl_get(&pdev->dev);
    if (IS_ERR(gf_dev->pinctrl)) {
        gf_debug(DEBUG_LOG, "can not get the gf pinctrl");
        return PTR_ERR(gf_dev->pinctrl);
    }
    gf_dev->pstate_gf_spi_active = pinctrl_lookup_state(gf_dev->pinctrl, "gf_spi_active");
    if (IS_ERR(gf_dev->pstate_gf_spi_active)) {
        gf_debug(DEBUG_LOG, "Can't find gf_spi_active pinctrl state\n");
        return PTR_ERR(gf_dev->pstate_gf_spi_active);
    }
    gf_dev->pstate_gf_spi_output_low = pinctrl_lookup_state(gf_dev->pinctrl, "gf_spi_output_low");
    if (IS_ERR(gf_dev->pstate_gf_spi_output_low)) {
        gf_debug(DEBUG_LOG, "Can't find gf_spi_output_low pinctrl state\n");
        return PTR_ERR(gf_dev->pstate_gf_spi_output_low);
    }
#ifdef GF_SUPPORT_PINCTRL
    gf_dev->pstate_before_power_on = pinctrl_lookup_state(gf_dev->pinctrl, "gf_before_power_on");
    if (IS_ERR(gf_dev->pstate_before_power_on)) {
        gf_debug(DEBUG_LOG, "Can't find befor_power_on pinctrl state\n");
        return PTR_ERR(gf_dev->pstate_before_power_on);
    } else {
        pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_before_power_on);
    }
    gf_dev->pstate_after_power_on = pinctrl_lookup_state(gf_dev->pinctrl, "gf_after_power_on");
    if (IS_ERR(gf_dev->pstate_after_power_on)) {
        gf_debug(DEBUG_LOG, "Can't find after_power_on pinctrl state\n");
        return PTR_ERR(gf_dev->pstate_after_power_on);
    }
    gf_dev->pins_reset_high = pinctrl_lookup_state(gf_dev->pinctrl, "gf_reset_high");
    if (IS_ERR(gf_dev->pins_reset_high)) {
        gf_debug(DEBUG_LOG, "Can't find gf_reset_high pinctrl state\n");
        return PTR_ERR(gf_dev->pins_reset_high);
    }
    gf_dev->pins_reset_low = pinctrl_lookup_state(gf_dev->pinctrl, "gf_reset_low");
    if (IS_ERR(gf_dev->pins_reset_low)) {
        gf_debug(DEBUG_LOG, "Can't find gf_reset_low pinctrl state\n");
        return PTR_ERR(gf_dev->pins_reset_low);
    }
#ifdef GF_POWER_GPIO
    gf_dev->pins_vdd_high = pinctrl_lookup_state(gf_dev->pinctrl, "gf_vdd_high");
    if (IS_ERR(gf_dev->pins_vdd_high)) {
        gf_debug(DEBUG_LOG, "Can't find gf_vdd_high pinctrl state\n");
        return PTR_ERR(gf_dev->pins_vdd_high);
    }
    gf_dev->pins_vdd_low = pinctrl_lookup_state(gf_dev->pinctrl, "gf_vdd_low");
    if (IS_ERR(gf_dev->pins_vdd_low)) {
        gf_debug(DEBUG_LOG, "Can't find gf_vdd_low pinctrl state\n");
        return PTR_ERR(gf_dev->pins_vdd_low);
    }
#endif
#endif
    return 0;
}


static void gf_hw_power_enable(struct gf_device *gf_dev, u8 onoff)
{
    /* TODO: LDO configure */
    static int enable = 1;
    int rc = 0;
    int ret = 0;
    if (onoff && enable) {
    /* TODO:  set power  according to actual situation  */
        enable = 0;
        if (gpio_is_valid(154)) {
            ret = gpio_request(154,"SPI0_CS");
            if (ret != 0) {
                gf_debug(ERR_LOG, "request spi0-cs gpio failed, %d", ret);
                return;
            }
        }
        ret = gpio_direction_output(154,0);
        gf_debug(INFO_LOG, "gf free spi0_cs");
        gpio_free(154);
        if (ret != 0) {
            gf_debug(ERR_LOG, "set spi0-cs gpio failed, %d", ret);
            return;
        }
        ret = gpio_get_value(154);
        gf_debug(INFO_LOG, "%d",ret);
        if(ret == 0)
        {
            gf_debug(INFO_LOG, "spi0_cs set 0");
        }
        
        gf_power_on(gf_dev);
        rc = pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_gf_spi_active);
        if(rc) {
            gf_debug(ERR_LOG, "[%s]: set spi active failed!\n", __func__);
        }
    } else if (!onoff && !enable) {
        enable = 1;
        gf_power_off(gf_dev);
        rc = pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_gf_spi_output_low);
        if(rc) {
            gf_debug(ERR_LOG, "[%s]: set spi outputlow failed!\n", __func__);
        }
        
        mdelay(4);
        if (gpio_is_valid(154)) {
            ret = gpio_request(154,"SPI0_CS");
            if (ret != 0) {
                gf_debug(ERR_LOG, "request spi0-cs gpio failed, %d", ret);
                return;
            }
        }
        ret = gpio_direction_output(154,0);
        gf_debug(INFO_LOG, "gf free spi0_cs");
        gpio_free(154);
        if (ret != 0) {
            gf_debug(ERR_LOG, "set spi0-cs gpio failed, %d", ret);
            return;
        }
        ret = gpio_get_value(154);
        gf_debug(INFO_LOG, "%d",ret);
        if(ret == 0)
        {
            gf_debug(INFO_LOG, "spi0_cs set 0");
        }
        gpio_direction_output(gf_dev->reset_gpio, 0);
        gpio_set_value(gf_dev->reset_gpio, 0);
    }
}

static void gf_bypass_flash_gpio_cfg(void)
{
    /* TODO: by pass flash IO config, default connect to GND */
}


static void gf_irq_gpio_cfg(struct gf_device *gf_dev)
{
    int ret = 0;
    
    if (!gf_dev) {
        gf_debug(ERR_LOG, "%s: Invalid device pointer\n", __func__);
        return;
    }
    
    if (gpio_is_valid(gf_dev->irq_gpio)) {
        ret = gpio_request(gf_dev->irq_gpio, "goodix_fp_irq");
        if (ret < 0) {
            gf_debug(ERR_LOG, "%s: Failed to request IRQ GPIO, error=%d\n", 
                    __func__, ret);
            return;
        }
        
        ret = gpio_direction_input(gf_dev->irq_gpio);
        if (ret < 0) {
            gf_debug(ERR_LOG, "%s: Failed to set GPIO direction, error=%d\n", 
                    __func__, ret);
            gpio_free(gf_dev->irq_gpio);
            return;
        }
        
        gf_dev->irq_num = gpio_to_irq(gf_dev->irq_gpio);
        if (gf_dev->irq_num < 0) {
            gf_debug(ERR_LOG, "%s: Failed to get IRQ number, error=%d\n", 
                    __func__, gf_dev->irq_num);
            gpio_free(gf_dev->irq_gpio);
            return;
        }
        
        gf_debug(INFO_LOG, "%s: IRQ GPIO configured successfully, irq=%d\n", 
                __func__, gf_dev->irq_num);
        gf_dev->irq = gf_dev->irq_num;
    } else {
        gf_debug(ERR_LOG, "%s: Invalid IRQ GPIO %d\n", __func__, gf_dev->irq_gpio);
    }
}

static void gf_enable_irq(struct gf_device *gf_dev)
{
    if (1 == gf_dev->irq_count) {
        gf_debug(ERR_LOG, "%s, irq already enabled\n", __func__);
    } else {
        enable_irq(gf_dev->irq);
        gf_dev->irq_count = 1;
        gf_debug(DEBUG_LOG, "%s enable interrupt!\n", __func__);
    }
}

static void gf_disable_irq(struct gf_device *gf_dev)
{
    if (0 == gf_dev->irq_count) {
        gf_debug(ERR_LOG, "%s, irq already disabled\n", __func__);
    } else {
        disable_irq(gf_dev->irq);
        gf_dev->irq_count = 0;
        gf_debug(DEBUG_LOG, "%s disable interrupt!\n", __func__);
    }
}


/* -------------------------------------------------------------------- */
/* netlink functions                 */
/* -------------------------------------------------------------------- */
void gf_netlink_send(struct gf_device *gf_dev, const int command)
{
    struct nlmsghdr *nlh = NULL;
    struct sk_buff *skb = NULL;
    int ret;

    gf_debug(INFO_LOG, "[%s] : enter, send command %d\n", __func__, command);
    if (NULL == gf_dev->nl_sk) {
        gf_debug(ERR_LOG, "[%s] : invalid socket\n", __func__);
        return;
    }

    if (0 == pid) {
        gf_debug(ERR_LOG, "[%s] : invalid native process pid\n", __func__);
        return;
    }

    /*alloc data buffer for sending to native*/
    /*malloc data space at least 1500 bytes, which is ethernet data length*/
    skb = alloc_skb(MAX_NL_MSG_LEN, GFP_ATOMIC);
    if (skb == NULL) {
        return;
    }

    nlh = nlmsg_put(skb, 0, 0, 0, MAX_NL_MSG_LEN, 0);
    if (!nlh) {
        gf_debug(ERR_LOG, "[%s] : nlmsg_put failed\n", __func__);
        kfree_skb(skb);
        return;
    }

    NETLINK_CB(skb).portid = 0;
    NETLINK_CB(skb).dst_group = 0;

    *(char *)NLMSG_DATA(nlh) = command;
    ret = netlink_unicast(gf_dev->nl_sk, skb, pid, MSG_DONTWAIT);
    if (ret == 0) {
        gf_debug(ERR_LOG, "[%s] : send failed\n", __func__);
        return;
    }

    gf_debug(INFO_LOG, "[%s] : send done, data length is %d\n", __func__, ret);
}

static void gf_netlink_recv(struct sk_buff *__skb)
{
    struct sk_buff *skb = NULL;
    struct nlmsghdr *nlh = NULL;
    char str[128];
    uint32_t data_len;

    gf_debug(INFO_LOG, "[%s] : enter \n", __func__);

    skb = skb_get(__skb);
    if (skb == NULL) {
        gf_debug(ERR_LOG, "[%s] : skb_get return NULL\n", __func__);
        return;
    }

    /* presume there is 5byte payload at leaset */
    if (skb->len >= NLMSG_SPACE(0)) {
        nlh = nlmsg_hdr(skb);
        data_len = NLMSG_PAYLOAD(nlh, 0);
        memcpy(str, NLMSG_DATA(nlh), min(data_len, (uint32_t)sizeof(str)));
        pid = nlh->nlmsg_pid;
        gf_debug(INFO_LOG, "[%s] : pid: %d, msg: %s\n", __func__, pid, str);

    } else {
        gf_debug(ERR_LOG, "[%s] : not enough data length\n", __func__);
    }

    kfree_skb(skb);
}

static int gf_netlink_init(struct gf_device *gf_dev)
{
    struct netlink_kernel_cfg cfg;

    memset(&cfg, 0, sizeof(struct netlink_kernel_cfg));
    cfg.input = gf_netlink_recv;

    gf_dev->nl_sk = netlink_kernel_create(&init_net, GF_NETLINK_ROUTE, &cfg);
    if (gf_dev->nl_sk == NULL) {
        gf_debug(ERR_LOG, "[%s] : netlink create failed\n", __func__);
        return -1;
    }

    gf_debug(INFO_LOG, "[%s] : netlink create success\n", __func__);
    return 0;
}

static void gf_netlink_destroy(struct gf_device *gf_dev)
{
    if (!gf_dev) {
        gf_debug(ERR_LOG, "[%s] invalid device\n", __func__);
        return;
    }
    
    if (gf_dev->nl_sk) {
        netlink_kernel_release(gf_dev->nl_sk);
        gf_dev->nl_sk = NULL;
        return;
    }

    gf_debug(ERR_LOG, "[%s] no netlink socket yet\n", __func__);
}


/* -------------------------------------------------------------------- */
/* file operation function                                              */
/* -------------------------------------------------------------------- */
static ssize_t gf_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    int retval = 0;

    FUNC_EXIT();
    return retval;
}

static ssize_t gf_write(struct file *filp, const char __user *buf,
            size_t count, loff_t *f_pos)
{
    gf_debug(ERR_LOG, "%s: Not support write operation in TEE mode\n", __func__);
    return -EFAULT;
}

static irqreturn_t gf_irq(int irq, void *handle)
{
    struct gf_device *gf_dev = (struct gf_device *)handle;
    FUNC_ENTRY();
    gf_debug(DEBUG_LOG, "%s gf irq...!\n", __func__);
#if GF_WAKEUP_SOURCE
    __pm_wakeup_event(gf_dev->fp_wl, WAKELOCK_HOLD_TIME);
#else
    wake_lock_timeout(gf_dev->fp_wl, msecs_to_jiffies(WAKELOCK_HOLD_TIME));
#endif
    gf_netlink_send(gf_dev, GF_NETLINK_IRQ);
    gf_dev->sig_count++;

    FUNC_EXIT();
    return IRQ_HANDLED;
}


static long gf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct gf_device *gf_dev = NULL;
    struct gf_key gf_key;
    uint32_t key_input = 0;

    int retval = 0;
    u8  buf    = 0;
    u8 netlink_route = GF_NETLINK_ROUTE;
    struct gf_ioc_chip_info info;

    FUNC_ENTRY();
    if (_IOC_TYPE(cmd) != GF_IOC_MAGIC)
        return -EINVAL;

    /* Check access direction once here; don't repeat below.
    * IOC_DIR is from the user perspective, while access_ok is
    * from the kernel perspective; so they look reversed.
    */
    if (_IOC_DIR(cmd) & _IOC_READ)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
        retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
#else
        retval = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
#endif

    if (retval == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
        retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
#else
        retval = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
#endif
    if (retval) {
        gf_debug(ERR_LOG, "%s: param check fail ======\n", __func__);
        return -EINVAL;
    }

    gf_dev = (struct gf_device *)filp->private_data;
    if (!gf_dev) {
        gf_debug(ERR_LOG, "%s: gf_dev IS NULL ======\n", __func__);
        return -EINVAL;
    }

    switch (cmd) {
    case GF_IOC_INIT:
        gf_debug(INFO_LOG, "%s: gf init,Version %s\n", __func__, GF_LINUX_VERSION);
        //gf_debug(INFO_LOG, "%s: gf init,Linux version: %d.%d.%d\n",
        //   (LINUX_VERSION_CODE >> 16) & 0xff,
        //   (LINUX_VERSION_CODE >> 8) & 0xff,
        //   LINUX_VERSION_CODE & 0xff);
        if (copy_to_user((void __user *)arg, (void *)&netlink_route, sizeof(u8))) {
            retval = -EFAULT;
            break;
        }

        if (gf_dev->system_status) {
            gf_debug(INFO_LOG, "%s: system re-started======\n", __func__);
            break;
        }
        retval = request_threaded_irq(gf_dev->irq, NULL, gf_irq,
                IRQF_TRIGGER_RISING | IRQF_ONESHOT, "goodix_fp_irq", gf_dev);
        if (!retval)
            gf_debug(INFO_LOG, "%s irq thread request success!\n", __func__);
        else
            gf_debug(ERR_LOG, "%s irq thread request failed, retval=%d\n", __func__, retval);

        gf_dev->irq_count = 1;
        enable_irq_wake(gf_dev->irq);
        gf_disable_irq(gf_dev);
        /* register screen on/off callback */
        gf_reg_screen_notify(gf_dev);
        gf_dev->sig_count = 0;
        gf_dev->system_status = 1;
        gf_debug(INFO_LOG, "%s: gf init finished======\n", __func__);
        break;

    case GF_IOC_CHIP_INFO:
        if (copy_from_user(&info, (struct gf_ioc_chip_info *)arg, sizeof(struct gf_ioc_chip_info))) {
            retval = -EFAULT;
            break;
        }
        g_vendor_id = info.vendor_id;

        gf_debug(INFO_LOG, "%s: vendor_id 0x%x\n", __func__, g_vendor_id);
        gf_debug(INFO_LOG, "%s: mode 0x%x\n", __func__, info.mode);
        gf_debug(INFO_LOG, "%s: operation 0x%x\n", __func__, info.operation);
        break;

    case GF_IOC_EXIT:
        gf_debug(INFO_LOG, "%s: GF_IOC_EXIT ======\n", __func__);
        gf_disable_irq(gf_dev);
        disable_irq_wake(gf_dev->irq);
        if (gf_dev->irq) {
            free_irq(gf_dev->irq, gf_dev);
            gf_dev->irq_count = 0;
            gf_dev->irq = 0;
        }

        gf_unreg_screen_notify(gf_dev);
        gf_un_reg_spi_driver();//for mtk spi reg
        gf_dev->system_status = 0;
        gf_debug(INFO_LOG, "%s: gf exit finished ======\n", __func__);
        break;

    case GF_IOC_RESET:
        gf_debug(INFO_LOG, "%s: chip reset command\n", __func__);
        gf_hw_reset(gf_dev, 10);
        break;

    case GF_IOC_ENABLE_IRQ:
        gf_debug(INFO_LOG, "%s: GF_IOC_ENABLE_IRQ ======\n", __func__);
        gf_enable_irq(gf_dev);
        break;

    case GF_IOC_DISABLE_IRQ:
        gf_debug(INFO_LOG, "%s: GF_IOC_DISABLE_IRQ ======\n", __func__);
        gf_disable_irq(gf_dev);
        break;

    case GF_IOC_ENABLE_SPI_CLK:
        gf_debug(INFO_LOG, "%s: GF_IOC_ENABLE_SPI_CLK ======\n", __func__);
#if GF_WAKEUP_SOURCE
        __pm_wakeup_event(gf_dev->fp_wl, WAKELOCK_HOLD_TIME);
#else   
        wake_lock_timeout(gf_dev->fp_wl, msecs_to_jiffies(WAKELOCK_HOLD_TIME));
#endif
        gf_spi_clk_enable(gf_dev);
        break;

    case GF_IOC_DISABLE_SPI_CLK:
        gf_debug(INFO_LOG, "%s: GF_IOC_DISABLE_SPI_CLK ======\n", __func__);
        gf_spi_clk_disable(gf_dev);
        break;

    case GF_IOC_ENABLE_POWER:
        gf_debug(INFO_LOG, "%s: GF_IOC_ENABLE_POWER ======\n", __func__);
        gf_hw_power_enable(gf_dev, 1);
        break;

    case GF_IOC_DISABLE_POWER:
        gf_debug(INFO_LOG, "%s: GF_IOC_DISABLE_POWER ======\n", __func__);
        gf_hw_power_enable(gf_dev, 0);
        break;

    case GF_IOC_INPUT_KEY_EVENT:
        if (copy_from_user(&gf_key, (struct gf_key *)arg, sizeof(struct gf_key))) {
            gf_debug(ERR_LOG, "Failed to copy input key event from user to kernel\n");
            retval = -EFAULT;
            break;
        }

        if (GF_KEY_HOME == gf_key.key) {
            key_input = GF_KEY_INPUT_HOME;
        } else if (GF_KEY_POWER == gf_key.key) {
            key_input = GF_KEY_INPUT_POWER;
        } else if (GF_KEY_CAMERA == gf_key.key) {
            key_input = GF_KEY_INPUT_CAMERA;
        } else {
            /* add special key define */
            key_input = gf_key.key;
        }
        gf_debug(INFO_LOG, "%s: received key event[%d], key=%d, value=%d\n",
                __func__, key_input, gf_key.key, gf_key.value);

        if ((GF_KEY_POWER == gf_key.key || GF_KEY_CAMERA == gf_key.key) && (gf_key.value == 1)) {
            input_report_key(gf_dev->input, key_input, 1);
            input_sync(gf_dev->input);
            input_report_key(gf_dev->input, key_input, 0);
            input_sync(gf_dev->input);
        }

        if (GF_KEY_HOME == gf_key.key) {
            input_report_key(gf_dev->input, key_input, gf_key.value);
            input_sync(gf_dev->input);
        }

        break;

    case GF_IOC_NAV_EVENT:
        gf_debug(ERR_LOG, "nav event send by hal with uinput");
#if 0
        if (copy_from_user(&nav_event, (gf_nav_event_t *)arg, sizeof(gf_nav_event_t))) {
            gf_debug(ERR_LOG, "Failed to copy nav event from user to kernel\n");
            retval = -EFAULT;
            break;
        }

        switch (nav_event) {
        case GF_NAV_FINGER_DOWN:
            gf_debug(ERR_LOG, "nav finger down");
            break;

        case GF_NAV_FINGER_UP:
            gf_debug(ERR_LOG, "nav finger up");
            break;

        case GF_NAV_DOWN:
            nav_input = GF_NAV_INPUT_DOWN;
            gf_debug(ERR_LOG, "nav down");
            break;

        case GF_NAV_UP:
            nav_input = GF_NAV_INPUT_UP;
            gf_debug(ERR_LOG, "nav up");
            break;

        case GF_NAV_LEFT:
            nav_input = GF_NAV_INPUT_LEFT;
            gf_debug(ERR_LOG, "nav left");
            break;

        case GF_NAV_RIGHT:
            nav_input = GF_NAV_INPUT_RIGHT;
            gf_debug(ERR_LOG, "nav right");
            break;

        case GF_NAV_CLICK:
            nav_input = GF_NAV_INPUT_CLICK;
            gf_debug(ERR_LOG, "nav click");
            break;

        case GF_NAV_HEAVY:
            nav_input = GF_NAV_INPUT_HEAVY;
            break;

        case GF_NAV_LONG_PRESS:
            nav_input = GF_NAV_INPUT_LONG_PRESS;
            break;

        case GF_NAV_DOUBLE_CLICK:
            nav_input = GF_NAV_INPUT_DOUBLE_CLICK;
            break;

        default:
            gf_debug(INFO_LOG, "%s: not support nav event nav_event: %d ======\n", __func__, nav_event);
            break;
        }

        if ((nav_event != GF_NAV_FINGER_DOWN) && (nav_event != GF_NAV_FINGER_UP)) {
            input_report_key(gf_dev->input, nav_input, 1);
            input_sync(gf_dev->input);
            input_report_key(gf_dev->input, nav_input, 0);
            input_sync(gf_dev->input);
        }
#endif
        break;

    case GF_IOC_ENTER_SLEEP_MODE:
        gf_debug(INFO_LOG, "%s: GF_IOC_ENTER_SLEEP_MODE ======\n", __func__);
        break;

    case GF_IOC_GET_FW_INFO:
        gf_debug(INFO_LOG, "%s: GF_IOC_GET_FW_INFO ======\n", __func__);
        buf = gf_dev->need_update;

        gf_debug(DEBUG_LOG, "%s: firmware info  0x%x\n", __func__, buf);
        get_hardware_info_data(HWID_FINGERPRINT,"GF3986");
        if (copy_to_user((void __user *)arg, (void *)&buf, sizeof(u8))) {
            gf_debug(ERR_LOG, "Failed to copy data to user\n");
            retval = -EFAULT;
        }

        break;
    case GF_IOC_REMOVE:
        gf_debug(INFO_LOG, "%s: GF_IOC_REMOVE ======\n", __func__);

        gf_netlink_destroy(gf_dev);

        mutex_lock(&gf_dev->release_lock);
        if (gf_dev->input == NULL) {
            mutex_unlock(&gf_dev->release_lock);
            break;
        }
        input_unregister_device(gf_dev->input);
        gf_dev->input = NULL;
        cdev_del(&gf_dev->cdev);
        //sysfs_remove_group(&gf_dev->device->kobj, &gf_debug_attr_group);
        device_destroy(gf_dev->class, gf_dev->devno);
        list_del(&gf_dev->device_entry);
        unregister_chrdev_region(gf_dev->devno, 1);
        class_destroy(gf_dev->class);
        gf_cleanup_gpio(gf_dev);
        gf_dev->spi = NULL;
        mutex_unlock(&gf_dev->release_lock);
        mutex_destroy(&gf_dev->release_lock);

        break;
    default:
        gf_debug(ERR_LOG, "gf doesn't support this command(%x)\n", cmd);
        break;
    }

    FUNC_EXIT();
    return retval;
}

#if IS_ENABLED(CONFIG_COMPAT)
static long gf_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;

    FUNC_ENTRY();

    retval = filp->f_op->unlocked_ioctl(filp, cmd, arg);

    FUNC_EXIT();
    return retval;
}
#endif

static unsigned int gf_poll(struct file *filp, struct poll_table_struct *wait)
{
    gf_debug(ERR_LOG, "Not support poll opertion in TEE version\n");
    return -EFAULT;
}


/* -------------------------------------------------------------------- */
/* devfs                                                              */
/* -------------------------------------------------------------------- */
static ssize_t gf_debug_show(struct device *dev,
            struct device_attribute *attr, char *buf)
{
    gf_debug(INFO_LOG, "%s: Show debug_level = 0x%x\n", __func__, g_debug_level);
    return sprintf(buf, "vendor id 0x%x\n", g_vendor_id);
}

static ssize_t gf_debug_store(struct device *dev,
            struct device_attribute *attr, const char *buf, size_t count)
{
    struct gf_device *gf_dev =  dev_get_drvdata(dev);
    u8 flag = 0;
    if (!strncmp(buf, "-8", 2)) {
        gf_debug(INFO_LOG, "%s: parameter is -8, enable spi clock test===============\n", __func__);

    } else if (!strncmp(buf, "-9", 2)) {
        gf_debug(INFO_LOG, "%s: parameter is -9, disable spi clock test===============\n", __func__);

    } else if (!strncmp(buf, "-10", 3)) {
        gf_debug(INFO_LOG, "%s: parameter is -10, gf init start===============\n", __func__);
    } else if (!strncmp(buf, "-11", 3)) {
        gf_debug(INFO_LOG, "%s: parameter is -11, enable irq===============\n", __func__);
        gf_enable_irq(gf_dev);

    } else if (!strncmp(buf, "-12", 3)) {
        gf_debug(INFO_LOG, "%s: parameter is -12, GPIO test===============\n", __func__);

#if IS_ENABLED(CONFIG_OF)
        if (flag == 0) {
            pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_before_power_on);
            gf_debug(INFO_LOG, "%s: set PIN to befor power on\n", __func__);
            flag = 1;
        } else {
            pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_after_power_on);
            gf_debug(INFO_LOG, "%s: set PIN to after power on\n", __func__);
            flag = 0;
        }
#endif

    } else if (!strncmp(buf, "-13", 3)) {
        gf_debug(INFO_LOG, "%s: parameter is -13, Vendor ID test --> 0x%x\n", __func__, g_vendor_id);
    } else {
        gf_debug(ERR_LOG, "%s: wrong parameter!===============\n", __func__);
    }

    return count;
}

/* -------------------------------------------------------------------- */
/* device function                                */
/* -------------------------------------------------------------------- */
static int gf_open(struct inode *inode, struct file *filp)
{
    struct gf_device *gf_dev = NULL;
    int status = -ENXIO;

    FUNC_ENTRY();
    
    if (!inode || !filp) {
        gf_debug(ERR_LOG, "%s: Invalid parameters\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&device_list_lock);
    list_for_each_entry(gf_dev, &device_list, device_entry) {
        if (gf_dev->devno == inode->i_rdev) {
            gf_debug(INFO_LOG, "%s: Found device\n", __func__);
            status = 0;
            break;
        }
    }
    
    if (status) {
        gf_debug(ERR_LOG, "%s: No device for minor %d\n", __func__, iminor(inode));
        mutex_unlock(&device_list_lock);
        return status;
    }
    mutex_unlock(&device_list_lock);

    if (gf_dev->system_status) {
        gf_debug(INFO_LOG, "%s: System already started\n", __func__);
    } else {
        status = gf_get_dts_info(gf_dev);
        if (status) {
            gf_debug(ERR_LOG, "%s: Failed to get DTS info, status=%d\n", __func__, status);
            return status;
        }
    }
    
    filp->private_data = gf_dev;
    nonseekable_open(inode, filp);
    gf_debug(INFO_LOG, "%s: Opened device, irq=%d\n", __func__, gf_dev->irq);
    gf_reg_spi_driver();

    FUNC_EXIT();
    return status;
}
static int gf_release(struct inode *inode, struct file *filp)
{
    struct gf_device *gf_dev = NULL;
    int status = 0;

    FUNC_ENTRY();
    gf_dev = filp->private_data;
    if (gf_dev->irq)
        gf_disable_irq(gf_dev);
    gf_dev->need_update = 0;
    FUNC_EXIT();
    return status;
}


static const struct file_operations gf_fops = {
    .owner =    THIS_MODULE,
    /* REVISIT switch to aio primitives, so that userspace
    * gets more complete API coverage.  It'll simplify things
    * too, except for the locking.
    */
    .write =    gf_write,
    .read =     gf_read,
    .unlocked_ioctl = gf_ioctl,
#if IS_ENABLED(CONFIG_COMPAT)
    .compat_ioctl = gf_compat_ioctl,
#endif
    .open =     gf_open,
    .release =  gf_release,
    .poll   = gf_poll,
};

/*-------------------------------------------------------------------------*/
static int gf_platform_probe(struct platform_device *pldev)
{
    struct gf_device *gf_dev = NULL;
    int status = -EINVAL;
    struct device *dev = &pldev->dev;

    FUNC_ENTRY();

    /* Allocate driver data */
    gf_dev = kzalloc(sizeof(struct gf_device), GFP_KERNEL);
    if (!gf_dev) {
        status = -ENOMEM;
        goto err;
    }

    mutex_init(&gf_dev->release_lock);

    INIT_LIST_HEAD(&gf_dev->device_entry);

    gf_dev->device_count     = 0;
    gf_dev->probe_finish     = 0;
    gf_dev->system_status    = 0;
    gf_dev->need_update      = 0;

    /*setup gf configurations.*/
    gf_debug(INFO_LOG, "%s, Setting gf device configuration==========\n", __func__);

    gf_dev->pldev = pldev;
    //gf_dev->device = dev;
    dev_set_drvdata(dev, gf_dev);

    gf_bypass_flash_gpio_cfg();
     

    /* create class */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    gf_dev->class = class_create(GF_CLASS_NAME);
#else
    gf_dev->class = class_create(THIS_MODULE,GF_CLASS_NAME);
#endif
    if (IS_ERR(gf_dev->class)) {
        gf_debug(ERR_LOG, "%s, Failed to create class.\n", __func__);
        status = -ENODEV;
        goto err_class;
    }

    /* get device no */
    if (GF_DEV_MAJOR > 0) {
        gf_dev->devno = MKDEV(GF_DEV_MAJOR, gf_dev->device_count++);
        status = register_chrdev_region(gf_dev->devno, 1, GF_DEV_NAME);
    } else {
        status = alloc_chrdev_region(&gf_dev->devno, gf_dev->device_count++, 1, GF_DEV_NAME);
    }
    if (status < 0) {
        gf_debug(ERR_LOG, "%s, Failed to alloc devno.\n", __func__);
        goto err_devno;
    } else {
        gf_debug(INFO_LOG, "%s, major=%d, minor=%d\n", __func__, MAJOR(gf_dev->devno), MINOR(gf_dev->devno));
    }

    /* create device */
    gf_dev->device = device_create(gf_dev->class, &pldev->dev, gf_dev->devno, gf_dev, GF_DEV_NAME);
    if (IS_ERR(gf_dev->device)) {
        gf_debug(ERR_LOG, "%s, Failed to create device.\n", __func__);
        status = -ENODEV;
        goto err_device;
    } else {
        mutex_lock(&device_list_lock);
        list_add(&gf_dev->device_entry, &device_list);
        mutex_unlock(&device_list_lock);
        gf_debug(INFO_LOG, "%s, device create success.\n", __func__);
    }

    /* create sysfs */
    status = sysfs_create_group(&gf_dev->device->kobj, &gf_debug_attr_group);
    if (status) {
        gf_debug(ERR_LOG, "%s, Failed to create sysfs file.\n", __func__);
        status = -ENODEV;
        goto err_sysfs;
    } else {
        gf_debug(INFO_LOG, "%s, Success create sysfs file.\n", __func__);
    }

    /* cdev init and add */
    cdev_init(&gf_dev->cdev, &gf_fops);
    gf_dev->cdev.owner = THIS_MODULE;
    status = cdev_add(&gf_dev->cdev, gf_dev->devno, 1);
    if (status) {
        gf_debug(ERR_LOG, "%s, Failed to add cdev.\n", __func__);
        goto err_cdev;
    }

    /*register device within input system.*/
    gf_dev->input = input_allocate_device();
    if (gf_dev->input == NULL) {
        gf_debug(ERR_LOG, "%s, Failed to allocate input device.\n", __func__);
        status = -ENOMEM;
        goto err_input;
    }

    __set_bit(EV_KEY, gf_dev->input->evbit);
    __set_bit(GF_KEY_INPUT_HOME, gf_dev->input->keybit);

    __set_bit(GF_KEY_INPUT_MENU, gf_dev->input->keybit);
    __set_bit(GF_KEY_INPUT_BACK, gf_dev->input->keybit);
    __set_bit(GF_KEY_INPUT_POWER, gf_dev->input->keybit);

    __set_bit(GF_NAV_INPUT_UP, gf_dev->input->keybit);
    __set_bit(GF_NAV_INPUT_DOWN, gf_dev->input->keybit);
    __set_bit(GF_NAV_INPUT_RIGHT, gf_dev->input->keybit);
    __set_bit(GF_NAV_INPUT_LEFT, gf_dev->input->keybit);
    __set_bit(GF_KEY_INPUT_CAMERA, gf_dev->input->keybit);
    __set_bit(GF_NAV_INPUT_CLICK, gf_dev->input->keybit);
    __set_bit(GF_NAV_INPUT_DOUBLE_CLICK, gf_dev->input->keybit);
    __set_bit(GF_NAV_INPUT_LONG_PRESS, gf_dev->input->keybit);
    __set_bit(GF_NAV_INPUT_HEAVY, gf_dev->input->keybit);

    gf_dev->input->name = GF_INPUT_NAME;
    if (input_register_device(gf_dev->input)) {
        gf_debug(ERR_LOG, "%s, Failed to register input device.\n", __func__);
        status = -ENODEV;
        goto err_input_2;
    }

    /* netlink interface init */
    status = gf_netlink_init(gf_dev);
    if (status == -1) {
        mutex_lock(&gf_dev->release_lock);
        input_unregister_device(gf_dev->input);
        gf_dev->input = NULL;
        mutex_unlock(&gf_dev->release_lock);
        goto err_input;
    }
#if GF_WAKEUP_SOURCE
    gf_dev->fp_wl = wakeup_source_register(&gf_dev->spi->dev, "fp_wakelock");
#else
    wake_lock_init(gf_dev->fp_wl, WAKE_LOCK_SUSPEND, "fp_wakelock");
#endif

    gf_dev->probe_finish = 1;
    gf_debug(INFO_LOG, "%s probe finished\n", __func__);

    FUNC_EXIT();
    return 0;

err_input_2:
    mutex_lock(&gf_dev->release_lock);
    input_free_device(gf_dev->input);
    gf_dev->input = NULL;
    mutex_unlock(&gf_dev->release_lock);

err_input:
    cdev_del(&gf_dev->cdev);

err_cdev:
    sysfs_remove_group(&dev->kobj, &gf_debug_attr_group);

err_sysfs:
    device_destroy(gf_dev->class, gf_dev->devno);
    list_del(&gf_dev->device_entry);

err_device:
    unregister_chrdev_region(gf_dev->devno, 1);

err_devno:
    class_destroy(gf_dev->class);

err_class:
    gf_hw_power_enable(gf_dev, 0);
    mutex_destroy(&gf_dev->release_lock);
    dev_set_drvdata(dev, NULL);
    kfree(gf_dev);
    gf_dev = NULL;
err:

    FUNC_EXIT();
    return status;
}

static int gf_platform_remove(struct platform_device *pldev)
{
    struct device *dev = &pldev->dev;
    struct gf_device *gf_dev = dev_get_drvdata(dev);

    FUNC_ENTRY();
#if GF_WAKEUP_SOURCE    
    wakeup_source_unregister(gf_dev->fp_wl);
#else
    wake_lock_destroy(gf_dev->fp_wl);
#endif
    /* make sure ops on existing fds can abort cleanly */
    if (gf_dev->irq) {
        free_irq(gf_dev->irq, gf_dev);
        gf_dev->irq_count = 0;
        gf_dev->irq = 0;
    }

    //fb_unregister_client(&gf_dev->notifier);
    mutex_lock(&gf_dev->release_lock);
    if (gf_dev->input == NULL) {
        kfree(gf_dev);
        mutex_unlock(&gf_dev->release_lock);
        FUNC_EXIT();
        return 0;
    }
    input_unregister_device(gf_dev->input);
    gf_dev->input = NULL;
    mutex_unlock(&gf_dev->release_lock);

    gf_netlink_destroy(gf_dev);
    cdev_del(&gf_dev->cdev);
    sysfs_remove_group(&pldev->dev.kobj, &gf_debug_attr_group);
    device_destroy(gf_dev->class, gf_dev->devno);
    list_del(&gf_dev->device_entry);

    unregister_chrdev_region(gf_dev->devno, 1);
    class_destroy(gf_dev->class);
    gf_hw_power_enable(gf_dev, 0);

    dev_set_drvdata(dev, NULL);
    mutex_destroy(&gf_dev->release_lock);

    kfree(gf_dev);
    FUNC_EXIT();
    return 0;
}

/*-------------------------------------------------------------------------*/
static struct platform_driver goodix_fp_driver = {
    .driver = {
            .name = "goodix_fp",
            .owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_OF)
        .of_match_table = gf_of_match,
#endif
            },
    .probe = gf_platform_probe,
    .remove = gf_platform_remove
};

static int __init gf_init(void)
{
    int status = 0;

    FUNC_ENTRY();
    gf_debug(DEBUG_LOG,"%s %d\n", __func__, __LINE__);

    status = platform_driver_register(&goodix_fp_driver);
    if (status < 0) {
        gf_debug(ERR_LOG, "%s, Failed to register SPI driver.\n",
             __func__);
        return -EINVAL;
    }

    FUNC_EXIT();
    return status;
}
//module_init(gf_init);
late_initcall(gf_init);

static void __exit gf_exit(void)
{
    FUNC_ENTRY();
    platform_driver_unregister(&goodix_fp_driver);

    FUNC_EXIT();
}
module_exit(gf_exit);


MODULE_AUTHOR("goodix");
MODULE_DESCRIPTION("Goodix Fingerprint chip TEE driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:gf_spi");
