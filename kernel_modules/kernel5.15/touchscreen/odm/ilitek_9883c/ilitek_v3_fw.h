/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Dicky Chiang <dicky_chiang@ilitek.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __ILITEK_V3_FW_H
#define __ILITEK_V3_FW_H

/* define names and paths for the variety of tp modules */
#define DEF_INI_NAME_PATH		"/sdcard/mp.ini"
#define DEF_FW_FILP_PATH		"/sdcard/ILITEK_FW"
#define DEF_INI_REQUEST_PATH		"mp.ini"
#define DEF_FW_REQUEST_PATH		"FW_TXD_BOE_ILI9883C.bin"
static unsigned char CTPM_FW_DEF[] = {
	#include "FW_TDDI_TRUNK_FB.ili"
};

#define BOE_9883C_LATTET_INI_NAME_PATH		"/sdcard/mp_ILI9883C_LATTET.ini"
#define BOE_9883C_LATTET_FW_FILP_PATH		"/sdcard/ILITEK_FW_ILI9883C_LATTET"
#define BOE_9883C_LATTET_INI_REQUEST_PATH		"mp_ILI9883C_LATTET.ini"
#define BOE_9883C_LATTET_FW_REQUEST_PATH		"FW_TXD_BOE_ILI9883C_LATTET.bin"
static unsigned char CTPM_FW_BOE_9883C_LATTET[] = {
	#include "FW_TDDI_TRUNK_FB_ILI9883C_LATTET.ili"
};

#define BOE_79505A_INI_NAME_PATH		"/sdcard/mp_ILI79505A.ini"
#define BOE_79505A_FW_FILP_PATH		"/sdcard/ILITEK_FW_ILI79505A"
#define BOE_79505A_INI_REQUEST_PATH		"mp_ILI79505A.ini"
#define BOE_79505A_FW_REQUEST_PATH		"FW_TXD_BOE_ILI79505A.bin"
static unsigned char CTPM_FW_BOE_79505A[] = {
	#include "FW_TDDI_TRUNK_FB_ILI79505A.ili"
};

#define TXD_79505A_NAIROBI_INI_NAME_PATH		"/sdcard/mp_ILI79505A_NAIROBI.ini"
#define TXD_79505A_NAIROBI_FW_FILP_PATH		"/sdcard/ILITEK_FW_ILI79505A_NAIROBI"
#define TXD_79505A_NAIROBI_INI_REQUEST_PATH		"mp_ILI79505A_NAIROBI.ini"
#define TXD_79505A_NAIROBI_FW_REQUEST_PATH		"FW_TXD_BOE_ILI79505A_NAIROBI.bin"
static unsigned char CTPM_FW_TXD_NAIROBI_79505A[] = {
	#include "FW_TDDI_TRUNK_FB_ILI79505A_NAIROBI.ili"
};

#define TXD_79505A_CRUISERB4_INI_NAME_PATH		"/sdcard/mp_TXD_ILI79505A_CRUISERB4.ini"
#define TXD_79505A_CRUISERB4_FW_FILP_PATH		"/sdcard/ILITEK_FW_TXD_ILI79505A_CRUISERB4"
#define TXD_79505A_CRUISERB4_INI_REQUEST_PATH		"mp_TXD_ILI79505A_CRUISERB4.ini"
#define TXD_79505A_CRUISERB4_FW_REQUEST_PATH		"FW_TXD_ILI79505A_CRUISERB4.bin"
static unsigned char CTPM_FW_TXD_CRUISERB4_79505A[] = {
	#include "FW_TDDI_TRUNK_FB_TXD_ILI79505A_CRUISERB4.ili"
};

#define TM_79505A_CRUISERB4_INI_NAME_PATH		"/sdcard/mp_TM_ILI79505A_CRUISERB4.ini"
#define TM_79505A_CRUISERB4_FW_FILP_PATH		"/sdcard/ILITEK_FW_TM_ILI79505A_CRUISERB4"
#define TM_79505A_CRUISERB4_INI_REQUEST_PATH		"mp_TM_ILI79505A_CRUISERB4.ini"
#define TM_79505A_CRUISERB4_FW_REQUEST_PATH		"FW_TM_ILI79505A_CRUISERB4.bin"
static unsigned char CTPM_FW_TM_CRUISERB4_79505A[] = {
	#include "FW_TDDI_TRUNK_FB_TM_ILI79505A_CRUISERB4.ili"
};

#define CSOT_INI_NAME_PATH		"/sdcard/mp_csot.ini"
#define CSOT_FW_FILP_PATH		"/sdcard/ILITEK_FW_CSOT"
#define CSOT_INI_REQUEST_PATH		"mp_csot.ini"
#define CSOT_FW_REQUEST_PATH		"ILITEK_FW_CSOT"
static unsigned char CTPM_FW_CSOT[] = {
	0xFF,
};

#define AUO_INI_NAME_PATH		"/sdcard/mp_auo.ini"
#define AUO_FW_FILP_PATH		"/sdcard/ILITEK_FW_AUO"
#define AUO_INI_REQUEST_PATH		"mp_auo.ini"
#define AUO_FW_REQUEST_PATH		"ILITEK_FW_AUO"
static unsigned char CTPM_FW_AUO[] = {
	0xFF,
};

#define BOE_INI_NAME_PATH		"/sdcard/mp_boe.ini"
#define BOE_FW_FILP_PATH		"/sdcard/ILITEK_FW_BOE"
#define BOE_INI_REQUEST_PATH		"mp_boe.ini"
#define BOE_FW_REQUEST_PATH		"ILITEK_FW_BOE"
static unsigned char CTPM_FW_BOE[] = {
	0xFF,
};

#define INX_INI_NAME_PATH		"/sdcard/mp_inx.ini"
#define INX_FW_FILP_PATH		"/sdcard/ILITEK_FW_INX"
#define INX_INI_REQUEST_PATH		"mp_inx.ini"
#define INX_FW_REQUEST_PATH		"ILITEK_FW_INX"
static unsigned char CTPM_FW_INX[] = {
	0xFF,
};

#define DJ_INI_NAME_PATH		"/sdcard/mp_dj.ini"
#define DJ_FW_FILP_PATH                 "/sdcard/ILITEK_FW_DJ"
#define DJ_INI_REQUEST_PATH		"mp_dj.ini"
#define DJ_FW_REQUEST_PATH		"ILITEK_FW_DJ"
static unsigned char CTPM_FW_DJ[] = {
	0xFF,
};

#define TXD_INI_NAME_PATH		"/sdcard/mp_txd.ini"
#define TXD_FW_FILP_PATH		"/sdcard/ILITEK_FW_TXD"
#define TXD_INI_REQUEST_PATH		"mp_txd.ini"
#define TXD_FW_REQUEST_PATH		"ILITEK_FW_TXD"
static unsigned char CTPM_FW_TXD[] = {
	0xFF,
};

#define TM_INI_NAME_PATH		"/sdcard/mp_tm.ini"
#define TM_FW_FILP_PATH                 "/sdcard/ILITEK_FW_TM"
#define TM_INI_REQUEST_PATH		"mp_tm.ini"
#define TM_FW_REQUEST_PATH		"ILITEK_FW_TM"
static unsigned char CTPM_FW_TM[] = {
	0xFF,
};

#endif