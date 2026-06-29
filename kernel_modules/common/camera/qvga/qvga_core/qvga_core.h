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

#ifndef _QVGA_DRV_H_
#define _QVGA_DRV_H_

#define SPRD_QVGA_ON           1
#define SPRD_QVGA_OFF          0

#define QVGA_IOC_MAGIC	'Q'
#define QVGA_IO_OPEN \
	 _IOW(QVGA_IOC_MAGIC,  0,  uint8_t)
#define QVGA_IO_CLOSE \
	 _IOW(QVGA_IOC_MAGIC,  1,  uint8_t)
#define QVGA_IO_GETBV \
	 _IOW(QVGA_IOC_MAGIC,  2,  uint8_t)
#define QVGA_IO_SET_PRIVATE_KEY \
	 _IOW(QVGA_IOC_MAGIC,  3,  uint8_t)

#define SPRD_QVGA_PRIVATE_KEY  0xA1B2


struct sprd_qvga_driver_ops {
	int (*open)(void *drvd);
	int (*close)(void *drvd);
	int (*identify)(void *drvd);
	int (*getbv)(void *drvd, void *arg);
};




#endif
