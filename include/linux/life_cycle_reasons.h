/*
 * life_cycle_reasons.h
 *
 * Device life cycle reasons header file
 *
 * Copyright 2011-2018 Amazon.com, Inc. or its Affiliates. All rights reserved.
 * Original Code: Yang Liu (yangliu@lab126.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 */

#ifndef __LIFE_CYCLE_REASONS_H
#define __LIFE_CYCLE_REASONS_H

enum life_cycle_reason {
	/* Device Boot Reason */
	LIFE_CYCLE_NOT_AVAILABLE     = -1,
	WARMBOOT_BY_KERNEL_PANIC     = 0x100,
	WARMBOOT_BY_KERNEL_WATCHDOG  = 0x101,
	WARMBOOT_BY_HW_WATCHDOG      = 0x102,
	WARMBOOT_BY_SW               = 0x103,
	COLDBOOT_BY_USB              = 0x104,
	COLDBOOT_BY_POWER_KEY        = 0x105,
	COLDBOOT_BY_POWER_SUPPLY     = 0x106,
	/* Device Shutdown Reason */
	SHUTDOWN_BY_LONG_PWR_KEY_PRESS = 0x201,
	SHUTDOWN_BY_SW                 = 0x202,
	SHUTDOWN_BY_PWR_KEY            = 0x203,
	SHUTDOWN_BY_SUDDEN_POWER_LOSS  = 0x204,
	SHUTDOWN_BY_UNKNOWN_REASONS    = 0x205,
	/* Thermal Shutdown Reason */
	THERMAL_SHUTDOWN_REASON_BATTERY = 0x301,
	THERMAL_SHUTDOWN_REASON_PMIC    = 0x302,
	THERMAL_SHUTDOWN_REASON_SOC     = 0x303,
	THERMAL_SHUTDOWN_REASON_MODEM   = 0x304,
	THERMAL_SHUTDOWN_REASON_WIFI    = 0x305,
	THERMAL_SHUTDOWN_REASON_PCB     = 0x306,
	THERMAL_SHUTDOWN_REASON_BTS     = 0x307,
	THERMAL_SHUTDOWN_REASON_SVS     = 0x308,
	THERMAL_SHUTDOWN_REASON_CVS     = 0x309,
	THERMAL_SHUTDOWN_REASON_TVS     = 0x30A,
	THERMAL_SHUTDOWN_REASON_BVS     = 0x30B,
	/* LIFE CYCLE Special Mode */
	LIFE_CYCLE_SMODE_NONE                    = 0x400,
	LIFE_CYCLE_SMODE_LOW_BATTERY             = 0x401,
	LIFE_CYCLE_SMODE_WARM_BOOT_USB_CONNECTED = 0x402,
	LIFE_CYCLE_SMODE_OTA                     = 0x403,
	LIFE_CYCLE_SMODE_FACTORY_RESET           = 0x404,
};

/* life_cycle_reasons_data */
struct life_cycle_reasons_data {
	enum life_cycle_reason reason_value;
	char *life_cycle_reasons;
	const char *life_cycle_type;
};

/* life cycle reasons operations */
struct life_cycle_reasons_ops {
	int (*read_boot_reason)(enum life_cycle_reason *boot_reason);
	int (*write_boot_reason)(enum life_cycle_reason boot_reason);
	int (*read_shutdown_reason)(enum life_cycle_reason *shutdown_reason);
	int (*write_shutdown_reason)(enum life_cycle_reason shutdown_reason);
	int (*read_thermal_shutdown_reason)(
		enum life_cycle_reason *thermal_shutdown_reason);
	int (*write_thermal_shutdown_reason)(
		enum life_cycle_reason thermal_shutdown_reason);
	int (*read_special_mode)(enum life_cycle_reason *special_mode);
	int (*write_special_mode)(enum life_cycle_reason special_mode);
	int (*lcr_reset)(void);
};

#ifdef CONFIG_AMAZON_LIFE_CYCLE_REASONS

int register_life_cycle_reasons_dev(struct life_cycle_reasons_ops *lcr_ops);

/*
 * life_cycle_set_boot_reason
 * Description: this function will set the boot reason which causing device
 * booting
 */
int life_cycle_set_boot_reason(enum life_cycle_reason boot_reason);

/*
 * life_cycle_set_shutdown_reason
 * Description: this function will set the Shutdown reason which causing device
 * shutdown
 */
int life_cycle_set_shutdown_reason(enum life_cycle_reason shutdown_reason);

/*
 * life_cycle_set_thermal_shutdown_reason
 * Description: this function will set the Thermal Shutdown reason which causing
 * device shutdown
 */
int life_cycle_set_thermal_shutdown_reason(
	enum life_cycle_reason thermal_shutdown_reason);

/*
 * life_cycle_set_special_mode
 * Description: this function will set the special mode which causing device
 * life cycle change
 */
int life_cycle_set_special_mode(enum life_cycle_reason life_cycle_special_mode);

#else /* !CONFIG_AMAZON_LIFE_CYCLE_REASONS */

static inline int register_life_cycle_reasons_dev(
	struct life_cycle_reasons_ops *lcr_ops)
{
	return -EINVAL;
}


static inline int life_cycle_set_boot_reason(enum life_cycle_reason boot_reason)
{
	return -EINVAL;
}

static inline int life_cycle_set_shutdown_reason(
	enum life_cycle_reason shutdown_reason)
{
	return -EINVAL;
}

static inline int life_cycle_set_thermal_shutdown_reason(
	enum life_cycle_reason thermal_shutdown_reason)
{
	return -EINVAL;
}

static inline int life_cycle_set_special_mode(
	enum life_cycle_reason life_cycle_special_mode)
{
	return -EINVAL;
}

#endif /* CONFIG_AMAZON_LIFE_CYCLE_REASONS */

#endif
