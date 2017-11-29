
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
**********************************************************************************************/

#ifndef DEFS_H
#define DEFS_H

/* #define F_CPU 16000000UL / * gets declared in makefile * / */

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

/******************************************************
 * SET THE ONE PRODUCT TO BE BUILT TO NON-ZERO (ONE-AT-A-TIME PLEASE), OTHER PRODUCTS SHOULD BE DEFINED AS ZERO */
#define PRODUCT_CONTROL_HEAD 0
#define PRODUCT_DUAL_BAND_RECEIVER 1
#define PRODUCT_TEST_INSTRUMENT_HEAD 0
#define PRODUCT_TEST_DIGITAL_INTERFACE 0
/*******************************************************/

/******************************************************
 * Set the text that gets displayed to the user */
#define SW_REVISION "0.7.10"

//#define DEBUG_FUNCTIONS_ENABLE

#if PRODUCT_CONTROL_HEAD
   #define PRODUCT_NAME_SHORT "Control Head"
   #define PRODUCT_NAME_LONG "Control Head with 2x20 Display"
   #define EXCLUDE_SI5351_SUPPORT
#elif PRODUCT_DUAL_BAND_RECEIVER
   #define PRODUCT_NAME_SHORT "ARDF Rx"
   #define PRODUCT_NAME_LONG "ARDF Dual-Band Receiver"
#elif PRODUCT_TEST_DIGITAL_INTERFACE
   #define PRODUCT_NAME_SHORT "Interface"
   #define PRODUCT_NAME_LONG "Interface"
#else
   #define PRODUCT_NAME_SHORT "RDP"
   #define PRODUCT_NAME_LONG "RDP"
#endif
/*******************************************************/

/******************************************************
 * Include only the necessary hardware support */
#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD
   #define INCLUDE_ST7036_SUPPORT
   #define INCLUDE_DS3231_SUPPORT // Maxim RTC
	/* TODO: Add LSM303DLHC compass module support
	 * TODO: Add GPS support (http://adafruit/3133) */
#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE
   #define INCLUDE_SI5351_SUPPORT // Silicon Labs Programmable Clock
   //#define INCLUDE_DS3231_SUPPORT // Maxim RTC
   #define INCLUDE_PCF8574_SUPPORT
   #define INCLUDE_RECEIVER_SUPPORT
	/* TODO: Add DAC081C085 support
	 * TODO: Add MAX5478EUD+ support
	 * TODO: Add AT24CS01-STUM support */
#endif
/*******************************************************/

/******************************************************
 * Include only the necessary software support */
#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD
   #define ENABLE_1_SEC_INTERRUPTS
#else
   #define ENABLE_1_SEC_INTERRUPTS
#endif
/*******************************************************/

#ifndef SELECTIVELY_DISABLE_OPTIMIZATION
	#define SELECTIVELY_DISABLE_OPTIMIZATION
#endif

/******************************************************
 * EEPROM definitions */
#define EEPROM_INITIALIZED_FLAG 0xA5
#define EEPROM_BACKLIGHT_DEFAULT BL_LOW
#define EEPROM_TONE_VOLUME_DEFAULT 60

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

   #define EEPROM_CONTRAST_DEFAULT 0x03

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

   #define EEPROM_MAIN_VOLUME_DEFAULT 11

#endif  /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */


#if PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

   #define EEPROM_SI5351_CALIBRATION_DEFAULT 0x00
   #define EEPROM_CLK0_OUT_DEFAULT 133000000
   #define EEPROM_CLK1_OUT_DEFAULT 70000000
   #define EEPROM_CLK2_OUT_DEFAULT 10700000
   #define EEPROM_CLK0_ONOFF_DEFAULT OFF
   #define EEPROM_CLK1_ONOFF_DEFAULT OFF
   #define EEPROM_CLK2_ONOFF_DEFAULT OFF

#endif  /* PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE */

/******************************************************
 * General definitions for making the code easier to understand */
#define ROTARY_SYNC_DELAY 100

#define         SDA_PIN (1 << PINC4)
#define         SCL_PIN (1 << PINC5)
#define         I2C_PINS (SCL_PIN | SDA_PIN)

#ifndef FALSE
   #define FALSE 0
#endif

#ifndef TRUE
   #define TRUE !FALSE
#endif

#ifndef BOOL
	typedef uint8_t BOOL;
#endif

#ifndef Frequency_Hz
	typedef uint32_t Frequency_Hz;
#endif

#define ON              1
#define OFF             0
#define UNDETERMINED 3

#define ADC_REF_VOLTAGE_mV 3300UL

#define MAX( a, b ) ( ( a > b) ? a : b )
#define MIN( a, b ) ( ( a > b) ? b : a )

typedef enum
{
	DOWN = -1,
	NOCHANGE = 0,
	UP = 1,
	SETTOVALUE
} IncrType;

#define QUAD_MASK 0xC0
#define QUAD_A 7
#define QUAD_B 6

#define MAX_TONE_VOLUME_SETTING 0xFF
#define MAX_MAIN_VOLUME_SETTING 15

#define POWER_OFF_DELAY 5000
#define BACKLIGHT_OFF_DELAY 5000
#define BACKLIGHT_ALWAYS_ON 65535
#define HEADPHONE_REMOVED_DELAY 100
#define POWERUP_LOW_VOLTAGE_DELAY 900   /* A short delay at first power up before declaring battery is too low */
#define LOW_VOLTAGE_DELAY 9000          /* A longer delay if the receiver has been running and the battery starts to sag */
#define CURSOR_EXPIRATION_DELAY 5000    /* Keep cursor displayed this long without user action */
#define LONG_PRESS_TICK_COUNT 1200      /* Press a button for this many ticks in order to access a long-press function */

#define SEND_ID_DELAY 4100

/*#define BATTERY_VOLTAGE_COEFFICIENT 332 */
#define BATTERY_VOLTAGE_COEFFICIENT 223                                                                     /* R1 = 69.8k; R2 = 20k; volts x this = mV measured at ADC pin (minus losses) */
#define POWER_SUPPLY_VOLTAGE_DROP_MV 218                                                                    /* This is the voltage drop in mV multiplied by voltage divider ratio */

#define POWER_OFF_VOLT_THRESH_MV (((24 * BATTERY_VOLTAGE_COEFFICIENT) / 10) - POWER_SUPPLY_VOLTAGE_DROP_MV) /* 2.4 V = 2400 mV */
#define POWER_ON_VOLT_THRESH_MV ((3 * BATTERY_VOLTAGE_COEFFICIENT) - POWER_SUPPLY_VOLTAGE_DROP_MV)          /* 3.0 V = 3000 mV */

#define BEEP_SHORT 100

/******************************************************
 * UI Hardware-related definitions */

typedef enum lcdRow
{
	ROW0,
	ROW1,
	NUMBER_OF_LCD_ROWS
} LcdRowType;

typedef enum lcdColumn
{
	COL0,
	COL1,
	COL2,
	COL3,
	COL4,
	COL5,
	COL6,
	COL7,
	COL8,
	COL9,
	COL10,
	COL11,
	COL12,
	COL13,
	COL14,
	COL15,
	COL16,
	COL17,
	COL18,
	COL19,
	NUMBER_OF_LCD_COLS,
	INVALID_LCD_COLUMN
} LcdColType;

typedef enum
{
	BUTTON1_COLUMN = COL0,
	BUTTON2_COLUMN = COL5,
	BUTTON3_COLUMN = COL10,
	BUTTON4_COLUMN = COL15
} ButtonColumn;

typedef enum
{
	FrequencyFormat,
	HourMinuteSecondFormat,
	HourMinuteSecondDateFormat
} TextFormat;

#define DISPLAY_WIDTH_STRING_SIZE (NUMBER_OF_LCD_COLS + 1)

typedef uint8_t BackLightSettingType;
#define BL_OFF 0xFF
#define BL_LOW 0xCF
#define BL_MED 0x8F
#define BL_HIGH 0x00

typedef uint8_t ContrastType;

typedef enum volumeSetting
{
	VOL_ZERO = 0,
	VOL_10,
	VOL_20,
	VOL_30,
	VOL_40,
	VOL_50,
	VOL_60,
	VOL_70,
	VOL_80,
	VOL_90,
	VOL_100,
	DECREMENT_VOL,
	INCREMENT_VOL,
	VOL_NOT_SPECIFIED
} VolumeSetting;

typedef enum volumeType
{
	TONE_VOLUME,
	MAIN_VOLUME
} VolumeType;

typedef enum batteryType
{
	BATTERY_9V,
	BATTERY_4r2V,
	BATTERY_UNKNOWN
} BatteryType;

typedef enum buttons
{
	BUTTON1,
	BUTTON2,
	BUTTON3,
	BUTTON4,
	NUMBER_OF_BUTTONS
} ButtonType;

typedef uint16_t BatteryLevel;  /* in milliVolts */

#define VOLTS_5 ((5 * BATTERY_VOLTAGE_COEFFICIENT) - POWER_SUPPLY_VOLTAGE_DROP_MV)
#define VOLTS_3_19 (((319 * BATTERY_VOLTAGE_COEFFICIENT) / 100) - POWER_SUPPLY_VOLTAGE_DROP_MV)
#define VOLTS_3_0 ((3 * BATTERY_VOLTAGE_COEFFICIENT) - POWER_SUPPLY_VOLTAGE_DROP_MV)

typedef enum
{
	Minutes_Seconds,                        /* minutes up to 59 */
	Hours_Minutes_Seconds,                  /* hours up to 23 */
	Day_Month_Year_Hours_Minutes_Seconds,   /* Year up to 99 */
	Minutes_Seconds_Elapsed,                /* minutes up to 99 */
	Time_Format_Not_Specified
} TimeFormat;

#define NO_TIME_SPECIFIED (-1)

#define SecondsFromHours(hours) ((hours) * 3600)
#define SecondsFromMinutes(min) ((min) * 60)

#endif  /* DEFS_H */





