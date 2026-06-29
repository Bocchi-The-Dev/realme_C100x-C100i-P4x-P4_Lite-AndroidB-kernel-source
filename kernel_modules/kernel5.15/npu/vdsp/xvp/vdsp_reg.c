/**
 * SPDX-FileContributor: vdsp register function.
 */

#include <linux/of_device.h>
#include <linux/regmap.h>
#include "vdsp_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-vdsp: reg %d: %d %s:" \
	fmt, current->pid, __LINE__, __func__


/***********************register function********************/
static int vdsp_regmap_read(struct regmap *regmap, uint32_t reg, uint32_t *val)
{
	if (!regmap)
		return -EINVAL;
	return regmap_read(regmap, reg, val);
}

static int vdsp_regmap_write(struct regmap *regmap, uint32_t reg, uint32_t val)
{
	if (!regmap)
		return -EINVAL;
	return regmap_write(regmap, reg, val);
}

int vdsp_regmap_read_mask(struct regmap *regmap, uint32_t reg,
	uint32_t mask, uint32_t *val)
{
	int ret;

	if ((!regmap) || (!val))
		return -EINVAL;
	ret = regmap_read(regmap, reg, val);
	if (!ret)
		*val &= ( uint32_t) mask;

	return ret;
}

static int regmap_set(struct regmap *regmap, uint32_t offset, uint32_t val, uint32_t set_clr_type)
{
	int ret = -1;

	switch (set_clr_type) {
	case RT_SET_CLR:
		ret = vdsp_regmap_write(regmap, offset + SET_OFFSET_1000, val);
		break;
	default:
		break;
	}
	pr_debug("return:%d, reg type:%d\n", ret, set_clr_type);
	return ret;
}

static int regmap_clr(struct regmap *regmap, uint32_t offset, uint32_t val, uint32_t set_clr_type)
{
	int ret = -1;

	switch (set_clr_type) {
	case RT_SET_CLR:
		ret = vdsp_regmap_write(regmap, offset + CLR_OFFSET_2000, val);
		break;
	default:
		break;
	}
	pr_debug("return:%d, reg type:%d\n", ret, set_clr_type);
	return ret;
}

static int vdsp_regmap_set_clr(struct regmap *regmap, uint32_t offset,
	uint32_t mask, uint32_t val, uint32_t set_clr_type)
{
	uint32_t set, clr;
	int ret = 0;

	set = val & mask;
	clr = (~val) & mask;

	pr_debug("regmap:%p, offset:%#x, mask:%#x val:%#x, set:%#x, clr:%#x, rt:%#x\n",
		regmap, offset, mask, val, set, clr, set_clr_type);

	if (set) {
		ret = regmap_set(regmap, offset, set, set_clr_type);
		if (ret) {
			pr_err("regmap_set failed, regmap:%p, offset:%#x, set:%#x,rt:%#x\n",
				regmap, offset, set, set_clr_type);
			goto end;
		}
	}
	if (clr) {
		ret = regmap_clr(regmap, offset, clr, set_clr_type);
		if (ret) {
			pr_err("regmap_clr failed, regmap:%p, offset:%#x, clr:%#x,rt:%#x\n",
				regmap, offset, clr, set_clr_type);
			goto end;
		}
	}
end:
	return ret;
}

static int vdsp_regmap_not_set_clr(struct regmap *regmap, uint32_t offset,
	uint32_t mask, uint32_t val, uint32_t set_clr_type)
{
	uint32_t tmp = 0;
	int ret;

	pr_debug("regmap:%p, offset:%#x, mask:%#x, val:%#x, rt:%#x\n", regmap, offset, mask, val, set_clr_type);

	ret = vdsp_regmap_read(regmap, offset, &tmp);
	if (ret) {
		pr_err("regmap read  error!\n");
		return ret;
	}
	tmp &= ~mask;
	ret = vdsp_regmap_write(regmap, offset, tmp | (val & mask));

	return ret;
}

int vdsp_regmap_update_bits(struct regmap *regmap, uint32_t offset,
	uint32_t mask, uint32_t val, uint32_t set_clr_type)
{
	if (set_clr_type == RT_SET_CLR) {
		return vdsp_regmap_set_clr(regmap, offset, mask, val, set_clr_type);
	}
	else if (set_clr_type == RT_NO_SET_CLR) {
		return vdsp_regmap_not_set_clr(regmap, offset, mask, val, set_clr_type);
	}
	else {
		pr_err("[error]input unknowned reg type\n");
		return -1;
	}
}

int vdsp_reg_read(struct vdsp_reg *p, uint32_t *val)
{
	int ret = 0;

	if ((!p) || (!(p->gpr)) || (!val))
		return -1;
	ret = vdsp_regmap_read(p->gpr, p->reg, val);
	if (!ret)
		*val &= ( uint32_t) p->mask;
	return ret;
}

int vdsp_reg_update(struct vdsp_reg *p, uint32_t val, uint32_t set_clr_type)
{
	if ((!p) || (!(p->gpr))) {
		return -1;
	}
	return vdsp_regmap_update_bits(p->gpr, p->reg, p->mask, val, set_clr_type);
}

