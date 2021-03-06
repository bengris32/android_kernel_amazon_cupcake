/*
 * Copyright (c) 2014-2015 MediaTek Inc.
 * Author: Tianping.Fang <tianping.fang@mediatek.com>
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

#ifndef __MT6397_RTC_MISC_H__
#define __MT6397_RTC_MISC_H__
#include <linux/types.h>

typedef enum {
	RTC_GPIO_USER_WIFI = 8,
	RTC_GPIO_USER_GPS = 9,
	RTC_GPIO_USER_BT = 10,
	RTC_GPIO_USER_FM = 11,
	RTC_GPIO_USER_PMIC = 12,
} rtc_gpio_user_t;

#ifdef CONFIG_MT6397_MISC
extern void mtk_misc_mark_fast(void);
extern void mtk_misc_mark_recovery(void);
extern bool mtk_misc_low_power_detected(void);
extern bool mtk_misc_crystal_exist_status(void);
extern int mtk_misc_set_spare_fg_value(u32 val);
extern u32 mtk_misc_get_spare_fg_value(void);
extern void rtc_gpio_enable_32k(rtc_gpio_user_t user);
extern void rtc_gpio_disable_32k(rtc_gpio_user_t user);
extern void mtk_misc_mark_kpoc(void);
extern void mtk_misc_mark_enter_kpoc(void);
extern bool mtk_misc_enter_kpoc_detected(void);
extern void mtk_misc_mark_enter_sw_lprst(void);
extern void mtk_misc_mark_enter_lprst(void);
extern void mtk_misc_mark_clear_lprst(void);
#else
#define mtk_misc_mark_fast()			do {} while (0)
#define mtk_misc_mark_recovey()			do {} while (0)
#define mtk_misc_low_power_detected()       ({ 0; })
#define mtk_misc_crystal_exist_status()     ({ 1; })
#define mtk_misc_set_spare_fg_value(val)    ({ 0; })
#define mtk_misc_get_spare_fg_value()       ({ 0; })
#define rtc_gpio_enable_32k(user)		do {} while (0)
#define rtc_gpio_disable_32k(user)		do {} while (0)
#define mtk_misc_mark_kpoc(void)	do {} while (0)
#define mtk_misc_mark_enter_kpoc(void)	do {} while (0)
#define mtk_misc_enter_kpoc_detected(void)	({ 0; })
#define mtk_misc_mark_enter_sw_lprst(void)	do {} while (0)
#define mtk_misc_mark_enter_lprst(void)	do {} while (0)
#define mtk_misc_mark_clear_lprst(void)	do {} while (0)
#endif

extern bool upmu_is_chr_det(void);
extern unsigned int upmu_get_reg_value(unsigned int reg);
#endif /* __MT6397_RTC_MISC_H__ */

