/**
 * SPDX-FileContributor: vdsp dvfs function, dynamically adjust frequency voltage.
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/sprd_soc_id.h>
#include "dvfs_phy.h"
#include "mm_dvfs.h"
#include "mmsys_dvfs_comm.h"
#include "vdsp_debugfs.h"
#include "vdsp_hw.h"
#include "vdsp_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-vdsp: dvfs-phy %d: %d %s:" \
	fmt, current->pid, __LINE__, __func__

#define MM_SYS_EN		(0x0)
#define DVFS_EN			BIT(3) //GENMASK(23, 16)

#define vmin(a, b) ((a) < (b) ? (a) : (b))
#define vmax(a, b) ((a) > (b) ? (a) : (b))

struct vdsp_dvfs_table {
	uint32_t voltage;
	uint32_t voltage_index;
	uint32_t freq;
	uint32_t freq_index;
};

static struct vdsp_dvfs_table vdsp_table[6] = {
	{600, 1, 26000000, 0},	//power level min
	{600, 1, 307200000, 1},
	{600, 1, 512000000, 2},
	{650, 2, 614400000, 3},
	{700, 3, 819200000, 4},
	{750, 4, 1014000000, 5},	//power level max
};

static int dvfs_phy_enable(void *hw_arg)
{
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	if (!(hw->mm_ahb)) {
		pr_err("Invalid argument\n");
		return -1;
	}
	if (vdsp_regmap_update_bits(hw->mm_ahb, MM_SYS_EN, DVFS_EN, DVFS_EN, RT_MMSYS)) {
		pr_err("error enable dvfs\n");
		return -1;
	}
	return 0;
}

static int dvfs_phy_disable(void *hw_arg)
{
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	if (!(hw->mm_ahb)) {
		pr_err("Invalid argument\n");
		return -1;
	}
	if (vdsp_regmap_update_bits(hw->mm_ahb, MM_SYS_EN, DVFS_EN, 0, RT_MMSYS)) {
		pr_err("error disable dvfs\n");
		return -1;
	}
	return 0;
}

static uint32_t dvfs_phy_level_to_freq(uint32_t level)
{
	if (level > 5) {
		level = 5;
		pr_err("dvfs level exceed range:%d\n", level);
	}
	return vdsp_table[level].freq;
}

static uint32_t soc_ver_id_check(void)
{
	int ret;
	uint32_t ver_id = 0;

	ret = sprd_get_soc_id(AON_VER_ID, &ver_id, 1);
	if (ret) {
		void __iomem *aon_reg;
		volatile uint32_t value;

		aon_reg = ioremap(0x64900000, 0x1000);
		value = __raw_readl(aon_reg + 0xF0);
		ver_id = (value & 0x1F);
		iounmap(aon_reg);
		pr_debug("ver_id is %d\n", ver_id);
		goto end;
	}
	if (ver_id == 0) {
		pr_warn("soc is AA\n");
	}
	else if (ver_id == 1) {
		pr_debug("soc is AB\n");
	}
	else {
		pr_warn("unkowned soc\n");
		ver_id = 0;
	}
end:
	return ver_id;
}

#if 1 /* software config */
static inline void reg_write32_setbit_h(void *addr, u32 v)
{
	volatile u32 value;
	value = __raw_readl(addr);
	value = v | value;
	__raw_writel(value, addr);
}

static inline void reg_write32_clearbit_h(void *addr, u32 v)
{
	volatile u32 value;
	value = __raw_readl(addr);
	value = v & value;
	__raw_writel(value, addr);
}

static int set_work_freq_sw(void *hw_arg, uint32_t clk_index)
{
	void __iomem *mm_clk_config;

	(void)hw_arg;
	pr_debug("[sw workaround] dvfs clk index:%d, -> register [0x30010028]\n", clk_index);

	mm_clk_config = ioremap(0x30010000, 0x1000);
	reg_write32_setbit_h((void *)(mm_clk_config + 0x28), clk_index);
	iounmap(mm_clk_config);
	return 0;
}
#endif

static int set_work_freq_hw(void *hw_arg, uint32_t freq)
{
	uint32_t val = 0;
	struct ip_dvfs_ops *vdsp_dvfs_ops_ptr = get_vdsp_dvfs_ops();
	struct vdsp_hw *hw = (struct vdsp_hw *)hw_arg;

	if (!vdsp_dvfs_ops_ptr || !(vdsp_dvfs_ops_ptr->set_work_freq)) {
		pr_err("dvfs_phy: set_work_freq operation not supported");
		return -1;
	}
	if (!vdsp_regmap_read_mask(hw->mm_ahb, MM_SYS_EN, DVFS_EN, &val)) {
		if (val == 0) {
			pr_warn("dvfs not enable, vdsp skip to set dvfs\n");
		}
		else {
			pr_debug("call mm dvfs to set freq[%d]\n", freq);
			vdsp_dvfs_ops_ptr->set_work_freq(NULL, freq);
		}
	}
	return 0;
}

int setdvfs_hw(void *hw_arg, uint32_t level)
{
	unsigned int debugfs_dvfs_level;

	debugfs_dvfs_level = vdsp_debugfs_dvfs_level();
	if (debugfs_dvfs_level > 0)
	{
		level = debugfs_dvfs_level;
		pr_info("debugfs force dvfs, level:%d, freq:%d\n", level, vdsp_table[level].freq);
	}

	level = vmin(level, 5);		//vdsp table index max, 5-1014M
#ifdef DVFS_HIGH_FREQ_RANGE		// APOLLO version, high performance, no low freq
	level = vmax(level, 4);		// 4-819.2M
	pr_info("APOLLO version, level[%d]\n", level);
#endif
	pr_info("level:%d, freq:%d\n", level, vdsp_table[level].freq);
	// AB chip
	if (1 == soc_ver_id_check()) {
		return set_work_freq_hw(hw_arg, vdsp_table[level].freq);
	} else {
		return set_work_freq_sw(hw_arg, vdsp_table[level].freq_index);
	}
}

int setdvfs_sw(void *hw_arg, uint32_t level)
{
	unsigned int debugfs_dvfs_level;

	debugfs_dvfs_level = vdsp_debugfs_dvfs_level();
	if (debugfs_dvfs_level > 0) {
		level = debugfs_dvfs_level;
		pr_info("debugfs force dvfs, level:%d, freq:%d\n", level, vdsp_table[level].freq);
	}

	level = vmin(level, 5);		//vdsp table index max, 5-1014M
	pr_info("level:%d, freq:%d\n", level, vdsp_table[level].freq);

	return set_work_freq_sw(hw_arg, vdsp_table[level].freq_index);
}

static struct dvfs_phy_ops vdsp_dvfs_ops = {
	.enable = dvfs_phy_enable,
	.disable = dvfs_phy_disable,
	.level_to_freq = dvfs_phy_level_to_freq,
	.setdvfs = setdvfs_hw,	// only test need use sw
	.set_voltage = NULL,
	.set_freq = NULL,
};

static struct dvfs_phy_desc sub_dvfs_phy_desc = {
	.ops = &vdsp_dvfs_ops,
};

struct dvfs_phy_desc *get_dvfs_phy_desc(void)
{
	return &sub_dvfs_phy_desc;
}

