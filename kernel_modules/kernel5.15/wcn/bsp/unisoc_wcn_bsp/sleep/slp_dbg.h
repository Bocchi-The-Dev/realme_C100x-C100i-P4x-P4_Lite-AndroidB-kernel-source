 /*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __SLP_DBG_H__
#define __SLP_DBG_H__

#include "../include/wcn_dbg.h"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "WCN SLP_MGR" fmt

#endif
