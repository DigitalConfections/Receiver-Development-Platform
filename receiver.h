/**********************************************************************************************
 * Copyright © 2017 Digital Confections LLC
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of 
 * this software and associated documentation files (the "Software"), to deal in the 
 * Software without restriction, including without limitation the rights to use, copy, 
 * modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, 
 * and to permit persons to whom the Software is furnished to do so, subject to the 
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR 
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE 
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 *
 **********************************************************************************************
 *
 * receiver.h
 *
 */ 


#ifndef RECEIVER_H_
#define RECEIVER_H_

#include "defs.h"

typedef uint16_t SignalStrength;
typedef int16_t Attenuation;

#define RADIO_NUMBER_OF_BANDS 2
#define RADIO_IF_FREQUENCY 10700000
#define RADIO_MINIMUM_RECEIVE_FREQ 3500000

typedef enum 
{
	BAND_2M = 0,
	BAND_80M = 1,
	BAND_INVALID
} RadioBand;

/* 
	There are two VFO frequencies that will mix with the received signal to produce the 
	IF frequency: rf + IF, and rf - IF. The receiver will support either VFO setting. The
	following typedef is used for variables holding that value type.
*/
typedef enum
{
	VFO_2M_LOW_80M_LOW = 0, /* VFO = rf - IF */
	VFO_2M_HIGH_80M_LOW = 1, 
	VFO_2M_LOW_80M_HIGH = 2, 
	VFO_2M_HIGH_80M_HIGH = 3
} RadioVFOConfig;

typedef enum
{
	MEMORY_1 = 1,
	MEMORY_2,
	MEMORY_3,
	MEMORY_4,
	MEMORY_5,
	ILLEGAL_MEMORY
} MemoryStore;

#define EEPROM_2M_MEM1_DEFAULT 144250000
#define EEPROM_2M_MEM2_DEFAULT 144300000
#define EEPROM_2M_MEM3_DEFAULT 144350000
#define EEPROM_2M_MEM4_DEFAULT 144400000
#define EEPROM_2M_MEM5_DEFAULT 144450000
#define EEPROM_80M_MEM1_DEFAULT 3579500
#define EEPROM_80M_MEM2_DEFAULT 3560000
#define EEPROM_80M_MEM3_DEFAULT 3565000
#define EEPROM_80M_MEM4_DEFAULT 3570000
#define EEPROM_80M_MEM5_DEFAULT 3575000


#define DEFAULT_RX_2M_FREQUENCY 145520000
#define DEFAULT_RX_80M_FREQUENCY 3550000
#define DEFAULT_RX_ACTIVE_BAND BAND_2M
#define RX_MINIMUM_2M_FREQUENCY 144000000
#define RX_MAXIMUM_2M_FREQUENCY 148000000
#define RX_MINIMUM_80M_FREQUENCY 3500000
#define RX_MAXIMUM_80M_FREQUENCY 4000000

typedef struct
{
//	BatteryLevel battery;
//	BatteryType batteryType;
//	SignalStrength rssi;
//	SignalStrength rfsi;
	RadioBand bandSetting;
	Frequency_Hz currentMemoryFrequency;
	Frequency_Hz currentUserFrequency;
//	Frequency_Hz freqSetting[RADIO_NUMBER_OF_BANDS];
//	VolumeSetting afGain;
//	VolumeSetting toneGain;
//	Attenuation atten;
} Receiver;

typedef enum
{
	FREQ_SOURCE_NOT_SPECIFIED,
	MEM1_2M_SETTING,
	MEM2_2M_SETTING,
	MEM3_2M_SETTING,
	MEM4_2M_SETTING,
	MEM5_2M_SETTING,
	MEM1_80M_SETTING,
	MEM2_80M_SETTING,
	MEM3_80M_SETTING,
	MEM4_80M_SETTING,
	MEM5_80M_SETTING
} FrequencySource;

#ifdef INCLUDE_RECEIVER_SUPPORT
/**
*/
	void init_receiver(Receiver* rx);

/**
*/
	void rxSetBand(RadioBand band);

/**
*/
	RadioBand rxGetBand(void);

/**
*/
	BOOL rxSetFrequency(Frequency_Hz *freq);

/**
*/
	Frequency_Hz rxGetFrequency(void);

/**
*/
	void rxSetVFOConfiguration(RadioVFOConfig config);

#endif // #ifdef INCLUDE_RECEIVER_SUPPORT

/**
*/
RadioBand bandForFrequency(Frequency_Hz freq);

#endif /* RECEIVER_H_ */