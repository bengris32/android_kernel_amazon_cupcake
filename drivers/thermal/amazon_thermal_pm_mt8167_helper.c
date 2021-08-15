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

#include <linux/printk.h>
#include <soc/mediatek/spm.h>

int set_suspend_wakeup_timer_s(unsigned int seconds)
{
	int ret;

	ret = spm_set_suspend_ctrl_timer(seconds);
	if (ret) {
		pr_err("Suspend ctrl timer set failed with err(%d)", ret);
		return ret;
	}

	ret = spm_set_pcm_timer_state(true);
	if (ret)
		pr_err("Pcm timer enable failed with err(%d)", ret);

	return ret;
}
