/*
 * Copyright (C) 2021-2022 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "cam_thread.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_THREAD: %d %d %s : " fmt, current->pid, __LINE__, __func__

int camthread_loop(void *arg)
{
	struct cam_thread_info *thrd = NULL;

	if (!arg) {
		pr_err("fail to get valid input ptr\n");
		return -1;
	}

	thrd = (struct cam_thread_info *)arg;
	pr_info("%s loop starts %p\n", thrd->thread_name, thrd);
	while (1) {
		if (!IS_ERR_OR_NULL(thrd) && wait_for_completion_interruptible(
			&thrd->thread_com) == 0) {
			if (atomic_cmpxchg(&thrd->thread_stop, 1, 0) == 1) {
				pr_info("thread %s should stop.\n", thrd->thread_name);
				break;
			}
			if (thrd->proc_func(thrd->ctx_handle)) {
				pr_err("fail to start %s. exit thread\n", thrd->thread_name);
				if (thrd->data_cb_func) {
					thrd->data_cb_func(thrd->error_type, thrd->ctx_handle, thrd->data_cb_handle);
					break;
				}
			}
		} else {
			pr_debug("thread %s exit!", thrd->thread_name);
			break;
		}
	}
	complete(&thrd->thread_stop_com);
	return 0;
}

int camthread_create(void *param,
	struct cam_thread_info *thrd, proc_func func)
{
	thrd->ctx_handle = param;
	thrd->proc_func = func;

	atomic_set(&thrd->thread_stop, 0);
	init_completion(&thrd->thread_com);
	init_completion(&thrd->thread_stop_com);
	thrd->thread_task = kthread_run(camthread_loop,
		thrd, "%s", thrd->thread_name);
	if (IS_ERR_OR_NULL(thrd->thread_task)) {
		pr_err("fail to start thread %s\n", thrd->thread_name);
		thrd->thread_task = NULL;
		return -EFAULT;
	}
	return 0;
}

int camthread_stop(struct cam_thread_info *thrd)
{
	int ret = 0;

	if (thrd->thread_task) {
		atomic_set(&thrd->thread_stop, 1);
		complete(&thrd->thread_com);
		ret = wait_for_completion_timeout(&thrd->thread_stop_com, CAMTHREAD_STOP_TIMEOUT);
		if (ret == 0)
			pr_err("fail to stop wait thread %s, timeout.\n", thrd->thread_name);
		else
			pr_info("thread %s stopped. wait %d ms\n", thrd->thread_name, ret);
		thrd->thread_task = NULL;
		thrd = NULL;
	}
	return 0;
}
