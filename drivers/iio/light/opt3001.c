/**
 * opt3001.c - Texas Instruments OPT3001 Light Sensor
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Andreas Dannenberg <dannenberg@ti.com>
 * Based on previous work from: Felipe Balbi <balbi@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 of the License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define OPT3001_RESULT		0x00
#define OPT3001_CONFIGURATION	0x01
#define OPT3001_LOW_LIMIT	0x02
#define OPT3001_HIGH_LIMIT	0x03
#define OPT3001_MANUFACTURER_ID	0x7e
#define OPT3001_DEVICE_ID	0x7f

#define OPT3001_CONFIGURATION_RN_MASK	(0xf << 12)
#define OPT3001_CONFIGURATION_RN_AUTO	(0xc << 12)

#define OPT3001_CONFIGURATION_CT	BIT(11)

#define OPT3001_CONFIGURATION_M_MASK	(3 << 9)
#define OPT3001_CONFIGURATION_M_SHUTDOWN (0 << 9)
#define OPT3001_CONFIGURATION_M_SINGLE	(1 << 9)
#define OPT3001_CONFIGURATION_M_CONTINUOUS (2 << 9) /* also 3 << 9 */

#define OPT3001_CONFIGURATION_OVF	BIT(8)
#define OPT3001_CONFIGURATION_CRF	BIT(7)
#define OPT3001_CONFIGURATION_FH	BIT(6)
#define OPT3001_CONFIGURATION_FL	BIT(5)
#define OPT3001_CONFIGURATION_L		BIT(4)
#define OPT3001_CONFIGURATION_POL	BIT(3)
#define OPT3001_CONFIGURATION_ME	BIT(2)

#define OPT3001_CONFIGURATION_FC_MASK	(3 << 0)

/* The end-of-conversion enable is located in the low-limit register */
#define OPT3001_LOW_LIMIT_EOC_ENABLE	0xc000

#define OPT3001_REG_EXPONENT(n)		((n) >> 12)
#define OPT3001_REG_MANTISSA(n)		((n) & 0xfff)

#define OPT3001_INT_TIME_LONG		800000
#define OPT3001_INT_TIME_SHORT		100000

/*
 * Time to wait for conversion result to be ready. The device datasheet
 * sect. 6.5 states results are ready after total integration time plus 3ms.
 * This results in worst-case max values of 113ms or 883ms, respectively.
 * Add some slack to be on the safe side.
 */
#define OPT3001_RESULT_READY_SHORT	150
#define OPT3001_RESULT_READY_LONG	1000

#define MAX_MEASURED_LUX	400

static s32 calibrated_lux_at_0;
static s32 calibrated_lux_at_400 = -1;
static s32 calibrated_lux;

struct opt3001 {
	struct i2c_client	*client;
	struct device		*dev;

	struct mutex		lock;
	bool			ok_to_ignore_lock;
	bool			result_ready;
	wait_queue_head_t	result_ready_queue;
	u16			result;

	u32			int_time;
	u32			mode;

	u16			high_thresh_mantissa;
	u16			low_thresh_mantissa;

	u8			high_thresh_exp;
	u8			low_thresh_exp;

	bool			use_irq;
};

struct opt3001_scale {
	int	val;
	int	val2;
};

static const struct opt3001_scale opt3001_scales[] = {
	{
		.val = 40,
		.val2 = 950000,
	},
	{
		.val = 81,
		.val2 = 900000,
	},
	{
		.val = 163,
		.val2 = 800000,
	},
	{
		.val = 327,
		.val2 = 600000,
	},
	{
		.val = 655,
		.val2 = 200000,
	},
	{
		.val = 1310,
		.val2 = 400000,
	},
	{
		.val = 2620,
		.val2 = 800000,
	},
	{
		.val = 5241,
		.val2 = 600000,
	},
	{
		.val = 10483,
		.val2 = 200000,
	},
	{
		.val = 20966,
		.val2 = 400000,
	},
	{
		.val = 83865,
		.val2 = 600000,
	},
};

static int opt3001_get_lux(struct opt3001 *opt, int *val, int *val2);
static ssize_t calibrated_lux_show(struct device *dev,
		struct device_attribute *attr,
		char *buf) {


	struct opt3001 *opt = iio_priv(dev_to_iio_dev(dev));
	int val1;
	int val2;

	opt3001_get_lux(opt, &val1, &val2);
	if (calibrated_lux_at_400 > 0)
		return sprintf(buf, "%d\n", calibrated_lux);
	return sprintf(buf, "%d.%d\n", calibrated_lux/1000, calibrated_lux % 1000);
}

static ssize_t calibrated_lux_at_400_show(struct device *dev,
		struct device_attribute *attr,
		char *buf) {
		return sprintf(buf, "%d\n", calibrated_lux_at_400);
}

static ssize_t calibrated_lux_at_400_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count) {
	int ret = kstrtouint(buf, 10, &calibrated_lux_at_400);

	if (ret) {
		pr_err("%s: %u: could not parse calibrated lux at 400: %d\n",
				__func__, __LINE__, ret);
		return ret;
	}
	return count;
}

DEVICE_ATTR_RO(calibrated_lux);
DEVICE_ATTR_RW(calibrated_lux_at_400);

static int opt3001_find_scale(const struct opt3001 *opt, int val,
		int val2, u8 *exponent)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(opt3001_scales); i++) {
		const struct opt3001_scale *scale = &opt3001_scales[i];

		/*
		 * Combine the integer and micro parts for comparison
		 * purposes. Use milli lux precision to avoid 32-bit integer
		 * overflows.
		 */
		if ((val * 1000 + val2 / 1000) <=
				(scale->val * 1000 + scale->val2 / 1000)) {
			*exponent = i;
			return 0;
		}
	}

	return -EINVAL;
}

static void opt3001_to_iio_ret(struct opt3001 *opt, u8 exponent,
		u16 mantissa, int *val, int *val2)
{
	int lux;

	lux = 10 * (mantissa << exponent);
	*val = lux / 1000;
	*val2 = (lux - (*val * 1000)) * 1000;
}

static void opt3001_set_mode(struct opt3001 *opt, u16 *reg, u16 mode)
{
	*reg &= ~OPT3001_CONFIGURATION_M_MASK;
	*reg |= mode;
	opt->mode = mode;
}

static IIO_CONST_ATTR_INT_TIME_AVAIL("0.1 0.8");

static struct attribute *opt3001_attributes[] = {
	&iio_const_attr_integration_time_available.dev_attr.attr,
	&dev_attr_calibrated_lux.attr,
	&dev_attr_calibrated_lux_at_400.attr,
	NULL
};

static const struct attribute_group opt3001_attribute_group = {
	.attrs = opt3001_attributes,
};

static const struct iio_event_spec opt3001_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec opt3001_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				BIT(IIO_CHAN_INFO_INT_TIME),
		.event_spec = opt3001_event_spec,
		.num_event_specs = ARRAY_SIZE(opt3001_event_spec),
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static int opt3001_get_lux(struct opt3001 *opt, int *val, int *val2)
{
	int ret;
	u16 mantissa;
	u16 reg;
	u8 exponent;
	u16 value;
	long timeout;
	int lux_val;

	if (opt->use_irq) {
		/*
		 * Enable the end-of-conversion interrupt mechanism. Note that
		 * doing so will overwrite the low-level limit value however we
		 * will restore this value later on.
		 */
		ret = i2c_smbus_write_word_swapped(opt->client,
					OPT3001_LOW_LIMIT,
					OPT3001_LOW_LIMIT_EOC_ENABLE);
		if (ret < 0) {
			dev_err(opt->dev, "failed to write register %02x\n",
					OPT3001_LOW_LIMIT);
			return ret;
		}

		/* Allow IRQ to access the device despite lock being set */
		opt->ok_to_ignore_lock = true;
	}

	/* Reset data-ready indicator flag */
	opt->result_ready = false;

	/* Configure for single-conversion mode and start a new conversion */
	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_CONFIGURATION);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_CONFIGURATION);
		goto err;
	}

	reg = ret;
	opt3001_set_mode(opt, &reg, OPT3001_CONFIGURATION_M_SINGLE);

	ret = i2c_smbus_write_word_swapped(opt->client, OPT3001_CONFIGURATION,
			reg);
	if (ret < 0) {
		dev_err(opt->dev, "failed to write register %02x\n",
				OPT3001_CONFIGURATION);
		goto err;
	}

	if (opt->use_irq) {
		/* Wait for the IRQ to indicate the conversion is complete */
		ret = wait_event_timeout(opt->result_ready_queue,
				opt->result_ready,
				msecs_to_jiffies(OPT3001_RESULT_READY_LONG));
	} else {
		/* Sleep for result ready time */
		timeout = (opt->int_time == OPT3001_INT_TIME_SHORT) ?
			OPT3001_RESULT_READY_SHORT : OPT3001_RESULT_READY_LONG;
		msleep(timeout);

		/* Check result ready flag */
		ret = i2c_smbus_read_word_swapped(opt->client,
						  OPT3001_CONFIGURATION);
		if (ret < 0) {
			dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_CONFIGURATION);
			goto err;
		}

		if (!(ret & OPT3001_CONFIGURATION_CRF)) {
			ret = -ETIMEDOUT;
			goto err;
		}

		/* Obtain value */
		ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_RESULT);
		if (ret < 0) {
			dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_RESULT);
			goto err;
		}
		opt->result = ret;
		opt->result_ready = true;
	}

err:
	if (opt->use_irq)
		/* Disallow IRQ to access the device while lock is active */
		opt->ok_to_ignore_lock = false;

	if (!opt->result_ready) {
		if (ret == 0)
			return -ETIMEDOUT;
		else if (ret < 0)
			return ret;
	}

	if (opt->use_irq) {
		/*
		 * Disable the end-of-conversion interrupt mechanism by
		 * restoring the low-level limit value (clearing
		 * OPT3001_LOW_LIMIT_EOC_ENABLE). Note that selectively clearing
		 * those enable bits would affect the actual limit value due to
		 * bit-overlap and therefore can't be done.
		 */
		value = (opt->low_thresh_exp << 12) | opt->low_thresh_mantissa;
		ret = i2c_smbus_write_word_swapped(opt->client,
						   OPT3001_LOW_LIMIT,
						   value);
		if (ret < 0) {
			dev_err(opt->dev, "failed to write register %02x\n",
					OPT3001_LOW_LIMIT);
			return ret;
		}
	}

	exponent = OPT3001_REG_EXPONENT(opt->result);
	mantissa = OPT3001_REG_MANTISSA(opt->result);

	opt3001_to_iio_ret(opt, exponent, mantissa, val, val2);

	lux_val = *val2/1000 + *val * 1000;

	if (calibrated_lux_at_400 > 0) {
		long calibrated_val = 0;
		calibrated_val = (lux_val * MAX_MEASURED_LUX ) / (calibrated_lux_at_400);
		calibrated_lux = (int)calibrated_val;
	} else {
		calibrated_lux = (int)lux_val;
	}


	return IIO_VAL_INT_PLUS_MICRO;
}

static int opt3001_get_int_time(struct opt3001 *opt, int *val, int *val2)
{
	*val = 0;
	*val2 = opt->int_time;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int opt3001_set_int_time(struct opt3001 *opt, int time)
{
	int ret;
	u16 reg;

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_CONFIGURATION);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_CONFIGURATION);
		return ret;
	}

	reg = ret;

	switch (time) {
	case OPT3001_INT_TIME_SHORT:
		reg &= ~OPT3001_CONFIGURATION_CT;
		opt->int_time = OPT3001_INT_TIME_SHORT;
		break;
	case OPT3001_INT_TIME_LONG:
		reg |= OPT3001_CONFIGURATION_CT;
		opt->int_time = OPT3001_INT_TIME_LONG;
		break;
	default:
		return -EINVAL;
	}

	return i2c_smbus_write_word_swapped(opt->client, OPT3001_CONFIGURATION,
			reg);
}

static int opt3001_read_raw(struct iio_dev *iio,
		struct iio_chan_spec const *chan, int *val, int *val2,
		long mask)
{
	struct opt3001 *opt = iio_priv(iio);
	int ret;

	if (opt->mode == OPT3001_CONFIGURATION_M_CONTINUOUS)
		return -EBUSY;

	if (chan->type != IIO_LIGHT)
		return -EINVAL;

	mutex_lock(&opt->lock);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = opt3001_get_lux(opt, val, val2);
		break;
	case IIO_CHAN_INFO_INT_TIME:
		ret = opt3001_get_int_time(opt, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&opt->lock);

	return ret;
}

static int opt3001_write_raw(struct iio_dev *iio,
		struct iio_chan_spec const *chan, int val, int val2,
		long mask)
{
	struct opt3001 *opt = iio_priv(iio);
	int ret;

	if (opt->mode == OPT3001_CONFIGURATION_M_CONTINUOUS)
		return -EBUSY;

	if (chan->type != IIO_LIGHT)
		return -EINVAL;

	if (mask != IIO_CHAN_INFO_INT_TIME)
		return -EINVAL;

	if (val != 0)
		return -EINVAL;

	mutex_lock(&opt->lock);
	ret = opt3001_set_int_time(opt, val2);
	mutex_unlock(&opt->lock);

	return ret;
}

static int opt3001_read_event_value(struct iio_dev *iio,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir, enum iio_event_info info,
		int *val, int *val2)
{
	struct opt3001 *opt = iio_priv(iio);
	int ret = IIO_VAL_INT_PLUS_MICRO;

	mutex_lock(&opt->lock);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		opt3001_to_iio_ret(opt, opt->high_thresh_exp,
				opt->high_thresh_mantissa, val, val2);
		break;
	case IIO_EV_DIR_FALLING:
		opt3001_to_iio_ret(opt, opt->low_thresh_exp,
				opt->low_thresh_mantissa, val, val2);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&opt->lock);

	return ret;
}

static int opt3001_write_event_value(struct iio_dev *iio,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir, enum iio_event_info info,
		int val, int val2)
{
	struct opt3001 *opt = iio_priv(iio);
	int ret;

	u16 mantissa;
	u16 value;
	u16 reg;

	u8 exponent;

	if (val < 0)
		return -EINVAL;

	mutex_lock(&opt->lock);

	ret = opt3001_find_scale(opt, val, val2, &exponent);
	if (ret < 0) {
		dev_err(opt->dev, "can't find scale for %d.%06u\n", val, val2);
		goto err;
	}

	mantissa = (((val * 1000) + (val2 / 1000)) / 10) >> exponent;
	value = (exponent << 12) | mantissa;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		reg = OPT3001_HIGH_LIMIT;
		opt->high_thresh_mantissa = mantissa;
		opt->high_thresh_exp = exponent;
		break;
	case IIO_EV_DIR_FALLING:
		reg = OPT3001_LOW_LIMIT;
		opt->low_thresh_mantissa = mantissa;
		opt->low_thresh_exp = exponent;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	ret = i2c_smbus_write_word_swapped(opt->client, reg, value);
	if (ret < 0) {
		dev_err(opt->dev, "failed to write register %02x\n", reg);
		goto err;
	}

err:
	mutex_unlock(&opt->lock);

	return ret;
}

static int opt3001_read_event_config(struct iio_dev *iio,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir)
{
	struct opt3001 *opt = iio_priv(iio);

	return opt->mode == OPT3001_CONFIGURATION_M_CONTINUOUS;
}

static int opt3001_write_event_config(struct iio_dev *iio,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir, int state)
{
	struct opt3001 *opt = iio_priv(iio);
	int ret;
	u16 mode;
	u16 reg;

	if (state && opt->mode == OPT3001_CONFIGURATION_M_CONTINUOUS)
		return 0;

	if (!state && opt->mode == OPT3001_CONFIGURATION_M_SHUTDOWN)
		return 0;

	mutex_lock(&opt->lock);

	mode = state ? OPT3001_CONFIGURATION_M_CONTINUOUS
		: OPT3001_CONFIGURATION_M_SHUTDOWN;

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_CONFIGURATION);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_CONFIGURATION);
		goto err;
	}

	reg = ret;
	opt3001_set_mode(opt, &reg, mode);

	ret = i2c_smbus_write_word_swapped(opt->client, OPT3001_CONFIGURATION,
			reg);
	if (ret < 0) {
		dev_err(opt->dev, "failed to write register %02x\n",
				OPT3001_CONFIGURATION);
		goto err;
	}

err:
	mutex_unlock(&opt->lock);

	return ret;
}

static const struct iio_info opt3001_info = {
	.attrs = &opt3001_attribute_group,
	.read_raw = opt3001_read_raw,
	.write_raw = opt3001_write_raw,
	.read_event_value = opt3001_read_event_value,
	.write_event_value = opt3001_write_event_value,
	.read_event_config = opt3001_read_event_config,
	.write_event_config = opt3001_write_event_config,
};

static int opt3001_read_id(struct opt3001 *opt)
{
	char manufacturer[2];
	u16 device_id;
	int ret;

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_MANUFACTURER_ID);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_MANUFACTURER_ID);
		return ret;
	}

	manufacturer[0] = ret >> 8;
	manufacturer[1] = ret & 0xff;

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_DEVICE_ID);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_DEVICE_ID);
		return ret;
	}

	device_id = ret;

	dev_info(opt->dev, "Found %c%c OPT%04x\n", manufacturer[0],
			manufacturer[1], device_id);

	return 0;
}

static int opt3001_configure(struct opt3001 *opt)
{
	int ret;
	u16 reg;

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_CONFIGURATION);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_CONFIGURATION);
		return ret;
	}

	reg = ret;

	/* Enable automatic full-scale setting mode */
	reg &= ~OPT3001_CONFIGURATION_RN_MASK;
	reg |= OPT3001_CONFIGURATION_RN_AUTO;

	/* Reflect status of the device's integration time setting */
	if (reg & OPT3001_CONFIGURATION_CT)
		opt->int_time = OPT3001_INT_TIME_LONG;
	else
		opt->int_time = OPT3001_INT_TIME_SHORT;

	/* Ensure device is in shutdown initially */
	opt3001_set_mode(opt, &reg, OPT3001_CONFIGURATION_M_SHUTDOWN);

	/* Configure for latched window-style comparison operation */
	reg |= OPT3001_CONFIGURATION_L;
	reg &= ~OPT3001_CONFIGURATION_POL;
	reg &= ~OPT3001_CONFIGURATION_ME;
	reg &= ~OPT3001_CONFIGURATION_FC_MASK;

	ret = i2c_smbus_write_word_swapped(opt->client, OPT3001_CONFIGURATION,
			reg);
	if (ret < 0) {
		dev_err(opt->dev, "failed to write register %02x\n",
				OPT3001_CONFIGURATION);
		return ret;
	}

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_LOW_LIMIT);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_LOW_LIMIT);
		return ret;
	}

	opt->low_thresh_mantissa = OPT3001_REG_MANTISSA(ret);
	opt->low_thresh_exp = OPT3001_REG_EXPONENT(ret);

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_HIGH_LIMIT);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_HIGH_LIMIT);
		return ret;
	}

	opt->high_thresh_mantissa = OPT3001_REG_MANTISSA(ret);
	opt->high_thresh_exp = OPT3001_REG_EXPONENT(ret);

	return 0;
}

#if CONFIG_IDME
#define IDME_OF_ALSCAL 		"/idme/alscal"
#define IDME_OF_BOARD_ID	"/idme/board_id"
static void idme_set_alscal_calibrated_values(void)
{
	struct device_node *ap = NULL;
	char *alscal_idme = NULL;
	char *boardid_idme = NULL;
	char alscal_copy[20];
	char *alscal = alscal_copy;
	char *lux_at_0_str, *lux_at_400_str;
	char *lux_at_400_str_integer = NULL;
	char *lux_at_400_str_decimal = NULL;
	int lux_at_400_integer = 0;
	int lux_at_400_decimal = 0;
	const char delimiters[] = "., ";

	pr_err("fetching calibration values for ALSCAL\n");
	ap = of_find_node_by_path(IDME_OF_ALSCAL);
	if (ap) {
		alscal_idme = (char *)of_get_property(ap, "value", NULL);
	} else {
		pr_err("of_find_node_by_path failed\n");
		return;
	}

	if (!alscal_idme) {
		pr_err("alscal node not found in idme\n");
		return;
	}

	ap = of_find_node_by_path(IDME_OF_BOARD_ID);
	if (ap)
		boardid_idme = (char *)of_get_property(ap, "value", NULL);
	else {
		pr_err("of_find_node_by_path for board_id failed\n");
		return;
	}


	strcpy(alscal, alscal_idme);

	/* alscal is stored in the format alscal=00,06 */
	lux_at_0_str = strsep(&alscal, ", ");

	if (!alscal) {
		return;
	}

	lux_at_400_str = strsep(&alscal, "");

	lux_at_400_str_integer = strsep(&lux_at_400_str, delimiters);

	if (lux_at_400_str != NULL)
		lux_at_400_str_decimal = strsep(&lux_at_400_str, "");

	if (kstrtos32(lux_at_400_str_integer, 16, &lux_at_400_integer) != 0) {
		pr_err("Calibration data not found\n");
		return;
	}

	if (lux_at_400_str_decimal != NULL) {
		if (kstrtos32(lux_at_400_str_decimal, 10, &lux_at_400_decimal) != 0) {
			pr_err("Calibration data not found\n");
			return;
		}
	}

	/* figuring out the nearest multiplier for the decimal part */
	if (lux_at_400_decimal < 10)
		lux_at_400_decimal *= 100;
	else if (lux_at_400_decimal < 100)
		lux_at_400_decimal *= 10;
	else if (lux_at_400_decimal < 1000)
		lux_at_400_decimal *= 1;

	/*
	 * Multiply by 1000 to aid in fixed point division when
	 * scaling the values
	 */
	calibrated_lux_at_400 = lux_at_400_integer * 1000 + lux_at_400_decimal;
	calibrated_lux_at_0 *= 1000;

	pr_err("Raw value at 400 lux is %d\n", calibrated_lux_at_400);
}
#else
	static inline void idme_set_alscal_calibrated_values(void) {}
#endif

static irqreturn_t opt3001_irq(int irq, void *_iio)
{
	struct iio_dev *iio = _iio;
	struct opt3001 *opt = iio_priv(iio);
	int ret;

	if (!opt->ok_to_ignore_lock)
		mutex_lock(&opt->lock);

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_CONFIGURATION);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_CONFIGURATION);
		goto out;
	}

	if ((ret & OPT3001_CONFIGURATION_M_MASK) ==
			OPT3001_CONFIGURATION_M_CONTINUOUS) {
		if (ret & OPT3001_CONFIGURATION_FH)
			iio_push_event(iio,
					IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
							IIO_EV_TYPE_THRESH,
							IIO_EV_DIR_RISING),
					iio_get_time_ns());
		if (ret & OPT3001_CONFIGURATION_FL)
			iio_push_event(iio,
					IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
							IIO_EV_TYPE_THRESH,
							IIO_EV_DIR_FALLING),
					iio_get_time_ns());
	} else if (ret & OPT3001_CONFIGURATION_CRF) {
		ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_RESULT);
		if (ret < 0) {
			dev_err(opt->dev, "failed to read register %02x\n",
					OPT3001_RESULT);
			goto out;
		}
		opt->result = ret;
		opt->result_ready = true;
		wake_up(&opt->result_ready_queue);
	}

out:
	if (!opt->ok_to_ignore_lock)
		mutex_unlock(&opt->lock);

	return IRQ_HANDLED;
}

static int opt3001_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;

	struct iio_dev *iio;
	struct opt3001 *opt;
	int irq = client->irq;
	int ret;

	iio = devm_iio_device_alloc(dev, sizeof(*opt));
	if (!iio)
		return -ENOMEM;

	opt = iio_priv(iio);
	opt->client = client;
	opt->dev = dev;

	mutex_init(&opt->lock);
	init_waitqueue_head(&opt->result_ready_queue);
	i2c_set_clientdata(client, iio);

	ret = opt3001_read_id(opt);
	if (ret)
		return ret;

	ret = opt3001_configure(opt);
	if (ret)
		return ret;

	idme_set_alscal_calibrated_values();

	iio->name = client->name;
	iio->channels = opt3001_channels;
	iio->num_channels = ARRAY_SIZE(opt3001_channels);
	iio->dev.parent = dev;
	iio->modes = INDIO_DIRECT_MODE;
	iio->info = &opt3001_info;

	ret = devm_iio_device_register(dev, iio);
	if (ret) {
		dev_err(dev, "failed to register IIO device\n");
		return ret;
	}

	/* Make use of INT pin only if valid IRQ no. is given */
	if (irq > 0) {
		ret = request_threaded_irq(irq, NULL, opt3001_irq,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"opt3001", iio);
		if (ret) {
			dev_err(dev, "failed to request IRQ #%d\n", irq);
			return ret;
		}
		opt->use_irq = true;
	} else {
		dev_dbg(opt->dev, "enabling interrupt-less operation\n");
	}

	return 0;
}

static int opt3001_remove(struct i2c_client *client)
{
	struct iio_dev *iio = i2c_get_clientdata(client);
	struct opt3001 *opt = iio_priv(iio);
	int ret;
	u16 reg;

	if (opt->use_irq)
		free_irq(client->irq, iio);

	ret = i2c_smbus_read_word_swapped(opt->client, OPT3001_CONFIGURATION);
	if (ret < 0) {
		dev_err(opt->dev, "failed to read register %02x\n",
				OPT3001_CONFIGURATION);
		return ret;
	}

	reg = ret;
	opt3001_set_mode(opt, &reg, OPT3001_CONFIGURATION_M_SHUTDOWN);

	ret = i2c_smbus_write_word_swapped(opt->client, OPT3001_CONFIGURATION,
			reg);
	if (ret < 0) {
		dev_err(opt->dev, "failed to write register %02x\n",
				OPT3001_CONFIGURATION);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id opt3001_id[] = {
	{ "opt3001", 0 },
	{ } /* Terminating Entry */
};
MODULE_DEVICE_TABLE(i2c, opt3001_id);

static const struct of_device_id opt3001_of_match[] = {
	{ .compatible = "ti,opt3001" },
	{ }
};
MODULE_DEVICE_TABLE(of, opt3001_of_match);

static struct i2c_driver opt3001_driver = {
	.probe = opt3001_probe,
	.remove = opt3001_remove,
	.id_table = opt3001_id,

	.driver = {
		.name = "opt3001",
		.of_match_table = of_match_ptr(opt3001_of_match),
	},
};

module_i2c_driver(opt3001_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andreas Dannenberg <dannenberg@ti.com>");
MODULE_DESCRIPTION("Texas Instruments OPT3001 Light Sensor Driver");