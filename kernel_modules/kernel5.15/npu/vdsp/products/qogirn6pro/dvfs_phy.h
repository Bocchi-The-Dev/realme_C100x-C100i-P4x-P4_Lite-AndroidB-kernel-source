/**
 * SPDX-FileContributor: vdsp dvfs function, dynamically adjust frequency voltage.
 */

#ifndef _DVFS_PHY_H_
#define _DVFS_PHY_H_
#include <linux/types.h>

struct dvfs_phy_ops {
	int (*enable)(void *hw_arg);
	int (*disable)(void *hw_arg);
	uint32_t (*level_to_freq)(uint32_t level);
	int (*setdvfs)(void *hw_arg,uint32_t level);
	int (*set_voltage) (void *hw_arg, uint32_t voltage);
	int (*set_freq) (void *hw_arg, uint32_t freq);
};

struct dvfs_phy_desc {
	struct dvfs_phy_ops *ops;
};

struct dvfs_phy_desc *get_dvfs_phy_desc(void);

#endif


