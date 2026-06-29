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

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <sprd_camsys_domain.h>
#include <linux/i2c.h>
#include <linux/version.h>

#include "qvga_core.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "QVGA_DRV:: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define QVGA_DEVICE_NAME       "sprd_qvga"


struct qvga_device {
	struct miscdevice md;
	struct mutex qvga_sync_lock;
	const struct sprd_qvga_driver_ops *ops;
	void *driver_data;
	struct i2c_client *i2c_info;
	struct wakeup_source *ws;
	int active_users;
	atomic_t users_cnt;
	int qvga_registed;
};

struct qvga_file_tag {
	unsigned int private_key;
	unsigned int dev_status;
};


static struct platform_device *sprd_qvga_dev;
static struct qvga_device *g_qvga_dev;
static char qvga_dev_name[255];


static ssize_t sprd_qvga_get_dev_name(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t sprd_qvga_get_dev_status(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);
static ssize_t sprd_qvga_set_dev_enable(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t size);
static ssize_t sprd_qvga_get_dev_bv(struct device *dev,
					 struct device_attribute *attr,
					 char *buf);


static DEVICE_ATTR(qvga_dev_name, S_IRUSR, sprd_qvga_get_dev_name, NULL);

static DEVICE_ATTR(enable, S_IRUSR | S_IWUSR, sprd_qvga_get_dev_status,
			sprd_qvga_set_dev_enable);

static DEVICE_ATTR(light, S_IRUSR, sprd_qvga_get_dev_bv, NULL);




int sprd_qvga_register(const struct sprd_qvga_driver_ops *ops,
			void *drvd, struct i2c_client *i2c_info, const char *dev_name)
{
	int ret = 0, ret1 = 0;

	pr_info("E\n");

	if (!g_qvga_dev || !ops || !i2c_info)
		return -EPROBE_DEFER;

	mutex_lock(&g_qvga_dev->qvga_sync_lock);

	if(g_qvga_dev->qvga_registed) {
		mutex_unlock(&g_qvga_dev->qvga_sync_lock);
		pr_info("qvga driver registed\n");
		return -EPROBE_DEFER;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	ret = sprd_cam_pw_on();
	if (ret) {
		pr_err("%s: mm power on err\n", __func__);
		return ret;
	}
	sprd_cam_domain_eb();
#else
	ret = sprd_glb_mm_pw_on_cfg();
	pm_runtime_get_sync(&i2c_info->dev);
#endif

	if(ops->identify && !ops->identify(drvd)) {
		g_qvga_dev->ops = ops;
		g_qvga_dev->driver_data = drvd;
		g_qvga_dev->i2c_info = i2c_info;
		g_qvga_dev->qvga_registed = 1;
		memset(qvga_dev_name, 0, 255);
		memcpy(qvga_dev_name, dev_name, strlen(dev_name));
		ret1 = device_create_file(g_qvga_dev->md.this_device,
				 &dev_attr_qvga_dev_name);
		if (ret1 < 0)
			pr_err("fail to create file");

		ret1 = device_create_file(g_qvga_dev->md.this_device,
					&dev_attr_enable);
		if (ret1 < 0)
			pr_err("fail to create enable sysfs");

		ret1 = device_create_file(g_qvga_dev->md.this_device,
					&dev_attr_light);
		if (ret1 < 0)
			pr_err("fail to create light sysfs");
	} else {
		pr_info("rigister QVGA driver failed\n");
		ret = -ENODEV;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	sprd_cam_domain_disable();
	sprd_cam_pw_off();
#else
	sprd_glb_mm_pw_off_cfg();
	pm_runtime_put_sync(&i2c_info->dev);
#endif

	mutex_unlock(&g_qvga_dev->qvga_sync_lock);

	return ret;
}
EXPORT_SYMBOL(sprd_qvga_register);


static int sprd_qvga_drv_active(void *drvd) {

	int ret = 0;

	if(g_qvga_dev->active_users == 0) {
		ret = g_qvga_dev->ops->open(drvd);
	}
	if(!ret) {
		g_qvga_dev->active_users += 1;
	}

	return ret;
}


static int sprd_qvga_drv_disable(void *drvd) {

	int ret = 0;

	if(g_qvga_dev->active_users == 1) {
		ret = g_qvga_dev->ops->close(drvd);
	}
	g_qvga_dev->active_users -= 1;

	return ret;
}

static int sprd_qvga_set_private_key(struct qvga_file_tag *p_file,
					unsigned long arg)
{
	int ret = 0;
	unsigned int private_key;

	ret = copy_from_user(&private_key, (unsigned int *)arg, sizeof(unsigned int));
	if(ret) {
		pr_info("copy private_key from user failed.\n");
		return ret;
	}

	if(private_key == SPRD_QVGA_PRIVATE_KEY) {
		p_file->private_key = 1;
		pr_info("qvga set private key successfully\n");
    } else {
		pr_info("qvga set private key faild\n");
		return 1;
    }
	return ret;
}


static long sprd_qvga_ioctl(struct file *file, unsigned int cmd,
				   unsigned long arg)
{
	int ret = 0;
	struct qvga_file_tag *qvga_file = (struct qvga_file_tag*)file->private_data;
	uint32_t bv_value = 0;

	if(NULL == qvga_file) {
		pr_info("private_data is NULL!\n");
		return -EFAULT;
	}

	if((1 != qvga_file->private_key) && (QVGA_IO_SET_PRIVATE_KEY != cmd)) {
		pr_info("unrecognized private key, permission deny!\n");
		return -EFAULT;
	}

	mutex_lock(&g_qvga_dev->qvga_sync_lock);
	switch (cmd) {
	case QVGA_IO_OPEN:
		if(qvga_file->dev_status == SPRD_QVGA_OFF) {
			ret = sprd_qvga_drv_active(g_qvga_dev->driver_data);
			qvga_file->dev_status = SPRD_QVGA_ON;
		} else {
			pr_info("QVGA device is already opened");
		}
		break;
	case QVGA_IO_CLOSE:
		if(qvga_file->dev_status == SPRD_QVGA_ON) {
			ret = sprd_qvga_drv_disable(g_qvga_dev->driver_data);
			qvga_file->dev_status = SPRD_QVGA_OFF;
		}
		break;
	case QVGA_IO_GETBV:
		ret = g_qvga_dev->ops->getbv(g_qvga_dev->driver_data, &bv_value);
		if(!ret) {
			ret = copy_to_user((void __user *)arg, &bv_value, sizeof(uint32_t));
		}
		break;
	case QVGA_IO_SET_PRIVATE_KEY:
		ret = sprd_qvga_set_private_key(qvga_file, arg);
		break;

	}
	mutex_unlock(&g_qvga_dev->qvga_sync_lock);

	return ret;
}


static int sprd_qvga_file_open(struct inode *node, struct file *file)
{
	int ret = 0;
	struct qvga_device *qvga_dev;
	struct miscdevice *md = file->private_data;
	struct qvga_file_tag *p_file = NULL;

	pr_info("E\n");

	if (!md) {
		pr_err("%s: misc device not found", __func__);
		return -EFAULT;
	}

	qvga_dev = md->this_device->platform_data;
	if(!qvga_dev) {
		pr_err("%s: module data not found", __func__);
		return -EFAULT;
	}

	if((0 == qvga_dev->qvga_registed) || (NULL == qvga_dev->driver_data)) {
		pr_err("%s: qvga driver not found", __func__);
		return -EFAULT;
	}

	p_file = kzalloc(sizeof(struct qvga_file_tag), GFP_KERNEL);
	memset(p_file, 0, sizeof(struct qvga_file_tag));
	if (!p_file) {
		return -ENOMEM;
	}

	if(atomic_inc_return(&qvga_dev->users_cnt) == 1) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
		ret = sprd_cam_pw_on();
		sprd_cam_domain_eb();
#else
		ret = sprd_glb_mm_pw_on_cfg();
		pm_runtime_get_sync(&qvga_dev->i2c_info->dev);
#endif
		__pm_stay_awake(qvga_dev->ws);
	}

	if (ret) {
		kfree(p_file);
		pr_err("%s: mm power on err\n", __func__);
	} else {
		file->private_data = (void *)p_file;
	}

	return ret;
}


static int sprd_qvga_file_release(struct inode *node, struct file *file)
{
	struct qvga_file_tag *qvga_file = (struct qvga_file_tag*)file->private_data;
	pr_info("E\n");

	if (!g_qvga_dev || !sprd_qvga_dev)
		return -EFAULT;

	if(qvga_file->dev_status == SPRD_QVGA_ON) {
		sprd_qvga_drv_disable(g_qvga_dev->driver_data);
		qvga_file->dev_status = SPRD_QVGA_OFF;
	}

	if(atomic_dec_return(&g_qvga_dev->users_cnt) == 0) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
		sprd_cam_domain_disable();
		sprd_cam_pw_off();
#else
		sprd_glb_mm_pw_off_cfg();
		pm_runtime_put_sync(&g_qvga_dev->i2c_info->dev);
#endif
		__pm_relax(g_qvga_dev->ws);
	}

	kfree(file->private_data);
	file->private_data = NULL;

	return 0;
}


static ssize_t sprd_qvga_get_dev_name(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n",  qvga_dev_name);
}


static ssize_t sprd_qvga_get_dev_status(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",  g_qvga_dev->active_users);
}

static ssize_t sprd_qvga_set_dev_enable(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t size)
{
	int ret = 0;
	unsigned int val = 0;
	if(NULL == g_qvga_dev) {
		return 0;
	}

	ret = kstrtouint(buf, size, &val);
	if (ret) {
		return 0;
	}

	mutex_lock(&g_qvga_dev->qvga_sync_lock);

	if(SPRD_QVGA_ON == val) {

		if(g_qvga_dev->active_users == 0) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
			ret = sprd_cam_pw_on();
			sprd_cam_domain_eb();
#else
			ret = sprd_glb_mm_pw_on_cfg();
			pm_runtime_get_sync(&g_qvga_dev->i2c_info->dev);
#endif
			__pm_stay_awake(g_qvga_dev->ws);
			ret = g_qvga_dev->ops->open(g_qvga_dev->driver_data);
			if(!ret) {
				g_qvga_dev->active_users = 1;
			}
		}

	} else if (SPRD_QVGA_OFF == val) {
		if(g_qvga_dev->active_users == 1) {
			ret = g_qvga_dev->ops->close(g_qvga_dev->driver_data);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
			sprd_cam_domain_disable();
			sprd_cam_pw_off();
#else
			sprd_glb_mm_pw_off_cfg();
			pm_runtime_put_sync(&g_qvga_dev->i2c_info->dev);
#endif
			__pm_relax(g_qvga_dev->ws);
			g_qvga_dev->active_users = 0;
		}

	} else {
		pr_info("invalid qvga status!");
	}

	mutex_unlock(&g_qvga_dev->qvga_sync_lock);
	if(ret) {
		return 0;
	}
	return size;
}


static ssize_t sprd_qvga_get_dev_bv(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int ret = 0;
	uint32_t bv_value = 0;

	if(NULL == g_qvga_dev) {
		return 0;
	}
	if(g_qvga_dev->ops &&g_qvga_dev->ops->getbv)
		ret = g_qvga_dev->ops->getbv(g_qvga_dev->driver_data, &bv_value);
	if(ret) {
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n",  bv_value);
}


static const struct file_operations qvga_fops = {
	.owner = THIS_MODULE,
	.open = sprd_qvga_file_open,
	.unlocked_ioctl = sprd_qvga_ioctl,
	.compat_ioctl = sprd_qvga_ioctl,
	.release = sprd_qvga_file_release,
};


static int sprd_qvga_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct qvga_device *qvga_dev = NULL;
	pr_info("E\n");

	qvga_dev = devm_kzalloc(&pdev->dev, sizeof(*qvga_dev), GFP_KERNEL);
	if (!qvga_dev)
		return -ENOMEM;

	qvga_dev->md.minor = MISC_DYNAMIC_MINOR;
	qvga_dev->md.name = QVGA_DEVICE_NAME;
	qvga_dev->md.fops = &qvga_fops;
	qvga_dev->md.parent = NULL;
	ret = misc_register(&qvga_dev->md);
	if (ret) {
		pr_err("fail to register misc devices\n");
		goto exit;
	}

	mutex_init(&qvga_dev->qvga_sync_lock);
	atomic_set(&qvga_dev->users_cnt, 0);
	qvga_dev->active_users = 0;
	qvga_dev->qvga_registed = 0;
	qvga_dev->ws = wakeup_source_create("QVGA Wakeup Source");
	wakeup_source_add(qvga_dev->ws);
	qvga_dev->md.this_device->platform_data = (void *)qvga_dev;
	platform_set_drvdata(pdev, (void *)qvga_dev);

	g_qvga_dev = qvga_dev;
	sprd_qvga_dev = pdev;

exit:
	return ret;
}


static int sprd_qvga_remove(struct platform_device *pdev)
{
	struct qvga_device *qvga_dev = NULL;
	pr_info("E\n");

	qvga_dev = platform_get_drvdata(pdev);
	mutex_destroy(&qvga_dev->qvga_sync_lock);
	device_remove_file(qvga_dev->md.this_device, &dev_attr_qvga_dev_name);
	device_remove_file(qvga_dev->md.this_device, &dev_attr_enable);
	device_remove_file(qvga_dev->md.this_device, &dev_attr_light);
	misc_deregister(&qvga_dev->md);
	devm_kfree(&pdev->dev, qvga_dev);
	platform_set_drvdata(pdev, NULL);
	g_qvga_dev = NULL;
	sprd_qvga_dev = NULL;

	return 0;
}


static struct platform_driver sprd_qvga_drvier = {
	.probe = sprd_qvga_probe,
	.remove = sprd_qvga_remove,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = QVGA_DEVICE_NAME,
		   },
};


static int __init qvga_init(void)
{
	int ret = 0;
	pr_info("E\n");

	ret = platform_driver_register(&sprd_qvga_drvier);
	if (ret) {
		pr_err("fail to register qvga driver\n");
		goto exit;
	}

	sprd_qvga_dev = platform_device_register_simple(QVGA_DEVICE_NAME, -1, NULL, 0);
	if (IS_ERR_OR_NULL(sprd_qvga_dev)) {
		pr_err("fail to register sprd_qvga\n");
		ret = PTR_ERR(sprd_qvga_dev);
		platform_driver_unregister(&sprd_qvga_drvier);
		goto exit;
	}

exit:
	return ret;
}


static void __exit qvga_exit(void)
{
	platform_device_unregister(sprd_qvga_dev);
	platform_driver_unregister(&sprd_qvga_drvier);

}

subsys_initcall(qvga_init);
module_exit(qvga_exit);
MODULE_DESCRIPTION("Sprd Qvga Driver");
MODULE_LICENSE("GPL");
