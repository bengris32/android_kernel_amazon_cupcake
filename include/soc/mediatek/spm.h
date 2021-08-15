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

#ifndef MTK_SPM_H
#define MTK_SPM_H

#include <linux/types.h>

int spm_set_pcm_timer_state(bool state);
int spm_set_suspend_ctrl_timer(unsigned int time_s);

#endif
