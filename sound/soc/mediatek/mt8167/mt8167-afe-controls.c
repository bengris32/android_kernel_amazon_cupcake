/*
 * Mediatek Platform driver ALSA contorls
 *
 * Copyright (c) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mt8167-afe-controls.h"
#include "mt8167-afe-common.h"
#include "mt8167-afe-regs.h"
#include "mt8167-afe-util.h"
#include <sound/soc.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>


#define ENUM_TO_STR(enum) #enum
#define ENABLE_SPDIFIN_UEVENT_NOTIFY 1

struct snd_soc_platform *spdif_platform;
#if ENABLE_SPDIFIN_UEVENT_NOTIFY
struct work_struct uevent_work;
#endif

enum {
	CTRL_SGEN_EN = 0,
	CTRL_SGEN_FS,
	CTRL_AP_LOOPBACK,
};

enum {
	AFE_SGEN_OFF = 0,
	AFE_SGEN_I0I1,
	AFE_SGEN_I2,
	AFE_SGEN_I3I4,
	AFE_SGEN_I5I6,
	AFE_SGEN_I7I8,
	AFE_SGEN_I9,
	AFE_SGEN_I10I11,
	AFE_SGEN_I12I13,
	AFE_SGEN_I14,
	AFE_SGEN_I15I16,
	AFE_SGEN_I17I18,
	AFE_SGEN_I19I20,
	AFE_SGEN_I21I22,

	AFE_SGEN_O0O1,
	AFE_SGEN_O2,
	AFE_SGEN_O3,
	AFE_SGEN_O4,
	AFE_SGEN_O3O4,
	AFE_SGEN_O5O6,
	AFE_SGEN_O7O8,
	AFE_SGEN_O9O10,
	AFE_SGEN_O11,
	AFE_SGEN_O12,
	AFE_SGEN_O13O14,
	AFE_SGEN_O15O16,
	AFE_SGEN_O17O18,
	AFE_SGEN_O19O20,
	AFE_SGEN_O21O22,
	AFE_SGEN_O23O24,
	AFE_SGEN_SPDIFIN,
};

enum {
	AFE_SGEN_8K = 0,
	AFE_SGEN_11K,
	AFE_SGEN_12K,
	AFE_SGEN_16K,
	AFE_SGEN_22K,
	AFE_SGEN_24K,
	AFE_SGEN_32K,
	AFE_SGEN_44K,
	AFE_SGEN_48K,
};

enum {
	AP_LOOPBACK_NONE = 0,
	AP_LOOPBACK_AMIC_TO_SPK,
	AP_LOOPBACK_AMIC_TO_HP,
	AP_LOOPBACK_DMIC_TO_SPK,
	AP_LOOPBACK_DMIC_TO_HP,
	AP_LOOPBACK_HEADSET_MIC_TO_SPK,
	AP_LOOPBACK_HEADSET_MIC_TO_HP,
	AP_LOOPBACK_DUAL_AMIC_TO_SPK,
	AP_LOOPBACK_DUAL_AMIC_TO_HP,
	AP_LOOPBACK_DUAL_DMIC_TO_SPK,
	AP_LOOPBACK_DUAL_DMIC_TO_HP,
};

#if ENABLE_SPDIFIN_UEVENT_NOTIFY
enum {
	NOTIFY_UEVENT_KEY_LOCK = 0,
	NOTIFY_UEVENT_KEY_RATE,
	NOTIFY_UEVENT_KEY_CHSTS,
	NOTIFY_UEVENT_KEY_TYPE,
	NOTIFY_UEVENT_KEY_NUM,
};

static const char *const key_str[] = {
	"LOCK=%d",
	"RATE=%d",
	"CHSTS=%08x%08x%08x%08x%08x%08x",
	"TYPE=%08x",
};
#endif

static const char *const sgen_func[] = {
	ENUM_TO_STR(AFE_SGEN_OFF),
	ENUM_TO_STR(AFE_SGEN_I0I1),
	ENUM_TO_STR(AFE_SGEN_I2),
	ENUM_TO_STR(AFE_SGEN_I3I4),
	ENUM_TO_STR(AFE_SGEN_I5I6),
	ENUM_TO_STR(AFE_SGEN_I7I8),
	ENUM_TO_STR(AFE_SGEN_I9),
	ENUM_TO_STR(AFE_SGEN_I10I11),
	ENUM_TO_STR(AFE_SGEN_I12I13),
	ENUM_TO_STR(AFE_SGEN_I14),
	ENUM_TO_STR(AFE_SGEN_I15I16),
	ENUM_TO_STR(AFE_SGEN_I17I18),
	ENUM_TO_STR(AFE_SGEN_I19I20),
	ENUM_TO_STR(AFE_SGEN_I21I22),
	ENUM_TO_STR(AFE_SGEN_O0O1),
	ENUM_TO_STR(AFE_SGEN_O2),
	ENUM_TO_STR(AFE_SGEN_O3),
	ENUM_TO_STR(AFE_SGEN_O4),
	ENUM_TO_STR(AFE_SGEN_O3O4),
	ENUM_TO_STR(AFE_SGEN_O5O6),
	ENUM_TO_STR(AFE_SGEN_O7O8),
	ENUM_TO_STR(AFE_SGEN_O9O10),
	ENUM_TO_STR(AFE_SGEN_O11),
	ENUM_TO_STR(AFE_SGEN_O12),
	ENUM_TO_STR(AFE_SGEN_O13O14),
	ENUM_TO_STR(AFE_SGEN_O15O16),
	ENUM_TO_STR(AFE_SGEN_O17O18),
	ENUM_TO_STR(AFE_SGEN_O19O20),
	ENUM_TO_STR(AFE_SGEN_O21O22),
	ENUM_TO_STR(AFE_SGEN_O23O24),
	ENUM_TO_STR(AFE_SGEN_SPDIFIN),
};

static const char *const sgen_fs_func[] = {
	ENUM_TO_STR(AFE_SGEN_8K),
	ENUM_TO_STR(AFE_SGEN_11K),
	ENUM_TO_STR(AFE_SGEN_12K),
	ENUM_TO_STR(AFE_SGEN_16K),
	ENUM_TO_STR(AFE_SGEN_22K),
	ENUM_TO_STR(AFE_SGEN_24K),
	ENUM_TO_STR(AFE_SGEN_32K),
	ENUM_TO_STR(AFE_SGEN_44K),
	ENUM_TO_STR(AFE_SGEN_48K),
};

static const char *const ap_loopback_func[] = {
	ENUM_TO_STR(AP_LOOPBACK_NONE),
	ENUM_TO_STR(AP_LOOPBACK_AMIC_TO_SPK),
	ENUM_TO_STR(AP_LOOPBACK_AMIC_TO_HP),
	ENUM_TO_STR(AP_LOOPBACK_DMIC_TO_SPK),
	ENUM_TO_STR(AP_LOOPBACK_DMIC_TO_HP),
	ENUM_TO_STR(AP_LOOPBACK_HEADSET_MIC_TO_SPK),
	ENUM_TO_STR(AP_LOOPBACK_HEADSET_MIC_TO_HP),
	ENUM_TO_STR(AP_LOOPBACK_DUAL_AMIC_TO_SPK),
	ENUM_TO_STR(AP_LOOPBACK_DUAL_AMIC_TO_HP),
	ENUM_TO_STR(AP_LOOPBACK_DUAL_DMIC_TO_SPK),
	ENUM_TO_STR(AP_LOOPBACK_DUAL_DMIC_TO_HP),
};

static int pcm_device;

struct gnt_memif_info {
	int dai_id;
	char dai_name[32];
	unsigned int pbuf_size_reg;
	unsigned int pbuf_size_shift;
	unsigned int pbuf_size_mask;
	unsigned int pbuf_size_unit;
	unsigned int irq_mon_reg;
	int cur_tolerance;
};

struct gnt_memif_info gnt_memif_data[] = {
	{
		.dai_id = MT8167_AFE_MEMIF_DL1,
		.dai_name = "DL1",
		.pbuf_size_reg = AFE_MEMIF_PBUF_SIZE,
		.pbuf_size_shift = 0,
		.pbuf_size_mask = 0x3,
		.pbuf_size_unit = 32,
		.irq_mon_reg = AFE_IRQ1_MCU_CNT_MON,
		.cur_tolerance = 1,
	},
	{
		.dai_id = MT8167_AFE_MEMIF_HDMI,
		.dai_name = "HDMI",
		.pbuf_size_reg = AFE_MEMIF_PBUF_SIZE,
		.pbuf_size_shift = 4,
		.pbuf_size_mask = 0x3,
		.pbuf_size_unit = 16,
		.irq_mon_reg = AFE_IRQ5_MCU_CNT_MON,
		.cur_tolerance = 1,
	},
};

#if ENABLE_SPDIFIN_UEVENT_NOTIFY
static char env_ext[NOTIFY_UEVENT_KEY_NUM][64];
#endif

static int mt8167_afe_sgen_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	struct mt8167_afe_control_data *data = &afe->ctrl_data;

	ucontrol->value.integer.value[0] = data->sinegen_type;
	return 0;
}

static int mt8167_afe_sgen_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	struct mt8167_afe_control_data *data = &afe->ctrl_data;

	if (data->sinegen_type == ucontrol->value.integer.value[0])
		return 0;

	mt8167_afe_enable_main_clk(afe);

	if (data->sinegen_type != AFE_SGEN_OFF)
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0xf0000000);

	switch (ucontrol->value.integer.value[0]) {
	case AFE_SGEN_I0I1:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x048c2762);
		break;
	case AFE_SGEN_I2:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x146c2662);
		break;
	case AFE_SGEN_I3I4:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x24862862);
		break;
	case AFE_SGEN_I5I6:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x346c2662);
		break;
	case AFE_SGEN_I7I8:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x446c2662);
		break;
	case AFE_SGEN_I10I11:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x646c2662);
		break;
	case AFE_SGEN_I12I13:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x746c2662);
		break;
	case AFE_SGEN_I15I16:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x946c2662);
		break;
	case AFE_SGEN_O0O1:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x0c7c27c2);
		break;
	case AFE_SGEN_O2:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x1c6c26c2);
		break;
	case AFE_SGEN_O3:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x2e8c28c2);
		break;
	case AFE_SGEN_O4:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x2d8c28c2);
		break;
	case AFE_SGEN_O3O4:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x2c8c28c2);
		break;
	case AFE_SGEN_O5O6:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x3c6c26c2);
		break;
	case AFE_SGEN_O7O8:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x4c6c26c2);
		break;
	case AFE_SGEN_O9O10:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x5c6c26c2);
		break;
	case AFE_SGEN_O11:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x6c6c26c2);
		break;
	case AFE_SGEN_O12:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x7c0e80e8);
		break;
	case AFE_SGEN_O13O14:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x8c6c26c2);
		break;
	case AFE_SGEN_O15O16:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0x9c6c26c2);
		break;
	case AFE_SGEN_I9:
	case AFE_SGEN_I14:
	case AFE_SGEN_I17I18:
	case AFE_SGEN_I19I20:
	case AFE_SGEN_I21I22:
	case AFE_SGEN_O17O18:
	case AFE_SGEN_O19O20:
	case AFE_SGEN_O21O22:
	case AFE_SGEN_O23O24:
		/* not supported */
		break;
	case AFE_SGEN_SPDIFIN: /*2ch 24bit*/
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON_SPDIFIN, 0xffffffff, 0x110e10e2 /*0x110c10e2*/);
		break;
	default:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xffffffff, 0xf0000000);
		break;
	}

	mt8167_afe_disable_main_clk(afe);

	data->sinegen_type = ucontrol->value.integer.value[0];

	return 0;
}

static int mt8167_afe_sgen_fs_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	struct mt8167_afe_control_data *data = &afe->ctrl_data;

	ucontrol->value.integer.value[0] = data->sinegen_fs;
	return 0;
}

static int mt8167_afe_sgen_fs_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	struct mt8167_afe_control_data *data = &afe->ctrl_data;

	mt8167_afe_enable_main_clk(afe);

	switch (ucontrol->value.integer.value[0]) {
	case AFE_SGEN_8K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0x0);
		break;
	case AFE_SGEN_11K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0x100100);
		break;
	case AFE_SGEN_12K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0x200200);
		break;
	case AFE_SGEN_16K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0x400400);
		break;
	case AFE_SGEN_22K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0x500500);
		break;
	case AFE_SGEN_24K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0x600600);
		break;
	case AFE_SGEN_32K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0x800800);
		break;
	case AFE_SGEN_44K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0x900900);
		break;
	case AFE_SGEN_48K:
		regmap_update_bits(afe->regmap, AFE_SGEN_CON0, 0xf00f00, 0xa00a00);
		break;
	default:
		break;
	}

	mt8167_afe_disable_main_clk(afe);

	data->sinegen_fs = ucontrol->value.integer.value[0];

	return 0;
}

static int mt8167_afe_ap_loopback_get(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	struct mt8167_afe_control_data *data = &afe->ctrl_data;

	ucontrol->value.integer.value[0] = data->loopback_type;

	return 0;
}

static int mt8167_afe_ap_loopback_put(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	struct mt8167_afe_control_data *data = &afe->ctrl_data;
	uint32_t sample_rate = 48000;
	long val = ucontrol->value.integer.value[0];

	if (data->loopback_type == val)
		return 0;

	if (data->loopback_type != AP_LOOPBACK_NONE) {
		if (val == AP_LOOPBACK_AMIC_TO_SPK ||
		    val == AP_LOOPBACK_AMIC_TO_HP ||
		    val == AP_LOOPBACK_DMIC_TO_SPK ||
		    val == AP_LOOPBACK_DMIC_TO_HP) {
			/* disconnect I03 <-> O03, I03 <-> O04 */
			regmap_update_bits(afe->regmap, AFE_CONN1,
					   AFE_CONN1_I03_O03_S,
					   0);
			regmap_update_bits(afe->regmap, AFE_CONN2,
					   AFE_CONN2_I03_O04_S,
					   0);
		} else {
			/* disconnect I03 <-> O03, I04 <-> O04 */
			regmap_update_bits(afe->regmap, AFE_CONN1,
					   AFE_CONN1_I03_O03_S,
					   0);
			regmap_update_bits(afe->regmap, AFE_CONN2,
					   AFE_CONN2_I04_O04_S,
					   0);
		}

		regmap_update_bits(afe->regmap, AFE_ADDA_UL_DL_CON0, 0x1, 0x0);
		regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0, 0x1, 0x0);
		regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0, 0x1, 0x0);
		regmap_update_bits(afe->regmap, AFE_I2S_CON1, 0x1, 0x0);

		mt8167_afe_disable_afe_on(afe);

		mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_DAC);
		mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_DAC_PREDIS);
		mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_ADC);
		mt8167_afe_disable_main_clk(afe);
	}

	if (val != AP_LOOPBACK_NONE) {
		if (val == AP_LOOPBACK_DMIC_TO_SPK ||
		    val == AP_LOOPBACK_DMIC_TO_HP ||
		    val == AP_LOOPBACK_DUAL_DMIC_TO_HP ||
		    val == AP_LOOPBACK_DUAL_DMIC_TO_HP) {
			sample_rate = 32000;
		}

		mt8167_afe_enable_main_clk(afe);

		mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_DAC);
		mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_DAC_PREDIS);
		mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_ADC);

		if (val == AP_LOOPBACK_AMIC_TO_SPK ||
		    val == AP_LOOPBACK_AMIC_TO_HP ||
		    val == AP_LOOPBACK_DMIC_TO_SPK ||
		    val == AP_LOOPBACK_DMIC_TO_HP) {
			/* connect I03 <-> O03, I03 <-> O04 */
			regmap_update_bits(afe->regmap, AFE_CONN1,
					   AFE_CONN1_I03_O03_S,
					   AFE_CONN1_I03_O03_S);
			regmap_update_bits(afe->regmap, AFE_CONN2,
					   AFE_CONN2_I03_O04_S,
					   AFE_CONN2_I03_O04_S);
		} else {
			/* connect I03 <-> O03, I04 <-> O04 */
			regmap_update_bits(afe->regmap, AFE_CONN1,
					   AFE_CONN1_I03_O03_S,
					   AFE_CONN1_I03_O03_S);
			regmap_update_bits(afe->regmap, AFE_CONN2,
					   AFE_CONN2_I04_O04_S,
					   AFE_CONN2_I04_O04_S);
		}

		/* 16 bit by default */
		regmap_update_bits(afe->regmap, AFE_CONN_24BIT,
				AFE_CONN_24BIT_O03 | AFE_CONN_24BIT_O04, 0);

		/* configure uplink */
		if (sample_rate == 32000) {
			regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
					   0x001e0000, (2 << 17) | (2 << 19));
			regmap_update_bits(afe->regmap, AFE_ADDA_NEWIF_CFG1,
					   0xc00, 1 << 10);
		} else {
			regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
					   0x001e0000, (3 << 17) | (3 << 19));
			regmap_update_bits(afe->regmap, AFE_ADDA_NEWIF_CFG1,
					   0xc00, 3 << 10);
		}

		regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0, 0x1, 0x1);

		/* configure downlink */
		regmap_update_bits(afe->regmap, AFE_ADDA_PREDIS_CON0,
				   0xffffffff, 0);
		regmap_update_bits(afe->regmap, AFE_ADDA_PREDIS_CON1,
				   0xffffffff, 0);

		if (sample_rate == 32000) {
			regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0,
					   0xffffffff, 0x63001802);
			regmap_update_bits(afe->regmap, AFE_I2S_CON1,
					   0xf << 8, 0x9 << 8);
		} else {
			regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0,
					   0xffffffff, 0x83001802);
			regmap_update_bits(afe->regmap, AFE_I2S_CON1,
					   0xf << 8, 0xa << 8);
		}

		regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON1,
				   0xffffffff, 0xf74f0000);
		regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0, 0x1, 0x1);
		regmap_update_bits(afe->regmap, AFE_ADDA_UL_DL_CON0, 0x1, 0x1);
		regmap_update_bits(afe->regmap, AFE_I2S_CON1, 0x1, 0x1);

		mt8167_afe_enable_afe_on(afe);
	}

	data->loopback_type = ucontrol->value.integer.value[0];

	return 0;
}

static int mt8167_afe_hdmi_force_clk_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	struct mt8167_afe_control_data *data = &afe->ctrl_data;

	ucontrol->value.integer.value[0] = data->hdmi_force_clk;

	return 0;
}

static int mt8167_afe_hdmi_force_clk_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	struct mt8167_afe_control_data *data = &afe->ctrl_data;

	data->hdmi_force_clk = ucontrol->value.integer.value[0];

	return 0;
}

static int mt8167_afe_tdm_out_sgen_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	unsigned int val = 0;

	mt8167_afe_enable_main_clk(afe);

	regmap_read(afe->regmap, AFE_SINEGEN_CON_TDM, &val);

	mt8167_afe_disable_main_clk(afe);

	ucontrol->value.integer.value[0] = (val & AFE_SINEGEN_CON_TDM_OUT_EN);

	return 0;
}

static int mt8167_afe_tdm_out_sgen_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);

	mt8167_afe_enable_main_clk(afe);

	if (ucontrol->value.integer.value[0])
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON_TDM,
				   GENMASK(31, 0), 0x11071071);
	else
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON_TDM,
				   GENMASK(31, 0), 0x100100);

	mt8167_afe_disable_main_clk(afe);

	return 0;
}

static int mt8167_afe_tdm_in_sgen_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	unsigned int val = 0;

	mt8167_afe_enable_main_clk(afe);

	regmap_read(afe->regmap, AFE_SINEGEN_CON_TDM_IN, &val);

	mt8167_afe_disable_main_clk(afe);

	ucontrol->value.integer.value[0] = (val & AFE_SINEGEN_CON_TDM_IN_EN);

	return 0;
}

static int mt8167_afe_tdm_in_sgen_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);

	mt8167_afe_enable_main_clk(afe);

	if (ucontrol->value.integer.value[0])
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON_TDM_IN,
				   GENMASK(31, 0), 0x110E40E4);
	else
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON_TDM_IN,
				   GENMASK(31, 0), 0x110E40E4);

	mt8167_afe_disable_main_clk(afe);

	return 0;
}

static int mt8167_afe_playback_state_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct snd_soc_card *card = platform->component.card;
	struct snd_pcm_substream *substream;
	struct snd_soc_pcm_runtime *rtd = NULL;
	struct snd_pcm_runtime *runtime;
	unsigned long flag;
	int state = 0;
	int i;

	/* search target substream */
	for (i = 0; i < card->num_rtd; i++) {
		rtd = &(card->rtd[i]);
		if (rtd->pcm->device == pcm_device)
			break;
	}
	if (i == card->num_rtd) {
		pr_err("%s() can not find the target substream\n", __func__);
		goto exit;
	}
	substream = rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	snd_pcm_stream_lock_irqsave(substream, flag);
	runtime = substream->runtime;
	if (runtime != NULL)
		state = snd_pcm_running(substream);
	snd_pcm_stream_unlock_irqrestore(substream, flag);
exit:
	ucontrol->value.integer.value[0] = state;

	return 0;
}

static int mt8167_afe_playback_state_device_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = pcm_device;
	return 0;
}

static int mt8167_afe_playback_state_device_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	pcm_device = ucontrol->value.integer.value[0];
	pr_notice("pcm device(%d) playback state will be update\n", pcm_device);
	return 0;
}

static long long mt8167_afe_get_next_write_timestamp(struct snd_soc_card *card,
				    struct gnt_memif_info info, int dbg_on)
{
	struct snd_pcm_substream *substream;
	struct snd_soc_pcm_runtime *rtd = NULL;
	struct snd_pcm_runtime *runtime;
	struct mtk_afe *afe;
	struct mt8167_afe_memif *memif;
	unsigned long flag;
	unsigned int pbuf_size = 0;
	int i;
	int ret;
	static long long lastValidTimestamp = -1;
	long long timestamp = 0;
	long long temp;
	int remain_size = 0;
	int real_remain_size;
	int delta = 0;
	int irq_count;
	int rptr_offset = 0;
	int max_prefetch_size;
	int period = 0;
	int rate;

	/* search target substream */
	for (i = 0; i < card->num_rtd; i++) {
		rtd = &(card->rtd[i]);
		if (rtd->cpu_dai->id == info.dai_id &&
		    !strcmp(rtd->cpu_dai->name, info.dai_name)) {
			break;
		}
	}

	if (i == card->num_rtd) {
		pr_err("%s() can not find the target substream\n", __func__);
		return lastValidTimestamp;
	}

	substream = rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	afe = snd_soc_platform_get_drvdata(rtd->platform);
	memif = &afe->memif[rtd->cpu_dai->id];

	/* get target sub stream information */
	snd_pcm_stream_lock_irqsave(substream, flag);
	runtime = substream->runtime;
	if (runtime == NULL) {
		pr_err("%s() playback substream is not opened(%d)\n",
		    __func__, substream->hw_opened);
		snd_pcm_stream_unlock_irqrestore(substream, flag);
		goto output_from_regs;
	}

	if ((runtime->status->state == SNDRV_PCM_STATE_OPEN) ||
	    (runtime->status->state == SNDRV_PCM_STATE_DISCONNECTED) ||
	    (runtime->status->state == SNDRV_PCM_STATE_SUSPENDED)) {
		pr_err("%s() playback state(%d) is not right\n",
		    __func__, runtime->status->state);
		snd_pcm_stream_unlock_irqrestore(substream, flag);
		goto output_from_regs;
	}

	period = (int)(runtime->period_size);
	rate = (int)(runtime->rate);

	if (runtime->status->state == SNDRV_PCM_STATE_XRUN) {
		real_remain_size = 0;
		timestamp = (long long)mtk_timer_get_cnt(6);
		snd_pcm_stream_unlock_irqrestore(substream, flag);
		pr_err("%s() playback state is xrun state\n", __func__);
		goto output_cal;
	}

	if ((runtime->status->state != SNDRV_PCM_STATE_XRUN) &&
		  (!snd_pcm_running(substream))) {
		real_remain_size =
		    (int)(runtime->buffer_size - snd_pcm_playback_avail(runtime) + runtime->delay);
		timestamp = (long long)mtk_timer_get_cnt(6);
		snd_pcm_stream_unlock_irqrestore(substream, flag);
		goto output_cal;
	}

	regmap_read(afe->regmap, info.pbuf_size_reg, &pbuf_size);
	pbuf_size = ((pbuf_size>>info.pbuf_size_shift) & info.pbuf_size_mask);
	pbuf_size = (info.pbuf_size_unit)<<pbuf_size;
	max_prefetch_size = bytes_to_frames(runtime, pbuf_size);

	timestamp = (long long)mtk_timer_get_cnt(6);
	regmap_read(afe->regmap, info.irq_mon_reg, &irq_count);
	ret = snd_pcm_update_hw_ptr(substream);

	if (ret < 0) {
		real_remain_size = 0;
		snd_pcm_stream_unlock_irqrestore(substream, flag);
		goto output_cal;
	}

	remain_size =
	    (int)(runtime->buffer_size - snd_pcm_playback_avail(runtime) + runtime->delay);
	delta = (int)(runtime->control->appl_ptr % runtime->period_size);
	if (delta != 0)
		delta = period - delta;

	rptr_offset = (int)(runtime->status->hw_ptr % runtime->period_size);
	snd_pcm_stream_unlock_irqrestore(substream, flag);

	if (remain_size + delta <= irq_count) {
		real_remain_size = irq_count;
		real_remain_size -= delta;
	} else {
		int tmp;

		tmp = remain_size + delta - irq_count;
		real_remain_size = irq_count + (tmp / period + ((tmp%period) ? 1 : 0)) * period;
		real_remain_size -= delta;
	}

	/* unormal case check */
	if ((real_remain_size > (remain_size + max_prefetch_size + info.cur_tolerance)) ||
	    (real_remain_size < (remain_size - info.cur_tolerance))) {
		if (dbg_on)
			pr_err("%s() invalid real_remain_size(%d), irq_count(%d), rptr_offset(%d), delta(%d), periods(%d), remain(%d)\n",
			    __func__, real_remain_size, irq_count,
			    rptr_offset, delta, period, remain_size);
		real_remain_size = remain_size;
	}

output_cal:

	temp = (13000000LL) * (long long)real_remain_size;
	do_div(temp, (long long)rate);
	timestamp += temp;
	lastValidTimestamp = timestamp;
	return timestamp;

output_from_regs:

	timestamp = (long long)mtk_timer_get_cnt(6);
	lastValidTimestamp = timestamp;
	return timestamp;
}

static int mt8167_afe_dl1_timestamp_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct snd_soc_card *card = platform->component.card;

	ucontrol->value.integer64.value[0] =
	    mt8167_afe_get_next_write_timestamp(card, gnt_memif_data[0], 1);
	return 0;
}

static int mt8167_afe_hdmi_timestamp_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct snd_soc_card *card = platform->component.card;

	ucontrol->value.integer64.value[0] =
	    mt8167_afe_get_next_write_timestamp(card, gnt_memif_data[1], 0);
	return 0;
}

static const struct soc_enum mt8167_afe_soc_enums[] = {
	[CTRL_SGEN_EN] = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sgen_func),
				sgen_func),
	[CTRL_SGEN_FS] = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sgen_fs_func),
				sgen_fs_func),
	[CTRL_AP_LOOPBACK] = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ap_loopback_func),
				ap_loopback_func),
};

static int mt8167_afe_hw_gain1_vol_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	unsigned int val;

	mt8167_afe_enable_main_clk(afe);
	regmap_read(afe->regmap, AFE_GAIN1_CON1, &val);
	mt8167_afe_disable_main_clk(afe);
	ucontrol->value.integer.value[0] = val & AFE_GAIN1_CON1_MASK;

	return 0;
}

static int mt8167_afe_hw_gain1_vol_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	unsigned int val;

	val = ucontrol->value.integer.value[0];
	mt8167_afe_enable_main_clk(afe);
	regmap_update_bits(afe->regmap,
		AFE_GAIN1_CON1, AFE_GAIN1_CON1_MASK, val);
	mt8167_afe_disable_main_clk(afe);
	return 0;
}

static int mt8167_afe_hw_gain1_sampleperstep_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	unsigned int val;

	mt8167_afe_enable_main_clk(afe);
	regmap_read(afe->regmap, AFE_GAIN1_CON0, &val);
	mt8167_afe_disable_main_clk(afe);
	ucontrol->value.integer.value[0] =
		(val & AFE_GAIN1_CON0_SAMPLE_PER_STEP_MASK) >> 8;

	return 0;
}

static int mt8167_afe_hw_gain1_sampleperstep_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform = snd_soc_kcontrol_platform(kcontrol);
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(platform);
	unsigned int val;

	val = ucontrol->value.integer.value[0];
	mt8167_afe_enable_main_clk(afe);
	regmap_update_bits(afe->regmap,
		AFE_GAIN1_CON0, AFE_GAIN1_CON0_SAMPLE_PER_STEP_MASK, val << 8);
	mt8167_afe_disable_main_clk(afe);
	return 0;
}

/*********spdif in**************/
#define ISPDIF_FS_SUPPORT_RANGE 9

typedef enum {
	SPDIFIN_OUT_RANGE = 0x00, /*0x00~0x06 Freq out of range*/
	SPDIFIN_32K = 0x07,
	SPDIFIN_44K = 0x08,
	SPDIFIN_48K = 0x09,
	SPDIFIN_64K = 0x0A,
	SPDIFIN_88K = 0x0B,
	SPDIFIN_96K = 0x0C,
	SPDIFIN_128K = 0x0D,
	SPDIFIN_176K = 0x0E,
	SPDIFIN_192K = 0x0F
} SPDIFIN_FS;

struct afe_dir_info {
	int rate;
	int u_bit[2][6];
	int c_bit[6];
	int stream_type;
};

enum afe_spdifrx_port {
	SPDIFRX_PORT_NONE = 0,
	SPDIFRX_PORT_OPT = 1,
	SPDIFRX_PORT_ARC = 2
};

static volatile struct afe_dir_info spdifrx_state;
static bool spdifrx_inited;

static u32 spdifrx_fscnt[16][9] = {
	/*32k       44.1k        48k             64k             88.2k      96k          128k       176k         192k*/
	{6750, 4898, 4500, 3375, 2455, 2250, 1688, 1227, 1125 }, /* 1 subframe*/
	{13500, 9796, 9000, 6750, 4909, 4500, 3375, 2455, 2250 }, /* 2 subframe*/
	{27000, 19592, 18000, 13500, 9818, 9000, 6750, 4909, 4500 }, /* 4 subframe*/
	{54000, 39184, 36000, 27000, 19636, 18000, 13500, 9818, 9000 }, /* 8 subframe*/
	{108000, 78367, 72000, 54000, 39273, 36000, 27000, 19636, 18000 }, /* 16 subframe*/
	{216000, 156735, 144000, 108000, 78546, 72000, 54000, 39273, 36000 }, /* 32 subframe*/
	{432000, 313469, 288000, 216000, 157091, 144000, 108000, 78546, 72000 }, /* 64 subframe*/
	{864000, 626939, 576000, 432000, 314182, 288000, 216000, 157091, 144000 }, /* 128 subframe*/
	{1728027, 1253897, 1152018, 864014, 626949, 576008, 432000, 313469, 288000 }, /*256 subframe*/
	{3456000, 2507755, 2304000, 1728000, 1256727, 1152000, 864000, 628364, 576000 }, /* 512 subframe*/
	{6912000, 5015510, 4608000, 3456000, 2513455, 2304000, 1728000, 1256727, 1152000 }, /* 1024 subframe*/
	{13824000, 10031020, 9216000, 6912000, 5026909, 4608000, 3456000, 2513455, 2304000 }, /* 2048 subframe*/
	 /* 4096 subframe*/
	{27648000, 20062041, 18432000, 13824000, 10053818, 9216000, 6912000, 5026909, 4608000 },
	/* 8192 subframe*/
	{55296000, 40124082, 36864000, 27648000, 20107636, 18432000, 13824000, 10053818, 9216000 },
	/* 16384 subframe*/
	{110592000, 80248163, 73728000, 55296000, 40215272, 36864000, 27648000, 20107636, 18432000},
	/* 32768 subframe*/
	{221184000, 160496327, 147456000, 110592000, 80430546, 73728000, 55296000, 40215273, 36864000}
};

static u32 spdifrx_fsoft[16][9]  = {
	/*32k       44.1k        48k            64k         88.2k       96k         128k        176k        192k*/
	{78, 78, 78, 78, 78, 78, 78, 78, 78 }, /* 1 subframe*/
	{156, 156, 156, 156, 156, 156, 156, 156, 156 }, /* 2 subframe*/
	{312, 312, 312, 312, 312, 312, 312, 312, 312 }, /* 4 subframe*/
	{625, 625, 625, 625, 625, 625, 625, 625, 625 }, /* 8 subframe*/
	{1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250 }, /* 16 subframe*/
	{2500, 2500, 2500, 2500, 2500, 2500, 2500, 2500, 2500 }, /*32 subframe*/
	{5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000 }, /* 64 subframe*/
	{10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000 }, /* 128 subframe*/
	{200000, 45000, 45000, 27000, 20000, 18000, 14000, 10000, 9000 }, /* 256 subframe*/
	{60000, 45000, 45000, 20000, 20000, 20000, 20000, 20000, 20000 }, /* 512 subframe*/
	{80000, 80000, 80000, 80000, 80000, 80000, 80000, 80000, 80000 }, /* 1024 subframe*/
	{160000, 160000, 160000, 160000, 160000, 160000, 160000, 160000, 160000 }, /* 2048 subframe*/
	{320000, 320000, 320000, 320000, 320000, 320000, 320000, 320000, 320000 }, /* 4096 subframe*/
	{640000, 640000, 640000, 640000, 640000, 640000, 640000, 640000, 640000 }, /* 8192 subframe*/
	{1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000 }, /* 16384 subframe*/
	{2560000, 2560000, 2560000, 2560000, 2560000, 2560000, 2560000, 2560000, 2560000 }  /* 32768 subframe*/
};

u32 _u4LRCKCmp432M[9] = {
    /*32k   44.1k    48k     64k   88.2k    96k      128k   176.4k    192k*/
    /*203,   147,   135,   102,   73,      68,    51,      37,         34   432M*3%*fs/2*/
176,   98,   90,   68,   49,      46,    34,      20,         23  /* 432M*3%*fs/3 ,32k : 136+40 176.4k(25-5)*/
    /*102,   74,   68,   51,   37,      34,    26,      18,         17  // 432M*3%*fs/4 */
};

u32 _u4LRCKCmp594M[9] = {
    /*32k   44.1k    48k     64k   88.2k    96k      128k   176.4k    192k */
279,    203,    186,     140,   102,    93,      70,   51,       47
};

const volatile struct afe_dir_info *afe_spdifrx_state(void)
{
	return &spdifrx_state;
}

static void spdifrx_select_port(enum afe_spdifrx_port port)
{
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(spdif_platform);
	unsigned int val;

	if (port == SPDIFRX_PORT_OPT) {
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT, MULTI_INPUT_SEL_MASK, MULTI_INPUT_SEL_OPT);
		/* cautious
		* AFE_SPDIFIN_INT_EXT: 0x08, not 0x108
		* if bit(8) ==1 irq9 will continue come, not stop
		*/
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT, 0xf << 8, 0 << 8);
	} else {
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT, MULTI_INPUT_SEL_MASK, MULTI_INPUT_SEL_ARC);
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT, 0xf << 8, 4 << 8);
	}

	regmap_read(afe->regmap, AFE_SPDIFIN_CFG1, &val);
	val &= (AFE_SPDIFIN_REAL_OPTICAL) & (AFE_SPDIFIN_SWITCH_REAL_OPTICAL);
	regmap_write(afe->regmap, AFE_SPDIFIN_CFG1, val);

	regmap_read(afe->regmap, AFE_SPDIFIN_CFG1, &val);
	val |= AFE_SPDIFIN_FIFOSTARTPOINT_5;
	regmap_write(afe->regmap, AFE_SPDIFIN_CFG1, val);
}

static void spdifrx_clear_vucp(void)
{
	memset((void *)spdifrx_state.c_bit, 0xff, sizeof(spdifrx_state.c_bit));
	memset((void *)spdifrx_state.u_bit, 0xff, sizeof(spdifrx_state.u_bit));
}

static u32 spdifrx_fs_interpreter(u32 fsval)
{
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(spdif_platform);
	u8 period, cnt;
	u32 fs = SPDIFIN_OUT_RANGE;
	u32 rangeplus, rangeminus;
	unsigned int val;

	regmap_read(afe->regmap, AFE_SPDIFIN_BR, &val);
	period = (val&AFE_SPDIFIN_BR_SUBFRAME_MASK) >> 8;

	for (cnt = 0; cnt < ISPDIF_FS_SUPPORT_RANGE; cnt++) {
		rangeplus = (spdifrx_fscnt[period][cnt] + spdifrx_fsoft[period][cnt]);
		rangeminus = (spdifrx_fscnt[period][cnt] - spdifrx_fsoft[period][cnt]);
		rangeplus = (rangeplus * 624) / 432;
		rangeminus = (rangeminus * 624) / 432;
		if ((fsval > rangeminus) && (fsval < rangeplus)) {
			fs = cnt + SPDIFIN_32K; /*from 32k~192k*/
			break;
		}
	}

	if (cnt > ISPDIF_FS_SUPPORT_RANGE) {
		fs = SPDIFIN_OUT_RANGE;
		pr_err("%s()FS Out of Detected Range!\n", __func__);
	}

	return fs;
}

static void (*spdifrx_callback)(void);

/*
 * [Programming Guide]
 * [SPDIF IN] spdif in IRQ9 callback
 */
static u32 get_clear_bits(u32 v)
{
	u32 bits = 0;
	/* AFE_SPDIFIN_DEBUG3 */
	if (v & SPDIFIN_PRE_ERR_NON_STS)
		bits |= SPDIFIN_PRE_ERR_CLEAR;				/* 0-0 */
	if (v & SPDIFIN_PRE_ERR_B_STS)
		bits |= SPDIFIN_PRE_ERR_B_CLEAR;				/* 1-1 */
	if (v & SPDIFIN_PRE_ERR_M_STS)
		bits |= SPDIFIN_PRE_ERR_M_CLEAR;				/* 2-2 */
	if (v & SPDIFIN_PRE_ERR_W_STS)
		bits |= SPDIFIN_PRE_ERR_W_CLEAR;				/* 3-3 */
	if (v & SPDIFIN_PRE_ERR_BITCNT_STS)
		bits |= SPDIFIN_PRE_ERR_BITCNT_CLEAR;			/* 4-4 */
	if (v & SPDIFIN_PRE_ERR_PARITY_STS)
		bits |= SPDIFIN_PRE_ERR_PARITY_CLEAR;			/* 5-5 */
	if (v & SPDIFIN_FIFO_ERR_STS)
		bits |= SPDIFIN_FIFO_ERR_CLEAR;				/* 30,31 - 6,7 */
	if (v & SPDIFIN_TIMEOUT_ERR_STS)
		bits |= SPDIFIN_TIMEOUT_INT_CLEAR;				/* 6-8 */
	/* AFE_SPDIFIN_INT_EXT2 */
	if (v & SPDIFIN_LRCK_CHG_INT_STS)
		bits |= SPDIFIN_DATA_LRCK_CHANGE_CLEAR;			/* 27-16 */
	/* AFE_SPDIFIN_DEBUG1 */
	if (v & SPDIFIN_DATA_LATCH_ERR)
		bits |= SPDIFIN_DATA_LATCH_CLEAR;				/* 10-17 */
	/* not error AFE_SPDIFIN_DEBUG2*/
	if (v & SPDIFIN_CHSTS_PREAMPHASIS_STS)
		bits |= SPDIFIN_CHSTS_PREAMPHASIS_CLEAR;				/* 7-9 */
	if (v & SPDIFIN_CHSTS_INT_FLAG)
		bits |= SPDIFIN_CHSTS_INT_CLR_EN;		/* 26-11 */
	return bits;
}

void afe_spdifrx_isr(void)
{
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(spdif_platform);
	u32 regval1, regval2, regval3, fsval, fsvalod, chsintflag, type_det;
	int i, j, fs_changed, chsts_changed, type_changed, n;
	unsigned int err, noterr, clear_bits;
	struct mt8167_afe_memif *memif = &afe->memif[MT8167_AFE_MEMIF_MULTILINE_IN];
	struct snd_pcm_substream *substream = memif->substream;
	unsigned long _flags;

	regmap_read(afe->regmap, AFE_SPDIFIN_DEBUG3, &regval1);
	regmap_read(afe->regmap, AFE_SPDIFIN_INT_EXT2, &regval2);
	regmap_read(afe->regmap, AFE_SPDIFIN_DEBUG1, &regval3);
	regmap_read(afe->regmap, AFE_SPDIFIN_DEBUG2, &chsintflag);


	err = (regval1 & SPDIFIN_ALL_ERR_ERR_STS) |	(regval2 & SPDIFIN_LRCK_CHG_INT_STS) |
		(regval3 & SPDIFIN_DATA_LATCH_ERR) | (chsintflag & SPDIFIN_FIFO_ERR_STS);
	noterr = (regval1 & SPDIFIN_CHSTS_PREAMPHASIS_STS) | (chsintflag & SPDIFIN_CHSTS_INT_FLAG);
	clear_bits = get_clear_bits(err);
	if (err != 0) {
		if (spdifrx_state.rate > 0) {
			if (regval1 & SPDIFIN_ALL_ERR_ERR_STS)
				pr_err("%s Error is 0x%x\n", __func__, regval1 & SPDIFIN_ALL_ERR_ERR_STS);
			if (regval2 & SPDIFIN_LRCK_CHG_INT_STS)
				pr_err("%s LRCK Change\n", __func__);
			if (regval3 & SPDIFIN_DATA_LATCH_ERR)
				pr_err("%s Data Latch error!\n", __func__);
			if (chsintflag & SPDIFIN_FIFO_ERR_STS)
				pr_err("%s FIFO error!\n", __func__);
			spdifrx_state.rate = 0;
			spdifrx_state.stream_type = 0;
			spdifrx_clear_vucp();

			if (substream) {
				snd_pcm_stream_lock_irqsave(substream, _flags);
				if (snd_pcm_running(substream))
						  snd_pcm_stop(substream, SNDRV_PCM_STATE_DISCONNECTED);
				snd_pcm_stream_unlock_irqrestore(substream, _flags);
			}

			if (spdifrx_callback) {
#if ENABLE_SPDIFIN_UEVENT_NOTIFY
				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_LOCK], 64,
					key_str[NOTIFY_UEVENT_KEY_LOCK], 0);
				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_RATE], 64,
					key_str[NOTIFY_UEVENT_KEY_RATE], 0);
				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_CHSTS], 64,
					key_str[NOTIFY_UEVENT_KEY_CHSTS], 0, 0, 0, 0, 0, 0);
				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_TYPE], 64,
					key_str[NOTIFY_UEVENT_KEY_TYPE], 0);

				schedule_work(&uevent_work);
#endif
			}
		}
		/*Disable SpdifRx interrupt disable*/
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG0, SPDIFIN_INT_EN_MASK | SPDIFIN_EN_MASK, SPDIFIN_INT_DIS | SPDIFIN_DIS);
		/*Clear interrupt bits*/
		regmap_write(afe->regmap, AFE_SPDIFIN_EC, clear_bits);
		/*Enable SpdifRx interrupt disable*/
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG0, SPDIFIN_INT_EN_MASK | SPDIFIN_EN_MASK, SPDIFIN_INT_EN | SPDIFIN_EN);
	} else {
		/*Enable Timeout Interrupt*/
		fs_changed = 0;
		chsts_changed = 0;
		type_changed = 0;

		regmap_read(afe->regmap, AFE_SPDIFIN_BR_DBG1, &fsval);
		fsval = spdifrx_fs_interpreter(fsval);

		if (fsval != SPDIFIN_OUT_RANGE) {
			regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT2, SPDIFIN_LRC_MASK, _u4LRCKCmp594M[fsval-SPDIFIN_32K]);
			regmap_read(afe->regmap, SPDIFIN_TYPE_DET, &type_det);
			if (spdifrx_state.stream_type != type_det)
			    type_changed = 1;
			spdifrx_state.stream_type = type_det;
			fsvalod = spdifrx_state.rate;
			spdifrx_state.rate = fsval;
			if (fsvalod != fsval)
				fs_changed = 1;

		}
		if (((chsintflag & SPDIFIN_CHSTS_INT_FLAG) != 0) && (fsval != SPDIFIN_OUT_RANGE)) {
			/* check channel status changed or not */
			for (i = 0; i < 6; i++) {
				unsigned int temp;

				regmap_read(afe->regmap, AFE_SPDIFIN_CHSTS1 + i * 0x4, &temp);

				if (temp != spdifrx_state.c_bit[i]) {
					chsts_changed = 1;
					spdifrx_state.c_bit[i] =  temp;
				}
			}
			/* check user data changed or not */
			for (i = 0; i < 2; i++) {
				for (j = 0; j < 6; j++) {
					unsigned int temp;

					regmap_read(afe->regmap, SPDIFIN_FREQ_USERCODE1 + (i * 6 + j) * 0x4, &temp);

					if (temp != spdifrx_state.u_bit[i][j])
						spdifrx_state.u_bit[i][j] = temp;

				}
			}

		}

		if (spdifrx_callback) {
#if ENABLE_SPDIFIN_UEVENT_NOTIFY
			if (fs_changed) {
				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_LOCK], 64,
					key_str[NOTIFY_UEVENT_KEY_LOCK], 1);

				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_RATE], 64,
					key_str[NOTIFY_UEVENT_KEY_RATE], fsval);

				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_TYPE], 64,
					key_str[NOTIFY_UEVENT_KEY_TYPE], type_det);
			}

			if (chsts_changed || type_changed) {
				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_LOCK], 64,
					key_str[NOTIFY_UEVENT_KEY_LOCK], 1);

				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_CHSTS], 64,
					key_str[NOTIFY_UEVENT_KEY_CHSTS],
					spdifrx_state.c_bit[0], spdifrx_state.c_bit[1],
					spdifrx_state.c_bit[2], spdifrx_state.c_bit[3],
					spdifrx_state.c_bit[4], spdifrx_state.c_bit[5]);

				n = scnprintf(env_ext[NOTIFY_UEVENT_KEY_TYPE], 64,
					key_str[NOTIFY_UEVENT_KEY_TYPE], type_det);
			}

			if (fs_changed || chsts_changed || type_changed)
				schedule_work(&uevent_work);
#endif
		}

		if (fsval == SPDIFIN_OUT_RANGE) /* Disable SpdifRx interrupt */
			regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG0, SPDIFIN_INT_EN_MASK | SPDIFIN_EN_MASK, SPDIFIN_INT_DIS | SPDIFIN_DIS);

		/*Clear interrupt bits*/
		regmap_write(afe->regmap, AFE_SPDIFIN_EC, SPDIFIN_INT_CLEAR_ALL);

		if (fsval == SPDIFIN_OUT_RANGE) /* Enable SpdifRx interrupt */
		    regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG0, SPDIFIN_INT_EN_MASK | SPDIFIN_EN_MASK, SPDIFIN_INT_EN | SPDIFIN_EN);
	}
}

static void spdifrx_irq_enable(int en)
{
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(spdif_platform);

	if (en) {
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 2, 1 << 2);/*enable IRQ 9*/
		/*
		* AFE_SPDIFIN_CFG1:
		* ok->0xb3f00010, 0xb3f00000 can not lock signal sometimes, the AFE_SPDIFIN_FIFOSTARTPOINT value
		* should between in 5 and 7, default value is 3
		* bit0 should be 0
		*/
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG1,
			      SPDIFIN_INT_ERR_EN_MASK | SEL_BCK_SPDIFIN | AFE_SPDIFIN_FIFOSTARTPOINT_5,
			      SPDIFIN_ALL_ERR_INT_EN | SEL_BCK_SPDIFIN | AFE_SPDIFIN_FIFOSTARTPOINT_5);
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT, SPDIFIN_DATALATCH_ERR_EN_MASK, SPDIFIN_DATALATCH_ERR_EN);

		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT, SPDIFIN_DERR_NEW_RETEN_EN_MASK, SPDIFIN_DERR_NEW_RETEN_EN);
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG0,
			      SPDIFIN_EN_MASK | SPDIFIN_INT_EN_MASK | SPDIFIN_FLIP_EN_MASK|
			      SPDIFIN_DE_CNT_MASK | SPDIFIN_DE_SEL_MASK | MAX_LEN_NUM_MASK,
			      SPDIFIN_EN | SPDIFIN_INT_EN | SPDIFIN_FLIP_EN | 4 << 8 |
			      SPDIFIN_DE_SEL_DECNT | 0xED << 16);

		regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG0,
			      SPDIFIN_DPEER_EN_MASK | SPDIFIN_DEER_EN_MASK | SPDIFIN_TIMEOUT_MASK,
			      SPDIFIN_DPEER_EN | SPDIFIN_DEER_EN | SPDIFIN_TIMEOUT_EN);

	} else {
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG0,
			      SPDIFIN_EN | SPDIFIN_INT_EN | AFE_SPDIFIN_SEL_SPDIFIN_EN | SPDIFIN_FLIP_EN,
			      SPDIFIN_DIS | SPDIFIN_INT_DIS | AFE_SPDIFIN_SEL_SPDIFIN_DIS | SPDIFIN_FLIP_DIS);

		regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG0,
				      SPDIFIN_DPEER_EN_MASK | SPDIFIN_DEER_EN_MASK | SPDIFIN_TIMEOUT_MASK,
				      SPDIFIN_DPEER_DIS | SPDIFIN_DEER_DIS | SPDIFIN_TIMEOUT_DIS);

		regmap_update_bits(afe->regmap, AFE_SPDIFIN_CFG1,
			      SPDIFIN_INT_ERR_EN_MASK | SEL_BCK_SPDIFIN,
			      SPDIFIN_ALL_ERR_INT_DIS | ~SEL_BCK_SPDIFIN);
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT, SPDIFIN_DATALATCH_ERR_EN_MASK, SPDIFIN_DATALATCH_ERR_DIS);
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT, SPDIFIN_DERR_NEW_RETEN_EN_MASK, SPDIFIN_DERR_NEW_RETEN_DIS);
		regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT2, SPDIFIN_LRCK_CHG_INT_MASK, SPDIFIN_LRCK_CHG_INT_DIS);

		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 2, 0 << 2);/*disable IRQ 9*/
	}
}

static void spdifrx_init(enum afe_spdifrx_port port)
{
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(spdif_platform);

	if (spdifrx_inited) {
		pr_debug("%s() Dir has already inited.\n", __func__);
		return;
	}
	spdifrx_clear_vucp();
	spdifrx_state.rate = 0;
	spdifrx_state.stream_type = 0;

	mt8167_afe_enable_main_clk(afe);
	/*
	 * Set spdifin clk cfg
	 */
	mt_afe_spdif_dir_clk_on(afe);

	/*
	 * [Programming Guide]
	 * [SPDIF IN] spdifin config
	 * AFE_SPDIFIN_INT_EXT2: 0x00020000
	 * SPDIFIN_FREQ_INFO_2:  0x006596e8 or 0x6596ED
	 * SPDIFIN_FREQ_INFO_3:  0x000005a5 or 0x5A4
	 * AFE_SPDIFIN_BR:            0x00079000
	 */
	regmap_write(afe->regmap, SPDIFIN_FREQ_INFO, 0x00877986);
	regmap_write(afe->regmap, SPDIFIN_FREQ_INFO_2, 0x006596e8);
	regmap_write(afe->regmap, SPDIFIN_FREQ_INFO_3, 0x000005a5);

	/*Bitclk recovery enable and lowbound*/
	regmap_write(afe->regmap, AFE_SPDIFIN_BR, 0x00039000);
	regmap_update_bits(afe->regmap, AFE_SPDIFIN_INT_EXT2, SPDIFIN_594MODE_MASK, SPDIFIN_594MODE_EN);

	mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_INTDIR_CK);
	mt8167_afe_enable_afe_on(afe);
	spdifrx_select_port(port);

	spdifrx_irq_enable(1);
	spdifrx_inited = 1;
}

static void spdifrx_uninit(void)
{
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(spdif_platform);
	unsigned long flags;

	if (!spdifrx_inited) {
		pr_err("%s() Dir has already uninited.\n", __func__);
		return;
	}
	spin_lock_irqsave(&afe->spdifin_ctrl_lock, flags);
	spdifrx_irq_enable(0);
	mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_INTDIR_CK);
	mt_afe_spdif_dir_clk_off(afe);
	mt8167_afe_disable_afe_on(afe);
	mt8167_afe_disable_main_clk(afe);
	spdifrx_state.rate = 0;
	spdifrx_clear_vucp();
	spdifrx_inited = 0;
	spin_unlock_irqrestore(&afe->spdifin_ctrl_lock, flags);
}

void afe_spdifrx_start(enum afe_spdifrx_port port, void (*callback)(void))
{
	/*
	 * [Programming Guide]
	 * [SPDIF IN]GPIO mode setting
	 */
	switch (port) {
	case SPDIFRX_PORT_OPT:
		break;
	case SPDIFRX_PORT_ARC:
		break;
	default:
		pr_err("%s() invalid port: %d\n", __func__, port);
		return;
	}
	spdifrx_callback = callback;
	spdifrx_init(port);
}

void afe_spdifrx_stop(void)
{
	spdifrx_uninit();
}

static int spdif_rx_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 20;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1000000000;
	return 0;
}

static int spdif_rx_en_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	return 0;
}


static int spdif_rx_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	const volatile struct afe_dir_info *state = afe_spdifrx_state();
	int rate;
	int i;
	int index;

	switch (state->rate) {
	case 0x7:
		rate = 32000;
		break;
	case 0x8:
		rate = 44100;
		break;
	case 0x9:
		rate = 48000;
		break;
	case 0xb:
		rate = 88200;
		break;
	case 0xc:
		rate = 96000;
		break;
	case 0xe:
		rate = 176400;
		break;
	case 0xf:
		rate = 192000;
		break;
	default:
		rate = 0;
		break;
	}

	ucontrol->value.integer.value[0] = rate;
	index = sizeof(state->rate)/sizeof(int);
	for (i = 0; i < sizeof(state->u_bit)/sizeof(int); i++)
		ucontrol->value.integer.value[i + index] = *(&state->u_bit[0][0] + i);

	index += sizeof(state->u_bit)/sizeof(int);
	for (i = 0; i < sizeof(state->c_bit)/sizeof(int); i++)
		ucontrol->value.integer.value[i + index] = state->c_bit[i];

	index += sizeof(state->c_bit)/sizeof(int);
	ucontrol->value.integer.value[index++] = state->stream_type;

	pr_err("%s() rate=%d, stream_type=%d, c_bit=%d\n", __func__, rate,
			state->stream_type, state->c_bit[0]);

	return 0;

}

static int spdif_rx_en_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	const volatile struct afe_dir_info *state = afe_spdifrx_state();
	int rate;

	switch (state->rate) {
	case 0x7:
		rate = 32000;
		break;
	case 0x8:
		rate = 44100;
		break;
	case 0x9:
		rate = 48000;
		break;
	case 0xb:
		rate = 88200;
		break;
	case 0xc:
		rate = 96000;
		break;
	case 0xe:
		rate = 176400;
		break;
	case 0xf:
		rate = 192000;
		break;
	default:
		rate = 0;
		break;
	}

	ucontrol->value.integer.value[0] = rate;

	pr_info("%s() rate=%d\n", __func__, rate);

	return 0;

}



static struct snd_kcontrol *snd_ctl_find_name(struct snd_card *card,
					      unsigned char *name)
{
	struct snd_kcontrol *kctl;

	if (snd_BUG_ON(!card || !name))
		return NULL;
	list_for_each_entry(kctl, &card->controls, list) {
		if (!strncmp(kctl->id.name, name, sizeof(kctl->id.name)))
			return kctl;
	}
	return NULL;
}

static void spdif_rx_ctl_notify(void)
{
	struct snd_kcontrol *kctl;
	struct snd_card *card = spdif_platform->component.card->snd_card;

	kctl = snd_ctl_find_name(card, "SPDIF In");
	if (!kctl) {
		pr_err("%s() can not get name\n", __func__);
		return;
	}
	if (!kctl->id.name) {
		pr_err("%s() can not get name\n", __func__);
		return;
	}
	pr_err("%s() notify to userspace!\n", __func__);
	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);
}

static int spdif_rx_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	/* 0:stop, 1:start opt */
	enum afe_spdifrx_port port =
	(enum afe_spdifrx_port)(ucontrol->value.integer.value[0]);

	spdif_platform = snd_soc_kcontrol_platform(kcontrol);

	if (port != SPDIFRX_PORT_NONE &&
	    port != SPDIFRX_PORT_OPT &&
	    port != SPDIFRX_PORT_ARC)
		return -EINVAL;
	pr_debug("%s() port=%d\n", __func__, port);
	if (port == SPDIFRX_PORT_NONE)
		afe_spdifrx_stop();
	else
		afe_spdifrx_start(port, spdif_rx_ctl_notify);

	return 0;
}

static const struct snd_kcontrol_new mt8167_afe_controls[] = {
	SOC_ENUM_EXT("Audio_SideGen_Switch",
		     mt8167_afe_soc_enums[CTRL_SGEN_EN],
		     mt8167_afe_sgen_get,
		     mt8167_afe_sgen_put),
	SOC_ENUM_EXT("Audio_SideGen_SampleRate",
		     mt8167_afe_soc_enums[CTRL_SGEN_FS],
		     mt8167_afe_sgen_fs_get,
		     mt8167_afe_sgen_fs_put),
	SOC_ENUM_EXT("AP_Loopback_Select",
		     mt8167_afe_soc_enums[CTRL_AP_LOOPBACK],
		     mt8167_afe_ap_loopback_get,
		     mt8167_afe_ap_loopback_put),
	SOC_SINGLE_BOOL_EXT("HDMI_Force_Clk_Switch",
			    0,
			    mt8167_afe_hdmi_force_clk_get,
			    mt8167_afe_hdmi_force_clk_put),
	SOC_SINGLE_BOOL_EXT("TDM_Out_Sgen_Switch",
			    0,
			    mt8167_afe_tdm_out_sgen_get,
			    mt8167_afe_tdm_out_sgen_put),
	SOC_SINGLE_BOOL_EXT("TDM_In_Sgen_Switch",
			    0,
			    mt8167_afe_tdm_in_sgen_get,
			    mt8167_afe_tdm_in_sgen_put),
	SOC_SINGLE_EXT("HW Gain1 Volume",
			    0,
			    0,
			    0x80000,
			    0,
			    mt8167_afe_hw_gain1_vol_get,
			    mt8167_afe_hw_gain1_vol_put),
	SOC_SINGLE_EXT("HW Gain1 SamplePerStep",
			    0,
			    0,
			    255,
			    0,
			    mt8167_afe_hw_gain1_sampleperstep_get,
			    mt8167_afe_hw_gain1_sampleperstep_put),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "SPDIF In",
		.info = spdif_rx_info,
		.get = spdif_rx_get,
	},
		{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "SPDIF In En",
		.info = spdif_rx_en_info,
		.put = spdif_rx_put,
		.get = spdif_rx_en_get,
	},
	SND_SOC_BYTES_EXT("DL1 Timestamp",
			    8,
			    mt8167_afe_dl1_timestamp_get,
			    NULL),
	SND_SOC_BYTES_EXT("HDMI Timestamp",
			    8,
			    mt8167_afe_hdmi_timestamp_get,
			    NULL),
	SOC_SINGLE_BOOL_EXT("Playback State",
			    0,
			    mt8167_afe_playback_state_get,
			    NULL),
	SOC_SINGLE_EXT("Playback State Device Select",
			    0,
			    0,
			    32,
			    0,
			    mt8167_afe_playback_state_device_get,
			    mt8167_afe_playback_state_device_put),
};

#if ENABLE_SPDIFIN_UEVENT_NOTIFY
static void uevent_notify_work(struct work_struct *wq)
{
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(spdif_platform);
	char *envp_ext[NOTIFY_UEVENT_KEY_NUM + 1] = {NULL};

	envp_ext[NOTIFY_UEVENT_KEY_LOCK] = env_ext[NOTIFY_UEVENT_KEY_LOCK];
	envp_ext[NOTIFY_UEVENT_KEY_RATE] = env_ext[NOTIFY_UEVENT_KEY_RATE];
	envp_ext[NOTIFY_UEVENT_KEY_CHSTS] = env_ext[NOTIFY_UEVENT_KEY_CHSTS];
	envp_ext[NOTIFY_UEVENT_KEY_TYPE] = env_ext[NOTIFY_UEVENT_KEY_TYPE];

	kobject_uevent_env(&afe->dev->kobj, KOBJ_CHANGE, envp_ext);
}
#endif

int mt8167_afe_add_controls(struct snd_soc_platform *platform)
{
#if ENABLE_SPDIFIN_UEVENT_NOTIFY
	INIT_WORK(&uevent_work, uevent_notify_work);
#endif
	return snd_soc_add_platform_controls(platform, mt8167_afe_controls,
					     ARRAY_SIZE(mt8167_afe_controls));
}

