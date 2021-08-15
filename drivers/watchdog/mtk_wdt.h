/*
 * mtk_wdt.h
 *
 * Copyright (C) 2018 Amazon.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MTK_WDT_H
#define __MTK_WDT_H

#define BOOT_REASON_RPMBPK_BIT_POS 0
#define BOOT_REASON_RECOVERY_BIT_POS 1
#define BOOT_REASON_BOOTLOADER_BIT_POS 2
#define BOOT_REASON_KPOC_BIT_POS 3
#define BOOT_REASON_ENTER_KPOC_BIT_POS 4
#define BOOT_REASON_WMFK_BIT_POS 5
/*
 * The high 3 bits of the WDT_NONRST_REG2 is for thermal shutdown reason and
 * life cycle special mode.
 */
#define THERMAL_SHUTDOWN_REASON_BIT_MASK 0xE0000000
#define THERMAL_SHUTDOWN_REASON_SOC_BITS_VALUE 0x20000000
#define THERMAL_SHUTDOWN_REASON_SVS_BITS_VALUE 0x40000000
#define THERMAL_SHUTDOWN_REASON_CVS_BITS_VALUE 0x60000000
#define THERMAL_SHUTDOWN_REASON_TVS_BITS_VALUE 0x80000000
#define THERMAL_SHUTDOWN_REASON_BVS_BITS_VALUE 0xA0000000

#define LIFE_CYCLE_SPECIAL_MODE_BIT_MASK 0x1C000000
#define LIFE_CYCLE_SPECIAL_MODE_LOW_BATTERY_BITS_VALUE 0x04000000
#define LIFE_CYCLE_SPECIAL_MODE_WARM_BOOT_USB_CONNECTED_BITS_VALUE 0x08000000
#define LIFE_CYCLE_SPECIAL_MODE_OTA_BITS_VALUE 0x0C000000
#define LIFE_CYCLE_SPECIAL_MODE_FACTORY_RESET_BITS_VALUE 0x10000000

int mtk_read_nonrst_reg2(u32 *value);
int mtk_setbits_nonrst_reg2(u32 bit_mask, u32 value);

#endif
