/*
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/errno.h>

#ifdef CONFIG_MACH_MT8167
int set_suspend_wakeup_timer_s(unsigned int seconds);
#else
static inline int set_suspend_wakeup_timer_s(unsigned int seconds)
{
	return -EOPNOTSUPP;
}
#endif
