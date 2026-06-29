#include "../../../../fs/proc/internal.h"
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/device_info.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/of_platform.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/minmax.h>
struct pinctrl *p;
extern int search_str(char* str, char **result);

#define DEVINFO_NAME "devinfo"
#define INFO_BUF_LEN 64
/**for definfo log**/
#define log_fmt(fmt) "[line:%d][module:%s][%s] " fmt

#define DEVINFO_ERR(a, arg...)                                           \
	do {                                                                 \
		printk(KERN_NOTICE log_fmt(a), __LINE__, DEVINFO_NAME, __func__, \
			   ##arg);                                                   \
	} while (0)

#define DEVINFO_MSG(a, arg...)                                                 \
	do {                                                                       \
		printk(KERN_INFO log_fmt(a), __LINE__, DEVINFO_NAME, __func__, ##arg); \
	} while (0)

/**definfo log end**/
static char *devinfo_prj_name = "unknow";
static char *devinfo_pcb_version = "unknow";
int dtsi_name = -1;
int temp_value = -1;
typedef struct{
	short board_id_num;
	char *pcba_type_name;
} BOARD_TYPE_TABLE;

enum{
	BOARD_ZERO,
	BOARD_ONE,
	BOARD_TWO,
	BOARD_THREE,
	BOARD_FOUR,
	BOARD_FIVE,
	BOARD_SIX,
	BOARD_SEVEN,
	BOARD_MAX,
};

static BOARD_TYPE_TABLE pcba_type_table_nico[] = {
		{ .board_id_num = BOARD_ZERO,  .pcba_type_name = "EVB"},
		{ .board_id_num = BOARD_ONE,   .pcba_type_name = "T0-1"},
		{ .board_id_num = BOARD_TWO,   .pcba_type_name = "T0-2"},
		{ .board_id_num = BOARD_THREE, .pcba_type_name = "EVT1"},
		{ .board_id_num = BOARD_FOUR,  .pcba_type_name = "DVT"},
		{ .board_id_num = BOARD_FIVE,  .pcba_type_name = "PVT"},
		{ .board_id_num = BOARD_SIX,   .pcba_type_name = "MP-0"},
		{ .board_id_num = BOARD_SEVEN, .pcba_type_name = "MP-1"},
		{ .board_id_num = BOARD_MAX,   .pcba_type_name = "NULL"},
};

static BOARD_TYPE_TABLE pcba_type_table_nicky[] = {
		{ .board_id_num = BOARD_ZERO,  .pcba_type_name = "EVB"},
		{ .board_id_num = BOARD_ONE,   .pcba_type_name = "T0-1"},
		{ .board_id_num = BOARD_TWO,   .pcba_type_name = "EVT1"},
		{ .board_id_num = BOARD_THREE, .pcba_type_name = "EVT2"},
		{ .board_id_num = BOARD_FOUR,  .pcba_type_name = "DVT"},
		{ .board_id_num = BOARD_FIVE,  .pcba_type_name = "PVT"},
		{ .board_id_num = BOARD_SIX,   .pcba_type_name = "MP-0"},
		{ .board_id_num = BOARD_SEVEN, .pcba_type_name = "MP-1"},
		{ .board_id_num = BOARD_MAX,   .pcba_type_name = "NULL"},
};

static struct of_device_id devinfo_id[] = {
	{
		.compatible = "oplus, device_info",
	},
	{},
};

struct devinfo_data {
	struct platform_device *devinfo;
	struct pinctrl *pinctrl;
	int hw_id1_gpio;
	int hw_id2_gpio;
	int hw_id3_gpio;
	int sub_hw_id1;
	int sub_hw_id2;
	int main_hw_id5;
	int main_hw_id6;
	int sub_board_id;
	int sdcard_gpio;
	struct pinctrl_state *hw_sub_id_sleep;
	struct pinctrl_state *hw_sub_id_active;
	struct pinctrl_state *hw_main_id5_active;
	struct pinctrl_state *hw_main_id6_active;
	int ant_select_gpio;
	struct manufacture_info sub_mainboard_info;
};

static struct devinfo_data *dev_info;
static struct proc_dir_entry *parent = NULL;
char buff[128];
char sensor_vendor[3][80];
char fuse_vendor[80];

struct sensor_devinfo sensordevinfo[] = {
	{"light_stk3a5x", "SensorTek"},
	{"light_mn78911", "mn"},
	{"accelerometer_mc3416", "MCUBE"},
	{"accelerometer_sc7a20", "SLANWEI"},
	{"magnetic_mmc5603", "MMC"},
	{"magnetic_akm09919", "AKM"},
};

static void hq_parse_sensor_devinfo(int type, char ic_name[], char vendor[])
{
	sprintf(sensor_vendor[type], "Device version:\t\t%s\nDevice manufacture:\t\t%s\n",
			ic_name, vendor);
}

void hq_register_sensor_info(int type, char ic_name[])
{
	int i;
	if (type >= 0 && type < 3) {
		for (i = 0; i < sizeof(sensordevinfo) / sizeof(struct sensor_devinfo); i++) {
			if (!strncmp(ic_name, sensordevinfo[i].ic_name, strlen(ic_name))) {
				hq_parse_sensor_devinfo(type, sensordevinfo[i].ic_name, sensordevinfo[i].vendor_name);
				break;
			}
		}
	}
	return;
}

static const char *const devinfo_proc_list[] = {
	"Sensor_accel",
	"Sensor_alsps",
	"Sensor_gyro",
	"Sensor_msensor",
	"Fuse_flag",
};

HQ_DEVINFO_ATTR(Sensor_accel, "%s", sensor_vendor[ACCEL_HQ]);
HQ_DEVINFO_ATTR(Sensor_alsps, "%s", sensor_vendor[ALSPS_HQ]);
HQ_DEVINFO_ATTR(Sensor_gyro, "%s", sensor_vendor[ACCEL_HQ]);
HQ_DEVINFO_ATTR(Sensor_msensor, "%s", sensor_vendor[MSENSOR_HQ]);
HQ_DEVINFO_ATTR(Fuse_flag, "%s", fuse_vendor);

/*
static const struct proc_ops *proc_fops_list[] = {
	&Sensor_accel_fops,
	&Sensor_alsps_fops,
	&Sensor_gyro_fops,
	&Sensor_msensor_fops,
	&Fuse_flag_fops,
};
*/
static void *device_seq_start(struct seq_file *s, loff_t *pos)
{
	static unsigned long counter = 0;
	if (*pos == 0) {
		return &counter;
	} else {
		*pos = 0;
		return NULL;
	}
}

static void *device_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return NULL;
}

static void device_seq_stop(struct seq_file *s, void *v) { return; }

static int device_seq_show(struct seq_file *s, void *v)
{
	struct proc_dir_entry *pde = s->private;
	struct manufacture_info *info = pde->data;
	if (info) {
		seq_printf(s, "Device version:\t\t%s\nDevice manufacture:\t\t%s\n",
				   info->version, info->manufacture);
	}
	return 0;
}

static struct seq_operations device_seq_ops = {.start = device_seq_start,
											   .next = device_seq_next,
											   .stop = device_seq_stop,
											   .show = device_seq_show};

static int device_proc_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &device_seq_ops);
	pr_err("%s is called\n", __func__);

	if (!ret) {
		struct seq_file *sf = file->private_data;
		sf->private = PDE(inode);
	}
	pr_err("%s is end\n", __func__);
	return ret;
}

static const struct proc_ops device_node_fops = {
	.proc_open = device_proc_open,
	.proc_read = seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release = seq_release,
};

int register_device_proc(char *name, char *version, char *manufacture)
{
	struct proc_dir_entry *d_entry;
	struct manufacture_info *info;
	pr_err("%s is called\n", __func__);
	if (!parent) {
		parent = proc_mkdir("devinfo", NULL);
		if (!parent) {
			pr_err("can't create devinfo proc\n");
			return -ENOENT;
		}
	}

	info = kzalloc(sizeof *info, GFP_KERNEL);
	info->version = version;
	info->manufacture = manufacture;
	d_entry = proc_create_data(name, S_IRUGO, parent, &device_node_fops, info);
	if (!d_entry) {
		DEVINFO_ERR("create %s proc failed.\n", name);
		kfree(info);
		return -ENOENT;
	}
	pr_err("%s is end\n", __func__);
	return 0;
}

int register_devinfo(char *name, struct manufacture_info *info)
{
	struct proc_dir_entry *d_entry;
	if (!parent) {
		parent = proc_mkdir("devinfo", NULL);
		if (!parent) {
			pr_err("can't create devinfo proc\n");
			return -ENOENT;
		}
	}

	d_entry = proc_create_data(name, S_IRUGO, parent, &device_node_fops, info);
	if (!d_entry) {
		pr_err("create %s proc failed.\n", name);
		return -ENOENT;
	}
	return 0;
}

static int mainboard_init(void)
{
	pr_err("%s is called\n", __func__);
	if ((0 > temp_value) || (BOARD_MAX <= temp_value)) {
		pr_err("temp_value error %d\n", temp_value);
		return register_device_proc("mainboard", "UNKNOW", devinfo_prj_name);
	}
	if (dtsi_name == 21660) {
		devinfo_pcb_version = pcba_type_table_nico[temp_value].pcba_type_name;
	} else if (dtsi_name == 0x2171A || dtsi_name == 21723) {
		devinfo_pcb_version = pcba_type_table_nicky[temp_value].pcba_type_name;
	} else {
		pr_err("dtsi_name error %d\n", dtsi_name);
		return register_device_proc("mainboard", "DTSI_NAME UNKNOW", devinfo_prj_name);
	}
	pr_err("%s is end\n", __func__);
	return register_device_proc("mainboard", devinfo_pcb_version, devinfo_prj_name);
}

static int subboard_init(struct devinfo_data *const devinfo_data)
{
	int ret = 0;
	struct device_node *np = NULL;
	pr_err("%s is called\n", __func__);
	np = devinfo_data->devinfo->dev.of_node;

	devinfo_data->sub_board_id = of_get_named_gpio(np, "sub_board-gpio", 0);
	if (devinfo_data->sub_board_id < 0) {
		DEVINFO_ERR("devinfo_data->sub_board_id not specified\n");
		return -1;
	}
	DEVINFO_MSG("sub_board-gpio for match is %d \n", devinfo_data->sub_board_id);

	devinfo_data->sub_mainboard_info.manufacture = kzalloc(INFO_BUF_LEN, GFP_KERNEL);
	if (devinfo_data->sub_mainboard_info.manufacture == NULL) {
		kfree(devinfo_data->sub_mainboard_info.manufacture);
		return -1;
	}

	devinfo_data->sub_mainboard_info.version = kzalloc(INFO_BUF_LEN, GFP_KERNEL);
	if (devinfo_data->sub_mainboard_info.version == NULL) {
		kfree(devinfo_data->sub_mainboard_info.version);
		return -1;
	}

	ret = register_device_proc("sub_mainboard",
							   devinfo_data->sub_mainboard_info.version, devinfo_data->sub_mainboard_info.manufacture);
	pr_err("%s is end\n", __func__);
	return ret;
}

static int config_gpio_init(struct devinfo_data *const devinfo_data)
{
	int ret = 0;
	struct gpio_desc *config_0_gpio, *config_1_gpio;

	config_0_gpio = devm_gpiod_get(&(devinfo_data->devinfo)->dev, "config_0", GPIOD_OUT_LOW);
	if (IS_ERR(config_0_gpio)) {
		DEVINFO_ERR("cannot get config0 gpio\n");
		return -1;
	}

	config_1_gpio = devm_gpiod_get(&(devinfo_data->devinfo)->dev, "config_1", GPIOD_OUT_LOW);
	if (IS_ERR(config_1_gpio)) {
		DEVINFO_ERR("cannot get config1 gpio\n");
		return -1;
	}

	ret = gpiod_direction_output_raw(config_0_gpio, 0);
	if (ret < 0) {
		DEVINFO_ERR("fail to output config0\n");
		return -1;
	}

	ret = gpiod_direction_output_raw(config_1_gpio, 0);
	if (ret < 0) {
		DEVINFO_ERR("fail to output config1\n");
		return -1;
	}

	return ret;
}

int str2uint32(const char *str, unsigned int base, u32 *result)
{
	char *ep;

	*result = simple_strtoul(str, &ep, base);
	if (ep == str || *ep != '\0')
		return -EINVAL;

	return 0;
}
static char *get_project_name()
{
	struct device_node *cmdline_node;
	const char *cmd_line = 0;
	char *temp_name = 0;
	int ret = 0;
	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node,"bootargs",&cmd_line);
	if(!ret){
		temp_name = strstr(cmd_line,"prj_name=");
		if(temp_name != NULL){
		temp_name += strlen("prj_name=");
		pr_info("prj_name_hw_param=%s\n",temp_name);
	    }else{
		    pr_err("read prj_name_hw_param err");
	    }
	}
	pr_info("temp_name=%s\n",temp_name);
	return temp_name;
}
static int isdhaka_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name !=NULL && (strncmp(prj_name,"24760",5) ==0 ||strncmp(prj_name,"24761",5) ==0 \
			 || strncmp(prj_name,"24762",5) ==0 || strncmp(prj_name,"24763",5) ==0 \
			 || strncmp(prj_name,"24764",5) ==0  || strncmp(prj_name,"24765",5) ==0 \
			 || strncmp(prj_name,"24766",5) ==0  || strncmp(prj_name,"24768",5) ==0 )) {
			printk("is dhaka prj.\n");
			return 1;
	}
	return 0;
}

static int isNairobi_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name != NULL &&
		(strncmp(prj_name, "25736", 5) == 0 || strncmp(prj_name, "25737", 5) == 0 ||
		 strncmp(prj_name, "25738", 5) == 0 || strncmp(prj_name, "25739", 5) == 0 ||
		 strncmp(prj_name, "25740", 5) == 0 || strncmp(prj_name, "25741", 5) == 0 ||
		 strncmp(prj_name, "25752", 5) == 0 || strncmp(prj_name, "26612", 5) == 0 ||
		 strncmp(prj_name, "26614", 5) == 0 || strncmp(prj_name, "26615", 5) == 0)) {
		printk("device_info is nairobi prj.\n");
		return 1;
	}
	return 0;
}

static int iscruiserB4_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name != NULL &&
		(strncmp(prj_name, "25391", 5) == 0 || strncmp(prj_name, "25392", 5) == 0 || strncmp(prj_name, "25393", 5) == 0 ||
		 strncmp(prj_name, "25394", 5) == 0 )) {
		printk("device_info is cruiserB4 prj.\n");
		return 1;
	}
	return 0;
}

static int ishulk_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name !=NULL && (strncmp(prj_name,"22716",5) ==0 ||strncmp(prj_name,"22717",5) ==0 \
	|| strncmp(prj_name,"22718",5) ==0 || strncmp(prj_name,"22720",5) ==0  \
	|| strncmp(prj_name,"22721",5) ==0 || strncmp(prj_name,"22729",5) ==0  \
	|| strncmp(prj_name,"22722",5) ==0 || strncmp(prj_name,"22723",5) ==0  \
	|| strncmp(prj_name,"22724",5) ==0 || strncmp(prj_name,"22726",5) ==0  \
	|| strncmp(prj_name,"22734",5) ==0 || strncmp(prj_name,"23665",5) ==0  \
	|| strncmp(prj_name,"23666",5) ==0 || strncmp(prj_name,"23711",5) ==0  \
	|| strncmp(prj_name,"23712",5) ==0 || strncmp(prj_name,"23713",5) ==0  \
	|| strncmp(prj_name,"23714",5) ==0 || strncmp(prj_name,"23728",5) ==0  \
	|| strncmp(prj_name,"23633",5) ==0 )) {
		printk("is hulk prj.\n");
		return 1;
	}

	return 0;
}

static int subboard_verify(struct devinfo_data *const devinfo_data)
{
	int ret = 0;
	int subboard_gpio_value = -1;
	char *str_modem_id = NULL;
	unsigned int mainboard_modem_id = -1;
	int subboard_kind = -1;

	pr_err("%s is called\n", __func__);
	DEVINFO_MSG("Enter subboard_verify\n");
	if (IS_ERR_OR_NULL(devinfo_data)) {
		DEVINFO_ERR("devinfo_data is NULL\n");
		return -1;
	}

	DEVINFO_MSG("In subboard_verify, sub_board-gpio for match is %d \n", devinfo_data->sub_board_id);
	if (devinfo_data->sub_board_id >= 0) {
		ret = gpio_request(devinfo_data->sub_board_id, "sub_board_id");
		if (ret != 0) {
			DEVINFO_ERR("failed to request sub_board_id gpio\n");
			return -1;
		}
		subboard_gpio_value = gpio_get_value(devinfo_data->sub_board_id);
		DEVINFO_MSG("subboard_gpio_value before set is %d \n", subboard_gpio_value);
		if (subboard_gpio_value < 0) {
			DEVINFO_ERR("subboard_gpio_value before set is error\n");
			gpio_free(devinfo_data->sub_board_id);
			return -1;
		}
	} else {
		DEVINFO_ERR("devinfo_data->sub_board_id not specified\n");
		gpio_free(devinfo_data->sub_board_id);
		return -1;
	}

	if (subboard_gpio_value == 0) {
		subboard_kind = 0; //The asia-pacific version
	} else {
		subboard_kind = 1; //The other version
	}
	DEVINFO_MSG("subboard_kind is %d \n", subboard_kind);

	snprintf(devinfo_data->sub_mainboard_info.version, INFO_BUF_LEN, "%d", subboard_kind);

	search_str("rfboard.id=", &str_modem_id);
	DEVINFO_MSG("str_modem_id is %s \n", str_modem_id);
	if (str_modem_id != NULL) {
		str2uint32(str_modem_id, 0, &mainboard_modem_id);
		DEVINFO_MSG("mainboard_modem_id is %d \n", mainboard_modem_id);
	}
	if(isdhaka_prjname()) {
		/*  dhaka modem id
			modem_id = 0  全频段
			modem_id = 1  国际版
			modem_id = 2  二供:全频段
			modem_id = 3  二供:国际版
			modem_id = 4  亚太版
			subboard_kind表示小板，与射频确认只有一种小板
		*/
		if((subboard_kind == 1) && ((mainboard_modem_id == 0) || (mainboard_modem_id == 1) \
			|| (mainboard_modem_id == 2) || (mainboard_modem_id == 3) || (mainboard_modem_id == 4))) {
			DEVINFO_MSG("dhaka sub-match.\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-match");
			gpio_free(devinfo_data->sub_board_id);
		} else {
			DEVINFO_MSG("dhaka sub-unmatch\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-unmatch");
			gpio_free(devinfo_data->sub_board_id);
		}
	} else if (isNairobi_prjname()) {
		/*  nairobi
			subboard_kind表示小板类型，nairobi只有一种小板必定匹配
		*/
		if (subboard_kind == 0) {
			DEVINFO_MSG("nairobi sub-match.\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-match");
			gpio_free(devinfo_data->sub_board_id);
		} else {
			DEVINFO_MSG("nairobi sub-unmatch\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-unmatch");
			gpio_free(devinfo_data->sub_board_id);
		}
	} else if (iscruiserB4_prjname()) {
		/*  cruiserB4
			subboard_kind表示小板类型，cruiserB4只有一种小板必定匹配
		*/
		if (subboard_kind == 0) {
			DEVINFO_MSG("cruiserB4 sub-match.\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-match");
			gpio_free(devinfo_data->sub_board_id);
		} else {
			DEVINFO_MSG("cruiserB4 sub-unmatch\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-unmatch");
			gpio_free(devinfo_data->sub_board_id);
		}
	} else if (ishulk_prjname()) {
		/*  hulk modem id
			modem_id = 0  亚太简版
			modem_id = 1  全频段
			modem_id = 2  国际版+NFC新方案
			modem_id = 3  亚太简版--孟加拉
			modem_id = 4  国际版+NFC
		*/
		pr_err("%s hulk_prj\n", __func__);
		if ((subboard_kind == 0 && mainboard_modem_id == 0)||(subboard_kind == 0 && mainboard_modem_id == 3)) {
			DEVINFO_MSG("sub-match , asia-pacific version\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-match");
			gpio_free(devinfo_data->sub_board_id);
		} else if (subboard_kind == 1 && mainboard_modem_id != 0) {
			DEVINFO_MSG("sub-match , other version\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-match");
			gpio_free(devinfo_data->sub_board_id);
		} else {
			DEVINFO_MSG("sub-unmatch\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-unmatch");
			gpio_free(devinfo_data->sub_board_id);
		}
	} else {
		/*  latte modem id
			modem_id = 0  亚太简版
			modem_id = 1  全频段
			modem_id = 2  亚太简-成熟方案
			modem_id = 3  全频段-成熟方案
			modem_id = 4  国际版+NFC（CD方案）
			modem_id = 5  国际版+NFC-成熟方案
		*/
		pr_err("%s latte_prj\n", __func__);
		if ((subboard_kind == 0 && mainboard_modem_id == 0)||(subboard_kind == 0 && mainboard_modem_id == 2)) {
			DEVINFO_MSG("sub-match , asia-pacific version\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-match");
			gpio_free(devinfo_data->sub_board_id);
		} else if (subboard_kind == 1 && (mainboard_modem_id != 0 && mainboard_modem_id != 2)) {
			DEVINFO_MSG("sub-match , other version\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-match");
			gpio_free(devinfo_data->sub_board_id);
		} else {
			DEVINFO_MSG("sub-unmatch\n");
			snprintf(devinfo_data->sub_mainboard_info.manufacture, INFO_BUF_LEN, "sub-unmatch");
			gpio_free(devinfo_data->sub_board_id);
		}
	}
	pr_err("%s is end\n", __func__);
	return ret;
}

static unsigned int value=0;
static ssize_t show_uart_gpio_ctrl(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = sprintf(buf, "%u\n", value);
	printk("[uart]show_uart_gpio_ctrl value:%u", value);
	return ret;
}

static ssize_t store_uart_gpio_ctrl(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret_rx, ret_tx;
	struct pinctrl_state *state_rx, *state_tx;
	int rx_gpio_num = 64 + 39;
	int tx_gpio_num = 64 + 38;
	struct gpio_desc *desc;
	ssize_t ret;

	//printk("[uart]store_uart_gpio_ctrl enter");
	ret = kstrtouint(buf, 10, &value);
	if (ret)
		printk("store_uart_gpio_ctrl sscanf");
	printk("[uart]store_uart_gpio_ctrl value:%u", value);

	if (value > 0)
	{
	printk("[uart]store_uart_gpio_ctrl value:%u not support!!!\n", value);
    } else {
		state_rx = pinctrl_lookup_state(p, "ap_uart_rx_low");
		if (IS_ERR(state_rx)) {
			printk("[uart]fail to get pin state_rx\n");
			return PTR_ERR(state_rx);
		}

		state_tx = pinctrl_lookup_state(p, "ap_uart_tx_low");
		if (IS_ERR(state_tx)) {
			printk("[uart]fail to get pin state_tx\n");
			return PTR_ERR(state_tx);
		}
		ret_rx = pinctrl_select_state(p, state_rx);
		if (ret_rx != 0) {
			printk("[uart]fail to select pin state_rx\n");
			return ret_rx;
		}

		ret_tx = pinctrl_select_state(p, state_tx);
		if (ret_tx != 0) {
			printk("[uart]fail to select pin state_tx\n");
			return ret_tx;
		}

		desc = gpio_to_desc(rx_gpio_num);
		desc = gpio_to_desc(tx_gpio_num);
		gpio_request(rx_gpio_num, "change_rx_to_gpio");
		//gpio_direction_output(rx_gpio_num, 0);
		gpio_direction_input(rx_gpio_num);

		gpio_request(tx_gpio_num, "change_tx_to_gpio");
		//gpio_direction_output(tx_gpio_num, 0);
		gpio_direction_input(tx_gpio_num);
	}

	return count;
}

static DEVICE_ATTR(uart_gpio_ctrl, 0644, show_uart_gpio_ctrl, store_uart_gpio_ctrl);

static int devinfo_probe(struct platform_device *pdev)
{
	int ret = 0;
	//struct pinctrl_state *state;
	struct devinfo_data *const devinfo_data =
		devm_kzalloc(&pdev->dev, sizeof(struct devinfo_data), GFP_KERNEL);
	pr_err("%s is called\n", __func__);
	if (IS_ERR_OR_NULL(devinfo_data)) {
		printk("devinfo_data kzalloc failed\n");
		ret = -ENOMEM;
		return ret;
	}

	devinfo_data->devinfo = pdev;
	dev_info = devinfo_data;

	if (!parent) {
		parent = proc_mkdir("devinfo", NULL);
		if (!parent) {
			printk("can't create devinfo proc\n");
			ret = -ENOENT;
		}
	}
	/*
	proc_create("devinfo/Sensor_accel", 0444, NULL, proc_fops_list[0]);
	proc_create("devinfo/Sensor_alsps", 0444, NULL, proc_fops_list[1]);
	proc_create("devinfo/Sensor_gyro", 0444, NULL, proc_fops_list[2]);
	proc_create("devinfo/Sensor_msensor", 0444, NULL, proc_fops_list[3]);
	proc_create("devinfo/Fuse_flag", 0444, NULL, proc_fops_list[4]);
	*/
	ret = register_device_proc("audio_mainboard", "SPRD", "sub-match");
	if (ret < 0) {
		DEVINFO_ERR("register audio_mainboard failed\n");
	}

	ret = mainboard_init();
	if (ret < 0) {
		DEVINFO_ERR("register mainboard failed\n");
	}

	ret = subboard_init(devinfo_data);
	if (ret < 0) {
		DEVINFO_ERR("register subboard failed\n");
	} else {
		subboard_verify(devinfo_data);
	}

	ret = config_gpio_init(devinfo_data);
	pr_info("config_gpio_init ret = %d\n", ret);

	p = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(p)) {
		pr_err("get pinctrl failed!\n");
	}

	/*change pin matrix when use ap_uart0, conect inf1 to ap_uart0*/
	/*
	state = pinctrl_lookup_state(p, "ap_uart0_1");
	if (IS_ERR(state)) {
		pr_err("fail to get pin state\n");
	} else {
		ret = pinctrl_select_state(p, state);
		if (ret != 0) {
			pr_err("fail to select pin state\n");
		}
	}
	*/

	ret = device_create_file(&pdev->dev, &dev_attr_uart_gpio_ctrl);
	if(ret)
		pr_err("device create file failed\n");

	pr_err("%s is end\n", __func__);
	return ret;
}

static int devinfo_remove(struct platform_device *dev)
{
	pr_err("%s is called\n", __func__);
	remove_proc_entry(DEVINFO_NAME, NULL);
	return 0;
}

static struct platform_driver devinfo_platform_driver = {
	.probe = devinfo_probe,
	.remove = devinfo_remove,
	.driver =
		{
			.name = DEVINFO_NAME,
			.of_match_table = devinfo_id,
		},
};

module_platform_driver(devinfo_platform_driver);

MODULE_DESCRIPTION("oplus device info");
MODULE_LICENSE("GPL v2");
