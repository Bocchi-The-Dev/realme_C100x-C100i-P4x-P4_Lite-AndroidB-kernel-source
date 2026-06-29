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

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "cam_trusty.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "CAM_CA: %d %d %s : "\
	fmt, current->pid, __LINE__, __func__

#include <linux/device.h>
#include <linux/trusty/trusty_ipc.h>

#define CAM_TA_PORT_NAME		"com.android.trusty.camera"

static struct cam_ca_ctrl cam_ca = {TIPC_CHANNEL_DISCONNECTED, 0, };

struct tipc_msg_buf *cam_ca_handle_msg(void *data,
				struct tipc_msg_buf *rxbuf, u16 flag)
{
	struct cam_ca_ctrl *ca = data;
	struct tipc_msg_buf *newbuf = rxbuf;

	mutex_lock(&ca->rlock);
	if (ca->chanel_state == TIPC_CHANNEL_CONNECTED) {
		/* get new buffer */
		newbuf = tipc_chan_get_rxbuf(ca->chan);
		if (!IS_ERR(newbuf)) {
			unsigned long flag = 0;

			pr_info("received new data, rxbuf %p, newbuf %p\n",
				rxbuf, newbuf);
			/* queue an old buffer and return a new one */
			spin_lock_irqsave(&ca->queue_lock, flag);
			list_add_tail(&rxbuf->node, &ca->rx_msg_queue);
			spin_unlock_irqrestore(&ca->queue_lock, flag);

			wake_up_interruptible(&ca->readq);
		} else {
			/*
			 * return an old buffer effectively discarding
			 * incoming message
			 */
			pr_info("discard incoming message\n");

			newbuf = rxbuf;
		}
	}
	mutex_unlock(&ca->rlock);

	return newbuf;
}

static void cam_ca_handle_event(void *data, int event)
{
	struct cam_ca_ctrl *ca = data;

	switch (event) {
	case TIPC_CHANNEL_SHUTDOWN:
		pr_info("channel shutdown\n");
		break;

	case TIPC_CHANNEL_DISCONNECTED:
		pr_info("channel disconnected\n");
		ca->chanel_state = TIPC_CHANNEL_DISCONNECTED;
		break;

	case TIPC_CHANNEL_CONNECTED:
		pr_info("channel connected\n");
		ca->chanel_state = TIPC_CHANNEL_CONNECTED;
		wake_up_interruptible(&ca->readq);
		break;

	default:
		pr_err("fail to handle unknown event type %d\n", event);
		break;
	}
}

static struct tipc_chan_ops cam_ca_ops = {
	.handle_msg = cam_ca_handle_msg,
	.handle_event = cam_ca_handle_event,
};

static void cam_ca_free_msg_buf_list(struct list_head *list)
{
	struct tipc_msg_buf *mb = NULL;

	mb = list_first_entry_or_null(list, struct tipc_msg_buf, node);
	while (mb) {
		list_del(&mb->node);

		free_pages_exact(mb->buf_va, mb->buf_sz);
		kfree(mb);

		mb = list_first_entry_or_null(list, struct tipc_msg_buf, node);
	}
}

int cam_trusty_connect(void)
{
	struct cam_ca_ctrl *ca = &cam_ca;
	int chan_conn_ret = 0;
	int ret = 0;

	pr_info("chanel_state =%d\n", ca->chanel_state);

	if (ca->chanel_state == TIPC_CHANNEL_CONNECTED) {
		pr_info("cam has already been connected\n");
		return true;
	}

	if (!ca->con_init) {
		struct tipc_chan *chan;

		chan = tipc_create_channel(NULL, &cam_ca_ops, ca);
		if (IS_ERR(chan)) {
			pr_err("fail to create channel\n");
			return PTR_ERR(chan);
		}
		ca->chan = chan;
		mutex_init(&ca->rlock);
		mutex_init(&ca->wlock);
		init_waitqueue_head(&ca->readq);
		spin_lock_init(&ca->queue_lock);
		INIT_LIST_HEAD(&ca->rx_msg_queue);
		ca->con_init = true;
	}

	chan_conn_ret = tipc_chan_connect(ca->chan, CAM_TA_PORT_NAME);
	if (chan_conn_ret) {
		pr_err("fail to connect channel\n");
		return -EFAULT;
	}

	pr_info("cam connect channel done\n");

	if (!wait_event_interruptible_timeout(ca->readq,
			(ca->chanel_state == TIPC_CHANNEL_CONNECTED),
			msecs_to_jiffies(CA_CONN_TIMEOUT))) {
		pr_err("fail to wait read response, time out!\n");
		ret = -EFAULT;
	}

	pr_info("ret =%d\n", ret);

	return ret;
}

void cam_trusty_disconnect(void)
{
	struct cam_ca_ctrl *ca = &cam_ca;
	unsigned long flag = 0;

	wake_up_interruptible_all(&ca->readq);

	spin_lock_irqsave(&ca->queue_lock, flag);
	cam_ca_free_msg_buf_list(&ca->rx_msg_queue);
	spin_unlock_irqrestore(&ca->queue_lock, flag);
	ca->con_init = false;

	/* shutdown channel  */
	tipc_chan_shutdown(ca->chan);
	ca->chanel_state = TIPC_CHANNEL_DISCONNECTED;
	/* and destroy it */
	tipc_chan_destroy(ca->chan);

	pr_info("cam_trusty ca disconnect\n");
}

static int cam_ca_write(void *buf, size_t len)
{
	int ret = 0;
	int avail;
	struct tipc_msg_buf *txbuf = NULL;
	struct cam_ca_ctrl *ca = &cam_ca;
	int msg_ret = 0;

	pr_info("cam_trusty ca write enter\n");

	if (ca->chanel_state != TIPC_CHANNEL_CONNECTED)
		return -EFAULT;

	txbuf = tipc_chan_get_txbuf_timeout(ca->chan, GET_TXBUF_TIMEOUT);
	if (IS_ERR(txbuf))
		return PTR_ERR(txbuf);

	avail = mb_avail_space(txbuf);
	if (len > avail) {
		pr_err("fail to get buffer space, len = %d, avail = %d\n",
			(int)len, (int)avail);
		ret = -EMSGSIZE;
		goto err;
	}

	memcpy(mb_put_data(txbuf, len), buf, len);

	msg_ret = tipc_chan_queue_msg(ca->chan, txbuf);
	if (msg_ret)
		goto err;

	return ret;

err:
	tipc_chan_put_txbuf(ca->chan, txbuf);
	return ret;
}

ssize_t cam_ca_read(void *buf, size_t max_len)
{
	struct tipc_msg_buf *mb;
	ssize_t	len = 0;
	struct cam_ca_ctrl *ca = &cam_ca;
	unsigned long flag = 0;
	int ret = 0;

	pr_info("ca read enter\n");

	if (ca->chanel_state != TIPC_CHANNEL_CONNECTED)
		return -EFAULT;

	ret = wait_event_interruptible_timeout(ca->readq, !list_empty(&ca->rx_msg_queue), msecs_to_jiffies(CA_READ_TIMEOUT));
	if (ret <= 0) {
		pr_err("fail to wait read response, time out! ret = %d\n", ret);
		return -ETIMEDOUT;
	}

	spin_lock_irqsave(&ca->queue_lock, flag);
	mb = list_first_entry(&ca->rx_msg_queue, struct tipc_msg_buf, node);

	len = mb_avail_data(mb);
	if (len > max_len)
		len = max_len;

	memcpy(buf, mb_get_data(mb, len), len);

	list_del(&mb->node);
	spin_unlock_irqrestore(&ca->queue_lock, flag);
	tipc_chan_put_rxbuf(ca->chan, mb);

	return len;
}

static int cam_ca_wait_ack(uint32_t cmd)
{
	ssize_t size = 0;
	struct ack_message ack_msg = {0};

	pr_info("wait ack enter\n");

	size = cam_ca_read(&ack_msg, sizeof(struct ack_message));

	if (size != sizeof(struct ack_message)) {
		pr_err("fail to get remote response size, size= %zd\n", size);
		return -EFAULT;
	}

	if (ack_msg.cmd != (cmd | TA_RESP_BIT)) {
		pr_err("fail to get remote ack_msg.cmd, ack_msg.cmd=0x%x, cmd=0x%x\n",
			ack_msg.cmd, cmd);
		return -EFAULT;
	}

	if (ack_msg.ack ==  TA_CMD_ERR) {
		pr_err("fail to get remote ack_msg.ack, ack=0x%x\n", ack_msg.ack);
		return -EFAULT;
	}

	return 0;
}

int cam_trusty_isp_store_pitch_set(uint32_t y_pitch, uint32_t u_pitch, uint32_t v_pitch)
{
	struct img_pitch_reg_msg store_pitch_set = {0};
	int ret = 0;
	struct cam_ca_ctrl *ca = &cam_ca;

	pr_info("cam_trusty store pitch enter\n");

	mutex_lock(&ca->wlock);

	store_pitch_set.msg_cmd = TA_IMG_ISP_STORE_PITCH_SET;

	store_pitch_set.y_pitch = y_pitch;
	store_pitch_set.u_pitch = u_pitch;
	store_pitch_set.v_pitch = v_pitch;

	ret = cam_ca_write(&store_pitch_set, sizeof(struct img_pitch_reg_msg));
	if (ret) {
		pr_err("fail to write cam_ca, ret = %d\n", ret);
		mutex_unlock(&ca->wlock);
		return ret;
	}

	ret = cam_ca_wait_ack(store_pitch_set.msg_cmd);
	mutex_unlock(&ca->wlock);

	pr_info("exit, ret =%d\n", ret);
	return ret;
}

int cam_trusty_isp_store_set(unsigned long y_addr, unsigned long u_addr,
	unsigned long v_addr)
{
	struct img_yuv_reg_msg yuv_store_addr_set = {0};
	int ret = 0;
	struct cam_ca_ctrl *ca = &cam_ca;

	pr_info("ca set isp store addr\n");

	mutex_lock(&ca->wlock);

	yuv_store_addr_set.msg_cmd = TA_IMG_ISP_STORE_ADDR_SET;

	yuv_store_addr_set.y_addr = y_addr;
	yuv_store_addr_set.u_addr = u_addr;
	yuv_store_addr_set.v_addr = v_addr;

	ret = cam_ca_write(&yuv_store_addr_set, sizeof(struct img_yuv_reg_msg));
	if (ret) {
		pr_err("fail to write cam_ca, ret =%d\n", ret);
		mutex_unlock(&ca->wlock);
		return ret;
	}

	ret = cam_ca_wait_ack(yuv_store_addr_set.msg_cmd);
	mutex_unlock(&ca->wlock);

	pr_info("ca set isp store addr ret =%d\n", ret);
	return ret;
}

int cam_trusty_isp_fetch_addr_set(unsigned long y_addr, unsigned long u_addr,
	unsigned long v_addr)
{
	struct img_yuv_reg_msg yuv_addr_set = {0};
	int ret = 0;
	struct cam_ca_ctrl *ca = &cam_ca;

	pr_info("ca set isp fetch addr enter\n");

	mutex_lock(&ca->wlock);

	yuv_addr_set.msg_cmd = TA_IMG_ISP_YUV_ADDR_SET;

	yuv_addr_set.y_addr = y_addr;
	yuv_addr_set.u_addr = u_addr;
	yuv_addr_set.v_addr = v_addr;

	ret = cam_ca_write(&yuv_addr_set, sizeof(struct img_yuv_reg_msg));
	if (ret) {
		pr_err("fail to write cam_ca, ret =%d\n", ret);
		mutex_unlock(&ca->wlock);
		return ret;
	}

	ret = cam_ca_wait_ack(yuv_addr_set.msg_cmd);
	mutex_unlock(&ca->wlock);

	pr_info("ca set isp fetch addr ret =%d\n", ret);
	return ret;

}

int cam_trusty_isp_pitch_set(uint32_t y_pitch, uint32_t u_pitch, uint32_t v_pitch)
{
	struct img_pitch_reg_msg pitch_set = {0};
	int ret = 0;
	struct cam_ca_ctrl *ca = &cam_ca;

	pr_info("ca isp_fetch pitch enter\n");

	mutex_lock(&ca->wlock);

	pitch_set.msg_cmd = TA_IMG_ISP_PITCH_SET;

	pitch_set.y_pitch = y_pitch;
	pitch_set.u_pitch = u_pitch;
	pitch_set.v_pitch = v_pitch;

	ret = cam_ca_write(&pitch_set, sizeof(struct img_pitch_reg_msg));
	if (ret) {
		pr_err("fail to write cam_ca, ret = %d\n", ret);
		mutex_unlock(&ca->wlock);
		return ret;
	}

	ret = cam_ca_wait_ack(pitch_set.msg_cmd);
	mutex_unlock(&ca->wlock);

	pr_info("exit, ret =%d\n", ret);
	return ret;
}

int cam_trusty_csi_switch_ctrl_set(uint32_t csi_sel_ctrl)
{
	struct csi_switch_ctrl_msg switch_ctrl = {0};
	int ret = 0;
	struct cam_ca_ctrl *ca = &cam_ca;

	pr_info("enter\n");

	mutex_lock(&ca->wlock);
	switch_ctrl.msg_cmd = TA_IMG_CSI_SWICH_CTRL_SET;

	switch_ctrl.csi_sel_ctrl = csi_sel_ctrl;

	ret = cam_ca_write(&switch_ctrl, sizeof(struct csi_switch_ctrl_msg));
	if (ret) {
		pr_err("fail to write cam_ca ret =%d\n", ret);
		mutex_unlock(&ca->wlock);
		return ret;
	}

	ret = cam_ca_wait_ack(switch_ctrl.msg_cmd);
	mutex_unlock(&ca->wlock);

	pr_info("ret =%d\n", ret);
	return ret;

}


static int camca_enter_tamode(struct sprd_cam_sec_cfg *camsec_cfg)
{
	int ret = 0;
	struct faceid_cfg_msg faceid_msg = {0};
	struct cam_ca_ctrl *ca = &cam_ca;

	pr_info("ca chanel_state =%d, camsec_mode=%d, work_mode=%d\n",
		ca->chanel_state, camsec_cfg->camsec_mode,
		camsec_cfg->work_mode);

	if (ca->chanel_state != TIPC_CHANNEL_CONNECTED) {
		pr_err("fail to enter ta mode, con err\n");
		return -EFAULT;
	}

	mutex_lock(&ca->wlock);
	faceid_msg.msg_cmd = TA_FACEID_ENTER_TEMODE;
	faceid_msg.faceid_version = FACEID_VERSION;
	faceid_msg.sec_cfg.work_mode = camsec_cfg->work_mode;
	faceid_msg.sec_cfg.camsec_mode = camsec_cfg->camsec_mode;

	ret = cam_ca_write(&faceid_msg, sizeof(struct faceid_cfg_msg));
	if (ret) {
		pr_err("fail to write cam_ca ret =%d\n", ret);
		mutex_unlock(&ca->wlock);
		return ret;
	}

	ret = cam_ca_wait_ack(faceid_msg.msg_cmd);

	mutex_unlock(&ca->wlock);

	pr_info("ret =%d\n", ret);

	return ret;

}

static int camca_exit_tamode(struct sprd_cam_sec_cfg *camsec_cfg)
{
	int ret = 0;
	struct cam_ca_ctrl *ca = &cam_ca;
	struct faceid_cfg_msg faceid_msg = {0};

	pr_info("ca chanel_state =%d, camsec_mode=%d, work_mode=%d\n",
		ca->chanel_state, camsec_cfg->camsec_mode, camsec_cfg->work_mode);

	mutex_lock(&ca->wlock);
	faceid_msg.msg_cmd = TA_FACEID_EXIT_TEMODE;
	faceid_msg.faceid_version = FACEID_VERSION;
	faceid_msg.sec_cfg.work_mode = camsec_cfg->work_mode;
	faceid_msg.sec_cfg.camsec_mode = camsec_cfg->camsec_mode;

	ret = cam_ca_write(&faceid_msg, sizeof(struct faceid_cfg_msg));
	if (ret) {
		pr_err("fail to write cam_ca ret =%d\n", ret);
		mutex_unlock(&ca->wlock);
		return ret;
	}

	ret = cam_ca_wait_ack(faceid_msg.msg_cmd);
	mutex_unlock(&ca->wlock);

	pr_info("ret =%d\n", ret);

	return ret;

}

int cam_trusty_security_set(struct sprd_cam_sec_cfg *camsec_cfg,
	enum cam_trusty_mode mode)
{
	int ret = 0;

	pr_info("camca security set enter\n");

	switch (mode) {
	case CAM_TRUSTY_ENTER:
		ret = camca_enter_tamode(camsec_cfg);
		break;
	case CAM_TRUSTY_EXIT:
		ret = camca_exit_tamode(camsec_cfg);
		break;
	default:
		pr_err("fail to get valid mode %d\n", mode);
		break;
	}

	pr_info("camca security set, ret=%d\n",  ret);
	return ret;
}

