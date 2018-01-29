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
#include "ad5245.h"		/* Potentiometer for tone volume on Digital Interface */
#include "ds3231.h"
#include "mcp23017.h"	/* Port expander on Rev X2 Digital Interface board */
#include "i2c.h"
#include "linkbus.h"
#include "transmitter.h"
#include "huzzah.h"
#include "util.h"

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
	WD_FORCE_RESET
} WDReset;


/***********************************************************************
 * Global Variables & String Constants
 *
 * Identify each global with a "g_" prefix
 * Whenever possible limit globals' scope to this file using "static"
 * Use "volatile" for globals shared between ISRs and foreground
 ************************************************************************/

static volatile BOOL g_powering_off = FALSE;

static volatile BOOL g_radio_port_changed = FALSE;
static volatile uint16_t g_beep_length = 0;
static volatile uint8_t g_wifi_enable_delay = 0;

static volatile uint16_t g_tick_count = 0;
static volatile BOOL g_battery_measurements_active = FALSE;
static volatile uint16_t g_maximum_battery = 0;
static volatile BatteryType g_battery_type = BATTERY_UNKNOWN;
static volatile uint16_t g_send_ID_countdown = 0;
static volatile BOOL g_initialization_complete = FALSE;

#ifdef INCLUDE_DS3231_SUPPORT
	static int32_t g_start_time;
#endif

/* Linkbus variables */
static DeviceID g_LB_attached_device = NO_ID;
static uint16_t g_LB_broadcasts_enabled = 0;
static BOOL g_terminal_mode = INKBUS_TERMINAL_MODE_DEFAULT;
static BOOL g_lb_repeat_rssi = FALSE;
static uint16_t g_rssi_countdown = 0;

#ifdef DEBUG_FUNCTIONS_ENABLE
static volatile uint16_t g_debug_atten_step = 0;
#endif

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

static volatile Frequency_Hz g_receiver_freq = 0;

/* Digital Potentiometer Defines */
static uint8_t EEMEM ee_tone_volume_setting = EEPROM_TONE_VOLUME_DEFAULT;
/* Headphone Driver Defines */
static uint8_t EEMEM ee_main_volume_setting = EEPROM_MAIN_VOLUME_DEFAULT;
static uint8_t EEMEM ee_audio_RSSI_setting = EEPROM_AUDIO_RSSI_DEFAULT;
static uint8_t EEMEM ee_tone_RSSI_direction_setting = EEPROM_TONE_RSSI_DIRECTION_DEFAULT;
static uint8_t EEMEM ee_rssi_filter_setting = EEPROM_TONE_RSSI_FILTER_DEFAULT;

static volatile uint8_t g_main_volume = 0;
static volatile uint8_t g_hw_main_volume = EEPROM_MAIN_VOLUME_DEFAULT;
static volatile uint8_t g_tone_volume;
static volatile uint8_t g_audio_RSSI = FALSE;
static volatile uint8_t g_tone_RSSI_direction = 0;
static volatile uint8_t g_rssi_filter = 0;

#ifdef ENABLE_1_SEC_INTERRUPTS

	static volatile uint16_t g_seconds_count = 0;
	static volatile uint8_t g_seconds_int = FALSE;

#endif  /* #ifdef ENABLE_1_SEC_INTERRUPTS */

/* ADC Defines */

#define RF_READING 0
#define BATTERY_READING 1
#define RSSI_READING 2

#define RF_LEVEL 0x03
#define BAT_VOLTAGE 0x06
#define RSSI_LEVEL 0x07
#define NUMBER_OF_POLLED_ADC_CHANNELS 3
static const uint8_t activeADC[NUMBER_OF_POLLED_ADC_CHANNELS] = { RF_LEVEL, BAT_VOLTAGE, RSSI_LEVEL };

static const uint16_t g_adcChannelConversionPeriod_ticks[NUMBER_OF_POLLED_ADC_CHANNELS] = { TIMER2_0_5HZ, TIMER2_0_5HZ, TIMER2_20HZ };
static volatile uint16_t g_tickCountdownADCFlag[NUMBER_OF_POLLED_ADC_CHANNELS] = { TIMER2_0_5HZ, TIMER2_0_5HZ, TIMER2_20HZ };
static uint16_t g_filterADCValue[NUMBER_OF_POLLED_ADC_CHANNELS] = { 500, 500, 3 };
static volatile BOOL g_adcUpdated[NUMBER_OF_POLLED_ADC_CHANNELS] = { FALSE, FALSE, FALSE };
static volatile uint16_t g_lastConversionResult[NUMBER_OF_POLLED_ADC_CHANNELS];

static volatile uint32_t g_filteredRSSI = 0;

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
void saveAllEEPROM(void);
void wdt_init(WDReset resetType);
void tonePitch(uint8_t pitch);

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
	g_seconds_int = TRUE;
	
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

	g_tick_count++;

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
	if(g_lb_repeat_rssi)
	{
		if(g_rssi_countdown)
		{
			g_rssi_countdown--;
		}
	}

	if(g_LB_broadcast_interval)
	{
		g_LB_broadcast_interval--;
	}
	
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
	else if(g_hw_main_volume != g_main_volume)
	{
		if(g_sufficient_power_detected) // wait until audio amp is powered up
		{
			if(!(PORTC & (1 << PORTC0)))
			{
				PORTC |= (1 << PORTC0); /* set clock high */
			}
			if(g_hw_main_volume > g_main_volume)
			{
				PORTC &= ~(1 << PORTC1);    /* set direction down */
				g_hw_main_volume--;
			}
			else                            /* if(g_hw_main_volume < g_main_volume) */
			{
				PORTC |= (1 << PORTC1);     /* set direction up */
				g_hw_main_volume++;
			}

			volumeSetInProcess = TRUE;
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
		else if(indexConversionInProcess == RSSI_READING)
		{
			uint16_t thresh = 0;
			
			lastResult = holdConversionResult;
			
			switch(g_rssi_filter)
			{
				case 1:
				     thresh = 2500;
				break;
				
				case 2:
				     thresh = 1000;
				break;
				
				case 3:
				     thresh = 500;
				break;
				
				case 4:
				     thresh = 200;
				break;
				
				case 5:
					thresh = 50;
				break;
				
				default:
				     thresh = 5;
				break;
			}

			if(delta > thresh)
			{
				g_filteredRSSI = lastResult;
			}
			else
			{
				const int n = 3;
				g_filteredRSSI = g_filteredRSSI << n;
				g_filteredRSSI += lastResult;
				g_filteredRSSI /= ((1 << n) + 1);
			}
			
			if(g_audio_RSSI)
			{
				g_filteredRSSI = MAX(g_filteredRSSI, 100);
				tonePitch((g_filteredRSSI - 100) >> 3);
			}
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
				if(g_LB_attached_device == NO_ID)
				{
					buff->id = LINKBUS_MSG_UNKNOWN;
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
	BOOL attach_success = TRUE; // Start out in TTY terminal communication mode
	BOOL err = FALSE;
	uint16_t hold_tick_count = 0;
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
//		PORTB |= (1 << PORTB0) | (1 << PORTB2);        /* Enable Radio hardware, and pull up RTC interrupt pin */

	/**
	 * Set up PortD */
	// PD0 = RXD
	// PD1 = TXD
	// PD2 = RTC interrupt
	// PD3 = Port expander interrupt A
	// PD4 = RTTY mark/space
	// PD5 = AM modulation PWM output
	// PD6 = Test Point
	// PD7 =  Port expander interrupt B

//		DDRD  = 0b00000010;     /* Set PORTD pin data directions */
		DDRD  = 0b00000000;     /* Set PORTD pin data directions */
		PORTD = 0b11111100;     /* Enable pull-ups on input pins, and set output levels on all outputs */

	/**
	 * Set up PortC */
	// PC0 = ADC - 80m Forward Power
	// PC1 = ADC - 80m Reflected Power
	// PC2 = ADC - 2m Relative Power
	// PC3 = Test Point
	// PC4 = SDA
	// PC5 = SCL
	// PC6 = Reset
	// PC7 = N/A

	DDRC = 0b00000011;                                          /* PC4 and PC5 are inputs (should be true by default); PC2 and PC3 are used for their ADC function; PC1 and PC0 outputs control main volume */
	PORTC = (I2C_PINS | (1 << PORTC2));                         /* Set all Port C pins low, except I2C lines and PC2; includes output port PORTC0 and PORTC1 (main volume controls) */

	/**
	 * PD5 (OC0B) is PWM output for AM modulation generation
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
	PCICR |= (1 << PCIE2) | (1 << PCIE1) | (1 << PCIE0);    /* Enable pin change interrupts PCI2, PCI1 and PCI0 */
	PCMSK2 |= 0b10001000;                                   /* Enable port D pin change interrupts */
//	PCMSK1 |= (1 << PCINT10);                               /* Enable port C pin change interrupts on pin PC2 */
//	PCMSK0 |= (1 << PORTB2);                                /* Do not enable interrupts until HW is ready */

	EICRA  |= ((1 << ISC01) | (1 << ISC00));	/* Configure INT0 for RTC 1-second interrupts */
	EIMSK |= (1 << INT0);		

	cpu_irq_enable();                                           /* same as sei(); */

	g_low_voltage_shutdown_delay = POWERUP_LOW_VOLTAGE_DELAY;

	/**
	 * Enable watchdog interrupts before performing I2C calls that might cause a lockup */
#ifndef TRANQUILIZE_WATCHDOG
	wdt_init(WD_SW_RESETS);
	wdt_reset();                                    /* HW watchdog */
#endif // TRANQUILIZE_WATCHDOG

	/**
	 * Initialize the transmitter */
	err = init_transmitter();

	if(g_audio_RSSI)
	{
		TCCR0A |= (1 << COM0B0);    /* Toggle OC0B (PD5) on Compare Match */
	}
	
	/**
	 * The watchdog must be petted periodically to keep it from barking */
	wdt_reset();                /* HW watchdog */

	/**
	 * Initialize tone volume setting */
//	ad5245_set_potentiometer(TONE_POT_VAL(g_tone_volume));    /* move to receiver initialization */

	mcp23017_init();
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
		ds3231_read_time(&g_start_time, NULL, Time_Format_Not_Specified);
	   #ifdef ENABLE_1_SEC_INTERRUPTS
			g_seconds_count = 0;    /* sync seconds count to clock */
			ds3231_1s_sqw(ON);
	   #endif    /* #ifdef ENABLE_1_SEC_INTERRUPTS */
		g_wifi_enable_delay = 4;
	#endif

	while(linkbusTxInProgress())
	{
	}               /* wait until transmit finishes */

	g_send_ID_countdown = 0; /* Do not send ID broadcasts initially */
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
		* Check for Power Off
		***************************************/
		if(g_battery_measurements_active)                                                                           /* if ADC battery measurements have stabilized */
		{
			if((g_lastConversionResult[BATTERY_READING] < POWER_OFF_VOLT_THRESH_MV) && g_sufficient_power_detected) /* Battery measurement indicates headphones removed */
			{
				if(!g_headphone_removed_delay)
				{
					if(!g_powering_off)                                                              /* Handle the case of power off immediately after power on */
					{
						g_powering_off = TRUE;
						g_power_off_countdown = POWER_OFF_DELAY;
					}

					g_backlight_off_countdown = g_backlight_delay_value;    /* turn on backlight */


					if(g_terminal_mode)
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
//						PORTB &= ~(1 << PORTB1);    /* latch power off */
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
						g_powering_off = FALSE;
					}
				}
			}
			else if(g_lastConversionResult[BATTERY_READING] > POWER_ON_VOLT_THRESH_MV)  /* Battery measurement indicates sufficient voltage */
			{
				g_sufficient_power_detected = TRUE;
				g_headphone_removed_delay = HEADPHONE_REMOVED_DELAY;
				g_low_voltage_shutdown_delay = LOW_VOLTAGE_DELAY;
				g_power_off_countdown = POWER_OFF_DELAY;    /* restart countdown */

//				PORTB |= (1 << PORTB1); /* latch power on */
			}
			else
			{
				if(!g_low_voltage_shutdown_delay)
				{
					if(!g_powering_off)
					{
						g_powering_off = TRUE;
						g_power_off_countdown = POWER_OFF_DELAY;
					}

					if(!g_power_off_countdown)
					{
						PORTB &= ~(1 << PORTB1);    /* latch power off */

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
//						g_lb_repeat_rssi = FALSE;
//						g_debug_atten_step = 0;
//					}
//					else
//					{
//						toggle = TRUE;
//						g_lb_repeat_rssi = TRUE;
//						g_debug_atten_step = 1;
//					}

					if(toggle)
					{
						toggle = FALSE;
						g_lb_repeat_rssi = FALSE;
						g_debug_atten_step = 0;
					}
					else
					{
						toggle = TRUE;
						g_lb_repeat_rssi = TRUE;
//						rxSetAttenuation(0);
						g_debug_atten_step = 1;
					}
				}
				break;
#endif //DEBUG_FUNCTIONS_ENABLE		

				case MESSAGE_TONE_RSSI:
				{
					if(lb_buff->fields[FIELD1][0])
					{
						int8_t arg;
						arg = atoi(lb_buff->fields[FIELD1]); // Prevent optimizer from breaking this
						if(arg > 0)
						{
							g_tone_RSSI_direction = 0;
							g_audio_RSSI = 1;
							g_rssi_filter = arg;
						}
						else if(arg < 0)
						{
							g_tone_RSSI_direction = 1;
							g_audio_RSSI = 1;
							g_rssi_filter = -arg;
						}
						else
						{
							g_audio_RSSI = 0;
						}
					}
					else
					{
						g_audio_RSSI = !g_audio_RSSI;
					}
	
					if(g_audio_RSSI)
					{
						TCCR0A |= (1 << COM0B0);    /* Toggle OC0B (PD5) on Compare Match */
					}
					else if(!g_beep_length)
					{
						TCCR0A &= ~(1 << COM0B0);   /* Turn off toggling of OC0B (PD5) */
					}
			
					lb_broadcast_num((uint16_t)g_audio_RSSI, NULL);
				}
				break;

				case MESSAGE_PREAMP:
				{
					BOOL result = rxGetPreamp();
					
					if(lb_buff->fields[FIELD1][0])
					{
						uint8_t setting = atoi(lb_buff->fields[FIELD1]);
						
						result = rxSetPreamp(setting);
					}
					
					lb_broadcast_num((uint16_t)result, NULL);

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

						if(result != 2)
						{
							linkbus_init(BAUD);
						}
					}
					
					if(g_terminal_mode) lb_broadcast_num((uint16_t)result, NULL);

				}
				break;

				case MESSAGE_ATTENUATION:
				{
					uint16_t attenuation;
					
					if(lb_buff->fields[FIELD1][0])
					{
						attenuation = CLAMP(0, (uint16_t)atoi(lb_buff->fields[FIELD1]), 100); 
						attenuation = rxSetAttenuation(attenuation);
					}
					else
					{
						attenuation = rxGetAttenuation();
					}

					lb_broadcast_num(attenuation, NULL);
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
				
				case MESSAGE_RSSI_REPEAT_BC:
				{
					g_lb_repeat_rssi = !g_lb_repeat_rssi;
				}
				break;

				case MESSAGE_CW_OFFSET:
				{
					Frequency_Hz offset;
					
					if(lb_buff->fields[FIELD1][0])
					{
						offset = atol(lb_buff->fields[FIELD1]); // Prevent optimizer from breaking this
						rxSetCWOffset(offset);
					}
					
					offset = rxGetCWOffset();
					lb_send_FRE(LINKBUS_MSG_REPLY, offset, FALSE);
				}
				break;
				
				case MESSAGE_PERM:
				{
					store_receiver_values();
					saveAllEEPROM();
				}
				break;
				
				case MESSAGE_TIME:
				{
					if(lb_buff->type == LINKBUS_MSG_COMMAND) // ignore replies since, as the time source, we should never be sending queries anyway
					{
						if(lb_buff->fields[FIELD1][0])
						{
							volatile int32_t time = -1; // prevent optimizer from breaking this

							if(g_terminal_mode)
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
								#ifdef INCLUDE_DS3231_SUPPORT
									ds3231_set_time(time);
								#endif
							}
						}
					}
					else if(lb_buff->type == LINKBUS_MSG_QUERY)
					{
						static int32_t lastTime = 0;
						#ifdef INCLUDE_DS3231_SUPPORT
							
						if(g_seconds_count != lastTime)
						{
							int32_t time;
							ds3231_read_time(&time, NULL, Time_Format_Not_Specified);
							lb_send_TIM(LINKBUS_MSG_REPLY, time);
								
							lastTime = g_seconds_count;
						}
						#endif
					}
				}
				break;

				case MESSAGE_SET_FREQ:
				{
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
									volatile Frequency_Hz memFreq = 0; // Prevent optimizer from breaking this
									isMem = TRUE;

									if(g_terminal_mode)              /* Handle terminal mode message */
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
										if(g_terminal_mode)
										{
											Frequency_Hz m = memFreq;
											if(rxSetFrequency(&m))
											{
												g_receiver_freq = m;
												f = m;
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
								Frequency_Hz f = atol(lb_buff->fields[FIELD1]); // Prevent optimizer from breaking this							
								
								Frequency_Hz ff = f;
								if(rxSetFrequency(&ff))
								{
									g_receiver_freq = ff;
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
								rxSetBand(BAND_80M);
							}
							else if(b == 2)
							{
								rxSetBand(BAND_2M);
							}
						}

						band = rxGetBand();

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

					g_LB_broadcasts_enabled = 0;    /* disable all broadcasts */
					attach_success = TRUE;          /* stop any ongoing ID messages */
					g_send_ID_countdown = 0;
					linkbus_setLineTerm("\n\n");
					
					linkbus_init(BAUD);
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
				
				case MESSAGE_RSSI_BC:
				{
					lb_broadcast_rssi(g_lastConversionResult[RSSI_READING]);
				}
				break;

				case MESSAGE_RF_BC:
				{
				}
				break;

				case MESSAGE_ALL_INFO:
				{
					#ifdef INCLUDE_DS3231_SUPPORT
						int32_t time;
					#endif
					
					cli(); wdt_reset(); /* HW watchdog */ sei();
					linkbus_setLineTerm("\n");
					lb_send_BND(LINKBUS_MSG_REPLY, rxGetBand());
					lb_send_FRE(LINKBUS_MSG_REPLY, rxGetFrequency(), FALSE);
					lb_send_value(g_audio_RSSI, "TON");
					lb_send_FRE(LINKBUS_MSG_REPLY, rxGetCWOffset(), FALSE);
					lb_send_value(rxGetAttenuation(), "ATT");
					lb_send_value(rxGetPreamp(), "PRE");
					lb_send_value(g_main_volume, "MAIN VOL");
					lb_send_value(g_tone_volume, "TONE VOL");
					cli(); wdt_reset(); /* HW watchdog */ sei();
					lb_broadcast_num(g_lastConversionResult[BATTERY_READING], "BAT");
					lb_broadcast_rssi(g_lastConversionResult[RSSI_READING]);
					linkbus_setLineTerm("\n\n");
					cli(); wdt_reset(); /* HW watchdog */ sei();
					#ifdef INCLUDE_DS3231_SUPPORT
						ds3231_read_time(&time, NULL, Time_Format_Not_Specified);
						lb_send_TIM(LINKBUS_MSG_REPLY, time);
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
			if(hold_tick_count != g_tick_count)
			{
				hold_tick_count = g_tick_count;

				if(g_lb_repeat_rssi)
				{
					static uint16_t lastRSSI = 0;
					static uint16_t lastRoundedRSSI = 0;
					
					if(!g_rssi_countdown)
					{
						if(lastRSSI != g_filteredRSSI)
						{
							uint16_t roundedRSSI = g_filteredRSSI / 10;
							
							if(lastRoundedRSSI != roundedRSSI)
							{
								lastRoundedRSSI = roundedRSSI;
#ifndef DEBUG_FUNCTIONS_ENABLE
								g_rssi_countdown = 100;
#endif
								lb_broadcast_rssi(10*roundedRSSI);
							}

							lastRSSI = g_filteredRSSI;
							
						}
						
#ifdef DEBUG_FUNCTIONS_ENABLE
//						g_rssi_countdown = 100;
//						if(g_debug_atten_step)
//						{
//							static uint16_t attenuation = 0;
//						
//							g_debug_atten_step++;
//						
//							if(attenuation)
//							{
//								g_receiver_port_shadow |= 0b00000100;
//								mcp23017_writePort(g_receiver_port_shadow); /* initialize receiver port expander */
//							}
//							else
//							{
//								g_receiver_port_shadow &= 0b11111011;
//								mcp23017_writePort(g_receiver_port_shadow); /* initialize receiver port expander */
//							}
//
//							attenuation++;
//							attenuation = attenuation % 100;
//
//						}

						g_rssi_countdown = 100;
						if(g_debug_atten_step)
						{
//							static uint8_t attenuation = 0;
							static uint16_t pitch = 0;
							
							
							if(g_beep_length ==0)
							{
								uint8_t cs = TCCR0B & 0x07;
								/* 
								1,2,3,4,5
								1,8,64,256,1024
								*/
								
								pitch++;
								
								uint8_t thresh;
								if((cs == 5) || (cs == 4))
								{
									thresh = 64;
								}
								else if((cs == 3) || (cs == 2))
								{
									thresh = 32;	
								}
								else
								{
									thresh = 1;
								}
								
								if(OCR0A> thresh)
								{
									OCR0A--;
								}
								else
								{
									uint8_t cs = TCCR0B & 0x07;
									cs--;
									if(cs == 0)
									{ 
										cs = ((1 << CS02) | (1 << CS00)); 
										pitch = 0;
									}
									cs |= (TCCR0B & 0xF8);
									TCCR0B = cs;                /*  Prescaler */
									OCR0A = 255;
								}

								g_beep_length = 2*BEEP_SHORT;
								
								char str[20];
								sprintf(str, "%d, %d", (TCCR0B & 0x07), OCR0A);
								lb_broadcast_num(pitch, str);
							}

						//	rxSetAttenuation(attenuation++);
						}

#endif // DEBUG_FUNCTIONS_ENABLE

					}				
				}

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
					        lb_broadcast_num(v, "BAT");
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
//						lb_send_ID(LINKBUS_MSG_COMMAND, RECEIVER_ID, g_LB_attached_device);
					}
				}
			}


	}       /* while(1) */
}/* main */

/**********************
**********************/

void initializeEEPROMVars(void)
{
	if(eeprom_read_byte(&ee_interface_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
	{
		g_tone_volume = eeprom_read_byte(&ee_tone_volume_setting);
		g_main_volume = eeprom_read_byte(&ee_main_volume_setting);
		g_audio_RSSI = eeprom_read_byte(&ee_audio_RSSI_setting);
		g_tone_RSSI_direction = eeprom_read_byte(&ee_tone_RSSI_direction_setting);
		g_rssi_filter = eeprom_read_byte(&ee_rssi_filter_setting);
	}
	else
	{
		g_tone_volume = EEPROM_TONE_VOLUME_DEFAULT;
		g_main_volume = EEPROM_MAIN_VOLUME_DEFAULT;
		g_audio_RSSI = EEPROM_AUDIO_RSSI_DEFAULT;
		g_tone_RSSI_direction = EEPROM_TONE_RSSI_DIRECTION_DEFAULT;
		g_rssi_filter = EEPROM_TONE_RSSI_FILTER_DEFAULT;

		saveAllEEPROM();
		eeprom_write_byte(&ee_interface_eeprom_initialization_flag, EEPROM_INITIALIZED_FLAG);
		wdt_reset();                                    /* HW watchdog */
	}
}

void saveAllEEPROM()
{
	wdt_reset();                                    /* HW watchdog */
	storeEEbyteIfChanged(&ee_tone_volume_setting, g_tone_volume);
	storeEEbyteIfChanged(&ee_main_volume_setting, g_main_volume);
	storeEEbyteIfChanged(&ee_audio_RSSI_setting, g_audio_RSSI);
	storeEEbyteIfChanged(&ee_tone_RSSI_direction_setting, g_tone_RSSI_direction);
	storeEEbyteIfChanged(&ee_rssi_filter_setting, g_rssi_filter);
}
	
void tonePitch(uint8_t pitch)
{
	Frequency_Hz freq;
	uint16_t prescale = 8;
	
	if(g_tone_RSSI_direction == 1)
	{
		pitch = 255 - pitch;
	}
	
	freq = 15 + (13*pitch);
					
	TCCR0B &= 0xF8;

	if(freq < 60) // 1024
	{
		TCCR0B |= 0x05;
		prescale = 1024;
	}
	else if(freq < 240) // 256
	{
		TCCR0B |= 0x04;
		prescale = 256;
	}
	else if(freq < 1900) // 64
	{
		TCCR0B |= 0x03;
		prescale = 64;
	}
	else // 8
	{
		TCCR0B |= 0x02;
	}

	OCR0A =  MIN(((8000000 / (2 * prescale * freq)) - 1), 255);
}
