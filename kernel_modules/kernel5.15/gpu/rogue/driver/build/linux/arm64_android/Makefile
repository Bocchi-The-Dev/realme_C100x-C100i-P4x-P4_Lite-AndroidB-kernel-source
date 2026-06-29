########################################################################### ###
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#@License       Dual MIT/GPLv2
#
# The contents of this file are subject to the MIT license as set out below.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
#
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
#
# This License is also included in this distribution in the file called
# "MIT-COPYING".
#
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

override TARGET_DEVICE := arm64_android

# Maca BVNC:
RGX_BVNC ?= 35.3.408.101

include ../config/preconfig.mk

PVR_SYSTEM := rgx_nohw

include ../common/android/arch.mk
include ../common/android/features.mk

# Heap names should be defined as per the labels used in the kernel.
# Kernels >=4.12, support heap query so we use heap names
# to get the correct heap id.
# For kernels <4.12, we use heap_id_mask defined in this Makefile.
# Heap names defined here are for reference only.
ION_DEFAULT_HEAP_NAME := \"system\"
ION_DEFAULT_HEAP_ID_MASK := (1 << ION_HEAP_TYPE_SYSTEM)
ION_FALLBACK_HEAP_NAME := \"carveout\"
ION_FALLBACK_HEAP_ID_MASK := (1 << ION_HEAP_TYPE_CARVEOUT)

PVR_ANDROID_COMPOSERHAL ?= drm
DISPLAY_CONTROLLER := drm_nulldisp
KERNEL_COMPONENTS += $(DISPLAY_CONTROLLER)
PVR_DRM_MODESET_DRIVER_NAME := nulldisp

DEVICE_MEMSETCPY_ALIGN_IN_BYTES := 16

PVR_LDM_PLATFORM_PRE_REGISTERED := 1

RGX_SECONDARY_OS_CLOCK_SOURCE := sched

include ../config/core.mk
include ../common/android/extra_config.mk
