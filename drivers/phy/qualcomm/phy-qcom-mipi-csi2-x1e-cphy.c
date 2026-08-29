// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm X1E80100 private C-PHY receive profile.
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2025 Linaro Ltd.
 *
 * C-PHY operations adapted from the authored SP11 provider port. The
 * independently transcribed register observations remain in their original
 * table header. Generic D-PHY operations use the separate upstream source.
 */
#include <dt-bindings/phy/phy.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/time64.h>

#include "phy-qcom-mipi-csi2-cphy.h"
#include "phy-qcom-mipi-csi2-cphy-x1e80100.h"

#define X1E80100_CPHY_SYMBOL_RATE	2406000000UL
#define X1E80100_CPHY_CLOCK_RATE	480000000UL
#define X1E80100_CPHY_TIMER_RATE	400000000UL
#define X1E80100_CPHY_COMMON_OFFSET	0x1000
#define X1E80100_CPHY_CTRL(n)	(X1E80100_CPHY_COMMON_OFFSET + 4 * (n))
#define X1E80100_CPHY_STATUS(n)	(X1E80100_CPHY_COMMON_OFFSET + 0xb0 + 4 * (n))

struct x1e80100_cphy {
	struct device *dev;
	struct phy *phy;
	void __iomem *base;
	struct clk_bulk_data clocks[4];
	struct regulator_bulk_data supplies[2];
	int irq;
	bool configured;
};

const struct qcom_mipi_csi2_cphy_match x1e80100_cphy_match = {
	.private_cphy = true,
};

static const char * const x1e80100_cphy_clocks[] = {
	"camnoc_axi", "cpas_ahb", "csiphy", "csiphy_timer",
};

static const char * const x1e80100_cphy_supplies[] = {
	"vdda-0p8", "vdda-1p2",
};

static void x1e80100_cphy_write(struct x1e80100_cphy *cphy,
				const struct x1e80100_cphy_reg *table,
				unsigned int num_entries)
{
	unsigned int i;

	might_sleep();
	for (i = 0; i < num_entries; i++) {
		u32 delay_us;

		writel(table[i].reg_data, cphy->base + table[i].reg_addr);
		if (!table[i].delay_ns)
			continue;

		delay_us = max_t(u32, table[i].delay_ns / NSEC_PER_USEC, 1);
		if (delay_us <= 50)
			udelay(delay_us);
		else
			usleep_range(delay_us,
				     delay_us + max(delay_us / 100, 10U));
	}
}

static irqreturn_t x1e80100_cphy_isr(int irq, void *data)
{
	struct x1e80100_cphy *cphy = data;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(x1e80100_cphy_irq_clear); i++) {
		u32 delay_us;

		writel(x1e80100_cphy_irq_clear[i].reg_data,
		       cphy->base + x1e80100_cphy_irq_clear[i].reg_addr);
		if (!x1e80100_cphy_irq_clear[i].delay_ns)
			continue;

		delay_us = max_t(u32, x1e80100_cphy_irq_clear[i].delay_ns /
				 NSEC_PER_USEC, 1);
		udelay(delay_us);
	}

	return IRQ_HANDLED;
}

static int x1e80100_cphy_set_mode(struct phy *phy, enum phy_mode mode,
				   int submode)
{
	struct x1e80100_cphy *cphy = phy_get_drvdata(phy);

	if (mode != PHY_MODE_MIPI_CPHY)
		return -EOPNOTSUPP;
	if (submode)
		return -EINVAL;
	if (cphy->irq <= 0)
		return -EOPNOTSUPP;

	return 0;
}

static int x1e80100_cphy_configure(struct phy *phy,
				 union phy_configure_opts *opts)
{
	struct x1e80100_cphy *cphy = phy_get_drvdata(phy);

	if (phy_get_mode(phy) != PHY_MODE_MIPI_CPHY)
		return -EINVAL;
	if (opts->mipi_dphy.lanes != 1)
		return -EINVAL;
	if (opts->mipi_dphy.hs_clk_rate != X1E80100_CPHY_SYMBOL_RATE)
		return -EOPNOTSUPP;

	/* Only one validated profile exists; no D-PHY timing fields are used. */
	cphy->configured = true;
	return 0;
}

static int x1e80100_cphy_set_clock_rates(struct x1e80100_cphy *cphy)
{
	const unsigned long rates[] = {
		X1E80100_CPHY_CLOCK_RATE, X1E80100_CPHY_TIMER_RATE,
	};
	unsigned int i;
	int ret;

	/* camnoc_axi and cpas_ahb are enable-only, as in the original profile. */
	for (i = 0; i < ARRAY_SIZE(rates); i++) {
		struct clk *clk = cphy->clocks[i + 2].clk;
		long rate = clk_round_rate(clk, rates[i]);

		if (rate < 0)
			return rate;
		ret = clk_set_rate(clk, rate);
		if (ret)
			return ret;
	}

	return 0;
}

static void x1e80100_cphy_read_version(struct x1e80100_cphy *cphy)
{
	u32 version;

	writel(BIT(1), cphy->base + X1E80100_CPHY_CTRL(6));
	version = readl_relaxed(cphy->base + X1E80100_CPHY_STATUS(12));
	version |= readl_relaxed(cphy->base + X1E80100_CPHY_STATUS(13)) << 8;
	version |= readl_relaxed(cphy->base + X1E80100_CPHY_STATUS(14)) << 16;
	version |= readl_relaxed(cphy->base + X1E80100_CPHY_STATUS(15)) << 24;
	dev_dbg(cphy->dev, "CSIPHY 3PH HW Version = 0x%08x\n", version);
}

static int x1e80100_cphy_lanes_enable(struct x1e80100_cphy *cphy)
{
	if (!cphy->configured)
		return -EINVAL;

	BUILD_BUG_ON(ARRAY_SIZE(x1e80100_cphy_reset) != 2);
	BUILD_BUG_ON(ARRAY_SIZE(x1e80100_cphy_toggle) != 6);
	BUILD_BUG_ON(ARRAY_SIZE(x1e80100_cphy_common) != 9);
	BUILD_BUG_ON(ARRAY_SIZE(x1e80100_cphy_config) != 121);
	BUILD_BUG_ON(ARRAY_SIZE(x1e80100_cphy_irq_clear) != 24);
	BUILD_BUG_ON(ARRAY_SIZE(x1e80100_cphy_shutdown) != 3);

	x1e80100_cphy_write(cphy, x1e80100_cphy_reset,
			    ARRAY_SIZE(x1e80100_cphy_reset));
	x1e80100_cphy_write(cphy, x1e80100_cphy_toggle,
			    ARRAY_SIZE(x1e80100_cphy_toggle));
	x1e80100_cphy_write(cphy, x1e80100_cphy_common,
			    ARRAY_SIZE(x1e80100_cphy_common));
	x1e80100_cphy_write(cphy, x1e80100_cphy_config,
			    ARRAY_SIZE(x1e80100_cphy_config));
	return 0;
}

static int x1e80100_cphy_power_on(struct phy *phy)
{
	struct x1e80100_cphy *cphy = phy_get_drvdata(phy);
	int ret;

	if (phy_get_mode(phy) != PHY_MODE_MIPI_CPHY || !cphy->configured)
		return -EINVAL;

	ret = regulator_bulk_enable(ARRAY_SIZE(cphy->supplies), cphy->supplies);
	if (ret)
		return ret;
	ret = x1e80100_cphy_set_clock_rates(cphy);
	if (ret)
		goto disable_supplies;
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(cphy->clocks), cphy->clocks);
	if (ret)
		goto disable_supplies;

	x1e80100_cphy_read_version(cphy);
	enable_irq(cphy->irq);
	ret = x1e80100_cphy_lanes_enable(cphy);
	if (!ret)
		return 0;

	disable_irq(cphy->irq);
	clk_bulk_disable_unprepare(ARRAY_SIZE(cphy->clocks), cphy->clocks);
disable_supplies:
	regulator_bulk_disable(ARRAY_SIZE(cphy->supplies), cphy->supplies);
	return ret;
}

static int x1e80100_cphy_power_off(struct phy *phy)
{
	struct x1e80100_cphy *cphy = phy_get_drvdata(phy);

	x1e80100_cphy_write(cphy, x1e80100_cphy_shutdown,
			    ARRAY_SIZE(x1e80100_cphy_shutdown));
	disable_irq(cphy->irq);
	clk_bulk_disable_unprepare(ARRAY_SIZE(cphy->clocks), cphy->clocks);
	regulator_bulk_disable(ARRAY_SIZE(cphy->supplies), cphy->supplies);
	return 0;
}

static const struct phy_ops x1e80100_cphy_ops = {
	.set_mode = x1e80100_cphy_set_mode,
	.configure = x1e80100_cphy_configure,
	.power_on = x1e80100_cphy_power_on,
	.power_off = x1e80100_cphy_power_off,
	.owner = THIS_MODULE,
};

static struct phy *x1e80100_cphy_xlate(struct device *dev,
				    const struct of_phandle_args *args)
{
	struct x1e80100_cphy *cphy = dev_get_drvdata(dev);

	if (args->np != dev->of_node || args->args_count)
		return ERR_PTR(-EINVAL);
	return cphy->phy;
}

int phy_qcom_mipi_csi2_x1e_cphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_provider *provider;
	struct x1e80100_cphy *cphy;
	u32 cells, type;
	unsigned int i;
	int ret;

	if (!of_machine_is_compatible("microsoft,denali") ||
	    device_get_match_data(dev) != &x1e80100_cphy_match)
		return -ENODEV;
	if (of_property_count_u32_elems(np, "#phy-cells") != 1 ||
	    of_property_read_u32(np, "#phy-cells", &cells) || cells)
		return -EINVAL;
	if (of_property_count_u32_elems(np, "phy-type") != 1 ||
	    of_property_read_u32(np, "phy-type", &type) || type != PHY_TYPE_CPHY)
		return -EINVAL;
	if (of_find_property(np, "operating-points-v2", NULL) ||
	    of_find_property(np, "power-domain-names", NULL) ||
	    of_count_phandle_with_args(np, "power-domains", "#power-domain-cells") != 1)
		return -EINVAL;
	if (of_property_count_strings(np, "clock-names") !=
	    ARRAY_SIZE(x1e80100_cphy_clocks))
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(x1e80100_cphy_clocks); i++)
		if (of_property_match_string(np, "clock-names",
					     x1e80100_cphy_clocks[i]) != i)
			return -EINVAL;

	cphy = devm_kzalloc(dev, sizeof(*cphy), GFP_KERNEL);
	if (!cphy)
		return -ENOMEM;
	cphy->dev = dev;
	for (i = 0; i < ARRAY_SIZE(cphy->clocks); i++)
		cphy->clocks[i].id = x1e80100_cphy_clocks[i];
	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(cphy->clocks), cphy->clocks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get C-PHY clocks\n");
	for (i = 0; i < ARRAY_SIZE(cphy->supplies); i++)
		cphy->supplies[i].supply = x1e80100_cphy_supplies[i];
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(cphy->supplies), cphy->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get C-PHY supplies\n");
	cphy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cphy->base))
		return PTR_ERR(cphy->base);
	cphy->irq = platform_get_irq(pdev, 0);
	if (cphy->irq <= 0)
		return cphy->irq ?: -ENXIO;
	ret = devm_request_irq(dev, cphy->irq, x1e80100_cphy_isr,
			       IRQF_TRIGGER_RISING | IRQF_NO_AUTOEN,
			       dev_name(dev), cphy);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request C-PHY IRQ\n");
	cphy->phy = devm_phy_create(dev, NULL, &x1e80100_cphy_ops);
	if (IS_ERR(cphy->phy))
		return PTR_ERR(cphy->phy);
	phy_set_drvdata(cphy->phy, cphy);
	platform_set_drvdata(pdev, cphy);
	provider = devm_of_phy_provider_register(dev, x1e80100_cphy_xlate);
	return PTR_ERR_OR_ZERO(provider);
}
