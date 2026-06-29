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

#include "dcam_core.h"
#include "dcam_dummy.h"
#include "dcam_int_common.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_CORE: %d %d %s : " fmt, current->pid, __LINE__, __func__

static DEFINE_MUTEX(s_dcam_dev_mutex);

static void dcamcore_get_fmcu(struct dcam_hw_context *pctx_hw)
{
	int ret = 0, hw_ctx_id = 0;
	struct cam_hw_info *hw = NULL;
	struct dcam_fmcu_ctx_desc *fmcu_info = NULL;

	if (!pctx_hw) {
		pr_err("fail to get valid input ptr\n");
		return;
	}

	hw = pctx_hw->hw;
	hw_ctx_id = pctx_hw->hw_ctx_id;

	if(hw->ip_dcam[hw_ctx_id]->dcamhw_abt && hw->ip_dcam[hw_ctx_id]->dcamhw_abt->fmcu_support) {
		fmcu_info = dcam_fmcu_ctx_desc_get(hw, hw_ctx_id);
		if (fmcu_info && fmcu_info->ops) {
			ret = fmcu_info->ops->ctx_init(fmcu_info);
			if (ret) {
				pr_err("fail to init fmcu ctx\n");
				dcam_fmcu_ctx_desc_put(fmcu_info);
				pctx_hw->fmcu = NULL;
			} else
				pctx_hw->fmcu = fmcu_info;
		} else
			pr_debug("no more fmcu or ops\n");
	}
}

static void dcamcore_put_fmcu(struct dcam_hw_context *pctx_hw)
{
	struct dcam_fmcu_ctx_desc *fmcu_info = NULL;

	if (!pctx_hw) {
		pr_err("fail to get valid input ptr\n");
		return;
	}

	fmcu_info = (struct dcam_fmcu_ctx_desc *)pctx_hw->fmcu;
	if (fmcu_info) {
		fmcu_info->ops->ctx_deinit(fmcu_info);
		dcam_fmcu_ctx_desc_put(fmcu_info);
		pctx_hw->fmcu = NULL;
	}
}

static void dcamcore_get_dummy_slave(struct dcam_hw_context *pctx_hw)
{
	struct dcam_dummy_slave *dummy_slave = NULL;
	uint32_t hw_ctx_id = 0;

	if (!pctx_hw) {
		pr_err("fail to get valid input ptr\n");
		return;
	}

	hw_ctx_id = pctx_hw->hw_ctx_id;
	if(pctx_hw->hw->ip_dcam[hw_ctx_id]->dcamhw_abt && pctx_hw->hw->ip_dcam[hw_ctx_id]->dcamhw_abt->dummy_slave_support) {
		dummy_slave = dcam_dummy_ctx_desc_get(pctx_hw->hw, DCAM_DUMMY_0);
		if (dummy_slave && dummy_slave->dummy_ops) {
			pctx_hw->dummy_slave = dummy_slave;
			dummy_slave->hw_ctx[hw_ctx_id] = pctx_hw;
		} else
			pr_debug("no more dummy or ops\n");
	}
}

static void dcamcore_put_dummy_slave(struct dcam_hw_context *pctx_hw)
{
	struct dcam_dummy_slave *dummy_slave = NULL;

	if (!pctx_hw) {
		pr_err("fail to get valid input ptr\n");
		return;
	}
	dummy_slave = (struct dcam_dummy_slave *)pctx_hw->dummy_slave;
	if (dummy_slave) {
		dcam_dummy_ctx_desc_put(dummy_slave);
		pctx_hw->dummy_slave = NULL;
	}
}

static int dcamcore_dummy_config(void *ctx, void *para)
{
	struct dcam_dummy_slave *dummy_slave = NULL;
	struct dcam_hw_context *dcam_hw_ctx = NULL;
	struct dcam_dummy_param *dummy_param = NULL;
	uint32_t param = 0;

	dcam_hw_ctx = VOID_PTR_TO(ctx, struct dcam_hw_context);
	dummy_param = VOID_PTR_TO(para, struct dcam_dummy_param);
	dummy_slave = dcam_hw_ctx->dummy_slave;
	param = DCAM_DUMMY_MODE_HW_AUTO;
	dummy_slave->dummy_ops->cfg_param(dummy_slave, DCAM_DUMMY_CFG_HW_MODE, &param);
	param = dcam_hw_ctx->hw->ip_dcam[dcam_hw_ctx->hw_ctx_id]->dcamhw_abt->dummy_slave_support;
	dummy_slave->dummy_ops->cfg_param(dummy_slave, DCAM_DUMMY_CFG_SW_MODE, &param);
	param = DCAM_DUMMY_SLAVE_SKIP_NUM;
	dummy_slave->dummy_ops->cfg_param(dummy_slave, DCAM_DUMMY_CFG_SKIP_NUM, &param);
	dummy_slave->dummy_ops->cfg_param(dummy_slave, DCAM_DUMMY_CFG_RESERVED_BUF, dummy_param);
	dummy_param->hw_ctx_id = dcam_hw_ctx->hw_ctx_id;
	dummy_slave->dummy_ops->dummy_enable(dummy_slave, dummy_param);
	return 0;
}

static int dcamcore_context_init(struct dcam_pipe_dev *dev)
{
	int i = 0, ret = 0;
	struct dcam_hw_context *pctx_hw = NULL;

	if (!dev) {
		pr_err("fail to get valid input ptr\n");
		return -EINVAL;
	}

	pr_debug("dcam hw contexts init start!\n");
	memset(&dev->hw_ctx[0], 0, sizeof(dev->hw_ctx));
	for (i = 0; i < DCAM_HW_CONTEXT_MAX; i++) {
		pctx_hw = &dev->hw_ctx[i];
		pctx_hw->hw_ctx_id = i;
		pctx_hw->node_id = 0xFFFFFFFF;
		pctx_hw->node_type = CAM_NODE_TYPE_MAX;
		pctx_hw->node = NULL;
		pctx_hw->fid = 0;
		pctx_hw->hw = dev->hw;
		atomic_set(&pctx_hw->user_cnt, 0);
		atomic_set(&pctx_hw->shadow_done_cnt, 0);
		atomic_set(&pctx_hw->shadow_config_cnt, 0);
		spin_lock_init(&pctx_hw->glb_reg_lock);
		spin_lock_init(&pctx_hw->fbc_lock);

		pr_debug("register irq for dcam %d. irq_no %d\n", i, dev->hw->ip_dcam[i]->irq_no);
		ret = dcam_int_common_irq_request(&dev->hw->pdev->dev, dev->hw->ip_dcam[i]->irq_no, pctx_hw);
		if (ret)
			pr_err("fail to register irq for hw_ctx %d\n", i);
		dcam_int_irq_init(pctx_hw);

		dcamcore_get_fmcu(pctx_hw);
		dcamcore_get_dummy_slave(pctx_hw);
	}

	pr_debug("done!\n");
	return ret;
}

static int dcamcore_context_deinit(struct dcam_pipe_dev *dev)
{
	int ret = 0;
	int i = 0;
	struct dcam_hw_context *pctx_hw = NULL;

	pr_debug("enter.\n");
	if (!dev) {
		pr_err("fail to get valid input ptr\n");
		return -EINVAL;
	}

	for (i = 0; i < DCAM_HW_CONTEXT_MAX; i++) {
		pctx_hw = &dev->hw_ctx[i];
		if (pctx_hw)
			cam_debug_logfile_deinit(&pctx_hw->debug.block_param_log);
		dcamcore_put_dummy_slave(pctx_hw);
		dcamcore_put_fmcu(pctx_hw);
		dcam_int_common_irq_free(&dev->hw->pdev->dev, pctx_hw);
		atomic_set(&pctx_hw->user_cnt, 0);
	}
	pr_debug("dcam contexts deinit done!\n");
	return ret;
}

static int dcamcore_dev_open(void *dcam_handle)
{
	int ret = 0;
	int hw_ctx_id = DCAM_HW_CONTEXT_0;
	struct dcam_pipe_dev *dev = (struct dcam_pipe_dev *)dcam_handle;

	pr_info("enter.\n");
	if (!dcam_handle) {
		pr_err("fail to get valid input ptr, isp_handle %p\n", dcam_handle);
		return -EFAULT;
	}

	mutex_lock(&dev->ctx_mutex);
	if (atomic_inc_return(&dev->enable) == 1) {
		dev->hw->dcam_ioctl(dev->hw, hw_ctx_id, DCAM_HW_CFG_INIT_AXI, &hw_ctx_id);

		ret = dcamcore_context_init(dev);
		if (ret) {
			pr_err("fail to init dcam context.\n");
			ret = -EFAULT;
			goto exit;
		}

		mutex_init(&dev->path_mutex);
		spin_lock_init(&dev->ctx_lock);
	}
	mutex_unlock(&dev->ctx_mutex);
	pr_info("open dcam pipe dev done! enable %d\n",atomic_read(&dev->enable));
	return 0;

exit:
	atomic_dec(&dev->enable);
	mutex_unlock(&dev->ctx_mutex);
	pr_err("fail to open dcam dev!\n");
	return ret;
}

static int dcamcore_dev_close(void *dcam_handle)
{
	int ret = 0;
	struct dcam_pipe_dev *dev = (struct dcam_pipe_dev *)dcam_handle;

	if (!dcam_handle) {
		pr_err("fail to get valid input ptr\n");
		return -EINVAL;
	}
	mutex_lock(&dev->ctx_mutex);
	if (atomic_dec_return(&dev->enable) == 0) {
		mutex_destroy(&dev->path_mutex);
		ret = dcamcore_context_deinit(dev);
	}
	mutex_unlock(&dev->ctx_mutex);
	pr_info("dcam dev disable done enable %d\n",atomic_read(&dev->enable));
	return ret;

}

static void dcamcore_dev_recovery(void *dcam_handle)
{
	int recovery_id = DCAM_HW_CONTEXT_0, i = 0;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *hw_ctx = NULL;

	dev = (struct dcam_pipe_dev *)dcam_handle;
	dev->hw->dcam_ioctl(dev->hw, recovery_id, DCAM_HW_CFG_ALL_RESET, &recovery_id);
	cam_buf_iommu_restore(CAM_BUF_IOMMUDEV_DCAM);
	for (i = 0; i < DCAM_HW_CONTEXT_MAX; i++) {
		hw_ctx = &dev->hw_ctx[i];
		if (hw_ctx->is_offline_proc)
			dcam_core_offline_reset(hw_ctx);
	}
}

static int dcamcore_ctx_bind(void *dev_handle, void *node, uint32_t node_id,
		uint32_t csi_controller_idx, uint32_t slw_cnt, uint32_t *hw_id, uint32_t *slw_type)
{
	int i = 0;
	uint32_t hw_ctx_id = DCAM_HW_CONTEXT_BIND_MAX, mode = 0;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *pctx_hw = NULL;

	dev = (struct dcam_pipe_dev *)dev_handle;
	mode = dev->hw->csi_connect_type;
	if (mode == DCAM_BIND_FIXED) {
		pctx_hw = &dev->hw_ctx[csi_controller_idx];
		if (node_id == pctx_hw->node_id && pctx_hw->node == node) {
			pr_err("fail to node %d & hw %d are already binded, cnt=%d\n",
				node_id, csi_controller_idx, atomic_read(&pctx_hw->user_cnt));
			return -1;
		}
		if (atomic_inc_return(&pctx_hw->user_cnt) == 1) {
			hw_ctx_id = pctx_hw->hw_ctx_id;
			goto exit;
		}
		atomic_dec(&pctx_hw->user_cnt);
	} else if (mode == DCAM_BIND_DYNAMIC) {
		for (i = 0; i < DCAM_HW_CONTEXT_BIND_MAX; i++) {
			pctx_hw = &dev->hw_ctx[i];
			if (node_id == pctx_hw->node_id && pctx_hw->node == node) {
				atomic_inc(&pctx_hw->user_cnt);
				pr_info("node %d & hw %d are already binded, cnt=%d\n",
							node_id, i, atomic_read(&pctx_hw->user_cnt));
				return 0;
			}
		}
		for (i = 0; i < DCAM_HW_CONTEXT_BIND_MAX; i++) {
			pctx_hw = &dev->hw_ctx[i];
			if (atomic_inc_return(&pctx_hw->user_cnt) == 1) {
				hw_ctx_id = pctx_hw->hw_ctx_id;
				if (slw_cnt && dev->hw->ip_dcam[i]->dcamhw_abt && dev->hw->ip_dcam[i]->dcamhw_abt->fmcu_support) {
					*slw_type = DCAM_SLW_FMCU;
					if (!pctx_hw->fmcu)
						continue;
				}
				goto exit;
			}
			atomic_dec(&pctx_hw->user_cnt);
		}
	}
exit:
	if (hw_ctx_id == DCAM_HW_CONTEXT_BIND_MAX) {
		pr_warn_ratelimited("warning: get hw_ctx_id fail. mode=%d\n", mode);
		return -1;
	}
	pctx_hw->node = node;
	pctx_hw->node_id = node_id;
	memset(pctx_hw->frame_addr, 0, sizeof(uint32_t) * DCAM_ADDR_RECORD_FRAME_NUM * DCAM_RECORD_PORT_INFO_MAX);
	for (i = DCAM_PATH_FULL; i < DCAM_PATH_MAX; i++)
		atomic_set(&pctx_hw->path_done[i], 0);
	*hw_id = hw_ctx_id;
	pr_info("node_id=%d, hw_ctx_id=%d, mode=%d, cnt=%d\n", pctx_hw->node_id, hw_ctx_id, mode, atomic_read(&pctx_hw->user_cnt));
	return 0;
}

static int dcamcore_ctx_unbind(void *dev_handle, void *node, uint32_t node_id)
{
	int i = 0, cnt = 0, loop = 0;
	int hw_ctx_id = DCAM_HW_CONTEXT_MAX;
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_hw_context *pctx_hw = NULL;

	if (!dev_handle || !node) {
		pr_err("fail to get valid in ptr %p %p\n", dev_handle, node);
		return -EFAULT;
	}

	dev = (struct dcam_pipe_dev *)dev_handle;
	for (i = 0; i < DCAM_HW_CONTEXT_MAX; i++) {
		pctx_hw = &dev->hw_ctx[i];
		if ((node_id == pctx_hw->node_id) && (node == pctx_hw->node)) {
			hw_ctx_id = pctx_hw->hw_ctx_id;
			break;
		}
	}

	if (hw_ctx_id < 0) {
		pr_err("fail to binding sw ctx %d to any hw ctx\n", node_id);
		return -EINVAL;
	}

	for (i = 0; i < DCAM_HW_CONTEXT_MAX; i++) {
		pctx_hw = &dev->hw_ctx[i];
		if ((node_id != pctx_hw->node_id) || (node != pctx_hw->node))
			continue;

		if (atomic_dec_return(&pctx_hw->user_cnt) == 0) {
			pr_info("node_id=%d, hw_id=%d unbind success\n", node_id, pctx_hw->hw_ctx_id);
			if (!pctx_hw->is_offline_proc) {
				while (pctx_hw->in_irq_proc && loop < 2000) {
					pr_debug("ctx % in irq. wait %d\n", pctx_hw->hw_ctx_id, loop);
					loop++;
					os_adapt_time_udelay(500);
				};
				if (loop == 2000)
					pr_warn("warning: dcam node unind wait irq timeout\n");
			}
			pctx_hw->node = NULL;
			pctx_hw->node_id = 0xFFFFFFFF;
			for (i = DCAM_PATH_FULL; i < DCAM_PATH_MAX; i++)
				atomic_set(&pctx_hw->path_done[i], 0);
			goto exit;
		} else {
			pr_err("fail to cnt set 0, node id=%d, hw_id=%d, cnt=%d\n",
				node_id, pctx_hw->hw_ctx_id, cnt);
			return -1;
		}
	}

exit:
	return 0;
}

/*
 * Operations for this dcam_pipe_ops.
 */
static struct dcam_pipe_ops s_dcam_pipe_ops = {
	.open = dcamcore_dev_open,
	.close = dcamcore_dev_close,
	.bind = dcamcore_ctx_bind,
	.unbind = dcamcore_ctx_unbind,
	.dummy_cfg = dcamcore_dummy_config,
	.recovery = dcamcore_dev_recovery,
};

inline void dcam_core_offline_reset(struct dcam_hw_context *hw_ctx)
{
	struct dcam_irq_proc_desc irq_desc = {0};

	if (hw_ctx->is_offline_proc && hw_ctx->dcam_irq_cb_func) {
		irq_desc.dcam_cb_type = CAM_CB_DCAM_DEV_ERR;
		hw_ctx->dcam_irq_cb_func(&irq_desc, hw_ctx->dcam_irq_cb_handle);
	}
}

void dcam_core_offline_irq_proc(struct dcam_hw_context *dcam_hw_ctx,
		struct dcam_irq_info *irq_info)
{
	int i = 0, ret = 0;
	uint32_t irq_status = 0, dummy_status = 0, path_id = 0;
	struct dcam_irq_proc_desc irq_desc = {0};

	if (!dcam_hw_ctx || !irq_info) {
		pr_err("fail to get invalid hw_ctx %p %p\n", dcam_hw_ctx, irq_info);
		return;
	}

	pr_info("dcam offline node do irq proc\n");
	/* unbind alway be called in irq proc for offline proc
	 * no need to wait irq proc done */
	irq_status = irq_info->status;
	if (dcam_hw_ctx->dummy_slave) {
		dummy_status = atomic_read(&dcam_hw_ctx->dummy_slave->status);
		if (dummy_status == DCAM_DUMMY_TRIGGER || dummy_status == DCAM_DUMMY_DONE)
			return;
	}
	for (i = 0; i < irq_info->irq_num; i++) {
		if (irq_status & BIT(i)) {
			ret = dcam_int_irq_desc_get(i, &irq_desc);
			if (ret) {
				pr_warn("Warning: not to get valid irq desc %d\n", i);
				irq_status &= ~BIT(i);
				continue;
			}
			path_id = dcamoffline_portid_convert_to_pathid(irq_desc.dcam_port_id);
			if (path_id >= DCAM_PATH_MAX) {
				irq_status &= ~BIT(i);
				continue;
			}
			atomic_inc(&dcam_hw_ctx->path_done[path_id]);
			if (dcam_hw_ctx->dcam_irq_cb_func)
				dcam_hw_ctx->dcam_irq_cb_func(&irq_desc, dcam_hw_ctx->dcam_irq_cb_handle);
		}
		irq_status &= ~BIT(i);
		if (!irq_status)
			break;
	}
}

void *dcam_core_pipe_dev_get(struct cam_hw_info *hw, void *s_dcam_dev)
{
	struct dcam_pipe_dev *dev = NULL;
	struct dcam_pipe_dev **s_dcam = (struct dcam_pipe_dev **)s_dcam_dev;

	mutex_lock(&s_dcam_dev_mutex);

	if (*s_dcam) {
		atomic_inc(&(*s_dcam)->user_cnt);
		dev = *s_dcam;
		pr_info("s_dcam_dev_new is already exist=%p, user_cnt=%d",
			*s_dcam, atomic_read(&(*s_dcam)->user_cnt));
		goto exit;
	}

	dev = cam_buf_kernel_sys_vzalloc(sizeof(struct dcam_pipe_dev));
	if (!dev)
		goto exit;

	atomic_set(&dev->user_cnt, 1);
	atomic_set(&dev->enable, 0);
	mutex_init(&dev->ctx_mutex);

	dev->dcam_pipe_ops = &s_dcam_pipe_ops;
	dev->hw = hw;
	hw->s_dcam_dev_debug = dev;
	*s_dcam = dev;
	s_dcam_dev = (void *)(*s_dcam);
	if (dev)
		pr_info("get dcam pipe dev: %p\n", dev);
exit:
	mutex_unlock(&s_dcam_dev_mutex);

	return dev;
}

int dcam_core_pipe_dev_put(void *dcam_handle, void *s_dcam_dev)
{
	int ret = 0;
	struct dcam_pipe_dev *dev = (struct dcam_pipe_dev *)dcam_handle;
	struct dcam_pipe_dev **s_dcam = (struct dcam_pipe_dev **)s_dcam_dev;

	if (!dev) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	pr_info("put dcam pipe dev:%p, s_dcam_dev:%p, users: %d, enable: %d\n",
		dev, *s_dcam, atomic_read(&dev->user_cnt), atomic_read(&dev->enable));

	mutex_lock(&s_dcam_dev_mutex);

	if (dev != *s_dcam) {
		mutex_unlock(&s_dcam_dev_mutex);
		pr_err("fail to match dev: %p, %p\n", dev, *s_dcam);
		return -EINVAL;
	}

	if (atomic_dec_return(&dev->user_cnt) == 0) {
		pr_info("free dcam pipe dev %p\n", dev);
		mutex_destroy(&dev->ctx_mutex);
		cam_buf_kernel_sys_vfree(dev);
		dev = NULL;
		*s_dcam = NULL;
	}
	mutex_unlock(&s_dcam_dev_mutex);

	if (dev)
		pr_info("put dcam pipe dev: %p\n", dev);

	return ret;
}
