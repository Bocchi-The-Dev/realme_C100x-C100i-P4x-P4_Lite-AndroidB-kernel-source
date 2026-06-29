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
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "qvga_ov8856_drv.h"

extern int sprd_qvga_register(const struct sprd_qvga_driver_ops *ops,
			void *drvd, struct i2c_client *i2c_info, const char* dev_name);


/******* common interface *******/

static int sprd_qvga_drv_ops_open(void *drvd) {
    int ret = 0;
    struct sprd_qvga_dev_info_tag *p_dev = (struct sprd_qvga_dev_info_tag*)drvd;

    //1. power on, enter hardware standby
    ret = sprd_qvga_drv_power(p_dev, SPRD_QVGA_POWER_STATE_ON);
    if(ret) {
        pr_err("qvga power on failed %s", __func__);
        return ret;
    }

    //2. enable qvga sensor
    //TO DO:
    ret = sprd_qvga_write_regtab(p_dev, &init_reg_tab);
    if(ret) {
        pr_err("qvga load init setting failed %s", __func__);
        sprd_qvga_drv_power(p_dev, SPRD_QVGA_POWER_STATE_OFF);
        return ret;
    }

    pr_info("qvga activated\n");

    return ret;
}


static int sprd_qvga_drv_ops_close(void *drvd) {
    int ret = 0;
    struct sprd_qvga_dev_info_tag *p_dev = (struct sprd_qvga_dev_info_tag*)drvd;

    //1. disable qvga sensor
    //TO DO:

    //2. Power off
    ret = sprd_qvga_drv_power(p_dev, SPRD_QVGA_POWER_STATE_OFF);
    return ret;
}

static int sprd_qvga_drv_ops_getbv(void *drvd, void* arg) {
    int ret = 0;
    struct sprd_qvga_dev_info_tag *p_dev = (struct sprd_qvga_dev_info_tag*)drvd;
    uint32_t *bv_val = (uint32_t *) arg;
    struct sprd_qvga_reg_tag reg_tmp = {0};
    reg_tmp.reg_addr = QVGA_IC_BV_ADDR;
    ret = sprd_qvga_read_reg(p_dev, &reg_tmp);
    if(!ret) {
        *bv_val = reg_tmp.reg_value;
    }

    return ret;
}

static int sprd_qvga_drv_identify(void *dev_info) {
    int ret = 0;
    struct sprd_qvga_dev_info_tag *p_dev = (struct sprd_qvga_dev_info_tag *)dev_info;
    struct sprd_qvga_reg_tag reg_tmp = {0};
    uint16_t pid_val;
    uint16_t ver_val;

    if(NULL == p_dev) {
        return -ENODEV;
    }

    //1. Enter hardware standby
    ret = sprd_qvga_drv_power(p_dev, SPRD_QVGA_POWER_STATE_ON);
    if(ret) {
        pr_err("qvga power on failed %s\n", __func__);
        return ret;
    }

    //2. match chip ID
    reg_tmp.reg_addr = QVGA_IC_PID_ADDR;
    ret = sprd_qvga_read_reg(p_dev, &reg_tmp);
    if(ret) {
        pr_err("read pid value failed %s\n", __func__);
        goto exit;
    }
    pid_val = reg_tmp.reg_value;

    reg_tmp.reg_addr = QVGA_IC_VER_ADDR;
    ret = sprd_qvga_read_reg(p_dev, &reg_tmp);
    if(ret) {
        pr_err("read ver value failed %s\n", __func__);
        goto exit;
    }
    ver_val = reg_tmp.reg_value;

    if(QVGA_IC_PID_VALUE != pid_val || QVGA_IC_VER_VALUE != ver_val) {
        ret = -ENODEV;
        pr_err("%s identify failed, pid %d, ver %d\n", QVGA_DRV_NAME, pid_val, ver_val);
        goto exit;
    }

    pr_info("%s identifyed, %s\n", QVGA_DRV_NAME, __func__);


//3. Exit hardware standby, power off
exit:
    sprd_qvga_drv_power(p_dev, SPRD_QVGA_POWER_STATE_OFF);
    return ret;
}

static const struct sprd_qvga_driver_ops sprd_qvga_drv_ops = {
    .open = sprd_qvga_drv_ops_open,
    .close = sprd_qvga_drv_ops_close,
    .identify = sprd_qvga_drv_identify,
    .getbv = sprd_qvga_drv_ops_getbv,
};




/******* internal function *******/

static int sprd_qvga_drv_ic_init(void *dev_info) {

    int ret = 0;
    struct sprd_qvga_dev_info_tag *p_dev = (struct sprd_qvga_dev_info_tag *)dev_info;
    mutex_init(&p_dev->sync_lock);

    mutex_init(&p_dev->set_voltage_lock);
    p_dev->i2c_reg_bits = QVGA_I2C_REG_BIT;
    p_dev->i2c_burst_mode = QVGA_I2C_BURST_MODE;
    p_dev->i2c_info->addr = QVGA_I2C_SLAVE_ADDR >> 1;
    p_dev->i2c_clock = 400000;
    if(p_dev->i2c_reg_bits & I2C_1M_FLAG_QVGA) {
        p_dev->i2c_clock = 1000000;
    }
    return ret;
}


static int sprd_qvga_drv_power(void *dev_info, int power_state) {
    int ret = 0;
    struct sprd_qvga_dev_info_tag *p_dev = (struct sprd_qvga_dev_info_tag *)dev_info;
    if(SPRD_QVGA_POWER_STATE_ON == power_state) {

        sprd_qvga_set_pd_level(p_dev, QVGA_POWER_DOWN_PULSE_LEVEL);
        sprd_qvga_set_rst_level(p_dev, QVGA_RESET_PULSE_LEVEL);
        sprd_qvga_set_mclk(p_dev, SPRD_QVGA_MCLK_DISABLE);
        sprd_qvga_set_avdd(p_dev, SPRD_QVGA_VDD_CLOSED);
        sprd_qvga_set_dvdd(p_dev, SPRD_QVGA_VDD_CLOSED);

        usleep_range(1000, 1100);
        sprd_qvga_set_iovdd(p_dev, QVGA_IOVDD_VOLTAGE_VAL);
        sprd_qvga_set_avdd(p_dev, QVGA_AVDD_VOLTAGE_VAL);
        sprd_qvga_set_dvdd(p_dev, QVGA_DVDD_VOLTAGE_VAL);

        usleep_range(1000, 1100);
        sprd_qvga_set_pd_level(p_dev, !QVGA_POWER_DOWN_PULSE_LEVEL);
        sprd_qvga_set_rst_level(p_dev, !QVGA_RESET_PULSE_LEVEL);
        usleep_range(1000, 1100);
        sprd_qvga_set_mclk(p_dev, QVGA_MCLK);
        usleep_range(1000, 1100);

    } else if(SPRD_QVGA_POWER_STATE_OFF == power_state) {

        sprd_qvga_set_mclk(p_dev, SPRD_QVGA_MCLK_DISABLE);
        sprd_qvga_set_pd_level(p_dev, QVGA_POWER_DOWN_PULSE_LEVEL);
        sprd_qvga_set_rst_level(p_dev, QVGA_RESET_PULSE_LEVEL);

        usleep_range(500, 600);
        sprd_qvga_set_avdd(p_dev, SPRD_QVGA_VDD_CLOSED);
        sprd_qvga_set_dvdd(p_dev, SPRD_QVGA_VDD_CLOSED);
        sprd_qvga_set_iovdd(p_dev, SPRD_QVGA_VDD_CLOSED);

    } else {
        pr_err("invalid power state %s", __func__);
        ret = -EINVAL;
    }
    return ret;
}

static int sprd_qvga_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id) {
    int ret = 0;
    struct sprd_qvga_dev_info_tag *pdata = NULL;
    struct device *dev = &client->dev;

    if (!dev->of_node) {
		pr_err("no device node %s", __func__);
		return -ENODEV;
	}

    pdata = devm_kzalloc(dev, sizeof(struct sprd_qvga_dev_info_tag), GFP_KERNEL);
    if (!pdata)
		return -ENOMEM;
	client->dev.platform_data = (void *)pdata;
	pdata->i2c_info = client;

    ret = sprd_qvga_parse_dt(dev, pdata);
    if(ret)
        goto exit;

    sprd_qvga_drv_ic_init(pdata);

    ret = sprd_qvga_register(&sprd_qvga_drv_ops, pdata, client, QVGA_DRV_NAME);
    if(ret) {
        pr_err("%s identify failed, %s", QVGA_DRV_NAME, __func__);
    }

exit:
    return ret;
}



static int sprd_qvga_driver_remove(struct i2c_client *client)
{
    int ret = 0;
    struct sprd_qvga_dev_info_tag *pdata = client->dev.platform_data;
    if(pdata) {
        mutex_destroy(&pdata->sync_lock);
        mutex_destroy(&pdata->set_voltage_lock);
        sprd_qvga_free_gpio(&client->dev, pdata);
        devm_kfree(&client->dev, pdata);
    }

    client->dev.platform_data = NULL;
    return ret;    
}

static const struct of_device_id sprd_qvga_of_match_table[] = {
	{.compatible = SPRD_QVGA_COMPATIBLE},
	{/* MUST end with empty struct */},
};

static const struct i2c_device_id sprd_qvga_drv_ids[] = {
	{}
};

static struct i2c_driver sprd_qvga_driver = {
	.driver = {
		.of_match_table = of_match_ptr(sprd_qvga_of_match_table),
		.name = QVGA_DRV_NAME,
	},
	.probe = sprd_qvga_driver_probe,
	.remove = sprd_qvga_driver_remove,
	.id_table = sprd_qvga_drv_ids,
};

static int __init sprd_qvga_driver_init(void) {
    int ret = 0;
    ret = i2c_add_driver(&sprd_qvga_driver);
    if(ret) {
        i2c_del_driver(&sprd_qvga_driver);
        pr_err("i2c_add_driver error\n");
    }
    return ret;
}

static void __exit sprd_qvga_driver_deinit(void) {
    i2c_del_driver(&sprd_qvga_driver);
}

module_init(sprd_qvga_driver_init);
module_exit(sprd_qvga_driver_deinit);
MODULE_DESCRIPTION("SPRD OV8856 QVGA Driver");
MODULE_LICENSE("GPL");