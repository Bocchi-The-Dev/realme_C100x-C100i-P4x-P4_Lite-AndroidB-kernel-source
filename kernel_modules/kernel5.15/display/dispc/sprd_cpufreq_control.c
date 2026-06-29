// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Unisoc Inc.
 */

#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/llist.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/cpufreq.h>

struct cpufreq_policy *big_core_policy;
static bool force_bigcore_maxfreq;

static void force_big_core_max_freq_handler(void *data, struct cpufreq_policy *policy,
					    unsigned int *target_freq, unsigned int old_target)
{
	if (unlikely(force_bigcore_maxfreq && big_core_policy == policy)) {
		int idx;

		*target_freq = clamp_val(old_target, policy->cpuinfo.max_freq,
						     policy->cpuinfo.max_freq);

		if (unlikely(policy->freq_table_sorted == CPUFREQ_TABLE_UNSORTED)) {
				pr_info("%s: freq table unsorted\n", __func__);
				idx = cpufreq_table_index_unsorted(policy, *target_freq,
									CPUFREQ_RELATION_L);
		} else {

			if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
				idx = cpufreq_table_find_index_al(policy, *target_freq);
			else
				idx = cpufreq_table_find_index_dl(policy, *target_freq);
		}

		policy->cached_resolved_idx = idx;
		policy->cached_target_freq = *target_freq;
		*target_freq = policy->freq_table[idx].frequency;
	}

}


int force_big_core_max_freq_enable(void)
{
	force_bigcore_maxfreq = true;

	if (big_core_policy && !policy_is_inactive(big_core_policy)) {
		down_write(&big_core_policy->rwsem);
		__cpufreq_driver_target(big_core_policy, big_core_policy->cpuinfo.max_freq, CPUFREQ_RELATION_L);
		refresh_frequency_limits(big_core_policy);
		up_write(&big_core_policy->rwsem);
	}

	return 0;
}

int force_big_core_max_freq_disable(void)
{
	force_bigcore_maxfreq = false;
	if (big_core_policy && !policy_is_inactive(big_core_policy)) {
		down_write(&big_core_policy->rwsem);
		refresh_frequency_limits(big_core_policy);
		up_write(&big_core_policy->rwsem);
	}

	return 0;
}

int force_big_core_max_freq_int(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (arch_scale_cpu_capacity(cpu) == SCHED_CAPACITY_SCALE) {
			big_core_policy = cpufreq_cpu_get(cpu);

			if (!big_core_policy)
				return -EINVAL;

			cpufreq_cpu_put(big_core_policy);
			break;
		}

	}
	if (cpu >= nr_cpu_ids || cpu == 0)
		return -EINVAL;

	register_trace_android_vh_cpufreq_target(force_big_core_max_freq_handler, NULL);

	return 0;
}

int force_big_core_max_freq_exit(void)
{
	unregister_trace_android_vh_cpufreq_target(force_big_core_max_freq_handler, NULL);
	big_core_policy = NULL;

	return 0;
}

MODULE_AUTHOR("Xuewen Yan <xuewen.yan@unisoc.com>");
MODULE_DESCRIPTION("Unisoc CPU FREQ Control Driver");
MODULE_LICENSE("GPL v2");
