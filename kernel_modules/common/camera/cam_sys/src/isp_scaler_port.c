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

#include <linux/vmalloc.h>

#include "cam_zoom.h"
#include "dcam_core.h"
#include "isp_drv.h"
#include "isp_port.h"
#include "isp_scaler_node.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_SCALER_PORT: %d %d %s : " fmt, current->pid, __LINE__, __func__

static enum cam_en_status ispscaler_port_fid_check(struct cam_frame *frame, void *data)
{
	uint32_t target_fid;

	if (!frame || !data)
		return false;

	target_fid = *(uint32_t *)data;

	pr_debug("target_fid = %d frame->user_fid = %d\n", target_fid, frame->common.user_fid);
	return frame->common.user_fid == CAMERA_RESERVE_FRAME_NUM
		|| frame->common.user_fid == target_fid;
}

static int ispscaler_port_scaler_param_calc(struct img_trim *in_trim,
	struct img_size *out_size, struct yuv_scaler_info *scaler,
	struct img_deci_info *deci)
{
	int ret = 0;
	unsigned int tmp_dstsize = 0;
	unsigned int align_size = 0;
	unsigned int d_max = ISP_SC_COEFF_DOWN_MAX;
	unsigned int u_max = ISP_SC_COEFF_UP_MAX;
	unsigned int f_max = CAM_SCALER_DECI_FAC_MAX;

	CAM_ZOOM_DEBUG("in_trim_size_x:%d, in_trim_size_y:%d, out_size_w:%d,out_size_h:%d\n",
		in_trim->size_x, in_trim->size_y, out_size->w, out_size->h);
	/* check input crop limit with max scale up output size(2 bit aligned) */
	if (in_trim->size_x > (out_size->w * d_max * (1 << f_max)) ||
		in_trim->size_y > (out_size->h * d_max * (1 << f_max)) ||
		in_trim->size_x < ISP_DIV_ALIGN_W(out_size->w, u_max) ||
		in_trim->size_y < ISP_DIV_ALIGN_H(out_size->h, u_max)) {
		pr_err("fail to get in_trim %d %d. out _size %d %d, fmax %d, u_max %d\n",
				in_trim->size_x, in_trim->size_y,
				out_size->w, out_size->h, f_max, d_max);
		ret = -EINVAL;
	} else {
		scaler->scaler_factor_in = in_trim->size_x;
		scaler->scaler_ver_factor_in = in_trim->size_y;
		if (in_trim->size_x > out_size->w * d_max) {
			tmp_dstsize = out_size->w * d_max;
			deci->deci_x = cam_zoom_port_deci_factor_get(in_trim->size_x, tmp_dstsize, CAM_SCALER_DECI_FAC_MAX);
			deci->deci_x_eb = 1;
			align_size = (1 << (deci->deci_x + 1)) * ISP_PIXEL_ALIGN_WIDTH;
			in_trim->size_x = (in_trim->size_x) & ~(align_size - 1);
			in_trim->start_x = (in_trim->start_x) & ~(align_size - 1);
			scaler->scaler_factor_in = in_trim->size_x >> (deci->deci_x + 1);
		} else {
			deci->deci_x = 1;
			deci->deci_x_eb = 0;
		}

		if (in_trim->size_y > out_size->h * d_max) {
			tmp_dstsize = out_size->h * d_max;
			deci->deci_y = cam_zoom_port_deci_factor_get(in_trim->size_y, tmp_dstsize, CAM_SCALER_DECI_FAC_MAX);
			deci->deci_y_eb = 1;
			align_size = (1 << (deci->deci_y + 1)) * ISP_PIXEL_ALIGN_HEIGHT;
			in_trim->size_y = (in_trim->size_y) & ~(align_size - 1);
			in_trim->start_y = (in_trim->start_y) & ~(align_size - 1);
			scaler->scaler_ver_factor_in = in_trim->size_y >> (deci->deci_y + 1);
		} else {
			deci->deci_y = 1;
			deci->deci_y_eb = 0;
		}
		CAM_ZOOM_DEBUG("end out_size  w %d, h %d\n",
			out_size->w, out_size->h);

		scaler->scaler_factor_out = out_size->w;
		scaler->scaler_ver_factor_out = out_size->h;
		scaler->scaler_out_width = out_size->w;
		scaler->scaler_out_height = out_size->h;
	}

	return ret;
}

static int ispscaler_port_hwinfo_get(void *cfg_in, struct isp_hw_path_scaler *path)
{
	int ret = 0;
	uint32_t is_yuv422 = 0, scale2yuv420 = 0;
	struct yuv_scaler_info *scaler = NULL;
	struct isp_scaler_port *in_ptr = NULL;


	if (!cfg_in || !path) {
		pr_err("fail to get valid input ptr %p, %p\n", cfg_in, path);
		return -EFAULT;
	}
	in_ptr = (struct isp_scaler_port *)cfg_in;

	scaler = &path->scaler;
	if (in_ptr->fmt == CAM_UYVY_1FRAME)
		path->uv_sync_v = 1;
	else
		path->uv_sync_v = 0;
	if (in_ptr->fmt == CAM_FULL_RGB14)
		path->path_sel = 2;
	else
		path->path_sel = 0;
	path->frm_deci = 0;
	path->dst = in_ptr->dst;
	path->out_trim.start_x = 0;
	path->out_trim.start_y = 0;
	path->out_trim.size_x = in_ptr->dst.w;
	path->out_trim.size_y = in_ptr->dst.h;
	path->regular_info.regular_mode = in_ptr->regular_mode;
	ret = ispscaler_port_scaler_param_calc(&path->in_trim, &path->dst,
		&path->scaler, &path->deci);
	if (ret) {
		pr_err("fail to calc scaler param.\n");
		return ret;
	}

	if ((in_ptr->fmt == CAM_YUV422_2FRAME) || (in_ptr->fmt == CAM_YVU422_2FRAME))
		is_yuv422 = 1;

	if (((scaler->scaler_ver_factor_in == scaler->scaler_ver_factor_out)
		&& (scaler->scaler_factor_in == scaler->scaler_factor_out)
		&& (is_yuv422 || in_ptr->scaler_bypass_ctrl))
		|| in_ptr->fmt == CAM_FULL_RGB14) {
		scaler->scaler_bypass = 1;
	} else {
		scaler->scaler_bypass = 0;

		if (in_ptr->scaler_coeff_ex) {
			/*0:yuv422 to 422 ;1:yuv422 to 420 2:yuv420 to 420*/
			scaler->work_mode = 2;
			ret = cam_scaler_coeff_calc_ex(scaler);
		}  else {
			scale2yuv420 = is_yuv422 ? 0 : 1;
			ret = cam_scaler_coeff_calc(scaler, scale2yuv420);
		}

		if (ret) {
			pr_err("fail to calc scaler coeff.\n");
			return ret;
		}
	}
	scaler->odata_mode = is_yuv422 ? 0x00 : 0x01;

	return ret;
}

static struct cam_frame *ispscaler_port_reserved_buf_get(reserved_buf_get_cb resbuf_get_cb,
	void *cb_data, struct isp_scaler_port *port)
{
	struct cam_frame *frame = NULL;

	if (resbuf_get_cb)
		resbuf_get_cb((void *)&frame, cb_data);
	if (frame != NULL)
		frame->common.priv_data = port;
	pr_debug("isp scaler use reserved buffer for port %d\n", port->port_id);
	return frame;
}

static struct cam_frame *ispscaler_port_out_frame_get(struct isp_scaler_port *port, struct isp_scaler_port_cfg *port_cfg)
{
	int ret = 0;
	struct isp_yuv_scaler_node *inode = NULL;
	struct cam_frame *out_frame = NULL;

	if (!port || !port_cfg) {
		pr_err("fail to get valid input port %p, port_cfg %p\n", port, port_cfg);
		return NULL;
	}

	inode = (struct isp_yuv_scaler_node *)port_cfg->node_handle;
	if (!inode) {
		pr_err("fail to get valid input\n");
		return NULL;
	}

	pr_debug("port->port_id %d\n", port->port_id);

	if (inode->uinfo.uframe_sync && port_cfg->target_fid != CAMERA_RESERVE_FRAME_NUM)
		out_frame = cam_queue_dequeue_if(&port->out_buf_queue, ispscaler_port_fid_check, (void *)&port_cfg->target_fid);
	else
		out_frame = CAM_QUEUE_DEQUEUE(&port->out_buf_queue, struct cam_frame, list);

	if (out_frame)
		port_cfg->valid_out_frame = 1;
	else
		out_frame = ispscaler_port_reserved_buf_get(port->resbuf_get_cb, port->resbuf_cb_data, port);

	if (out_frame != NULL) {
		if (out_frame->common.is_reserved == 0 && (out_frame->common.buf.mapping_state & CAM_BUF_MAPPING_ISP) == 0) {
			ret = cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_GET_IOVA, CAM_BUF_IOMMUDEV_ISP);
			pr_debug("map output buffer %08x\n", (uint32_t)out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP]);
			if (ret) {
				CAM_QUEUE_ENQUEUE(&port->out_buf_queue, &out_frame->list);
				out_frame = NULL;
				pr_err("fail to map isp iommu buf.\n");
			}
		}
	}

	return out_frame;
}

static int ispscaler_port_store_frm_set(struct isp_pipe_info *pipe_info, uint32_t hw_path_id, struct cam_frame *frame)
{
	int ret = 0, planes = 0;
	unsigned long offset_u, offset_v, yuv_addr[3] = {0};
	struct isp_store_info *store = NULL;

	if (!pipe_info || !frame || hw_path_id == ISP_SPATH_NUM) {
		pr_err("fail to get valid input ptr, pipe_info %p, frame %p\n",
			pipe_info, frame);
		return -EINVAL;
	}

	pr_debug("enter.port->spath_id %d\n",hw_path_id);
	store = &pipe_info->store[hw_path_id].store;

	if (store->color_fmt == CAM_UYVY_1FRAME)
		planes = 1;
	else if ((store->color_fmt == CAM_YUV422_3FRAME)
			|| (store->color_fmt == CAM_YUV420_3FRAME))
		planes = 3;
	else
		planes = 2;

	if (frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP] == 0) {
		pr_err("fail to get valid iova address, fd = 0x%x\n",
			frame->common.buf.mfd);
		return -EINVAL;
	}

	yuv_addr[0] = frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];

	pr_debug("fmt %s, planes %d addr %lx %lx %lx, pitch:%d\n",
		camport_fmt_name_get(store->color_fmt), planes, yuv_addr[0], yuv_addr[1], yuv_addr[2], store->pitch.pitch_ch0);

	if ((planes > 1) && yuv_addr[1] == 0) {
		offset_u = store->pitch.pitch_ch0 * store->size.h;
		yuv_addr[1] = yuv_addr[0] + offset_u;
	}

	if ((planes > 2) && yuv_addr[2] == 0) {
		offset_v = store->pitch.pitch_ch1 * store->size.h;
		if (store->color_fmt == CAM_YUV420_3FRAME)
			offset_v >>= 1;
		yuv_addr[2] = yuv_addr[1] + offset_v;
	}

	pr_debug("path %d planes %d addr %lx %lx %lx\n",
		hw_path_id, planes, yuv_addr[0], yuv_addr[1], yuv_addr[2]);

	store->addr.addr_ch0 = yuv_addr[0];
	store->addr.addr_ch1 = yuv_addr[1];
	store->addr.addr_ch2 = yuv_addr[2];
	pr_debug("done %x %x %x\n", store->addr.addr_ch0, store->addr.addr_ch1, store->addr.addr_ch2);

	return ret;
}

static int ispscaler_port_store_frameproc(struct isp_scaler_port *port,
	struct cam_frame *out_frame, struct isp_scaler_port_cfg *port_cfg)
{
	int ret = 0;
	uint32_t hw_path_id = -1, loop = 0;
	struct isp_yuv_scaler_node *inode = NULL;

	if (out_frame == NULL || !port_cfg || !port_cfg->src_frame) {
		pr_err("fail to get out_frame, port id:%d\n", port->port_id);
		return -EINVAL;
	}

	inode = (struct isp_yuv_scaler_node *)port_cfg->node_handle;
	out_frame->common.fid = port_cfg->src_frame->common.fid;
	out_frame->common.sensor_time = port_cfg->src_frame->common.sensor_time;
	out_frame->common.boot_sensor_time = port_cfg->src_frame->common.boot_sensor_time;
	out_frame->common.link_from.port_id = port->port_id;

	hw_path_id = isp_port_id_switch(port->port_id);
	if (hw_path_id >= ISP_SPATH_NUM) {
		pr_err("fail to get correct path id\n");
		return -EFAULT;
	}

	if (inode->ch_id == CAM_CH_CAP)
		pr_info("isp scaler node %d is_reserved %d iova 0x%x, user_fid: %x mfd 0x%x\n",
			inode->node_id, out_frame->common.is_reserved,
			(uint32_t)out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP], out_frame->common.user_fid,
			out_frame->common.buf.mfd);
	else
		pr_debug("isp scaler node %d, path%d is_reserved %d iova 0x%x, user_fid: %x mfd 0x%x fid: %d\n",
			inode->node_id, hw_path_id, out_frame->common.is_reserved,
			(uint32_t)out_frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP], out_frame->common.user_fid,
			out_frame->common.buf.mfd, out_frame->common.fid);

	/* config store buffer */
	ret = ispscaler_port_store_frm_set(port_cfg->pipe_info, hw_path_id, out_frame);
	if (ret) {
		cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_MOVE_TO_ION, CAM_BUF_IOMMUDEV_ISP);
		cam_buf_ionbuf_put(&out_frame->common.buf);
		pr_err("fail to set store buffer\n");
		return -EINVAL;
	}

	do {
		ret = CAM_QUEUE_ENQUEUE(&port->result_queue, &out_frame->list);
		if (ret == 0)
			break;
		printk_ratelimited(KERN_INFO "wait for output queue. loop %d\n", loop);
		/* wait for previous frame output queue done */
		os_adapt_time_mdelay(1);
	} while (loop++ < 500);

	if (ret) {
		pr_err("fail to enqueue, path %d\n", hw_path_id);
		/* ret frame to original queue */
		if (out_frame->common.is_reserved) {
			cam_queue_empty_frame_put(out_frame);
		} else {
			cam_buf_manager_buf_status_cfg(&out_frame->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
			CAM_QUEUE_ENQUEUE(&port->out_buf_queue, &out_frame->list);
		}
		return -EINVAL;
	}

	return ret;
}

static int ispscaler_port_store_frame_cycle(struct isp_scaler_port *port, void *param)
{
	int ret = 0;
	struct isp_yuv_scaler_node *inode = NULL;
	struct isp_scaler_port_cfg *port_cfg = NULL;
	struct cam_frame *out_frame = NULL;

	port_cfg = (struct isp_scaler_port_cfg *)param;
	if (!port || !port_cfg) {
		pr_err("fail to get valid port %p, port_cfg %p\n", port, port_cfg);
		return -EINVAL;
	}
	inode = (struct isp_yuv_scaler_node *)port_cfg->node_handle;
	if (!inode) {
		pr_err("fail to get valid in_ptr\n");
		return -EINVAL;
	}
	out_frame = ispscaler_port_out_frame_get(port, port_cfg);
	ret = ispscaler_port_store_frameproc(port, out_frame, port_cfg);
	return ret;
}

static int ispscaler_port_frame_cycle(struct isp_scaler_port *port, void *param)
{
	int ret = 0;

	if (port->type == PORT_TRANSFER_OUT) {
		switch (port->port_id) {
		case PORT_PRE_OUT:
		case PORT_CAP_OUT:
			ret = ispscaler_port_store_frame_cycle(port, param);
			break;
		case PORT_VID_OUT:
			break;
		default:
			pr_err("fail to get port_id:%d or type:%d\n", port->port_id, port->type);
		}
	}
	return ret;
}

static void ispscaler_port_frame_ret(void *param)
{
	struct cam_frame * pframe = NULL;
	struct isp_scaler_port *port = NULL;

	if (!param) {
		pr_err("fail to get input ptr.\n");
		return;
	}

	pframe = (struct cam_frame *)param;
	port = (struct isp_scaler_port *)pframe->common.priv_data;
	if (!port) {
		pr_err("fail to get out_frame port.\n");
		return;
	}

	if (pframe->common.is_reserved)
		cam_queue_empty_frame_put(pframe);
	else {
		if (pframe->common.buf.mapping_state & CAM_BUF_MAPPING_ISP)
			cam_buf_manager_buf_status_cfg(&pframe->common.buf, CAM_BUF_STATUS_PUT_IOVA, CAM_BUF_IOMMUDEV_ISP);
		port->data_cb_func(CAM_CB_ISP_RET_DST_BUF, pframe, port->data_cb_handle);
	}
}

static int ispscaler_port_fetch_normal_get(void *cfg_in, void *cfg_out, struct cam_frame *frame)
{
	int ret = 0;
	uint32_t trim_offset[3] = { 0 };
	struct img_size *src = NULL;
	struct img_trim *intrim = NULL;
	struct isp_hw_fetch_info *fetch = NULL;
	struct isp_scaler_port *port = NULL;
	uint32_t mipi_word_num_start[16] = {
		0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5};
	uint32_t mipi_word_num_end[16] = {
		0, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5};

	if (!cfg_in || !cfg_out || !frame) {
		pr_err("fail to get valid input ptr, %p, %p\n", cfg_in, cfg_out);
		return -EFAULT;
	}

	port = VOID_PTR_TO(cfg_in, struct isp_scaler_port);
	fetch = (struct isp_hw_fetch_info *)cfg_out;
	src = &port->size;
	intrim = &port->trim;
	fetch->src = *src;
	fetch->in_trim = *intrim;
	fetch->fetch_fmt = port->fmt;
	fetch->bayer_pattern = port->bayer_pattern;
	if (cam_raw_fmt_get(port->fmt))
		fetch->dispatch_color = 0;
	else if (port->fmt == CAM_FULL_RGB10)
		fetch->dispatch_color = 1;
	else
		fetch->dispatch_color = 2;
	fetch->addr.addr_ch0 = frame->common.buf.iova[CAM_BUF_IOMMUDEV_ISP];

	switch (fetch->fetch_fmt) {
	case CAM_YUV422_3FRAME:
		fetch->pitch.pitch_ch0 = src->w;
		fetch->pitch.pitch_ch1 = src->w / 2;
		fetch->pitch.pitch_ch2 = src->w / 2;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x;
		trim_offset[1] = intrim->start_y * fetch->pitch.pitch_ch1 + intrim->start_x / 2;
		trim_offset[2] = intrim->start_y * fetch->pitch.pitch_ch2 + intrim->start_x / 2;
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		fetch->addr.addr_ch2 = fetch->addr.addr_ch1 + fetch->pitch.pitch_ch1 * fetch->src.h;
		break;
	case CAM_YUYV_1FRAME:
	case CAM_UYVY_1FRAME:
	case CAM_YVYU_1FRAME:
	case CAM_VYUY_1FRAME:
		fetch->pitch.pitch_ch0 = src->w * 2;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x * 2;
		break;
	case CAM_RAW_HALFWORD_10:
	case CAM_RAW_14:
		fetch->pitch.pitch_ch0 = cal_sprd_pitch(src->w, port->fmt);
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x * 2;
		break;
	case CAM_YUV422_2FRAME:
	case CAM_YVU422_2FRAME:
		fetch->pitch.pitch_ch0 = src->w;
		fetch->pitch.pitch_ch1 = src->w;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x;
		trim_offset[1] = intrim->start_y * fetch->pitch.pitch_ch1 + intrim->start_x;
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		break;
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
		fetch->pitch.pitch_ch0 = src->w;
		fetch->pitch.pitch_ch1 = src->w;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x;
		trim_offset[1] = intrim->start_y * fetch->pitch.pitch_ch1 / 2 + intrim->start_x;
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		pr_debug("y_addr: %x, pitch:: %x\n", fetch->addr.addr_ch0, fetch->pitch.pitch_ch0);
		break;
	case CAM_YUV420_2FRAME_10:
	case CAM_YVU420_2FRAME_10:
		fetch->pitch.pitch_ch0 = (src->w * 16 + 127) / 128 * 128 / 8;
		fetch->pitch.pitch_ch1 = (src->w * 16 + 127) / 128 * 128 / 8;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x * 2;
		trim_offset[1] = intrim->start_y * fetch->pitch.pitch_ch1 / 2 + intrim->start_x * 2;
		break;
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
	{
		uint32_t start_col = intrim->start_x;
		uint32_t start_row = intrim->start_y;
		uint32_t end_col =  intrim->start_x + intrim->size_x - 1;
		fetch->pitch.pitch_ch0 = (src->w * 10 + 127) / 128 * 128 / 8;
		fetch->pitch.pitch_ch1 = (src->w * 10 + 127) / 128 * 128 / 8;
		fetch->mipi_byte_rel_pos = intrim->start_x & 0xf;
		fetch->mipi_word_num = ((end_col + 1) >> 4) * 5
			+ mipi_word_num_end[(end_col + 1) & 0xF]
			- ((start_col + 1) >> 4) * 5
			- mipi_word_num_start[(start_col + 1) & 0xF] + 1;
		fetch->mipi_byte_rel_pos_uv = fetch->mipi_byte_rel_pos;
		fetch->mipi_word_num_uv = fetch->mipi_word_num;
		trim_offset[0] = start_row * fetch->pitch.pitch_ch0 + (start_col >> 2) * 5 +
			(start_col & 0x3);
		trim_offset[1] = (start_row >> 1) * fetch->pitch.pitch_ch1 + (start_col >> 2) * 5 +
			(start_col & 0x3);
		fetch->addr.addr_ch1 = fetch->addr.addr_ch0 + fetch->pitch.pitch_ch0 * fetch->src.h;
		break;
	}
	case CAM_FULL_RGB10:
		fetch->pitch.pitch_ch0 = src->w * 8;
		trim_offset[0] = intrim->start_y * fetch->pitch.pitch_ch0 + intrim->start_x * 8;
		break;
	case CAM_RAW_PACK_10:
	{
		uint32_t mipi_byte_info = 0;
		uint32_t mipi_word_info = 0;
		uint32_t start_col = intrim->start_x;
		uint32_t start_row = intrim->start_y;
		uint32_t end_col =  intrim->start_x + intrim->size_x - 1;
		mipi_byte_info = start_col & 0xF;
		mipi_word_info = ((end_col + 1) >> 4) * 5
			+ mipi_word_num_end[(end_col + 1) & 0xF]
			- ((start_col + 1) >> 4) * 5
			- mipi_word_num_start[(start_col + 1) & 0xF] + 1;
		fetch->mipi_byte_rel_pos = mipi_byte_info;
		fetch->mipi_word_num = mipi_word_info;
		fetch->pitch.pitch_ch0 = cal_sprd_pitch(src->w, CAM_RAW_PACK_10);
		/* same as slice starts */
		trim_offset[0] = start_row * fetch->pitch.pitch_ch0 + (start_col >> 2) * 5 + (start_col & 0x3);
		break;
	}
	default:
		pr_err("fail to get fetch format: %s\n", camport_fmt_name_get(fetch->fetch_fmt));
		ret = -EINVAL;
		break;
	}

	fetch->addr_hw.addr_ch0 = fetch->addr.addr_ch0 + trim_offset[0];
	fetch->addr_hw.addr_ch1 = fetch->addr.addr_ch1 + trim_offset[1];
	fetch->addr_hw.addr_ch2 = fetch->addr.addr_ch2 + trim_offset[2];
	pr_debug("fetch fmt %s, y_addr: %x, u_addr: %x\n", camport_fmt_name_get(fetch->fetch_fmt), fetch->addr_hw.addr_ch0, fetch->addr_hw.addr_ch1);

	return ret;
}

static int ispscaler_port_store_normal_get(struct isp_scaler_port *port, struct isp_hw_path_store *store_info)
{
	int ret = 0;
	struct isp_store_info *store = NULL;

	if (!port || !store_info) {
		pr_err("fail to get valid input ptr %p, port %p\n", store_info, port);
		return -EFAULT;
	}

	store = &store_info->store;
	store->color_fmt = port->fmt;
	store->bypass = 0;
	store->endian = port->data_endian;
	if (store->color_fmt == CAM_FULL_RGB14)
		store->speed_2x = 0;
	else
		store->speed_2x = 1;

	if (cam_data_bits(port->fmt) == CAM_10_BITS)
		store->need_bwd = 0;
	else
		store->need_bwd = 1;

	store->mirror_en = 0;
	store->max_len_sel = 0;
	store->shadow_clr_sel = 1;
	store->shadow_clr = 1;
	store->store_res = 1;
	store->rd_ctrl = 0;
	/*need to confirm*/
	store->size = port->size;
	switch (store->color_fmt) {
	case CAM_UYVY_1FRAME:
		store->pitch.pitch_ch0 = store->size.w * 2;
		store->total_size = store->size.w * store->size.h * 2;
		break;
	case CAM_YUV422_2FRAME:
	case CAM_YVU422_2FRAME:
		store->pitch.pitch_ch0 = store->size.w;
		store->pitch.pitch_ch1 = store->size.w;
		store->total_size = store->size.w * store->size.h * 2;
		break;
	case CAM_YUV420_2FRAME:
	case CAM_YVU420_2FRAME:
		store->pitch.pitch_ch0 = store->size.w;
		store->pitch.pitch_ch1 = store->size.w;
		store->total_size = store->size.w * store->size.h * 3 / 2;
		break;
	case CAM_YUV420_2FRAME_10:
	case CAM_YVU420_2FRAME_10:
		store->pitch.pitch_ch0 = store->size.w * 2;
		store->pitch.pitch_ch1 = store->size.w * 2;
		store->total_size = store->size.w * store->size.h * 3 / 2;
		break;
	case CAM_YUV420_2FRAME_MIPI:
	case CAM_YVU420_2FRAME_MIPI:
		store->pitch.pitch_ch0 = store->size.w * 10 / 8;
		store->pitch.pitch_ch1 = store->size.w * 10 / 8 ;
		store->total_size = store->size.w * store->size.h * 3 / 2;
		break;
	case CAM_YUV422_3FRAME:
		store->pitch.pitch_ch0 = store->size.w;
		store->pitch.pitch_ch1 = store->size.w / 2;
		store->pitch.pitch_ch2 = store->size.w / 2;
		store->total_size = store->size.w * store->size.h * 2;
		break;
	case CAM_YUV420_3FRAME:
		store->pitch.pitch_ch0 = store->size.w;
		store->pitch.pitch_ch1 = store->size.w / 2;
		store->pitch.pitch_ch2 = store->size.w / 2;
		store->total_size = store->size.w * store->size.h * 3 / 2;
		break;
	case CAM_FULL_RGB14:
		store->pitch.pitch_ch0 = store->size.w * 8;
		break;
	default:
		pr_err("fail to get support store fmt: %s\n", camport_fmt_name_get(store->color_fmt));
		store->pitch.pitch_ch0 = 0;
		store->pitch.pitch_ch1 = 0;
		store->pitch.pitch_ch2 = 0;
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ispscaler_port_fetch_pipeinfo_get(struct isp_scaler_port *port, struct isp_scaler_port_cfg *port_cfg)
{
	struct isp_pipe_info *pipe_in = NULL;
	uint32_t ret = 0;

	pipe_in = port_cfg->pipe_info;
	pipe_in->fetch.ctx_id = port_cfg->cfg_id;
	ret = ispscaler_port_fetch_normal_get(port, &pipe_in->fetch, port_cfg->src_frame);
	if (ret) {
		pr_err("fail to get pipe fetch info\n");
		return -EFAULT;
	}
	port_cfg->src_size = port->size;
	port_cfg->src_crop = port->trim;
	port_cfg->in_fmt = port->fmt;
	CAM_ZOOM_DEBUG("in_fmt %d src %d %d trim %d %d %d %d\n", port->fmt,
		port_cfg->src_size.w, port_cfg->src_size.h, port_cfg->src_crop.start_x,
		port_cfg->src_crop.start_y, port_cfg->src_crop.size_x, port_cfg->src_crop.size_y);

	return ret;
}

static int ispscaler_port_store_pipeinfo_get(struct isp_scaler_port *port, struct isp_scaler_port_cfg *port_cfg)
{
	struct isp_pipe_info *pipe_in = NULL;
	uint32_t ret = 0, path_id = 0;

	path_id = isp_port_id_switch(port->port_id);
	if (path_id == ISP_SPATH_NUM) {
		pr_err("fail to get hw_path_id\n");
		return 0;
	}
	pipe_in = port_cfg->pipe_info;
	pipe_in->store[path_id].ctx_id = port_cfg->cfg_id;
	pipe_in->store[path_id].spath_id = path_id;
	ret = ispscaler_port_store_normal_get(port, &pipe_in->store[path_id]);
	if (ret) {
		pr_err("fail to get pipe store normal info\n");
		return -EFAULT;
	}

	pipe_in->scaler[path_id].ctx_id = port_cfg->cfg_id;
	pipe_in->scaler[path_id].spath_id = path_id;
	pipe_in->scaler[path_id].src.w = port_cfg->src_crop.size_x;
	pipe_in->scaler[path_id].src.h = port_cfg->src_crop.size_y;
	pipe_in->scaler[path_id].in_trim = port->trim;
	port->scaler_coeff_ex = port_cfg->scaler_coeff_ex;
	port->scaler_bypass_ctrl = port_cfg->scaler_bypass_ctrl;
	ret = ispscaler_port_hwinfo_get(port, &pipe_in->scaler[path_id]);
	if (ret) {
		pr_err("fail to get pipe path scaler info\n");
		return -EFAULT;
	}
	return ret;
}

static int ispscaler_thumbport_hwinfo_get(void *cfg_in, struct isp_hw_thumbscaler_info *scalerInfo)
{
	int ret = 0;
	uint32_t deci_w = 0;
	uint32_t deci_h = 0;
	uint32_t trim_w, trim_h, temp_w, temp_h;
	uint32_t offset, shift, is_yuv422 = 0;
	struct img_size src, dst;
	uint32_t align_size = 0;
	struct isp_scaler_port *in_ptr = NULL;

	if (!cfg_in || !scalerInfo) {
		pr_err("fail to get valid cfg_int %p, input ptr %p\n", cfg_in, scalerInfo);
		return -EFAULT;
	}
	in_ptr = (struct isp_scaler_port *)cfg_in;

	scalerInfo->scaler_bypass = 0;
	scalerInfo->frame_deci = 0;
	/* y factor & deci */
	src.w = in_ptr->trim.size_x;
	src.h = in_ptr->trim.size_y;
	dst = in_ptr->dst;
	ret = isp_drv_trim_deci_info_cal(src.w, dst.w, &temp_w, &deci_w);
	ret |= isp_drv_trim_deci_info_cal(src.h, dst.h, &temp_h, &deci_h);
	if (deci_w == 0 || deci_h == 0)
		return -EINVAL;
	if (ret) {
		pr_err("fail to set thumbscaler ydeci. src %d %d, dst %d %d\n",
					src.w, src.h, dst.w, dst.h);
		return ret;
	}

	scalerInfo->y_deci.deci_x = deci_w;
	scalerInfo->y_deci.deci_y = deci_h;
	if (deci_w > 1)
		scalerInfo->y_deci.deci_x_eb = 1;
	else
		scalerInfo->y_deci.deci_x_eb = 0;
	if (deci_h > 1)
		scalerInfo->y_deci.deci_y_eb = 1;
	else
		scalerInfo->y_deci.deci_y_eb = 0;
	align_size = deci_w * ISP_PIXEL_ALIGN_WIDTH;
	trim_w = (temp_w) & ~(align_size - 1);
	align_size = deci_h * ISP_PIXEL_ALIGN_HEIGHT;
	trim_h = (temp_h) & ~(align_size - 1);
	scalerInfo->y_factor_in.w = trim_w / deci_w;
	scalerInfo->y_factor_in.h = trim_h / deci_h;
	scalerInfo->y_factor_out = in_ptr->dst;

	if ((in_ptr->fmt == CAM_YUV422_2FRAME) || (in_ptr->fmt == CAM_YVU422_2FRAME))
		is_yuv422 = 1;

	/* uv factor & deci, input: yuv422(isp pipeline format) */
	shift = is_yuv422 ? 0 : 1;
	scalerInfo->uv_deci.deci_x = deci_w;
	scalerInfo->uv_deci.deci_y = deci_h;
	if (deci_w > 1)
		scalerInfo->uv_deci.deci_x_eb = 1;
	else
		scalerInfo->uv_deci.deci_x_eb = 0;
	if (deci_h > 1)
		scalerInfo->uv_deci.deci_y_eb = 1;
	else
		scalerInfo->uv_deci.deci_y_eb = 0;
	trim_w >>= 1;
	scalerInfo->uv_factor_in.w = trim_w / deci_w;
	scalerInfo->uv_factor_in.h = trim_h / deci_h;
	scalerInfo->uv_factor_out.w = dst.w / 2;
	scalerInfo->uv_factor_out.h = dst.h >> shift;

	scalerInfo->src0.w = in_ptr->trim.size_x;
	scalerInfo->src0.h = in_ptr->trim.size_y;

	/* y trim */
	trim_w = scalerInfo->y_factor_in.w * scalerInfo->y_deci.deci_x;
	offset = (in_ptr->trim.size_x - trim_w) / 2;
	scalerInfo->y_trim.start_x = in_ptr->trim.start_x + offset;
	scalerInfo->y_trim.size_x = trim_w;

	trim_h = scalerInfo->y_factor_in.h * scalerInfo->y_deci.deci_y;
	offset = (in_ptr->trim.size_y - trim_h) / 2;
	scalerInfo->y_trim.start_y = in_ptr->trim.start_y + offset;
	scalerInfo->y_trim.size_y = trim_h;

	scalerInfo->y_src_after_deci = scalerInfo->y_factor_in;
	scalerInfo->y_dst_after_scaler = scalerInfo->y_factor_out;

	/* uv trim */
	trim_w = scalerInfo->uv_factor_in.w * scalerInfo->uv_deci.deci_x;
	offset = (in_ptr->trim.size_x / 2 - trim_w) / 2;
	scalerInfo->uv_trim.start_x = in_ptr->trim.start_x / 2 + offset;
	scalerInfo->uv_trim.size_x = trim_w;

	trim_h = scalerInfo->uv_factor_in.h * scalerInfo->uv_deci.deci_y;
	offset = (in_ptr->trim.size_y - trim_h) / 2;
	scalerInfo->uv_trim.start_y = in_ptr->trim.start_y + offset;
	scalerInfo->uv_trim.size_y = trim_h;

	scalerInfo->uv_src_after_deci = scalerInfo->uv_factor_in;
	scalerInfo->uv_dst_after_scaler = scalerInfo->uv_factor_out;
	scalerInfo->odata_mode = is_yuv422 ? 0x00 : 0x01;

	scalerInfo->y_deci.deci_x = isp_drv_deci_factor_cal(scalerInfo->y_deci.deci_x);
	scalerInfo->y_deci.deci_y = isp_drv_deci_factor_cal(scalerInfo->y_deci.deci_y);
	scalerInfo->uv_deci.deci_x = isp_drv_deci_factor_cal(scalerInfo->uv_deci.deci_x);
	scalerInfo->uv_deci.deci_y = isp_drv_deci_factor_cal(scalerInfo->uv_deci.deci_y);

	/* N6pro thumbscaler calculation regulation */
	if (scalerInfo->thumbscl_cal_version == 1) {
		scalerInfo->y_init_phase.w = scalerInfo->y_dst_after_scaler.w / 2;
		scalerInfo->y_init_phase.h = scalerInfo->y_dst_after_scaler.h / 2;
		scalerInfo->uv_src_after_deci.w = scalerInfo->y_src_after_deci.w / 2;
		scalerInfo->uv_src_after_deci.h = scalerInfo->y_src_after_deci.h;
		scalerInfo->uv_dst_after_scaler.w = scalerInfo->y_dst_after_scaler.w / 2;
		scalerInfo->uv_dst_after_scaler.h = scalerInfo->y_dst_after_scaler.h / 2;
		scalerInfo->uv_trim.size_x = scalerInfo->y_trim.size_x / 2;
		scalerInfo->uv_trim.size_y = scalerInfo->y_trim.size_y / 2;
		scalerInfo->uv_init_phase.w = scalerInfo->uv_dst_after_scaler.w / 2;
		scalerInfo->uv_init_phase.h = scalerInfo->uv_dst_after_scaler.h / 2;
		scalerInfo->uv_factor_in.w = scalerInfo->y_factor_in.w / 2;
		scalerInfo->uv_factor_in.h = scalerInfo->y_factor_in.h / 2;
		scalerInfo->uv_factor_out.w = scalerInfo->y_factor_out.w / 2;
		scalerInfo->uv_factor_out.h = scalerInfo->y_factor_out.h / 2;
	}

	pr_debug("deciY %d %d, Yfactor (%d %d) => (%d %d) ytrim (%d %d %d %d)\n",
		scalerInfo->y_deci.deci_x, scalerInfo->y_deci.deci_y,
		scalerInfo->y_factor_in.w, scalerInfo->y_factor_in.h,
		scalerInfo->y_factor_out.w, scalerInfo->y_factor_out.h,
		scalerInfo->y_trim.start_x, scalerInfo->y_trim.start_y,
		scalerInfo->y_trim.size_x, scalerInfo->y_trim.size_y);
	pr_debug("deciU %d %d, Ufactor (%d %d) => (%d %d), Utrim (%d %d %d %d)\n",
		scalerInfo->uv_deci.deci_x, scalerInfo->uv_deci.deci_y,
		scalerInfo->uv_factor_in.w, scalerInfo->uv_factor_in.h,
		scalerInfo->uv_factor_out.w, scalerInfo->uv_factor_out.h,
		scalerInfo->uv_trim.start_x, scalerInfo->uv_trim.start_y,
		scalerInfo->uv_trim.size_x, scalerInfo->uv_trim.size_y);

	pr_debug("my frameY: %d %d %d %d\n",
		scalerInfo->y_src_after_deci.w, scalerInfo->y_src_after_deci.h,
		scalerInfo->y_dst_after_scaler.w,
		scalerInfo->y_dst_after_scaler.h);
	pr_debug("my frameU: %d %d %d %d\n",
		scalerInfo->uv_src_after_deci.w,
		scalerInfo->uv_src_after_deci.h,
		scalerInfo->uv_dst_after_scaler.w,
		scalerInfo->uv_dst_after_scaler.h);

	pr_debug("init_phase: Y(%d %d), UV(%d %d)\n",
		scalerInfo->y_init_phase.w, scalerInfo->y_init_phase.h,
		scalerInfo->uv_init_phase.w, scalerInfo->uv_init_phase.h);

	return ret;
}

static int ispscaler_thumbport_pipeinfo_get(struct isp_scaler_port *port, struct isp_scaler_port_cfg *port_cfg)
{
	struct isp_pipe_info *pipe_in = NULL;
	uint32_t ret = 0, path_id = 0;

	path_id = isp_port_id_switch(port->port_id);
	if (path_id == ISP_SPATH_NUM) {
		pr_err("fail to get hw_path_id\n");
		return 0;
	}
	pipe_in = port_cfg->pipe_info;
	pipe_in->store[path_id].ctx_id = port_cfg->cfg_id;
	pipe_in->store[path_id].spath_id = path_id;
	ret = ispscaler_port_store_normal_get(port, &pipe_in->store[path_id]);
	if (ret) {
		pr_err("fail to get pipe store normal info\n");
		return -EFAULT;
	}

	pipe_in->thumb_scaler.idx = port_cfg->cfg_id;
	/*need to confirm*/
	if (port_cfg->thumb_scaler_cal_version)
		pipe_in->thumb_scaler.thumbscl_cal_version = 1;
	ret = ispscaler_thumbport_hwinfo_get(port, &pipe_in->thumb_scaler);
	if (ret) {
		pr_err("fail to get pipe thumb scaler info\n");
		return -EFAULT;
	}
	return 0;
}

static int ispscaler_port_pipeinfo_get(struct isp_scaler_port *port, void *param)
{
	int ret = 0;
	struct isp_scaler_port_cfg *port_cfg = NULL;

	port_cfg = VOID_PTR_TO(param, struct isp_scaler_port_cfg);
	if (port->type == PORT_TRANSFER_IN) {
		ret = ispscaler_port_fetch_pipeinfo_get(port, port_cfg);
	}

	if (port->type == PORT_TRANSFER_OUT && atomic_read(&port->user_cnt) >= 1) {
		switch (port->port_id) {
		case PORT_PRE_ISP_YUV_SCALER_OUT:
		case PORT_VID_ISP_YUV_SCALER_OUT:
		case PORT_CAP_ISP_YUV_SCALER_OUT:
			ret = ispscaler_port_store_pipeinfo_get(port, port_cfg);
			break;
		case PORT_THUMB_ISP_YUV_SCALER_OUT:
			ret = ispscaler_thumbport_pipeinfo_get(port, port_cfg);
			break;
		default:
			pr_err("fail to get port_id:%d or type:%d\n", port->port_id, port->type);
		}
	}
	return ret;
}

static int ispscaler_port_size_update(struct isp_scaler_port *port, void *param)
{
	int ret = 0;
	struct isp_scaler_port_cfg *port_cfg = NULL;
	struct cam_frame *pframe = NULL;
	struct cam_zoom_base zoom_base = {0};
	struct cam_zoom_index zoom_index = {0};

	port_cfg = VOID_PTR_TO(param, struct isp_scaler_port_cfg);
	pframe = port_cfg->src_frame;
	if (pframe) {
		zoom_index.node_type = CAM_NODE_TYPE_ISP_YUV_SCALER;
		zoom_index.node_id = port_cfg->node_id;
		zoom_index.port_type = port->type;
		zoom_index.port_id = port->port_id;
		zoom_index.zoom_data = &pframe->common.zoom_data;
		ret = cam_zoom_frame_base_get(&zoom_base, &zoom_index);
		if (!ret) {
			port->size = zoom_base.dst;
			port->trim = zoom_base.crop;
		}

		CAM_ZOOM_DEBUG("frame %d ret %d type %d id %d size %d %d trim %d %d %d %d\n", pframe->common.fid,
			ret, port->type, port->port_id, port->size.w, port->size.h, port->trim.start_x,
			port->trim.start_y, port->trim.size_x, port->trim.size_y);
	}

	return ret;
}

static int ispscaler_port_cfg_callback(void *param, uint32_t cmd, void *handle)
{
	int ret = 0;
	struct isp_scaler_port *port = NULL;

	port = VOID_PTR_TO(handle, struct isp_scaler_port);
	switch (cmd) {
	case ISP_SCALER_PORT_SIZE_UPDATE:
		ret = ispscaler_port_size_update(port, param);
		break;
	case ISP_SCALER_PORT_PIPEINFO_GET:
		ret = ispscaler_port_pipeinfo_get(port, param);
		break;
	case ISP_SCALER_PORT_FRAME_CYCLE:
		ret = ispscaler_port_frame_cycle(port, param);
		break;
	default:
		pr_err("fail to support port type %d\n", cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

uint32_t isp_scaler_port_id_switch(uint32_t port_id)
{
	uint32_t hw_path_id = ISP_SPATH_NUM;
	switch (port_id) {
	case PORT_PRE_OUT:
	case PORT_CAP_OUT:
		hw_path_id = ISP_SPATH_CP;
		break;
	case PORT_VID_OUT:
		hw_path_id = ISP_SPATH_VID;
		break;
	case PORT_THUMB_OUT:
		hw_path_id = ISP_SPATH_FD;
		break;
	default:
		pr_err("port_id:%d\n",port_id);
	}
	return hw_path_id;
}

void *isp_scaler_port_get(uint32_t port_id, struct isp_scaler_port_desc *param)
{
	struct isp_scaler_port *port = NULL;

	if (!param) {
		pr_err("fail to get valid param\n");
		return NULL;
	}

	pr_info("port id %d  node_dev %p\n", port_id, *param->port_dev);
	if (*param->port_dev == NULL) {
		port = cam_buf_kernel_sys_vzalloc(sizeof(struct isp_scaler_port));
		if (!port) {
			pr_err("fail to get valid isp port %d\n", port_id);
			return NULL;
		}
	} else {
		port = *param->port_dev;
		pr_info("isp port has been alloc %p %d\n", port, port_id);
		goto exit;
	}
	CAM_QUEUE_INIT(&port->result_queue, ISP_RESULT_Q_LEN, ispscaler_port_frame_ret);
	CAM_QUEUE_INIT(&port->out_buf_queue, ISP_OUT_BUF_Q_LEN, ispscaler_port_frame_ret);

	port->resbuf_get_cb = param->resbuf_get_cb;
	port->resbuf_cb_data = param->resbuf_cb_data;
	port->data_cb_handle = param->data_cb_handle;
	port->data_cb_func = param->data_cb_func;
	port->port_cfg_cb_func = ispscaler_port_cfg_callback;
	port->type = param->transfer_type;
	port->port_id = port_id;
	if (param->transfer_type == PORT_TRANSFER_IN) {
		port->fmt = param->in_fmt;
		port->bayer_pattern = param->bayer_pattern;
	} else {
		port->fmt = param->out_fmt;
		port->dst = param->output_size;
		port->regular_mode = param->regular_mode;
		port->data_endian = param->endian;
	}

	*param->port_dev = port;
exit:
	atomic_inc(&port->user_cnt);
	return port;
}

void isp_scaler_port_put(struct isp_scaler_port *port)
{
	if (!port) {
		pr_err("fail to get invalid port ptr\n");
		return;
	}

	if (atomic_dec_return(&port->user_cnt) == 0) {
		CAM_QUEUE_CLEAN(&port->out_buf_queue, struct cam_frame, list);
		CAM_QUEUE_CLEAN(&port->result_queue, struct cam_frame, list);
		port->resbuf_get_cb = NULL;
		port->data_cb_func = NULL;
		port->port_cfg_cb_func = NULL;
		cam_buf_kernel_sys_vfree(port);
		port = NULL;
		pr_info("isp port free success\n");
	}
}

