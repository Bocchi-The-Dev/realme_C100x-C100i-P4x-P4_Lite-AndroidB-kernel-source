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

#include "pyr_dec_port.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "PYR_DEC_PORT: %d %d %s : " fmt, current->pid, __LINE__, __func__

int pyr_dec_port_param_cfg(void *handle, enum cam_port_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct pyr_dec_port *port = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_frame **frame = NULL;
	struct cam_buf_alloc_desc *alloc_param = NULL;
	struct cam_buf_pool_id pool_id = {0};

	if(!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	port = (struct pyr_dec_port *)handle;

	if (port->share_buffer)
		pool_id.tag_id = CAM_BUF_POOL_SHARE_DEC_BUF;
	else
		pool_id = port->unprocess_pool;

	switch (cmd) {
	case PORT_CFG_BUFFER_SET:
		pframe = (struct cam_frame *)param;
		ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, NULL, port->buf_manager_handle);
		if (ret) {
			pr_err("fail to enqueue pyrdec buffer\n");
			ret = -EFAULT;
		}
		break;
	case PORT_CFG_BUFFER_CYCLE:
		frame = (struct cam_frame **)param;
		*frame = cam_buf_manager_buf_dequeue(&pool_id, NULL, port->buf_manager_handle);
		if (*frame) {
			ret = cam_buf_manager_buf_enqueue(&port->result_pool, *frame, NULL, port->buf_manager_handle);
			if (ret) {
				pr_err("fail to enqueue store result pool\n");
				ret = cam_buf_manager_buf_enqueue(&pool_id, *frame, NULL, port->buf_manager_handle);
				if (ret)
					pr_err("fail to enqueue frame\n");
			}
		}
		break;
	case PORT_CFG_BUFFER_GET:
		frame = (struct cam_frame **)param;
		*frame = cam_buf_manager_buf_dequeue(&port->result_pool, NULL, port->buf_manager_handle);
		if (*frame == NULL) {
			pr_err("fail to available dcamoffline result buf %d\n", port->port_id);
			ret = -EFAULT;
		}
		break;
	case PORT_CFG_BUFFER_ALLOC:
		alloc_param = (struct cam_buf_alloc_desc *)param;
		ret = pyr_dec_port_buf_alloc(port, alloc_param);
		break;
	case PORT_CFG_BUFFER_CLR:
		pframe = cam_buf_manager_buf_dequeue(&port->result_pool, NULL, port->buf_manager_handle);
		if (pframe) {
			ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, NULL, port->buf_manager_handle);
			if (ret) {
				pr_err("fail to enqueue pyrdec buffer\n");
				ret = -EFAULT;
			}
		}
		break;
	default:
		pr_err("fail to support port cfg cmd %d\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

int pyr_dec_port_buf_alloc(void *handle, struct cam_buf_alloc_desc *param)
{
	int ret = 0;
	uint32_t buffer_size = 0;
	struct pyr_dec_port *port = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_buf_pool_id pool_id = {0};
	struct camera_buf_get_desc buf_desc = {0};

	if(!handle || !param) {
		pr_err("fail to get valid inptr %p, %p\n", handle, param);
		return -EFAULT;
	}

	if (!param->is_pyr_dec)
		return 0;

	port = (struct pyr_dec_port *)handle;

	if (param->share_buffer)
		pool_id.tag_id = CAM_BUF_POOL_SHARE_DEC_BUF;
	else
		pool_id = port->unprocess_pool;

	buffer_size = dcam_if_cal_pyramid_size(param->width, param->height, param->pyr_out_fmt, 0, ISP_PYR_DEC_LAYER_NUM);
	buffer_size = ALIGN(buffer_size, CAM_BUF_ALIGN_SIZE);
	pframe = cam_queue_empty_frame_get(CAM_FRAME_GENERAL);
	if (!pframe) {
		pr_err("fail to get pframe.\n");
		ret = -EINVAL;
		goto exit;
	}
	pframe->common.width = param->width;
	pframe->common.height = param->height;
	pframe->common.channel_id = param->ch_id;
	pframe->common.is_compressed = 0;
	ret = cam_buf_alloc(&pframe->common.buf, buffer_size, param->iommu_enable);
	if (ret) {
		pr_err("fail to alloc dec buf\n");
		goto exit;
	}
	cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_ISP);

	pframe->common.buf.bypass_iova_ops = 1;
	ret = cam_buf_manager_buf_enqueue(&pool_id, pframe, &buf_desc, port->buf_manager_handle);
	if (ret) {
		pr_err("fail to enq pyrdec buffer\n");
		cam_queue_empty_frame_put(pframe);
	}

exit:
	return ret;
}

void *pyr_dec_port_get(uint32_t port_id, struct pyr_dec_port_desc *param)
{
	struct pyr_dec_port *port = NULL;
	int ret = 0;

	if (!param) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	if (*param->port_dev == NULL) {
		port = cam_buf_kernel_sys_vzalloc(sizeof(struct pyr_dec_port));
		if (!port) {
			pr_err("fail to get valid pyr dec port %d\n", port_id);
			return NULL;
		}
		pr_debug("pyr dec port %d dev %p\n", port_id, port);
	} else {
		port = *param->port_dev;
		pr_debug("pyr dec port has been alloc %p %d\n", port, port_id);
		goto exit;
	}

	port->port_id = port_id;
	port->share_buffer = param->share_buffer;
	port->buf_manager_handle = param->buf_manager_handle;
	*param->port_dev = port;
	atomic_set(&port->is_work, 0);

	ret = cam_buf_manager_pool_reg(NULL, PYR_DEC_OUT_BUF_Q_LEN, port->buf_manager_handle);
	if (ret <= 0) {
		pr_err("fail to reg result pool for dcam offline node\n");
		cam_buf_kernel_sys_vfree(port);
		port = NULL;
		return NULL;
	}
	port->unprocess_pool.private_pool_id = ret;

	ret = cam_buf_manager_pool_reg(NULL, PYR_DEC_OUT_BUF_Q_LEN, port->buf_manager_handle);
	if (ret <= 0) {
		pr_err("fail to reg result pool for dcam offline node\n");
		cam_buf_kernel_sys_vfree(port);
		port = NULL;
		return NULL;
	}
	port->result_pool.private_pool_id = ret;
	pr_debug("%d reg pool %d", port_id, port->result_pool.private_pool_id);
	*param->port_dev = port;
	port->port_id = port_id;
	port->data_cb_handle = param->data_cb_handle;
	port->data_cb_func = param->data_cb_func;
	pr_info("port id %d node_dev %p\n", port_id, *param->port_dev);

exit:
	atomic_inc(&port->user_cnt);
	return port;
}

void pyr_dec_port_put(struct pyr_dec_port *port)
{
	if (!port) {
		pr_err("fail to get invalid port ptr\n");
		return;
	}

	if (atomic_dec_return(&port->user_cnt) == 0) {
		cam_buf_manager_pool_unreg(&port->result_pool, port->buf_manager_handle);
		cam_buf_manager_pool_unreg(&port->unprocess_pool, port->buf_manager_handle);
		port->data_cb_handle = NULL;
		port->data_cb_func = NULL;
		port->buf_manager_handle = NULL;
		pr_debug("pyr_dec_port %d put success\n", port->port_id);
		cam_buf_kernel_sys_vfree(port);
		port = NULL;
	}
}
