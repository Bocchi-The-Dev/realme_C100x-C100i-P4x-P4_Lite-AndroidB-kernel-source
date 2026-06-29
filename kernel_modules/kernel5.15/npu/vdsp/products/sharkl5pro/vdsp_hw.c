/**
 * SPDX-FileContributor: vdsp driver hardware function: power and contrl.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/soc/sprd/hwfeature.h>
#include <linux/version.h>
#include <asm/cacheflush.h>
#include "vdsp_debugfs.h"
#include "vdsp_hw.h"
#include "vdsp_qos.h"
#include "vdsp_reg.h"
#include "xrp_internal.h"
#include "xrp_kernel_defs.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-vdsp: [hw] %d: %d %s:" \
	fmt, current->pid, __LINE__, __func__

//PWR_STATUS0_DBG[31:24]
#define PD_VDSP_SHUTDOWN    (0x7)	//shutdown mode
#define PD_VDSP_WAKEUP      (0x0)	//work mode

static int vdsp_wait_stable_power_state(void *hw_arg, uint32_t state)
{
	uint32_t cycle = 0;
	uint32_t power_state1 = 0;
	uint32_t power_state2 = 0;
	uint32_t power_state3 = 0;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	/*Wait for the pmu state to stabilize (PD_AD_VDSP_STATE)*/
	do {
		udelay(25);	/*About a few hundred microsecond */
		cycle++;
		if (vdsp_regmap_read_mask(hw->pmu_regmap, 0xbc, 0xff000000, &power_state1)
			|| vdsp_regmap_read_mask(hw->pmu_regmap, 0xbc, 0xff000000, &power_state2)
			|| vdsp_regmap_read_mask(hw->pmu_regmap, 0xbc, 0xff000000, &power_state3)) {
			pr_err("[error] fail to get vdsp power state\n");
			return -1;
		}
	} while (((power_state1 != state) ||
		(power_state1 != power_state2) ||
		(power_state2 != power_state3)) && (cycle < 200));

	if (cycle >= 200) {
		pr_err("[error]cycle timeout %d, power_state[0x%x-0x%x-0x%x]\n",
			cycle, power_state1, power_state2, power_state3);
		return -1;
	}
	pr_debug("wait power state done, power[ 0x%x, 0x%x, 0x%x] cost cycle:%d\n",
		power_state1, power_state2, power_state3, cycle);
	return 0;
}

static int vdsp_wait_pwait_mode(void *hw_arg)
{
	int ret = 0;
	uint32_t cycle = 0;
	uint32_t val = 0;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	/* Wait 100 ms for pwait mode */
	do {
		cycle++;
		udelay(25);
		if (vdsp_regmap_read_mask(hw->ahb_regmap, REG_LP_CTL, VDSP_PWAITMODE, &val)) {
			pr_err("[error] can not get pwait states\n");
			ret = -1;
			break;
		}
	} while ((val == 0) && (cycle < 4000)); /*wait 1 second*/
	pr_debug("wait pwait time:[%d * 25]us\n", cycle);
	if (cycle >= 4000) {
		pr_err("[error] fail to wait vdsp pwaitmode\n");
		ret = -1;
	}
	return ret;
}

static void reset(void *hw_arg)
{
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	vdsp_regmap_update_bits(hw->ahb_regmap, REG_RESET, (0x3 << 9), (0x3 << 9), RT_APAHB);
	udelay(10);
	vdsp_regmap_update_bits(hw->ahb_regmap, REG_RESET, (0x3 << 9), 0, RT_APAHB);
}

static void release(void *hw_arg)
{
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	vdsp_regmap_update_bits(hw->ahb_regmap, REG_RUNSTALL, (0x1 << 2), 0, RT_NO_SET_CLR);
}

static void halt(void *hw_arg)
{
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	vdsp_regmap_update_bits(hw->ahb_regmap, REG_RUNSTALL, (0x1 << 2), (0x1 << 2), RT_NO_SET_CLR);
}

static int enable(void *hw_arg)
{
	int ret;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	/*pd_ap_vdsp_force_shutdown bit */
	vdsp_regmap_update_bits(hw->pmu_regmap, REG_PD_AP_VDSP_CORE_INT_DISABLE, (0x1 << 0), 0, RT_PMU);
	vdsp_regmap_update_bits(hw->pmu_regmap, REG_PD_AP_VDSP_CFG, (0x1 << 25), 0, RT_PMU);
	vdsp_regmap_update_bits(hw->pmu_regmap, REG_PD_AP_VDSP_DLSP_ENA, (0x1 << 0), 0, RT_PMU);
	/*vdsp_stop_en */
	vdsp_regmap_update_bits(hw->ahb_regmap, REG_LP_CTL, 0xC, 0x8, RT_NO_SET_CLR);
	/*isppll open for 936M */
	vdsp_regmap_update_bits(hw->pmu_regmap, REG_ISPPLL_REL_CFG, (0x1 << 0), 0x1, RT_PMU);
	/* loop PD_AD_VDSP_STATE */
	ret = vdsp_wait_stable_power_state(hw_arg, PD_VDSP_WAKEUP);

	/* IPI &vdma enable */
	vdsp_regmap_update_bits(hw->ahb_regmap, REG_AHB_EN, (0x1 << 6) | (0x1 << 3), (0x1 << 6) | (0x1 << 3), RT_APAHB);
	/*vdsp_all_int_mask = 0 */
	vdsp_regmap_update_bits(hw->ahb_regmap, REG_VDSP_INT_CTL, (0x1 << 13), 0, RT_NO_SET_CLR);

	return ret;
}

static int disable(void *hw_arg)
{
	int ret;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	/*vdma&IPI  disable */
	vdsp_regmap_update_bits(hw->ahb_regmap, REG_AHB_EN, (0x1 << 3) | (0x1 << 6), 0, RT_APAHB);
	/*vdsp_stop_en = 1 */
	vdsp_regmap_update_bits(hw->ahb_regmap, REG_LP_CTL, (0x1 << 2), (0x1 << 2), RT_NO_SET_CLR);
	/*mask all int */
	vdsp_regmap_update_bits(hw->ahb_regmap, REG_VDSP_INT_CTL, 0x1ffff, 0x1ffff, RT_NO_SET_CLR);
	/*pmu ap_vdsp_core_int_disable set 1 */
	vdsp_regmap_update_bits(hw->pmu_regmap, REG_PD_AP_VDSP_CORE_INT_DISABLE, 0x1, 0x1, RT_PMU);
	udelay(1);
	ret = vdsp_wait_pwait_mode(hw_arg);
	if (ret == 0) {
		/*pmu auto shutdown by vdsp core */
		vdsp_regmap_update_bits(hw->pmu_regmap, REG_PD_AP_VDSP_CFG, 0x9000000, 0x1000000, RT_PMU);	/*bit 24 27 */
		vdsp_regmap_update_bits(hw->pmu_regmap, REG_PD_AP_VDSP_DLSP_ENA, 0x1, 0x1, RT_PMU);
	} else {
		pr_warn("timed out need force shut down\n");
		/*bit25 =1 , bit24 = 0 */
		vdsp_regmap_update_bits(hw->pmu_regmap, REG_PD_AP_VDSP_CFG, 0xb000000, 0x2000000, RT_PMU);
	}
	return 0;
}

static int send_irq(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;

	hw->vdsp_ipi_desc->ops->irq_send(hw->device_irq[1]);
	return 0;
}

static irqreturn_t xrp_hw_irq_handler(int irq, void *dev_id)
{
	struct vdsp_hw *hw = dev_id;

	(void)irq;
	return xrp_irq_handler(hw->xrp);
}

static irqreturn_t xrp_hw_log_irq_handler(int irq, void *dev_id)
{
	struct vdsp_hw *hw = dev_id;

	(void)irq;
	return vdsp_log_irq_handler(hw->xrp);
}

static void *get_hw_sync_data(void *hw_arg, size_t *sz, uint32_t log_addr)
{
	static const uint32_t irq_mode[] = {
		[XRP_IRQ_NONE] = XRP_DSP_SYNC_IRQ_MODE_NONE,
		[XRP_IRQ_LEVEL] = XRP_DSP_SYNC_IRQ_MODE_LEVEL,
		[XRP_IRQ_EDGE] = XRP_DSP_SYNC_IRQ_MODE_EDGE,
		[XRP_IRQ_EDGE_SW] = XRP_DSP_SYNC_IRQ_MODE_EDGE,
	};
	struct vdsp_hw *hw = hw_arg;
	struct vdsp_side_sync_data *hw_sync_data = kmalloc(sizeof(*hw_sync_data), GFP_KERNEL);

	if (!hw_sync_data) {
		pr_err("hw_sync_data is NULL !!!\n");
		return NULL;
	}

	*hw_sync_data = (struct vdsp_side_sync_data) {
		.device_mmio_base = hw->ipi_phys,
		.host_irq_mode = hw->host_irq_mode,
		.host_irq_offset = hw->host_irq[0],
		.host_irq_bit = hw->host_irq[1],
		.device_irq_mode = irq_mode[hw->device_irq_mode],
		.device_irq_offset = hw->device_irq[0],
		.device_irq_bit = hw->device_irq[1],
		.device_irq = hw->device_irq[2],
		.vdsp_smsg_addr = (uint32_t)*sz,
		.vdsp_log_addr = log_addr,
	};
	pr_debug("device_mmio_base:%x, (host_irq)mode:%d, offset:%d, bit:%d,"
		"(device_irq)mode:%d, offset:%d, bit:%d, irq:%d,"
		"vdsp_smsg addr:0x%x, vdsp_log_addr:0x%x\n",
		hw_sync_data->device_mmio_base, hw_sync_data->host_irq_mode,
		hw_sync_data->host_irq_offset, hw_sync_data->host_irq_bit,
		hw_sync_data->device_irq_mode, hw_sync_data->device_irq_offset,
		hw_sync_data->device_irq_bit, hw_sync_data->device_irq,
		hw_sync_data->vdsp_smsg_addr, hw_sync_data->vdsp_log_addr);

	*sz = sizeof(*hw_sync_data);

	return hw_sync_data;
}

static int vdsp_request_irq(void *xvp_arg, void *hw_arg)
{
	int ret;
	struct device *dev = (struct device *)xvp_arg;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	pr_debug("dev %p ,request irq %d handle %p hw %p\n",
		dev, hw->client_irq, hw->vdsp_ipi_desc->ops->irq_handler, hw);
	ret = devm_request_irq(dev, hw->client_irq, hw->vdsp_ipi_desc->ops->irq_handler,
		IRQF_SHARED, DRIVER_NAME, hw);
	if (ret < 0) {
		pr_err("devm_request_irq fail, ret %d\n", ret);
	}

	return ret;
}

static void vdsp_free_irq(void *xvp_arg, void *hw_arg)
{
	struct device *dev = (struct device *)xvp_arg;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	pr_debug("free irq %d dev %p hw %p\n", hw->client_irq, dev, hw);
	devm_free_irq(dev, hw->client_irq, hw);
}

static const struct xrp_hw_ops hw_ops = {
	.halt = halt,
	.release = release,
	.reset = reset,
	.get_hw_sync_data = get_hw_sync_data,
	.send_irq = send_irq,
	.memcpy_tohw = NULL,	/*memcpy_hw_function */
	.memset_hw = NULL,	/*memset_hw_function */
	.enable = enable,
	.disable = disable,
	.set_qos = set_qos,
	.vdsp_request_irq = vdsp_request_irq,
	.vdsp_free_irq = vdsp_free_irq,
	.stop_vdsp = NULL,
};

static int sprd_vdsp_parse_hw_dt(struct platform_device *pdev,
	struct vdsp_hw *hw, int mem_idx, enum vdsp_init_flags *init_flags)
{
	int ret;
	struct resource *mem;
	struct device_node *np;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, mem_idx);
	if (unlikely(!mem)) {
		pr_err("get mem_idx:%d failed\n", mem_idx);
		return -ENODEV;
	}
	hw->ipi_phys = mem->start;
	hw->ipi = devm_ioremap_resource(&pdev->dev, mem);
	pr_debug("ipi = %pap/%p\n", &mem->start, hw->ipi);

	np = pdev->dev.of_node;
	if (np == NULL) {
		pr_err("np is null\n");
		return -EINVAL;
	}
	hw->ahb_regmap = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-ap-ahb");
	if (IS_ERR(hw->ahb_regmap)) {
		pr_err("can not get ahb_regmap regmap struct!\n");
		return -EINVAL;
	}
	hw->pmu_regmap = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-pmu");
	if (IS_ERR(hw->pmu_regmap)) {
		pr_err("can not get pmu_regmap regmap struct!\n");
		return -EINVAL;
	}
	/* qos */
	parse_qos(hw, pdev->dev.of_node);

	/* sw dts config */
	hw->device_irq[0] = 0;
	hw->device_irq[1] = 0;
	hw->device_irq[2] = 0;
	hw->device_irq_host_offset = 0;
	hw->device_irq_mode = 0;
	hw->host_irq_mode = 0;
	hw->host_irq[0] = 0;
	hw->host_irq[1] = 0;
	ret = of_property_read_u32_array(pdev->dev.of_node, "device-irq", hw->device_irq, ARRAY_SIZE(hw->device_irq));
	if (ret) {
		pr_err("dts get hw->device-irq fail\n");
	}
	ret = of_property_read_u32(pdev->dev.of_node, "device-irq-host-offset", &(hw->device_irq_host_offset));
	if (ret) {
		pr_err("dts get hw->device-irq-host-offset fail\n");
	}
	ret = of_property_read_u32(pdev->dev.of_node, "device-irq-mode", &(hw->device_irq_mode));
	if (ret) {
		pr_err("dts get hw->device-irq-mode fail\n");
	}
	ret = of_property_read_u32(pdev->dev.of_node, "host-irq-mode", &(hw->host_irq_mode));
	if (ret) {
		pr_err("dts get hw->host-irq-mode fail\n");
	}
	ret = of_property_read_u32_array(pdev->dev.of_node, "host-irq", hw->host_irq, ARRAY_SIZE(hw->host_irq));
	if (ret) {
		pr_err("dts get hw->host-irq fail\n");
	}
	hw->client_irq = platform_get_irq(pdev, 0);
	/* ipi */
	hw->vdsp_ipi_desc = get_vdsp_ipi_ctx_desc();
	if (hw->vdsp_ipi_desc == NULL || hw->vdsp_ipi_desc->ops == NULL) {
		pr_err("vdsp_ipi_desc is null error\n");
		return -EFAULT;
	}
	hw->vdsp_ipi_desc->base_addr = hw->ahb_regmap;
	hw->vdsp_ipi_desc->ipi_addr = hw->ipi;
	hw->vdsp_ipi_desc->irq_mode = hw->host_irq_mode;
	ret = vdsp_request_irq(&pdev->dev, hw);
	if (ret < 0) {
		pr_err("request_irq failed\n");
		goto err;
	}
	hw->vdsp_ipi_desc->ops->irq_register(0, xrp_hw_irq_handler, hw);
	hw->vdsp_ipi_desc->ops->irq_register(1, xrp_hw_irq_handler, hw);
	hw->vdsp_ipi_desc->ops->irq_register(2, xrp_hw_log_irq_handler, hw);
	*init_flags |= XRP_INIT_USE_HOST_IRQ;

	ret = 0;
err:
	return ret;
}

static long init_sprd(struct platform_device *pdev, struct vdsp_hw *hw)
{
	int ret;
	enum vdsp_init_flags init_flags = 0;

	ret = sprd_vdsp_parse_hw_dt(pdev, hw, 0, &init_flags);
	if (unlikely(ret < 0)) {
		pr_err("[error]parse hw dts fail\n");
		return -1;
	}
	return sprd_vdsp_init(pdev, init_flags, &hw_ops, hw);
}

// #ifdef CONFIG_OF
static const struct of_device_id vdsp_device_match[] = {
	{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
		.compatible = "sprd,sharkl5pro-vdsp",	//5.4 (dts related)
#else
		.compatible = "sprd,vdsp",				//5.15 (dts related)
#endif
		.data = init_sprd,
	},
	{},
};

MODULE_DEVICE_TABLE(of, vdsp_device_match);
// #endif

static int vdsp_driver_probe(struct platform_device *pdev)
{
	struct vdsp_hw *hw = devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	const struct of_device_id *match;
	long (*init) (struct platform_device *pdev, struct vdsp_hw *hw);
	long ret;

	pr_info("sprd_vdsp probe\n");//for debug
	if (!hw) {
		pr_err("vdsp hw kzalloc fail\n");
		return -ENOMEM;
	}

	match = of_match_device(of_match_ptr(vdsp_device_match), &pdev->dev);
	if (!match) {
		pr_err("[error]sprd,vdsp not match\n");
		return -ENODEV;
	}

	init = match->data;
	ret = init(pdev, hw);
	if (IS_ERR_VALUE(ret)) {
		pr_err("[error]vdsp init fail\n");
		return -1;
	} else {
		hw->xrp = ERR_PTR(ret);
		return 0;
	}
}

static int vdsp_driver_remove(struct platform_device *pdev)
{
	return sprd_vdsp_deinit(pdev);
}

static const struct dev_pm_ops vdsp_pm_ops = {
	SET_RUNTIME_PM_OPS(vdsp_runtime_suspend, vdsp_runtime_resume, NULL)
};

static struct platform_driver vdsp_driver = {
	.probe = vdsp_driver_probe,
	.remove = vdsp_driver_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(vdsp_device_match),
		.pm = &vdsp_pm_ops,
	},
};

int vdsp_hw_irq_register(void *data)
{
	struct vdsp_ipi_ctx_desc *vdsp_ipi_desc = NULL;

	vdsp_ipi_desc = get_vdsp_ipi_ctx_desc();
	if (vdsp_ipi_desc) {
		return vdsp_ipi_desc->ops->irq_register(0, xrp_hw_irq_handler, data);
	}
	return -1;
}

int vdsp_hw_irq_unregister(void)
{
	struct vdsp_ipi_ctx_desc *vdsp_ipi_desc = NULL;

	vdsp_ipi_desc = get_vdsp_ipi_ctx_desc();
	if (vdsp_ipi_desc) {
		return vdsp_ipi_desc->ops->irq_unregister(0);
	}
	return -1;
}

module_platform_driver(vdsp_driver);

MODULE_DESCRIPTION("Sprd VDSP Driver");
MODULE_AUTHOR("Vdsp@unisoc");
MODULE_LICENSE("GPL");
