/*
 * leds-is31fl3236.c
 *
 * Copyright (c) 2016, 2018 Amazon.com, Inc. or its affiliates. All Rights
 * Reserved
 *
 * The code contained herein is licensed under the GNU General Public
 * License Version 2. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include "leds-is31fl3236.h"

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <dt-bindings/leds/leds-is31fl3236.h>

#define REG_SW_SHUTDOWN 0x00
#define REG_PWM_BASE 0x01
#define REG_UPDATE 0x25
#define REG_CTRL_BASE 0x26
#define REG_G_CTRL 0x4A /* Global Control Register */
#define REG_RST 0x4F
#define REG_OUTPUT_FREQUENCY 0x4B

#define LED_SW_ON 0x01
#define LED_CTRL_UPDATE 0x0
#define LED_CHAN_DISABLED 0x0
#define LED_CHAN_ENABLED 0x01

#define NUM_LED_COLORS 3
#define NUM_LED_CALIB_PARAMS 3

#define LED_PWM_MAX_SCALING 0x7F
#define BYTEMASK 0xFF
#define LED_PWM_SCALING_DEFAULT 0x7F7F7F
#define LED_PWM_MAX_LIMIT_DEFAULT 0xFFFFFF
#define INDEX_LEDCALIBENABLE 0
#define INDEX_PWMSCALING 1
#define INDEX_PWMMAXLIMIT 2


#define BOOT_ANIMATION_FRAME_DELAY 88


static int ledcalibparams[NUM_LED_CALIB_PARAMS];
static int ledcalibenable[NUM_LED_CALIB_PARAMS];
static int count = NUM_LED_CALIB_PARAMS;

MODULE_PARM_DESC(ledcalibparams, "ledcalibparams=1,0xRRGGBB,0xRRGGBB");
MODULE_PARM_DESC(ledcalibenable, "Alternative of lebcalibparams, will overwrite it");
module_param_array(ledcalibparams, int, &count, S_IRUGO);
module_param_array(ledcalibenable, int, &count, S_IRUGO);

struct is31fl3236_data {
	struct mutex lock;
	bool play_boot_animation;
	bool setup_device;
	int enable_gpio;
	int output_frequency;
	struct task_struct *boot_anim_task;
	int enabled;
	uint8_t *state;
	uint8_t led_current;
	struct i2c_client *client;
	uint8_t ledpwmmaxlimiterrgb[NUM_LED_COLORS];
	uint8_t ledpwmscalingrgb[NUM_LED_COLORS];
	int ledcalibenable;
	int ledpwmscaling;
	int ledpwmmaxlimiter;
	uint8_t ch_offset;
	size_t channel_map_count;
	uint8_t channel_map[NUM_CHANNELS];
};

static int is31fl3236_write_reg(struct i2c_client *client,
				uint32_t reg,
				uint8_t value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int is31fl3236_update_led(struct i2c_client *client,
				 uint32_t led,
				 uint8_t value)
{
	return is31fl3236_write_reg(client, REG_PWM_BASE+led, value);
}

static int update_frame(struct is31fl3236_data *pdata,
						const uint8_t *buf)
{
	struct i2c_client *client = pdata->client;
	int ret;
	int i, scaledvalue;
	int channel_index;

	if (pdata == NULL || buf == NULL) {
		pr_err("ISSI: Invalid input parameters to update_frame");
		return -EINVAL;
	}

	mutex_lock(&pdata->lock);

	for (i = 0; i < NUM_CHANNELS; i++) {
		if (pdata->ledcalibenable) {
			scaledvalue = ((int)buf[i] *
				       (int)pdata->ledpwmscalingrgb[i %
					NUM_LED_COLORS])
					/ LED_PWM_MAX_SCALING;
			pdata->state[i] =
			    min(scaledvalue,
				(int) pdata->ledpwmmaxlimiterrgb[i %
				 NUM_LED_COLORS]);
		} else {
			pdata->state[i] = buf[i];
		}
		channel_index = pdata->channel_map[(i + pdata->ch_offset) % pdata->channel_map_count];
		ret = is31fl3236_update_led(client, channel_index, pdata->state[i]);
		if (ret != 0)
			goto fail;
	}
	ret = is31fl3236_write_reg(client, REG_UPDATE, 0x0);
fail:
	mutex_unlock(&pdata->lock);
	return ret;
}

static int update_output_frequency(struct is31fl3236_data *pdata)
{
	return is31fl3236_write_reg(pdata->client, REG_OUTPUT_FREQUENCY,
				    pdata->output_frequency);
}

static int boot_anim_thread(void *data)
{
	struct is31fl3236_data *pdata = (struct is31fl3236_data *)data;
	int i = 0;

	while (!kthread_should_stop()) {
		update_frame(pdata, &frames[i][0]);
		msleep(BOOT_ANIMATION_FRAME_DELAY);
		i = (i + 1) % ARRAY_SIZE(frames);
	}
	update_frame(pdata, clear_frame);
	return 0;
}

static void is31fl3236_convert_to_rgb(uint8_t *rgbarray, int bitmap)
{
	int i, j;

	if (rgbarray == NULL) {
		pr_err("Invalid Input to %s, rgbarray is NULL", __func__);
		return;
	}

	for (i = NUM_LED_COLORS - 1, j = 0; i >= 0; i--, j++)
		rgbarray[j] = (bitmap & (BYTEMASK << 8 * i)) >> 8 * i;
}

static ssize_t boot_animation_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t len)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	uint8_t val;
	ssize_t ret;
	struct task_struct *stop_struct = NULL;

	ret = kstrtou8(buf, 10, &val);
	if (ret) {
		pr_err("ISSI: Invalid input to store_boot_anim\n");
		goto fail;
	}

	mutex_lock(&pdata->lock);
	if (!val) {
		if (pdata->boot_anim_task) {
			stop_struct = pdata->boot_anim_task;
			pdata->boot_anim_task = NULL;
		}
	} else {
		if (!pdata->boot_anim_task) {
			pdata->boot_anim_task = kthread_run(boot_anim_thread,
							(void *)pdata,
							"boot_animation_thread");
			if (IS_ERR(pdata->boot_anim_task))
				pr_err("ISSI: could not create boot animation thread\n");
		}
	}
	mutex_unlock(&pdata->lock);

	if (stop_struct)
		kthread_stop(stop_struct);

	ret = len;
fail:
	return ret;
}

static ssize_t boot_animation_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	int ret;

	mutex_lock(&pdata->lock);
	ret = sprintf(buf, "%d\n", pdata->boot_anim_task != NULL);
	mutex_unlock(&pdata->lock);

	return ret;
}
static DEVICE_ATTR_RW(boot_animation);

static ssize_t led_current_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t len)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	struct i2c_client *client = pdata->client;
	uint8_t val;
	int i;
	ssize_t ret;

	ret = kstrtou8(buf, 10, &val);
	if (ret) {
		pr_err("ISSI: Invalid input to store_led_current\n");
		goto fail;
	}
	if (val < 0 || val > 3) {
		pr_err("ISSI: Invalid led current division\n");
		ret = -EINVAL;
		goto fail;
	}

	mutex_lock(&pdata->lock);
	pdata->led_current = val;
	val = (val << 1) | 0x1;
	for (i = 0; i < NUM_CHANNELS; i++)
		is31fl3236_write_reg(client, REG_CTRL_BASE + i, val);

	is31fl3236_write_reg(client, REG_UPDATE, 0x0);
	mutex_unlock(&pdata->lock);
	ret = len;
fail:
	return ret;
}

static ssize_t led_current_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	int ret;

	mutex_lock(&pdata->lock);
	ret = sprintf(buf, "%d\n", pdata->led_current);
	mutex_unlock(&pdata->lock);

	return ret;
}
static DEVICE_ATTR_RW(led_current);

static ssize_t frame_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	int len = 0;
	int i = 0;

	mutex_lock(&pdata->lock);
	for (i = 0; i < NUM_CHANNELS; i++) {
		len += sprintf(buf, "%s%02x",
				buf, pdata->state[i]);
	}
	mutex_unlock(&pdata->lock);
	return sprintf(buf, "%s\n", buf);
}

static ssize_t frame_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t len)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	uint8_t new_state[NUM_CHANNELS];
	char val[3];
	int count = 0;
	int ret, i;

	val[2] = '\0';
	for (i = 0; i < NUM_CHANNELS * 2; i += 2) {
		val[0] = buf[i];
		val[1] = buf[i + 1];
		ret = kstrtou8(val, 16, &new_state[count]);
		if (ret) {
			pr_err("ISSI: Invalid input for frame_store: %d\n",
			       i);
			return ret;
		}
		count++;
	}

	ret = update_frame(pdata, &new_state[0]);
	if (ret < 0) {
		pr_err("ISSI: could not update frame\n");
		return ret;
	}

	return len;
}

static DEVICE_ATTR_RW(frame);

static ssize_t frequency_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t len)
{
	int ret;
	u8 frequency;
	struct is31fl3236_data *pdata = dev_get_platdata(dev);

	ret = kstrtou8(buf, 10, &frequency);
	if (ret)
		return ret;

	switch (frequency) {
	case PWM_OUTPUT_FREQ_3p35_KHZ:
	case PWM_OUTPUT_FREQ_25_KHZ:
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&pdata->lock);
	pdata->output_frequency = frequency;
	ret = update_output_frequency(pdata);
	mutex_unlock(&pdata->lock);

	if (ret)
		len = ret;

	return len;
}

static DEVICE_ATTR_WO(frequency);

static ssize_t ledcalibenable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	int ret;
	int temp;
	struct is31fl3236_data *pdata = dev_get_platdata(dev);

	ret = kstrtoint(buf, 10, &temp);
	if (ret)
		return ret;

	mutex_lock(&pdata->lock);
	pdata->ledcalibenable = temp;
	mutex_unlock(&pdata->lock);

	return len;
}

static ssize_t ledcalibenable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	int ret;

	mutex_lock(&pdata->lock);
	ret = sprintf(buf, "%d\n", pdata->ledcalibenable);
	mutex_unlock(&pdata->lock);

	return ret;
}

static DEVICE_ATTR_RW(ledcalibenable);

static ssize_t ledpwmscaling_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	int ret;
	int temp;
	struct is31fl3236_data *pdata = dev_get_platdata(dev);

	ret = kstrtoint(buf, 16, &temp);
	if (ret)
		return ret;

	mutex_lock(&pdata->lock);
	pdata->ledpwmscaling = temp;
	is31fl3236_convert_to_rgb(pdata->ledpwmscalingrgb, temp);
	mutex_unlock(&pdata->lock);

	pr_debug("ledpwmscaling = %x\n", pdata->ledpwmscaling);

	return len;
}

static ssize_t ledpwmscaling_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	int ret;

	mutex_lock(&pdata->lock);
	ret = sprintf(buf, "%x\n", pdata->ledpwmscaling);
	mutex_unlock(&pdata->lock);

	return ret;
}

static DEVICE_ATTR_RW(ledpwmscaling);

static ssize_t ledpwmmaxlimit_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	int ret;
	int temp;

	struct is31fl3236_data *pdata = dev_get_platdata(dev);

	ret = kstrtoint(buf, 16, &temp);
	if (ret)
		return ret;

	mutex_lock(&pdata->lock);
	pdata->ledpwmmaxlimiter = temp;
	is31fl3236_convert_to_rgb(pdata->ledpwmmaxlimiterrgb, temp);
	mutex_unlock(&pdata->lock);

	return len;
}

static ssize_t ledpwmmaxlimit_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);
	int ret;

	mutex_lock(&pdata->lock);
	ret = sprintf(buf, "%x\n", pdata->ledpwmmaxlimiter);
	mutex_unlock(&pdata->lock);

	return ret;
}

static DEVICE_ATTR_RW(ledpwmmaxlimit);

static void is31fl3236_parse_dt(struct is31fl3236_data *pdata,
				struct device_node *node)
{
	int i;
	int success;
	uint8_t channel_map[NUM_CHANNELS];
	int channel_map_count;
	const char *ledcalibparams_prop;

	pdata->play_boot_animation = of_property_read_bool(node,
							"play-boot-animation");
	pdata->setup_device = of_property_read_bool(node, "setup-device");
	pdata->enable_gpio = of_get_named_gpio(node, "enable-gpio", 0);

	if (of_property_read_u32(node, "output-frequency",
				 &pdata->output_frequency))
		pdata->output_frequency = PWM_OUTPUT_FREQ_3p35_KHZ;

	if (of_property_read_u8(node, "channel-offset", &pdata->ch_offset))
		pdata->ch_offset = 0;
	pdata->ch_offset = pdata->ch_offset % NUM_CHANNELS;

	channel_map_count = of_property_count_u8_elems(node, "channel-map");
	if (channel_map_count <= 0 || channel_map_count > NUM_CHANNELS)
		channel_map_count = NUM_CHANNELS;
	pdata->channel_map_count = channel_map_count;

	success = of_property_read_u8_array(node, "channel-map",
		channel_map, pdata->channel_map_count);

	for (i = 0; i < pdata->channel_map_count; i++) {
		if (success != 0) {
			/* default to one-to-one mapping if node does not exist or contains
			* an invalid value.
			*/
			pdata->channel_map[i] = i;
		} else if (channel_map[i] < 0 || channel_map[i] >= pdata->channel_map_count) {
			/* channel_map element is not in a valid range */
			pr_err("ISSI: invalid channel_map element %d at index %d\n", (int)channel_map[i], i);
			pdata->channel_map[i] = i;
		} else {
			pdata->channel_map[i] = channel_map[i];
		}
	}

	success = of_property_read_string(node, "ledcalibparams",
						&ledcalibparams_prop);
	if (success != 0) {
		pr_info("ISSI: ledcalibparams not found in DT :%d\n", success);
	} else {
		success = sscanf(ledcalibparams_prop, "%d %x %x",
				&pdata->ledcalibenable, &pdata->ledpwmscaling,
				&pdata->ledpwmmaxlimiter);
		if (success != NUM_LED_CALIB_PARAMS)
			pr_err("ISSI: Invalid ledcalibparams property :%d\n",
				success);
	}
}

static bool is31fl3236_enable(struct is31fl3236_data *pdata, bool enable)
{
	if (!WARN_ON(!gpio_is_valid(pdata->enable_gpio))) {
		gpio_set_value(pdata->enable_gpio, enable);
		return true;
	}

	return false;
}

static int is31fl3236_power_on_device(struct i2c_client *client)
{
	struct is31fl3236_data *pdata = dev_get_platdata(&client->dev);
	int i;
	int ret = 0;

	if (pdata->setup_device) {
		if (gpio_is_valid(pdata->enable_gpio)) {
			ret = devm_gpio_request_one(&client->dev,
						    pdata->enable_gpio,
						    GPIOF_DIR_OUT,
						    "is31fl3236_enable");
			if (ret < 0) {
				pr_err("ISSI: could not request enable gpio\n");
				return ret;
			}
			/*
			 * Device shuts down if enable_gpio is low so
			 * force it high always.
			 */
			is31fl3236_enable(pdata, true);
		}

		ret = is31fl3236_write_reg(client, REG_RST, 0x0);
		if (ret < 0) {
			pr_err("ISSI: Could not reset registers\n");
			goto fail;
		}

		ret = is31fl3236_write_reg(client, REG_SW_SHUTDOWN, LED_SW_ON);
		if (ret < 0) {
			pr_err("ISSI: Could not start device\n");
			goto fail;
		}
	}
	pdata->enabled = LED_CHAN_ENABLED;

	for (i = 0; i < NUM_CHANNELS; i++) {
		ret = is31fl3236_write_reg(client, REG_CTRL_BASE + i, 0x01);
		if (ret < 0) {
			pr_err("ISSI: Could not enabled led: %d\n", i);
			goto fail;
		}
	}
	ret = is31fl3236_write_reg(client, REG_UPDATE, 0x0);

	if (ret) {
		pr_err("ISSI: Failed to write REG_UPDATE=%d\n", ret);
	} else {
		ret = update_output_frequency(pdata);
		if (ret)
			pr_err("ISSI: Failed to write output freq=%d\n", ret);
	}
	return ret;
fail:
	if (is31fl3236_enable(pdata, false))
		devm_gpio_free(&client->dev, pdata->enable_gpio);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int is31fl3236_suspend(struct device *dev)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);

	is31fl3236_enable(pdata, false);

	pr_debug("%s leave\n", __func__);

	return 0;
}

static int is31fl3236_resume(struct device *dev)
{
	struct is31fl3236_data *pdata = dev_get_platdata(dev);

	is31fl3236_enable(pdata, true);

	pr_debug("%s leave\n", __func__);

	return 0;
}
static SIMPLE_DEV_PM_OPS(is31fl3236_pm_ops, is31fl3236_suspend,
			 is31fl3236_resume);
#endif

static int is31fl3236_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct is31fl3236_data *pdata = devm_kzalloc(&client->dev,
						sizeof(struct is31fl3236_data),
						GFP_KERNEL);
	int ret = 0;

	if (!pdata) {
		pr_err("ISSI: Could not allocate memory for platform data\n");
		ret = -ENOMEM;
		goto fail;
	}

	pdata->client = client;
	pdata->state = devm_kzalloc(&client->dev,
				    sizeof(uint8_t) * NUM_CHANNELS,
								GFP_KERNEL);
	if (!pdata->state) {
		pr_err("ISSI: Could not allocate memory for state data\n");
		ret = -ENOMEM;
		devm_kfree(&client->dev, pdata);
		goto fail;
	}
	pdata->led_current = 0;
	pdata->enabled = LED_CHAN_DISABLED;
	mutex_init(&pdata->lock);
	pdata->boot_anim_task = NULL;

	/*
	 * Incase modparams got passed as ledcalibenable instead of ledcalibparams
	 * as in case of some early abc123 PVT devices
	 */
	if (ledcalibenable[0])
		memcpy(ledcalibparams, ledcalibenable, sizeof(ledcalibparams));

	if (ledcalibparams[0]) {
		pdata->ledcalibenable = ledcalibparams[INDEX_LEDCALIBENABLE];
		pdata->ledpwmscaling = ledcalibparams[INDEX_PWMSCALING];
		pdata->ledpwmmaxlimiter = ledcalibparams[INDEX_PWMMAXLIMIT];
	}

	is31fl3236_parse_dt(pdata, client->dev.of_node);

	if (!pdata->ledpwmscaling || !pdata->ledpwmmaxlimiter) {
		pdata->ledpwmscaling = LED_PWM_SCALING_DEFAULT;
		pdata->ledpwmmaxlimiter = LED_PWM_MAX_LIMIT_DEFAULT;
	}

	client->dev.platform_data = pdata;
	ret = is31fl3236_power_on_device(client);
	if (ret < 0) {
		pr_err("ISSI: Could not power on device: %d\n", ret);
		goto fail;
	}

	ret = device_create_file(&client->dev, &dev_attr_frame);
	if (ret) {
		pr_err("ISSI: Could not create frame sysfs entry\n");
		goto fail;
	}
	ret = device_create_file(&client->dev, &dev_attr_led_current);
	if (ret) {
		pr_err("ISSI: Could not create brightness sysfs entry\n");
		goto fail;
	}

	ret = device_create_file(&client->dev, &dev_attr_boot_animation);
	if (ret) {
		pr_err("ISSI: Could not create boot animation entry\n");
		goto fail;
	}
	ret = device_create_file(&client->dev, &dev_attr_frequency);
	if (ret) {
		pr_err("ISSI: Could not create frequency sysfs entry\n");
		goto fail;
	}
	ret = device_create_file(&client->dev, &dev_attr_ledcalibenable);
	if (ret) {
		pr_err("ISSI: Could not create ledcalibenable sysfs entry\n");
		goto fail;
	}
	ret = device_create_file(&client->dev, &dev_attr_ledpwmmaxlimit);
	if (ret) {
		pr_err("ISSI: Could not create ledpwmmaxlimit sysfs entry\n");
		goto fail;
	}
	ret = device_create_file(&client->dev, &dev_attr_ledpwmscaling);
	if (ret) {
		pr_err("ISSI: Could not create ledpwmscaling sysfs entry\n");
		goto fail;
	}

	/*
	 * Extract the individual calibration byte for each color from
	 * the int value passed in by the lk
	 */
	is31fl3236_convert_to_rgb(pdata->ledpwmscalingrgb,
					pdata->ledpwmscaling);
	is31fl3236_convert_to_rgb(pdata->ledpwmmaxlimiterrgb,
					pdata->ledpwmmaxlimiter);

	if (pdata->play_boot_animation) {
		mutex_lock(&pdata->lock);
		pdata->boot_anim_task = kthread_run(boot_anim_thread,
						    (void *)pdata,
						    "boot_animation_thread");
		if (IS_ERR(pdata->boot_anim_task))
			pr_err("ISSI: could not start boot animation thread");
		mutex_unlock(&pdata->lock);
	}
fail:
	return ret;
}

static int is31fl3236_remove(struct i2c_client *client)
{
	struct is31fl3236_data *pdata = dev_get_platdata(&client->dev);
	int ret = 0;

	mutex_lock(&pdata->lock);
	if (pdata->boot_anim_task) {
		kthread_stop(pdata->boot_anim_task);
		pdata->boot_anim_task = NULL;
	}

	ret = is31fl3236_write_reg(client, REG_RST, 0x0);
	if (ret < 0) {
		pr_err("ISSI: Could not reset registers\n");
		goto fail;
	}

fail:
	is31fl3236_enable(pdata, false);
	mutex_unlock(&pdata->lock);
	return ret;
}

static void is31fl3236_shutdown(struct i2c_client *client)
{
	is31fl3236_remove(client);
}

static struct i2c_device_id is31fl3236_i2c_match[] = {
	{"issi,is31fl3236", 0},
};
MODULE_DEVICE_TABLE(i2c, is31fl3236_i2c_match);

static struct of_device_id is31fl3236_of_match[] = {
	{ .compatible = "issi,is31fl3236"},
};
MODULE_DEVICE_TABLE(of, is31fl3236_of_match);

static struct i2c_driver is31fl3236_driver = {
	.driver = {
		.name = "is31fl3236",
		.of_match_table = of_match_ptr(is31fl3236_of_match),
		.pm = &is31fl3236_pm_ops,
	},
	.probe = is31fl3236_probe,
	.remove = is31fl3236_remove,
	.shutdown = is31fl3236_shutdown,
	.id_table = is31fl3236_i2c_match,
};

module_i2c_driver(is31fl3236_driver);

MODULE_AUTHOR("Amazon.com");
MODULE_DESCRIPTION("ISSI IS31FL3236 LED Driver");
MODULE_LICENSE("GPL");
