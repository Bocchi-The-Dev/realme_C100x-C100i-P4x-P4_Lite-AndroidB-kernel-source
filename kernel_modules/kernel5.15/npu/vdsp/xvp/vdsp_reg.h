/**
 * SPDX-FileContributor: vdsp register function.
 */

#ifndef _VDSP_REG_H
#define _VDSP_REG_H

#include <linux/types.h>
#include <linux/regmap.h>

//register set clr type
#define RT_NO_SET_CLR	(0)
#define RT_SET_CLR	(1)

#define SET_OFFSET_1000	0x1000
#define CLR_OFFSET_2000	0x2000

#define RT_PMU		(RT_SET_CLR)
#define RT_APAHB	(RT_SET_CLR)	//l5pro
#define RT_MMSYS	(RT_SET_CLR)	//n6pro
#define RT_VDSP_SYS	(RT_SET_CLR)	//k8
#define RT_ISP_PLL	(RT_SET_CLR)	//k8
#define RT_VDSP_CLK	(RT_SET_CLR)	//k8

struct vdsp_reg {
	struct regmap *gpr;
	uint32_t reg;
	uint32_t mask;
};

int vdsp_regmap_update_bits(struct regmap *regmap, uint32_t offset,
	uint32_t mask, uint32_t val, uint32_t set_clr_type);
int vdsp_regmap_read_mask(struct regmap *regmap, uint32_t reg,
	uint32_t mask, uint32_t *val);
int vdsp_reg_read(struct vdsp_reg *p, uint32_t *val);
int vdsp_reg_update(struct vdsp_reg *p, uint32_t val, uint32_t set_clr_type);

#endif

