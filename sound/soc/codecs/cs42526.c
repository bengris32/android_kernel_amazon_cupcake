/*
 * cs42526.c -- CS42526 driver
 *
 * Copyright 2018 Amazon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "cs42526.h"

enum {
	CS42526_MODEX_RXP_INPUT = 0x0,
	CS42526_MODEX_MUTE_MODE = 0x1,
	CS42526_MODEX_GPO_DRIVE_LOW = 0x2,
	CS42526_MODEX_GPO_DRIVE_HIGH = 0x3
};

/*
 * Initial steps in codec probe
 */
static struct reg_default init_list[] = {
	{ CS42526_PWRCTL, 0x6F },
	{ CS42526_FUNC_MODE, 0x08 },
	{ CS42526_IF_FORMAT, 0x40 },
	{ CS42526_MISC_CTL, 0x00 },
	{ CS42526_CLK_CTL, 0x02 },
	{ CS42526_VTR_CTL, 0x20},
	{ CS42526_MUTEC_CTR, 0x20 },
	{ CS42526_PWRCTL, 0x40 }
};

/* Power-on default values for the registers
 *
 * This array contains the power-on default values of the registers, with the
 * exception of the "CHIPID" register (01h).  The lower four bits of that
 * register contain the hardware revision, so it is treated as volatile.
 */
static const struct reg_default cs42526_reg_defaults[] = {
	{ CS42526_PWRCTL, 0x81 },
	{ CS42526_FUNC_MODE, 0x00},
	{ CS42526_IF_FORMAT, 0x40 },
	{ CS42526_MISC_CTL, 0x0 },
	{ CS42526_CLK_CTL, 0x0 },
	{ CS42526_VOLUME_CTRL_A1, 0x0 },
	{ CS42526_VOLUME_CTRL_B1, 0x0 },
	{ CS42526_VOLUME_CTRL_A2, 0x0 },
	{ CS42526_VOLUME_CTRL_B2, 0x0 },
	{ CS42526_VOLUME_CTRL_A3, 0x0 },
	{ CS42526_VOLUME_CTRL_B3, 0x0 },
	{ CS42526_ADC_LEFT_CH_GAIN, 0x0 },
	{ CS42526_ADC_RIGHT_CH_GAIN, 0x0 },
};

struct cs42526_private {
	struct regmap *regmap;
	unsigned int mclk; /* Input frequency of the MCLK pin */
	unsigned int mode; /* The mode (I2S or left-justified) */
	unsigned int slave_mode;
	unsigned int adc_left_gain_idx;
	unsigned int adc_right_gain_idx;
	unsigned int rxp_gpo_mod_ctl[CS42526_RXP_GPO_NUM];
	struct regulator *audio_regulator;
	int reset_gpio;
	unsigned int init_only;
	bool reinit;
};

static const struct snd_soc_dapm_widget cs42526_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_INPUT("LINEINL"),
	SND_SOC_DAPM_INPUT("LINEINR"),
};

static const struct snd_soc_dapm_route cs42526_dapm_routes[] = {
	{ "OUT", NULL, "Playback" },
	{ "Capture", NULL, "LINEINL" },
	{ "Capture", NULL, "LINEINR" },
};

#define CS42526_RATES SNDRV_PCM_RATE_48000

#define CS42526_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)

static bool cs42526_reg_is_readable(struct device *dev, unsigned int reg)
{
	return (reg >= 0) && (reg <= CS42526_MAX_REGISTER);
}

static bool cs42526_reg_is_volatile(struct device *dev, unsigned int reg)
{
	/* Unreadable registers are considered volatile */
	if ((reg < 0) || (reg > CS42526_MAX_REGISTER))
		return 1;

	return reg == CS42526_CHIP_ID;
}

static int cs42526_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "%s: freq=%d", __func__, freq);

	cs42526->mclk = freq;
	return 0;
}

static int cs42526_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;

	dev_info(codec->dev, "%s: dai fmt\n", __func__);
	return 0;
}

static int cs42526_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);

	int width = params_width(params);
	int stream_channels = params_channels(params);
	int i, ret;

	dev_info(codec->dev, "%s stream_ch %d width %d dir %d reinit %d\n",
		__func__, stream_channels, width, substream->stream,
		cs42526->reinit);

	if (cs42526->init_only)
		return 0;

	if (cs42526->reinit == false)
		return 0;

	for (i = 0; i < sizeof(init_list) / sizeof(struct reg_default); i++) {
		ret = snd_soc_write(codec, init_list[i].reg, init_list[i].def);
		if (ret < 0) {
			dev_err(codec->dev, "i2c write failed reg: 0x%x val: 0x%x\n",
				init_list[i].reg, init_list[i].def);
		}
	}

	cs42526->reinit = false;

	return 0;
}

/*
 * DAC volume control.
 * -127.5 dB, 0.5dB per step
 * -127.5 dB is actually mute
 */
static DECLARE_TLV_DB_SCALE(volume_tlv, -12750, 50, 0);

/*
 * ADC gain control. -15 dB, 1 dB per step
 */
static DECLARE_TLV_DB_SCALE(adc_tlv, -1500, 100, 0);

static int cs42526_adc_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = cs42526->adc_left_gain_idx;
	ucontrol->value.integer.value[1] = cs42526->adc_right_gain_idx;
	return 0;
}

static int cs42526_adc_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	unsigned int sel_left = ucontrol->value.integer.value[0];
	unsigned int sel_right = ucontrol->value.integer.value[1];

	/*
	 * Compliment code: negative value in reg
	 * -15 (0xf1), -14 (0xf2), -13 (0xf3), ......, 0, 1, 2, ...... 15
	 */
	unsigned int compliment[31] = {
		0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
		0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc,
		0xfd, 0xfe, 0xff, 0, 1, 2, 3, 4,
		5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
		};

	if (sel_left > mc->max || sel_right > mc->max)
		return -EINVAL;

	cs42526->adc_left_gain_idx = sel_left;
	cs42526->adc_right_gain_idx = sel_right;

	snd_soc_update_bits(codec, mc->reg, CS42526_ADC_GAIN_MASK,
		compliment[sel_left] << mc->shift);
	snd_soc_update_bits(codec, mc->rreg, CS42526_ADC_GAIN_MASK,
		compliment[sel_right] << mc->rshift);

	return 0;
}

static const char * const rxp_gpo_mode_ctrl[] = {
	"RXP Input", "Mute", "GPO Drive Low", "GPO Drive High"
};

static const struct soc_enum cs42526_rxp_gpo_mode_ctrl_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rxp_gpo_mode_ctrl),
		rxp_gpo_mode_ctrl);

static int rxp_gpo_mode_ctrl_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol, unsigned int index)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);

	if (index >= CS42526_RXP_GPO_NUM) {
		dev_err(codec->dev,
			"%s: rxp_gpo_mode_ctrl_put. Invalue index =%d\n",
			__func__, index);
		return -EINVAL;
	}

	dev_info(codec->dev, "%s RXP/GPO mode=%d previous_mode=%d\n",
		__func__, ucontrol->value.enumerated.item[0],
		cs42526->rxp_gpo_mod_ctl[index]);

	switch (ucontrol->value.enumerated.item[0]) {
	case CS42526_MODEX_RXP_INPUT:
	case CS42526_MODEX_MUTE_MODE:
	case CS42526_MODEX_GPO_DRIVE_LOW:
	case CS42526_MODEX_GPO_DRIVE_HIGH:
		break;
	default:
		dev_err(codec->dev,
			"%s: rxp_gpo_mode_ctrl_put. Value =%d\n",
			__func__, ucontrol->value.enumerated.item[0]);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, CS42526_RXP_GPO_1 - index,
		CS42526_RXP_GPO_MOD_CTL_MASK,
		ucontrol->value.enumerated.item[0]
			<< CS42526_RXP_GPO_MOD_CTL_SHIFT);

	cs42526->rxp_gpo_mod_ctl[index] = ucontrol->value.enumerated.item[0];

	return 0;
}

static int rxp_gpo_mode_ctrl_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol, unsigned int index)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);

	if (index >= CS42526_RXP_GPO_NUM) {
		dev_err(codec->dev,
			"%s: rxp_gpo_mode_ctrl_put. Invalue index =%d\n",
			__func__, index);
		return -EINVAL;
	}

	dev_info(codec->dev, "%s rxp_gpo_mode_ctrl_get = %d\n",
		__func__, cs42526->rxp_gpo_mod_ctl[index]);

	ucontrol->value.enumerated.item[0] = cs42526->rxp_gpo_mod_ctl[index];

	return 0;
}

static int rxp_gpo_mode_ctrl_put1(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_put(kcontrol, ucontrol, 0);
}

static int rxp_gpo_mode_ctrl_get1(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_get(kcontrol, ucontrol, 0);
}

static int rxp_gpo_mode_ctrl_put2(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_put(kcontrol, ucontrol, 1);

}

static int rxp_gpo_mode_ctrl_get2(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_get(kcontrol, ucontrol, 1);
}

static int rxp_gpo_mode_ctrl_put3(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_put(kcontrol, ucontrol, 2);
}

static int rxp_gpo_mode_ctrl_get3(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_get(kcontrol, ucontrol, 2);
}

static int rxp_gpo_mode_ctrl_put4(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_put(kcontrol, ucontrol, 3);
}

static int rxp_gpo_mode_ctrl_get4(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_get(kcontrol, ucontrol, 3);
}

static int rxp_gpo_mode_ctrl_put5(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_put(kcontrol, ucontrol, 4);
}

static int rxp_gpo_mode_ctrl_get5(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_get(kcontrol, ucontrol, 4);
}

static int rxp_gpo_mode_ctrl_put6(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_put(kcontrol, ucontrol, 5);
}

static int rxp_gpo_mode_ctrl_get6(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_get(kcontrol, ucontrol, 5);
}

static int rxp_gpo_mode_ctrl_put7(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_put(kcontrol, ucontrol, 6);
}

static int rxp_gpo_mode_ctrl_get7(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol) {
	return rxp_gpo_mode_ctrl_get(kcontrol, ucontrol, 6);
}

static int cs42526_init_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = cs42526->reinit;
	return 0;
}

static int cs42526_init_set(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "%s: reinit: before %d now %d\n", __func__,
			cs42526->reinit, (int)ucontrol->value.integer.value[0]);

	cs42526->reinit = (ucontrol->value.integer.value[0] == 1);

	return 0;
}

static const struct snd_kcontrol_new cs42526_snd_controls[] = {
	SOC_DOUBLE_R_TLV("CS42526 Volume Control AOUT1",
		CS42526_VOLUME_CTRL_A1, CS42526_VOLUME_CTRL_B1,
		0, 0xff, 1, volume_tlv),
	SOC_DOUBLE_R_TLV("CS42526 Volume Control AOUT2",
		CS42526_VOLUME_CTRL_A2, CS42526_VOLUME_CTRL_B2,
		0, 0xff, 1, volume_tlv),
	SOC_DOUBLE_R_TLV("CS42526 Volume Control AOUT3",
		CS42526_VOLUME_CTRL_A3, CS42526_VOLUME_CTRL_B3,
		0, 0xff, 1, volume_tlv),
	SOC_DOUBLE_R_EXT_TLV("CS42526 ADC Gain",
		CS42526_ADC_LEFT_CH_GAIN, CS42526_ADC_RIGHT_CH_GAIN,
		0, 0x1e, 0, cs42526_adc_get, cs42526_adc_put, adc_tlv),
	SOC_ENUM_EXT("CS42526 RXP GPO Mode Ctrl 1",
		cs42526_rxp_gpo_mode_ctrl_enum,
		rxp_gpo_mode_ctrl_get1, rxp_gpo_mode_ctrl_put1),
	SOC_ENUM_EXT("CS42526 RXP GPO Mode Ctrl 2",
		cs42526_rxp_gpo_mode_ctrl_enum,
		rxp_gpo_mode_ctrl_get2, rxp_gpo_mode_ctrl_put2),
	SOC_ENUM_EXT("CS42526 RXP GPO Mode Ctrl 3",
		cs42526_rxp_gpo_mode_ctrl_enum,
		rxp_gpo_mode_ctrl_get3, rxp_gpo_mode_ctrl_put3),
	SOC_ENUM_EXT("CS42526 RXP GPO Mode Ctrl 4",
		cs42526_rxp_gpo_mode_ctrl_enum,
		rxp_gpo_mode_ctrl_get4, rxp_gpo_mode_ctrl_put4),
	SOC_ENUM_EXT("CS42526 RXP GPO Mode Ctrl 5",
		cs42526_rxp_gpo_mode_ctrl_enum,
		rxp_gpo_mode_ctrl_get5, rxp_gpo_mode_ctrl_put5),
	SOC_ENUM_EXT("CS42526 RXP GPO Mode Ctrl 6",
		cs42526_rxp_gpo_mode_ctrl_enum,
		rxp_gpo_mode_ctrl_get6, rxp_gpo_mode_ctrl_put6),
	SOC_ENUM_EXT("CS42526 RXP GPO Mode Ctrl 7",
		cs42526_rxp_gpo_mode_ctrl_enum,
		rxp_gpo_mode_ctrl_get7, rxp_gpo_mode_ctrl_put7),
	SOC_SINGLE("CS42526 SHUTDOWN", CS42526_PWRCTL, 0, 1, 0),
	SOC_SINGLE_BOOL_EXT("CS42526 Init", 0,
		cs42526_init_get, cs42526_init_set),
};

static const struct snd_soc_dai_ops cs42526_dai_ops = {
	.hw_params = cs42526_hw_params,
	.set_sysclk = cs42526_set_dai_sysclk,
	.set_fmt = cs42526_set_dai_fmt,
};

static struct snd_soc_dai_driver cs42526_dai[] = {
	{
		.name = "cs42526-dai",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS42526_RATES,
			.formats = CS42526_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS42526_RATES,
			.formats = CS42526_FORMATS,
		},
		.ops = &cs42526_dai_ops,
	},
};

static int cs42526_probe(struct snd_soc_codec *codec)
{
	int ret;
	int i;

	dev_info(codec->dev, "+ %s enter\n", __func__);

	for (i = 0; i < sizeof(init_list) / sizeof(struct reg_default); i++) {
		ret = snd_soc_write(codec, init_list[i].reg, init_list[i].def);
		if (ret < 0) {
			dev_err(codec->dev, "i2c write failed reg: 0x%x val: 0x%x\n",
				init_list[i].reg, init_list[i].def);
			return ret;
		}
	}

	dev_info(codec->dev, "- %s leave\n", __func__);

	return 0;
}

static int cs42526_remove(struct snd_soc_codec *codec)
{
	return 0;
};

static int cs42526_suspend(struct snd_soc_codec *codec)
{
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);
	int ret;

	if (gpio_is_valid(cs42526->reset_gpio)) {
		gpio_set_value_cansleep(cs42526->reset_gpio, 0);
	}

	if (!IS_ERR(cs42526->audio_regulator)) {
		ret = regulator_disable(cs42526->audio_regulator);
		if (ret) {
			dev_err(codec->dev, "%s failed to disable audio regulator(%d)",
				__func__, ret);
			return ret;
		}
	}
	return 0;
};

static int cs42526_resume(struct snd_soc_codec *codec)
{
	struct cs42526_private *cs42526 = snd_soc_codec_get_drvdata(codec);
	int ret;

	if (!IS_ERR(cs42526->audio_regulator)) {
		ret = regulator_enable(cs42526->audio_regulator);
		if (ret) {
			dev_err(codec->dev, "%s failed to enable audio regulator(%d)\n",
				__func__, ret);
			return ret;
		}
	}

	if (gpio_is_valid(cs42526->reset_gpio)) {
		gpio_set_value_cansleep(cs42526->reset_gpio, 1);
	}
	return 0;
};

static int cs42526_set_bias_level(struct snd_soc_codec *codec,
					enum snd_soc_bias_level level)
{
	dev_info(codec->dev, "+ %s level =%x\n", __func__, level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}
	return 0;
}

static const struct snd_soc_codec_driver soc_codec_cs42526 = {
	.probe = cs42526_probe,
	.remove = cs42526_remove,

	.set_bias_level = cs42526_set_bias_level,
	.controls = cs42526_snd_controls,
	.num_controls = ARRAY_SIZE(cs42526_snd_controls),
	.dapm_widgets = cs42526_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs42526_dapm_widgets),
	.dapm_routes = cs42526_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(cs42526_dapm_routes),
	.suspend = cs42526_suspend,
	.resume = cs42526_resume,
};

static const struct regmap_config cs42526_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = CS42526_MAX_REGISTER,
	.reg_defaults = cs42526_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs42526_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.readable_reg = cs42526_reg_is_readable,
	.volatile_reg = cs42526_reg_is_volatile,
};

static int cs42526_i2c_probe(struct i2c_client *i2c_client,
			     const struct i2c_device_id *id)
{
	struct device_node *np = i2c_client->dev.of_node;
	struct cs42526_private *cs42526;
	int ret = 0;
	unsigned int devid = 0;
	unsigned int reg, val32;
	int i;

	dev_info(&i2c_client->dev, "+ %s enter\n", __func__);

	cs42526 = devm_kzalloc(&i2c_client->dev, sizeof(struct cs42526_private),
			       GFP_KERNEL);
	if (cs42526 == NULL)
		return -ENOMEM;

	cs42526->audio_regulator = devm_regulator_get(&i2c_client->dev, "audio-regulator");
	if (IS_ERR(cs42526->audio_regulator)) {
		dev_warn(&i2c_client->dev, "failed to get audio regulator\n");
	} else {
		ret = regulator_enable(cs42526->audio_regulator);
		if (ret) {
			dev_err(&i2c_client->dev, "failed to regulator_enable(%d)\n", ret);
			return ret;
		}
	}
	/*
	 * Bring the CODEC out of reset
	 */
	if (np) {
		cs42526->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);

		if (gpio_is_valid(cs42526->reset_gpio)) {
			ret = devm_gpio_request_one(
				&i2c_client->dev, cs42526->reset_gpio,
					GPIOF_OUT_INIT_HIGH, "CS42526 /RST");
			if (ret < 0) {
				dev_err(&i2c_client->dev,
					"Failed to request /RST %d: %d\n",
					cs42526->reset_gpio, ret);
				return ret;
			}
			gpio_set_value_cansleep(cs42526->reset_gpio, 0);
			msleep(50);
			gpio_set_value_cansleep(cs42526->reset_gpio, 1);
			msleep(50);
		}
	}

	cs42526->regmap = devm_regmap_init_i2c(i2c_client, &cs42526_regmap);
	if (IS_ERR(cs42526->regmap)) {
		ret = PTR_ERR(cs42526->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	/* Verify that we have a cs42526 */
	ret = regmap_read(cs42526->regmap, CS42526_CHIP_ID, &reg);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "failed to read i2c at addr %X\n",
		       i2c_client->addr);
		return ret;
	}
	devid = reg & CS42526_CHIP_ID_MASK;
	if (devid != CS42526_CHIP_ID_VAL) {
		ret = -ENODEV;
		dev_err(&i2c_client->dev,
			"CS42526 Device ID (%X). Expected %X\n",
			devid, CS42526_CHIP_ID);
		return ret;
	}
	dev_info(&i2c_client->dev,
		"CS42526 Version %x\n",
			reg & CS42526_REV_ID_MASK);

	/* The initial index value for adc gain after reboot, 0 dB */
	cs42526->adc_left_gain_idx = 15;
	cs42526->adc_right_gain_idx = 15;

	/* The initial value after reboot, RXP Input */
	for (i = 0; i < CS42526_RXP_GPO_NUM; i++)
		cs42526->rxp_gpo_mod_ctl[i] = CS42526_MODEX_RXP_INPUT;

	if (of_property_read_u32(np, "adc-sp-selx", &val32) >= 0)
		for (i = 0;
			i < sizeof(init_list) / sizeof(struct reg_default);
			i++) {
			if (init_list[i].reg == CS42526_FUNC_MODE) {
				init_list[i].def &=
					~(CS42526_FUNC_MODE_ADC_SP_SELX_MASK <<
					CS42526_FUNC_MODE_ADC_SP_SELX_SHIFT);
				init_list[i].def |= (val32 <<
					CS42526_FUNC_MODE_ADC_SP_SELX_SHIFT);
				break;
			}
		}

	if (of_property_read_u32(np, "sai-sp-master", &val32) >= 0)
		for (i = 0;
			i < sizeof(init_list) / sizeof(struct reg_default);
			i++) {
			if (init_list[i].reg == CS42526_MISC_CTL) {
				init_list[i].def &=
					~(CS42526_MISC_CTL_SAI_SP_MS_MASK <<
					CS42526_MISC_CTL_SAI_SP_MS_SHIFT);
				init_list[i].def |= (val32 <<
					CS42526_MISC_CTL_SAI_SP_MS_SHIFT);
				break;
			}
		}

	if (of_property_read_u32(np, "init-only", &val32) >= 0)
		cs42526->init_only = val32;

	i2c_set_clientdata(i2c_client, cs42526);

	ret =  snd_soc_register_codec(&i2c_client->dev,
			&soc_codec_cs42526, cs42526_dai,
			ARRAY_SIZE(cs42526_dai));

	dev_info(&i2c_client->dev, "- %s leave, ret %d\n", __func__, ret);

	return ret;
}

static int cs42526_i2c_remove(struct i2c_client *i2c_client)
{
	snd_soc_unregister_codec(&i2c_client->dev);
	return 0;
}

static const struct of_device_id cs42526_of_match[] = {
	{ .compatible = "cirrus,cs42526", },
	{ }
};
MODULE_DEVICE_TABLE(of, cs42526_of_match);

static const struct i2c_device_id cs42526_id[] = {
	{ "cs42526", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs42526_id);

static struct i2c_driver cs42526_i2c_driver = {
	.driver = {
		.name = "cs42526",
		.of_match_table = cs42526_of_match,
	},
	.id_table = cs42526_id,
	.probe =    cs42526_i2c_probe,
	.remove =   cs42526_i2c_remove,
};

module_i2c_driver(cs42526_i2c_driver);

MODULE_DESCRIPTION("Cirrus Logic CS42526 Codec driver");
MODULE_LICENSE("GPL");
