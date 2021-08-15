/*
 * cupcake.c  --  CUPCAKE ALSA SoC machine driver
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

#include <linux/module.h>
#include <sound/soc.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm_params.h>
#include "../../codecs/tlv320aic3101.h"

#define ENUM_TO_STR(enum) #enum

static unsigned int mt_soc_mclk_freq = AIC31XX_FREQ_2048000;
#define LINEIN_ADC_DIS 0
static int linein_adc_enab = LINEIN_ADC_DIS;

/* HP Ext Amp Switch */
static const struct snd_kcontrol_new cupcake_hp_ext_amp_switch_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

/* HP Spk Amp */
static int cupcake_hp_spk_amp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;

	dev_info(card->dev, "%s snd, event %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* mt8167_evb_ext_hp_amp_turn_on(card); */
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* mt8167_evb_ext_hp_amp_turn_off(card); */
		break;
	default:
		break;
	}

	return 0;
}

/* LINEOUT Ext Amp Switch */
static const struct snd_kcontrol_new cupcake_lineout_ext_amp_switch_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

/* Ext Spk Amp */
static int cupcake_ext_spk_amp_wevent(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;

	dev_info(card->dev, "%s, event %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* mt8167_evb_ext_spk_amp_turn_on(card); */
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* mt8167_evb_ext_spk_amp_turn_off(card); */
		break;
	default:
		break;
	}

	return 0;
}

enum PINCTRL_PIN_STATE {
	PIN_STATE_DEFAULT = 0,
	PIN_STATE_MAX
};

static const char * const cupcake_pinctrl_pin_str[PIN_STATE_MAX] = {
	"default",
};

struct cupcake_priv {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_states[PIN_STATE_MAX];
};

static int tlv3101_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;

	pr_info("aic:%s\n", __func__);
	if (substream == NULL) {
		pr_err("invalid stream parameter\n");
		return -EINVAL;
	}

	rtd = substream->private_data;
	if (rtd == NULL) {
		pr_err("invalid runtime parameter\n");
		return -EINVAL;
	}

	codec_dai = rtd->codec_dai;
	if (codec_dai == NULL) {
		pr_err("invalid dai parameter\n");
		return -EINVAL;
	}

	if (mt_soc_mclk_freq == AIC31XX_FREQ_24576000) {
		snd_soc_dai_set_pll(codec_dai, AIC3101_PLL_BCLK,
			AIC3101_PLL_ADC_FS_CLKIN_MCLK,
			mt_soc_mclk_freq, params_rate(params));
	} else {
		snd_soc_dai_set_pll(codec_dai, 0,
			AIC3101_PLL_ADC_FS_CLKIN_PLL_BCLK,
			mt_soc_mclk_freq, params_rate(params));
	}

	snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_CBS_CFS |
			SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF);

	/* If line in is enabled at adc send new mask. New value only enables
	 * left channel from the 8th adc to propagate. We also have to change
	 * the slot width value. The 8th channel by default is cut off
	 * by the fpga. To counter this we set the offset value to 0.
	 */
	if (linein_adc_enab)
		snd_soc_dai_set_tdm_slot(codec_dai, 0x00, 0x40,
					params_channels(params), 0);
	else
		snd_soc_dai_set_tdm_slot(codec_dai, 0x00, 0x7f,
				params_channels(params),
				snd_pcm_format_width(params_format(params)));

	return 0;
}

static struct snd_soc_ops tlv3101_machine_ops = {
	.hw_params = tlv3101_hw_params,
};

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link cupcake_dais[] = {
	/* Front End DAI links */
	{
		.name = "HDMI",
		.stream_name = "HMDI_PLayback",
		.cpu_dai_name = "HDMI",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "TDM Capture",
		.stream_name = "TDM_Capture",
		.cpu_dai_name = "TDM_IN",
		.codec_name = "tlv320aic3101.0-0018",
		.codec_dai_name = "tlv320aic3101-codec",
		.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.ops = &tlv3101_machine_ops,
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "MultiMedia1 Capture",
		.stream_name = "MultiMedia1_Capture",
		.cpu_dai_name = "VUL",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "AWB Capture",
		.stream_name = "AWB_Record",
		.cpu_dai_name = "AWB",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
#ifdef CONFIG_MTK_BTCVSD_ALSA
	{
		.name = "BTCVSD_RX",
		.stream_name = "BTCVSD_Capture",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "mt-soc-btcvsd-rx-pcm",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
	},
	{
		.name = "BTCVSD_TX",
		.stream_name = "BTCVSD_Playback",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "mt-soc-btcvsd-tx-pcm",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
	},
#endif
	{
		.name = "DL1 Playback",
		.stream_name = "DL1_Playback",
		.cpu_dai_name = "DL1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "Ref In Capture",
		.stream_name = "DL1_AWB_Record",
		.cpu_dai_name = "AWB",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "DL2 Playback",
		.stream_name = "MultiMedia2_PLayback",
		.cpu_dai_name = "DL2",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "DAI Capture",
		.stream_name = "VOIP_Call_BT_Capture",
		.cpu_dai_name = "DAI",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	/* Backend End DAI links */
	{
		.name = "HDMI BE",
		.cpu_dai_name = "HDMIO",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_playback = 1,
	},
	{
		.name = "2ND EXT Codec",
		.cpu_dai_name = "2ND I2S",
		.no_pcm = 1,
		.codec_name = "linux_bt_sco_codec",
		.codec_dai_name = "bt-sco-pcm-wb",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "MTK Codec",
		.cpu_dai_name = "INT ADDA",
		.no_pcm = 1,
		.codec_name = "mt8167-codec",
		.codec_dai_name = "mt8167-codec-dai",
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "TDM IN BE",
		.cpu_dai_name = "TDM_IN_IO",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_capture = 1,
	},
	{
		.name = "I2S BE",
		.cpu_dai_name = "I2S",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_playback = 1,
	},
	{
		.name = "DL BE",
		.cpu_dai_name = "DL Input",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dpcm_capture = 1,
	},
	{
		.name = "HW Gain1 BE",
		.cpu_dai_name = "HW_GAIN1",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dpcm_capture = 1,
	},
};

static const struct snd_soc_dapm_widget cupcake_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("External Line Out"),

	SND_SOC_DAPM_SPK("HP Spk Amp", cupcake_hp_spk_amp_event),
	SND_SOC_DAPM_SWITCH("HP Ext Amp",
		SND_SOC_NOPM, 0, 0, &cupcake_hp_ext_amp_switch_ctrl),

	SND_SOC_DAPM_SPK("Ext Spk Amp", cupcake_ext_spk_amp_wevent),
	SND_SOC_DAPM_SWITCH("LINEOUT Ext Amp",
		SND_SOC_NOPM, 0, 0, &cupcake_lineout_ext_amp_switch_ctrl),
};

static const struct snd_soc_dapm_route cupcake_audio_map[] = {
	{"External Line Out", NULL, "I2S Playback"},

	/* use external spk amp via AU_LOL */
	{"LINEOUT Ext Amp", "Switch", "AU_LOL"},
	{"Ext Spk Amp", NULL, "LINEOUT Ext Amp"},

	/* use external spk amp via AU_HPL/AU_HPR */
	{"HP Ext Amp", "Switch", "AU_HPL"},
	{"HP Ext Amp", "Switch", "AU_HPR"},

	{"HP Spk Amp", NULL, "HP Ext Amp"},
	{"HP Spk Amp", NULL, "HP Ext Amp"},

	/* ADDA clock - Uplink */
	{"AIF TX", NULL, "AFE_CLK"},
	{"AIF TX", NULL, "AD_CLK"},

	/* ADDA clock - Downlink */
	{"AIF RX", NULL, "AFE_CLK"},
	{"AIF RX", NULL, "DA_CLK"},
};

static struct snd_soc_card cupcake_card = {
	.name = "mt-snd-card",
	.owner = THIS_MODULE,
	.dai_link = cupcake_dais,
	.num_links = ARRAY_SIZE(cupcake_dais),
	.dapm_widgets = cupcake_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cupcake_dapm_widgets),
	.dapm_routes = cupcake_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cupcake_audio_map),
};

static int cupcake_gpio_probe(struct snd_soc_card *card)
{
	struct cupcake_priv *card_data;
	int ret = 0;
	int i;

	dev_info(card->dev, "%s+ snd\n", __func__);

	card_data = snd_soc_card_get_drvdata(card);

	card_data->pinctrl = devm_pinctrl_get(card->dev);
	if (IS_ERR(card_data->pinctrl)) {
		ret = PTR_ERR(card_data->pinctrl);
		dev_err(card->dev, "%s pinctrl_get failed %d\n",
			__func__, ret);
		goto exit;
	}

	for (i = 0 ; i < PIN_STATE_MAX ; i++) {
		card_data->pin_states[i] =
			pinctrl_lookup_state(card_data->pinctrl,
				cupcake_pinctrl_pin_str[i]);
		if (IS_ERR(card_data->pin_states[i])) {
			ret = PTR_ERR(card_data->pin_states[i]);
			dev_warn(card->dev, "%s Can't find pinctrl state %s %d\n",
				__func__, cupcake_pinctrl_pin_str[i], ret);
		}
	}

	/* default state */
	if (!IS_ERR(card_data->pin_states[PIN_STATE_DEFAULT])) {
		ret = pinctrl_select_state(card_data->pinctrl,
				card_data->pin_states[PIN_STATE_DEFAULT]);
		if (ret) {
			dev_err(card->dev, "%s failed to select state %d\n",
				__func__, ret);
			goto exit;
		}
	}

exit:

	dev_info(card->dev, "%s- snd %d\n", __func__, ret);
	return ret;
}

static int cupcake_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &cupcake_card;
	struct device_node *platform_node;
	struct device_node *codec_node;
	int ret, i;
	struct cupcake_priv *card_data;

	dev_info(card->dev, "%s+ snd\n", __func__);

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}

	for (i = 0; i < card->num_links; i++) {
		if (cupcake_dais[i].platform_name)
			continue;
		cupcake_dais[i].platform_of_node = platform_node;
	}

	codec_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,audio-codec", 0);
	if (codec_node) {
		for (i = 0; i < card->num_links; i++) {
			if (cupcake_dais[i].codec_name)
				continue;
			cupcake_dais[i].codec_of_node = codec_node;
		}
	}

	card->dev = &pdev->dev;

	card_data = devm_kzalloc(&pdev->dev,
		sizeof(struct cupcake_priv), GFP_KERNEL);
	if (!card_data) {
		ret = -ENOMEM;
		dev_err(&pdev->dev,
			"%s allocate card private data fail %d\n",
			__func__, ret);
		return ret;
	}

	snd_soc_card_set_drvdata(card, card_data);

	cupcake_gpio_probe(card);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
		__func__, ret);
		return ret;
	}

	dev_info(card->dev, "%s- snd %d\n", __func__, ret);
	return ret;
}

static const struct of_device_id cupcake_dt_match[] = {
	{ .compatible = "mediatek,mt8516-soc-card", },
	{ }
};
MODULE_DEVICE_TABLE(of, cupcake_dt_match);

static struct platform_driver cupcake_mach_driver = {
	.driver = {
		   .name = "mt8516-soc-card",
		   .of_match_table = cupcake_dt_match,
#ifdef CONFIG_PM
		   .pm = &snd_soc_pm_ops,
#endif
	},
	.probe = cupcake_dev_probe,
};

module_platform_driver(cupcake_mach_driver);

/* Module information */
MODULE_DESCRIPTION("MT8516 Cupcake ALSA SoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mt8516-soc-card");

