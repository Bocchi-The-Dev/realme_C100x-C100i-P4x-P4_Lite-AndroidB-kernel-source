// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022-2023 Unisoc(Shanghai) Technologies Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "isp_cfg.h"
#include "isp_drv.h"
#include "isp_gtm.h"
#include "isp_int_common.h"
#include "isp_node.h"
#include "isp_reg.h"
#include "pyr_dec_node.h"
#include "cam_debugger.h"


#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "ISP_DEV: %d %d %s : " fmt, current->pid, __LINE__, __func__

unsigned long *isp_cfg_poll_addr[ISP_CONTEXT_SW_NUM];
static DEFINE_MUTEX(isp_pipe_dev_mutex);
uint32_t s_dbg_linebuf_len = ISP_LINE_BUFFER_W;

static int ispdev_sec_cfg(struct isp_pipe_dev *dev, void *param)
{
	int i = 0;
	struct cam_hw_info *hw = NULL;
	enum sprd_cam_sec_mode *sec_mode = (enum sprd_cam_sec_mode *)param;

	dev->sec_mode = *sec_mode;
	dev->wmode = ISP_AP_MODE;
	hw = dev->isp_hw;

	/* bypass config mode by default */
	hw->isp_ioctl(hw, ISP_HW_CFG_DEFAULT_PARA_SET, NULL);

	for (i = 0; i < ISP_CONTEXT_SW_NUM; i++)
		isp_cfg_poll_addr[i] = &s_isp_regbase[0];
	for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
		atomic_set(&dev->hw_ctx[i].user_cnt, 0);
		if (i != ISP_CONTEXT_HW_P0)
			atomic_set(&dev->hw_ctx[i].user_cnt, 1);
	}

	pr_debug("camca: isp sec_mode=%d\n", dev->sec_mode);

	return 0;
}

static int ispdev_context_init(struct isp_pipe_dev *dev)
{
	int ret = 0;
	int i, bind_fmcu;
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct isp_cfg_ctx_desc *cfg_desc = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	struct cam_hw_info *hw = NULL;

	pr_info("isp contexts init start!\n");
	/* CFG module init */
	if (dev->wmode == ISP_AP_MODE) {
		pr_info("isp ap mode.\n");
		for (i = 0; i < ISP_CONTEXT_SW_NUM; i++)
			isp_cfg_poll_addr[i] = &s_isp_regbase[0];

	} else {
		cfg_desc = isp_cfg_ctx_desc_get();
		if (!cfg_desc || !cfg_desc->ops) {
			pr_err("fail to get isp cfg ctx %p.\n", cfg_desc);
			ret = -EINVAL;
			goto cfg_null;
		}
		cfg_desc->hw = dev->isp_hw;
		pr_debug("cfg_init start.\n");

		ret = cfg_desc->ops->ctx_init(cfg_desc);
		if (ret) {
			pr_err("fail to cfg ctx init.\n");
			goto ctx_fail;
		}
		pr_debug("cfg_init done.\n");

		ret = cfg_desc->ops->hw_init(cfg_desc);
		if (ret)
			goto hw_fail;
	}
	dev->cfg_handle = cfg_desc;

	pr_info("isp hw contexts init start!\n");
	for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
		pctx_hw = &dev->hw_ctx[i];
		pctx_hw->hw_ctx_id = i;
		pctx_hw->node_id = 0xffff;
		atomic_set(&pctx_hw->user_cnt, 0);
		hw = dev->isp_hw;
		pctx_hw->hw = hw;
		hw->isp_ioctl(hw, ISP_HW_CFG_ENABLE_IRQ, &i);
		hw->isp_ioctl(hw, ISP_HW_CFG_CLEAR_IRQ, &i);
		pctx_hw->pipe_info.fetch_fbd_yuv.fetch_fbd_bypass = 1;
		pctx_hw->pipe_info.thumb_scaler.scaler_bypass = 1;
		bind_fmcu = 0;
		pctx_hw->postproc_func = NULL;
		init_completion(&pctx_hw->slice_done);
		if (unlikely(dev->wmode == ISP_AP_MODE)) {
			/* for AP mode, multi-context is not supported */
			if (i != ISP_CONTEXT_HW_P0) {
				atomic_set(&pctx_hw->user_cnt, 1);
				continue;
			}
			bind_fmcu = 1;
		} else if (*(hw->ip_isp->isphw_abt->ctx_fmcu_support + i)) {
			bind_fmcu = 1;
		}

		if (bind_fmcu) {
			fmcu = isp_fmcu_ctx_desc_get(hw, i);
			pr_debug("get fmcu %p\n", fmcu);

			if (fmcu && fmcu->ops) {
				fmcu->hw = dev->isp_hw;
				ret = fmcu->ops->ctx_init(fmcu);
				if (ret) {
					pr_err("fail to init fmcu ctx\n");
					isp_fmcu_ctx_desc_put(fmcu);
				} else {
					pctx_hw->fmcu_handle = fmcu;
				}
			} else {
				pr_info("no more fmcu or ops\n");
			}
		}
		pr_info("isp hw context %d init done. fmcu %p\n", i, pctx_hw->fmcu_handle);
	}

	pr_debug("done!\n");
	return 0;

hw_fail:
	cfg_desc->ops->ctx_deinit(cfg_desc);
ctx_fail:
	isp_cfg_ctx_desc_put(cfg_desc);
cfg_null:
	return ret;
}

static int ispdev_context_deinit(struct isp_pipe_dev *dev)
{
	int ret = 0;
	int i;
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct isp_cfg_ctx_desc *cfg_desc = NULL;
	struct isp_hw_context *pctx_hw = NULL;

	pr_debug("enter.\n");
	for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
		pctx_hw = &dev->hw_ctx[i];
		fmcu = (struct isp_fmcu_ctx_desc *)pctx_hw->fmcu_handle;
		if (fmcu) {
			fmcu->ops->ctx_deinit(fmcu);
			isp_fmcu_ctx_desc_put(fmcu);
		}
		pctx_hw->fmcu_handle = NULL;
		pctx_hw->fmcu_used = 0;
		if (pctx_hw->slice_ctx)
			isp_slice_ctx_put(&pctx_hw->slice_ctx);
		atomic_set(&pctx_hw->user_cnt, 0);
		isp_int_common_irq_hw_cnt_trace(i);
	}
	pr_info("isp contexts deinit done!\n");

	cfg_desc = (struct isp_cfg_ctx_desc *)dev->cfg_handle;
	if (cfg_desc) {
		cfg_desc->ops->ctx_deinit(cfg_desc);
		isp_cfg_ctx_desc_put(cfg_desc);
	}
	dev->cfg_handle = NULL;

	pr_debug("done.\n");
	return ret;
}

static int ispdev_open(void *isp_handle, void *param)
{
	int ret = 0;
	struct isp_pipe_dev *dev = NULL;
	struct cam_hw_info *hw = NULL;

	pr_debug("enter.\n");
	if (!isp_handle || !param) {
		pr_err("fail to get valid input ptr, isp_handle %p, param %p\n",
			isp_handle, param);
		return -EFAULT;
	}
	dev = (struct isp_pipe_dev *)isp_handle;
	hw = (struct cam_hw_info *)param;
	if (hw == NULL) {
		pr_err("fail to get valid hw\n");
		return -EFAULT;
	}

	mutex_lock(&dev->dev_mutex);
	if (atomic_inc_return(&dev->enable) == 1) {
		pr_info("open_start: sec_mode: %d, work mode: %d, line_buf_len: %d\n",
			dev->sec_mode, dev->wmode, g_camctrl.isp_linebuf_len);

		/* line_buffer_len for debug */
		if (s_dbg_linebuf_len > 0 &&
			s_dbg_linebuf_len <= ISP_LINE_BUFFER_W)
			g_camctrl.isp_linebuf_len = s_dbg_linebuf_len;
		else
			g_camctrl.isp_linebuf_len = ISP_LINE_BUFFER_W;

		if (dev->sec_mode == SEC_TIME_PRIORITY)
			dev->wmode = ISP_AP_MODE;
		else
			dev->wmode = ISP_CFG_MODE;
		g_camctrl.isp_wmode = dev->wmode;

		dev->isp_hw = hw;
		mutex_init(&dev->path_mutex);
		mutex_init(&dev->dev_lock);
		mutex_init(&dev->pyr_mulshare_dec_lock);
		mutex_init(&dev->pyr_mulshare_rec_lock);
		spin_lock_init(&dev->ctx_lock);
		spin_lock_init(&hw->isp_cfg_lock);

		ret = isp_drv_hw_init(dev);
		atomic_set(&dev->pd_clk_rdy, 1);
		if (dev->pyr_dec_handle == NULL && hw->ip_isp->isphw_abt->pyr_dec_support) {
			dev->pyr_dec_handle = pyr_dec_dev_get(dev, hw);
			if (!dev->pyr_dec_handle) {
				pr_err("fail to get memory for dec_dev.\n");
				ret = -ENOMEM;
				goto err_init;
			}
		}
		ret = ispdev_context_init(dev);
		if (ret) {
			pr_err("fail to init isp context.\n");
			ret = -EFAULT;
			goto dec_err;
		}
	}
	mutex_unlock(&dev->dev_mutex);
	pr_info("open isp pipe dev done!\n");
	return 0;

dec_err:
	if (dev->pyr_dec_handle && hw->ip_isp->isphw_abt->pyr_dec_support) {
		pyr_dec_dev_put(dev->pyr_dec_handle);
		dev->pyr_dec_handle = NULL;
	}
err_init:
	if (atomic_dec_return(&dev->enable) == 0) {
		hw->isp_ioctl(hw, ISP_HW_CFG_STOP, NULL);
		atomic_set(&dev->pd_clk_rdy, 0);
		isp_drv_hw_deinit(dev);
		mutex_destroy(&dev->path_mutex);
		mutex_destroy(&dev->dev_lock);
		mutex_destroy(&dev->pyr_mulshare_dec_lock);
		mutex_destroy(&dev->pyr_mulshare_rec_lock);
	}
	mutex_unlock(&dev->dev_mutex);
	pr_err("fail to open isp dev!\n");
	return ret;
}

static int ispdev_close(void *isp_handle)
{
	int ret = 0;
	struct isp_pipe_dev *dev = NULL;
	struct cam_hw_info *hw = NULL;

	if (!isp_handle) {
		pr_err("fail to get valid input ptr\n");
		return -EINVAL;
	}

	dev = (struct isp_pipe_dev *)isp_handle;
	hw = dev->isp_hw;
	mutex_lock(&dev->dev_mutex);
	if (atomic_dec_return(&dev->enable) == 0 && atomic_read(&dev->pd_clk_rdy)) {
		ret = hw->isp_ioctl(hw, ISP_HW_CFG_STOP, NULL);
		ret = ispdev_context_deinit(dev);
		if (dev->pyr_dec_handle && hw->ip_isp->isphw_abt->pyr_dec_support) {
			pyr_dec_dev_put(dev->pyr_dec_handle);
			dev->pyr_dec_handle = NULL;
		}
		atomic_set(&dev->pd_clk_rdy, 0);
		ret = isp_drv_hw_deinit(dev);
		mutex_destroy(&dev->path_mutex);
		mutex_destroy(&dev->dev_lock);
		mutex_destroy(&dev->pyr_mulshare_dec_lock);
		mutex_destroy(&dev->pyr_mulshare_rec_lock);
	}

	mutex_unlock(&dev->dev_mutex);
	pr_info("isp dev disable done\n");
	return ret;

}

static int ispdev_reset(void *isp_handle, void *param)
{
	int ret = 0, i;
	uint32_t reset_flag = 0;
	struct isp_pipe_dev *dev = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_cfg_ctx_desc *cfg_desc = NULL;
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct isp_hw_context *pctx_hw = NULL;

	if (!isp_handle || !param) {
		pr_err("fail to get valid input ptr, isp_handle %p, param %p\n",
			isp_handle, param);
		return -EFAULT;
	}
	dev = (struct isp_pipe_dev *)isp_handle;
	hw = (struct cam_hw_info *)param;

	reset_flag = ISP_RESET_BEFORE_POWER_OFF;
	hw->isp_ioctl(hw, ISP_HW_CFG_RESET, &reset_flag);
	hw->isp_ioctl(hw, ISP_HW_CFG_DEFAULT_PARA_SET, NULL);

	cfg_desc = dev->cfg_handle;
	if (cfg_desc && cfg_desc->ops)
		ret = cfg_desc->ops->hw_init(cfg_desc);

	for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
		pctx_hw = &dev->hw_ctx[i];
		pctx_hw->hw_ctx_id = i;
		pctx_hw->node_id = 0xffff;
		atomic_set(&pctx_hw->user_cnt, 0);

		hw->isp_ioctl(hw, ISP_HW_CFG_CLEAR_IRQ, &i);
		hw->isp_ioctl(hw, ISP_HW_CFG_ENABLE_IRQ, &i);

		pr_debug("isp hw context %d init done. fmcu %p\n",
				i, pctx_hw->fmcu_handle);

		fmcu = (struct isp_fmcu_ctx_desc *)pctx_hw->fmcu_handle;
		if (fmcu) {
			struct isp_hw_fmcu_sel fmcu_sel = {0};

			fmcu_sel.fmcu_id = fmcu->fid;
			fmcu_sel.hw_idx = i;
			hw->isp_ioctl(hw, ISP_HW_CFG_FMCU_VALID_GET, &fmcu_sel);

		}
	}
	sprd_iommu_restore(&hw->soc_isp->pdev->dev);

	pr_debug("isp dev reset done\n");
	return ret;
}

static void *ispnode_context_bind(void *node, int slice_need, isp_irq_postproc postproc_func)
{
	int i = 0, m = 0, loop = 0, use_fmcu = 0;
	int hw_ctx_id = -1;
	unsigned long flag = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	struct isp_node *inode = NULL;

	if (!node) {
		pr_err("fail to get node");
		return NULL;
	}
	inode = (struct isp_node *)node;
	dev = inode->dev;
	spin_lock_irqsave(&dev->ctx_lock, flag);

	if (slice_need)
		use_fmcu = FMCU_IS_NEED;
	if (use_fmcu && (inode->uinfo.mode_3dnr != MODE_3DNR_OFF))
		use_fmcu |= FMCU_IS_MUST;
	if (inode->uinfo.enable_slowmotion)
		use_fmcu |= FMCU_IS_MUST;
	if (inode->uinfo.pyr_layer_num != 0)
		use_fmcu |= FMCU_IS_MUST;

	for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
		pctx_hw = &dev->hw_ctx[i];
		if (pctx_hw->node == inode) {
			atomic_inc(&pctx_hw->user_cnt);
			pr_debug("node %d & hw %d already binding, cnt=%d\n",
				inode->node_id, i, atomic_read(&pctx_hw->user_cnt));
			spin_unlock_irqrestore(&dev->ctx_lock, flag);
			return pctx_hw;
		}
	}

	loop = (use_fmcu & FMCU_IS_MUST) ? 1 : 2;
	for (m = 0; m < loop; m++) {
		for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
			pctx_hw = &dev->hw_ctx[i];
			if (m == 0) {
				/* first pass we just select fmcu/no-fmcu context */
				if ((!use_fmcu && pctx_hw->fmcu_handle) ||
					(use_fmcu && !pctx_hw->fmcu_handle))
					continue;
			}
			if (atomic_inc_return(&pctx_hw->user_cnt) == 1) {
				hw_ctx_id = pctx_hw->hw_ctx_id;
				goto exit;
			}
			atomic_dec(&pctx_hw->user_cnt);
		}
	}

exit:
	if (hw_ctx_id == -1) {
		spin_unlock_irqrestore(&dev->ctx_lock, flag);
		return NULL;
	}

	pctx_hw->node = inode;
	pctx_hw->node_id = inode->node_id;
	pctx_hw->cfg_id = inode->cfg_id;
	pctx_hw->postproc_func = postproc_func;
	inode->is_bind = 1;
	inode->pctx_hw_id = hw_ctx_id;
	pr_debug("sw %d, hw %d %d, fmcu_need %d ptr 0x%lx\n",
		inode->node_id, hw_ctx_id, pctx_hw->hw_ctx_id,
		use_fmcu, (unsigned long)pctx_hw->fmcu_handle);
	spin_unlock_irqrestore(&dev->ctx_lock, flag);

	return pctx_hw;
}

static uint32_t ispnode_context_unbind(void *node)
{
	int i, cnt;
	struct isp_pipe_dev *dev = NULL;
	struct isp_hw_context *pctx_hw = NULL;
	struct isp_node *inode = NULL;
	unsigned long flag = 0;

	inode = VOID_PTR_TO(node, struct isp_node);
	dev = inode->dev;
	spin_lock_irqsave(&dev->ctx_lock, flag);
	if (!inode->is_bind) {
		pr_debug("binding sw ctx %d to any hw ctx, is_bind:%d\n",
			inode->node_id, inode->is_bind);
		spin_unlock_irqrestore(&dev->ctx_lock, flag);
		return 0;
	}

	for (i = 0; i < ISP_CONTEXT_HW_NUM; i++) {
		pctx_hw = &dev->hw_ctx[i];
		if ((inode->node_id != pctx_hw->node_id) ||
			(inode != pctx_hw->node))
			continue;

		if (atomic_dec_return(&pctx_hw->user_cnt) == 0) {
			pr_debug("sw_id=%d, hw_id=%d unbind success node:%p\n",
				inode->node_id, pctx_hw->hw_ctx_id, inode);
			pctx_hw->node = NULL;
			pctx_hw->postproc_func = NULL;
			inode->is_bind = 0;
			inode->pctx_hw_id = ISP_CONTEXT_HW_NUM;
			goto exit;
		}

		cnt = atomic_read(&pctx_hw->user_cnt);
		if (cnt >= 1) {
			pr_debug("sw id=%d, hw_id=%d, cnt=%d\n",
				inode->node_id, pctx_hw->hw_ctx_id, cnt);
		} else {
			pr_debug("should not be here: sw id=%d, hw id=%d, cnt=%d node:%p\n",
				inode->node_id, pctx_hw->hw_ctx_id, cnt, inode);
			spin_unlock_irqrestore(&dev->ctx_lock, flag);
			return -EINVAL;
		}
	}

exit:
	spin_unlock_irqrestore(&dev->ctx_lock, flag);
	return 0;
}

static int ispdev_ioctl(void *isp_handle, enum isp_ioctrl_cmd cmd, void *param)
{
	int ret = 0, cfg_id = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_cfg_ctx_desc *cfg_desc = NULL;

	if (!isp_handle) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}
	dev = (struct isp_pipe_dev *)isp_handle;
	/*ispnode shelves only keep cfg_sec,init_node_hw, others move to node or port*/
	switch (cmd) {
	case ISP_IOCTL_CFG_SEC:
		ret = ispdev_sec_cfg(dev, param);
		break;
	case ISP_IOCTL_INIT_NODE_HW:
		if (dev->wmode == ISP_CFG_MODE) {
			cfg_desc = (struct isp_cfg_ctx_desc *)dev->cfg_handle;
			ret = cfg_desc->ops->ctx_get(cfg_desc);
			if (ret < 0) {
				pr_err("fail to get cfg ctx\n");
				break;
			}
			cfg_id = ret;
			cfg_desc->ops->ctx_reset(cfg_desc, cfg_id);
		}
		dev->isp_hw->isp_ioctl(dev->isp_hw, ISP_HW_CFG_DEFAULT_PARA_CFG, &cfg_id);
		*(uint32_t *)param = cfg_id;
		break;
	default:
		pr_err("fail to get known cmd: %d\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static struct isp_pipe_ops isp_ops = {
	.open = ispdev_open,
	.close = ispdev_close,
	.reset = ispdev_reset,
	.ioctl = ispdev_ioctl,
	.bind = ispnode_context_bind,
	.unbind = ispnode_context_unbind,
};

void *isp_core_pipe_dev_get(struct cam_hw_info *hw, void *s_isp_dev)
{
	struct isp_pipe_dev *dev = NULL;
	struct isp_pipe_dev **s_isp = (struct isp_pipe_dev **)s_isp_dev;

	mutex_lock(&isp_pipe_dev_mutex);

	if (*s_isp) {
		atomic_inc(&(*s_isp)->user_cnt);
		dev = *s_isp;
		pr_info("s_isp_dev is already exist=%p, user_cnt=%d",
			*s_isp, atomic_read(&(*s_isp)->user_cnt));
		goto exit;
	}

	dev = cam_buf_kernel_sys_vzalloc(sizeof(struct isp_pipe_dev));
	if (!dev)
		goto exit;

	atomic_set(&dev->user_cnt, 1);
	atomic_set(&dev->enable, 0);
	mutex_init(&dev->dev_mutex);
	init_completion(&dev->frm_done);
	complete(&dev->frm_done);
	dev->isp_ops = &isp_ops;

	hw->s_isp_dev_debug = dev;
	*s_isp = dev;
	s_isp_dev = (void *)(*s_isp);
	if (dev)
		pr_info("get isp pipe dev: %p\n", dev);
exit:
	mutex_unlock(&isp_pipe_dev_mutex);

	return dev;
}

int isp_core_pipe_dev_put(void *isp_handle, void *s_isp_dev)
{
	int ret = 0;
	struct isp_pipe_dev *dev = NULL;
	struct isp_pipe_dev **s_isp = (struct isp_pipe_dev **)s_isp_dev;

	if (!isp_handle) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	dev = (struct isp_pipe_dev *)isp_handle;
	pr_info("put isp pipe dev:%p, s_isp_dev:%p, users: %d\n",
		dev, *s_isp, atomic_read(&dev->user_cnt));

	mutex_lock(&isp_pipe_dev_mutex);

	if (dev != *s_isp) {
		mutex_unlock(&isp_pipe_dev_mutex);
		pr_err("fail to match dev: %p, %p\n", dev, *s_isp);
		return -EINVAL;
	}

	if (atomic_dec_return(&dev->user_cnt) == 0) {
		mutex_destroy(&dev->dev_mutex);
		atomic_set(&dev->pyr_mulshare_dec_alloced, 0);
		atomic_set(&dev->pyr_mulshare_rec_alloced, 0);
		pr_info("free isp pipe dev %p\n", dev);
		cam_buf_kernel_sys_vfree(dev);
		dev = NULL;
		*s_isp = NULL;
	}
	mutex_unlock(&isp_pipe_dev_mutex);

	if (dev)
		pr_info("put isp pipe dev: %p\n", dev);

	return ret;
}
