/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef __MTK_TS_BTS_H__
#define __MTK_TS_BTS_H__

#define NTC_TABLE_SIZE 34

extern int IMM_IsAdcInitReady(void);

extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int *rawdata);

typedef struct {
	__s32 BTS_Temp;
	__s32 TemperatureR;
} BTS_TEMPERATURE;

extern BTS_TEMPERATURE BTS_Temperature_Table[NTC_TABLE_SIZE];

/* AP_NTC_BL197 */
extern BTS_TEMPERATURE BTS_Temperature_Table1[NTC_TABLE_SIZE];

/* AP_NTC_TSM_1 */
extern BTS_TEMPERATURE BTS_Temperature_Table2[NTC_TABLE_SIZE];

/* AP_NTC_10_SEN_1 */
extern BTS_TEMPERATURE BTS_Temperature_Table3[NTC_TABLE_SIZE];

/* AP_NTC_10(TSM0A103F34D1RZ) */
extern BTS_TEMPERATURE BTS_Temperature_Table4[NTC_TABLE_SIZE];

/* AP_NTC_47 */
extern BTS_TEMPERATURE BTS_Temperature_Table5[NTC_TABLE_SIZE];

/* NTCG104EF104F(100K) */
extern BTS_TEMPERATURE BTS_Temperature_Table6[NTC_TABLE_SIZE];

/* NCP15WF104F03RC(100K) */
extern BTS_TEMPERATURE BTS_Temperature_Table7[NTC_TABLE_SIZE];

/* NCP15XH103(10K) */
extern BTS_TEMPERATURE BTS_Temperature_Table8[NTC_TABLE_SIZE];

/* NCP03WF104F05RL(100K) */
extern BTS_TEMPERATURE BTS_Temperature_Table9[NTC_TABLE_SIZE];

#endif /* __MTK_TS_BTS_H__ */
