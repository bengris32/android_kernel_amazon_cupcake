/*
 * external_amp_mute.h  -  Machine driver operation
 *
 * Copyright (c) 2019 Amazon Inc.
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

#ifndef __EXTERNAL_AMP_MUTE_H
#define __EXTERNAL_AMP_MUTE_H

/*
 * external_amp_mute
 * Mute External Amplifier.
 *
 */

#ifdef CONFIG_SND_SOC_EXT_AMP_MUTE
void external_amp_mute(bool mute);
#else
/*
 * Donut's machine driver defines this function for 64bit kernel.
 * Puffin devices use 32 bit kernel where Donut machine driver is not used
 * so define a stub function for 32 bit kernel
 */
void external_amp_mute(bool mute) {};
#endif


#endif /* __EXTERNAL_AMP_MUTE_H */
