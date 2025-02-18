/*
 * Broadcom Starfighter 2 DSA switch driver
 *
 * Copyright (C) 2014, Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/mii.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <net/dsa.h>
#include <linux/ethtool.h>
#include <linux/if_bridge.h>
#include <linux/brcmphy.h>
#include <linux/etherdevice.h>
#include <linux/platform_data/b53.h>

#include "bcm_sf2.h"
#include "bcm_sf2_regs.h"
#include "b53/b53_priv.h"
#include "b53/b53_regs.h"

static enum dsa_tag_protocol bcm_sf2_sw_get_tag_protocol(struct dsa_switch *ds)
{
	return DSA_TAG_PROTO_BRCM;
}

static void bcm_sf2_imp_vlan_setup(struct dsa_switch *ds, int cpu_port)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int i;
	u32 reg;

	/* Enable the IMP Port to be in the same VLAN as the other ports
	 * on a per-port basis such that we only have Port i and IMP in
	 * the same VLAN.
	 */
	for (i = 0; i < priv->hw_params.num_ports; i++) {
		if (!((1 << i) & ds->enabled_port_mask))
			continue;

		reg = core_readl(priv, CORE_PORT_VLAN_CTL_PORT(i));
		reg |= (1 << cpu_port);
		core_writel(priv, reg, CORE_PORT_VLAN_CTL_PORT(i));
	}
}

static void bcm_sf2_brcm_hdr_setup(struct bcm_sf2_priv *priv, int port)
{
	u32 reg, val;

	/* Resolve which bit controls the Broadcom tag */
	switch (port) {
	case 8:
		val = BRCM_HDR_EN_P8;
		break;
	case 7:
		val = BRCM_HDR_EN_P7;
		break;
	case 5:
		val = BRCM_HDR_EN_P5;
		break;
	default:
		val = 0;
		break;
	}

	/* Enable Broadcom tags for IMP port */
	reg = core_readl(priv, CORE_BRCM_HDR_CTRL);
	reg |= val;
	core_writel(priv, reg, CORE_BRCM_HDR_CTRL);

	/* Enable reception Broadcom tag for CPU TX (switch RX) to
	 * allow us to tag outgoing frames
	 */
	reg = core_readl(priv, CORE_BRCM_HDR_RX_DIS);
	reg &= ~(1 << port);
	core_writel(priv, reg, CORE_BRCM_HDR_RX_DIS);

	/* Enable transmission of Broadcom tags from the switch (CPU RX) to
	 * allow delivering frames to the per-port net_devices
	 */
	reg = core_readl(priv, CORE_BRCM_HDR_TX_DIS);
	reg &= ~(1 << port);
	core_writel(priv, reg, CORE_BRCM_HDR_TX_DIS);
}

static void bcm_sf2_imp_setup(struct dsa_switch *ds, int port)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int i;
	u32 reg, offset;

	/* Enable the port memories */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg &= ~P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);

	/* Enable forwarding */
	core_writel(priv, SW_FWDG_EN, CORE_SWMODE);

	/* Enable IMP port in dumb mode */
	reg = core_readl(priv, CORE_SWITCH_CTRL);
	reg |= MII_DUMB_FWDG_EN;
	core_writel(priv, reg, CORE_SWITCH_CTRL);

	/* Configure Traffic Class to QoS mapping, allow each priority to map
	 * to a different queue number
	 */
	reg = core_readl(priv, CORE_PORT_TC2_QOS_MAP_PORT(port));
	for (i = 0; i < SF2_NUM_EGRESS_QUEUES; i++)
		reg |= i << (PRT_TO_QID_SHIFT * i);
	core_writel(priv, reg, CORE_PORT_TC2_QOS_MAP_PORT(port));

	bcm_sf2_brcm_hdr_setup(priv, port);

	if (port == 8) {
		if (priv->type == BCM7445_DEVICE_ID)
			offset = CORE_STS_OVERRIDE_IMP;
		else
			offset = CORE_STS_OVERRIDE_IMP2;

		/* Force link status for IMP port */
		reg = core_readl(priv, offset);
		reg |= (MII_SW_OR | LINK_STS);
		reg &= ~GMII_SPEED_UP_2G;
		core_writel(priv, reg, offset);

		/* Enable Broadcast, Multicast, Unicast forwarding to IMP port */
		reg = core_readl(priv, CORE_IMP_CTL);
		reg |= (RX_BCST_EN | RX_MCST_EN | RX_UCST_EN);
		reg &= ~(RX_DIS | TX_DIS);
		core_writel(priv, reg, CORE_IMP_CTL);
	} else {
		reg = core_readl(priv, CORE_G_PCTL_PORT(port));
		reg &= ~(RX_DIS | TX_DIS);
		core_writel(priv, reg, CORE_G_PCTL_PORT(port));
	}
}

static void bcm_sf2_eee_enable_set(struct dsa_switch *ds, int port, bool enable)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 reg;

	reg = core_readl(priv, CORE_EEE_EN_CTRL);
	if (enable)
		reg |= 1 << port;
	else
		reg &= ~(1 << port);
	core_writel(priv, reg, CORE_EEE_EN_CTRL);
}

static void bcm_sf2_gphy_enable_set(struct dsa_switch *ds, bool enable)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 reg;

	reg = reg_readl(priv, REG_SPHY_CNTRL);
	if (enable) {
		reg |= PHY_RESET;
		reg &= ~(EXT_PWR_DOWN | IDDQ_BIAS | IDDQ_GLOBAL_PWR | CK25_DIS);
		reg_writel(priv, reg, REG_SPHY_CNTRL);
		udelay(21);
		reg = reg_readl(priv, REG_SPHY_CNTRL);
		reg &= ~PHY_RESET;
	} else {
		reg |= EXT_PWR_DOWN | IDDQ_BIAS | PHY_RESET;
		reg_writel(priv, reg, REG_SPHY_CNTRL);
		mdelay(1);
		reg |= CK25_DIS;
	}
	reg_writel(priv, reg, REG_SPHY_CNTRL);

	/* Use PHY-driven LED signaling */
	if (!enable) {
		reg = reg_readl(priv, REG_LED_CNTRL(0));
		reg |= SPDLNK_SRC_SEL;
		reg_writel(priv, reg, REG_LED_CNTRL(0));
	}
}

static inline void bcm_sf2_port_intr_enable(struct bcm_sf2_priv *priv,
					    int port)
{
	unsigned int off;

	switch (port) {
	case 7:
		off = P7_IRQ_OFF;
		break;
	case 0:
		/* Port 0 interrupts are located on the first bank */
		intrl2_0_mask_clear(priv, P_IRQ_MASK(P0_IRQ_OFF));
		return;
	default:
		off = P_IRQ_OFF(port);
		break;
	}

	intrl2_1_mask_clear(priv, P_IRQ_MASK(off));
}

static inline void bcm_sf2_port_intr_disable(struct bcm_sf2_priv *priv,
					     int port)
{
	unsigned int off;

	switch (port) {
	case 7:
		off = P7_IRQ_OFF;
		break;
	case 0:
		/* Port 0 interrupts are located on the first bank */
		intrl2_0_mask_set(priv, P_IRQ_MASK(P0_IRQ_OFF));
		intrl2_0_writel(priv, P_IRQ_MASK(P0_IRQ_OFF), INTRL2_CPU_CLEAR);
		return;
	default:
		off = P_IRQ_OFF(port);
		break;
	}

	intrl2_1_mask_set(priv, P_IRQ_MASK(off));
	intrl2_1_writel(priv, P_IRQ_MASK(off), INTRL2_CPU_CLEAR);
}

static int bcm_sf2_port_setup(struct dsa_switch *ds, int port,
			      struct phy_device *phy)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	s8 cpu_port = ds->dst->cpu_dp->index;
	unsigned int i;
	u32 reg;

	/* Clear the memory power down */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg &= ~P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);

	/* Disable learning */
	reg = core_readl(priv, CORE_DIS_LEARN);
	reg |= BIT(port);
	core_writel(priv, reg, CORE_DIS_LEARN);

	/* Enable Broadcom tags for that port if requested */
	if (priv->brcm_tag_mask & BIT(port))
		bcm_sf2_brcm_hdr_setup(priv, port);

	/* Configure Traffic Class to QoS mapping, allow each priority to map
	 * to a different queue number
	 */
	reg = core_readl(priv, CORE_PORT_TC2_QOS_MAP_PORT(port));
	for (i = 0; i < SF2_NUM_EGRESS_QUEUES; i++)
		reg |= i << (PRT_TO_QID_SHIFT * i);
	core_writel(priv, reg, CORE_PORT_TC2_QOS_MAP_PORT(port));

	/* Clear the Rx and Tx disable bits and set to no spanning tree */
	core_writel(priv, 0, CORE_G_PCTL_PORT(port));

	/* Re-enable the GPHY and re-apply workarounds */
	if (priv->int_phy_mask & 1 << port && priv->hw_params.num_gphy == 1) {
		bcm_sf2_gphy_enable_set(ds, true);
		if (phy) {
			/* if phy_stop() has been called before, phy
			 * will be in halted state, and phy_start()
			 * will call resume.
			 *
			 * the resume path does not configure back
			 * autoneg settings, and since we hard reset
			 * the phy manually here, we need to reset the
			 * state machine also.
			 */
			phy->state = PHY_READY;
			phy_init_hw(phy);
		}
	}

	/* Enable MoCA port interrupts to get notified */
	if (port == priv->moca_port)
		bcm_sf2_port_intr_enable(priv, port);

	/* Set this port, and only this one to be in the default VLAN,
	 * if member of a bridge, restore its membership prior to
	 * bringing down this port.
	 */
	reg = core_readl(priv, CORE_PORT_VLAN_CTL_PORT(port));
	reg &= ~PORT_VLAN_CTRL_MASK;
	reg |= (1 << port);
	reg |= priv->dev->ports[port].vlan_ctl_mask;
	core_writel(priv, reg, CORE_PORT_VLAN_CTL_PORT(port));

	bcm_sf2_imp_vlan_setup(ds, cpu_port);

	/* If EEE was enabled, restore it */
	if (priv->port_sts[port].eee.eee_enabled)
		bcm_sf2_eee_enable_set(ds, port, true);

	return 0;
}

static void bcm_sf2_port_disable(struct dsa_switch *ds, int port,
				 struct phy_device *phy)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 off, reg;

	if (priv->wol_ports_mask & (1 << port))
		return;

	if (port == priv->moca_port)
		bcm_sf2_port_intr_disable(priv, port);

	if (priv->int_phy_mask & 1 << port && priv->hw_params.num_gphy == 1)
		bcm_sf2_gphy_enable_set(ds, false);

	if (dsa_is_cpu_port(ds, port))
		off = CORE_IMP_CTL;
	else
		off = CORE_G_PCTL_PORT(port);

	reg = core_readl(priv, off);
	reg |= RX_DIS | TX_DIS;
	core_writel(priv, reg, off);

	/* Power down the port memory */
	reg = core_readl(priv, CORE_MEM_PSM_VDD_CTRL);
	reg |= P_TXQ_PSM_VDD(port);
	core_writel(priv, reg, CORE_MEM_PSM_VDD_CTRL);
}

/* Returns 0 if EEE was not enabled, or 1 otherwise
 */
static int bcm_sf2_eee_init(struct dsa_switch *ds, int port,
			    struct phy_device *phy)
{
	int ret;

	ret = phy_init_eee(phy, 0);
	if (ret)
		return 0;

	bcm_sf2_eee_enable_set(ds, port, true);

	return 1;
}

static int bcm_sf2_sw_get_mac_eee(struct dsa_switch *ds, int port,
				  struct ethtool_eee *e)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct ethtool_eee *p = &priv->port_sts[port].eee;
	u32 reg;

	reg = core_readl(priv, CORE_EEE_LPI_INDICATE);
	e->eee_enabled = p->eee_enabled;
	e->eee_active = !!(reg & (1 << port));

	return 0;
}

static int bcm_sf2_sw_set_mac_eee(struct dsa_switch *ds, int port,
				  struct ethtool_eee *e)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct ethtool_eee *p = &priv->port_sts[port].eee;

	p->eee_enabled = e->eee_enabled;
	bcm_sf2_eee_enable_set(ds, port, e->eee_enabled);

	return 0;
}

static int bcm_sf2_sw_indir_rw(struct bcm_sf2_priv *priv, int op, int addr,
			       int regnum, u16 val)
{
	int ret = 0;
	u32 reg;

	reg = reg_readl(priv, REG_SWITCH_CNTRL);
	reg |= MDIO_MASTER_SEL;
	reg_writel(priv, reg, REG_SWITCH_CNTRL);

	/* Page << 8 | offset */
	reg = 0x70;
	reg <<= 2;
	core_writel(priv, addr, reg);

	/* Page << 8 | offset */
	reg = 0x80 << 8 | regnum << 1;
	reg <<= 2;

	if (op)
		ret = core_readl(priv, reg);
	else
		core_writel(priv, val, reg);

	reg = reg_readl(priv, REG_SWITCH_CNTRL);
	reg &= ~MDIO_MASTER_SEL;
	reg_writel(priv, reg, REG_SWITCH_CNTRL);

	return ret & 0xffff;
}

static int bcm_sf2_sw_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct bcm_sf2_priv *priv = bus->priv;

	/* Intercept reads from Broadcom pseudo-PHY address, else, send
	 * them to our master MDIO bus controller
	 */
	if (addr == BRCM_PSEUDO_PHY_ADDR && priv->indir_phy_mask & BIT(addr))
		return bcm_sf2_sw_indir_rw(priv, 1, addr, regnum, 0);
	else
		return mdiobus_read_nested(priv->master_mii_bus, addr, regnum);
}

static int bcm_sf2_sw_mdio_write(struct mii_bus *bus, int addr, int regnum,
				 u16 val)
{
	struct bcm_sf2_priv *priv = bus->priv;

	/* Intercept writes to the Broadcom pseudo-PHY address, else,
	 * send them to our master MDIO bus controller
	 */
	if (addr == BRCM_PSEUDO_PHY_ADDR && priv->indir_phy_mask & BIT(addr))
		return bcm_sf2_sw_indir_rw(priv, 0, addr, regnum, val);
	else
		return mdiobus_write_nested(priv->master_mii_bus, addr,
				regnum, val);
}

static irqreturn_t bcm_sf2_switch_0_isr(int irq, void *dev_id)
{
	struct bcm_sf2_priv *priv = dev_id;

	priv->irq0_stat = intrl2_0_readl(priv, INTRL2_CPU_STATUS) &
				~priv->irq0_mask;
	intrl2_0_writel(priv, priv->irq0_stat, INTRL2_CPU_CLEAR);

	return IRQ_HANDLED;
}

static irqreturn_t bcm_sf2_switch_1_isr(int irq, void *dev_id)
{
	struct bcm_sf2_priv *priv = dev_id;

	priv->irq1_stat = intrl2_1_readl(priv, INTRL2_CPU_STATUS) &
				~priv->irq1_mask;
	intrl2_1_writel(priv, priv->irq1_stat, INTRL2_CPU_CLEAR);

	if (priv->irq1_stat & P_LINK_UP_IRQ(P7_IRQ_OFF))
		priv->port_sts[7].link = 1;
	if (priv->irq1_stat & P_LINK_DOWN_IRQ(P7_IRQ_OFF))
		priv->port_sts[7].link = 0;

	return IRQ_HANDLED;
}

static int bcm_sf2_sw_rst(struct bcm_sf2_priv *priv)
{
	unsigned int timeout = 1000;
	u32 reg;

	reg = core_readl(priv, CORE_WATCHDOG_CTRL);
	reg |= SOFTWARE_RESET | EN_CHIP_RST | EN_SW_RESET;
	core_writel(priv, reg, CORE_WATCHDOG_CTRL);

	do {
		reg = core_readl(priv, CORE_WATCHDOG_CTRL);
		if (!(reg & SOFTWARE_RESET))
			break;

		usleep_range(1000, 2000);
	} while (timeout-- > 0);

	if (timeout == 0)
		return -ETIMEDOUT;

	return 0;
}

static void bcm_sf2_intr_disable(struct bcm_sf2_priv *priv)
{
	intrl2_0_mask_set(priv, 0xffffffff);
	intrl2_0_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);
	intrl2_1_mask_set(priv, 0xffffffff);
	intrl2_1_writel(priv, 0xffffffff, INTRL2_CPU_CLEAR);
}

static void bcm_sf2_identify_ports(struct bcm_sf2_priv *priv,
				   struct device_node *dn)
{
	struct device_node *port;
	int mode;
	unsigned int port_num;

	priv->moca_port = -1;

	for_each_available_child_of_node(dn, port) {
		if (of_property_read_u32(port, "reg", &port_num))
			continue;

		/* Internal PHYs get assigned a specific 'phy-mode' property
		 * value: "internal" to help flag them before MDIO probing
		 * has completed, since they might be turned off at that
		 * time
		 */
		mode = of_get_phy_mode(port);
		if (mode < 0)
			continue;

		if (mode == PHY_INTERFACE_MODE_INTERNAL)
			priv->int_phy_mask |= 1 << port_num;

		if (mode == PHY_INTERFACE_MODE_MOCA)
			priv->moca_port = port_num;

		if (of_property_read_bool(port, "brcm,use-bcm-hdr"))
			priv->brcm_tag_mask |= 1 << port_num;
	}
}

static int bcm_sf2_mdio_register(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct device_node *dn;
	static int index;
	int err;

	/* Find our integrated MDIO bus node */
	dn = of_find_compatible_node(NULL, NULL, "brcm,unimac-mdio");
	priv->master_mii_bus = of_mdio_find_bus(dn);
	if (!priv->master_mii_bus) {
		of_node_put(dn);
		return -EPROBE_DEFER;
	}

	get_device(&priv->master_mii_bus->dev);
	priv->master_mii_dn = dn;

	priv->slave_mii_bus = devm_mdiobus_alloc(ds->dev);
	if (!priv->slave_mii_bus) {
		of_node_put(dn);
		return -ENOMEM;
	}

	priv->slave_mii_bus->priv = priv;
	priv->slave_mii_bus->name = "sf2 slave mii";
	priv->slave_mii_bus->read = bcm_sf2_sw_mdio_read;
	priv->slave_mii_bus->write = bcm_sf2_sw_mdio_write;
	snprintf(priv->slave_mii_bus->id, MII_BUS_ID_SIZE, "sf2-%d",
		 index++);
	priv->slave_mii_bus->dev.of_node = dn;

	/* Include the pseudo-PHY address to divert reads towards our
	 * workaround. This is only required for 7445D0, since 7445E0
	 * disconnects the internal switch pseudo-PHY such that we can use the
	 * regular SWITCH_MDIO master controller instead.
	 *
	 * Here we flag the pseudo PHY as needing special treatment and would
	 * otherwise make all other PHY read/writes go to the master MDIO bus
	 * controller that comes with this switch backed by the "mdio-unimac"
	 * driver.
	 */
	if (of_machine_is_compatible("brcm,bcm7445d0"))
		priv->indir_phy_mask |= (1 << BRCM_PSEUDO_PHY_ADDR);
	else
		priv->indir_phy_mask = 0;

	ds->phys_mii_mask = priv->indir_phy_mask;
	ds->slave_mii_bus = priv->slave_mii_bus;
	priv->slave_mii_bus->parent = ds->dev->parent;
	priv->slave_mii_bus->phy_mask = ~priv->indir_phy_mask;

	if (dn)
		err = of_mdiobus_register(priv->slave_mii_bus, dn);
	else
		err = mdiobus_register(priv->slave_mii_bus);

	if (err)
		of_node_put(dn);

	return err;
}

static void bcm_sf2_mdio_unregister(struct bcm_sf2_priv *priv)
{
	mdiobus_unregister(priv->slave_mii_bus);
	if (priv->master_mii_dn)
		of_node_put(priv->master_mii_dn);
}

static u32 bcm_sf2_sw_get_phy_flags(struct dsa_switch *ds, int port)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);

	/* The BCM7xxx PHY driver expects to find the integrated PHY revision
	 * in bits 15:8 and the patch level in bits 7:0 which is exactly what
	 * the REG_PHY_REVISION register layout is.
	 */
	if (priv->int_phy_mask & BIT(port))
		return priv->hw_params.gphy_rev;
	else
		return 0;
}

static void bcm_sf2_sw_adjust_link(struct dsa_switch *ds, int port,
				   struct phy_device *phydev)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct ethtool_eee *p = &priv->port_sts[port].eee;
	u32 id_mode_dis = 0, port_mode;
	u16 lcl_adv = 0, rmt_adv = 0;
	const char *str = NULL;
	u8 flowctrl = 0;
	u32 reg, offset;

	if (priv->type == BCM7445_DEVICE_ID)
		offset = CORE_STS_OVERRIDE_GMIIP_PORT(port);
	else
		offset = CORE_STS_OVERRIDE_GMIIP2_PORT(port);

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		str = "RGMII (no delay)";
		id_mode_dis = 1;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (!str)
			str = "RGMII (TX delay)";
		port_mode = EXT_GPHY;
		break;
	case PHY_INTERFACE_MODE_MII:
		str = "MII";
		port_mode = EXT_EPHY;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		str = "Reverse MII";
		port_mode = EXT_REVMII;
		break;
	default:
		/* All other PHYs: internal and MoCA */
		goto force_link;
	}

	/* If the link is down, just disable the interface to conserve power */
	if (!phydev->link) {
		reg = reg_readl(priv, REG_RGMII_CNTRL_P(port));
		reg &= ~RGMII_MODE_EN;
		reg_writel(priv, reg, REG_RGMII_CNTRL_P(port));
		goto force_link;
	}

	/* Clear id_mode_dis bit, and the existing port mode, but
	 * make sure we enable the RGMII block for data to pass
	 */
	reg = reg_readl(priv, REG_RGMII_CNTRL_P(port));
	reg &= ~ID_MODE_DIS;
	reg &= ~(PORT_MODE_MASK << PORT_MODE_SHIFT);
	reg &= ~(RX_PAUSE_EN | TX_PAUSE_EN);

	reg |= port_mode | RGMII_MODE_EN;
	if (id_mode_dis)
		reg |= ID_MODE_DIS;

	if (phydev->pause) {
		if (phydev->asym_pause)
			reg |= TX_PAUSE_EN;
		reg |= RX_PAUSE_EN;
	}

	reg_writel(priv, reg, REG_RGMII_CNTRL_P(port));

	pr_debug("Port %d configured for %s\n", port, str);

force_link:
	/* Force link settings detected from the PHY */
	reg = SW_OVERRIDE;
	switch (phydev->speed) {
	case SPEED_1000:
		reg |= SPDSTS_1000 << SPEED_SHIFT;
		break;
	case SPEED_100:
		reg |= SPDSTS_100 << SPEED_SHIFT;
		break;
	}

	if (phydev->duplex == DUPLEX_FULL &&
	    phydev->autoneg == AUTONEG_ENABLE) {
		if (phydev->pause)
			rmt_adv = LPA_PAUSE_CAP;
		if (phydev->asym_pause)
			rmt_adv |= LPA_PAUSE_ASYM;
		if (phydev->advertising & ADVERTISED_Pause)
			lcl_adv = ADVERTISE_PAUSE_CAP;
		if (phydev->advertising & ADVERTISED_Asym_Pause)
			lcl_adv |= ADVERTISE_PAUSE_ASYM;
		flowctrl = mii_resolve_flowctrl_fdx(lcl_adv, rmt_adv);
	}

	if (phydev->link)
		reg |= LINK_STS;
	if (phydev->duplex == DUPLEX_FULL)
		reg |= DUPLX_MODE;
	if (flowctrl & FLOW_CTRL_TX)
		reg |= TXFLOW_CNTL;
	if (flowctrl & FLOW_CTRL_RX)
		reg |= RXFLOW_CNTL;

	core_writel(priv, reg, offset);

	if (!phydev->is_pseudo_fixed_link)
		p->eee_enabled = bcm_sf2_eee_init(ds, port, phydev);
}

static void bcm_sf2_sw_fixed_link_update(struct dsa_switch *ds, int port,
					 struct fixed_phy_status *status)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	u32 duplex, pause, offset;
	u32 reg;

	if (priv->type == BCM7445_DEVICE_ID)
		offset = CORE_STS_OVERRIDE_GMIIP_PORT(port);
	else
		offset = CORE_STS_OVERRIDE_GMIIP2_PORT(port);

	duplex = core_readl(priv, CORE_DUPSTS);
	pause = core_readl(priv, CORE_PAUSESTS);

	status->link = 0;

	/* MoCA port is special as we do not get link status from CORE_LNKSTS,
	 * which means that we need to force the link at the port override
	 * level to get the data to flow. We do use what the interrupt handler
	 * did determine before.
	 *
	 * For the other ports, we just force the link status, since this is
	 * a fixed PHY device.
	 */
	if (port == priv->moca_port) {
		status->link = priv->port_sts[port].link;
		/* For MoCA interfaces, also force a link down notification
		 * since some version of the user-space daemon (mocad) use
		 * cmd->autoneg to force the link, which messes up the PHY
		 * state machine and make it go in PHY_FORCING state instead.
		 */
		if (!status->link)
			netif_carrier_off(ds->ports[port].netdev);
		status->duplex = 1;
	} else {
		status->link = 1;
		status->duplex = !!(duplex & (1 << port));
	}

	reg = core_readl(priv, offset);
	reg |= SW_OVERRIDE;
	if (status->link)
		reg |= LINK_STS;
	else
		reg &= ~LINK_STS;
	core_writel(priv, reg, offset);

	if ((pause & (1 << port)) &&
	    (pause & (1 << (port + PAUSESTS_TX_PAUSE_SHIFT)))) {
		status->asym_pause = 1;
		status->pause = 1;
	}

	if (pause & (1 << port))
		status->pause = 1;
}

static int bcm_sf2_sw_suspend(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int port;

	bcm_sf2_intr_disable(priv);

	/* Disable all ports physically present including the IMP
	 * port, the other ones have already been disabled during
	 * bcm_sf2_sw_setup
	 */
	for (port = 0; port < DSA_MAX_PORTS; port++) {
		if ((1 << port) & ds->enabled_port_mask ||
		    dsa_is_cpu_port(ds, port))
			bcm_sf2_port_disable(ds, port, NULL);
	}

	return 0;
}

static int bcm_sf2_sw_resume(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	int ret;

	ret = bcm_sf2_sw_rst(priv);
	if (ret) {
		pr_err("%s: failed to software reset switch\n", __func__);
		return ret;
	}

	if (priv->hw_params.num_gphy == 1)
		bcm_sf2_gphy_enable_set(ds, true);

	ds->ops->setup(ds);

	return 0;
}

static void bcm_sf2_sw_get_wol(struct dsa_switch *ds, int port,
			       struct ethtool_wolinfo *wol)
{
	struct net_device *p = ds->dst->cpu_dp->netdev;
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct ethtool_wolinfo pwol;

	/* Get the parent device WoL settings */
	p->ethtool_ops->get_wol(p, &pwol);

	/* Advertise the parent device supported settings */
	wol->supported = pwol.supported;
	memset(&wol->sopass, 0, sizeof(wol->sopass));

	if (pwol.wolopts & WAKE_MAGICSECURE)
		memcpy(&wol->sopass, pwol.sopass, sizeof(wol->sopass));

	if (priv->wol_ports_mask & (1 << port))
		wol->wolopts = pwol.wolopts;
	else
		wol->wolopts = 0;
}

static int bcm_sf2_sw_set_wol(struct dsa_switch *ds, int port,
			      struct ethtool_wolinfo *wol)
{
	struct net_device *p = ds->dst->cpu_dp->netdev;
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	s8 cpu_port = ds->dst->cpu_dp->index;
	struct ethtool_wolinfo pwol;

	p->ethtool_ops->get_wol(p, &pwol);
	if (wol->wolopts & ~pwol.supported)
		return -EINVAL;

	if (wol->wolopts)
		priv->wol_ports_mask |= (1 << port);
	else
		priv->wol_ports_mask &= ~(1 << port);

	/* If we have at least one port enabled, make sure the CPU port
	 * is also enabled. If the CPU port is the last one enabled, we disable
	 * it since this configuration does not make sense.
	 */
	if (priv->wol_ports_mask && priv->wol_ports_mask != (1 << cpu_port))
		priv->wol_ports_mask |= (1 << cpu_port);
	else
		priv->wol_ports_mask &= ~(1 << cpu_port);

	return p->ethtool_ops->set_wol(p, wol);
}

static int bcm_sf2_vlan_op_wait(struct bcm_sf2_priv *priv)
{
	unsigned int timeout = 10;
	u32 reg;

	do {
		reg = core_readl(priv, CORE_ARLA_VTBL_RWCTRL);
		if (!(reg & ARLA_VTBL_STDN))
			return 0;

		usleep_range(1000, 2000);
	} while (timeout--);

	return -ETIMEDOUT;
}

static int bcm_sf2_vlan_op(struct bcm_sf2_priv *priv, u8 op)
{
	core_writel(priv, ARLA_VTBL_STDN | op, CORE_ARLA_VTBL_RWCTRL);

	return bcm_sf2_vlan_op_wait(priv);
}

static void bcm_sf2_sw_configure_vlan(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int port;

	/* Clear all VLANs */
	bcm_sf2_vlan_op(priv, ARLA_VTBL_CMD_CLEAR);

	for (port = 0; port < priv->hw_params.num_ports; port++) {
		if (!((1 << port) & ds->enabled_port_mask))
			continue;

		core_writel(priv, 1, CORE_DEFAULT_1Q_TAG_P(port));
	}
}

static int bcm_sf2_sw_setup(struct dsa_switch *ds)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	unsigned int port;

	/* Enable all valid ports and disable those unused */
	for (port = 0; port < priv->hw_params.num_ports; port++) {
		/* IMP port receives special treatment */
		if ((1 << port) & ds->enabled_port_mask)
			bcm_sf2_port_setup(ds, port, NULL);
		else if (dsa_is_cpu_port(ds, port))
			bcm_sf2_imp_setup(ds, port);
		else
			bcm_sf2_port_disable(ds, port, NULL);
	}

	bcm_sf2_sw_configure_vlan(ds);

	return 0;
}

/* The SWITCH_CORE register space is managed by b53 but operates on a page +
 * register basis so we need to translate that into an address that the
 * bus-glue understands.
 */
#define SF2_PAGE_REG_MKADDR(page, reg)	((page) << 10 | (reg) << 2)

static int bcm_sf2_core_read8(struct b53_device *dev, u8 page, u8 reg,
			      u8 *val)
{
	struct bcm_sf2_priv *priv = dev->priv;

	*val = core_readl(priv, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_read16(struct b53_device *dev, u8 page, u8 reg,
			       u16 *val)
{
	struct bcm_sf2_priv *priv = dev->priv;

	*val = core_readl(priv, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_read32(struct b53_device *dev, u8 page, u8 reg,
			       u32 *val)
{
	struct bcm_sf2_priv *priv = dev->priv;

	*val = core_readl(priv, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_read64(struct b53_device *dev, u8 page, u8 reg,
			       u64 *val)
{
	struct bcm_sf2_priv *priv = dev->priv;

	*val = core_readq(priv, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_write8(struct b53_device *dev, u8 page, u8 reg,
			       u8 value)
{
	struct bcm_sf2_priv *priv = dev->priv;

	core_writel(priv, value, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_write16(struct b53_device *dev, u8 page, u8 reg,
				u16 value)
{
	struct bcm_sf2_priv *priv = dev->priv;

	core_writel(priv, value, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_write32(struct b53_device *dev, u8 page, u8 reg,
				u32 value)
{
	struct bcm_sf2_priv *priv = dev->priv;

	core_writel(priv, value, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static int bcm_sf2_core_write64(struct b53_device *dev, u8 page, u8 reg,
				u64 value)
{
	struct bcm_sf2_priv *priv = dev->priv;

	core_writeq(priv, value, SF2_PAGE_REG_MKADDR(page, reg));

	return 0;
}

static const struct b53_io_ops bcm_sf2_io_ops = {
	.read8	= bcm_sf2_core_read8,
	.read16	= bcm_sf2_core_read16,
	.read32	= bcm_sf2_core_read32,
	.read48	= bcm_sf2_core_read64,
	.read64	= bcm_sf2_core_read64,
	.write8	= bcm_sf2_core_write8,
	.write16 = bcm_sf2_core_write16,
	.write32 = bcm_sf2_core_write32,
	.write48 = bcm_sf2_core_write64,
	.write64 = bcm_sf2_core_write64,
};

static const struct dsa_switch_ops bcm_sf2_ops = {
	.get_tag_protocol	= bcm_sf2_sw_get_tag_protocol,
	.setup			= bcm_sf2_sw_setup,
	.get_strings		= b53_get_strings,
	.get_ethtool_stats	= b53_get_ethtool_stats,
	.get_sset_count		= b53_get_sset_count,
	.get_phy_flags		= bcm_sf2_sw_get_phy_flags,
	.adjust_link		= bcm_sf2_sw_adjust_link,
	.fixed_link_update	= bcm_sf2_sw_fixed_link_update,
	.suspend		= bcm_sf2_sw_suspend,
	.resume			= bcm_sf2_sw_resume,
	.get_wol		= bcm_sf2_sw_get_wol,
	.set_wol		= bcm_sf2_sw_set_wol,
	.port_enable		= bcm_sf2_port_setup,
	.port_disable		= bcm_sf2_port_disable,
	.get_mac_eee		= bcm_sf2_sw_get_mac_eee,
	.set_mac_eee		= bcm_sf2_sw_set_mac_eee,
	.port_bridge_join	= b53_br_join,
	.port_bridge_leave	= b53_br_leave,
	.port_stp_state_set	= b53_br_set_stp_state,
	.port_fast_age		= b53_br_fast_age,
	.port_vlan_filtering	= b53_vlan_filtering,
	.port_vlan_prepare	= b53_vlan_prepare,
	.port_vlan_add		= b53_vlan_add,
	.port_vlan_del		= b53_vlan_del,
	.port_fdb_dump		= b53_fdb_dump,
	.port_fdb_add		= b53_fdb_add,
	.port_fdb_del		= b53_fdb_del,
	.get_rxnfc		= bcm_sf2_get_rxnfc,
	.set_rxnfc		= bcm_sf2_set_rxnfc,
	.port_mirror_add	= b53_mirror_add,
	.port_mirror_del	= b53_mirror_del,
};

struct bcm_sf2_of_data {
	u32 type;
	const u16 *reg_offsets;
	unsigned int core_reg_align;
	unsigned int num_cfp_rules;
};

/* Register offsets for the SWITCH_REG_* block */
static const u16 bcm_sf2_7445_reg_offsets[] = {
	[REG_SWITCH_CNTRL]	= 0x00,
	[REG_SWITCH_STATUS]	= 0x04,
	[REG_DIR_DATA_WRITE]	= 0x08,
	[REG_DIR_DATA_READ]	= 0x0C,
	[REG_SWITCH_REVISION]	= 0x18,
	[REG_PHY_REVISION]	= 0x1C,
	[REG_SPHY_CNTRL]	= 0x2C,
	[REG_RGMII_0_CNTRL]	= 0x34,
	[REG_RGMII_1_CNTRL]	= 0x40,
	[REG_RGMII_2_CNTRL]	= 0x4c,
	[REG_LED_0_CNTRL]	= 0x90,
	[REG_LED_1_CNTRL]	= 0x94,
	[REG_LED_2_CNTRL]	= 0x98,
};

static const struct bcm_sf2_of_data bcm_sf2_7445_data = {
	.type		= BCM7445_DEVICE_ID,
	.core_reg_align	= 0,
	.reg_offsets	= bcm_sf2_7445_reg_offsets,
	.num_cfp_rules	= 256,
};

static const u16 bcm_sf2_7278_reg_offsets[] = {
	[REG_SWITCH_CNTRL]	= 0x00,
	[REG_SWITCH_STATUS]	= 0x04,
	[REG_DIR_DATA_WRITE]	= 0x08,
	[REG_DIR_DATA_READ]	= 0x0c,
	[REG_SWITCH_REVISION]	= 0x10,
	[REG_PHY_REVISION]	= 0x14,
	[REG_SPHY_CNTRL]	= 0x24,
	[REG_RGMII_0_CNTRL]	= 0xe0,
	[REG_RGMII_1_CNTRL]	= 0xec,
	[REG_RGMII_2_CNTRL]	= 0xf8,
	[REG_LED_0_CNTRL]	= 0x40,
	[REG_LED_1_CNTRL]	= 0x4c,
	[REG_LED_2_CNTRL]	= 0x58,
};

static const struct bcm_sf2_of_data bcm_sf2_7278_data = {
	.type		= BCM7278_DEVICE_ID,
	.core_reg_align	= 1,
	.reg_offsets	= bcm_sf2_7278_reg_offsets,
	.num_cfp_rules	= 128,
};

static const struct of_device_id bcm_sf2_of_match[] = {
	{ .compatible = "brcm,bcm7445-switch-v4.0",
	  .data = &bcm_sf2_7445_data
	},
	{ .compatible = "brcm,bcm7278-switch-v4.0",
	  .data = &bcm_sf2_7278_data
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, bcm_sf2_of_match);

static int bcm_sf2_sw_probe(struct platform_device *pdev)
{
	const char *reg_names[BCM_SF2_REGS_NUM] = BCM_SF2_REGS_NAME;
	struct device_node *dn = pdev->dev.of_node;
	const struct of_device_id *of_id = NULL;
	const struct bcm_sf2_of_data *data;
	struct b53_platform_data *pdata;
	struct dsa_switch_ops *ops;
	struct device_node *ports;
	struct bcm_sf2_priv *priv;
	struct b53_device *dev;
	struct dsa_switch *ds;
	void __iomem **base;
	struct resource *r;
	unsigned int i;
	u32 reg, rev;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ops = devm_kzalloc(&pdev->dev, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	dev = b53_switch_alloc(&pdev->dev, &bcm_sf2_io_ops, priv);
	if (!dev)
		return -ENOMEM;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	of_id = of_match_node(bcm_sf2_of_match, dn);
	if (!of_id || !of_id->data)
		return -EINVAL;

	data = of_id->data;

	/* Set SWITCH_REG register offsets and SWITCH_CORE align factor */
	priv->type = data->type;
	priv->reg_offsets = data->reg_offsets;
	priv->core_reg_align = data->core_reg_align;
	priv->num_cfp_rules = data->num_cfp_rules;

	/* Auto-detection using standard registers will not work, so
	 * provide an indication of what kind of device we are for
	 * b53_common to work with
	 */
	pdata->chip_id = priv->type;
	dev->pdata = pdata;

	priv->dev = dev;
	ds = dev->ds;
	ds->ops = &bcm_sf2_ops;

	/* Advertise the 8 egress queues */
	ds->num_tx_queues = SF2_NUM_EGRESS_QUEUES;

	dev_set_drvdata(&pdev->dev, priv);

	spin_lock_init(&priv->indir_lock);
	mutex_init(&priv->stats_mutex);
	mutex_init(&priv->cfp.lock);

	/* CFP rule #0 cannot be used for specific classifications, flag it as
	 * permanently used
	 */
	set_bit(0, priv->cfp.used);

	/* Balance of_node_put() done by of_find_node_by_name() */
	of_node_get(dn);
	ports = of_find_node_by_name(dn, "ports");
	if (ports) {
		bcm_sf2_identify_ports(priv, ports);
		of_node_put(ports);
	}

	priv->irq0 = irq_of_parse_and_map(dn, 0);
	priv->irq1 = irq_of_parse_and_map(dn, 1);

	base = &priv->core;
	for (i = 0; i < BCM_SF2_REGS_NUM; i++) {
		r = platform_get_resource(pdev, IORESOURCE_MEM, i);
		*base = devm_ioremap_resource(&pdev->dev, r);
		if (IS_ERR(*base)) {
			pr_err("unable to find register: %s\n", reg_names[i]);
			return PTR_ERR(*base);
		}
		base++;
	}

	ret = bcm_sf2_sw_rst(priv);
	if (ret) {
		pr_err("unable to software reset switch: %d\n", ret);
		return ret;
	}

	bcm_sf2_gphy_enable_set(priv->dev->ds, true);

	ret = bcm_sf2_mdio_register(ds);
	if (ret) {
		pr_err("failed to register MDIO bus\n");
		return ret;
	}

	bcm_sf2_gphy_enable_set(priv->dev->ds, false);

	ret = bcm_sf2_cfp_rst(priv);
	if (ret) {
		pr_err("failed to reset CFP\n");
		goto out_mdio;
	}

	/* Disable all interrupts and request them */
	bcm_sf2_intr_disable(priv);

	ret = devm_request_irq(&pdev->dev, priv->irq0, bcm_sf2_switch_0_isr, 0,
			       "switch_0", priv);
	if (ret < 0) {
		pr_err("failed to request switch_0 IRQ\n");
		goto out_mdio;
	}

	ret = devm_request_irq(&pdev->dev, priv->irq1, bcm_sf2_switch_1_isr, 0,
			       "switch_1", priv);
	if (ret < 0) {
		pr_err("failed to request switch_1 IRQ\n");
		goto out_mdio;
	}

	/* Reset the MIB counters */
	reg = core_readl(priv, CORE_GMNCFGCFG);
	reg |= RST_MIB_CNT;
	core_writel(priv, reg, CORE_GMNCFGCFG);
	reg &= ~RST_MIB_CNT;
	core_writel(priv, reg, CORE_GMNCFGCFG);

	/* Get the maximum number of ports for this switch */
	priv->hw_params.num_ports = core_readl(priv, CORE_IMP0_PRT_ID) + 1;
	if (priv->hw_params.num_ports > DSA_MAX_PORTS)
		priv->hw_params.num_ports = DSA_MAX_PORTS;

	/* Assume a single GPHY setup if we can't read that property */
	if (of_property_read_u32(dn, "brcm,num-gphy",
				 &priv->hw_params.num_gphy))
		priv->hw_params.num_gphy = 1;

	rev = reg_readl(priv, REG_SWITCH_REVISION);
	priv->hw_params.top_rev = (rev >> SWITCH_TOP_REV_SHIFT) &
					SWITCH_TOP_REV_MASK;
	priv->hw_params.core_rev = (rev & SF2_REV_MASK);

	rev = reg_readl(priv, REG_PHY_REVISION);
	priv->hw_params.gphy_rev = rev & PHY_REVISION_MASK;

	ret = b53_switch_register(dev);
	if (ret)
		goto out_mdio;

	pr_debug("Starfighter 2 top: %x.%02x, core: %x.%02x base: 0x%p, IRQs: %d, %d\n",
		priv->hw_params.top_rev >> 8, priv->hw_params.top_rev & 0xff,
		priv->hw_params.core_rev >> 8, priv->hw_params.core_rev & 0xff,
		priv->core, priv->irq0, priv->irq1);

	return 0;

out_mdio:
	bcm_sf2_mdio_unregister(priv);
	return ret;
}

static int bcm_sf2_sw_remove(struct platform_device *pdev)
{
	struct bcm_sf2_priv *priv = platform_get_drvdata(pdev);

	priv->wol_ports_mask = 0;
	dsa_unregister_switch(priv->dev->ds);
	/* Disable all ports and interrupts */
	bcm_sf2_sw_suspend(priv->dev->ds);
	bcm_sf2_mdio_unregister(priv);

	return 0;
}

static void bcm_sf2_sw_shutdown(struct platform_device *pdev)
{
	struct bcm_sf2_priv *priv = platform_get_drvdata(pdev);

	/* For a kernel about to be kexec'd we want to keep the GPHY on for a
	 * successful MDIO bus scan to occur. If we did turn off the GPHY
	 * before (e.g: port_disable), this will also power it back on.
	 *
	 * Do not rely on kexec_in_progress, just power the PHY on.
	 */
	if (priv->hw_params.num_gphy == 1)
		bcm_sf2_gphy_enable_set(priv->dev->ds, true);
}

#ifdef CONFIG_PM_SLEEP
static int bcm_sf2_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bcm_sf2_priv *priv = platform_get_drvdata(pdev);

	return dsa_switch_suspend(priv->dev->ds);
}

static int bcm_sf2_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bcm_sf2_priv *priv = platform_get_drvdata(pdev);

	return dsa_switch_resume(priv->dev->ds);
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(bcm_sf2_pm_ops,
			 bcm_sf2_suspend, bcm_sf2_resume);


static struct platform_driver bcm_sf2_driver = {
	.probe	= bcm_sf2_sw_probe,
	.remove	= bcm_sf2_sw_remove,
	.shutdown = bcm_sf2_sw_shutdown,
	.driver = {
		.name = "brcm-sf2",
		.of_match_table = bcm_sf2_of_match,
		.pm = &bcm_sf2_pm_ops,
	},
};
module_platform_driver(bcm_sf2_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Driver for Broadcom Starfighter 2 ethernet switch chip");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:brcm-sf2");
