/*
 * SPDX-FileCopyrightText: 2021 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
*/



#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/hwspinlock.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include <mali_kbase_debug.h>
//For fix r24p0 --> r27p0 compile error.
//#include <backend/gpu/mali_kbase_device_internal.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of_address.h>
#ifdef KBASE_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>

#include <linux/smp.h>
#include <linux/sprd_sip_svc.h>
#include <trace/events/power.h>
#include <linux/nvmem-consumer.h>
#include <linux/thermal.h>
#include <linux/reset.h>
#include <gpu,qogirn6pro-regs.h>
#include <gpu,qogirn6pro-mask.h>
#include "mali_kbase_gpu_sys_qogirn6pro_qos.h"

#ifdef CONFIG_TRACE_POWER_GPU_FREQUENCY
#include <trace/events/power_gpu_frequency.h>
#else
#include "mali_power_gpu_frequency_trace.h"
#endif

#define MOVE_BIT_LEFT(x,n) ((unsigned long)x << n)

//#define VENDOR_FTRACE_MODULE_NAME    "unisoc-gpu"

#define DTS_CLK_OFFSET      1
#define PM_RUNTIME_DELAY_MS 50
#define UP_THRESHOLD        9/10
#define FREQ_KHZ            1000
#define VOLT_KMV            1000
#define GPU_MIN_FREQ_INDEX  3

#define GPU_26M_FREQ        26000000
#define GPU_76M8_FREQ       76800000
#define GPU_153M6_FREQ      153600000

#define GPLL_780M      780000000
#define GPLL_686M      686400000
#define GPLL_655M      655200000

#define GPU_680M_FREQ       680000000
#define GPU_850M_FREQ       850000000

#define PWR_KEY_VALUE       0x2968a819
#define PWR_OVERRIDE1_VALUE 0x0ff08008

unsigned long gpll_1950M, gpll_1716M,  gpll_1638M;

#if 0
struct gpu_qos_config {
	u8 arqos;
	u8 awqos;
	u8 arqos_threshold;
	u8 awqos_threshold;
};
#endif

struct gpu_freq_info {
	struct clk* clk_src;
	int freq;	//kHz
	int volt;	//uV
	int dvfs_index;
};

struct gpu_reg_info {
	struct regmap* regmap_ptr;
	uint32_t args[2];
};

struct gpu_dvfs_context {
	atomic_t gpu_clock_state;
	atomic_t gpu_power_state;

	int cur_voltage;

	struct clk*  clk_gpu_i;
	struct clk** gpu_clk_src;
	int gpu_clk_num;

	struct gpu_freq_info* freq_list;
	int freq_list_len;

	int cur_index;
	const struct gpu_freq_info* freq_cur;
	const struct gpu_freq_info* freq_default;

	struct semaphore* sem;

	struct gpu_reg_info top_dvfs_cfg_reg;
	struct gpu_reg_info gpu_sw_dvfs_ctrl_reg;
	struct gpu_reg_info top_force_reg;
	struct gpu_reg_info gpu_top_state_reg;
	struct gpu_reg_info clk_core_gpu_eb_reg;

	struct gpu_reg_info gpu_core0_state_reg;
	struct gpu_reg_info gpu_core1_state_reg;
	struct gpu_reg_info gpu_core2_state_reg;
	struct gpu_reg_info gpu_core3_state_reg;
	struct gpu_reg_info cur_st_st0_reg;
	struct gpu_reg_info cur_st_st1_reg;
	struct gpu_reg_info cur_st_st2_reg;
	struct gpu_reg_info cur_st_st3_reg;

	struct gpu_reg_info gpll_cfg_force_off_reg;
	struct gpu_reg_info gpll_cfg_force_on_reg;
	struct gpu_reg_info dcdc_gpu_pd;
	struct gpu_reg_info bridge_trans_idle_reg;
#if 0
	struct gpu_reg_info gpu_qos_sel;
	struct gpu_reg_info gpu_qos;
#endif
	struct gpu_reg_info dvfs_index_cfg;
	struct gpu_reg_info sw_dvfs_ctrl;
	struct gpu_reg_info freq_upd_cfg;

	struct gpu_reg_info core_index6_map_sel;

	struct regmap* gpu_apb_base_ptr;
	struct regmap* gpu_dvfs_apb_base_ptr;

	struct reset_control* gpu_soft_rst;

	//struct regulator *gpu_reg_ptr;

	u32 chip_id;
	u32 gpu_bin;
	struct sprd_sip_svc_handle *sip;
	int last_gpu_temperature;
	int gpu_temperature;
	u32 pos;

};

DEFINE_SEMAPHORE(gpu_dfs_sem);
static struct gpu_dvfs_context gpu_dvfs_ctx=
{
	.last_gpu_temperature = 0,
	.gpu_temperature = 0,
	.sem = &gpu_dfs_sem,
	.pos = 1,
};

#if 0
static struct gpu_qos_config gpu_qos_cfg=
{
	.arqos = 0,
	.awqos = 0,
	.arqos_threshold = 0,
	.awqos_threshold = 0,
};
#endif

#ifdef CONFIG_MALI_BOOST
int gpu_boost_level = 0;
int gpu_boost_level2 = 0;
module_param(gpu_boost_level2, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH); /* rw-rw-r-- */
MODULE_PARM_DESC(gpu_boost_level2, "GPU gpu_boost_level");
#endif
void __iomem *mali_qos_reg_base_mtx_m0;
void __iomem *mali_qos_reg_base_mtx_m1;
void __iomem *mali_qos_reg_base_apb_rf;
#ifdef CONFIG_MALI_DEVFREQ
static void InitFreqStats(struct kbase_device *kbdev)
{
	int i = 0;

	kbdev->enable_freq_stats = 0;
	kbdev->freq_num = gpu_dvfs_ctx.freq_list_len;
	kbdev->freq_stats = vmalloc(sizeof(struct kbase_devfreq_stats) * kbdev->freq_num);
	KBASE_DEBUG_ASSERT(kbdev->freq_stats);

	for (i = 0; i < kbdev->freq_num; i++) {
		kbdev->freq_stats[i].freq = gpu_dvfs_ctx.freq_list[i].freq * FREQ_KHZ;
		kbdev->freq_stats[i].busy_time = 0;
		kbdev->freq_stats[i].total_time = 0;
	}
}

static void DeinitFreqStats(struct kbase_device *kbdev)
{
	if (NULL != kbdev->freq_stats) {
		vfree(kbdev->freq_stats);
		kbdev->freq_stats = NULL;
	}
}

#endif

static void gpu_pmic_get_regmap(void)
{
	struct device_node *regmap_np;
	struct platform_device *regmap_pdev;

	regmap_np = of_find_compatible_node(NULL, NULL, "sprd,ump962x-syscon");
	if(!regmap_np) {
		pr_err("%s: unable to get pmic syscon node\n",__func__);
		return;
	}

	regmap_pdev = of_find_device_by_node(regmap_np);
	if(!regmap_pdev) {
		of_node_put(regmap_np);
		pr_err("%s: unable to get pmic syscon platform device\n",__func__);
		return;
	}
	gpu_dvfs_ctx.dcdc_gpu_pd.regmap_ptr = dev_get_regmap(regmap_pdev->dev.parent, NULL);
	if (!gpu_dvfs_ctx.dcdc_gpu_pd.regmap_ptr) {
		pr_err("%s: unable to get pmic regmap device\n",__func__);
	}

	of_node_put(regmap_np);
	put_device(&regmap_pdev->dev);

	return;
}

static inline void mali_freq_init(struct device *dev)
{
	int i = 0, clk_cnt = 0, ret;
	//struct device_node *qos_node = NULL;

	gpu_dvfs_ctx.sip = sprd_sip_svc_get_handle();
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.sip);
	gpu_dvfs_ctx.sip->gpu_ops.get_id(&gpu_dvfs_ctx.chip_id, &gpu_dvfs_ctx.gpu_bin);
	pr_info (" %s chip_id:%d,gpu_bin:%d\n", __func__, gpu_dvfs_ctx.chip_id, gpu_dvfs_ctx.gpu_bin);

	//gpu_dvfs_ctx.gpu_reg_ptr = devm_regulator_get(dev, "gpu");
	//KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_reg_ptr);

	gpu_dvfs_ctx.gpu_soft_rst = devm_reset_control_get(dev, "gpu_soft_rst");
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_soft_rst);

	gpu_dvfs_ctx.top_dvfs_cfg_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"top_dvfs_cfg", 2, (uint32_t *)gpu_dvfs_ctx.top_dvfs_cfg_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.top_dvfs_cfg_reg.regmap_ptr);

	gpu_dvfs_ctx.gpu_sw_dvfs_ctrl_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"gpu_sw_dvfs_ctrl", 2, (uint32_t *)gpu_dvfs_ctx.gpu_sw_dvfs_ctrl_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_sw_dvfs_ctrl_reg.regmap_ptr);

	gpu_dvfs_ctx.dcdc_gpu_pd.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"dcdc_gpu_pd", 2, (uint32_t *)gpu_dvfs_ctx.dcdc_gpu_pd.args);
	gpu_pmic_get_regmap();//write real pmic syscon
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.dcdc_gpu_pd.regmap_ptr);

	//enable gpu hw dvfs
	regmap_update_bits(gpu_dvfs_ctx.top_dvfs_cfg_reg.regmap_ptr, gpu_dvfs_ctx.top_dvfs_cfg_reg.args[0], gpu_dvfs_ctx.top_dvfs_cfg_reg.args[1], ~gpu_dvfs_ctx.top_dvfs_cfg_reg.args[1]);
	regmap_update_bits(gpu_dvfs_ctx.gpu_sw_dvfs_ctrl_reg.regmap_ptr, gpu_dvfs_ctx.gpu_sw_dvfs_ctrl_reg.args[0], gpu_dvfs_ctx.gpu_sw_dvfs_ctrl_reg.args[1], ~gpu_dvfs_ctx.gpu_sw_dvfs_ctrl_reg.args[1]);

	gpu_dvfs_ctx.top_force_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"top_force_shutdown", 2, (uint32_t *)gpu_dvfs_ctx.top_force_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.top_force_reg.regmap_ptr);

	gpu_dvfs_ctx.gpu_top_state_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"gpu_top_state", 2, (uint32_t *)gpu_dvfs_ctx.gpu_top_state_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_top_state_reg.regmap_ptr);

	gpu_dvfs_ctx.clk_core_gpu_eb_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"clk_core_gpu_eb", 2, (uint32_t *)gpu_dvfs_ctx.clk_core_gpu_eb_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.clk_core_gpu_eb_reg.regmap_ptr);

	gpu_dvfs_ctx.gpu_core0_state_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"gpu_core0_state", 2, (uint32_t *)gpu_dvfs_ctx.gpu_core0_state_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_core0_state_reg.regmap_ptr);

	gpu_dvfs_ctx.gpu_core1_state_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"gpu_core1_state", 2, (uint32_t *)gpu_dvfs_ctx.gpu_core1_state_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_core1_state_reg.regmap_ptr);

	gpu_dvfs_ctx.gpu_core2_state_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"gpu_core2_state", 2, (uint32_t *)gpu_dvfs_ctx.gpu_core2_state_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_core2_state_reg.regmap_ptr);

	gpu_dvfs_ctx.gpu_core3_state_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"gpu_core3_state", 2, (uint32_t *)gpu_dvfs_ctx.gpu_core3_state_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_core3_state_reg.regmap_ptr);

	gpu_dvfs_ctx.cur_st_st0_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"cur_st_st0", 2, (uint32_t *)gpu_dvfs_ctx.cur_st_st0_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.cur_st_st0_reg.regmap_ptr);

	gpu_dvfs_ctx.cur_st_st1_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"cur_st_st1", 2, (uint32_t *)gpu_dvfs_ctx.cur_st_st1_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.cur_st_st1_reg.regmap_ptr);

	gpu_dvfs_ctx.cur_st_st2_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"cur_st_st2", 2, (uint32_t *)gpu_dvfs_ctx.cur_st_st2_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.cur_st_st2_reg.regmap_ptr);

	gpu_dvfs_ctx.cur_st_st3_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"cur_st_st3", 2, (uint32_t *)gpu_dvfs_ctx.cur_st_st3_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.cur_st_st3_reg.regmap_ptr);

	gpu_dvfs_ctx.gpll_cfg_force_off_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"gpll_cfg_frc_off", 2, (uint32_t *)gpu_dvfs_ctx.gpll_cfg_force_off_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpll_cfg_force_off_reg.regmap_ptr);

	gpu_dvfs_ctx.gpll_cfg_force_on_reg.regmap_ptr = syscon_regmap_lookup_by_phandle_args(dev->of_node,"gpll_cfg_frc_on", 2, (uint32_t *)gpu_dvfs_ctx.gpll_cfg_force_on_reg.args);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpll_cfg_force_on_reg.regmap_ptr);

	gpu_dvfs_ctx.gpu_apb_base_ptr = syscon_regmap_lookup_by_phandle(dev->of_node, "sprd,gpu-apb-syscon");
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_apb_base_ptr);
	gpu_dvfs_ctx.gpu_dvfs_apb_base_ptr = syscon_regmap_lookup_by_phandle(dev->of_node, "sprd,gpu-dvfs-apb-syscon");
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_dvfs_apb_base_ptr);

	gpu_dvfs_ctx.bridge_trans_idle_reg.regmap_ptr = gpu_dvfs_ctx.gpu_apb_base_ptr;
	gpu_dvfs_ctx.bridge_trans_idle_reg.args[0] = REG_GPU_APB_RF_ASYBC_BRIDGE_TOP_W;
#if 0
	//qos
	gpu_dvfs_ctx.gpu_qos_sel.regmap_ptr = gpu_dvfs_ctx.gpu_apb_base_ptr;
	gpu_dvfs_ctx.gpu_qos_sel.args[0] = REG_GPU_APB_RF_GPU_NIC400_QOS;
	gpu_dvfs_ctx.gpu_qos_sel.args[1] = MASK_GPU_APB_RF_GPU_QOS_SEL;

	gpu_dvfs_ctx.gpu_qos.regmap_ptr = gpu_dvfs_ctx.gpu_apb_base_ptr;
	gpu_dvfs_ctx.gpu_qos.args[0] = REG_GPU_APB_RF_GPU_NIC400_QOS;
	gpu_dvfs_ctx.gpu_qos.args[1] = MASK_GPU_APB_RF_AWQOS_THRESHOLD_GPU | MASK_GPU_APB_RF_ARQOS_THRESHOLD_GPU | MASK_GPU_APB_RF_AWQOS_GPU | MASK_GPU_APB_RF_ARQOS_GPU;

	/* qos dts parse */
	qos_node = of_parse_phandle(dev->of_node, "sprd,qos", 0);
	if (qos_node)
	{
		if (of_property_read_u8(qos_node, "arqos", &gpu_qos_cfg.arqos)) {
			pr_warn("gpu arqos config reading fail.\n");
		}
		if (of_property_read_u8(qos_node, "awqos", &gpu_qos_cfg.awqos)) {
			pr_warn("gpu awqos config reading fail.\n");
		}
		if (of_property_read_u8(qos_node, "arqos-threshold", &gpu_qos_cfg.arqos_threshold)) {
			pr_warn("gpu arqos_threshold config reading fail.\n");
		}
		if (of_property_read_u8(qos_node, "awqos-threshold", &gpu_qos_cfg.awqos_threshold)) {
			pr_warn("gpu awqos_threshold config reading fail.\n");
		}
	} else {
		pr_warn("can't find gpu qos config node\n");
	}
#endif

	//gpu index cfg
	gpu_dvfs_ctx.dvfs_index_cfg.regmap_ptr = gpu_dvfs_ctx.gpu_dvfs_apb_base_ptr;
	gpu_dvfs_ctx.dvfs_index_cfg.args[0] = REG_GPU_DVFS_APB_RF_GPU_DVFS_INDEX_CFG;
	gpu_dvfs_ctx.dvfs_index_cfg.args[1] = MASK_GPU_DVFS_APB_RF_GPU_DVFS_INDEX;

	//sw dvfs ctrl
	gpu_dvfs_ctx.sw_dvfs_ctrl.regmap_ptr = gpu_dvfs_ctx.gpu_dvfs_apb_base_ptr;
	gpu_dvfs_ctx.sw_dvfs_ctrl.args[0] = REG_GPU_DVFS_APB_RF_GPU_SW_DVFS_CTRL;
	gpu_dvfs_ctx.sw_dvfs_ctrl.args[1] = MASK_GPU_DVFS_APB_RF_GPU_DVFS_ACK | MASK_GPU_DVFS_APB_RF_GPU_DVFS_VOLTAGE_SW | MASK_GPU_DVFS_APB_RF_GPU_DVFS_REQ_SW;

	//freq update cfg
	gpu_dvfs_ctx.freq_upd_cfg.regmap_ptr = gpu_dvfs_ctx.gpu_dvfs_apb_base_ptr;
	gpu_dvfs_ctx.freq_upd_cfg.args[0] = REG_GPU_DVFS_APB_RF_GPU_FREQ_UPD_TYPE_CFG0;
	gpu_dvfs_ctx.freq_upd_cfg.args[1] = MASK_GPU_DVFS_APB_RF_GPU_FREQ_UPD_HDSK_EN | MASK_GPU_DVFS_APB_RF_GPU_FREQ_UPD_DELAY_EN;

	//core index6 map sel
	gpu_dvfs_ctx.core_index6_map_sel.regmap_ptr = gpu_dvfs_ctx.gpu_dvfs_apb_base_ptr;
	gpu_dvfs_ctx.core_index6_map_sel.args[0] = REG_GPU_DVFS_APB_RF_GPU_INDEX6_MAP;
	gpu_dvfs_ctx.core_index6_map_sel.args[1] = MASK_GPU_DVFS_APB_RF_GPU_INDEX6_MAP_SEL;


	gpu_dvfs_ctx.clk_gpu_i = of_clk_get(dev->of_node, 0);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.clk_gpu_i);

	clk_cnt = of_clk_get_parent_count(dev->of_node);
	gpu_dvfs_ctx.gpu_clk_num = clk_cnt - DTS_CLK_OFFSET;

	gpu_dvfs_ctx.gpu_clk_src = vmalloc(sizeof(struct clk*) * gpu_dvfs_ctx.gpu_clk_num);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_clk_src);

	for (i = 0; i < gpu_dvfs_ctx.gpu_clk_num; i++) {
		gpu_dvfs_ctx.gpu_clk_src[i] = of_clk_get(dev->of_node, i+DTS_CLK_OFFSET);
		KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.gpu_clk_src[i]);
	}

	gpu_dvfs_ctx.freq_list_len = of_property_count_elems_of_size(dev->of_node,"operating-points",2*sizeof(u32));
	gpu_dvfs_ctx.freq_list = vmalloc(sizeof(struct gpu_freq_info) * gpu_dvfs_ctx.freq_list_len);
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.freq_list);

	for(i = 0; i < gpu_dvfs_ctx.freq_list_len; i++) {
		gpu_dvfs_ctx.freq_list[i].clk_src = gpu_dvfs_ctx.gpu_clk_src[i];
		KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.freq_list[i].clk_src);
		ret = of_property_read_u32_index(dev->of_node, "operating-points", 2*i,   &gpu_dvfs_ctx.freq_list[i].freq);
		if(ret)pr_err("%s get operating-points %d read failed %d",__func__, 2*i,ret);
		ret = of_property_read_u32_index(dev->of_node, "operating-points", 2*i+1, &gpu_dvfs_ctx.freq_list[i].volt);
		if(ret)pr_err("%s get operating-points %d read failed %d",__func__, 2*i+1,ret);
		gpu_dvfs_ctx.freq_list[i].dvfs_index = i;
	}

	//T760 table: 384M, 512M, 650M
	//T770 table: 384M, 512M, 680M, 780M
	//T790 table: 384M, 512M, 680M, 850M
	//default table: 384M, 512M, 680M, 850M
	if (gpu_dvfs_ctx.chip_id == 3 || gpu_dvfs_ctx.chip_id == 2) { //T760
		//modify 680M to 650M
		gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-2].freq = GPLL_655M / FREQ_KHZ;

		//remove 850M
		memset(&gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1], 0, sizeof(struct gpu_freq_info));

		//modify freq list len
		gpu_dvfs_ctx.freq_list_len = gpu_dvfs_ctx.freq_list_len - 1;
		//compute the gpll freq
		gpll_1638M = GPLL_655M * 2.5;
	} else if (gpu_dvfs_ctx.chip_id == 1) { //T770
		//modify max-freq 850M to 780M
		memcpy(&gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1],
				&gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-2],
				sizeof(struct gpu_freq_info));
		gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].freq = GPLL_780M / FREQ_KHZ;
		//modify freq index
		gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].dvfs_index = gpu_dvfs_ctx.freq_list_len - 1;
		//compute the gpll freq
		gpll_1950M = GPLL_780M * 2.5;
		gpll_1716M = GPLL_686M * 2.5;
	}

	gpu_dvfs_ctx.sip->gpu_ops.update_voltage_list(gpu_dvfs_ctx.gpu_temperature, gpu_dvfs_ctx.pos);

	ret = of_property_read_u32(dev->of_node, "sprd,dvfs-default", &i);
	if(ret) pr_err("%s get dvfs-default failed %d",__func__,ret);
	gpu_dvfs_ctx.freq_default = &gpu_dvfs_ctx.freq_list[i];
	KBASE_DEBUG_ASSERT(gpu_dvfs_ctx.freq_default);

	gpu_dvfs_ctx.cur_index = GPU_MIN_FREQ_INDEX;
	gpu_dvfs_ctx.freq_cur = &gpu_dvfs_ctx.freq_list[GPU_MIN_FREQ_INDEX];
	gpu_dvfs_ctx.cur_voltage = gpu_dvfs_ctx.freq_cur->volt;

	atomic_set(&gpu_dvfs_ctx.gpu_power_state, 0);
	atomic_set(&gpu_dvfs_ctx.gpu_clock_state, 0);
}
/* SPRD:gpu_qos_reg_map */
static void gpu_qos_reg_map(void)
{
       u32 qos_offset = 0x100;
       mali_qos_reg_base_mtx_m0 = ioremap(nic400_gpu_sys_mtx_m0_qos_list[0].base_addr,qos_offset);
       mali_qos_reg_base_mtx_m1 = ioremap(nic400_gpu_sys_mtx_m1_qos_list[0].base_addr,qos_offset);
       mali_qos_reg_base_apb_rf = ioremap(gpu_apb_rf_qos_list[0].base_addr,qos_offset);
}
/* SPRD:gpu_qos_reg_unmap */
static void gpu_qos_reg_unmap(void)
{
       iounmap(mali_qos_reg_base_mtx_m0);
       iounmap(mali_qos_reg_base_mtx_m1);
       iounmap(mali_qos_reg_base_apb_rf);
}

//PCIe访问DDR优先级-各个sys modules需要独立管理配置Qos,目前按照要求在各个模块内统一配置
//SPRD note:register 0x23018000 do not support set/clear features, so not use dts reg configuration
#if 1
static void mali_kbase_gpu_sys_qos_reg_rw(struct qos_reg gpu_qos_list_name[], int len)
{
	int i;
	u32 tmp;
	void __iomem *mali_qos_reg_base;

	if (gpu_qos_list_name[0].base_addr == nic400_gpu_sys_mtx_m0_qos_list[0].base_addr)
		mali_qos_reg_base = mali_qos_reg_base_mtx_m0;
	else if (gpu_qos_list_name[0].base_addr == nic400_gpu_sys_mtx_m1_qos_list[0].base_addr)
		mali_qos_reg_base = mali_qos_reg_base_mtx_m1;
	else
		mali_qos_reg_base = mali_qos_reg_base_apb_rf;

	len = len - 1;

	for (i = 0; i < len; i++) {
		tmp = readl_relaxed(mali_qos_reg_base +
				    (gpu_qos_list_name[i].base_addr -
				    gpu_qos_list_name[0].base_addr));
		tmp &= ~(gpu_qos_list_name[i].mask_value);
		tmp |= gpu_qos_list_name[i].set_value;
		writel_relaxed(tmp, mali_qos_reg_base +
			       (gpu_qos_list_name[i].base_addr -
			       gpu_qos_list_name[0].base_addr));
	}
}

static void mali_kbase_gpu_sys_qos_cfg(void)
{
	mali_kbase_gpu_sys_qos_reg_rw(nic400_gpu_sys_mtx_m0_qos_list,
				 sizeof(nic400_gpu_sys_mtx_m0_qos_list) /
				 sizeof(nic400_gpu_sys_mtx_m0_qos_list[0]));
	mali_kbase_gpu_sys_qos_reg_rw(nic400_gpu_sys_mtx_m1_qos_list,
				 sizeof(nic400_gpu_sys_mtx_m1_qos_list) /
				 sizeof(nic400_gpu_sys_mtx_m1_qos_list[0]));
	mali_kbase_gpu_sys_qos_reg_rw(gpu_apb_rf_qos_list,
				 sizeof(gpu_apb_rf_qos_list) /
				 sizeof(gpu_apb_rf_qos_list[0]));
}
#endif
u32 top_pwr = 0;
u32 top_pwr_mask = MOVE_BIT_LEFT(0x1f,16);
u32 top_pwr_wakeup = MOVE_BIT_LEFT(0,16);

static inline int mali_top_state_check(void)
{
	int counter = 0;//count the time of check
	int res = 0;
	//check if the top_state ready or not
	regmap_read(gpu_dvfs_ctx.gpu_top_state_reg.regmap_ptr, gpu_dvfs_ctx.gpu_top_state_reg.args[0], &top_pwr);
	top_pwr = top_pwr & top_pwr_mask;
	while(top_pwr != top_pwr_wakeup) {
		udelay(50);
		regmap_read(gpu_dvfs_ctx.gpu_top_state_reg.regmap_ptr, gpu_dvfs_ctx.gpu_top_state_reg.args[0], &top_pwr);
		top_pwr = top_pwr & top_pwr_mask;
		printk(KERN_ERR "SPRDDEBUG gpu_top_pwr = 0x%x, counter = %d\n ", top_pwr, counter);
		if(counter++ > 200) {
			printk(KERN_ERR "gpu top power on is timeout ! ");
			WARN_ON(1);
			res = -1;
			return res;
		}

	}
	return res;
}

static inline void mali_check_bridge_trans_idle(void)
{
	u32 bridge_trans_idle_bit = 0;
	int counter = 0;

	regmap_read(gpu_dvfs_ctx.bridge_trans_idle_reg.regmap_ptr, gpu_dvfs_ctx.bridge_trans_idle_reg.args[0], &bridge_trans_idle_bit);
	bridge_trans_idle_bit = ((bridge_trans_idle_bit & MASK_GPU_APB_RF_BRIDGE_TRANS_IDLE) >> 1);

	while(bridge_trans_idle_bit == 0)
	{
		udelay(10);
		regmap_read(gpu_dvfs_ctx.bridge_trans_idle_reg.regmap_ptr, gpu_dvfs_ctx.bridge_trans_idle_reg.args[0], &bridge_trans_idle_bit);
		bridge_trans_idle_bit = ((bridge_trans_idle_bit & MASK_GPU_APB_RF_BRIDGE_TRANS_IDLE) >> 1);
		printk(KERN_ERR "SPRDDEBUG mali gpu bridge_trans_idle_bit is FALSE:0(not idle), cannot clock off and power off, counter = %d\n ", counter);
		if(counter++ > 200)
		{
			printk(KERN_ERR "gpu polling mali_check_bridge_trans_idle timeout !");
			WARN_ON(1);
			return;
		}
	}

	udelay(10);
	return;
}

int kbase_platform_update_dvfs_table(struct kbase_device *kbdev)
{
	int ret;

	ret = thermal_zone_get_temp(kbdev->gpu_tz, &gpu_dvfs_ctx.gpu_temperature);
	if (ret){
		printk(KERN_ERR "failed to get the temp of gpu thmzone.\n");
		return -1;
	}
	down(gpu_dvfs_ctx.sem);
	if (atomic_read(&gpu_dvfs_ctx.gpu_power_state) && atomic_read(&gpu_dvfs_ctx.gpu_clock_state)) {
		if ((gpu_dvfs_ctx.last_gpu_temperature <= 65000 && gpu_dvfs_ctx.gpu_temperature >= 65000) ||
			(gpu_dvfs_ctx.last_gpu_temperature >= 65000 && gpu_dvfs_ctx.gpu_temperature <= 65000)) {
			if (mali_top_state_check() != 0) {
				printk(KERN_ERR "mali GPU_set_DVFS_table failed.\n");
				up(gpu_dvfs_ctx.sem);
				return -1;
			} else
				printk(KERN_ERR "mali GPU_set_DVFS_table %s gpu_power_state = %d gpu_clock_state = %d,gpu_temperature = %d, last_gpu_temperature = %d.\n",
						 __func__, atomic_read(&gpu_dvfs_ctx.gpu_power_state), atomic_read(&gpu_dvfs_ctx.gpu_clock_state), gpu_dvfs_ctx.gpu_temperature, gpu_dvfs_ctx.last_gpu_temperature);

			if(gpu_dvfs_ctx.gpu_temperature < 0){
				gpu_dvfs_ctx.gpu_temperature = - gpu_dvfs_ctx.gpu_temperature;
				gpu_dvfs_ctx.pos = 0;
			}
			gpu_dvfs_ctx.sip->gpu_ops.update_voltage_list(gpu_dvfs_ctx.gpu_temperature, gpu_dvfs_ctx.pos);

			gpu_dvfs_ctx.last_gpu_temperature = gpu_dvfs_ctx.gpu_temperature;
		}
	}
	up(gpu_dvfs_ctx.sem);
	return 0;
}

/*gpu_soft_reset function*/
static void gpu_reset_assert(struct reset_control* rst_ctrl)
{
	int ret = 0;
	ret = reset_control_assert(rst_ctrl);
	if (ret)
		printk(KERN_ERR "GPU_RESET %s error = %d\n", __func__, ret);

}

static void gpu_reset_deassert(struct reset_control* rst_ctrl)
{
	int ret = 0;
	ret = reset_control_deassert(rst_ctrl);
	if (ret)
		printk(KERN_ERR "GPU_RESET %s error = %d\n", __func__, ret);

}
static inline void mali_power_on(void)
{
	//gpll force off: 0:not force off; 1:force off
	regmap_update_bits(gpu_dvfs_ctx.gpll_cfg_force_off_reg.regmap_ptr, gpu_dvfs_ctx.gpll_cfg_force_off_reg.args[0], gpu_dvfs_ctx.gpll_cfg_force_off_reg.args[1], ~gpu_dvfs_ctx.gpll_cfg_force_off_reg.args[1]);
	udelay(100);
	//gpll force on: 0:not force on; 1:force on
	regmap_update_bits(gpu_dvfs_ctx.gpll_cfg_force_on_reg.regmap_ptr, gpu_dvfs_ctx.gpll_cfg_force_on_reg.args[0], gpu_dvfs_ctx.gpll_cfg_force_on_reg.args[1], gpu_dvfs_ctx.gpll_cfg_force_on_reg.args[1]);
	udelay(150);

	//dcdcgpu_on [3] 0: enable 1: power down
	regmap_update_bits(gpu_dvfs_ctx.dcdc_gpu_pd.regmap_ptr, gpu_dvfs_ctx.dcdc_gpu_pd.args[0], gpu_dvfs_ctx.dcdc_gpu_pd.args[1], ~gpu_dvfs_ctx.dcdc_gpu_pd.args[1]);
	udelay(400);
	/*
	//gpu regulator enable
	if (!regulator_is_enabled(gpu_dvfs_ctx.gpu_reg_ptr))
	{
		regulator_enable(gpu_dvfs_ctx.gpu_reg_ptr);
		udelay(400);
 	}
	*/

	regmap_update_bits(gpu_dvfs_ctx.top_force_reg.regmap_ptr, gpu_dvfs_ctx.top_force_reg.args[0], gpu_dvfs_ctx.top_force_reg.args[1], ~gpu_dvfs_ctx.top_force_reg.args[1]);

	//reset gpu sys
	gpu_reset_assert(gpu_dvfs_ctx.gpu_soft_rst);
	udelay(10);
	gpu_reset_deassert(gpu_dvfs_ctx.gpu_soft_rst);
	udelay(300);
	//check if the top_state ready or not
	mali_top_state_check();

	atomic_inc(&gpu_dvfs_ctx.gpu_power_state);
}


static inline void mali_power_off(void)
{

	atomic_dec(&gpu_dvfs_ctx.gpu_power_state);

	regmap_update_bits(gpu_dvfs_ctx.top_force_reg.regmap_ptr, gpu_dvfs_ctx.top_force_reg.args[0], gpu_dvfs_ctx.top_force_reg.args[1], gpu_dvfs_ctx.top_force_reg.args[1]);

	//dcdcgpu_on [3] 0: enable 1: power down
	regmap_update_bits(gpu_dvfs_ctx.dcdc_gpu_pd.regmap_ptr, gpu_dvfs_ctx.dcdc_gpu_pd.args[0], gpu_dvfs_ctx.dcdc_gpu_pd.args[1], gpu_dvfs_ctx.dcdc_gpu_pd.args[1]);
	udelay(10);
	/*
	//gpu regulator disable
	if (regulator_is_enabled(gpu_dvfs_ctx.gpu_reg_ptr))
	{
		regulator_disable(gpu_dvfs_ctx.gpu_reg_ptr);
		udelay(10);
	}
	*/
	//gpll force off: 0:not force off; 1:force off
	regmap_update_bits(gpu_dvfs_ctx.gpll_cfg_force_off_reg.regmap_ptr, gpu_dvfs_ctx.gpll_cfg_force_off_reg.args[0], gpu_dvfs_ctx.gpll_cfg_force_off_reg.args[1], gpu_dvfs_ctx.gpll_cfg_force_off_reg.args[1]);
	//gpll force on: 0:not force on; 1:force on
	regmap_update_bits(gpu_dvfs_ctx.gpll_cfg_force_on_reg.regmap_ptr, gpu_dvfs_ctx.gpll_cfg_force_on_reg.args[0], gpu_dvfs_ctx.gpll_cfg_force_on_reg.args[1], ~gpu_dvfs_ctx.gpll_cfg_force_on_reg.args[1]);
}

#if 0
static void maliQosConfig(void)
{
	regmap_update_bits(gpu_dvfs_ctx.gpu_qos_sel.regmap_ptr, gpu_dvfs_ctx.gpu_qos_sel.args[0], gpu_dvfs_ctx.gpu_qos_sel.args[1], gpu_dvfs_ctx.gpu_qos_sel.args[1]);
	regmap_update_bits(gpu_dvfs_ctx.gpu_qos.regmap_ptr, gpu_dvfs_ctx.gpu_qos.args[0], gpu_dvfs_ctx.gpu_qos.args[1], ((gpu_qos_cfg.awqos_threshold << 12) | (gpu_qos_cfg.arqos_threshold << 8) | (gpu_qos_cfg.awqos << 4) | gpu_qos_cfg.arqos));
}
#endif

static inline void mali_clock_on(void)
{
	int i, ret = 0;
	static int clock_on_count = 0;

	//enable all clocks
	for(i = 0; i < gpu_dvfs_ctx.gpu_clk_num; i++) {
		clk_prepare_enable(gpu_dvfs_ctx.gpu_clk_src[i]);
	}
	clk_prepare_enable(gpu_dvfs_ctx.clk_gpu_i);

	// put register map into normal mode
	regcache_cache_only(gpu_dvfs_ctx.gpu_apb_base_ptr, false);
	regcache_cache_only(gpu_dvfs_ctx.gpu_dvfs_apb_base_ptr, false);
	udelay(200);

	//enable gpu clock
	regmap_update_bits(gpu_dvfs_ctx.clk_core_gpu_eb_reg.regmap_ptr, gpu_dvfs_ctx.clk_core_gpu_eb_reg.args[0],
						gpu_dvfs_ctx.clk_core_gpu_eb_reg.args[1], gpu_dvfs_ctx.clk_core_gpu_eb_reg.args[1]);
	udelay(400);
	//check if the top_state ready or not
	mali_top_state_check();

	//set core index map and clk rate
	//GPU_INDEX_MAP [9:6]default index0-0, index1-0, index2-0, index3-1, index4-2, index5-3,index6-4;
	if (gpu_dvfs_ctx.chip_id == 3) {
		ret = clk_set_rate(gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].clk_src,  gpll_1638M);
		if (ret != 0)
			printk(KERN_ERR "%s clk_set_rate failed error = %d\n", __func__, ret);
	} else if (gpu_dvfs_ctx.chip_id == 1)
		regmap_update_bits(gpu_dvfs_ctx.core_index6_map_sel.regmap_ptr, gpu_dvfs_ctx.core_index6_map_sel.args[0], gpu_dvfs_ctx.core_index6_map_sel.args[1], 0x5);

	//update freq cfg
	regmap_update_bits(gpu_dvfs_ctx.freq_upd_cfg.regmap_ptr, gpu_dvfs_ctx.freq_upd_cfg.args[0], gpu_dvfs_ctx.freq_upd_cfg.args[1], 1);

	//init freq
	regmap_update_bits(gpu_dvfs_ctx.dvfs_index_cfg.regmap_ptr, gpu_dvfs_ctx.dvfs_index_cfg.args[0], gpu_dvfs_ctx.dvfs_index_cfg.args[1], gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.cur_index].dvfs_index);

#if 0
	//qos
	maliQosConfig();
#endif
#if 1
        /* SPRD:qos_reg_map */
    if(clock_on_count == 0) {
    	gpu_qos_reg_map();
		clock_on_count++;
    }
    //SPRD:PCIe Qos Config gpu_sys_qos_config
    mali_kbase_gpu_sys_qos_cfg();
#endif
	udelay(200);
	atomic_inc(&gpu_dvfs_ctx.gpu_clock_state);
}

static inline void mali_clock_off(void)
{
	int i;

	mali_check_bridge_trans_idle();

	atomic_dec(&gpu_dvfs_ctx.gpu_clock_state);

	//disable gpu clock
	regmap_update_bits(gpu_dvfs_ctx.clk_core_gpu_eb_reg.regmap_ptr, gpu_dvfs_ctx.clk_core_gpu_eb_reg.args[0],
						gpu_dvfs_ctx.clk_core_gpu_eb_reg.args[1], ~gpu_dvfs_ctx.clk_core_gpu_eb_reg.args[1]);

	// put register map into cache only mode
	regcache_cache_only(gpu_dvfs_ctx.gpu_apb_base_ptr, true);
	regcache_cache_only(gpu_dvfs_ctx.gpu_dvfs_apb_base_ptr, true);
	udelay(200);

	clk_disable_unprepare(gpu_dvfs_ctx.clk_gpu_i);

	//disable all clocks
	for(i = 0; i < gpu_dvfs_ctx.gpu_clk_num; i++) {
		clk_disable_unprepare(gpu_dvfs_ctx.gpu_clk_src[i]);
	}
}

static int mali_platform_init(struct kbase_device *kbdev)
{
	//gpu freq
	mali_freq_init(kbdev->dev);

#ifdef CONFIG_MALI_DEVFREQ
	InitFreqStats(kbdev);
	kbase_pm_statistics_FreqInit(kbdev);
#endif

	mali_power_on();

	//clock on
	mali_clock_on();

	return 0;
}

static void mali_platform_term(struct kbase_device *kbdev)
{
	down(gpu_dvfs_ctx.sem);

	//clock off
	mali_clock_off();

	//power off
	mali_power_off();

#ifdef CONFIG_MALI_DEVFREQ
	kbase_pm_statistics_FreqDeinit(kbdev);
	DeinitFreqStats(kbdev);
#endif
    /* SPRD:gpu_qos_unmap */
    gpu_qos_reg_unmap();
	//free
	vfree(gpu_dvfs_ctx.freq_list);
	vfree(gpu_dvfs_ctx.gpu_clk_src);

	up(gpu_dvfs_ctx.sem);
}

struct kbase_platform_funcs_conf platform_qogirn6pro_funcs = {
	.platform_init_func = mali_platform_init,
	.platform_term_func = mali_platform_term
};

static void mali_power_mode_change(struct kbase_device *kbdev, int power_mode)
{
	down(gpu_dvfs_ctx.sem);
	//dev_info(kbdev->dev, "mali_power_mode_change: %d, gpu_power_state=%d gpu_clock_state=%d",power_mode,gpu_dvfs_ctx.gpu_power_state,gpu_dvfs_ctx.gpu_clock_state);
	switch (power_mode) {
	case POWER_ON:
		if (!atomic_read(&gpu_dvfs_ctx.gpu_power_state)) {
			mali_power_on();
			mali_clock_on();
		}

		if (!atomic_read(&gpu_dvfs_ctx.gpu_clock_state))
			mali_clock_on();
		break;

	case LIGHT_SLEEP:
	case DEEP_SLEEP:
		if(atomic_read(&gpu_dvfs_ctx.gpu_clock_state))
			mali_clock_off();

		if(atomic_read(&gpu_dvfs_ctx.gpu_power_state))
			mali_power_off();

		break;

		default:
			break;
	}

	kbase_pm_set_statistics(kbdev, power_mode);
	up(gpu_dvfs_ctx.sem);
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
#ifdef KBASE_PM_RUNTIME
	int res;

	res = pm_runtime_put_sync(kbdev->dev);
	if (res < 0)
		printk(KERN_ERR "mali----pm_runtime_put_sync return (%d)\n", res);

#endif

	mali_power_mode_change(kbdev, 1);
}

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	mali_power_mode_change(kbdev, 0);

#ifdef KBASE_PM_RUNTIME
	{
		int res;

		res = pm_runtime_get_sync(kbdev->dev);
		if (res < 0)
			printk(KERN_ERR "mali----pm_runtime_get_sync return (%d)\n", res);
	}
#endif

	return 1;
}

static void pm_callback_power_suspend(struct kbase_device *kbdev)
{
	mali_power_mode_change(kbdev, 2);
}

static void pm_callback_power_resume(struct kbase_device *kbdev)
{
	mali_power_mode_change(kbdev, 0);
}

#ifdef KBASE_PM_RUNTIME
static int pm_callback_power_runtime_init(struct kbase_device *kbdev)
{
	pm_runtime_set_active(kbdev->dev);
	pm_suspend_ignore_children(kbdev->dev, true);
	pm_runtime_set_autosuspend_delay(kbdev->dev, PM_RUNTIME_DELAY_MS);
	pm_runtime_use_autosuspend(kbdev->dev);
	pm_runtime_enable(kbdev->dev);

	return 0;
}

static void pm_callback_power_runtime_term(struct kbase_device *kbdev)
{
	pm_runtime_disable(kbdev->dev);
}
#endif/*CONFIG_PM_RUNTIME*/

static void pm_callback_shader_polling(struct kbase_device *kbdev);
static void pm_callback_set_pwr_override1(struct kbase_device *kbdev);

struct kbase_pm_callback_conf pm_qogirn6pro_callbacks = {
	.power_off_callback = pm_callback_power_off,
	.power_on_callback = pm_callback_power_on,
	.power_suspend_callback = pm_callback_power_suspend,
	.power_resume_callback = pm_callback_power_resume,
	.power_shader_polling_callback = pm_callback_shader_polling,
	.power_set_pwr_override1_callback = pm_callback_set_pwr_override1,
#ifdef KBASE_PM_RUNTIME
	.power_runtime_init_callback = pm_callback_power_runtime_init,
	.power_runtime_term_callback = pm_callback_power_runtime_term,
	.power_runtime_off_callback = NULL,
	.power_runtime_on_callback = NULL
#endif
};


static struct kbase_platform_config versatile_platform_config = {
};

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &versatile_platform_config;
}

int kbase_platform_early_init(void)
{
	/* Nothing needed at this stage */
	return 0;
}

#if defined(CONFIG_MALI_DEVFREQ)
static int freq_search(struct gpu_freq_info freq_list[], int len, int key)
{
	int low=0, high=len-1, mid;

	if (0 > key) {
		return -1;
	}

	while(low <= high) {
		mid = (low+high) / 2;
		if(key == freq_list[mid].freq)
			return mid;
		else if(key < freq_list[mid].freq)
			high = mid-1;
		else
			low = mid+1;
	}
	return -1;
}

int kbase_platform_get_init_freq(void)
{
	return (gpu_dvfs_ctx.freq_list[GPU_MIN_FREQ_INDEX].freq * FREQ_KHZ);
}

int kbase_platform_get_min_freq(void)
{
	return (gpu_dvfs_ctx.freq_list[GPU_MIN_FREQ_INDEX].freq * FREQ_KHZ);
}

int kbase_platform_get_max_freq(void)
{
	return (gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len -1].freq * FREQ_KHZ);
}

void kbase_platform_limit_max_freq(struct device *dev)
{
	int ret = 0;

	//disable 26M/76.8M/153.6M
	dev_pm_opp_disable(dev, GPU_26M_FREQ);
	dev_pm_opp_disable(dev, GPU_76M8_FREQ);
	dev_pm_opp_disable(dev, GPU_153M6_FREQ);
	//T760: GPU max freq is 650M
	//T770: GPU max freq is 780M
	//T790: GPU max freq is 850M
	//default table:384M, 512M, 680M, 850M
	if (gpu_dvfs_ctx.chip_id == 3 || gpu_dvfs_ctx.chip_id == 2) { //T760 table:384M, 512M, 650M
		//remove 680M and 850M
		dev_pm_opp_disable(dev, GPU_680M_FREQ);
		dev_pm_opp_disable(dev, GPU_850M_FREQ);
		//add GPU max freq 650M
		ret = dev_pm_opp_add(dev, gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].freq * FREQ_KHZ, gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].volt);
		if (ret != 0)
			printk(KERN_ERR "%s: dev_pm_opp_add return %d\n", __func__, ret);

	} else if (gpu_dvfs_ctx.chip_id == 1) { //T770 table: 384M, 512M, 680M, 780M
		//default table:384M, 512M, 680M, 780M
		//remove 850M
		dev_pm_opp_disable(dev, GPU_850M_FREQ);
		//add GPU max freq 780M
		ret = dev_pm_opp_add(dev, gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].freq * FREQ_KHZ, gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].volt);
		if (ret != 0)
			printk(KERN_ERR "%s: dev_pm_opp_add return %d\n", __func__, ret);

	}
}

int kbase_platform_set_freq_volt(int freq, int volt)
{
	int index, ret = 0;
	freq = freq/FREQ_KHZ;
	index = freq_search(gpu_dvfs_ctx.freq_list, gpu_dvfs_ctx.freq_list_len, freq);
	printk(KERN_ERR "mali GPU_DVFS %s index = %d cur_freq = %d MHz cur_volt = %d mV --> freq = %d MHz volt = %d mV gpu_power_state = %d gpu_clock_state = %d , gpu_temperature = %u.%lu, chip_id = %d, gpu_binning = %d.\n",
		__func__, index, gpu_dvfs_ctx.freq_cur->freq / FREQ_KHZ, gpu_dvfs_ctx.cur_voltage / VOLT_KMV, freq / FREQ_KHZ, volt / VOLT_KMV,
		atomic_read(&gpu_dvfs_ctx.gpu_power_state), atomic_read(&gpu_dvfs_ctx.gpu_clock_state), (unsigned)gpu_dvfs_ctx.gpu_temperature / 1000,
		(unsigned long)gpu_dvfs_ctx.gpu_temperature % 1000, gpu_dvfs_ctx.chip_id, gpu_dvfs_ctx.gpu_bin);

	if (0 <= index) {
		down(gpu_dvfs_ctx.sem);

		//set frequency
		if (atomic_read(&gpu_dvfs_ctx.gpu_power_state) && atomic_read(&gpu_dvfs_ctx.gpu_clock_state) )
		{
			//T770 the clk src of 680MHz and 780MHz are both clk_gpll
			//set gpll VCO 1950M(780M*2.5)
			if (gpu_dvfs_ctx.freq_list_len-1 == index && gpu_dvfs_ctx.chip_id == 1) {
				ret = clk_set_rate(gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].clk_src, gpll_1950M);
				if (ret != 0)
					printk(KERN_ERR "%s:%d clk_set_rate failed error = %d\n", __func__,  __LINE__, ret);
				udelay(50);
			} else if (gpu_dvfs_ctx.freq_list_len-2 == index && gpu_dvfs_ctx.chip_id == 1) {
				ret = clk_set_rate(gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-2].clk_src, gpll_1716M);
				if (ret != 0)
					printk(KERN_ERR "%s:%d clk_set_rate failed error = %d\n", __func__, __LINE__, ret);
				udelay(50);
			}
			//set dvfs index
			regmap_update_bits(gpu_dvfs_ctx.dvfs_index_cfg.regmap_ptr, gpu_dvfs_ctx.dvfs_index_cfg.args[0], gpu_dvfs_ctx.dvfs_index_cfg.args[1], gpu_dvfs_ctx.freq_list[index].dvfs_index);
		}
		gpu_dvfs_ctx.cur_index = index;
		gpu_dvfs_ctx.freq_cur = &gpu_dvfs_ctx.freq_list[index];
		//trace_clock_set_rate(VENDOR_FTRACE_MODULE_NAME, gpu_dvfs_ctx.freq_cur->freq, raw_smp_processor_id());
		up(gpu_dvfs_ctx.sem);
	}
	return 0;
}

#ifdef CONFIG_MALI_BOOST
static void set_freq_range(struct gpu_freq_info** freq_min, struct gpu_freq_info** freq_max, int boost_level)
{
	switch (boost_level) {
	case 10:
		*freq_min = &gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1];
		*freq_max = &gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1];
		break;
	case 5:
		*freq_min = &gpu_dvfs_ctx.freq_list[(gpu_dvfs_ctx.freq_list_len + GPU_MIN_FREQ_INDEX) / 2];
		*freq_max = &gpu_dvfs_ctx.freq_list[(gpu_dvfs_ctx.freq_list_len + GPU_MIN_FREQ_INDEX) / 2];
		break;
	case 1:
		*freq_min = &gpu_dvfs_ctx.freq_list[GPU_MIN_FREQ_INDEX];
		*freq_max = &gpu_dvfs_ctx.freq_list[GPU_MIN_FREQ_INDEX];
		break;
	case 0:
	default:
		*freq_min = &gpu_dvfs_ctx.freq_list[GPU_MIN_FREQ_INDEX];
		*freq_max = &gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1];
		break;
	}
}

void kbase_platform_modify_target_freq(struct device *dev, unsigned long *target_freq)
{
	int min_index = -1, max_index = -1, modify_flag = 0, user_max_freq, user_min_freq;
	struct gpu_freq_info* freq_max = &gpu_dvfs_ctx.freq_list[GPU_MIN_FREQ_INDEX];
	struct gpu_freq_info* freq_min = &gpu_dvfs_ctx.freq_list[GPU_MIN_FREQ_INDEX];
	int trace_freq, trace_index;

	if (gpu_boost_level2) {
		set_freq_range(&freq_min, &freq_max, gpu_boost_level2);
	} else {
		set_freq_range(&freq_min, &freq_max, gpu_boost_level);
	}

	user_max_freq = dev_pm_qos_read_value(dev, DEV_PM_QOS_MAX_FREQUENCY);
	user_min_freq = dev_pm_qos_read_value(dev, DEV_PM_QOS_MIN_FREQUENCY);
	//limit min freq
	min_index = freq_search(gpu_dvfs_ctx.freq_list, gpu_dvfs_ctx.freq_list_len, user_min_freq/FREQ_KHZ);
	if ((0 <= min_index) &&
		(freq_min->freq < gpu_dvfs_ctx.freq_list[min_index].freq)) {
		freq_min = &gpu_dvfs_ctx.freq_list[min_index];
		if (freq_min->freq > freq_max->freq) {
			freq_max = freq_min;
		}
	}

	//limit max freq
	max_index = freq_search(gpu_dvfs_ctx.freq_list, gpu_dvfs_ctx.freq_list_len, user_max_freq /FREQ_KHZ);
	if ((0 <= max_index) &&
		(freq_max->freq > gpu_dvfs_ctx.freq_list[max_index].freq)) {
		freq_max = &gpu_dvfs_ctx.freq_list[max_index];
		if (freq_max->freq < freq_min->freq) {
			freq_min = freq_max;
		}
	}

	//gpu_boost_level = 0;

	//set target frequency
	if (*target_freq < (unsigned long)freq_min->freq*FREQ_KHZ) {
		*target_freq = (unsigned long)freq_min->freq*FREQ_KHZ;
		modify_flag = 1;
	}
	if (*target_freq > (unsigned long)freq_max->freq*FREQ_KHZ) {
		*target_freq = (unsigned long)freq_max->freq*FREQ_KHZ;
		modify_flag = 1;
	}
	//T790 SS gpu_temperature >= 65 the max freq of gpu is 680MHz
	if ((*target_freq == (unsigned long)gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-1].freq*FREQ_KHZ )
		&& 0 == gpu_dvfs_ctx.chip_id
		&& (3 == gpu_dvfs_ctx.gpu_bin || 0 == gpu_dvfs_ctx.gpu_bin)
		&& gpu_dvfs_ctx.gpu_temperature >= 65000) {
		*target_freq = (unsigned long)gpu_dvfs_ctx.freq_list[gpu_dvfs_ctx.freq_list_len-2].freq*FREQ_KHZ;
		modify_flag = 1;
	}
	if(1 == modify_flag) {
		printk(KERN_INFO "GPU_DVFS %s gpu_boost_level:%d gpu_boost_level2:%d min_freq=%dMHz max_freq=%dMHz target_freq=%ldMHz \n",
			__func__, gpu_boost_level, gpu_boost_level2, freq_min->freq / FREQ_KHZ, freq_max->freq / FREQ_KHZ, *target_freq / (FREQ_KHZ * FREQ_KHZ));
	}

	//trace gpu frequency
	trace_freq = *target_freq / FREQ_KHZ;
	trace_index = freq_search(gpu_dvfs_ctx.freq_list, gpu_dvfs_ctx.freq_list_len, trace_freq);
	if (trace_index >= 0)
	{
		trace_gpu_frequency(trace_freq, trace_index);
	}
}
#endif
#endif

#ifdef CONFIG_MALI_BOOST

int boost_tgid = 0;
int boost_pid = 0;

void kbase_platform_set_boost(struct kbase_device *kbdev, struct kbase_context *kctx, int boost_level)
{
	if (boost_level == 0 || boost_level == 10) {
		if (boost_level == 0 && kctx->tgid == boost_tgid && kctx->pid != boost_pid) {
			printk(KERN_INFO "GPU_DVFS %s boost_level = %d, gpu_boost_level = %d, tgid = %d, pid = %d, previous tgid = %d, previous pid = %d",
					__func__, boost_level, gpu_boost_level, kctx->tgid, kctx->pid, boost_tgid, boost_pid);
			return;
		}
		gpu_boost_level = boost_level;
		//printk(KERN_INFO"GPU_DVFS %s gpu_boost_level =%d \n", __func__, gpu_boost_level);
	}
	boost_tgid = kctx->tgid;
	boost_pid = kctx->pid;
}
#endif


u32 core0_pwr = 0;
u32 core1_pwr = 0;
u32 core2_pwr = 0;
u32 core3_pwr = 0;
u32 cur_st_st0_pwr = 0;
u32 cur_st_st1_pwr = 0;
u32 cur_st_st2_pwr = 0;
u32 cur_st_st3_pwr = 0;
u32 core0_pwr_mask = MOVE_BIT_LEFT(0x1f,24);
u32 core1_pwr_mask = MOVE_BIT_LEFT(0x1f,0);
u32 core2_pwr_mask = MOVE_BIT_LEFT(0x1f,8);
u32 core3_pwr_mask = MOVE_BIT_LEFT(0x1f,16);
u32 core0_pwr_wakeup = MOVE_BIT_LEFT(8,24);
u32 core1_pwr_wakeup = MOVE_BIT_LEFT(8,0);
u32 core2_pwr_wakeup = MOVE_BIT_LEFT(8,8);
u32 core3_pwr_wakeup = MOVE_BIT_LEFT(8,16);

u32 cur_st_mask = MOVE_BIT_LEFT(0xf0,0);
u32 st_pwr_mode = MOVE_BIT_LEFT(7,4);


bool is_shader_ready(void)
{
    //check if the gpu_core_state ready or not
    regmap_read(gpu_dvfs_ctx.gpu_core0_state_reg.regmap_ptr, gpu_dvfs_ctx.gpu_core0_state_reg.args[0], &core0_pwr);
    core0_pwr = core0_pwr & core0_pwr_mask ;
    regmap_read(gpu_dvfs_ctx.gpu_core1_state_reg.regmap_ptr, gpu_dvfs_ctx.gpu_core1_state_reg.args[0], &core1_pwr);
    core1_pwr = core1_pwr & core1_pwr_mask ;
    regmap_read(gpu_dvfs_ctx.gpu_core2_state_reg.regmap_ptr, gpu_dvfs_ctx.gpu_core2_state_reg.args[0], &core2_pwr);
    core2_pwr = core2_pwr & core2_pwr_mask ;
    regmap_read(gpu_dvfs_ctx.gpu_core3_state_reg.regmap_ptr, gpu_dvfs_ctx.gpu_core3_state_reg.args[0], &core3_pwr);
    core3_pwr = core3_pwr & core3_pwr_mask ;
    //read the st reg state
    regmap_read(gpu_dvfs_ctx.cur_st_st0_reg.regmap_ptr, gpu_dvfs_ctx.cur_st_st0_reg.args[0], &cur_st_st0_pwr);
    cur_st_st0_pwr = cur_st_st0_pwr & cur_st_mask ;
    regmap_read(gpu_dvfs_ctx.cur_st_st1_reg.regmap_ptr, gpu_dvfs_ctx.cur_st_st1_reg.args[0], &cur_st_st1_pwr);
    cur_st_st1_pwr = cur_st_st1_pwr & cur_st_mask ;
    regmap_read(gpu_dvfs_ctx.cur_st_st2_reg.regmap_ptr, gpu_dvfs_ctx.cur_st_st2_reg.args[0], &cur_st_st2_pwr);
    cur_st_st2_pwr = cur_st_st2_pwr & cur_st_mask ;
    regmap_read(gpu_dvfs_ctx.cur_st_st3_reg.regmap_ptr, gpu_dvfs_ctx.cur_st_st3_reg.args[0], &cur_st_st3_pwr);
    cur_st_st3_pwr = cur_st_st3_pwr & cur_st_mask ;

    return  ( core0_pwr == core0_pwr_wakeup )&&( core1_pwr == core1_pwr_wakeup )&&( core2_pwr == core2_pwr_wakeup )
        &&( core3_pwr == core3_pwr_wakeup )&&(cur_st_st0_pwr == st_pwr_mode)&&(cur_st_st1_pwr == st_pwr_mode)
        &&(cur_st_st2_pwr == st_pwr_mode)&&(cur_st_st3_pwr == st_pwr_mode);
}

//to check if the gpu core state is ready or not
void mali_core_state_check(void)
{
    int pass_retry = 5;//the time of consecutive passes
    int pass_counter = 0;
    int counter = 0;//count the time of check

    //printk(KERN_ERR "SPRDDEBUG gpu_core0_pwr = 0x%x,gpu_core1_pwr = 0x%x,gpu_core2_pwr = 0x%x,gpu_core3_pwr = 0x%x,cur_st_st0_pwr = 0x%x,cur_st_st1_pwr = 0x%x,cur_st_st2_pwr = 0x%x,cur_st_st3_pwr= 0x%x, counter = %d\n ",
    //       core0_pwr,core1_pwr,core2_pwr,core3_pwr,cur_st_st0_pwr,cur_st_st1_pwr,cur_st_st2_pwr,cur_st_st3_pwr, counter);
    while(1)
    {
        udelay(5);
        //printk(KERN_ERR "SPRDDEBUG gpu_core0_pwr = 0x%x,gpu_core1_pwr = 0x%x,gpu_core2_pwr = 0x%x,gpu_core3_pwr = 0x%x,cur_st_st0_pwr = 0x%x,cur_st_st1_pwr = 0x%x,cur_st_st2_pwr = 0x%x,cur_st_st3_pwr= 0x%x, counter = %d\n ",
                       //core0_pwr,core1_pwr,core2_pwr,core3_pwr,cur_st_st0_pwr,cur_st_st1_pwr,cur_st_st2_pwr,cur_st_st3_pwr, counter);
        if(is_shader_ready()){
            pass_counter=0;
            while(pass_counter < pass_retry){
                if(!is_shader_ready()){
                        break;
                }else pass_counter++;
            }
            if(pass_counter>=pass_retry){
                printk(KERN_INFO "SPRDDEBUG gpu core power on polling SUCCESS !");
                break;
            }
        }
        if(counter++ > 2000 && !is_shader_ready() ){
            printk(KERN_ERR "SPRDDEBUG gpu core power on TIMEOUT !");
        	printk(KERN_ERR "SPRDDEBUG gpu_core0_pwr = 0x%x,gpu_core1_pwr = 0x%x,gpu_core2_pwr = 0x%x,gpu_core3_pwr = 0x%x,cur_st_st0_pwr = 0x%x,cur_st_st1_pwr = 0x%x,cur_st_st2_pwr = 0x%x,cur_st_st3_pwr= 0x%x, counter = %d\n ",
                   core0_pwr,core1_pwr,core2_pwr,core3_pwr,cur_st_st0_pwr,cur_st_st1_pwr,cur_st_st2_pwr,cur_st_st3_pwr, counter);
            WARN_ON(1);
            break;
        }
    }
}

static void pm_callback_shader_polling(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	mali_core_state_check();
}

extern void kbase_reg_write(struct kbase_device *kbdev, u32 offset, u32 value);

static void pm_callback_set_pwr_override1(struct kbase_device *kbdev)
{
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PWR_KEY), PWR_KEY_VALUE);
	// mother delay & ram power on delay default value
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PWR_OVERRIDE1), PWR_OVERRIDE1_VALUE);
}
