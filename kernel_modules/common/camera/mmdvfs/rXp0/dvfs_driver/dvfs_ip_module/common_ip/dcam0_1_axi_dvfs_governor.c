// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mm_devfreq_common.h"

static int dvfs_probe(struct platform_device *pdev);
static int dvfs_remove(struct platform_device *pdev);
static int dvfs_gov_get_target(struct devfreq *devfreq,
			unsigned long *freq);
static int dvfs_gov_event_handler(struct devfreq *devfreq,
			unsigned int event, void *data);

static const struct of_device_id dcam0_1_axi_dvfs_of_match[] = {
	#if defined(DVFS_VERSION_R1P0)
	{.compatible = "sprd,hwdvfs-dcam-axi"},
	#elif defined(DVFS_VERSION_N6P) || defined(DVFS_VERSION_N6L)
	{.compatible = "sprd,hwdvfs-dcam0_1-axi"},
	#endif
	{},
};
MODULE_DEVICE_TABLE(of, dcam0_1_axi_dvfs_of_match);

static struct platform_driver dcam0_1_axi_dvfs_driver = {
	.probe = dvfs_probe,
	.remove = dvfs_remove,
	.driver = {
			.name = "dcam0_1_axi-dvfs",
			.of_match_table = dcam0_1_axi_dvfs_of_match,
	},
};

static struct devfreq_dev_profile dvfs_profile = {
	.polling_ms = 200,
	.target = dvfs_target,
	.get_dev_status = dvfs_get_dev_status,
	.get_cur_freq = dvfs_get_cur_freq,
};

static int dvfs_gov_get_target(struct devfreq *devfreq,
			unsigned long *freq)
{
	struct module_dvfs *module = dev_get_drvdata(devfreq->dev.parent);

	if (module->dvfs_enable) {
		unsigned long adjusted_freq = module->user_freq;

		if (module->user_freq_type == DVFS_WORK)
			adjusted_freq = module->module_dvfs_para.u_work_freq;
		else
			adjusted_freq = module->module_dvfs_para.u_idle_freq;

#if (KERNEL_VERSION(5, 15, 0) > LINUX_VERSION_CODE)
		if (devfreq->max_freq && adjusted_freq > devfreq->max_freq)
			adjusted_freq = devfreq->max_freq;

		if (devfreq->min_freq && adjusted_freq < devfreq->min_freq)
			adjusted_freq = devfreq->min_freq;
#endif
		*freq = adjusted_freq;
	} else
		pr_err("dcam01_axi_dvfs_gov, %s: dvfs unenable", __func__);

	pr_info("dcam01_axi_dvfs_gov, %s: *freq:%lu", __func__, *freq);

	return MM_DVFS_SUCCESS;
}

static int dvfs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct module_dvfs *module;
	int ret = MM_DVFS_SUCCESS;

	pr_info("module-dvfs initialized\n");

	module = devm_kzalloc(dev, sizeof(*module), GFP_KERNEL);
	if (!module)
		return -ENOMEM;

	mutex_init(&module->lock);

#ifdef SUPPORT_SWDVFS

	module->clk_module_core = devm_clk_get(dev, "clk_module_core");
	if (IS_ERR(module->clk_module_core))
		dev_err(dev, "Cannot get the clk_module_core clk\n");

	if (of_property_read_u32(np, "sprd,dvfs-wait-window", &module->dvfs_wait_window))
		pr_err("np: sprd,dvfs-wait-window");
#endif
	ret = dev_pm_opp_of_add_table(dev);
	if (ret) {
		dev_err(dev, "dvfs: Invalid operating-points in device tree.\n");
		goto err;
	}

	if (of_property_read_u32(np, "sprd,dvfs-work-index-def",
		&module->module_dvfs_para.ip_coffe.work_index_def))
		pr_err("np: the value of the of_property_read_u32\n");
	platform_set_drvdata(pdev, module);
	module->devfreq =
		devm_devfreq_add_device(dev, &dvfs_profile, "dcam0_1_axi_dvfs", NULL);
	if (IS_ERR(module->devfreq)) {
		dev_err(dev, "failed to add devfreq dev with module-dvfs governor\n");
		ret = PTR_ERR(module->devfreq);
		goto err;
	}

	module->pw_nb.priority = 0;
	module->pw_nb.notifier_call = dvfs_notify_callback;
	ret = mmsys_register_notifier(&module->pw_nb);
	if (ret)
		pr_err("error: mmsys_register_notifier failed");

	return ret;

err:
	dev_pm_opp_of_remove_table(dev);
	return -EINVAL;
}

static int dvfs_remove(struct platform_device *pdev)
{
	pr_err("dvfs remove success:\n");

	return MM_DVFS_SUCCESS;
}

/*sys for gov_entries*/
static DEVICE_ATTR_RW(set_hw_dvfs_en);
static DEVICE_ATTR_RW(set_auto_tune_en);
static DEVICE_ATTR_RW(set_dvfs_coffe);
static DEVICE_ATTR_RW(get_ip_status);
static DEVICE_ATTR_RW(set_work_freq);
static DEVICE_ATTR_RW(set_idle_freq);
static DEVICE_ATTR_RW(set_work_index);
static DEVICE_ATTR_RW(set_idle_index);
static DEVICE_ATTR_RW(set_fix_dvfs_value);
static DEVICE_ATTR_RW(get_dvfs_table_info);

/*sys for coeff_entries*/
static DEVICE_ATTR_RW(set_gfree_wait_delay);
static DEVICE_ATTR_RW(set_freq_upd_hdsk_en);
static DEVICE_ATTR_RW(set_freq_upd_delay_en);
static DEVICE_ATTR_RW(set_freq_upd_en_byp);
static DEVICE_ATTR_RW(set_sw_trig_en);

static struct attribute *dev_entries[] = {

	&dev_attr_set_hw_dvfs_en.attr,
	&dev_attr_set_auto_tune_en.attr,
	&dev_attr_set_dvfs_coffe.attr,
	&dev_attr_get_ip_status.attr,
	&dev_attr_set_work_freq.attr,
	&dev_attr_set_idle_freq.attr,
	&dev_attr_set_work_index.attr,
	&dev_attr_set_idle_index.attr,
	&dev_attr_set_fix_dvfs_value.attr,
	&dev_attr_get_dvfs_table_info.attr,
	NULL,
};

static struct attribute *coeff_entries[] = {

	&dev_attr_set_gfree_wait_delay.attr,
	&dev_attr_set_freq_upd_hdsk_en.attr,
	&dev_attr_set_freq_upd_delay_en.attr,
	&dev_attr_set_freq_upd_en_byp.attr,

	&dev_attr_set_sw_trig_en.attr,
	&dev_attr_set_auto_tune_en.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.name = "dcam0_1-axi_governor", .attrs = dev_entries,
};

static struct attribute_group coeff_attr_group = {
	.name = "dcam0_1-axi_coeff", .attrs = coeff_entries,
};

struct devfreq_governor dcam0_1_axi_dvfs_gov = {
	.name = "dcam0_1_axi_dvfs",
	.get_target_freq = dvfs_gov_get_target,
	.event_handler = dvfs_gov_event_handler,
};

static void userspace_exit(struct devfreq *devfreq)
{
	/*
	 * Remove the sysfs entry, unless this is being called after
	 * device_del(), which should have done this already via kobject_del().
	 */
	if (devfreq->dev.kobj.sd) {
		sysfs_remove_group(&devfreq->dev.kobj, &dev_attr_group);
		sysfs_remove_group(&devfreq->dev.kobj, &coeff_attr_group);
	}
}

static int userspace_init(struct devfreq *devfreq)
{
	int err;

	struct module_dvfs *module = dev_get_drvdata(devfreq->dev.parent);

	module->dvfs_ops = get_ip_dvfs_ops("D01AXI_DVFS_OPS");

	err = sysfs_create_group(&devfreq->dev.kobj, &dev_attr_group);
	err = sysfs_create_group(&devfreq->dev.kobj, &coeff_attr_group);

	return err;
}

static int dvfs_gov_event_handler(struct devfreq *devfreq,
			unsigned int event, void *data)
{
	int ret = MM_DVFS_SUCCESS;

	pr_info("devfreq_governor-->event_handler(%d)\n", event);
	switch (event) {
	case DEVFREQ_GOV_START:
		ret = userspace_init(devfreq);
		break;
	case DEVFREQ_GOV_STOP:
		userspace_exit(devfreq);
		break;
	default:
		break;
	}

	return ret;
}

int dcam0_1_axi_dvfs_init(void)
{
	int ret;

	ret = register_ip_dvfs_ops(&dcam0_1_axi_dvfs_ops);
	if (ret) {
		pr_err("%s: failed to add ops: %d\n", __func__, ret);
		return ret;
	}

	ret = devfreq_add_governor(&dcam0_1_axi_dvfs_gov);
	if (ret) {
		pr_err("%s: failed to add governor: %d\n", __func__, ret);
		return ret;
	}
	ret = platform_driver_register(&dcam0_1_axi_dvfs_driver);
	if (ret)
		devfreq_remove_governor(&dcam0_1_axi_dvfs_gov);
	return ret;
}

void dcam0_1_axi_dvfs_exit(void)
{
	int ret;

	platform_driver_unregister(&dcam0_1_axi_dvfs_driver);

	ret = devfreq_remove_governor(&dcam0_1_axi_dvfs_gov);
	if (ret)
		pr_err("%s: failed to remove governor: %d\n", __func__, ret);

	ret = unregister_ip_dvfs_ops(&dcam0_1_axi_dvfs_ops);
	if (ret)
		pr_err("%s: failed to remove ops: %d\n", __func__, ret);
}


