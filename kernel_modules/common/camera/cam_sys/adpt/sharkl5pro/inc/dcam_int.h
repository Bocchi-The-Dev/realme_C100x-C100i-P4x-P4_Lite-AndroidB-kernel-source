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

/* interrupt bits for DCAM0 DCAM1 DCAM2 */
enum {
	DCAM_SENSOR_SOF         = 0,
	DCAM_SENSOR_EOF         = 1,
	DCAM_CAP_SOF            = 2,
	DCAM_CAP_EOF            = 3,

	DCAM_DCAM_OVF           = 4,
	DCAM_CAP_LINE_ERR       = 5,//DCAM_PREVIEW_SOF
	DCAM_CAP_FRM_ERR        = 6,//DCAM_AFL_LAST_SOF
	DCAM_FETCH_SOF_INT      = 7,

	DCAM_ISP_ENABLE_PULSE   = 8,
	DCAM_PREVIEW_SOF        = 9,//DCAM_BPC_MEM_ERR
	DCAM_PDAF_SOF           = 10,
	DCAM_AEM_SOF            = 11,

	DCAM_HIST_SOF           = 12,//DCAM_FULL_PATH_END
	DCAM_AFL_SOF            = 13,//DCAM_BIN_PATH_END
	DCAM_AFM_SOF            = 14,
	DCAM_LSCM_SOF           = 15,

	DCAM_FULL_PATH_TX_DONE  = 16,//DCAM_FULL_PATH_TX_DONE
	DCAM_PREV_PATH_TX_DONE  = 17,//DCAM_PREV_PATH_TX_DONE
	DCAM_PDAF_PATH_TX_DONE  = 18,//DCAM_PDAF_PATH_TX_DONE
	DCAM_VCH2_PATH_TX_DONE  = 19,//DCAM_VCH2_PATH_TX_DONE

	DCAM_VCH3_PATH_TX_DONE  = 20,//DCAM_VCH3_PATH_TX_DONE
	DCAM_AEM_TX_DONE        = 21,//DCAM_AEM_TX_DONE
	DCAM_HIST_TX_DONE       = 22,//DCAM_HIST_TX_DONE
	DCAM_AFL_TX_DONE        = 23,//DCAM_AFL_TX_DONE

	DCAM_BPC_MAP_DONE       = 24,//DCAM_BPC_MAP_DONE
	DCAM_BPC_POS_DONE       = 25,//DCAM_BPC_POS_DONE
	DCAM_AFM_INTREQ0        = 26,//DCAM_AFM_INTREQ0
	DCAM_AFM_INTREQ1        = 27,//DCAM_AFM_INTREQ1

	DCAM_NR3_TX_DONE        = 28,//DCAM_NR3_TX_DONE
	DCAM_LSCM_TX_DONE       = 29,
	DCAM_GTM_TX_DONE        = 30,
	DCAM_MMU_INT            = 31,//DCAM_MMU_INT

	DCAM_IRQ_NUMBER         = 32,//DCAM_IRQ_NUMBER
};

/* only for dcam_test.c */
#define DCAM2_SENSOR_EOF          DCAM_SENSOR_EOF
#define DCAM2_FULL_PATH_TX_DONE   DCAM_FULL_PATH_TX_DONE

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
	DCAM_RECORD_PORT_LSCM,
	DCAM_RECORD_PORT_AEM,
	DCAM_RECORD_PORT_HIST,
	DCAM_RECORD_PORT_HIST_1,
	DCAM_RECORD_PORT_HIST_2,
	DCAM_RECORD_PORT_HIST_3,
	DCAM_RECORD_PORT_PPE,
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
		BIT(DCAM_HIST_TX_DONE) |                  \
		BIT(DCAM_AFL_TX_DONE) |                   \
		BIT(DCAM_BPC_MAP_DONE) |                  \
		BIT(DCAM_BPC_POS_DONE) |                  \
		BIT(DCAM_AFM_INTREQ1) |                   \
		BIT(DCAM_LSCM_TX_DONE) |                  \
		BIT(DCAM_NR3_TX_DONE) |                   \
		BIT(DCAM_GTM_TX_DONE))

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

int dcam_int_irq_desc_get(uint32_t index, void *param);
struct nr3_done dcam_int_nr3_done_rd(void *param, uint32_t idx);
void dcam_int_iommu_regs_dump(void *param);
void dcam_int_irq_init(void *dcam_hw_ctx);

#endif
