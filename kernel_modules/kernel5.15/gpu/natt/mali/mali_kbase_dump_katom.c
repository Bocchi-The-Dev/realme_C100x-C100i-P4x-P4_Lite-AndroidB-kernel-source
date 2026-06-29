// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 * This file has been modified by Unisoc (Shanghai) Technologies Co., Ltd in 2024.
 *
 * (C) COPYRIGHT 2012-2023 ARM Limited. All rights reserved.
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

#define __NO_FORTIFY
#include <linux/dma-buf.h>
#include <linux/dma-fence-array.h>
#if IS_ENABLED(CONFIG_COMPAT)
#include <linux/compat.h>
#endif
#include <mali_kbase.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/ratelimit.h>
#include <linux/priority_control_manager.h>
#if KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE
#include <linux/sched/signal.h>
#else
#include <linux/signal.h>
#endif

#include <mali_kbase_jm.h>
#include <mali_kbase_fence.h>
#include <mali_kbase_sync.h>

#include <mali_kbase_linux.h>
#include <mali_kbase_dump_katom.h>


#ifdef SPRD_SUPPORT_FAULT_KEYWORD
#include <linux/sunrpc/cache.h>
extern ktime_t time[FAULT_KEYWORD_NUM];
#endif

#define IS_TIMEOUT(a, b, c) ((b != 0) && (a - b > c))

extern const char *kbase_l2_core_state_to_string(enum kbase_l2_core_state state);
extern const char *kbase_shader_core_state_to_string(enum kbase_shader_core_state state);
extern void kbase_debug_dump_registers(struct kbase_device *kbdev);
static void kbase_atom_dump_info(struct kbase_jd_atom *katom, bool reset);

enum {
	CORE_REQ_DEP_ONLY,
	CORE_REQ_SOFT_FENCE_WAIT,
	CORE_REQ_SOFT_FENCE_TRIGGER,
	CORE_REQ_SOFT_JIT_ALLOC,
	CORE_REQ_SOFT_JIT_FREE,
	CORE_REQ_SOFT,
	CORE_REQ_COMPUTE,
	CORE_REQ_FRAGMENT,
	CORE_REQ_VERTEX,
	CORE_REQ_TILER,
	CORE_REQ_FRAGMENT_VERTEX,
	CORE_REQ_FRAGMENT_VERTEX_TILER,
	CORE_REQ_FRAGMENT_TILER,
	CORE_REQ_VERTEX_TILER,
	CORE_REQ_UNKNOWN
};
static const char *const core_req_strings[] = {
	"DepOnly",
	"FenceWait",
	"FenceTrigger",
	"JitAlloc",
	"JitFree",
	"OtherSoftJob",
	"CS",
	"FS",
	"V/GS",
	"Tiler",
	"FS+VS",
	"FS+V/GS+Tiler",
	"FS+Tiler",
	"V/GS+Tiler",
	"UnknownJob"
};
const char *kbasep_map_core_reqs_to_string(base_jd_core_req core_req)
{
	if (core_req & BASE_JD_REQ_SOFT_JOB) {
		switch (core_req) {
		case BASE_JD_REQ_SOFT_FENCE_WAIT:
			return core_req_strings[CORE_REQ_SOFT_FENCE_WAIT];
		case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
			return core_req_strings[CORE_REQ_SOFT_FENCE_TRIGGER];
		case BASE_JD_REQ_SOFT_JIT_ALLOC:
			return core_req_strings[CORE_REQ_SOFT_JIT_ALLOC];
		case BASE_JD_REQ_SOFT_JIT_FREE:
			return core_req_strings[CORE_REQ_SOFT_JIT_FREE];
		}
		return core_req_strings[CORE_REQ_SOFT];
	}

	if (core_req & BASE_JD_REQ_ONLY_COMPUTE)
		return core_req_strings[CORE_REQ_COMPUTE];
	switch (core_req & (BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T)) {
	case BASE_JD_REQ_DEP:
		return core_req_strings[CORE_REQ_DEP_ONLY];
	case BASE_JD_REQ_FS:
		return core_req_strings[CORE_REQ_FRAGMENT];
	case BASE_JD_REQ_CS:
		return core_req_strings[CORE_REQ_VERTEX];
	case BASE_JD_REQ_T:
		return core_req_strings[CORE_REQ_TILER];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_CS):
		return core_req_strings[CORE_REQ_FRAGMENT_VERTEX];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_FRAGMENT_TILER];
	case (BASE_JD_REQ_CS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_VERTEX_TILER];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_FRAGMENT_VERTEX_TILER];
	}
	return core_req_strings[CORE_REQ_UNKNOWN];
}

static void kbase_get_time_string(char *time_string, ktime_t time)
{
	u32 time_s = time / (1000 * 1000 * 1000);
	u32 time_ms = time / (1000 * 1000);
	u64 time_us = time / 1000;
	if (time != 0)
		sprintf(time_string, "%02us:%03ums:%03uus", time_s % 60, time_ms % 1000, (unsigned)(time_us % 1000));
	else
		sprintf(time_string, "-1");
}

//void kbase_dump_dma_fence_array(struct kbase_device *kbdev, struct dma_fence *head)
//{
	// int index = 0;
	// struct dma_fence *fence = NULL;
	// dma_fence_array_for_each(fence, index, head)
	// {
	// 	if (fence->ops && fence->ops->get_driver_name && fence->ops->get_timeline_name)
	// 	{
	// 		printk("dma_fence_array idx:%d fence[drv_name:%s tl_name:%s ctx:%llu seq:%llu]\n",
	// 			index, fence->ops->get_driver_name(fence), fence->ops->get_timeline_name(fence), fence->context, fence->seqno);
	// 	}
	// 	kbase_dump_dma_fence(kbdev, fence);
	// }
//}


void kbase_dump_dma_fence(struct kbase_device *kbdev, struct dma_fence *fence)
{
	if (fence == NULL)

		printk("mali dump_dma_fence_in failed\n");

	if (is_mali_fence(fence))
	{
		struct kbase_context *temp_kctx = NULL;
		struct kbase_jd_atom *dump_katom = NULL;
		int i;
		mutex_lock(&kbdev->kctx_list_lock);
		list_for_each_entry(temp_kctx, &kbdev->kctx_list, kctx_list_link) {
#if defined(CONFIG_MALI_DMA_FENCE) || defined(CONFIG_SYNC_FILE)
			if (temp_kctx != NULL) {
				for (i = 0; i < BASE_JD_ATOM_COUNT; i++) {
					if (temp_kctx->jctx.atoms[i].dma_fence.context == fence->context &&
						atomic_read(&temp_kctx->jctx.atoms[i].dma_fence.seqno) == fence->seqno) {
						dump_katom = &temp_kctx->jctx.atoms[i];
						break;
					}
				}
			}
#endif
		}
		if (dump_katom != NULL && kbase_active_dump_atoms(dump_katom, TIMEOUT_10ms))
		{
			printk("mali dump katom info at %s:%d\n", __func__, __LINE__);
		}
		mutex_unlock(&kbdev->kctx_list_lock);
	} 
	//else if (dma_fence_is_array(fence))
	//	kbase_dump_dma_fence_array(kbdev, fence);
}

static void kbase_dump_pm_status(struct kbase_context* kctx)
{
	unsigned long flags;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbasep_js_device_data *js_devdata =  &kbdev->js_data;
	struct kbase_pm_backend_data *backend = &kctx->kbdev->pm.backend;
	ktime_t cur_t = ktime_get();

	if (kctx->dump_job_status_time == 0 ||
		IS_TIMEOUT(cur_t, kctx->dump_job_status_time, KATOM_DUMP_PERIOD)) {

		pr_info("GPU Kctx State:kctx:%px pid:%d tgid:%d", kctx, kctx->pid, kctx->tgid);
		pr_info("refcount:%d flags:0x%x pulled_all_slots:%d ",
			atomic_read(&kctx->refcount), atomic_read(&kctx->flags),
			atomic_read(&kctx->atoms_pulled_all_slots));
		pr_info("as_nr:%d age_count:%d slots_pullable:0x%x ",
			kctx->as_nr, kctx->age_count, kctx->slots_pullable);
		pr_info("event_count:%d event_closed:%d poll_status:{time:%lldms ",
			atomic_read(&kctx->event_count), atomic_read(&kctx->event_closed),
			kctx->poll_status.poll_time_newest / (1000*1000));
		pr_info("event_pending:%d} submit_allowed:0x%x priority:%d\n",
			kctx->poll_status.event_pending, js_devdata->runpool_irq.submit_allowed,
			kctx->priority);
		kctx->dump_job_status_time = cur_t;
	}

	if (kbdev->dump_pm_status_time == 0 ||
		IS_TIMEOUT(cur_t, kbdev->dump_pm_status_time, KATOM_DUMP_PERIOD)) {

		pr_info("GPU Power State:active_count:%d suspending:%d gpu_powered:%d ",
			kbdev->pm.active_count, kbdev->pm.suspending, backend->gpu_powered);
		pr_info("l2_state:%s shaders_state:%s ",
			kbase_l2_core_state_to_string(backend->l2_state),
			kbase_shader_core_state_to_string(backend->shaders_state));
		pr_info("wait_in_progress:%d invoke_poweroff_wait:%d poweron_required:%d\n",
			backend->poweroff_wait_in_progress,
			backend->invoke_poweroff_wait_wq_when_l2_off,
			backend->poweron_required);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		if (kbdev->pm.backend.gpu_powered)
			kbase_debug_dump_registers(kctx->kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		kbdev->dump_pm_status_time = cur_t;
	}
}

bool kbase_active_dump_atoms(struct kbase_jd_atom *katom, u64 timeout)
{
	int i, success = 0;
	ktime_t current_time, suspend_time, resume_time;
	struct kbase_context *kctx;
	bool res = false;

	if (katom == NULL) {
		printk("%s:%d The katom is NULL", __func__, __LINE__);
		return res;
	}

	kctx = katom->kctx;
	if (kctx == NULL) {
		printk("%s:%d The kctx is NULL", __func__, __LINE__);
		return res;
	}

	success = mutex_trylock(&kctx->dump_lock);
	if (!success) {
		printk("%s:%d Acquire dump_lock failed\n", __func__, __LINE__);
		return res;
	}

	mutex_lock(&kctx->kbdev->time_lock);
	suspend_time = kctx->kbdev->suspend_time;
	resume_time = kctx->kbdev->resume_time;
	mutex_unlock(&kctx->kbdev->time_lock);

	current_time = ktime_get();

	if (katom->jb_proc_ts.time[JB_SUBMIT] != 0 &&
		katom->jb_proc_ts.time[JB_SUBMIT] < suspend_time &&
		current_time > resume_time && resume_time != 0) {
		katom->jb_proc_ts.time[JB_SUBMIT] += (resume_time - suspend_time);
		printk("%s:%d suspend happened after job is submitted!\n", __func__, __LINE__);
	}

	if (((katom->jb_proc_ts.time[PROCESS_EVENT] == 0) &&
		IS_TIMEOUT(current_time, katom->jb_proc_ts.time[JB_SUBMIT], timeout)) ||
		((katom->jb_proc_ts.time[PROCESS_EVENT] != 0) &&
		IS_TIMEOUT(katom->jb_proc_ts.time[PROCESS_EVENT], katom->jb_proc_ts.time[JB_SUBMIT], timeout))) {
		;
	} else {
		mutex_unlock(&kctx->dump_lock);
		return res;
	}

	res = true;
	printk("------------------------KMD DUMP KATOMS INFO---------------------");

	/*dump power management relate info*/
	kbase_dump_pm_status(kctx);

	/*dumping target katom info*/
	kbase_atom_dump_info(katom, false);

	/*dumping dependent katom info*/
	for (i = 0; i < BASE_JD_ATOM_COUNT; i++) {
		struct kbase_jd_atom *atom = &kctx->jctx.atoms[i];
		if (test_bit(i, katom->dependencies)) {
			current_time = ktime_get();
			if ( atom->last_dump_time == 0 ||
				(current_time - atom->last_dump_time >= KATOM_DUMP_PERIOD)) {
				kbase_atom_dump_info(atom, false);
			} else {
				char time_str[100];
				kbase_get_time_string(time_str, atom->last_dump_time);
				printk("mali atom_%d has been dumped at %s\n", i, time_str);
			}
		}
	}

	printk("---------------------------------------------------------------");
	mutex_unlock(&kctx->dump_lock);
	return res;
}

int kbase_dump_atoms(struct kbase_context *kctx, u64 device, u64 command)
{
	struct kbase_jd_atom *atoms;
	int i = 0, success = 0;
	ktime_t cur_t = ktime_get();
	atoms = kctx->jctx.atoms;

	if (kbase_ctx_flag(kctx, KCTX_DYING)) {
		pr_info("%s:%d The kctx is Dying", __func__, __LINE__);
		return -1;
	}

	atomic_set(&kctx->jctx.is_dumping, 1);

#ifdef SPRD_SUPPORT_FAULT_KEYWORD
	time[ATOM_DUMPED] = cur_t;
#endif

	success = mutex_trylock(&kctx->dump_lock);
	if (!success) {
		printk("%s:%d Acquire dump_lock failed\n", __func__, __LINE__);
		return -1;
	}

	pr_info("------------------------UMD IOCTL DUMP ATOM INFO---------------------");
	kbase_dump_pm_status(kctx);

	for (i = 0; i != BASE_JD_ATOM_COUNT; ++i) {
		struct kbase_jd_atom *atom = &atoms[i];
		if ((atom->udata.blob[1] == command) && (atom->udata.blob[0] == device)) {
			if (atom->last_dump_time == 0 ||
				IS_TIMEOUT(cur_t, atom->last_dump_time, KATOM_DUMP_PERIOD)) {
				struct kbase_jd_atom *target_atom;
				kbase_atom_dump_info(atom, false);
				target_atom = atom;
				/*dumping dependent katom info*/
				printk("mali dumping dependent info of atom:%p\n", atom);
				for (i = 0; i < BASE_JD_ATOM_COUNT; i++) {
					struct kbase_jd_atom *dep_atom = &kctx->jctx.atoms[i];
					if (test_bit(i, target_atom->dependencies)) {
						kbase_atom_dump_info(dep_atom, false);
					}
				}
			} else {
				char time_str[100];
				kbase_get_time_string(time_str, atom->last_dump_time);
				printk("mali skip dumping info of atom_%d due to it has been dumped at %s\n", i, time_str);
			}
			break;
		}

	}
	if (i == BASE_JD_ATOM_COUNT)
		printk("mali there is no eligible atom for command:0x%llx\n", command);
	pr_info("------------------------------------------------------------");
	atomic_set(&kctx->jctx.is_dumping, 0);
	wake_up(&kctx->jctx.dumping_atoms_wait);
	mutex_unlock(&kctx->dump_lock);
	return 0;
}

static void kbase_atom_dump_info(struct kbase_jd_atom *katom, bool reset)
{
	ktime_t cur_t = ktime_get();
	int job_type = 0, i;
	const struct kbase_context* kctx = katom->kctx;

#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
	struct fence *fence;
#else
	struct dma_fence *fence;
#endif
	char time_string[JB_TIME_NUM+1][100];
	char dep_resolved_string[2][100], start_string[100], fence_wait[2][100];

	if (kctx == NULL) {
		pr_err("%s:%d The kctx is NULL", __func__, __LINE__);
		dump_stack();
		return;
	}

	kbase_get_time_string(time_string[JB_TIME_NUM], cur_t);
	kbase_get_time_string(dep_resolved_string[0], katom->user_dep[0] != -1 ? katom->dep[0].dep_resolved : 0);
	kbase_get_time_string(dep_resolved_string[1], katom->user_dep[1] != -1 ? katom->dep[1].dep_resolved : 0);

	for (i = 0; i < JB_TIME_NUM; i++) {
		kbase_get_time_string(time_string[i], katom->jb_proc_ts.time[i]);
	}

	pr_info("                        ");
	katom->last_dump_time = cur_t;

	// 0x202 0x203 0x201
	if (katom->core_req & BASE_JD_REQ_SOFT_JOB)
		job_type |= 0x1;
	else if ((katom->core_req & BASE_JD_REQ_HARD_JOB))
		job_type |= 0x2;

	pr_info("Dump job infos-1 katom{atom_number{%d} flush_id{0x%x} core_type{%s}},"
			"core_req{0x%x} status{0x%x} run_status{0x%x} rb_state{0x%x} seq_nr{%lld} age{%d}\n",
			katom->atom_number, katom->flush_id, kbasep_map_core_reqs_to_string(katom->core_req),
			katom->core_req, katom->status, katom->run_status, katom->gpu_rb_state, katom->seq_nr, katom->age);

	pr_info("Dump job infos-2 usr_dep[0]{%d}-{%s} usr_dep[1]{%d}-{%s} pulled{%d}"
			" atom_flags{0x%x} event_code{0x%x} wfe_code{0x%x} "
			"blocked{%d} pri{%d} slot_nr{%d} e{pid:%d tgid:%d}\n",
			katom->user_dep[0], dep_resolved_string[0], katom->user_dep[1], dep_resolved_string[1],
			atomic_read(&kctx->atoms_pulled_all_slots), katom->atom_flags, katom->event_code, katom->will_fail_event_code,
			atomic_read(&katom->blocked), katom->sched_priority, katom->slot_nr, kctx->pid, kctx->tgid);

	pr_info("Dump job infos-3 cur{%s} jb_submit{%s} js_add{%s} js_pull{%s} js_complete{%s} "
			"jd_nolock{%s} post_event{%s} wakeup{%s} process_event{%s}\n",
			time_string[JB_TIME_NUM], time_string[JB_SUBMIT], time_string[JS_ADD], time_string[JS_PULL], time_string[JS_COMPLETE],
			time_string[JD_DONE], time_string[POST_EVENT], time_string[WAKE_UP], time_string[PROCESS_EVENT]);

	/*
	 * 1.job submit -> wake up
	 * 2.wake up -> event process
	 */
	// software job
	/* fence err
	 * < 0 : error
	 * 0 : active
	 * 1 : signaled
	 */

	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_EVENT_WAIT:
		pr_info("software job infos: work{jd_req_soft_event_set:%d ioctl:%d soft_event(q:%lldms, w:%lldms)}",
			katom->core_req & BASE_JD_REQ_SOFT_EVENT_SET ? 1 : 0, kctx->ioctl & KBASE_IOCTL_SOFT_EVENT_UPDATE ? 1 : 0,
			katom->soft_event_complete_work.queue_work_time / (1000*1000), katom->soft_event_complete_work.done_work_time / (1000*1000));
		break;
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
		fence = dma_fence_get(katom->dma_fence.fence);
		if (fence) {
			pr_info("software job infos: fence trigger{fence_status(0x%px Drv:%s TL:%s Context:%llu Seqo:%llu flags:%lu)"
														" signal_time:%s jb_wait_t:%lldms}",
				fence, fence->ops->get_driver_name(fence), fence->ops->get_timeline_name(fence),
				fence->context, fence->seqno, fence->flags, time_string[FENCE_TRIGGER],
				(cur_t - katom->jb_proc_ts.time[JB_SUBMIT]) / (1000*1000));
			dma_fence_put(fence);
		} else {
			pr_info("software job infos: gpu fence has signaled at %s", time_string[FENCE_TRIGGER]);
		}
		break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT:
		kbase_get_time_string(fence_wait[0], katom->sync_wait_fence_work.queue_work_time);
		kbase_get_time_string(fence_wait[1], katom->sync_wait_fence_work.done_work_time);
		fence = dma_fence_get(katom->dma_fence.fence_in);
		if (fence) {
			pr_info("software job infos: work{fence_status(0x%px Drv:%s TL:%s Context:%llu Seqo:%llu flags:%lu)"
											" soft_fence(q{%s}-w{%s}) job waittime:%lldms}",
				fence, fence->ops->get_driver_name(fence), fence->ops->get_timeline_name(fence),
				fence->context, fence->seqno, fence->flags, fence_wait[0], fence_wait[1],
				(cur_t - katom->jb_proc_ts.time[JB_SUBMIT]) / (1000*1000));
			dma_fence_put(fence);
		} else {
			if ((katom->run_status & (KRun_FenceInAddCallbackFail | KRun_SyncFenceInWaitAndQueueWork))== 0)
				pr_info("software job infos: fence_in before gpu wait has signaled");
			else
				pr_info("software job infos: gpu wait fence_in has signaled, cb_added_fail:%d fence_cb_added:%d soft_fence(q{%s}-w{%s})",
						katom->run_status & KRun_FenceInAddCallbackFail ? 1 : 0,
						katom->run_status & KRun_SyncFenceInWaitAndQueueWork ? 1 : 0, fence_wait[0], fence_wait[1]);
		}
		break;
	default:
		break;
	}

	// hardware job
	if (job_type & 0x2)  {
		kbase_get_time_string(start_string, katom->start_timestamp);
		pr_info("hardware job infos: work{hw_job(start:%s,hw_submit:%s,hw_end:%s,code:0x%x)}",
			start_string, time_string[HW_SUBMIT], time_string[HW_END], katom->completion_code);
	}

	if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
		pr_info("external resources: dma fence may exist, work{dma_fence_work(q:%lldms, w:%lldms)}",
			katom->dma_fence_work.queue_work_time / (1000*1000), katom->dma_fence_work.done_work_time / (1000*1000));

}

void kbase_atom_dump_reset(struct kbase_jd_atom *katom)
{
	katom->completion_code = 0;
	katom->run_status = KRun_Invalid;
	bitmap_zero(katom->dependencies, BASE_JD_ATOM_COUNT);
	katom->last_dump_time = 0;
	memset(&katom->jb_proc_ts, 0, sizeof(struct kbase_job_process_timestamp));
	memset(&katom->jd_done_work, 0, sizeof(struct kbase_work_time_spent));
	memset(&katom->dma_fence_work, 0, sizeof(struct kbase_work_time_spent));
	memset(&katom->sync_wait_fence_work, 0, sizeof(struct kbase_work_time_spent));
	memset(&katom->soft_event_complete_work, 0, sizeof(struct kbase_work_time_spent));
}
