/*
 * Omnivision TCM touchscreen driver
 *
 * Copyright (C) 2017-2018 OmniVision Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND OmniVision
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL OmniVision BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF OmniVision WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, OmniVision'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

#ifndef _OMNIVISION_TCM_CONFIG_H_
#define _OMNIVISION_TCM_CONFIG_H_

/*choose platform*/
//#define QCOM_PLATFORM
//#define MTK_PLATFORM
#define ZHANRUI_PLATFORM 1

#define CONFIG_SUPPORT_MULTI_FIRMWARE   1  //Firmware Separation

#define SUPPORT_VERIFICATION_REPORT_INFO  1 //Firmware Encryption

/*choose ko or build in kernel*/
#define BUILD_AS_KO_MODULE   1 //need to change the Makefile too


/*wake up gesture: 
1: enable, need to keep lcd power, tp and lcd reset pin should  keep high, isr enable
0 disable, tp isr should be disable before power off lcd
*/
//#define WAKEUP_GESTURE 1
#define AOD_GESTURE_SUPPORT (1)
//#define SPEED_UP_RESUME
//#define CONFIG_OVT_CHARGER_DETECT
//#define CONFIG_OVT_EARJACK_DETECT

#if defined(CONFIG_PM) && CONFIG_PM
#define CONFIG_PM_IN_TP_DRIVER
#endif

#if defined(CONFIG_FB) && CONFIG_FB
#define CONFIG_FB_IN_TP_DRIVER
#endif

#if defined(CONFIG_DRM) && CONFIG_DRM
#define CONFIG_DRM_IN_TP_DRIVER
#endif

#endif
