/*
 * SPDX-FileCopyrightText: 2015 Spreadtrum Communications (Shanghai) Co., Ltd
 * SPDX-FileCopyrightText: 2016 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef __HEADSET_SPRD_H__
#define __HEADSET_SPRD_H__

#if (defined(CONFIG_SND_SOC_UNISOC_CODEC_SC2730) || defined(CONFIG_SND_SOC_UNISOC_CODEC_SC2730_MODULE))
#include "./sc2730/codec/sprd-headset-2730.h"
#elif (defined(CONFIG_SND_SOC_UNISOC_CODEC_UMP9620) || defined(CONFIG_SND_SOC_UNISOC_CODEC_UMP9620_MODULE))
#include "./ump9620/codec/sprd-headset-ump9620.h"
#elif (defined(CONFIG_SND_SOC_UNISOC_CODEC_SC2721) || defined(CONFIG_SND_SOC_UNISOC_CODEC_SC2721_MODULE))
#include "./sc2721/codec/sprd-headset-2721.h"
#endif

#endif
