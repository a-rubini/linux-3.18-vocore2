/*
 * Allwinner ralink USB phy driver
 *
 * Copyright (C) 2016 John Crispin <blogic@openwrt.org>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/of_platform.h>

#include <asm/mach-ralink/ralink_regs.h>

#define RT_SYSC_REG_SYSCFG1		0x014
#define RT_SYSC_REG_CLKCFG1		0x030
#define RT_SYSC_REG_USB_PHY_CFG		0x05c

#define OFS_U2_PHY_AC0                  0x800
#define OFS_U2_PHY_AC1                  0x804
#define OFS_U2_PHY_AC2                  0x808
#define OFS_U2_PHY_ACR0                 0x810
#define OFS_U2_PHY_ACR1                 0x814
#define OFS_U2_PHY_ACR2                 0x818
#define OFS_U2_PHY_ACR3                 0x81C
#define OFS_U2_PHY_ACR4                 0x820
#define OFS_U2_PHY_AMON0                0x824
#define OFS_U2_PHY_DCR0                 0x860
#define OFS_U2_PHY_DCR1                 0x864
#define OFS_U2_PHY_DTM0                 0x868
#define OFS_U2_PHY_DTM1                 0x86C

#define RT_RSTCTRL_UDEV			BIT(25)
#define RT_RSTCTRL_UHST			BIT(22)
#define RT_SYSCFG1_USB0_HOST_MODE	BIT(10)

#define MT7620_CLKCFG1_UPHY0_CLK_EN	BIT(25)
#define MT7620_CLKCFG1_UPHY1_CLK_EN	BIT(22)
#define RT_CLKCFG1_UPHY1_CLK_EN		BIT(20)
#define RT_CLKCFG1_UPHY0_CLK_EN		BIT(18)

#define USB_PHY_UTMI_8B60M		BIT(1)
#define UDEV_WAKEUP			BIT(0)

struct ralink_usb_phy {
	struct reset_control	*rstdev;
	struct reset_control	*rsthost;
	u32			clk;
	struct phy		*phy;
	void __iomem		*base;
};

static void u2_phy_w32(struct ralink_usb_phy *phy, u32 val, u32 reg)
{
	iowrite32(val, phy->base + reg);
}

static u32 u2_phy_r32(struct ralink_usb_phy *phy, u32 reg)
{
	return ioread32(phy->base + reg);
}

static void
u2_phy_init(struct ralink_usb_phy *phy)
{
	u2_phy_r32(phy, OFS_U2_PHY_AC2);
	u2_phy_r32(phy, OFS_U2_PHY_ACR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);

	u2_phy_w32(phy, 0x00ffff02, OFS_U2_PHY_DCR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);
	u2_phy_w32(phy, 0x00555502, OFS_U2_PHY_DCR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);
	u2_phy_w32(phy, 0x00aaaa02, OFS_U2_PHY_DCR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);
	u2_phy_w32(phy, 0x00000402, OFS_U2_PHY_DCR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);
	u2_phy_w32(phy, 0x0048086a, OFS_U2_PHY_AC0);
	u2_phy_w32(phy, 0x4400001c, OFS_U2_PHY_AC1);
	u2_phy_w32(phy, 0xc0200000, OFS_U2_PHY_ACR3);
	u2_phy_w32(phy, 0x02000000, OFS_U2_PHY_DTM0);
}

static int ralink_usb_phy_power_on(struct phy *_phy)
{
	struct ralink_usb_phy *phy = phy_get_drvdata(_phy);
	u32 t;

	/* enable the phy */
	rt_sysc_m32(0, phy->clk, RT_SYSC_REG_CLKCFG1);

	/* setup host mode */
	rt_sysc_m32(0, RT_SYSCFG1_USB0_HOST_MODE, RT_SYSC_REG_SYSCFG1);

	/* deassert the reset lines */
	reset_control_deassert(phy->rsthost);
	reset_control_deassert(phy->rstdev);

	/*
	 * The SDK kernel had a delay of 100ms. however on device
	 * testing showed that 10ms is enough
	 */
	mdelay(10);

	if (!IS_ERR(phy->base))
		u2_phy_init(phy);

	/* print some status info */
	t = rt_sysc_r32(RT_SYSC_REG_USB_PHY_CFG);
	dev_info(&phy->phy->dev, "remote usb device wakeup %s\n",
		(t & UDEV_WAKEUP) ? ("enabled") : ("disabled"));
	if (t & USB_PHY_UTMI_8B60M)
		dev_info(&phy->phy->dev, "UTMI 8bit 60MHz\n");
	else
		dev_info(&phy->phy->dev, "UTMI 16bit 30MHz\n");

	return 0;
}

static int ralink_usb_phy_power_off(struct phy *_phy)
{
	struct ralink_usb_phy *phy = phy_get_drvdata(_phy);

	/* assert the reset lines */
	reset_control_assert(phy->rstdev);
	reset_control_assert(phy->rsthost);

	/* disable the phy */
	rt_sysc_m32(phy->clk, 0, RT_SYSC_REG_CLKCFG1);

	return 0;
}

static struct phy_ops ralink_usb_phy_ops = {
	.power_on	= ralink_usb_phy_power_on,
	.power_off	= ralink_usb_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id ralink_usb_phy_of_match[] = {
	{
		.compatible = "ralink,rt3xxx-usbphy",
		.data = (void *) (RT_CLKCFG1_UPHY1_CLK_EN |
				  RT_CLKCFG1_UPHY0_CLK_EN)
	},
	{
		.compatible = "ralink,mt7620a-usbphy",
		.data = (void *) (MT7620_CLKCFG1_UPHY1_CLK_EN |
				  MT7620_CLKCFG1_UPHY0_CLK_EN) },
	{ },
};
MODULE_DEVICE_TABLE(of, ralink_usb_phy_of_match);

static int ralink_usb_phy_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	const struct of_device_id *match;
	struct ralink_usb_phy *phy;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	match = of_match_device(ralink_usb_phy_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	phy->clk = (int) match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy->base = devm_ioremap_resource(&pdev->dev, res);

	phy->rsthost = devm_reset_control_get(&pdev->dev, "host");
	if (IS_ERR(phy->rsthost)) {
		dev_err(dev, "host reset is missing\n");
		return PTR_ERR(phy->rsthost);
	}

	phy->rstdev = devm_reset_control_get(&pdev->dev, "device");
	if (IS_ERR(phy->rstdev)) {
		dev_err(dev, "device reset is missing\n");
		return PTR_ERR(phy->rstdev);
	}

	phy->phy = devm_phy_create(dev, NULL, &ralink_usb_phy_ops, NULL);
	if (IS_ERR(phy->phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy->phy);
	}
	phy_set_drvdata(phy->phy, phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver ralink_usb_phy_driver = {
	.probe	= ralink_usb_phy_probe,
	.driver = {
		.of_match_table	= ralink_usb_phy_of_match,
		.name  = "ralink-usb-phy",
	}
};
module_platform_driver(ralink_usb_phy_driver);

MODULE_DESCRIPTION("Ralink USB phy driver");
MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_LICENSE("GPL v2");
