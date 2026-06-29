/***********************************************************
** Copyright (C), 2008-2016, OPPO Mobile Comm Corp., Ltd.
** ODM_WT_EDIT
** File: - hardware_info.c
** Description: source  for hardware infomation
**
** Version: 1.0
** Date : 2018/08/11
** Author: Jinfan.Hu@BSP.Kernel.Boot
**
** ------------------------------- Revision History: -------------------------------
**  	<author>		<data> 	   <version >	       <desc>
**      Jinfan.Hu       2018/08/11     1.0               source  for hardware infomation
**
****************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/hardware_info_lux.h>
#include <linux/of_platform.h>
#include <soc/oplus/system/oplus_project.h>

char Lcm_name[HARDWARE_MAX_ITEM_LONGTH];
char Sar_name[HARDWARE_MAX_ITEM_LONGTH];
char board_id[HARDWARE_MAX_ITEM_LONGTH];
static char hardwareinfo_name[HARDWARE_MAX_ITEM][HARDWARE_MAX_ITEM_LONGTH];
char hardware_id[HARDWARE_MAX_ITEM_LONGTH];

char *hardwareinfo_items[HARDWARE_MAX_ITEM] =
{
        "LCD",
        "TP",
        "MEMORY",
        "CAM_FRONT",
        "CAM_BACK",
        "BT",
        "WIFI",
        "GSENSOR",
        "PLSENSOR",
        "GYROSENSOR",
        "MSENSOR",
        "SAR",
        "GPS",
        "FM",
        "NFC",
        "BATTERY",
        "CAM_M_BACK",
        "CAM_M_FRONT",
        "BOARD_ID",
        "HARDWARE_ID",
        /* Bug 538123, zhangbin2.wt, 20200311, Add for battery info */
        "CHARGER_IC",
        "BMS_GAUGE",
};

static long atol(const char *s)
{
    unsigned long ret = 0;
    unsigned long d;
    int neg = 0;

    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (1) {
        d = (*s++) - '0';
        if (d > 9)
            break;
        ret *= 10;
        ret += d;
    }
    return neg ? -ret : ret;
}

static int atoi(const char *s)
{
    return atol(s);
}

static char *get_rfboard_id()
{
    struct device_node *cmdline_node;
    const char *cmd_line = 0;
    char *temp_id = 0;
    int ret = 0;
    cmdline_node = of_find_node_by_path("/chosen");
    ret = of_property_read_string(cmdline_node,"bootargs",&cmd_line);
    if(!ret) {
        temp_id = strstr(cmd_line,"rfboard.id=");
        if(temp_id != NULL) {
            temp_id += strlen("rfboard.id=");
            pr_info("rfboard.id=%s\n",temp_id);
        } else {
            pr_err("read rfboard.id err");
        }
    }
    pr_info("temp_id=%s\n",temp_id);
    return temp_id;
}

static char *get_pcb_version()
{
    struct device_node *cmdline_node;
    const char *cmd_line = 0;
    char *temp_pcbversion = 0;
    int ret = 0;
    cmdline_node = of_find_node_by_path("/chosen");
    ret = of_property_read_string(cmdline_node,"bootargs",&cmd_line);
    if(!ret) {
        temp_pcbversion = strstr(cmd_line,"pcb.version=");
        if(temp_pcbversion != NULL) {
            temp_pcbversion += strlen("pcb.version=");
            pr_info("pcb.version=%s\n",temp_pcbversion);
        } else {
            pr_err("read pcb.version err");
        }
    }
    pr_info("temp_pcbversion=%s\n",temp_pcbversion);
    return temp_pcbversion;
}

static char *get_project_name(void)
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
        pr_info("prj_name=%s\n",temp_name);
        }else{
            pr_err("read prj_name err");
        }
    }
    return temp_name;
}

static int isNairobi_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name != NULL &&
		(strncmp(prj_name, "25736", 5) == 0 || strncmp(prj_name, "25737", 5) == 0 || strncmp(prj_name, "25738", 5) == 0)) {
		pr_info("hardware_info is isNairobi, prjname is %s.\n", prj_name);
		return 1;
	}
	return 0;
}

static int isNairobiA_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name != NULL &&
		(strncmp(prj_name, "25739", 5) == 0 || strncmp(prj_name, "25740", 5) == 0 || strncmp(prj_name, "25741", 5) == 0 ||
		strncmp(prj_name, "25752", 5) == 0)) {
		pr_info("hardware_info is isNairobiA, prjname is %s.\n", prj_name);
		return 1;
	}
	return 0;
}

static int isNairobiP_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name != NULL &&
		(strncmp(prj_name, "26614", 5) == 0 || strncmp(prj_name, "26615", 5) == 0)) {
		pr_info("hardware_info is isNairobiP, prjname is %s\n", prj_name);
		return 1;
	}
	return 0;
}

static int isNairobiA2_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name != NULL &&
		(strncmp(prj_name, "26612", 5) == 0)) {
		pr_info("hardware_info is isNairobiA2, prjname is %s\n", prj_name);
		return 1;
	}
	return 0;
}

static int isCruiserB4_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name != NULL &&
		(strncmp(prj_name, "25391", 5) == 0 || strncmp(prj_name, "25392", 5) == 0 || strncmp(prj_name, "25393", 5) == 0 ||
		strncmp(prj_name, "25394", 5) == 0 )) {
		pr_info("hardware_info is cruiserB4,prjname is %s.\n", prj_name);
		return 1;
	}
	return 0;
}

static char *get_nfc_id_chipset()
{
    struct device_node *cmdline_node;
    const char *cmd_line = 0;
    int ret = 0;

    cmdline_node = of_find_node_by_path("/chosen");
    ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
    if (ret)
        return "Unknown";

    if (strstr(cmd_line, "nfc_id=pn560")) {
        return "NXP:PN560";
    } else if (strstr(cmd_line, "nfc_id=thn31f")) {
        return "TMS:THN31F";
    } else {
        return "Not Support";
    }
    return "Not Support";
}

static char *boardid_get(void) {
    char *s1 = "not found";
    char *rfboard_id_str = get_rfboard_id();
    int rfboard_id = rfboard_id_str ? atoi(rfboard_id_str) : -1;
    pr_err("rfboard_id is %s\n", rfboard_id_str);
    memset(board_id, 0, sizeof(board_id));
    if(isNairobi_prjname()) {
        switch (rfboard_id) {
        case(0):
                strncpy(board_id, "S19618AA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(1):
                strncpy(board_id, "S19618XA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(2):
                strncpy(board_id, "S19618KA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(3):
                strncpy(board_id, "S19618OB1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(4):
                strncpy(board_id, "S19618TA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        default:
                strncpy(board_id, "Unknown", HARDWARE_MAX_ITEM_LONGTH);
                break;
        }
    }
    else if(isNairobiA_prjname()) {
        switch (rfboard_id) {
        case(0):
                strncpy(board_id, "S19888LA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(1):
                strncpy(board_id, "S19888FA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(2):
                strncpy(board_id, "S19888AA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(3):
                strncpy(board_id, "S19888RA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(4):
                strncpy(board_id, "S19888WA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(5):
                strncpy(board_id, "S19888CB1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(6):
                strncpy(board_id, "S19888HB1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        default:
                strncpy(board_id, "Unknown", HARDWARE_MAX_ITEM_LONGTH);
                break;
        }
    }
    else if(isCruiserB4_prjname()) {
        switch (rfboard_id) {
        case(0):
                strncpy(board_id, "S19818AA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(1):
                strncpy(board_id, "S19818CA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(2):
                strncpy(board_id, "S19818FA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(3):
                strncpy(board_id, "S19818KA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        default:
                strncpy(board_id, "Unknown", HARDWARE_MAX_ITEM_LONGTH);
                break;
        }
    }
    else if(isNairobiP_prjname()) {
        switch (rfboard_id) {
        case(1):
                strncpy(board_id, "S19628AA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(2):
                strncpy(board_id, "S19628GA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        default:
                strncpy(board_id, "Unknown", HARDWARE_MAX_ITEM_LONGTH);
                break;
        }
    }
    else if(isNairobiA2_prjname()) {
        switch (rfboard_id) {
        case(0):
                strncpy(board_id, "S19828FA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        case(1):
                strncpy(board_id, "S19828AA1", HARDWARE_MAX_ITEM_LONGTH);
                break;
        default:
                strncpy(board_id, "Unknown", HARDWARE_MAX_ITEM_LONGTH);
                break;
        }
    }
    s1 = board_id;
    sprintf(s1, "%s", board_id);
    return s1;
}

static char *hwid_get(void) {
    char *s1 = "not found";
    char *pcb_version_str = get_pcb_version();
    int pcb_version = pcb_version_str ? atoi(pcb_version_str) : -1;
    pr_err("pcb_version is %s\n", pcb_version_str);
    memset(hardware_id, 0, sizeof(hardware_id));
    switch (pcb_version) {
    case 0:
            strncpy(hardware_id, "PVT", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 1:
            strncpy(hardware_id, "MP1", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 2:
            strncpy(hardware_id, "MP2", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 3:
            strncpy(hardware_id, "MP3", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 4:
            strncpy(hardware_id, "DVT3", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 5:
            strncpy(hardware_id, "DVT2", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 6:
            strncpy(hardware_id, "DVT1", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 7:
            strncpy(hardware_id, "EVT3", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 8:
            strncpy(hardware_id, "EVT2", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 9:
            strncpy(hardware_id, "EVT1", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 10:
            strncpy(hardware_id, "T1", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 11:
            strncpy(hardware_id, "T0", HARDWARE_MAX_ITEM_LONGTH);
            break;
    case 12:
            strncpy(hardware_id, "EVB", HARDWARE_MAX_ITEM_LONGTH);
            break;
    default:
            strncpy(hardware_id, "Unknown", HARDWARE_MAX_ITEM_LONGTH);
            break;
    }
    s1 = hardware_id;
    sprintf(s1, "%s", hardware_id);
    return s1;
}

int hardwareinfo_set_prop(int cmd, const char *name)
{
        if (cmd < 0 || cmd >= HARDWARE_MAX_ITEM)
                return -1;
        strcpy(hardwareinfo_name[cmd], name);
        return 0;
}

int __weak tid_hardware_info_get(char *buf, int size)
{
        snprintf(buf, size, "touch info interface is not ready\n");
        return 0;
}

EXPORT_SYMBOL_GPL(hardwareinfo_set_prop);

int hardwareinfo_get_gauge_type(void)
{
	int ret = 0;

	if ((isNairobi_prjname() != 0 || isNairobiA_prjname() != 0) &&
		boardid_get() != NULL) {
		if (strncmp(board_id, "S19618AA1", 9) == 0
			|| strncmp(board_id, "S19888LA1", 9) == 0) {
			pr_info("support nfg8011b-gauge ic, board_id is %s.\n", board_id);
			ret = SELECT_NFG8011B_GAUGE_IC;
		} else {
			pr_info("support platform-gauge ic, board_id is %s.\n", board_id);
			ret = SELECT_PLATFORM_GAUGE_IC;
		}
	} else if (isNairobiA2_prjname() != 0 && boardid_get() != NULL) {
		if (strncmp(board_id, "S19828FA1", 9) == 0) {
			pr_info("support nfg8011b-gauge ic, board_id is %s.\n", board_id);
			ret = SELECT_NFG8011B_GAUGE_IC;
		} else {
			pr_info("support platform-gauge ic, board_id is %s.\n", board_id);
			ret = SELECT_PLATFORM_GAUGE_IC;
		}
	} else {
		pr_info("not support gauge select.\n");
		ret = NO_SUPPORT_GAUGE_SELECT;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(hardwareinfo_get_gauge_type);

bool hardwareinfo_replace_new_battery(void)
{
	bool ret = false;
	char *pcb_version_str = get_pcb_version();
	int pcb_version = pcb_version_str ? atoi(pcb_version_str) : -1;

	if (isNairobi_prjname() != 0 && boardid_get() != NULL) {
		if (pcb_version >= 0 && pcb_version <= 8) {/* 0:PVT  8:EVT2 */
			pr_info("hwid=%s, EVT2 and later.\n", hwid_get());
			ret = true;
		} else {
			pr_info("hwid=%s, before EVT2.\n", hwid_get());
			ret = false;
		}
	} else {
		pr_info("not support replace new battery.\n");
		ret = false;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(hardwareinfo_replace_new_battery);

static long hardwareinfo_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        int ret = 0, hardwareinfo_num;
        void __user *data = (void __user *)arg;

        switch (cmd) {
        case HARDWARE_LCD_GET:
                hardwareinfo_num = HARDWARE_LCD;
                break;
        case HARDWARE_TP_GET:
                hardwareinfo_num = HARDWARE_TP;
                break;
        case HARDWARE_FLASH_GET:
                hardwareinfo_num = HARDWARE_FLASH;
                break;
        case HARDWARE_FRONT_CAM_GET:
                hardwareinfo_num = HARDWARE_FRONT_CAM;
                break;
        case HARDWARE_BACK_CAM_GET:
                hardwareinfo_num = HARDWARE_BACK_CAM;
                break;
        case HARDWARE_BACK_SUBCAM_GET:
                hardwareinfo_num = HARDWARE_BACK_SUBCAM;
                break;
        case HARDWARE_BT_GET:
                hardwareinfo_set_prop(HARDWARE_BT, "Qualcomm:WCN3950");
                hardwareinfo_num = HARDWARE_BT;
                break;
        case HARDWARE_WIFI_GET:
                hardwareinfo_set_prop(HARDWARE_WIFI, "Qualcomm:WCN3950");
                hardwareinfo_num = HARDWARE_WIFI;
                break;
        case HARDWARE_ACCELEROMETER_GET:
                hardwareinfo_num = HARDWARE_ACCELEROMETER;
                break;
        case HARDWARE_ALSPS_GET:
                hardwareinfo_num = HARDWARE_ALSPS;
                break;
        case HARDWARE_GYROSCOPE_GET:
                hardwareinfo_num = HARDWARE_GYROSCOPE;
                break;
        case HARDWARE_MAGNETOMETER_GET:
                hardwareinfo_num = HARDWARE_MAGNETOMETER;
                break;
        /*bug 417945 , add sar info, chenrongli.wt, 20181218, begin*/
        case HARDWARE_SAR_GET:
                hardwareinfo_set_prop(HARDWARE_SAR, Sar_name);
                hardwareinfo_num = HARDWARE_SAR;
                break;
        case HARDWARE_GPS_GET:
                hardwareinfo_set_prop(HARDWARE_GPS, "Qualcomm:WTR2965");
                hardwareinfo_num = HARDWARE_GPS;
                break;
        case HARDWARE_FM_GET:
                hardwareinfo_set_prop(HARDWARE_FM, "Qualcomm:WCN3950");
                hardwareinfo_num = HARDWARE_FM;
                break;
        case HARDWARE_BATTERY_ID_GET:
                hardwareinfo_num = HARDWARE_BATTERY_ID;
                break;
        /* +Bug 538123, zhangbin2.wt, 20200311, Add for battery info, Begin +++ */
        case HARDWARE_CHARGER_IC_INFO_GET:
                hardwareinfo_num = HARDWARE_CHARGER_IC;
                break;
        case HARDWARE_BMS_GAUGE_GET:
                hardwareinfo_num = HARDWARE_BMS_GAUGE;
                break;
        /* -Bug 538123, zhangbin2.wt, 20200311, Add for battery info, End --- */
        case HARDWARE_BACK_CAM_MOUDULE_ID_GET:
                hardwareinfo_num = HARDWARE_BACK_CAM_MOUDULE_ID;
                break;
        case HARDWARE_BACK_SUBCAM_MODULEID_GET:
                hardwareinfo_num = HARDWARE_BACK_SUBCAM_MODULEID;
                break;
        case HARDWARE_FRONT_CAM_MODULE_ID_GET:
                hardwareinfo_num = HARDWARE_FRONT_CAM_MOUDULE_ID;
                break;
        case HARDWARE_BOARD_ID_GET:
                boardid_get();
                hardwareinfo_set_prop(HARDWARE_BOARD_ID, board_id);
                hardwareinfo_num = HARDWARE_BOARD_ID;
                break;
        case HARDWARE_HARDWARE_ID_GET:
                hwid_get();
                hardwareinfo_set_prop(HARDWARE_HARDWARE_ID, hardware_id);
                hardwareinfo_num = HARDWARE_HARDWARE_ID;
                break;
        case HARDWARE_BACK_CAM_MOUDULE_ID_SET:
                if (copy_from_user(hardwareinfo_name[HARDWARE_BACK_CAM_MOUDULE_ID], data, sizeof(data))) {
                        pr_err("wgz copy_from_user error");
                        ret =  -EINVAL;
                }
                goto set_ok;
                break;
        case HARDWARE_FRONT_CAM_MODULE_ID_SET:
                if (copy_from_user(hardwareinfo_name[HARDWARE_FRONT_CAM_MOUDULE_ID], data, sizeof(data))) {
                        pr_err("wgz copy_from_user error");
                        ret =  -EINVAL;
                }
                goto set_ok;
                break;
        case HARDWARE_NFC_GET:
                hardwareinfo_set_prop(HARDWARE_NFC, get_nfc_id_chipset());
                hardwareinfo_num = HARDWARE_NFC;
                break;
        case HARDWARE_SMARTPA_IC_GET:
                pr_info("%s smartpa get ic", __func__);
                hardwareinfo_num = HARDWARE_SMARTPA;
                break;
        default:
                ret = -EINVAL;
                goto err_out;
        }
        if (copy_to_user(data, hardwareinfo_name[hardwareinfo_num], strlen(hardwareinfo_name[hardwareinfo_num]))) {
                ret =  -EINVAL;
        }
set_ok:
err_out:
        return ret;
}

static ssize_t show_boardinfo(struct device *dev, struct device_attribute *attr, char *buf)
{
        int i = 0;
        char temp_buffer[HARDWARE_MAX_ITEM_LONGTH];
        int buf_size = 0;

        for (i = 0; i < HARDWARE_MAX_ITEM; i++) {
                memset(temp_buffer, 0, HARDWARE_MAX_ITEM_LONGTH);
                if (i == HARDWARE_LCD) {
                        sprintf(temp_buffer, "%s : %s\n", hardwareinfo_items[i], Lcm_name);
                }
                else if (i == HARDWARE_BT || i == HARDWARE_WIFI || i == HARDWARE_GPS || i == HARDWARE_FM) {
                        sprintf(temp_buffer, "%s : %s\n", hardwareinfo_items[i], "Qualcomm");
                }
                else
                {
                        sprintf(temp_buffer, "%s : %s\n", hardwareinfo_items[i], hardwareinfo_name[i]);
                }
                strcat(buf, temp_buffer);
                buf_size +=strlen(temp_buffer);
        }

        return buf_size;
}
static DEVICE_ATTR(boardinfo, S_IRUGO, show_boardinfo, NULL);

static int boardinfo_probe(struct platform_device *pdev)
{
        struct device *dev = &pdev->dev;
        int rc = 0;

        printk("%s: start\n", __func__);

        rc = device_create_file(dev, &dev_attr_boardinfo);
        if (rc < 0)
                return rc;

        dev_info(dev, "%s: ok\n", __func__);

        return 0;
}

static int boardinfo_remove(struct platform_device *pdev)
{
        struct device *dev = &pdev->dev;

        device_remove_file(dev, &dev_attr_boardinfo);
        dev_info(&pdev->dev, "%s\n", __func__);
        return 0;
}

static struct file_operations hardwareinfo_fops = {
        .owner = THIS_MODULE,
        .open = simple_open,
        .unlocked_ioctl = hardwareinfo_ioctl,
        .compat_ioctl = hardwareinfo_ioctl,
};

static struct miscdevice hardwareinfo_device = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "hardwareinfo",
        .fops = &hardwareinfo_fops,
};

static struct of_device_id boardinfo_of_match[] = {
        { .compatible = "lux:boardinfo", },
        {}
};

static struct platform_driver boardinfo_driver = {
        .driver = {
                .name   = "boardinfo",
                .owner  = THIS_MODULE,
                .of_match_table = boardinfo_of_match,
        },
        .probe          = boardinfo_probe,
        .remove         = boardinfo_remove,
};

static int __init hardwareinfo_init_module(void)
{
        int ret, i;

        for (i = 0; i < HARDWARE_MAX_ITEM; i++)
                strcpy(hardwareinfo_name[i], "NULL");

        ret = misc_register(&hardwareinfo_device);
        if (ret < 0) {
                return -ENODEV;
        }

        ret = platform_driver_register(&boardinfo_driver);
        if (ret != 0) {
                return -ENODEV;
        }

        return 0;
}

static void __exit hardwareinfo_exit_module(void)
{
        misc_deregister(&hardwareinfo_device);
}

module_init(hardwareinfo_init_module);
module_exit(hardwareinfo_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ming He <heming@wingtech.com>");
