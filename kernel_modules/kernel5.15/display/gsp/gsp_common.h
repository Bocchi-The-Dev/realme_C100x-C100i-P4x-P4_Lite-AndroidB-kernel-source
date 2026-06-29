/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (C) 2020 Unisoc Inc.
*/

#ifndef _GSP_COMMON_H
#define _GSP_COMMON_H

#include <uapi/drm/sprd_drm_gsp.h>
#include <uapi/drm/gsp_lite_r2p0_cfg.h>
#include <uapi/drm/gsp_lite_r3p0_cfg.h>
#include <uapi/drm/gsp_lite_r4p0_cfg.h>
#include <uapi/drm/gsp_r6p0_cfg.h>
#include <uapi/drm/gsp_r8p0_cfg.h>
#include <uapi/drm/gsp_r9p0_cfg.h>

#define GSP_LITE_R2P0_NAME "LITE_R2P0"
#define GSP_LITE_R3P0_NAME "LITE_R3P0"
#define GSP_LITE_R4P0_NAME "LITE_R4P0"
#define GSP_R6P0_NAME "R6P0"
#define GSP_R8P0_NAME "R8P0"
#define GSP_R9P0_NAME "R9P0"


#define LITE_R2P0_CFG sizeof(struct gsp_lite_r2p0_cfg_user)
#define LITE_R3P0_CFG sizeof(struct gsp_lite_r3p0_cfg_user)
#define LITE_R4P0_CFG sizeof(struct gsp_lite_r4p0_cfg_user)
#define R6P0_CFG sizeof(struct gsp_r6p0_cfg_user)
#define R8P0_CFG sizeof(struct gsp_r8p0_cfg_user)
#define R9P0_CFG sizeof(struct gsp_r9p0_cfg_user)

#define GSP_CAPA sizeof(struct gsp_capability)
#define LITE_R2P0_CAPA sizeof(struct gsp_lite_r2p0_capability)
#define LITE_R3P0_CAPA sizeof(struct gsp_lite_r3p0_capability)
#define LITE_R4P0_CAPA sizeof(struct gsp_lite_r4p0_capability)
#define R6P0_CAPA sizeof(struct gsp_r6p0_capability)
#define R8P0_CAPA sizeof(struct gsp_r8p0_capability)
#define R9P0_CAPA sizeof(struct gsp_r9p0_capability)

#endif

