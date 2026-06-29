#include "qvga_core.h"

#ifdef CONFIG_REGULATOR_SC2721
#define SPRD_QVGA_VDD_1000MV_VAL	1006250
#else
#define SPRD_QVGA_VDD_1000MV_VAL	1000000
#endif
#define SPRD_QVGA_VDD_1100MV_VAL	1100000
#define SPRD_QVGA_VDD_1200MV_VAL	1200000
#define SPRD_QVGA_VDD_1300MV_VAL	1300000
#define SPRD_QVGA_VDD_1500MV_VAL	1500000
#define SPRD_QVGA_VDD_1800MV_VAL	1800000
#define SPRD_QVGA_VDD_2000MV_VAL	2000000
#define SPRD_QVGA_VDD_2200MV_VAL      2200000
#define SPRD_QVGA_VDD_2500MV_VAL	2500000
#define SPRD_QVGA_VDD_2800MV_VAL	2800000
#define SPRD_QVGA_VDD_3000MV_VAL	3000000
#define SPRD_QVGA_VDD_3300MV_VAL	3300000
#define SPRD_QVGA_VDD_3800MV_VAL	3800000

#define SPRD_QVGA_MCLK_DISABLE 0
#define SPRD_QVGA_MAX_MCLK 96

#define SPRD_QVGA_I2C_SINGLE_WRITE      0
#define SPRD_QVGA_I2C_BURST_SAMSUNG		1
#define SPRD_QVGA_I2C_BURST_REG16_VAL8	2
#define SPRD_QVGA_I2C_BURST_REG16_VAL16	3

#define SPRD_QVGA_I2C_VAL_8BIT		0x00
#define SPRD_QVGA_I2C_VAL_16BIT		0x01
#define SPRD_QVGA_I2C_REG_8BIT		(0x00 << 1)
#define SPRD_QVGA_I2C_REG_16BIT		(0x01 << 1)
#define I2C_1M_FLAG_QVGA            0X0080
#define I2C_400K_FLAG_QVGA          0X0040
#define SPRD_QVGA_I2C_OP_TRY_NUM    3
#define SPRD_QVGA_I2C_WRITE_SUCCESS_CNT	1
#define SPRD_QVGA_I2C_READ_SUCCESS_CNT	2
#define SPRD_QVGA_WRITE_DELAY 0xffff

#define SPRD_QVGA_COMPATIBLE "sprd,qvga-ic"

enum sprd_qvga_vdd_e {
	SPRD_QVGA_VDD_3800MV = 0,
	SPRD_QVGA_VDD_3300MV,
	SPRD_QVGA_VDD_3000MV,
	SPRD_QVGA_VDD_2800MV,
	SPRD_QVGA_VDD_2500MV,
	SPRD_QVGA_VDD_2200MV,
	SPRD_QVGA_VDD_2000MV,
	SPRD_QVGA_VDD_1800MV,
	SPRD_QVGA_VDD_1500MV,
	SPRD_QVGA_VDD_1300MV,
	SPRD_QVGA_VDD_1200MV,
	SPRD_QVGA_VDD_1100MV,
	SPRD_QVGA_VDD_1000MV,
	SPRD_QVGA_VDD_CLOSED,
	SPRD_QVGA_VDD_UNUSED
};

enum SPRD_QVGA_GPIO_TAG_E {
	SPRD_QVGA_RST_GPIO_TAG_E,
	SPRD_QVGA_PWN_GPIO_TAG_E,
	SPRD_QVGA_IOVDD_GPIO_TAG_E,
	SPRD_QVGA_AVDD_GPIO_TAG_E,
	SPRD_QVGA_DVDD_GPIO_TAG_E,
	SPRD_QVGA_GPIO_TAG_MAX,
};

enum SPRD_QVGA_REGULATOR_TAG_E {
	SPRD_QVGA_REGULATOR_IOVDD_TAG_E,
	SPRD_QVGA_REGULATOR_AVDD_TAG_E,
	SPRD_QVGA_REGULATOR_DVDD_TAG_E,
	SPRD_QVGA_REGULATOR_TAG_MAX,
};

enum SPRD_QVGA_POWER_STATE_TAG_E {
	SPRD_QVGA_POWER_STATE_OFF = 0,
	SPRD_QVGA_POWER_STATE_ON,
};

enum SPRD_QVGA_IOCTL_CMD_TAG_E {
	SPRD_QVGA_IOCTL_OPEN,
	SPRD_QVGA_IOCTL_CLOSE,
	SPRD_QVGA_IOCTL_GETBV,
	SPRD_QVGA_IOCTL_CMD_MAX,
};

struct qvga_mclk_tag {
	uint32_t clock;
	char *src_name;
};

static const struct qvga_mclk_tag c_qvga_mclk_tab[] = {
	{96, "clk_96m"},
#ifdef MCLK_NEW_PROCESS
	{64, "clk_64m"},
	{51, "clk_51m2"},
#else
	{77, "clk_76m8"},
#endif
	{48, "clk_48m"},
	{26, "clk_26m"},
};

static const char *const sprd_qvga_supply_names[] = {
	"vddio",
	"vddcama",
	"vddcamd",
};

static const char *const sprd_qvga_gpio_names[] = {
	"reset-gpios",
	"power-down-gpios",
	"iovdd-gpios",
	"avdd-gpios",
	"dvdd-gpios",
};

struct sprd_qvga_dev_info_tag {
	struct mutex sync_lock;
	struct mutex set_voltage_lock;
	struct i2c_client *i2c_info;
    uint32_t i2c_reg_bits;
    int i2c_burst_mode;
	uint32_t i2c_clock;
	struct clk *qvga_clk;
	struct clk *qvga_clk_default;
	unsigned long qvga_clk_default_rate;
	struct clk *qvga_eb;
	struct clk *ccir_eb;
	int mclk_freq;
	int mclk_count;
	struct regulator *regulator_supply[SPRD_QVGA_REGULATOR_TAG_MAX];
	unsigned int power_on_count[SPRD_QVGA_REGULATOR_TAG_MAX];
	int gpio_tab[SPRD_QVGA_GPIO_TAG_MAX];
	unsigned int vdd_val[SPRD_QVGA_REGULATOR_TAG_MAX];
};

struct sprd_qvga_reg_tag {
    uint16_t reg_addr;
    uint16_t reg_value;
};

struct sprd_qvga_reg_tab_tag {
    struct sprd_qvga_reg_tag *qvga_reg_tab_ptr;
    uint32_t reg_count;
};


int sprd_qvga_set_pd_level(struct sprd_qvga_dev_info_tag *p_dev, int power_level);
int sprd_qvga_set_rst_level(struct sprd_qvga_dev_info_tag *p_dev, int power_level);
int sprd_qvga_reset(struct sprd_qvga_dev_info_tag *p_dev,
                    unsigned int level, unsigned int width);
int sprd_qvga_set_mclk(struct sprd_qvga_dev_info_tag *p_dev,
			        unsigned int set_mclk);
int sprd_qvga_set_avdd(struct sprd_qvga_dev_info_tag *p_dev,
				    unsigned int vdd_val);
int sprd_qvga_set_dvdd(struct sprd_qvga_dev_info_tag *p_dev,
				    unsigned int vdd_val);
int sprd_qvga_set_iovdd(struct sprd_qvga_dev_info_tag *p_dev,
				    unsigned int vdd_val);
int sprd_qvga_parse_dt(struct device *dev,
				struct sprd_qvga_dev_info_tag *qvga_info);
int sprd_qvga_read_reg(struct sprd_qvga_dev_info_tag *p_dev, struct sprd_qvga_reg_tag *pReg);
int sprd_qvga_write_reg(struct sprd_qvga_dev_info_tag *p_dev, struct sprd_qvga_reg_tag *pReg);
int sprd_qvga_write_regtab(struct sprd_qvga_dev_info_tag *p_dev,
                struct sprd_qvga_reg_tab_tag *p_reg_table);
int sprd_qvga_free_gpio(struct device *dev,
				struct sprd_qvga_dev_info_tag *qvga_info);
int sprd_qvga_set_i2c_burst(struct sprd_qvga_dev_info_tag *p_dev,
                            uint32_t burst_mode);