/*
 * cs42526.h -- CS42526 ALSA SoC audio driver
 *
 * Copyright 2018 Amazon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __CS42526_H__
#define __CS42526_H__

#define CS42526_MAP				0x0

#define CS42526_CHIP_ID				0x1
#define CS42526_CHIP_ID_VAL			0xF0
#define CS42526_CHIP_ID_MASK			0xF0
#define CS42526_REV_ID_MASK			0x0F

#define CS42526_PWRCTL				0x2

#define CS42526_FUNC_MODE			0x3
#define CS42526_FUNC_MODE_ADC_SP_SELX_MASK	0x3
#define CS42526_FUNC_MODE_ADC_SP_SELX_SHIFT	0x2

#define CS42526_IF_FORMAT			0x4

#define CS42526_MISC_CTL			0x5
#define CS42526_MISC_CTL_SAI_SP_MS_MASK		0x1
#define CS42526_MISC_CTL_SAI_SP_MS_SHIFT	0x0

#define CS42526_CLK_CTL				0x6
#define CS42526_VTR_CTL				0xD

#define CS42526_VOLUME_CTRL_A1			0xF
#define CS42526_VOLUME_CTRL_B1			0x10
#define CS42526_VOLUME_CTRL_A2			0x11
#define CS42526_VOLUME_CTRL_B2			0x12
#define CS42526_VOLUME_CTRL_A3			0x13
#define CS42526_VOLUME_CTRL_B3			0x14

#define CS42526_ADC_LEFT_CH_GAIN		0x1C
#define CS42526_ADC_RIGHT_CH_GAIN		0x1D
#define CS42526_ADC_GAIN_MASK			0x3F

#define CS42526_MUTEC_CTR			0x28
#define CS42526_RXP_GPO_1			0x2F
#define CS42526_RXP_GPO_NUM			7
#define CS42526_RXP_GPO_MOD_CTL_MASK		0xC0
#define CS42526_RXP_GPO_MOD_CTL_SHIFT		6

#define CS42526_MAX_REGISTER			0x2F

#define CS42526_ADC_PORT_SEL_MASK		0x0C

#endif
