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

#include "cam_offline_statis.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_STATIS: %d %d %s : " fmt, current->pid, __LINE__, __func__

/* VCH2 maybe used for raw picture output
 * If yes, PDAF should not output data though VCH2
 * todo: avoid conflict between raw/pdaf type3
 */
struct offlinestatis_port_buf_info s_offlinestatis_port_info_all[] = {
	{PORT_OFFLINE_PDAF_OUT,       STATIS_PDAF,    0,  0},
	{PORT_OFFLINE_AEM_OUT,        STATIS_AEM,     0,  0},
	{PORT_OFFLINE_AFM_OUT,        STATIS_AFM,     0,  0},
	{PORT_OFFLINE_AFL_OUT,        STATIS_AFL,     0,  0},
	{PORT_OFFLINE_BAYER_HIST_OUT, STATIS_HIST,    0,  0},
	{PORT_OFFLINE_FRGB_HIST_OUT,  STATIS_HIST2,   0,  0},
	{PORT_OFFLINE_LSCM_OUT,       STATIS_LSCM,    0,  0},
	{PORT_OFFLINE_GTM_HIST_OUT,   STATIS_GTMHIST, 0,  0},
};

static enum cam_port_dcam_offline_out_id camoffstatis_dcam_type_to_port_id(enum isp_statis_buf_type type)
{
	switch (type) {
	case STATIS_AEM:
		return PORT_OFFLINE_AEM_OUT;
	case STATIS_AFM:
		return PORT_OFFLINE_AFM_OUT;
	case STATIS_AFL:
		return PORT_OFFLINE_AFL_OUT;
	case STATIS_HIST:
		return PORT_OFFLINE_BAYER_HIST_OUT;
	case STATIS_HIST2:
		return PORT_OFFLINE_FRGB_HIST_OUT;
	case STATIS_PDAF:
		return PORT_OFFLINE_PDAF_OUT;
	case STATIS_LSCM:
		return PORT_OFFLINE_LSCM_OUT;
	case STATIS_GTMHIST:
		return PORT_OFFLINE_GTM_HIST_OUT;
	default:
		return PORT_DCAM_OFFLINE_OUT_MAX;
	}
}

int camoffline_statis_dcam_port_bufferq_init(
	struct dcam_offline_node *dcam_node,
	struct dcam_statis_param *statis_param)
{
	int ret = 0, i = 0, j = 0;
	uint32_t res_buf_size = 0;
	int32_t mfd = 0;
	enum cam_port_dcam_offline_out_id port_id = PORT_DCAM_OFFLINE_OUT_MAX;
	enum isp_statis_buf_type stats_type = STATIS_INIT;
	struct camera_buf *ion_buf = NULL;
	struct cam_frame *pframe = NULL;
	struct dcam_offline_port *dcam_port = NULL;
	struct isp_statis_buf_input *input = NULL;

	pr_debug("enter\n");
	input = (struct isp_statis_buf_input *)statis_param->param;
	res_buf_size = *statis_param->buf_size;

	memset(&dcam_node->statis_buf_array[0][0], 0, sizeof(dcam_node->statis_buf_array));
	for (i = 0; i < ARRAY_SIZE(s_offlinestatis_port_info_all); i++) {
		port_id = s_offlinestatis_port_info_all[i].port_id;
		stats_type = s_offlinestatis_port_info_all[i].buf_type;
		if (dcam_node->alg_type == ALG_TYPE_VID_NR && port_id != PORT_OFFLINE_GTM_HIST_OUT)
			continue;
		dcam_port = dcam_offline_node_port_get(dcam_node, port_id);
		if (!stats_type || !dcam_port)
			continue;
		if (port_id == PORT_OFFLINE_GTM_HIST_OUT && dcam_node->dev->hw->ip_isp->isphw_abt->rgb_gtm_support)
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

			pr_debug("statis %d buf_mfd %d offset %ld kaddr 0x%lx iova 0x%08x size %zu\n",
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

int cam_offline_statis_dcam_port_bufferq_deinit(void *dcam_handle)
{
	int i = 0, j = 0;
	int32_t mfd = 0;
	enum cam_port_dcam_offline_out_id port_id = PORT_DCAM_OFFLINE_OUT_MAX;
	enum isp_statis_buf_type stats_type = STATIS_INIT;
	struct camera_buf *ion_buf = NULL;
	struct dcam_offline_node *dcam_node = NULL;
	struct dcam_offline_port *dcam_port = NULL;

	dcam_node = (struct dcam_offline_node *)dcam_handle;
	for (i = 0; i < ARRAY_SIZE(s_offlinestatis_port_info_all); i++) {
		port_id = s_offlinestatis_port_info_all[i].port_id;
		stats_type = s_offlinestatis_port_info_all[i].buf_type;

		if (dcam_node->alg_type == ALG_TYPE_VID_NR && port_id != PORT_OFFLINE_GTM_HIST_OUT)
			continue;

		dcam_port = dcam_offline_node_port_get(dcam_node, port_id);
		if (!stats_type || !dcam_port)
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

int cam_offline_statis_dcam_port_buffer_cfg(
		void *dcam_handle, void *param)
{
	int ret = 0, j = 0;
	int32_t mfd = 0;
	uint32_t offset = 0;
	enum cam_port_dcam_offline_out_id port_id = PORT_DCAM_OFFLINE_OUT_MAX;
	struct camera_buf *ion_buf = NULL;
	struct cam_frame *pframe = NULL;
	struct dcam_offline_node *dcam_node = NULL;
	struct dcam_offline_port *dcam_port = NULL;
	struct dcam_statis_param *statis_param = NULL;
	struct isp_statis_buf_input *input = NULL;

	if (!dcam_handle) {
		pr_err("fail to dcam_online_node is NULL\n");
		return -1;
	}

	dcam_node = (struct dcam_offline_node *)dcam_handle;
	statis_param = (struct dcam_statis_param *)param;
	input = (struct isp_statis_buf_input *)statis_param->param;
	if (input->type == STATIS_INIT) {
		ret = camoffline_statis_dcam_port_bufferq_init(dcam_node, statis_param);
		if (ret) {
			pr_err("fail to statis_init\n");
			goto exit;
		}
	} else {
		port_id = camoffstatis_dcam_type_to_port_id(input->type);
		if (port_id == PORT_DCAM_OFFLINE_OUT_MAX) {
			pr_err("fail to get a valid statis type: %d\n", input->type);
			ret = -EINVAL;
			goto exit;
		}

		dcam_port = dcam_offline_node_port_get(dcam_node, port_id);
		if (!dcam_port) {
			pr_err("fail to find dcam port, statis: %d\n", input->type);
			ret = -EINVAL;
			goto exit;
		}
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
			pr_err("fail to get statis buf %d, type %d\n", input->type, input->mfd);
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
		pr_debug("statis %d, mfd %d, off %d, iova 0x%08x,  kaddr 0x%lx\n",
			input->type, mfd, offset,
			(uint32_t)pframe->common.buf.iova[CAM_BUF_IOMMUDEV_DCAM], pframe->common.buf.addr_k);

		if (ret)
			cam_queue_empty_frame_put(pframe);
	}
exit:
	return ret;
}
