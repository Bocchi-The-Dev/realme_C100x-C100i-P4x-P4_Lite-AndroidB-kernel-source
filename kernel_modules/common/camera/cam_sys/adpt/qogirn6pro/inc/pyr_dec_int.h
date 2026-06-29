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

#ifndef _PYR_DEC_INT_H_
#define _PYR_DEC_INT_H_

enum pyr_dec_irq_id {
	PYR_INT_DEC_ALL_DONE,
	PYR_INT_DEC_STORE_DONE,
	PYR_INT_DCT_STORE_DONE,
	PYR_INT_DEC_SHADOW_DONE,

	PYR_INT_DCT_SHADOW_DONE,
	PYR_INT_DEC_DISPATCH_DONE,
	PYR_INT_DEC_FMCU_LOAD_DONE,
	PYR_INT_DEC_FMCU_CONFIG_DONE,

	PYR_INT_DEC_FMCU_SHADOW_DONE,
	PYR_INT_DEC_FMCU_CMD_X,
	PYR_INT_DEC_FMCU_TIMEOUT,
	PYR_INT_DEC_FMCU_CMD_ERR,

	PYR_INT_DEC_FMCU_STOP_DONE,
	PYR_INT_DEC_NULL13,
	PYR_INT_DEC_NULL14,
	PYR_INT_DEC_NULL15,

	PYR_INT_DEC_FBD_HEADER_ERR,
	PYR_INT_DEC_FBD_PAYLOAD_ERR,
	PYR_INT_DEC_NULL18,
	PYR_INT_DEC_NULL19,

	PYR_INT_DEC_NULL20,
	PYR_INT_DEC_NULL21,
	PYR_INT_DEC_NULL22,
	PYR_INT_DEC_NULL23,

	PYR_INT_DEC_NULL24,
	PYR_INT_DEC_NULL25,
	PYR_INT_DEC_NULL26,
	PYR_INT_DEC_NULL27,

	PYR_INT_DEC_NULL28,
	PYR_INT_DEC_NULL29,
	PYR_INT_DEC_NULL30,
	PYR_INT_DEC_NULL31,
};

#define PYR_DEC_INT_LINE_MASK_ERR                \
	((1 << PYR_INT_DEC_FMCU_TIMEOUT) |       \
	(1 << PYR_INT_DEC_FMCU_CMD_ERR) |      \
	(1 << PYR_INT_DEC_FBD_HEADER_ERR) |       \
	(1 << PYR_INT_DEC_FBD_PAYLOAD_ERR))

#define PYR_DEC_INT_LINE_MASK                    \
	((1 << PYR_INT_DEC_FMCU_CONFIG_DONE) | PYR_DEC_INT_LINE_MASK_ERR)

#endif
