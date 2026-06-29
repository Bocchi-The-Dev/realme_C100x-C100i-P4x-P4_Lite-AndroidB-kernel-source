// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */

#include <linux/of.h>
#include <linux/module.h>
#include "oplus_region_check.h"

static int dbg_nvid = 0x00;
module_param(dbg_nvid, int, 0644);
MODULE_PARM_DESC(dbg_nvid, "debug nvid for autotest");

#define PPS_REGION_COUNT_MAX 16
#define PROP_REGION "ro.boot.regionmark="
#define PROP_PRJ "ro.vendor.oplus.regionmark="
#define PROPERTY_VALUE_MAX 92

struct region_list {
	int elem_count;
	u32 pps_region_list[PPS_REGION_COUNT_MAX];
};

enum region_list_index {
	PPS_SUPPORT_REGION,
	PPS_PRIORITY_REGION,
	SVOOC_LIMIT_CURRENT_REGION,
	PPS_COMM_CHG_SUPPORT_REGION,
	ECO_DESIGN_SUPPORT_REGION,
	REGION_INDEX_MAX,
};

static u8 region_id = 0xFF;

static const char *const dts_region_id_list[REGION_INDEX_MAX] = {
	/* support third pps, EU/JP... */
	[PPS_SUPPORT_REGION] = "oplus,pps_region_list",
	/* pd_svooc adapter works preferentially in pps mode, EU */
	[PPS_PRIORITY_REGION] = "oplus,pps_priority_list",
	[SVOOC_LIMIT_CURRENT_REGION] = "oplus,limit_svooc_current_area_list",
	[PPS_COMM_CHG_SUPPORT_REGION] = "oplus,pps_common_chg_region_list",
	[ECO_DESIGN_SUPPORT_REGION] = "oplus,eco_common_support_region_list",
};

static struct region_list region_list_arrry[REGION_INDEX_MAX] =
{
	{0, {0x00}},
	{0, {0x00}},
	{0, {0x00}},
};

int oplus_get_nv_id_from_cmdline(void)
{
	struct device_node *cmdline_node = NULL;
	const char *cmdline;
	char *match, *match_end;
	char *str = "nvcode.id=";
	char result[92] = {};
	int len, match_str_len, ret;
	unsigned long binary_value = 0;
	char *ptr = result;

	memset(result, '\0',sizeof(result));
	match_str_len = strlen(str);

	cmdline_node = of_find_node_by_path("/chosen");
	if (!cmdline_node) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	ret = of_property_read_string(cmdline_node, "bootargs", &cmdline);
	if (ret) {
		pr_err("%s failed to read bootargs\n", __func__);
		return -EINVAL;
	}

	match = strstr(cmdline, str);
	if (!match) {
		pr_err("Mmatch: %s fail in cmdline\n", str);
		return -EINVAL;
	}

	match_end = strstr((match + match_str_len), " ");
	if (!match_end) {
		pr_err("Match end of : %s fail in cmdline\n", str);
		return -EINVAL;
	}

	len = match_end - (match + match_str_len);
	if (len < 0 || len > sizeof(result)) {
		pr_err("Match cmdline :%s fail, len = %d\n", str, len);
		return -EINVAL;
	}

	memcpy(result, (match + match_str_len), len);
	result[len] = '\0';

	while (*ptr) {
		if (*ptr != '0' && *ptr != '1') {
			pr_err("Invalid binary string: %s\n", result);
			return -EINVAL;
			}
		binary_value = (binary_value << 1) | (*ptr - '0');
		ptr++;
	}

    if (binary_value == 0x44) {
		region_id = 0x44;
        pr_err("result matches 'EUEX'\n");
    } else {
		region_id = 0x00;
        pr_err("result does not match 'EUEX'\n");
    }
	return 1;
}

static void oplus_parse_pps_region_list(struct oplus_chg_chip *chip)
{
	int len = 0;
	int rc = 0;
	int i = 0;
	struct device_node *node;

	node = chip->dev->of_node;
	chg_err("oplus_parse_pps_region_list enter");
	for (i = 0; i < REGION_INDEX_MAX; i++) {
		rc = of_property_count_elems_of_size(
			node, dts_region_id_list[i], sizeof(u32));
		if (rc >= 0) {
			len = rc <= PPS_REGION_COUNT_MAX ? rc : PPS_REGION_COUNT_MAX;
			rc = of_property_read_u32_array(node, dts_region_id_list[i],
							region_list_arrry[i].pps_region_list, len);
			if (rc < 0) {
				len = 0;
				chg_err("parse %s failed, rc=%d", dts_region_id_list[i], rc);
			}
		} else {
			len = 0;
			chg_err("parse %s_length failed, rc=%d", dts_region_id_list[i], rc);
		}
		region_list_arrry[i].elem_count = len;
		for (len = 0; len < region_list_arrry[i].elem_count; len++)
			chg_info("%s[%d]=0x%02x", dts_region_id_list[i], len,
					region_list_arrry[i].pps_region_list[len]);
	}
}

static bool find_id_in_region_list(u8 id, enum region_list_index list_index)
{
	int index = 0;
	u8 region_id_tmp = 0;
	u32 * id_list = NULL;
	int list_length = 0;

	if (list_index >= PPS_SUPPORT_REGION &&
	    list_index < REGION_INDEX_MAX) {
		id_list = region_list_arrry[list_index].pps_region_list;
		list_length = region_list_arrry[list_index].elem_count;
	}
	if (list_length == 0 || id_list == NULL)
		return false;

	while (index < list_length) {
		region_id_tmp = id_list[index] & 0xFF;
		chg_info("pps_list[%d]=0x%x", index, id_list[index]);
		if (id == region_id_tmp) {
			return true;
		}
		index++;
	}

	return false;
}

bool third_pps_supported_comm_chg_nvid(void)
{
	int region_temp = 0;
	if (dbg_nvid != 0)
		region_temp = dbg_nvid;
	else
		region_temp = region_id;

	return find_id_in_region_list(region_temp, PPS_COMM_CHG_SUPPORT_REGION);
}

bool eco_design_supported_comm_chg_nvid(void)
{
	int region_temp = 0;
	if (dbg_nvid != 0)
		region_temp = dbg_nvid;
	else
		region_temp = region_id;
	chg_err("region_id=%x", region_id);
	return find_id_in_region_list(region_temp, ECO_DESIGN_SUPPORT_REGION);
}

bool third_pps_supported_from_nvid(void)
{
	int region_temp = 0;
	if (dbg_nvid != 0)
		region_temp = dbg_nvid;
	else
		region_temp = region_id;

	/* all region support pps if only one 0xFFFF configed in dtsi */
	if (region_list_arrry[PPS_SUPPORT_REGION].pps_region_list[0] == 0xFFFF
		&& region_list_arrry[PPS_SUPPORT_REGION].elem_count == 1)
		return true;

	return find_id_in_region_list(region_temp, PPS_SUPPORT_REGION);
}

bool oplus_limit_svooc_current(void)
{
	int region_temp = 0;
	if (dbg_nvid != 0)
		region_temp = dbg_nvid;
	else
		region_temp = region_id;

	return find_id_in_region_list(region_temp, SVOOC_LIMIT_CURRENT_REGION);
}

bool third_pps_priority_than_svooc(void)
{
	int region_temp = 0;
	if (dbg_nvid != 0)
		region_temp = dbg_nvid;
	else
		region_temp = region_id;

	return find_id_in_region_list(region_temp, PPS_PRIORITY_REGION);
}

void oplus_chg_region_check_init(struct oplus_chg_chip *chip)
{
	if (chip == NULL)
		return;
	chg_err("region_check_init enter");
	if (oplus_get_nv_id_from_cmdline())
		oplus_parse_pps_region_list(chip);

}
