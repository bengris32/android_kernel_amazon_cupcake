/*
 * life_cycle_reasons_mtk.c
 *
 * Copyright 2011-2018 Amazon.com, Inc. or its Affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/life_cycle_reasons.h>
#include <linux/watchdog.h>
#include <mt-plat/mtk_boot_common.h>
#include "mtk_wdt.h"

static enum life_cycle_reason lc_boot = LIFE_CYCLE_NOT_AVAILABLE;
static enum life_cycle_reason lc_shutdown = LIFE_CYCLE_NOT_AVAILABLE;

static int mtk_read_boot_reason(enum life_cycle_reason *reason)
{
	*reason = lc_boot;
	return 0;
}

static int mtk_write_boot_reason(enum life_cycle_reason reason)
{
	return -EOPNOTSUPP;
}

static int mtk_write_shutdown_reason(enum life_cycle_reason shutdown_reason)
{
	return -EOPNOTSUPP;
}

static int mtk_write_thermal_shutdown_reason(enum life_cycle_reason reason)
{
	u32 wdt_shutdown_reason = 0;
	int ret;

	switch (reason) {
	case THERMAL_SHUTDOWN_REASON_SOC:
		wdt_shutdown_reason =
				THERMAL_SHUTDOWN_REASON_SOC_BITS_VALUE;
		break;
	case THERMAL_SHUTDOWN_REASON_SVS:
		wdt_shutdown_reason =
				THERMAL_SHUTDOWN_REASON_SVS_BITS_VALUE;
		break;
	case THERMAL_SHUTDOWN_REASON_CVS:
		wdt_shutdown_reason =
				THERMAL_SHUTDOWN_REASON_CVS_BITS_VALUE;
		break;
	case THERMAL_SHUTDOWN_REASON_TVS:
		wdt_shutdown_reason =
				THERMAL_SHUTDOWN_REASON_TVS_BITS_VALUE;
		break;
	case THERMAL_SHUTDOWN_REASON_BVS:
		wdt_shutdown_reason =
				THERMAL_SHUTDOWN_REASON_BVS_BITS_VALUE;
		break;
	default:
		pr_err("%s: unsupported thermal life cycle reason: 0x%x\n",
							__func__, reason);
		return -EOPNOTSUPP;
	}

	ret = mtk_setbits_nonrst_reg2(THERMAL_SHUTDOWN_REASON_BIT_MASK,
							wdt_shutdown_reason);
	if (ret < 0) {
		pr_err("%s: error(%d) writing nonrst reg2 from wdt\n",
							__func__, ret);
		return ret;
	}

	return 0;
}

static int mtk_read_shutdown_reason(enum life_cycle_reason *shutdown_reason)
{
	*shutdown_reason = lc_shutdown;
	return 0;
}

static int mtk_read_thermal_shutdown_reason(enum life_cycle_reason *reason)
{
	u32 wdt_shutdown_reason = 0;
	int ret;

	ret = mtk_read_nonrst_reg2(&wdt_shutdown_reason);

	if (ret < 0) {
		pr_err("%s: error(%d) reading nonrst reg2 from wdt\n",
							__func__, ret);
		return ret;
	}

	wdt_shutdown_reason &= THERMAL_SHUTDOWN_REASON_BIT_MASK;

	pr_info("%s: thermal life cycle reason is 0x%x\n", __func__,
							wdt_shutdown_reason);

	switch (wdt_shutdown_reason) {
	case THERMAL_SHUTDOWN_REASON_SOC_BITS_VALUE:
		*reason = THERMAL_SHUTDOWN_REASON_SOC;
		break;
	case THERMAL_SHUTDOWN_REASON_SVS_BITS_VALUE:
		*reason = THERMAL_SHUTDOWN_REASON_SVS;
		break;
	case THERMAL_SHUTDOWN_REASON_CVS_BITS_VALUE:
		*reason = THERMAL_SHUTDOWN_REASON_CVS;
		break;
	case THERMAL_SHUTDOWN_REASON_TVS_BITS_VALUE:
		*reason = THERMAL_SHUTDOWN_REASON_TVS;
		break;
	case THERMAL_SHUTDOWN_REASON_BVS_BITS_VALUE:
		*reason = THERMAL_SHUTDOWN_REASON_BVS;
		break;
	default:
		*reason = LIFE_CYCLE_NOT_AVAILABLE;
	}

	return 0;
}

static int mtk_read_special_mode(enum life_cycle_reason *special_mode)
{
	u32 wdt_special_mode = 0;
	int ret;

	ret = mtk_read_nonrst_reg2(&wdt_special_mode);

	if (ret < 0) {
		pr_err("%s: error(%d) reading nonrst reg2 from wdt\n",
							__func__, ret);
		return ret;
	}

	wdt_special_mode &= LIFE_CYCLE_SPECIAL_MODE_BIT_MASK;

	pr_info("%s: life cycle special mode is 0x%x\n", __func__,
							wdt_special_mode);

	switch (wdt_special_mode) {
	case LIFE_CYCLE_SPECIAL_MODE_LOW_BATTERY_BITS_VALUE:
		*special_mode = LIFE_CYCLE_SMODE_LOW_BATTERY;
		break;
	case LIFE_CYCLE_SPECIAL_MODE_WARM_BOOT_USB_CONNECTED_BITS_VALUE:
		*special_mode = LIFE_CYCLE_SMODE_WARM_BOOT_USB_CONNECTED;
		break;
	case LIFE_CYCLE_SPECIAL_MODE_OTA_BITS_VALUE:
		*special_mode = LIFE_CYCLE_SMODE_OTA;
		break;
	case LIFE_CYCLE_SPECIAL_MODE_FACTORY_RESET_BITS_VALUE:
		*special_mode = LIFE_CYCLE_SMODE_FACTORY_RESET;
		break;
	default:
		pr_err("%s: unsupported life cycle special mode 0x%x\n",
						__func__, wdt_special_mode);
		*special_mode = LIFE_CYCLE_NOT_AVAILABLE;
	}

	return 0;
}

static int mtk_write_special_mode(enum life_cycle_reason special_mode)
{
	u32 wdt_special_mode = 0;
	int ret;

	switch (special_mode) {
	case LIFE_CYCLE_SMODE_LOW_BATTERY:
		wdt_special_mode =
			LIFE_CYCLE_SPECIAL_MODE_LOW_BATTERY_BITS_VALUE;
		break;
	case LIFE_CYCLE_SMODE_WARM_BOOT_USB_CONNECTED:
		wdt_special_mode =
			LIFE_CYCLE_SPECIAL_MODE_WARM_BOOT_USB_CONNECTED_BITS_VALUE;
		break;
	case LIFE_CYCLE_SMODE_OTA:
		wdt_special_mode =
			LIFE_CYCLE_SPECIAL_MODE_OTA_BITS_VALUE;
		break;
	case LIFE_CYCLE_SMODE_FACTORY_RESET:
		wdt_special_mode =
			LIFE_CYCLE_SPECIAL_MODE_FACTORY_RESET_BITS_VALUE;
		break;
	default:
		pr_err("%s: unsupported life cycle special mode: 0x%x\n",
							__func__, special_mode);
		return -EOPNOTSUPP;
	}

	ret = mtk_setbits_nonrst_reg2(LIFE_CYCLE_SPECIAL_MODE_BIT_MASK,
							wdt_special_mode);
	if (ret < 0) {
		pr_err("%s: error(%d) writing nonrst reg2 from wdt\n",
							__func__, ret);
		return ret;
	}

	return 0;
}

static int mtk_lcr_reset(void)
{
	mtk_setbits_nonrst_reg2(THERMAL_SHUTDOWN_REASON_BIT_MASK, 0);
	mtk_setbits_nonrst_reg2(LIFE_CYCLE_SPECIAL_MODE_BIT_MASK, 0);

	return 0;
}

static struct life_cycle_reasons_ops lcr_ops = {
	.read_boot_reason = mtk_read_boot_reason,
	.write_boot_reason = mtk_write_boot_reason,
	.read_shutdown_reason = mtk_read_shutdown_reason,
	.write_shutdown_reason = mtk_write_shutdown_reason,
	.read_thermal_shutdown_reason = mtk_read_thermal_shutdown_reason,
	.write_thermal_shutdown_reason = mtk_write_thermal_shutdown_reason,
	.read_special_mode = mtk_read_special_mode,
	.write_special_mode = mtk_write_special_mode,
	.lcr_reset = mtk_lcr_reset,
};

static int __init mtk_life_cycle_reasons_init(void)
{
	(void) register_life_cycle_reasons_dev(&lcr_ops);

	/*
	 * If the current boot mode is currently FACTORY_BOOT or RECOVERY_BOOT,
	 * we know factory-reset is running.
	 */
	if ((get_boot_mode() == FACTORY_BOOT) ||
		(get_boot_mode() == RECOVERY_BOOT))
		mtk_write_special_mode(LIFE_CYCLE_SMODE_FACTORY_RESET);

	return 0;
}

static int __init lcr_mtk_bootreason(char *options)
{
	if (!strcmp("=power_key", options))
		lc_boot = COLDBOOT_BY_POWER_SUPPLY;
	else if (!strcmp("=usb", options))
		lc_boot = COLDBOOT_BY_USB;
	else if (!strcmp("=wdt", options))
		lc_boot = WARMBOOT_BY_KERNEL_WATCHDOG;
	else if (!strcmp("=wdt_by_pass_pwk", options))
		lc_boot = WARMBOOT_BY_SW;
	else if (!strcmp("=tool_by_pass_pwk", options))
		lc_boot = WARMBOOT_BY_SW;
	else if (!strcmp("=2sec_reboot", options))
		lc_boot = WARMBOOT_BY_SW;
	else if (!strcmp("=unknown", options))
		lc_shutdown = SHUTDOWN_BY_UNKNOWN_REASONS;
	else if (!strcmp("=kernel_panic", options))
		lc_boot = WARMBOOT_BY_KERNEL_PANIC;
	else if (!strcmp("=reboot", options))
		lc_boot = WARMBOOT_BY_SW;
	else if (!strcmp("=watchdog", options))
		lc_boot = WARMBOOT_BY_HW_WATCHDOG;

	return 0;
}
__setup("androidboot.bootreason", lcr_mtk_bootreason);

late_initcall(mtk_life_cycle_reasons_init);
MODULE_LICENSE("GPL v2");
