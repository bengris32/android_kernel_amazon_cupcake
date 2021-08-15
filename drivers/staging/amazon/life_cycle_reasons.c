/*
 * life_cycle_reasons.c
 *
 * Device Life Cycle Reasons information
 *
 * Copyright 2011-2018 Amazon.com, Inc. or its Affiliates. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/life_cycle_reasons.h>

#define DEV_LCR_VERSION "2.0"
#define DEV_LCR_PROC_NAME "life_cycle_reason"
#define MAX_SIZE 10

static const char lcr_normal[] = "LCR_normal";
static const char lcr_abnormal[] = "LCR_abnormal";

/* platform specific lcr_data */
static const struct life_cycle_reasons_data lcr_data[] = {
	/* The default value in case we fail to find a good reason */
	{
		LIFE_CYCLE_NOT_AVAILABLE,
		"Life Cycle Reason Not Available",
		lcr_abnormal
	},
	/* Normal software shutdown */
	{
		SHUTDOWN_BY_SW,
		"Software Shutdown",
		lcr_normal
	},
	/* Device shutdown due to long pressing of power key */
	{
		SHUTDOWN_BY_LONG_PWR_KEY_PRESS,
		"Long Pressed Power Key Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to pressing of power key */
	{
		SHUTDOWN_BY_PWR_KEY,
		"Pressed Power Key Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated PMIC */
	{
		THERMAL_SHUTDOWN_REASON_PMIC,
		"PMIC Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated battery */
	{
		THERMAL_SHUTDOWN_REASON_BATTERY,
		"Battery Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated Soc chipset */
	{
		THERMAL_SHUTDOWN_REASON_SOC,
		"SOC Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated PCB main board */
	{
		THERMAL_SHUTDOWN_REASON_PCB,
		"PCB Overheated Thermtal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated WIFI chipset */
	{
		THERMAL_SHUTDOWN_REASON_WIFI,
		"WIFI Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated BTS */
	{
		THERMAL_SHUTDOWN_REASON_BTS,
		"BTS Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated Modem chipset */
	{
		THERMAL_SHUTDOWN_REASON_MODEM,
		"Modem Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated Skin virtual sensor */
	{
		THERMAL_SHUTDOWN_REASON_SVS,
		"Skin Virtual Sensor Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated Case virtual sensor */
	{
		THERMAL_SHUTDOWN_REASON_CVS,
		"Case Virtual Sensor Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated Tweeter virtual sensor */
	{
		THERMAL_SHUTDOWN_REASON_TVS,
		"Tweeter Virtual Sensor Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to overheated Body virtual sensor */
	{
		THERMAL_SHUTDOWN_REASON_BVS,
		"Body Virtual Sensor Overheated Thermal Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to extream low battery level */
	{
		LIFE_CYCLE_SMODE_LOW_BATTERY,
		"Low Battery Shutdown",
		lcr_normal
	},
	/* Device shutdown due to sudden power lost */
	{
		SHUTDOWN_BY_SUDDEN_POWER_LOSS,
		"Sudden Power Loss Shutdown",
		lcr_abnormal
	},
	/* Device shutdown due to unknown reason */
	{
		SHUTDOWN_BY_UNKNOWN_REASONS,
		"Unknown Shutdown",
		lcr_abnormal
	},
	/* Device powerup due to power key pressed */
	{
		COLDBOOT_BY_POWER_KEY,
		"Cold Boot By Power Key",
		lcr_normal
	},
	/* Device powerup due to power supply plugged */
	{
		COLDBOOT_BY_POWER_SUPPLY,
		"Cold Boot By Power Supply",
		lcr_normal
	},
	/* Device powerup due to usb cable plugged*/
	{
		COLDBOOT_BY_USB,
		"Cold Boot By USB Charger",
		lcr_normal
	},
	/* Device reboot due software reboot */
	{
		WARMBOOT_BY_SW,
		"Warm Boot By Software",
		lcr_normal
	},
	/* Device reboot due kernel panic */
	{
		WARMBOOT_BY_KERNEL_PANIC,
		"Warm Boot By Kernel Panic",
		lcr_abnormal
	},
	/* Device reboot due software watchdog timeout */
	{
		WARMBOOT_BY_KERNEL_WATCHDOG,
		"Warm Boot By Kernel Watchdog",
		lcr_abnormal
	},
	/* Device reboot due kernel HW watchdog timeout */
	{
		WARMBOOT_BY_HW_WATCHDOG,
		"Warm Boot By HW Watchdog",
		lcr_abnormal
	},
	/* Device powerup into charger mode */
	{
		LIFE_CYCLE_SMODE_WARM_BOOT_USB_CONNECTED,
		"Power Off Charging Mode",
		lcr_normal
	},
	/* Device reboot into factory reset mode */
	{
		LIFE_CYCLE_SMODE_FACTORY_RESET,
		"Factory Reset Reboot",
		lcr_normal
	},
	/* Device reboot into recovery OTA mode */
	{
		LIFE_CYCLE_SMODE_OTA,
		"OTA Reboot",
		lcr_normal
	},
};

static struct proc_dir_entry *life_cycle_reason_file;
static bool is_charger_mode;

/* Structure to store the Life Cycle Reason */
struct dev_lcr {
	char version[MAX_SIZE];
	u8 life_cycle_reason_idx;
	struct life_cycle_reasons_ops *lcr_ops;
};

static struct dev_lcr *p_dev_lcr;

static int search_lcr_table(const struct life_cycle_reasons_data *table,
			    int num, enum life_cycle_reason reason)
{
	int i;
	/* start from table[1], table[0] is the default value */
	for (i = 1; i < num; i++) {
		if (table[i].reason_value == reason)
			return i;
	}
	/* fails to match with any predefined reason, print a warnning msg */
	pr_warn("the lcr_reason %d is not in the pre-defined table\n", reason);

	/* fails to find a good reason, return the default index= 0 */
	return 0;
}

static int life_cycle_reason_lookup(void)
{
	enum life_cycle_reason boot_reason = LIFE_CYCLE_NOT_AVAILABLE;
	enum life_cycle_reason shutdown_reason = LIFE_CYCLE_NOT_AVAILABLE;
	enum life_cycle_reason thermal_shutdown_reason =
		LIFE_CYCLE_NOT_AVAILABLE;
	enum life_cycle_reason s_mode = LIFE_CYCLE_NOT_AVAILABLE;
	int table_num = ARRAY_SIZE(lcr_data);
	int index = 0;

	if (!p_dev_lcr || !p_dev_lcr->lcr_ops) {
		pr_err("the life cycle driver is not initialized\n");
		return -EAGAIN;
	}

	if (!((p_dev_lcr->lcr_ops->read_boot_reason) &&
	      (p_dev_lcr->lcr_ops->read_shutdown_reason) &&
	      (p_dev_lcr->lcr_ops->read_thermal_shutdown_reason) &&
	      (p_dev_lcr->lcr_ops->read_special_mode))) {
		pr_err("no platform supported\n");
		return -EINVAL;
	}

	p_dev_lcr->lcr_ops->read_boot_reason(&boot_reason);
	p_dev_lcr->lcr_ops->read_shutdown_reason(&shutdown_reason);
	p_dev_lcr->lcr_ops->read_thermal_shutdown_reason(
		&thermal_shutdown_reason);
	p_dev_lcr->lcr_ops->read_special_mode(&s_mode);

	/* step 1, find abnormal thermal shutdown reason if it's valid */
	if (thermal_shutdown_reason > 0) {
		index = search_lcr_table(lcr_data, table_num,
					 thermal_shutdown_reason);
		if (index)
			goto found;
	}

	/* step 2, find shutdown reason if it's valid */
	if (shutdown_reason > 0) {
		index = search_lcr_table(lcr_data, table_num, shutdown_reason);
		if (index)
			goto found;
	}

	/* step 3, find reboot reason if it's valid */
	if (boot_reason > 0) {
		index = search_lcr_table(lcr_data, table_num, boot_reason);
		/* Special mode can also cause SW warmboot */
		if (index && (boot_reason != WARMBOOT_BY_SW))
			goto found;
	}

	/* step 4, find special mode reason if it's valid */
	if (s_mode > 0) {
		index = search_lcr_table(lcr_data, table_num, s_mode);
		if (index)
			goto found;
	}

found:
	p_dev_lcr->life_cycle_reason_idx = index;
	return index;
}

static int life_cycle_reason_show(struct seq_file *m, void *v)
{
	int index;

	if (p_dev_lcr) {
		index = p_dev_lcr->life_cycle_reason_idx;
		seq_printf(m, "%s\n",
			   lcr_data[index].life_cycle_reasons);
	} else {
		seq_puts(m, "life cycle reasons driver is not initialized");
	}
	return 0;
}

static int life_cycle_reason_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, life_cycle_reason_show, NULL);
}

static const struct file_operations life_cycle_reason_proc_fops = {
	.open = life_cycle_reason_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void life_cycle_reason_proc_init(void)
{
	life_cycle_reason_file = proc_create(DEV_LCR_PROC_NAME,
					     0444, NULL,
					     &life_cycle_reason_proc_fops);
	if (!life_cycle_reason_file)
		pr_err("%s: Can't create life cycle reason proc entry\n",
		       __func__);
}

static void life_cycle_reason_proc_done(void)
{
	remove_proc_entry(DEV_LCR_PROC_NAME, NULL);
}

/*
 * life_cycle_set_boot_reason
 * Description: this function will set the boot reason which
 * causing device booting
 * @boot_reason next boot reason
 * @Return 0 on success, -1 on failure
 */
int life_cycle_set_boot_reason(enum life_cycle_reason boot_reason)
{
	if (!p_dev_lcr || !p_dev_lcr->lcr_ops) {
		pr_err("%s: the life cycle driver is not initialized\n",
		       __func__);
		return -EAGAIN;
	}

	if (!p_dev_lcr->lcr_ops->write_boot_reason) {
		pr_err("%s: no platform supported\n", __func__);
		return -EINVAL;
	}
	p_dev_lcr->lcr_ops->write_boot_reason(boot_reason);

	return 0;
}
EXPORT_SYMBOL(life_cycle_set_boot_reason);

/*
 * life_cycle_set_shutdown_reason
 * Description: this function will set the Shutdown reason
 * which causing device shutdown
 * @shutdown_reason the shutdown reason excluded thermal shutdown
 * @Return 0 on success, -1 on failure
 */
int life_cycle_set_shutdown_reason(enum life_cycle_reason shutdown_reason)
{
	if (!p_dev_lcr || !p_dev_lcr->lcr_ops) {
		pr_err("%s: the life cycle driver is not initialized\n",
		       __func__);
		return -EAGAIN;
	}

	if (!p_dev_lcr->lcr_ops->write_shutdown_reason) {
		pr_err("%s: no platform supported\n", __func__);
		return -EINVAL;
	}
	p_dev_lcr->lcr_ops->write_shutdown_reason(shutdown_reason);

	return 0;
}
EXPORT_SYMBOL(life_cycle_set_shutdown_reason);

/*
 * life_cycle_set_thermal_shutdown_reason
 * Description: this function will set the Thermal Shutdown reason
 * which causing device shutdown
 * @thermal_shutdown_reason the thermal shutdown reason
 * @Return 0 on success, -1 on failure
 */
int life_cycle_set_thermal_shutdown_reason(
	enum life_cycle_reason thermal_shutdown_reason)
{
	if (!p_dev_lcr || !p_dev_lcr->lcr_ops) {
		pr_err("%s: the life cycle driver is not initialized\n",
		       __func__);
		return -EAGAIN;
	}

	if (!p_dev_lcr->lcr_ops->write_thermal_shutdown_reason) {
		pr_err("%s: no platform supported\n", __func__);
		return -EINVAL;
	}
	p_dev_lcr->lcr_ops->write_thermal_shutdown_reason(
		thermal_shutdown_reason);

	return 0;
}
EXPORT_SYMBOL(life_cycle_set_thermal_shutdown_reason);

/*
 * life_cycle_set_special_mode
 * Description: this function will set the special mode which causing
 * device life cycle change
 * @life_cycle_special_mode the special mode value
 * @Return 0 on success, -1 on failure
 */
int life_cycle_set_special_mode(enum life_cycle_reason life_cycle_special_mode)
{
	if (!p_dev_lcr || !p_dev_lcr->lcr_ops) {
		pr_err("%s: the life cycle driver is not initialized\n",
		       __func__);
		return -EAGAIN;
	}

	if (!p_dev_lcr->lcr_ops->write_special_mode) {
		pr_err("%s: no platform supported\n", __func__);
		return -EINVAL;
	}
	p_dev_lcr->lcr_ops->write_special_mode(life_cycle_special_mode);

	return 0;
}
EXPORT_SYMBOL(life_cycle_set_special_mode);

int register_life_cycle_reasons_dev(struct life_cycle_reasons_ops *lcr_ops)
{
	if (!lcr_ops) {
		pr_err("%s: failed to register lcr driver\n",
		       __func__);
		return -EINVAL;
	}

	if (!p_dev_lcr) {
		pr_err("%s: the life cycle driver is not initialized\n",
		       __func__);
		return -EAGAIN;
	}
	p_dev_lcr->lcr_ops = lcr_ops;

	/* look up the life cycle reason.*/
	/* if fails, prints out a warnning but continues.*/
	if (!life_cycle_reason_lookup())
		pr_warn("didn't find the right lcr reason for previous booting\n");

	/* clean up the life cycle reason settings */
	if (!is_charger_mode) {
		pr_info("not in charger mode. Reset LCR registers.\n");
		if (p_dev_lcr->lcr_ops->lcr_reset)
			p_dev_lcr->lcr_ops->lcr_reset();
	} else {
		pr_info("in charger mode. Don't reset LCR registers.\n");
	}

	pr_info("life cycle reason is %s\n",
		lcr_data[p_dev_lcr->life_cycle_reason_idx].life_cycle_reasons);

	return 0;
}
EXPORT_SYMBOL(register_life_cycle_reasons_dev);

static int __init dev_lcr_init(void)
{
	pr_info("life cycle reasons driver init\n");
	p_dev_lcr = kzalloc(sizeof(*p_dev_lcr), GFP_KERNEL);
	if (!p_dev_lcr) {
		pr_err("%s: kmalloc allocation failed\n", __func__);
		return -ENOMEM;
	}

	strncpy(p_dev_lcr->version, DEV_LCR_VERSION, MAX_SIZE);

	/* set the default life cycle reason */
	p_dev_lcr->life_cycle_reason_idx = LIFE_CYCLE_NOT_AVAILABLE;

	/* create the proc entry for life cycle reason */
	life_cycle_reason_proc_init();

	return 0;
}

static void __exit dev_lcr_cleanup(void)
{
	life_cycle_reason_proc_done();
	kfree(p_dev_lcr);
}

static int __init lcr_bootmode(char *options)
{
	pr_info("bootmode=%s\n", options);
	if (!strcmp("charger", options))
		is_charger_mode = true;

	return 0;
}
__setup("androidboot.mode=", lcr_bootmode);

early_initcall(dev_lcr_init);
module_exit(dev_lcr_cleanup);
MODULE_LICENSE("GPL v2");
