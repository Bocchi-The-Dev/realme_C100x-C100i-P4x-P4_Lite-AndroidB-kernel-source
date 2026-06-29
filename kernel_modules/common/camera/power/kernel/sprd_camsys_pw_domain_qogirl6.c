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
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/pm_domain.h>
#include <os_adapt_common.h>

#include "sprd_camsys_domain.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd_campd_r8p0: %d %d %s : "\
	fmt, current->pid, __LINE__, __func__

#define PD_MM_DOWN_FLAG    0x7
#define SHIFT_MASK(a)      (ffs(a) ? ffs(a) - 1 : 0)


static const char * const syscon_name[] = {
	"force-shutdown",
	"shutdown-en", /* clear */
	"power-state", /* on: 0; off:7 */
	"cam-soft-rst-sel",
	"aon-apb-mm-eb"
};

enum {
	FORCE_SHUTDOWN = 0,
	SHUTDOWN_EN,
	PWR_STATUS0,
	CAMSYS_SOFT_RST_SEL,
	CAMSYS_MM_EB,
};

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
	pr_info("cb %pS\n", __builtin_return_address(0));

	/* mm bus enable */
	clk_prepare_enable(pw_info->u.qogirl6.cam_mm_eb);
	clk_prepare_enable(pw_info->u.qogirl6.cam_mm_ahb_eb);
	/* config cam ahb clk */
	clk_set_parent(pw_info->u.qogirl6.cam_ahb_clk,
		pw_info->u.qogirl6.cam_ahb_clk_parent);
	clk_prepare_enable(pw_info->u.qogirl6.cam_ahb_clk);

	/* config cam mtx clk */
	clk_set_parent(pw_info->u.qogirl6.cam_mtx_clk,
		pw_info->u.qogirl6.cam_mtx_clk_parent);
	clk_prepare_enable(pw_info->u.qogirl6.cam_mtx_clk);

	clk_prepare_enable(pw_info->u.qogirl6.isppll_clk);

	/* change register read from register */
	regcache_cache_only(pw_info->u.qogirl6.ahb_map, false);
	regcache_cache_only(pw_info->u.qogirl6.anlg_phy_g10_controller, false);
	udelay(200);

	return 0;
}

static int sprd_cam_domain_disable(struct camsys_power_info *pw_info)
{
	pr_info("cb %pS\n", __builtin_return_address(0));

	/* change register read from cache */
	regcache_cache_only(pw_info->u.qogirl6.ahb_map, true);
	regcache_cache_only(pw_info->u.qogirl6.anlg_phy_g10_controller, true);
	udelay(200);

	clk_disable_unprepare(pw_info->u.qogirl6.isppll_clk);

	clk_set_parent(pw_info->u.qogirl6.cam_ahb_clk,
		pw_info->u.qogirl6.cam_ahb_clk_default);
	clk_disable_unprepare(pw_info->u.qogirl6.cam_ahb_clk);

	clk_set_parent(pw_info->u.qogirl6.cam_mtx_clk,
		pw_info->u.qogirl6.cam_mtx_clk_default);
	clk_disable_unprepare(pw_info->u.qogirl6.cam_mtx_clk);

	clk_disable_unprepare(pw_info->u.qogirl6.cam_mm_ahb_eb);
	clk_disable_unprepare(pw_info->u.qogirl6.cam_mm_eb);
	return 0;
}

static int sprd_cam_pw_off(struct camsys_power_info *pw_info)
{
	int ret = 0;
	unsigned int power_state = 0;
	unsigned int read_count = 0;
	int shift = 0;

	/* 1:auto shutdown en, shutdown with ap; 0: control by b25 */
	regmap_update_bits_mmsys(&pw_info->u.qogirl6.regs[SHUTDOWN_EN], 0);
	/* set 1 to shutdown */
	regmap_update_bits_mmsys(&pw_info->u.qogirl6.regs[FORCE_SHUTDOWN], ~((uint32_t)0));
	/* shift for power off status bits */
	if (pw_info->u.qogirl6.regs[PWR_STATUS0].gpr != NULL)
		shift = SHIFT_MASK(pw_info->u.qogirl6.regs[PWR_STATUS0].mask);
	do {
		os_adapt_time_usleep_range(300, 350);
		read_count++;

		ret = regmap_read_mmsys(&pw_info->u.qogirl6.regs[PWR_STATUS0],
				&power_state);
		if (ret)
			goto err_pw_off;
	} while (((power_state != (PD_MM_DOWN_FLAG << shift)) &&
		(read_count < 30)));
	if (power_state != (PD_MM_DOWN_FLAG << shift)) {
		pr_err("fail to get power_state : 0x%x\n", power_state);
		ret = -EIO;
		return ret;
	}
	/* if count != 0, other using */
	pr_info("Done, read count %d, cb: %pS\n",
		read_count, __builtin_return_address(0));
	return ret;

err_pw_off:
	pr_err("fail to pw off. ret: %d, count: %d, cb: %pS\n", ret, read_count,
		__builtin_return_address(0));

	return ret;
}

static int sprd_cam_pw_on(struct camsys_power_info *pw_info)
{
	int ret = 0;
	unsigned int power_state = 0;
	unsigned int cnt = 0, i = 0, j = 0;

	/* clear force shutdown */
	regmap_update_bits_mmsys(&pw_info->u.qogirl6.regs[FORCE_SHUTDOWN], 0);
	/* power on */
	regmap_update_bits_mmsys(&pw_info->u.qogirl6.regs[SHUTDOWN_EN], 0);

	for (i = 0; i < 30; i++) {
		os_adapt_time_usleep_range(300, 350);
		cnt = 0;

		for (j = 0; j < 5; j++) {
			ret = regmap_read_mmsys(&pw_info->u.qogirl6.regs[PWR_STATUS0], &power_state);
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
		ret = -EIO;
		return ret;
	}
	/*SOFT RST 0x64020B98 bit3 is set to 0, another module regmap_write 0x64020B98,camera not kernel crash - 2670477*/
	regmap_update_bits_mmsys(&pw_info->u.qogirl6.regs[CAMSYS_SOFT_RST_SEL], 0);
	pr_info("Done, i %d\n", i);
	return ret;
}

static long sprd_campw_init(struct platform_device *pdev, struct camsys_power_info *pw_info)
{
	struct device_node *np = pdev->dev.of_node;

	const char *pname = NULL;
	struct regmap *tregmap = NULL;
	uint32_t i = 0;
	uint32_t args[2] = {0};

	if (IS_ERR_OR_NULL(pw_info)) {
		pr_err("fail to alloc pw_info\n");
		return -ENOMEM;
	}

	pw_info->u.qogirl6.cam_mm_eb =
		of_clk_get_by_name(np, "clk_mm_eb");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.cam_mm_eb))
		return PTR_ERR(pw_info->u.qogirl6.cam_mm_eb);

	pw_info->u.qogirl6.cam_mm_ahb_eb =
		of_clk_get_by_name(np, "clk_mm_ahb_eb");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.cam_mm_ahb_eb))
		return PTR_ERR(pw_info->u.qogirl6.cam_mm_ahb_eb);

	pw_info->u.qogirl6.cam_ahb_clk =
		of_clk_get_by_name(np, "clk_mm_ahb");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.cam_ahb_clk))
		return PTR_ERR(pw_info->u.qogirl6.cam_ahb_clk);

	pw_info->u.qogirl6.cam_ahb_clk_parent =
		of_clk_get_by_name(np, "clk_mm_ahb_parent");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.cam_ahb_clk_parent))
		return PTR_ERR(pw_info->u.qogirl6.cam_ahb_clk_parent);

	pw_info->u.qogirl6.cam_ahb_clk_default =
		clk_get_parent(pw_info->u.qogirl6.cam_ahb_clk);
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.cam_ahb_clk_default))
		return PTR_ERR(pw_info->u.qogirl6.cam_ahb_clk_default);

	/* need set cgm_mm_emc_sel :512m , DDR  matrix clk*/
	pw_info->u.qogirl6.cam_mtx_clk =
		of_clk_get_by_name(np, "clk_mm_mtx");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.cam_mtx_clk))
		return PTR_ERR(pw_info->u.qogirl6.cam_mtx_clk);

	pw_info->u.qogirl6.cam_mtx_clk_parent =
		of_clk_get_by_name(np, "clk_mm_mtx_parent");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.cam_mtx_clk_parent))
		return PTR_ERR(pw_info->u.qogirl6.cam_mtx_clk_parent);

	pw_info->u.qogirl6.cam_mtx_clk_default =
		clk_get_parent(pw_info->u.qogirl6.cam_mtx_clk);
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.cam_mtx_clk_default))
		return PTR_ERR(pw_info->u.qogirl6.cam_mtx_clk_default);

	pw_info->u.qogirl6.isppll_clk = of_clk_get_by_name(np, "clk_isppll");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.isppll_clk))
		return PTR_ERR(pw_info->u.qogirl6.isppll_clk);

	/* read cam-ahb-syscon register*/
	pw_info->u.qogirl6.ahb_map = syscon_regmap_lookup_by_phandle(np, "sprd,cam-ahb-syscon");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.ahb_map))
		pr_err("fail to get sprd,cam-ahb-syscon\n");

	/* read csi phy registers*/
	pw_info->u.qogirl6.anlg_phy_g10_controller =
		syscon_regmap_lookup_by_phandle(np, "sprd,anlg_phy_g10_controller");
	if (IS_ERR_OR_NULL(pw_info->u.qogirl6.anlg_phy_g10_controller))
		pr_err("fail to get sprd,anlg_phy_g10_controller\n");

	/* read global register */
	for (i = 0; i < ARRAY_SIZE(syscon_name); i++) {
		pname = syscon_name[i];
		tregmap = syscon_regmap_lookup_by_phandle_args(np, pname, 2, args);
		if (IS_ERR_OR_NULL(tregmap)) {
			pr_err("fail to read %s reg_map 0x%lx %d\n",
					pname, (uintptr_t)tregmap, IS_ERR_OR_NULL(tregmap));
			continue;
		}
		pw_info->u.qogirl6.regs[i].gpr = tregmap;
		pw_info->u.qogirl6.regs[i].reg = args[0];
		pw_info->u.qogirl6.regs[i].mask = args[1];
		pr_debug("dts[%s] 0x%x 0x%x\n", pname,
			pw_info->u.qogirl6.regs[i].reg,
			pw_info->u.qogirl6.regs[i].mask);
	}

	/* calibration mode MM SHUTDOWN */
	regmap_update_bits_mmsys(&pw_info->u.qogirl6.regs[CAMSYS_MM_EB],
		0);
	regmap_update_bits_mmsys(&pw_info->u.qogirl6.regs[SHUTDOWN_EN],
		0);
	regmap_update_bits_mmsys(&pw_info->u.qogirl6.regs[FORCE_SHUTDOWN],
		~((uint32_t)0));

	return 0;
}

struct camsys_power_ops camsys_power_ops_qogirl6 = {
	.sprd_campw_init = sprd_campw_init,
	.sprd_cam_pw_on = sprd_cam_pw_on,
	.sprd_cam_pw_off = sprd_cam_pw_off,
	.sprd_cam_domain_eb = sprd_cam_domain_eb,
	.sprd_cam_domain_disable = sprd_cam_domain_disable,
};
