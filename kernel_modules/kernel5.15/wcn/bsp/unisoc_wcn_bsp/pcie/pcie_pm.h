/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PCIE_PM_H__
#define __PCIE_PM_H__
#include "wcn_bus.h"

/* B = 0x70 (PCI_CAP_ID_EXP) ID = 0x10 */
#define VF_LINK_CAPABILITIES_REG		0x0C
#define PCIE_CAP_ACTIVE_STATE_LINK_PM_SUPPORT	((0x1 << 10) | (0x1 << 11))

#define VF_LINK_CONTROL_LINK_STATUS_REG		0x10

#define PCIE_CAP_ACTIVE_STATE_LINK_PM_CONTROL	(0x1 << 0)
#define PCIE_CAP_EN_CLK_POWER_MAN		(0x1 << 8)

#define DEVICE_CONTROL2_DEVICE_STATUS2_REG	0x28
#define PCIE_CAP_LTR_EN				(0x1 << 10)

#define PL_LTR_LATENCY_OFF			0xB30
#define SNOOP_LATENCY_VALUE			0xffff
#define SNOOP_LATENCY_SCALE			0xff0ff

/* B = 0x150(L1SS PM Substatus) ID=0x001E */
#define L1SUB_CONTROL1_REG			0x8
#define L1_1_ASPM_EN				(0x1 << 3)
#define L1_2_ASPM_EN				(0x1 << 2)
#define L1_2_PCIPM_EN				0x0
#define T_COMMON_MODE				(0x2 << 8)
#define L1_2_TH_VAL				(0x80 << 16)

#define L1SUB_CONTROL2_REG			0xC
#define T_POWER_ON_VALUE			(0x1 << 3)
#define T_POWER_ON_SCALE			((0x1 << 0) | (0x1 << 1))

struct aspm_register_info {
	u32 support:2;
	u32 enabled:2;
	u32 latency_encoding_l0s;
	u32 latency_encoding_l1;

	/* L1 substates */
	u32 l1ss_cap_ptr;
	u32 l1ss_cap;
	u32 l1ss_ctl1;
	u32 l1ss_ctl2;
};

#ifndef ASPM_STATE_L0S_UP
#define ASPM_STATE_L0S_UP	(1)	/* Upstream direction L0s state */
#endif
#ifndef ASPM_STATE_L0S_DW
#define ASPM_STATE_L0S_DW	(2)	/* Downstream direction L0s state */
#endif
#ifndef ASPM_STATE_L1
#define ASPM_STATE_L1		(4)	/* L1 state */
#endif
#ifndef ASPM_STATE_L1_1
#define ASPM_STATE_L1_1		(8)	/* ASPM L1.1 state */
#endif
#ifndef ASPM_STATE_L1_2
#define ASPM_STATE_L1_2		(0x10)	/* ASPM L1.2 state */
#endif
#ifndef ASPM_STATE_L1_1_PCIPM
#define ASPM_STATE_L1_1_PCIPM	(0x20)	/* PCI PM L1.1 state */
#endif
#ifndef ASPM_STATE_L1_2_PCIPM
#define ASPM_STATE_L1_2_PCIPM	(0x40)	/* PCI PM L1.2 state */
#endif
#ifndef ASPM_STATE_L1_SS_PCIPM
#define ASPM_STATE_L1_SS_PCIPM	(ASPM_STATE_L1_1_PCIPM | ASPM_STATE_L1_2_PCIPM)
#endif
#ifndef ASPM_STATE_L1_2_MASK
#define ASPM_STATE_L1_2_MASK	(ASPM_STATE_L1_2 | ASPM_STATE_L1_2_PCIPM)
#endif
#ifndef ASPM_STATE_L1SS
#define ASPM_STATE_L1SS		(ASPM_STATE_L1_1 | ASPM_STATE_L1_1_PCIPM |\
				 ASPM_STATE_L1_2_MASK)
#endif
#ifndef ASPM_STATE_L0S
#define ASPM_STATE_L0S		(ASPM_STATE_L0S_UP | ASPM_STATE_L0S_DW)
#endif
#ifndef ASPM_STATE_ALL
#define ASPM_STATE_ALL		(ASPM_STATE_L0S | ASPM_STATE_L1 |	\
				 ASPM_STATE_L1SS)
#endif

int sprdwcn_pci_enable_link_state(struct pci_dev *pdev, enum wcn_bus_pm_state state);

#endif
