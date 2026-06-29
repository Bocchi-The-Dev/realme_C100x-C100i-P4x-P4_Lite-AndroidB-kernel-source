// SPDX-License-Identifier: GPL-2.0
/*
 * unisoc cpu and ddr frequency link
 *
 * Copyright (c) 2024, Unisoc (shanghai) Technologies Co., Ltd
 */

#define pr_fmt(fmt) "unisoc-freqlink: " fmt

#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/soc/sprd/hwfeature.h>
#include <linux/suspend.h>
#include <trace/events/power.h>
#include <trace/hooks/sched.h>
#include <uapi/linux/sched/types.h>

#include "../../../kernel/sched/sched.h"
#include "../../devfreq/sprd/sprd_ddr_dvfs.h"

#define DDR_FREQ_UP_TIME	2000ULL
#define DDR_FREQ_DOWN_TIME	4000ULL

struct freqlink_pair {
	/* KHz */
	unsigned int master_freq;
	unsigned int slave_freq;
};

struct cpu_ddr_freqlink {
	struct freqlink_pair *cpu_ddr_fl_table;
	int cpu;
	int npairs;
	bool enable;
	struct list_head node;
};

static LIST_HEAD(cpu_ddr_fl_list);
static bool cpu_ddr_fl_initialized;
static u64 last_ddr_freq_update_time;
static u64 ddr_freq_up_time_ns = DDR_FREQ_UP_TIME * NSEC_PER_USEC;
static u64 ddr_freq_down_time_ns = DDR_FREQ_DOWN_TIME * NSEC_PER_USEC;
static unsigned int target_ddr_freq;
static unsigned int default_target;
static struct task_struct *ddr_freq_vote_task;
static bool g_enable, scene_enter;
static bool freqlink_suspended;
DEFINE_MUTEX(ddrfreq_vote_mutex);

static bool is_idle_cpu(int cpu)
{
	struct rq *rq = cpu_rq(cpu);

	if (rq->curr != rq->idle)
		return false;

	if (rq->nr_running)
		return false;

#ifdef CONFIG_SMP
	if (rq->ttwu_pending)
		return false;
#endif

	return true;
}

static unsigned int sprd_freqlink_get_ddr_freq(struct cpu_ddr_freqlink *freqlink)
{
	struct cpufreq_policy *policy;
	unsigned int ddr_freq = default_target;
	unsigned int cpu_freq;
	int i, cpu, core_active = 0;

	policy = cpufreq_cpu_get(freqlink->cpu);
	if (!policy)
		return ddr_freq;

	/* all cores of this policy are already offline, vote the default ddr freq */
	if (cpumask_empty(policy->cpus))
		goto RET;

	/* all cores of this policy are in idle state, vote the default ddr freq */
	for_each_cpu(cpu, policy->cpus) {
		if (cpu != smp_processor_id()) {
			if (!is_idle_cpu(cpu)) {
				core_active = 1;
				break;
			}
		} else if (!(raw_rq()->nr_running == 1)) {
			core_active = 1;
			break;
		}
	}

	if (!core_active)
		goto RET;

	cpu_freq = policy->cur;
	for (i = 0; i < freqlink->npairs; i++) {
		if (freqlink->cpu_ddr_fl_table[i].master_freq == cpu_freq) {
			ddr_freq = freqlink->cpu_ddr_fl_table[i].slave_freq;
			break;
		}
	}
RET:
	cpufreq_cpu_put(policy);
	return ddr_freq;
}

static int sprd_freqlink_vote_to_ddr(unsigned int vote_freq)
{
	int ret = 0;

	if (unlikely(!scene_enter)) {
		ret = scene_dfs_request("cpu");
		if (!ret)
			scene_enter = true;
		else
			return ret;
	}

	ret = change_scene_freq("cpu", vote_freq);
	if (!ret) {
		target_ddr_freq = vote_freq;
		trace_clock_set_rate("cpu-scene-ddr_req", target_ddr_freq,
					smp_processor_id());
	} else {
		pr_err("change cpu scene ddr freq to %dMHz failed, ret=%d\n", vote_freq, ret);
	}

	return ret;
}

static void sprd_freqlink_cpu_ddr_target(void)
{
	struct cpu_ddr_freqlink *freqlink = NULL;
	u64 now, delta_ns;
	unsigned int max_ddr_freq = 0, tmp_ddr_freq;

	if (!cpu_ddr_fl_initialized)
		return;

	mutex_lock(&ddrfreq_vote_mutex);
	if (!g_enable || freqlink_suspended)
		goto TARGET_DONE;

	now = sched_clock();
	delta_ns = now - last_ddr_freq_update_time;

	if (delta_ns < min(ddr_freq_up_time_ns, ddr_freq_down_time_ns))
		goto TARGET_DONE;

	/* choose the max target value among the freqlinks */
	list_for_each_entry(freqlink, &cpu_ddr_fl_list, node) {
		if (freqlink->enable)
			tmp_ddr_freq = sprd_freqlink_get_ddr_freq(freqlink);
		else
			tmp_ddr_freq = default_target;

		if (max_ddr_freq < tmp_ddr_freq)
			max_ddr_freq = tmp_ddr_freq;
	}

	if (max_ddr_freq == target_ddr_freq)
		goto TARGET_DONE;

	if ((max_ddr_freq > target_ddr_freq && delta_ns < ddr_freq_up_time_ns) ||
		(max_ddr_freq < target_ddr_freq && delta_ns < ddr_freq_down_time_ns))
		goto TARGET_DONE;

	if (!sprd_freqlink_vote_to_ddr(max_ddr_freq))
		last_ddr_freq_update_time = now;

TARGET_DONE:
	mutex_unlock(&ddrfreq_vote_mutex);
}

static int sprd_freqlink_ddr_vote(void *data)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		set_current_state(TASK_RUNNING);
		sprd_freqlink_cpu_ddr_target();
	}

	return 0;
}

static int sprd_freqlink_cpufreq_notifier(struct notifier_block *nb,
					  unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_policy *policy = freq->policy;
	struct cpu_ddr_freqlink *freqlink = NULL;

	if (!cpu_ddr_fl_initialized || !g_enable || freqlink_suspended)
		return NOTIFY_DONE;

	if (val != CPUFREQ_POSTCHANGE)
		return NOTIFY_DONE;

	list_for_each_entry(freqlink, &cpu_ddr_fl_list, node) {
		if (cpumask_test_cpu(freqlink->cpu, policy->related_cpus)) {
			if (!freqlink->enable)
				return NOTIFY_DONE;
		}
	}

	wake_up_process(ddr_freq_vote_task);

	return NOTIFY_OK;
}

static struct notifier_block cpufreq_notifier_block = {
	.notifier_call = sprd_freqlink_cpufreq_notifier,
};

static int sprd_freqlink_pm_notifier(struct notifier_block *nb,
		unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		mutex_lock(&ddrfreq_vote_mutex);
		freqlink_suspended = true;
		mutex_unlock(&ddrfreq_vote_mutex);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		freqlink_suspended = false;
		break;
	default:
		pr_err("Unknown PM request type!\n");
		break;
	}

	return 0;
}

static struct notifier_block freqlink_pm_notifier_block = {
	.notifier_call = sprd_freqlink_pm_notifier,
};

static int sprd_freqlink_reboot_notifier(struct notifier_block *nb,
					 unsigned long val, void *data)
{
	pr_info("system reboot, stop freqlink!\n");
	g_enable = false;

	return NOTIFY_OK;
}

static struct notifier_block freqlink_reboot_notifier_block = {
	.notifier_call = sprd_freqlink_reboot_notifier,
};

static u64 last_tick_update_time;
static void update_ddr_freq_by_tick(void *unused, struct rq *rq)
{
	u64 now;
	struct cpu_ddr_freqlink *freqlink = NULL;
	unsigned int max_ddr_freq = 0, tmp_ddr_freq;

	if (!cpu_ddr_fl_initialized || !g_enable || freqlink_suspended)
		return;

	now = sched_clock();
	if (now - last_tick_update_time < TICK_NSEC / 2)
		return;

	last_tick_update_time = now;
	list_for_each_entry(freqlink, &cpu_ddr_fl_list, node) {
		tmp_ddr_freq = sprd_freqlink_get_ddr_freq(freqlink);
		max_ddr_freq = max(max_ddr_freq, tmp_ddr_freq);
	}

	if (max_ddr_freq != target_ddr_freq)
		wake_up_process(ddr_freq_vote_task);
}

static ssize_t show_enable(struct cpufreq_policy *policy, char *buf)
{
	struct cpu_ddr_freqlink *freqlink = NULL;
	ssize_t count = 0;

	list_for_each_entry(freqlink, &cpu_ddr_fl_list, node) {
		if (cpumask_test_cpu(freqlink->cpu, policy->related_cpus)) {
			count = snprintf(buf, PAGE_SIZE, "%d\n", freqlink->enable);
			break;
		}
	}

	return count;
}

static ssize_t store_enable(struct cpufreq_policy *policy, const char *buf, size_t count)
{
	struct cpu_ddr_freqlink *freqlink = NULL;
	unsigned int val;
	bool enable, g_enable_tmp = 0;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	enable = !!val;

	mutex_lock(&ddrfreq_vote_mutex);
	list_for_each_entry(freqlink, &cpu_ddr_fl_list, node) {
		if (cpumask_test_cpu(freqlink->cpu, policy->related_cpus)) {
			if (freqlink->enable != enable) {
				freqlink->enable = enable;
			} else {
				mutex_unlock(&ddrfreq_vote_mutex);
				return count;
			}
		}
		g_enable_tmp |= freqlink->enable;
	}

	if (g_enable != g_enable_tmp) {
		g_enable = g_enable_tmp;

		if (!g_enable_tmp) {
			scene_exit("cpu");
			scene_enter = false;
			pr_info("g_enable is clear, cpu scene exit!\n");
		} else {
			wake_up_process(ddr_freq_vote_task);
		}
	}
	mutex_unlock(&ddrfreq_vote_mutex);

	return count;
}

static ssize_t show_ddr_freq_up_time_us(struct cpufreq_policy *policy, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n", ddr_freq_up_time_ns / NSEC_PER_USEC);
}

static ssize_t store_ddr_freq_up_time_us(struct cpufreq_policy *policy, const char *buf,
					size_t count)
{
	u64 val;

	if (sscanf(buf, "%llu\n", &val) != 1)
		return -EINVAL;

	pr_info("ddr_freq_up_time is changed to %lluus.\n", val);

	ddr_freq_up_time_ns = val * NSEC_PER_USEC;

	return count;
}

static ssize_t show_ddr_freq_down_time_us(struct cpufreq_policy *policy, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n", ddr_freq_down_time_ns / NSEC_PER_USEC);
}

static ssize_t store_ddr_freq_down_time_us(struct cpufreq_policy *policy, const char *buf,
					size_t count)
{
	u64 val;

	if (sscanf(buf, "%llu\n", &val) != 1)
		return -EINVAL;

	pr_info("ddr_freq_down_time is changed to %lluus.\n", val);

	ddr_freq_down_time_ns = val * NSEC_PER_USEC;

	return count;
}

static ssize_t show_freqlink_table(struct cpufreq_policy *policy, char *buf)
{
	struct cpu_ddr_freqlink *freqlink = NULL;
	ssize_t count = 0;
	int i;

	list_for_each_entry(freqlink, &cpu_ddr_fl_list, node) {
		if (cpumask_test_cpu(freqlink->cpu, policy->related_cpus)) {
			count += scnprintf(buf + count, PAGE_SIZE - count,
				"cpufreq(KHz)   ddrfreq(MHz)\n");

			for (i = 0; i < freqlink->npairs; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count, "%12u   %12u\n",
					freqlink->cpu_ddr_fl_table[i].master_freq,
					freqlink->cpu_ddr_fl_table[i].slave_freq);

			break;
		}
	}

	return count;
}

static ssize_t store_freqlink_table(struct cpufreq_policy *policy, const char *buf,
					size_t count)
{
	struct cpu_ddr_freqlink *freqlink = NULL;
	char *val_str, *val_str_tmp;
	int i, num = 0;
	unsigned int value;
	unsigned int table[20];

	val_str = kstrndup(buf, count, GFP_KERNEL);
	if (!val_str)
		return -ENOMEM;

	memset(table, 0, sizeof(table));
	while (((val_str_tmp = strsep(&val_str, " ")) != NULL) && (num < 20)) {
		if (sysfs_streq(val_str_tmp, ""))
			continue;

		val_str_tmp = strim(val_str_tmp);

		if (kstrtou32(val_str_tmp, 10, &value))
			return -EINVAL;

		table[num] = value;
		num++;
	}

	list_for_each_entry(freqlink, &cpu_ddr_fl_list, node) {
		if (cpumask_test_cpu(freqlink->cpu, policy->related_cpus)) {
			/* The number of frequencies is fixed. */
			if (num != freqlink->npairs)
				goto err;

			for (i = 0; i < num; i++)
				freqlink->cpu_ddr_fl_table[i].slave_freq = table[i];

			break;
		}
	}

	return count;

err:
	kfree(val_str);
	val_str = NULL;
	return -EINVAL;
}

cpufreq_freq_attr_rw(enable);
cpufreq_freq_attr_rw(ddr_freq_up_time_us);
cpufreq_freq_attr_rw(ddr_freq_down_time_us);
cpufreq_freq_attr_rw(freqlink_table);

static struct attribute *freqlink_attrs[] = {
	&enable.attr,
	&ddr_freq_up_time_us.attr,
	&ddr_freq_down_time_us.attr,
	&freqlink_table.attr,
	NULL
};

static const struct attribute_group freqlink_attr_group = {
	.attrs = freqlink_attrs,
	.name = "freqlink"
};

static void free_all_fl(void)
{
	struct cpu_ddr_freqlink *freqlink, *temp_freqlink;
	struct cpufreq_policy *policy;

	list_for_each_entry_safe(freqlink, temp_freqlink, &cpu_ddr_fl_list, node) {
		policy = cpufreq_cpu_get(freqlink->cpu);
		if (policy) {
			sysfs_remove_group(&policy->kobj, &freqlink_attr_group);
			cpufreq_cpu_put(policy);
		}

		kfree(freqlink->cpu_ddr_fl_table);
		list_del(&freqlink->node);
		kfree(freqlink);
		freqlink = NULL;
	}
}

static int sprd_freqlink_cpu_ddr_link_parse(void)
{
	struct cpufreq_policy *policy;
	struct device_node *cpu_node, *cdfl_node, *hwf_node;
	const struct property *fl_prop;
	const char *chip_type_str, *chip_type;
	const __be32 *val;
	int cpu, i, ret = 0;

	hwf_node = of_find_node_by_path("/hwfeature/auto");
	if (IS_ERR_OR_NULL(hwf_node)) {
		pr_err("NO hwfeature/auto node found\n");
		return PTR_ERR(hwf_node);
	}

	chip_type = of_get_property(hwf_node, "efuse", NULL);
	if (!chip_type) {
		of_node_put(hwf_node);
		return -EINVAL;
	}
	if (!strncmp(chip_type, "UMS9230H", strlen("UMS9230H")))
		chip_type_str = "freqlink-table-ums9230h";
	else
		chip_type_str = "freqlink-table";

	of_node_put(hwf_node);

	for_each_possible_cpu(cpu) {
		struct cpu_ddr_freqlink *cpu_ddr_fl_ptr;
		struct cpufreq_frequency_table *table;
		u32 enable, freq;

		cpu_node = of_get_cpu_node(cpu, NULL);
		if (!cpu_node) {
			pr_warn("CPU device node missing for CPU %d\n", cpu);
			return -EINVAL;
		}

		if (!of_find_property(cpu_node, "cpu-ddr-freq-link", NULL))
			continue;

		cdfl_node = of_parse_phandle(cpu_node, "cpu-ddr-freq-link", 0);
		if (!cdfl_node) {
			pr_warn("parse cpu-ddr-freq-link for cpu%d failed!\n", cpu);
			ret = -EINVAL;
			goto CPU_NODE_OUT;
		}

		ret = of_property_read_u32(cdfl_node, "freqlink-enable", &enable);
		if (ret < 0) {
			pr_warn("missing cpu%d freqlink-enable property\n", cpu);
			goto CDFL_NODE_OUT;
		}

		ret = of_property_read_u32(cdfl_node, "default-ddr-freq", &freq);
		if (ret < 0) {
			pr_warn("missing cpu%d default-ddr-freq property\n", cpu);
			goto CDFL_NODE_OUT;
		}

		if (!default_target && freq)
			default_target = freq;

		policy = cpufreq_cpu_get(cpu);
		if (policy == NULL) {
			pr_err("Failed to get cpu%d policy\n", cpu);
			ret = -ENODEV;
			goto CDFL_NODE_OUT;
		}

		cpu_ddr_fl_ptr = kcalloc(1, sizeof(struct cpu_ddr_freqlink), GFP_NOWAIT);
		if (!cpu_ddr_fl_ptr) {
			ret = -ENOMEM;
			goto ERR_OUT;
		}

		cpu_ddr_fl_ptr->enable = enable;
		g_enable |= cpu_ddr_fl_ptr->enable;

		cpu_ddr_fl_ptr->cpu = cpu;

		/* if there is no dts nodes for freqlink table, user can set the
		 * value by sysfs node.
		 */
		fl_prop = of_find_property(cdfl_node, chip_type_str, NULL);
		if (!fl_prop) {
			pr_info("no freklink property for cpu%d, get it by policy\n", cpu);

			cpu_ddr_fl_ptr->npairs =
			cpufreq_frequency_table_get_index(policy, policy->cpuinfo.max_freq) + 1;

			cpu_ddr_fl_ptr->cpu_ddr_fl_table = kcalloc(cpu_ddr_fl_ptr->npairs,
							sizeof(struct freqlink_pair), GFP_NOWAIT);

			if (!cpu_ddr_fl_ptr->cpu_ddr_fl_table) {
				pr_warn("alloc cpu_ddr_fl_table for cpu%d failed!\n", cpu);
				kfree(cpu_ddr_fl_ptr);
				ret = -ENOMEM;
				goto ERR_OUT;
			}

			table = policy->freq_table;
			for (i = 0; i < cpu_ddr_fl_ptr->npairs; i++) {
				/* In descending order. */
				cpu_ddr_fl_ptr->cpu_ddr_fl_table[i].master_freq =
					table[cpu_ddr_fl_ptr->npairs - 1 - i].frequency;
				cpu_ddr_fl_ptr->cpu_ddr_fl_table[i].slave_freq = 0;

			}
		} else {
			cpu_ddr_fl_ptr->npairs = (fl_prop->length / sizeof(u32)) / 2;
			cpu_ddr_fl_ptr->cpu_ddr_fl_table = kcalloc(cpu_ddr_fl_ptr->npairs,
							sizeof(struct freqlink_pair), GFP_NOWAIT);

			if (!cpu_ddr_fl_ptr->cpu_ddr_fl_table) {
				pr_warn("alloc cpu_ddr_fl_table for cpu%d failed!\n", cpu);
				kfree(cpu_ddr_fl_ptr);
				ret = -ENOMEM;
				goto ERR_OUT;
			}

			for (i = 0, val = fl_prop->value; i < cpu_ddr_fl_ptr->npairs; i++) {
				cpu_ddr_fl_ptr->cpu_ddr_fl_table[i].master_freq =
					be32_to_cpup(val++);
				cpu_ddr_fl_ptr->cpu_ddr_fl_table[i].slave_freq =
					be32_to_cpup(val++);
			}
		}

		list_add_tail(&cpu_ddr_fl_ptr->node, &cpu_ddr_fl_list);

		ret = sysfs_create_group(&policy->kobj, &freqlink_attr_group);
		if (ret)
			goto ERR_OUT;

		cpufreq_cpu_put(policy);
		of_node_put(cdfl_node);
		of_node_put(cpu_node);

	}

	if (list_empty(&cpu_ddr_fl_list)) {
		pr_err("get cpu-ddr freqlink fail!\n");
		return -EINVAL;
	}

	return 0;

ERR_OUT:
	cpufreq_cpu_put(policy);
CDFL_NODE_OUT:
	of_node_put(cdfl_node);
CPU_NODE_OUT:
	of_node_put(cpu_node);

	free_all_fl();

	return ret;
}

static int __init sprd_freqlink_init(void)
{
	int ret;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO / 2 };

	ret = sprd_freqlink_cpu_ddr_link_parse();
	if (ret) {
		pr_err("parse dt for cpu-ddr freqlink fail!\n");
		return ret;
	}

	ddr_freq_vote_task = kthread_create(sprd_freqlink_ddr_vote, NULL, "sprd_fl_ddr_vote");
	if (IS_ERR(ddr_freq_vote_task))
		return PTR_ERR(ddr_freq_vote_task);

	sched_setscheduler_nocheck(ddr_freq_vote_task, SCHED_FIFO, &param);

	if (g_enable)
		wake_up_process(ddr_freq_vote_task);

	cpufreq_register_notifier(&cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
	register_reboot_notifier(&freqlink_reboot_notifier_block);
	register_pm_notifier(&freqlink_pm_notifier_block);
	register_trace_android_vh_scheduler_tick(update_ddr_freq_by_tick, NULL);

	cpu_ddr_fl_initialized = 1;

	return ret;
}

late_initcall(sprd_freqlink_init);

MODULE_DESCRIPTION("sprd cpu ddr frequency link driver");
MODULE_LICENSE("GPL");
