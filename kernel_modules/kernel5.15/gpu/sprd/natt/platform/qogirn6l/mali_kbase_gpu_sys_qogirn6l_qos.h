/*
 * SPDX-FileCopyrightText: 2021 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
*/
//SUBSYS: gpu_sys, sheet:gpu_sys

#ifndef __GPU_SYS_QOS_H__
#define __GPU_SYS_QOS_H__

typedef struct qos_reg {
	const char *reg_name;
	u32 base_addr;
	u32 mask_value;
	u32 set_value;
}QOS_REG_T;

QOS_REG_T nic400_gpu_sys_mtx_m0_qos_list[] = {

    { "REGU_OT_CTRL_EN",                0x23018000, 0x00000001, 0x00000000},
    { "REGU_OT_CTRL_AW_CFG",            0x23018004, 0xffffffff, 0x00000000},
    { "REGU_OT_CTRL_AR_CFG",            0x23018008, 0x3f3f3f3f, 0x00000000},
    { "REGU_OT_CTRL_Ax_CFG",            0x2301800C, 0x3f3fffff, 0x00000000},
    { "REGU_BW_NRT_EN",                 0x23018040, 0x00000003, 0x00000000},
    { "REGU_BW_NRT_W_CFG_0",            0x23018044, 0xffffffff, 0x00000000},
    { "REGU_BW_NRT_W_CFG_1",            0x23018048, 0x073fffff, 0x00000000},
    { "REGU_BW_NRT_R_CFG_0",            0x2301804C, 0xffffffff, 0x00000000},
    { "REGU_BW_NRT_R_CFG_1",            0x23018050, 0x073fffff, 0x00000000},
    { "REGU_AXQOS_GEN_EN",              0x23018060, 0x80000003, 0x00000003},
    { "REGU_AXQOS_GEN_CFG",             0x23018064, 0x3fff3fff, 0x06660666},
    { "REGU_URG_CNT_CFG",               0x23018068, 0x00000701, 0x00000001},
    { "end",                            0x00000000, 0x00000000, 0x00000000}
};

QOS_REG_T gpu_apb_rf_qos_list[] = {

    { "SYS_LPC_M0",                     0x23000004, 0x00010000, 0x00000000},
    { "SYS_LPC_M0",                     0x23000004, 0x00010000, 0x00010000},
    { "end",                            0x00000000, 0x00000000, 0x00000000}
};

#endif
