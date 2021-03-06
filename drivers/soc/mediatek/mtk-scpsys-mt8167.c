/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>
#include <linux/soc/mediatek/infracfg.h>

#include <dt-bindings/power/mt8167-power.h>

#define SPM_VDE_PWR_CON			0x0210
#define SPM_MFG_PWR_CON			0x0214
#define SPM_VEN_PWR_CON			0x0230
#define SPM_ISP_PWR_CON			0x0238
#define SPM_DIS_PWR_CON			0x023c
#define SPM_CONN_PWR_CON		0x0280
#define SPM_VEN2_PWR_CON		0x0298
#define SPM_AUDIO_PWR_CON		0x029c	/* MT8173 */
#define SPM_BDP_PWR_CON			0x029c	/* MT2701 */
#define SPM_ETH_PWR_CON			0x02a0
#define SPM_HIF_PWR_CON			0x02a4
#define SPM_IFR_MSC_PWR_CON		0x02a8
#define SPM_MFG_2D_PWR_CON		0x02c0
#define SPM_MFG_ASYNC_PWR_CON		0x02c4
#define SPM_USB_PWR_CON			0x02cc

#define SPM_PWR_STATUS			0x060c
#define SPM_PWR_STATUS_2ND		0x0610

#define PWR_RST_B_BIT			BIT(0)
#define PWR_ISO_BIT			BIT(1)
#define PWR_ON_BIT			BIT(2)
#define PWR_ON_2ND_BIT			BIT(3)
#define PWR_CLK_DIS_BIT			BIT(4)

#define PWR_STATUS_CONN			BIT(1)
#define PWR_STATUS_DISP			BIT(3)
#define PWR_STATUS_MFG			BIT(4)
#define PWR_STATUS_ISP			BIT(5)
#define PWR_STATUS_VDEC			BIT(7)
#define PWR_STATUS_BDP			BIT(14)
#define PWR_STATUS_ETH			BIT(15)
#define PWR_STATUS_HIF			BIT(16)
#define PWR_STATUS_IFR_MSC		BIT(17)
#define PWR_STATUS_VENC_LT		BIT(20)
#define PWR_STATUS_VENC			BIT(21)
#define PWR_STATUS_MFG_2D		BIT(22)	/* MT8173 */
#define PWR_STATUS_MFG_ASYNC		BIT(23)	/* MT8173 */
#define PWR_STATUS_AUDIO		BIT(24)	/* MT8173 */
#define PWR_STATUS_USB			BIT(25)	/* MT8173 */
#define PWR_STATUS_MFG_2D_MT8167	BIT(24)	/* MT8167 */
#define PWR_STATUS_MFG_ASYNC_MT8167	BIT(25)	/* MT8167 */

enum clk_id {
	CLK_NONE,
	CLK_MM,
	CLK_MFG,
	CLK_AXI_MFG,
	CLK_VDEC,
	CLK_VENC,
	CLK_VENC_LT,
	CLK_ETHIF,
	CLK_MAX,
};

#define MAX_CLKS	2

struct scp_domain_data {
	const char *name;
	u32 sta_mask;
	int ctl_offs;
	u32 sram_pdn_bits;
	u32 sram_pdn_ack_bits;
	u32 bus_prot_mask;
	u32 axi_si1_way_en;
	enum clk_id clk_id[MAX_CLKS];
	bool active_wakeup;
};

struct scp;

struct scp_domain {
	struct generic_pm_domain genpd;
	struct scp *scp;
	struct clk *clk[MAX_CLKS];
	const struct scp_domain_data *data;
	struct regulator *supply;
};

struct scp {
	struct scp_domain *domains;
	struct genpd_onecell_data pd_data;
	struct device *dev;
	void __iomem *base;
	struct regmap *infracfg;
};

static int scpsys_domain_is_on(struct scp_domain *scpd)
{
	struct scp *scp = scpd->scp;

	u32 status = readl(scp->base + SPM_PWR_STATUS) & scpd->data->sta_mask;
	u32 status2 = readl(scp->base + SPM_PWR_STATUS_2ND) &
				scpd->data->sta_mask;

	/*
	 * A domain is on when both status bits are set. If only one is set
	 * return an error. This happens while powering up a domain
	 */

	if (status && status2)
		return true;
	if (!status && !status2)
		return false;

	return -EINVAL;
}

static int scpsys_power_on(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	unsigned long timeout;
	bool expired;
	void __iomem *ctl_addr = scp->base + scpd->data->ctl_offs;
	u32 sram_pdn_ack = scpd->data->sram_pdn_ack_bits;
	u32 val;
	int ret;
	int i;

	if (scpd->supply) {
		ret = regulator_enable(scpd->supply);
		if (ret)
			return ret;
	}

	for (i = 0; i < MAX_CLKS && scpd->clk[i]; i++) {
		ret = clk_prepare_enable(scpd->clk[i]);
		if (ret) {
			for (--i; i >= 0; i--)
				clk_disable_unprepare(scpd->clk[i]);

			dev_err(scp->dev, "%s: clk failed\n", genpd->name);
			goto err_clk;
		}
	}

	if (scpd->data->axi_si1_way_en)
		mtk_infracfg_set_axi_si1_way_en(scp->infracfg,
						scpd->data->axi_si1_way_en);

	val = readl(ctl_addr);
	val |= PWR_ON_BIT;
	writel(val, ctl_addr);
	val |= PWR_ON_2ND_BIT;
	writel(val, ctl_addr);

	/* wait until PWR_ACK = 1 */
	timeout = jiffies + HZ;
	expired = false;
	while (1) {
		ret = scpsys_domain_is_on(scpd);
		if (ret > 0)
			break;

		if (expired) {
			ret = -ETIMEDOUT;
			dev_err(scp->dev, "%s: PWR_ACK timeout\n", genpd->name);
			goto err_pwr_ack;
		}

		cpu_relax();

		if (time_after(jiffies, timeout))
			expired = true;
	}

	val &= ~PWR_CLK_DIS_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ISO_BIT;
	writel(val, ctl_addr);

	val |= PWR_RST_B_BIT;
	writel(val, ctl_addr);

	val &= ~scpd->data->sram_pdn_bits;
	writel(val, ctl_addr);

	/* wait until SRAM_PDN_ACK all 0 */
	timeout = jiffies + HZ;
	expired = false;
	while (sram_pdn_ack && (readl(ctl_addr) & sram_pdn_ack)) {

		if (expired) {
			ret = -ETIMEDOUT;
			dev_err(scp->dev, "%s: SRAM_PDN_ACK timeout\n",
					genpd->name);
			goto err_pwr_ack;
		}

		cpu_relax();

		if (time_after(jiffies, timeout))
			expired = true;
	}

	if (scpd->data->bus_prot_mask) {
		ret = mtk_infracfg_clear_bus_protection(scp->infracfg,
				scpd->data->bus_prot_mask);
		if (ret) {
			dev_err(scp->dev, "%s: bus protection failed\n",
					genpd->name);
			goto err_pwr_ack;
		}
	}

	return 0;

err_pwr_ack:
	if (scpd->data->axi_si1_way_en)
		mtk_infracfg_clear_axi_si1_way_en(scp->infracfg,
						scpd->data->axi_si1_way_en);

	for (i = MAX_CLKS - 1; i >= 0; i--) {
		if (scpd->clk[i])
			clk_disable_unprepare(scpd->clk[i]);
	}
err_clk:
	if (scpd->supply)
		regulator_disable(scpd->supply);

	dev_err(scp->dev, "Failed to power on domain %s\n", genpd->name);

	return ret;
}

static int scpsys_power_off(struct generic_pm_domain *genpd)
{
	struct scp_domain *scpd = container_of(genpd, struct scp_domain, genpd);
	struct scp *scp = scpd->scp;
	unsigned long timeout;
	bool expired;
	void __iomem *ctl_addr = scp->base + scpd->data->ctl_offs;
	u32 pdn_ack = scpd->data->sram_pdn_ack_bits;
	u32 val;
	int ret;
	int i;

	if (scpd->data->bus_prot_mask) {
		ret = mtk_infracfg_set_bus_protection(scp->infracfg,
				scpd->data->bus_prot_mask);
		if (ret) {
			dev_err(scp->dev, "%s: bus protection failed\n",
					genpd->name);
			goto out;
		}
	}

	val = readl(ctl_addr);
	val |= scpd->data->sram_pdn_bits;
	writel(val, ctl_addr);

	/* wait until SRAM_PDN_ACK all 1 */
	timeout = jiffies + HZ;
	expired = false;
	while (pdn_ack && (readl(ctl_addr) & pdn_ack) != pdn_ack) {
		if (expired) {
			ret = -ETIMEDOUT;
			dev_err(scp->dev, "%s: SRAM_PDN_ACK timeout\n",
					genpd->name);
			goto out;
		}

		cpu_relax();

		if (time_after(jiffies, timeout))
			expired = true;
	}

	val |= PWR_ISO_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_RST_B_BIT;
	writel(val, ctl_addr);

	val |= PWR_CLK_DIS_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ON_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ON_2ND_BIT;
	writel(val, ctl_addr);

	/* wait until PWR_ACK = 0 */
	timeout = jiffies + HZ;
	expired = false;
	while (1) {
		ret = scpsys_domain_is_on(scpd);
		if (ret == 0)
			break;

		if (expired) {
			ret = -ETIMEDOUT;
			dev_err(scp->dev, "%s: PWR_ACK timeout\n", genpd->name);
			goto out;
		}

		cpu_relax();

		if (time_after(jiffies, timeout))
			expired = true;
	}

	if (scpd->data->axi_si1_way_en)
		mtk_infracfg_clear_axi_si1_way_en(scp->infracfg,
						scpd->data->axi_si1_way_en);

	for (i = 0; i < MAX_CLKS && scpd->clk[i]; i++)
		clk_disable_unprepare(scpd->clk[i]);

	if (scpd->supply)
		regulator_disable(scpd->supply);

	return 0;

out:
	dev_err(scp->dev, "Failed to power off domain %s\n", genpd->name);

	return ret;
}

static bool scpsys_active_wakeup(struct device *dev)
{
	struct generic_pm_domain *genpd;
	struct scp_domain *scpd;

	genpd = pd_to_genpd(dev->pm_domain);
	scpd = container_of(genpd, struct scp_domain, genpd);

	return scpd->data->active_wakeup;
}

static void init_clks(struct platform_device *pdev, struct clk *clk[CLK_MAX])
{
	enum clk_id clk_ids[] = {
		CLK_MM,
		CLK_MFG,
		CLK_AXI_MFG,
		CLK_VDEC,
		CLK_VENC,
		CLK_VENC_LT,
		CLK_ETHIF
	};

	static const char * const clk_names[] = {
		"mm",
		"mfg",
		"axi_mfg",
		"vdec",
		"venc",
		"venc_lt",
		"ethif",
	};

	int i;

	for (i = 0; i < ARRAY_SIZE(clk_ids); i++)
		clk[clk_ids[i]] = devm_clk_get(&pdev->dev, clk_names[i]);
}

static struct scp *init_scp(struct platform_device *pdev,
			const struct scp_domain_data *scp_domain_data, int num)
{
	struct genpd_onecell_data *pd_data;
	struct resource *res;
	int i, j;
	struct scp *scp;
	struct clk *clk[CLK_MAX];

	scp = devm_kzalloc(&pdev->dev, sizeof(*scp), GFP_KERNEL);
	if (!scp)
		return ERR_PTR(-ENOMEM);

	scp->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	scp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(scp->base))
		return ERR_CAST(scp->base);

	scp->infracfg = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
			"infracfg");
	if (IS_ERR(scp->infracfg)) {
		dev_err(&pdev->dev, "Cannot find infracfg controller: %ld\n",
				PTR_ERR(scp->infracfg));
		return ERR_CAST(scp->infracfg);
	}

	scp->domains = devm_kzalloc(&pdev->dev,
				sizeof(*scp->domains) * num, GFP_KERNEL);
	if (!scp->domains)
		return ERR_PTR(-ENOMEM);

	pd_data = &scp->pd_data;

	pd_data->domains = devm_kzalloc(&pdev->dev,
			sizeof(*pd_data->domains) * num, GFP_KERNEL);
	if (!pd_data->domains)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < num; i++) {
		struct scp_domain *scpd = &scp->domains[i];
		const struct scp_domain_data *data = &scp_domain_data[i];

		scpd->supply = devm_regulator_get_optional(&pdev->dev, data->name);
		if (IS_ERR(scpd->supply)) {
			if (PTR_ERR(scpd->supply) == -ENODEV)
				scpd->supply = NULL;
			else
				return ERR_CAST(scpd->supply);
		}
	}

	pd_data->num_domains = num;

	init_clks(pdev, clk);

	for (i = 0; i < num; i++) {
		struct scp_domain *scpd = &scp->domains[i];
		struct generic_pm_domain *genpd = &scpd->genpd;
		const struct scp_domain_data *data = &scp_domain_data[i];

		pd_data->domains[i] = genpd;
		scpd->scp = scp;

		scpd->data = data;

		for (j = 0; j < MAX_CLKS && data->clk_id[j]; j++) {
			struct clk *c = clk[data->clk_id[j]];

			if (IS_ERR(c)) {
				dev_err(&pdev->dev, "%s: clk unavailable\n",
					data->name);
				return ERR_CAST(c);
			}

			scpd->clk[j] = c;
		}

		genpd->name = data->name;
		genpd->power_off = scpsys_power_off;
		genpd->power_on = scpsys_power_on;
		genpd->dev_ops.active_wakeup = scpsys_active_wakeup;
	}

	return scp;
}

static void mtk_register_power_domains(struct platform_device *pdev,
				struct scp *scp, int num)
{
	struct genpd_onecell_data *pd_data;
	int i, ret;

	for (i = 0; i < num; i++) {
		struct scp_domain *scpd = &scp->domains[i];
		struct generic_pm_domain *genpd = &scpd->genpd;

		/*
		 * Initially turn on all domains to make the domains usable
		 * with !CONFIG_PM and to get the hardware in sync with the
		 * software.  The unused domains will be switched off during
		 * late_init time.
		 */
		genpd->power_on(genpd);

		pm_genpd_init(genpd, NULL, false);
	}

	/*
	 * We are not allowed to fail here since there is no way to unregister
	 * a power domain. Once registered above we have to keep the domains
	 * valid.
	 */

	pd_data = &scp->pd_data;

	ret = of_genpd_add_provider_onecell(pdev->dev.of_node, pd_data);
	if (ret)
		dev_err(&pdev->dev, "Failed to add OF provider: %d\n", ret);
}

/*
 * MT8167 power domain support
 */

static const struct scp_domain_data scp_domain_data_mt8167[] = {
	[MT8167_POWER_DOMAIN_DISP] = {
		.name = "disp",
		.sta_mask = PWR_STATUS_DISP,
		.ctl_offs = SPM_DIS_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.bus_prot_mask = BIT(1) | BIT(11),
		.clk_id = {CLK_MM},
		.active_wakeup = true,
	},
	[MT8167_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = PWR_STATUS_VDEC,
		.ctl_offs = SPM_VDE_PWR_CON,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.clk_id = {CLK_MM, CLK_VDEC},
		.active_wakeup = true,
	},
	[MT8167_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = PWR_STATUS_ISP,
		.ctl_offs = SPM_ISP_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.clk_id = {CLK_MM},
		.active_wakeup = true,
	},
	[MT8167_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = PWR_STATUS_CONN,
		.ctl_offs = SPM_CONN_PWR_CON,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = 0,
		.bus_prot_mask = BIT(4) | BIT(8) | BIT(9),
		.clk_id = {CLK_NONE},
		.active_wakeup = true,
	},
};

#define NUM_DOMAINS_MT8167	ARRAY_SIZE(scp_domain_data_mt8167)

static int __init scpsys_probe_mt8167(struct platform_device *pdev)
{
	struct scp *scp;

	scp = init_scp(pdev, scp_domain_data_mt8167, NUM_DOMAINS_MT8167);
	if (IS_ERR(scp))
		return PTR_ERR(scp);

	mtk_register_power_domains(pdev, scp, NUM_DOMAINS_MT8167);

	return 0;
}

/*
 * scpsys driver init
 */

static const struct of_device_id of_scpsys_match_tbl[] = {
	{
		.compatible = "mediatek,mt8167-scpsys",
		.data = scpsys_probe_mt8167,
	}, {
		/* sentinel */
	}
};

static int scpsys_probe(struct platform_device *pdev)
{
	int (*probe)(struct platform_device *);
	const struct of_device_id *of_id;

	of_id = of_match_node(of_scpsys_match_tbl, pdev->dev.of_node);
	if (!of_id || !of_id->data)
		return -EINVAL;

	probe = of_id->data;

	return probe(pdev);
}

static struct platform_driver scpsys_drv = {
	.probe = scpsys_probe,
	.driver = {
		.name = "mtk-scpsys",
		.suppress_bind_attrs = true,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(of_scpsys_match_tbl),
	},
};

static int __init scpsys_drv_init(void)
{
	return platform_driver_register(&scpsys_drv);
}

/*
 * There are some Mediatek drivers which depend on the power domain driver need
 * to probe in earlier initcall levels. So scpsys driver also need to probe
 * earlier.
 *
 * IOMMU(M4U) and SMI drivers for example. SMI is a bridge between IOMMU and
 * multimedia HW. IOMMU depends on SMI, and SMI is a power domain consumer,
 * so the proper probe sequence should be scpsys -> SMI -> IOMMU driver.
 * IOMMU drivers are initialized during subsys_init by default, so we need to
 * move SMI and scpsys drivers to subsys_init or earlier init levels.
 */
subsys_initcall(scpsys_drv_init);
