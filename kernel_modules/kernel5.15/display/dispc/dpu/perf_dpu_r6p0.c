// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <drm/drm_vblank.h>
#include <linux/apsys_dvfs.h>
#include <linux/backlight.h>
#include <linux/dma-buf.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/trusty/smcall.h>
#include "corner_param.h"
#include "sprd_dpu.h"
#include "dpu_enhance_param.h"
#include "sprd_crtc.h"
#include "sprd_plane.h"
#include <../drivers/trusty/trusty.h>
#include "../sprd_dsc.h"
#include "dpu_r6px_scale_param.h"

#define XFBC8888_HEADER_SIZE(w, h) (ALIGN((ALIGN((w), 16)) * \
				(ALIGN((h), 16)) / 16, 128))
#define XFBC8888_PAYLOAD_SIZE(w, h) (ALIGN((w), 16) * ALIGN((h), 16) * 4)
#define XFBC8888_BUFFER_SIZE(w, h) (XFBC8888_HEADER_SIZE(w, h) \
				+ XFBC8888_PAYLOAD_SIZE(w, h))

#define SLP_BRIGHTNESS_THRESHOLD 0x20

/* DPU registers size, 4 Bytes(32 Bits) */
#define DPU_REG_SIZE					0x04
/* Layer registers offset */
#define DPU_LAY_REG_OFFSET				0x10

#define DPU_MAX_REG_OFFSET				0x19AC

#define DSC_REG_OFFSET					0x1A00
#define DSC1_REG_OFFSET					0x1B00

#define DPU_REG_RD(reg) readl_relaxed(reg)

#define DPU_REG_WR(reg, mask) writel_relaxed(mask, reg)

#define DPU_REG_SET(reg, mask) \
		writel_relaxed(readl_relaxed(reg) | mask, reg)

#define DPU_REG_CLR(reg, mask) \
		writel_relaxed(readl_relaxed(reg) & ~mask, reg)

#define DPU_LAY_REG(reg, index) \
		(reg + index * DPU_LAY_REG_OFFSET * DPU_REG_SIZE)

#define DPU_LAY_PLANE_ADDR(reg, index, plane) \
		(reg + index * DPU_LAY_REG_OFFSET * DPU_REG_SIZE + plane * DPU_REG_SIZE)

#define DSC_REG(reg) (reg + DSC_REG_OFFSET)

#define DSC1_REG(reg) (reg + DSC1_REG_OFFSET)


/*Global control registers */
#define REG_DPU_CTRL					0x08
#define REG_DPU_CFG0					0x0C
#define REG_DPU_CFG1					0x10
#define REG_PANEL_SIZE					0x18
#define REG_BLEND_SIZE					0x1C
#define REG_SCL_EN					0x20
#define REG_BG_COLOR					0x24

/* DPU Secure reg */
#define REG_DPU_SECURE					0x14

/* Layer enable */
#define REG_LAYER_ENABLE				0x2c

/* Layer0 control registers */
#define REG_LAY_BASE_ADDR				0x30
#define REG_LAY_CTRL					0x40
#define REG_LAY_DES_SIZE				0x44
#define REG_LAY_SRC_SIZE				0x48
#define REG_LAY_PITCH					0x4C
#define REG_LAY_POS					0x50
#define REG_LAY_ALPHA					0x54
#define REG_LAY_CK					0x58
#define REG_LAY_PALLETE					0x5C
#define REG_LAY_CROP_START				0x60

/* Write back config registers */
#define REG_WB_BASE_ADDR				0x230
#define REG_WB_CTRL					0x234
#define REG_WB_CFG					0x238
#define REG_WB_PITCH					0x23C

/* Interrupt control registers */
#define REG_DPU_INT_EN					0x250
#define REG_DPU_INT_CLR					0x254
#define REG_DPU_INT_STS					0x258
#define REG_DPU_INT_RAW					0x25C

/* DPI control registers */
#define REG_DPI_CTRL					0x260
#define REG_DPI_H_TIMING				0x264
#define REG_DPI_V_TIMING				0x268

/* SCL coef registers */
#define REG_SCL_COEF_HOR_CFG				0x300
#define REG_SCL_COEF_VER_CFG				0x380

/* DPU STS */
#define REG_DPU_STS_20					0x750
#define REG_DPU_STS_21					0x754
#define REG_DPU_STS_22					0x758

#define REG_DPU_MMU0_UPDATE				0x1808
#define REG_DPU_MODE					0x04

/* DPU SCL */
#define REG_DPU_SCL_EN					0x20

/* DSC REG */
#define REG_DSC_CTRL					0x00
#define REG_DSC_PIC_SIZE				0x04
#define REG_DSC_GRP_SIZE				0x08
#define REG_DSC_SLICE_SIZE				0x0c
#define REG_DSC_H_TIMING				0x10
#define REG_DSC_V_TIMING				0x14
#define REG_DSC_CFG0					0x18
#define REG_DSC_CFG1					0x1c
#define REG_DSC_CFG2					0x20
#define REG_DSC_CFG3					0x24
#define REG_DSC_CFG4					0x28
#define REG_DSC_CFG5					0x2c
#define REG_DSC_CFG6					0x30
#define REG_DSC_CFG7					0x34
#define REG_DSC_CFG8					0x38
#define REG_DSC_CFG9					0x3c
#define REG_DSC_CFG10					0x40
#define REG_DSC_CFG11					0x44
#define REG_DSC_CFG12					0x48
#define REG_DSC_CFG13					0x4c
#define REG_DSC_CFG14					0x50
#define REG_DSC_CFG15					0x54
#define REG_DSC_CFG16					0x58
#define REG_DSC_STS0					0x5c
#define REG_DSC_STS1					0x60
#define REG_DSC_VERSION					0x64

/* PQ Enhance config registers */
#define REG_DPU_ENHANCE_CFG				0x500
#define REG_ENHANCE_UPDATE				0x504
#define REG_SLP_LUT_BASE_ADDR				0x510
#define REG_THREED_LUT_BASE_ADDR			0x514
#define REG_HSV_LUT_BASE_ADDR				0x518
#define REG_GAMMA_LUT_BASE_ADDR				0x51C
#define REG_EPF_EPSILON					0x520
#define REG_EPF_GAIN0_3					0x524
#define REG_EPF_GAIN4_7					0x528
#define REG_EPF_DIFF					0x52C
#define REG_CM_COEF01_00				0x530
#define REG_CM_COEF03_02				0x534
#define REG_CM_COEF11_10				0x538
#define REG_CM_COEF13_12				0x53C
#define REG_CM_COEF21_20				0x540
#define REG_CM_COEF23_22				0x544
#define REG_SLP_CFG0					0x550
#define REG_SLP_CFG1					0x554
#define REG_SLP_CFG2					0x558
#define REG_SLP_CFG3					0x55C
#define REG_SLP_CFG4					0x560
#define REG_SLP_CFG5					0x564
#define REG_SLP_CFG6					0x568
#define REG_SLP_CFG7					0x56C
#define REG_SLP_CFG8					0x570
#define REG_SLP_CFG9					0x574
#define REG_SLP_CFG10					0x578
#define REG_HSV_CFG					0x580
#define REG_CABC_CFG0					0x590
#define REG_CABC_CFG1					0x594
#define REG_CABC_CFG2					0x598
#define REG_CABC_CFG3					0x59C
#define REG_CABC_CFG4					0x5A0
#define REG_CABC_CFG5					0x5A4
#define REG_UD_CFG0					0x5B0
#define REG_UD_CFG1					0x5B4
#define REG_CABC_HIST0					0x600
#define REG_GAMMA_LUT_ADDR				0x780
#define REG_GAMMA_LUT_RDATA				0x784
#define REG_SLP_LUT_ADDR				0x798
#define REG_SLP_LUT_RDATA				0x79C
#define REG_HSV_LUT0_ADDR				0x7A0
#define REG_HSV_LUT0_RDATA				0x7A4
#define REG_THREED_LUT0_ADDR				0x7C0
#define REG_THREED_LUT0_RDATA				0x7C4
#define REG_DPU_MMU_EN					0x1804
#define REG_DPU_MMU_INV_ADDR_RD				0x185C
#define REG_DPU_MMU_INV_ADDR_WR				0x1860
#define REG_DPU_MMU_UNS_ADDR_RD				0x1864
#define REG_DPU_MMU_UNS_ADDR_WR				0x1868
#define REG_DPU_MMU_INT_EN				0x18A0
#define REG_DPU_MMU_INT_CLR				0x18A4
#define REG_DPU_MMU_INT_STS				0x18A8
#define REG_DPU_MMU_INT_RAW				0x18AC

#define REG_DPU_MMU1_EN					0x1904
#define REG_DPU_MMU1_INV_ADDR_RD			0x195C
#define REG_DPU_MMU1_INV_ADDR_WR			0x1960
#define REG_DPU_MMU1_UNS_ADDR_RD			0x1964
#define REG_DPU_MMU1_UNS_ADDR_WR			0x1968
#define REG_DPU_MMU1_INT_EN				0x19A0
#define REG_DPU_MMU1_INT_CLR				0x19A4
#define REG_DPU_MMU1_INT_STS				0x19A8
#define REG_DPU_MMU1_INT_RAW				0x19AC

/* Corner config registers */
#define REG_CORNER_CONFIG			0x4d0
#define REG_TOP_CORNER_LUT_ADDR		0x4d4
#define REG_TOP_CORNER_LUT_WDATA	0x4d8
#define REG_BOT_CORNER_LUT_ADDR		0x4e0
#define REG_BOT_CORNER_LUT_WDATA	0x4e4

/* Global control bits */
#define BIT_DPU_RUN					BIT(0)
#define BIT_DPU_STOP					BIT(1)
#define BIT_DPU_ALL_UPDATE				BIT(2)
#define BIT_DPU_REG_UPDATE				BIT(3)
#define BIT_LAY_REG_UPDATE				BIT(4)
#define BIT_DPU_IF_EDPI					BIT(0)

/* Corner config bits */
#define BIT_TOP_CORNER_EN				BIT(0)
#define BIT_BOT_CORNER_EN				BIT(16)

/* scaling config bits */
#define BIT_DPU_SCALING_EN				BIT(0)

/* Layer control bits */
// #define BIT_DPU_LAY_EN				BIT(0)
#define BIT_DPU_LAY_LAYER_ALPHA				(0x01 << 2)
#define BIT_DPU_LAY_COMBO_ALPHA				(0x01 << 3)
#define BIT_DPU_LAY_PALLETE_EN				(0x01 << 13)
#define BIT_DPU_LAY_MODE_BLEND_NORMAL			(0x01 << 16)
#define BIT_DPU_LAY_MODE_BLEND_PREMULT			(0x01 << 16)

#define FROMAT_YUV422_2P				(0x00 << 4)
#define FROMAT_YUV420_2P				(0x01 << 4)
#define FROMAT_YUV420_3P				(0x02 << 4)
#define FROMAT_ARGB8888					(0x03 << 4)
#define FORMAT_RGB565					(0x04 << 4)
#define FORMAT_XFBC_ARGB8888				(0x08 << 4)
#define FORMAT_XFBC_RGB565				(0x09 << 4)
#define FORMAT_XFBC_YUV420				(0x0A << 4)

#define ENDIAN_B0B1B2B3					(0x00 << 8)
#define ENDIAN_B3B2B1B0					(0x01 << 8)
#define ENDIAN_B2B3B0B1					(0x02 << 8)
#define ENDIAN_B1B0B3B2					(0x03 << 8)

#define SWITCH_565_RGB					(0x00 << 10)
#define SWITCH_565_RBG					(0x01 << 10)
#define SWITCH_565_GRB					(0x02 << 10)
#define SWITCH_565_GBR					(0x03 << 10)
#define SWITCH_565_BGR					(0x04 << 10)
#define SWITCH_565_BRG					(0x05 << 10)
#define SWITCH_OTHER_NO					(0x00 << 10)
#define SWITCH_OTHER_RB					(0x01 << 10)
#define SWITCH_OTHER_UV					(0x01 << 10)
#define SWITCH_OTHER_RG					(0x02 << 10)
#define SWITCH_OTHER_GB					(0x03 << 10)

/*Interrupt control & status bits */
#define BIT_DPU_INT_DONE				BIT(0)
#define BIT_DPU_INT_TE					BIT(1)
#define BIT_DPU_INT_ERR					BIT(2)
#define BIT_DPU_INT_VSYNC				BIT(4)
#define BIT_DPU_INT_WB_DONE_EN				BIT(5)
#define BIT_DPU_INT_WB_ERR_EN				BIT(6)
#define BIT_DPU_INT_FBC_PLD_ERR				BIT(7)
#define BIT_DPU_INT_FBC_HDR_ERR				BIT(8)
#define BIT_DPU_INT_DPU_ALL_UPDATE_DONE			BIT(16)
#define BIT_DPU_INT_DPU_REG_UPDATE_DONE			BIT(17)
#define BIT_DPU_INT_LAY_REG_UPDATE_DONE			BIT(18)
#define BIT_DPU_INT_PQ_REG_UPDATE_DONE			BIT(19)
#define BIT_DPU_INT_PQ_LUT_UPDATE_DONE			BIT(20)

/* DPI control bits */
#define BIT_DPU_EDPI_TE_EN				BIT(8)
#define BIT_DPU_EDPI_FROM_EXTERNAL_PAD			BIT(10)
#define BIT_DPU_DPI_HALT_EN				BIT(16)
#define BIT_DPU_STS_RCH_DPU_BUSY			BIT(15)

/* MMU Interrupt bits */
#define BIT_DPU_INT_MMU_VAOR_RD_MASK			BIT(0)
#define BIT_DPU_INT_MMU_VAOR_WR_MASK			BIT(1)
#define BIT_DPU_INT_MMU_INV_RD_MASK			BIT(2)
#define BIT_DPU_INT_MMU_INV_WR_MASK			BIT(3)
#define BIT_DPU_INT_MMU_UNS_RD_MASK			BIT(4)
#define BIT_DPU_INT_MMU_UNS_WR_MASK			BIT(5)
#define BIT_DPU_INT_MMU_PAOR_RD_MASK			BIT(6)
#define BIT_DPU_INT_MMU_PAOR_WR_MASK			BIT(7)

/* enhance config bits */
#define BIT_DPU_ENHANCE_EN				BIT(0)
#define GAMMA_LUT_MODE					5
#define HSV_LUT_MODE					3
#define LUT3D_MODE					8
#define LUT3D_MAX_INDEX					8
#define LUTS_SIZE_4K					(GAMMA_LUT_MODE + HSV_LUT_MODE + LUT3D_MODE * 6)
#define LUTS_COPY_TIME					(LUTS_SIZE_4K * 2)
#define DPU_LUTS_SIZE					(LUTS_SIZE_4K * 4096)
#define DPU_LUTS_SLP_OFFSET				0
#define DPU_LUTS_GAMMA_OFFSET				4096
#define DPU_LUTS_HSV_OFFSET				(DPU_LUTS_GAMMA_OFFSET + 4096 * (GAMMA_LUT_MODE))
#define DPU_LUTS_LUT3D_OFFSET				(DPU_LUTS_HSV_OFFSET + 4096 * (HSV_LUT_MODE))
#define CABC_BL_COEF					1020

#define REG_DSC_STS1			0x60
#define BIT_DSC_UNDERFLOW_MASK		BIT(31)
#define BIT_DSC_OVERFLOW_MASK		BIT(30)
#define BIT_DSC_EN		BIT(0)

struct layer_info {
	u16 dst_x;
	u16 dst_y;
	u16 dst_w;
	u16 dst_h;
};

enum sprd_fw_attr {
	FW_ATTR_NON_SECURE = 0,
	FW_ATTR_SECURE,
	FW_ATTR_PROTECTED,
};

struct wb_region {
	u32 index;
	u16 pos_x;
	u16 pos_y;
	u16 size_w;
	u16 size_h;
};

struct layer_reg {
	u32 addr[4];
	u32 ctrl;
	u32 dst_size;
	u32 src_size;
	u32 pitch;
	u32 pos;
	u32 alpha;
	u32 ck;
	u32 pallete;
	u32 crop_start;
	u32 reserved[3];
};

struct enhance_module {
	u32 epf_en: 1;
	u32 hsv_en: 1;
	u32 cm_en: 1;
	u32 gamma_en: 1;
	u32 lut3d_en: 1;
	u32 dither_en: 1;
	u32 slp_en: 1;
	u32 ltm_en: 1;
	u32 slp_mask_en: 1;
	u32 cabc_en: 1;
	u32 ud_en: 1;
	u32 ud_local_en: 1;
	u32 ud_mask_en: 1;
	u32 scl_en: 1;
};

struct luts_typeindex {
	u16 type;
	u16 index;
};

struct scale_cfg {
	u32 in_w;
	u32 in_h;
};

struct epf_cfg {
	u16 e0;
	u16 e1;
	u8  e2;
	u8  e3;
	u8  e4;
	u8  e5;
	u8  e6;
	u8  e7;
	u8  e8;
	u8  e9;
	u8  e10;
	u8  e11;
};

struct cm_cfg {
	u16 c00;
	u16 c01;
	u16 c02;
	u16 c03;
	u16 c10;
	u16 c11;
	u16 c12;
	u16 c13;
	u16 c20;
	u16 c21;
	u16 c22;
	u16 c23;
};

struct slp_cfg {
	u16 s0;
	u16 s1;
	u8  s2;
	u8  s3;
	u8  s4;
	u8  s5;
	u8  s6;
	u8  s7;
	u8  s8;
	u8  s9;
	u8  s10;
	u8  s11;
	u16 s12;
	u16 s13;
	u8  s14;
	u8  s15;
	u8  s16;
	u8  s17;
	u8  s18;
	u8  s19;
	u8  s20;
	u8  s21;
	u8  s22;
	u8  s23;
	u16 s24;
	u16 s25;
	u8  s26;
	u8  s27;
	u8  s28;
	u8  s29;
	u8  s30;
	u8  s31;
	u8  s32;
	u8  s33;
	u8  s34;
	u8  s35;
	u8  s36;
	u8  s37;
	u8  s38;
};

struct hsv_params {
	short h0;
	short h1;
	short h2;
};

struct ud_cfg {
	short u0;
	short u1;
	short u2;
	short u3;
	short u4;
	short u5;
};

struct hsv_lut_table {
	u16 s_g[64];
	u16 h_o[64];
};

struct hsv_luts {
	struct hsv_lut_table hsv_lut[4];
};

struct gamma_entry {
	u16 r;
	u16 g;
	u16 b;
};

struct gamma_lut {
	u16 r[256];
	u16 g[256];
	u16 b[256];
};

struct threed_lut {
	uint16_t r[729];
	uint16_t g[729];
	uint16_t b[729];
};

struct rgb_integrate_arr {
	uint32_t rgb_value[729];
};

struct lut_base_addrs {
	u64 lut_gamma_addr;
	u32 lut_hsv_addr;
	u32 lut_lut3d_addr;
};

enum {
	LUTS_GAMMA_TYPE,
	LUTS_HSV_TYPE,
	LUTS_LUT3D_TYPE,
	LUTS_ALL
};

enum {
	CM_CTM,
	CM_PQ,
};

enum {
	CABC_DISABLED,
	CABC_STOPPING,
	CABC_WORKING
};

struct cabc_para {
	u32 cabc_hist[64];
	u32 cfg0;
	u32 cfg1;
	u32 cfg2;
	u32 cfg3;
	u32 cfg4;
	u16 bl_fix;
	u16 cur_bl;
};

static const u32 primary_fmts[] = {
	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBX8888, DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12, DRM_FORMAT_NV21,
	DRM_FORMAT_NV16, DRM_FORMAT_NV61,
	DRM_FORMAT_YUV420, DRM_FORMAT_YVU420,
};

static const u32 format_ctrl[][4] = {
	/* DRM_FORMAT_XRGB8888 */
	{FROMAT_ARGB8888, FORMAT_XFBC_ARGB8888, ENDIAN_B0B1B2B3, SWITCH_OTHER_NO},
	/* DRM_FORMAT_XBGR8888 */
	{FROMAT_ARGB8888, FORMAT_XFBC_ARGB8888, ENDIAN_B0B1B2B3, SWITCH_OTHER_RB},
	/* DRM_FORMAT_ARGB8888 */
	{FROMAT_ARGB8888, FORMAT_XFBC_ARGB8888, ENDIAN_B0B1B2B3, SWITCH_OTHER_NO},
	/* DRM_FORMAT_ABGR8888 */
	{FROMAT_ARGB8888, FORMAT_XFBC_ARGB8888, ENDIAN_B0B1B2B3, SWITCH_OTHER_RB},
	/* DRM_FORMAT_RGBA8888 */
	{FROMAT_ARGB8888, FORMAT_XFBC_ARGB8888, ENDIAN_B3B2B1B0, SWITCH_OTHER_RB},
	/* DRM_FORMAT_BGRA8888 */
	{FROMAT_ARGB8888, FORMAT_XFBC_ARGB8888, ENDIAN_B3B2B1B0, SWITCH_OTHER_NO},
	/* DRM_FORMAT_RGBX8888 */
	{FROMAT_ARGB8888, FORMAT_XFBC_ARGB8888, ENDIAN_B3B2B1B0, SWITCH_OTHER_RB},
	/* DRM_FORMAT_BGRX8888 */
	{FROMAT_ARGB8888, FORMAT_XFBC_ARGB8888, ENDIAN_B3B2B1B0, SWITCH_OTHER_NO},
	/* DRM_FORMAT_RGB565 */
	{FORMAT_RGB565, FORMAT_XFBC_RGB565, ENDIAN_B0B1B2B3, SWITCH_565_RGB},
	/* DRM_FORMAT_BGR565 */
	{FORMAT_RGB565, FORMAT_XFBC_RGB565, ENDIAN_B0B1B2B3, SWITCH_565_BGR},
	/* DRM_FORMAT_NV12 */
	{FROMAT_YUV420_2P, FORMAT_XFBC_YUV420, ENDIAN_B0B1B2B3, SWITCH_OTHER_NO},
	/* DRM_FORMAT_NV21 */
	{FROMAT_YUV420_2P, FORMAT_XFBC_YUV420, ENDIAN_B0B1B2B3, SWITCH_OTHER_UV},
	/* DRM_FORMAT_NV16 */
	{FROMAT_YUV422_2P, FROMAT_YUV422_2P, ENDIAN_B3B2B1B0, SWITCH_OTHER_UV},
	/* DRM_FORMAT_NV61 */
	{FROMAT_YUV422_2P, FROMAT_YUV422_2P, ENDIAN_B0B1B2B3, SWITCH_OTHER_NO},
	/* DRM_FORMAT_YUV420 */
	{FROMAT_YUV420_3P, FROMAT_YUV420_3P, ENDIAN_B0B1B2B3, SWITCH_OTHER_NO},
	/* DRM_FORMAT_YVU420 */
	{FROMAT_YUV420_3P, FROMAT_YUV420_3P, ENDIAN_B0B1B2B3, SWITCH_OTHER_UV},
};

static void dpu_sr_config(struct dpu_context *ctx);
static void dpu_clean_all(struct dpu_context *ctx);
static void dpu_layer(struct dpu_context *ctx,
		    struct sprd_layer_state *hwlayer);

struct dpu_enhance {
	u32 enhance_en;
	u32 hsv_cfg_copy;
	int frame_no;
	bool first_frame;
	bool cabc_bl_set;
	int cabc_bl_set_delay;
	bool mode_changed;
	bool need_scale;
	bool flash_finished;
	u8 skip_layer_index;
	u32 dpu_luts_paddr;
	u8 *dpu_luts_vaddr;
	u32 *lut_slp_vaddr;
	u32 *lut_gamma_vaddr;
	u32 *lut_hsv_vaddr;
	u32 *lut_lut3d_vaddr;
	u8 gamma_lut_index;
	u8 hsv_lut_index;
	u8 lut3d_index;
	u8 video_mode;
	int cabc_state;
	bool ctm_set;
	bool pq_update_by_flip;
	bool pq_lut_update_flag;
	struct cm_cfg cm_copy;
	struct cm_cfg ctm_copy;
	struct slp_cfg slp_copy;
	struct gamma_lut gamma_copy;
	struct epf_cfg epf_copy;
	struct rgb_integrate_arr rgb_arr_copy;
	struct hsv_params hsv_offset_copy;
	struct hsv_luts hsv_lut_copy;
	struct threed_lut lut3d_copy;
	struct ud_cfg ud_copy;
	struct luts_typeindex typeindex_cpy;
	struct lut_base_addrs lut_addrs_cpy;
	struct cabc_para cabc_para;
	struct backlight_device *bl_dev;
	struct device_node *g_np;
};

static void dpu_clean_all(struct dpu_context *ctx);
static void dpu_layer(struct dpu_context *ctx,
		    struct sprd_layer_state *hwlayer);

static void dpu_version(struct dpu_context *ctx)
{
	ctx->version = "dpu-r6p0";
}

static void dpu_dump(struct dpu_context *ctx)
{
	u32 *reg = (u32 *)ctx->base;
	int i;

	pr_info("      0          4          8          C\n");
	for (i = 0; i < 256; i += 4) {
		pr_info("%04x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			i * 4, reg[i], reg[i + 1], reg[i + 2], reg[i + 3]);
	}
}

static u32 check_mmu_isr(struct dpu_context *ctx, u32 reg_val)
{
	u32 mmu_mask = BIT_DPU_INT_MMU_VAOR_RD_MASK |
			BIT_DPU_INT_MMU_VAOR_WR_MASK |
			BIT_DPU_INT_MMU_INV_RD_MASK |
			BIT_DPU_INT_MMU_INV_WR_MASK |
			BIT_DPU_INT_MMU_UNS_RD_MASK |
			BIT_DPU_INT_MMU_UNS_WR_MASK |
			BIT_DPU_INT_MMU_PAOR_RD_MASK |
			BIT_DPU_INT_MMU_PAOR_WR_MASK;
	u32 val = reg_val & mmu_mask;

	if (val) {
		ctx->int_cnt.int_cnt_dpu_int_mmu++;

		pr_err("--- iommu interrupt err: 0x%04x ---\n", val);

		pr_err("iommu0 invalid read error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU_INV_ADDR_RD));
		pr_err("iommu1 invalid read error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU1_INV_ADDR_RD));
		pr_err("iommu0 invalid write error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU_INV_ADDR_WR));
		pr_err("iommu1 invalid write error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU1_INV_ADDR_WR));
		pr_err("iommu0 unsecurity read error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU_UNS_ADDR_RD));
		pr_err("iommu1 unsecurity read error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU1_UNS_ADDR_RD));
		pr_err("iommu0 unsecurity  write error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU_UNS_ADDR_WR));
		pr_err("iommu1 unsecurity  write error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU1_UNS_ADDR_WR));
		pr_err("BUG: iommu failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);

		dpu_dump(ctx);

		/* panic("iommu panic\n"); */
	}

	return val;
}

static u32 dpu_isr(struct dpu_context *ctx)
{
	struct dpu_enhance *enhance = ctx->enhance;
	struct sprd_dpu *dpu =
		(struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);
	u32 reg_val, int_mask = 0;
	u32 mmu_reg_val, mmu_int_mask = 0;
	u32 mmu1_reg_val, mmu1_int_mask = 0;
	ktime_t time;
	ktime_t gap;

	reg_val = DPU_REG_RD(ctx->base + REG_DPU_INT_STS);
	if (reg_val & BIT_DPU_INT_TE) {
		/*
		 * FIXME:
		 * In CMD mode, mipi host send data to panel should sync with TE signal.
		 * Accord to our asic design, panel TE signal notifies DPU by interruption.
		 * So, software should run dpu and send mipi data out once receive TE int.
		 * However, isr handler called by CPU need time cost,
		 * and the magnitude is determined by CPU current performance.
		 * In order to keep display normally, we drop frame when TE occurs too late.
		 */
		if (ctx->cmd_dpi_mode) {
			spin_lock(&ctx->irq_lock);
			time = ktime_get();
			gap = time - ctx->te_int_time;
			ctx->te_int_time = time;

			if (ctx->dpu_run_flag) {
				if (gap < ctx->te_int_min_gap) {
					pr_warn("dpu te int occur inappropriate, skip this frame, gap is :%ld, min gap is %ld\n", gap, ctx->te_int_min_gap);
					ctx->dpu_run_flag = false;
				} else if (gap > ctx->te_int_max_gap) {
					pr_warn("dpu te int occur too late, skip this frame, gap is :%ld, max gap is %ld,\n", gap, ctx->te_int_max_gap);
					ctx->dpu_run_flag = false;
					ctx->evt_te_update = true;
					wake_up_interruptible_all(&ctx->te_update_wq);
				} else {
					if (enhance->pq_lut_update_flag) {
						DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(2) | BIT(0));
						enhance->pq_lut_update_flag = false;
					} else
						DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(3) | BIT(0));

					ctx->stopped = false;
					ctx->dpu_run_flag = false;
					ctx->evt_te_update = true;
					wake_up_interruptible_all(&ctx->te_update_wq);
					ctx->te_int_time = ktime_get();
				}
			}

			ctx->evt_te = true;
			spin_unlock(&ctx->irq_lock);
			wake_up_interruptible_all(&ctx->te_wq);
			drm_crtc_handle_vblank(&dpu->crtc->base);
		} else {
			if (ctx->te_check_en) {
				ctx->evt_te = true;
				wake_up_interruptible_all(&ctx->te_wq);
			}

			if (ctx->if_type == SPRD_DPU_IF_EDPI)
				drm_crtc_handle_vblank(&dpu->crtc->base);
		}
	}

	mmu_reg_val = DPU_REG_RD(ctx->base + REG_DPU_MMU_INT_STS);
	mmu1_reg_val = DPU_REG_RD(ctx->base + REG_DPU_MMU1_INT_STS);

	int_mask = reg_val & (BIT_DPU_INT_FBC_PLD_ERR |
				BIT_DPU_INT_FBC_HDR_ERR | BIT_DPU_INT_ERR);
	DPU_REG_WR(ctx->base + REG_DPU_INT_CLR, reg_val);
	DPU_REG_CLR(ctx->base + REG_DPU_INT_EN, int_mask);

	/* clear & disable mmu0 int */
	mmu_int_mask |= check_mmu_isr(ctx, mmu_reg_val);
	DPU_REG_WR(ctx->base + REG_DPU_MMU_INT_CLR, mmu_reg_val);
	DPU_REG_CLR(ctx->base + REG_DPU_MMU_INT_EN, mmu_int_mask);

	/* clear & disable mmu1 int, mmu1 controlled by mmu0 */
	mmu1_int_mask |= check_mmu_isr(ctx, mmu1_reg_val);
	DPU_REG_WR(ctx->base + REG_DPU_MMU_INT_CLR, mmu1_reg_val);
	DPU_REG_CLR(ctx->base + REG_DPU_MMU_INT_EN, mmu1_int_mask);

	/* dpu vsync isr */
	/* dpu vsync isr */
	if (reg_val & BIT_DPU_INT_VSYNC) {
		if (!ctx->cmd_dpi_mode) {
			ctx->int_cnt.int_cnt_vsync++;
			drm_crtc_handle_vblank(&dpu->crtc->base);

			ctx->vsync_count++;
		}
	}

	/* dpu update done isr */
	if (reg_val & BIT_DPU_INT_LAY_REG_UPDATE_DONE) {
		ctx->int_cnt.int_cnt_lay_reg_update_done++;
		ctx->evt_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	if (reg_val & BIT_DPU_INT_DPU_REG_UPDATE_DONE) {
		ctx->int_cnt.int_cnt_dpu_reg_update_done++;
		ctx->evt_all_regs_update = true;
		ctx->evt_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	if (reg_val & BIT_DPU_INT_DPU_ALL_UPDATE_DONE) {
		ctx->int_cnt.int_cnt_dpu_all_update_done++;
		ctx->evt_update = true;
		ctx->evt_all_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
		/* dpu dvfs feature */
		tasklet_schedule(&ctx->dvfs_task);
	}

	if (reg_val & BIT_DPU_INT_PQ_REG_UPDATE_DONE) {
		ctx->int_cnt.int_cnt_pq_reg_update_done++;
		ctx->evt_pq_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	if (reg_val & BIT_DPU_INT_PQ_LUT_UPDATE_DONE) {
		ctx->int_cnt.int_cnt_pq_lut_update_done++;
		ctx->evt_pq_lut_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	/* dpu stop done isr */
	if (reg_val & BIT_DPU_INT_DONE) {
		ctx->int_cnt.int_cnt_dpu_int_done++;
		ctx->evt_stop = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	/* dpu afbc payload error isr */
	if (reg_val & BIT_DPU_INT_FBC_PLD_ERR) {
		ctx->int_cnt.int_cnt_dpu_int_fbc_pld_err++;
		ctx->err_cnt.dpu_fbc_pld_err++;
		pr_err("dpu afbc payload error\n");
	}

	/* dpu afbc header error isr */
	if (reg_val & BIT_DPU_INT_FBC_HDR_ERR) {
		ctx->int_cnt.int_cnt_dpu_int_fbc_hdr_err++;
		ctx->err_cnt.dpu_fbc_hdr_err++;
		pr_err("dpu afbc header error\n");
	}

	return reg_val;
}

static int dpu_wait_stop_done(struct dpu_context *ctx)
{
	int rc, i;
	u32 dpu_sts_21, dpu_sts_22;
	struct sprd_dpu *dpu =
		(struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);

	if (ctx->stopped)
		return 0;

	/* wait for stop done interrupt */
	rc = wait_event_interruptible_timeout(ctx->wait_queue, ctx->evt_stop,
					       msecs_to_jiffies(500));
	ctx->evt_stop = false;

	ctx->stopped = true;

	if (!rc)
		/* time out */
		pr_err("dpu wait for stop done time out!\n");


	for (i = 1; i <= 3000; i++) {
		dpu_sts_21 = DPU_REG_RD(ctx->base + REG_DPU_STS_21);
		dpu_sts_22 = DPU_REG_RD(ctx->base + REG_DPU_STS_22);
		if ((dpu_sts_21 & BIT(15)) ||
		  (dpu_sts_22 & BIT(15)))
			mdelay(1);
		else {
			pr_info("dpu is idle now\n");
			break;
		}

		if (i == 3000) {
			pr_err("wait for dpu read idle 3s timeout need to reset dpu\n");
			dpu->glb->reset(ctx);
			break;
		}
	}

	return 0;
}

static int dpu_wait_update_done(struct dpu_context *ctx)
{
	int rc;

	/* clear the event flag before wait */
	ctx->evt_update = false;
	if (!ctx->stopped)
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0) | BIT(2));
	else
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0) | BIT(2));

	/* wait for reg update done interrupt */
	rc = wait_event_interruptible_timeout(ctx->wait_queue, ctx->evt_update,
					       msecs_to_jiffies(500));

	if (!rc) {
		/* time out */
		pr_err("dpu wait for reg update done time out!\n");
		return -1;
	}

	return 0;
}

static void dpu_stop(struct dpu_context *ctx)
{
	if (ctx->if_type == SPRD_DPU_IF_DPI)
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT_DPU_STOP);

	dpu_wait_stop_done(ctx);
	ctx->evt_update = false;
	pr_info("dpu stop\n");
}

static void dpu_run(struct dpu_context *ctx)
{
	if (ctx->cmd_dpi_mode){
		dpu_wait_te_flush(ctx);
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(2) | BIT(0));
	} else if (ctx->if_type == SPRD_DPU_IF_DPI) {
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(4) | BIT(0));
	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(2) | BIT(0));
	}
	ctx->stopped = false;

	if ((ctx->if_type == SPRD_DPU_IF_EDPI) || ctx->cmd_dpi_mode)  {
		/*
		 * If the panel read GRAM speed faster than
		 * DSI write GRAM speed, it will display some
		 * mass on screen when backlight on. So wait
		 * a TE period after flush the GRAM.
		 */
		if (!ctx->panel_ready) {
			dpu_wait_stop_done(ctx);
			/* wait for TE again */
			mdelay(20);
			ctx->panel_ready = true;
		}
	}
}

static void dpu_dvfs_task_func(unsigned long data)
{
	struct dpu_context *ctx = (struct dpu_context *)data;
	struct layer_info layer, layers[8];
	int i, j, max_x, max_y, min_x, min_y;
	int layer_en, max, maxs[8], count = 0;
	u32 dvfs_freq, reg_val;

	if (!ctx->enabled) {
		pr_err("dpu is not initialized\n");
		return;
	}

	/*
	 * Count the current total number of active layers
	 * and the corresponding pos_x, pos_y, size_x and size_y.
	 */
	for (i = 0; i < 8; i++) {
		layer_en = DPU_REG_RD(ctx->base + REG_LAYER_ENABLE) & BIT(i);
		if (layer_en) {
			reg_val = DPU_REG_RD(ctx->base + DPU_LAY_REG(REG_LAY_POS, i));
			layers[count].dst_x = reg_val & 0xffff;
			layers[count].dst_y = reg_val >> 16;

			reg_val = DPU_REG_RD(ctx->base + DPU_LAY_REG(REG_LAY_DES_SIZE, i));
			layers[count].dst_w = reg_val & 0xffff;
			layers[count].dst_h = reg_val >> 16;
			count++;
		}
	}

	/*
	 * Calculate the number of overlaps between each
	 * layer with other layers, not include itself.
	 */
	for (i = 0; i < count; i++) {
		layer.dst_x = layers[i].dst_x;
		layer.dst_y = layers[i].dst_y;
		layer.dst_w = layers[i].dst_w;
		layer.dst_h = layers[i].dst_h;
		maxs[i] = 1;

		for (j = 0; j < count; j++) {
			if (layer.dst_x + layer.dst_w > layers[j].dst_x &&
				layers[j].dst_x + layers[j].dst_w > layer.dst_x &&
				layer.dst_y + layer.dst_h > layers[j].dst_y &&
				layers[j].dst_y + layers[j].dst_h > layer.dst_y &&
				i != j) {
				max_x = max(layers[i].dst_x, layers[j].dst_x);
				max_y = max(layers[i].dst_y, layers[j].dst_y);
				min_x = min(layers[i].dst_x + layers[i].dst_w,
					layers[j].dst_x + layers[j].dst_w);
				min_y = min(layers[i].dst_y + layers[i].dst_h,
					layers[j].dst_y + layers[j].dst_h);

				layer.dst_x = max_x;
				layer.dst_y = max_y;
				layer.dst_w = min_x - max_x;
				layer.dst_h = min_y - max_y;

				maxs[i]++;
			}
		}
	}

	/* take the maximum number of overlaps */
	max = maxs[0];
	for (i = 1; i < count; i++) {
		if (maxs[i] > max)
			max = maxs[i];
	}

	/*
	 * Determine which frequency to use based on the
	 * maximum number of overlaps.
	 * Every IP here may be different, so need to modify it
	 * according to the actual dpu core clock.
	 */
	if (max <= 2)
		dvfs_freq = 409600000;
	else if (max == 3)
		dvfs_freq = 512000000;
	else if (max == 4)
		dvfs_freq = 614400000;
	else
		dvfs_freq = 614400000;

	if(ctx->vrr_enabled)
		dvfs_freq = 614400000;

#if IS_ENABLED(CONFIG_DVFS_APSYS_SPRD)
	dpu_dvfs_notifier_call_chain(&dvfs_freq);
#endif
}

static void dpu_dvfs_task_init(struct dpu_context *ctx)
{
	static int need_config = 1;

	if (!need_config)
		return;

	need_config = 0;
	tasklet_init(&ctx->dvfs_task, dpu_dvfs_task_func,
			(unsigned long)ctx);
}

static void dpu_scl_coef_cfg(struct dpu_context *ctx)
{
	int i, j;

	for (i = 0, j = 0; i < 64; i += 2) {
		DPU_REG_WR(ctx->base + REG_SCL_COEF_HOR_CFG + j * 4, r6px_scl_coef[i]);
		DPU_REG_CLR(ctx->base + REG_SCL_COEF_HOR_CFG + j * 4, (0xFFFF << 16));
		DPU_REG_SET(ctx->base + REG_SCL_COEF_HOR_CFG + j * 4, (r6px_scl_coef[i+1] << 16));

		DPU_REG_WR(ctx->base + REG_SCL_COEF_VER_CFG + j * 4, r6px_scl_coef[i]);
		DPU_REG_CLR(ctx->base + REG_SCL_COEF_VER_CFG + j * 4, (0xFFFF << 16));
		DPU_REG_SET(ctx->base + REG_SCL_COEF_VER_CFG + j * 4, (r6px_scl_coef[i+1] << 16));
		j++;
	}
}

static int dpu_init(struct dpu_context *ctx)
{
	u32 reg_val, size;
	u32 dvfs_freq;
	//int ret;
	struct sprd_dpu *dpu = (struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);
	struct sprd_dummy_panel_info *info = &dpu->perf_conn->panel_info;
	struct dpu_enhance *enhance = ctx->enhance;

	if (info->dual_dsi_en)
		DPU_REG_WR(ctx->base + REG_DPU_MODE, BIT(0));
/*
	if (panel->info.dsc_en) {
		calc_dsc_params(&ctx->dsc_init);
		dpu_config_dsc_param(ctx);
	}
*/
	/* set bg color */
	DPU_REG_WR(ctx->base + REG_BG_COLOR, 0x00);

	/* set dpu output size */
	size = (ctx->vm.vactive << 16) | ctx->vm.hactive;
	DPU_REG_WR(ctx->base + REG_PANEL_SIZE, size);
	DPU_REG_WR(ctx->base + REG_BLEND_SIZE, size);

	DPU_REG_WR(ctx->base + REG_DPU_CFG0, 0x00);
	if (ctx->cmd_dpi_mode) {
		DPU_REG_SET(ctx->base + REG_DPU_CFG0, BIT(1));
		ctx->is_single_run = true;
	}

	reg_val = (ctx->qos_cfg.awqos_high << 12) |
		(ctx->qos_cfg.awqos_low << 8) |
		(ctx->qos_cfg.arqos_high << 4) |
		(ctx->qos_cfg.arqos_low) | BIT(18) | BIT(22) | BIT(23);
	DPU_REG_WR(ctx->base + REG_DPU_CFG1, reg_val);;
	if (ctx->stopped)
		dpu_clean_all(ctx);

	dpu_scl_coef_cfg(ctx);

	DPU_REG_WR(ctx->base + REG_DPU_INT_CLR, 0xffff);

	dpu_dvfs_task_init(ctx);

	enhance->frame_no = 0;

	if(ctx->vrr_enabled){
		dvfs_freq = 614400000;
#if IS_ENABLED(CONFIG_DVFS_APSYS_SPRD)
		dpu_dvfs_notifier_call_chain(&dvfs_freq);
#endif
	}

	return 0;
}

static void dpu_fini(struct dpu_context *ctx)
{
	DPU_REG_WR(ctx->base + REG_DPU_INT_EN, 0x00);
	DPU_REG_WR(ctx->base + REG_DPU_INT_CLR, 0xff);

	ctx->panel_ready = false;
}

enum {
	DPU_LAYER_FORMAT_YUV422_2PLANE,
	DPU_LAYER_FORMAT_YUV420_2PLANE,
	DPU_LAYER_FORMAT_YUV420_3PLANE,
	DPU_LAYER_FORMAT_ARGB8888,
	DPU_LAYER_FORMAT_RGB565,
	DPU_LAYER_FORMAT_XFBC_ARGB8888 = 8,
	DPU_LAYER_FORMAT_XFBC_RGB565,
	DPU_LAYER_FORMAT_XFBC_YUV420,
	DPU_LAYER_FORMAT_MAX_TYPES,
};

enum {
	DPU_LAYER_ROTATION_0,
	DPU_LAYER_ROTATION_90,
	DPU_LAYER_ROTATION_180,
	DPU_LAYER_ROTATION_270,
	DPU_LAYER_ROTATION_0_M,
	DPU_LAYER_ROTATION_90_M,
	DPU_LAYER_ROTATION_180_M,
	DPU_LAYER_ROTATION_270_M,
};

static u32 to_dpu_rotation(u32 angle)
{
	u32 rot = DPU_LAYER_ROTATION_0;

	switch (angle) {
	case 0:
	case DRM_MODE_ROTATE_0:
		rot = DPU_LAYER_ROTATION_0;
		break;
	case DRM_MODE_ROTATE_90:
		rot = DPU_LAYER_ROTATION_90;
		break;
	case DRM_MODE_ROTATE_180:
		rot = DPU_LAYER_ROTATION_180;
		break;
	case DRM_MODE_ROTATE_270:
		rot = DPU_LAYER_ROTATION_270;
		break;
	case DRM_MODE_REFLECT_Y:
	case (DRM_MODE_REFLECT_Y | DRM_MODE_ROTATE_0):
		rot = DPU_LAYER_ROTATION_180_M;
		break;
	case (DRM_MODE_REFLECT_Y | DRM_MODE_ROTATE_90):
		rot = DPU_LAYER_ROTATION_90_M;
		break;
	case DRM_MODE_REFLECT_X:
	case (DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_0):
		rot = DPU_LAYER_ROTATION_0_M;
		break;
	case (DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_90):
		rot = DPU_LAYER_ROTATION_270_M;
		break;
	default:
		pr_err("rotation convert unsupport angle (drm)= 0x%x\n", angle);
		break;
	}

	return rot;
}

static u32 dpu_img_ctrl(u32 format, u32 blending, u32 compression, u32 y2r_coef,
		u32 rotation)
{
	int reg_val = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(primary_fmts); i++) {
		if (format == primary_fmts[i])
			break;
	}

	if (i == ARRAY_SIZE(primary_fmts)) {
		pr_err("error: invalid format %c%c%c%c\n", format,
						format >> 8,
						format >> 16,
						format >> 24);
		return -EINVAL;
	}

	reg_val |= (compression ? format_ctrl[i][1] : format_ctrl[i][0]) |
						format_ctrl[i][2] | format_ctrl[i][3];

	switch (blending) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		/* don't do blending, maybe RGBX */
		/* alpha mode select - layer alpha */
		reg_val |= BIT_DPU_LAY_LAYER_ALPHA;
		break;
	case DRM_MODE_BLEND_COVERAGE:
		/* alpha mode select - combo alpha */
		reg_val |= BIT_DPU_LAY_COMBO_ALPHA;
		/* blending mode select - normal mode */
		reg_val &= (~BIT_DPU_LAY_MODE_BLEND_NORMAL);
		break;
	case DRM_MODE_BLEND_PREMULTI:
		/* alpha mode select - combo alpha */
		reg_val |= BIT_DPU_LAY_COMBO_ALPHA;
		/* blending mode select - pre-mult mode */
		reg_val |= BIT_DPU_LAY_MODE_BLEND_PREMULT;
		break;
	default:
		/* alpha mode select - layer alpha */
		reg_val |= BIT_DPU_LAY_LAYER_ALPHA;
		break;
	}

	reg_val |= y2r_coef << 28;
	rotation = to_dpu_rotation(rotation);
	reg_val |= (rotation & 0x7) << 20;

	return reg_val;
}

static void dpu_clean_all(struct dpu_context *ctx)
{
	int i;

	for (i = 0; i < 8; i++)
		DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_CTRL, i), 0x00);

	DPU_REG_WR(ctx->base + REG_LAYER_ENABLE, 0);
}

static void dpu_bgcolor(struct dpu_context *ctx, u32 color)
{
	int ret;
	unsigned long irq_flags;

	if (ctx->if_type == SPRD_DPU_IF_EDPI)
		dpu_wait_stop_done(ctx);

	DPU_REG_WR(ctx->base + REG_BG_COLOR, color);

	dpu_clean_all(ctx);

	if (ctx->is_single_run) {
		if (ctx->cmd_dpi_mode) {
			spin_lock_irqsave(&ctx->irq_lock, irq_flags);
			ctx->dpu_run_flag = true;
			ctx->evt_te = false;
			spin_unlock_irqrestore(&ctx->irq_lock, irq_flags);
			ret = wait_event_interruptible_timeout(ctx->te_wq, ctx->evt_te,
								msecs_to_jiffies(20));
			if (!ret) {
				pr_err("bgcolor set wait for te time out!\n");
			} else if (ret == -ERESTARTSYS) {
				pr_err("bgcolor set waiting process is interrupted by signal!\n");
			}
		} else {
			DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(4));
			DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0));
		}
	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT_DPU_RUN);
		ctx->stopped = false;
	} else if ((ctx->if_type == SPRD_DPU_IF_DPI) && !ctx->stopped) {
		dpu_wait_update_done(ctx);
	}
}

static void dpu_dma_request(struct dpu_context *ctx)
{
	DPU_REG_WR(ctx->base + REG_CABC_CFG5, 1);
}

static void dpu_layer(struct dpu_context *ctx,
		    struct sprd_layer_state *hwlayer)
{
	const struct drm_format_info *info;
	struct layer_reg tmp = {};
	u32 dst_size, src_size, offset, wd, rot;
	int i;

	/* for secure displaying, just use layer 7 as secure layer */
	if ((hwlayer->secure_en || ctx->secure_debug) && ctx->fastcall_en)
		hwlayer->index = 7;

	offset = (hwlayer->dst_x & 0xffff) | ((hwlayer->dst_y) << 16);
	src_size = (hwlayer->src_w & 0xffff) | ((hwlayer->src_h) << 16);
	dst_size = (hwlayer->dst_w & 0xffff) | ((hwlayer->dst_h) << 16);

	if (hwlayer->pallete_en) {
		tmp.pos = offset;
		tmp.src_size = src_size;
		tmp.dst_size = dst_size;
		tmp.alpha = hwlayer->alpha;
		tmp.pallete = hwlayer->pallete_color;

		/* pallete layer enable */
		tmp.ctrl = 0x2004;
		pr_debug("dst_x = %d, dst_y = %d, dst_w = %d, dst_h = %d, pallete:%d\n",
				hwlayer->dst_x, hwlayer->dst_y,
				hwlayer->dst_w, hwlayer->dst_h, tmp.pallete);
	} else {
		if (src_size != dst_size) {
			rot = to_dpu_rotation(hwlayer->rotation);
			if ((rot == DPU_LAYER_ROTATION_90) || (rot == DPU_LAYER_ROTATION_270) ||
				(rot == DPU_LAYER_ROTATION_90_M) || (rot == DPU_LAYER_ROTATION_270_M))
				dst_size = (hwlayer->dst_h & 0xffff) | ((hwlayer->dst_w) << 16);
		}

		/*
		 * FIXME:
		 * Bypass dst and src same size scaling to avoid causing performance problem.
		 * Dst and src frame in same size will still be scaling in current version.
		 * It will be bypassed by digital ip in next version.
		 */
		if (src_size != dst_size)
			tmp.ctrl = BIT(24);
		else
			tmp.ctrl = 0;

		for (i = 0; i < hwlayer->planes; i++) {
			if (hwlayer->addr[i] % 16)
				pr_err("layer addr[%d] is not 16 bytes align, it's 0x%08x\n",
						i, hwlayer->addr[i]);
			tmp.addr[i] = hwlayer->addr[i];
		}

		tmp.pos = offset;
		tmp.src_size = src_size;
		tmp.dst_size = dst_size;
		tmp.crop_start = (hwlayer->src_y << 16) | hwlayer->src_x;
		tmp.alpha = hwlayer->alpha;

		info = drm_format_info(hwlayer->format);
		if (IS_ERR_OR_NULL(info))
		{
			pr_warn("drm format info is invalid.\n");
			return;
		}

		wd = info->cpp[0];
		if (wd == 0) {
			pr_err("layer[%d] bytes per pixel is invalid\n", hwlayer->index);
			return;
		}

		if (hwlayer->planes == 3)
			/* UV pitch is 1/2 of Y pitch*/
			tmp.pitch = (hwlayer->pitch[0] / wd) |
				(hwlayer->pitch[0] / wd << 15);
		else
			tmp.pitch = hwlayer->pitch[0] / wd;

		tmp.ctrl |= dpu_img_ctrl(hwlayer->format, hwlayer->blending,
				hwlayer->xfbc, hwlayer->y2r_coef, hwlayer->rotation);
	}

/*
	if (!ctx->fastcall_en) {
		if (hwlayer->secure_en || ctx->secure_debug) {
			ctx->tos_msg->cmd = TA_REG_SET;
			ctx->tos_msg->version = DPU_R6P0;
			memcpy(ctx->tos_msg + 1, &tmp, sizeof(tmp));
			disp_ca_write(ctx->tos_msg, sizeof(*ctx->tos_msg) + sizeof(tmp));
			disp_ca_wait_response();
			return;
		}
	}
*/

	for (i = 0; i < hwlayer->planes; i++)
		DPU_REG_WR(ctx->base + DPU_LAY_PLANE_ADDR(REG_LAY_BASE_ADDR,
					hwlayer->index, i), tmp.addr[i]);

	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_POS,
			hwlayer->index), tmp.pos);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_SRC_SIZE,
			hwlayer->index), tmp.src_size);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_DES_SIZE,
			hwlayer->index), tmp.dst_size);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_CROP_START,
			hwlayer->index), tmp.crop_start);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_ALPHA,
			hwlayer->index), tmp.alpha);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_PITCH,
			hwlayer->index), tmp.pitch);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_CTRL,
			hwlayer->index), tmp.ctrl);
	DPU_REG_SET(ctx->base + REG_LAYER_ENABLE,
			(1 << hwlayer->index));
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_PALLETE,
				hwlayer->index), tmp.pallete);

	pr_debug("dst_x = %d, dst_y = %d, dst_w = %d, dst_h = %d\n",
				hwlayer->dst_x, hwlayer->dst_y,
				hwlayer->dst_w, hwlayer->dst_h);
	pr_debug("start_x = %d, start_y = %d, start_w = %d, start_h = %d\n",
				hwlayer->src_x, hwlayer->src_y,
				hwlayer->src_w, hwlayer->src_h);
}

static int dpu_vrr_cmd(struct dpu_context *ctx)
{
	return 0;
}

static int dpu_vrr_video(struct dpu_context *ctx)
{
	struct sprd_dpu *dpu = (struct sprd_dpu *)container_of(ctx,
			struct sprd_dpu, ctx);
	u32 reg_val;

	if (ctx->stopped) {
		pr_err("dpu is stoped\n");
 		dpu->crtc->fps_mode_changed = false;
		return 0;
	}

	mutex_lock(&ctx->vrr_lock);

	dpu_stop(ctx);

	reg_val = (ctx->vm.vsync_len << 0) |
		(ctx->vm.vback_porch << 8) |
		(ctx->vm.vfront_porch << 20);
	DPU_REG_WR(ctx->base + REG_DPI_V_TIMING, reg_val);

	reg_val = (ctx->vm.hsync_len << 0) |
		(ctx->vm.hback_porch << 8) |
		(ctx->vm.hfront_porch << 20);
	DPU_REG_WR(ctx->base + REG_DPI_H_TIMING, reg_val);

	dpu_wait_update_done(ctx);
	ctx->stopped = false;
	DPU_REG_WR(ctx->base + REG_DPU_MMU0_UPDATE, 1);
	dpu->crtc->fps_mode_changed = false;

	mutex_unlock(&ctx->vrr_lock);

	return 0;
}

static void dpu_scaling_recovery(struct dpu_context *ctx)
{
	struct scale_config_param *scale_cfg = &ctx->scale_cfg;
	u32 reg_val;

	reg_val = (scale_cfg->in_h << 16) |
		scale_cfg->in_w;
	DPU_REG_WR(ctx->base + REG_BLEND_SIZE, reg_val);
	if (!scale_cfg->need_scale)
		DPU_REG_CLR(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
	else
		DPU_REG_SET(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
}

static void dpu_scaling(struct dpu_context *ctx,
		struct sprd_plane planes[], u8 count)
{
	int i;
	u16 src_w;
	u16 src_h;
	u32 reg_val;
	struct sprd_layer_state *layer_state;
	struct sprd_plane_state *plane_state;
	struct scale_config_param *scale_cfg = &ctx->scale_cfg;
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);

	if (dpu->crtc->sr_mode_changed) {
		pr_debug("------------------------------------\n");
		for (i = 0; i < count; i++) {
			plane_state = to_sprd_plane_state(planes[i].base.state);
			layer_state = &plane_state->layer;
			pr_debug("layer[%d] : %dx%d --- (%d)\n", i,
					layer_state->dst_w, layer_state->dst_h,
					scale_cfg->in_w);
			if (layer_state->dst_w != scale_cfg->in_w) {
				scale_cfg->skip_layer_index = i;
				break;
			}
		}

		plane_state = to_sprd_plane_state(planes[count - 1].base.state);
		layer_state = &plane_state->layer;
		if  (layer_state->dst_w <= scale_cfg->in_w) {
			dpu_sr_config(ctx);
			dpu->crtc->sr_mode_changed = false;
			pr_info("do scaling enhance, bottom layer(%dx%d)\n",
					layer_state->dst_w, layer_state->dst_h);
		}
	} else {
		if (count == 1) {
			plane_state = to_sprd_plane_state(planes[count - 1].base.state);
			layer_state = &plane_state->layer;
			// btm_layer = &layers[count - 1];
			if (layer_state->rotation & (DRM_MODE_ROTATE_90 |
						DRM_MODE_ROTATE_270)) {
				src_w = layer_state->src_h;
				src_h = layer_state->src_w;
			} else {
				src_w = layer_state->src_w;
				src_h = layer_state->src_h;
			}
			if (src_w == layer_state->dst_w
					&& src_h == layer_state->dst_h) {
				dpu_scaling_recovery(ctx);
			} else {
				/*
				 * When the layer src size is not euqal to the
				 * dst size, screened by dpu hal,the single
				 * layer need to scaling-up. Regardless of
				 * whether the SR function is turned on, dpu
				 * blend size should be set to the layer src
				 * size.
				 * However, blend size must be 4 pixel align,
				 * so, if src size is not 4 pixel align, return
				 */
				if ((src_h % 4) || (src_w % 4)) {
					pr_debug("src size is not 4 pixel align, use layer scaler\n");
					dpu_scaling_recovery(ctx);
					return;
				}
				reg_val = (src_h << 16) | src_w;
				DPU_REG_WR(ctx->base + REG_BLEND_SIZE, reg_val);
				/*
				 * When the layer src size is equal to panel
				 * size, close dpu scaling-up function.
				 */
				if (src_h == ctx->vm.vactive &&
						src_w == ctx->vm.hactive) {
					DPU_REG_CLR(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
				} else {
					DPU_REG_SET(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
					layer_state->dst_w = layer_state->src_w;
					layer_state->dst_h = layer_state->src_h;
				}
			}
		} else {
			dpu_scaling_recovery(ctx);
		}
	}
}

/*
static int dpu_secure_state_change(struct dpu_context *ctx, bool secure_en)
{
	int ret;

	if (ctx->fastcall_en) {
		if (secure_en) {
			ctx->wb_pending = true;
			ret = dpu_wait_wb_done(ctx);
			if (ret)
				return ret;

			ret = trusty_fast_call32(NULL, SMC_FC_DPU_FW_SET_SECURITY,
							FW_ATTR_SECURE, 0, 0);
			pr_debug("Trusty fastcall enter secure for dpu\n");
		} else {
			ret = trusty_fast_call32(NULL, SMC_FC_DPU_FW_SET_SECURITY,
							FW_ATTR_NON_SECURE, 0, 0);
			ctx->wb_pending = false;
			pr_debug("Trusty fastcall exit secure for dpu\n");
		}
		if (ret) {
			pr_err("Trusty fastcall set firewall failed, ret = %d\n", ret);
			return -EBUSY;
		}
	} else {
		if (secure_en) {
			static bool disp_connected;

			ctx->wb_pending = true;
			if (!disp_connected) {
				disp_ca_connect();
				udelay(ctx->time);
				disp_connected = true;
			}

			ret = dpu_wait_wb_done(ctx);
			if (ret)
				return ret;

			ctx->tos_msg->cmd = TA_FIREWALL_SET;
			ctx->tos_msg->version = DPU_R6P0;
			disp_ca_write(ctx->tos_msg, sizeof(*ctx->tos_msg));
			disp_ca_wait_response();
			pr_debug("Trusty TA enter secure for dpu\n");
		} else {
			ctx->tos_msg->cmd = TA_REG_CLR;
			ctx->tos_msg->version = DPU_R6P0;
			disp_ca_write(ctx->tos_msg, sizeof(*ctx->tos_msg));
			disp_ca_wait_response();

			ctx->tos_msg->cmd = TA_FIREWALL_CLR;
			ctx->tos_msg->version = DPU_R6P0;
			disp_ca_write(ctx->tos_msg, sizeof(*ctx->tos_msg));
			disp_ca_wait_response();
			ctx->wb_pending = false;
			pr_debug("Trusty TA exit secure for dpu\n");
		}
	}

	return 0;
}
*/

/*
static int dpu_secure_detect(struct dpu_context *ctx, bool enter, bool secure_en)
{
	static bool last_secure_en;

	if (last_secure_en == secure_en)
		return 0;

	pr_debug("last_secure_en:%d secure_en:%d\n", last_secure_en, secure_en);

	if (enter == true && secure_en == true) {
		if (dpu_secure_state_change(ctx, true))
			return -1;
		last_secure_en = secure_en;
	} else if (enter == false && secure_en == false) {
		if (dpu_secure_state_change(ctx, false))
			return -1;
		last_secure_en = secure_en;
	}

	return 0;
}
*/

static void dpu_update_and_wait(struct dpu_context *ctx)
{
	//struct dpu_enhance *enhance = ctx->enhance;
	unsigned long irq_flags;
	int ret;

	if (ctx->is_single_run) {
		if (ctx->cmd_dpi_mode) {
			spin_lock_irqsave(&ctx->irq_lock, irq_flags);
			ctx->dpu_run_flag = true;
			ctx->evt_te_update = false;
			spin_unlock_irqrestore(&ctx->irq_lock, irq_flags);
			ret = wait_event_interruptible_timeout(ctx->te_update_wq, ctx->evt_te_update,
								msecs_to_jiffies(20));
			if (!ret) {
				spin_lock_irqsave(&ctx->irq_lock, irq_flags);
				ctx->dpu_run_flag = false;
				ctx->evt_te_update = false;
				spin_unlock_irqrestore(&ctx->irq_lock, irq_flags);
				pr_err("dpu flip wait for te time out!\n");
			}
		} else {
			DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(4));
			DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0));
		}
	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT_DPU_RUN);
		ctx->stopped = false;
	} else if (ctx->if_type == SPRD_DPU_IF_DPI) {
		//dpu_wait_all_update_done(ctx);
		dpu_wait_update_done(ctx);
		DPU_REG_SET(ctx->base + REG_DPU_INT_EN, BIT_DPU_INT_ERR);
	}
}

static void dpu_flip(struct dpu_context *ctx,
		     struct sprd_plane planes[], u8 count)
{
	int i;
	u32 reg_val;
	struct sprd_plane_state *state;
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);
	struct sprd_dummy_panel_info *info = &dpu->perf_conn->panel_info;

	ctx->vsync_count = 0;

	state = to_sprd_plane_state(planes[0].base.state);

/*
	if (dpu_secure_detect(ctx, true, state->layer.secure_en)) {
		pr_err("dpu switch secure failed!\n");
		return;
	}
*/
	/*
	 * Make sure the dpu is in stop status. DPU_r6p0 has no shadow
	 * registers in EDPI mode. So the config registers can only be
	 * updated in the rising edge of DPU_RUN bit.
	 */
	if (ctx->if_type == SPRD_DPU_IF_EDPI)
		dpu_wait_stop_done(ctx);

	 /* to check if dpu need change the frame rate */
	if ((dpu->crtc->fps_mode_changed && ctx->cmd_dpi_mode) ||
	    (info->vrefresh_cmd_changed && ctx->cmd_dpi_mode)) {
		dpu->crtc->fps_mode_changed = true;
		dpu_vrr_cmd(ctx);
		dpu->crtc->fps_mode_changed = false;
	} else if (dpu->crtc->fps_mode_changed || info->vrefresh_cmd_changed) {
		dpu_vrr_video(ctx);
	}

	/* reset the bgcolor to black */
	DPU_REG_WR(ctx->base + REG_BG_COLOR, 0x00);

	/* disable all the layers */
	dpu_clean_all(ctx);

	/* to check if dpu need scaling the frame for SR */
	dpu_scaling(ctx, planes, count);

	/* start configure dpu layers */
	for (i = 0; i < count; i++) {
		state = to_sprd_plane_state(planes[i].base.state);
		dpu_layer(ctx, &state->layer);
	}

	//dpu_cm_set(ctx, CM_CTM);

	/* update trigger and wait */
	dpu_update_and_wait(ctx);

	/*
	if (dpu_secure_detect(ctx, false, state->layer.secure_en))
		pr_err("dpu switch non secure failed!\n");
	*/
	/*
	 * If the following interrupt was disabled in isr,
	 * re-enable it.
	 */
	reg_val = BIT_DPU_INT_FBC_PLD_ERR |
	BIT_DPU_INT_FBC_HDR_ERR;
	DPU_REG_SET(ctx->base + REG_DPU_INT_EN, reg_val);
}

static void dpu_dpi_init(struct dpu_context *ctx)
{
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);
	struct sprd_dummy_panel_info *info = &dpu->perf_conn->panel_info;
	struct videomode vm;
	u32 int_mask = 0;
	u32 reg_val;
	int i;

	if (ctx->cmd_dpi_mode) {
		for (i = 0; i < info->display_mode_count; i++) {
			if ((info->buildin_modes[i].hdisplay == info->mode.hdisplay) &&
					(info->buildin_modes[i].vdisplay == info->mode.vdisplay) &&
					(drm_mode_vrefresh(&info->buildin_modes[i]) == (info->max_vrefresh))) {
				sprd_drm_mode_copy(&dpu->actual_mode, &(info->buildin_modes[i]));
				drm_display_mode_to_videomode(&dpu->actual_mode, &vm);
				break;
			}
		}
	} else {
		if (dpu->crtc->fps_mode_changed && (dpu->mode.type == DRM_MODE_TYPE_DRIVER)) {
			drm_display_mode_to_videomode(&info->mode, &vm);
		} else if (dpu->mode.type & DRM_MODE_TYPE_USERDEF) {
			drm_display_mode_to_videomode(&info->mode, &vm);
		} else{
			drm_display_mode_to_videomode(&dpu->actual_mode, &vm);
		}
	}

	if (ctx->if_type == SPRD_DPU_IF_DPI) {
		/* use dpi as interface */
		DPU_REG_CLR(ctx->base + REG_DPU_CFG0, BIT_DPU_IF_EDPI);

		/* disable Halt function for SPRD DSI */
		DPU_REG_CLR(ctx->base + REG_DPI_CTRL, BIT_DPU_DPI_HALT_EN);

		if (ctx->is_single_run)
			DPU_REG_SET(ctx->base + REG_DPI_CTRL, (BIT(0)));

		/* set dpi timing */
		reg_val = vm.hsync_len << 0 |
			  vm.hback_porch << 8 |
			  vm.hfront_porch << 20;
		DPU_REG_WR(ctx->base + REG_DPI_H_TIMING, reg_val);

		reg_val = vm.vsync_len << 0 |
			  vm.vback_porch << 8 |
			  vm.vfront_porch << 20;
		DPU_REG_WR(ctx->base + REG_DPI_V_TIMING, reg_val);

		if (!ctx->vrr_enabled && (vm.vsync_len + vm.vback_porch < 32))
			pr_warn("Warning: (vsync + vbp) < 32, "
				"underflow risk!\n");

		if (ctx->vrr_enabled && (vm.vsync_len + vm.vback_porch < 64))
			pr_warn("vrr_enabled, Warning: (vsync + vbp) < 64, "
				"underflow risk!\n");

		/* enable dpu update done INT */
		int_mask |= BIT_DPU_INT_DPU_ALL_UPDATE_DONE;
		int_mask |= BIT_DPU_INT_DPU_REG_UPDATE_DONE;
		int_mask |= BIT_DPU_INT_PQ_LUT_UPDATE_DONE;
		int_mask |= BIT_DPU_INT_LAY_REG_UPDATE_DONE;
		int_mask |= BIT_DPU_INT_PQ_REG_UPDATE_DONE;
		/* enable dpu DONE  INT */
		int_mask |= BIT_DPU_INT_DONE;
		/* enable dpu dpi vsync */
		int_mask |= BIT_DPU_INT_VSYNC;
		/* enable underflow err INT */
		int_mask |= BIT_DPU_INT_ERR;

	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		/* use edpi as interface */
		DPU_REG_SET(ctx->base + REG_DPU_CFG0, BIT_DPU_IF_EDPI);

		/* use external te */
		DPU_REG_SET(ctx->base + REG_DPI_CTRL, BIT_DPU_EDPI_FROM_EXTERNAL_PAD);;

		/* enable te */
		DPU_REG_SET(ctx->base + REG_DPI_CTRL, BIT_DPU_EDPI_TE_EN);

		/* enable stop DONE INT */
		int_mask |= BIT_DPU_INT_DONE;
		/* enable TE INT */
		int_mask |= BIT_DPU_INT_TE;
	}

	/* enable ifbc payload error INT */
	int_mask |= BIT_DPU_INT_FBC_PLD_ERR;
	/* enable ifbc header error INT */
	int_mask |= BIT_DPU_INT_FBC_HDR_ERR;

	DPU_REG_WR(ctx->base + REG_DPU_INT_EN, int_mask);
}

static void enable_vsync(struct dpu_context *ctx)
{
	if (ctx->enabled)
		DPU_REG_SET(ctx->base + REG_DPU_INT_EN, BIT_DPU_INT_VSYNC);
	else
		pr_err("dpu do not has power\n");
}

static void disable_vsync(struct dpu_context *ctx)
{
	// DPU_REG_CLR(ctx->base + REG_DPU_INT_EN, BIT_DPU_INT_VSYNC);
}

static int dpu_context_init(struct dpu_context *ctx, struct device *dev)
{
	struct device_node *qos_np;
	struct dpu_enhance *enhance;
	struct device_node *np = dev->of_node;
	int ret = 0;

	enhance = devm_kzalloc(dev, sizeof(*enhance), GFP_KERNEL);
	if (!enhance) {
		pr_err("%s() enhance kzalloc failed!\n", __func__);
		return -ENOMEM;
	}

	ret = of_property_read_u32(np, "sprd,corner-radius",
					&ctx->corner_radius);
	if (!ret)
		pr_info("round corner support, radius = %d.\n",
					ctx->corner_radius);

	if (of_property_read_bool(np, "sprd,widevine-use-fastcall")) {
		ctx->fastcall_en = true;
		pr_info("read widevine-use-fastcall success, fastcall_en = true\n");
	} else {
		ctx->fastcall_en = false;
		pr_info("read widevine-use-fastcall failed, fastcall_en = false\n");
	}

	qos_np = of_parse_phandle(np, "sprd,qos", 0);
	if (!qos_np)
		pr_warn("can't find dpu qos cfg node\n");

	ret = of_property_read_u8(qos_np, "arqos-low",
					&ctx->qos_cfg.arqos_low);
	if (ret) {
		pr_warn("read arqos-low failed, use default\n");
		ctx->qos_cfg.arqos_low = 0x0a;
	}

	ret = of_property_read_u8(qos_np, "arqos-high",
					&ctx->qos_cfg.arqos_high);
	if (ret) {
		pr_warn("read arqos-high failed, use default\n");
		ctx->qos_cfg.arqos_high = 0x0c;
	}

	ret = of_property_read_u8(qos_np, "awqos-low",
					&ctx->qos_cfg.awqos_low);
	if (ret) {
		pr_warn("read awqos_low failed, use default\n");
		ctx->qos_cfg.awqos_low = 0x0a;
	}

	ret = of_property_read_u8(qos_np, "awqos-high",
					&ctx->qos_cfg.awqos_high);
	if (ret) {
		pr_warn("read awqos-high failed, use default\n");
		ctx->qos_cfg.awqos_high = 0x0c;
	}

	ctx->enhance = enhance;
	enhance->cabc_state = CABC_DISABLED;

	ctx->base_offset[0] = 0x0;
	ctx->base_offset[1] = DPU_MAX_REG_OFFSET / 4;

	ctx->wb_configed = true;
	ctx->evt_wb_done = true;

	/* Allocate memory for trusty */
	ctx->tos_msg = devm_kzalloc(dev, sizeof(struct disp_message) + sizeof(struct layer_reg), GFP_KERNEL);
	if (!ctx->tos_msg)
		return -ENOMEM;

	return 0;
}

static void dpu_sr_config(struct dpu_context *ctx)
{
	struct scale_config_param *scale_cfg = &ctx->scale_cfg;
	u32 reg_val;

	reg_val = (scale_cfg->in_h << 16) | scale_cfg->in_w;
	DPU_REG_WR(ctx->base + REG_BLEND_SIZE, reg_val);
	if (scale_cfg->need_scale)
		DPU_REG_SET(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
	else
		DPU_REG_CLR(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);

	ctx->wb_pending = false;
}

static bool check_dsc_state(struct dpu_context *ctx)
{
	u32 reg_val;

	reg_val = DPU_REG_RD(ctx->base + DSC_REG_OFFSET + REG_DSC_STS1);
	if (reg_val & BIT_DSC_UNDERFLOW_MASK) {
		pr_warn("dsc underflow occurred, need soft reset dsc\n");
		return false;
	} else if (reg_val & BIT_DSC_OVERFLOW_MASK) {
		pr_warn("dsc overflow occurred, need soft reset dsc\n");
		return false;
	}

	return true;
}

static int dpu_modeset(struct dpu_context *ctx,
		struct drm_display_mode *mode)
{
	struct scale_config_param *scale_cfg = &ctx->scale_cfg;
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);
	struct sprd_dummy_panel_info *info = &dpu->perf_conn->panel_info;
	struct sprd_crtc_state *state = to_sprd_crtc_state(dpu->crtc->base.state);
	u32 mode_vrefresh;

	scale_cfg->in_w = mode->hdisplay;
	scale_cfg->in_h = mode->vdisplay;
	mode_vrefresh = drm_mode_vrefresh(mode);

	if (state->resolution_change) {
		if ((mode->hdisplay != info->mode.hdisplay) || (mode->vdisplay != info->mode.vdisplay))
			scale_cfg->need_scale = true;
		else
			scale_cfg->need_scale = false;
		ctx->wb_pending = true;

		dpu->crtc->sr_mode_changed = state->resolution_change;
	}

	if (state->frame_rate_change) {
		dpu->crtc->fps_mode_changed = state->frame_rate_change;
	}

	if (mode_vrefresh == 120) {
		ctx->te_int_max_gap = WAIT_TE_MAX_TIME_120;
		ctx->te_int_min_gap = WAIT_TE_MIN_TIME_120;
	} else if (mode_vrefresh == 90) {
		ctx->te_int_max_gap = WAIT_TE_MAX_TIME_90;
		ctx->te_int_min_gap = WAIT_TE_MIN_TIME_90;
	} else {
		ctx->te_int_max_gap = WAIT_TE_MAX_TIME_60;
		ctx->te_int_min_gap = WAIT_TE_MIN_TIME_60;
	}

	ctx->wb_size_changed = true;
	pr_info("begin switch to %u x %u\n", mode->hdisplay, mode->vdisplay);

	return 0;
}

static void dpu_capability(struct dpu_context *ctx,
			struct sprd_crtc_capability *cap)
{
	cap->max_layers = 6;
	cap->fmts_ptr = primary_fmts;
	cap->fmts_cnt = ARRAY_SIZE(primary_fmts);
}

const struct dpu_core_ops dpu_r6p0_core_ops = {
	.version = dpu_version,
	.init = dpu_init,
	.fini = dpu_fini,
	.run = dpu_run,
	.stop = dpu_stop,
	.isr = dpu_isr,
	.ifconfig = dpu_dpi_init,
	.capability = dpu_capability,
	.bg_color = dpu_bgcolor,
	.flip = dpu_flip,
	.enable_vsync = enable_vsync,
	.disable_vsync = disable_vsync,
	.context_init = dpu_context_init,
	.modeset = dpu_modeset,
	.dma_request = dpu_dma_request,
	.reg_dump = dpu_dump,
	.check_dsc_state = check_dsc_state,
};
