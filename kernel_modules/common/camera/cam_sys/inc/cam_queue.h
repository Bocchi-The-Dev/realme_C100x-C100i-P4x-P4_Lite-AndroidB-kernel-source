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

#ifndef _CAM_QUEUE_H_
#define _CAM_QUEUE_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "cam_block.h"
#include "cam_buf.h"
#include "cam_types.h"
#include "dcam_interface.h"
#include "isp_interface.h"

#define CAM_ZOOM_EMP_Q_LEN_MAX          128
#define CAM_EMP_Q_LEN_INC               16
#define CAM_EMP_Q_LEN_MAX               3072
#define CAM_INT_EMP_Q_LEN_INC           48
#define CAM_INT_EMP_Q_LEN_MAX           256
#define NODE_ZOOM_CNT_MAX               8
#define PORT_ZOOM_CNT_MAX               5
#define CAM_EMP_ARRAY_LEN_MAX           1200
#define CAM_EMP_ARRAY_INIT_LEN          120
#define CAM_EMP_ARRAT_LEN_PER(type)     (4096 * 4 / sizeof(type))
enum {
	CAM_Q_INIT = 1,
	CAM_Q_EMPTY,
	CAM_Q_FULL,
	CAM_Q_CLEAR
};

enum cam_pipeline_type {
	CAM_PIPELINE_PREVIEW,
	CAM_PIPELINE_VIDEO,
	CAM_PIPELINE_CAPTURE,
	CAM_PIPELINE_ZSL_CAPTURE,
	CAM_PIPELINE_SENSOR_RAW,
	CAM_PIPELINE_SCALER_YUV,
	CAM_PIPELINE_OFFLINE_RAW2YUV,
	CAM_PIPELINE_ONLINERAW_2_OFFLINEYUV,
	CAM_PIPELINE_ONLINERAW_2_USER_2_OFFLINEYUV,
	CAM_PIPELINE_ONLINERAW_2_COPY_2_USER_2_OFFLINEYUV,
	CAM_PIPELINE_ONLINEBPCRAW_2_USER_2_OFFLINEYUV,
	CAM_PIPELINE_ONLINERAW_2_USER_2_BPCRAW_2_USER_2_OFFLINEYUV,
	CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_RAW2USER2YUV,
	CAM_PIPELINE_ONLINE_NORMALZSLCAPTURE_OR_RAW2USER2YUV,
	CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_RAW2USER2BPC2USER2YUV,
	CAM_PIPELINE_ONLINE_NORMAL2YUV_OR_BPCRAW2USER2YUV,
	CAM_PIPELINE_ONLINE_NORMALZSLCAPTURE_OR_RAW2USER2BPC2USER2YUV,
	CAM_PIPELINE_ONLINE_NORMALZSLCAPTURE_OR_BPCRAW2USER2YUV,
	CAM_PIPELINE_OFFLINE_RAW2FRGB_OFFLINE_FRGB2YUV,
	CAM_PIPELINE_ONLINEYUV_2_USER_2_OFFLINEYUV_2_NR,
	CAM_PIPELINE_ONLINERAW_2_OFFLINEPREVIEW,
	CAM_PIPELINE_TYPE_MAX,
};

enum cam_node_buf_type {
	CAM_NODE_BUF_KERNEL,
	CAM_NODE_BUF_USER,
	CAM_NODE_BUF_TYPE_MAX,
};

enum cam_port_transfer_type {
	PORT_TRANSFER_IN,
	PORT_TRANSFER_OUT,
	PORT_TRANSFER_MAX,
};

/* description of all node types */
enum cam_node_type {
	CAM_NODE_TYPE_DCAM_ONLINE,
	CAM_NODE_TYPE_DCAM_OFFLINE,
	CAM_NODE_TYPE_DCAM_OFFLINE_BPC_RAW,
	CAM_NODE_TYPE_DCAM_OFFLINE_LSC_RAW,
	CAM_NODE_TYPE_DCAM_OFFLINE_RAW2FRGB,
	CAM_NODE_TYPE_DCAM_OFFLINE_FRGB2YUV,
	CAM_NODE_TYPE_ISP_OFFLINE,
	CAM_NODE_TYPE_FRAME_CACHE,
	CAM_NODE_TYPE_ISP_YUV_SCALER,
	CAM_NODE_TYPE_PYR_DEC,
	CAM_NODE_TYPE_PYR_REC,
	CAM_NODE_TYPE_DUMP,
	CAM_NODE_TYPE_DATA_COPY,
	CAM_NODE_TYPE_USER,
	CAM_NODE_TYPE_REPLACE,
	CAM_NODE_TYPE_MAX,
};

/*queue_out check in stream off, all check in camcore_release*/
enum cam_frame_check {
	CAM_FRAME_CHECK_QUEUE_OUT,
	CAM_FRAME_CHECK_ALL,
};

struct cam_zoom_base {
	uint32_t ratio_width;
	uint32_t total_crop_width;
	uint32_t need_post_proc;
	struct img_trim ori_user_crop;
	struct img_size src;
	struct img_trim crop;
	struct img_size dst;
};

struct cam_zoom_port {
	enum cam_port_transfer_type port_type;
	uint32_t port_id;
	struct cam_zoom_base zoom_base;
};

struct cam_zoom_node {
	enum cam_node_type node_type;
	uint32_t node_id;
	struct cam_zoom_port zoom_port[PORT_ZOOM_CNT_MAX];
};

struct cam_zoom_frame {
	struct cam_zoom_node zoom_node[NODE_ZOOM_CNT_MAX];
};

struct blk_param_info {
	uint32_t update;
	struct dcam_isp_k_block *param_block;
};

struct isp_xtm_conflict_info {
	uint32_t need_ltm_hist;
	uint32_t need_ltm_map;
	uint32_t need_gtm_hist;
	uint32_t need_gtm_map;
	uint32_t gtm_mod_en;
};

struct isp_xtm_nonzsl_info {
	uint32_t hist_eb;
	struct img_size full_size;
};

struct camera_zoom_frame {
	struct sprd_img_rect zoom_crop;
	struct sprd_img_rect total_zoom_crop;
};

struct cam_decblk_frame {
	uint32_t fid;
	struct dcam_isp_k_block *decblk_pm;
};

struct cam_ispblk_frame {
	uint32_t fid;
	uint32_t update;
	struct dcam_isp_k_block *param_block;
};

struct cam_port_linkage {
	enum cam_node_type node_type;
	uint32_t node_id;
	uint32_t port_id;
};

struct camera_frame_buf {
	struct camera_buf buf;
};

struct camera_frame {
	struct cam_port_linkage link_from;
	struct cam_port_linkage link_to;
	enum cam_en_status dump_en;
	uint32_t dump_node_id;
	enum cam_en_status replace_en;
	uint32_t replace_node_id;
	enum cam_en_status copy_en;
	uint32_t copy_node_id;
	uint32_t fid;
	uint32_t width;
	uint32_t height;
	uint32_t img_fmt;
	enum cam_format cam_fmt;
	uint32_t evt;
	enum cam_ch_id channel_id;
	uint32_t irq_type;
	uint32_t irq_property;
	enum cam_postproc_mode proc_mode;
	enum cam_reserved_buf_type is_reserved;
	enum cam_en_status is_compressed;
	enum cam_en_status is_3dnr_close;
	enum cam_en_status pyr_status;
	enum dcam_full_src_sel_type raw_src;
	enum cam_en_status cfg_to_alter_q;
	/*use for isp ltm ctrl*/
	struct isp_xtm_conflict_info xtm_conflict;
	uint32_t not_use_isp_reserved_buf;
	uint32_t user_fid;
	uint32_t zoom_ratio;
	uint32_t total_zoom;
	struct dcam_compress_info fbc_info;
	void *priv_data;
	timeval sensor_time;/* time without suspend @SOF */
	ktime_t boot_sensor_time;/* ns from boot @SOF */
	int64_t frame_interval_time;/* dual frame diff @SOF */
	struct blk_param_info blkparam_info;
	struct camera_buf buf;
	struct nr3_me_data nr3_me;
	/*need to delete after scaler buffer opt.*/
	enum isp_stream_state state;
	enum isp_stream_buf_type buf_type;
	void *pframe_data;
	struct cam_zoom_frame zoom_data;
	uint32_t is_flash_status;
	/*statis info*/
	struct sprd_img_aem_info aem_info;
	struct sprd_img_bayerhist_info bayerhist_info;
	struct sprd_img_afm_info afm_info;
	struct sprd_img_pdaf_info pdaf_info;
	struct dcam_dev_lscm_param lscm_info;
	struct img_slice_info slice_info;

	struct sprd_img_ltm_info ltm_info;
	struct isp_xtm_nonzsl_info nonzsl_xtm;
};

enum camera_frame_type {
	CAM_FRAME_GENERAL = 1,
	CAM_FRMAE_USER_ZOOM,
	CAM_FRAME_NODE_ZOOM,
	CAM_FRAME_ISP_BLK,
	CAM_FRAME_DEC_BLK,
	CAM_FRAME_MAX,
};

struct cam_frame {
	struct cam_q_head list;
	enum camera_frame_type type;
	union {
		struct camera_frame common;
		struct camera_zoom_frame user_zoom;
		struct cam_zoom_frame node_zoom;
		struct cam_ispblk_frame isp_blk;
		struct cam_decblk_frame dec_blk;
	};
};

struct camera_queue {
	uint32_t state;
	uint32_t max;
	uint32_t cnt;
	spinlock_t lock;
	uint32_t node_size;
	void *node;
	void *node_head;
	void *node_tail;
	struct list_head head;
	void (*destroy)(void *param);
};

struct cam_queue_frame_manager {
	uint32_t array_num;
	struct cam_frame *frame_array[CAM_EMP_ARRAY_LEN_MAX];
	struct camera_queue empty_frame_q;
	spinlock_t frame_lock;
	struct camera_queue delay_put_q;
	struct cam_thread_info delay_put_thread;
};

#define CAM_QUEUE_LIST_ADD_CHECK(head, _list) ({ \
	struct cam_q_head *pos = NULL; \
	uint32_t ret = 0; \
	list_for_each_entry(pos, head, list) { \
		if (pos == _list) \
			ret = 1; \
	} \
	ret; \
})

#define CAM_QUEUE_LIST_DEL_CHECK(head) ({ \
	struct list_head *entry = (head); \
	struct list_head *prev = NULL; \
	struct list_head *next = NULL; \
	uint32_t ret = 0; \
	prev = entry->prev; \
	next = entry->next; \
	if (prev->next != entry || next->prev != entry) { \
		prev->next = next; \
		next->prev = prev; \
		prev = LIST_POISON2; \
		next = LIST_POISON1; \
	} \
	if (next == NULL || prev == NULL) { \
		prev = LIST_POISON2; \
		next = LIST_POISON1; \
	} \
	if (prev == LIST_POISON2 || next == LIST_POISON1) \
		ret = 1; \
	ret; \
})

#define CAM_QUEUE_LIST_ADD(_list, head, is_tail) ({ \
	struct cam_q_head *__list = (_list); \
	enum cam_en_status __is_tail = (is_tail); \
	int ret = -1; \
	if (atomic_cmpxchg(&__list->status, CAM_Q_FREE, CAM_Q_USED) == CAM_Q_FREE) { \
		if (__is_tail) \
			list_add_tail(&__list->list, head); \
		else \
			list_add(&__list->list, head); \
		ret = 0; \
	} else \
		pr_err("fail to get list status:%d, cb:%pS\n", (int)(atomic_read(&__list->status)), __builtin_return_address(0));\
	ret; \
})

#define CAM_QUEUE_LIST_DEL(_list) ({ \
	struct cam_q_head *__list = (_list); \
	uint32_t is_check = 0; \
	uint32_t ret = -1; \
	if (atomic_cmpxchg(&__list->status, CAM_Q_USED, CAM_Q_FREE) == CAM_Q_USED) { \
		is_check = CAM_QUEUE_LIST_DEL_CHECK(&__list->list); \
		if (!is_check) { \
			list_del(&__list->list);\
			ret = 0; \
		} else \
			pr_err("fail to list del valid, addr: %p\n", __list); \
	} else \
		pr_err("fail to get list status:%d, cb:%pS\n", \
			(int)(atomic_read(&__list->status)), __builtin_return_address(0)); \
	ret; \
})

#define CAM_QUEUE_INIT(queue, queue_max, data_cb_func) ( { \
	struct camera_queue *__q = (queue); \
	if (__q != NULL) { \
		__q->cnt = 0; \
		__q->max = (queue_max); \
		__q->state = CAM_Q_INIT; \
		__q->destroy = (data_cb_func); \
		spin_lock_init(&__q->lock); \
		INIT_LIST_HEAD(&__q->head); \
	} \
})

#define CAM_QUEUE_DEINIT(queue, type, member) ({ \
	unsigned long __flags = 0; \
	struct camera_queue *__q = (queue); \
	type *__node = NULL; \
	struct cam_q_head *_list = NULL; \
	if ((!__q->state)  || (__q == NULL)) { \
		pr_warn("warning: wrong state for queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		do { \
			if ((list_empty(&__q->head)) || (__q->cnt == 0)) \
				break; \
			_list = list_first_entry(&__q->head, struct cam_q_head, list); \
			if (_list == NULL) \
				break; \
			CAM_QUEUE_LIST_DEL(_list); \
			__node = container_of(_list, type, member); \
			__q->cnt--; \
			if (__q->destroy) { \
				spin_unlock_irqrestore(&__q->lock, __flags); \
				__q->destroy(__node); \
				spin_lock_irqsave(&__q->lock, __flags); \
			} \
		} while (1); \
		__q->cnt = 0; \
		__q->max = 0; \
		__q->state = CAM_Q_CLEAR; \
		__q->destroy = NULL; \
		INIT_LIST_HEAD(&__q->head); \
		if (__q->node_head) \
			cam_buf_kernel_sys_vfree(__q->node_head); \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
})

#define CAM_QUEUE_CNT_GET(queue) ( { \
	unsigned long __flags = 0 ; \
	uint32_t tmp = 0; \
	struct camera_queue *__q = (queue); \
	if ((!__q->state)  || (__q == NULL)) { \
		pr_warn("warning: wrong state for queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		tmp = __q->cnt; \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
	tmp; \
})

#define CAM_QUEUE_ENQUEUE(queue, list) ( { \
	int ret = -1; \
	unsigned long __flags = 0; \
	struct camera_queue *__q = (queue); \
	struct cam_q_head *_list = (list); \
	if ((!__q->state)  || (__q == NULL) || ((_list) == NULL)) { \
		pr_warn("warning: wrong state for queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		if ((__q->state != CAM_Q_CLEAR) && (__q->cnt < __q->max)) { \
			ret = CAM_QUEUE_LIST_ADD(_list, &__q->head, true); \
			if (!ret) \
				__q->cnt++; \
		} \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
	ret; \
})

#define CAM_QUEUE_ENQUEUE_HEAD(queue, list) ( { \
	int ret = -1; \
	unsigned long __flags = 0; \
	struct camera_queue *__q = (queue); \
	struct cam_q_head *_list = (list); \
	if ((!__q->state)  || (__q == NULL) || ((_list) == NULL)) { \
		pr_warn("warning: wrong state for queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		if ((__q->state != CAM_Q_CLEAR) && (__q->cnt < __q->max)) { \
			ret = CAM_QUEUE_LIST_ADD(_list, &__q->head, false); \
			if (!ret) \
				__q->cnt++; \
		} \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
	ret; \
})

#define CAM_QUEUE_DEQUEUE(queue, type, member) ({ \
	unsigned long __flags = 0; \
	struct camera_queue *__q = (queue); \
	type *__node = NULL; \
	struct cam_q_head *_list = NULL; \
	if ((!__q->state)  || (__q == NULL)) { \
		pr_warn("warning: wrong state for queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		if ((!list_empty(&__q->head)) && (__q->cnt) \
			&& (__q->state != CAM_Q_CLEAR)) { \
			_list = list_first_entry(&__q->head, struct cam_q_head, list); \
			if (_list) { \
				CAM_QUEUE_LIST_DEL(_list); \
				__node = container_of(_list, type, member); \
				__q->cnt--; \
			} \
		} \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
	__node; \
})

#define CAM_QUEUE_DEQUEUE_TAIL(queue, type, member) ({ \
	unsigned long __flags = 0; \
	struct camera_queue *__q = (queue); \
	type *__node = NULL; \
	struct cam_q_head *_list = NULL; \
	if ((!__q->state)  || (__q == NULL)) { \
		pr_warn("warning: wrong state for queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		if ((!list_empty(&__q->head)) && (__q->cnt) \
			&& (__q->state != CAM_Q_CLEAR)) { \
			_list = list_last_entry(&__q->head, struct cam_q_head, list); \
			if (_list) { \
				CAM_QUEUE_LIST_DEL(_list); \
				__node = container_of(_list, type, member); \
				__q->cnt--; \
			} \
		} \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
	__node; \
})

#define CAM_QUEUE_DEQUEUE_PEEK(queue, type, member) ({ \
	unsigned long __flags = 0; \
	struct camera_queue *__q = (queue); \
	type *__node = NULL; \
	struct cam_q_head *_list = NULL; \
	if ((!__q->state)  || (__q == NULL)) { \
		pr_warn("warning: wrong state for queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		if ((!list_empty(&__q->head)) && (__q->cnt) \
			&& (__q->state != CAM_Q_CLEAR)) { \
			_list = list_first_entry(&__q->head, struct cam_q_head, list); \
			if (_list) { \
				__node = container_of(_list, type, member); \
			} \
		} \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
	__node; \
})

#define CAM_QUEUE_CLEAN(queue, type, member) ({ \
	unsigned long __flags = 0; \
	struct camera_queue *__q = (queue); \
	type *__node = NULL; \
	struct cam_q_head *_list = NULL; \
	if ((!__q->state)  || (__q == NULL)) { \
		pr_warn("warning: wrong state for queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		do { \
			if ((list_empty(&__q->head)) || (__q->cnt == 0)) \
				break; \
			_list = list_first_entry(&__q->head, struct cam_q_head, list); \
			if (_list == NULL) \
				break; \
			CAM_QUEUE_LIST_DEL(_list); \
			__q->cnt--; \
			__node = container_of(_list, type, member); \
			if (__q->destroy) { \
				spin_unlock_irqrestore(&__q->lock, __flags); \
				__q->destroy(__node); \
				spin_lock_irqsave(&__q->lock, __flags); \
			} \
		} while (1); \
		__q->cnt = 0; \
		__q->max = 0; \
		__q->state = CAM_Q_CLEAR; \
		__q->destroy = NULL; \
		INIT_LIST_HEAD(&__q->head); \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
})

#define CAM_QUEUE_TAIL_PEEK(queue, type, member) ({ \
	unsigned long __flags; \
	struct camera_queue *__q = (queue); \
	struct cam_q_head *_list = NULL; \
	type *__node = NULL; \
	if ((!__q->state)  || (__q == NULL)) { \
		pr_warn("warning: no need to clear queue\n"); \
	} else { \
		spin_lock_irqsave(&__q->lock, __flags); \
		if ((!list_empty(&__q->head)) && (__q->cnt) \
			&& (__q->state != CAM_Q_CLEAR)) { \
			_list = list_last_entry(&__q->head, struct cam_q_head, list); \
			if (_list) { \
				__node = container_of(_list, type, member); \
			} \
		} \
		spin_unlock_irqrestore(&__q->lock, __flags); \
	} \
	__node; \
})

#define CAM_QUEUE_FRAME_FLAG_RESET(frame) ({ \
	struct camera_frame *__p = (frame); \
	if (__p != NULL) { \
		__p->xtm_conflict.need_gtm_hist = 1; \
		__p->xtm_conflict.need_gtm_map = 1; \
		__p->xtm_conflict.gtm_mod_en = 1; \
		__p->xtm_conflict.need_ltm_hist = 1; \
		__p->xtm_conflict.need_ltm_map = 1; \
		__p->not_use_isp_reserved_buf = 0; \
	} \
})

/*list_for_each_entry*/
#define CAM_QUEUE_FOR_EACH_ENTRY(param, head, node) \
	for (param = container_of(list_first_entry(head, struct cam_q_head, list), typeof(*param), node); \
		!list_entry_is_head((&param->node), head, list); \
		param = container_of(list_next_entry(&param->node, list), typeof(*param), node))

/*list_for_each_entry_safe*/
#define CAM_QUEUE_FOR_EACH_ENTRY_SAFE(param, param_next, head, node) \
	for (param = container_of(list_first_entry(head, struct cam_q_head, list), typeof(*param), node), \
		param_next = container_of(list_next_entry(&param->node, list), typeof(*param), node); \
		!list_entry_is_head((&param->node), head, list); \
		param = param_next, \
		param_next = container_of(list_next_entry(&param_next->node, list), typeof(*param), node))

struct cam_frame *cam_queue_dequeue_if(struct camera_queue *q, enum cam_en_status (*filter)(struct cam_frame *, void *), void *data);
int cam_queue_same_frame_get(struct camera_queue *q0, struct cam_frame **pf0, int64_t t_sec, int64_t t_usec);
int cam_queue_frame_array_check(uint32_t mode);
struct cam_frame * cam_queue_empty_blk_param_get(struct camera_queue *q);
void cam_queue_empty_frame_put(void *pframe);
int cam_queue_empty_frame_init(void);
int cam_queue_empty_frame_deinit(void);
int cam_queue_recycle_blk_param(struct camera_queue *q, struct cam_frame *param_pframe);
struct cam_frame *cam_queue_empty_frame_get(enum camera_frame_type type);

#endif/* _CAM_QUEUE_H_ */
