/*
* Copyright (C) 2016 MediaTek Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See http://www.gnu.org/licenses/gpl-2.0.html for more details.
*/

#ifndef __TEMPERATURE_FACTORY_H__
#define __TEMPERATURE_FACTORY_H__

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/atomic.h>

#include <hwmsensor.h>
#include <hwmsen_dev.h>
#include <sensors_io.h>
#include <hwmsen_helper.h>
#include <batch.h>


#include "temperature.h"
#include "cust_temp.h"


extern struct temp_context *temp_context_obj;


int temp_factory_device_init(void);

#endif

