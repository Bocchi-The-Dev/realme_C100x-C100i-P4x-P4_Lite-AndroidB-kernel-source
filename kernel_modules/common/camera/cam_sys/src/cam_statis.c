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

#define __NO_FORTIFY
#include <linux/slab.h>
#include "cam_statis.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_STATIS: %d %d %s : " fmt, current->pid, __LINE__, __func__

struct statis_port_buf_info s_statis_port_info_all[] = {
	{PORT_PDAF_OUT,       STATIS_PDAF,    0,  0},
	{PORT_AEM_OUT,        STATIS_AEM,     0,  0},
	{PORT_AFM_OUT,        STATIS_AFM,     0,  0},
	{PORT_AFL_OUT,        STATIS_AFL,     0,  0},
	{PORT_BAYER_HIST_OUT, STATIS_HIST,    0,  0},
	{PORT_FRGB_HIST_OUT,  STATIS_HIST2,   0,  0},
	{PORT_LSCM_OUT,       STATIS_LSCM,    0,  0},
	{PORT_GTM_HIST_OUT,   STATIS_GTMHIST, 0,  0},
};

static enum cam_port_dcam_online_out_id camstatis_dcam_type_to_port_id(enum isp_statis_buf_type type)
{
	switch (type) {
	case STATIS_AEM:
		return PORT_AEM_OUT;
	case STATIS_AFM:
		return PORT_AFM_OUT;
	case STATIS_AFL:
		return PORT_AFL_OUT;
	case STATIS_HIST:
		return PORT_BAYER_HIST_OUT;
	case STATIS_HIST2:
		return PORT_FRGB_HIST_OUT;
	case STATIS_PDAF:
		return PORT_PDAF_OUT;
	case STATIS_EBD:
		return PORT_VCH2_OUT;
	case STATIS_LSCM:
		return PORT_LSCM_OUT;
	case STATIS_GTMHIST:
		return PORT_GTM_HIST_OUT;
	default:
		return PORT_DCAM_OUT_MAX;
	}
}

static int camstatis_isp_port_buffer_init(void *isp_handle, void *node,
		struct isp_statis_buf_input *input)
{
	int ret = 0, j = 0;
	int32_t mfd = 0;
	enum isp_statis_buf_type stats_type;
	struct isp_pipe_dev *dev = NULL;
	struct isp_node *inode = NULL;
	struct camera_buf *ion_buf = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_buf_pool_id *statis_q = NULL;

	dev = (struct isp_pipe_dev *)isp_handle;
	inode = (struct isp_node *)node;

	memset(&inode->statis_buf_array[0][0], 0, sizeof(inode->statis_buf_array));

	for (stats_type = STATIS_HIST2; stats_type <= STATIS_LTMHIST; stats_type++) {
		if ((stats_type == STATIS_GTMHIST) && (inode->dev->isp_hw->ip_isp->isphw_abt->rgb_gtm_support == 0))
			continue;
		if (stats_type == STATIS_HIST2)
			statis_q = &inode->hist2_pool;
		else if (stats_type == STATIS_GTMHIST)
			statis_q = &inode->gtmhist_outpool;
		else if (stats_type == STATIS_LTMHIST)
			statis_q = &inode->ltmhist_outpool;
		else
			continue;
		for (j = 0; j < STATIS_BUF_NUM_MAX; j++) {
			mfd = input->mfd_array[stats_type][j];
			if (mfd <= 0)
				continue;
			ion_buf = &inode->statis_buf_array[stats_type][j];
			ion_buf->mfd = mfd;
			ion_buf->offset[0] = input->offset_array[stats_type][j];
			ion_buf->type = CAM_BUF_USER;
			ion_buf->status = CAM_BUF_ALLOC;
			ret = cam_buf_manager_buf_status_cfg(ion_buf, CAM_BUF_STATUS_GET_IOVA_K_ADDR, CAM_BUF_IOMMUDEV_ISP);
			if (ret)
				continue;
			pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
			if (!pframe) {
				pr_err("fail to get pframe.\n");
				ret = -EINVAL;
				break;
			}
			pframe->common.channel_id = inode->ch_id;
			pframe->common.irq_property = stats_type;
			pframe->common.buf = *ion_buf;
			pframe->common.buf.type = CAM_BUF_NONE;
			ret = cam_buf_manager_buf_enqueue(statis_q, pframe, NULL, inode->buf_manager_handle);
			if (ret) {
				pr_info("statis %d overflow\n", stats_type);
				cam_queue_empty_frame_put(pframe);
			}

			pr_debug("statis type:%d, buf_num %d, buf %d, off %ld, kaddr 0x%lx iova 0x%08x\n",
				stats_type, j, ion_buf->mfd, ion_buf->offset[0],
				ion_buf->addr_k, (uint32_t)ion_buf->iova[CAM_BUF_IOMMUDEV_ISP]);
		}
	}

	return ret;
}

int cam_statis_isp_port_buffer_deinit(void *isp_handle, void *node)
{
	int j = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_node *inode = NULL;
	struct camera_buf *ion_buf = NULL;
	enum isp_statis_buf_type stats_type = {0};

	dev = (struct isp_pipe_dev *)isp_handle;
	inode = VOID_PTR_TO(node, struct isp_node);

	pr_debug("enter\n");

	for (stats_type = STATIS_HIST2; stats_type <= STATIS_LTMHIST; stats_type++) {
		if ((stats_type == STATIS_GTMHIST) && (inode->dev->isp_hw->ip_isp->isphw_abt->rgb_gtm_support == 0))
			continue;
		for (j = 0; j < STATIS_BUF_NUM_MAX; j++) {
			ion_buf = &inode->statis_buf_array[stats_type][j];
			if (ion_buf->mfd <= 0) {
				memset(ion_buf, 0, sizeof(struct camera_buf));
				continue;
			}

			pr_debug("ctx %d free buf %d, off %ld, kaddr 0x%lx iova 0x%08x\n",
				inode->node_id, ion_buf->mfd, ion_buf->offset[0],
				ion_buf->addr_k, (uint32_t)ion_buf->iova[CAM_BUF_IOMMUDEV_ISP]);

			cam_buf_manager_buf_status_cfg(ion_buf, CAM_BUF_STATUS_MOVE_TO_ALLOC, CAM_BUF_IOMMUDEV_ISP);
			memset(ion_buf, 0, sizeof(struct camera_buf));
		}
	}
	pr_debug("done.\n");
	return 0;
}

int cam_statis_isp_port_buffer_cfg(void *isp_handle, void *node,
		struct isp_statis_buf_input *input)
{
	int ret = 0, j = 0;
	int32_t mfd = 0;
	uint32_t offset = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_node *inode = NULL;
	struct camera_buf *ion_buf = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_buf_pool_id *statis_q = NULL;

	if (!isp_handle || !node || !input) {
		pr_info("isp_handle=%p, cxt_id=%p\n", isp_handle, node);
		return 0;
	}

	dev = (struct isp_pipe_dev *)isp_handle;
	inode = VOID_PTR_TO(node, struct isp_node);

	if (atomic_read(&inode->user_cnt) == 0) {
		pr_info("isp ctx %d is not enable\n", inode->node_id);
		return 0;
	}

	if (input->type == STATIS_INIT) {
		camstatis_isp_port_buffer_init(isp_handle, inode, input);
		pr_debug("init done\n");
		return 0;
	}
	if ((input->type == STATIS_GTMHIST) && (inode->dev->isp_hw->ip_isp->isphw_abt->rgb_gtm_support == 0))
		return 0;

	if (input->type == STATIS_HIST2)
		statis_q = &inode->hist2_pool;
	else if (input->type == STATIS_GTMHIST)
		statis_q = &inode->gtmhist_outpool;
	else if (input->type == STATIS_LTMHIST)
		statis_q = &inode->ltmhist_outpool;
	else {
		pr_warn("statis type %d not support\n", input->type);
		return 0;
	}

	for (j = 0; j < STATIS_BUF_NUM_MAX; j++) {
		mfd = inode->statis_buf_array[input->type][j].mfd;
		offset = inode->statis_buf_array[input->type][j].offset[0];
		if ((mfd > 0) && (mfd == input->mfd)
			&& (offset == input->offset)) {
			ion_buf = &inode->statis_buf_array[input->type][j];
			break;
		}
	}

	if (ion_buf == NULL) {
		pr_err("fail to get statis buf %d, type %d\n",
				input->type, input->mfd);
		ret = -EINVAL;
		return ret;
	}

	pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	if (!pframe) {
		pr_err("fail to get frame\n");
		ret = -EINVAL;
		return ret;
	}
	pframe->common.channel_id = inode->ch_id;
	pframe->common.irq_property = input->type;
	pframe->common.buf = *ion_buf;
	pframe->common.buf.type = CAM_BUF_NONE;
	ret = cam_buf_manager_buf_enqueue(statis_q, pframe, NULL, inode->buf_manager_handle);
	if (ret) {
		pr_info("statis %d overflow\n", input->type);
		cam_queue_empty_frame_put(pframe);
	}
	pr_debug("buf %d, off %ld, kaddr 0x%lx iova 0x%08x\n",
		ion_buf->mfd, ion_buf->offset[0],
		ion_buf->addr_k, (uint32_t)ion_buf->iova[CAM_BUF_IOMMUDEV_ISP]);
	return ret;
}

int camstatis_dcam_port_buffer_init(
	struct dcam_online_node *dcam_node,
	struct dcam_statis_param *statis_param)
{
	int ret = 0, i = 0, j = 0;
	int32_t mfd = 0;
	uint32_t res_buf_size = 0;
	enum cam_port_dcam_online_out_id port_id = PORT_DCAM_OUT_MAX;
	enum isp_statis_buf_type stats_type = STATIS_INIT;
	struct camera_buf *ion_buf = NULL;
	struct cam_frame *pframe = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct isp_statis_buf_input *input = NULL;

	pr_debug("enter\n");
	input = (struct isp_statis_buf_input *)statis_param->param;
	res_buf_size = *statis_param->buf_size;

	memset(&dcam_node->statis_buf_array[0][0], 0, sizeof(dcam_node->statis_buf_array));
	for (i = 0; i < ARRAY_SIZE(s_statis_port_info_all); i++) {
		port_id = s_statis_port_info_all[i].port_id;
		stats_type = s_statis_port_info_all[i].buf_type;
		dcam_port = dcam_online_node_port_get(dcam_node, port_id);
		if (dcam_node->alg_type == ALG_TYPE_VID_NR && stats_type == STATIS_GTMHIST)
			continue;

		if (!dcam_port || stats_type == STATIS_INIT)
			continue;

		if (port_id == PORT_VCH2_OUT && dcam_port->raw_src != ORI_RAW_SRC_SEL)
			continue;
		if (port_id == PORT_GTM_HIST_OUT && dcam_node->dev->hw->ip_isp->isphw_abt->rgb_gtm_support)
			continue;

		for (j = 0; j < STATIS_BUF_NUM_MAX; j++) {
			mfd = input->mfd_array[stats_type][j];

			pr_debug("i %d, type %d, mfd %d, offset %d\n",
				i, stats_type, mfd, input->offset_array[stats_type][j]);

			if (mfd <= 0)
				continue;
			ion_buf = &dcam_node->statis_buf_array[stats_type][j];
			ion_buf->mfd = mfd;
			ion_buf->offset[0] = input->offset_array[stats_type][j];
			ion_buf->type = CAM_BUF_USER;
			if (mfd > 0)
				ion_buf->status = CAM_BUF_ALLOC;

			if (stats_type == STATIS_GTMHIST)
				ret = cam_buf_manager_buf_status_cfg(ion_buf, CAM_BUF_STATUS_GET_IOVA_K_ADDR, CAM_BUF_IOMMUDEV_DCAM);
			else
				ret = cam_buf_manager_buf_status_cfg(ion_buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_DCAM);
			if (ret)
				continue;

			pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
			if (!pframe) {
				pr_err("fail to get frame\n");
				ret = -EINVAL;
				break;
			}
			pframe->common.channel_id = 0;
			pframe->common.irq_property = stats_type;
			pframe->common.buf = *ion_buf;
			pframe->common.buf.type = CAM_BUF_NONE;

			ret = cam_buf_manager_buf_enqueue(&dcam_port->unprocess_pool, pframe, NULL, dcam_node->buf_manager_handle);
			if (ret) {
				pr_err("fail to enqueue, statis %d overflow\n", stats_type);
				cam_queue_empty_frame_put(pframe);
			}

			pr_debug("statis %d buf mfd %d offset %ld kaddr 0x%lx iova 0x%08x, size %zu\n",
				stats_type, ion_buf->mfd, ion_buf->offset[0], ion_buf->addr_k,
				(uint32_t)ion_buf->iova[CAM_BUF_IOMMUDEV_DCAM], ion_buf->size);
			if (ion_buf->size > res_buf_size)
				res_buf_size = (uint32_t)ion_buf->size;
		}
	}
	*statis_param->buf_size = res_buf_size;

	pr_info("init done.\n");
	return ret;
}

int cam_statis_dcam_port_buffer_deinit(void *dcam_handle)
{
	int i = 0, j = 0;
	int32_t mfd = 0;
	enum isp_statis_buf_type stats_type = STATIS_INIT;
	struct camera_buf *ion_buf = NULL;
	struct dcam_online_node *dcam_node = NULL;

	dcam_node = (struct dcam_online_node *)dcam_handle;
	for (i = 0; i < ARRAY_SIZE(s_statis_port_info_all); i++) {
		stats_type = s_statis_port_info_all[i].buf_type;
		if (stats_type == STATIS_INIT)
			continue;

		for (j = 0; j < STATIS_BUF_NUM_MAX; j++) {
			ion_buf = &dcam_node->statis_buf_array[stats_type][j];
			mfd = ion_buf->mfd;
			if (mfd <= 0)
				continue;

			pr_debug("stats %d,  j %d,  mfd %d, offset %ld\n",
				stats_type, j, mfd, ion_buf->offset[0]);
			cam_buf_manager_buf_status_cfg(ion_buf, CAM_BUF_STATUS_MOVE_TO_ALLOC, CAM_BUF_IOMMUDEV_DCAM);
			ion_buf->iova[CAM_BUF_IOMMUDEV_DCAM] = 0UL;
			ion_buf->addr_k = 0UL;
		}
	}

	pr_info("done\n");
	return 0;
}

int cam_statis_dcam_port_buffer_cfg(
		void *dcam_handle, void *param)
{
	int ret = 0, j = 0;
	int32_t mfd = 0;
	uint32_t offset = 0;
	enum cam_port_dcam_online_out_id port_id = PORT_DCAM_OUT_MAX;
	struct camera_buf *ion_buf = NULL;
	struct cam_frame *pframe = NULL;
	struct dcam_online_node *dcam_node = NULL;
	struct dcam_online_port *dcam_port = NULL;
	struct dcam_statis_param *statis_param = NULL;
	struct isp_statis_buf_input *input = NULL;

	if (!dcam_handle) {
		pr_err("fail to dcam_online_node is NULL\n");
		return -1;
	}

	dcam_node = (struct dcam_online_node *)dcam_handle;
	statis_param = (struct dcam_statis_param *)param;
	input = (struct isp_statis_buf_input *)statis_param->param;
	if (dcam_node->alg_type == ALG_TYPE_VID_NR && input->type == STATIS_GTMHIST)
		return 0;
	if (input->type == STATIS_INIT) {
		ret = camstatis_dcam_port_buffer_init(dcam_node, statis_param);
		if (ret) {
			pr_err("fail to statis_init\n");
			goto exit;
		}
	} else {
		port_id = camstatis_dcam_type_to_port_id(input->type);
		if (port_id == PORT_DCAM_OUT_MAX) {
			pr_err("fail to get a valid statis type: %d\n", input->type);
			ret = -EINVAL;
			goto exit;
		}

		dcam_port = dcam_online_node_port_get(dcam_node, port_id);
		if (!dcam_port) {
			pr_err("fail to find dcam port, statis: %d\n", input->type);
			ret = -EINVAL;
			goto exit;
		}

		if (port_id == PORT_VCH2_OUT && dcam_port->raw_src != ORI_RAW_SRC_SEL)
			goto exit;

		for (j = 0; j < STATIS_BUF_NUM_MAX; j++) {
			mfd = dcam_node->statis_buf_array[input->type][j].mfd;
			offset = dcam_node->statis_buf_array[input->type][j].offset[0];
			if ((mfd > 0) && (mfd == input->mfd)
				&& (offset == input->offset)) {
				ion_buf = &dcam_node->statis_buf_array[input->type][j];
				break;
			}
		}

		if (ion_buf == NULL) {
			pr_err("fail to get statis buf type %d, mfd %d\n",
					input->type, input->mfd);
			ret = -EINVAL;
			goto exit;
		}

		pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
		if (!pframe) {
			pr_err("fail to get pframe.\n");
			ret = -EINVAL;
			goto exit;
		}
		pframe->common.irq_property = input->type;
		pframe->common.buf = *ion_buf;
		pframe->common.buf.type = CAM_BUF_NONE;

		ret = cam_buf_manager_buf_enqueue(&dcam_port->unprocess_pool, pframe, NULL, dcam_node->buf_manager_handle);
		pr_debug("statis %d, mfd %d, off %d, iova 0x%08x, kaddr 0x%lx\n",
			input->type, mfd, offset, (uint32_t)pframe->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM], pframe->common.buf.addr_k);

		if (ret)
			cam_queue_empty_frame_put(pframe);
	}
exit:
	return ret;
}
