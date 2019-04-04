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

#include <string.h>
#include <stdlib.h>
#include "transmitter.h"
#include "i2c.h" /* DAC on 80m VGA of Rev X1 Receiver board */

#define BUCK_9V 50

#ifdef INCLUDE_TRANSMITTER_SUPPORT

	static volatile BOOL g_tx_initialized = FALSE;
	static volatile Frequency_Hz g_2m_frequency = DEFAULT_TX_2M_FREQUENCY;
	static volatile Frequency_Hz g_80m_frequency = DEFAULT_TX_80M_FREQUENCY;
	static volatile uint8_t g_2m_power_level = DEFAULT_TX_2M_POWER;
	static volatile uint8_t g_80m_power_level = DEFAULT_TX_80M_POWER;
	static volatile Frequency_Hz g_rtty_offset = DEFAULT_RTTY_OFFSET_FREQUENCY;
	static volatile RadioBand g_activeBand = DEFAULT_TX_ACTIVE_BAND;
	static volatile Modulation g_2m_modulationFormat = DEFAULT_TX_2M_MODULATION;
	static volatile BOOL g_am_modulation_enabled = FALSE;
	static volatile uint8_t g_am_drive_level_high = DEFAULT_AM_DRIVE_LEVEL_HIGH;
	static volatile uint8_t g_am_drive_level_low = DEFAULT_AM_DRIVE_LEVEL_LOW;
	static volatile uint8_t g_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;

	static volatile BOOL g_transmitter_keyed = FALSE;

/* EEPROM Defines */
#define EEPROM_BAND_DEFAULT BAND_80M

	static BOOL EEMEM ee_eeprom_initialization_flag = EEPROM_INITIALIZED_FLAG;
	static int32_t EEMEM ee_si5351_ref_correction = EEPROM_SI5351_CALIBRATION_DEFAULT;

	static uint8_t EEMEM ee_active_band = EEPROM_BAND_DEFAULT;
	static uint32_t EEMEM ee_active_2m_frequency = DEFAULT_TX_2M_FREQUENCY;
	static uint8_t EEMEM ee_2m_power_level = DEFAULT_TX_2M_POWER;
	static uint32_t EEMEM ee_active_80m_frequency = DEFAULT_TX_80M_FREQUENCY;
	static uint8_t EEMEM ee_80m_power_level = DEFAULT_TX_80M_POWER;
	static uint32_t EEMEM ee_cw_offset_frequency = DEFAULT_RTTY_OFFSET_FREQUENCY;
	static uint8_t EEMEM ee_am_drive_level_high = DEFAULT_AM_DRIVE_LEVEL_HIGH;
	static uint8_t EEMEM ee_am_drive_level_low = DEFAULT_AM_DRIVE_LEVEL_LOW;
	static uint8_t EEMEM ee_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;
	static uint8_t EEMEM ee_active_2m_modulation = DEFAULT_TX_2M_MODULATION;
	static uint8_t EEMEM ee_80m_power_table[22] = DEFAULT_80M_POWER_TABLE;
	static uint8_t EEMEM ee_2m_am_power_table[22] = DEFAULT_2M_AM_POWER_TABLE;
	static uint8_t EEMEM ee_2m_am_drive_low_table[22] = DEFAULT_2M_AM_DRIVE_LOW_TABLE;
	static uint8_t EEMEM ee_2m_am_drive_high_table[22] = DEFAULT_2M_AM_DRIVE_HIGH_TABLE;
	static uint8_t EEMEM ee_2m_cw_power_table[22] = DEFAULT_2M_CW_POWER_TABLE;
	static uint8_t EEMEM ee_2m_cw_drive_table[22] = DEFAULT_2M_CW_DRIVE_TABLE;

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
				si5351_set_freq(*freq, TX_CLOCK_VHF, TRUE);
			}
			else
			{
				si5351_set_freq(*freq, TX_CLOCK_HF_0, TRUE);
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

	void txGetModulationLevels(uint8_t *high, uint8_t *low)
	{
		*high = (uint8_t)g_am_drive_level_high;
		*low = (uint8_t)g_am_drive_level_low;
	}

	void txSetModulationLevels(uint8_t *high, uint8_t *low)
	{
		if(g_activeBand != BAND_2M) return;

		if(high)
		{
			g_am_drive_level_high = MIN(*high, MAX_2M_AM_DRIVE_LEVEL);
			g_cw_drive_level = MIN(*high, MAX_2M_AM_DRIVE_LEVEL);
		}

		if(low)
		{
			g_am_drive_level_low = MIN(*low, MAX_2M_AM_DRIVE_LEVEL);
		}
	}


	void __attribute__((optimize("O0"))) txSetBand(RadioBand band, BOOL enable)
	{
		keyTransmitter(OFF);
		powerToTransmitter(OFF);

		if(band == BAND_80M)
		{
			g_activeBand = band;
			Frequency_Hz f = g_80m_frequency;
			txSetFrequency(&f);
			txSetPowerLevel(g_80m_power_level);
			txSetModulation(MODE_CW);
			powerToTransmitter(enable);
		}
		else if(band == BAND_2M)
		{
			g_activeBand = band;
			Frequency_Hz f = g_2m_frequency;
			txSetFrequency(&f);
			txSetModulation(g_2m_modulationFormat);
			txSetPowerLevel(g_2m_power_level);
			powerToTransmitter(enable);
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
				PORTB &= ~(1 << PORTB0); /* Turn VHF off */
				PORTB |= (1 << PORTB1); /* Turn HF on */
			}
			else
			{
				PORTB &= ~(1 << PORTB1); /* Turn HF off */
				PORTB |= (1 << PORTB0); /* Turn VHF on */
			}
		}
		else
		{
			PORTB &= ~((1 << PORTB0) | (1 << PORTB1)); /* Turn off both bands */
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
					PORTD |= (1 << PORTD4);
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
				PORTD &= ~(1 << PORTD4);
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

	BOOL txSetPowerLevel(uint8_t power)
	{
		BOOL err;

		// Prevent possible damage to transmitter
		if(g_activeBand == BAND_2M)
		{
			g_2m_power_level = MIN(power, MAX_2M_PWR_SETTING);
			power = g_2m_power_level;
			// TODO: Set modulation settings for appropriate power level
			err = dac081c_set_dac(BUCK_9V, PA_DAC); /* set to 9V for the MAAP-011232 */
			err |= dac081c_set_dac(power, BIAS_DAC); /* set negative bias for correct power output */
		}
		else
		{
			g_80m_power_level = MIN(power, MAX_80M_PWR_SETTING);
			power = g_80m_power_level;
			err = dac081c_set_dac(power, PA_DAC);
		}


		if(err || (power == 0))
		{
			PORTB &= ~(1 << PORTB6); /* Turn off Tx power */
		}
		else
		{
			PORTB |= (1 << PORTB6); /* Turn on Tx power */
		}

		return err;
	}

	uint8_t txGetPowerLevel(void)
	{
		uint8_t pwr;
		while(dac081c_read_dac(&pwr, PA_DAC));
		return pwr;
	}

	void txSetModulation(Modulation mode)
	{
		if((g_activeBand == BAND_2M) && (mode == MODE_AM))
		{
			g_2m_modulationFormat = MODE_AM;
			txSetModulationLevels((uint8_t*)&g_am_drive_level_high, (uint8_t*)&g_am_drive_level_low);
			g_am_modulation_enabled = TRUE;
		}
		else
		{
			g_am_modulation_enabled = FALSE;
			if(g_activeBand == BAND_2M) g_2m_modulationFormat = MODE_CW;
			txSetModulationLevels((uint8_t*)&g_cw_drive_level, NULL);
		}
	}

	Modulation txGetModulation(void)
	{
		if (g_activeBand == BAND_2M)
		{
			return g_2m_modulationFormat;
		}

		return MODE_INVALID;
	}

	BOOL txAMModulationEnabled(void)
	{
		return g_am_modulation_enabled;
	}

	BOOL init_transmitter(void)
	{
		if(si5351_init(SI5351_CRYSTAL_LOAD_6PF, 0)) return TRUE;

		initializeTransmitterEEPROMVars();

		txSetBand(g_activeBand, OFF);    /* sets most tx settings leaving power to transmitter OFF */
		if(txSetPowerLevel(0)) return TRUE;

		if(si5351_drive_strength(TX_CLOCK_HF_0, SI5351_DRIVE_8MA)) return TRUE;
		if(si5351_clock_enable(TX_CLOCK_HF_0, SI5351_CLK_DISABLED)) return TRUE;

		if(si5351_drive_strength(TX_CLOCK_HF_1, SI5351_DRIVE_8MA)) return TRUE;
		if(si5351_clock_enable(TX_CLOCK_HF_1, SI5351_CLK_DISABLED)) return TRUE;

		if(si5351_drive_strength(TX_CLOCK_VHF, SI5351_DRIVE_8MA)) return TRUE;
		if(si5351_clock_enable(TX_CLOCK_VHF, SI5351_CLK_DISABLED)) return TRUE;

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
			g_am_drive_level_high = eeprom_read_byte(&ee_am_drive_level_high);
			g_am_drive_level_low = eeprom_read_byte(&ee_am_drive_level_low);
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
			g_am_drive_level_high = DEFAULT_AM_DRIVE_LEVEL_HIGH;
			g_am_drive_level_low = DEFAULT_AM_DRIVE_LEVEL_LOW;
			g_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;
			g_2m_modulationFormat = DEFAULT_TX_2M_MODULATION;

			saveAllTransmitterEEPROM();
		}
	}

	void saveAllTransmitterEEPROM(void)
	{
		uint8_t table[22];
		eeprom_update_byte(&ee_active_band, g_activeBand);
		eeprom_update_dword((uint32_t*)&ee_active_2m_frequency, g_2m_frequency);
		eeprom_update_byte(&ee_2m_power_level, g_2m_power_level);
		eeprom_update_dword((uint32_t*)&ee_active_80m_frequency, g_80m_frequency);
		eeprom_update_byte(&ee_80m_power_level, g_80m_power_level);
		eeprom_update_dword((uint32_t*)&ee_cw_offset_frequency, g_rtty_offset);
		eeprom_update_dword((uint32_t*)&ee_si5351_ref_correction, si5351_get_correction());
		eeprom_update_byte(&ee_am_drive_level_high, g_am_drive_level_high);
		eeprom_update_byte(&ee_am_drive_level_high, g_am_drive_level_low);
		eeprom_update_byte(&ee_cw_drive_level, g_cw_drive_level);
		eeprom_update_byte(&ee_active_2m_modulation, g_2m_modulationFormat);
		memcpy(table, DEFAULT_80M_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_80m_power_table, sizeof(table));
		memcpy(table, DEFAULT_2M_AM_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_am_power_table, sizeof(table));
		memcpy(table, DEFAULT_2M_AM_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_am_drive_high_table, sizeof(table));
		memcpy(table, DEFAULT_2M_AM_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_am_drive_low_table, sizeof(table));
		memcpy(table, DEFAULT_2M_AM_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_cw_power_table, sizeof(table));
		memcpy(table, DEFAULT_2M_AM_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_cw_drive_table, sizeof(table));
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

BOOL txMilliwattsToSettings(uint16_t powerMW, uint8_t* powerLevel, uint8_t* modLevelHigh, uint8_t* modLevelLow)
{
	RadioBand band = txGetBand();

	if(band == BAND_80M)
	{
		powerMW = CLAMP(0, powerMW, MAX_TX_POWER_80M_MW);
	}
	else
	{
		powerMW = CLAMP(0, powerMW, MAX_TX_POWER_2M_MW);
	}

	if(powerMW)
	{
		if(powerMW > 99)
		{
			powerMW /= 100;
			powerMW++;
		}
		else
		{
			powerMW /= 10;
			if(powerMW > 2)
			{
				powerMW = 1;
			}
			else
			{
				powerMW = 0;
			}
		}

		if(band == BAND_80M)
		{
			*powerLevel = eeprom_read_byte(&ee_80m_power_table[powerMW]);
			*modLevelHigh = 0;
			*modLevelLow = 0;
		}
		else
		{
			if(txGetModulation() == MODE_AM)
			{
				*powerLevel = eeprom_read_byte(&ee_2m_am_power_table[powerMW]);
				*modLevelHigh = eeprom_read_byte(&ee_2m_am_drive_high_table[powerMW]);
				*modLevelLow = eeprom_read_byte(&ee_2m_am_drive_low_table[powerMW]);
			}
			else
			{
				*powerLevel = eeprom_read_byte(&ee_2m_cw_power_table[powerMW]);
				*modLevelHigh = eeprom_read_byte(&ee_2m_cw_drive_table[powerMW]);
				*modLevelLow = *modLevelHigh;
			}
		}
	}
	else
	{
		*powerLevel = 0;
		*modLevelHigh = 0;
		*modLevelLow = 0;
	}

	return FALSE;
}
