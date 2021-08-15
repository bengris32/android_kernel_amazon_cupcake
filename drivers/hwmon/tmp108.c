/* Texas Instruments TMP108 SMBus temperature sensor driver
 *
 * Copyright (C) 2016 John Muir <john@jmuir.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define	DRIVER_NAME "tmp108"

#define	TMP108_REG_TEMP		0x00
#define	TMP108_REG_CONF		0x01
#define	TMP108_REG_TLOW		0x02
#define	TMP108_REG_THIGH	0x03

#define TMP108_TEMP_MIN_MC	-50000 /* Minimum millicelcius. */
#define TMP108_TEMP_MAX_MC	127937 /* Maximum millicelcius. */

/* Configuration register bits.
 * Note: these bit definitions are byte swapped.
 */
#define TMP108_CONF_M0		0x0100 /* Sensor mode. */
#define TMP108_CONF_M1		0x0200
#define TMP108_CONF_TM		0x0400 /* Thermostat mode. */
#define TMP108_CONF_FL		0x0800 /* Watchdog flag - TLOW */
#define TMP108_CONF_FH		0x1000 /* Watchdog flag - THIGH */
#define TMP108_CONF_CR0		0x2000 /* Conversion rate. */
#define TMP108_CONF_CR1		0x4000
#define TMP108_CONF_ID		0x8000
#define TMP108_CONF_HYS0	0x0010 /* Hysteresis. */
#define TMP108_CONF_HYS1	0x0020
#define TMP108_CONF_POL		0x0080 /* Polarity of alert. */

/* Defaults set by the hardware upon reset. */
#define TMP108_CONF_DEFAULTS		(TMP108_CONF_CR0 | TMP108_CONF_TM |\
					 TMP108_CONF_HYS0 | TMP108_CONF_M1)
/* These bits are read-only. */
#define TMP108_CONF_READ_ONLY		(TMP108_CONF_FL | TMP108_CONF_FH |\
					 TMP108_CONF_ID)

#define TMP108_CONF_MODE_MASK		(TMP108_CONF_M0|TMP108_CONF_M1)
#define TMP108_MODE_SHUTDOWN		0x0000
#define TMP108_MODE_ONE_SHOT		TMP108_CONF_M0
#define TMP108_MODE_CONTINUOUS		TMP108_CONF_M1		/* Default */
					/* When M1 is set, M0 is ignored. */

#define TMP108_CONF_CONVRATE_MASK	(TMP108_CONF_CR0|TMP108_CONF_CR1)
#define TMP108_CONVRATE_0P25HZ		0x0000
#define TMP108_CONVRATE_1HZ		TMP108_CONF_CR0		/* Default */
#define TMP108_CONVRATE_4HZ		TMP108_CONF_CR1
#define TMP108_CONVRATE_16HZ		(TMP108_CONF_CR0|TMP108_CONF_CR1)

#define TMP108_CONF_HYSTERESIS_MASK	(TMP108_CONF_HYS0|TMP108_CONF_HYS1)
#define TMP108_HYSTERESIS_0C		0x0000
#define TMP108_HYSTERESIS_1C		TMP108_CONF_HYS0	/* Default */
#define TMP108_HYSTERESIS_2C		TMP108_CONF_HYS1
#define TMP108_HYSTERESIS_4C		(TMP108_CONF_HYS0|TMP108_CONF_HYS1)

#define TMP108_CONVERSION_TIME_MS	30	/* in milli-seconds */

enum tmp108_attributes {
	tmp108_conversion_rate,
	tmp108_temp_input,
	tmp108_temp_min,
	tmp108_temp_max,
	tmp108_temp_min_alarm,
	tmp108_temp_max_alarm,
	tmp108_temp_min_hyst,
	tmp108_temp_max_hyst
};

struct tmp108 {
	struct device *dev;
	struct regmap *regmap;
	u16 orig_config;
	unsigned long ready_time;
};

/* convert 12-bit TMP108 register value to milliCelsius */
static inline int tmp108_temp_reg_to_mC(s16 val)
{
	return (val & ~0x0f) * 1000 / 256;
}

/* convert milliCelsius to left adjusted 12-bit TMP108 register value */
static inline u16 tmp108_mC_to_temp_reg(int val)
{
	return (val * 256) / 1000;
}

static int tmp108_read(struct device *dev, enum tmp108_attributes attr, long *temp)
{
	struct tmp108 *tmp108 = dev_get_drvdata(dev);
	unsigned int regval;
	int err, hyst;

	switch (attr) {
	case tmp108_temp_input:
		/* Is it too early to return a conversion ? */
		if (time_before(jiffies, tmp108->ready_time)) {
			dev_dbg(dev, "%s: Conversion not ready yet..\n",
				__func__);
			return -EAGAIN;
		}
		err = regmap_read(tmp108->regmap, TMP108_REG_TEMP, &regval);
		if (err < 0)
			return err;
		*temp = tmp108_temp_reg_to_mC(regval);
		break;
	case tmp108_temp_min:
	case tmp108_temp_max:
		err = regmap_read(tmp108->regmap, attr == tmp108_temp_min ?
				  TMP108_REG_TLOW : TMP108_REG_THIGH, &regval);
		if (err < 0)
			return err;
		*temp = tmp108_temp_reg_to_mC(regval);
		break;
	case tmp108_temp_min_alarm:
	case tmp108_temp_max_alarm:
		err = regmap_read(tmp108->regmap, TMP108_REG_CONF, &regval);
		if (err < 0)
			return err;
		*temp = !!(regval & (attr == tmp108_temp_min_alarm ?
				     TMP108_CONF_FL : TMP108_CONF_FH));
		break;
	case tmp108_temp_min_hyst:
	case tmp108_temp_max_hyst:
		err = regmap_read(tmp108->regmap, TMP108_REG_CONF, &regval);
		if (err < 0)
			return err;
		switch (regval & TMP108_CONF_HYSTERESIS_MASK) {
		case TMP108_HYSTERESIS_0C:
		default:
			hyst = 0;
			break;
		case TMP108_HYSTERESIS_1C:
			hyst = 1000;
			break;
		case TMP108_HYSTERESIS_2C:
			hyst = 2000;
			break;
		case TMP108_HYSTERESIS_4C:
			hyst = 4000;
			break;
		}
		err = regmap_read(tmp108->regmap, attr == tmp108_temp_min_hyst ?
				  TMP108_REG_TLOW : TMP108_REG_THIGH, &regval);
		if (err < 0)
			return err;
		*temp = tmp108_temp_reg_to_mC(regval);
		if (attr == tmp108_temp_min_hyst)
			*temp += hyst;
		else
			*temp -= hyst;
		break;
	case tmp108_conversion_rate:
		err = regmap_read(tmp108->regmap, TMP108_REG_CONF,
				  &regval);
		if (err < 0)
			return err;
		switch (regval & TMP108_CONF_CONVRATE_MASK) {
		case TMP108_CONVRATE_0P25HZ:
		default:
			*temp = 4000;
			break;
		case TMP108_CONVRATE_1HZ:
			*temp = 1000;
			break;
		case TMP108_CONVRATE_4HZ:
			*temp = 250;
			break;
		case TMP108_CONVRATE_16HZ:
			*temp = 63;
			break;
		}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int tmp108_write(struct device *dev, enum tmp108_attributes attr, long temp)
{
	struct tmp108 *tmp108 = dev_get_drvdata(dev);
	u32 regval, mask;
	int err;

	switch (attr) {
	case tmp108_temp_min:
	case tmp108_temp_max:
		temp = clamp_val(temp, TMP108_TEMP_MIN_MC, TMP108_TEMP_MAX_MC);
		return regmap_write(tmp108->regmap,
				    attr == tmp108_temp_min ?
					TMP108_REG_TLOW : TMP108_REG_THIGH,
				    tmp108_mC_to_temp_reg(temp));
	case tmp108_temp_min_hyst:
	case tmp108_temp_max_hyst:
		temp = clamp_val(temp, TMP108_TEMP_MIN_MC, TMP108_TEMP_MAX_MC);
		err = regmap_read(tmp108->regmap,
				  attr == tmp108_temp_min_hyst ?
					TMP108_REG_TLOW : TMP108_REG_THIGH,
				  &regval);
		if (err < 0)
			return err;
		if (attr == tmp108_temp_min_hyst)
			temp -= tmp108_temp_reg_to_mC(regval);
		else
			temp = tmp108_temp_reg_to_mC(regval) - temp;
		if (temp < 500)
			mask = TMP108_HYSTERESIS_0C;
		else if (temp < 1500)
			mask = TMP108_HYSTERESIS_1C;
		else if (temp < 3000)
			mask = TMP108_HYSTERESIS_2C;
		else
			mask = TMP108_HYSTERESIS_4C;
		return regmap_update_bits(tmp108->regmap, TMP108_REG_CONF,
					  TMP108_CONF_HYSTERESIS_MASK, mask);
	case tmp108_conversion_rate:
		if (temp < 156)
			mask = TMP108_CONVRATE_16HZ;
		else if (temp < 625)
			mask = TMP108_CONVRATE_4HZ;
		else if (temp < 2500)
			mask = TMP108_CONVRATE_1HZ;
		else
			mask = TMP108_CONVRATE_0P25HZ;
		return regmap_update_bits(tmp108->regmap,
					  TMP108_REG_CONF,
					  TMP108_CONF_CONVRATE_MASK,
					  mask);
	default:
		return -EOPNOTSUPP;
	}
}

#define DECLARE_SHOW_ATTRIBUTE(NAME, ATTRIBUTE) \
static ssize_t show_##NAME(struct device *dev, struct device_attribute *attr, \
			  char *buf) \
{ \
	long temp; \
	int ret; \
 \
	ret = tmp108_read(dev, ATTRIBUTE, &temp); \
	if (ret) { \
		pr_err("%s:%d could not read" #ATTRIBUTE ": %d\n",__func__, __LINE__, ret); \
		return ret; \
	} \
 \
	return sprintf(buf, "%ld\n", temp); \
} \

DECLARE_SHOW_ATTRIBUTE(temp, tmp108_temp_input);
DECLARE_SHOW_ATTRIBUTE(temp_limit_min, tmp108_temp_min);
DECLARE_SHOW_ATTRIBUTE(temp_limit_max, tmp108_temp_max);
DECLARE_SHOW_ATTRIBUTE(temp_limit_hyst_min, tmp108_temp_min_hyst);
DECLARE_SHOW_ATTRIBUTE(temp_limit_hyst_max, tmp108_temp_max_hyst);
DECLARE_SHOW_ATTRIBUTE(conversion_rate, tmp108_conversion_rate);

#define DECLARE_STORE_ATTRIBUTE(NAME, ATTRIBUTE) \
static ssize_t store_##NAME(struct device *dev, struct device_attribute *attr, \
			  const char *buf, size_t count) \
{ \
	long temp; \
	int ret; \
 \
	ret = kstrtol(buf, 10, &temp); \
	if (ret) { \
		pr_err("%s:%d could not convert input: %d\n", __func__, __LINE__, ret); \
		return ret; \
	} \
 \
	ret = tmp108_write(dev, ATTRIBUTE, temp); \
	if (ret) { \
		pr_err("%s:%d could not write value: %d\n", __func__, __LINE__, ret); \
	} \
 \
	return ret; \
} \

DECLARE_STORE_ATTRIBUTE(temp_limit_min, tmp108_temp_min);
DECLARE_STORE_ATTRIBUTE(temp_limit_max, tmp108_temp_max);
DECLARE_STORE_ATTRIBUTE(temp_limit_hyst_min, tmp108_temp_min_hyst);
DECLARE_STORE_ATTRIBUTE(temp_limit_hyst_max, tmp108_temp_max_hyst);
DECLARE_STORE_ATTRIBUTE(conversion_rate, tmp108_conversion_rate);

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp_limit_max, store_temp_limit_max, 0);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_temp_limit_min, store_temp_limit_min, 0);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO, show_temp_limit_hyst_max, store_temp_limit_hyst_max, 0);
static SENSOR_DEVICE_ATTR(temp1_min_hyst, S_IWUSR | S_IRUGO, show_temp_limit_hyst_min, store_temp_limit_hyst_min, 0);
static SENSOR_DEVICE_ATTR(temp1_conversion_rate, S_IWUSR | S_IRUGO, show_conversion_rate, store_conversion_rate, 0);

static struct attribute *tmp108_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_min_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_conversion_rate.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(tmp108);

static void tmp108_restore_config(void *data)
{
	struct tmp108 *tmp108 = data;

	regmap_write(tmp108->regmap, TMP108_REG_CONF, tmp108->orig_config);
}

static bool tmp108_is_writeable_reg(struct device *dev, unsigned int reg)
{
	return reg != TMP108_REG_TEMP;
}

static bool tmp108_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/* Configuration register must be volatile to enable FL and FH. */
	return reg == TMP108_REG_TEMP || reg == TMP108_REG_CONF;
}

static const struct regmap_config tmp108_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = TMP108_REG_THIGH,
	.writeable_reg = tmp108_is_writeable_reg,
	.volatile_reg = tmp108_is_volatile_reg,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.cache_type = REGCACHE_RBTREE,
	.use_single_rw = true,
};

static int tmp108_get_temp(void *data, int *t)
{
	struct tmp108 *tmp108 = data;
	long temp = 0;

	if (tmp108_read(tmp108->dev, tmp108_temp_input, &temp) == -EAGAIN)
		*t = 0;
	else
		*t = (int)temp;

	return 0;
}

static struct thermal_zone_of_device_ops tmp108_sensor_ops = {
	.get_temp = tmp108_get_temp,
};

static int tmp108_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct tmp108 *tmp108;
	int err;
	u32 config;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(dev,
			"adapter doesn't support SMBus word transactions\n");
		return -ENODEV;
	}

	tmp108 = devm_kzalloc(dev, sizeof(*tmp108), GFP_KERNEL);
	if (!tmp108)
		return -ENOMEM;

	dev_set_drvdata(dev, tmp108);
	tmp108->dev = dev;

	tmp108->regmap = devm_regmap_init_i2c(client, &tmp108_regmap_config);
	if (IS_ERR(tmp108->regmap)) {
		err = PTR_ERR(tmp108->regmap);
		dev_err(dev, "regmap init failed: %d", err);
		return err;
	}

	err = regmap_read(tmp108->regmap, TMP108_REG_CONF, &config);
	if (err < 0) {
		dev_err(dev, "error reading config register: %d", err);
		return err;
	}
	tmp108->orig_config = config;

	/* Only continuous mode is supported. */
	config &= ~TMP108_CONF_MODE_MASK;
	config |= TMP108_MODE_CONTINUOUS;

	/* Only comparator mode is supported. */
	config &= ~TMP108_CONF_TM;

	err = regmap_write(tmp108->regmap, TMP108_REG_CONF, config);
	if (err < 0) {
		dev_err(dev, "error writing config register: %d", err);
		return err;
	}

	tmp108->ready_time = jiffies;
	if ((tmp108->orig_config & TMP108_CONF_MODE_MASK) ==
	    TMP108_MODE_SHUTDOWN)
		tmp108->ready_time +=
			msecs_to_jiffies(TMP108_CONVERSION_TIME_MS);

	err = devm_add_action(dev, tmp108_restore_config, tmp108);
	if (err) {
		tmp108_restore_config(tmp108);
		dev_err(dev, "add action failed: %d", err);
		return err;
	}

	hwmon_dev = hwmon_device_register_with_groups(dev, client->name,
						      tmp108, tmp108_groups);

	if (IS_ERR(thermal_zone_of_sensor_register(dev,
					  0,  tmp108, &tmp108_sensor_ops)))
		pr_err("%s Failed to register sensor\n", __func__);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int __maybe_unused tmp108_suspend(struct device *dev)
{
	struct tmp108 *tmp108 = dev_get_drvdata(dev);

	return regmap_update_bits(tmp108->regmap, TMP108_REG_CONF,
				  TMP108_CONF_MODE_MASK, TMP108_MODE_SHUTDOWN);
}

static int __maybe_unused tmp108_resume(struct device *dev)
{
	struct tmp108 *tmp108 = dev_get_drvdata(dev);
	int err;

	err = regmap_update_bits(tmp108->regmap, TMP108_REG_CONF,
				 TMP108_CONF_MODE_MASK, TMP108_MODE_CONTINUOUS);
	tmp108->ready_time = jiffies +
			     msecs_to_jiffies(TMP108_CONVERSION_TIME_MS);
	return err;
}

static SIMPLE_DEV_PM_OPS(tmp108_dev_pm_ops, tmp108_suspend, tmp108_resume);

static const struct i2c_device_id tmp108_i2c_ids[] = {
	{ "tmp108", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tmp108_i2c_ids);

#ifdef CONFIG_OF
static const struct of_device_id tmp108_of_ids[] = {
	{ .compatible = "ti,tmp108", },
	{}
};
MODULE_DEVICE_TABLE(of, tmp108_of_ids);
#endif

static struct i2c_driver tmp108_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.pm	= &tmp108_dev_pm_ops,
		.of_match_table = of_match_ptr(tmp108_of_ids),
	},
	.probe		= tmp108_probe,
	.id_table	= tmp108_i2c_ids,
};

module_i2c_driver(tmp108_driver);

MODULE_AUTHOR("John Muir <john@jmuir.com>");
MODULE_DESCRIPTION("Texas Instruments TMP108 temperature sensor driver");
MODULE_LICENSE("GPL");
