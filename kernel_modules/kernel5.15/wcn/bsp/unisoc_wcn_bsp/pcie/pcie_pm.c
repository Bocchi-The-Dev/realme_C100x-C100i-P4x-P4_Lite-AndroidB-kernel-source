/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include "pcie_pm.h"
#include "wcn_dbg.h"
#include "pcie.h"

static u32 calc_l0s_latency(u32 encoding)
{
	if (encoding == 0x7)
		return (5 * 1000);	/* > 4us */
	return (64 << encoding);
}

static u32 calc_l1_latency(u32 encoding)
{
	if (encoding == 0x7)
		return (65 * 1000);	/* > 64us */
	return (1000 << encoding);
}

/* Convert L1SS T_pwr encoding to usec */
static u32 calc_l1ss_pwron(struct pci_dev *pdev, u32 scale, u32 val)
{
	switch (scale) {
	case 0:
		return val * 2;
	case 1:
		return val * 10;
	case 2:
		return val * 100;
	}
	pci_err(pdev, "%s: Invalid T_PwrOn scale: %u\n", __func__, scale);
	return 0;
}

static u32 calc_l0s_acceptable(u32 encoding)
{
	if (encoding == 0x7)
		return -1U;
	return (64 << encoding);
}

static u32 calc_l1_acceptable(u32 encoding)
{
	if (encoding == 0x7)
		return -1U;
	return (1000 << encoding);
}

static void encode_l12_threshold(u32 threshold_us, u32 *scale, u32 *value)
{
	u32 threshold_ns = threshold_us * 1000;

	/* See PCIe r3.1, sec 7.33.3 and sec 6.18 */
	if (threshold_ns < 32) {
		*scale = 0;
		*value = threshold_ns;
	} else if (threshold_ns < 1024) {
		*scale = 1;
		*value = threshold_ns >> 5;
	} else if (threshold_ns < 32768) {
		*scale = 2;
		*value = threshold_ns >> 10;
	} else if (threshold_ns < 1048576) {
		*scale = 3;
		*value = threshold_ns >> 15;
	} else if (threshold_ns < 33554432) {
		*scale = 4;
		*value = threshold_ns >> 20;
	} else {
		*scale = 5;
		*value = threshold_ns >> 25;
	}
}

static void pcie_get_aspm_reg(struct pci_dev *pdev,
			      struct aspm_register_info *info)
{
	u16 reg16;
	u32 reg32;

	pcie_capability_read_dword(pdev, PCI_EXP_LNKCAP, &reg32);
	info->support = (reg32 & PCI_EXP_LNKCAP_ASPMS) >> 10;
	info->latency_encoding_l0s = (reg32 & PCI_EXP_LNKCAP_L0SEL) >> 12;
	info->latency_encoding_l1  = (reg32 & PCI_EXP_LNKCAP_L1EL) >> 15;
	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &reg16);
	info->enabled = reg16 & PCI_EXP_LNKCTL_ASPMC;

	/* Read L1 PM substate capabilities */
	info->l1ss_cap = info->l1ss_ctl1 = info->l1ss_ctl2 = 0;
	info->l1ss_cap_ptr = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);
	if (!info->l1ss_cap_ptr)
		return;
	pci_read_config_dword(pdev, info->l1ss_cap_ptr + PCI_L1SS_CAP,
			      &info->l1ss_cap);
	if (!(info->l1ss_cap & PCI_L1SS_CAP_L1_PM_SS)) {
		info->l1ss_cap = 0;
		return;
	}
	pci_read_config_dword(pdev, info->l1ss_cap_ptr + PCI_L1SS_CTL1,
			      &info->l1ss_ctl1);
	pci_read_config_dword(pdev, info->l1ss_cap_ptr + PCI_L1SS_CTL2,
			      &info->l1ss_ctl2);
}

static void sprdwcn_pci_clear_and_set_dword(struct pci_dev *pdev, int pos,
					u32 clear, u32 set)
{
	u32 val;

	pci_read_config_dword(pdev, pos, &val);
	val &= ~clear;
	val |= set;
	pci_write_config_dword(pdev, pos, val);
}

bool sprdwcn_pcie_retrain_link(struct pci_dev *parent)
{
	unsigned long end_jiffies;
	u16 reg16;

	pcie_capability_read_word(parent, PCI_EXP_LNKCTL, &reg16);
	reg16 |= PCI_EXP_LNKCTL_RL;
	pcie_capability_write_word(parent, PCI_EXP_LNKCTL, reg16);
	if (parent->clear_retrain_link) {
		reg16 &= ~PCI_EXP_LNKCTL_RL;
		pcie_capability_write_word(parent, PCI_EXP_LNKCTL, reg16);
	}

	end_jiffies = jiffies + HZ;
	do {
		pcie_capability_read_word(parent, PCI_EXP_LNKSTA, &reg16);
		if (!(reg16 & PCI_EXP_LNKSTA_LT))
			break;
		usleep_range(1000, 2000);
	} while (time_before(jiffies, end_jiffies));
	return !(reg16 & PCI_EXP_LNKSTA_LT);
}

static void sprdwcn_pcie_aspm_configure_common_clock(struct pci_dev *child)
{
	int same_clock = 1;
	u16 reg16, parent_reg, child_reg;
	struct pci_dev *parent = child->bus->self;
	bool consistent = true;

	pcie_capability_read_word(child, PCI_EXP_LNKSTA, &reg16);
	WCN_INFO("EP(0x%x): PCI_EXP_LNKSTA=0x%x\n", pci_pcie_cap(child) + PCI_EXP_LNKSTA, reg16);
	if (!(reg16 & PCI_EXP_LNKSTA_SLC))
		same_clock = 0;

	pcie_capability_read_word(parent, PCI_EXP_LNKSTA, &reg16);
	WCN_INFO("RC(0x%x): PCI_EXP_LNKSTA=0x%x\n", pci_pcie_cap(parent) + PCI_EXP_LNKSTA, reg16);
	if (!(reg16 & PCI_EXP_LNKSTA_SLC))
		same_clock = 0;

	pcie_capability_read_word(parent, PCI_EXP_LNKCTL, &reg16);
	if (same_clock && (reg16 & PCI_EXP_LNKCTL_CCC)) {
		do {
			pcie_capability_read_word(child, PCI_EXP_LNKCTL,
						  &reg16);
			if (!(reg16 & PCI_EXP_LNKCTL_CCC)) {
				consistent = false;
				break;
			}
		} while (0);
		if (consistent)
			return;
		WCN_INFO("Reconfiguring common clock\n");
	}

	pcie_capability_read_word(child, PCI_EXP_LNKCTL, &reg16);
	child_reg = reg16;
	if (same_clock)
		reg16 |= PCI_EXP_LNKCTL_CCC;
	else
		reg16 &= ~PCI_EXP_LNKCTL_CCC;
	pcie_capability_write_word(child, PCI_EXP_LNKCTL, reg16);

	pcie_capability_read_word(parent, PCI_EXP_LNKCTL, &reg16);
	parent_reg = reg16;
	if (same_clock)
		reg16 |= PCI_EXP_LNKCTL_CCC;
	else
		reg16 &= ~PCI_EXP_LNKCTL_CCC;
	pcie_capability_write_word(parent, PCI_EXP_LNKCTL, reg16);

	if (sprdwcn_pcie_retrain_link(parent))
		return;

	WCN_ERR("ASPM: Could not configure common clock\n");
	pcie_capability_write_word(child, PCI_EXP_LNKCTL,
						child_reg);
	pcie_capability_write_word(parent, PCI_EXP_LNKCTL, parent_reg);
}

static void sprdwcn_aspm_calc_l1ss_info(struct pci_dev *pdev,
				struct aspm_register_info *upreg,
				struct aspm_register_info *dwreg, u32 aspm_cap)
{
	u32 val1, val2, scale1, scale2;
	u32 t_common_mode, t_power_on, l1_2_threshold, scale, value;
	struct wcn_pcie_info *priv = pci_get_drvdata(pdev);

	priv->link_state.l1ss.up_cap_ptr = upreg->l1ss_cap_ptr;
	priv->link_state.l1ss.dw_cap_ptr = dwreg->l1ss_cap_ptr;
	priv->link_state.l1ss.ctl1 = priv->link_state.l1ss.ctl2 = 0;

	if (!(aspm_cap & ASPM_STATE_L1_2_MASK))
		return;

	val1 = (upreg->l1ss_cap & PCI_L1SS_CAP_CM_RESTORE_TIME) >> 8;
	val2 = (dwreg->l1ss_cap & PCI_L1SS_CAP_CM_RESTORE_TIME) >> 8;
	t_common_mode = max(val1, val2);

	val1 = (upreg->l1ss_cap & PCI_L1SS_CAP_P_PWR_ON_VALUE) >> 19;
	scale1 = (upreg->l1ss_cap & PCI_L1SS_CAP_P_PWR_ON_SCALE) >> 16;
	val2 = (dwreg->l1ss_cap & PCI_L1SS_CAP_P_PWR_ON_VALUE) >> 19;
	scale2 = (dwreg->l1ss_cap & PCI_L1SS_CAP_P_PWR_ON_SCALE) >> 16;

	if (calc_l1ss_pwron(priv->link_state.parent, scale1, val1) >
		calc_l1ss_pwron(priv->link_state.child, scale2, val2)) {
		priv->link_state.l1ss.ctl2 |= scale1 | (val1 << 3);
		t_power_on = calc_l1ss_pwron(priv->link_state.parent, scale1, val1);
	} else {
		priv->link_state.l1ss.ctl2 |= scale2 | (val2 << 3);
		t_power_on = calc_l1ss_pwron(priv->link_state.child, scale2, val2);
	}

	l1_2_threshold = 2 + 4 + t_common_mode + t_power_on;
	encode_l12_threshold(l1_2_threshold, &scale, &value);
	priv->link_state.l1ss.ctl1 |= t_common_mode << 8 | scale << 29 | value << 16;
}

static void sprdwcn_pcie_aspm_check_latency(struct pci_dev *child, u32 *aspm_cap)
{
	u32 latency, l1_switch_latency = 0;
	struct aspm_latency *acceptable;
	struct wcn_pcie_info *priv = pci_get_drvdata(child);

	if ((child->current_state != PCI_D0) &&
		(child->current_state != PCI_UNKNOWN))
		return;
	acceptable = &priv->link_state.acceptable;
	if (((*aspm_cap & ASPM_STATE_L0S_UP) &&
				(priv->link_state.latency_up.l0s > acceptable->l0s)))
		*aspm_cap &= ASPM_STATE_L0S_UP;

	if (((*aspm_cap & ASPM_STATE_L0S_DW) &&
				(priv->link_state.latency_dw.l0s > acceptable->l0s)))
		*aspm_cap &= ASPM_STATE_L0S_DW;

	latency = max_t(u32, priv->link_state.latency_up.l1, priv->link_state.latency_dw.l1);
	if ((*aspm_cap & ASPM_STATE_L1) && (latency + l1_switch_latency > acceptable->l1))
		*aspm_cap &= ~ASPM_STATE_L1;
}

static int sprdwcn_pcie_aspm_get_cap(struct pci_dev *pdev, u32 *aspm_cap, u32 *aspm_eb)
{
	struct aspm_register_info upreg = {0}, dwreg = {0};
	struct wcn_pcie_info *priv;
	u32 reg32, encoding;
	struct aspm_latency *acceptable;

	if (!pdev || !pdev->bus->self || !aspm_cap || !aspm_eb)
		return -EINVAL;

	priv = pci_get_drvdata(pdev);
	acceptable = &priv->link_state.acceptable;

	pcie_get_aspm_reg(pdev->bus->self, &upreg);
	pcie_get_aspm_reg(pdev, &dwreg);

	if (!(upreg.support & dwreg.support))
		return -EINVAL;

	sprdwcn_pcie_aspm_configure_common_clock(pdev);

	pcie_get_aspm_reg(pdev->bus->self, &upreg);
	pcie_get_aspm_reg(pdev, &dwreg);

	if (dwreg.support & upreg.support & PCIE_LINK_STATE_L0S)
		*aspm_cap |= ASPM_STATE_L0S;
	if (upreg.support & dwreg.support & PCIE_LINK_STATE_L1)
		*aspm_cap |= ASPM_STATE_L1;
	if (upreg.l1ss_cap & dwreg.l1ss_cap & PCI_L1SS_CAP_ASPM_L1_1)
		*aspm_cap |= ASPM_STATE_L1_1;
	if (upreg.l1ss_cap & dwreg.l1ss_cap & PCI_L1SS_CAP_ASPM_L1_2)
		*aspm_cap |= ASPM_STATE_L1_2;
	if (upreg.l1ss_cap & dwreg.l1ss_cap & PCI_L1SS_CAP_PCIPM_L1_1)
		*aspm_cap |= ASPM_STATE_L1_1_PCIPM;
	if (upreg.l1ss_cap & dwreg.l1ss_cap & PCI_L1SS_CAP_PCIPM_L1_2)
		*aspm_cap |= ASPM_STATE_L1_2_PCIPM;

	if (dwreg.enabled & PCIE_LINK_STATE_L0S)
		*aspm_eb |= ASPM_STATE_L0S_UP;
	if (upreg.enabled & PCIE_LINK_STATE_L0S)
		*aspm_eb |= ASPM_STATE_L0S_DW;
	if (upreg.enabled & dwreg.enabled & PCIE_LINK_STATE_L1)
		*aspm_eb |= ASPM_STATE_L1;
	if (upreg.l1ss_ctl1 & dwreg.l1ss_ctl1 & PCI_L1SS_CTL1_ASPM_L1_1)
		*aspm_eb |= ASPM_STATE_L1_1;
	if (upreg.l1ss_ctl1 & dwreg.l1ss_ctl1 & PCI_L1SS_CTL1_ASPM_L1_2)
		*aspm_eb |= ASPM_STATE_L1_2;
	if (upreg.l1ss_ctl1 & dwreg.l1ss_ctl1 & PCI_L1SS_CTL1_PCIPM_L1_1)
		*aspm_eb |= ASPM_STATE_L1_1_PCIPM;
	if (upreg.l1ss_ctl1 & dwreg.l1ss_ctl1 & PCI_L1SS_CTL1_PCIPM_L1_2)
		*aspm_eb |= ASPM_STATE_L1_2_PCIPM;

	priv->link_state.latency_up.l0s = calc_l0s_latency(upreg.latency_encoding_l0s);
	priv->link_state.latency_dw.l0s = calc_l0s_latency(dwreg.latency_encoding_l0s);
	priv->link_state.latency_up.l1 = calc_l1_latency(upreg.latency_encoding_l1);
	priv->link_state.latency_dw.l1 = calc_l1_latency(dwreg.latency_encoding_l1);

	if (*aspm_cap & ASPM_STATE_L1SS)
		sprdwcn_aspm_calc_l1ss_info(pdev, &upreg, &dwreg, *aspm_cap);

	if (pci_pcie_type(pdev) != PCI_EXP_TYPE_ENDPOINT &&
		pci_pcie_type(pdev) != PCI_EXP_TYPE_LEG_END) {
		WCN_INFO("PCIe device type:%d\n", pci_pcie_type(pdev));
		goto out;
	}
	pcie_capability_read_dword(pdev, PCI_EXP_DEVCAP, &reg32);
	encoding = (reg32 & PCI_EXP_DEVCAP_L0S) >> 6;
	acceptable->l0s = calc_l0s_acceptable(encoding);
	encoding = (reg32 & PCI_EXP_DEVCAP_L1) >> 9;
	acceptable->l1 = calc_l1_acceptable(encoding);
	sprdwcn_pcie_aspm_check_latency(pdev, aspm_cap);

out:
	WCN_INFO("PCIe ASPM, enabled=0x%x[support=0x%x]\n", *aspm_eb, *aspm_cap);
	WCN_INFO("PCI_EXT_CAP_ID_L1SS[RC:0x%x, EP:0x%x]\n", upreg.l1ss_cap_ptr, dwreg.l1ss_cap_ptr);

	return 0;
}

static int sprdwcn_pcie_pm_state_to_link_state(enum wcn_bus_pm_state state)
{
	int link_state = -1;

	switch (state) {
	case BUS_PM_DISABLE:
		link_state = 0;
		break;
	case BUS_PM_L0s_L1_ENABLE:
		link_state = ASPM_STATE_L0S | ASPM_STATE_L1;
		break;
	case BUS_PM_ALL_ENABLE:
		link_state = ASPM_STATE_ALL;
		break;
	default:
		return  -EINVAL;
	}

	return link_state;
}

static int sprdwcn_pcie_capability_clear_and_set_word(struct pci_dev *dev, int pos,
						u16 clear, u16 set)
{
	int ret;
	u16 val;

	ret = pcie_capability_read_word(dev, pos, &val);
	if (!ret) {
		val &= ~clear;
		val |= set;
		ret = pcie_capability_write_word(dev, pos, val);
	}

	return ret;
}

static void sprdwcn_pcie_config_aspm_l1ss(struct pci_dev *child, u32 state)
{
	struct wcn_pcie_info *priv = pci_get_drvdata(child);
	struct pci_dev *parent = child->bus->self;
	u32 enable_req = 0, val = 0, up_cap_ptr = priv->link_state.l1ss.up_cap_ptr,
		dw_cap_ptr = priv->link_state.l1ss.dw_cap_ptr;
	int ret = 0;

	enable_req = (priv->link_state.aspm_enabled ^ state) & state;

	ret |= pci_read_config_dword(child, dw_cap_ptr + PCI_L1SS_CTL1, &val);
	WCN_INFO("EP L1SS enable=0x%x, enable_req=0x%x\n", val, enable_req);
	if (!ret && ((val & PCI_L1SS_CTL1_L1SS_MASK) == (enable_req & PCI_L1SS_CTL1_L1SS_MASK)))
		WCN_INFO("%s EP ASPM already enabled\n", __func__);

	ret |= pci_read_config_dword(parent, up_cap_ptr + PCI_L1SS_CTL1, &val);
	WCN_INFO("RC L1SS enable=0x%x, enable_req=0x%x\n", val, enable_req);
	if (!ret && ((val & PCI_L1SS_CTL1_L1SS_MASK) == (enable_req & PCI_L1SS_CTL1_L1SS_MASK)))
		WCN_INFO("%s RC ASPM already enabled\n", __func__);

	if (ret) {
		WCN_ERR("PCIe read config error %d\n", ret);
		return;
	}
	/* Disable all L1 substates */
	sprdwcn_pci_clear_and_set_dword(child, dw_cap_ptr + PCI_L1SS_CTL1,
				PCI_L1SS_CTL1_L1SS_MASK, 0);
	sprdwcn_pci_clear_and_set_dword(parent, up_cap_ptr + PCI_L1SS_CTL1,
				PCI_L1SS_CTL1_L1SS_MASK, 0);

	if (enable_req & (ASPM_STATE_L1_1 | ASPM_STATE_L1_2)) {
		sprdwcn_pcie_capability_clear_and_set_word(child, PCI_EXP_LNKCTL,
						   PCI_EXP_LNKCTL_ASPM_L1, 0);
		sprdwcn_pcie_capability_clear_and_set_word(parent, PCI_EXP_LNKCTL,
						   PCI_EXP_LNKCTL_ASPM_L1, 0);
	}

	/* The ASPM/PCIPM L1.2 must be disabled while programming timing parameters */
	if (enable_req & ASPM_STATE_L1_2_MASK) {
		pci_write_config_dword(parent, up_cap_ptr + PCI_L1SS_CTL2,
				       priv->link_state.l1ss.ctl2);
		pci_write_config_dword(child, dw_cap_ptr + PCI_L1SS_CTL2,
				       priv->link_state.l1ss.ctl2);

		sprdwcn_pci_clear_and_set_dword(parent, up_cap_ptr + PCI_L1SS_CTL1,
					PCI_L1SS_CTL1_CM_RESTORE_TIME,
					priv->link_state.l1ss.ctl1);
		sprdwcn_pci_clear_and_set_dword(parent,	up_cap_ptr + PCI_L1SS_CTL1,
					PCI_L1SS_CTL1_LTR_L12_TH_VALUE |
					PCI_L1SS_CTL1_LTR_L12_TH_SCALE,
					priv->link_state.l1ss.ctl1);
		sprdwcn_pci_clear_and_set_dword(child, dw_cap_ptr + PCI_L1SS_CTL1,
					PCI_L1SS_CTL1_LTR_L12_TH_VALUE |
					PCI_L1SS_CTL1_LTR_L12_TH_SCALE,
					priv->link_state.l1ss.ctl1);
	}
	val = 0;
	if (state & ASPM_STATE_L1_1)
		val |= PCI_L1SS_CTL1_ASPM_L1_1;
	if (state & ASPM_STATE_L1_2)
		val |= PCI_L1SS_CTL1_ASPM_L1_2;
	if (state & ASPM_STATE_L1_1_PCIPM)
		val |= PCI_L1SS_CTL1_PCIPM_L1_1;
	if (state & ASPM_STATE_L1_2_PCIPM)
		val |= PCI_L1SS_CTL1_PCIPM_L1_2;

	WCN_INFO("%s: Set L1SS 0x%x(0x%x)\n", __func__, state, val);
	/* Enable what we need to enable */
	sprdwcn_pci_clear_and_set_dword(parent, up_cap_ptr + PCI_L1SS_CTL1,
				PCI_L1SS_CTL1_L1SS_MASK, val);
	sprdwcn_pci_clear_and_set_dword(child, dw_cap_ptr + PCI_L1SS_CTL1,
				PCI_L1SS_CTL1_L1SS_MASK, val);
}

static void sprdwcn_pcie_config_aspm_l0s_l1(struct pci_dev *pdev, u32 val)
{
	sprdwcn_pcie_capability_clear_and_set_word(pdev, PCI_EXP_LNKCTL,
					   PCI_EXP_LNKCTL_ASPMC, val);
}

static int sprdwcn_pcie_config_aspm_link(struct pci_dev *child, u32 state)
{
	struct wcn_pcie_info *priv = pci_get_drvdata(child);
	struct sprdwcn_pcie_link_state *link = &priv->link_state;
	struct pci_dev *parent = child->bus->self;
	u32 upstream = 0, dwstream = 0;

	state &= link->aspm_cap;

	if (!(state & ASPM_STATE_L1) && (state & ASPM_STATE_L1SS)) {
		WCN_INFO("L1ss must enable L1\n");
		state &= ~ASPM_STATE_L1SS;
	}

	if (parent->current_state != PCI_D0 || child->current_state != PCI_D0) {
		WCN_INFO("RC state=%d, EP state=%d\n", parent->current_state, child->current_state);
		state &= ~ASPM_STATE_L1_SS_PCIPM;
		state |= (link->aspm_enabled & ASPM_STATE_L1_SS_PCIPM);
	}

	if (state & ASPM_STATE_L0S_UP)
		dwstream |= PCI_EXP_LNKCTL_ASPM_L0S;
	if (state & ASPM_STATE_L0S_DW)
		upstream |= PCI_EXP_LNKCTL_ASPM_L0S;
	if (state & ASPM_STATE_L1) {
		upstream |= PCI_EXP_LNKCTL_ASPM_L1;
		dwstream |= PCI_EXP_LNKCTL_ASPM_L1;
	}

	if (link->aspm_cap & ASPM_STATE_L1SS)
		sprdwcn_pcie_config_aspm_l1ss(child, state);

	if (state & ASPM_STATE_L1) {
		pci_info(parent, "%s: RC L0s %s, L1 Enable(0x%x)\n", __func__,
			(upstream & PCI_EXP_LNKCTL_ASPM_L0S) ? "Enable" : "Disable", upstream);
		sprdwcn_pcie_config_aspm_l0s_l1(parent, upstream);
		pci_info(child, "%s: EP L0s %s, L1 Enable(0x%x)\n", __func__,
			(dwstream & PCI_EXP_LNKCTL_ASPM_L0S) ? "Enable" : "Disable", dwstream);
		sprdwcn_pcie_config_aspm_l0s_l1(child, dwstream);
	} else {
		/* L1 status of EP should also be disabled. */
		pci_info(child, "%s: EP L0s %s, L1 Disable(0x%x)\n", __func__,
			(dwstream & PCI_EXP_LNKCTL_ASPM_L0S) ? "Enable" : "Disable", upstream);
		sprdwcn_pcie_config_aspm_l0s_l1(child, dwstream);
		pci_info(parent, "%s: RC L0s %s, L1 Disable(0x%x)\n", __func__,
			(upstream & PCI_EXP_LNKCTL_ASPM_L0S) ? "Enable" : "Disable", dwstream);
		sprdwcn_pcie_config_aspm_l0s_l1(parent, upstream);
	}

	return 0;
}

/**
 * sprdwcn_pci_enable_link_state - Set ASPM status
 * @pdev: EP(downstream device) to be set
 * @state: BUS_PM_* e.g. L0s/L1/L1.1/L1.2
 *
 * Only RC->EP models are supported. If the upstream device is a bridge device,
 * it cannot meet the support requirements.
 *
 * Return: 0-success
 */
int sprdwcn_pci_enable_link_state(struct pci_dev *pdev, enum wcn_bus_pm_state state)
{
	u32 aspm_cap = 0, aspm_eb = 0;
	int ret = -1, link_state_pending = 0;
	struct wcn_pcie_info *priv = pci_get_drvdata(pdev);

	if (!priv) {
		WCN_ERR("%s priv is null\n", __func__);
		return -EINVAL;
	}

	WARN(pcie_aspm_enabled(pdev), "kernel standard ASPM policy conflict!");
	priv->link_state.child = pdev;
	priv->link_state.parent = pci_upstream_bridge(pdev);
	if (!priv->link_state.parent) {
		WCN_ERR("The device architecture does not meet the requirements\n");
		return ret;
	}

	ret = sprdwcn_pcie_aspm_get_cap(pdev, &aspm_cap, &aspm_eb);
	if (ret < 0) {
		WCN_ERR("failed to get ASPM CAP\n");
		return ret;
	}
	WCN_INFO("ASPM L1.2 Config: ctl1=0x%x, ctl2=0x%x\n",
		priv->link_state.l1ss.ctl1, priv->link_state.l1ss.ctl2);

	if (aspm_cap != priv->link_state.aspm_cap) {
		WCN_INFO("update ASPM capable(0X%x to 0x%x)\n",
				priv->link_state.aspm_cap, aspm_cap);
		priv->link_state.aspm_cap = aspm_cap;
	}
	if (aspm_eb != priv->link_state.aspm_enabled) {
		WCN_INFO("update ASPM enabled(0X%x to 0x%x)\n",
				priv->link_state.aspm_enabled, aspm_eb);
		priv->link_state.aspm_enabled = aspm_eb;
	}

	link_state_pending = sprdwcn_pcie_pm_state_to_link_state(state);
	if (link_state_pending < 0)
		return -EINVAL;

	WCN_INFO("ASPM(0x%x->0x%x)\n", aspm_eb, link_state_pending);
	if (aspm_eb == link_state_pending) {
		WCN_INFO("ASPM not change\n");
		return 0;
	}

	sprdwcn_pcie_config_aspm_link(pdev, link_state_pending);
	priv->link_state.aspm_enabled = link_state_pending;

	return 0;
}

int sprdwcn_pci_enable_link_state_sample(enum wcn_bus_pm_state state)
{
	struct wcn_pcie_info *priv = get_wcn_device_info();

	if (!priv) {
		WCN_ERR("%s priv is null\n", __func__);
		return -EINVAL;
	}

	return sprdwcn_pci_enable_link_state(priv->dev, state);
}
