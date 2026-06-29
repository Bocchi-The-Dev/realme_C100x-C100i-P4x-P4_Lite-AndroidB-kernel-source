/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef _WCN_GNSS_H
#define _WCN_GNSS_H

struct sprdwcn_gnss_ops {
	int (*file_judge)(char *buff, int type);
	int (*backup_data)(void);
	int (*write_data)(void);
	void (*set_file_path)(char *buf);
	int (*wait_gnss_boot)(void);
};
int wcn_gnss_ops_register(struct sprdwcn_gnss_ops *ops);
void wcn_gnss_ops_unregister(void);

#endif
