#include <linux/device.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include "qvga_drv.h"



int sprd_qvga_set_pd_level(struct sprd_qvga_dev_info_tag *p_dev,
                                  int power_level)
{
	int ret = 0;
	int pwn_gpio_id = 0;

	if (!p_dev) {
		pr_err("%s, error\n", __func__);
		return -EINVAL;
	}
	pwn_gpio_id = p_dev->gpio_tab[SPRD_QVGA_PWN_GPIO_TAG_E];
	if (gpio_is_valid(pwn_gpio_id)) {
		if (power_level == 0)
			ret = gpio_direction_output(pwn_gpio_id, 0);
		else
			ret = gpio_direction_output(pwn_gpio_id, 1);

		pr_info("qvga set pd level %d\n", power_level);
	}

	return ret;
}



int sprd_qvga_set_rst_level(struct sprd_qvga_dev_info_tag *p_dev,
                                  int power_level)
{
	int ret = 0;
	int rst_gpio_id = 0;

	if (!p_dev) {
		pr_err("%s, error\n", __func__);
		return -EINVAL;
	}
	rst_gpio_id = p_dev->gpio_tab[SPRD_QVGA_RST_GPIO_TAG_E];
	if (gpio_is_valid(rst_gpio_id)) {
		if (power_level == 0)
			ret = gpio_direction_output(rst_gpio_id, 0);
		else
			ret = gpio_direction_output(rst_gpio_id, 1);

		pr_info("qvga set pd level %d\n", power_level);
	}

	return ret;
}

int sprd_qvga_reset(struct sprd_qvga_dev_info_tag *p_dev,
                           unsigned int level, unsigned int width)
{
	int ret = 0;
	int reset_gpio_id = 0;

	if (!p_dev) {
		pr_err("%s, error\n", __func__);
		return -EINVAL;
	}
	reset_gpio_id = p_dev->gpio_tab[SPRD_QVGA_RST_GPIO_TAG_E];
	if (gpio_is_valid(reset_gpio_id)) {
		ret = gpio_direction_output(reset_gpio_id, level);
		gpio_set_value(reset_gpio_id, level);
		mdelay(width);
		gpio_set_value(reset_gpio_id, !level);
	}
	pr_info("qvga reset%d width %d\n", level, width);

	return ret;
}



static int select_qvga_mclk(uint8_t clk_set, char **clk_src_name, uint8_t *clk_div)
{
	unsigned char i = 0;
	unsigned char j = 0;
	unsigned char mark_src = 0;
	unsigned char mark_div = 0;
	unsigned char mark_src_tmp = 0;
	int clk_tmp = 0x7ffffff;
	int src_delta = 0x7ffffff;
	int src_delta_min = 0x7ffffff;
	int div_delta_min = 0x7ffffff;
	int mclk_count = ARRAY_SIZE(c_qvga_mclk_tab);

	if (clk_set > 96 || !clk_src_name || !clk_div)
		return -EPERM;

	for (i = 0; i < 8; i++) {
		clk_tmp = (int)(clk_set * (i + 1));
		src_delta_min = 0x7ffffff;
		for (j = 0; j < mclk_count; j++) {
			src_delta = c_qvga_mclk_tab[j].clock - clk_tmp;
			src_delta = (src_delta > 0) ?
				(src_delta) : (-src_delta);
			if (src_delta < src_delta_min) {
				src_delta_min = src_delta;
				mark_src_tmp = j;
			}
		}
		if (src_delta_min < div_delta_min) {
			div_delta_min = src_delta_min;
			mark_src = mark_src_tmp;
			mark_div = i;
		}
	}
	pr_info("src %d, div=%d .\n", mark_src, mark_div);

	*clk_src_name = c_qvga_mclk_tab[mark_src].src_name;
	*clk_div = mark_div + 1;

	return 0;
}

int sprd_qvga_set_mclk(struct sprd_qvga_dev_info_tag *p_dev,
                              unsigned int set_mclk)
{
	int ret = 0;
	char *clk_src_name = NULL;
	unsigned char clk_div = 0;
	struct clk *clk_parent = NULL;

	if (!p_dev) {
		pr_err("qvga clock no device\n");
		return -EINVAL;
	}
    pr_info("set_mclk %d\n", set_mclk);

	if (set_mclk == 0  && p_dev->mclk_count) {
		if (p_dev->mclk_freq) {
#ifndef MCLK_NEW_PROCESS
			if (p_dev->qvga_eb)
				clk_disable_unprepare(p_dev->qvga_eb);
			else if (p_dev->ccir_eb)
				clk_disable_unprepare(p_dev->ccir_eb);
#endif

			if (p_dev->qvga_clk) {
				ret = clk_set_parent(p_dev->qvga_clk,
					       p_dev->qvga_clk_default);
				if (ret) {
					pr_err("set_parent failed\n");
					goto exit;
				}
				ret = clk_set_rate(p_dev->qvga_clk,
					     p_dev->qvga_clk_default_rate);
				if (ret) {
					pr_err("set rate failed\n");
					goto exit;
				}
				clk_disable_unprepare(p_dev->qvga_clk);
			}
#ifdef MCLK_NEW_PROCESS
			if (p_dev->qvga_eb)
				clk_disable_unprepare(p_dev->qvga_eb);
			else if (p_dev->ccir_eb)
				clk_disable_unprepare(p_dev->ccir_eb);
#endif
			p_dev->mclk_count--;
		}
	} else if (!p_dev->mclk_count && set_mclk) {
		if (set_mclk > SPRD_QVGA_MAX_MCLK)
			set_mclk = SPRD_QVGA_MAX_MCLK;

		if (p_dev->qvga_eb && p_dev->qvga_clk) {
			ret = select_qvga_mclk(set_mclk, &clk_src_name,
						 &clk_div);
			pr_info("clk_src_name %s\n", clk_src_name);
			if (ret != 0) {
				pr_err("select qvga mclk error\n");
				goto exit;
			}

			clk_parent =
			of_clk_get_by_name(p_dev->i2c_info->dev.of_node, clk_src_name);
			if (!clk_parent) {
				pr_err("get clk parent error\n");
				return -EINVAL;
			}
			pr_info("set_mclk %d \n",
				set_mclk);
#ifdef MCLK_NEW_PROCESS
			clk_prepare_enable(p_dev->qvga_eb);
#endif
			ret = clk_set_parent(p_dev->qvga_clk, clk_parent);
			if (ret) {
				pr_err("set parent failed\n");
				goto exit;
			}
			ret = clk_set_rate(p_dev->qvga_clk,
				     (set_mclk * 1000000));
			if (ret) {
				pr_err("set rate failed\n");
				goto exit;
			}
#ifndef MCLK_NEW_PROCESS

			clk_prepare_enable(p_dev->qvga_eb);
#endif
		} else if (p_dev->ccir_eb && p_dev->qvga_clk) {
			clk_prepare_enable(p_dev->ccir_eb);
		}

		clk_prepare_enable(p_dev->qvga_clk);
		p_dev->mclk_count++;
	} else if (p_dev->mclk_count && set_mclk != p_dev->mclk_freq) {
			ret = select_qvga_mclk(set_mclk, &clk_src_name,
						 &clk_div);
			pr_info("clk_src_name %s\n", clk_src_name);
			if (ret != 0) {
				pr_err("select qvga mclk error\n");
				goto exit;
			}

			clk_parent =
			of_clk_get_by_name(p_dev->i2c_info->dev.of_node, clk_src_name);
			if (!clk_parent) {
				pr_err("get clk parent error\n");
				return -EINVAL;
			}
			pr_info("set_mclk %d\n",
				set_mclk);

			ret = clk_set_parent(p_dev->qvga_clk, clk_parent);
			if (ret) {
				pr_err("set parent failed\n");
				goto exit;
			}
			ret = clk_set_rate(p_dev->qvga_clk,
				     (set_mclk * 1000000));
			if (ret) {
				pr_err("set rate failed\n");
				goto exit;
			}
			pr_info("set rate mclk count %d \n", p_dev->mclk_count);
	}
	p_dev->mclk_freq = set_mclk;
exit:
	pr_info("qvga mclk %d ret= %d\n", set_mclk, ret);
	return ret;
}



static int sprd_qvga_regulator_enable(struct regulator *p_reg,
					struct sprd_qvga_dev_info_tag *p_dev,
					int type)
{
	int err = 0;

	err = regulator_enable(p_reg);

	if (err != 0)
		pr_err("Error in regulator_enable: err %d", err);
	else
		p_dev->power_on_count[type]++;

	return err;
}

static int sprd_qvga_regulator_disable(struct regulator *p_reg,
					struct sprd_qvga_dev_info_tag *p_dev,
					int type)
{
	int err = 0;

	while (p_dev->power_on_count[type] > 0) {
		err = regulator_disable(p_reg);

		if (err != 0)
			pr_err("Error in regulator_disable: err %d", err);
		else
			p_dev->power_on_count[type]--;
	}

	return err;
}

static int sprd_qvga_set_voltage(struct sprd_qvga_dev_info_tag *p_dev,
                                 unsigned int val, int type)
{
	struct regulator *p_regulator = NULL;
	int ret = 0;
	struct device *dev = NULL;
	unsigned int tolerance = 0;

	if (p_dev == NULL) {
		pr_info("p_dev %d invalid\n", type);
		return -EINVAL;
	}
	mutex_lock(&p_dev->set_voltage_lock);
	if (p_dev->regulator_supply[type] == NULL) {
		dev = &p_dev->i2c_info->dev;
		p_dev->regulator_supply[type] = devm_regulator_get(dev,
			sprd_qvga_supply_names[type]);
	}
	p_regulator = p_dev->regulator_supply[type];
	pr_info("%s, regulator %s %p\n", __func__, sprd_qvga_supply_names[type], p_regulator);
	if (p_regulator == NULL) {
		pr_info("regulator %d invalid\n", type);
		mutex_unlock(&p_dev->set_voltage_lock);
		ret = -EINVAL;
		goto exit;
	}
	if (val) {
		tolerance = regulator_get_linear_step(p_regulator);
		pr_info("%s: voltage %d, type %d, tolerance %d\n", __func__, val, type, tolerance);
		ret = regulator_set_voltage(p_regulator, val - tolerance/2, val + tolerance/2);
		if (ret) {
			pr_err("regulator %s vol set %d fail ret%d\n",
			       sprd_qvga_supply_names[type], val, ret);
			goto exit;
		}
		ret = sprd_qvga_regulator_enable(p_regulator, p_dev, type);
		if (ret) {
			devm_regulator_put(p_regulator);
			p_dev->regulator_supply[type] = NULL;
			pr_err("regulator %s enable fail ret%d\n",
			       sprd_qvga_supply_names[type], ret);
			goto exit;
		}
	} else {
		if (p_regulator) {
			ret = sprd_qvga_regulator_disable(p_regulator,
				p_dev, type);
			if (ret) {
				pr_err("regulator disable fail ret%d\n", ret);
			} else {
				devm_regulator_put(p_regulator);
				p_dev->regulator_supply[type] = NULL;
			}
		} else {
			pr_err("regulator %s does not exist",
				sprd_qvga_supply_names[type]);
		}
	}

exit:
	mutex_unlock(&p_dev->set_voltage_lock);
	return ret;
}

static int sprd_qvga_set_voltage_by_gpio(struct sprd_qvga_dev_info_tag *p_dev,
                                         unsigned int val, int type)
{
	int ret = -EINVAL;
	int gpio_id = 0;

	if (!p_dev) {
		pr_err("%s, error\n", __func__);
		return -EINVAL;
	}
	gpio_id = p_dev->gpio_tab[type];
	if (gpio_is_valid(gpio_id)) {

		ret = gpio_direction_output(gpio_id, val ? 1:0);
		if (ret)
			goto exit;

		gpio_set_value(gpio_id, val ? 1:0);
	}
	pr_info("qvga vdd val %d\n", val);

exit:
	return ret;
}

static unsigned int sprd_qvga_get_voltage_value(unsigned int vdd_val)
{
	unsigned int volt_value = 0;

	switch (vdd_val) {

	case SPRD_QVGA_VDD_3800MV:
		volt_value = SPRD_QVGA_VDD_3800MV_VAL;
		break;
	case SPRD_QVGA_VDD_3300MV:
		volt_value = SPRD_QVGA_VDD_3300MV_VAL;
		break;
	case SPRD_QVGA_VDD_3000MV:
		volt_value = SPRD_QVGA_VDD_3000MV_VAL;
		break;
	case SPRD_QVGA_VDD_2800MV:
		volt_value = SPRD_QVGA_VDD_2800MV_VAL;
		break;
	case SPRD_QVGA_VDD_2500MV:
		volt_value = SPRD_QVGA_VDD_2500MV_VAL;
		break;
	case SPRD_QVGA_VDD_2200MV:
		volt_value = SPRD_QVGA_VDD_2200MV_VAL;
		break;
	case SPRD_QVGA_VDD_2000MV:
		volt_value = SPRD_QVGA_VDD_2000MV_VAL;
		break;
	case SPRD_QVGA_VDD_1800MV:
		volt_value = SPRD_QVGA_VDD_1800MV_VAL;
		break;
	case SPRD_QVGA_VDD_1500MV:
		volt_value = SPRD_QVGA_VDD_1500MV_VAL;
		break;
	case SPRD_QVGA_VDD_1300MV:
		volt_value = SPRD_QVGA_VDD_1300MV_VAL;
		break;
	case SPRD_QVGA_VDD_1200MV:
		volt_value = SPRD_QVGA_VDD_1200MV_VAL;
		break;
	case SPRD_QVGA_VDD_1100MV:
		volt_value = SPRD_QVGA_VDD_1100MV_VAL;
		break;
	case SPRD_QVGA_VDD_1000MV:
		volt_value = SPRD_QVGA_VDD_1000MV_VAL;
		break;
	case SPRD_QVGA_VDD_CLOSED:
		volt_value = 0;
		break;
	default:
		volt_value = vdd_val * 1000;
		break;
	}

	return volt_value;
}

int sprd_qvga_set_avdd(struct sprd_qvga_dev_info_tag *p_dev,
				   unsigned int vdd_val)
{
	int ret = 0, ret1 = 0;

    vdd_val = sprd_qvga_get_voltage_value(vdd_val);
    pr_info("set avdd %d\n", vdd_val);
    ret = sprd_qvga_set_voltage_by_gpio(p_dev,
        vdd_val,
        SPRD_QVGA_AVDD_GPIO_TAG_E);
    ret1 = sprd_qvga_set_voltage(p_dev,
        vdd_val,
        SPRD_QVGA_REGULATOR_AVDD_TAG_E);

	return ret & ret1;
}

int sprd_qvga_set_dvdd(struct sprd_qvga_dev_info_tag *p_dev,
				   unsigned int vdd_val)
{
	int ret = 0, ret1 = 0;
    vdd_val = sprd_qvga_get_voltage_value(vdd_val);
    pr_info("set dvdd %d\n", vdd_val);
    ret = sprd_qvga_set_voltage_by_gpio(p_dev,
        vdd_val,
        SPRD_QVGA_DVDD_GPIO_TAG_E);
    ret1 = sprd_qvga_set_voltage(p_dev,
        vdd_val,
        SPRD_QVGA_REGULATOR_DVDD_TAG_E);

	return ret & ret1;
}

int sprd_qvga_set_iovdd(struct sprd_qvga_dev_info_tag *p_dev,
				    unsigned int vdd_val)
{
	int ret = 0, ret1 = 0;

    vdd_val = sprd_qvga_get_voltage_value(vdd_val);
    pr_info("set iovdd %d\n", vdd_val);
    ret = sprd_qvga_set_voltage_by_gpio(p_dev,
        vdd_val,
        SPRD_QVGA_IOVDD_GPIO_TAG_E);
    ret1 = sprd_qvga_set_voltage(p_dev,
        vdd_val,
        SPRD_QVGA_REGULATOR_IOVDD_TAG_E);

	return ret & ret1;
}



static int sprd_qvga_parse_clk_dt(struct device *dev,
				struct sprd_qvga_dev_info_tag *qvga_info)
{
	qvga_info->qvga_clk = of_clk_get_by_name(dev->of_node, "clk_src");
	pr_info("qvga_info->qvga_clk: %p\n",
		qvga_info->qvga_clk);
	if (IS_ERR_OR_NULL(qvga_info->qvga_clk)) {
		pr_err("qvga clk config err\n");
		return -EINVAL;
	}

	qvga_info->qvga_clk_default =
		clk_get_parent(qvga_info->qvga_clk);
	if (IS_ERR_OR_NULL(qvga_info->qvga_clk_default))
		return -EINVAL;

	qvga_info->qvga_clk_default_rate =
		clk_get_rate(qvga_info->qvga_clk_default);

	qvga_info->qvga_eb =
		of_clk_get_by_name(dev->of_node, "sensor_eb");
	qvga_info->mclk_count = 0;
	qvga_info->mclk_freq = 0;
	if (IS_ERR_OR_NULL(qvga_info->qvga_eb)) {
		pr_err("qvga eb clk config err\n");
		return -EINVAL;
	}

	return 0;
}

static int sprd_qvga_parse_gpio_dt(struct device *dev,
				struct sprd_qvga_dev_info_tag
				*qvga_info)
{
	int i;
	int ret = 0;

	for (i = 0; i < SPRD_QVGA_GPIO_TAG_MAX; i++) {
		qvga_info->gpio_tab[i] = of_get_named_gpio(dev->of_node,
						sprd_qvga_gpio_names[i], 0);
		if (gpio_is_valid(qvga_info->gpio_tab[i])) {
			ret = devm_gpio_request(dev, qvga_info->gpio_tab[i],
						sprd_qvga_gpio_names[i]);
		} else {
			pr_info("invalid gpio: i=%d, gpio=%d, name=%s\n",
			       i, qvga_info->gpio_tab[i],
			       sprd_qvga_gpio_names[i]);
		}
	}

	return 0;
}

int sprd_qvga_free_gpio(struct device *dev,
				struct sprd_qvga_dev_info_tag *qvga_info)
{
	int i;
	int ret = 0;

	for (i = 0; i < SPRD_QVGA_GPIO_TAG_MAX; i++) {
		qvga_info->gpio_tab[i] = of_get_named_gpio(dev->of_node,
						sprd_qvga_gpio_names[i], 0);
		if (gpio_is_valid(qvga_info->gpio_tab[i]))
			devm_gpio_free(dev, qvga_info->gpio_tab[i]);

	}
	return ret;
}

int sprd_qvga_parse_dt(struct device *dev,
				struct sprd_qvga_dev_info_tag *qvga_info)
{

	if (sprd_qvga_parse_clk_dt(dev, qvga_info)) {
		pr_err("%s :clock parsing error\n", __func__);
		return -EINVAL;
	}

	if (sprd_qvga_parse_gpio_dt(dev, qvga_info)) {
		pr_err("%s :gpio parsing error\n", __func__);
		return -EINVAL;
	}

	return 0;
}


/* i2c interface */
int sprd_qvga_set_i2c_burst(struct sprd_qvga_dev_info_tag *p_dev,
                            uint32_t burst_mode)
{
	if (!p_dev) {
		pr_err("%s, error\n", __func__);
		return -EINVAL;
	}

    p_dev->i2c_burst_mode = burst_mode;
	pr_info("i2c_burst_mode:%d\n", burst_mode);

	return 0;
}

int sprd_qvga_read_reg(struct sprd_qvga_dev_info_tag *p_dev, struct sprd_qvga_reg_tag *pReg)
{
	uint8_t cmd[2] = { 0 };
	uint16_t w_cmd_num = 0;
	uint16_t r_cmd_num = 0;
	uint8_t buf_r[2] = { 0 };
	int ret = -1;
	struct i2c_msg msg_r[2];
	uint16_t reg_addr;
	int i = 0;
	int cnt = 0;

	if (!p_dev || !p_dev->i2c_info) {
		pr_err("%s, error\n", __func__);
		return -EINVAL;
	}
	pr_info("%s: slave addr %d\n", __func__, p_dev->i2c_info->addr);
	reg_addr = pReg->reg_addr;
	if (SPRD_QVGA_I2C_REG_16BIT ==
	(p_dev->i2c_reg_bits & SPRD_QVGA_I2C_REG_16BIT)) {
		cmd[w_cmd_num++] =
		(uint8_t) ((reg_addr >> 8) & 0x00ff);
		cmd[w_cmd_num++] =
		(uint8_t) (reg_addr & 0x00ff);
	} else {
		cmd[w_cmd_num++] = (uint8_t) reg_addr;
	}

	if (SPRD_QVGA_I2C_VAL_16BIT ==
	(p_dev->i2c_reg_bits & SPRD_QVGA_I2C_VAL_16BIT))
		r_cmd_num = 2;
	else
		r_cmd_num = 1;

	for (i = 0; i < SPRD_QVGA_I2C_OP_TRY_NUM; i++) {
		msg_r[0].addr = p_dev->i2c_info->addr;
		if (1000000 == p_dev->i2c_clock)
			msg_r[0].flags = 0 | I2C_1M_FLAG_QVGA;
		else
			msg_r[0].flags = 0 | I2C_400K_FLAG_QVGA;
		msg_r[0].buf = cmd;
		msg_r[0].len = w_cmd_num;
		msg_r[1].addr = p_dev->i2c_info->addr;
		if (1000000 == p_dev->i2c_clock)
			msg_r[1].flags = I2C_M_RD | I2C_1M_FLAG_QVGA;
		else
			msg_r[1].flags = I2C_M_RD | I2C_400K_FLAG_QVGA;
		msg_r[1].buf = buf_r;
		msg_r[1].len = r_cmd_num;
		cnt = i2c_transfer(p_dev->i2c_info->adapter, msg_r, 2);
		if (cnt != SPRD_QVGA_I2C_READ_SUCCESS_CNT) {
			pr_err("%s fail, ret %d, addr 0x%x, reg_addr 0x%x\n",
			__func__, ret, p_dev->i2c_info->addr, reg_addr);
			usleep_range(1000, 1500);
		} else {
			pReg->reg_value =
			(r_cmd_num ==
			1) ? (uint16_t) buf_r[0] : (uint16_t) ((buf_r[0] <<
								8) +
								buf_r[1]);
			ret = 0;
			break;
		}
	}

	return ret;
}

int sprd_qvga_write_reg(struct sprd_qvga_dev_info_tag *p_dev, struct sprd_qvga_reg_tag *pReg)
{
	uint8_t cmd[4] = { 0 };
	uint32_t index = 0;
	uint32_t cmd_num = 0;
	struct i2c_msg msg_w;
	int32_t ret = 0;
	uint16_t subaddr;
	uint16_t data;
	int i;
	int cnt = 0;

	if (!p_dev || !p_dev->i2c_info) {
		pr_err("%s, error\n", __func__);
		return -EINVAL;
	}
	subaddr = pReg->reg_addr;
	data = pReg->reg_value;

	if (SPRD_QVGA_I2C_REG_16BIT ==
		(p_dev->i2c_reg_bits & SPRD_QVGA_I2C_REG_16BIT)) {
		cmd[cmd_num++] =
		(uint8_t) ((subaddr >> 8) & 0x00ff);
		index++;
		cmd[cmd_num++] =
		(uint8_t) (subaddr & 0x00ff);
		index++;
	} else {
		cmd[cmd_num++] = (uint8_t) subaddr;
		index++;
	}

	if (SPRD_QVGA_I2C_VAL_16BIT ==
		(p_dev->i2c_reg_bits & SPRD_QVGA_I2C_VAL_16BIT)) {
		cmd[cmd_num++] =
		(uint8_t) ((data >> 8) & 0x00ff);
		index++;
		cmd[cmd_num++] = (uint8_t) (data & 0x00ff);
		index++;
	} else {
		cmd[cmd_num++] = (uint8_t) data;
		index++;
	}

	if (subaddr != SPRD_QVGA_WRITE_DELAY) {
		for (i = 0; i < SPRD_QVGA_I2C_OP_TRY_NUM; i++) {
			msg_w.addr = p_dev->i2c_info->addr;
			if (1000000 == p_dev->i2c_clock)
				msg_w.flags = 0 | I2C_1M_FLAG_QVGA;
			else
				msg_w.flags = 0 | I2C_400K_FLAG_QVGA;
            msg_w.buf = cmd;
			msg_w.len = index;
			cnt = i2c_transfer(p_dev->i2c_info->adapter, &msg_w, 1);
			if (cnt != SPRD_QVGA_I2C_WRITE_SUCCESS_CNT) {
				pr_err("%s fail to:\n"
					"i2cAddr=%x,\n"
					"addr=%x, value=%x, bit=%d\n",
					__func__, p_dev->i2c_info->addr,
					pReg->reg_addr, pReg->reg_value,
					p_dev->i2c_reg_bits);
				ret = -1;
				continue;
			} else {
				ret = 0;
				break;
			}
		}
	} else {
		if (data >= 20)
			msleep(data);
		else
			mdelay(data);
	}

	return ret;
}

static int sprd_qvga_burst_write_samsung(struct sprd_qvga_dev_info_tag *p_dev,
                struct sprd_qvga_reg_tag *p_reg_table,
				uint32_t init_table_size)
{
	int ret = 0;
	uint32_t i = 0;
	uint32_t written_num = 0;
	uint32_t wr_num_once = 0;
	uint8_t *p_reg_val_tmp = 0;
	struct i2c_msg msg_w;
	int cnt = 0;
	struct sprd_qvga_reg_tag reg_tag = {0};

	if (!p_dev || !p_dev->i2c_info) {
		pr_err("%s, error\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	p_reg_val_tmp = (uint8_t *)kzalloc(init_table_size * sizeof(struct sprd_qvga_reg_tag), GFP_KERNEL);
    if (!p_reg_val_tmp) {
		pr_err("%s, error\n", __func__);
        ret = -ENOMEM;
		goto exit;
	}

	while(written_num < init_table_size) {
		if (0x6004 == p_reg_table[written_num].reg_addr) {
			wr_num_once = 0;
			//first write , burst start signal& select page & set base-addr
			for(i = 0; i < 3; i++) {
				reg_tag.reg_addr = p_reg_table[written_num].reg_addr;
				reg_tag.reg_value = p_reg_table[written_num].reg_value;
				ret = sprd_qvga_write_reg(p_dev, &reg_tag);
				if (ret) {
					pr_err("sprd_qvga_burst_write_samsung failed!\n");
					goto exit;
				}
				written_num ++;
			}
			//second write, burst reg
			while(p_reg_table[written_num].reg_addr != 0x6004) {
				if(0 == wr_num_once) {
						p_reg_val_tmp[wr_num_once++] =
							(uint8_t) ((p_reg_table[written_num].reg_addr >> 8) & 0xff);
						p_reg_val_tmp[wr_num_once++] =
							(uint8_t) (p_reg_table[written_num].reg_addr & 0xff);
						p_reg_val_tmp[wr_num_once++] =
							(uint8_t) ((p_reg_table[written_num].reg_value >> 8) & 0xff);
						p_reg_val_tmp[wr_num_once++] =
							(uint8_t) (p_reg_table[written_num].reg_value & 0xff);
				} else {
						p_reg_val_tmp[wr_num_once++] =
							(uint8_t) ((p_reg_table[written_num].reg_value >> 8) & 0xff);
						p_reg_val_tmp[wr_num_once++] =
							(uint8_t) (p_reg_table[written_num].reg_value & 0xff);
				}
				written_num ++;
			}

			for(i = 0; i < SPRD_QVGA_I2C_OP_TRY_NUM; i++) {
				msg_w.addr = p_dev->i2c_info->addr;
				if (1000000 == p_dev->i2c_clock)
					msg_w.flags = 0 | I2C_1M_FLAG_QVGA;
				else
					msg_w.flags = 0 | I2C_400K_FLAG_QVGA;
				msg_w.buf = p_reg_val_tmp;
				msg_w.len = wr_num_once;
				cnt = i2c_transfer(p_dev->i2c_info->adapter, &msg_w, 1);
				if (cnt != SPRD_QVGA_I2C_WRITE_SUCCESS_CNT) {
					if(i < (SPRD_QVGA_I2C_OP_TRY_NUM - 1)) {
						continue;
					}
					pr_err("sprd_qvga_burst_write_samsung failed!\n");
					ret = -EINVAL;
					goto exit;
				} else {
					break;
				}
			}

			//last write, burst stop signal
			reg_tag.reg_addr = p_reg_table[written_num].reg_addr;
			reg_tag.reg_value = p_reg_table[written_num].reg_value;
			ret = sprd_qvga_write_reg(p_dev, &reg_tag);
			if (ret) {
				pr_err("sprd_qvga_burst_write_samsung failed!\n");
				goto exit;
			}
			written_num ++;
		} else {
			reg_tag.reg_addr = p_reg_table[written_num].reg_addr;
			reg_tag.reg_value = p_reg_table[written_num].reg_value;
			ret = sprd_qvga_write_reg(p_dev, &reg_tag);
			if (ret) {
				pr_err("sprd_qvga_burst_write_samsung failed!\n");
				goto exit;
			}
			written_num ++;
		}
	}

exit:
	if (p_reg_val_tmp)
		kfree(p_reg_val_tmp);

	return ret;
}

static int sprd_qvga_burst_write_reg16_val8(struct sprd_qvga_dev_info_tag *p_dev,
                struct sprd_qvga_reg_tag *p_reg_table,
				uint32_t init_table_size)
{
	int ret = 0;
	uint32_t i = 0;
	uint32_t written_num = 0;
	uint32_t wr_num_once = 0;
	uint8_t *p_reg_val_tmp = 0;
	struct i2c_msg msg_w;
	int cnt = 0;

	if (!p_dev || !p_dev->i2c_info) {
		pr_err("%s, error\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	p_reg_val_tmp = (uint8_t *)kzalloc(init_table_size * sizeof(struct sprd_qvga_reg_tag), GFP_KERNEL);
    if (!p_reg_val_tmp) {
		pr_err("%s, error\n", __func__);
        ret = -ENOMEM;
		goto exit;
	}

	while (written_num < init_table_size) {
		wr_num_once = 0;
		p_reg_val_tmp[wr_num_once++] =
			(uint8_t) ((p_reg_table[written_num].reg_addr >> 8) & 0xff);
		p_reg_val_tmp[wr_num_once++] =
			(uint8_t) (p_reg_table[written_num].reg_addr & 0xff);
		p_reg_val_tmp[wr_num_once++] =
			(uint8_t) (p_reg_table[written_num].reg_value & 0xff);
		written_num++;

		while ((written_num < init_table_size) &&
			(p_reg_table[written_num].reg_addr ==
			p_reg_table[written_num - 1].reg_addr + 1)) {
			p_reg_val_tmp[wr_num_once++] =
				(uint8_t) (p_reg_table[written_num].reg_value & 0xff);
			written_num++;
		}

		for(i = 0; i < SPRD_QVGA_I2C_OP_TRY_NUM; i++) {
			msg_w.addr = p_dev->i2c_info->addr;
			if (1000000 == p_dev->i2c_clock)
				msg_w.flags = 0 | I2C_1M_FLAG_QVGA;
			else
				msg_w.flags = 0 | I2C_400K_FLAG_QVGA;
            msg_w.buf = p_reg_val_tmp;
			msg_w.len = wr_num_once;
			cnt = i2c_transfer(p_dev->i2c_info->adapter, &msg_w, 1);
			if (cnt != SPRD_QVGA_I2C_WRITE_SUCCESS_CNT) {
				if(i < (SPRD_QVGA_I2C_OP_TRY_NUM - 1)) {
					continue;
				}
				pr_err("sprd_qvga_burst_write_common failed!\n");
				ret = -EINVAL;
				goto exit;
			} else {
				break;
			}
		}

	}
exit:
	if (p_reg_val_tmp)
		kfree(p_reg_val_tmp);

	return ret;
}

static int sprd_qvga_burst_write_reg16_val16(struct sprd_qvga_dev_info_tag *p_dev,
                struct sprd_qvga_reg_tag *p_reg_table,
				uint32_t init_table_size)
{
	int ret = 0;
	uint32_t i = 0;
	uint32_t written_num = 0;
	uint32_t wr_num_once = 0;
	uint8_t *p_reg_val_tmp = 0;
	struct i2c_msg msg_w;
	int cnt = 0;

	if (!p_dev || !p_dev->i2c_info) {
		pr_err("%s, error\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	p_reg_val_tmp = (uint8_t *)kzalloc(init_table_size * sizeof(struct sprd_qvga_reg_tag), GFP_KERNEL);
    if (!p_reg_val_tmp) {
		pr_err("%s, error\n", __func__);
        ret = -ENOMEM;
		goto exit;
	}

	while (written_num < init_table_size) {
		wr_num_once = 0;
		p_reg_val_tmp[wr_num_once++] =
			(uint8_t) ((p_reg_table[written_num].reg_addr >> 8) & 0xff);
		p_reg_val_tmp[wr_num_once++] =
			(uint8_t) (p_reg_table[written_num].reg_addr & 0xff);
		p_reg_val_tmp[wr_num_once++] =
			(uint8_t) ((p_reg_table[written_num].reg_value >> 8) & 0xff);
		p_reg_val_tmp[wr_num_once++] =
			(uint8_t) (p_reg_table[written_num].reg_value & 0xff);
		written_num++;

		while ((written_num < init_table_size) &&
			(p_reg_table[written_num].reg_addr ==
			p_reg_table[written_num - 1].reg_addr + 2)) {
			p_reg_val_tmp[wr_num_once++] =
				(uint8_t) ((p_reg_table[written_num].reg_value >> 8) & 0xff);
			p_reg_val_tmp[wr_num_once++] =
				(uint8_t) (p_reg_table[written_num].reg_value & 0xff);
			written_num++;
		}

		for(i = 0; i < SPRD_QVGA_I2C_OP_TRY_NUM; i++) {
			msg_w.addr = p_dev->i2c_info->addr;
			if (1000000 == p_dev->i2c_clock)
				msg_w.flags = 0 | I2C_1M_FLAG_QVGA;
			else
				msg_w.flags = 0 | I2C_400K_FLAG_QVGA;
            msg_w.buf = p_reg_val_tmp;
			msg_w.len = wr_num_once;
			cnt = i2c_transfer(p_dev->i2c_info->adapter, &msg_w, 1);
			if (cnt != SPRD_QVGA_I2C_WRITE_SUCCESS_CNT) {
				if(i < (SPRD_QVGA_I2C_OP_TRY_NUM - 1)) {
					continue;
				}
				pr_err("sprd_qvga_burst_write_common failed!\n");
				ret = -EINVAL;
				goto exit;
			} else {
				break;
			}
		}

	}
exit:
	if (p_reg_val_tmp)
		kfree(p_reg_val_tmp);

	return ret;
}


int sprd_qvga_write_regtab(struct sprd_qvga_dev_info_tag *p_dev,
                struct sprd_qvga_reg_tab_tag *p_reg_table)
{
	uint32_t cnt = p_reg_table->reg_count;
	int ret = 0;
	uint32_t size;
	struct sprd_qvga_reg_tag *qvga_reg_ptr;
	struct sprd_qvga_reg_tag reg_tag = {0};
	uint32_t i;
	/*struct timeval time1 = {0}, time2 = {0};*/

	size = cnt * sizeof(*qvga_reg_ptr);
	if(cnt != 0 && size/cnt != sizeof(*qvga_reg_ptr)) {
		ret = -EINVAL;
		pr_err("qvga w err: interger overflow occurs\n");
		goto exit;
	}

	qvga_reg_ptr = p_reg_table->qvga_reg_tab_ptr;

	switch (p_dev->i2c_burst_mode) {
	case SPRD_QVGA_I2C_SINGLE_WRITE: {
		for (i = 0; i < cnt; i++) {
			reg_tag.reg_addr = qvga_reg_ptr[i].reg_addr;
			reg_tag.reg_value = qvga_reg_ptr[i].reg_value;
			ret = sprd_qvga_write_reg(p_dev, &reg_tag);
			if (ret) {
				pr_err("QVGA WRITE REG TAB write reg fail\n");
				goto exit;
			}
		}
		break;
	}

	case SPRD_QVGA_I2C_BURST_SAMSUNG: {
		ret = sprd_qvga_burst_write_samsung(p_dev,
						qvga_reg_ptr, cnt);
		if (ret) {
			pr_err("sprd_qvga_burst_write_samsung failed\n");
			goto exit;
		}
		break;
	}

	case SPRD_QVGA_I2C_BURST_REG16_VAL8: {
		ret = sprd_qvga_burst_write_reg16_val8(p_dev,
						qvga_reg_ptr, cnt);
		if (ret) {
			pr_err("sprd_qvga_burst_write_common failed\n");
			goto exit;
		}
		break;
	}

	case SPRD_QVGA_I2C_BURST_REG16_VAL16: {
		ret = sprd_qvga_burst_write_reg16_val16(p_dev,
						qvga_reg_ptr, cnt);
		if (ret) {
			pr_err("sprd_qvga_burst_write_common failed\n");
			goto exit;
		}
		break;
	}

	default: {
		pr_err("invalid burst mode, turn to single write mode\n");
		for (i = 0; i < cnt; i++) {
			reg_tag.reg_addr = qvga_reg_ptr[i].reg_addr;
			reg_tag.reg_value = qvga_reg_ptr[i].reg_value;
			ret = sprd_qvga_write_reg(p_dev, &reg_tag);
			if (ret) {
				pr_err("QVGA WRITE REG TAB write reg fail\n");
				goto exit;
			}
		}
		break;
	}
	}
exit:
	return ret;
}

