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

#ifndef _DCAM_INT_H_
#define _DCAM_INT_H_

#include <linux/bitops.h>
#include <linux/device.h>
#include "cam_types.h"
#include "dcam_int_common.h"

/* interrupt bits for DCAM0 or DCAM1 */
enum {
	DCAM_SENSOR_SOF         = 0,
	DCAM_SENSOR_EOF         = 1,
	DCAM_CAP_SOF            = 2,
	DCAM_CAP_EOF            = 3,

	DCAM_DCAM_OVF           = 4,
	DCAM_PREVIEW_SOF        = 5,
	DCAM_ISP_ENABLE_PULSE   = 6,
	DCAM_FETCH_SOF_INT      = 7,

	DCAM_AFL_LAST_SOF       = 8,
	DCAM_BPC_MEM_ERR        = 9,
	DCAM_CAP_LINE_ERR       = 10,
	DCAM_CAP_FRM_ERR        = 11,

	DCAM_FULL_PATH_END      = 12,
	DCAM_BIN_PATH_END       = 13,
	/* reserved */
	/* reserved */

	/* reserved */
	/* reserved */
	DCAM_FULL_PATH_TX_DONE  = 18,
	DCAM_PREV_PATH_TX_DONE  = 19,

	DCAM_AEM_TX_DONE        = 20,
	DCAM_PDAF_PATH_TX_DONE  = 21,
	DCAM_VCH2_PATH_TX_DONE  = 22,
	DCAM_VCH3_PATH_TX_DONE  = 23,

	DCAM_BPC_MAP_DONE       = 24,
	DCAM_BPC_POS_DONE       = 25,
	DCAM_AFM_INTREQ0        = 26,
	DCAM_AFM_INTREQ1        = 27,

	DCAM_AFL_TX_DONE        = 28,
	DCAM_NR3_TX_DONE        = 29,
	/* reserved */
	DCAM_MMU_INT            = 31,

	DCAM_IRQ_NUMBER         = 32,
};

/* interrupt bits for DCAM2 */
enum {
	DCAM2_SENSOR_SOF        = 0,
	DCAM2_SENSOR_EOF        = 1,
	/* reserved */
	/* reserved */

	DCAM2_DCAM_OVF          = 4,
	/* reserved */
	/* reserved */
	/* reserved */

	/* reserved */
	/* reserved */
	DCAM2_CAP_LINE_ERR      = 10,
	DCAM2_CAP_FRM_ERR       = 11,

	DCAM2_FULL_PATH0_END    = 12,
	DCAM2_FULL_PATH1_END    = 13,
	/* reserved */
	/* reserved */

	/* reserved */
	/* reserved */
	DCAM2_FULL_PATH_TX_DONE = 18,
	/* reserved */

	/* reserved */
	/* reserved */
	/* reserved */
	/* reserved */

	DCAM2_MMU_INT           = 24,
	/* reserved */
	/* reserved */
	/* reserved */

	/* reserved */
	/* reserved */
	/* reserved */
	/* reserved */
};

enum {
	/* record frame count and addr to determine the source port of the exception
	 * and frame fid, when dcam mmu happens. */
	DCAM_RECORD_PORT_FULL,
	DCAM_RECORD_PORT_BIN,
	DCAM_RECORD_PORT_BIN_1,
	DCAM_RECORD_PORT_BIN_2,
	DCAM_RECORD_PORT_BIN_3,
	DCAM_RECORD_PORT_PDAF,
	DCAM_RECORD_PORT_VCH2,
	DCAM_RECORD_PORT_VCH3,
	DCAM_RECORD_PORT_LENS,
	DCAM_RECORD_PORT_AEM,
	DCAM_RECORD_PORT_AFL_GLB,
	DCAM_RECORD_PORT_AFL_REGION,
	DCAM_RECORD_PORT_BPC_MAP,
	DCAM_RECORD_PORT_BPC_OUT,
	DCAM_RECORD_PORT_AFM,
	DCAM_RECORD_PORT_NR3,
	DCAM_RECORD_PORT_IMG_FETCH,
	DCAM_RECORD_FRAME_CUR_COUNT,
	DCAM_RECORD_FRAME_CNT_SUM,
	DCAM_RECORD_PORT_INFO_MAX
};

#define DCAMINT_FATAL_ERROR \
	(BIT(DCAM_DCAM_OVF) | BIT(DCAM_CAP_LINE_ERR) | BIT(DCAM_CAP_FRM_ERR))

#define DCAMINT_ALL_ERROR \
	(DCAMINT_FATAL_ERROR | BIT(DCAM_MMU_INT))

/*
 * TX DONE bits
 * some bits is reserved in DCAM2
 * NOTE: BIT(24) is mmu_int in DCAM2
 */
#define DCAMINT_ALL_TX_DONE                       \
	(BIT(DCAM_FULL_PATH_TX_DONE) |            \
		BIT(DCAM_PREV_PATH_TX_DONE) |             \
		BIT(DCAM_PDAF_PATH_TX_DONE) |             \
		BIT(DCAM_VCH2_PATH_TX_DONE) |             \
		BIT(DCAM_VCH3_PATH_TX_DONE) |             \
		BIT(DCAM_AEM_TX_DONE) |                   \
		BIT(DCAM_AFL_TX_DONE) |                   \
		BIT(DCAM_AFM_INTREQ1) |                   \
		BIT(DCAM_NR3_TX_DONE))

#define DCAMINT_IRQ_LINE_MASK                     \
	(DCAMINT_ALL_ERROR | DCAMINT_ALL_TX_DONE |\
		BIT(DCAM_CAP_SOF) |                       \
		BIT(DCAM_SENSOR_SOF) |                    \
		BIT(DCAM_SENSOR_EOF) |                    \
		BIT(DCAM_PREVIEW_SOF))

#define DCAMINT_IRQ_LINE_EN_NORMAL                \
	(DCAMINT_ALL_ERROR | DCAMINT_ALL_TX_DONE |\
		BIT(DCAM_CAP_SOF) |                       \
		BIT(DCAM_SENSOR_EOF))

#define DCAM2INT_IRQ_LINE_MASK                    \
	(DCAMINT_FATAL_ERROR | BIT(DCAM2_MMU_INT) \
		| BIT(DCAM2_FULL_PATH_TX_DONE) |          \
		BIT(DCAM2_SENSOR_EOF) |                   \
		BIT(DCAM2_SENSOR_SOF))

#define DCAM2INT_IRQ_LINE_EN_NORMAL               \
	(DCAMINT_FATAL_ERROR | BIT(DCAM2_MMU_INT) \
		| BIT(DCAM2_FULL_PATH_TX_DONE) |          \
		BIT(DCAM2_SENSOR_EOF) |                   \
		BIT(DCAM2_SENSOR_SOF))

int dcam_int_irq_desc_get(uint32_t index, void *param);
struct nr3_done dcam_int_nr3_done_rd(void *param, uint32_t idx);
void dcam_int_iommu_regs_dump(void *param);
void dcam_int_irq_init(void *dcam_hw_ctx);
#endif
