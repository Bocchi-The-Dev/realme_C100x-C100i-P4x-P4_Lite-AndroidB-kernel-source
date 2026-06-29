/*
*SPDX-FileCopyrightText:2023 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier:GPL-2.0-only
*/

#include "vpu_drv.h"
#include "sprd_vpu.h"

static int get_eb_clk(struct vpu_platform_data *data, struct device_node *np)
{
	int ret = 0;
	struct clk *clk_dev_eb;
	struct clk *core_clk;
	struct clk *clk_domain_eb;
	struct clk *clk_ckg_eb;
	struct clk *clk_ahb_vsp;
	struct device *dev = data->dev;
	struct clk *clk_parent;

	core_clk = devm_clk_get(data->dev, "clk_vsp");

	if (IS_ERR_OR_NULL(core_clk)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n", "core_clk",
		       core_clk);
		ret = -EINVAL;
		data->clk.core_clk = NULL;
		goto errout;
	} else
		data->clk.core_clk = core_clk;

	clk_domain_eb = devm_clk_get(data->dev, "clk_domain_eb");

	if (IS_ERR_OR_NULL(clk_domain_eb)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n",
		       "clk_domain_eb", clk_domain_eb);
		data->clk.clk_domain_eb = NULL;
		ret = -EINVAL;
		goto errout;
	} else
		data->clk.clk_domain_eb = clk_domain_eb;

	clk_dev_eb =
		devm_clk_get(data->dev, "clk_dev_eb");

	if (IS_ERR_OR_NULL(clk_dev_eb)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n",
		       "clk_dev_eb", clk_dev_eb);
		ret = -EINVAL;
		data->clk.clk_dev_eb = NULL;
		goto errout;
	} else
		data->clk.clk_dev_eb = clk_dev_eb;

	clk_ahb_vsp =
		devm_clk_get(data->dev, "clk_ahb_vsp");

	if (IS_ERR_OR_NULL(clk_ahb_vsp)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n",
		       "clk_ahb_vsp", clk_ahb_vsp);
		ret = -EINVAL;
		goto errout;
	} else
		data->clk.clk_ahb_vsp = clk_ahb_vsp;

	clk_ckg_eb =
		devm_clk_get(data->dev, "clk_ckg_eb");

	if (IS_ERR_OR_NULL(clk_ckg_eb)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n",
		       "clk_ckg_eb", clk_ckg_eb);
		ret = -EINVAL;
		goto errout;
	} else
		data->clk.clk_ckg_eb = clk_ckg_eb;

	clk_parent = devm_clk_get(data->dev,
		       "clk_ahb_vsp_parent");

	if (IS_ERR_OR_NULL(clk_parent)) {
		dev_err(dev, "clock[%s]: failed to get parent in probe!\n",
		       "clk_ahb_vsp_parent");
		ret = -EINVAL;
		goto errout;
	} else
		data->clk.ahb_parent_clk = clk_parent;

errout:
	return ret;
}

static int clock_enable(struct vpu_platform_data *data)
{
	int ret = 0;
	struct vpu_clk *clk = &data->clk;
	struct device *dev = data->dev;

	if (clk->clk_domain_eb) {
		ret = clk_prepare_enable(clk->clk_domain_eb);
		if (ret) {
			dev_err(dev, "vsp clk_domain_eb: clk_enable failed!\n");
			goto error1;
		}
		dev_dbg(dev, "vsp clk_domain_eb: clk_prepare_enable ok.\n");
	}

	if (clk->clk_dev_eb) {
		ret = clk_prepare_enable(clk->clk_dev_eb);
		if (ret) {
			dev_err(dev, "clk_dev_eb: clk_prepare_enable failed!\n");
			goto error2;
		}
		dev_dbg(dev, "clk_dev_eb: clk_prepare_enable ok.\n");
	}

	if (clk->clk_ahb_vsp) {
		ret = clk_set_parent(clk->clk_ahb_vsp, clk->ahb_parent_clk);
		if (ret) {
			dev_err(dev, "clock[%s]: clk_set_parent() failed!",
				"ahb_parent_clk");
			goto error3;
		}
		ret = clk_prepare_enable(clk->clk_ahb_vsp);
		if (ret) {
			dev_err(dev, "clk_ahb_vsp: clk_prepare_enable failed!\n");
			goto error3;
		}
		dev_dbg(dev, "clk_ahb_vsp: clk_prepare_enable ok.\n");
	}

	ret = clk_set_parent(clk->core_clk, clk->core_parent_clk);
	if (ret) {
		dev_err(dev, "clock[%s]: clk_set_parent() failed!", "clk_core");
		goto error4;
	}

	ret = clk_prepare_enable(clk->core_clk);
	if (ret) {
		dev_err(dev, "core_clk: clk_prepare_enable failed!\n");
		goto error4;
	}
	dev_dbg(dev, "vsp_clk: clk_prepare_enable ok.\n");

	dev_dbg(data->dev, "%s %d,OK\n", __func__, __LINE__);


	return ret;

error4:
	if (clk->clk_ahb_vsp) {
		clk_disable_unprepare(clk->clk_ahb_vsp);
	}
error3:
	if (clk->clk_dev_eb) {
		clk_disable_unprepare(clk->clk_dev_eb);
	}
error2:
	if (clk->clk_domain_eb) {
		clk_disable_unprepare(clk->clk_domain_eb);
	}
error1:
	return ret;
}

static void clock_disable(struct vpu_platform_data *data)
{
	struct vpu_clk *clk = &data->clk;

	clk_disable_unprepare(clk->core_clk);
	clk_disable_unprepare(clk->clk_ahb_vsp);
	clk_disable_unprepare(clk->clk_dev_eb);
	clk_disable_unprepare(clk->clk_domain_eb);
	dev_dbg(data->dev, "%s %d,OK\n", __func__, __LINE__);
}

static void check_pw_status(struct vpu_platform_data *data)
{
	int ret = 0;
	u32 dpu_vsp_eb = 0;
	u32 dpu_vsp_apb_regs = 0;

	regmap_read(data->regs[VPU_DOMAIN_EB].gpr,
			data->regs[VPU_DOMAIN_EB].reg, &dpu_vsp_eb);

	/*aon_apb regs BIT(21) DPU_VSP_EB*/
	if ((dpu_vsp_eb & data->regs[VPU_DOMAIN_EB].mask) !=
			data->regs[VPU_DOMAIN_EB].mask) {
		dev_err(data->dev, "dpu_vsp_eb 0x%x\n", dpu_vsp_eb);
		ret = regmap_update_bits(data->regs[VPU_DOMAIN_EB].gpr,
					data->regs[VPU_DOMAIN_EB].reg,
					data->regs[VPU_DOMAIN_EB].mask,
					data->regs[VPU_DOMAIN_EB].mask);
	}

	/*
	 * dpu_vsp_apb_regs 0x30100000
	 * APB_EB(dev_eb) 0x0000 bit3:enc0_eb bit4:enc1_eb bit 5:dec_eb
	 * APB_RST 0x0004 bit3:enc0_rst bit4:enc1_rst bit 5:dec_rst
	 */
	regmap_read(data->regs[RESET].gpr, 0x0, &dpu_vsp_apb_regs); /*dev_eb*/

	if ((dpu_vsp_apb_regs & data->p_data->dev_eb_mask) !=
			data->p_data->dev_eb_mask) {
		dev_err(data->dev, "dpu_vsp_apb_regs APB_EB dev_eb 0x%x\n", dpu_vsp_apb_regs);
		ret = regmap_update_bits(data->regs[RESET].gpr, 0x0,
					data->p_data->dev_eb_mask, data->p_data->dev_eb_mask);
	}

}

const struct vpu_ops vpu_r1p0 = {
	.get_eb_clk = get_eb_clk,
	.clock_enable = clock_enable,
	.clock_disable = clock_disable,
	.check_pw_status = check_pw_status,
	.get_reset_mask = get_reset_mask,
};
/* for QOGIRN6PRO and QOGIRN6L */

