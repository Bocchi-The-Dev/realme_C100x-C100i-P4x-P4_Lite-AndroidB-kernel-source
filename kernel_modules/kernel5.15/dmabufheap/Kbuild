#SPDX-FileCopyrightText: 2019 Unisoc (Shanghai) Technologies Co.Ltd
#SPDX-License-Identifier: GPL-2.0-only

#
# Source List
#
KO_MODULE_NAME := sprd-dmabuf
KO_MODULE_PATH := $(src)
KO_MODULE_SRC  :=

KO_MODULE_SRC += $(wildcard $(KO_MODULE_PATH)/*.c)

#
# Build Options
#
ccflags-y := \
	-I$(srctree)/include/linux \
	-I$(srctree)/include/uapi/linux \
	-I$(src)

#
# Final Objects
#
obj-m := $(KO_MODULE_NAME).o
