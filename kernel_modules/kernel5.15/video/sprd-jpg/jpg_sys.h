/*
*SPDX-FileCopyrightText: 2019 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier: GPL-2.0-only
*/

#define CAM_JPG_SOC_QOS_BASE 0x30000000
#define JPG_SOC_QOS_BASE 0x30070000
#define SYS_MTX_CFG_EN 0x0

struct jpg_qos_reg {
	unsigned int	offset;
	unsigned int	mask;
	unsigned int	value;
};

struct jpg_qos_reg jpg_mtx_qos_qogirn6lite[] = {
	{ 0x0000, 0x00000001, 0x00000001},
	{ 0x0004, 0xffffffff, 0x08080402},
	{ 0x0008, 0x3f3f3f3f, 0x02030101},
	{ 0x000C, 0x3f3fffff, 0x04020808},
	{ 0x0060, 0x80000003, 0x00000003},
	{ 0x0064, 0x3fff3fff, 0x07770888},
	{ 0x0068, 0x00000701, 0x00000001},
};

struct jpg_qos_reg  cam_jpg_mtx_qos[] = {
	{ 0x003C, 0x00010000, 0x00000000},
	{ 0x003C, 0x00010000, 0x00010000},
	{ 0x0044, 0x00010000, 0x00000000},
	{ 0x0044, 0x00010000, 0x00010000},
	{ 0x0048, 0x00010000, 0x00000000},
	{ 0x0048, 0x00010000, 0x00010000},
};

