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

#include "mmsys_dvfs.h"
#include "top_dvfs_reg.h"
#include "mmsys_dvfs_comm.h"
#include <linux/sprd_soc_id.h>
#include <sprd_camsys_domain.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "mmsys-dvfs: devfreq %d %s : "\
		fmt, __LINE__, __func__


static int mmsys_dvfs_target(struct device *dev, unsigned long *freq,
			u32 flags);
static int mmsys_dvfs_get_dev_status(struct device *dev,
			struct devfreq_dev_status *stat);
static int mmsys_dvfs_get_cur_freq(struct device *dev, unsigned long *freq);
static int mmsys_dvfs_probe(struct platform_device *pdev);
static int mmsys_dvfs_remove(struct platform_device *pdev);

static const struct of_device_id mmsys_dvfs_of_match[] = {
	{.compatible = "sprd,hwdvfs-mmsys"}, {},
};
MODULE_DEVICE_TABLE(of, mmsys_dvfs_of_match);

static struct platform_driver mmsys_dvfs_driver = {
	.probe = mmsys_dvfs_probe,
	.remove = mmsys_dvfs_remove,
	.driver = {
			.name = "mmsys-dvfs",
			.of_match_table = mmsys_dvfs_of_match,
	},
};

static struct devfreq_dev_profile mmsys_dvfs_profile = {
	.polling_ms = 200,
	.target = mmsys_dvfs_target,
	.get_dev_status = mmsys_dvfs_get_dev_status,
	.get_cur_freq = mmsys_dvfs_get_cur_freq,
};

static int mmsys_dvfs_target(struct device *dev, unsigned long *freq,
			u32 flags)
{
	/* TODO for sw dvfs  & sys dvfs */
	return MM_DVFS_SUCCESS;
}

int mmsys_dvfs_get_dev_status(struct device *dev,
	struct devfreq_dev_status *stat)
{
	/* TODO for sw dvfs  & sys dvfs */
	pr_info("devfreq_dev_profile-->get_dev_status\n");
	return MM_DVFS_SUCCESS;
}

static int mmsys_dvfs_get_cur_freq(struct device *dev, unsigned long *freq)
{
	/* TODO for sw dvfs  & sys dvfs */
	pr_info("devfreq_dev_profile-->get_cur_freq\n");
	return MM_DVFS_SUCCESS;
}

static int mmsys_dvfs_notify_callback(struct notifier_block *nb,
			unsigned long type, void *data)
{
	struct mmsys_dvfs *mmsys = container_of(nb, struct mmsys_dvfs, pw_nb);

	pr_info("%s: %lu\n", __func__, type);
	switch (type) {

	case MMSYS_POWER_ON:
		if (mmsys->dvfs_ops != NULL && mmsys->dvfs_ops->power_on_nb != NULL)
			mmsys->dvfs_ops->power_on_nb(mmsys->devfreq);
		break;

	case MMSYS_POWER_OFF:
		if (mmsys->dvfs_ops != NULL && mmsys->dvfs_ops->power_off_nb != NULL)
			mmsys->dvfs_ops->power_off_nb(mmsys->devfreq);
		break;

	default:
		return -EINVAL;
	}

	return NOTIFY_OK;
}

static int mmsys_hw_dvfs_enable(u32 on)
{
	if (on)
		regmap_update_bits(g_mmreg_map.mmdvfs_top_regmap,
			REG_TOP_DVFS_APB_SUBSYS_SW_DVFS_EN_CFG,
			BIT_MM_SYS_SW_DVFS_EN, BIT_MM_SYS_SW_DVFS_EN);
	else
		regmap_update_bits(g_mmreg_map.mmdvfs_top_regmap,
			REG_TOP_DVFS_APB_SUBSYS_SW_DVFS_EN_CFG,
			BIT_MM_SYS_SW_DVFS_EN, 0);

	return MM_DVFS_SUCCESS;
}

u32 soc_ver_id_check(void)
{
	int ret;
	u32 ver_id;

	ret = sprd_get_soc_id(AON_VER_ID, &ver_id, 1);
	if (ret) {
		pr_err("fail to get soc id\n");
		return -ENOMEM;
	}
	if (ver_id == 0)
		pr_info("soc is AA\n");
	else if (ver_id == 1)
		pr_info("soc is AB\n");
	else {
		pr_info("unkowned soc\n");
		ver_id = 0;
	}

	return ver_id;
}

static int mmsys_dvfs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *top_node = NULL;
	struct mmsys_dvfs *mmsys;
	void __iomem *reg_base = NULL;
	struct resource reg_res = {0};
	int ret = MM_DVFS_SUCCESS;

	pr_info("mmsys-dvfs initialized\n");

	mmsys = devm_kzalloc(dev, sizeof(*mmsys), GFP_KERNEL);
	if (!mmsys)
		return -ENOMEM;

	mutex_init(&mmsys->lock);

#ifdef SUPPORT_SWDVFS

	mmsys->clk_mmsys_core = devm_clk_get(dev, "clk_mmsys_core");
	if (IS_ERR(mmsys->clk_mmsys_core)) {
		dev_err(dev, "Cannot get the clk_mmsys_core clk\n");
	};
#endif

	if (of_property_read_u32(np, "sprd,dvfs-sys-sw-dvfs-en",
		&mmsys->mmsys_dvfs_para.sys_sw_dvfs_en)){
		pr_err("np: the value of the of_property_read_u32\n");
	}
	if (of_property_read_u32(np, "sprd,dvfs-sys-dvfs-hold-en",
		&mmsys->mmsys_dvfs_para.sys_dvfs_hold_en)){
		pr_err("np: the value of the of_property_read_u32\n");
	}
	if (of_property_read_u32(np, "sprd,dvfs-sys-dvfs-clk-gate-ctrl",
		&mmsys->mmsys_dvfs_para.sys_dvfs_clk_gate_ctrl)){
		pr_err("np: the value of the of_property_read_u32\n");
	}
	if (of_property_read_u32(np, "sprd,dvfs-sys-dvfs-wait_window",
		&mmsys->mmsys_dvfs_para.sys_dvfs_wait_window)){
		pr_err("np: the value of the of_property_read_u32\n");
	}
	if (of_property_read_u32(np, "sprd,dvfs-sys-dvfs-min_volt",
		&mmsys->mmsys_dvfs_para.sys_dvfs_min_volt)){
		pr_err("np: the value of the of_property_read_u32\n");
	}

	//mmsys->top_apb = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-topapb");
#ifdef DVFS_VERSION_R1P0
	top_node = of_parse_phandle(np, "sprd,topdvfs_controller", 0);
#else
	top_node = of_parse_phandle(np, "sprd,syscon-topapb", 0);
#endif
	if (!top_node) {
		pr_err("Failed to find top node\n");
		ret = -EINVAL;
		goto err;
	}

	g_mmreg_map.mmdvfs_top_regmap = syscon_node_to_regmap(top_node);

	if (IS_ERR(g_mmreg_map.mmdvfs_top_regmap)) {
		pr_err("No regmap for syscon mmdvfs_top_regmap\n");
		ret = -ENODEV;
		goto err;
	}

	reg_res.start = REGS_MM_DVFS_AHB_START;
	reg_res.end = REGS_MM_DVFS_AHB_END;
	reg_base = ioremap(reg_res.start, reg_res.end - reg_res.start + 1);
	if (!reg_base) {
		pr_err("fail to map mmsys dvfs ahb reg base\n");
		goto err_iounmap;
	}
	g_mmreg_map.mmdvfs_ahb_regbase = (unsigned long)reg_base;

	reg_res.start = REGS_MM_AHB_REG_START;
	reg_res.end = REGS_MM_AHB_REG_END;
	reg_base = ioremap(reg_res.start, reg_res.end - reg_res.start + 1);
	if (!reg_base) {
		pr_err("fail to map mm_ahb reg base\n");
		goto err_iounmap;
	}
	g_mmreg_map.mm_ahb_regbase = (unsigned long)reg_base;

#if (KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE)
		ret = dev_pm_opp_of_add_table(dev);
		if (ret) {
			dev_err(dev, "mmsys_dvfs: Invalid operating-points in device tree.\n");
			goto err;
		}
#endif

	platform_set_drvdata(pdev, mmsys);
	mmsys->devfreq = devm_devfreq_add_device(dev, &mmsys_dvfs_profile, "mmsys_dvfs", NULL);
	if (IS_ERR(mmsys->devfreq)) {
		dev_err(dev, "failed to add devfreq dev with mmsys-dvfs governor\n");
		ret = PTR_ERR(mmsys->devfreq);
		goto err;
	}

	mmsys->pw_nb.priority = 0;
	mmsys->pw_nb.notifier_call = mmsys_dvfs_notify_callback;
	ret = mmsys_register_notifier(&mmsys->pw_nb);

	//hw dvfs en for mmsys
#if defined(DVFS_VERSION_N6P)
	pr_info("device is qogirn6pro,check aa or ab\n");
	if (soc_ver_id_check())
		ret = mmsys_hw_dvfs_enable(MM_SW_DVFS_DISABLE);
#else
	ret = mmsys_hw_dvfs_enable(MM_SW_DVFS_DISABLE);
#endif

	return MM_DVFS_SUCCESS;

err_iounmap:
err:
	if (top_node)
		of_node_put(top_node);
	return ret;
}

static int mmsys_dvfs_power(struct notifier_block *self, unsigned long event,
			void *ptr)
{
	int ret = MM_DVFS_SUCCESS;

	pr_info("%s:on=%ld begin", __func__, event);
	if (event == _E_PW_ON)
		mmsys_notify_call_chain(MMSYS_POWER_ON);
	else
		mmsys_notify_call_chain(MMSYS_POWER_OFF);

	return ret;
}

static struct notifier_block dvfs_power_notifier = {
	.notifier_call = mmsys_dvfs_power,
};

static int mmsys_dvfs_remove(struct platform_device *pdev)
{
	pr_info("%s: success", __func__);
	return MM_DVFS_SUCCESS;
}

#ifndef KO_DEFINE

int mmsys_dvfs_init(void)
{
	int ret = MM_DVFS_SUCCESS;

	ret = register_ip_dvfs_ops(&mmsys_dvfs_ops);
	if (ret) {
		pr_err("%s: failed to add ops: %d\n", __func__, ret);
		return ret;
	}

	ret = sprd_mm_pw_notify_register(&dvfs_power_notifier);
	if (ret) {
		pr_err("%s: failed to add dvfs_power: %d\n", __func__, ret);
		return ret;
	}

	ret = devfreq_add_governor(&mmsys_dvfs_gov);
	if (ret) {
		pr_err("%s: failed to add governor: %d\n", __func__, ret);
		return ret;
	}

	ret = platform_driver_register(&mmsys_dvfs_driver);

	if (ret)
		devfreq_remove_governor(&mmsys_dvfs_gov);

	return ret;
}

void mmsys_dvfs_exit(void)
{
	int ret = MM_DVFS_SUCCESS;

	platform_driver_unregister(&mmsys_dvfs_driver);

	ret = devfreq_remove_governor(&mmsys_dvfs_gov);
	if (ret)
		pr_err("%s: failed to remove governor: %d\n", __func__, ret);

	ret = unregister_ip_dvfs_ops(&mmsys_dvfs_ops);
	if (ret)
		pr_err("%s: failed to remove ops: %d\n", __func__, ret);
	ret = sprd_mm_pw_notify_unregister((&dvfs_power_notifier));
	if (ret)
		pr_err("%s: failed to remove mm_dvfs_power: %d\n", __func__, ret);
}

#else

int __init mmsys_dvfs_init(void)
{
	int ret = MM_DVFS_SUCCESS;

	ret = register_ip_dvfs_ops(&mmsys_dvfs_ops);
	if (ret) {
		pr_err("%s: failed to add ops: %d\n", __func__, ret);
		return ret;
	}

	ret = devfreq_add_governor(&mmsys_dvfs_gov);
	if (ret) {
		pr_err("%s: failed to add governor: %d\n", __func__, ret);
		return ret;
	}

	ret = platform_driver_register(&mmsys_dvfs_driver);

	if (ret)
		devfreq_remove_governor(&mmsys_dvfs_gov);

	return ret;
}

void __exit mmsys_dvfs_exit(void)
{
	int ret = MM_DVFS_SUCCESS;

	platform_driver_unregister(&mmsys_dvfs_driver);

	ret = devfreq_remove_governor(&mmsys_dvfs_gov);
	if (ret)
		pr_err("%s: failed to remove governor: %d\n", __func__, ret);

	ret = unregister_ip_dvfs_ops(&mmsys_dvfs_ops);
	if (ret)
		pr_err("%s: failed to remove ops: %d\n", __func__, ret);
}

#endif

#ifndef KO_DEFINE

int __init mmsys_module_init(void)
{
	int ret = MM_DVFS_SUCCESS;

	pr_info("enter mmsys_moudule_init\n");

	ret = mmsys_dvfs_init();
	ret = cpp_dvfs_init();
	ret = isp_dvfs_init();
	ret = dcam0_1_dvfs_init();
	ret = dcam0_1_axi_dvfs_init();
	ret = mm_mtx_dvfs_init();

#if defined(DVFS_VERSION_N6L) || defined(DVFS_VERSION_N6P)
	ret = dcam2_3_dvfs_init();
	ret = dcam2_3_axi_dvfs_init();
	ret = dcam_mtx_dvfs_init();
#endif

#if defined(DVFS_VERSION_N6P)
	ret = depth_dvfs_init();
	ret = vdsp_dvfs_init();
	ret = vdma_dvfs_init();
	ret = vdsp_mtx_dvfs_init();
#endif

#if defined(DVFS_VERSION_N6P) || defined(DVFS_VERSION_R1P0)
	ret = fd_dvfs_init();
#endif

#if defined(DVFS_VERSION_N6L) || defined(DVFS_VERSION_N6P) || defined(DVFS_VERSION_R1P0)
	ret = jpg_dvfs_init();
#endif

	pr_info("end mmsys_moudule_init\n");

	return ret;
}

void __exit mmsys_module_exit(void)
{
	pr_info("enter mmsys_moudule_exit\n");

	mmsys_dvfs_exit();
	cpp_dvfs_exit();
	isp_dvfs_exit();
	dcam0_1_dvfs_exit();
	dcam0_1_axi_dvfs_exit();
	mm_mtx_dvfs_exit();

#if defined(DVFS_VERSION_N6L) || defined(DVFS_VERSION_N6P)
	dcam2_3_dvfs_exit();
	dcam2_3_axi_dvfs_exit();
	dcam_mtx_dvfs_exit();
#endif

#if defined(DVFS_VERSION_N6P)
	depth_dvfs_exit();
	vdsp_dvfs_exit();
	vdma_dvfs_exit();
	vdsp_mtx_dvfs_exit();
#endif

#if defined(DVFS_VERSION_N6P) || defined(DVFS_VERSION_R1P0)
	fd_dvfs_exit();
#endif

#if defined(DVFS_VERSION_N6L) || defined(DVFS_VERSION_N6P) || defined(DVFS_VERSION_R1P0)
	jpg_dvfs_exit();
#endif

	pr_info("end mmsys_moudule_exit\n");
}

module_init(mmsys_module_init);
module_exit(mmsys_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sprd mmsys devfreq driver");
MODULE_AUTHOR("Multimedia_Camera@UNISOC");

#endif
