/*
 * Copyright (C) 2021-2022 UNISOC Communications Inc.
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

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/version.h>
#include <asm/cacheflush.h>
#include <linux/notifier.h>

#include <linux/mfd/syscon.h>
#include <linux/pm_domain.h>
#include <os_adapt_common.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <linux/pm_runtime.h>
#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
#include <linux/ion.h>
#endif
#else
#include <video/sprd_mmsys_pw_domain.h>
#include <video/sprd_mmsys_pw_domain_qogirn6pro.h>
#endif

#include "sprd_camsys_domain.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd_campd_qogirn6pro: %d %d %s : "\
	fmt, current->pid, __LINE__, __func__

#define PD_MM_DOWN_FLAG            0x7
#define PD_MM_DOWN_BIT             24
#define PD_MM_STATUS_SHIFT_BIT     24

#define PD_ISP_STATUS_SHIFT_BIT    16
#define PD_DCAM_STATUS_SHIFT_BIT   0

static const char * const syscon_name[] = {
	"force_shutdown",
	"shutdown_en",
	"camera_power_state",
	"isp_force_shutdown",
	"isp_shutdown_en",
	"isp_power_state",
	"dcam_force_shutdown",
	"dcam_shutdown_en",
	"dcam_power_state",
	"cam_soft_rst_sel",
	"aon_apb_mm_eb"
};

enum {
	CAMSYS_FORCE_SHUTDOWN = 0,
	CAMSYS_SHUTDOWN_EN,
	CAMSYS_PWR_STATUS,
	CAMSYS_ISP_FORCE_SHUTDOWN,
	CAMSYS_ISP_SHUTDOWN_EN,
	CAMSYS_ISP_STATUS,
	CAMSYS_DCAM_FORCE_SHUTDOWN,
	CAMSYS_DCAM_SHUTDOWN_EN,
	CAMSYS_DCAM_STATUS,
	CAMSYS_SOFT_RST_SEL,
	CAMSYS_MM_EB,
};

struct camsys_power_info;
static BLOCKING_NOTIFIER_HEAD(mmsys_chain);

static void regmap_update_bits_mmsys(struct register_gpr *p, uint32_t val)
{
	if ((!p) || (!(p->gpr)))
		return;

	regmap_update_bits(p->gpr, p->reg, p->mask, val);
}

static int regmap_read_mmsys(struct register_gpr *p, uint32_t *val)
{
	int ret = 0;

	if ((!p) || (!(p->gpr)) || (!val))
		return -1;
	ret = regmap_read(p->gpr, p->reg, val);
	if (!ret)
		*val &= (uint32_t)p->mask;

	return ret;
}

static int sprd_cam_domain_eb(struct camsys_power_info *pw_info)
{
	int ret = 0;

	pr_info("cb %pS\n", __builtin_return_address(0));

	/* config cam emc clk */
	clk_prepare_enable(pw_info->u.qogirn6pro.mm_eb);
	clk_prepare_enable(pw_info->u.qogirn6pro.ckg_en);
	clk_prepare_enable(pw_info->u.qogirn6pro.sys_cfg_mtx_busmon_en);
	clk_prepare_enable(pw_info->u.qogirn6pro.sys_mst_busmon_en);
	clk_prepare_enable(pw_info->u.qogirn6pro.mm_mtx_data_en);
	clk_set_parent(pw_info->u.qogirn6pro.mm_mtx_clk, pw_info->u.qogirn6pro.mm_mtx_clk_parent);
	clk_prepare_enable(pw_info->u.qogirn6pro.mm_mtx_clk);
	ret = clk_prepare_enable(pw_info->u.qogirn6pro.blk_cfg_en);
	if (ret)
		pr_err("fail to power on blk_cfg_en");

	/* change register read from register */
	regcache_cache_only(pw_info->u.qogirn6pro.switch_map, false);
	regcache_cache_only(pw_info->u.qogirn6pro.ahb_map, false);
	regcache_cache_only(pw_info->u.qogirn6pro.anlg_phy_g4_controller, false);
	regcache_cache_only(pw_info->u.qogirn6pro.anlg_phy_g4l_controller, false);
	udelay(200);

	return 0;
}

static int sprd_cam_domain_disable(struct camsys_power_info *pw_info)
{
	pr_info("cb %pS\n", __builtin_return_address(0));

	/* change register read from cache */
	regcache_cache_only(pw_info->u.qogirn6pro.switch_map, true);
	regcache_cache_only(pw_info->u.qogirn6pro.ahb_map, true);
	regcache_cache_only(pw_info->u.qogirn6pro.anlg_phy_g4_controller, true);
	regcache_cache_only(pw_info->u.qogirn6pro.anlg_phy_g4l_controller, true);
	udelay(200);

	/* mm bus enable */
	clk_disable_unprepare(pw_info->u.qogirn6pro.blk_cfg_en);
	clk_set_parent(pw_info->u.qogirn6pro.mm_mtx_clk, pw_info->u.qogirn6pro.mm_mtx_clk_defalut);
	clk_disable_unprepare(pw_info->u.qogirn6pro.mm_mtx_clk);
	clk_disable_unprepare(pw_info->u.qogirn6pro.mm_mtx_data_en);
	clk_disable_unprepare(pw_info->u.qogirn6pro.sys_mst_busmon_en);
	clk_disable_unprepare(pw_info->u.qogirn6pro.sys_cfg_mtx_busmon_en);
	clk_disable_unprepare(pw_info->u.qogirn6pro.ckg_en);
	clk_disable_unprepare(pw_info->u.qogirn6pro.mm_eb);

	return 0;
}

static int sprd_cam_pw_off(struct camsys_power_info *pw_info)
{
	int ret = 0;
	unsigned int power_state1 = 0;
	unsigned int power_state2 = 0;
	unsigned int power_state3 = 0;
	unsigned int read_count = 0;

	os_adapt_time_usleep_range(300, 350);

	/* 1:auto shutdown en, shutdown with ap; 0: control by b25 */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_DCAM_SHUTDOWN_EN], 0);
	/* set 1 to shutdown */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_DCAM_FORCE_SHUTDOWN], ~((uint32_t)0));

	do {
		os_adapt_time_usleep_range(300, 350);
		read_count++;

		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_DCAM_STATUS],
			&power_state1);
		if (ret)
			pr_err("fail to power off dcam , ret %d, read count %d\n", ret, read_count);
		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_DCAM_STATUS],
			&power_state2);
		if (ret)
			pr_err("fail to power off dcam , ret %d, read count %d\n", ret, read_count);
		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_DCAM_STATUS],
			&power_state3);
		if (ret)
			pr_err("fail to power off dcam , ret %d, read count %d\n", ret, read_count);
	} while (((power_state1 !=
		(PD_MM_DOWN_FLAG)) &&
		read_count < 30) ||
		(power_state1 != power_state2) ||
		(power_state2 != power_state3));

	if (power_state1 != PD_MM_DOWN_FLAG) {
		pr_err("fail to get dcam power state 0x%x\n", power_state1);
		ret = -1;
		goto err_pw_off;
	}

	/* 1:auto shutdown en, shutdown with ap; 0: control by b25 */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_ISP_SHUTDOWN_EN], 0);
	/* set 1 to shutdown */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_ISP_FORCE_SHUTDOWN], ~((uint32_t)0));

	do {
		os_adapt_time_usleep_range(300, 350);
		read_count++;

		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_ISP_STATUS],
			&power_state1);
		if (ret)
			pr_err("fail to power off isp , ret %d, read count %d\n", ret, read_count);
		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_ISP_STATUS],
			&power_state2);
		if (ret)
			pr_err("fail to power off isp , ret %d, read count %d\n", ret, read_count);
		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_ISP_STATUS],
			&power_state3);
		if (ret)
			pr_err("fail to power off isp , ret %d, read count %d\n", ret, read_count);
	} while (((power_state1 !=
		(PD_MM_DOWN_FLAG << PD_ISP_STATUS_SHIFT_BIT)) &&
		read_count < 30) ||
		(power_state1 != power_state2) ||
		(power_state2 != power_state3));

	if (power_state1 != (PD_MM_DOWN_FLAG << PD_ISP_STATUS_SHIFT_BIT)) {
		pr_err("fail to get isp power state 0x%x\n", power_state1);
		ret = -1;
		goto err_pw_off;
	}

	/* 1:auto shutdown en, shutdown with ap; 0: control by b25 */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_SHUTDOWN_EN], 0);
	/* set 1 to shutdown */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_FORCE_SHUTDOWN], ~((uint32_t)0));

	do {
		os_adapt_time_usleep_range(300, 350);
		read_count++;

		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_PWR_STATUS],
			&power_state1);
		if (ret)
			pr_err("fail to power off cam sys, ret %d, read count %d\n", ret, read_count);
		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_PWR_STATUS],
			&power_state2);
		if (ret)
			pr_err("fail to power off cam sys, ret %d, read count %d\n", ret, read_count);
		ret = regmap_read_mmsys(
			&pw_info->u.qogirn6pro.regs[CAMSYS_PWR_STATUS],
			&power_state3);
		if (ret)
			pr_err("fail to power off cam sys, ret %d, read count %d\n", ret, read_count);
	} while (((power_state1 !=
		(PD_MM_DOWN_FLAG << PD_MM_DOWN_BIT)) &&
		read_count < 30) ||
		(power_state1 != power_state2) ||
		(power_state2 != power_state3));

	if (power_state1 != (PD_MM_DOWN_FLAG << PD_MM_DOWN_BIT)) {
		pr_err("fail to get power state 0x%x\n", power_state1);
		ret = -1;
		goto err_pw_off;
	}

	pr_info("Done, cb: %pS\n", __builtin_return_address(0));
	return 0;

err_pw_off:
	pr_err("fail to power off cam sys, ret %d, read count %d\n", ret, read_count);
	return ret;
}

static int sprd_cam_pw_on(struct camsys_power_info *pw_info)
{
	int ret = 0;
	unsigned int power_state = 0;
	unsigned int cnt = 0, i = 0, j = 0, k = 0, l = 0;

	pr_info("power on state %d, cb %pS\n",
		atomic_read(&pw_info->u.qogirn6pro.users_pw),
		__builtin_return_address(0));

	/* clear force shutdown */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_SHUTDOWN_EN], 0);
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_FORCE_SHUTDOWN], 0);

	for (i = 0; i < 30; i++) {
		os_adapt_time_usleep_range(300, 350);
		cnt = 0;

		for (j = 0; j < 5; j++) {
			ret = regmap_read_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_PWR_STATUS], &power_state);
			if (ret == 0 && !(power_state))
				cnt++;
			else
				cnt = 0;
		}
		if (cnt == 5)
			break;
	}

	if (cnt < 5) {
		pr_err("fail to pw on cam domain\n");
		ret = -1;
		goto err_pw_on;
	}

	/* dcam domain power on */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_DCAM_SHUTDOWN_EN], 0);
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_DCAM_FORCE_SHUTDOWN], 0);

	for (k = 0; k < 30; k++) {
		os_adapt_time_usleep_range(300, 350);
		cnt = 0;

		for (j = 0; j < 5; j++) {
			ret = regmap_read_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_DCAM_STATUS], &power_state);
			if (ret == 0 && !(power_state))
				cnt++;
			else
				cnt = 0;
		}
		if (cnt == 5)
			break;

	}

	if (cnt < 5) {
		pr_err("fail to pw on dcam domain\n");
		ret = -1;
		goto err_pw_on;
	}
	/* isp domain power on */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_ISP_SHUTDOWN_EN], 0);
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_ISP_FORCE_SHUTDOWN], 0);

	for (l = 0; l < 30; l++) {
		os_adapt_time_usleep_range(300, 350);
		cnt = 0;

		for (j = 0; j < 5; j++) {
			ret = regmap_read_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_ISP_STATUS], &power_state);
			if (ret == 0 && !(power_state))
				cnt++;
			else
				cnt = 0;
		}
		if (cnt == 5)
			break;

	}

	if (cnt < 5) {
		pr_err("fail to pw on isp domain\n");
		ret = -1;
		goto err_pw_on;
	}

	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_SOFT_RST_SEL], 0);

	pr_info("Done, i %d k %d l %d\n", i, k, l);
	return 0;

err_pw_on:
	pr_err("fail to power on cam sys\n");
	return ret;
}

static long sprd_campw_init(struct platform_device *pdev, struct camsys_power_info *pw_info)
{
	int i, ret = 0;
	struct device_node *np = pdev->dev.of_node;
	const char *pname = NULL;
	struct regmap *tregmap = NULL;
	uint32_t args[2] = {0};

	pw_info->u.qogirn6pro.mm_eb = of_clk_get_by_name(np, "clk_mm_eb");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.mm_eb))
		return PTR_ERR(pw_info->u.qogirn6pro.mm_eb);

	pw_info->u.qogirn6pro.ckg_en = of_clk_get_by_name(np, "clk_ckg_en");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.ckg_en))
		return PTR_ERR(pw_info->u.qogirn6pro.ckg_en);

	pw_info->u.qogirn6pro.mm_mtx_data_en = of_clk_get_by_name(np, "clk_mm_mtx_data_en");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.mm_mtx_data_en))
		return PTR_ERR(pw_info->u.qogirn6pro.mm_mtx_data_en);

	pw_info->u.qogirn6pro.mm_mtx_clk = of_clk_get_by_name(np, "clk_mm_mtx_data");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.mm_mtx_clk))
		return PTR_ERR(pw_info->u.qogirn6pro.mm_mtx_clk);

	pw_info->u.qogirn6pro.blk_cfg_en = of_clk_get_by_name(np, "clk_blk_cfg_en");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.blk_cfg_en))
		return PTR_ERR(pw_info->u.qogirn6pro.blk_cfg_en);

	pw_info->u.qogirn6pro.sys_cfg_mtx_busmon_en = of_clk_get_by_name(np, "sys_cfg_mtx_busmon_en");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.sys_cfg_mtx_busmon_en))
		return PTR_ERR(pw_info->u.qogirn6pro.sys_cfg_mtx_busmon_en);

	pw_info->u.qogirn6pro.sys_mst_busmon_en = of_clk_get_by_name(np, "sys_mst_busmon_en");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.sys_mst_busmon_en))
		return PTR_ERR(pw_info->u.qogirn6pro.sys_mst_busmon_en);

	pw_info->u.qogirn6pro.mm_mtx_clk_defalut = of_clk_get_by_name(np, "clk_mm_mtx_defalut");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.mm_mtx_clk_defalut))
		return PTR_ERR(pw_info->u.qogirn6pro.mm_mtx_clk_defalut);

	pw_info->u.qogirn6pro.mm_mtx_clk_parent = of_clk_get_by_name(np, "clk_mm_mtx_parent");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.mm_mtx_clk_parent))
		return PTR_ERR(pw_info->u.qogirn6pro.mm_mtx_clk_parent);

	/* read csi-switch and cam-ahb-syscon registers*/
	pw_info->u.qogirn6pro.switch_map = syscon_regmap_lookup_by_phandle(np, "sprd,csi-switch");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.switch_map))
		pr_err("fail to get sprd,csi-switch\n");

	pw_info->u.qogirn6pro.ahb_map = syscon_regmap_lookup_by_phandle(np, "sprd,cam-ahb-syscon");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.ahb_map))
		pr_err("fail to get sprd,cam-ahb-syscon\n");

	/* read csi phy registers*/
	pw_info->u.qogirn6pro.anlg_phy_g4_controller =
		syscon_regmap_lookup_by_phandle(np, "sprd,anlg_phy_g4_controller");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.anlg_phy_g4_controller))
		pr_err("fail to get sprd,anlg_phy_g4_controller\n");

	pw_info->u.qogirn6pro.anlg_phy_g4l_controller =
		syscon_regmap_lookup_by_phandle(np, "sprd,anlg_phy_g4l_controller");
	if (IS_ERR_OR_NULL(pw_info->u.qogirn6pro.anlg_phy_g4l_controller))
		pr_err("fail to get sprd,anlg_phy_g4l_controller\n");

	/* read global register */
	for (i = 0; i < ARRAY_SIZE(syscon_name); i++) {
		pname = syscon_name[i];
		tregmap = syscon_regmap_lookup_by_phandle_args(np, pname, 2, args);
		if (IS_ERR_OR_NULL(tregmap)) {
			pr_err("fail to read %s regmap\n", pname);
			continue;
		}

		pw_info->u.qogirn6pro.regs[i].gpr = tregmap;
		pw_info->u.qogirn6pro.regs[i].reg = args[0];
		pw_info->u.qogirn6pro.regs[i].mask = args[1];
		pr_info("dts[%s] 0x%x 0x%x\n", pname,
			pw_info->u.qogirn6pro.regs[i].reg,
			pw_info->u.qogirn6pro.regs[i].mask);
	}

	/* calibration mode MM SHUTDOWN */
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_MM_EB], 0);
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_SHUTDOWN_EN], 0);
	regmap_update_bits_mmsys(&pw_info->u.qogirn6pro.regs[CAMSYS_FORCE_SHUTDOWN], ~((uint32_t)0));

	return ret;
}

struct camsys_power_ops camsys_power_ops_qogirn6pro = {
	.sprd_campw_init = sprd_campw_init,
	.sprd_cam_pw_on = sprd_cam_pw_on,
	.sprd_cam_pw_off = sprd_cam_pw_off,
	.sprd_cam_domain_eb = sprd_cam_domain_eb,
	.sprd_cam_domain_disable = sprd_cam_domain_disable,
};
