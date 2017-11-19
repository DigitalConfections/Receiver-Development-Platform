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
#include "receiver.h"
#include "si5351.h"

#ifdef INCLUDE_RECEIVER_SUPPORT

	static volatile BOOL g_rx_initialized = FALSE;
	static volatile Frequency_Hz g_freq_2m = DEFAULT_RX_2M_FREQUENCY;
	static volatile Frequency_Hz g_freq_80m = DEFAULT_RX_80M_FREQUENCY;
	static volatile Frequency_Hz g_freq_bfo = RADIO_IF_FREQUENCY;
	static volatile RadioVFOConfig g_vfo_configuration = VFO_2M_LOW_80M_HIGH;
	static volatile RadioBand g_activeBand = DEFAULT_RX_ACTIVE_BAND;

/* EEPROM Defines */
   #define EEPROM_BAND_DEFAULT BAND_2M

	static BOOL EEMEM ee_receiver_eeprom_initialization_flag = EEPROM_INITIALIZED_FLAG;
	static int32_t EEMEM ee_si5351_ref_correction = EEPROM_SI5351_CALIBRATION_DEFAULT;

	static uint8_t EEMEM ee_active_band = EEPROM_BAND_DEFAULT;

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
				vfo = labs(*freq - RADIO_IF_FREQUENCY);
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
				vfo = labs(*freq - RADIO_IF_FREQUENCY);
			}

			bandSet = BAND_2M;
		}

		if(bandSet == BAND_INVALID)
		{
			*freq = FREQUENCY_NOT_SPECIFIED;
		}
		else if(g_activeBand == bandSet)
		{
			si5351_set_freq(vfo, SI5351_CLK0);
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

	void rxSetBand(RadioBand band) 
	{
		if(band == BAND_80M)
		{
			g_activeBand = band;
			PORTD |= (1 << PORTD7);
			Frequency_Hz f = g_freq_80m;
			rxSetFrequency(&f);
		}
		else if(band == BAND_2M)
		{
			g_activeBand = band;
			PORTD &= ~(1 << PORTD7);
			Frequency_Hz f = g_freq_80m;
			rxSetFrequency(&f);
		}
	}

	RadioBand rxGetBand(void)
	{
		return(g_activeBand);
	}

	void init_receiver(Receiver* rx)
	{
		si5351_init(SI5351_CRYSTAL_LOAD_6PF, 0);

		g_freq_2m = DEFAULT_RX_2M_FREQUENCY;
		g_freq_80m = DEFAULT_RX_80M_FREQUENCY;
		g_activeBand = DEFAULT_RX_ACTIVE_BAND;

		if(rx)
		{
			rx->bandSetting = g_activeBand;
		}

		g_freq_bfo = RADIO_IF_FREQUENCY;
		rxSetBand(g_activeBand);    /* also sets Si5351 CLK0 to VFO frequency */

		si5351_set_freq(g_freq_bfo, SI5351_CLK2);
		si5351_drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
		si5351_clock_enable(SI5351_CLK0, TRUE);
		si5351_drive_strength(SI5351_CLK2, SI5351_DRIVE_8MA);
		si5351_clock_enable(SI5351_CLK2, TRUE);
		g_rx_initialized = TRUE;
	}

	void initializeReceiverEEPROMVars(void)
	{
		if(eeprom_read_byte(&ee_receiver_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
		{
			g_activeBand = eeprom_read_byte(&ee_active_band);
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

			saveAllReceiverEEPROM();
		}
	}

	void saveAllReceiverEEPROM(void)
	{
		storeEEbyteIfChanged(&ee_active_band, g_activeBand);
		storeEEdwordIfChanged((uint32_t*)&ee_si5351_ref_correction, si5351_get_correction());
	}


#endif  /*#ifdef INCLUDE_RECEIVER_SUPPORT */

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


