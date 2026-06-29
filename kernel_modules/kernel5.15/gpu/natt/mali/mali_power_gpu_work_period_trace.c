// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2023 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/* Create the trace point if not configured in kernel */
#ifndef CONFIG_TRACE_POWER_GPU_WORK_PERIOD
#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)
#define CREATE_TRACE_POINTS
#include "mali_power_gpu_work_period_trace.h"
#else
uint64_t LastEndTimeNanoseconds = 0U;
void kbase_trace_gpu_work_period(struct kbase_device *kbdev, struct kbase_jd_atom *katom, ktime_t end_timestamp)
{
	const struct cred *cred = current_cred();
	const uint64_t MaxGpuTimeNanoseconds = 1000000000U;
	uint64_t start_time, end_time;
	uint32_t uid;
	start_time = ktime_to_ns(katom->start_timestamp);

	/*get current process's uid*/
	uid = cred->uid.val;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (start_time < LastEndTimeNanoseconds)
	{
		/*if the start_timestamp is later than LastEndTime, then modify start_timestamp*/
		start_time = LastEndTimeNanoseconds + 1;
	}

	if (ktime_to_ns(end_timestamp) - start_time > MaxGpuTimeNanoseconds)
	{
		/*The period duration must be at most 1 second*/
		end_time = start_time + MaxGpuTimeNanoseconds - 100;
	}
	else
	{
		end_time = ktime_to_ns(end_timestamp);
		/*The period duration must be non-zero.*/
		if (end_time < start_time)
			end_time = start_time + 10;
	}

	if (likely(kbdev))
	{
		trace_gpu_work_period(kbdev->id, uid, start_time, end_time +10, end_time - start_time);
		LastEndTimeNanoseconds = end_time + 10;
	}
	//dev_info(kbdev->dev "mali %s id = %u uid = %u, current->common = %s,current->pid=%u, start_time = %llu, end_time = %llu, duration = %llu, last_end_time =  %llu",
				//__func__, kbdev->id, uid, current->comm, current->pid,
				//start_time, end_time, end_time - start_time, LastEndTimeNanoseconds);
}
#endif /* CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD */
#endif
