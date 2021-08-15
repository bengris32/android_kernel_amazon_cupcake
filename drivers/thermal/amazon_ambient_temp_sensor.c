/*
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/thermal_framework.h>
#include <linux/cpufreq.h>
#include <thermal_core.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/types.h>
#include "cust_temp.h"
#include "temperature.h"

#define AMBIENT_TEMP_VIRTUAL_SENSOR_NAME "amazon_ambient_virtual_sensor"
#define AMBIENT_TEMP_VIRTUAL_SENSOR_NUM 1
#define AMBIENT_TEMP_VENDOR_DIV 1000

/**
 * struct ambient_temp_sensor_info  - Structure for ambient temp sensor info
 * @pdev: Platform device ptr
 * @tdev: Thermal zone device ptr
 * @ambient_temp: ambient temp
 * @lock: Mutex lock
 */
struct ambient_temp_sensor_info {
	struct platform_device *pdev;
	struct thermal_dev *tdev;
	int ambient_temp;
	struct mutex lock;
};
static struct ambient_temp_sensor_info *g_info;

static int ambient_temp_get_temp(struct ambient_temp_sensor_info *info)
{
	int value;

	mutex_lock(&info->lock);
	value = info->ambient_temp;
	mutex_unlock(&info->lock);

	return value;
}

#ifdef CONFIG_CUSTOM_KERNEL_TEMPERATURE

static int ambient_temp_sensor_mtk_init(void);
static int ambient_temp_sensor_mtk_uninit(void);


static struct temp_init_info ambient_temp_init_info = {
	.name = AMBIENT_TEMP_VIRTUAL_SENSOR_NAME,
	.init = ambient_temp_sensor_mtk_init,
	.uninit = ambient_temp_sensor_mtk_uninit
};

static int ambient_temp_open_report_data(int open)
{
	return 0;
}

static int ambient_temp_enable_nodata(int en)
{
	return 0;
}

static int ambient_temp_set_delay(u64 ns)
{
	return 0;
}

static int ambient_temp_get_data(int *value, int *status)
{
	*value = ambient_temp_get_temp(g_info);
	*status = SENSOR_STATUS_ACCURACY_MEDIUM;
	return 0;
}

static int ambient_temp_sensor_mtk_init(void)
{
	struct temp_control_path ctl = { 0 };
	struct temp_data_path data = { 0 };
	int err;

	ctl.is_use_common_factory = false;
	ctl.open_report_data = ambient_temp_open_report_data;
	ctl.enable_nodata = ambient_temp_enable_nodata;
	ctl.set_delay = ambient_temp_set_delay;
	ctl.is_report_input_direct = false;
	ctl.is_support_batch = false;

	err = temp_register_control_path(&ctl);
	if (err) {
		pr_err("%s:%d: register temp control path err\n", __func__,
		       __LINE__);
		return -1;
	}

	data.get_data = ambient_temp_get_data;
	data.vender_div = AMBIENT_TEMP_VENDOR_DIV;
	err = temp_register_data_path(&data);
	if (err) {
		pr_err("%s:%d: temp_register_data_path failed, err = %d\n",
		       __func__, __LINE__, err);
		return -1;
	}
	err = batch_register_support_info(ID_TEMPRERATURE, false,
					  data.vender_div, 0);
	if (err) {
		pr_err("%s:%d: register temp batch support err = %d\n",
		       __func__, __LINE__, err);
		return -1;
	}

	return 0;
}

static int ambient_temp_sensor_mtk_uninit(void)
{
	return 0;
}

#endif

static int ambient_temp_sensor_read_temp(struct thermal_dev *tdev, int *temp)
{
	struct platform_device *pdev = to_platform_device(tdev->dev);
	struct ambient_temp_sensor_info *info = platform_get_drvdata(pdev);

	*temp = ambient_temp_get_temp(info);
	return 0;
}

static ssize_t _show_temp(struct device *dev,
				 struct device_attribute *devattr,
				 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ambient_temp_sensor_info *info = platform_get_drvdata(pdev);

	if (!info)
		return -EINVAL;

	return sprintf(buf, "%d", info->ambient_temp);
}

static ssize_t _store_temp(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ambient_temp_sensor_info *info = platform_get_drvdata(pdev);
	int ret = -EINVAL;

	if (!info)
		return ret;

	mutex_lock(&info->lock);
	if (sscanf(buf, "%d", &info->ambient_temp) == 1)
		ret = count;
	mutex_unlock(&info->lock);
	return ret;
}

static DEVICE_ATTR(temp, 0644, _show_temp, _store_temp);

static struct thermal_dev_ops ambient_temp_sensor_ops = {
	.get_temp = ambient_temp_sensor_read_temp,
};

static int ambient_temp_sensor_probe(struct platform_device *pdev)
{
	struct ambient_temp_sensor_info *info;
	int ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "%s: Error no of_node\n", __func__);
		return -EINVAL;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(struct ambient_temp_sensor_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "%s:%d Could not allocate space for ambient temp sensor info\n",
		       __func__, __LINE__);
		return -ENOMEM;
	}

	g_info = info;
	info->pdev = pdev;

	info->tdev = devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev), GFP_KERNEL);
	if (!info->tdev) {
		dev_err(&pdev->dev, "%s:%d Could not allocate space for ambient thermal dev\n",
		       __func__, __LINE__);
		return -ENOMEM;
	}

	info->tdev->name = pdev->dev.of_node->name;
	info->tdev->dev = &pdev->dev;
	info->tdev->vs = AMBIENT_TEMP_VIRTUAL_SENSOR_NUM;
	info->tdev->dev_ops = &ambient_temp_sensor_ops;

	ret = thermal_dev_register(info->tdev);
	if (ret)
		dev_err(&pdev->dev, "%s error registering thermal device\n", __func__);

	ret = device_create_file(&pdev->dev, &dev_attr_temp);
	if (ret)
		pr_err("%s Failed to create temp attr\n", __func__);

	mutex_init(&info->lock);
	dev_set_drvdata(&pdev->dev, info);

#ifdef CONFIG_CUSTOM_KERNEL_TEMPERATURE
	temp_driver_add(&ambient_temp_init_info);
#endif

	return 0;
}

static int ambient_temp_sensor_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_temp);
	return 0;
}

static struct of_device_id sensor_of_match[] = {
	{.compatible = "amazon,ambient_temp_virtual_sensor", },
	{ },
};

static struct platform_driver ambient_temp_sensor_driver = {
	.probe = ambient_temp_sensor_probe,
	.remove = ambient_temp_sensor_remove,
	.driver = {
		.name  = AMBIENT_TEMP_VIRTUAL_SENSOR_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sensor_of_match,
	},
};

static int __init ambient_temp_sensor_init(void)
{
	int ret;

	ret = platform_driver_register(&ambient_temp_sensor_driver);
	if (ret) {
		pr_err("Unable to register ambient temp sensor driver (%d)\n", ret);
		return ret;
	}

	return 0;
}

static void __exit ambient_temp_sensor_exit(void)
{
	platform_driver_unregister(&ambient_temp_sensor_driver);
}

module_init(ambient_temp_sensor_init);
module_exit(ambient_temp_sensor_exit);

MODULE_DESCRIPTION("AMAZON ambient temp sensor driver");
MODULE_AUTHOR("Tarun Karra <tkkarra@amazon.com>");
MODULE_LICENSE("GPL");
