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

#include "cam_hw.h"
#include "cam_buf_manager.h"
#include "isp_fmcu.h"
#include "isp_reg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_FMCU %d: %d %s:" fmt, current->pid, __LINE__, __func__

static int ispfmcu_cmd_debug(struct isp_fmcu_ctx_desc *fmcu_ctx)
{
	uint32_t i = 0, ret = 0, cmd_num = 0;
	unsigned long addr = 0;

	if (!fmcu_ctx) {
		pr_err("fail to get valid ptr\n");
		return -EFAULT;
	}

	addr = (unsigned long)fmcu_ctx->cmd_buf[fmcu_ctx->cur_buf_id];
	cmd_num = (uint32_t)fmcu_ctx->cmdq_pos[fmcu_ctx->cur_buf_id] / 2;
	pr_debug("fmcu %d  cmd num %d\n", (int)fmcu_ctx->fid, cmd_num);

	for (i = 0; i <= cmd_num; i += 2) {
		pr_debug("a:0x%08x c: 0x%08x | a:0x%08x c: 0x%08x\n",
			*(uint32_t *)(addr + 4),
			*(uint32_t *)(addr),
			*(uint32_t *)(addr + 12),
			*(uint32_t *)(addr + 8));
		addr += 16;
	}
	return ret;
}

static int ispfmcu_cmd_push(void *handle, uint32_t addr, uint32_t cmd)
{
	int ret = 0;
	uint32_t *ptr = NULL;
	struct isp_fmcu_ctx_desc *fmcu_ctx = NULL;

	if (!handle) {
		pr_err("fail to get fmcu_ctx pointer\n");
		return -EFAULT;
	}

	fmcu_ctx = (struct isp_fmcu_ctx_desc *)handle;
	if (fmcu_ctx->cmdq_pos[fmcu_ctx->cur_buf_id]
		>= (fmcu_ctx->cmdq_size / sizeof(uint32_t))) {
		pr_err("fail to get fmcu%d cmdq, overflow.\n", fmcu_ctx->fid);
		return -EFAULT;
	}

	ptr = fmcu_ctx->cmd_buf[fmcu_ctx->cur_buf_id]
				+ fmcu_ctx->cmdq_pos[fmcu_ctx->cur_buf_id];
	*ptr++ = cmd;
	*ptr++ = addr;
	fmcu_ctx->cmdq_pos[fmcu_ctx->cur_buf_id] += 2;

	return ret;
}

static int ispfmcu_start(void *handle)
{
	int ret = 0, cmd_num = 0;
	struct isp_hw_fmcu_start startarg;
	struct isp_fmcu_ctx_desc *fmcu_ctx = NULL;

	if (!handle) {
		pr_err("fail to get fmcu_ctx pointer\n");
		return -EFAULT;
	}

	fmcu_ctx = (struct isp_fmcu_ctx_desc *)handle;
	if (fmcu_ctx->cmdq_pos[fmcu_ctx->cur_buf_id]
		>= (fmcu_ctx->cmdq_size / sizeof(uint32_t))) {
		pr_err("fail to get fmcu%d cmdq, overflow.\n", fmcu_ctx->fid);
		return -EFAULT;
	}

	spin_lock(&fmcu_ctx->lock);
	cmd_num = (int) fmcu_ctx->cmdq_pos[fmcu_ctx->cur_buf_id] / 2;
	ispfmcu_cmd_debug(fmcu_ctx);

	startarg.fid = fmcu_ctx->fid;
	startarg.hw_addr = fmcu_ctx->hw_addr[fmcu_ctx->cur_buf_id];
	startarg.cmd_num = cmd_num;
	fmcu_ctx->hw->isp_ioctl(fmcu_ctx->hw, ISP_HW_CFG_FMCU_START, &startarg);
	pr_debug("fmcu%d start done, cmdq len %d\n", fmcu_ctx->fid,
		(uint32_t)fmcu_ctx->cmdq_pos[fmcu_ctx->cur_buf_id] * 4);

	fmcu_ctx->cur_buf_id = !(fmcu_ctx->cur_buf_id);
	spin_unlock(&fmcu_ctx->lock);
	return ret;
}

static int ispfmcu_ctx_reset(void *handle)
{
	int ret = 0;
	struct isp_fmcu_ctx_desc *fmcu_ctx = NULL;

	if (!handle) {
		pr_err("fail to get fmcu_ctx pointer\n");
		return -EFAULT;
	}

	fmcu_ctx = (struct isp_fmcu_ctx_desc *)handle;
	fmcu_ctx->cmdq_pos[fmcu_ctx->cur_buf_id] = 0;
	memset(fmcu_ctx->cmd_buf[fmcu_ctx->cur_buf_id], 0, fmcu_ctx->cmdq_size);

	pr_debug("Done\n");
	return ret;
}

static int ispfmcu_ctx_init(void *handle)
{
	int ret = 0, i = 0, iommu_enable = 0;
	struct camera_buf *ion_buf = NULL;
	struct isp_fmcu_ctx_desc *fmcu_ctx = NULL;

	if (!handle) {
		pr_err("fail to get fmcu_ctx pointer\n");
		return -EFAULT;
	}

	fmcu_ctx = (struct isp_fmcu_ctx_desc *)handle;
	fmcu_ctx->cmdq_size = ISP_FMCU_CMDQ_SIZE;
	fmcu_ctx->lock = __SPIN_LOCK_UNLOCKED(&fmcu_ctx->lock);

	/*alloc cmd queue buffer*/
	for (i = 0; i < MAX_BUF; i++) {
		ion_buf = &fmcu_ctx->ion_pool[i];
		memset(ion_buf, 0, sizeof(fmcu_ctx->ion_pool[i]));

		if (cam_buf_iommu_status_get(CAM_BUF_IOMMUDEV_ISP) == 0) {
			pr_debug("isp iommu enable\n");
			iommu_enable = 1;
		} else {
			pr_debug("isp iommu disable\n");
			iommu_enable = 0;
		}
		ret = cam_buf_alloc(ion_buf, fmcu_ctx->cmdq_size, iommu_enable);
		if (ret) {
			pr_err("fail to get fmcu buffer\n");
			ret = -EFAULT;
			goto err_alloc_fmcu;
		}
	}
	for (i = 0; i < MAX_BUF; i++) {
		ion_buf = &fmcu_ctx->ion_pool[i];
		ret = cam_buf_manager_buf_status_cfg(ion_buf, CAM_BUF_STATUS_GET_IOVA_K_ADDR, CAM_BUF_IOMMUDEV_ISP);
		if (ret) {
			pr_err("fail to map fmcu buffer\n");
			ret = -EFAULT;
			goto err_map_fmcu;
		}
		fmcu_ctx->cmd_buf[i] = (uint32_t *)ion_buf->addr_k;
		fmcu_ctx->hw_addr[i] = ion_buf->iova[CAM_BUF_IOMMUDEV_ISP];
		fmcu_ctx->cmdq_pos[i] = 0;
		pr_info("fmcu%d cmd buf hw_addr:0x%lx, sw_addr:%p, size:%zd\n",
			i, fmcu_ctx->hw_addr[i], fmcu_ctx->cmd_buf[i],
			ion_buf->size);
	}

	return 0;
err_map_fmcu:
	for (i = 0; i < MAX_BUF; i++) {
		ion_buf = &fmcu_ctx->ion_pool[i];
		if (ion_buf)
			cam_buf_manager_buf_status_cfg(ion_buf, CAM_BUF_STATUS_PUT_IOVA_K_ADDR, CAM_BUF_IOMMUDEV_ISP);
	}
err_alloc_fmcu:
	for (i = 0; i < MAX_BUF; i++) {
		ion_buf = &fmcu_ctx->ion_pool[i];
		if (ion_buf)
			cam_buf_free(ion_buf);
	}
	pr_err("fail to init fmcu%d.\n", fmcu_ctx->fid);
	return ret;
}

static int ispfmcu_ctx_deinit(void *handle)
{
	int ret = 0, i = 0;
	struct camera_buf *ion_buf = NULL;
	struct isp_fmcu_ctx_desc *fmcu_ctx = NULL;

	if (!handle) {
		pr_err("fail to get fmcu_ctx pointer\n");
		return -EFAULT;
	}

	fmcu_ctx = (struct isp_fmcu_ctx_desc *)handle;
	for (i = 0; i < MAX_BUF; i++) {
		ion_buf = &fmcu_ctx->ion_pool[i];
		cam_buf_manager_buf_status_cfg(ion_buf, CAM_BUF_STATUS_MOVE_TO_ALLOC, CAM_BUF_IOMMUDEV_ISP);
		cam_buf_free(ion_buf);
		ion_buf = NULL;
	}

	pr_debug("Done\n");
	return ret;
}

struct isp_fmcu_ops fmcu_ops = {
	.ctx_init = ispfmcu_ctx_init,
	.ctx_deinit = ispfmcu_ctx_deinit,
	.ctx_reset = ispfmcu_ctx_reset,
	.push_cmdq = ispfmcu_cmd_push,
	.hw_start = ispfmcu_start,
};

struct isp_fmcu_ctx_desc s_fmcu_desc[ISP_FMCU_NUM] = {
	{
		.fid = ISP_FMCU_0,
		.ops = &fmcu_ops,
		.cur_buf_id = PING,
	},
	{
		.fid = ISP_FMCU_REC,
		.ops = &fmcu_ops,
		.cur_buf_id = PING,
	},
	{
		.fid = ISP_FMCU_1,
		.ops = &fmcu_ops,
		.cur_buf_id = PING,
	},
	{
		.fid = ISP_FMCU_DEC,
		.ops = &fmcu_ops,
		.cur_buf_id = PING,
	},
};

struct isp_fmcu_ctx_desc *isp_fmcu_dec_ctx_get(void *arg)
{
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct cam_hw_info *hw = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return NULL;
	}

	hw = (struct cam_hw_info *)arg;
	if (atomic_inc_return(&s_fmcu_desc[ISP_FMCU_DEC].user_cnt) == 1)
		fmcu = &s_fmcu_desc[ISP_FMCU_DEC];

	return fmcu;
}

struct isp_fmcu_ctx_desc *isp_fmcu_ctx_desc_get(void *arg, uint32_t hw_idx)
{
	int i;
	uint32_t fmcu_id;
	struct isp_fmcu_ctx_desc *fmcu = NULL;
	struct cam_hw_info *hw = NULL;
	struct isp_hw_fmcu_sel fmcu_sel = {0};

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return NULL;
	}

	hw = (struct cam_hw_info *)arg;
	for (i = 0; i < ISP_FMCU_NUM; i++) {
		if (atomic_inc_return(&s_fmcu_desc[i].user_cnt) == 1) {
			fmcu_id = s_fmcu_desc[i].fid;
			fmcu_sel.fmcu_id = fmcu_id;
			fmcu_sel.hw_idx = hw_idx;
			if (hw->isp_ioctl(hw, ISP_HW_CFG_FMCU_VALID_GET, &fmcu_sel)) {
				fmcu = &s_fmcu_desc[i];
				pr_info("fmcu %d , %p\n", fmcu->fid, fmcu);
				break;
			}
		}
		atomic_dec(&s_fmcu_desc[i].user_cnt);
	}

	return fmcu;
}

int isp_fmcu_ctx_desc_put(struct isp_fmcu_ctx_desc *fmcu)
{
	int i;

	pr_info("fmcu %d. %p\n", fmcu->fid, fmcu);
	for (i = 0; i < ISP_FMCU_NUM; i++) {
		if (fmcu == &s_fmcu_desc[i]) {
			fmcu = NULL;
			atomic_dec(&s_fmcu_desc[i].user_cnt);
			break;
		}
	}

	if (fmcu != NULL)
		pr_err("fail to match original ptr %p\n", fmcu);

	return 0;
}
