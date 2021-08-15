/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/stddef.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/thermal.h>

#include <linux/input/tmp103_temp_sensor.h>
#include <linux/thermal_framework.h>
#include "inc/mtk_ts_bts.h"
#include <dt-bindings/thermal/amazon,virtual_sensor_thermistor.h>

#define VIRTUAL_SENSOR_THERMISTOR_NAME "virtual_sensor_thermistor"

struct mtk_thermistor_info {
	int g_RAP_pull_up_R;
	int g_TAP_over_critical_low;
	int g_RAP_pull_up_voltage;
	int g_RAP_ntc_table;
	int g_RAP_ADC_channel;
	const char *channelName;
	struct platform_device *pdev;
	struct tmp103_temp_sensor *virt_sensor;
};

static BTS_TEMPERATURE *ntc_table_list[] = {
	NULL,
	BTS_Temperature_Table1, /* THERMISTOR_AP_NTC_BL197 */
	BTS_Temperature_Table2, /* THERMISTOR_AP_NTC_TSM_1 */
	BTS_Temperature_Table3, /* THERMISTOR_AP_NTC_10_SEN_1 */
	BTS_Temperature_Table4, /* THERMISTOR_AP_NTC_10 */
	BTS_Temperature_Table5, /* THERMISTOR_AP_NTC_47 */
	BTS_Temperature_Table6, /* THERMISTOR_NTCG104EF104F */
	BTS_Temperature_Table7, /* THERMISTOR_NCP15WF104F03RC */
	BTS_Temperature_Table8, /* THERMISTOR_NCP15XH103 */
	BTS_Temperature_Table9, /* THERMISTOR_NCP03WF104F05RL */
};

/* convert register to temperature  */
static int resistance_to_temp(struct mtk_thermistor_info *info,
			int Res, int *temp, bool *res_in_range)
{
	int i;
	int RES1, RES2 = 0;
	int TMP1, TMP2 = 0;
	BTS_TEMPERATURE *ntc_table = ntc_table_list[info->g_RAP_ntc_table];

	if (Res >= ntc_table[0].TemperatureR) {
		*res_in_range = false;
		*temp = -40;		/* min */
	} else if (Res <= ntc_table[NTC_TABLE_SIZE - 1].TemperatureR) {
		*res_in_range = false;
		*temp = 125;		/* max */
	} else {
		*res_in_range = true;
		RES1 = ntc_table[0].TemperatureR;
		TMP1 = ntc_table[0].BTS_Temp;

		for (i = 0; i < NTC_TABLE_SIZE; i++) {
			if (Res >= ntc_table[i].TemperatureR) {
				RES2 = ntc_table[i].TemperatureR;
				TMP2 = ntc_table[i].BTS_Temp;
				break;
			}
			RES1 = ntc_table[i].TemperatureR;
			TMP1 = ntc_table[i].BTS_Temp;
		}

		*temp = (((Res - RES2) * TMP1) + ((RES1 - Res) * TMP2)) /
								(RES1 - RES2);
	}
	return 0;
}

/* convert ADC_AP_temp_volt to register */
/*Volt to Temp formula same with 6589*/
static int volt_to_temp(struct mtk_thermistor_info *info, int dwVolt,
			int *temp, bool *volt_in_range)
{
	int TRes;
	uint64_t dwVCriAP = 0;

	dwVCriAP = div64_u64(((uint64_t)info->g_TAP_over_critical_low *
			      info->g_RAP_pull_up_voltage),
			     (info->g_TAP_over_critical_low +
			      info->g_RAP_pull_up_R));

	if (dwVolt > dwVCriAP) {
		TRes = info->g_TAP_over_critical_low;
	} else {
		TRes = (info->g_RAP_pull_up_R * dwVolt) /
		(info->g_RAP_pull_up_voltage - dwVolt);
	}
	pr_debug("dwVCriAP=%llu, TRes=%d\n", dwVCriAP, TRes);

	/* convert register to temperature */
	return resistance_to_temp(info, TRes, temp, volt_in_range);
}

static int thermistor_get_temp(struct mtk_thermistor_info *info,
				int *temp, bool *temp_in_range)
{

	int ret, data[4], ret_value, ret_volt;
	int Channel = info->g_RAP_ADC_channel;

	if (IMM_IsAdcInitReady() == 0) {
		pr_debug("[thermal_auxadc_get_data]: AUXADC is not ready\n");
		return -EBUSY;
	}

	ret = IMM_GetOneChannelValue(Channel, data, &ret_value);
	if (!ret) {
		pr_debug("[thermal_auxadc_get_data(AUX_IN0_NTC)]:ret_value=%d\n",
			ret_value);
		ret_volt = ret_value * 1500 / 4096;/* 82's ADC power */
		pr_debug("APtery output mV = %d\n", ret_volt);
		pr_debug("Channel = %d\n", Channel);
		ret = volt_to_temp(info, ret_volt, temp, temp_in_range);
		if (!ret)
			pr_debug("BTS output temperature = %d\n",
								*temp);
		else
			pr_debug("volt_to_temp error(%d)\n", ret);
	}

	return ret;
}

static int mtktsthermistor_read_temp(struct thermal_dev *tdev, int *temp)
{
	struct platform_device *pdev = to_platform_device(tdev->dev);
	struct mtk_thermistor_info *info = platform_get_drvdata(pdev);
	bool temp_in_range;

	return thermistor_get_temp(info, temp, &temp_in_range);
}

static int mtktsthermistor_is_temp_in_range(struct thermal_dev *tdev,
						bool *temp_in_range)
{
	struct platform_device *pdev = to_platform_device(tdev->dev);
	struct mtk_thermistor_info *info = platform_get_drvdata(pdev);
	int temp;

	return thermistor_get_temp(info, &temp, temp_in_range);
}

static struct thermal_dev_ops mtktsthermistor_sensor_fops = {
	.get_temp = mtktsthermistor_read_temp,
	.is_temp_in_range = mtktsthermistor_is_temp_in_range,
};

static ssize_t mtktsthermistor_sensor_show_temp(struct device *dev,
				 struct device_attribute *devattr,
				 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mtk_thermistor_info *info = platform_get_drvdata(pdev);
	int ret, temp = 0;
	bool temp_in_range;

	ret = thermistor_get_temp(info, &temp, &temp_in_range);
	if (!ret)
		return sprintf(buf, "%d\n", temp);
	else
		return sprintf(buf, "Error\n");
}

static DEVICE_ATTR(temp, 0444, mtktsthermistor_sensor_show_temp, NULL);

static int thermistor_parse_channel_info_dt(struct mtk_thermistor_info *info, struct device *dev) {
	int ret = 0;

	ret = of_property_read_u32(dev->of_node, "pull-up-resistance",
				   &info->g_RAP_pull_up_R);
	if (ret)
		goto err;

	pr_info("%s:%d pull-up-resistance = %d\n", __func__, __LINE__, info->g_RAP_pull_up_R);

	ret = of_property_read_u32(dev->of_node, "over-critical-low",
				   &info->g_TAP_over_critical_low);
	if (ret)
		goto err;

	pr_info("%s:%d over-critical-low = %d\n", __func__, __LINE__, info->g_TAP_over_critical_low);

	ret = of_property_read_u32(dev->of_node, "pull-up-voltage",
				   &info->g_RAP_pull_up_voltage);
	if (ret)
		goto err;

	pr_info("%s:%d pull-up-voltage = %d\n", __func__, __LINE__, info->g_RAP_pull_up_voltage);

	ret = of_property_read_u32(dev->of_node, "ntc-table",
				   &info->g_RAP_ntc_table);
	if (ret)
		goto err;

	pr_info("%s:%d ntc-table = %d\n", __func__, __LINE__, info->g_RAP_ntc_table);

	ret = of_property_read_u32(dev->of_node, "auxadc-channel",
				   &info->g_RAP_ADC_channel);
	if (ret)
		goto err;

	pr_info("%s:%d auxadc-channel = %d\n", __func__, __LINE__, info->g_RAP_ADC_channel);

err:
	return ret;
}

static int mtktsthermistor_get_temp(void *data, int *t)
{
	int ret, temp = 0;
	bool temp_in_range;

	ret = thermistor_get_temp(data, &temp, &temp_in_range);
	if (!ret)
		*t = temp * 1000;
	return ret;
}

static struct thermal_zone_of_device_ops mtktsthermistor_sensor_ops = {
	.get_temp = mtktsthermistor_get_temp,
};

static int mtktsthermistor_probe(struct platform_device *pdev)
{
	struct mtk_thermistor_info *info;
	int ret = 0;

	info = devm_kzalloc(&pdev->dev, sizeof(struct mtk_thermistor_info), GFP_KERNEL);
	if (!info) {
		pr_err("%s:%d Could not allocate space for thermistor info\n",
		       __func__, __LINE__);
		return -ENOMEM;
	}

	info->virt_sensor = devm_kzalloc(&pdev->dev, sizeof(struct tmp103_temp_sensor), GFP_KERNEL);
	if (!info->virt_sensor) {
		pr_err("%s:%d Could not allocate space for virtual sensor info\n",
		       __func__, __LINE__);
		kfree(info);
		return -ENOMEM;
	}

	info->pdev = pdev;
	info->channelName = pdev->dev.of_node->name;

	mutex_init(&info->virt_sensor->sensor_mutex);
	info->virt_sensor->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	info->virt_sensor->last_update = jiffies - HZ;

	ret = thermistor_parse_channel_info_dt(info, &pdev->dev);
	if (ret) {
		goto dt_parse_err;
	}

	info->virt_sensor->therm_fw = devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev), GFP_KERNEL);
	if (info->virt_sensor->therm_fw) {
		info->virt_sensor->therm_fw->name = pdev->dev.of_node->name;
		info->virt_sensor->therm_fw->dev = info->virt_sensor->dev;
		info->virt_sensor->therm_fw->dev_ops = &mtktsthermistor_sensor_fops;
		ret = thermal_dev_register(info->virt_sensor->therm_fw);
		if (ret)
			dev_err(&pdev->dev, "%s error registering thermal device\n", __func__);

		if (IS_ERR(thermal_zone_of_sensor_register(&pdev->dev,
				  0,  info, &mtktsthermistor_sensor_ops)))
			pr_err("%s Failed to register sensor\n", __func__);
	} else {
		ret = -ENOMEM;
		goto therm_fw_alloc_err;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_temp);
	if (ret) {
		pr_err("%s Failed to create temp attr\n", __func__);
		goto therm_fw_alloc_err;
	}

	return 0;

dt_parse_err:
therm_fw_alloc_err:
	mutex_destroy(&info->virt_sensor->sensor_mutex);
	return ret;
}

static int mtktsthermistor_remove(struct platform_device *pdev)
{
	struct mtk_thermistor_info *info = platform_get_drvdata(pdev);

	mutex_destroy(&info->virt_sensor->sensor_mutex);
	device_remove_file(&pdev->dev, &dev_attr_temp);
	return 0;
}

static struct of_device_id thermistor_of_match[] = {
	{.compatible = "amazon,virtual_sensor_thermistor", },
	{ },
};

static struct platform_driver mtktsthermistor_driver = {
	.probe = mtktsthermistor_probe,
	.remove = mtktsthermistor_remove,
	.driver = {
		.name  = VIRTUAL_SENSOR_THERMISTOR_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = thermistor_of_match,
#endif
	},
};

static int __init mtktsthermistor_sensor_init(void)
{
	int ret;

	ret = platform_driver_register(&mtktsthermistor_driver);
	if (ret) {
		pr_err("Unable to register mtktsthermistor driver (%d)\n", ret);
		return ret;
	}
	return 0;
}

static void __exit mtktsthermistor_sensor_exit(void)
{
	platform_driver_unregister(&mtktsthermistor_driver);
}

module_init(mtktsthermistor_sensor_init);
module_exit(mtktsthermistor_sensor_exit);
