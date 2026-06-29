/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Unisoc Inc.
 */

#ifndef _SPRD_CPUFREQ_CONTROL_H_
#define _SPRD_CPUFREQ_CONTROL_H_

#ifdef CONFIG_DRM_SPRD_CPU_FREQ

int force_big_core_max_freq_int(void);
int force_big_core_max_freq_enable(void);
int force_big_core_max_freq_disable(void);
int force_big_core_max_freq_exit(void);

#else
static inline int force_big_core_max_freq_int(void)
{
    return 0;
}

static inline int force_big_core_max_freq_enable(void)
{
    return 0;
}

static inline int force_big_core_max_freq_disable(void)
{
    return 0;
}

static inline int force_big_core_max_freq_exit(void)
{
    return 0;
}

#endif

#endif /* _SPRD_CPUFREQ_CONTROL_H_ */


