/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of wcn sysfs debug
 */

#define DRIVER_AUTHOR "Songhe Wei<songhe.wei@unisoc.com>"
#define DRIVER_DESC "support sysfs and uevent for debug wcn"
#define WCN_SYSFS_VERSION	"v0.0"

#include <linux/kobject.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include "wcn_bus.h"

#include "pcie.h"
#include "../sdio/sdiohal.h"
#include "wcn_dbg.h"
#include "wcn_glb.h"
#include "wcn_debug_bus.h"
#include "wcn_boot.h"
#include "wcn_types.h"


static bool from_ddr;
extern int is_wcn_shutdown;

bool isInAtCmd;
struct wcn_sysfs_info sysfs_info;

void wcn_send_atcmd_lock(void)
{
	mutex_lock(&sysfs_info.mutex);
}

void wcn_send_atcmd_unlock(void)
{
	mutex_unlock(&sysfs_info.mutex);
}

int notify_at_cmd_finish(void *buf, unsigned char len)
{
	sysfs_info.p = buf;
	sysfs_info.len = len;
	complete(&sysfs_info.cmd_completion);

	return 0;
}

void wcn_source_ctl_set(int flag)
{
	sysfs_info.source_ctl = flag;
}

int wcn_source_ctl_get(void)
{
	return sysfs_info.source_ctl;
}

int wcn_send_atcmd(void *cmd, size_t cmd_len, void *response, size_t *response_len)
{
	struct mbuf_t *head = NULL;
	struct mbuf_t *tail = NULL;
	int num = 1;
	int ret;
	unsigned long timeleft;
	struct sdiohal_data_t *p_data = sdiohal_get_data();
	struct wcn_match_data *g_match_config = get_wcn_match_config();
	unsigned int pub_head_rsv;
	/* dma_buf for dma */
	static struct dma_buf dma_buf;
	static int at_buf_flag;
	struct wcn_pcie_info *pcie_dev;
	/* common buf for kmalloc */
	unsigned char *com_buf = NULL;

	if (cmd_len > 128) {
		WCN_ERR("%s:cmd_len %ld should not be larger than 128\n", __func__, cmd_len);
		return -1;
	}

	if (g_match_config && !g_match_config->unisoc_wcn_integrated) {
		if (flag_download_done != 1) {
			WCN_WARN("%s:can not send atcmd before download flag is true\n", __func__);
			return -EIO;
		}
	}

	if (g_match_config && g_match_config->unisoc_wcn_pcie) {
		pcie_dev = get_wcn_device_info();
		if (!pcie_dev) {
			WCN_ERR("%s:PCIE device link error\n", __func__);
			return -1;
		}
	}

	if (g_match_config && g_match_config->unisoc_wcn_sdio) {
		pub_head_rsv = SDIOHAL_PUB_HEAD_RSV;
		if (!WCN_CARD_EXIST(&p_data->xmit_cnt)) {
			WCN_INFO("%s:already power off\n", __func__);
			return 0;
		}
	} else {
		pub_head_rsv = PUB_HEAD_RSV;
	}

	wcn_send_atcmd_lock();
	ret = sprdwcn_bus_list_alloc(0, &head, &tail, &num);
	if (ret || !head || !tail) {
		WCN_ERR("%s:%d mbuf_link_alloc fail\n", __func__, __LINE__);
		wcn_send_atcmd_unlock();
		return -1;
	}

	if (g_match_config && g_match_config->unisoc_wcn_pcie) {
		if (at_buf_flag == 0) {
			ret = dmalloc(pcie_dev, &dma_buf, 128);
			if (ret != 0) {
				wcn_send_atcmd_unlock();
				return -1;
			}
			at_buf_flag = 1;
		}
		head->buf = (unsigned char *)(dma_buf.vir);
		head->phy = (unsigned long)(dma_buf.phy);
		head->len = cmd_len;
		memset(head->buf, 0x0, head->len);
		memcpy(head->buf, cmd, cmd_len);
		head->next = NULL;
	} else {
		com_buf = kzalloc(128 + pub_head_rsv + 1, GFP_KERNEL);
		if (!com_buf) {
			wcn_send_atcmd_unlock();
			return -ENOMEM;
		}
		memcpy(com_buf + pub_head_rsv, cmd, cmd_len);
		head->buf = com_buf;
		head->len = cmd_len;
		head->next = NULL;
	}

	reinit_completion(&sysfs_info.cmd_completion);
	ret = sprdwcn_bus_push_list(0, head, tail, num);
	if (ret) {
		WCN_INFO("sprdwcn_bus_push_list error=%d\n", ret);
		if ((ret == -E_INVALIDPARA) && g_match_config && g_match_config->unisoc_wcn_sipc)
			sprdwcn_bus_list_free(0, head, tail, num);
		else if ((ret == -ENODEV) && g_match_config && g_match_config->unisoc_wcn_sdio)
			sprdwcn_bus_list_free(0, head, tail, num);
		wcn_send_atcmd_unlock();
		return -ENOMEM;
	}
	timeleft = wait_for_completion_timeout(&sysfs_info.cmd_completion,
					       3 * HZ);
	if (g_match_config && g_match_config->unisoc_wcn_sdio) {
		if (!WCN_CARD_EXIST(&p_data->xmit_cnt)) {
			WCN_INFO("%s:already power off\n", __func__);
			wcn_send_atcmd_unlock();
			return 0;
		}
	}

	if (!timeleft) {
		WCN_ERR("%s,Timeout(%d sec),didn't get at cmd(%s) response\n",
			__func__, jiffies_to_msecs(3 * HZ) / 1000, (char *)cmd);
		wcn_send_atcmd_unlock();
		return -ETIMEDOUT;
	}
	if (!response) {
		wcn_send_atcmd_unlock();
		return 0;
	}

	if (*response_len == WCN_AT_RSP_RAW_FLAG)
		memcpy(response, sysfs_info.p, sysfs_info.len);
	else
		scnprintf(response, (size_t)sysfs_info.len, "%s", (char *)sysfs_info.p);

	*response_len = sysfs_info.len;
	scnprintf(response, (size_t)sysfs_info.len, "%s",
		  (char *)sysfs_info.p);
	WCN_DBG("len=%zu, buf=%s\n", *response_len, (char *)(response));
	wcn_send_atcmd_unlock();

	return 0;
}

char *__wcn_get_sw_ver(void)
{
	return sysfs_info.sw_ver_buf;
}
EXPORT_SYMBOL_GPL(__wcn_get_sw_ver);

static int wcn_get_sw_ver(void)
{
	char a[] = "at+spatgetcp2info\r\n";

	/*
	 * len is 64bit, belows function 32bit, need assigned 0 to len,
	 * make sure high 32bit=0
	 */
	wcn_send_atcmd(a, strlen(a), &sysfs_info.sw_ver_buf,
		       &sysfs_info.sw_ver_len);

	WCN_DBG("show:len=%zd, buf=%s\n", sysfs_info.sw_ver_len,
		sysfs_info.sw_ver_buf);

	return 0;
}

int wcn_set_armlog_status(void)
{
	char a[16];

	scnprintf(a, (size_t)sizeof(a), "%s%d%s", "at+armlog=",
		  sysfs_info.armlog_status, "\r\n");
	WCN_INFO("%s:%s", __func__, a);
	wcn_send_atcmd(a, strlen(a), NULL, NULL);

	return 0;
}

static int wcn_set_loglevel(void)
{
	char a[64];

	if (atomic_read(&sysfs_info.set_mask)
		& WCN_SYSFS_LOGLEVEL_SET_BIT) {
		scnprintf(a, (size_t)sizeof(a), "%s%d%s", "at+loglevel=",
			  sysfs_info.loglevel, "\r\n");
		WCN_INFO("%s:%s\n", __func__, a);
		wcn_send_atcmd(a, strlen(a), NULL, NULL);
		atomic_and((unsigned int)(~(WCN_SYSFS_LOGLEVEL_SET_BIT)),
			   &sysfs_info.set_mask);
	}

	return 0;
}

static int wcn_get_loglevel(void)
{
	unsigned long tmp;
	int ret;
	char a[] = "at+loglevel?\r\n";

	wcn_send_atcmd(a, strlen(a), &sysfs_info.loglevel_buf,
		       &sysfs_info.loglevel_len);

	ret = kstrtoul(sysfs_info.loglevel_buf + 11, 10, &tmp);
	if (ret < 0) {
		WCN_ERR("incorrect get loglevel\n");
		return -EINVAL;
	}

	WCN_DBG("show:len=%zd, buf=%s, level=%d\n", sysfs_info.loglevel_len,
		sysfs_info.loglevel_buf, sysfs_info.loglevel);

	return 0;
}

void wcn_firmware_init(void)
{
	wcn_get_sw_ver();
	wcn_set_armlog_status();
	wcn_set_loglevel();
	wcn_get_loglevel();
	/* TODO: set can pass functionmask */
	/* wcn_set_loglevel, etc */
}

void wcn_firmware_init_wq(struct work_struct *work)
{
	wcn_firmware_init();
}

static ssize_t wcn_sysfs_show_sleep_state(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", "not support to read");
}

static ssize_t wcn_sysfs_store_sleep_state(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	unsigned long res;
	int ret;
	char a[64];

	ret = kstrtoul(buf, 10, &res);
	if (ret < 0) {
		WCN_ERR("incorrect value written to sleep\n");
		return -EINVAL;
	}

	if (res == 1) {
		/* close CP2 sleep function */
		scnprintf(a, (size_t)sizeof(a), "%s", "at+debug=1\r\n");
		wcn_send_atcmd(a, strlen(a), NULL, NULL);
	} else if (res == 0) {
		/* open CP2 sleep function */
		scnprintf(a, (size_t)sizeof(a), "%s", "at+debug=0\r\n");
		wcn_send_atcmd(a, strlen(a), NULL, NULL);
	} else {
		WCN_ERR("incorrect value written to sleep\n");
		return -EINVAL;
	}

	return count;
}

/* S_IRUGO | S_IWUSR */
static DEVICE_ATTR(sleep_state, 0644,
		   wcn_sysfs_show_sleep_state,
		   wcn_sysfs_store_sleep_state);

static ssize_t wcn_sysfs_show_sw_ver(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	size_t len = 0;
	char a[] = "at+spatgetcp2info\r\n";
	isInAtCmd = true;

	WCN_INFO("%s \n", __func__);
	if (!marlin_get_module_status()) {
		memcpy(buf, sysfs_info.sw_ver_buf, sysfs_info.sw_ver_len);
		isInAtCmd = false;
		return sysfs_info.sw_ver_len;
	}

	/*
	 * len is 64bit, belows function 32bit, need assigned 0 to len,
	 * make sure high 32bit=0
	 */
	wcn_send_atcmd(a, strlen(a), buf, &len);

	WCN_INFO("show:len=%zd, buf=%s\n", len, buf);
	/* because cp2 pass wrong len */
	len = strlen(buf);
	WCN_INFO("show:len=%zd\n", len);
	isInAtCmd = false;

	return len;
}

/* S_IRUGO */
static DEVICE_ATTR(sw_ver, 0444,
		   wcn_sysfs_show_sw_ver, NULL);

static ssize_t wcn_sysfs_read_fwlog(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *bin_attr,
				    char *buffer, loff_t pos, size_t count)
{
	return 0;
}

/* S_IRUSR */
static const struct bin_attribute fwlog_attr = {
	.attr = {.name = "fwlog", .mode = 0400},
	.read = wcn_sysfs_read_fwlog,
};

static ssize_t wcn_sysfs_show_hw_ver(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	ssize_t len;

	len = PAGE_SIZE;
	mutex_lock(&sysfs_info.mutex);
	len = snprintf(buf, len, "%s\n", wcn_get_chip_name());
	mutex_unlock(&sysfs_info.mutex);

	return len;
}

static ssize_t wcn_sysfs_store_hw_ver(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(hw_ver, 0644,
		   wcn_sysfs_show_hw_ver,
		   wcn_sysfs_store_hw_ver);

static ssize_t wcn_sysfs_show_watchdog_state(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	ssize_t len;

	len = PAGE_SIZE;
	mutex_lock(&sysfs_info.mutex);

	/* len:4 */
	len = snprintf(buf, len, "not support\n");
	mutex_unlock(&sysfs_info.mutex);

	return len;
}

static ssize_t wcn_sysfs_store_watchdog_state(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	size_t len = 0;
	char a[] = "at+cp2_enter_user=?\r\n";

	WCN_INFO("%s: buf=%s\n", __func__, buf);

	if (strncmp(buf, "1", 1) == 0) {
		scnprintf(a, (size_t)sizeof(a), "%s",
			  "at+cp2_enter_user=1\r\n");
		wcn_send_atcmd(a, strlen(a), NULL, NULL);
	} else if (strncmp(buf, "0", 1) == 0) {
		scnprintf(a, (size_t)sizeof(a), "%s",
			  "at+cp2_enter_user=0\r\n");
		wcn_send_atcmd(a, strlen(a), NULL, NULL);
	} else {
		return -EINVAL;
	}

	len = strlen(buf);
	WCN_INFO("show:len=%zd, count=%zd\n", len, count);

	return count;
}

static DEVICE_ATTR(watchdog_state, 0644,
		   wcn_sysfs_show_watchdog_state,
		   wcn_sysfs_store_watchdog_state);

static ssize_t wcn_sysfs_show_armlog_status(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	size_t len = 0;
	char a[] = "at+armlog?\r\n";

	wcn_send_atcmd(a, strlen(a), buf, &len);

	WCN_INFO("%s:len=%zd, buf=%s\n", __func__, len, buf);
	if (!memcmp(buf, "+ARMLOG: ", strlen("+ARMLOG: "))) {
		buf[0] = buf[strlen("+ARMLOG: ")];
		buf[1] = '\0';
	} else {
		buf[0] = '\0';
	}

	len = strlen(buf);
	WCN_INFO("show:len=%zd\n", len);

	return len;
}

static ssize_t wcn_sysfs_store_armlog_status(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	unsigned long res;
	int ret;
	char a[64];

	ret = kstrtoul(buf, 10, &res);
	if (ret < 0) {
		WCN_ERR("incorrect value written to armlog\n");
		return -EINVAL;
	}

	WCN_INFO("%s:set armlog = %ld\n", __func__, res);
	sysfs_info.armlog_status = res;

	if (!marlin_get_module_status())
		return count;

	if (res == 1) {
		/* open CP2 armlog */
		scnprintf(a, (size_t)sizeof(a), "%s", "at+armlog=1\r\n");
		wcn_send_atcmd(a, strlen(a), NULL, NULL);
	} else if (res == 0) {
		/* close CP2 armlog */
		scnprintf(a, (size_t)sizeof(a), "%s", "at+armlog=0\r\n");
		wcn_send_atcmd(a, strlen(a), NULL, NULL);
	} else {
		WCN_ERR("incorrect value(%ld)written to armlog\n", res);
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR(armlog_status, 0644,
		   wcn_sysfs_show_armlog_status,
		   wcn_sysfs_store_armlog_status);
/* wcn_sysfs_show_loglevel:len=128, buf=+LOGLEVEL: 0 */
static ssize_t wcn_sysfs_show_loglevel(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	size_t len = 0;
	char a[] = "at+loglevel?\r\n";

	WCN_INFO("%s:buf=%s\n", __func__, buf);
	if (!marlin_get_module_status()) {
		if (sysfs_info.loglevel >= 6)
			sysfs_info.loglevel = 0;
		scnprintf(buf, PAGE_SIZE, "%s: %d", "+LOGLEVEL",
			  sysfs_info.loglevel);
		len = strlen(buf);

		return len;
	}

	wcn_send_atcmd(a, strlen(a), buf, &len);

	WCN_INFO("%s:len=%zd, buf=%s\n", __func__, len, buf);
	len = strlen(buf);
	WCN_INFO("show:len=%zd\n", len);

	return len;
}

static ssize_t wcn_sysfs_store_loglevel(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long res;
	int ret;
	char a[64];

	WCN_INFO("%s:buf=%s\n", __func__, buf);
	ret = kstrtoul(buf, 10, &res);
	if (ret < 0) {
		WCN_ERR("incorrect value written to loglevel\n");
		return -EINVAL;
	}

	if (res >= 6) {
		WCN_ERR("incorrect value(%ld)written to loglevel\n", res);
		return -EINVAL;
	}

	sysfs_info.loglevel = res;

	if (marlin_get_module_status()) {
		scnprintf(a, (size_t)sizeof(a), "%s%ld%s", "at+loglevel=",
			  res, "\r\n");
		WCN_INFO("%s:buf=%s\n", __func__, a);
		wcn_send_atcmd(a, strlen(a), NULL, NULL);
		atomic_and((unsigned int)(~(WCN_SYSFS_LOGLEVEL_SET_BIT)),
			   &sysfs_info.set_mask);
	} else {
		atomic_or(WCN_SYSFS_LOGLEVEL_SET_BIT, &sysfs_info.set_mask);
	}
	WCN_INFO("%s:set_mask:%d\n", __func__,
		 atomic_read(&sysfs_info.set_mask));

	return count;
}

static DEVICE_ATTR(loglevel, 0644,
		   wcn_sysfs_show_loglevel,
		   wcn_sysfs_store_loglevel);

int wcn_sysfs_get_n79_prop(void)
{
	return atomic_read(&sysfs_info.is_n79_mode);
}

static ssize_t n79_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t len = 0;
	int n79_prop = wcn_sysfs_get_n79_prop();

	scnprintf(buf, PAGE_SIZE, "%d\n", n79_prop);
	len = strlen(buf);
	return len;
}

static ssize_t n79_mode_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long res;
	int ret;
	struct wcn_match_data *g_match_config = get_wcn_match_config();

	WCN_INFO("%s: rf_reg n79_mode buf=%s\n", __func__, buf);
	if ((g_match_config && (!g_match_config->unisoc_wcn_m3lite))
	|| (g_match_config && (!marlin_dev->n79_mode_support))) {
		WCN_INFO("g_match_config = %d, do not support n79\n",
				g_match_config->unisoc_wcn_m3lite);
		return -EINVAL;
	}
	ret = kstrtoul(buf, 10, &res);
	if (ret < 0) {
		WCN_ERR("incorrect value written to n79_mode\n");
		return -EINVAL;
	}

	if (res) {
		if (wcn_sysfs_get_n79_prop() == 0) {
			atomic_set(&sysfs_info.is_n79_mode, 1);
			start_marlin(WCN_AUTO);
		} else
			WCN_INFO("rf_reg has been config in n79_mode\n");
	} else {
		if (wcn_sysfs_get_n79_prop() == 1) {
			WCN_INFO("rf_reg do not exit n79_mode\n");
			atomic_set(&sysfs_info.is_n79_mode, 0);
			stop_marlin(WCN_AUTO);
		} else
			WCN_INFO("rf_reg has not been config, modem is not in n79\n");
	}
	return count;
}

static DEVICE_ATTR_RW(n79_mode);

static ssize_t rf_reg_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t len = 0;
	unsigned int i, temp_val = 0;
	char temp_buf[128];
	struct wifi_rf_reg *temp_lna_ldo_enable = get_wifi_rf_reg(&len);
	struct wcn_match_data *g_match_config = get_wcn_match_config();

	if ((g_match_config && (!g_match_config->unisoc_wcn_m3lite))
	|| (g_match_config && (!marlin_dev->n79_mode_support))) {
		WCN_INFO("g_match_config = %d, do not support n79\n",
				g_match_config->unisoc_wcn_m3lite);
		return 0;
	}
	if (marlin_get_power() == 0) {
		WCN_INFO("wcn has not power on, can not read rf_reg\n");
		return 0;
	}
	for (i = 0; i < len / 2; i++) {
		wifi_read_rf_reg(temp_lna_ldo_enable[i].reg_addr, &temp_val);

		scnprintf(temp_buf, 128, "%s%x%s%x\n",
				"addr: 0x", temp_lna_ldo_enable[i].reg_addr,
				", reg_val: 0x", temp_val);
		strncat(buf, temp_buf, 128);
	}
	len = strlen(buf);
	return len;
}

static ssize_t rf_reg_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	WCN_INFO("%s: buf=%s, not support to write\n", __func__, buf);
	return count;
}

static DEVICE_ATTR_RW(rf_reg);

static ssize_t wcn_sysfs_show_reset_dump(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	ssize_t len = PAGE_SIZE;
	int reset_prop = atomic_read(&sysfs_info.is_reset);

	if (reset_prop == WCN_ASSERT_ONLY_DUMP)
		len = snprintf(buf, len, "dump\n");
	else if (reset_prop == WCN_ASSERT_ONLY_RESET)
		len = snprintf(buf, len, "reset\n");
	else if (reset_prop == WCN_ASSERT_BOTH_RESET_DUMP)
		len = snprintf(buf, len, "reset_dump\n");
	else
		return -EINVAL;

	return len;
}

static ssize_t wcn_sysfs_store_reset_dump(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	WCN_INFO("%s: buf=%s\n", __func__, buf);

	if (strncmp(buf, "dump", 4) == 0) {
		atomic_set(&sysfs_info.is_reset, WCN_ASSERT_ONLY_DUMP);
	} else if (strncmp(buf, "reset_dump", 10) == 0) {
		atomic_set(&sysfs_info.is_reset, WCN_ASSERT_BOTH_RESET_DUMP);
	} else if (strncmp(buf, "reset", 5) == 0) {
		atomic_set(&sysfs_info.is_reset, WCN_ASSERT_ONLY_RESET);
	} else if (strncmp(buf, "manual_dump", 11) == 0) {
		wcn_assert_interface(WCN_SOURCE_BTWF, "dumpmem");
	} else
		return -EINVAL;

	return count;
}

bool shoudl_do_sdio_block_workround(void)
{
	struct sdiohal_data_t *p_data = sdiohal_get_data();
	long long now = ktime_get_boot_fast_ns();
	long long max_blocked_time = (3LL * 1000000000LL);
	long long within_blocked_time = (3LL * 1000000000LL);

	if (p_data == NULL)
		return false;

	if ((now - p_data->last_sdio_blocked_time) < within_blocked_time) {
		sdiohal_disable_rx_irq(p_data->irq_num);
		WCN_INFO("force WCN reset\n");
		return true;
	}

	if (p_data->op_enter_ns > p_data->op_leave_ns &&
		((now - p_data->op_enter_ns) > max_blocked_time)) {
		WCN_INFO("holds xmit_lock!!! time=%llu.%llu\n",
			p_data->op_enter_ns, p_data->op_leave_ns);
		return true;
	}
	return false;
}

int wcn_sysfs_get_reset_prop(void)
{
	return atomic_read(&sysfs_info.is_reset);
}

static DEVICE_ATTR(reset_dump, 0644,
		   wcn_sysfs_show_reset_dump,
		   wcn_sysfs_store_reset_dump);

static ssize_t wcn_sysfs_show_atcmd(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", "not support to read");
}

#define N79_ATCMD "at+spn79="
#define ASSERT_ATCMD "at+spatassert=1"
extern u32 g_assert_flag;
static ssize_t wcn_sysfs_store_atcmd(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int ret;
	char *parse_cmd;
	struct wcn_match_data *g_match_config = get_wcn_match_config();

	WCN_INFO("%s: buf=%s\n", __func__, buf);

	parse_cmd = strstr(buf, N79_ATCMD);
	if (parse_cmd) {
		if ((g_match_config && (!g_match_config->unisoc_wcn_m3lite))
		|| (g_match_config && (!marlin_dev->n79_mode_support))) {
			WCN_INFO("g_match_config = %d, do not support n79\n",
					g_match_config->unisoc_wcn_m3lite);
			return count;
		}
		if (!strncmp(parse_cmd + strlen(N79_ATCMD), "1", 1)) {
			if (wcn_sysfs_get_n79_prop() == 0) {
				atomic_set(&sysfs_info.is_n79_mode, 1);
				start_marlin(WCN_AUTO);
			} else
				WCN_INFO("rf_reg has been config in n79_mode\n");
		}
		return count;
	}
	parse_cmd = strstr(buf, ASSERT_ATCMD);
	if (parse_cmd) {
		if ((wcn_platform_chip_type() == WCN_PLATFORM_TYPE_SHARKL3) &&
			(wcn_sysfs_get_reset_prop() == WCN_ASSERT_ONLY_RESET)) {
			WCN_INFO("For SharkL3 special assert process!");
			g_assert_flag = 1;
			wcn_assert_interface(WCN_SOURCE_BTWF,
				"WCN Assert in pseudo_atc_cfg_pike2.c line 428, Manual Assert");
			g_assert_flag = 0;
			return count;
		}
	}
	ret = wcn_send_atcmd((void *)buf, count, NULL, NULL);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(atcmd, 0644,
		   wcn_sysfs_show_atcmd,
		   wcn_sysfs_store_atcmd);

static ssize_t wcn_sysfs_show_shutting_down(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	ssize_t len = PAGE_SIZE;

	len = snprintf(buf, len, "%d\n", is_wcn_shutdown);

	return len;
}

static ssize_t wcn_sysfs_store_shutting_down(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	WCN_INFO("%s: buf=%s, count=%lu\n", __func__, buf, count);

	if (strncmp(buf, "shutting", strlen("shutting")) == 0) {
		WCN_INFO("%s:Ready to shutdown\n", __func__);
		is_wcn_shutdown = 1;
	} else {
		WCN_INFO("%s:Clear WCN shutdown flag\n", __func__);
		is_wcn_shutdown = 0;
	}

	return count;
}
static DEVICE_ATTR(shutting_down, 0644,
		   wcn_sysfs_show_shutting_down,
		   wcn_sysfs_store_shutting_down);

static ssize_t debugbus_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	static int num;
	ssize_t max_ret = PAGE_SIZE - 4;

	if (IS_ERR_OR_NULL(s_wcn_device.btwf_device)) {
		WCN_ERR("debugbus is not ready!\n");
		return 0;
	}
	len = s_wcn_device.btwf_device->dbus.curr_size;
	if (len == 0 || (from_ddr && s_wcn_device.btwf_device->db_to_ddr_disable)) {
		WCN_INFO("%s debugbus data not imported\n", __func__);
		return 0;
	}

	WCN_INFO("%s from_ddr=%d, len=%lu, num=%d, max_ret=%lu\n", __func__, from_ddr,
			len, num, max_ret);
	if (len < (num + 1) * max_ret) {
		/*
		 * Because the maximum number of 'show' returns is PAGE_SIZE, so we use the method
		 * of segmented transmission. If we execute here, we think that the last packet of
		 * has been transmitted, and all the data should be spliced at the user layer.
		 */
		WCN_INFO("%s last debugbus info, length=%ld\n", __func__, len % max_ret);
		if (!from_ddr) {
			memcpy(buf, &(s_wcn_device.btwf_device->dbus.dbus_data_pool[max_ret * num]),
					len % max_ret);
		} else {
			if (wcn_read_data_from_phy_addr(
				s_wcn_device.btwf_device->dbus.base_addr + (max_ret * num),
				buf, len % max_ret)) {
				WCN_ERR("%s Fail to read(0x%llx,0x%lx)", __func__,
					s_wcn_device.btwf_device->dbus.base_addr, len % max_ret);
			}
		}
		num = 0;
		return len % max_ret;
	}

	WCN_INFO("%s copy %lu(%d)\n", __func__, max_ret, num);
	if (!from_ddr)
		memcpy(buf, &s_wcn_device.btwf_device->dbus.dbus_data_pool[max_ret * num], max_ret);
	else {
		if (wcn_read_data_from_phy_addr(
			s_wcn_device.btwf_device->dbus.base_addr + (max_ret * num), buf, max_ret)) {
			WCN_ERR("%s Fail to read(0x%llx,0x%lx)", __func__,
					s_wcn_device.btwf_device->dbus.base_addr, max_ret);
		}
	}
	num++;

	return max_ret;
}

static ssize_t debugbus_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	WCN_INFO("%s: buf=%s, count=%lu\n", __func__, buf, count);

	if (strncmp(buf, "ddr", strlen("ddr")) == 0) {
		from_ddr = true;
		WCN_INFO("%s:Read debugbus data from DDR\n", __func__);
	} else if (strncmp(buf, "temp", strlen("temp")) == 0) {
		WCN_INFO("%s:Read debugbus data from temporary array\n", __func__);
		from_ddr = false;
	} else
		WCN_ERR("Invalid, valid strings:'ddr' and 'temp'!\n");

	return count;
}
/* aiaiai: wcn_sys_show_debugbus to debugbus_show, wcn_sys_store_debugbus to debugbus_store */
static DEVICE_ATTR_RW(debugbus);


static ssize_t debugbus_show_trigger_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	WCN_INFO("%s %s enter\n", buf, __func__);

	debug_bus_show("debugbus_show_trigger_store");
	return count;
}
static DEVICE_ATTR_WO(debugbus_show_trigger);

static ssize_t pm_qos_enable_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	WCN_INFO("%s %s enter\n", __func__, buf);

	if (!strncmp(buf, "enable", strlen("enable")))
		wcn_pm_qos_enable();
	else if (!strncmp(buf, "disable", strlen("disable")))
		wcn_pm_qos_disable();
	else
		WCN_INFO("Invalid, valid strings:'enable' or 'disable'\n");

	return count;
}
static DEVICE_ATTR_WO(pm_qos_enable);

/**
 * pm_policy_store - Configure PCIe ASPM from user space.
 *
 *
 * Write 1(BUS_PM_DISABLE),2(BUS_PM_L0s_L1_ENABLE) and 3(BUS_PM_L0s_L1_ENABLE)
 * to pm_policy node.
 */
static ssize_t pm_policy_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret = 0;
	unsigned long res = 0;

	ret = kstrtoul(buf, 10, &res);
	if (ret < 0 || (res < BUS_PM_DISABLE || res > BUS_PM_ALL_ENABLE)) {
		WCN_ERR("incorrect value written to pm_policy\n");
		return -EINVAL;
	}

	sprdwcn_bus_set_pm_policy(AUTO, res);
	return count;

}
static DEVICE_ATTR_WO(pm_policy);

static ssize_t slp_info_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	unsigned long res;
	int ret;

	ret = kstrtoul(buf, 10, &res);
	if (ret < 0 || res > WCN_SOURCE_GNSS) {
		WCN_ERR("incorrect value written to slp_info(0-1)\n");
		return -EINVAL;
	}

	sysfs_info.slpinfo_sys = res;

	return count;
}

static ssize_t slp_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = PAGE_SIZE;
	char info[1024] = {0};

	if (wcn_slpinfo_get(sysfs_info.slpinfo_sys, &info))
		len = snprintf(buf, len, "Device busy\n");
	else
		len = snprintf(buf, len, "OK\n");

	return len;
}
static DEVICE_ATTR_RW(slp_info);

static ssize_t timer_check_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	WCN_INFO("%s: buf=%s, not support to write\n", __func__, buf);
	return count;
}

static ssize_t timer_check_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char at_cmd_timercheck[] = "at+timercheck\r", at_cmd_timercheck_response[128], *ret_t;
	size_t timer_check_len = WCN_AT_RSP_RAW_FLAG - 1;
	int ret = 0;

	memset(&at_cmd_timercheck_response, 0, sizeof(at_cmd_timercheck_response));
	ret = wcn_send_atcmd(at_cmd_timercheck, strlen(at_cmd_timercheck),
			&at_cmd_timercheck_response, &timer_check_len);
	if (ret) {
		WCN_ERR("%s: BTFW is closed or CMD no support!%d\n", __func__, ret);
		return ret;
	}
	ret_t = strstr(at_cmd_timercheck_response, "CHECKERROR");
	if (ret_t)
		wcn_assert_interface(WCN_SOURCE_BTWF, "TIMER CHECK ERROR!");
	return ret;
}
static DEVICE_ATTR_RW(timer_check);

static long wcn_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static int wcn_open(struct inode *inode, struct file *filep)
{
	return 0;
}

static ssize_t wcn_read_data(struct file *filp, char __user *buf,
			     size_t count, loff_t *pos)
{
	return 0;
}

static int wcn_release(struct inode *inode, struct file *filep)
{
	return 0;
}

#ifdef CONFIG_COMPAT
static long wcn_compat_ioctl(struct file *file, unsigned int cmd,
			     unsigned long data)
{
	cmd = cmd & 0xFFF0FFFF;
	cmd = cmd | 0x00080000;
	return wcn_ioctl(file, cmd, data);
}
#endif

static const struct file_operations wcn_misc_fops = {
	.owner = THIS_MODULE,
	.open = wcn_open,
	.read = wcn_read_data,
	.unlocked_ioctl = wcn_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = wcn_compat_ioctl,
#endif
	.release = wcn_release,
};

static struct attribute *wcn_attrs[] = {
	&dev_attr_sleep_state.attr,
	&dev_attr_sw_ver.attr,
	&dev_attr_hw_ver.attr,
	&dev_attr_watchdog_state.attr,
	&dev_attr_armlog_status.attr,
	&dev_attr_loglevel.attr,
	&dev_attr_reset_dump.attr,
	&dev_attr_atcmd.attr,
	&dev_attr_shutting_down.attr,
	&dev_attr_debugbus.attr,
	&dev_attr_debugbus_show_trigger.attr,
	&dev_attr_pm_qos_enable.attr,
	&dev_attr_pm_policy.attr,
	&dev_attr_slp_info.attr,
	&dev_attr_n79_mode.attr,
	&dev_attr_rf_reg.attr,
	&dev_attr_timer_check.attr,
	NULL,
};

static struct attribute_group wcn_attribute_group = {
	.name = "devices",
	.attrs = wcn_attrs,
};

struct miscdevice wcn_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "wcn",
	.fops = &wcn_misc_fops,
};

void wcn_notify_fw_error(enum wcn_source_type type, char *buf)
{
	int len;
	char *pbuf;
	char *envp[4];

	if (shoudl_do_sdio_block_workround())
		return;
	WCN_ERR("Notify firmware error:%s\n", buf);
	len = strlen(buf) + strlen(WCN_UEVENT_REASON) + 1;
	if (len > 256)
		len = 256;
	pbuf = kmalloc(len, GFP_KERNEL);
	if (!pbuf) {
		WCN_ERR("no mem for notify fw error\n");
		pbuf = kmalloc(64, GFP_KERNEL);
		if (!pbuf) {
			WCN_ERR("notify fw error faile\n");
			return;
		}
	}

	snprintf(pbuf, len, "%s%s", WCN_UEVENT_REASON, buf);

	if (type == WCN_SOURCE_BTWF)
		envp[0] = "SOURCE=WCN-CP2-EXCEPTION";
	else if (type == WCN_SOURCE_GNSS)
		envp[0] = "SOURCE=WCN-GE2-EXCEPTION";
	else if (type == WCN_SOURCE_CP2_ALIVE)
		envp[0] = "SOURCE=WCN-CP2-ALIVE";
	else
		envp[0] = "SOURCE=WCN";
	envp[1] = WCN_UEVENT_FW_ERRO;
	envp[2] = pbuf;
	envp[3] = NULL;

	kobject_uevent_env(&wcn_misc_device.this_device->kobj,
			   KOBJ_CHANGE, envp);

	kfree(pbuf);
	pbuf = NULL;
}

int init_wcn_sysfs(void)
{
	int ret;

	WCN_INFO("%s\n", __func__);
	ret = misc_register(&wcn_misc_device);
	if (ret)
		WCN_ERR("wcn misc dev register fail\n");

	ret = sysfs_create_group(&wcn_misc_device.this_device->kobj,
				 &wcn_attribute_group);
	if (ret) {
		WCN_ERR("%s failed to create device group.\n",
			__func__);
		return -1;
	}

	ret = sysfs_create_bin_file(&wcn_misc_device.this_device->kobj,
				    &fwlog_attr);
	if (ret) {
		WCN_ERR("%s failed to create bin file.\n",
			__func__);
		return -1;
	}

	init_completion(&sysfs_info.cmd_completion);
	mutex_init(&sysfs_info.mutex);
	atomic_set(&sysfs_info.set_mask, 0x0);

#ifdef FLAG_WCN_USER
		atomic_set(&sysfs_info.is_reset, 0x1);
		sysfs_info.armlog_status = 0;
#else
		atomic_set(&sysfs_info.is_reset, 0x0);
		sysfs_info.armlog_status = 1;
#endif
	atomic_set(&sysfs_info.is_n79_mode, 0);

	return 0;
}

void exit_wcn_sysfs(void)
{
	misc_deregister(&wcn_misc_device);
}
