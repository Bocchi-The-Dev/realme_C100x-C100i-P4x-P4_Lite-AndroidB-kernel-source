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

#ifndef _CAM_BUF_MANAGER_H_
#define _CAM_BUF_MANAGER_H_

#include "cam_queue.h"

#define CAM_COUNT_MAX         4
#define PIPELINE_CNT_MAX      10
#define NODE_CNT_MAX          5
#define PORT_CNT_MAX          20
#define POOL_TAG_MAX          10
#define PRIVATE_POOL_NUM_MAX  ((PIPELINE_CNT_MAX * NODE_CNT_MAX * PORT_CNT_MAX) + POOL_TAG_MAX)
#define RESERVE_BUF_Q_LEN     500
#define CAM_RESERVE_BUF_Q_LEN 50
#define PYR_DEC_REC_BUF_Q_LEN 1

struct cam_buf_pool_id {
	uint32_t tag_id;
	uint32_t private_pool_id;
};

enum cam_buf_pool_status_type {
	CAM_BUF_POOL_STATUS_LENTH,
	CAM_BUF_POOL_STATUS_CNT,
	CAM_BUF_POOL_STATUS_MINFID,
	CAM_BUF_POOL_STATUS_NUM,
};

enum cam_buf_pool_tag_id {
	CAM_BUF_POOL_SHARE_FULL_PATH = 1,
	CAM_BUF_POOL_ABNORAM_RECYCLE,
	CAM_BUF_POOL_SHARE_DEC_BUF,
	CAM_BUF_POOL_SHARE_REC_BUF,
	CAM_BUF_POOL_TAG_ID_NUM,
};

enum cam_queue_ops_cmd {
	CAM_QUEUE_DEL_TAIL = 1,
	CAM_QUEUE_DEQ_PEEK,
	CAM_QUEUE_TAIL_PEEK,
	CAM_QUEUE_TAIL,
	CAM_QUEUE_IF,
	CAM_QUEUE_FRONT,
};

enum cambufmanager_status_ops_cmd {
	CAM_BUF_STATUS_MOVE_TO_ALLOC = 1,
	CAM_BUF_STATUS_MOVE_TO_ION,
	CAM_BUF_STATUS_GET_IOVA,
	CAM_BUF_STATUS_GET_SINGLE_PAGE_IOVA,
	CAM_BUF_STATUS_GET_K_ADDR,
	CAM_BUF_STATUS_PUT_IOVA,
	CAM_BUF_STATUS_PUT_K_ADDR,
	CAM_BUF_STATUS_GET_IOVA_K_ADDR,
	CAM_BUF_STATUS_PUT_IOVA_K_ADDR,
	CAM_BUF_STATUS_OPS_CMD_NUM,
};

enum cambufmanager_status_cmd {
	CAM_BUF_STATUS_ALLOC_2_ION = 1,
	CAM_BUF_STATUS_ALLOC_2_IOVA,
	CAM_BUF_STATUS_ALLOC_2_SINGLE_PAGE_MAP,
	CAM_BUF_STATUS_ALLOC_2_KMAP,
	CAM_BUF_STATUS_ALLOC_2_IOVA_K,

	CAM_BUF_STATUS_ION_2_ALLOC,
	CAM_BUF_STATUS_ION_2_IOVA,
	CAM_BUF_STATUS_ION_2_SINGLE_PAGE_MAP,
	CAM_BUF_STATUS_ION_2_KMAP,
	CAM_BUF_STATUS_ION_2_IOVA_K,

	CAM_BUF_STATUS_IOVA_2_ALLOC,
	CAM_BUF_STATUS_IOVA_2_ION,
	CAM_BUF_STATUS_IOVA_2_KMAP,
	CAM_BUF_STATUS_IOVA_2_IOVA_K,

	CAM_BUF_STATUS_SINGLE_PAGE_MAP_2_ALLOC,
	CAM_BUF_STATUS_SINGLE_PAGE_MAP_2_ION,

	CAM_BUF_STATUS_KMAP_2_ALLOC,
	CAM_BUF_STATUS_KMAP_2_ION,
	CAM_BUF_STATUS_KMAP_2_IOVA,
	CAM_BUF_STATUS_KMAP_2_IOVA_K,

	CAM_BUF_STATUS_K_IOVA_2_ALLOC,
	CAM_BUF_STATUS_K_IOVA_2_ION,
	CAM_BUF_STATUS_K_IOVA_2_IOVA,
	CAM_BUF_STATUS_K_IOVA_2_K,

	CAM_BUF_STATUS_SWITCH_NUM,
};

struct camera_buf_get_desc {
	uint32_t target_fid;
	enum cambufmanager_status_ops_cmd buf_ops_cmd;
	enum cam_queue_ops_cmd q_ops_cmd;
	enum cam_buf_iommudev_type mmu_type;
	enum cam_en_status (*filter)(struct cam_frame *, void *);
};

struct cam_buf_manager {
	atomic_t user_cnt;
	struct camera_queue *private_buf_pool[PRIVATE_POOL_NUM_MAX];
	struct camera_queue *tag_pool[CAM_BUF_POOL_TAG_ID_NUM];
	struct mutex pool_lock;
};

#define CAM_BUF_GET_IOVA_METHOD(pframe)  \
	(pframe->common.user_fid == CAMERA_RESERVE_FRAME_NUM) ?  \
		CAM_BUF_STATUS_GET_SINGLE_PAGE_IOVA : \
		CAM_BUF_STATUS_GET_IOVA; \

int cam_buf_manager_buf_status_cfg(struct camera_buf *buf, enum cambufmanager_status_ops_cmd cmd, enum cam_buf_iommudev_type type);
struct cam_frame *cam_buf_manager_buf_dequeue(struct cam_buf_pool_id *pool_id, struct camera_buf_get_desc *buf_desc, void *buf_manager_handle);
int cam_buf_manager_buf_enqueue(struct cam_buf_pool_id *pool_id, struct cam_frame *pframe, struct camera_buf_get_desc *buf_desc, void *buf_manager_handle);
void cam_buf_manager_buf_clear(struct cam_buf_pool_id *pool_id, void *buf_manager_handle);
int cam_buf_manager_pool_reg(struct cam_buf_pool_id *pool_id, uint32_t length, void *buf_manager_handle);
int cam_buf_manager_pool_unreg(struct cam_buf_pool_id *pool_id, void *buf_manager_handle);
int cam_buf_manager_pool_cnt(struct cam_buf_pool_id *pool_id, void *buf_manager_handle);
int cam_buf_manager_init(uint32_t cam_id, void *buf_manager_handle);
int cam_buf_manager_deinit(uint32_t cam_id, void *buf_manager_handle);

#endif
