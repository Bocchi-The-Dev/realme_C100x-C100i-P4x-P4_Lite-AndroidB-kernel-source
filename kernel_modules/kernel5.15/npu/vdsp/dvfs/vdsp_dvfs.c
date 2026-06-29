/**
 * SPDX-FileContributor: vdsp dvfs function, dynamically adjust frequency voltage .
 */

#include <linux/hashtable.h>
#include <linux/kthread.h>
#include "dvfs_phy.h"
#include "vdsp_dvfs.h"
#include "vdsp_hw.h"
#include "xrp_internal.h"
#include "xvp_main.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-vdsp: [dvfs] %d: %d %s:" \
	fmt, current->pid, __LINE__, __func__


static vdsp_power_level convert_to_power_level(int32_t data)
{
	switch (data) {
	case 0:
		return VDSP_PWR_MIN;
	case 1:
		return VDSP_PWR_2;
	case 2:
		return VDSP_PWR_3;
	case 3:
		return VDSP_PWR_4;
	case 4:
		return VDSP_PWR_5;
	case 5:
		return VDSP_PWR_MAX;
	default:
		return VDSP_PWR_MAX;
	}
}

/*translate powerhint flag from user*/
static vdsp_powerhint_flag convert_to_powerhint_flag(uint32_t flag)
{
	switch (flag)
	{
	case 0:
		return POWERHINT_ENABLE;
	case 1:
		return POWERHINT_DISABLE;
	default:
		return POWERHINT_INVALID;
	}
}

static int dvfs_enable(void *hw_arg)
{
	struct dvfs_phy_desc *dvfs_phy = get_dvfs_phy_desc();

	if (!dvfs_phy->ops || !dvfs_phy->ops->enable) {
		pr_err("dvfs_phy: enable operation not supported");
		return -1;
	}
	return dvfs_phy->ops->enable(hw_arg);
}

static int dvfs_disable(void *hw_arg)
{
	struct dvfs_phy_desc *dvfs_phy = get_dvfs_phy_desc();

	if (!dvfs_phy->ops || !dvfs_phy->ops->disable) {
		pr_err("dvfs_phy: disable operation not supported");
		return -1;
	}
	return dvfs_phy->ops->disable(hw_arg);
}

static int dvfs_default_level(void)
{
	return 3;
}
static int dvfs_set(void *hw_arg, uint32_t level)
{
	struct dvfs_phy_desc *dvfs_phy = get_dvfs_phy_desc();

	if (!dvfs_phy->ops || !dvfs_phy->ops->setdvfs) {
		pr_err("dvfs_phy: setdvfs operation not supported");
		return -1;
	}
	return dvfs_phy->ops->setdvfs(hw_arg, level);
}

static int dvfs_set_voltage(void *hw_arg, uint32_t voltage)
{
	struct dvfs_phy_desc *dvfs_phy = get_dvfs_phy_desc();

	if (!dvfs_phy->ops || !dvfs_phy->ops->set_voltage) {
		pr_err("dvfs_phy: set_voltage operation not supported");
		return -1;
	}
	return dvfs_phy->ops->set_voltage(hw_arg, voltage);
}
static int dvfs_set_freq(void *hw_arg, uint32_t freq)
{
	struct dvfs_phy_desc *dvfs_phy = get_dvfs_phy_desc();

	if (!dvfs_phy->ops || !dvfs_phy->ops->set_freq) {
		pr_err("dvfs_phy: set_freq operation not supported");
		return -1;
	}
	return dvfs_phy->ops->set_freq(hw_arg, freq);
}

static int dvfs_powerhint_record(struct file *filp, vdsp_power_level level,
	vdsp_powerhint_flag flag)
{
	struct xvp_file *xvp_file = (struct xvp_file*)filp->private_data;

	if (flag == POWERHINT_ENABLE) {
		xvp_file->powerhint.record[level]++;
	}
	else if (flag == POWERHINT_DISABLE) {
		if (unlikely(xvp_file->powerhint.record[level] == 0)) {
			pr_warn("power hint record already 0, more disable\n");
		}
		else {
			xvp_file->powerhint.record[level]--;
		}
	}
	else {
		pr_err("invalid parameters:%d\n", flag);
		return -1;
	}
	pr_debug("flag:%d, level:%d, count:%d\n", flag, level, xvp_file->powerhint.record[level]);
	return 0;
}

static vdsp_power_level dvfs_max_record_power_level(struct file *filp)
{
	int32_t i;
	int find = 0;
	unsigned long bkt;
	struct xvp *xvp = ((struct xvp_file*)(filp->private_data))->xvp;
	struct xvp_file * tmp_xvpfile;
	struct xrp_known_file *p;
	vdsp_power_level max_level = VDSP_PWR_MIN;
	vdsp_power_level tmp_level;

	/*check every file*/
	mutex_lock(&xvp->xrp_known_files_lock);
	hash_for_each(xvp->xrp_known_files, bkt, p, node) {
		tmp_xvpfile = (struct xvp_file*)(((struct file*)(p->filp))->private_data);
		tmp_level = VDSP_PWR_MIN;
		for (i = VDSP_PWR_MAX; i >= VDSP_PWR_MIN; i--) {
			if (tmp_xvpfile->powerhint.record[i] != 0) {
				tmp_level = i;
				find = 1;
				break;
			}
		}
		if (tmp_level > max_level) {
			max_level = tmp_level;
		}
	}
	if (find == 0) {
		max_level = dvfs_default_level();
		pr_debug("no user setting, get default power level:%d\n", max_level);
	}
	mutex_unlock(&xvp->xrp_known_files_lock);

	pr_debug("max record power level:%d\n", max_level);
	return max_level;
}

static int dvfs_set_powerhint(void *data, int32_t power, uint32_t flag)
{
	int ret = 0;
	struct file *filp = (struct file*)data;
	struct xvp *xvp = ((struct xvp_file*)(filp->private_data))->xvp;
	vdsp_power_level user_level, max_level;
	vdsp_powerhint_flag user_flag;

	/* 1. user input data check and convert */
	pr_debug("[IN]power:%d, flag:%d\n", power, flag);
	user_level = convert_to_power_level(power);
	user_flag = convert_to_powerhint_flag(flag);
	if (unlikely(user_flag == POWERHINT_INVALID)){
		pr_err("invalid power hint flag[%d]\n", user_flag);
		return -1;
	}

	/* 2.select the max dvfs level to set */
	mutex_lock(&xvp->dvfs_info.dvfs_lock);
	dvfs_powerhint_record(filp, user_level, user_flag);
	max_level = dvfs_max_record_power_level(filp);
	pr_debug("en(0)/dis(1):%d, current user level:%d, max level:%d, last level:%d\n",
		flag, user_level, max_level, xvp->dvfs_info.last_level);
	if (xvp->dvfs_info.last_level != max_level) {
		ret = dvfs_set(xvp->hw_arg, max_level);
		if (ret == 0)
			xvp->dvfs_info.last_level = max_level;
	}
	mutex_unlock(&xvp->dvfs_info.dvfs_lock);

	return ret;
}

static int dvfs_release_powerhint(void *data)
{
	int ret = 0;
	int32_t i;
	vdsp_power_level max_level;
	struct file *filp = (struct file*) data;
	struct xvp_file *xvp_file = (struct xvp_file*)filp->private_data;
	struct xvp *xvp = ((struct xvp_file*)(filp->private_data))->xvp;

	mutex_lock(&(xvp->dvfs_info.dvfs_lock));
	/* 1 init current file*/
	for (i = 0; i < VDSP_PWR_NUM; i++)
		xvp_file->powerhint.record[i] = 0;

	/* 2 set other file, max reocrd power */
	max_level = dvfs_max_record_power_level(filp);
	if (max_level != xvp->dvfs_info.last_level) {
		ret = dvfs_set(xvp->hw_arg, max_level);
		if (ret == 0)
			xvp->dvfs_info.last_level = max_level;
	}
	mutex_unlock(&(xvp->dvfs_info.dvfs_lock));
	return ret;
}

static int dvfs_init(void *data)
{
	struct xvp* xvp = (struct xvp*)data;

	mutex_init(&xvp->dvfs_info.dvfs_lock);
	xvp->dvfs_info.last_level = dvfs_default_level();
	pr_debug("dvfs init done\n");
	return 0;
}

static int dvfs_deinit(void *data)
{
	struct xvp* xvp = (struct xvp*)data;

	if ( xvp->dvfs_info.last_level !=dvfs_default_level())
		dvfs_set(xvp->hw_arg, dvfs_default_level());
	mutex_destroy(&xvp->dvfs_info.dvfs_lock);
	pr_debug("dvfs deinit exit\n");
	return 0;
}

static struct vdsp_dvfs_ops vdsp_dvfs_ops = {
	.init = dvfs_init,
	.deinit = dvfs_deinit,
	.enable = dvfs_enable,
	.disable = dvfs_disable,
	.set_powerhint = dvfs_set_powerhint,
	.release_powerhint = dvfs_release_powerhint,
	.setdvfs = dvfs_set,
	.set_voltage = dvfs_set_voltage,
	.set_freq = dvfs_set_freq,
};

static struct vdsp_dvfs_desc sub_dvfs_desc = {
	.ops = &vdsp_dvfs_ops,
};

struct vdsp_dvfs_desc *get_vdsp_dvfs_desc(void)
{
	return &sub_dvfs_desc;
}

