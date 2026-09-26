# SPDX-License-Identifier: GPL-2.0

sprd-gsp-$(CONFIG_UNISOC_GSP) += gsp_r8p0/gsp_r8p0_core.o \
	gsp_coef_cal.o

# qogirl6 (ums9230) is an r8p0 part and pairs with the sharkl5pro interface,
# which is what its live device tree selects (sprd,gsp-r8p0-qogirl6).
#
# The r9p0 core and the qogirn6pro interface are deliberately NOT built here.
# gsp_r9p0_core.c does not compile against this tree's own
# include/uapi/drm/gsp_r9p0_cfg.h: it references four struct members that header
# never declares (hdr_regamma_lut_table, tone_map_en, reg_hdr_maxcll_gain_bypass).
# That is a pre-existing mismatch in the released source, invisible until
# something enables UNISOC_GSP.  It only ever mattered for qogirn6pro, a
# different SoC generation that this target does not run, and "fixing" it would
# mean editing a uapi struct shared with the userspace GSP HAL -- changing its
# layout under an ABI we cannot test.  Dropping the unused path is the safe fix.
sprd-gsp-$(CONFIG_UNISOC_GSP) += gsp_interface/gsp_interface_sharkl5pro.o

sprd-gsp-$(CONFIG_UNISOC_GSP) += gsp_core.o gsp_dev.o gsp_interface.o gsp_kcfg.o \
	gsp_layer.o gsp_sync.o gsp_sysfs.o gsp_workqueue.o

obj-$(CONFIG_UNISOC_GSP) += sprd-gsp.o
#sprd-gsp-$(CONFIG_DRM_SPRD_GSP_IPC_TRUSTY) += gsp_ipc_trusty.o
#sprd-gsp-$(CONFIG_DRM_SPRD_GSP_DVFS) += gsp_r9p0/gsp_r9p0_dvfs.o
