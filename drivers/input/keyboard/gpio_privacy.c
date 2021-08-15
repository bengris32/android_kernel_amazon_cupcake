/*
 * Copyright 2018 Amazon Technologies, Inc. All Rights Reserved.
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

#define pr_fmt(fmt) "gpio-privacy: " fmt

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/input.h>

#define DEFAULT_DEBOUNCE_INTERVAL 5

struct privacy_input_event {
	const char *desc;
	unsigned int code;
	int debounce_interval;
	struct input_dev *input_dev;
	struct delayed_work work;
	bool wakeup_capable;
	int button_gpio;
	enum of_gpio_flags button_gpio_flags;
	int last_button_event;
};

struct privacy_priv {
	int num_of_state_gpios;
	int *state_gpios;
	int *state_gpios_flags;
	int enable_gpio;
	int enable_gpio_flags;
	struct mutex mutex;
	struct privacy_input_event *input_event;
};

static void privacy_input_event_work_func(struct work_struct *work)
{
	struct privacy_input_event *input =
		container_of(work, struct privacy_input_event, work.work);

	input_report_key(input->input_dev, input->code, 1);
	input_sync(input->input_dev);
	input_report_key(input->input_dev, input->code, 0);
	input_sync(input->input_dev);

	if (input->wakeup_capable)
		pm_relax(input->input_dev->dev.parent);
}

static void button_input_event_work_func(struct work_struct *work)
{
	bool value;
	struct privacy_input_event *input =
		container_of(work, struct privacy_input_event, work.work);

	value = gpio_get_value_cansleep(input->button_gpio);
	if (unlikely(value < 0)) {
		/*
		 * gpio read can fail, however we should report button
		 * press in order to notify userspace that privacy
		 * state has been changed.  force it to
		 * !input->last_button_event for that case in the hope
		 * we just missed one press or release.
		 */
		pr_warn_ratelimited("gpio-privacy: gpio %d read failed=%d\n",
				    input->button_gpio, value);
		value = !input->last_button_event;
	} else if (input->button_gpio_flags & OF_GPIO_ACTIVE_LOW) {
		value = !value;
	}

	if (input->last_button_event == value) {
		/*
		 * We can reach here when :
		 * 1) previous press/release has been canceled due to
		 *    debouce interval.
		 * 2) gpio_get_value() failed.
		 *
		 * We should report button press by all means in order for
		 * userspace to be notified about new privacy mode change.
		 * Thus send out an artificial event.
		 */
		input_report_key(input->input_dev, input->code, !value);
		input_sync(input->input_dev);
	} else {
		input->last_button_event = value;
	}
	input_report_key(input->input_dev, input->code, value);
	input_sync(input->input_dev);

	if (input->wakeup_capable)
		pm_relax(input->input_dev->dev.parent);
}

static irqreturn_t privacy_interrupt(int irq, void *arg)
{
	struct privacy_input_event *priv_event = arg;

	if (priv_event->wakeup_capable)
		pm_stay_awake(priv_event->input_dev->dev.parent);

	cancel_delayed_work(&priv_event->work);
	schedule_delayed_work(&priv_event->work,
			msecs_to_jiffies(priv_event->debounce_interval));

	return IRQ_HANDLED;
}

static int privacy_request_interrupts(struct platform_device *pdev)
{
	int i, ret;
	struct privacy_priv *priv = platform_get_drvdata(pdev);
	struct privacy_input_event *priv_event = priv->input_event;

	/*
	 * If there is dedicated gpio for input event use it as
	 * input interrupt source.
	 */
	if (priv_event && gpio_is_valid(priv->input_event->button_gpio)) {
		ret = devm_request_irq(&pdev->dev,
				gpio_to_irq(priv->input_event->button_gpio),
				privacy_interrupt,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"gpio-privacy", priv_event);
		return ret;
	}

	/* Otherwise use state_gpios */
	for (i = 0; i < priv->num_of_state_gpios; i++) {
		ret = devm_request_irq(&pdev->dev,
					gpio_to_irq(priv->state_gpios[i]),
					privacy_interrupt,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
					"gpio-privacy", priv_event);

		if (ret)
			return ret;
	}

	return 0;
}

static int privacy_setup_input_event(struct platform_device *pdev)
{
	int ret;
	struct input_dev *input;
	struct device *dev = &pdev->dev;
	struct privacy_priv *priv = platform_get_drvdata(pdev);

	if (gpio_is_valid(priv->input_event->button_gpio))
		INIT_DELAYED_WORK(&priv->input_event->work,
				  button_input_event_work_func);
	else
		INIT_DELAYED_WORK(&priv->input_event->work,
				  privacy_input_event_work_func);

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = "gpio-privacy";
	input->dev.parent = &pdev->dev;

	input_set_capability(input, EV_KEY, priv->input_event->code);

	priv->input_event->input_dev = input;

	ret = input_register_device(input);
	if (ret)
		return ret;

	return 0;
}

#ifdef CONFIG_OF
static int privacy_input_event_parse_of(struct platform_device *pdev)
{
	int ret;
	enum of_gpio_flags flags;
	struct device_node *node;
	struct device_node *input_event_node;
	struct privacy_priv *priv = platform_get_drvdata(pdev);

	node = pdev->dev.of_node;

	input_event_node = of_get_child_by_name(node, "input_event");
	if (!input_event_node) {
		dev_info(&pdev->dev, "No input event configured\n");
		return 0;
	}

	priv->input_event = devm_kzalloc(&pdev->dev, sizeof(*priv->input_event),
					 GFP_KERNEL);
	if (!priv->input_event)
		return -ENOMEM;

	priv->input_event->desc = of_get_property(input_event_node, "label",
						  NULL);

	if (of_property_read_u32(input_event_node, "linux,code",
				 &priv->input_event->code))
		return -EINVAL;

	if (of_property_read_u32(input_event_node, "debounce-interval",
				 &priv->input_event->debounce_interval))
		priv->input_event->debounce_interval =
			DEFAULT_DEBOUNCE_INTERVAL;

	priv->input_event->button_gpio = of_get_gpio_flags(input_event_node, 0,
							   &flags);
	if (gpio_is_valid(priv->input_event->button_gpio)) {
		priv->input_event->button_gpio_flags = flags;

		ret = devm_gpio_request_one(&pdev->dev,
					    priv->input_event->button_gpio,
					    GPIOF_IN, "privacy-button");
		if (ret)
			return ret;
		dev_info(&pdev->dev, "Button gpio %d configured.\n",
			 priv->input_event->button_gpio);
	} else {
		dev_info(&pdev->dev, "No button gpio configured.\n");
	}

	priv->input_event->wakeup_capable =
			of_property_read_bool(input_event_node, "wakeup-source");

	return 0;
}

static int privacy_parse_of(struct platform_device *pdev)
{
	enum of_gpio_flags flags;
	int gpios, i, gpio, ret, gpio_init_val;
	struct privacy_priv *priv = platform_get_drvdata(pdev);

	gpios = of_gpio_named_count(pdev->dev.of_node, "state-gpios");
	if (gpios < 0)
		return -EINVAL;

	if (gpios == 0) {
		pr_warn("no state-gpios");
	} else {
		priv->state_gpios =
			devm_kzalloc(&pdev->dev,
				     sizeof(*priv->state_gpios) * gpios,
				     GFP_KERNEL);
		if (!priv->state_gpios)
			return -ENOMEM;

		priv->state_gpios_flags =
			devm_kzalloc(&pdev->dev,
				     sizeof(*priv->state_gpios_flags) * gpios,
				     GFP_KERNEL);
		if (!priv->state_gpios_flags)
			return -ENOMEM;

		for (i = 0; i < gpios; i++) {
			gpio = of_get_named_gpio_flags(pdev->dev.of_node,
						       "state-gpios",
						       i, &flags);
			if (!gpio_is_valid(gpio))
				return -EINVAL;

			ret = devm_gpio_request_one(&pdev->dev, gpio,
						    GPIOF_DIR_IN,
						    "gpio-privacy-state-gpios");
			if (ret)
				return ret;

			priv->state_gpios[i] = gpio;
			priv->state_gpios_flags[i] = flags;
		}

		priv->num_of_state_gpios = gpios;
	}

	gpio = of_get_named_gpio_flags(pdev->dev.of_node, "enable-gpio", 0,
				       &flags);
	if (gpio_is_valid(gpio)) {
		if (flags & OF_GPIO_ACTIVE_LOW)
			gpio_init_val = GPIOF_OUT_INIT_HIGH;
		else
			gpio_init_val = GPIOF_OUT_INIT_LOW;

		ret = devm_gpio_request_one(&pdev->dev, gpio,
					    gpio_init_val,
					    "privacy_enable_gpio");
		if (ret)
			return ret;

		priv->enable_gpio = gpio;
		priv->enable_gpio_flags = flags;
	} else {
		return gpio;
	}

	return 0;
}
#else
static int privacy_input_event_parse_of(struct platform_device *pdev)
{
	return -EINVAL;
}

static int privacy_parse_of(struct platform_device *pdev)
{
	return -EINVAL;
}
#endif

static int __privacy_state(struct device *dev)
{
	int i, value;
	struct privacy_priv *priv;
	struct platform_device *pdev = to_platform_device(dev);

	priv = platform_get_drvdata(pdev);

	if (priv->num_of_state_gpios == 0)
		return -EINVAL;

	for (i = 0; i < priv->num_of_state_gpios; i++) {
		value = gpio_get_value_cansleep(priv->state_gpios[i]);
		if ((!value &&
		     priv->state_gpios_flags[i] & OF_GPIO_ACTIVE_LOW) ||
		    (value &&
		     !(priv->state_gpios_flags[i] & OF_GPIO_ACTIVE_LOW)))
			/* return true when any of state shows privacy is on */
			return 1;
	}

	return 0;
}

static int privacy_state(struct device *dev)
{
	int ret;
	struct platform_device *pdev = to_platform_device(dev);
	struct privacy_priv *priv = platform_get_drvdata(pdev);

	mutex_lock(&priv->mutex);
	ret = __privacy_state(dev);
	mutex_unlock(&priv->mutex);

	return ret;
}

static ssize_t show_privacy_state(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int state = privacy_state(dev);

	if (state < 0)
		return state;

	return snprintf(buf, PAGE_SIZE, "%d\n", state);
}

static ssize_t store_privacy_enable(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int i = 0;
	int value;
	int enable;
	const int max_wait = 100;
	struct privacy_priv *priv;
	struct platform_device *pdev = to_platform_device(dev);

	priv = platform_get_drvdata(pdev);

	if (!kstrtoint(buf, 10, &enable)) {
		/* Don't allow userspace to turn off Privacy Mode */
		if (!enable)
			return -EINVAL;

		mutex_lock(&priv->mutex);

		if (priv->enable_gpio_flags & OF_GPIO_ACTIVE_LOW)
			value = !enable;
		else
			value = !!enable;

		gpio_set_value(priv->enable_gpio, value);

		/* wait for privacy enabled for up to 100ms */
		while (i < max_wait) {
			if (__privacy_state(dev))
				break;
			usleep_range(1000, 1100);
			i++;
		}

		gpio_set_value(priv->enable_gpio, !value);

		mutex_unlock(&priv->mutex);

		if (i == max_wait)
			return -ETIMEDOUT;
	} else {
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR(enable, S_IWUSR | S_IWGRP,
		   NULL, store_privacy_enable);
static DEVICE_ATTR(state, S_IRUGO, show_privacy_state, NULL);

static struct attribute *gpio_keys_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_state.attr,
	NULL,
};

static struct attribute_group gpio_keys_attr_group = {
	.attrs = gpio_keys_attrs,
};

static int gpio_privacy_probe(struct platform_device *pdev)
{
	int ret;
	struct privacy_priv *priv;
	struct device *dev = &pdev->dev;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->mutex);

	platform_set_drvdata(pdev, priv);

	ret = privacy_parse_of(pdev);
	if (ret) {
		pr_err("failed to parse device tree = %d\n", ret);
		return ret;
	}

	ret = privacy_input_event_parse_of(pdev);
	if (ret) {
		pr_err("failed to parse input event device tree = %d\n", ret);
		return ret;
	}

	if (priv->input_event) {
		ret = privacy_request_interrupts(pdev);
		if (ret) {
			pr_err("failed to request interrupt = %d\n", ret);
			return ret;
		}

		ret = privacy_setup_input_event(pdev);
		if (ret) {
			pr_err("failed to setup input event = %d\n", ret);
			return ret;
		}
	}

	ret = sysfs_create_group(&dev->kobj, &gpio_keys_attr_group);
	if (ret) {
		pr_err("failed to create sysfs group = %d\n", ret);
		return ret;
	}

	if (priv->input_event != NULL)
		device_init_wakeup(&pdev->dev, priv->input_event->wakeup_capable);

	return 0;
}

static int gpio_privacy_remove(struct platform_device *pdev)
{
	struct privacy_priv *priv;
	struct device *dev = &pdev->dev;
	struct privacy_input_event *priv_event;
	int i;

	priv = platform_get_drvdata(pdev);
	priv_event = priv->input_event;

	for (i = 0; i < priv->num_of_state_gpios; i++)
		devm_free_irq(dev,
			      gpio_to_irq(priv->state_gpios[i]),
			      "gpio-privacy");

	sysfs_remove_group(&dev->kobj, &gpio_keys_attr_group);
	cancel_delayed_work_sync(&priv_event->work);
	pm_relax(priv_event->input_dev->dev.parent);
	mutex_destroy(&priv->mutex);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gpio_privacy_suspend(struct device *dev)
{
	int i, gpios;
	struct privacy_priv *priv;
	struct privacy_input_event *input_event;
	struct platform_device *pdev = to_platform_device(dev);

	priv = platform_get_drvdata(pdev);
	input_event = priv->input_event;

	if (input_event && input_event->wakeup_capable) {
		if (gpio_is_valid(input_event->button_gpio))
			enable_irq_wake(gpio_to_irq(input_event->button_gpio));

		gpios = of_gpio_named_count(pdev->dev.of_node, "state-gpios");
		for (i = 0; i < gpios; i++)
			enable_irq_wake(gpio_to_irq(priv->state_gpios[i]));
	}

	return 0;
}

static int gpio_privacy_resume(struct device *dev)
{
	int i, gpios;
	struct privacy_priv *priv;
	struct privacy_input_event *input_event;
	struct platform_device *pdev = to_platform_device(dev);

	priv = platform_get_drvdata(pdev);
	input_event = priv->input_event;

	if (input_event && input_event->wakeup_capable) {
		if (gpio_is_valid(input_event->button_gpio))
			disable_irq_wake(gpio_to_irq(input_event->button_gpio));

		gpios = of_gpio_named_count(pdev->dev.of_node, "state-gpios");
		for (i = 0; i < gpios; i++)
			disable_irq_wake(gpio_to_irq(priv->state_gpios[i]));
	}

	return 0;
}
#endif

#ifdef CONFIG_OF
static const struct of_device_id privacy_of_table[] = {
	{ .compatible = "gpio-privacy", },
	{ },
};
MODULE_DEVICE_TABLE(of, privacy_of_table);
#endif

static SIMPLE_DEV_PM_OPS(gpio_privacy_pm_ops, gpio_privacy_suspend, gpio_privacy_resume);

static struct platform_driver gpio_privacy_driver = {
	.driver = {
		.name = "gpio-privacy",
#ifdef CONFIG_PM_SLEEP
		.pm = &gpio_privacy_pm_ops,
#endif
		.of_match_table	= of_match_ptr(privacy_of_table),
	},
	.probe	= gpio_privacy_probe,
	.remove	= gpio_privacy_remove,
};

static int __init gpio_privacy_init(void)
{
	return platform_driver_register(&gpio_privacy_driver);
}

static void __exit gpio_privacy_exit(void)
{
	platform_driver_unregister(&gpio_privacy_driver);
}

module_init(gpio_privacy_init);
module_exit(gpio_privacy_exit);
MODULE_LICENSE("GPL");
