/**********************************************************************************************
 * Copyright � 2017 Digital Confections LLC
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
 * transmitter.h
 *
 */


#ifndef TRANSMITTER_H_
#define TRANSMITTER_H_

#include "defs.h"
#include "si5351.h"
#include "mcp23017.h"

typedef uint16_t SignalStrength;
typedef int16_t Attenuation;

#define RADIO_NUMBER_OF_BANDS 2
#define RADIO_IF_FREQUENCY ((Frequency_Hz)10700000)
#define RADIO_MINIMUM_RECEIVE_FREQ ((Frequency_Hz)3500000)
#define MAX_TX_POWER_80M_MW 2000
#define MAX_TX_POWER_2M_MW 1000

/*
 * Define clock pins
 */
#define TX_CLOCK_VHF_FM SI5351_CLK2
#define TX_CLOCK_HF_0 SI5351_CLK1
#define TX_CLOCK_VHF SI5351_CLK0

typedef enum
{
	BAND_2M = 0,
	BAND_80M = 1,
	BAND_INVALID
} RadioBand;

typedef enum
{
	MODE_CW = 0,
	MODE_AM = 1,
	MODE_INVALID
} Modulation;

/*
 *       There are two VFO frequencies that will mix with the received signal to produce the
 *       IF frequency: rf + IF, and rf - IF. The receiver will support either VFO setting. The
 *       following typedef is used for variables holding that value type.
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

typedef enum
{
	BIAS_POWERED_OFF,
	BIAS_POWER_UP_STEP1,
	BIAS_POWER_UP_STEP2,
	BIAS_POWERED_UP,
	BIAS_POWER_OFF_STEP1,
	BIAS_POWER_OFF_STEP2
	} BiasState;

#define BIAS_DELAY_PERIOD_TICKS 100

#define DEFAULT_TX_2M_FREQUENCY 145566000
#define DEFAULT_TX_80M_FREQUENCY 3550000
#define DEFAULT_RTTY_OFFSET_FREQUENCY 170
#define DEFAULT_TX_ACTIVE_BAND BAND_80M
#define DEFAULT_TX_2M_MODULATION MODE_AM

#define DEFAULT_80M_POWER_TABLE ((const uint8_t[]){0, 1, 5, 20, 35, 50, 65, 80, 95, 110, 125, 140, 155, 170, 185, 200, 215, 230, 245, 250, 250, 250})

#define DEFAULT_2M_AM_POWER_TABLE ((const uint8_t[]){250, 240, 228, 216, 204, 192, 180, 168, 156, 144, 132, 120, 108, 96, 84, 72, 60, 48, 36, 24, 12, 1})
#define DEFAULT_2M_AM_DRIVE_HIGH_TABLE ((const uint8_t[]){150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240})
#define DEFAULT_2M_AM_DRIVE_LOW_TABLE ((const uint8_t[]){150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240})

#define DEFAULT_2M_CW_POWER_TABLE ((const uint8_t[]){250, 240, 228, 216, 204, 192, 180, 168, 156, 144, 132, 120, 108, 96, 84, 72, 60, 48, 36, 24, 12, 1})
#define DEFAULT_2M_CW_DRIVE_TABLE ((const uint8_t[]){150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240})

#define TX_MINIMUM_2M_FREQUENCY 144000000
#define TX_MAXIMUM_2M_FREQUENCY 148000000
#define TX_MINIMUM_80M_FREQUENCY 3500000
#define TX_MAXIMUM_80M_FREQUENCY 8000000

#define DEFAULT_TX_2M_POWER_MW 100
#define DEFAULT_TX_80M_POWER_MW 500

#define MIN_2M_BIAS_SETTING 80 /* minimum safe BIAS DAC setting = -0.8V*/
#define MAX_80M_PWR_SETTING 255 /* maximum safe DAC setting */

#define DEFAULT_AM_DRIVE_LEVEL_HIGH 195
#define DEFAULT_AM_DRIVE_LEVEL_LOW 0
#define DEFAULT_CW_DRIVE_LEVEL 195

#define MAX_2M_AM_DRIVE_LEVEL 195
#define MAX_2M_CW_DRIVE_LEVEL 195

#define MINUS2V_BIAS 250

typedef struct
{
/*	BatteryLevel battery; */
/*	BatteryType batteryType; */
/*	SignalStrength rssi; */
/*	SignalStrength rfsi; */
	RadioBand bandSetting;
	Frequency_Hz currentMemoryFrequency;
	Frequency_Hz currentUserFrequency;
/*	Frequency_Hz freqSetting[RADIO_NUMBER_OF_BANDS]; */
/*	VolumeSetting afGain; */
/*	VolumeSetting toneGain; */
/*	Attenuation atten; */
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

typedef enum {
	ANT_80M_DET0,
	ANT_80M_DET1,
	ANT_2M_DET,
	VHF_ENABLE,
	HF_ENABLE,
	NOT_USED_TXBIT,
	T_ENABLE,
	R_ENABLE
} TxBit;

#ifdef INCLUDE_TRANSMITTER_SUPPORT
/**
 */
	EC init_transmitter(void);

/**
*/
	BOOL txSleeping(BOOL enableSleep);

/**
 */
	void storeTransmitterValues(void);

/**
 */
	void txSetBand(RadioBand band, BOOL enable);

/**
 */
	RadioBand txGetBand(void);

/**
 */
	void txSetModulation(Modulation mode);

/**
 */
	Modulation txGetModulation(void);

/**
 */
	BOOL txAMModulationEnabled(void);

/**
 */
	EC txSetParameters(uint16_t* power_mW, RadioBand* band, BOOL* enableAM, BOOL* enableDriverPwr);

/**
 */
	BOOL txSetFrequency(Frequency_Hz *freq);

/**
 */
	Frequency_Hz txGetFrequency(void);

#endif  /* #ifdef INCLUDE_TRANSMITTER_SUPPORT */

/**
 */
RadioBand bandForFrequency(Frequency_Hz freq);

/**
 */
void keyTransmitter(BOOL on);

/**
 */
BOOL powerToTransmitterDriver(BOOL on);

/**
 */
BOOL txMilliwattsToSettings(uint16_t powerMW, uint8_t* powerLevel, uint8_t* modLevelHigh, uint8_t* modLevelLow);

/**
Returns TRUE if an antenna for the active band is connected to the transmitter
 */
BOOL txIsAntennaForBand(void);

/**
 */
void initializeTransmitterEEPROMVars(void);

#endif  /* TRANSMITTER_H_ */