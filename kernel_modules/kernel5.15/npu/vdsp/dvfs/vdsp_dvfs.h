/**
 * SPDX-FileContributor: vdsp dvfs function, dynamically adjust frequency voltage .
 */

#ifndef _VDSP_DVFS_H_
#define _VDSP_DVFS_H_
#include "vdsp_hw.h"

typedef enum {
	POWERHINT_ENABLE = 0,
	POWERHINT_DISABLE,
	POWERHINT_INVALID,
}vdsp_powerhint_flag;

typedef enum {
	VDSP_PWR_MIN = 0,	//26M (n6pro)
	VDSP_PWR_2,	//307.2M
	VDSP_PWR_3,	//512M
	VDSP_PWR_4,	//614.4M
	VDSP_PWR_5,	//819.2M
	VDSP_PWR_MAX,	//1014M
	VDSP_PWR_NUM,	// total power level numbers
}vdsp_power_level;	// power level

struct vdsp_dvfs_info {
	struct mutex dvfs_lock;
	uint32_t last_level;
};

struct vdsp_powerhint {
	uint32_t record[VDSP_PWR_NUM];
};

struct vdsp_dvfs_ops {
	int (*init)(void *data);
	int (*deinit)(void *data);
	int (*enable)(void *hw_arg);
	int (*disable)(void *hw_arg);
	int (*release_powerhint)(void *filp);
	int (*set_powerhint)(void *data, int32_t level, uint32_t flag);
	int (*setdvfs)(void *hw_arg, uint32_t level);
	int (*set_voltage) (void *hw_arg, uint32_t voltage);
	int (*set_freq) (void *hw_arg, uint32_t freq);
};

struct vdsp_dvfs_desc {
	struct vdsp_dvfs_ops *ops;
};

struct vdsp_dvfs_desc *get_vdsp_dvfs_desc(void);

#endif

