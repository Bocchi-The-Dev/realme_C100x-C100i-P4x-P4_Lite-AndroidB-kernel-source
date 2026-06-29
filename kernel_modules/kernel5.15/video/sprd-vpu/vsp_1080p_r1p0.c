/*
*SPDX-FileCopyrightText:2023 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier:GPL-2.0-only
*/

#include "vpu_drv.h"
#include "sprd_vpu.h"

static int get_eb_clk(struct vpu_platform_data *data, struct device_node *np)
{
	int ret = 0;
	struct clk *core_clk;
	struct clk *clk_ahb_gate_vsp_eb;
	struct clk *clk_mm_eb;
	struct clk *clk_axi_gate_vsp;
	struct clk *clk_vsp_mq_ahb_eb;
	struct device *dev = data->dev;

	clk_mm_eb = devm_clk_get(data->dev, "clk_mm_eb");

	if (IS_ERR_OR_NULL(clk_mm_eb)) {
		dev_err(dev,"Failed: Can't get clock [%s]! %p\n",
			"clk_mm_eb", clk_mm_eb);
		data->clk.clk_mm_eb = NULL;
		ret = -EINVAL;
		goto errout;
	} else
		data->clk.clk_mm_eb = clk_mm_eb;

	clk_axi_gate_vsp = devm_clk_get(data->dev,"clk_axi_gate_vsp");
	if (IS_ERR_OR_NULL(clk_axi_gate_vsp)) {
		dev_err(dev,"Failed: Can't get clock [%s]! %p\n",
			"clk_axi_gate_vsp",clk_axi_gate_vsp);
		data->clk.clk_axi_gate_vsp = NULL;
		goto errout;
	} else {
		data->clk.clk_axi_gate_vsp = clk_axi_gate_vsp;
	}

	core_clk = devm_clk_get(data->dev, "clk_vsp");
	if (IS_ERR_OR_NULL(core_clk)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n", "core_clk",
		       core_clk);
		ret = -EINVAL;
		data->clk.core_clk = NULL;
		goto errout;
	} else
		data->clk.core_clk = core_clk;

	clk_ahb_gate_vsp_eb = devm_clk_get(data->dev, "clk_ahb_gate_vsp_eb");
	if (IS_ERR_OR_NULL(clk_ahb_gate_vsp_eb)) {
		dev_err(dev, "Failed: Can't get clock [%s]! %p\n", "clk_ahb_gate_vsp_eb",
			   clk_ahb_gate_vsp_eb);
		ret = -EINVAL;
		data->clk.clk_ahb_gate_vsp_eb = NULL;
		goto errout;
	} else
		data->clk.clk_ahb_gate_vsp_eb = clk_ahb_gate_vsp_eb;

	clk_vsp_mq_ahb_eb = devm_clk_get(data->dev, "clk_vsp_mq_ahb_eb");

	if (IS_ERR(clk_vsp_mq_ahb_eb)) {
		dev_err(dev,"Failed: Can't get clock [%s]! %p\n",
		       "clk_vsp_mq_ahb_eb", clk_vsp_mq_ahb_eb);
		ret = PTR_ERR(clk_vsp_mq_ahb_eb);
		data->clk.clk_vsp_mq_ahb_eb = NULL;
		goto errout;
	} else {
		data->clk.clk_vsp_mq_ahb_eb = clk_vsp_mq_ahb_eb;
	}

errout:
	return ret;
}

static int clock_enable(struct vpu_platform_data *data)
{
	int ret = 0;
	struct vpu_clk *clk = &data->clk;
	struct device *dev = data->dev;

	if (clk->clk_mm_eb) {
		ret = clk_prepare_enable(clk->clk_mm_eb);
		if (ret) {
			dev_err(dev, "vsp clk_mm_eb: clk_enable failed!\n");
			goto error1;
		}
		dev_dbg(dev, "vsp clk_mm_eb: clk_prepare_enable ok.\n");
	}

    if (clk->clk_ahb_gate_vsp_eb) {
		ret = clk_prepare_enable(clk->clk_ahb_gate_vsp_eb);
		if (ret) {
			dev_err(dev, "vsp clk_ahb_gate_vsp_eb: clk_enable failed!\n");
			goto error2;
		}
		dev_dbg(dev, "vsp clk_ahb_gate_vsp_eb: clk_prepare_enable ok.\n");
	}

	if (clk->clk_axi_gate_vsp) {
		ret = clk_prepare_enable(clk->clk_axi_gate_vsp);
		if (ret) {
			pr_err("clk_axi_gate_vsp: clk_prepare_enable fail!\n");
			goto error3;
		}
		dev_dbg(dev, "clk_axi_gate_vsp: clk_prepare_enable ok.\n");
	}

	if (clk->clk_vsp_mq_ahb_eb) {
		ret = clk_prepare_enable(clk->clk_vsp_mq_ahb_eb);
		if (ret) {
			pr_err("clk_vsp_mq_ahb_eb: clk_enable fail!\n");
			goto error4;
		}
		dev_dbg(dev, "clk_vsp_mq_ahb_eb: clk_prepare_enable ok.\n");
	}

	ret = clk_set_parent(clk->core_clk, clk->core_parent_clk);
	if (ret) {
		dev_err(dev, "clock[%s]: clk_set_parent() failed!", "clk_core");
		goto error5;
	}

	ret = clk_prepare_enable(clk->core_clk);
	if (ret) {
		dev_err(dev, "core_clk: clk_prepare_enable failed!\n");
		goto error5;
	}
	dev_dbg(dev, "vsp_clk: clk_prepare_enable ok.\n");

	dev_dbg(data->dev, "%s %d,OK\n", __func__, __LINE__);

	return ret;

error5:
	if (clk->clk_vsp_mq_ahb_eb) {
		clk_disable_unprepare(clk->clk_vsp_mq_ahb_eb);
	}
error4:
	if (clk->clk_axi_gate_vsp) {
		clk_disable_unprepare(clk->clk_axi_gate_vsp);
	}
error3:
    if (clk->clk_ahb_gate_vsp_eb) {
        clk_disable_unprepare(clk->clk_ahb_gate_vsp_eb);
    }
error2:
	if (clk->clk_mm_eb) {
		clk_disable_unprepare(clk->clk_mm_eb);
	}
error1:
	return ret;


}

static void clock_disable(struct vpu_platform_data *data)
{
	struct vpu_clk *clk = &data->clk;

	clk_disable_unprepare(clk->core_clk);
	clk_disable_unprepare(clk->clk_axi_gate_vsp);
	clk_disable_unprepare(clk->clk_vsp_mq_ahb_eb);
	clk_disable_unprepare(clk->clk_ahb_gate_vsp_eb);
	clk_disable_unprepare(clk->clk_mm_eb);

	dev_dbg(data->dev, "%s %d,OK\n", __func__, __LINE__);
}

static u32 get_reset_mask_lite(struct vpu_platform_data *data)
{
	u8 need_rst_axi = 0;

	need_rst_axi = (readl_relaxed(data->glb_reg_base +
						VPU_AXI_STS_OFF) & 0x7) > 0;
	if (need_rst_axi) {
		pr_info("vsp_axi_busy");
		return (data->regs[RESET].mask | BIT(12));
	} else
		return data->regs[RESET].mask;
}



const struct vpu_ops vsp_pike2 = {
	.get_eb_clk = get_eb_clk,
	.clock_enable = clock_enable,
	.clock_disable = clock_disable,
	.check_pw_status = vsp_check_pw_status,
	.get_reset_mask = get_reset_mask_lite,
};
/* for pike2 */

