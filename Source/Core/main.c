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
 * main.c
 *
 */

#include <ctype.h>
#include <asf.h>
#include "defs.h"
#include "si5351.h"
#include "st7036.h"
#include "ad5245.h"
#include "pcf8574.h"
#include "menu.h"
#include "i2c.h"
#include "linkbus.h"
#include "receiver.h"
#include "util.h"

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

   #include "ds3231.h"

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

   #include "pcf2129.h"

#endif

#include <avr/io.h>
#include <stdint.h>         /* has to be added to use uint8_t */
#include <avr/interrupt.h>  /* Needed to use interrupts */
#include <stdio.h>
#include <string.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>

/***********************************************************************
 * Global Variables & String Constants
 *
 * Identify each global with a "g_" prefix
 * Whenever possible limit globals' scope to this file using "static"
 * Use "volatile" for globals shared between ISRs and foreground
 ************************************************************************/

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

	/* LCD Defines */
	static char g_textBuffer[NUMBER_OF_LCD_ROWS][DISPLAY_WIDTH_STRING_SIZE];    /* string storage for displayed text */
	static char g_tempBuffer[DISPLAY_WIDTH_STRING_SIZE];
	static const char *g_labels[4];                                             /* storage to hold pushbutton labels */

	const char NULL_CHAR[] = "\0";
	const char textTemperature[] = "Temperature";
	const char textTime[] = "Time";
	const char textRemote[] = "Remote";
	const char textBattery[] = "Battery";
	const char textRSSI[] = "RSSI";
	const char textMore[] = " More";
	const char textOn[] =  "<On>";
	const char textOff[] = "<Off>";
	const char textPlus[] = "  +";
	const char textMinus[] = "  -";
	const char textBacklight[] = "Backlight";
	const char textContrast[] = "Contrast";
	const char textTurnKnob[] = "Turn Knob";
	const char textToneVolume[] = "Tone Volume";
	const char textMainVolume[] = "Main Volume";
	const char textBandSelect[] = "Band = ";
	const char textChargeBattery[] = "Charge Battery";
	const char textShuttingDown[] = "Shutting down...";
	const char textMenusAccess[] = "Menus: Push Knob";

#endif  /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */

static volatile MenuType g_menu_state = MENU_MAIN;
static volatile int16_t g_rotary_count = 0;

/* Pushbutton Defines */
static volatile uint16_t g_button0_press_ticks = FALSE;
static volatile BOOL g_button0_pressed = FALSE;
static volatile uint8_t g_button1_presses = 0;
static volatile BOOL g_button1_pressed = FALSE;
static volatile uint8_t g_button2_presses = 0;
static volatile BOOL g_button2_pressed = FALSE;
static volatile uint8_t g_button3_presses = 0;
static volatile BOOL g_button3_pressed = FALSE;
static volatile uint8_t g_button4_presses = 0;
static volatile BOOL g_button4_pressed = FALSE;
static volatile uint8_t g_button5_presses = 0;
static volatile BOOL g_button5_pressed = FALSE;
static volatile uint16_t g_pressed_button_ticks = 0;
static volatile BOOL g_ignore_button_release = FALSE;

#if PRODUCT_DUAL_BAND_RECEIVER

	static volatile BOOL g_radio_port_changed = FALSE;
	static volatile uint16_t g_beep_length = 0;
	static volatile BOOL g_volume_set_beep_delay = 0;

#elif PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

	static volatile uint16_t g_cursor_active_countdown = 0;
	static volatile uint8_t g_time_update_countdown = 200;
	static volatile uint16_t g_menu_delay_countdown = 0;

#endif

static volatile uint16_t g_tick_count = 0;
static volatile BOOL g_battery_measurements_active = FALSE;
static volatile uint16_t g_maximum_battery = 0;
static volatile BatteryType g_battery_type = BATTERY_UNKNOWN;
static volatile uint16_t g_send_ID_countdown = 0;
static volatile BOOL g_initialization_complete = FALSE;

static int32_t g_start_time;

/* Linkbus variables */
static DeviceID g_LB_attached_device = NO_ID;
static uint16_t g_LB_broadcasts_enabled = 0;
static BOOL g_lb_terminal_mode = FALSE;

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

	/* EEPROM Defines */
	static BOOL EEMEM ee_eeprom_initialization_flag = EEPROM_INITIALIZED_FLAG;

	static BackLightSettingType EEMEM ee_backlight_setting = EEPROM_BACKLIGHT_DEFAULT;
	static volatile BackLightSettingType g_backlight_setting;

	static ContrastType EEMEM ee_contrast_setting = EEPROM_CONTRAST_DEFAULT;
	volatile static ContrastType g_contrast_setting;

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

	static uint8_t g_LB_broadcast_interval = 100;
	static BOOL EEMEM ee_interface_eeprom_initialization_flag = EEPROM_INITIALIZED_FLAG;

	extern uint32_t EEMEM ee_receiver_2m_mem1_freq;
	extern uint32_t EEMEM ee_receiver_2m_mem2_freq;
	extern uint32_t EEMEM ee_receiver_2m_mem3_freq;
	extern uint32_t EEMEM ee_receiver_2m_mem4_freq;
	extern uint32_t EEMEM ee_receiver_2m_mem5_freq;
	extern uint32_t EEMEM ee_receiver_80m_mem1_freq;
	extern uint32_t EEMEM ee_receiver_80m_mem2_freq;
	extern uint32_t EEMEM ee_receiver_80m_mem3_freq;
	extern uint32_t EEMEM ee_receiver_80m_mem4_freq;
	extern uint32_t EEMEM ee_receiver_80m_mem5_freq;

#endif  /* #if PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE */

#if PRODUCT_CONTROL_HEAD || PRODUCT_DUAL_BAND_RECEIVER
	static Frequency_Hz g_receiver_freq = 0;
#endif  /* PRODUCT_CONTROL_HEAD || PRODUCT_DUAL_BAND_RECEIVER */

#if PRODUCT_CONTROL_HEAD
	static Receiver dual_band_receiver;
	static int32_t g_remote_device_time = 0;
#endif  /* PRODUCT_CONTROL_HEAD */


#if PRODUCT_TEST_INSTRUMENT_HEAD

	static uint32_t EEMEM ee_si5351_clk0_freq = EEPROM_CLK0_OUT_DEFAULT;
	static uint32_t g_si5351_clk0_freq;
	static BOOL EEMEM ee_si5351_clk0_enabled = FALSE;
	static BOOL g_si5351_clk0_enabled;
	static Si5351_drive g_si5351_clk0_drive = SI5351_DRIVE_2MA;

	static uint32_t EEMEM ee_si5351_clk1_freq = EEPROM_CLK1_OUT_DEFAULT;
	static uint32_t g_si5351_clk1_freq;
	static BOOL EEMEM ee_si5351_clk1_enabled = FALSE;
	static BOOL g_si5351_clk1_enabled;
	static Si5351_drive g_si5351_clk1_drive = SI5351_DRIVE_2MA;

	static uint32_t EEMEM ee_si5351_clk2_freq = EEPROM_CLK2_OUT_DEFAULT;
	static uint32_t g_si5351_clk2_freq;
	static BOOL EEMEM ee_si5351_clk2_enabled = FALSE;
	static BOOL g_si5351_clk2_enabled;
	static Si5351_drive g_si5351_clk2_drive = SI5351_DRIVE_2MA;

#endif  /* PRODUCT_TEST_INSTRUMENT_HEAD */


#if PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

	/* Digital Potentiometer Defines */
	static uint8_t EEMEM ee_tone_volume_setting = EEPROM_TONE_VOLUME_DEFAULT;
	/* Headphone Driver Defines */
	static uint8_t EEMEM ee_main_volume_setting = EEPROM_MAIN_VOLUME_DEFAULT;

#endif  /* PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE */

static volatile uint8_t g_main_volume;
static volatile uint8_t g_tone_volume;

#ifdef ENABLE_1_SEC_INTERRUPTS

	static volatile uint16_t g_seconds_count = 0;
	static volatile uint8_t g_seconds_int = FALSE;

#endif  /* #ifdef ENABLE_1_SEC_INTERRUPTS */

/* ADC Defines */
#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

   #define LCD_TEMP_READING 0
   #define BATTERY_READING 1

   #define BAT_VOLTAGE 0x06
   #define LCD_TEMP 0x07
   #define NUMBER_OF_POLLED_ADC_CHANNELS 2
	static const uint8_t activeADC[NUMBER_OF_POLLED_ADC_CHANNELS] = { LCD_TEMP, BAT_VOLTAGE };

	static const uint16_t g_adcChannelConversionPeriod_ticks[NUMBER_OF_POLLED_ADC_CHANNELS] = { 1000, 1000 };
	static volatile uint16_t g_tickCountdownADCFlag[NUMBER_OF_POLLED_ADC_CHANNELS] = { 1000, 1000 };
	static uint16_t g_filterADCValue[NUMBER_OF_POLLED_ADC_CHANNELS] = { 500, 500 };
	static volatile BOOL g_adcUpdated[NUMBER_OF_POLLED_ADC_CHANNELS] = { FALSE, FALSE };
	static volatile uint16_t g_lastConversionResult[NUMBER_OF_POLLED_ADC_CHANNELS];

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

   #define RF_READING 0
   #define BATTERY_READING 1
   #define RSSI_READING 2

   #define RF_LEVEL 0x03
   #define BAT_VOLTAGE 0x06
   #define RSSI_LEVEL 0x07
   #define NUMBER_OF_POLLED_ADC_CHANNELS 3
	static const uint8_t activeADC[NUMBER_OF_POLLED_ADC_CHANNELS] = { RF_LEVEL, BAT_VOLTAGE, RSSI_LEVEL };

	static const uint16_t g_adcChannelConversionPeriod_ticks[NUMBER_OF_POLLED_ADC_CHANNELS] = { 1000, 1000, 100 };
	static volatile uint16_t g_tickCountdownADCFlag[NUMBER_OF_POLLED_ADC_CHANNELS] = { 1000, 1000, 100 };
	static uint16_t g_filterADCValue[NUMBER_OF_POLLED_ADC_CHANNELS] = { 500, 500, 3 };
	static volatile BOOL g_adcUpdated[NUMBER_OF_POLLED_ADC_CHANNELS] = { FALSE, FALSE, FALSE };
	static volatile uint16_t g_lastConversionResult[NUMBER_OF_POLLED_ADC_CHANNELS];

	/* Broadcast data received from Dual-Band Receiver */
	static int16_t g_RSSI_data = WAITING_FOR_UPDATE;
	static int16_t g_Battery_data = WAITING_FOR_UPDATE;
#endif  /* PRODUCT_CONTROL_HEAD */

static volatile uint16_t g_power_off_countdown = POWER_OFF_DELAY;
static volatile uint16_t g_headphone_removed_delay = HEADPHONE_REMOVED_DELAY;
static volatile uint16_t g_low_voltage_shutdown_delay = LOW_VOLTAGE_DELAY;
static volatile uint16_t g_backlight_off_countdown = BACKLIGHT_ALWAYS_ON;
static uint16_t g_backlight_delay_value = BACKLIGHT_ALWAYS_ON;
extern volatile BOOL g_i2c_not_timed_out;
static volatile BOOL g_sufficient_power_detected = FALSE;
static volatile BOOL g_enableHardwareWDResets = FALSE;

/***********************************************************************
 * Private Function Prototypes
 *
 * These functions are available only within this file
 ************************************************************************/

void initializeEEPROMVars(void);
void setMsgGraph(uint8_t len, LcdRowType row);
void saveAllEEPROM(void);
void printButtons(char buff[DISPLAY_WIDTH_STRING_SIZE], char* labels[]);
void clearTextBuffer(LcdRowType);
void updateLCDTextBuffer(char* buffer, char* text, BOOL preserveContents);
void wdt_init(BOOL enableHWResets);
LcdColType columnForDigit(int8_t digit, TextFormat format);


#if PRODUCT_CONTROL_HEAD

	void printFrequency(Frequency_Hz freq, uint8_t digit);
	Frequency_Hz setFrequency(Frequency_Hz freq, uint8_t digit, BOOL increment);

#elif PRODUCT_TEST_INSTRUMENT_HEAD

	Frequency_Hz printFrequency(uint8_t index, uint8_t digit, BOOL increment);

#endif  /* PRODUCT_TEST_INSTRUMENT_HEAD */

/***********************************************************************
 * Watchdog Timer ISR
 *
 * Notice: Optimization must be enabled before watchdog can be set
 * in C (WDCE). Use __attribute__ to enforce optimization level.
 ************************************************************************/
void __attribute__((optimize("O1"))) wdt_init(BOOL enableHWResets)
{
	wdt_reset();

	if(MCUSR & (1 << WDRF))     /* If a reset was caused by the Watchdog Timer perform any special operations */
	{
		MCUSR &= (1 << WDRF);   /* Clear the WDT reset flag */
	}

	if(enableHWResets)
	{
		WDTCSR |= (1 << WDCE) | (1 << WDE);
		WDTCSR = (1 << WDP3) | (1 << WDIE) | (1 << WDE);    /* Enable WD interrupt every 4 seconds, and hardware resets */
		/*	WDTCSR = (1 << WDP3) | (1 << WDP0) | (1 << WDIE) | (1 << WDE); // Enable WD interrupt every 8 seconds, and hardware resets */
	}
	else
	{
		WDTCSR |= (1 << WDCE) | (1 << WDE);
		/*	WDTCSR = (1 << WDP3) | (1 << WDIE); // Enable WD interrupt every 4 seconds (no HW reset)
		 *	WDTCSR = (1 << WDP3) | (1 << WDP0)  | (1 << WDIE); // Enable WD interrupt every 8 seconds (no HW reset) */
		WDTCSR = (1 << WDP1) | (1 << WDP2)  | (1 << WDIE);  /* Enable WD interrupt every 1 seconds (no HW reset) */
	}

	g_enableHardwareWDResets = enableHWResets;
}


/***********************************************************************
 * Timer/Counter2 Compare Match A ISR
 *
 * Handles periodic tasks not requiring precise timing.
 ************************************************************************/
ISR( TIMER2_COMPB_vect )
{
	static BOOL conversionInProcess = FALSE;
	static int8_t indexConversionInProcess;

	g_tick_count++;
	g_pressed_button_ticks++;

	if(g_power_off_countdown)
	{
		g_power_off_countdown--;
	}
	if(g_low_voltage_shutdown_delay)
	{
		g_low_voltage_shutdown_delay--;
	}
	if(g_send_ID_countdown)
	{
		g_send_ID_countdown--;
	}
	if(g_headphone_removed_delay)
	{
		g_headphone_removed_delay--;
	}
	if(g_button0_pressed)
	{
		g_button0_press_ticks++;
	}

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

		static uint16_t holdRotaryCount = 0;
		static uint8_t rotaryNoMotionCountdown = ROTARY_SYNC_DELAY;

		if(g_backlight_off_countdown && (g_backlight_off_countdown != BACKLIGHT_ALWAYS_ON))
		{
			g_backlight_off_countdown--;
		}
		if(g_menu_delay_countdown)
		{
			g_menu_delay_countdown--;
		}
		if(g_cursor_active_countdown)
		{
			g_cursor_active_countdown--;
		}
		if(g_time_update_countdown)
		{
			g_time_update_countdown--;
		}

		/**********************
		 * This is a kluge that helps ensure that the rotary encoder count remains in sync with the
		 * encoder's indents. This kluge seems to be necessary because when the encoder is turned
		 * rapidly (and especially if the direction of turn reverses) a transition can be missed,
		 * causing indents to no longer align. Testing indicates that this re-alignment is rarely needed,
		 * but when it is provided it improves user experience. */
		if(holdRotaryCount == g_rotary_count)
		{
			rotaryNoMotionCountdown--;  /* underflow of the countdown is harmless */

			if(!rotaryNoMotionCountdown)
			{
				if(g_rotary_count % 4)  /* need to make the count be a multiple of 4 edges */
				{
					g_rotary_count += 2;
					g_rotary_count = ((g_rotary_count >> 2) << 2);
				}
			}
		}
		else
		{
			rotaryNoMotionCountdown = ROTARY_SYNC_DELAY;
			holdRotaryCount = g_rotary_count;
		}

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

		if(g_LB_broadcast_interval)
		{
			g_LB_broadcast_interval--;
		}

		static uint8_t mainVolumeSetting = EEPROM_MAIN_VOLUME_DEFAULT;
		static BOOL volumeSetInProcess = FALSE;
		static BOOL beepInProcess = FALSE;

		/**
		 * Handle earphone beeps */
		if(g_beep_length)
		{
			if(!beepInProcess)
			{
				TCCR0A |= (1 << COM0B0);    /* Toggle OC0B (PD5) on Compare Match */
				beepInProcess = TRUE;
			}
			else
			{
				g_beep_length--;

				if(!g_beep_length)
				{
					TCCR0A &= ~(1 << COM0B0);   /* Turn off toggling of OC0B (PD5) */
					beepInProcess = FALSE;
				}
			}
		}

		if(volumeSetInProcess)
		{
			if(PORTC & (1 << PORTC0))
			{
				PORTC &= ~(1 << PORTC0);    /* set clock low */
			}
			else
			{
				PORTC |= (1 << PORTC0);     /* set clock high */
				volumeSetInProcess = FALSE;
			}
		}
		else if(mainVolumeSetting != g_main_volume)
		{
			if(!(PORTC & (1 << PORTC0)))
			{
				PORTC |= (1 << PORTC0); /* set clock high */

			}
			if(mainVolumeSetting > g_main_volume)
			{
				PORTC &= ~(1 << PORTC1);    /* set direction down */
				mainVolumeSetting--;
			}
			else                            /* if(mainVolumeSetting < g_main_volume) */
			{
				PORTC |= (1 << PORTC1);     /* set direction up */
				mainVolumeSetting++;
			}

			volumeSetInProcess = TRUE;
		}

#endif  /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */

	/**
	 * Handle Periodic ADC Readings
	 * The following algorithm allows multipe ADC channel readings to be performed at different polling intervals. */
	if(!conversionInProcess)
	{
		/* Note: countdowns will pause while a conversion is in process. Conversions are so fast that this should not be an issue though. */

		volatile uint8_t i; /* volatile to prevent optimization performing undefined behavior */
		indexConversionInProcess = -1;

		for(i = 0; i < NUMBER_OF_POLLED_ADC_CHANNELS; i++)
		{
			if(g_tickCountdownADCFlag[i])
			{
				g_tickCountdownADCFlag[i]--;
			}

			if(g_tickCountdownADCFlag[i] == 0)
			{
				indexConversionInProcess = (int8_t)i;
			}
		}

		if(indexConversionInProcess >= 0)
		{
			g_tickCountdownADCFlag[indexConversionInProcess] = g_adcChannelConversionPeriod_ticks[indexConversionInProcess];    /* reset the tick countdown */
			ADMUX = (ADMUX & 0xF0) | activeADC[indexConversionInProcess];                                                       /* index through all active channels */
			ADCSRA |= (1 << ADSC);                                                                                              /*single conversion mode */
			conversionInProcess = TRUE;
		}
	}
	else if(!( ADCSRA & (1 << ADSC) ))                                                                                          /* wait for conversion to complete */
	{
		uint16_t hold = ADC;
		uint16_t holdConversionResult = (uint16_t)(((uint32_t)hold * ADC_REF_VOLTAGE_mV) >> 10);                                /* millivolts at ADC pin */
		uint16_t lastResult = g_lastConversionResult[indexConversionInProcess];
		BOOL directionUP = holdConversionResult > lastResult;
		uint16_t delta = directionUP ? holdConversionResult - lastResult : lastResult - holdConversionResult;

		g_adcUpdated[indexConversionInProcess] = TRUE;

		if(delta > g_filterADCValue[indexConversionInProcess])
		{
			lastResult = holdConversionResult;
			g_tickCountdownADCFlag[indexConversionInProcess] = 100; /* speed up next conversion */
		}
		else
		{
			if(directionUP)
			{
				lastResult++;
			}
			else if(delta)
			{
				lastResult--;
			}

			if(indexConversionInProcess == BATTERY_READING) /* Set flag indicating that battery/headphone monitoring has begun */
			{
				g_battery_measurements_active = TRUE;

				if(lastResult > VOLTS_5)
				{
					g_battery_type = BATTERY_9V;
				}
				else if(lastResult > VOLTS_3_0)
				{
					g_battery_type = BATTERY_4r2V;
				}
			}

		}

		g_lastConversionResult[indexConversionInProcess] = lastResult;

		conversionInProcess = FALSE;
	}
}/* ISR */


#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

/***********************************************************************
 * Pin Change Interrupt Request 0 ISR
 *
 * The pin change interrupt PCI0 will trigger if any enabled PCINT[7:0]
 * pin changes.
 * The PCMSK0 Register controls which pins contribute to the pin change
 * interrupts. Pin change interrupts on PCINT23...0 are detected
 * asynchronously. This implies that these interrupts can be used for
 * waking the part from sleep modes other than Idle mode.
 *
 * The External Interrupts can be triggered by a falling or rising edge
 * or a low level. This is set up as indicated in the specification for
 * the External Interrupt Control Registers – EICRA (INT2:0). When the
 * external interrupt is enabled and is configured as level triggered,
 * the interrupt will trigger as long as the pin is held low. Low level
 * interrupts and the edge interrupt on INT2:0 are detected
 * asynchronously. This implies that these interrupts can be used for
 * waking the part also from sleep modes other than Idle mode.
 *
 * Note: For quadrature reading the interrupt is set for "Any logical
 * change on INT0 generates an interrupt request."
 ************************************************************************/
	ISR( PCINT0_vect )
	{
		static uint8_t portBhistory = 0xFF; /* default is high because the pull-up */

		uint8_t changedbits;
		uint8_t quad = PINB;

		changedbits = PINB ^ portBhistory;
		portBhistory = PINB;

		if(!changedbits)    /* noise? */
		{
			return;
		}

   #ifdef ENABLE_1_SEC_INTERRUPTS

			if(changedbits & (1 << PORTB0)) /* RTC Interrupt */
			{
				/* PCINT0 changed */
				if(PINB & (1 << PORTB0))    /* rising edge */
				{
					g_seconds_count++;
					g_seconds_int = TRUE;
				}
			}

   #endif                               /* #ifdef ENABLE_1_SEC_INTERRUPTS */

		if(changedbits & (1 << PORTB1)) /* INT1 Compass */
		{
			/* PCINT1 changed */
		}

		quad = changedbits & QUAD_MASK; /* A and B for quadrature rotary encoder */

		/* Note: the following logic results in the count incrementing by 4 for each full
		 * quadrature cycle. The click count can be derived by shifting right twice (dividing
		 * by four). */
		if(quad)
		{
			g_backlight_off_countdown = g_backlight_delay_value;    /* keep backlight illuminated */
			g_power_off_countdown = POWER_OFF_DELAY;                /* restart countdown */

			BOOL _asignal = (PINB & (1 << QUAD_A)) >> QUAD_A;
			BOOL _bsignal = (PINB & (1 << QUAD_B)) >> QUAD_B;

			if(quad == (1 << QUAD_A))   /* "A" changed */
			{
				if(_asignal == _bsignal)
				{
					g_rotary_count++;
				}
				else
				{
					g_rotary_count--;
				}
			}
			else if(quad == (1 << QUAD_B))  /* "B" changed */
			{
				if(_asignal == _bsignal)
				{
					g_rotary_count--;
				}
				else
				{
					g_rotary_count++;
				}
			}
		}
	}

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

/***********************************************************************
 * Pin Change Interrupt Request 0 ISR
 *
 * The pin change interrupt PCI0 will trigger if any enabled PCINT[7:0]
 * pin changes.
 * The PCMSK0 Register controls which pins contribute to the pin change
 * interrupts. Pin change interrupts on PCINT23...0 are detected
 * asynchronously. This implies that these interrupts can be used for
 * waking the part from sleep modes other than Idle mode.
 *
 * The External Interrupts can be triggered by a falling or rising edge
 * or a low level. This is set up as indicated in the specification for
 * the External Interrupt Control Registers – EICRA (INT2:0). When the
 * external interrupt is enabled and is configured as level triggered,
 * the interrupt will trigger as long as the pin is held low. Low level
 * interrupts and the edge interrupt on INT2:0 are detected
 * asynchronously. This implies that these interrupts can be used for
 * waking the part also from sleep modes other than Idle mode.
 *
 * Note: For quadrature reading the interrupt is set for "Any logical
 * change on INT0 generates an interrupt request."
 ************************************************************************/
	ISR( PCINT0_vect )
	{
		static uint8_t portBhistory = 0xFF; /* default is high because the pull-up */

		uint8_t changedbits;
		uint8_t quad = PINB;

		changedbits = PINB ^ portBhistory;
		portBhistory = PINB;

		if(!changedbits)    /* noise? */
		{
			return;
		}

		if(changedbits & (1 << PORTB1)) /* INT1 Compass */
		{
			/* PCINT1 changed */
		}

		quad = changedbits & QUAD_MASK; /* A and B for quadrature rotary encoder */

		/* Note: the following logic results in the count incrementing by 4 for each full
		 * quadrature cycle. The click count can be derived by shifting right twice (dividing
		 * by four). */
		if(quad)
		{
			g_backlight_off_countdown = g_backlight_delay_value;    /* keep backlight illuminated */
			g_power_off_countdown = POWER_OFF_DELAY;                /* restart countdown */

			BOOL _asignal = (PINB & (1 << QUAD_A)) >> QUAD_A;
			BOOL _bsignal = (PINB & (1 << QUAD_B)) >> QUAD_B;

			if(quad == (1 << QUAD_A))   /* "A" changed */
			{
				if(_asignal == _bsignal)
				{
					g_rotary_count++;
				}
				else
				{
					g_rotary_count--;
				}
			}
			else if(quad == (1 << QUAD_B))  /* "B" changed */
			{
				if(_asignal == _bsignal)
				{
					g_rotary_count--;
				}
				else
				{
					g_rotary_count++;
				}
			}
		}
	}

#endif  /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */


#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

/***********************************************************************
 * Pin Change Interrupt Request 1 ISR
 *
 * The pin change interrupt PCI1 will trigger if any enabled
 * PCINT[14:8] pin toggles.
 * The PCMSK1 Register controls which pins contribute to the pin change
 * interrupts. Pin change interrupts on PCINT23...0 are detected
 * asynchronously. This implies that these interrupts can be used for
 * waking the part from sleep modes other than Idle mode.
 ************************************************************************/
	ISR( PCINT1_vect )
	{
		static uint8_t portChistory = 0xFF; /* default is high because the pull-up */

		/* Switches are PCINT8 and PCINT9 */
		uint8_t changedbits;

		if(!g_initialization_complete)
		{
			return; /* ignore keypresses before initialization completes */

		}
		changedbits = PINC ^ portChistory;
		portChistory = PINC;

		if(!changedbits)    /* noise? */
		{
			return;
		}

		if(changedbits & (1 << PORTC0))                             /* Rotary encoder switch */
		{
			g_power_off_countdown = POWER_OFF_DELAY;                /* restart countdown */
			g_backlight_off_countdown = g_backlight_delay_value;    /* keep backlight illuminated */

			if(PINC & (1 << PORTC0))                                /* rising edge */
			{
				g_button0_pressed = FALSE;

				if((g_button0_press_ticks < 200) && !g_cursor_active_countdown)
				{
   #if PRODUCT_CONTROL_HEAD

						switch(g_menu_state)
						{
							case MENU_REMOTE_DEVICE:
							case MENU_VOLUME:
							case MENU_LCD:
							{
								g_menu_state++;
							}
							break;

							case MENU_SET_TIME:
							{
								g_menu_state = MENU_VOLUME;
							}
							break;

							case MENU_STATUS:
							{
								g_menu_state = MENU_MAIN;
							}
							break;

							case MENU_POWER_OFF:
							{
							}
							break;

							default:
							case MENU_MAIN:
							{
								if(g_LB_attached_device == RECEIVER_ID)
								{
									g_menu_state = MENU_REMOTE_DEVICE;
								}
								else
								{
									g_menu_state = MENU_VOLUME;
								}
							}
							break;
						}

   #else

						switch(g_menu_state)
						{
							case MENU_BAND:
							case MENU_SI5351:
							case MENU_VOLUME:
							case MENU_LCD:
							{
								g_menu_state++;
							}
							break;

							case MENU_SET_TIME:
							{
								g_menu_state = MENU_BAND;
							}
							break;

							case MENU_STATUS:
							{
								g_menu_state = MENU_MAIN;
							}
							break;

							case MENU_POWER_OFF:
							{
							}
							break;

							default:
							case MENU_MAIN:
							{
								if(g_LB_attached_device == RECEIVER_ID)
								{
									g_menu_state = MENU_REMOTE_DEVICE;
								}
								else
								{
									g_menu_state = MENU_BAND;
								}
							}
							break;
						}
   #endif
				}
			}
			else
			{
				/* PCINT16 changed */
				g_button0_press_ticks = 0;
				g_button0_pressed = TRUE;
			}
		}

		if(changedbits & (1 << PORTC1))                             /* Switch 1 (first from left) */
		{
			g_power_off_countdown = POWER_OFF_DELAY;                /* restart countdown */
			g_backlight_off_countdown = g_backlight_delay_value;    /* keep backlight illuminated */

			/* PCINT17 changed */
			if(PINC & (1 << PORTC1))                                /* rising edge */
			{
				g_button1_pressed = FALSE;
				g_button1_presses++;
			}
			else
			{
				g_button1_pressed = TRUE;
				g_pressed_button_ticks = 0;
			}
		}
	}

#elif PRODUCT_DUAL_BAND_RECEIVER

/***********************************************************************
 * Pin Change Interrupt Request 1 ISR
 *
 * The pin change interrupt PCI1 will trigger if any enabled
 * PCINT[14:8] pin toggles.
 * The PCMSK1 Register controls which pins contribute to the pin change
 * interrupts. Pin change interrupts on PCINT23...0 are detected
 * asynchronously. This implies that these interrupts can be used for
 * waking the part from sleep modes other than Idle mode.
 ************************************************************************/
	ISR( PCINT1_vect )
	{
		static uint8_t portChistory = 0xFF; /* default is high because the pull-up */

		uint8_t changedbits;

		if(!g_initialization_complete)
		{
			return; /* ignore keypresses before initialization completes */

		}
		changedbits = PINC ^ portChistory;
		portChistory = PINC;

		if(!changedbits)    /* noise? */
		{
			return;
		}

		if(changedbits & (1 << PORTC2)) /* Receiver port changed */
		{
			if(PINC & (1 << PORTC2))    /* rising edge */
			{
			}
			else
			{
				g_radio_port_changed = TRUE;
			}
		}
	}

#endif  /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */


/***********************************************************************
 * Watchdog Timeout ISR
 *
 * The Watchdog timer helps prevent lockups due to hardware problems.
 * It is especially helpful in this application for preventing I2C bus
 * errors from locking up the foreground process.
 ************************************************************************/
ISR(WDT_vect)
{
	static uint8_t limit = 10;

	g_i2c_not_timed_out = FALSE;    /* unstick I2C */
	saveAllEEPROM();                /* Make sure changed values get saved */

	/* Don't allow an unlimited number of WD interrupts to occur without enabling
	 * hardware resets. But a limited number might be required during hardware
	 * initialization. */
	if(!g_enableHardwareWDResets && limit)
	{
		WDTCSR |= (1 << WDIE);  /* this prevents hardware resets from occurring */
	}

	if(limit)
	{
		limit--;

		if(g_lb_terminal_mode)
		{
			lb_send_WDTError();
		}
	}
}


/***********************************************************************
 * USART Rx Interrupt ISR
 *
 * This ISR is responsible for reading characters from the USART
 * receive buffer to implement the Linkbus.
 *
 *      Message format:
 *              $id,f1,f2... fn;
 *              where
 *                      id = Linkbus MessageID
 *                      fn = variable length fields
 *                      ; = end of message flag
 ************************************************************************/
ISR(USART_RX_vect)
{
	static char textBuff[LINKBUS_MAX_MSG_FIELD_LENGTH];
	static LinkbusRxBuffer* buff = NULL;
	static uint8_t charIndex = 0;
	static uint8_t field_index = 0;
	static uint8_t field_len = 0;
	static uint32_t msg_ID = 0;
	static BOOL receiving_msg = FALSE;
	uint8_t rx_char;

	rx_char = UDR0;

	if(!buff)
	{
		buff = nextEmptyRxBuffer();
	}

	if(buff)
	{
		if(g_lb_terminal_mode)
		{
			static uint8_t ignoreCount = 0;

			rx_char = toupper(rx_char);

			if(ignoreCount)
			{
				rx_char = '\0';
				ignoreCount--;
			}
			else if(rx_char == 0x1B)    /* ESC sequence start */
			{
				rx_char = '\0';

				if(charIndex < LINKBUS_MAX_MSG_FIELD_LENGTH)
				{
					rx_char = textBuff[charIndex];
				}

				ignoreCount = 2;                            /* throw out the next two characters */
			}

			if(rx_char == 0x0D)                             /* Handle carriage return */
			{
				g_power_off_countdown = POWER_OFF_DELAY;    /* restart countdown */

				if(receiving_msg)
				{
					if(charIndex > 0)
					{
						buff->type = LINKBUS_MSG_QUERY;
						buff->id = msg_ID;

						if(field_index > 0) /* terminate the last field */
						{
							buff->fields[field_index - 1][field_len] = 0;
						}

						textBuff[charIndex] = '\0'; /* terminate last-message buffer */
					}

					lb_send_NewLine();
				}
				else
				{
					buff->id = INVALID_MESSAGE; /* print help message */
				}

				charIndex = 0;
				field_len = 0;
				msg_ID = LINKBUS_MSG_UNKNOWN;

				field_index = 0;
				buff = NULL;

				receiving_msg = FALSE;
			}
			else if(rx_char)
			{
				textBuff[charIndex] = rx_char;  /* hold the characters for re-use */

				if(charIndex)
				{
					if(rx_char == 0x7F)         /* Handle backspace */
					{
						charIndex--;
						if(field_index == 0)
						{
							msg_ID -= textBuff[charIndex];
							msg_ID /= 10;
						}
						else if(field_len)
						{
							field_len--;
						}
						else
						{
							buff->fields[field_index][0] = '\0';
							field_index--;
						}
					}
					else
					{
						if(rx_char == ' ')
						{
							if(textBuff[charIndex - 1] == ' ')
							{
								rx_char = '\0';
							}
							else
							{
								/* if(field_index == 0) // message ID received */
								if(field_index > 0)
								{
									buff->fields[field_index - 1][field_len] = 0;
								}

								field_index++;
								field_len = 0;
							}
						}
						else
						{
							if(field_index == 0)    /* message ID received */
							{
								msg_ID = msg_ID * 10 + rx_char;
							}
							else
							{
								buff->fields[field_index - 1][field_len++] = rx_char;
							}
						}

						charIndex++;
					}
				}
				else
				{
					if((rx_char == 0x7F) || (rx_char == ' '))   /* Handle backspace and Space */
					{
						rx_char = '\0';
					}
					else                                        /* start of new message */
					{
						uint8_t i;
						field_index = 0;
						msg_ID = 0;

						msg_ID = msg_ID * 10 + rx_char;

						/* Empty the field buffers */
						for(i = 0; i < LINKBUS_MAX_MSG_NUMBER_OF_FIELDS; i++)
						{
							buff->fields[i][0] = '\0';
						}

						receiving_msg = TRUE;
						charIndex++;
					}
				}

				if(rx_char)
				{
					lb_echo_char(rx_char);
				}
			}
		}
		else
		{
			if((rx_char == '$') || (rx_char == '!'))    /* start of new message = $ */
			{
				charIndex = 0;
				buff->type = (rx_char == '!') ? LINKBUS_MSG_REPLY : LINKBUS_MSG_COMMAND;
				field_len = 0;
				msg_ID = LINKBUS_MSG_UNKNOWN;
				receiving_msg = TRUE;

				/* Empty the field buffers */
				for(field_index = 0; field_index < LINKBUS_MAX_MSG_NUMBER_OF_FIELDS; field_index++)
				{
					buff->fields[field_index][0] = '\0';
				}

				field_index = 0;
			}
			else if(receiving_msg)
			{
				if((rx_char == ',') || (rx_char == ';') || (rx_char == '?'))    /* new field = ,; end of message = ; */
				{
					/* if(field_index == 0) // message ID received */
					if(field_index > 0)
					{
						buff->fields[field_index - 1][field_len] = 0;
					}

					field_index++;
					field_len = 0;

					if(rx_char == ';')
					{
						if(charIndex > LINKBUS_MIN_MSG_LENGTH)
						{
							buff->id = msg_ID;
						}
						receiving_msg = FALSE;
					}
					else if(rx_char == '?')
					{
						buff->type = LINKBUS_MSG_QUERY;
						if(charIndex > LINKBUS_MIN_MSG_LENGTH)
						{
							buff->id = msg_ID;
						}
						receiving_msg = FALSE;
					}

					if(!receiving_msg)
					{
						buff = 0;
					}
				}
				else
				{
					if(field_index == 0)    /* message ID received */
					{
						msg_ID = msg_ID * 10 + rx_char;
					}
					else
					{
						buff->fields[field_index - 1][field_len++] = rx_char;
					}
				}
			}
			else if(rx_char == 0x0D)    /* Handle carriage return */
			{
				if(g_LB_attached_device == NO_ID)
				{
					buff->id = MESSAGE_TTY;
					charIndex = LINKBUS_MAX_MSG_LENGTH;
					field_len = 0;
					msg_ID = LINKBUS_MSG_UNKNOWN;
					field_index = 0;
					buff = NULL;
				}
			}

			if(++charIndex >= LINKBUS_MAX_MSG_LENGTH)
			{
				receiving_msg = FALSE;
				charIndex = 0;
			}
		}
	}
}


/***********************************************************************
 * USART Tx UDRE ISR
 *
 * This ISR is responsible for filling the USART transmit buffer. It
 * implements the transmit function of the Linkbus.
 ************************************************************************/
ISR(USART_UDRE_vect)
{
	static LinkbusTxBuffer* buff = 0;
	static uint8_t charIndex = 0;

	if(!buff)
	{
		buff = nextFullTxBuffer();
	}

	if((*buff)[charIndex])
	{
		/* Put data into buffer, sends the data */
		UDR0 = (*buff)[charIndex++];
	}
	else
	{
		charIndex = 0;
		(*buff)[0] = '\0';
		buff = nextFullTxBuffer();
		if(!buff)
		{
			linkbus_end_tx();
		}
	}
}

/***********************************************************************
 * Pin Change Interrupt 2 ISR
 *
 * The pin change interrupt PCI2 will trigger if any enabled
 * PCINT[23:16] pin toggles. The PCMSK2 Register controls which pins
 * contribute to the pin change interrupts. Pin change interrupts on
 * PCINT23...0 are detected asynchronously. This implies that these
 * interrupts can be used for waking the part from sleep modes other
 * than Idle mode.
 ************************************************************************/
ISR( PCINT2_vect )
{
	static uint8_t portDhistory = 0xFF; /* default is high because the pull-up */

	/* Control Head
	 * Switches are PCINT18, PCINT19, PCINT20, PCINT21, and PCINT22 */

	uint8_t changedbits;

	if(!g_initialization_complete)
	{
		return; /* ignore keypresses before initialization completes */

	}
	changedbits = PIND ^ portDhistory;
	portDhistory = PIND;

	if(!changedbits)    /* noise? */
	{
		return;
	}

	if(changedbits & 0b00011100)                                /* Only do this for button presses */
	{
		g_power_off_countdown = POWER_OFF_DELAY;                /* restart countdown */
		g_backlight_off_countdown = g_backlight_delay_value;    /* keep backlight illuminated */

	}

	if(changedbits & (1 << PORTD2))                             /* Switch 2 (second from left) */
	{
		/* PCINT18 changed */
		if(PIND & (1 << PORTD2))                                /* rising edge */
		{
			g_button2_pressed = FALSE;
			if(!g_ignore_button_release)
			{
				g_button2_presses++;
			}
			g_ignore_button_release = FALSE;
		}
		else
		{
			g_button2_pressed = TRUE;
			g_pressed_button_ticks = 0;
		}
	}

	if(changedbits & (1 << PORTD3)) /* Switch 3 (third from left) */
	{
		/* PCINT19 changed */
		if(PIND & (1 << PORTD3))    /* rising edge */
		{
			g_button3_pressed = FALSE;
			g_button3_presses++;
		}
		else
		{
			g_button3_pressed = TRUE;
			g_pressed_button_ticks = 0;
		}
	}

	if(changedbits & (1 << PORTD4)) /* Switch 4 (fourth from left) */
	{
		/* PCINT20 changed */
		if(PIND & (1 << PORTD4))    /* rising edge */
		{
			g_button4_pressed = FALSE;
			g_button4_presses++;
		}
		else
		{
			g_button4_pressed = TRUE;
			g_pressed_button_ticks = 0;
		}
	}

#if PRODUCT_CONTROL_HEAD

		if(changedbits & (1 << PORTD6)) /* Compass Module Interrupt 2 */
		{
			/* PCINT22 changed */
			if(PIND & (1 << PORTD6))    /* rising edge */
			{
				g_button5_pressed = FALSE;
			}
			else
			{
				g_button5_pressed = TRUE;
				g_button5_presses++;
			}
		}

#endif
}


/***********************************************************************
 * Main Foreground Task
 *
 * main() is responsible for setting up registers and other
 * initialization tasks. It also implements an infinite while(1) loop
 * that handles all "foreground" tasks. All relatively slow processes
 * need to be handled in the foreground, not in ISRs. This includes
 * communications over the I2C bus, handling messages received over the
 * Linkbus, etc.
 ************************************************************************/
int main( void )
{

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

		BOOL cursorOFF = TRUE;
		uint8_t selectedField = 2;                  /* default to 10^2 (100s) digit */
		uint8_t displayedSubMenu[NUMBER_OF_BUTTONS] = { 0, 0, 0, 0 };
		BOOL inhibitMenuExpiration = FALSE;
		MenuType holdMenuState = NUMBER_OF_MENUS;   /* Ensure the menu is shown initially */

#endif /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */

	BOOL attach_success = FALSE;
	int16_t holdCount = 0;
	int16_t newCount;
	uint8_t hold_button1_presses = 0;
	uint8_t hold_button2_presses = 0;
	uint8_t hold_button3_presses = 0;
	uint8_t hold_button4_presses = 0;
	uint8_t hold_button5_presses = 0;
	uint16_t hold_tick_count = 0;

	LinkbusRxBuffer* lb_buff = 0;

	/**
	 * Initialize internal EEPROM if needed */
	initializeEEPROMVars();

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

		/**
		 * Set up PortB/TIMER1 for controlling backlight */
		DDRB |= (1 << DDB2);                                                                    /* outputs: PB2 (backlight brightness); inputs: all the rest */
		PORTB |= (1 << QUAD_A) | (1 << QUAD_B) | (1 << PORTB0) | (1 << PORTB1) | (1 << PORTB2); /* Pull-ups enabled ON quadrature input, and latch power on, turn off backlight, and RTC IRQ */

		/* Write high byte first of 16-bit registers */
		ICR1H = 0xFF;                                                                           /* set TOP and bottom to 16bit */
		ICR1L = 0xFF;
		OCR1BH = g_backlight_setting;                                                           /* set PWM duty cycle @ 16bit */
		OCR1BL = 0xFF;
		TCCR1A |= (1 << COM1B1);                                                                /* set non-inverting mode */
		TCCR1A |= (1 << WGM11);
		TCCR1B |= (1 << WGM12) | (1 << WGM13);                                                  /* set Fast PWM mode using ICR1 as TOP */
		TCCR1B |= (1 << CS10);                                                                  /* Start the timer with no prescaler */

#elif PRODUCT_DUAL_BAND_RECEIVER

		DDRB |= (1 << PORTB0);                                                                  /* PB0 is Radio Enable output; */
		PORTB |= (1 << PORTB0) | (1 << PORTB2);                                                 /* Enable Radio hardware, and pull up RTC interrupt pin */

#endif /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */

	/**
	 * Set up PortD for reading switches and PWM tone generation */

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

		DDRD = 0b10000010;      /* All pins in PORTD are inputs, except PD7 (LCD reset) and PD2 (USART TXD) */
		PORTD = 0b01111100;     /* Pull-ups enabled on all input pins */
		_delay_ms(20);
		PORTD |= 0b10000000;    /* Pull display out of reset */

#else

		DDRD  = 0b11100010;     /* All pins in PORTD are inputs, except PD5 (tone out), PD6 (audio pwr) and PD7 (LCD reset) */
		PORTD = 0b00011100;     /* Pull-ups enabled on all input pins, all outputs set to high except PD6 (audio power) */

#endif

	/**
	 * Set up PortC */

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD
		DDRC = 0b00001100;                                          /* PC4 and PC5 are inputs (should be true by default); PC2 and PC3 are outputs to control power; PC1 and PC0 are switch inputs */
		PORTC |= ((1 << PORTC2) | (1 << PORTC1) | (1 << PORTC0));   /* Turn on remote power and enable input pull-ups */
		linkbus_init();                                             /* Initialize USART */

		PORTC |= I2C_PINS;                                          /* Pull up I2C lines */
#else
		DDRC = 0b00000011;                                          /* PC4 and PC5 are inputs (should be true by default); PC2 and PC3 are used for their ADC function; PC1 and PC0 outputs control main volume */
		PORTC = (I2C_PINS | (1 << PORTC2));                         /* Set all Port C pins low, except I2C lines and PC2; includes output port PORTC0 and PORTC1 (main volume controls) */
		linkbus_init();

#endif

	/**
	 * PD5 (OC0B) is PWM output for audio tone generation
	 * Write 8-bit registers for TIMER0 */
	OCR0A = 0x0C;                                       /* set frequency to ~300 Hz (0x0c) */
	TCCR0A |= (1 << WGM01);                             /* set CTC with OCRA */
	TCCR0B |= (1 << CS02) | (1 << CS00);                /* 1024 Prescaler */
/*	TIMSK0 &= ~(1 << OCIE0B); // disable compare interrupt - disabled by default */

	/**
	 * TIMER2 is for periodic interrupts */
	OCR2A = 0x0C;                                       /* set frequency to ~300 Hz (0x0c) */
	TCCR2A |= (1 << WGM01);                             /* set CTC with OCRA */
	TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20);  /* 1024 Prescaler */
	TIMSK2 |= (1 << OCIE0B);                            /* enable compare interrupt */

	/**
	 * Set up ADC */
	ADMUX |= (1 << REFS0);
	ADCSRA |= (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0) | (1 << ADEN);

	/**
	 * Set up pin interrupts */
#if PRODUCT_CONTROL_HEAD | PRODUCT_TEST_INSTRUMENT_HEAD
		PCICR |= (1 << PCIE2) | (1 << PCIE1) | (1 << PCIE0);    /* Enable pin change interrupts PCI2, PCI1 and PCI0 */
		PCMSK2 |= 0b01111100;                                   /* Enable all port D pin change interrupts except PD0, PD1 and PD7 */
		PCMSK1 |= 0b00000011;                                   /* Enable port C pin change interrupts on pins 0 and 1 */
		PCMSK0 |= (1 << QUAD_A) | (1 << QUAD_B) | 0b00000011;   /* Enable port B pin change interrupts on pins 0, 1, and quadrature changes on rotary encoder */
#else
		PCICR |= (1 << PCIE2) | (1 << PCIE1) | (1 << PCIE0);    /* Enable pin change interrupts PCI2, PCI1 and PCI0 */
		PCMSK2 |= 0b00011100;                                   /* Enable port D pin change interrupts PD2, PD3, and PD4 */
		PCMSK1 |= (1 << PCINT10);                               /* Enable port C pin change interrupts on pin PC2 */
		PCMSK0 |= (1 << PORTB2);                                /* | (1 << QUAD_A) | (1 << QUAD_B); // Enable port B pin 2 and quadrature changes on rotary encoder. */
#endif

	cpu_irq_enable();                                           /* same as sei(); */

	g_low_voltage_shutdown_delay = POWERUP_LOW_VOLTAGE_DELAY;

	/**
	 * Enable watchdog interrupts before performing I2C calls that might cause a lockup */
	wdt_init(FALSE);

	/**
	 * Initialize the receiver */

#if PRODUCT_DUAL_BAND_RECEIVER
		init_receiver(NULL);
#elif PRODUCT_CONTROL_HEAD
#elif PRODUCT_TEST_INSTRUMENT_HEAD
/*	g_si5351_clk0_freq =
 *	g_si5351_clk1_freq = */
		g_si5351_clk0_enabled = TRUE;
		g_si5351_clk1_enabled = TRUE;
		g_si5351_clk0_drive = SI5351_DRIVE_2MA;
		g_si5351_clk1_drive = SI5351_DRIVE_2MA;
#endif  /* #if PRODUCT_CONTROL_HEAD */

	/**
	 * The watchdog must be petted periodically to keep it from barking */
	wdt_reset();                /* HW watchdog */

	/**
	 * Initialize the display */

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD
		LCD_init(NUMBER_OF_LCD_ROWS, NUMBER_OF_LCD_COLS, LCD_I2C_SLAVE_ADDRESS, g_contrast_setting);
#else
		ad5245_set_potentiometer(g_tone_volume);    /* move to receiver initialization */
		writePort(RECEIVER_REMOTE_PORT_ADDR, 0b11111111);
#endif

	wdt_reset();                                    /* HW watchdog */
	lb_send_sync();                                 /* send test pattern to help synchronize baud rate with any attached device */
	while(linkbusTxInProgress())
	{
		;                                           /* wait until transmit finishes */
	}
	wdt_reset();

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD
		lb_send_ID(LINKBUS_MSG_COMMAND, CONTROL_HEAD_ID, NO_ID);
		ds3231_read_time(&g_start_time, NULL, Time_Format_Not_Specified);
   #ifdef ENABLE_1_SEC_INTERRUPTS
			g_seconds_count = 0;    /* sync seconds count to clock */
			ds3231_1s_sqw(ON);
   #endif                           /* #ifdef ENABLE_1_SEC_INTERRUPTS */
#elif PRODUCT_DUAL_BAND_RECEIVER
		lb_send_ID(LINKBUS_MSG_COMMAND, RECEIVER_ID, NO_ID);
		pcf2129_init();
		pcf2129_read_time(&g_start_time, NULL, Time_Format_Not_Specified);
   #ifdef ENABLE_1_SEC_INTERRUPTS
			g_seconds_count = 0;    /* sync seconds count to clock */
			pcf2129_1s_sqw(ON);
   #endif                           /* #ifdef ENABLE_1_SEC_INTERRUPTS */
#endif

	while(linkbusTxInProgress())
	{
	}               /* wait until transmit finishes */

	g_send_ID_countdown = SEND_ID_DELAY;
	wdt_init(TRUE); /* enable hardware interrupts */
	g_initialization_complete = TRUE;

	while(1)
	{
		/**************************************
		* The watchdog must be petted periodically to keep it from barking
		**************************************/
		cli(); wdt_reset(); /* HW watchdog */ sei();

#if PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

			/**************************************
			 * Beep once at new volume level if the volume was set
			 ***************************************/
			if(g_volume_set_beep_delay)
			{
				g_volume_set_beep_delay--;

				if(!g_volume_set_beep_delay)
				{
					g_beep_length = BEEP_SHORT;
				}
			}

#endif                                                                                                              /* #if PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE */


		/***************************************
		* Check for Power Off
		***************************************/
		if(g_battery_measurements_active)                                                                           /* if ADC battery measurements have stabilized */
		{
			if((g_lastConversionResult[BATTERY_READING] < POWER_OFF_VOLT_THRESH_MV) && g_sufficient_power_detected) /* Battery measurement indicates headphones removed */
			{
				if(!g_headphone_removed_delay)
				{
					if(g_menu_state != MENU_POWER_OFF)                                                              /* Handle the case of power off immediately after power on */
					{
						g_menu_state = MENU_POWER_OFF;

#if PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

							PORTD &= ~(1 << PORTD6);    /* Disable audio power */

#endif

						g_power_off_countdown = POWER_OFF_DELAY;
					}

					g_backlight_off_countdown = g_backlight_delay_value;    /* turn on backlight */


					if(g_lb_terminal_mode)
					{
						static uint8_t lastCountdown = 0;
						uint8_t countdown = (uint8_t)((10 * g_power_off_countdown) / POWER_OFF_DELAY);

						if(countdown != lastCountdown)
						{
							lb_poweroff_msg(countdown);
							lb_send_NewPrompt();
							lastCountdown = countdown;
						}
					}

					if(!g_power_off_countdown)
					{
						saveAllEEPROM();

#if PRODUCT_CONTROL_HEAD

							PORTC &= ~(1 << PORTC2);    /* Turn off remote power */
							PORTC &= ~(1 << PORTC3);    /* latch power off */

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

							PORTB &= ~(1 << PORTB1);    /* latch power off */

#endif

						g_power_off_countdown = POWER_OFF_DELAY;

						while(1)    /* wait for power-off */
						{
							/* The following things can prevent shutdown
							 * HW watchdog will expire and reset the device eventually if none of the following happens first: */
							if(!g_power_off_countdown)
							{
								break;  /* Timeout waiting for power to be removed */
							}

							if(g_lastConversionResult[BATTERY_READING] > POWER_ON_VOLT_THRESH_MV)
							{
								break;  /* Headphone re-inserted */
							}
						}

						/**
						 * Execution reaches here if power was restored before the processor shut down, or if shutdown timed out.
						 * Attempt to restart things as if nothing has happened. */
						wdt_reset();    /* HW watchdog */
						g_menu_state = MENU_MAIN;

#if PRODUCT_CONTROL_HEAD

							holdMenuState = NUMBER_OF_MENUS;

#endif
					}
				}
			}
			else if(g_lastConversionResult[BATTERY_READING] > POWER_ON_VOLT_THRESH_MV)  /* Battery measurement indicates sufficient voltage */
			{
				g_sufficient_power_detected = TRUE;
				g_headphone_removed_delay = HEADPHONE_REMOVED_DELAY;
				g_low_voltage_shutdown_delay = LOW_VOLTAGE_DELAY;
				g_power_off_countdown = POWER_OFF_DELAY;    /* restart countdown */

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

					PORTC |= (1 << PORTC2);                 /* Turn on remote power */
					PORTC |= (1 << PORTC3);                 /* latch power on */
					if(g_menu_state == MENU_POWER_OFF)
					{
						g_menu_state = MENU_MAIN;
					}

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

					PORTB |= (1 << PORTB1); /* latch power on */
					PORTD |= (1 << PORTD6); /* Enable audio power */

#endif
			}
			else
			{
				if(!g_low_voltage_shutdown_delay)
				{
					if(g_menu_state != MENU_LOW_BATTERY)
					{
						g_menu_state = MENU_LOW_BATTERY;
						g_power_off_countdown = POWER_OFF_DELAY;
					}

					if(!g_power_off_countdown)
					{
#if PRODUCT_CONTROL_HEAD

							PORTC &= ~(1 << PORTC2);    /* Turn off remote power */
							PORTC &= ~(1 << PORTC3);    /* latch power off */
							OCR1BH = BL_OFF;            /* turn off backlight */
							OCR1BL = 0xFF;

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

							PORTB &= ~(1 << PORTB1);    /* latch power off */
							PORTD &= ~(1 << PORTD6);    /* Disable audio power */

#endif

						while(1)                        /* wait for power-off */
						{
							/* These things can prevent shutdown
							 * HW watchdog will expire and reset the device eventually if none of the following happens first: */
							if(g_lastConversionResult[BATTERY_READING] > POWER_ON_VOLT_THRESH_MV)
							{
								break;                  /* Voltage rises sufficiently */
							}
						}

						wdt_reset();                    /* HW watchdog */

#if PRODUCT_CONTROL_HEAD

							g_menu_state = MENU_MAIN;   /* Recover if power is restored before power off */
							holdMenuState = NUMBER_OF_MENUS;

#endif
					}
				}
			}
		}

		/***********************************************************************
		 *  Handle arriving Linkbus messages
		 ************************************************************************/
		while((lb_buff = nextFullRxBuffer()))
		{
			LBMessageID msg_id = lb_buff->id;

#if PRODUCT_TEST_INSTRUMENT_HEAD

				uint32_t *clkFreq = 0;
				BOOL *enabled = 0;
				Si5351_clock clk = (Si5351_clock)(msg_id - MESSAGE_SETCLK0);
				Si5351_drive *drive = 0;

#endif  /* PRODUCT_TEST_INSTRUMENT_HEAD */

			switch(msg_id)
			{
				case MESSAGE_TIME:
				{
#if PRODUCT_CONTROL_HEAD

						if(g_LB_attached_device == RECEIVER_ID)
						{
							if(lb_buff->fields[FIELD1][0])
							{
								g_remote_device_time = atol(lb_buff->fields[FIELD1]);
							}
						}

#elif PRODUCT_DUAL_BAND_RECEIVER

						if(lb_buff->fields[FIELD1][0])
						{
							int32_t time = -1;

							if(g_lb_terminal_mode)
							{
								if(((lb_buff->fields[FIELD1][2] == ':') && (lb_buff->fields[FIELD1][5] == ':')) || ((lb_buff->fields[FIELD1][1] == ':') && (lb_buff->fields[FIELD1][4] == ':')))
								{
									time = stringToTimeVal(lb_buff->fields[FIELD1]);
								}
							}
							else
							{
								time = atol(lb_buff->fields[FIELD1]);
							}

							if(time >= 0)
							{
								pcf2129_set_time(time, FALSE);
							}
						}

						if(lb_buff->type == LINKBUS_MSG_QUERY)
						{
							int32_t time;
							pcf2129_read_time(&time, NULL, Time_Format_Not_Specified);
							lb_send_TIM(LINKBUS_MSG_REPLY, time);
						}

#endif  /* #if PRODUCT_CONTROL_HEAD */
				}
				break;

				case MESSAGE_SET_FREQ:
				{
#if PRODUCT_CONTROL_HEAD

						if(g_LB_attached_device == RECEIVER_ID)
						{
							if(lb_buff->fields[FIELD1][0])
							{
								Frequency_Hz f = atol(lb_buff->fields[FIELD1]);
								RadioBand b = bandForFrequency(f);

								if(b != BAND_INVALID)
								{
									g_receiver_freq = f;
									dual_band_receiver.bandSetting = b;

									if(lb_buff->fields[FIELD2][0] == 'M')
									{
										dual_band_receiver.currentMemoryFrequency = f;
										dual_band_receiver.currentUserFrequency = f;
									}
									else
									{
										dual_band_receiver.currentUserFrequency = f;
									}
								}

								if(g_menu_state == MENU_REMOTE_DEVICE)
								{
									holdMenuState = UPDATE_DISPLAY_WITH_LB_DATA;
								}
							}
						}

#elif PRODUCT_DUAL_BAND_RECEIVER

						BOOL isMem = FALSE;

						if(lb_buff->fields[FIELD1][0])
						{
							if(lb_buff->fields[FIELD1][0] == 'M')
							{
								uint8_t mem = atoi(&lb_buff->fields[FIELD1][1]);

								Frequency_Hz f = FREQUENCY_NOT_SPECIFIED;
								RadioBand b = rxGetBand();
								Frequency_Hz *eemem_location = NULL;

								switch(mem)
								{
									case 1:
									{
										if(b == BAND_2M)
										{
											eemem_location = &ee_receiver_2m_mem1_freq;
										}
										else
										{
											eemem_location = &ee_receiver_80m_mem1_freq;
										}

									}
									break;

									case 2:
									{
										if(b == BAND_2M)
										{
											eemem_location = &ee_receiver_2m_mem2_freq;
										}
										else
										{
											eemem_location = &ee_receiver_80m_mem2_freq;
										}

									}
									break;

									case 3:
									{
										if(b == BAND_2M)
										{
											eemem_location = &ee_receiver_2m_mem3_freq;
										}
										else
										{
											eemem_location = &ee_receiver_80m_mem3_freq;
										}

									}
									break;

									case 4:
									{
										if(b == BAND_2M)
										{
											eemem_location = &ee_receiver_2m_mem4_freq;
										}
										else
										{
											eemem_location = &ee_receiver_80m_mem4_freq;
										}

									}
									break;

									case 5:
									{
										if(b == BAND_2M)
										{
											eemem_location = &ee_receiver_2m_mem5_freq;
										}
										else
										{
											eemem_location = &ee_receiver_80m_mem5_freq;
										}

									}
									break;

									default:
									{
									}
									break;
								}

								if(eemem_location)
								{
									Frequency_Hz memFreq = 0;
									isMem = TRUE;

									if(g_lb_terminal_mode)              /* Handle terminal mode message */
									{
										if(lb_buff->fields[FIELD2][0])  /* second field holds frequency to be written to memory */
										{
											memFreq = atol(lb_buff->fields[FIELD2]);
											lb_buff->type = LINKBUS_MSG_COMMAND;
										}
									}

									if(lb_buff->type == LINKBUS_MSG_QUERY)  /* Query: apply and return the memory setting */
									{
										f = eeprom_read_dword(eemem_location);

										if(f != FREQUENCY_NOT_SPECIFIED)
										{
											if(rxSetFrequency(&f))
											{
												g_receiver_freq = f;
											}
										}
									}
									else if(lb_buff->type == LINKBUS_MSG_COMMAND)   /* Command: save the current frequency setting to the memory location */
									{
										if(g_lb_terminal_mode)
										{
											if(rxSetFrequency(&memFreq))
											{
												g_receiver_freq = memFreq;
												f = memFreq;
											}
											else
											{
												f = FREQUENCY_NOT_SPECIFIED;
											}
										}
										else
										{
											f = rxGetFrequency();
										}

										if(f != FREQUENCY_NOT_SPECIFIED)
										{
											storeEEdwordIfChanged(eemem_location, f);
										}
									}
								}
							}
							else
							{
								Frequency_Hz f = atol(lb_buff->fields[FIELD1]);
								if(rxSetFrequency(&f))
								{
									g_receiver_freq = f;
								}
							}
						}
						else
						{
							g_receiver_freq = rxGetFrequency();
						}

						if(g_receiver_freq)
						{
							lb_send_FRE(LINKBUS_MSG_REPLY, g_receiver_freq, isMem);
						}

#endif  /* #if PRODUCT_CONTROL_HEAD */
				}
				break;

				case MESSAGE_ID:
				{
					if(lb_buff->fields[FIELD1][0])
					{
						g_LB_attached_device = atoi(lb_buff->fields[FIELD1]);
						DeviceID reportedID = NO_ID;

						if(lb_buff->fields[FIELD2][0])
						{
							reportedID = atoi(lb_buff->fields[FIELD2]);
						}

#if PRODUCT_CONTROL_HEAD

							if(reportedID != CONTROL_HEAD_ID)
							{
								lb_send_ID(LINKBUS_MSG_REPLY, CONTROL_HEAD_ID, g_LB_attached_device);

								attach_success = FALSE;
								g_send_ID_countdown = SEND_ID_DELAY;
							}
							else if(g_LB_attached_device == RECEIVER_ID)
							{
								if(!attach_success)
								{
									lb_send_ID(LINKBUS_MSG_REPLY, CONTROL_HEAD_ID, g_LB_attached_device);
								}
								attach_success = TRUE;  /* stop any ongoing ID messages */
								g_send_ID_countdown = 0;

								if(g_menu_state != MENU_REMOTE_DEVICE)
								{
									g_menu_state = MENU_REMOTE_DEVICE;
									lb_send_FRE(LINKBUS_MSG_QUERY, MEMORY_1, FALSE);    /* Get programmed MEM1 frequency */
								}

								holdMenuState = UPDATE_DISPLAY_WITH_LB_DATA;
							}

#elif PRODUCT_DUAL_BAND_RECEIVER

							if(reportedID != RECEIVER_ID)
							{
								lb_send_ID(LINKBUS_MSG_REPLY, RECEIVER_ID, g_LB_attached_device);
								attach_success = FALSE;
								g_send_ID_countdown = SEND_ID_DELAY;
							}
							else
							{
								if(!attach_success)
								{
									lb_send_ID(LINKBUS_MSG_REPLY, RECEIVER_ID, g_LB_attached_device);
								}
								attach_success = TRUE;  /* stop any ongoing ID messages */
								g_send_ID_countdown = 0;
							}

#endif                                                  /* #if PRODUCT_CONTROL_HEAD */
					}
				}
				break;

				case MESSAGE_BAND:
				{
					RadioBand band;

#if PRODUCT_DUAL_BAND_RECEIVER

						if(lb_buff->fields[FIELD1][0])  /* band field */
						{
							RadioBand b = atoi(lb_buff->fields[FIELD1]);

							if(g_lb_terminal_mode)
							{
								if(b == 80)
								{
									b = BAND_80M;
								}
								if(b == 2)
								{
									b = BAND_2M;
								}

								if(lb_buff->fields[FIELD2][0] == 'W')
								{
									rxSetBand(b);
								}
							}
							else
							{
								rxSetBand(b);
							}
						}

						band = rxGetBand();
#else

						band = BAND_INVALID;

						if(lb_buff->fields[FIELD1][0])  /* band field */
						{
							band = atoi(lb_buff->fields[FIELD1]);
						}

#endif  /* #if PRODUCT_DUAL_BAND_RECEIVER */


#if PRODUCT_CONTROL_HEAD

						if(lb_buff->type == LINKBUS_MSG_REPLY)                  /* Reply */
						{
							if(g_menu_state == MENU_REMOTE_DEVICE)
							{
								holdMenuState = UPDATE_DISPLAY_WITH_LB_DATA;    /* Ensure the screen is updated */

								if(dual_band_receiver.bandSetting != band)
								{
									dual_band_receiver.bandSetting = band;
									lb_send_FRE(LINKBUS_MSG_QUERY, MEMORY_1, FALSE);
									displayedSubMenu[BUTTON2] = 0;  /* set MEM button to agree with requested frequency */
								}
							}
						}

#elif PRODUCT_TEST_INSTRUMENT_HEAD

						if(lb_buff->type == LINKBUS_MSG_REPLY)  /* Reply */
						{
							dual_band_receiver.bandSetting = band;
							if(g_menu_state == MENU_BAND)
							{
								holdMenuState = UPDATE_DISPLAY_WITH_LB_DATA;    /* Ensure the screen is updated */
							}
						}

#elif PRODUCT_DUAL_BAND_RECEIVER

						if(lb_buff->type == LINKBUS_MSG_QUERY)  /* Query */
						{
							/* Send a reply */
							lb_send_BND(LINKBUS_MSG_REPLY, band);
						}

#endif  /* #if PRODUCT_CONTROL_HEAD */

				}
				break;

#if PRODUCT_TEST_DIGITAL_INTERFACE || PRODUCT_TEST_INSTRUMENT_HEAD

						case MESSAGE_SETCLK0:
						{
							clkFreq = &g_si5351_clk0_freq;
							enabled = &g_si5351_clk0_enabled;
							drive = &g_si5351_clk0_drive;

							/* Intentional fall-through */
						}

						case MESSAGE_SETCLK1:
						{
							if(!clkFreq)
							{
								clkFreq = &g_si5351_clk1_freq;
								enabled = &g_si5351_clk1_enabled;
								drive = &g_si5351_clk1_drive;
							}
							/* Intentional fall-through */
						}

						case MESSAGE_SETCLK2:
						{
							if(!clkFreq)
							{
								clkFreq = &g_si5351_clk2_freq;
								enabled = &g_si5351_clk2_enabled;
								drive = &g_si5351_clk2_drive;
							}

							if(lb_buff->fields[FIELD1][0])  /* frequency field */
							{
								*clkFreq = atol((const char*)lb_buff->fields[FIELD1]);
								si5351_set_freq(*clkFreq, clk);
							}

							if(lb_buff->fields[FIELD2][0])  /* enabled field */
							{
								*enabled =  lb_buff->fields[FIELD2][0] != '0';
								si5351_clock_enable(clk, *enabled);
							}

							if(lb_buff->fields[FIELD3][0])
							{
								*drive = (Si5351_drive)(lb_buff->fields[FIELD3][0] - '0');
								si5351_drive_strength(clk, *drive);
							}

   #if PRODUCT_TEST_INSTRUMENT_HEAD

								if(g_menu_state == MENU_SI5351)
								{
									holdMenuState = UPDATE_DISPLAY_WITH_LB_DATA;    /* Ensure the screen is updated */

								}
   #else
								saveAllEEPROM();

   #endif                                                           /* #if PRODUCT_TEST_INSTRUMENT_HEAD */

							if(lb_buff->type == LINKBUS_MSG_QUERY)  /* Query */
							{
								/* Send a reply */
								lb_send_CKn(LINKBUS_MSG_REPLY, clk, *clkFreq, *enabled ? SI5351_CLK_ENABLED : SI5351_CLK_DISABLED, SI5351_DRIVE_NOT_SPECIFIED);
							}

						}
						break;

#endif  /* PRODUCT_TEST_DIGITAL_INTERFACE || #if PRODUCT_TEST_INSTRUMENT_HEAD */


				case MESSAGE_VOLUME:
				{
					VolumeType volType;
					BOOL valid = FALSE;

					if(lb_buff->fields[FIELD1][0] == 'T')   /* volume type field */
					{
						valid = TRUE;
						volType = TONE_VOLUME;
					}
					else if(lb_buff->fields[FIELD1][0] == 'M')
					{
						valid = TRUE;
						volType = MAIN_VOLUME;
					}

					if(valid)
					{
						IncrType direction = NOCHANGE;
						int16_t holdVol = -1;

						if(lb_buff->fields[FIELD2][0])
						{
							if(lb_buff->fields[FIELD2][0] == '+')
							{
								direction = UP;
							}
							else if(lb_buff->fields[FIELD2][0] == '-')
							{
								direction = DOWN;
							}
							else
							{
								holdVol = atoi(lb_buff->fields[FIELD2]);
								direction = SETTOVALUE;
							}
						}

						if(volType == TONE_VOLUME)
						{
							if(holdVol < 0)
							{
								holdVol = g_tone_volume;
							}

							if(direction == UP)
							{
								if(holdVol < MAX_TONE_VOLUME_SETTING)
								{
									holdVol += 10;
								}
							}
							else if(direction == DOWN)
							{
								if(holdVol)
								{
									holdVol -= 10;
								}
							}

							if(holdVol > MAX_TONE_VOLUME_SETTING)
							{
								holdVol = MAX_TONE_VOLUME_SETTING;
							}
							else if(holdVol < 0)
							{
								holdVol = 0;
							}

							if(holdVol != g_tone_volume)
							{
								ad5245_set_potentiometer(holdVol);
								g_tone_volume = (uint8_t)holdVol;
							}

							g_volume_set_beep_delay = 20;
						}
						else    /* volType == MAIN_VOLUME */
						{
							if(direction == UP)
							{
								if(g_main_volume < MAX_MAIN_VOLUME_SETTING)
								{
									g_main_volume++;
								}
							}
							else if(direction == DOWN)
							{
								if(g_main_volume)
								{
									g_main_volume--;
								}
							}
							else if(direction == SETTOVALUE)
							{
								if(holdVol > MAX_MAIN_VOLUME_SETTING)
								{
									g_main_volume = MAX_MAIN_VOLUME_SETTING;
								}
								else if(holdVol < 0)
								{
									g_main_volume = 0;
								}
								else
								{
									g_main_volume = holdVol;
								}
							}

							g_volume_set_beep_delay = 20;
						}

#if PRODUCT_CONTROL_HEAD

							if(g_menu_state == MENU_VOLUME)
							{
								holdMenuState = UPDATE_DISPLAY_WITH_LB_DATA;    /* Ensure the screen is updated */

							}
#else

							saveAllEEPROM();

#endif  /* #if PRODUCT_CONTROL_HEAD */

						if(g_lb_terminal_mode)
						{
							if(volType == MAIN_VOLUME)
							{
								lb_send_value(g_main_volume, "MAIN VOL");
							}
							else
							{
								lb_send_value(g_tone_volume, "TONE VOL");
							}
						}
						else
						{
							if(lb_buff->type == LINKBUS_MSG_QUERY)  /* Query */
							{
								/* Send a reply */
								lb_send_VOL(LINKBUS_MSG_REPLY, volType, VOL_NOT_SPECIFIED);
							}
						}
					}
				}
				break;

				case MESSAGE_TTY:
				{
					g_lb_terminal_mode = linkbus_toggleTerminalMode();

					if(g_lb_terminal_mode)
					{
						g_LB_broadcasts_enabled = 0;    /* disable all broadcasts */
						attach_success = TRUE;          /* stop any ongoing ID messages */
						g_send_ID_countdown = 0;
						linkbus_setLineTerm("\n\n");
					}
				}
				break;

				case MESSAGE_BCR:
				{
					LBbroadcastType bcType = atoi(lb_buff->fields[FIELD1]);

					if(lb_buff->type == LINKBUS_MSG_QUERY)  /* Query */
					{
						g_LB_broadcasts_enabled |= bcType;
					}
					else
					{
						g_LB_broadcasts_enabled &= ~bcType;
					}
				}
				break;

				case MESSAGE_BAT_BC:
				{
					if(g_lb_terminal_mode)
					{
						lb_broadcast_bat(g_lastConversionResult[BATTERY_READING]);
					}
					else
					{
						g_Battery_data = atoi(lb_buff->fields[FIELD1]);
					}
				}
				break;

				case MESSAGE_TEMPERATURE_BC:
				{
				}
				break;

				case MESSAGE_RSSI_BC:
				{
					if(g_lb_terminal_mode)
					{
						lb_broadcast_rssi(g_lastConversionResult[RSSI_READING]);
					}
					else
					{
						g_RSSI_data = atoi(lb_buff->fields[FIELD1]);
					}
				}
				break;

				case MESSAGE_RF_BC:
				{
				}
				break;

				case MESSAGE_ALL_INFO:
				{
					int32_t time;
					linkbus_setLineTerm("\n");
					lb_send_BND(LINKBUS_MSG_REPLY, rxGetBand());
					lb_send_FRE(LINKBUS_MSG_REPLY, rxGetFrequency(), FALSE);
					lb_send_value(g_main_volume, "MAIN VOL");
					lb_send_value(g_tone_volume, "TONE VOL");
					lb_broadcast_bat(g_lastConversionResult[BATTERY_READING]);
					lb_broadcast_rssi(g_lastConversionResult[RSSI_READING]);
					linkbus_setLineTerm("\n\n");
					cli(); wdt_reset(); /* HW watchdog */ sei();
					pcf2129_read_time(&time, NULL, Time_Format_Not_Specified);
					lb_send_TIM(LINKBUS_MSG_REPLY, time);
					cli(); wdt_reset(); /* HW watchdog */ sei();
				}
				break;

				default:
				{
					if(g_lb_terminal_mode)
					{
						lb_send_Help();
					}
					else
					{
						linkbus_reset_rx(); /* flush buffer */
					}
				}
				break;
			}

			if(g_lb_terminal_mode)
			{
				lb_send_NewPrompt();
			}

			lb_buff->id = MESSAGE_EMPTY;
		}


#if PRODUCT_DUAL_BAND_RECEIVER

			/* ////////////////////////////////////
			 * Handle Receiver interrupts (e.g., trigger button presses) */
			if(g_radio_port_changed)
			{
				g_radio_port_changed = FALSE;

				uint8_t portPins;
				readPort(RECEIVER_REMOTE_PORT_ADDR, &portPins);

				/* Take appropriate action for the pin change */
				/*			if((~portPins) & 0b00010000)
				 *			{
				 *			} */
			}

#endif  /* #if PRODUCT_DUAL_BAND_RECEIVER */

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

			/* ////////////////////////////////////
			 * Handle periodic tasks triggered by the tick count */
			if(hold_tick_count != g_tick_count)
			{
				hold_tick_count = g_tick_count;

				/* ////////////////////////////////////
				 * Turn off backlight after a delay */
				if(!g_backlight_off_countdown)
				{
					if(g_backlight_setting != BL_OFF)
					{
						OCR1BH = BL_OFF;    /* set PWM duty cycle @ 16bit */
						OCR1BL = 0xFF;
						if((g_menu_state != MENU_LOW_BATTERY) && (g_menu_state != MENU_POWER_OFF) && !inhibitMenuExpiration)
						{
							g_menu_state = MENU_MAIN;
						}
					}
				}
				else
				{
					OCR1BH = g_backlight_setting;   /* set PWM duty cycle @ 16bit */
					OCR1BL = 0xFF;
				}

				/* ///////////////////////////////////////
				 * Perform periodic tasks based on current menu setting */
				switch(holdMenuState)
				{
   #if PRODUCT_TEST_INSTRUMENT_HEAD

						case MENU_SI5351:
						{
							if(g_button2_pressed || g_button3_pressed)
							{
								if((hold_tick_count > 1000) && !(hold_tick_count % 100))
								{
									printFrequency(displayedSubMenu[BUTTON4], selectedField, g_button2_pressed);
									LCD_blink_cursor_row_col(OFF, ROW0, COL0);
									cursorOFF = TRUE;
								}

								g_backlight_off_countdown = g_backlight_delay_value;    /* keep backlight illuminated */
							}
							else if(cursorOFF)
							{
								LCD_blink_cursor_row_col(ON, ROW1, columnForDigit(selectedField, FrequencyFormat));
								cursorOFF = FALSE;
							}
						}
						break;

						case MENU_STATUS:
						{
							if(g_adcUpdated[displayedSubMenu])
							{
								BOOL printADCResult = FALSE;
								g_adcUpdated[displayedSubMenu] = FALSE;

								switch(displayedSubMenu[BUTTON1])
								{
									case 0:                                                                                     /* Temperature */
									{
										int32_t t = 100 * (1866 - g_lastConversionResult[displayedSubMenu[BUTTON4]]) / 1169;    /* degrees C for LM20 */
										BOOL neg = FALSE;
										if(t < 0)
										{
											neg = TRUE;
											t = -t;
										}

										sprintf(g_tempBuffer, "%s%2ldC  ", neg ? "-" : "+", t);
										updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);
										printADCResult = TRUE;
									}

									/* intentional fall-through */
									case 1:                                                                                                                                                                         /* Battery Voltage */
									{
										if(!printADCResult)
										{
											uint16_t v = (uint16_t)( ( 1000 * ( (uint32_t)(g_lastConversionResult[displayedSubMenu[BUTTON4]] + POWER_SUPPLY_VOLTAGE_DROP_MV) ) ) / BATTERY_VOLTAGE_COEFFICIENT );   /* round up and adjust for voltage divider and drops */
											int16_t pc = -1;

											if(g_battery_type == BATTERY_4r2V)
											{
												pc = (v < 3200) ? 0 : MIN(5 * ((v - 3150) / 50), 100);
											}
											else if(g_battery_type == BATTERY_9V)
											{
												pc = (v < 7000) ? 0 : MIN(5 * ((v - 6950) / 100), 100);
											}

											sprintf(g_tempBuffer, "%1u.%02uV",  v / 1000, (v / 10) % 100);
											updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);

											if(pc >= 0)
											{
												sprintf(g_tempBuffer, "%s (%d%%) ",  g_tempBuffer, pc);
												updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);
											}

											printADCResult = TRUE;
										}
									}

									/* intentional fall-through */
									case 2: /* RSSI */
									{
										if(!printADCResult)
										{
											if(g_RSSI_data != WAITING_FOR_UPDATE)
											{
												sprintf(g_tempBuffer, "S: %d/3300mV    ",  g_RSSI_data);
												updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);
												printADCResult = TRUE;
												g_RSSI_data = WAITING_FOR_UPDATE;
											}
										}
									}
									break;

									default:
									{
									}
									break;
								}

								if(printADCResult)
								{
									LCD_print_row(g_textBuffer[ROW1], ROW1);
								}
							}
						}
						break;

   #elif PRODUCT_CONTROL_HEAD

						case MENU_STATUS:
						{
							BOOL caseHandled = FALSE;
							BOOL printResult = FALSE;
							clearTextBuffer(ROW1);

							switch(displayedSubMenu[BUTTON1])
							{
								case 0: /* Temperature */
								{
									updateLCDTextBuffer(g_textBuffer[ROW1], "N/A", FALSE);
									printResult = TRUE;
									caseHandled = TRUE;
								}

								/* intentional fall-through */
								case 1: /* Battery Voltage */
								{
									if(!caseHandled)
									{
										if(g_Battery_data != WAITING_FOR_UPDATE)
										{
											sprintf(g_tempBuffer, " %dmV    ",  g_Battery_data);
											updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);
											g_Battery_data = WAITING_FOR_UPDATE;
											printResult = TRUE;
										}

										caseHandled = TRUE;
									}
								}

								/* intentional fall-through */
								case 2: /* RSSI */
								{
									if(!caseHandled)
									{
										if(g_RSSI_data != WAITING_FOR_UPDATE)
										{
											sprintf(g_tempBuffer, " %d/3300mV    ",  g_RSSI_data);
											updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);
											g_RSSI_data = WAITING_FOR_UPDATE;
											printResult = TRUE;
										}
									}
								}
								break;

								default:
								{
								}
								break;
							}

							if(printResult)
							{
								LCD_print_row(g_textBuffer[ROW1], ROW1);
							}
						}
						break;


   #endif   /* PRODUCT_TEST_INSTRUMENT_HEAD */

						case MENU_POWER_OFF:
						{
							static uint8_t lastCountdown = 0;
							uint8_t countdown = (uint8_t)((10 * g_power_off_countdown) / POWER_OFF_DELAY);

							if(countdown != lastCountdown)
							{
								sprintf(g_tempBuffer, "        %2d", countdown);
								updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);
								lastCountdown = countdown;
								LCD_print_row(g_textBuffer[ROW1], ROW1);
							}
						}
						break;

						case MENU_SET_TIME:
						case MENU_REMOTE_DEVICE:
						{
							BOOL updateCursor = FALSE;
							static BOOL holdButtonState = OFF;
							BOOL buttonChanged = FALSE;
							static BOOL cursorModeEntered = FALSE;
							TextFormat textFormat;
							uint8_t maxDigit;
							uint8_t minDigit;

							if(holdMenuState == MENU_SET_TIME)
							{
								textFormat = HourMinuteSecondFormat;
								maxDigit = 3;
								minDigit = 1;
							}
							else
							{
								textFormat = FrequencyFormat;
								maxDigit = (dual_band_receiver.bandSetting == BAND_2M) ? 6 : 5;
								minDigit = 2;
							}

							if(holdButtonState != g_button0_pressed)
							{
								holdButtonState = g_button0_pressed;
								buttonChanged = TRUE;
							}

							if(g_button0_press_ticks > LONG_PRESS_TICK_COUNT)   /* long knob press detected */
							{
								if(cursorOFF)
								{
									cursorOFF = FALSE;
									selectedField = minDigit;
									updateCursor = TRUE;
									cursorModeEntered = FALSE;  /* ensure the button release gets ignored */
								}
								else
								{
									g_cursor_active_countdown = 0;
								}

								g_button0_press_ticks = 0;
							}
							else if(!cursorOFF && buttonChanged)
							{
								if(!cursorModeEntered)  /* ignore the button change that occurs when the knob is released */
								{
									cursorModeEntered = TRUE;
									g_button0_press_ticks = 0;
								}
								else
								{
									if(g_button0_pressed)               /* knob is currently pressed */
									{
										g_cursor_active_countdown = CURSOR_EXPIRATION_DELAY;
									}
									else if(g_button0_press_ticks > 50) /* knob has been released, and pressed again */
									{
										selectedField++;
										if(selectedField > maxDigit)
										{
											selectedField = minDigit;
										}
										updateCursor = TRUE;
									}
								}
							}

							if(updateCursor)                                            /* handle cursor moves and countdown resets */
							{
								LCD_blink_cursor_row_col(ON, ROW1, columnForDigit(selectedField, textFormat));
								g_cursor_active_countdown = CURSOR_EXPIRATION_DELAY;
								g_backlight_off_countdown = g_backlight_delay_value;    /* keep backlight illuminated */
							}
							else if(!g_cursor_active_countdown && !cursorOFF)           /* time to turn off the cursor */
							{
								cursorOFF = TRUE;
								LCD_blink_cursor_row_col(OFF, ROW0, COL0);
							}

							if(holdMenuState == MENU_SET_TIME)
							{
								if(!g_time_update_countdown)
								{
									g_time_update_countdown = 200;

									if(displayedSubMenu[BUTTON1] == 0)
									{
										ds3231_read_time(NULL, g_tempBuffer, HourMinuteSecondFormat);
										LCD_print_row_col(g_tempBuffer, ROW1, COL6);

										if(!cursorOFF)                                                                              /* Put the data into the display buffer for printing by the cursor-using function */
										{
											LCD_set_cursor_row_col(ROW1, columnForDigit(selectedField, HourMinuteSecondFormat));    /* return cursor where it belongs */
											char* buff = &g_textBuffer[ROW1][BUTTON4_COLUMN];
											updateLCDTextBuffer(buff, g_tempBuffer, TRUE);
										}
									}
									else
									{
										if(g_remote_device_time)
										{
											timeValToString(g_tempBuffer, g_remote_device_time, HourMinuteSecondFormat);
											LCD_print_row_col(g_tempBuffer, ROW1, COL6);
											g_remote_device_time = 0;
										}

										lb_send_TIM(LINKBUS_MSG_QUERY, NO_TIME_SPECIFIED);
									}
								}
							}
							else    /* if(holdMenuState == MENU_REMOTE_DEVICE) */
							{
								if(displayedSubMenu[BUTTON3] == 0)
								{
									if(g_RSSI_data != WAITING_FOR_UPDATE)
									{
										static int16_t lastRSSI = 0;

										if(g_RSSI_data != lastRSSI)
										{
											sprintf(g_tempBuffer, "%3d", g_RSSI_data / 33);

											LCD_print_row_col(g_tempBuffer, ROW1, BUTTON3_COLUMN);

											if(!cursorOFF)                                                                      /* Put the data into the display buffer for printing by the cursor-using function */
											{
												LCD_set_cursor_row_col(ROW1, columnForDigit(selectedField, FrequencyFormat));   /* return cursor where it belongs */
												char* buff = &g_textBuffer[ROW1][BUTTON3_COLUMN];
												updateLCDTextBuffer(buff, g_tempBuffer, TRUE);
											}

											lastRSSI = g_RSSI_data;
											g_RSSI_data = WAITING_FOR_UPDATE;
										}
									}
								}

								if(displayedSubMenu[BUTTON4] == 0)  /* time displayed */
								{
									if(!g_time_update_countdown)
									{
										int32_t now;

										g_time_update_countdown = 200;
										ds3231_read_time(&now, NULL, Time_Format_Not_Specified);
										timeValToString(g_tempBuffer, now - g_start_time, Minutes_Seconds_Elapsed);
										LCD_print_row_col(g_tempBuffer, ROW1, BUTTON4_COLUMN);

										if(!cursorOFF)                                                                      /* Put the data into the display buffer for printing by the cursor-using function */
										{
											LCD_set_cursor_row_col(ROW1, columnForDigit(selectedField, FrequencyFormat));   /* return cursor where it belongs */
											char* buff = &g_textBuffer[ROW1][BUTTON4_COLUMN];
											updateLCDTextBuffer(buff, g_tempBuffer, TRUE);
										}
									}
								}
							}
						}
						break;

						default:
						{
						}
						break;
				}

				if(!g_send_ID_countdown && !attach_success)
				{
					static uint8_t tries = 10;

					if(tries)
					{
						tries--;
						g_send_ID_countdown = SEND_ID_DELAY;
						lb_send_ID(LINKBUS_MSG_COMMAND, CONTROL_HEAD_ID, g_LB_attached_device);
					}
				}
			}

#elif PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

			/* ////////////////////////////////////
			 * Handle periodic tasks triggered by the tick count */
			if(hold_tick_count != g_tick_count)
			{
				hold_tick_count = g_tick_count;

				if(!g_LB_broadcast_interval && g_LB_broadcasts_enabled)
				{
					if(g_LB_broadcasts_enabled & UPC_TEMP_BROADCAST)
					{
						/* not yet supported - gets read from processor chip */
					}

					if(g_LB_broadcasts_enabled & BATTERY_BROADCAST)
					{
						if(g_adcUpdated[BATTERY_READING])
						{
							uint16_t v = (uint16_t)( ( 1000 * ( (uint32_t)(g_lastConversionResult[BATTERY_READING] + POWER_SUPPLY_VOLTAGE_DROP_MV) ) ) / BATTERY_VOLTAGE_COEFFICIENT ); /* round up and adjust for voltage divider and drops */
							lb_broadcast_bat(v);
							g_adcUpdated[BATTERY_READING] = FALSE;
							g_LB_broadcast_interval = 100;                                                                                                                              /* minimum delay before next broadcast */
						}
					}

					if(g_LB_broadcasts_enabled & RSSI_BROADCAST)
					{
						if(g_adcUpdated[RSSI_READING])
						{
							uint16_t v = g_lastConversionResult[RSSI_READING];  /* round up and adjust for voltage divider */
							lb_broadcast_rssi(v);
							g_adcUpdated[RSSI_READING] = FALSE;
							g_LB_broadcast_interval = 100;                      /* minimum delay before next broadcast */
						}
					}

					if(g_LB_broadcasts_enabled & RF_BROADCAST)
					{
						if(g_adcUpdated[RF_READING])
						{
							g_adcUpdated[RF_READING] = FALSE;
							uint16_t v = (uint16_t)(((uint32_t)(g_lastConversionResult[RF_READING]) + 9) / 100);    /* round up and adjust for voltage divider */
							lb_broadcast_rf(v);
							g_LB_broadcast_interval = 100;                                                          /* minimum delay before next broadcast */
						}
					}
				}

				if(!g_send_ID_countdown && !attach_success)
				{
					static uint8_t tries = 10;

					if(tries)
					{
						tries--;
						g_send_ID_countdown = SEND_ID_DELAY;
						lb_send_ID(LINKBUS_MSG_COMMAND, RECEIVER_ID, g_LB_attached_device);
					}
				}
			}

#endif  /* #if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */

#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

			/*********************************
			* Handle Rotary Encoder Turns
			*********************************/
			newCount = g_rotary_count / 4;
			if(newCount != holdCount)
			{
				switch(holdMenuState)
				{
   #if PRODUCT_TEST_INSTRUMENT_HEAD

						case MENU_BAND:
						{
							if(!g_menu_delay_countdown) /* slow things down to prevent double increments if dial clicks more than once */
							{
								RadioBand band = dual_band_receiver.bandSetting;

								if(band == BAND_80M)
								{
									band = BAND_2M;
								}
								else
								{
									band = BAND_80M;
								}

								lb_send_BND(LINKBUS_MSG_QUERY, band);

								holdMenuState = NUMBER_OF_MENUS;
								g_menu_delay_countdown = 100;
							}
						}
						break;

						case MENU_SI5351:
						{
							int8_t hold = selectedField;
							newCount > holdCount ? hold++ : hold--;

							if((hold > 1) && (hold < 9))
							{
								selectedField = hold;
								LCD_blink_cursor_row_col(ON, ROW1, columnForDigit(selectedField, FrequencyFormat));
							}
						}
						break;

   #endif   /* PRODUCT_TEST_INSTRUMENT_HEAD */

					case MENU_LCD:
					{
						if(displayedSubMenu[BUTTON4] == 0)
						{
							uint8_t up = newCount > holdCount;

							switch(g_backlight_setting)
							{
								case BL_OFF:
								{
									g_backlight_setting = up ? BL_LOW : BL_OFF;
								}
								break;

								case BL_LOW:
								{
									g_backlight_setting = up ? BL_MED : BL_OFF;
								}
								break;

								case BL_MED:
								{
									g_backlight_setting = up ? BL_HIGH : BL_LOW;
								}
								break;

								default:
								{
									g_backlight_setting = up ? BL_HIGH : BL_MED;
								}
								break;
							}

							OCR1BH = g_backlight_setting;   /* set PWM duty cycle @ 16bit */
							OCR1BL = 0xFF;
						}
						else
						{
							if(newCount > holdCount)
							{
								if(g_contrast_setting < 0x0F)
								{
									g_contrast_setting++;
								}
							}
							else
							{
								if(g_contrast_setting)
								{
									g_contrast_setting--;
								}
							}

							LCD_set_contrast(g_contrast_setting);
						}
					}
					break;

					case MENU_VOLUME:
					{
						if(!g_menu_delay_countdown) /* slow things down to prevent double increments if dial clicks more than once */
						{
							uint8_t up = newCount > holdCount;

							if(displayedSubMenu[BUTTON4] == 0)
							{
								lb_send_VOL(LINKBUS_MSG_QUERY, TONE_VOLUME, up ? INCREMENT_VOL : DECREMENT_VOL);
							}
							else
							{
								lb_send_VOL(LINKBUS_MSG_QUERY, MAIN_VOLUME, up ? INCREMENT_VOL : DECREMENT_VOL);
							}

							g_menu_delay_countdown = 50;
						}
					}
					break;

					case MENU_REMOTE_DEVICE:
					{
						if(!cursorOFF)
						{
							int32_t inc = 1;
							for(uint8_t i = 0; i < selectedField; inc *= 10, i++)
							{
								;
							}

							if(newCount < holdCount)
							{
								inc = -inc;
							}

							g_receiver_freq += inc;
							lb_send_FRE(LINKBUS_MSG_QUERY, g_receiver_freq, FALSE);
							g_cursor_active_countdown = CURSOR_EXPIRATION_DELAY;
						}
					}
					break;

					case MENU_SET_TIME:
					{
						if(!cursorOFF)
						{
							int32_t inc = 1;
							for(uint8_t i = 1; i < selectedField; inc *= 60, i++)
							{
								;
							}

							if(newCount < holdCount)
							{
								inc = -inc;
							}

							ds3231_set_time(inc);
							g_cursor_active_countdown = CURSOR_EXPIRATION_DELAY;
						}
					}
					break;

					default:    /* Includes MENU_POWER_OFF */
					{
					}
					break;
				}

				holdCount = newCount;
			}

			/**
			 * Handle Menu Changes */
			if(g_menu_state != holdMenuState)
			{
				cpu_irq_disable();  /* same as cli(); */
				MenuType tempMenuState = g_menu_state;
				g_rotary_count = 0;
				holdCount = 0;
				cpu_irq_enable();   /* same as sei(); */


   #if PRODUCT_TEST_INSTRUMENT_HEAD

					uint8_t clockstate = UNDETERMINED;

   #endif   /* PRODUCT_TEST_INSTRUMENT_HEAD */

				if(holdMenuState < NUMBER_OF_MENUS)
				{
					displayedSubMenu[BUTTON1] = 0;
					displayedSubMenu[BUTTON4] = 0;
					LCD_blink_cursor_row_col(OFF, ROW0, COL0);
				}

				switch(tempMenuState)
				{
   #if PRODUCT_TEST_INSTRUMENT_HEAD

						case MENU_BAND:
						{
							g_labels[BUTTON1] = textBandSelect;
							g_labels[BUTTON3] = (dual_band_receiver.bandSetting == BAND_80M) ? " 80m" : "  2m";
							g_labels[BUTTON2] = g_labels[BUTTON4] = NULL_CHAR;
							printButtons(g_textBuffer[ROW0], (char**)g_labels);
							updateLCDTextBuffer(g_textBuffer[ROW1], (char*)textTurnKnob, FALSE);
							LCD_print_screen(g_textBuffer);
						}
						break;

						case MENU_SI5351:
						{
							Frequency_Hz f;

							if(displayedSubMenu[BUTTON4] == 0)
							{
								clockstate = g_si5351_clk0_enabled;
								f = g_si5351_clk0_freq;
							}
							else if(displayedSubMenu[BUTTON4] == 1)
							{
								clockstate = g_si5351_clk1_enabled;
								f = g_si5351_clk1_freq;
							}
							else
							{
								clockstate = g_si5351_clk2_enabled;
								f = g_si5351_clk2_freq;
							}

							g_labels[BUTTON1] = clockstate ? textOn : textOff;
							g_labels[BUTTON2] = textPlus;
							g_labels[BUTTON3] = textMinus;
							g_labels[BUTTON4] = textMore;

							sprintf(g_tempBuffer, "CLK%d: %9ld Hz", displayedSubMenu[BUTTON4], f);
							updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);

							printButtons(g_textBuffer[ROW0], (char**)g_labels);
							LCD_print_screen(g_textBuffer);

							LCD_blink_cursor_row_col(ON, ROW1, columnForDigit(selectedField, FrequencyFormat));
						}
						break;

   #endif   /* PRODUCT_TEST_INSTRUMENT_HEAD */

					case MENU_LCD:
					{
						if(displayedSubMenu[BUTTON4] == 0)
						{
							g_labels[BUTTON1] = textBacklight;
							g_labels[BUTTON2] = g_labels[BUTTON3] = NULL_CHAR;
							g_labels[BUTTON4] = textMore;
						}
						else
						{
							g_labels[BUTTON1] = textContrast;
							g_labels[BUTTON2] = g_labels[BUTTON3] = NULL_CHAR;
							g_labels[BUTTON4] = textMore;
						}

						printButtons(g_textBuffer[ROW0], (char**)g_labels);
						updateLCDTextBuffer(g_textBuffer[ROW1], (char*)textTurnKnob, FALSE);
						LCD_print_screen(g_textBuffer);
					}
					break;

					case MENU_VOLUME:
					{
						if(displayedSubMenu[BUTTON4] == 0)
						{
							g_labels[BUTTON1] = textToneVolume;
						}
						else    /* if(displayedSubMenu[BUTTON4] == 1) */
						{
							g_labels[BUTTON1] = textMainVolume;
						}

						g_labels[BUTTON2] = g_labels[BUTTON3] = NULL_CHAR;
						g_labels[BUTTON4] = textMore;

						printButtons(g_textBuffer[ROW0], (char**)g_labels);
						updateLCDTextBuffer(g_textBuffer[ROW1], (char*)textTurnKnob, FALSE);
						LCD_print_screen(g_textBuffer);
					}
					break;

					case MENU_STATUS:
					{
						/*LCD_TEMP, BAT_VOLTAGE, RSSI_LEVEL (remote) */
						if(displayedSubMenu[BUTTON1] == 0)
						{
							g_labels[BUTTON1] = textTemperature;
						}
						else if(displayedSubMenu[BUTTON1] == 1)
						{
							g_labels[BUTTON1] = textBattery;
							lb_send_BCR(BATTERY_BROADCAST, ON);
						}
						else
						{
							g_labels[BUTTON1] = textRSSI;
							lb_send_BCR(RSSI_BROADCAST, ON);
						}

						g_labels[BUTTON2] = g_labels[BUTTON3] = g_labels[BUTTON4] = NULL_CHAR;

						printButtons(g_textBuffer[ROW0], (char**)g_labels);
						clearTextBuffer(ROW1);
						LCD_print_screen(g_textBuffer);
					}
					break;

					case MENU_POWER_OFF:
					{
						updateLCDTextBuffer(g_textBuffer[ROW0], (char*)textShuttingDown, FALSE);
						clearTextBuffer(ROW1);
						LCD_print_screen(g_textBuffer);
					}
					break;

					case MENU_LOW_BATTERY:
					{
						LCD_print_row((char*)textChargeBattery, ROW0);
						clearTextBuffer(ROW1);
						LCD_print_row(g_textBuffer[ROW1], ROW1);
					}
					break;

					case MENU_REMOTE_DEVICE:
					{
						if(g_LB_attached_device == RECEIVER_ID)
						{
							char tempStr[5];    /* temporary character storage */

							if(holdMenuState <= UPDATE_DISPLAY_WITH_LB_DATA)
							{
								g_labels[BUTTON1] = (dual_band_receiver.bandSetting == BAND_80M) ? " 80m" : "  2m";

								BOOL freqChanged = dual_band_receiver.currentMemoryFrequency != dual_band_receiver.currentUserFrequency;
								sprintf(tempStr, "MEM%1d%s", 1 + displayedSubMenu[BUTTON2], freqChanged ? "*" : NULL);  /* MEM1 -> M5 */
								g_labels[BUTTON2] = (const char*)tempStr;

								if(displayedSubMenu[BUTTON3] == 0)
								{
									g_labels[BUTTON3] = textRSSI;   /* S, RF, distance, etc */
									lb_send_BCR(RSSI_BROADCAST, ON);
								}

								g_labels[BUTTON4] = " TIME";        /* time, battery, temperature, bearing, etc */

								printButtons(g_textBuffer[ROW0], (char**)g_labels);

								printFrequency(g_receiver_freq, cursorOFF ? 0 : selectedField);
								inhibitMenuExpiration = TRUE;

							}

							LCD_print_screen(g_textBuffer);

							if(!cursorOFF)
							{
								LCD_blink_cursor_row_col(ON, ROW1, columnForDigit(selectedField, FrequencyFormat));
							}
						}
					}
					break;

					case MENU_SET_TIME:
					{
						if(displayedSubMenu[BUTTON1] == 0)
						{
							g_labels[BUTTON1] = textTime;
						}
						else
						{
							g_labels[BUTTON1] = textRemote;
						}

						g_labels[BUTTON2] = g_labels[BUTTON3] = g_labels[BUTTON4] = NULL_CHAR;

						if((g_LB_attached_device == RECEIVER_ID) && (displayedSubMenu[BUTTON1] == 0))
						{
							g_labels[BUTTON4] = "SYNC";
						}

						printButtons(g_textBuffer[ROW0], (char**)g_labels);
						clearTextBuffer(ROW1);
						LCD_print_screen(g_textBuffer);
					}
					break;

					default:
					case MENU_MAIN:
					{
						if(holdMenuState != UPDATE_DISPLAY_WITH_LB_DATA)    /* Prevent Linkbus replies from triggering more queries */
						{
/*						displayedSubMenu[BUTTON4] = NUMBER_OF_POLLED_ADC_CHANNELS; */
							sprintf(g_tempBuffer, "%s - v %s", PRODUCT_NAME_SHORT, SW_REVISION);
							updateLCDTextBuffer(g_textBuffer[ROW0], g_tempBuffer, FALSE);
							LCD_print_row(g_textBuffer[ROW0], ROW0);
							LCD_print_row((char*)textMenusAccess, ROW1);
						}
					}
					break;
				}

				holdMenuState = tempMenuState;
			}

			/* Handle Button 1 Presses */
			if(hold_button1_presses != g_button1_presses)
			{
				hold_button1_presses = g_button1_presses;

				switch(holdMenuState)
				{
   #if PRODUCT_TEST_INSTRUMENT_HEAD

						case MENU_SI5351:
						{
							Si5351_clock_enable state;

							if(displayedSubMenu[BUTTON4] == 0)
							{
								state = g_si5351_clk0_enabled = !g_si5351_clk0_enabled;
							}
							else if(displayedSubMenu[BUTTON4] == 1)
							{
								state = g_si5351_clk1_enabled = !g_si5351_clk1_enabled;
							}
							else
							{
								state = g_si5351_clk2_enabled = !g_si5351_clk2_enabled;
							}

							lb_send_CKn(LINKBUS_MSG_QUERY, (Si5351_clock)displayedSubMenu[BUTTON4], FREQUENCY_NOT_SPECIFIED, state, SI5351_DRIVE_NOT_SPECIFIED);
						}
						break;

   #elif PRODUCT_CONTROL_HEAD

						case MENU_REMOTE_DEVICE:
						{
							lb_send_BND(LINKBUS_MSG_QUERY, (dual_band_receiver.bandSetting == BAND_2M) ? BAND_80M : BAND_2M);
						}
						break;

   #endif                                                           /* PRODUCT_TEST_INSTRUMENT_HEAD */

						case MENU_STATUS:
						{
							if(displayedSubMenu[BUTTON1] == 0)      /* Temperature */
							{
								lb_send_BCR(RSSI_BROADCAST, OFF);
							}
							else if(displayedSubMenu[BUTTON1] == 1) /* Battery */
							{
								lb_send_BCR(UPC_TEMP_BROADCAST, OFF);
							}
							else                                    /* RSSI */
							{
								lb_send_BCR(BATTERY_BROADCAST, OFF);
							}

							displayedSubMenu[BUTTON1]++;
							displayedSubMenu[BUTTON1] %= 3;
							holdMenuState = LEAVE_MENU_UNCHANGED;   /* Ensure the screen is updated */
						}
						break;

						case MENU_MAIN:
						{
							if(g_backlight_delay_value == BACKLIGHT_OFF_DELAY)
							{
								g_backlight_delay_value = BACKLIGHT_ALWAYS_ON;
								inhibitMenuExpiration = TRUE;
							}
							else
							{
								g_backlight_delay_value = BACKLIGHT_OFF_DELAY;
								inhibitMenuExpiration = FALSE;
							}

							g_backlight_off_countdown = g_backlight_delay_value;
						}
						break;

						case MENU_SET_TIME:
						{
							displayedSubMenu[BUTTON1]++;
							displayedSubMenu[BUTTON1] %= 2;

							if(displayedSubMenu[BUTTON1] == 0)  /* Local RTC */
							{
							}
							else                                /* Remote RTC */
							{
								lb_send_TIM(LINKBUS_MSG_QUERY, NO_TIME_SPECIFIED);
							}

							holdMenuState = LEAVE_MENU_UNCHANGED;   /* Ensure the screen is updated */
						}
						break;

						default:
						{
						}
						break;
				}
			}

			/* Handle Button 2 Presses */
			if(hold_button2_presses != g_button2_presses)
			{
				hold_button2_presses = g_button2_presses;

				switch(holdMenuState)
				{
   #if PRODUCT_TEST_INSTRUMENT_HEAD

						case MENU_SI5351:
						{
							hold_tick_count = 0; g_tick_count = 1;
							printFrequency(displayedSubMenu[BUTTON4], selectedField, TRUE);
						}
						break;

   #elif PRODUCT_CONTROL_HEAD

						case MENU_REMOTE_DEVICE:
						{
							displayedSubMenu[BUTTON2]++;
							displayedSubMenu[BUTTON2] %= 5;
							holdMenuState = LEAVE_MENU_UNCHANGED;   /* Ensure the screen is updated */
							lb_send_FRE(LINKBUS_MSG_QUERY, (Frequency_Hz)(displayedSubMenu[BUTTON2] + 1), FALSE);
							g_cursor_active_countdown = 0;          /* exit cursor mode, if it happened to be in effect at this time */
						}
						break;


   #endif   /* PRODUCT_TEST_INSTRUMENT_HEAD */

						default:
						{
						}
						break;
				}
			}
			else if(g_button2_pressed)
			{
				if(g_pressed_button_ticks > LONG_PRESS_TICK_COUNT)
				{
					g_ignore_button_release = TRUE;
					lb_send_FRE(LINKBUS_MSG_COMMAND, (Frequency_Hz)(displayedSubMenu[BUTTON2] + 1), FALSE);
					g_cursor_active_countdown = 0;  /* exit cursor mode, if it happened to be in effect at this time */
				}
			}

			/* Handle Button 3 Presses */
			if(hold_button3_presses != g_button3_presses)
			{
				hold_button3_presses = g_button3_presses;

				switch(holdMenuState)
				{
   #if PRODUCT_TEST_INSTRUMENT_HEAD

						case MENU_SI5351:
						{
							hold_tick_count = 0; g_tick_count = 1;

							printFrequency(displayedSubMenu[BUTTON4], selectedField, FALSE);
						}
						break;

   #endif   /* PRODUCT_TEST_INSTRUMENT_HEAD */

					default:
					{
					}
					break;
				}
			}

			/* Handle Button 4 Presses */
			if(hold_button4_presses != g_button4_presses)
			{
				hold_button4_presses = g_button4_presses;

				uint8_t maxVal = 0;

				switch(holdMenuState)
				{

   #if PRODUCT_TEST_INSTRUMENT_HEAD

						case MENU_SI5351:
						{
							maxVal = NUMBER_OF_SI5351_SUBMENUS;
						}
						break;

   #endif   /* PRODUCT_TEST_INSTRUMENT_HEAD */

					case MENU_SET_TIME:
					{
						if(displayedSubMenu[BUTTON1] == 0)
						{
							int32_t time;
							ds3231_read_time(&time, NULL, Time_Format_Not_Specified);
							lb_send_TIM(LINKBUS_MSG_QUERY, time);
						}
					}
					break;

					case MENU_VOLUME:
					{
						maxVal = NUMBER_OF_VOLUME_SUBMENUS;
					}
					break;

					case MENU_LCD:
					{
						maxVal = NUMBER_OF_LCD_SUBMENUS;
					}
					break;

					default:
					{
					}
					break;
				}

				if(maxVal)
				{
					displayedSubMenu[BUTTON4]++;
					displayedSubMenu[BUTTON4] %= maxVal;

					switch(holdMenuState)
					{
   #if PRODUCT_TEST_INSTRUMENT_HEAD

							case MENU_SI5351:
							{
								lb_send_CKn(LINKBUS_MSG_QUERY, (Si5351_clock)displayedSubMenu[BUTTON4], FREQUENCY_NOT_SPECIFIED, SI5351_ENABLE_NOT_SPECIFIED, SI5351_DRIVE_NOT_SPECIFIED);
							}
							break;

   #endif   /* PRODUCT_TEST_INSTRUMENT_HEAD */

						case MENU_VOLUME:
						{

						}
						break;

						case MENU_LCD:
						{
						}
						break;

						default:
						{
						}
						break;
					}

					holdMenuState = LEAVE_MENU_UNCHANGED;   /* Ensure the screen is updated */
				}
			}

			if(hold_button5_presses != g_button5_presses)
			{
				hold_button5_presses = g_button5_presses;
			}

#else   /* #if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */

			/* ////////////////////////////////////
			 * Handle Rotary Encoder Turns */
			newCount = g_rotary_count / 4;
			if(newCount != holdCount)
			{
				holdCount = newCount;
			}

			/* Handle Button 1 Presses */
			if(hold_button1_presses != g_button1_presses)
			{
				g_beep_length = BEEP_SHORT;
				hold_button1_presses = g_button1_presses;
			}

			/* Handle Button 2 Presses */
			if(hold_button2_presses != g_button2_presses)
			{
				g_beep_length = BEEP_SHORT;
				hold_button2_presses = g_button2_presses;
			}

			/* Handle Button 3 Presses */
			if(hold_button3_presses != g_button3_presses)
			{
				g_beep_length = BEEP_SHORT;
				hold_button3_presses = g_button3_presses;
			}

			/* Handle Button 4 Presses */
			if(hold_button4_presses != g_button4_presses)
			{
				g_beep_length = BEEP_SHORT;
				hold_button4_presses = g_button4_presses;
			}

			if(hold_button5_presses != g_button5_presses)
			{
				g_beep_length = BEEP_SHORT;
				hold_button5_presses = g_button5_presses;
			}

#endif  /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */

	}       /* while(1) */
}/* main */

/**********************
**********************/

#if PRODUCT_DUAL_BAND_RECEIVER || PRODUCT_TEST_DIGITAL_INTERFACE

	void initializeEEPROMVars(void)
	{
		if(eeprom_read_byte(&ee_interface_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
		{
			g_tone_volume = eeprom_read_byte(&ee_tone_volume_setting);
			g_main_volume = eeprom_read_byte(&ee_main_volume_setting);
		}
		else
		{
			g_tone_volume = EEPROM_TONE_VOLUME_DEFAULT;
			g_main_volume = EEPROM_MAIN_VOLUME_DEFAULT;

			saveAllEEPROM();
			eeprom_write_byte(&ee_interface_eeprom_initialization_flag, EEPROM_INITIALIZED_FLAG);
		}
	}

	void saveAllEEPROM()
	{
		storeEEbyteIfChanged(&ee_tone_volume_setting, g_tone_volume);
		storeEEbyteIfChanged(&ee_main_volume_setting, g_main_volume);
	}

#elif PRODUCT_CONTROL_HEAD

/*********************************************************/
	void initializeEEPROMVars(void)
	{
		if(eeprom_read_byte(&ee_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
		{
			g_contrast_setting = eeprom_read_byte(&ee_contrast_setting);
			g_backlight_setting = eeprom_read_byte(&ee_backlight_setting);
		}
		else
		{
			g_backlight_setting = EEPROM_BACKLIGHT_DEFAULT;
			g_contrast_setting = EEPROM_CONTRAST_DEFAULT;
			saveAllEEPROM();
			eeprom_write_byte(&ee_eeprom_initialization_flag, EEPROM_INITIALIZED_FLAG);
		}
	}

	void saveAllEEPROM()
	{
		storeEEbyteIfChanged(&ee_contrast_setting, g_contrast_setting);
		storeEEbyteIfChanged(&ee_backlight_setting, g_backlight_setting);
	}


	void setMsgGraph(uint8_t len, LcdRowType row)
	{
		clearTextBuffer(row);

		uint8_t s = 0;
		g_textBuffer[row][0] = '#';

		for(int i = 1; i <= len; i++)
		{
			if(i % 2)
			{
				g_textBuffer[row][i] = '#';
			}
			else
			{
				g_textBuffer[row][i] = '1' + s++;
			}
		}
	}

/*********************************************************/

	Frequency_Hz setFrequency(Frequency_Hz freq, uint8_t digit, BOOL increment)
	{
		uint32_t inc = 1;

		for(int i = 0; i < digit; inc *= 10, i++)
		{
			;
		}

		Frequency_Hz hold = freq;
		freq = hold + (increment ? inc : -inc);

		if((freq <= RX_MAXIMUM_2M_FREQUENCY) && (freq >= RX_MINIMUM_80M_FREQUENCY))
		{
			lb_send_FRE(LINKBUS_MSG_QUERY, freq, FALSE);
			return(freq);
		}

		return(hold);
	}

	void printFrequency(Frequency_Hz freq, uint8_t digit)
	{
		if(freq == FREQUENCY_NOT_SPECIFIED)
		{
			updateLCDTextBuffer(g_textBuffer[ROW1], "N/A", FALSE);
		}
		else
		{
			char mhz[4];
			char khz[4];
			char h[2];

			sprintf(mhz, "%3ld", freq / 1000000);
			sprintf(khz, "%3ld", (freq % 1000000) / 1000);
			sprintf(h, "%1ld", (freq % 1000) / 100);
			sprintf(g_tempBuffer, "%s.%s.%s", mhz, khz, h);
			updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, TRUE);

			if(digit)
			{
				LCD_blink_cursor_row_col(ON, ROW1, columnForDigit(digit, FrequencyFormat));
			}
		}
	}

/*
 *       Provides the correct column value for the frequency digit when the frequency is printed left-justified in the following format:
 *       mmm.ttt.h where mmm is the MHz digits, ttt is the thousands digits, and h is the hundreds digit of Hertz. Handles time formats as well.
 */
	LcdColType columnForDigit(int8_t digit, TextFormat format)
	{
		LcdColType result = INVALID_LCD_COLUMN;

		switch(format)
		{
			case FrequencyFormat:   /* mmm.ttt.h where mmm is the MHz digits, ttt is the thousands digits, and h is the hundreds digit of Hertz */
			{
				result = 2 - digit + COL8;

				if(result < COL8)
				{
					result--;

					if(result < COL4)
					{
						result--;
					}
				}
			}
			break;

			case HourMinuteSecondFormat:    /* HH:MM:SS */
			{
				result = COL13 - 3 * (digit - 1);

				if(result < COL7)
				{
					result = COL13;
				}
			}
			break;

			case HourMinuteSecondDateFormat:
			{
				result = COL14 - digit;

				if(result < COL13)
				{
					result -= 2;

					if(result < COL11)
					{
						result -= 2;
					}
				}
			}
			break;
		}

		return(result);
	}

#elif PRODUCT_TEST_INSTRUMENT_HEAD

/*********************************************************/

	Frequency_Hz printFrequency(uint8_t index, uint8_t digit, BOOL increment)
	{
		uint32_t inc = 1;
		uint32_t *clk_freq;

		for(int i = 0; i < digit; inc *= 10, i++)
		{
			;
		}

		if(index == 0)
		{
			clk_freq = &g_si5351_clk0_freq;
		}
		else if(index == 1)
		{
			clk_freq = &g_si5351_clk1_freq;
		}
		else
		{
			clk_freq = &g_si5351_clk2_freq;
		}

		Frequency_Hz hold = *clk_freq;
		Frequency_Hz freq = hold + (increment ? inc : -inc);

		if((freq <= SI5351_CLKOUT_MAX_FREQ) && (freq >= SI5351_CLKOUT_MIN_FREQ))
		{
			lb_send_CKn(LINKBUS_MSG_QUERY, (Si5351_clock)index, freq, SI5351_ENABLE_NOT_SPECIFIED, SI5351_DRIVE_NOT_SPECIFIED);
			sprintf(g_tempBuffer, "CLK%d: %9ld Hz", index, freq);
			updateLCDTextBuffer(g_textBuffer[ROW1], g_tempBuffer, FALSE);
			LCD_print_row(g_textBuffer[ROW1], ROW1);
			LCD_blink_cursor_row_col(ON, ROW1, COL14 - digit);

			*clk_freq = freq;
			return(freq);
		}

		return(hold);
	}

#endif  /* PRODUCT_TEST_INSTRUMENT_HEAD */


#if PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD

	void clearTextBuffer(LcdRowType row)
	{
		memset(g_textBuffer[row], ' ', NUMBER_OF_LCD_COLS);
		g_textBuffer[row][NUMBER_OF_LCD_COLS] = '\0';
	}

/**
 *       Writes text into buffer, padding spaces in buffer locations not written by text
 */
	void updateLCDTextBuffer(char* buffer, char* text, BOOL preserveContents)
	{
		uint8_t i, len;

		len = strlen(text);

		for(i = 0; i < NUMBER_OF_LCD_COLS; i++)
		{
			if(i < len)
			{
				buffer[i] = text[i];
			}
			else
			{
				if(preserveContents)
				{
					break;
				}
				buffer[i] = ' ';
			}
		}
	}


	void printButtons(char buff[DISPLAY_WIDTH_STRING_SIZE], char* labels[])
	{
		memset(buff, ' ', NUMBER_OF_LCD_COLS);
		buff[NUMBER_OF_LCD_COLS] = '\0';

		for(uint8_t i = 0; i < 4; i++)
		{
			uint8_t x = 0;

			while(g_labels[i][x])
			{
				buff[i * 5 + x] = g_labels[i][x];
				x++;
			}
		}
	}

#endif  /* PRODUCT_CONTROL_HEAD || PRODUCT_TEST_INSTRUMENT_HEAD */