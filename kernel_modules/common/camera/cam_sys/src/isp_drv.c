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
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <sprd_mm.h>

#include "isp_int_common.h"
#include "isp_node.h"
#include "isp_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_DRV: %d %d %s : " fmt, current->pid, __LINE__, __func__

uint32_t s_isp_irq_no[ISP_LOGICAL_COUNT];
unsigned long s_isp_regbase[ISP_MAX_COUNT];
unsigned long isp_phys_base[ISP_MAX_COUNT];
unsigned long s_isp_mmubase;

static uint32_t ispdrv_deci_cal(uint32_t src, uint32_t dst)
{
	uint32_t deci = 1;

	if (src <= dst * 4)
		deci = 1;
	else if (src <= dst * 8)
		deci = 2;
	else if (src <= dst * 16)
		deci = 4;
	else if (src <= dst * 32)
		deci = 8;
	else if (src <= dst * 64)
		deci = 16;
	else
		deci = 0;
	return deci;
}

uint32_t isp_drv_deci_factor_cal(uint32_t deci)
{
	/* 0: 1/2, 1: 1/4, 2: 1/8, 3: 1/16*/
	if (deci == 16)
		return 3;
	else if (deci == 8)
		return 2;
	else if (deci == 4)
		return 1;
	else
		return 0;
}

int isp_drv_trim_deci_info_cal(uint32_t src, uint32_t dst,
	uint32_t *trim, uint32_t *deci)
{
	uint32_t tmp;

	tmp = ispdrv_deci_cal(src, dst);
	if (tmp == 0)
		return -EINVAL;

	if ((src % (2 * tmp)) == 0) {
		*trim = src;
		*deci = tmp;
	} else {
		*trim = (src / (2 * tmp) * (2 * tmp));
		*deci = ispdrv_deci_cal(*trim, dst);
	}
	return 0;
}

int isp_drv_dt_parse(struct device_node *dn,
		struct cam_hw_info *hw_info)
{
	int i = 0;
	void __iomem *reg_base;
	struct cam_hw_soc_info *soc_isp = NULL;
	struct cam_hw_ip_info *ip_isp = NULL;
	struct device_node *isp_node = NULL;
	struct device_node *qos_node = NULL;
	struct device_node *iommu_node = NULL;
	struct resource res = {0};
	uint32_t args[2];

	if (!dn || !hw_info) {
		pr_err("fail to get dn %p hw info %p\n", dn, hw_info);
		return -EINVAL;
	}

	pr_info("isp dev device node %s, full name %s\n",
		dn->name, dn->full_name);
	isp_node = of_parse_phandle(dn, "sprd,isp", 0);
	if (isp_node == NULL) {
		pr_err("fail to parse the property of sprd,isp\n");
		return -EFAULT;
	}

	soc_isp = hw_info->soc_isp;
	ip_isp = hw_info->ip_isp;
	pr_info("after isp dev device node %s, full name %s\n",
		isp_node->name, isp_node->full_name);
	soc_isp->pdev = of_find_device_by_node(isp_node);
	if (soc_isp->pdev == NULL) {
		pr_err("fail to get isp pdev\n");
		return -EFAULT;
	}
	pr_info("sprd s_isp_pdev name %s\n", soc_isp->pdev->name);

	if (of_device_is_compatible(isp_node, "sprd,isp")) {
		/* read clk from dts */
		if (hw_info->cam_ioctl(hw_info, CAM_HW_GET_ISP_DTS_CLK, isp_node)) {
			pr_err("fail to get clk\n");
			return -EFAULT;
		}

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
		iommu_node = of_parse_phandle(isp_node, "unisoc-iommus", 0);
#else
		iommu_node = of_parse_phandle(isp_node, "iommus", 0);
#endif
		if (iommu_node) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
			reg_base = of_iomap(iommu_node, 0);
#else
			reg_base = of_iomap(iommu_node, 1);
#endif
			if (!reg_base)
				pr_err("fail to map ISP IOMMU base\n");
			else
				s_isp_mmubase = (unsigned long)reg_base;
		}
		pr_info("ISP IOMMU Base  0x%lx\n", s_isp_mmubase);

		/* qos dt parse */
		qos_node = of_parse_phandle(isp_node, "isp_qos", 0);
		if (qos_node) {
			uint8_t val;

			if (of_property_read_u8(qos_node, "awqos-high", &val)) {
				pr_warn("warning: isp awqos-high reading fail.\n");
				val = 7;
			}
			soc_isp->awqos_high = (uint32_t)val;
			if (of_property_read_u8(qos_node, "awqos-low", &val)) {
				pr_warn("warning: isp awqos-low reading fail.\n");
				val = 6;
			}
			soc_isp->awqos_low = (uint32_t)val;
			if (of_property_read_u8(qos_node, "arqos-high", &val)) {
				pr_warn("warning: isp arqos-high reading fail.\n");
				val = 7;
			}
			soc_isp->arqos_high = (uint32_t)val;
			if (of_property_read_u8(qos_node, "arqos-low", &val)) {
				pr_warn("warning: isp arqos-low reading fail.\n");
				val = 6;
			}
			soc_isp->arqos_low = (uint32_t)val;
			pr_info("get isp qos node. r: %d %d w: %d %d\n",
				soc_isp->arqos_high, soc_isp->arqos_low,
				soc_isp->awqos_high, soc_isp->awqos_low);
		} else {
			soc_isp->awqos_high = 7;
			soc_isp->awqos_low = 6;
			soc_isp->arqos_high = 7;
			soc_isp->arqos_low = 6;
		}

		soc_isp->cam_ahb_gpr = syscon_regmap_lookup_by_phandle(isp_node,
			"sprd,cam-ahb-syscon");
		if (IS_ERR_OR_NULL(soc_isp->cam_ahb_gpr)) {
			pr_err("fail to get sprd,cam-ahb-syscon");
			return PTR_ERR(soc_isp->cam_ahb_gpr);
		}

		if (!cam_kernel_adapt_syscon_get_args_by_name(isp_node, "reset",
			ARRAY_SIZE(args), args)) {
			ip_isp->syscon.rst = args[0];
			ip_isp->syscon.rst_mask = args[1];
		} else {
			pr_err("fail to get isp reset syscon\n");
			return -EINVAL;
		}

		if (!cam_kernel_adapt_syscon_get_args_by_name(isp_node, "isp_ahb_reset",
			ARRAY_SIZE(args), args))
			ip_isp->syscon.rst_ahb_mask = args[1];

		if (!cam_kernel_adapt_syscon_get_args_by_name(isp_node, "isp_vau_reset",
			ARRAY_SIZE(args), args))
			ip_isp->syscon.rst_vau_mask = args[1];

#if defined (PROJ_QOGIRN6PRO)
		if (!cam_kernel_adapt_syscon_get_args_by_name(isp_node, "sys_h2p_db_soft_rst",
			ARRAY_SIZE(args), args)) {
			ip_isp->syscon.sys_soft_rst = args[0];
			ip_isp->syscon.sys_h2p_db_soft_rst = args[1];
		}
#endif

		if (of_address_to_resource(isp_node, i, &res))
			pr_err("fail to get isp phys addr\n");

		ip_isp->phy_base = (unsigned long)res.start;
		isp_phys_base[0] = ip_isp->phy_base;
		pr_info("isp phys reg base is %lx\n", isp_phys_base[0]);
		reg_base = of_iomap(isp_node, i);
		if (!reg_base) {
			pr_err("fail to get isp reg_base %d\n", i);
			return -ENXIO;
		}

		ip_isp->reg_base = (unsigned long)reg_base;
		s_isp_regbase[0] = ip_isp->reg_base;

		for (i = 0; i < ISP_LOGICAL_COUNT; i++) {
			s_isp_irq_no[i] = irq_of_parse_and_map(isp_node, i);
			if (s_isp_irq_no[i] <= 0) {
				pr_err("fail to get isp irq %d\n", i);
				return -EFAULT;
			}

			pr_info("ISP%d dts OK! regbase %lx, irq %d\n", i,
				s_isp_regbase[0], s_isp_irq_no[i]);
		}
		ip_isp->dec_irq_no = s_isp_irq_no[ISP_LOGICAL_COUNT - 1];
	} else {
		pr_err("fail to match isp device node\n");
		return -EINVAL;
	}

	return 0;
}

int isp_drv_hw_init(void *arg)
{
	int ret = 0;
	uint32_t reset_flag = 0;
	struct isp_pipe_dev *dev = NULL;
	struct cam_hw_info *hw = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	dev = (struct isp_pipe_dev *)arg;
	hw = dev->isp_hw;
	reset_flag = ISP_RESET_AFTER_POWER_ON;
	ret = hw->isp_ioctl(hw, ISP_HW_CFG_RESET, &reset_flag);
	if (ret)
		goto reset_fail;

	ret = isp_int_common_irq_request(&hw->pdev->dev, s_isp_irq_no, arg);
	if (ret)
		goto reset_fail;

	hw->isp_ioctl(hw, ISP_HW_CFG_DEFAULT_PARA_SET, NULL);

reset_fail:
	return ret;
}

int isp_drv_hw_deinit(void *arg)
{
	int ret = 0;
	uint32_t reset_flag = 0;
	struct isp_pipe_dev *dev = NULL;
	struct cam_hw_info *hw = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	dev = (struct isp_pipe_dev *)arg;
	hw = dev->isp_hw;

	reset_flag = ISP_RESET_BEFORE_POWER_OFF;
	ret = hw->isp_ioctl(hw, ISP_HW_CFG_RESET, &reset_flag);
	if (ret)
		pr_err("fail to reset isp\n");
	ret = isp_int_common_irq_free(&hw->pdev->dev, arg);
	if (ret)
		pr_err("fail to free isp irq\n");

	return ret;
}
