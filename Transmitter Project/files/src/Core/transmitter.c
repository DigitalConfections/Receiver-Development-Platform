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
#include "dac081c085.h" /* DAC on 80m VGA of Rev X1 Receiver board */

#ifdef INCLUDE_TRANSMITTER_SUPPORT

	static volatile BOOL g_tx_initialized = FALSE;
	static volatile Frequency_Hz g_2m_frequency = DEFAULT_TX_2M_FREQUENCY;
	static volatile Frequency_Hz g_80m_frequency = DEFAULT_TX_80M_FREQUENCY;
	static volatile uint8_t g_2m_power_level = DEFAULT_TX_2M_POWER;
	static volatile uint8_t g_80m_power_level = DEFAULT_TX_80M_POWER;
	static volatile Frequency_Hz g_rtty_offset = DEFAULT_RTTY_OFFSET_FREQUENCY;
	static volatile RadioBand g_activeBand = DEFAULT_TX_ACTIVE_BAND;
	static volatile Modulation g_2m_modulationFormat = DEFAULT_TX_2M_MODULATION;
	static volatile uint8_t g_am_drive_level = DEFAULT_AM_DRIVE_LEVEL;
	static volatile uint8_t g_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;
	
	static volatile BOOL g_transmitter_keyed = FALSE;
	
/* EEPROM Defines */
   #define EEPROM_BAND_DEFAULT BAND_2M

	static BOOL EEMEM ee_eeprom_initialization_flag = EEPROM_INITIALIZED_FLAG;
	static int32_t EEMEM ee_si5351_ref_correction = EEPROM_SI5351_CALIBRATION_DEFAULT;

	static uint8_t EEMEM ee_active_band = EEPROM_BAND_DEFAULT;
	static uint32_t EEMEM ee_active_2m_frequency = DEFAULT_TX_2M_FREQUENCY;
	static uint8_t EEMEM ee_2m_power_level = DEFAULT_TX_2M_POWER;
	static uint32_t EEMEM ee_active_80m_frequency = DEFAULT_TX_80M_FREQUENCY;
	static uint8_t EEMEM ee_80m_power_level = DEFAULT_TX_80M_POWER;
	static uint32_t EEMEM ee_cw_offset_frequency = DEFAULT_RTTY_OFFSET_FREQUENCY;
	static uint8_t EEMEM ee_am_drive_level = DEFAULT_AM_DRIVE_LEVEL;
	static uint8_t EEMEM ee_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;
	static uint8_t EEMEM ee_active_2m_modulation = DEFAULT_TX_2M_MODULATION;

/*
 *       Local Function Prototypes
 *
 */

	void saveAllTransmitterEEPROM(void);
	void initializeTransmitterEEPROMVars(void);

/*
 *       This function sets the VFO frequency (CLK0 of the Si5351) based on the intended receive frequency passed in by the parameter (freq),
 *       and the VFO configuration in effect. The VFO  frequency might be above or below the intended receive frequency, depending on the VFO
 *       configuration setting in effect for the radio band of the receive frequency.
 */
	BOOL txSetFrequency(Frequency_Hz *freq)
	{
		BOOL activeBandSet = FALSE;
		RadioBand bandSet = BAND_INVALID;

		if((*freq < TX_MAXIMUM_80M_FREQUENCY) && (*freq > TX_MINIMUM_80M_FREQUENCY))    /* 80m */
		{
			g_80m_frequency = *freq;
			bandSet = BAND_80M;
		}
		else if((*freq < TX_MAXIMUM_2M_FREQUENCY) && (*freq > TX_MINIMUM_2M_FREQUENCY)) /* 2m */
		{
			g_2m_frequency = *freq;
			bandSet = BAND_2M;
		}

		if(bandSet == BAND_INVALID)
		{
			*freq = FREQUENCY_NOT_SPECIFIED;
		}
		else if(g_activeBand == bandSet)
		{
			if(bandSet == BAND_2M)
			{
				si5351_set_freq(*freq, TX_CLOCK_VHF);
			}
			else
			{
				si5351_set_freq(*freq, TX_CLOCK_HF_0);
			}

			activeBandSet = TRUE;
		}

		return( activeBandSet);
	}

	Frequency_Hz txGetFrequency(void)
	{
		if(g_tx_initialized)
		{
			if(g_activeBand == BAND_2M)
			{
				return( g_2m_frequency);
			}
			else if(g_activeBand == BAND_80M)
			{
				return( g_80m_frequency);
			}
		}

		return( FREQUENCY_NOT_SPECIFIED);
	}

	void __attribute__((optimize("O0"))) txSetBand(RadioBand band)
	{
		keyTransmitter(OFF);
		powerToTransmitter(OFF);
	
		if(band == BAND_80M)
		{
			g_activeBand = band;
			Frequency_Hz f = g_80m_frequency;
			txSetFrequency(&f);
			txSetPowerLevel(g_80m_power_level);
		}
		else if(band == BAND_2M)
		{
			g_activeBand = band;
			Frequency_Hz f = g_2m_frequency;
			txSetFrequency(&f);
			txSetModulation(g_2m_modulationFormat);
			txSetPowerLevel(g_2m_power_level);
		}
	}

	RadioBand txGetBand(void)
	{
		return(g_activeBand);
	}
	
	void powerToTransmitter(BOOL on)
	{
		if(on)
		{
			if(g_activeBand == BAND_80M)
			{
	//			mcp23017_set(MCP23017_PORTA, VHF_ENABLE, FALSE);
				mcp23017_set(MCP23017_PORTA, VHF_ENABLE, TRUE); // temporary kluge to test 5V on 80m drivers
				mcp23017_set(MCP23017_PORTA, HF_ENABLE, TRUE);
			}
			else
			{
				mcp23017_set(MCP23017_PORTA, HF_ENABLE, FALSE);
				mcp23017_set(MCP23017_PORTA, VHF_ENABLE, TRUE);
			}
		}
		else
		{
			mcp23017_set(MCP23017_PORTA, VHF_ENABLE, FALSE);
			mcp23017_set(MCP23017_PORTA, HF_ENABLE, FALSE);
		}
	}
	
	void keyTransmitter(BOOL on)
	{
		if(on)
		{
			if(!g_transmitter_keyed)
			{
				if(g_activeBand == BAND_80M)
				{
					si5351_clock_enable(TX_CLOCK_HF_0, SI5351_CLK_ENABLED);
					si5351_clock_enable(TX_CLOCK_HF_1, SI5351_CLK_ENABLED);
				}
				else
				{
					si5351_clock_enable(TX_CLOCK_VHF, SI5351_CLK_ENABLED);
				}
			
				g_transmitter_keyed = TRUE;
			}
		}
		else if(g_transmitter_keyed)
		{
			if(g_activeBand == BAND_80M)
			{
				si5351_clock_enable(TX_CLOCK_HF_0, SI5351_CLK_DISABLED);
				si5351_clock_enable(TX_CLOCK_HF_1, SI5351_CLK_DISABLED);
			}
			else
			{
				si5351_clock_enable(TX_CLOCK_VHF, SI5351_CLK_DISABLED);
			}

			g_transmitter_keyed = FALSE;
		}
	}
	
	void txSetDrive(uint8_t drive)
	{
		if(g_activeBand != BAND_2M) return;
		
		if(g_2m_modulationFormat == MODE_AM)
		{
			g_am_drive_level = drive;
		}
		else
		{
			g_cw_drive_level = drive;
		}
		
		OCR1B = drive;
	}
	
	void txSetPowerLevel(uint8_t power)
	{
		if(g_activeBand == BAND_2M)
		{
			g_2m_power_level = power;
		}
		else
		{
			g_80m_power_level = power;
		}
		
		dac081c_set_dac(power);
						
		if(power == 0)
		{
			PORTB &= ~(1 << PORTB6); /* Turn off Tx power */
		}
		else
		{
			PORTB |= (1 << PORTB6); /* Turn on Tx power */
		}
	}

	uint8_t txGetPowerLevel(void)
	{
		uint8_t pwr = dac081c_read_dac();
		return pwr;
	}
	
	void txSetModulation(Modulation mode)
	{
		if((g_activeBand == BAND_2M) && (mode == MODE_AM))
		{
			txSetDrive(g_am_drive_level);
			g_2m_modulationFormat = MODE_AM;
		}
		else
		{
			if(g_activeBand == BAND_2M)
			{
				g_2m_modulationFormat = MODE_CW;
				txSetDrive(g_cw_drive_level);
			}
		}
	}

	BOOL init_transmitter(void)
	{
		if(si5351_init(SI5351_CRYSTAL_LOAD_6PF, 0)) return TRUE;

		initializeTransmitterEEPROMVars();

		txSetBand(g_activeBand);    /* sets most tx settings */

		si5351_drive_strength(TX_CLOCK_HF_0, SI5351_DRIVE_8MA);
		si5351_clock_enable(TX_CLOCK_HF_0, SI5351_CLK_DISABLED);

		si5351_drive_strength(TX_CLOCK_HF_1, SI5351_DRIVE_8MA);
		si5351_clock_enable(TX_CLOCK_HF_1, SI5351_CLK_DISABLED);
		
		si5351_drive_strength(TX_CLOCK_VHF, SI5351_DRIVE_8MA);
		si5351_clock_enable(TX_CLOCK_VHF, SI5351_CLK_DISABLED);
		
		g_tx_initialized = TRUE;
		
		return FALSE;
	}
	
	void storeTtransmitterValues(void)
	{
		saveAllTransmitterEEPROM();
	}


	void initializeTransmitterEEPROMVars(void)
	{
		if(eeprom_read_byte(&ee_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
		{
			g_activeBand = eeprom_read_byte(&ee_active_band);
			g_2m_frequency = eeprom_read_dword(&ee_active_2m_frequency);
			g_2m_power_level = eeprom_read_byte(&ee_2m_power_level);
			g_80m_frequency = eeprom_read_dword(&ee_active_80m_frequency);
			g_80m_power_level = eeprom_read_byte(&ee_80m_power_level);
			g_rtty_offset = eeprom_read_dword(&ee_cw_offset_frequency);
			g_am_drive_level = eeprom_read_byte(&ee_am_drive_level);
			g_cw_drive_level = eeprom_read_byte(&ee_cw_drive_level);
			g_2m_modulationFormat = eeprom_read_byte(&ee_active_2m_modulation);
		}
		else
		{
			eeprom_write_byte(&ee_eeprom_initialization_flag, EEPROM_INITIALIZED_FLAG);

			g_activeBand = EEPROM_BAND_DEFAULT;
			g_2m_frequency = DEFAULT_TX_2M_FREQUENCY;
			g_2m_power_level = DEFAULT_TX_2M_POWER;
			g_80m_frequency = DEFAULT_TX_80M_FREQUENCY;
			g_80m_power_level = DEFAULT_TX_80M_POWER;
			g_rtty_offset = DEFAULT_RTTY_OFFSET_FREQUENCY;
			g_am_drive_level = DEFAULT_AM_DRIVE_LEVEL;
			g_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;
			g_2m_modulationFormat = DEFAULT_TX_2M_MODULATION;

			saveAllTransmitterEEPROM();
		}
	}

	void saveAllTransmitterEEPROM(void)
	{
		storeEEbyteIfChanged(&ee_active_band, g_activeBand);
		storeEEdwordIfChanged((uint32_t*)&ee_active_2m_frequency, g_2m_frequency);
		storeEEbyteIfChanged(&ee_2m_power_level, g_2m_power_level);
		storeEEdwordIfChanged((uint32_t*)&ee_active_80m_frequency, g_80m_frequency);
		storeEEbyteIfChanged(&ee_80m_power_level, g_80m_power_level);
		storeEEdwordIfChanged((uint32_t*)&ee_cw_offset_frequency, g_rtty_offset);
		storeEEdwordIfChanged((uint32_t*)&ee_si5351_ref_correction, si5351_get_correction());
		storeEEbyteIfChanged(&ee_am_drive_level, g_am_drive_level);
		storeEEbyteIfChanged(&ee_cw_drive_level, g_cw_drive_level);
		storeEEbyteIfChanged(&ee_active_2m_modulation, g_2m_modulationFormat);
	}


#endif  /*#ifdef INCLUDE_TRANSMITTER_SUPPORT */

RadioBand bandForFrequency(Frequency_Hz freq)
{
	RadioBand result = BAND_INVALID;

	if((freq >= TX_MINIMUM_2M_FREQUENCY) && (freq <= TX_MAXIMUM_2M_FREQUENCY))
	{
		result = BAND_2M;
	}
	else if((freq >= TX_MINIMUM_80M_FREQUENCY) && (freq <= TX_MAXIMUM_80M_FREQUENCY))
	{
		result = BAND_80M;
	}

	return(result);
}
