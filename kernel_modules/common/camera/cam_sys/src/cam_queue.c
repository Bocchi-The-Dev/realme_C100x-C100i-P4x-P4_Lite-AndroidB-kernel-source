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

#include <linux/slab.h>

#include "cam_queue.h"
#include "cam_buf_manager.h"
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_QUEUE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static struct cam_queue_frame_manager *g_frame_manager = NULL;
static int camqueue_frame_state_clear(struct cam_frame *pframe)
{
	if (pframe == NULL) {
		pr_err("fail to get frame\n");
		return -1;
	}

	switch (pframe->type) {
	case CAM_FRAME_GENERAL:
		if (pframe->common.buf.type != CAM_BUF_NONE && pframe->common.buf.status != CAM_BUF_EMPTY) {
			cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_MOVE_TO_ALLOC, CAM_BUF_IOMMUDEV_MAX);
			if (pframe->common.buf.type == CAM_BUF_KERNEL)
				cam_buf_free(&pframe->common.buf);
		}
		if (pframe->common.is_reserved == CAM_RESERVED_BUFFER_COPY)
			cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_MOVE_TO_ION, CAM_BUF_IOMMUDEV_MAX);

		if (pframe->common.pframe_data)
			cam_queue_empty_frame_put(pframe->common.pframe_data);
		memset(&pframe->common, 0, sizeof(struct camera_frame));
		break;
	case CAM_FRAME_ISP_BLK:
		if (pframe->isp_blk.param_block)
			cam_buf_kernel_sys_vfree(pframe->isp_blk.param_block);
		memset(&pframe->isp_blk, 0, sizeof(struct cam_ispblk_frame));
		break;
	case CAM_FRAME_DEC_BLK:
		if (pframe->dec_blk.decblk_pm)
			cam_buf_kernel_sys_vfree(pframe->dec_blk.decblk_pm);
		memset(&pframe->dec_blk, 0, sizeof(struct cam_decblk_frame));
		break;
	case CAM_FRMAE_USER_ZOOM:
		memset(&pframe->user_zoom, 0, sizeof(struct camera_zoom_frame));
		break;
	case CAM_FRAME_NODE_ZOOM:
		memset(&pframe->node_zoom, 0, sizeof(struct cam_zoom_frame));
		break;
	default:
		pr_err("fail to get pframe %p type:%d\n", pframe, pframe->type);
		break;
	}
	pframe->type = 0;

	return 0;
}

/* in irq handler, may return NULL if alloc failed
 * else: will always retry alloc and return valid frame
 */
static int camqueue_frame_alloc(struct cam_queue_frame_manager *frame_manager)
{
	struct cam_frame *frame_array = NULL;
	int32_t ret = 0, j = 0;
	if (frame_manager->array_num >= CAM_EMP_ARRAY_LEN_MAX) {
		pr_err("fail to array_num:%d surpass max\n", frame_manager->array_num);
		return -1;
	}
	if (in_interrupt()) {
		frame_array = cam_buf_kernel_sys_kzalloc(sizeof(struct cam_frame) * CAM_EMP_ARRAT_LEN_PER(struct cam_frame), GFP_ATOMIC);
		if (!frame_array)
			return -1;
	} else
		frame_array = cam_buf_kernel_sys_kzalloc(sizeof(struct cam_frame) * CAM_EMP_ARRAT_LEN_PER(struct cam_frame), GFP_KERNEL);

	if (frame_array) {
		spin_lock(&frame_manager->frame_lock);
		for (j = 0; j < CAM_EMP_ARRAT_LEN_PER(struct cam_frame); ++j) {
			ret = CAM_QUEUE_ENQUEUE(&frame_manager->empty_frame_q, &frame_array[j].list);
			if (ret) {
				pr_err("fail to enqueue empty frame_q\n");
				break;
			}
		}
		if (ret) {
			while (j > 0) {
				--j;
				CAM_QUEUE_DEQUEUE_TAIL(&frame_manager->empty_frame_q, struct cam_frame, list);
			}
			cam_buf_kernel_sys_kfree(frame_array);
			frame_array = NULL;
			spin_unlock(&frame_manager->frame_lock);
			return -1;
		}
		frame_manager->frame_array[frame_manager->array_num] = frame_array;
		frame_manager->array_num++;
		atomic_add(CAM_EMP_ARRAT_LEN_PER(struct cam_frame), &g_mem_dbg->empty_frm_cnt);
		spin_unlock(&frame_manager->frame_lock);
	}
	pr_debug("frame alloc add num:%lu, q cnt:%d\n",
		CAM_EMP_ARRAT_LEN_PER(struct cam_frame),
		CAM_QUEUE_CNT_GET(&g_frame_manager->empty_frame_q));
	return 0;
}

struct cam_frame *cam_queue_dequeue_if(struct camera_queue *q,
	enum cam_en_status (*filter)(struct cam_frame *, void *), void *data)
{
	int fatal_err = 0;
	unsigned long flags = 0;
	struct cam_frame *pframe = NULL;
	struct cam_q_head *_list = NULL;

	if (q == NULL || !filter) {
		pr_err("fail to get valid param %p %p\n", q, filter);
		return NULL;
	}

	spin_lock_irqsave(&q->lock, flags);
	if (q->state == CAM_Q_CLEAR) {
		pr_warn("warning: q is clear\n");
		goto unlock;
	}

	if (list_empty(&q->head) || q->cnt == 0) {
		pr_debug("queue empty %d, %d\n",
			list_empty(&q->head), q->cnt);
		fatal_err = (list_empty(&q->head) ^ (q->cnt == 0));
		if (fatal_err)
			pr_err("fail to get list node, empty %d, cnt %d\n",
				list_empty(&q->head), q->cnt);
		goto unlock;
	}

	_list = list_first_entry(&q->head, struct cam_q_head, list);
	pframe = container_of(_list, struct cam_frame, list);
	if (!filter(pframe, data)) {
		pframe = NULL;
		goto unlock;
	}

	CAM_QUEUE_LIST_DEL(_list);
	q->cnt--;

unlock:
	spin_unlock_irqrestore(&q->lock, flags);

	return pframe;
}

/* Get the match two frame from two module
 * Through the master frame, travers the slave
 * queue, find a matching frame, if there is no,
 * use the slave next frame.
 */
int cam_queue_same_frame_get(struct camera_queue *q0,
	struct cam_frame **pf0, int64_t t_sec, int64_t t_usec)
{
	int ret = 0;
	unsigned long flags0 = 0;
	struct cam_frame *tmpf0 = NULL;
	int64_t t0 = 0, t1 = 0, t_diff = 0, mint = 0;

	spin_lock_irqsave(&q0->lock, flags0);
	if (list_empty(&q0->head)) {
		pr_warn("warning:Get list node fail\n");
		ret = -EFAULT;
		goto _EXT;
	}
	/* set mint a large value */
	mint = (((uint64_t)1 << 63) - 1);
	/* get least delta time two frame */
	t1 = t_sec * 1000000ll;
	t1 += t_usec;
	CAM_QUEUE_FOR_EACH_ENTRY(tmpf0, &q0->head, list) {
		t0 = tmpf0->common.sensor_time.tv_sec * 1000000ll;
		t0 += tmpf0->common.sensor_time.tv_usec;
		t_diff = t0 - t1;
		if (t_diff < 0)
			t_diff = -t_diff;
		if (t_diff < mint) {
			*pf0 = tmpf0;
			mint = t_diff;
		}
		if (mint < 400)
			break;
	}
	pr_info("mint:%lld\n", mint);
	t_diff = div64_u64((*pf0)->common.frame_interval_time, 1000 * 2);
	if (mint > t_diff) { /* delta > slave dynamic frame rate half fail */
		ret = -EFAULT;
		goto _EXT;
	}
	CAM_QUEUE_LIST_DEL(&((*pf0)->list));
	q0->cnt--;
	ret = 0;
_EXT:
	spin_unlock_irqrestore(&q0->lock, flags0);

	return ret;
}

int cam_queue_frame_array_check(uint32_t mode)
{
	int32_t i = 0, j = 0;
	struct cam_frame *pframe = NULL;
	for (i = 0; i < CAM_EMP_ARRAY_LEN_MAX; ++i) {
		if (!g_frame_manager->frame_array[i])
			break;
		for (j = 0; j < CAM_EMP_ARRAT_LEN_PER(struct cam_frame); ++j) {
			pframe = &g_frame_manager->frame_array[i][j];
			switch (mode) {
			case CAM_FRAME_CHECK_QUEUE_OUT:
				if (atomic_read(&pframe->list.status) == CAM_Q_FREE) {
					pr_warn("warning: frame:%p leak, frame type:%d status:%d\n",
						pframe, pframe->common.buf.type,
						pframe->common.buf.status);
					cam_queue_empty_frame_put(pframe);
				}
				break;
			case CAM_FRAME_CHECK_ALL:
				if (pframe->common.buf.status != CAM_BUF_EMPTY) {
					pr_warn("warning: frame:%p, type:%d status:%d list status:%d\n",
						pframe, pframe->common.buf.type,
						pframe->common.buf.status,
						atomic_read(&pframe->list.status));
					camqueue_frame_state_clear(pframe);
				}
				break;
			default:
				pr_err("fail to get mode:%d\n", mode);
			}
		}
	}
	return 0;
}

struct cam_frame *cam_queue_empty_frame_get(enum camera_frame_type type)
{
	int ret = 0;
	struct cam_frame *pframe = NULL;

	pr_debug("Enter.\n");
	if (CAM_QUEUE_CNT_GET(&g_frame_manager->empty_frame_q) <= CAM_EMP_ARRAT_LEN_PER(struct cam_frame) && !in_interrupt())
		camqueue_frame_alloc(g_frame_manager);
	do {
		pframe = CAM_QUEUE_DEQUEUE(&g_frame_manager->empty_frame_q, struct cam_frame, list);
		if (pframe == NULL) {
			ret = camqueue_frame_alloc(g_frame_manager);
			if (ret) {
				pr_err("fail to alloc frame\n");
				break;
			}
		}
	} while (pframe == NULL);

	if (pframe)
		pframe->type = type;
	pr_debug("Done. get frame %p\n", pframe);
	return pframe;
}

void cam_queue_empty_frame_put(void *frame)
{
	int ret = 0;
	struct cam_frame *pframe = NULL;

	if (frame == NULL) {
		pr_err("fail to get valid param\n");
		return;
	}
	pframe = (struct cam_frame *)frame;
	if (g_frame_manager->delay_put_thread.thread_task) {
		CAM_QUEUE_ENQUEUE(&g_frame_manager->delay_put_q, &pframe->list);
		complete(&g_frame_manager->delay_put_thread.thread_com);
	} else {
		camqueue_frame_state_clear(pframe);
		ret = CAM_QUEUE_ENQUEUE(&g_frame_manager->empty_frame_q, &pframe->list);
		if (ret)
			pr_warn("warning:frame in another queue or check fail\n");
	}
}

int cam_queue_frame_put_thread(void *param)
{
	int ret = 0;
	struct cam_frame *pframe = NULL;
	struct cam_queue_frame_manager *frame_manager = NULL;

	frame_manager = (struct cam_queue_frame_manager *)param;
	pframe = CAM_QUEUE_DEQUEUE(&frame_manager->delay_put_q, struct cam_frame, list);
	camqueue_frame_state_clear(pframe);
	ret = CAM_QUEUE_ENQUEUE(&frame_manager->empty_frame_q, &pframe->list);
	if (ret)
		pr_warn("warning:frame in another queue or check fail\n");
	return ret;
}

int cam_queue_empty_frame_init(void)
{
	int ret = 0;
	struct cam_queue_frame_manager *frame_manager = g_frame_manager;
	int32_t i = 0, j = 0, array_index = 0;
	struct cam_frame *frame_array[CAM_EMP_ARRAY_INIT_LEN] = {0};

	if (!frame_manager) {
		frame_manager = cam_buf_kernel_sys_vzalloc(sizeof(struct cam_queue_frame_manager));
		if (IS_ERR_OR_NULL(frame_manager)) {
			g_frame_manager = NULL;
			pr_err("fail to create cam buf manager\n");
			return -1;
		}
		frame_manager->array_num = 0;
		CAM_QUEUE_INIT(&frame_manager->empty_frame_q, CAM_EMP_ARRAY_LEN_MAX * CAM_EMP_ARRAT_LEN_PER(struct cam_frame), NULL);
		CAM_QUEUE_INIT(&frame_manager->delay_put_q,
			CAM_EMP_ARRAY_LEN_MAX * CAM_EMP_ARRAT_LEN_PER(struct cam_frame),
			NULL);

		snprintf(frame_manager->delay_put_thread.thread_name, ARRAY_SIZE(frame_manager->delay_put_thread.thread_name), "delay_put_thread");
		ret = camthread_create(frame_manager, &frame_manager->delay_put_thread,
			cam_queue_frame_put_thread);
		if (unlikely(ret != 0)) {
			pr_err("fail to create frame put thread\n");
			goto proc_thrd_err;
		}
		spin_lock_init(&frame_manager->frame_lock);
		g_frame_manager = frame_manager;
	}

	for (i = 0; i < CAM_EMP_ARRAY_INIT_LEN; ++i)
		frame_array[i] = cam_buf_kernel_sys_kzalloc(sizeof(struct cam_frame) * CAM_EMP_ARRAT_LEN_PER(struct cam_frame), GFP_KERNEL);

	spin_lock(&frame_manager->frame_lock);

	array_index = frame_manager->array_num;
	for (i = 0; i < CAM_EMP_ARRAY_INIT_LEN; ++i) {
		if (frame_array[i]) {
			frame_manager->frame_array[array_index] = frame_array[i];
			for (j = 0; j < CAM_EMP_ARRAT_LEN_PER(struct cam_frame); ++j)
				CAM_QUEUE_ENQUEUE(&frame_manager->empty_frame_q, &frame_manager->frame_array[array_index][j].list);
			array_index++;
		}
	}
	atomic_add((array_index - frame_manager->array_num) * CAM_EMP_ARRAT_LEN_PER(struct cam_frame), &g_mem_dbg->empty_frm_cnt);
	frame_manager->array_num = array_index;
	pr_info("frame array init,array:%d\n", frame_manager->array_num);
	spin_unlock(&frame_manager->frame_lock);
	return 0;

proc_thrd_err:
	cam_buf_kernel_sys_vfree(frame_manager);
	frame_manager = NULL;
	g_frame_manager = NULL;
	return ret;
}

int cam_queue_empty_frame_deinit(void)
{
	struct cam_queue_frame_manager *frame_manager = g_frame_manager;
	int32_t i = 0;
	uint32_t timeout = 1000;

	if (frame_manager == NULL) {
		pr_err("fail to get frame manager.\n");
		return -EFAULT;
	}

	while (CAM_QUEUE_CNT_GET(&frame_manager->delay_put_q) != 0 && timeout != 0) {
		os_adapt_time_udelay(500);
		timeout--;
	}
	camthread_stop(&frame_manager->delay_put_thread);
	spin_lock(&frame_manager->frame_lock);
	cam_queue_frame_array_check(CAM_FRAME_CHECK_ALL);
	CAM_QUEUE_CLEAN(&frame_manager->empty_frame_q, struct cam_frame, list);
	while (i < CAM_EMP_ARRAY_LEN_MAX && frame_manager->frame_array[i]) {
		cam_buf_kernel_sys_kfree(frame_manager->frame_array[i]);
		frame_manager->frame_array[i] = NULL;
		i++;
	}
	if (i != frame_manager->array_num)
		pr_err("fail to release all frame, i:%d, frame_manager->array_num:%d\n", i, frame_manager->array_num);
	atomic_sub(i * CAM_EMP_ARRAT_LEN_PER(struct cam_frame), &g_mem_dbg->empty_frm_cnt);
	frame_manager->array_num = 0;
	spin_unlock(&frame_manager->frame_lock);
	cam_buf_kernel_sys_vfree(frame_manager);
	frame_manager = NULL;
	g_frame_manager = NULL;
	pr_info("frame manager deinit\n");

	return 0;
}

int cam_queue_recycle_blk_param(struct camera_queue *q, struct cam_frame *param_pframe)
{
	int ret = 0;

	if (!q || !param_pframe) {
		pr_err("fail to get valid handle %p %p\n", q, param_pframe);
		return -1;
	}

	ret = CAM_QUEUE_ENQUEUE(q, &param_pframe->list);
	if(ret)
		pr_err("fail to recycle param node %p, cnt %d\n", param_pframe, q->cnt);
	return ret;
}

struct cam_frame * cam_queue_empty_blk_param_get(struct camera_queue *q)
{
	struct cam_frame *param_frame = NULL;

	if (!q) {
		pr_err("fail to get valid handle\n");
		return NULL;
	}

	param_frame = CAM_QUEUE_DEQUEUE(q, struct cam_frame, list);

	return param_frame;
}
