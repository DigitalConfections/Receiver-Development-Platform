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
#include "si5351.h"		/* Programmable clock generator */
#include "dac081c085.h"	/* Transmit power level DAC */
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

#define EVENT_TIME_PASSED -1

typedef enum
{
	WD_SW_RESETS,
	WD_HW_RESETS,
	WD_FORCE_RESET
} WDReset;


/***********************************************************************
 * Global Variables & String Constants
 *
 * Identify each global with a "g_" prefix
 * Whenever possible limit globals' scope to this file using "static"
 * Use "volatile" for globals shared between ISRs and foreground
 ************************************************************************/
static char g_tempStr[21] = {'\0'};

static volatile BOOL g_powering_off = FALSE;

static volatile BOOL g_radio_port_changed = FALSE;
static volatile uint8_t g_wifi_enable_delay = 0;

static volatile uint16_t g_tick_count = 0;
static volatile BOOL g_battery_measurements_active = FALSE;
static volatile uint16_t g_maximum_battery = 0;
static volatile BatteryType g_battery_type = BATTERY_UNKNOWN;
static volatile BOOL g_initialization_complete = FALSE;

#ifdef INCLUDE_DS3231_SUPPORT
	static int32_t g_startup_time;
#endif

/* Linkbus variables */
static BOOL g_terminal_mode = LINKBUS_TERMINAL_MODE_DEFAULT;

#ifdef DEBUG_FUNCTIONS_ENABLE
static volatile uint16_t g_debug_atten_step = 0;
#endif

#define MAX_STATION_ID_LENGTH 20
#define MAX_PATTERN_TEXT_LENGTH 20

static BOOL EEMEM ee_interface_eeprom_initialization_flag = EEPROM_UNINITIALIZED;

static char EEMEM ee_stationID_text[MAX_STATION_ID_LENGTH];
static char EEMEM ee_pattern_text[MAX_PATTERN_TEXT_LENGTH];
static uint8_t EEMEM ee_pattern_codespeed;
static uint8_t EEMEM ee_id_codespeed;
static uint8_t EEMEM ee_on_air_time;
static uint8_t EEMEM ee_off_air_time;
static uint8_t EEMEM ee_intra_cycle_delay_time;
static int32_t EEMEM ee_start_time;
static int32_t EEMEM ee_finish_time;

static char g_station_ID[MAX_STATION_ID_LENGTH] = "Foxcall";
static char g_pattern_text[MAX_PATTERN_TEXT_LENGTH] = "\0";
static uint8_t g_id_codespeed = EEPROM_ID_CODE_SPEED_DEFAULT;
static uint8_t g_pattern_codespeed = EEPROM_PATTERN_CODE_SPEED_DEFAULT;
static uint8_t g_on_air_time = EEPROM_ON_AIR_TIME_DEFAULT; /* amount of time to spend on the air */
static uint8_t g_off_air_time = EEPROM_OFF_AIR_TIME_DEFAULT; /* amount of time to wait before returning to the air */
static uint8_t g_intra_cycle_delay_time = EEPROM_INTRA_CYCLE_DELAY_TIME_DEFAULT; /* offset time into a repeating transmit cycle */
static int32_t g_event_start_time = EEPROM_START_TIME_DEFAULT;
static int32_t g_event_finish_time = EEPROM_FINISH_TIME_DEFAULT;

static int16_t g_on_the_air = 0;
static uint16_t g_time_to_send_ID = 0;
static uint16_t g_code_throttle = 50;
static uint8_t g_WiFi_shutdown_seconds = 120;

static volatile Frequency_Hz g_transmitter_freq = 0;

#ifdef ENABLE_1_SEC_INTERRUPTS
	static int32_t g_seconds_count = 0;
#endif  /* #ifdef ENABLE_1_SEC_INTERRUPTS */

/* ADC Defines */

#define BATTERY_READING 0
#define PA_VOLTAGE_READING 1

#define TX_PA_DRIVE_VOLTAGE 0x06
#define BAT_VOLTAGE 0x07
#define NUMBER_OF_POLLED_ADC_CHANNELS 2
static const uint8_t activeADC[NUMBER_OF_POLLED_ADC_CHANNELS] = { BAT_VOLTAGE, TX_PA_DRIVE_VOLTAGE };

static const uint16_t g_adcChannelConversionPeriod_ticks[NUMBER_OF_POLLED_ADC_CHANNELS] = { TIMER2_0_5HZ, TIMER2_0_5HZ };
static volatile uint16_t g_tickCountdownADCFlag[NUMBER_OF_POLLED_ADC_CHANNELS] = { TIMER2_0_5HZ, TIMER2_0_5HZ };
static uint16_t g_filterADCValue[NUMBER_OF_POLLED_ADC_CHANNELS] = { 500, 500 };
static volatile BOOL g_adcUpdated[NUMBER_OF_POLLED_ADC_CHANNELS] = { FALSE, FALSE };
static volatile uint16_t g_lastConversionResult[NUMBER_OF_POLLED_ADC_CHANNELS];

static volatile uint32_t g_PA_voltage = 0;

extern volatile BOOL g_i2c_not_timed_out;
static volatile BOOL g_sufficient_power_detected = FALSE;
static volatile BOOL g_enableHardwareWDResets = FALSE;

/***********************************************************************
 * Private Function Prototypes
 *
 * These functions are available only within this file
 ************************************************************************/

void initializeEEPROMVars(void);
void saveAllEEPROM(void);
void wdt_init(WDReset resetType);
uint16_t throttleValue(uint8_t speed);

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


ISR( INT0_vect )
{
#ifdef ENABLE_1_SEC_INTERRUPTS
	g_seconds_count++;

	if(g_on_the_air)
	{
		if(g_event_finish_time > 0)
		{
			ds3231_read_date_time(&g_seconds_count, NULL, Time_Format_Not_Specified);
		
			if(g_seconds_count >= g_event_finish_time)
			{
				g_on_the_air = 0;
				g_event_finish_time = EVENT_TIME_PASSED;
				keyTransmitter(OFF);
			}
		}

		if(g_on_the_air > 0) /* on the air */
		{
			g_on_the_air--;
		
			if(!g_on_the_air)
			{
				if(g_off_air_time)
				{
					keyTransmitter(OFF);
					g_on_the_air -= g_off_air_time;
				}
				else
				{
					g_on_the_air = g_on_air_time;
					g_code_throttle = throttleValue(g_pattern_codespeed);
					makeMorse(g_pattern_text, TRUE);
				}
			}
			else if(g_on_the_air == g_time_to_send_ID)
			{
				g_code_throttle = throttleValue(g_id_codespeed);
				makeMorse(g_station_ID, FALSE); /* Send only once */
			}
		}
		else if(g_on_the_air < 0) /* off the air - g_on_the_air = 0 means all transmissions are disabled */
		{
			g_on_the_air++;
		
			if(!g_on_the_air)
			{
				g_on_the_air = g_on_air_time;
				g_code_throttle = throttleValue(g_pattern_codespeed);
				makeMorse(g_pattern_text, TRUE);
			}
		}
	}
	else if(g_event_start_time > 0) /* off the air - waiting for the start time to arrive */
	{
		ds3231_read_date_time(&g_seconds_count, NULL, Time_Format_Not_Specified); 
		
		if(g_seconds_count >= g_event_start_time)
		{
			if(g_intra_cycle_delay_time)
			{
				g_on_the_air = -g_intra_cycle_delay_time;
			}
			else
			{
				g_on_the_air = g_on_air_time;
				g_code_throttle = throttleValue(g_pattern_codespeed);
				makeMorse(g_pattern_text, TRUE);
			}
			
			g_event_start_time = EVENT_TIME_PASSED;
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
			}
		}
	}

	
#else
	if(g_terminal_mode)
	{
		lb_send_string("\nError: INT0 occurred!\n");
	}
#endif
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

	g_tick_count++;

	static BOOL key = FALSE;
	
	if(g_on_the_air)
	{
		if(codeInc)
		{
			if(codeInc == 10)
			{
				key = makeMorse(NULL, FALSE);
				if(key) powerToTransmitter(ON);
			}
			else if(codeInc == g_code_throttle)
			{
//				if(!key) powerToTransmitter(OFF);	
			}
			
			codeInc--;
		}
		else
		{
			keyTransmitter(key);
			codeInc = g_code_throttle;
		}
	}
	else
	{
		
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
		else if(indexConversionInProcess == PA_VOLTAGE_READING)
		{
			lastResult = holdConversionResult;
			g_PA_voltage = holdConversionResult * PA_VOLTAGE_SCALE_FACTOR;
		}

		g_lastConversionResult[indexConversionInProcess] = lastResult;

		conversionInProcess = FALSE;
	}
}/* ISR */


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
	if(g_terminal_mode)
	{
		lb_send_string("\nError: PCINT0 occurred!\n");
	}
}


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

		if(g_terminal_mode)
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
		if(g_terminal_mode)
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

						charIndex = MIN(charIndex+1, LINKBUS_MAX_MSG_FIELD_LENGTH);
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
						charIndex = MIN(charIndex+1, LINKBUS_MAX_MSG_FIELD_LENGTH);
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
	if(g_terminal_mode)
	{
		lb_send_string("\nError: PCINT2 occurred!\n");
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
	BOOL err = FALSE;
	LinkbusRxBuffer* lb_buff = 0;

	/**
	 * Initialize internal EEPROM if needed */
	initializeEEPROMVars();
	
	/**
	 * Set up PortB  */
	// PB0 = D101 LED out
	// PB1 = SW101 input
	// PB2 = Tx Gain PWM output
	// PB3 = MOSI
	// PB4 = MISO
	// PB5 = SCK
	// PB6 = Tx Power Enable
	// PB7 = Main Power Enable

	DDRB |= (1 << PORTB0) | (1 << PORTB2) | (1 << PORTB6) | (1 << PORTB7);       /* PB0 is Radio Enable output; */
	PORTB |= (1 << PORTB7); /* Turn on main power */
//	PORTB |= (1 << PORTB0) | (1 << PORTB2);        /* Enable Radio hardware, and pull up RTC interrupt pin */

	/**
	 * Set up PortD */
	// PD0 = RXD
	// PD1 = TXD
	// PD2 = RTC interrupt
	// PD3 = Port expander interrupt A
	// PD4 = RTTY mark/space
	// PD5 = AM modulation tone output
	// PD6 = Antenna connected interrupt PCINT22
	// PD7 =  Port expander interrupt B

//	DDRD  = 0b00000010;     /* Set PORTD pin data directions */
	DDRD  |= (1 << PORTD4) | (1 << PORTD5);     /* Set PORTD pin data directions */
	PORTD = (1 << PORTD2) | (1 << PORTD3) | (1 << PORTD6) | (1 << PORTD7);     /* Enable pull-ups on input pins, and set output levels on all outputs */

	/**
	 * Set up PortC */
	// PC0 = ADC - 80m Forward Power
	// PC1 = ADC - 80m Reflected Power
	// PC2 = ADC - 2m Relative Power
	// PC3 = Test Point (enable internal pull-up)
	// PC4 = SDA
	// PC5 = SCL
	// PC6 = Reset
	// PC7 = N/A

	DDRC = 0b00000000;        
	PORTC = I2C_PINS | (1 << PORTC3);     

	/**
	 * PD5 (OC0B) is tone output for AM modulation generation
	 * TIMER0 */
//	OCR0A = 0x0C;                                       /* set compare value */
	OCR0A = 0x04;                                       /* set compare value */
	TCCR0A |= (1 << WGM01);                             /* set CTC (MODE 2) with OCRA */
	TCCR0A |= (1 << COM0B0);							/* Toggle OC0B on Compare Match */
	TCCR0B |= (1 << CS02) | (1 << CS00);                /* 1024 Prescaler */
/*	TIMSK0 &= ~(1 << OCIE0B); // disable compare interrupt - disabled by default */

	/** 
	* PB2 (OC1B) is PWM for output power level
	* TIMER1 is for transmit power PWM */
	OCR1B = DEFAULT_AM_DRIVE_LEVEL; /* Set initial duty cycle */
	TCCR1A |= (1 << WGM10); /* 8-bit Phase Correct PWM mode */
	TCCR1A |= (1 << COM1B1); /* Non-inverting mode */
	TCCR1B |= (1 << CS11) | (1 << CS10); /* Prescaler */

	/**
	 * TIMER2 is for periodic interrupts */
	OCR2A = 0x0C;                                       /* set frequency to ~300 Hz (0x0c) */
	TCCR2A |= (1 << WGM01);                             /* set CTC with OCRA */
	TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20);  /* 1024 Prescaler - why are we setting CS21?? */
	TIMSK2 |= (1 << OCIE0B);                            /* enable compare interrupt */

	/**
	 * Set up ADC */
	ADMUX |= (1 << REFS0);
	ADCSRA |= (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0) | (1 << ADEN);

	/**
	 * Set up pin interrupts */
	PCICR |= (1 << PCIE2) | (1 << PCIE1) | (1 << PCIE0);    /* Enable pin change interrupts PCI2, PCI1 and PCI0 */
	PCMSK2 |= 0b10001000;                                   /* Enable port D pin change interrupts */
//	PCMSK1 |= (1 << PCINT10);                               /* Enable port C pin change interrupts on pin PC2 */
//	PCMSK0 |= (1 << PORTB2);                                /* Do not enable interrupts until HW is ready */

	EICRA  |= ((1 << ISC01) | (1 << ISC00));	/* Configure INT0 for RTC 1-second interrupts */
	EIMSK |= (1 << INT0);		

	cpu_irq_enable();                                           /* same as sei(); */

	/**
	 * Enable watchdog interrupts before performing I2C calls that might cause a lockup */
#ifndef TRANQUILIZE_WATCHDOG
	wdt_init(WD_SW_RESETS);
	wdt_reset();                                    /* HW watchdog */
#endif // TRANQUILIZE_WATCHDOG

	mcp23017_init();

	/**
	 * Initialize the transmitter */
	err = init_transmitter();

	/**
	 * The watchdog must be petted periodically to keep it from barking */
	wdt_reset();                /* HW watchdog */

	/**
	 * Initialize tone volume setting */

//	wifi_power(ON); // power on WiFi
//	wifi_reset(OFF); // bring WiFi out of reset
	// Uncomment the two lines above and set a breakpoint after this line to permit serial access to ESP8266 serial lines for programming
	// You can then set a breakpoint on the line below to keep the serial port from being initialized
	linkbus_init(BAUD);

	wdt_reset();                                    /* HW watchdog */

	if(g_terminal_mode)
	{
		if(err)
		{
			lb_send_NewLine();
			lb_send_string("Init error!");
			lb_send_NewPrompt();		
			
		}
		else
		{
			lb_send_NewLine();
			lb_send_Help();
			lb_send_NewPrompt();		
		}
	}
	else 
	{
		lb_send_sync();                                 /* send test pattern to help synchronize baud rate with any attached device */
		while(linkbusTxInProgress())
		{
			;                                           /* wait until transmit finishes */
		}
		wdt_reset();
	}
	
	wdt_reset();
		
	#ifdef INCLUDE_DS3231_SUPPORT
		ds3231_read_date_time(&g_startup_time, NULL, Time_Format_Not_Specified);
	   #ifdef ENABLE_1_SEC_INTERRUPTS
			ds3231_1s_sqw(ON);
	   #endif    /* #ifdef ENABLE_1_SEC_INTERRUPTS */
		g_wifi_enable_delay = 4;
	#endif

	while(linkbusTxInProgress())
	{
	}               /* wait until transmit finishes */

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
		if(g_battery_measurements_active)                                                                           /* if ADC battery measurements have stabilized */
		{
			if(g_lastConversionResult[BATTERY_READING] > POWER_ON_VOLT_THRESH_MV)  /* Battery measurement indicates sufficient voltage */
			{
				g_sufficient_power_detected = TRUE;
			}
		}

		/***********************************************************************
		 *  Handle arriving Linkbus messages
		 ************************************************************************/
		while((lb_buff = nextFullRxBuffer()))
		{
			LBMessageID msg_id = lb_buff->id;

			switch(msg_id)
			{
#ifdef DEBUG_FUNCTIONS_ENABLE
				case MESSAGE_DEBUG:
				{
					static BOOL toggle = FALSE;
//					
//					if(toggle)
//					{
//						toggle = FALSE;
//						g_lb_repeat_readings = FALSE;
//						g_debug_atten_step = 0;
//					}
//					else
//					{
//						toggle = TRUE;
//						g_lb_repeat_readings = TRUE;
//						g_debug_atten_step = 1;
//					}

					if(toggle)
					{
						toggle = FALSE;
						g_debug_atten_step = 0;
					}
					else
					{
						toggle = TRUE;
						g_debug_atten_step = 1;
					}
				}
				break;
#endif //DEBUG_FUNCTIONS_ENABLE		

				case MESSAGE_DRIVE_LEVEL:
				{
					uint16_t result = OCR1B;
					
					if(lb_buff->fields[FIELD1][0])
					{
						uint16_t setting = atoi(lb_buff->fields[FIELD1]);
						
						if(setting >= 1000)
						{
							DDRD  &= ~(1 << PORTD5); // set clock pin to an input
							PORTD |= (1 << PORTD5); // enable pull-up
						}
						else if(setting > 255)
						{
							DDRD |= (1 << PORTD5); // set clock pin to an output
						}
						else
						{
							result = setting;
							txSetDrive(setting);
						}
					}
					
					lb_broadcast_num(result, "DRI");

				}
				break;
				
				case MESSAGE_WIFI:
				{
					BOOL result = wifi_enabled();
					
					if(lb_buff->fields[FIELD1][0])
					{
						result = atoi(lb_buff->fields[FIELD1]);
						
						cli(); 
						linkbus_disable();
						sei();	
						wifi_power(result);
						g_terminal_mode = !result;
						wifi_reset(g_terminal_mode);
						linkbus_setTerminalMode(g_terminal_mode);

						if(result == 2)
						{
							g_WiFi_shutdown_seconds = 0; // disable shutdown
						}
						else
						{
							linkbus_init(BAUD);
						}
					}
					
					if(g_terminal_mode) lb_broadcast_num((uint16_t)result, NULL);

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
						/* WiFi is awake. Send it the current time */
						#ifdef INCLUDE_DS3231_SUPPORT
						ds3231_read_date_time(NULL, g_tempStr, Time_Format_Not_Specified);
						lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);
						#endif
					}
					else if(f1 == '1')
					{
						/* ESP8266 is ready with event data */
						lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_ESP_LABEL, "1");
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
					else if(f1 == 'Z') /* No scheduled events - keep alive */
					{
						/* shut down WiFi after 2 minutes of inactivity */
						g_WiFi_shutdown_seconds = 120; // wait 2 more minutes before shutting down WiFi
					}
				}
				break;
				
				case MESSAGE_TX_POWER:
				{
					uint8_t pwr;
					
					if(lb_buff->fields[FIELD1][0])
					{
						pwr = atol(lb_buff->fields[FIELD1]); 
						txSetPowerLevel(pwr);
						saveAllEEPROM(); 
					}
					
					pwr = txGetPowerLevel();
					lb_send_value(pwr, "POW");
				}
				break;
				
				case MESSAGE_PERM:
				{
					storeTtransmitterValues();
					saveAllEEPROM();
				}
				break;
				
				case MESSAGE_TIME:
				{
					BOOL error = TRUE;
					int32_t time;
					
					if(lb_buff->fields[FIELD1][0] == 'S')
					{
						if(lb_buff->fields[FIELD2][0])
						{
							error = stringToSecondsSinceMidnight(lb_buff->fields[FIELD2], &time);
						}
		
						if(!error) g_event_start_time = time;
					}
					else if(lb_buff->fields[FIELD1][0] == 'F')
					{
						if(lb_buff->fields[FIELD2][0])
						{
							error = stringToSecondsSinceMidnight(lb_buff->fields[FIELD2], &time);
						}
						
						if(!error) g_event_finish_time = time;
					}
						
					if(!error)
					{
						saveAllEEPROM(); 
						if(g_terminal_mode) lb_send_value((int16_t)time, "sec=");
					}
					else if(g_terminal_mode)
					{
						lb_send_string("err\n");
					}
				}
				break;
				
				case MESSAGE_CLOCK:
				{
					if(g_terminal_mode)
					{
						if(lb_buff->fields[FIELD1][0])
						{ /* Expected format:  2018-03-23T18:00:00 */
							if((lb_buff->fields[FIELD1][13] == ':') && (lb_buff->fields[FIELD1][16] == ':'))
							{
								strncpy(g_tempStr, lb_buff->fields[FIELD1], 20);
								#ifdef INCLUDE_DS3231_SUPPORT
								ds3231_set_date_time(g_tempStr, RTC_CLOCK);
								#endif
							}
						}
						
						ds3231_read_date_time(NULL, g_tempStr, Time_Format_Not_Specified);
						lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);
					}
					else if(lb_buff->type == LINKBUS_MSG_COMMAND) // ignore replies since, as the time source, we should never be sending queries anyway
					{
						if(lb_buff->fields[FIELD1][0])
						{
							strncpy(g_tempStr, lb_buff->fields[FIELD1], 20);
							#ifdef INCLUDE_DS3231_SUPPORT
								ds3231_set_date_time(g_tempStr, RTC_CLOCK);
							#endif
						}
						else
						{
							#ifdef INCLUDE_DS3231_SUPPORT
							ds3231_read_date_time(NULL, g_tempStr, Time_Format_Not_Specified);
							lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);
							#endif
						}
					}
					else if(lb_buff->type == LINKBUS_MSG_QUERY)
					{
						static int32_t lastTime = 0;
						
						#ifdef INCLUDE_DS3231_SUPPORT							
						if(g_seconds_count != lastTime)
						{
							ds3231_read_date_time(NULL, g_tempStr, Time_Format_Not_Specified);
							lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);
							lastTime = g_seconds_count;
						}
						#endif
					}
				}
				break;
				
				case MESSAGE_SET_STATION_ID:
				{
					if(lb_buff->fields[FIELD1][0])
					{
						strncpy(g_station_ID, lb_buff->fields[FIELD1], MAX_STATION_ID_LENGTH);
						saveAllEEPROM(); 
						g_time_to_send_ID = (stringTimeRequiredToSend(g_station_ID, g_id_codespeed) + 999) / 1000;
					}
					
					lb_send_string(g_station_ID);
					lb_send_value(stringTimeRequiredToSend(g_station_ID, g_id_codespeed), "ms");
					lb_send_value(g_time_to_send_ID, "s");
					lb_send_NewLine();
				}
				break;
				
				case MESSAGE_CODE_SPEED:
				{
					uint8_t speed = g_pattern_codespeed;
					
					if(lb_buff->fields[FIELD1][0] == 'I')
					{
						speed = g_id_codespeed;
						if(lb_buff->fields[FIELD2][0]) speed = atol(lb_buff->fields[FIELD2]);
						if((speed > 4) && (speed < 21)) g_id_codespeed = speed;
						saveAllEEPROM(); 
					}
					else if(lb_buff->fields[FIELD1][0] == 'P')
					{
						if(lb_buff->fields[FIELD2][0]) speed = atol(lb_buff->fields[FIELD2]);
						if((speed > 4) && (speed < 21)) g_pattern_codespeed = speed;
						saveAllEEPROM();
						g_code_throttle = (7042 / g_pattern_codespeed) / 10;
					}
					
					lb_send_value(speed, "spd");
					lb_send_NewLine();
				}
				break;
				
				case MESSAGE_TIME_INTERVAL:
				{
					uint16_t time = g_on_air_time;
					
					if(lb_buff->fields[FIELD1][0] == '0')
					{
						time = g_off_air_time;
						if(lb_buff->fields[FIELD2][0]) time = atol(lb_buff->fields[FIELD2]);
						g_off_air_time = time;
						saveAllEEPROM(); 
					}
					else if(lb_buff->fields[FIELD1][0] == '1')
					{
						if(lb_buff->fields[FIELD2][0]) time = atol(lb_buff->fields[FIELD2]);
						g_on_air_time = time;
						saveAllEEPROM();
					}
					else if(lb_buff->fields[FIELD1][0] == 'D')
					{
						if(lb_buff->fields[FIELD2][0]) time = atol(lb_buff->fields[FIELD2]);
						g_intra_cycle_delay_time = time;
						saveAllEEPROM();
					}
					
					lb_send_value(time, "t");
					lb_send_NewLine();
				}
				break;
				
				case MESSAGE_SET_PATTERN:
				{
					if(lb_buff->fields[FIELD1][0])
					{
						strncpy(g_pattern_text, lb_buff->fields[FIELD1], MAX_PATTERN_TEXT_LENGTH);
						saveAllEEPROM(); 
						g_code_throttle = throttleValue(g_pattern_codespeed);
						makeMorse(g_pattern_text, TRUE);
					}
					
					lb_send_string(g_pattern_text);
					lb_send_value(stringTimeRequiredToSend(g_pattern_text, g_pattern_codespeed), "t");
					lb_send_NewLine();
				}
				break;

				case MESSAGE_SET_FREQ:
				{
						BOOL isMem = FALSE;

						if(lb_buff->fields[FIELD1][0])
						{
							Frequency_Hz f = atol(lb_buff->fields[FIELD1]); // Prevent optimizer from breaking this							
								
							Frequency_Hz ff = f;
							if(txSetFrequency(&ff))
							{
								g_transmitter_freq = ff;
							}
						}
						else
						{
							g_transmitter_freq = txGetFrequency();
						}

						if(g_transmitter_freq)
						{
							lb_send_FRE(LINKBUS_MSG_REPLY, g_transmitter_freq, isMem);
						}
				}
				break;

				case MESSAGE_BAND:
				{
					RadioBand band;

					if(lb_buff->fields[FIELD1][0])  /* band field */
					{
						int b = atoi(lb_buff->fields[FIELD1]);
							
						if(b == 80)
						{
							txSetBand(BAND_80M);
						}
						else if(b == 2)
						{
							txSetBand(BAND_2M);
						}
					}

					band = txGetBand();

					if(lb_buff->type == LINKBUS_MSG_QUERY)  /* Query */
					{
						/* Send a reply */
						lb_send_BND(LINKBUS_MSG_REPLY, band);
					}
				}
				break;

				case MESSAGE_TTY:
				{
					g_terminal_mode = TRUE;
					linkbus_setTerminalMode(g_terminal_mode);
					
					cli(); 
					linkbus_disable();
					sei();	
					wifi_reset(ON);
					wifi_power(OFF);

					linkbus_setLineTerm("\n\n");
					
					linkbus_init(BAUD);
				}
				break;

				case MESSAGE_BAT:
				{
					lb_broadcast_num(g_lastConversionResult[BATTERY_READING], "!BAT");
				}
				break;

				case MESSAGE_TEMP:
				{
					int16_t v;
					if(!ds3231_get_temp(&v)) lb_broadcast_num(v, "!TEM");
				}
				break;
				
				case MESSAGE_ALL_INFO:
				{
					cli(); wdt_reset(); /* HW watchdog */ sei();
					linkbus_setLineTerm("\n");
					lb_send_BND(LINKBUS_MSG_REPLY, txGetBand());
					lb_send_FRE(LINKBUS_MSG_REPLY, txGetFrequency(), FALSE);
					cli(); wdt_reset(); /* HW watchdog */ sei();
					lb_broadcast_num(g_lastConversionResult[BATTERY_READING], "BAT");
					lb_broadcast_num(g_PA_voltage, "Vpa");
					linkbus_setLineTerm("\n\n");
					cli(); wdt_reset(); /* HW watchdog */ sei();
					#ifdef INCLUDE_DS3231_SUPPORT
						ds3231_read_date_time(NULL, g_tempStr, Time_Format_Not_Specified);
						lb_send_msg(LINKBUS_MSG_REPLY, MESSAGE_TIME_LABEL, g_tempStr);
						cli(); wdt_reset(); /* HW watchdog */ sei();
					#endif
				}
				break;

				default:
				{
					if(g_terminal_mode)
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

			if(g_terminal_mode)
			{
				lb_send_NewPrompt();
			}

			lb_buff->id = MESSAGE_EMPTY;
		}


			/* ////////////////////////////////////
			 * Handle Receiver interrupts (e.g., trigger button presses) */
			if(g_radio_port_changed)
			{
				g_radio_port_changed = FALSE;

//				uint8_t portPins;
//				mcp23017_readPort(&portPins);

				/* Take appropriate action for the pin change */
				/*			if((~portPins) & 0b00010000)
				 *			{
				 *			} */
			}

			/* ////////////////////////////////////
			 * Handle periodic tasks triggered by the tick count */
//			if(hold_tick_count != g_tick_count)
//			{
//				hold_tick_count = g_tick_count;
//			}
	}       /* while(1) */
}/* main */

/**********************
**********************/

void initializeEEPROMVars(void)
{
	uint8_t i;
	
	if(eeprom_read_byte(&ee_interface_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
	{
		g_event_start_time = eeprom_read_dword((uint32_t*)(&ee_start_time));
		g_event_finish_time = eeprom_read_dword((uint32_t*)(&ee_finish_time));
		
		g_pattern_codespeed = eeprom_read_byte(&ee_pattern_codespeed);
		g_id_codespeed = eeprom_read_byte(&ee_id_codespeed);
		g_on_air_time = eeprom_read_byte(&ee_on_air_time);
		g_off_air_time = eeprom_read_byte(&ee_off_air_time);
		g_intra_cycle_delay_time = eeprom_read_byte(&ee_intra_cycle_delay_time);
		
		for(i=0; i<20; i++)
		{
			g_station_ID[i] = (char)eeprom_read_byte((uint8_t*)(&ee_stationID_text[i]));
			if(!g_station_ID[i]) break;
		}
		
		for(i=0; i<20; i++)
		{
			g_pattern_text[i] = (char)eeprom_read_byte((uint8_t*)(&ee_pattern_text[i]));
			if(!g_pattern_text[i]) break;
		}
	}
	else
	{
		g_event_start_time = EEPROM_START_TIME_DEFAULT;
		g_event_finish_time = EEPROM_FINISH_TIME_DEFAULT;
		
		g_id_codespeed = EEPROM_ID_CODE_SPEED_DEFAULT;
		g_pattern_codespeed = EEPROM_PATTERN_CODE_SPEED_DEFAULT;
		g_on_air_time = EEPROM_ON_AIR_TIME_DEFAULT;
		g_off_air_time = EEPROM_OFF_AIR_TIME_DEFAULT;
		g_intra_cycle_delay_time = EEPROM_INTRA_CYCLE_DELAY_TIME_DEFAULT;
		
		strncpy(g_station_ID, EEPROM_STATION_ID_DEFAULT, MAX_STATION_ID_LENGTH);
		strncpy(g_pattern_text, EEPROM_PATTERN_TEXT_DEFAULT, MAX_PATTERN_TEXT_LENGTH);

		saveAllEEPROM();
		eeprom_write_byte(&ee_interface_eeprom_initialization_flag, EEPROM_INITIALIZED_FLAG);
		wdt_reset();                                    /* HW watchdog */
	}
}

void saveAllEEPROM()
{
	int i;
	wdt_reset();                                    /* HW watchdog */
	
	storeEEdwordIfChanged((uint32_t*)&ee_start_time, g_event_start_time);
	storeEEdwordIfChanged((uint32_t*)&ee_finish_time, g_event_finish_time);
	
	storeEEbyteIfChanged(&ee_id_codespeed, g_id_codespeed);
	storeEEbyteIfChanged(&ee_pattern_codespeed, g_pattern_codespeed);
	storeEEbyteIfChanged(&ee_on_air_time, g_on_air_time);
	storeEEbyteIfChanged(&ee_off_air_time, g_off_air_time);
	storeEEbyteIfChanged(&ee_intra_cycle_delay_time, g_intra_cycle_delay_time);

	for(i=0; i<strlen(g_station_ID); i++)
	{
		storeEEbyteIfChanged((uint8_t*)&ee_stationID_text[i], (uint8_t)g_station_ID[i]);
	}

	storeEEbyteIfChanged((uint8_t*)&ee_stationID_text[i], 0);
	
	for(i=0; i<strlen(g_pattern_text); i++)
	{
		storeEEbyteIfChanged((uint8_t*)&ee_pattern_text[i], (uint8_t)g_pattern_text[i]);
	}
	
	storeEEbyteIfChanged((uint8_t*)&ee_pattern_text[i], 0);
}

uint16_t throttleValue(uint8_t speed)
{
	uint16_t temp = (7042L / (uint16_t)speed) / 10L;
	return temp;
}
