/*
 * dummy_ws_holder.c
 *
 * Copyright 2021 Amazon.com, Inc. or its Affiliates. All rights reserved.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif

#ifdef CONFIG_PM_WAKELOCKS
static struct wakeup_source dwh_ws;
#else
static struct wake_lock dwh_ws;
#endif

#ifdef CONFIG_PM_WAKELOCKS
static int __init dummy_ws_holder_init(void)
{
	wakeup_source_init(&dwh_ws, "dummy_wakelock");
	__pm_stay_awake(&dwh_ws);

	return 0;
}
#else
static int __init dummy_ws_holder_init(void)
{
	wake_lock_init(&dwh_ws, WAKE_LOCK_SUSPEND, "dummy_wakelock");
	wake_lock(&dwh_ws)

	return 0;
}
#endif

#ifdef CONFIG_PM_WAKELOCKS
static void __exit dummy_ws_holder_exit(void)
{
	__pm_relax(&dwh_ws);
	wakeup_source_trash(&dwh_ws);
}
#else
static void __exit dummy_ws_holder_exit(void)
{
	wake_unlock(&dwh_ws);
	wake_lock_destroy(&dwh_ws);
}
#endif

module_init(dummy_ws_holder_init);
module_exit(dummy_ws_holder_exit);
MODULE_LICENSE("GPL v2");
