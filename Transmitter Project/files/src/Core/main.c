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
 * main.c
 *
 */

#include <ctype.h>
#include <asf.h>
#include <time.h>
#include "defs.h"
#include "si5351.h"		/* Programmable clock generator */
#include "ds3231.h"
#include "mcp23017.h"	/* Port expander on Rev X2 Digital Interface board */
#include "i2c.h"
#include "linkbus.h"
#include "transmitter.h"
#include "huzzah.h"
#include "util.h"
#include "morse.h"

#include <avr/io.h>
#include <stdint.h>         /* has to be added to use uint8_t */
#include <avr/interrupt.h>  /* Needed to use interrupts */
#include <stdio.h>
#include <string.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>

/***********************************************************************
 * Local Typedefs
************************************************************************/

typedef enum
{
	WD_SW_RESETS,
	WD_HW_RESETS,
	WD_FORCE_RESET,
	WD_DISABLE
} WDReset;


/***********************************************************************
 * Global Variables & String Constants
 *
 * Identify each global with a "g_" prefix
 * Whenever possible limit globals' scope to this file using "static"
 * Use "volatile" for globals shared between ISRs and foreground
 ************************************************************************/
static char g_tempStr[21] = {'\0'};
static volatile EC g_last_error_code = ERROR_CODE_NO_ERROR;
static volatile SC g_last_status_code = STATUS_CODE_IDLE;

static volatile BOOL g_powering_off = FALSE;

static volatile uint8_t g_wifi_enable_delay = 0;

static volatile uint16_t g_util_tick_countdown = 0;
static volatile BOOL g_battery_measurements_active = FALSE;
static volatile uint16_t g_maximum_battery = 0;
volatile BatteryType g_battery_type = BATTERY_UNKNOWN;

static volatile BOOL g_antenna_connection_changed = TRUE;
volatile AntConnType g_antenna_connect_state = ANT_CONNECTION_UNDETERMINED;
static volatile uint16_t g_2m_bias_delay = 0;

//EC (*g_txTask)(BiasStateMachineCommand* smCommand) = NULL; /* allows the transmitter to specify functions to run in the foreground */
extern EC (*g_txTask)(BiasStateMachineCommand* smCommand);  /* allow the transmitter to specify functions to run in the foreground */
volatile uint8_t g_mod_up = MAX_2M_CW_DRIVE_LEVEL;
volatile uint8_t g_mod_down = MAX_2M_CW_DRIVE_LEVEL;

/* Linkbus variables */
#define MAX_PATTERN_TEXT_LENGTH 20

static BOOL EEMEM ee_interface_eeprom_initialization_flag = EEPROM_UNINITIALIZED;

static char EEMEM ee_stationID_text[MAX_PATTERN_TEXT_LENGTH+1];
static char EEMEM ee_pattern_text[MAX_PATTERN_TEXT_LENGTH+1];
static uint8_t EEMEM ee_pattern_codespeed;
static uint8_t EEMEM ee_id_codespeed;
static uint16_t EEMEM ee_on_air_time;
static uint16_t EEMEM ee_off_air_time;
static uint16_t EEMEM ee_intra_cycle_delay_time;
static uint16_t EEMEM ee_ID_time;
static time_t EEMEM ee_start_time;
static time_t EEMEM ee_finish_time;
//static BOOL EEMEM ee_event_enabled;

static char g_messages_text[2][MAX_PATTERN_TEXT_LENGTH+1] = {"\0", "\0"};
static volatile uint8_t g_id_codespeed = EEPROM_ID_CODE_SPEED_DEFAULT;
static volatile uint8_t g_pattern_codespeed = EEPROM_PATTERN_CODE_SPEED_DEFAULT;
static volatile uint16_t g_time_needed_for_ID = 0;
static volatile int16_t g_on_air_seconds = EEPROM_ON_AIR_TIME_DEFAULT; /* amount of time to spend on the air */
static volatile int16_t g_off_air_seconds = EEPROM_OFF_AIR_TIME_DEFAULT; /* amount of time to wait before returning to the air */
static volatile int16_t g_intra_cycle_delay_time = EEPROM_INTRA_CYCLE_DELAY_TIME_DEFAULT; /* offset time into a repeating transmit cycle */
static volatile int16_t g_ID_time = EEPROM_ID_TIME_INTERVAL_DEFAULT; /* amount of time between ID/callsign transmissions */
static volatile time_t g_event_start_time = EEPROM_START_TIME_DEFAULT;
static volatile time_t g_event_finish_time = EEPROM_FINISH_TIME_DEFAULT;
static volatile BOOL g_event_enabled = EEPROM_EVENT_ENABLED_DEFAULT; /* indicates that the conditions for executing the event are set */
static volatile BOOL g_event_commenced = FALSE;
static volatile BOOL g_check_for_next_event = FALSE;

static volatile int32_t g_on_the_air = 0;
static volatile uint16_t g_time_to_send_ID_countdown = 0;
static volatile uint16_t g_code_throttle = 50;
static volatile uint8_t g_WiFi_shutdown_seconds = 120;
static volatile BOOL g_report_seconds = FALSE;

static int16_t g_ID_seconds = 60; /* holds value for g_ID_time adjusted for g_on_air_seconds */

/* ADC Defines */

#define BATTERY_READING 0
#define PA_VOLTAGE_READING 1
#define V12V_VOLTAGE_READING 2
#define BAND_80M_ANTENNA 3
#define BAND_2M_ANTENNA 4

#define BAND_80M_ANT_DETECT ADCH0
#define BAND_2M_ANT_DETECT ADCH1
#define V12V_VOLTAGE ADCH2
#define TX_PA_DRIVE_VOLTAGE ADCH6
#define BAT_VOLTAGE ADCH7
#define NUMBER_OF_POLLED_ADC_CHANNELS 5
static const uint8_t activeADC[NUMBER_OF_POLLED_ADC_CHANNELS] = { BAT_VOLTAGE, TX_PA_DRIVE_VOLTAGE, V12V_VOLTAGE, BAND_80M_ANT_DETECT, BAND_2M_ANT_DETECT };

static const uint16_t g_adcChannelConversionPeriod_ticks[NUMBER_OF_POLLED_ADC_CHANNELS] = { TIMER2_0_5HZ, TIMER2_0_5HZ, TIMER2_0_5HZ, TIMER2_5_8HZ, TIMER2_5_8HZ };
static volatile uint16_t g_tickCountdownADCFlag[NUMBER_OF_POLLED_ADC_CHANNELS] = { TIMER2_0_5HZ, TIMER2_0_5HZ, TIMER2_0_5HZ, TIMER2_5_8HZ, TIMER2_5_8HZ };
static uint16_t g_filterADCValue[NUMBER_OF_POLLED_ADC_CHANNELS] = { 500, 500, 500, 500, 500 };
static volatile BOOL g_adcUpdated[NUMBER_OF_POLLED_ADC_CHANNELS] = { FALSE, FALSE, FALSE, FALSE, FALSE };
static volatile uint16_t g_lastConversionResult[NUMBER_OF_POLLED_ADC_CHANNELS];

//static volatile uint32_t g_PA_voltage = 0;

extern volatile BOOL g_i2c_not_timed_out;
static volatile BOOL g_sufficient_power_detected = FALSE;
static volatile BOOL g_enableHardwareWDResets = FALSE;
extern BOOL g_am_modulation_enabled;

static volatile BOOL g_go_to_sleep = FALSE;
static volatile BOOL g_sleeping = FALSE;
static volatile uint16_t g_seconds_left_to_sleep = 0;
static volatile uint16_t g_seconds_to_sleep = MAX_UINT16;

/***********************************************************************
 * Private Function Prototypes
 *
 * These functions are available only within this file
 ************************************************************************/
BOOL eventEnabled(void);
void handleLinkBusMsgs(void);
void initializeEEPROMVars(BOOL skipEventEnabled);
void saveAllEEPROM(void);
void wdt_init(WDReset resetType);
uint16_t throttleValue(uint8_t speed);
EC activateEventUsingCurrentSettings(SC* statusCode);
EC hw_init(void);
EC rtc_init(void);
void set_ports(InitActionType initType);
BOOL antennaIsConnected(void);
void initializeAllEventSettings(BOOL disableEvent);


/***********************************************************************
 * Watchdog Timer ISR
 *
 * Notice: Optimization must be enabled before watchdog can be set
 * in C (WDCE). Use __attribute__ to enforce optimization level.
 ************************************************************************/
void __attribute__((optimize("O1"))) wdt_init(WDReset resetType)
{
	wdt_reset();

	if(MCUSR & (1 << WDRF))     /* If a reset was caused by the Watchdog Timer perform any special operations */
	{
		MCUSR &= (1 << WDRF);   /* Clear the WDT reset flag */
	}

	if(resetType == WD_DISABLE)
	{
		/* Clear WDRF in MCUSR */
		MCUSR &= ~(1<<WDRF);
		/* Write logical one to WDCE and WDE */
		/* Keep old prescaler setting to prevent unintentional
		time-out */
		WDTCSR |= (1<<WDCE) | (1<<WDE);
		/* Turn off WDT */
		WDTCSR = 0x00;
		g_enableHardwareWDResets = FALSE;
	}
	else
	{
		if(resetType == WD_HW_RESETS)
		{
			WDTCSR |= (1 << WDCE) | (1 << WDE);
			WDTCSR = (1 << WDP3) | (1 << WDIE) | (1 << WDE);    /* Enable WD interrupt every 4 seconds, and hardware resets */
			/*	WDTCSR = (1 << WDP3) | (1 << WDP0) | (1 << WDIE) | (1 << WDE); // Enable WD interrupt every 8 seconds, and hardware resets */
		}
		else if(resetType == WD_SW_RESETS)
		{
			WDTCSR |= (1 << WDCE) | (1 << WDE);
			/*	WDTCSR = (1 << WDP3) | (1 << WDIE); // Enable WD interrupt every 4 seconds (no HW reset)
			 *	WDTCSR = (1 << WDP3) | (1 << WDP0)  | (1 << WDIE); // Enable WD interrupt every 8 seconds (no HW reset) */
			WDTCSR = (1 << WDP1) | (1 << WDP2)  | (1 << WDIE);  /* Enable WD interrupt every 1 seconds (no HW reset) */
		}
		else
		{
			WDTCSR |= (1 << WDCE) | (1 << WDE);
			WDTCSR = (1 << WDIE) | (1 << WDE);    /* Enable WD interrupt in 16ms, and hardware reset */
		}

		g_enableHardwareWDResets = (resetType != WD_SW_RESETS);
	}
}


/***********************************************************************
 * Handle antenna connection interrupts
 **********************************************************************/
#ifdef SELECTIVELY_DISABLE_OPTIMIZATION
__attribute__((optimize("O0"))) ISR( INT1_vect )
#else
ISR( INT1_vect )
#endif
{
//	if(g_sleeping)
//	{
//		g_seconds_left_to_sleep = 0;
//		g_go_to_sleep = FALSE;
//		g_sleeping = FALSE;
//	}
	BOOL ant = antennaIsConnected();

	if(!ant) // immediately detect disconnection
	{
		if(g_antenna_connect_state != ANT_ALL_DISCONNECTED)
		{
			g_antenna_connect_state = ANT_ALL_DISCONNECTED;
			g_antenna_connection_changed = TRUE;
		}
	}
}


/***********************************************************************
 * Handle RTC interrupts
 **********************************************************************/
#ifdef SELECTIVELY_DISABLE_OPTIMIZATION
__attribute__((optimize("O0"))) ISR( INT0_vect )
#else
ISR( INT0_vect )
#endif
{
	static BOOL lastAntennaConnectionState = FALSE;
	static uint8_t antennaReadCount = 3;
	BOOL ant = antennaIsConnected();

	if(!ant) // immediately detect disconnection
	{
		if(g_antenna_connect_state != ANT_ALL_DISCONNECTED)
		{
			g_antenna_connect_state = ANT_ALL_DISCONNECTED;
			g_antenna_connection_changed = TRUE;
		}
	}
	else if(g_antenna_connect_state == ANT_ALL_DISCONNECTED)
	{
		if(ant == lastAntennaConnectionState)
		{
			if(antennaReadCount)
			{
				antennaReadCount--;

				if(!antennaReadCount)
				{
					g_antenna_connect_state = ANT_CONNECTION_UNDETERMINED;
					g_antenna_connection_changed = TRUE;
					antennaReadCount = 3;
				}
			}
		}
		else
		{
			antennaReadCount = 3;
		}
	}

	lastAntennaConnectionState = ant;

	system_tick();

	if(g_sleeping)
	{
		if(g_seconds_left_to_sleep) g_seconds_left_to_sleep--;

		if(!g_seconds_left_to_sleep || g_antenna_connection_changed)
		{
			g_go_to_sleep = FALSE;
			g_sleeping = FALSE;
		}
	}
	else
	{
		time_t temp_time;

		if(g_event_enabled)
		{
			if(g_event_commenced)
			{
				BOOL repeat;

				if(g_time_to_send_ID_countdown) g_time_to_send_ID_countdown--;

				if(g_on_the_air)
				{
					if(g_event_finish_time > 0)
					{
						time(&temp_time);

						if(temp_time >= g_event_finish_time)
						{
							if(g_event_enabled) g_last_status_code = STATUS_CODE_EVENT_FINISHED;
							g_on_the_air = 0;
							g_event_finish_time = EEPROM_FINISH_TIME_DEFAULT;
							keyTransmitter(OFF);
							g_event_enabled = FALSE;
							g_event_commenced = FALSE;
							g_check_for_next_event = TRUE;
						}
					}

					if(g_on_the_air > 0) /* on the air */
					{
						g_on_the_air--;

						if(!g_time_to_send_ID_countdown && g_time_needed_for_ID)
						{
							if(g_on_the_air == g_time_needed_for_ID) // wait until the end of a transmission
							{
								g_last_status_code = STATUS_CODE_SENDING_ID;
								g_time_to_send_ID_countdown = g_ID_seconds;
								g_code_throttle = throttleValue(g_id_codespeed);
								repeat = FALSE;
								makeMorse(g_messages_text[STATION_ID], &repeat, NULL); /* Send only once */
							}
						}


						if(!g_on_the_air)
						{
							if(g_off_air_seconds)
							{
								keyTransmitter(OFF);
								g_on_the_air -= g_off_air_seconds;
								repeat = TRUE;
								makeMorse(g_messages_text[PATTERN_TEXT], &repeat, NULL); /* Reset pattern to start */
								g_last_status_code = STATUS_CODE_EVENT_STARTED_WAITING_FOR_TIME_SLOT;

								/* Enable sleep during off-air periods */
								if((g_off_air_seconds > 15) && !g_WiFi_shutdown_seconds) // sleep if there is time for it
								{
									g_seconds_to_sleep = g_off_air_seconds - 10;
									g_go_to_sleep = TRUE;
								}
							}
							else
							{
								g_on_the_air = g_on_air_seconds;
								g_code_throttle = throttleValue(g_pattern_codespeed);
							}
						}
					}
					else if(g_on_the_air < 0) /* off the air - g_on_the_air = 0 means all transmissions are disabled */
					{
						g_on_the_air++;

						if(!g_on_the_air) // off-the-air time has expired
						{
							g_last_status_code = STATUS_CODE_EVENT_STARTED_NOW_TRANSMITTING;
							g_on_the_air = g_on_air_seconds;
							g_code_throttle = throttleValue(g_pattern_codespeed);
							BOOL repeat = TRUE;
							makeMorse(g_messages_text[PATTERN_TEXT], &repeat, NULL);
						}
					}
				}
			}
			else if(g_event_start_time > 0) /* off the air - waiting for the start time to arrive */
			{
				time(&temp_time);

				if(temp_time >= g_event_start_time)
				{
					if(g_intra_cycle_delay_time)
					{
						g_last_status_code = STATUS_CODE_EVENT_STARTED_WAITING_FOR_TIME_SLOT;
						g_on_the_air = -g_intra_cycle_delay_time;
					}
					else
					{
						g_last_status_code = STATUS_CODE_EVENT_STARTED_NOW_TRANSMITTING;
						g_on_the_air = g_on_air_seconds;
						g_code_throttle = throttleValue(g_pattern_codespeed);
						BOOL repeat = TRUE;
						makeMorse(g_messages_text[PATTERN_TEXT], &repeat, NULL);
					}

					g_time_to_send_ID_countdown = g_ID_seconds;
					g_event_commenced = TRUE;
				}
			}
		}

		/**************************************
		 * Delay before re-enabling linkbus receive
		 ***************************************/
		if(g_wifi_enable_delay)
		{
			g_wifi_enable_delay--;

			if(!g_wifi_enable_delay)
			{
				wifi_power(ON); // power on WiFi
				wifi_reset(OFF); // bring WiFi out of reset
			}
		}
		else
		{
			if(g_WiFi_shutdown_seconds)
			{
				g_WiFi_shutdown_seconds--;

				if(!g_WiFi_shutdown_seconds)
				{
					wifi_reset(ON); // put WiFi into reset
					wifi_power(OFF); // power off WiFi
					g_go_to_sleep = TRUE;

					if(g_event_start_time && !g_event_commenced)
					{
						time(&temp_time);
						uint32_t dif = timeDif(g_event_start_time, temp_time);

						if(dif < 30)
						{
							g_go_to_sleep = FALSE;
						}
						else
						{
							g_seconds_to_sleep = dif - 10;
						}
					}
					else if(g_event_enabled)
					{
						if(g_on_the_air > -20)
						{
							g_go_to_sleep = FALSE;
						}
						else
						{
							g_seconds_to_sleep = -(g_on_the_air + 10);
						}
					}
					else
					{
						g_seconds_to_sleep = MAX_UINT16;
					}
				}
				else
				{
					g_report_seconds = TRUE;
				}
			}
		}
	}
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
	static uint16_t codeInc = 0;
	static uint8_t amModulation = 0;
	BOOL repeat, finished;

	if(g_util_tick_countdown) g_util_tick_countdown--;
	if(g_2m_bias_delay) g_2m_bias_delay--;

	static BOOL key = FALSE;

	if(g_event_enabled && g_event_commenced)
	{
		if(g_on_the_air > 0)
		{
			if(codeInc)
			{
				codeInc--;

				if(!codeInc)
				{
					key = makeMorse(NULL, &repeat, &finished);

					if(!repeat && finished) // ID has completed, so resume pattern
					{
						g_code_throttle = throttleValue(g_pattern_codespeed);
						repeat = TRUE;
						makeMorse(g_messages_text[PATTERN_TEXT], &repeat, NULL);
						key = makeMorse(NULL, &repeat, &finished);
					}

					if(key) powerToTransmitter(ON);
				}
			}
			else
			{
				keyTransmitter(key);
				codeInc = g_code_throttle;
			}
		}
		else if(!g_on_the_air)
		{
			if(key)
			{
				key = OFF;
				keyTransmitter(OFF);
				powerToTransmitter(OFF);
				g_last_status_code = STATUS_CODE_EVENT_STARTED_WAITING_FOR_TIME_SLOT;
			}
		}
	}

	if(g_am_modulation_enabled)
	{
		amModulation = !amModulation;

		if(amModulation)
		{
			dac081c_set_dac(g_mod_up, AM_DAC);
		}
		else
		{
			dac081c_set_dac(g_mod_down, AM_DAC);
		}
	}


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

		if(indexConversionInProcess == BATTERY_READING)
		{
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
//		else if(indexConversionInProcess == PA_VOLTAGE_READING)
//		{
//			lastResult = holdConversionResult;
//			g_PA_voltage = holdConversionResult;
//		}
		else
		{
			lastResult = holdConversionResult;
		}

		g_lastConversionResult[indexConversionInProcess] = lastResult;

		conversionInProcess = FALSE;
	}
}/* ISR */


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
//	saveAllEEPROM();                /* Make sure changed values get saved */

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
		g_last_error_code = ERROR_CODE_WD_TIMEOUT;
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
		rx_char = toupper(rx_char);
		SMCR = 0x00; // exit power-down mode

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
			buff->id = LINKBUS_MSG_UNKNOWN;
			charIndex = LINKBUS_MAX_MSG_LENGTH;
			field_len = 0;
			msg_ID = LINKBUS_MSG_UNKNOWN;
			field_index = 0;
			buff = NULL;
		}

		if(++charIndex >= LINKBUS_MAX_MSG_LENGTH)
		{
			receiving_msg = FALSE;
			charIndex = 0;
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


#ifdef ENABLE_PIN_CHANGE_INTERRUPT_0
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
 * the External Interrupt Control Registers � EICRA (INT2:0). When the
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
}
#endif // ENABLE_PIN_CHANGE_INTERRUPT_0

#ifdef ENABLE_PIN_CHANGE_INTERRUPT_1
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
#endif // ENABLE_PIN_CHANGE_INTERRUPT_1

#ifdef ENABLE_PIN_CHANGE_INTERRUPT_2
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
	static uint8_t portHistory = 0xFF; /* default is high because the pull-up */
	uint8_t changedbits;

	if(!g_sufficient_power_detected)
	{
		return; /* ignore changes before power stabilizes */

	}

	changedbits = PIND ^ portHistory;
	portHistory = PIND;

	if(!(changedbits & ((1 << PORTD4) || (1 << PORTD5))))    /* noise? */
	{
		return;
	}

	g_antenna_connect_state = ANT_CONNECTION_UNDETERMINED;
	g_antenna_connection_changed = TRUE;
}
#endif // ENABLE_PIN_CHANGE_INTERRUPT_2


EC rtc_init(void)
{
	uint8_t tries = 10;
	EC code = ERROR_CODE_SW_LOGIC_ERROR;

	while(code && tries--)
	{
		time_t epoch_time = ds3231_get_epoch(&code);

		if(code == ERROR_CODE_NO_ERROR)
		{
			set_system_time(epoch_time);
			ds3231_1s_sqw(ON);
		}
	}

	return code;
}


EC hw_init(void)
{
	/**
	 * Initialize the transmitter */
	EC code = init_transmitter();

	return code;
}

void __attribute__((optimize("O1"))) set_ports(InitActionType initType)
{
	if(initType == POWER_UP)
	{
		SMCR = 0x00; // clear sleep bit
		PRR = 0x00; // enable all clocks

		/** Hardware rev P1.10
		 * Set up PortB  */
		// PB0 = VHF_ENABLE
		// PB1 = HF_ENABLE
		// PB2 = 3V3_PWR_ENABLE
		// PB3 = MOSI
		// PB4 = MISO
		// PB5 = SCK
		// PB6 = Tx Final Voltage Enable
		// PB7 = Main Power Enable

		DDRB |= (1 << PORTB0) | (1 << PORTB1) | (1 << PORTB2) | (1 << PORTB6) | (1 << PORTB7);
		PORTB |= (1 << PORTB2) | (1 << PORTB7); /* Turn on main power */

		/** Hardware rev P1.10
		 * Set up PortD */
		// PD0 = RXD
		// PD1 = TXD
		// PD2 = RTC interrupt
		// PD3 = Antenna Connect Interrupt
		// PD4 = 80M_ANTENNA_DETECT
		// PD5 = 2M_ANTENNA_DETECT
		// PD6 = WIFI_RESET
		// PD7 = WIFI_ENABLE

	//	DDRD  = 0b00000010;     /* Set PORTD pin data directions */
		DDRD  |= (1 << PORTD6) | (1 << PORTD7);     /* Set PORTD pin data directions */
		PORTD = (1 << PORTD2) | (1 << PORTD3) | (1 << PORTD4) | (1 << PORTD5);     /* Enable pull-ups on input pins, and set output levels on all outputs */

		/** Hardware rev P1.10
		 * Set up PortC */
		// PC0 = ADC - 80M_ANTENNA_DETECT
		// PC1 = ADC - 2M_ANTENNA_DETECT
		// PC2 = n/c
		// PC3 = n/c
		// PC4 = SDA
		// PC5 = SCL
		// PC6 = Reset
		// PC7 = N/A

		DDRC = 0x00;
		PORTC = I2C_PINS | (1 << PORTC2) | (1 << PORTC3);

		/**
		 * TIMER2 is for periodic interrupts */
		OCR2A = 0x0C;                                       /* set frequency to ~300 Hz (0x0c) */
		TCCR2A |= (1 << WGM01);                             /* set CTC with OCRA */
		TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20);  /* 1024 Prescaler - why are we setting CS21?? */
		TIMSK2 |= (1 << OCIE0B);                            /* enable compare interrupt */

		/**
		 * Set up ADC */
		ADMUX |= (1 << REFS0) | (1 << REFS1); // Use internal 1.1V reference
		ADCSRA |= (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0) | (1 << ADEN);

		/**
		 * Set up pin interrupts */
		/* Enable pin change interrupts PCINT8 - 80m, PCINT9 - 2m, */
		// TODO

	//	PCICR |= (1 << PCIE2) | (1 << PCIE1) | (1 << PCIE0);  /* Enable pin change interrupts PCI2, PCI1 and PCI0 */
	//	PCMSK2 |= 0b10001000;                                   /* Enable port D pin change interrupts */
	//	PCMSK1 |= (1 << PCINT10);                               /* Enable port C pin change interrupts on pin PC2 */
	//	PCMSK0 |= (1 << PORTB2);                                /* Do not enable interrupts until HW is ready */

//		EICRA  |= ((1 << ISC01) | (1 << ISC00));	/* Configure INT0 rising edge for RTC 1-second interrupts */
		EICRA  |= ((1 << ISC01) | (1 << ISC10));	/* Configure INT0 falling edge for RTC 1-second interrupts, and INT1 any logic change */
		EIMSK |= ((1 << INT0) | (1 << INT1));

		i2c_init(); // initialize i2c bus
	}
	else
	{
		/** Hardware rev P1.10
		 * Set up PortB  */
		// PB0 = VHF_ENABLE
		// PB1 = HF_ENABLE
		// PB2 = Testpoint W306
		// PB3 = MOSI
		// PB4 = MISO
		// PB5 = SCK
		// PB6 = Tx Final Voltage Enable
		// PB7 = Main Power Enable

		DDRB = 0x00;     /* Set PORTD pin data directions */
		PORTB = 0x00;

		/** Hardware rev P1.10
		* Set up PortD */
		// PD0 = RXD
		// PD1 = TXD
		// PD2 = RTC interrupt
		// PD3 = Antenna Connect Interrupt
		// PD4 = 80M_ANTENNA_DETECT
		// PD5 = 2M_ANTENNA_DETECT
		// PD6 = WIFI_RESET
		// PD7 = WIFI_ENABLE

		DDRD = 0x00;
		PORTD = ((1 << PORTD2) | (1 << PORTD3) | (1 << PORTD4) | (1 << PORTD5)); /* Allow RTC and antenna-connect interrupts to continue */

		/** Hardware rev P1.10
		 * Set up PortC */
		// PC0 = ADC - 80M_ANTENNA_DETECT
		// PC1 = ADC - 2M_ANTENNA_DETECT
		// PC2 = n/c
		// PC3 = n/c
		// PC4 = SDA
		// PC5 = SCL
		// PC6 = Reset
		// PC7 = N/A

		DDRC = 0x00;
		PORTC = (1 << PORTC0) | (1 << PORTC1) | (1 << PORTC2) | (1 << PORTC3);

		/**
		 * TIMER2 is for periodic interrupts */
		TIMSK2 &= ~(1 << OCIE0B);                            /* disable compare interrupt */
		OCR2A = 0x00;                                       /* set frequency to ~300 Hz (0x0c) */
		TCCR2A &= ~(1 << WGM01);                             /* set CTC with OCRA */
		TCCR2B &= ~((1 << CS22) | (1 << CS21) | (1 << CS20));  /* Prescalar */

		/**
		 * Set up ADC */
		ADMUX &= ~((1 << REFS0) | (1 << REFS1));
		ADCSRA = 0;

		DIDR0 = 0x3f; // disable ADC pins
		DIDR1 = 0x03; // disable analog inputs

		/**
		* Set up pin interrupts */
		/* Enable pin change interrupts PCINT8, PCINT9, */
		// TODO

		PCICR = 0;
		PCMSK0 = 0;
		PCMSK1 = 0;
		PCMSK2 = 0;

		EICRA  |= ((1 << ISC01) | (1 << ISC11));	/* Configure INT0 and INT1 falling edge for RTC 1-second interrupts */
		EIMSK |= ((1 << INT0) | (1 << INT1));

		/* Configure INT1 for antenna connect interrupts */
		// TODO

		/**
		Turn off UART
		*/
//		linkbus_disable();

		/**
		Disable Watchdog timer
		*/
		wdt_init(WD_DISABLE);

		g_sleeping = TRUE;

		/* Disable brown-out detection
		**/
		PRR = 0xff;
		cli();
		SMCR = 0x05; // set power-down mode
		MCUCR = (1 << BODS) | (1 << BODSE);  // turn on brown-out enable select
		MCUCR = (1 << BODS);        // this must be done within 4 clock cycles of above
		sei();
		asm("sleep"); /* enter power-down mode */
	}
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
	static EC code = ERROR_CODE_SW_LOGIC_ERROR;
	uint8_t tries = 10;
	BOOL init_hardware = FALSE;
	static uint8_t hw_tries = 10; // give up after too many failures

	/**
	 * Initialize vars stored in EEPROM */
	initializeEEPROMVars(FALSE);
	g_event_enabled = FALSE; // ensure the event is disabled until hardware is initialized

	/**
	 * Initialize port pins and timers */
	set_ports(POWER_UP);

	cpu_irq_enable();                                           /* same as sei(); */

	/**
	 * Enable watchdog interrupts before performing I2C calls that might cause a lockup */
#ifndef TRANQUILIZE_WATCHDOG
	wdt_init(WD_SW_RESETS);
	wdt_reset();                                    /* HW watchdog */
#endif // TRANQUILIZE_WATCHDOG

	g_antenna_connect_state = antennaIsConnected() ? ANT_CONNECTION_UNDETERMINED : ANT_ALL_DISCONNECTED;

	while(code && tries)
	{
		if(tries) tries--;
		code = rtc_init();
	}

	g_last_error_code = code;

	linkbus_init(BAUD);
	g_wifi_enable_delay = 5;

	wdt_reset();                                    /* HW watchdog */

	g_util_tick_countdown = 20;
	while(linkbusTxInProgress() && g_util_tick_countdown)
	{
		;                                           /* wait until transmit finishes */
	}

#ifndef TRANQUILIZE_WATCHDOG
	wdt_init(WD_HW_RESETS); /* enable hardware interrupts */
#endif // TRANQUILIZE_WATCHDOG

	while(1)
	{
		/**************************************
		* The watchdog must be petted periodically to keep it from barking
		**************************************/
		cli(); wdt_reset(); /* HW watchdog */ sei();

		/***************************************
		* Check for Power
		***************************************/
		if(!g_sufficient_power_detected)                                                                           /* if ADC battery measurements have stabilized */
		{
			if(g_battery_measurements_active)
			{
				if(g_lastConversionResult[BATTERY_READING] > POWER_ON_VOLT_THRESH_MV)  /* Battery measurement indicates sufficient voltage */
				{
					g_sufficient_power_detected = TRUE;
					init_hardware = TRUE;
				}
				else if(!g_wifi_enable_delay) // no battery detected by the time WiFi  is turned on, assume an external battery is being used
				{
					if(g_battery_type == BATTERY_UNKNOWN)
					{
						g_battery_type = BATTERY_EXTERNAL;
						g_sufficient_power_detected = TRUE;
						init_hardware = TRUE;
					}
				}
			}
		}

		if(init_hardware)
		{
			code = ERROR_CODE_NO_ERROR;

			if(hw_tries)
			{
				hw_tries--;
				code = hw_init(); // initialize transmitter and related I2C devices
				init_hardware = (code != ERROR_CODE_NO_ERROR);

				if(!init_hardware) // hardware was successfully initialized
				{
					SC status = STATUS_CODE_IDLE;
					EC ec = ERROR_CODE_SW_LOGIC_ERROR;
					tries = 10;

					while(ec && tries)
					{
						if(tries) tries--;
						ec = rtc_init();
					}

					if(!ec)
					{
						ec = activateEventUsingCurrentSettings(&status);

						if(!ec)
						{
							if(status)
							{
								g_last_status_code = status;
							}

							g_event_enabled = eventEnabled();
						}
						else
						{
							g_last_error_code = ec;
						}
					}
				}
			}

			if(code) // avoid unnecessarily clearing any pre-existing error code
			{
				/*  If the  hardware fails to initialize, report the failure to
				    the user over WiFi by sending an appropriate error code. */
				g_last_error_code = code;

			}
		}

		/********************************
		* Handle sleep
		******************************/
		if(g_go_to_sleep)
		{
			if(txSleeping(TRUE))
			{
				init_hardware = FALSE; // ensure failing attempts are canceled
				g_sufficient_power_detected = FALSE; // init hardware on return from sleep
				g_seconds_left_to_sleep = g_seconds_to_sleep;
				linkbus_disable();

				while(g_go_to_sleep) set_ports(POWER_SLEEP); // Sleep occurs here

				set_ports(POWER_UP);
				linkbus_enable();
				wdt_init(WD_HW_RESETS); /* enable hardware interrupts */
				wdt_reset();                    /* HW watchdog */
				g_i2c_not_timed_out = FALSE;    /* unstick I2C */
				if(g_event_enabled)
				{
					hw_tries = 10; // give up after too many failures
					init_hardware = TRUE;
				}
				else
				{
					g_wifi_enable_delay = 2;
					g_WiFi_shutdown_seconds = 120;
					g_last_status_code = STATUS_CODE_RETURNED_FROM_SLEEP;
					g_check_for_next_event = TRUE;
				}
			}
		}

		if(g_report_seconds)
		{
			g_report_seconds = FALSE;
			sprintf(g_tempStr, "%lu", time(NULL));
			lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);
		}

		if(g_last_error_code)
		{
			sprintf(g_tempStr, "%u", g_last_error_code);
			lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_ERRORCODE_LABEL, g_tempStr);
			g_last_error_code = ERROR_CODE_NO_ERROR;
		}

		if(g_last_status_code)
		{
			sprintf(g_tempStr, "%u", g_last_status_code);
			lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_STATUSCODE_LABEL, g_tempStr);
			g_last_status_code = STATUS_CODE_IDLE;
		}

		if(g_antenna_connection_changed)
		{
			static AntConnType lastAntennaConnectState = ANT_CONNECTION_UNDETERMINED;
			static uint8_t confirmations = 5;

			if(g_antenna_connect_state == ANT_CONNECTION_UNDETERMINED)
			{
				if(!g_util_tick_countdown)
				{
					AntConnType connection = ANT_CONNECTION_UNDETERMINED;

					if(g_lastConversionResult[BAND_80M_ANTENNA] < ANTENNA_DETECT_THRESH)
					{
						connection = ANT_80M_CONNECTED;
					}

					if(g_lastConversionResult[BAND_2M_ANTENNA] < ANTENNA_DETECT_THRESH)
					{
						if(connection == ANT_80M_CONNECTED)
						{
							connection = ANT_2M_AND_80M_CONNECTED;
						}
						else
						{
							connection = ANT_2M_CONNECTED;
						}
					}

					if(lastAntennaConnectState == connection)
					{
						if(confirmations)
						{
							confirmations--;
							g_util_tick_countdown = ANTENNA_DETECT_DEBOUNCE;
						}
						else
						{
							g_antenna_connect_state = connection;
							g_antenna_connection_changed = FALSE;

							if(connection == ANT_80M_CONNECTED)
							{
								//txSetBand(BAND_80M, OFF);
								g_last_status_code = STATUS_CODE_80M_ANT_ATTACHED;

								/* TODO: re-enable transmitter power and state if it is currently operating */
							}
							else if(connection == ANT_2M_CONNECTED)
							{
								//txSetBand(BAND_2M, OFF);
								g_last_status_code = STATUS_CODE_2M_ANT_ATTACHED;

								/* TODO: re-enable transmitter power and state if it is currently operating */
							}
						}
					}
					else
					{
						confirmations = 5;
					}

					lastAntennaConnectState = connection;
				}
			}
			else if(g_antenna_connect_state == ANT_ALL_DISCONNECTED)
			{
				powerToTransmitter(OFF);
				g_antenna_connection_changed = FALSE;
				lastAntennaConnectState = ANT_ALL_DISCONNECTED;
				g_last_status_code = STATUS_CODE_NO_ANT_ATTACHED;
			}
			else /* logic error - this should not occur */
			{
				powerToTransmitter(OFF);
				g_antenna_connection_changed = FALSE;
			}
		}

		/* Perform tasks specified by the transmitter */
		if(g_txTask)
		{
			if(!g_2m_bias_delay)
			{
				EC ec;
				g_2m_bias_delay = 16; /* Adjust so that the state machine takes about 100 ms to step through all states */
				ec = (*g_txTask)(NULL);
				if(ec) g_last_error_code = ec;
			}
		}

		if(g_check_for_next_event)
		{
			/* Power up the WiFi module if it isn't already powered up */
			if(!wifi_enabled())
			{

			}

			/* If WiFi is powered up, send it a request for the next active event */

			g_check_for_next_event = FALSE;
		}

		/***********************************************************************
		 *  Handle arriving Linkbus messages
		 ************************************************************************/
		handleLinkBusMsgs();
	}       /* while(1) */
}/* main */

// The compiler does not seem to optimize large switch statements correctly
void  __attribute__((optimize("O0"))) handleLinkBusMsgs()
{
	LinkbusRxBuffer* lb_buff;
	static uint8_t event_parameter_count = 0;
	BOOL send_ack = TRUE;

	while((lb_buff = nextFullRxBuffer()))
	{
		LBMessageID msg_id = lb_buff->id;

		switch(msg_id)
		{
			case MESSAGE_WIFI:
			{
				BOOL result = wifi_enabled();

				if(lb_buff->fields[FIELD1][0])
				{
					result = atoi(lb_buff->fields[FIELD1]);

					cli();
					g_on_the_air = 0; //  stop transmitting
					g_event_commenced = FALSE; // get things stopped immediately
					g_event_enabled = FALSE; // get things stopped immediately
					linkbus_disable();
					g_WiFi_shutdown_seconds = 0; // disable shutdown
					sei();
					keyTransmitter(OFF);
					powerToTransmitter(OFF);

					if(result == 0) // shut off power to WiFi
					{
						PORTD &= ~((1 << PORTD6) | (1 << PORTD7));
					}
				}
			}
			break;

			case MESSAGE_RESET:
			{
#ifndef TRANQUILIZE_WATCHDOG
				wdt_init(WD_FORCE_RESET);
				while(1);
#endif // TRANQUILIZE_WATCHDOG
			}
			break;

			case MESSAGE_ESP_COMM:
			{
				uint8_t f1 = lb_buff->fields[FIELD1][0];

				if(f1 == '0') /* I'm awake message */
				{
					SC status = STATUS_CODE_IDLE;
					EC ec;

					/* WiFi is awake. Send it the current time */
					sprintf(g_tempStr, "%lu", time(NULL));
					lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);

					ec = activateEventUsingCurrentSettings(&status);

					if(ec || (status == STATUS_CODE_EVENT_FINISHED))
					{
						g_event_enabled = FALSE;
						saveAllEEPROM();
					}
					else
					{
						g_event_enabled = eventEnabled();
					}

					if(!g_event_enabled) // no enabled event is stored in EEPROM
					{
						// Ask the ESP for the next scheduled event
						sprintf(g_tempStr, "%lu", time(NULL));
						lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_ESP_LABEL, "1");
					}
				}
				else if(f1 == '1')
				{
					/* ESP8266 is ready with event data */
					// Prepare to receive new event configuration settings
					g_event_enabled = FALSE;
					initializeAllEventSettings(TRUE);
				}
				else if(f1 == '2') /* ESP module needs continuous power to save data */
				{
					g_WiFi_shutdown_seconds = 0; // disable shutdown
					lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_ESP_LABEL, "2"); /* Save data now */
				}
				else if(f1 == '3')
				{
					g_WiFi_shutdown_seconds = 3; /* Shut down WiFi in 3 seconds */
				}
				else if(f1 == 'Z') /* WiFi connected to browzer - keep alive */
				{
					/* shut down WiFi after 2 minutes of inactivity */
					g_WiFi_shutdown_seconds = 120; // wait 2 more minutes before shutting down WiFi
				}
			}
			break;

			case MESSAGE_TX_MOD:
			{
				if(lb_buff->fields[FIELD1][0] == 'A') // AM
				{
					BOOL setAMmodulation = TRUE;
					txSetParameters(NULL, NULL, &setAMmodulation, NULL);
					saveAllEEPROM();
					event_parameter_count++;
				}
				else if(lb_buff->fields[FIELD1][0] == 'C') // CW
				{
					BOOL setAMmodulation = FALSE;
					txSetParameters(NULL, NULL, &setAMmodulation, NULL);
					saveAllEEPROM();
					event_parameter_count++;
				}
			}
			break;

			case MESSAGE_TX_POWER:
			{
				static uint16_t pwr_mW;

				if(lb_buff->fields[FIELD1][0])
				{
					EC ec;

					if((lb_buff->fields[FIELD1][0] == 'M') && (lb_buff->fields[FIELD2][0]))
					{
						pwr_mW = (uint16_t)atoi(lb_buff->fields[FIELD2]);
						event_parameter_count++;
					}
					else
					{
						pwr_mW = (uint16_t)atoi(lb_buff->fields[FIELD1]);
					}

					ec = txSetParameters(&pwr_mW, NULL, NULL, NULL);
					if(ec) g_last_error_code = ec;

					//saveAllEEPROM();
					storeTransmitterValues();

					sprintf(g_tempStr, "M,%u", pwr_mW);
					lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TX_POWER_LABEL, g_tempStr);
				}
			}
			break;

			case MESSAGE_PERM:
			{
				storeTransmitterValues();
				saveAllEEPROM();
			}
			break;

			case MESSAGE_GO:
			{
				char f1 = lb_buff->fields[FIELD1][0];

				if((f1 == '1') || (f1 == '2'))
				{
					if(!txIsAntennaForBand())
					{
						g_last_error_code = ERROR_CODE_NO_ANTENNA_FOR_BAND;
					}
					else
					{
						if(f1 == '1') // Xmit immediately using current settings
						{
							if(txIsAntennaForBand())
							{
								/* Set the Morse code pattern and speed */
								cli();
								BOOL repeat = TRUE;
								makeMorse(g_messages_text[PATTERN_TEXT], &repeat, NULL);
								g_code_throttle = throttleValue(g_pattern_codespeed);
								sei();
								g_event_start_time = 1; // have it start a long time ago
								g_event_finish_time = MAX_TIME; // run for a long long time
								g_on_air_seconds = 9999; // on period is very long
								g_off_air_seconds = 0; // off period is very short
								g_on_the_air = 9999; //  start out transmitting
								g_time_to_send_ID_countdown = MAX_UINT16; // wait a long time to send the ID
								g_event_commenced = TRUE; // get things running immediately
								g_event_enabled = TRUE; // get things running immediately
								g_last_status_code = STATUS_CODE_EVENT_STARTED_NOW_TRANSMITTING;
							}
							else
							{
								g_last_error_code = ERROR_CODE_NO_ANTENNA_FOR_BAND;
							}
						}
						else if (f1 == '2') // enables a downloaded event stored in EEPROM
						{
							/* This command configures the transmitter to launch an event at its scheduled start time */
							SC status = STATUS_CODE_IDLE;
							EC ec;

							if(event_parameter_count < NUMBER_OF_ESSENTIAL_EVENT_PARAMETERS)
							{
								g_last_error_code = ERROR_CODE_EVENT_NOT_CONFIGURED;
							}
							else
							{
								g_event_enabled = FALSE;

								ec = activateEventUsingCurrentSettings(&status);

								if(!ec)
								{
									if(status)
									{
										g_last_status_code = status;
									}

									g_event_enabled = eventEnabled();
									saveAllEEPROM();            /* Make sure all  event values get saved */
									storeTransmitterValues();
								}
								else
								{
									g_last_error_code = ec;
								}
							}
						}
					}
				}
				else if(f1 == '0') // Stop continuous transmit (if enabled) and prepare to receive new event data
				{
					cli();
					g_on_the_air = 0; //  stop transmitting
					g_event_commenced = FALSE; // get things stopped immediately
					g_event_enabled = FALSE; // get things stopped immediately
					sei();
					keyTransmitter(OFF);
					powerToTransmitter(OFF);

					// Restore saved event settings
					event_parameter_count = 0;
					g_last_status_code = STATUS_CODE_RECEIVING_EVENT_DATA;
				}
			}
			break;

			case MESSAGE_TIME:
			{
				time_t mtime = 0;

				if(lb_buff->fields[FIELD1][0] == 'S')
				{
					if(lb_buff->fields[FIELD2][0])
					{
						mtime = atol(lb_buff->fields[FIELD2]);
					}

					if(mtime)
					{
						g_event_start_time = mtime;
						cli();
						set_system_time(ds3231_get_epoch(NULL)); // update system clock
						sei();
						event_parameter_count++;
					}
				}
				else if(lb_buff->fields[FIELD1][0] == 'F')
				{
					if(lb_buff->fields[FIELD2][0])
					{
						mtime = atol(lb_buff->fields[FIELD2]);
					}

					if(mtime)
					{
						g_event_finish_time = mtime;
						event_parameter_count++;
					}
				}

				if(mtime)
				{
					saveAllEEPROM();
				}
			}
			break;

			case MESSAGE_CLOCK:
			{
				if(lb_buff->type == LINKBUS_MSG_COMMAND) // ignore replies since, as the time source, we should never be sending queries anyway
				{
					if(lb_buff->fields[FIELD1][0])
					{
						strncpy(g_tempStr, lb_buff->fields[FIELD1], 20);
						ds3231_set_date_time(g_tempStr, RTC_CLOCK);
						set_system_time(ds3231_get_epoch(NULL)); // update system clock
					}
					else
					{
						sprintf(g_tempStr, "%lu", time(NULL));
						lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);
					}
				}
				else if(lb_buff->type == LINKBUS_MSG_QUERY)
				{
					static uint32_t lastTime = 0;

					uint32_t temp_time = ds3231_get_epoch(NULL);
					set_system_time(temp_time);

					if(temp_time != lastTime)
					{
						sprintf(g_tempStr, "%lu", temp_time);
						lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);
						lastTime = temp_time;
					}
				}
			}
			break;

			case MESSAGE_SET_STATION_ID:
			{
				if(lb_buff->fields[FIELD1][0])
				{
					strncpy(g_messages_text[STATION_ID], lb_buff->fields[FIELD1], MAX_PATTERN_TEXT_LENGTH);
					saveAllEEPROM();
					event_parameter_count++;

					if(g_messages_text[STATION_ID][0])
					{
						g_time_needed_for_ID = (500 + timeRequiredToSendStrAtWPM(g_messages_text[STATION_ID], g_id_codespeed)) / 1000;
					}
				}
			}
			break;

			case MESSAGE_CODE_SPEED:
			{
				uint8_t speed = g_pattern_codespeed;

				if(lb_buff->fields[FIELD1][0] == 'I')
				{
					if(lb_buff->fields[FIELD2][0])
					{
						speed = atol(lb_buff->fields[FIELD2]);
						g_id_codespeed = CLAMP(5, speed, 20);
						saveAllEEPROM();
						event_parameter_count++;

						if(g_messages_text[STATION_ID][0])
						{
							g_time_needed_for_ID = (500 + timeRequiredToSendStrAtWPM(g_messages_text[STATION_ID], g_id_codespeed)) / 1000;
						}
					}
				}
				else if(lb_buff->fields[FIELD1][0] == 'P')
				{
					if(lb_buff->fields[FIELD2][0])
					{
						speed = atol(lb_buff->fields[FIELD2]);
						g_pattern_codespeed = CLAMP(5, speed, 20);
						saveAllEEPROM();
						event_parameter_count++;
						g_code_throttle = (7042 / g_pattern_codespeed) / 10;
					}
				}
			}
			break;

			case MESSAGE_TIME_INTERVAL:
			{
				uint16_t time = 0;

				if(lb_buff->fields[FIELD1][0] == '0')
				{
					if(lb_buff->fields[FIELD2][0])
					{
						time = atol(lb_buff->fields[FIELD2]);
						g_off_air_seconds = time;
						saveAllEEPROM();
						event_parameter_count++;
					}
				}
				else if(lb_buff->fields[FIELD1][0] == '1')
				{
					if(lb_buff->fields[FIELD2][0])
					{
						time = atol(lb_buff->fields[FIELD2]);
						g_on_air_seconds = time;
						saveAllEEPROM();
						event_parameter_count++;
					}
				}
				else if(lb_buff->fields[FIELD1][0] == 'I')
				{
					if(lb_buff->fields[FIELD2][0])
					{
						time = atol(lb_buff->fields[FIELD2]);
						g_ID_time = time;
						saveAllEEPROM();
						event_parameter_count++;
					}
				}
				else if(lb_buff->fields[FIELD1][0] == 'D')
				{
					if(lb_buff->fields[FIELD2][0])
					{
						time = atol(lb_buff->fields[FIELD2]);
						g_intra_cycle_delay_time = time;
						saveAllEEPROM();
						event_parameter_count++;
					}
				}
			}
			break;

			case MESSAGE_SET_PATTERN:
			{
				if(lb_buff->fields[FIELD1][0])
				{
					strncpy(g_messages_text[PATTERN_TEXT], lb_buff->fields[FIELD1], MAX_PATTERN_TEXT_LENGTH);
					saveAllEEPROM();
					event_parameter_count++;
				}
			}
			break;

			case MESSAGE_SET_FREQ:
			{
				Frequency_Hz transmitter_freq = 0;

				if(lb_buff->fields[FIELD1][0])
				{
					static Frequency_Hz f;
					f = atol(lb_buff->fields[FIELD1]);

					Frequency_Hz ff = f;
					if(txSetFrequency(&ff, TRUE))
					{
						transmitter_freq = ff;
						event_parameter_count++;
						storeTransmitterValues();
					}
				}
				else
				{
					transmitter_freq = txGetFrequency();
				}

				if(transmitter_freq)
				{
					sprintf(g_tempStr, "%ld,", transmitter_freq);
					lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_SET_FREQ_LABEL, g_tempStr);
				}
			}
			break;

			case MESSAGE_BAND:
			{
				RadioBand band;

				if(lb_buff->fields[FIELD1][0])  /* band field */
				{
					EC ec = ERROR_CODE_ILLEGAL_COMMAND_RCVD;
					int b = atoi(lb_buff->fields[FIELD1]);

					if(b == 80)
					{
						RadioBand b = BAND_80M;
						BOOL en = TRUE;
						ec = txSetParameters(NULL, &b, NULL, &en);
						event_parameter_count++;
					}
					else if(b == 2)
					{
						RadioBand b = BAND_2M;
						BOOL en = TRUE;
						ec = txSetParameters(NULL, &b, NULL, &en);
						event_parameter_count++;
					}

					if(ec) g_last_error_code = ec;
				}

				band = txGetBand();

				if(lb_buff->type == LINKBUS_MSG_QUERY)  /* Query */
				{
					/* Send a reply */
					sprintf(g_tempStr, "%i", band);
					lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_BAND_LABEL, g_tempStr);
				}
			}
			break;

			case MESSAGE_BAT:
			{
				int32_t bat = BATTERY_PERCENTAGE(g_lastConversionResult[BATTERY_READING]);
				bat = CLAMP(0, bat, 100);
				lb_broadcast_num(bat, "!BAT");

				/* The system clock gets re-initialized whenever a battery message is received. This is
					    just to ensure the two stay closely in sync while the user interface is active */
				set_system_time(ds3231_get_epoch(NULL)); // update system clock
			}
			break;

			case MESSAGE_TEMP:
			{
				int16_t v;
				if(!ds3231_get_temp(&v)) lb_broadcast_num(v, "!TEM");
			}
			break;

			default:
			{
				linkbus_reset_rx(); /* flush buffer */
				g_last_error_code = ERROR_CODE_ILLEGAL_COMMAND_RCVD;
			}
			break;
		}

		lb_buff->id = MESSAGE_EMPTY;
		if(send_ack) linkbus_send_text(MESSAGE_ACK);
	}
}

BOOL eventEnabled(void)
{
	BOOL result = FALSE;
	time_t now = time(NULL);
	int32_t dif = timeDif(now, g_event_start_time);

	if(dif >= -60 ) // if now is within 60 seconds of the start time or later
	{
		dif = timeDif(g_event_finish_time, g_event_start_time);

		if(dif > 0) // if finish occurs after the start then the event runs for a limited amount of time
		{
			dif = timeDif(g_event_finish_time, now);

			// if finish is in the future then the event is still enabled
			if(dif > 0) result = TRUE;
		}
		else // if finish occurs before start then the event runs forever
		{
			result = TRUE;
		}
	}

	return result;
}

EC activateEventUsingCurrentSettings(SC* statusCode)
{
	// Make sure everything has been sanely initialized
	if(!g_event_start_time) return ERROR_CODE_EVENT_MISSING_START_TIME;
	if(!g_on_air_seconds) return ERROR_CODE_EVENT_MISSING_TRANSMIT_DURATION;
	if(g_intra_cycle_delay_time > (g_off_air_seconds + g_on_air_seconds)) return ERROR_CODE_EVENT_TIMING_ERROR;
	if(g_messages_text[PATTERN_TEXT][0] == '\0') return ERROR_CODE_EVENT_PATTERN_NOT_SPECIFIED;
	if(!g_pattern_codespeed) return ERROR_CODE_EVENT_PATTERN_CODE_SPEED_NOT_SPECIFIED;
	if(g_messages_text[STATION_ID][0] != '\0')
	{
		if((!g_id_codespeed || !g_ID_time)) return ERROR_CODE_EVENT_STATION_ID_ERROR;

		g_time_needed_for_ID = (500 + timeRequiredToSendStrAtWPM(g_messages_text[STATION_ID], g_id_codespeed)) / 1000;
		g_ID_seconds = g_ID_time;
		g_time_to_send_ID_countdown = g_ID_seconds; // countdown will not commence until event commences
	}
	else
	{
		g_time_needed_for_ID = 0; // ID will never be sent
	}

	if(g_event_start_time > g_event_finish_time) // the event never ends
	{
		g_event_finish_time = MAX_TIME;
	}

	time_t now = time(NULL);
	if(g_event_finish_time < now) // the event has already finished
	{
		if(statusCode) *statusCode = STATUS_CODE_EVENT_FINISHED;
	}
	else
	{
		int32_t dif = timeDif(now, g_event_start_time); // returns arg1 - arg2

		if(dif >= 0) // start time is in the past
		{
			BOOL turnOnTransmitter = FALSE;
			int cyclePeriod = g_on_air_seconds + g_off_air_seconds;
			int secondsIntoCycle = dif % cyclePeriod;
			int timeTillTransmit = g_intra_cycle_delay_time - secondsIntoCycle;

			if(timeTillTransmit <= 0) // we should have started transmitting already
			{
				if(g_on_air_seconds <= -timeTillTransmit) // we should have finished transmitting in this cycle
				{
					g_on_the_air = -(cyclePeriod + timeTillTransmit);
					if(statusCode) *statusCode = STATUS_CODE_EVENT_STARTED_WAITING_FOR_TIME_SLOT;
				}
				else // we should be transmitting right now
				{
					g_on_the_air = g_on_air_seconds + timeTillTransmit;
					turnOnTransmitter = TRUE;
					if(statusCode) *statusCode = STATUS_CODE_EVENT_STARTED_NOW_TRANSMITTING;
				}
			}
			else // not yet time time to transmit in this cycle
			{
				g_on_the_air = -timeTillTransmit;
				if(statusCode) *statusCode = STATUS_CODE_EVENT_STARTED_WAITING_FOR_TIME_SLOT;
			}

			if(turnOnTransmitter)
			{
				cli();
				BOOL repeat = TRUE;
				makeMorse(g_messages_text[PATTERN_TEXT], &repeat, NULL);
				g_code_throttle = throttleValue(g_pattern_codespeed);
				sei();
			}
			else
			{
				keyTransmitter(OFF);
			}

			g_event_commenced = TRUE;
		}
		else // start time is in the future
		{
			if(statusCode) *statusCode = STATUS_CODE_WAITING_FOR_EVENT_START;
			keyTransmitter(OFF);
		}
	}

	return ERROR_CODE_NO_ERROR;
}

/**********************
**********************/

void initializeEEPROMVars(BOOL skipEventEnabled)
{
	uint8_t i;

	if(eeprom_read_byte(&ee_interface_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
	{
		g_event_start_time = eeprom_read_dword((uint32_t*)(&ee_start_time));
		g_event_finish_time = eeprom_read_dword((uint32_t*)(&ee_finish_time));

//		if(!skipEventEnabled) g_event_enabled = eeprom_read_byte(&ee_event_enabled);
		g_pattern_codespeed = eeprom_read_byte(&ee_pattern_codespeed);
		g_id_codespeed = eeprom_read_byte(&ee_id_codespeed);
		g_on_air_seconds = eeprom_read_word(&ee_on_air_time);
		g_off_air_seconds = eeprom_read_word(&ee_off_air_time);
		g_intra_cycle_delay_time = eeprom_read_word(&ee_intra_cycle_delay_time);
		g_ID_time = eeprom_read_word(&ee_ID_time);

		for(i=0; i<20; i++)
		{
			g_messages_text[STATION_ID][i] = (char)eeprom_read_byte((uint8_t*)(&ee_stationID_text[i]));
			if(!g_messages_text[STATION_ID][i]) break;
		}

		for(i=0; i<20; i++)
		{
			g_messages_text[PATTERN_TEXT][i] = (char)eeprom_read_byte((uint8_t*)(&ee_pattern_text[i]));
			if(!g_messages_text[PATTERN_TEXT][i]) break;
		}
	}
	else
	{
		g_event_start_time = EEPROM_START_TIME_DEFAULT;
		g_event_finish_time = EEPROM_FINISH_TIME_DEFAULT;

//		if(!skipEventEnabled) g_event_enabled = EEPROM_EVENT_ENABLED_DEFAULT;

		g_id_codespeed = EEPROM_ID_CODE_SPEED_DEFAULT;
		g_pattern_codespeed = EEPROM_PATTERN_CODE_SPEED_DEFAULT;
		g_on_air_seconds = EEPROM_ON_AIR_TIME_DEFAULT;
		g_off_air_seconds = EEPROM_OFF_AIR_TIME_DEFAULT;
		g_intra_cycle_delay_time = EEPROM_INTRA_CYCLE_DELAY_TIME_DEFAULT;
		g_ID_time = EEPROM_ID_TIME_INTERVAL_DEFAULT;

		strncpy(g_messages_text[STATION_ID], EEPROM_STATION_ID_DEFAULT, MAX_PATTERN_TEXT_LENGTH);
		strncpy(g_messages_text[PATTERN_TEXT], EEPROM_PATTERN_TEXT_DEFAULT, MAX_PATTERN_TEXT_LENGTH);

		saveAllEEPROM();
		eeprom_write_byte(&ee_interface_eeprom_initialization_flag, EEPROM_INITIALIZED_FLAG);
		wdt_reset();                                    /* HW watchdog */
	}
}

void saveAllEEPROM()
{
	int i;
	wdt_reset();                                    /* HW watchdog */

	eeprom_update_dword((uint32_t*)&ee_start_time, g_event_start_time);
	eeprom_update_dword((uint32_t*)&ee_finish_time, g_event_finish_time);

//	eeprom_update_byte(&ee_event_enabled, g_event_enabled);
	eeprom_update_byte(&ee_id_codespeed, g_id_codespeed);
	eeprom_update_byte(&ee_pattern_codespeed, g_pattern_codespeed);
	eeprom_update_word(&ee_on_air_time, g_on_air_seconds);
	eeprom_update_word(&ee_off_air_time, g_off_air_seconds);
	eeprom_update_word(&ee_intra_cycle_delay_time, g_intra_cycle_delay_time);
	eeprom_update_word(&ee_ID_time, g_ID_time);

	for(i=0; i<strlen(g_messages_text[STATION_ID]); i++)
	{
		eeprom_update_byte((uint8_t*)&ee_stationID_text[i], (uint8_t)g_messages_text[STATION_ID][i]);
	}

	eeprom_update_byte((uint8_t*)&ee_stationID_text[i], 0);

	for(i=0; i<strlen(g_messages_text[PATTERN_TEXT]); i++)
	{
		eeprom_update_byte((uint8_t*)&ee_pattern_text[i], (uint8_t)g_messages_text[PATTERN_TEXT][i]);
	}

	eeprom_update_byte((uint8_t*)&ee_pattern_text[i], 0);
}

uint16_t throttleValue(uint8_t speed)
{
	uint16_t temp;
	speed = CLAMP(5, speed, 20);
	temp = (7042L / (uint16_t)speed) / 10L;
	return temp;
}

BOOL antennaIsConnected(void)
{
	return !(PIND & (1 << PORTD3));
//	return !(PIND & (1 << PORTD4) || (1 << PORTD5));
}

void initializeAllEventSettings(BOOL disableEvent)
{
	if(disableEvent)
	{
		cli();
		g_event_enabled = FALSE;  // enabled by event time settings - not stored
		g_event_commenced = FALSE; // commences when starttime is reached - not stored
		g_on_the_air = 0; // turn off any ongoing transmission - not stored
		sei();
		keyTransmitter(OFF); // turn off the transmit signal
	}

	initializeEEPROMVars(disableEvent);
	initializeTransmitterEEPROMVars();
}
