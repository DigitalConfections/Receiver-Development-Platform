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
 * receiver.c
 *
 */

#include "util.h"


#include <stdlib.h>
#include "transmitter.h"
#include "mcp23017.h"	/* Port expander on Rev X2 Digital Interface board */
#include "max5478.h"	/* Potentiometer for receiver attenuation on Rev X.1 Receiver board */
#include "dac081c085.h" /* DAC on 80m VGA of Rev X1 Receiver board */

#ifdef INCLUDE_RECEIVER_SUPPORT

	static volatile BOOL g_rx_initialized = FALSE;
	static volatile Frequency_Hz g_freq_2m = DEFAULT_RX_2M_FREQUENCY;
	static volatile Frequency_Hz g_freq_80m = DEFAULT_RX_80M_FREQUENCY;
	static volatile Frequency_Hz g_cw_offset = DEFAULT_RX_CW_OFFSET_FREQUENCY;
	static volatile uint8_t g_preamp_80m = DEFAULT_PREAMP_80M;
	static volatile uint8_t g_preamp_2m = DEFAULT_PREAMP_2M;
	static volatile uint8_t g_attenuation_setting = DEFAULT_ATTENUATION;
	static volatile Frequency_Hz g_freq_bfo = RADIO_IF_FREQUENCY;
	static volatile RadioVFOConfig g_vfo_configuration = VFO_2M_LOW_80M_HIGH;
	static volatile RadioBand g_activeBand = DEFAULT_RX_ACTIVE_BAND;
	
	static volatile uint8_t g_receiver_port_shadow = 0x00; // keep track of port value to avoid unnecessary reads
	
/* EEPROM Defines */
   #define EEPROM_BAND_DEFAULT BAND_2M

	static BOOL EEMEM ee_receiver_eeprom_initialization_flag = EEPROM_INITIALIZED_FLAG;
	static int32_t EEMEM ee_si5351_ref_correction = EEPROM_SI5351_CALIBRATION_DEFAULT;

	static uint8_t EEMEM ee_active_band = EEPROM_BAND_DEFAULT;
	static uint32_t EEMEM ee_active_2m_frequency = DEFAULT_RX_2M_FREQUENCY;
	static uint32_t EEMEM ee_active_80m_frequency = DEFAULT_RX_80M_FREQUENCY;
	static uint32_t EEMEM ee_cw_offset_frequency = DEFAULT_RX_CW_OFFSET_FREQUENCY;
	static uint8_t EEMEM ee_preamp_80m = DEFAULT_PREAMP_80M;
	static uint8_t EEMEM ee_preamp_2m = DEFAULT_PREAMP_2M;
	static uint8_t EEMEM ee_attenuation_setting = DEFAULT_ATTENUATION;

	uint32_t EEMEM ee_receiver_2m_mem1_freq = EEPROM_2M_MEM1_DEFAULT;
	uint32_t EEMEM ee_receiver_2m_mem2_freq = EEPROM_2M_MEM2_DEFAULT;
	uint32_t EEMEM ee_receiver_2m_mem3_freq = EEPROM_2M_MEM3_DEFAULT;
	uint32_t EEMEM ee_receiver_2m_mem4_freq = EEPROM_2M_MEM4_DEFAULT;
	uint32_t EEMEM ee_receiver_2m_mem5_freq = EEPROM_2M_MEM5_DEFAULT;
	uint32_t EEMEM ee_receiver_80m_mem1_freq = EEPROM_80M_MEM1_DEFAULT;
	uint32_t EEMEM ee_receiver_80m_mem2_freq = EEPROM_80M_MEM2_DEFAULT;
	uint32_t EEMEM ee_receiver_80m_mem3_freq = EEPROM_80M_MEM3_DEFAULT;
	uint32_t EEMEM ee_receiver_80m_mem4_freq = EEPROM_80M_MEM4_DEFAULT;
	uint32_t EEMEM ee_receiver_80m_mem5_freq = EEPROM_80M_MEM5_DEFAULT;

/*
 *       Local Function Prototypes
 *
 */

	void saveAllReceiverEEPROM(void);
	void initializeReceiverEEPROMVars(void);
	uint16_t potValFromAtten(uint16_t atten);

/*
 *       This function sets the VFO frequency (CLK0 of the Si5351) based on the intended receive frequency passed in by the parameter (freq),
 *       and the VFO configuration in effect. The VFO  frequency might be above or below the intended receive frequency, depending on the VFO
 *       configuration setting in effect for the radio band of the receive frequency.
 */
	BOOL rxSetFrequency(Frequency_Hz *freq)
	{
		BOOL activeBandSet = FALSE;
		Frequency_Hz vfo;
		RadioBand bandSet = BAND_INVALID;

		if((*freq < RX_MAXIMUM_80M_FREQUENCY) && (*freq > RX_MINIMUM_80M_FREQUENCY))    /* 80m */
		{
			g_freq_80m = *freq;

			if(g_vfo_configuration & VFO_2M_LOW_80M_HIGH)
			{
				vfo = RADIO_IF_FREQUENCY + *freq;
			}
			else
			{
				if(*freq > RADIO_IF_FREQUENCY)
				{
					vfo = *freq - RADIO_IF_FREQUENCY;
				}
				else
				{
					vfo = RADIO_IF_FREQUENCY - *freq;
				}
			}
			
			bandSet = BAND_80M;
		}
		else if((*freq < RX_MAXIMUM_2M_FREQUENCY) && (*freq > RX_MINIMUM_2M_FREQUENCY))
		{
			g_freq_2m = *freq;

			if(g_vfo_configuration & VFO_2M_HIGH_80M_LOW)
			{
				vfo = RADIO_IF_FREQUENCY + *freq;
			}
			else
			{
				if(*freq > RADIO_IF_FREQUENCY)
				{
					vfo = *freq - RADIO_IF_FREQUENCY;
				}
				else
				{
					vfo = RADIO_IF_FREQUENCY - *freq;
				}
			}

			bandSet = BAND_2M;
		}

		if(bandSet == BAND_INVALID)
		{
			*freq = FREQUENCY_NOT_SPECIFIED;
		}
		else if(g_activeBand == bandSet)
		{
			vfo -= g_cw_offset; // apply CW offset
			si5351_set_freq(vfo, RX_CLOCK_VFO);
			activeBandSet = TRUE;
		}

		return( activeBandSet);
	}

	Frequency_Hz rxGetFrequency(void)
	{
		if(g_rx_initialized)
		{
			if(g_activeBand == BAND_2M)
			{
				return( g_freq_2m);
			}
			else if(g_activeBand == BAND_80M)
			{
				return( g_freq_80m);
			}
		}

		return( FREQUENCY_NOT_SPECIFIED);
	}

	void rxSetVFOConfiguration(RadioVFOConfig config)
	{
		g_vfo_configuration = config;
	}

	void __attribute__((optimize("O0"))) rxSetBand(RadioBand band) 
	{
		if(band == BAND_80M)
		{
			g_activeBand = band;
			Frequency_Hz f = g_freq_80m;
			rxSetFrequency(&f);
		}
		else if(band == BAND_2M)
		{
			g_activeBand = band;
			Frequency_Hz f = g_freq_2m;
			rxSetFrequency(&f);
		}
	}

	RadioBand rxGetBand(void)
	{
		return(g_activeBand);
	}

	void init_receiver(void)
	{
		si5351_init(SI5351_CRYSTAL_LOAD_6PF, 0);

//		g_freq_2m = DEFAULT_RX_2M_FREQUENCY;
//		g_freq_80m = DEFAULT_RX_80M_FREQUENCY;
//		g_activeBand = DEFAULT_RX_ACTIVE_BAND;
		
		initializeReceiverEEPROMVars();

		g_freq_bfo = RADIO_IF_FREQUENCY;
		rxSetBand(g_activeBand);    /* also sets RX_CLOCK_VFO to VFO frequency */

		si5351_set_freq(g_freq_bfo, RX_CLOCK_BFO);
		si5351_drive_strength(RX_CLOCK_BFO, SI5351_DRIVE_2MA);
		si5351_clock_enable(RX_CLOCK_BFO, TRUE);
		
		si5351_drive_strength(RX_CLOCK_VFO, SI5351_DRIVE_2MA);
		si5351_clock_enable(RX_CLOCK_VFO, TRUE);
		
		/**
		 * Initialize port expander on receiver board */
		g_receiver_port_shadow = 0;
//		mcp23017_writePort(g_receiver_port_shadow); /* initialize receiver port expander */
		
//		rxSetAttenuation(g_attenuation_setting);
		if(g_activeBand == BAND_2M)
		{
//			rxSetPreamp(g_preamp_2m);
		}
		else
		{
//			rxSetPreamp(g_preamp_80m);
		}

		g_rx_initialized = TRUE;
	}
	
	void store_receiver_values(void)
	{
		saveAllReceiverEEPROM();
	}


	void initializeReceiverEEPROMVars(void)
	{
		if(eeprom_read_byte(&ee_receiver_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
		{
			g_activeBand = eeprom_read_byte(&ee_active_band);
			g_freq_2m = eeprom_read_dword(&ee_active_2m_frequency);
			g_freq_80m = eeprom_read_dword(&ee_active_80m_frequency);
			g_cw_offset = eeprom_read_dword(&ee_cw_offset_frequency);
			g_preamp_80m = eeprom_read_byte(&ee_preamp_80m);
			g_preamp_2m = eeprom_read_byte(&ee_preamp_2m);
			g_attenuation_setting = eeprom_read_byte(&ee_attenuation_setting);
		}
		else
		{
			eeprom_write_dword(&ee_receiver_2m_mem1_freq, EEPROM_2M_MEM1_DEFAULT);
			eeprom_write_dword(&ee_receiver_2m_mem2_freq, EEPROM_2M_MEM2_DEFAULT);
			eeprom_write_dword(&ee_receiver_2m_mem3_freq, EEPROM_2M_MEM3_DEFAULT);
			eeprom_write_dword(&ee_receiver_2m_mem4_freq, EEPROM_2M_MEM4_DEFAULT);
			eeprom_write_dword(&ee_receiver_2m_mem5_freq, EEPROM_2M_MEM5_DEFAULT);
			eeprom_write_dword(&ee_receiver_80m_mem1_freq, EEPROM_80M_MEM1_DEFAULT);
			eeprom_write_dword(&ee_receiver_80m_mem2_freq, EEPROM_80M_MEM2_DEFAULT);
			eeprom_write_dword(&ee_receiver_80m_mem3_freq, EEPROM_80M_MEM3_DEFAULT);
			eeprom_write_dword(&ee_receiver_80m_mem4_freq, EEPROM_80M_MEM4_DEFAULT);
			eeprom_write_dword(&ee_receiver_80m_mem5_freq, EEPROM_80M_MEM5_DEFAULT);
			eeprom_write_byte(&ee_receiver_eeprom_initialization_flag, EEPROM_INITIALIZED_FLAG);

			g_activeBand = EEPROM_BAND_DEFAULT;
			g_freq_2m = DEFAULT_RX_2M_FREQUENCY;
			g_freq_80m = DEFAULT_RX_80M_FREQUENCY;
			g_cw_offset = DEFAULT_RX_CW_OFFSET_FREQUENCY;
			g_preamp_80m = DEFAULT_PREAMP_80M;
			g_preamp_2m = DEFAULT_PREAMP_2M;
			g_attenuation_setting = DEFAULT_ATTENUATION;

			saveAllReceiverEEPROM();
		}
	}

	void saveAllReceiverEEPROM(void)
	{
		storeEEbyteIfChanged(&ee_active_band, g_activeBand);
		storeEEdwordIfChanged((uint32_t*)&ee_active_2m_frequency, g_freq_2m);
		storeEEdwordIfChanged((uint32_t*)&ee_active_80m_frequency, g_freq_80m);
		storeEEdwordIfChanged((uint32_t*)&ee_cw_offset_frequency, g_cw_offset);
		storeEEdwordIfChanged((uint32_t*)&ee_si5351_ref_correction, si5351_get_correction());
		storeEEbyteIfChanged(&ee_preamp_80m, g_preamp_80m);
		storeEEbyteIfChanged(&ee_preamp_2m, g_preamp_2m);
		storeEEbyteIfChanged(&ee_attenuation_setting, g_attenuation_setting);
	}


#endif  /*#ifdef INCLUDE_RECEIVER_SUPPORT */

BOOL rxSetCWOffset(Frequency_Hz offset)
{
	BOOL success = FALSE;
	
	if((offset >= 0) && (offset <= MAX_CW_OFFSET))
	{
		g_cw_offset = offset;
		rxSetBand(g_activeBand); // apply offset to currect frequency setting
		success = TRUE;
	}
	
	return success;
}

Frequency_Hz rxGetCWOffset(void)
{
	return g_cw_offset;
}

RadioBand bandForFrequency(Frequency_Hz freq)
{
	RadioBand result = BAND_INVALID;

	if((freq >= RX_MINIMUM_2M_FREQUENCY) && (freq <= RX_MAXIMUM_2M_FREQUENCY))
	{
		result = BAND_2M;
	}
	else if((freq >= RX_MINIMUM_80M_FREQUENCY) && (freq <= RX_MAXIMUM_80M_FREQUENCY))
	{
		result = BAND_80M;
	}

	return(result);
}

uint8_t rxSetAttenuation(uint8_t att)
{
	uint16_t attenuation = CLAMP(0, att, 100); 
	max5478_set_dualpotentiometer_wipers(potValFromAtten(attenuation));
	g_attenuation_setting = attenuation;
						
	if(attenuation)
	{
		g_receiver_port_shadow |= 0b00000100;
//		mcp23017_writePort(g_receiver_port_shadow); /* set receiver port expander */
	}
	else
	{
		g_receiver_port_shadow &= 0b11111011;
//		mcp23017_writePort(g_receiver_port_shadow); /* set receiver port expander */
	}
	
	return g_attenuation_setting;
}

uint8_t rxGetAttenuation(void)
{
	return g_attenuation_setting;
}
	
uint16_t potValFromAtten(uint16_t atten)
{
	uint16_t valLow = 0x00FF;
	uint16_t valHigh = 0;
		
	if(atten)
	{							
		if(atten < 23) // 0xFFF -> 0x23FF
		{
			valHigh = 0xFF00 - (atten * 0x0A00);
		}
		else if(atten < 41) // 0x23FF -> 0x00FF
		{
			valHigh = 0x2300 - (0x0200 * (atten - 23));
		}
		else // 0x00FF -> 0x0000
		{
			valLow = (255 * (100 - atten)) / 59;
		}
			
		valHigh += valLow;
	}
		
	return valHigh;
}

uint8_t rxGetPreamp(void)
{
	if(g_activeBand == BAND_2M)
	{
		return g_preamp_2m;
	}
	else
	{
		return g_preamp_80m;
	}
}

uint8_t rxSetPreamp(uint8_t setting)
{
	uint8_t result;
	
	if(g_activeBand == BAND_2M)
	{
		if(setting == 0)
		{
			g_receiver_port_shadow &= 0b11011111;
//			mcp23017_writePort(g_receiver_port_shadow); /* set receiver port expander */
		}
		else // if(setting == 1)
		{
			g_receiver_port_shadow |= 0b00100000;
//			mcp23017_writePort(g_receiver_port_shadow); /* set receiver port expander */
		}
	}
	else // if g_activeBand == BAND_80M
	{
//		dac081c_set_dac(setting);
	}
					
	if(g_activeBand == BAND_2M)
	{
		g_preamp_2m = (g_receiver_port_shadow & 0b00100000) >> 5;
		result = g_preamp_2m;
	}
	else
	{
//		g_preamp_80m = dac081c_read_dac();
		result = g_preamp_80m;
	}

	return(result);
}


