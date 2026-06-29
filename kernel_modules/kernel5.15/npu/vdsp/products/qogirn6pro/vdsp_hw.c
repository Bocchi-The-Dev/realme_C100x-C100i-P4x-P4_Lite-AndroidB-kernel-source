/**
 * SPDX-FileContributor: vdsp driver hardware function: power and contrl.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <asm/cacheflush.h>
#include "vdsp_debugfs.h"
#include "vdsp_hw.h"
#include "vdsp_mailbox_drv.h"
#include "vdsp_qos.h"
#include "vdsp_reg.h"
#include "xrp_internal.h"
#include "xrp_kernel_defs.h"
#include <sprd_camsys_domain.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-vdsp: [hw] %d: %d %s:" \
	fmt, current->pid, __LINE__, __func__

#define VDSP_RECV_CMD_MSG   0xF8F8F8F8F8F8F8F8ULL
#define VDSP_RECV_LOG_MSG   0xF5F5F5F5F5F5F5F5ULL
#define VDSP_SEND_CMD_MSG   0xABABABABABABABABULL

#define PD_VDSP_SHUTDOWN    (0x7)	//shutdown mode
#define PD_VDSP_WAKEUP      (0x0)	//work mode

#define MBOXID_AP           (0x0)
#define MBOXID_CH           (0x1)
#define MBOXID_VDSP         (0x2)

static const char *const syscon_name[] = {
	"vdsp_force_shutdown",
	"vdsp_auto_shutdown",
	"vdsp_power_state",
	"vdsp_intr_disable",
	"vdsp_deepsleep_enable",
	"vdsp_pd_sel",
	"vdsppll_force_off",
	"vdsppll_force_on",
};

enum {
	FORCE_SHUTDOWN = 0,	// pmu, 0x02FC-bit25
	AUTO_SHUTDOWN,		// pmu, 0x02FC-bit24
	PW_STATUS,		// pmu, 0x0510 -bit0-4
	INTR_DIS,		// pmu, 0x025C-bit0
	DPSL_EN,		// pmu, 0x01FC-bit0 (vdsp_blk domain deep sleep enable. 1:enable;0:disable)
	VDSP_PD_SEL,		// pmu, 0x02FC-bit27
	VDSPPLL_FORCE_OFF,	// pmu, 0X0A2C-bit1
	VDSPPLL_FORCE_ON,	// pmu, 0X0A2C-bit0
};

struct vdsp_dts_info {
	struct vdsp_reg regs[ARRAY_SIZE(syscon_name)];
};

static struct vdsp_dts_info *dts_info;

static int stop_vdsp(void *hw_arg);

static int vdsp_wait_stable_power_state(uint32_t state)
{
	uint32_t cycle = 0;
	uint32_t power_state1 = 0;
	uint32_t power_state2 = 0;
	uint32_t power_state3 = 0;

	/*Wait for the pmu state to stabilize */
	do {
		udelay(25);	/*About a few hundred microsecond */
		cycle++;
		if (vdsp_reg_read(&dts_info->regs[PW_STATUS], &power_state1)
			|| vdsp_reg_read(&dts_info->regs[PW_STATUS], &power_state2)
			|| vdsp_reg_read(&dts_info->regs[PW_STATUS], &power_state3)) {
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

static int vdsp_force_power_on(void)
{
	/* vdsp domain power on (old code)
		1. auto shutdown en, shutdown with ap; 0: control by bit25
		2. set 0 to shutdown
		3. deep sleep disable

		from low power.doc
		(1) Clear PMU register pd_vdsp_auto_shutdown_en.
		(2) Clear PMU resister pd_vdsp_force_shutdown.
		(3) PMU register "pd_vdsp_state" is "0x0".
	*/
	int ret;

	vdsp_reg_update(&dts_info->regs[AUTO_SHUTDOWN], 0, RT_PMU);
	vdsp_reg_update(&dts_info->regs[FORCE_SHUTDOWN], 0, RT_PMU);
	vdsp_reg_update(&dts_info->regs[DPSL_EN], 0, RT_PMU);

	ret = vdsp_wait_stable_power_state(PD_VDSP_WAKEUP);
	if (ret != 0) {
		pr_warn(" power state not ready, force shutdown\n");
		vdsp_reg_update(&dts_info->regs[FORCE_SHUTDOWN], ~((uint32_t) 0), RT_PMU);
	}

	return ret;
}

static int vdsp_force_shutdown(void)
{
	/*
	(1) Set PMU register pd_vdsp_force_shutdown.
	(2) Clear PMU register pd_vdsp_auto_shutdown_en.
	(3) PMU register "pd_vdsp_state" is "0x7".
	*/
	int ret;

	vdsp_reg_update(&dts_info->regs[FORCE_SHUTDOWN], ~((uint32_t) 0), RT_PMU);
	vdsp_reg_update(&dts_info->regs[AUTO_SHUTDOWN], 0, RT_PMU);
	ret = vdsp_wait_stable_power_state(PD_VDSP_SHUTDOWN);

	return ret;
}

static int vdsp_auto_shutdown_nap(void)
{
	/*
	(1) Clear PMU register pd_vdsp_blk_pd_sel
	(2) Set PMU register vdsp_blk_dslp_ena
	(3) Set PMU register pd_vdsp_blk_auto_shutdown_en.
	(4) Assert vdsp_blk_core_stop.
	(5) PMU register "pd_vdsp_blk_state" is "0x7".
	*/
	int ret;

	vdsp_reg_update(&dts_info->regs[VDSP_PD_SEL], 0, RT_PMU);
	vdsp_reg_update(&dts_info->regs[DPSL_EN], ~((uint32_t) 0), RT_PMU);
	vdsp_reg_update(&dts_info->regs[AUTO_SHUTDOWN], ~((uint32_t) 0), RT_PMU);
	ret = vdsp_wait_stable_power_state(PD_VDSP_SHUTDOWN);

	return ret;
}

static int vdsp_power_on(void *hw_arg)
{
	(void) (hw_arg);
	return vdsp_force_power_on();
}

static int vdsp_power_off(void *hw_arg)
{
	int ret;

	pr_debug("power off start\n");
	ret = stop_vdsp(hw_arg);
	if (ret != 0)
		ret = vdsp_force_shutdown();
	else
		ret = vdsp_auto_shutdown_nap();

	return ret;
}

static int vdsp_blk_enable(void *hw_arg)
{
	int ret;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	/* vdsp blk en */
	ret = vdsp_regmap_update_bits(hw->mm_ahb, VDSP_BLK_EN, 0x1FFF, ~((uint32_t) 0), RT_MMSYS);
	if (ret)
		pr_err("set vdsp blk failed:%d\n", ret);
	/*vdsp int mask */
	ret = vdsp_regmap_update_bits(hw->mm_ahb, VDSP_INT_MASK, 0x3F, 0, RT_NO_SET_CLR);
	if (ret)
		pr_err("clear intr mask failed:%d\n", ret);

	return ret;
}

static int vdsp_blk_disable(void *hw_arg)
{
	int ret;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	/* vdsp blk disable */
	ret = vdsp_regmap_update_bits(hw->mm_ahb, VDSP_BLK_EN, 0x1FFF, 0, RT_MMSYS);
	if (ret)
		pr_err("clear vdsp blk failed:%d\n", ret);
	/*vdsp int mask all */
	ret = vdsp_regmap_update_bits(hw->mm_ahb, VDSP_INT_MASK, 0x3F, ~((uint32_t) 0), RT_NO_SET_CLR);
	if (ret)
		pr_err("set vdsp blk failed:%d\n", ret);

	return ret;
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
		if (vdsp_regmap_read_mask(hw->mm_ahb, VDSP_CORE_CFG, VDSP_PWAITMODE, &val)) {
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

	vdsp_regmap_update_bits(hw->mm_ahb, REG_RESET, VDSP_RESET, ~((uint32_t) 0), RT_MMSYS);
	udelay(10);
	vdsp_regmap_update_bits(hw->mm_ahb, REG_RESET, VDSP_RESET, 0, RT_MMSYS);
}

static int stop_vdsp(void *hw_arg)
{
	/*mask all interrupt and wait vdsp to pwaitmode */
	vdsp_reg_update(&dts_info->regs[INTR_DIS], ~((uint32_t) 0), RT_PMU);
	return vdsp_wait_pwait_mode(hw_arg);
}

static void release(void *hw_arg)
{
	uint32_t val = 0;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	if (vdsp_regmap_read_mask(hw->mm_ahb, VDSP_CORE_CFG, 0xffffffff, &val))
		pr_err("error read mmahb:%d\n", VDSP_CORE_CFG);
	val &= ~(VDSP_RUNSTALL);	/*clear bit2 */
	if (vdsp_regmap_update_bits(hw->mm_ahb, VDSP_CORE_CFG, 0xFFFFFFFF, val, RT_NO_SET_CLR))
		pr_err("error write mmahb:%d\n", VDSP_CORE_CFG);
}

static void halt(void *hw_arg)
{
	uint32_t val = 0;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	if (vdsp_regmap_read_mask(hw->mm_ahb, VDSP_CORE_CFG, 0xffffffff, &val))
		pr_err("error read mmahb:%d\n", VDSP_CORE_CFG);
	val |= (VDSP_RUNSTALL);
	if (vdsp_regmap_update_bits(hw->mm_ahb, VDSP_CORE_CFG, 0xFFFFFFFF, val, RT_NO_SET_CLR))
		pr_err("error write mmahb:%d\n", VDSP_CORE_CFG);
}

static int enable(void *hw_arg)
{
	int ret;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	pr_debug("vdsp enable\n");
	ret = sprd_glb_mm_pw_on_cfg();
	pm_runtime_get_sync(hw->xrp->dev);
	if (ret < 0) {
		pr_err("vdsp fail to power on cam sys\n");
		goto err_cam_pw_on;
	}

	/* cancel force off vdsppll, need to wait 224us */
	vdsp_reg_update(&dts_info->regs[VDSPPLL_FORCE_OFF], 0, RT_PMU);
	vdsp_reg_update(&dts_info->regs[VDSPPLL_FORCE_ON], 0x1, RT_PMU);
	udelay(224);

	ret = vdsp_power_on(hw);
	if (ret) {
		pr_err("[error]vdsp pw on ret:%d\n", ret);
		goto err_dsp_pw_on;
	}
	ret = vdsp_blk_enable(hw);
	if (ret) {
		pr_err("[error]vdsp pw on ret:%d\n", ret);
		goto err_dsp_blk_eb;
	}
	return ret;

err_dsp_blk_eb:
	vdsp_power_off(hw);
err_dsp_pw_on:
	vdsp_reg_update(&dts_info->regs[VDSPPLL_FORCE_OFF], 0x2, RT_PMU);
	vdsp_reg_update(&dts_info->regs[VDSPPLL_FORCE_ON], 0, RT_PMU);
err_cam_pw_on:
	sprd_glb_mm_pw_off_cfg();
	pm_runtime_put_sync(hw->xrp->dev);

	return ret;
}

static int disable(void *hw_arg)
{
	int ret;
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	ret = vdsp_blk_disable(hw);
	if (ret) {
		pr_err("[error]cam disable ret:%d\n", ret);
	}
	ret = vdsp_power_off(hw);
	if (ret) {
		pr_err("[error]vdsp pw off ret:%d\n", ret);
	}
	/*force off vdsppll, need after vdsp power off */
	vdsp_reg_update(&dts_info->regs[VDSPPLL_FORCE_OFF], 0x2, RT_PMU);
	vdsp_reg_update(&dts_info->regs[VDSPPLL_FORCE_ON], 0, RT_PMU);

	sprd_glb_mm_pw_off_cfg();
	pm_runtime_put_sync(hw->xrp->dev);

	return ret;
}

static int send_irq(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;
	uint64_t msg = VDSP_SEND_CMD_MSG;

	return hw->vdsp_mbox_desc->ops->irq_send(hw->device_irq[1], msg);
}

static irqreturn_t xrp_hw_irq_handler(void *ptr, void *dev_id)
{
	struct vdsp_hw *hw = dev_id;
	uint64_t msg = *((uint64_t *) ptr);

	if (msg == VDSP_RECV_LOG_MSG) { /*if log interrupt */
		return vdsp_log_irq_handler(hw->xrp);
	}
	else if (msg == VDSP_RECV_CMD_MSG) { /* cmd interrupt */
		return xrp_irq_handler(hw->xrp);
	}
	pr_err("irq err msg content msg:%llx\n", msg);
	return IRQ_NONE;
}

static int init_mbox(void *hw_arg)
{
	struct vdsp_mbox_ctx_desc *mboxdesc = get_vdsp_mbox_ctx_desc();

	if (!mboxdesc->ops || !mboxdesc->ops->ctx_init) {
		pr_err("mboxdesc: ctx_init operation not supported");
		return -1;
	}
	return mboxdesc->ops->ctx_init(mboxdesc);
}

static int deinit_mbox(void *hw_arg)
{
	struct vdsp_mbox_ctx_desc *mboxdesc = get_vdsp_mbox_ctx_desc();

	if (!mboxdesc->ops || !mboxdesc->ops->ctx_deinit) {
		pr_err("mboxdesc: ctx_deinit operation not supported");
		return -1;
	}
	return mboxdesc->ops->ctx_deinit(mboxdesc);
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
		.device_mmio_base = hw->mbox_phys,
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
		dev, hw->client_irq, hw->vdsp_mbox_desc->ops->irq_handler, hw);
	ret = devm_request_irq(dev, hw->client_irq, hw->vdsp_mbox_desc->ops->irq_handler,
		IRQF_SHARED /*IRQF_ONESHOT */, DRIVER_NAME, hw);
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
	.stop_vdsp = stop_vdsp,
	.init_communication_hw = init_mbox,
	.deinit_communication_hw = deinit_mbox,
};
#ifdef RESERVE_MEM
static int sprd_vdsp_parse_mem(struct vdsp_hw *hw)
{
	int ret;
	u32 out_values[4] = {0};
	struct device_node *np;

	np = of_find_node_by_name(NULL, "vdsp-mem");
	if (!np) {
		pr_err("find memory node fail\n");
		return -ENOENT;
	}
	ret = of_property_read_u32_array(np, "reg", out_values, 4);
	if (!ret) {
		hw->vdsp_reserved_mem_addr = out_values[0];
		hw->vdsp_reserved_mem_addr = hw->vdsp_reserved_mem_addr << 32;
		hw->vdsp_reserved_mem_addr |= out_values[1];

		hw->vdsp_reserved_mem_size = out_values[2];
		hw->vdsp_reserved_mem_size = hw->vdsp_reserved_mem_size << 32;
		hw->vdsp_reserved_mem_size |= out_values[3];
	} else {
		hw->vdsp_reserved_mem_addr = 0;
		hw->vdsp_reserved_mem_size = 0;
	}
	pr_debug("vdsp reserved mem addr %llx size %llx\n",
		hw->vdsp_reserved_mem_addr, hw->vdsp_reserved_mem_size);

	return ret;
}
#endif

static int sprd_vdsp_parse_hw_dt(struct platform_device *pdev,
	struct vdsp_hw *hw, int mem_idx, enum vdsp_init_flags *init_flags)
{
	int i;
	int ret;
	uint32_t syscon_args[2] = {0};
	const char *pname;
	struct regmap *tregmap;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *sub_np;
	struct resource parse_resource = {0};

	dts_info = devm_kzalloc(&pdev->dev, sizeof(*dts_info), GFP_KERNEL);
	if (!dts_info) {
		ret = -ENOMEM;
		goto err;
	}
	hw->mm_ahb = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-mmahb");
	if (IS_ERR_OR_NULL(hw->mm_ahb)) {
		pr_err("mm_ahb regmap:%p\n", hw->mm_ahb);
	}

	sub_np = of_parse_phandle(np, "sprd,syscon-mailbox", 0);
	if (of_address_to_resource(sub_np, 0, &parse_resource)) {
		pr_err("get resource failed\n");
	}
	hw->mbox_phys = parse_resource.start;
	pr_debug("hw->mbox_phys:0x%llx\n", hw->mbox_phys);

	for (i = 0; i < ARRAY_SIZE(syscon_name); i++) {
		pname = syscon_name[i];
		tregmap = syscon_regmap_lookup_by_phandle_args(np, pname, 2, syscon_args);
		pr_debug("syscon parse:%s\n", pname);
		if (IS_ERR_OR_NULL(tregmap)) {
			pr_err("fail to read %s regmap\n", pname);
			continue;
		}
		dts_info->regs[i].gpr = tregmap;
		dts_info->regs[i].reg = syscon_args[0];
		dts_info->regs[i].mask = syscon_args[1];
		pr_debug("dts[%s] 0x%x 0x%x\n", pname, dts_info->regs[i].reg, dts_info->regs[i].mask);
	}
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

	/* mailbox */
	hw->vdsp_mbox_desc = get_vdsp_mbox_ctx_desc();
	if (hw->vdsp_mbox_desc == NULL || hw->vdsp_mbox_desc->ops == NULL) {
		pr_err("vdsp_mbox_desc is null error\n");
		return -EFAULT;
	}
	ret = hw->vdsp_mbox_desc->ops->mbox_init();
	if (ret != 0) {
		pr_err("vdsp mbox init fail\n");
		return -EFAULT;
	}
	hw->vdsp_mbox_desc->irq_mode = hw->host_irq_mode;
	hw->vdsp_mbox_desc->mbox_en_cfg = hw->mm_ahb;
	ret = vdsp_request_irq(&pdev->dev, hw);
	if (ret < 0) {
		pr_err("request_irq failed\n");
		goto err;
	}
	hw->vdsp_mbox_desc->ops->irq_register(MBOXID_VDSP, xrp_hw_irq_handler, hw);
	*init_flags |= XRP_INIT_USE_HOST_IRQ;

	/* dma*/
	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(36));
	if (ret) {
		pr_err("Error: dma_set_mask failed\n");
		goto err;
	}
#ifdef RESERVE_MEM
	ret = sprd_vdsp_parse_mem(hw);
#endif
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

#ifdef CONFIG_OF
static const struct of_device_id vdsp_device_match[] = {
	{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
		.compatible = "sprd,qogirn6pro-vdsp",	//5.4 (dts related)
#else
		.compatible = "sprd,vdsp",				//5.15 (dts related)
#endif
		.data = init_sprd,
	},
	{},
};

MODULE_DEVICE_TABLE(of, vdsp_device_match);
#endif

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

static struct platform_driver vdsp_driver = {
	.probe = vdsp_driver_probe,
	.remove = vdsp_driver_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(vdsp_device_match),
	},
};

/****************************************************/
int vdsp_hw_irq_register(void *data)
{
	struct vdsp_mbox_ctx_desc *mboxdesc = NULL;

	mboxdesc = get_vdsp_mbox_ctx_desc();
	if (mboxdesc) {
		return mboxdesc->ops->irq_register(0, xrp_hw_irq_handler, data);
	}
	return -1;
}

int vdsp_hw_irq_unregister(void)
{
	struct vdsp_mbox_ctx_desc *mboxdesc = NULL;

	mboxdesc = get_vdsp_mbox_ctx_desc();
	if (mboxdesc) {
		return mboxdesc->ops->irq_unregister(0);
	}
	return -1;
}

/****************************************************/

module_platform_driver(vdsp_driver);

MODULE_DESCRIPTION("Sprd VDSP Driver");
MODULE_AUTHOR("Vdsp@unisoc");
MODULE_LICENSE("GPL");
