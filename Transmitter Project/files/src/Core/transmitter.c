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
#include "i2c.h"    /* DAC on 80m VGA of Rev X1 Receiver board */

#ifdef INCLUDE_TRANSMITTER_SUPPORT

	extern volatile AntConnType g_antenna_connect_state;
	extern uint8_t g_mod_up;
	extern uint8_t g_mod_down;
	extern volatile BatteryType g_battery_type;
	EC (*g_txTask)(BiasStateMachineCommand* smCommand) = NULL; /* allows the transmitter to specify functions to run in the foreground */
//	extern EC (*g_txTask)(BiasStateMachineCommand* smCommand);  /* allow the transmitter to specify functions to run in the foreground */
	static volatile uint8_t g_new_2m_power_DAC_setting = 0;
	static volatile uint16_t g_new_power_level_mW = 0;
	static volatile RadioBand g_new_band_setting = BAND_INVALID;
	static volatile BOOL g_new_AM_mod_setting = FALSE;
	static volatile BOOL g_new_power_enable_setting = FALSE;
	static volatile BOOL g_new_2m_power_level_received = FALSE;
	static volatile BOOL g_new_parameters_received = FALSE;

	static volatile BOOL g_tx_initialized = FALSE;
	static volatile Frequency_Hz g_2m_frequency = DEFAULT_TX_2M_FREQUENCY;
	static volatile Frequency_Hz g_80m_frequency = DEFAULT_TX_80M_FREQUENCY;
	static volatile uint16_t g_2m_power_level_mW = DEFAULT_TX_2M_POWER_MW;
	static volatile uint16_t g_80m_power_level_mW = DEFAULT_TX_80M_POWER_MW;
	static volatile Frequency_Hz g_rtty_offset = DEFAULT_RTTY_OFFSET_FREQUENCY;
	static volatile RadioBand g_activeBand = DEFAULT_TX_ACTIVE_BAND;
	static volatile Modulation g_2m_modulationFormat = DEFAULT_TX_2M_MODULATION;
	volatile BOOL g_am_modulation_enabled = FALSE;
	static volatile uint8_t g_am_drive_level_high = DEFAULT_AM_DRIVE_LEVEL_HIGH;
	static volatile uint8_t g_am_drive_level_low = DEFAULT_AM_DRIVE_LEVEL_LOW;
//	static volatile uint8_t g_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;

	static volatile BOOL g_transmitter_keyed = FALSE;

/* EEPROM Defines */
#define EEPROM_BAND_DEFAULT BAND_80M

	static BOOL EEMEM ee_eeprom_initialization_flag = EEPROM_INITIALIZED_FLAG;
	static int32_t EEMEM ee_si5351_ref_correction = EEPROM_SI5351_CALIBRATION_DEFAULT;

	static uint8_t EEMEM ee_active_band = EEPROM_BAND_DEFAULT;
	static uint32_t EEMEM ee_active_2m_frequency = DEFAULT_TX_2M_FREQUENCY;
	static uint16_t EEMEM ee_2m_power_level_mW = DEFAULT_TX_2M_POWER_MW;
	static uint32_t EEMEM ee_active_80m_frequency = DEFAULT_TX_80M_FREQUENCY;
	static uint16_t EEMEM ee_80m_power_level_mW = DEFAULT_TX_80M_POWER_MW;
	static uint32_t EEMEM ee_cw_offset_frequency = DEFAULT_RTTY_OFFSET_FREQUENCY;
	static uint8_t EEMEM ee_am_drive_level_high = DEFAULT_AM_DRIVE_LEVEL_HIGH;
	static uint8_t EEMEM ee_am_drive_level_low = DEFAULT_AM_DRIVE_LEVEL_LOW;
//	static uint8_t EEMEM ee_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;
	static uint8_t EEMEM ee_active_2m_modulation = DEFAULT_TX_2M_MODULATION;
	static uint8_t EEMEM ee_80m_power_table[16] = DEFAULT_80M_POWER_TABLE;
	static uint8_t EEMEM ee_2m_am_power_table[16] = DEFAULT_2M_AM_POWER_TABLE;
	static uint8_t EEMEM ee_2m_am_drive_low_table[16] = DEFAULT_2M_AM_DRIVE_LOW_TABLE;
	static uint8_t EEMEM ee_2m_am_drive_high_table[16] = DEFAULT_2M_AM_DRIVE_HIGH_TABLE;
	static uint8_t EEMEM ee_2m_cw_power_table[16] = DEFAULT_2M_CW_POWER_TABLE;
	static uint8_t EEMEM ee_2m_cw_drive_table[16] = DEFAULT_2M_CW_DRIVE_TABLE;

/*
 *       Local Function Prototypes
 *
 */
	/**
	 */
	void saveAllTransmitterEEPROM(void);

	/**
	 */
	void txGet2mModulationLevels(uint8_t *high, uint8_t *low);

	/**
	 */
	EC tx2mBiasStateMachine(BiasStateMachineCommand* smCommand);

/*
 *       This function sets the VFO frequency (CLK0 of the Si5351) based on the intended frequency passed in by the parameter (freq),
 *       and the VFO configuration in effect. The VFO  frequency might be above or below the intended  frequency, depending on the VFO
 *       configuration setting in effect for the radio band of the frequency.
 */
	BOOL txSetFrequency(Frequency_Hz *freq, BOOL leaveClockOff)
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
				si5351_set_freq(*freq, TX_CLOCK_VHF, leaveClockOff);
			}
			else
			{
				si5351_set_freq(*freq, TX_CLOCK_HF_0, leaveClockOff);
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

	void txGet2mModulationLevels(uint8_t *high, uint8_t *low)
	{
		if(g_activeBand != BAND_2M)
		{
			return;
		}

		if(high)
		{
			g_am_drive_level_high = MIN(*high, MAX_2M_AM_DRIVE_LEVEL);
//			g_cw_drive_level = MIN(*high, MAX_2M_AM_DRIVE_LEVEL);
			g_mod_up = g_am_drive_level_high;
		}

		if(low)
		{
			g_am_drive_level_low = MIN(*low, MAX_2M_AM_DRIVE_LEVEL);
			g_mod_down = g_am_drive_level_low;
		}
	}


	RadioBand txGetBand(void)
	{
		return(g_activeBand);
	}

	BOOL powerToTransmitterDriver(BOOL on)
	{
		if(on)
		{
			if(!txIsAntennaForBand())
			{
				return( TRUE);
			}

			if(g_activeBand == BAND_80M)
			{
				PORTB &= ~(1 << PORTB0);    /* Turn VHF off */
				PORTB |= (1 << PORTB1);     /* Turn HF on */
			}
			else
			{
				PORTB &= ~(1 << PORTB1);    /* Turn HF off */
				PORTB |= (1 << PORTB0);     /* Turn VHF on */
			}
		}
		else
		{
			PORTB &= ~((1 << PORTB0) | (1 << PORTB1));  /* Turn off both bands */
		}

		return( FALSE);
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
				}
				else
				{
					si5351_clock_enable(TX_CLOCK_VHF, SI5351_CLK_ENABLED);
				}

				g_transmitter_keyed = TRUE;
			}
		}
		else
		{
			if(g_activeBand == BAND_80M)
			{
				si5351_clock_enable(TX_CLOCK_HF_0, SI5351_CLK_DISABLED);
			}
			else
			{
				si5351_clock_enable(TX_CLOCK_VHF, SI5351_CLK_DISABLED);
			}

			g_transmitter_keyed = FALSE;
		}
	}

	EC __attribute__((optimize("O0"))) txSetParameters(uint16_t* power_mW, RadioBand* band, BOOL* enableAM, BOOL* enableDriverPwr)
/*	EC txSetParameters(uint16_t* power_mW, RadioBand* band, BOOL* enableAM, BOOL* enableDriverPwr) */
	{
		BOOL err = FALSE;
		EC code = ERROR_CODE_NO_ERROR;

		BOOL isNewBand = FALSE;

		if(band != NULL)
		{
			/* Handle Band Setting */
			if(g_activeBand != *band)
			{
				isNewBand = TRUE;
				keyTransmitter(OFF);
				powerToTransmitterDriver(OFF);

				if(power_mW == NULL)
				{
					power_mW = (uint16_t*)g_80m_power_level_mW;
				}

				if(*band == BAND_80M)
				{
					if(power_mW == NULL)
					{
						power_mW = (uint16_t*)g_80m_power_level_mW;
					}
					g_new_AM_mod_setting = FALSE;
					g_new_power_enable_setting = TRUE;

					if(g_activeBand == BAND_2M) /* Must power off 2m before setting new band */
					{
						g_new_parameters_received = TRUE;
						g_new_2m_power_DAC_setting = 0;
						g_new_power_level_mW = *power_mW;
						g_new_band_setting = BAND_80M;

						if(enableAM != NULL)
						{
							g_new_AM_mod_setting = *enableAM;
						}

						if(enableDriverPwr)
						{
							g_new_power_enable_setting = *enableDriverPwr;
						}

						BiasStateMachineCommand bsmc = BIAS_SM_STABILITY_CHECK;
						code = tx2mBiasStateMachine(&bsmc);

						if(code == ERROR_CODE_NO_ERROR)
						{
							g_activeBand = BAND_INVALID;    /* prevent this branch from executing next time */
							g_txTask = tx2mBiasStateMachine;
						}

						return( code);
					}
				}
				else if(*band == BAND_2M)
				{
					g_activeBand = *band;
					Frequency_Hz f = g_2m_frequency;
					txSetFrequency(&f, TRUE);

					if(!enableAM)
					{
						enableAM = (BOOL*)&g_am_modulation_enabled;
					}
				}
			}
		}

		if(enableAM != NULL)
		{
			/* Handle Modulation Setting */
			if(isNewBand || (*enableAM != g_am_modulation_enabled))
			{
				if((g_activeBand == BAND_2M) && *enableAM)
				{
					g_2m_modulationFormat = MODE_AM;
					g_am_modulation_enabled = TRUE;
					if(power_mW == NULL)
					{
						power_mW = (uint16_t*)&g_2m_power_level_mW;
					}
				}
				else
				{
					g_am_modulation_enabled = FALSE;
					if(g_activeBand == BAND_2M)
					{
						g_2m_modulationFormat = MODE_CW;
						g_am_modulation_enabled = FALSE;
						if(power_mW == NULL)
						{
							power_mW = (uint16_t*)&g_2m_power_level_mW;
						}
					}
					else if(power_mW == NULL)
					{
						power_mW = (uint16_t*)&g_80m_power_level_mW;
					}
				}
			}
		}

		if(power_mW != NULL)
		{
			if(*power_mW <= MAX_TX_POWER_80M_MW)
			{
				if(!txIsAntennaForBand())   /* no antenna attached */
				{
					if(*power_mW > 0)
					{
						code = ERROR_CODE_NO_ANTENNA_PREVENTS_POWER_SETTING;
						err = TRUE;
					}
				}

				if(!err)
				{
					uint8_t biasDAC, modLevelHigh, modLevelLow;
					code = txMilliwattsToSettings(power_mW, &biasDAC, &modLevelHigh, &modLevelLow);
					err = (code == ERROR_CODE_SW_LOGIC_ERROR);

					/* Prevent possible damage to transmitter */
					if(g_activeBand == BAND_2M)
					{
						if(g_new_2m_power_level_received)  /* check to see if last power level change completed */
						{
							code = ERROR_CODE_2M_BIAS_SM_NOT_READY;
							err = TRUE;
						}
						else
						{
							txGet2mModulationLevels(&modLevelHigh, &modLevelLow);

							g_new_power_level_mW = *power_mW;
							g_new_2m_power_DAC_setting = biasDAC;

							BiasStateMachineCommand smc = BIAS_SM_STABILITY_CHECK;
							code = tx2mBiasStateMachine(&smc);

							if(code == ERROR_CODE_NO_ERROR)
							{
								g_new_2m_power_level_received = TRUE;
								g_txTask = tx2mBiasStateMachine;
							}

							if(g_2m_modulationFormat == MODE_CW)
							{
								err = dac081c_set_dac(modLevelHigh, AM_DAC);
								if(err)
								{
									code = ERROR_CODE_DAC2_NONRESPONSIVE;
								}
							}
						}
					}
					else
					{
						g_80m_power_level_mW = *power_mW;
						err = dac081c_set_dac(biasDAC, PA_DAC);
						if(err)
						{
							code = ERROR_CODE_DAC1_NONRESPONSIVE;
						}

						if(err || (biasDAC == 0))
						{
							PORTB &= ~(1 << PORTB6);    /* Turn off Tx power */
						}
						else
						{
							PORTB |= (1 << PORTB6);     /* Turn on Tx power */
						}
					}
				}
			}
		}

		if(!err)
		{
			if(enableDriverPwr != NULL) powerToTransmitterDriver(*enableDriverPwr);
		}

		return(code);
	}


	Modulation txGetModulation(void)
	{
		if(g_activeBand == BAND_2M)
		{
			return( g_2m_modulationFormat);
		}

		return( MODE_INVALID);
	}

	BOOL txAMModulationEnabled(void)
	{
		return( g_am_modulation_enabled);
	}

	EC init_transmitter(void)
	{
		EC code;
		uint16_t pwr = 0;

		if((code = si5351_init(SI5351_CRYSTAL_LOAD_6PF, 0)))
		{
			return( code);
		}

		initializeTransmitterEEPROMVars();

		if((code = txSetParameters(&pwr, (RadioBand*)&g_activeBand, NULL, NULL)))
		{
			return( code);
		}

		if((code = si5351_drive_strength(TX_CLOCK_HF_0, SI5351_DRIVE_8MA)))
		{
			return( code);
		}
		if((code = si5351_clock_enable(TX_CLOCK_HF_0, SI5351_CLK_DISABLED)))
		{
			return( code);
		}

		if((code = si5351_drive_strength(TX_CLOCK_VHF, SI5351_DRIVE_8MA)))
		{
			return( code);
		}
		if((code = si5351_clock_enable(TX_CLOCK_VHF, SI5351_CLK_DISABLED)))
		{
			return( code);
		}

		if((code = si5351_drive_strength(TX_CLOCK_VHF_FM, SI5351_DRIVE_8MA)))
		{
			return( code);
		}
		if((code = si5351_clock_enable(TX_CLOCK_VHF_FM, SI5351_CLK_DISABLED)))
		{
			return( code);
		}

		BiasStateMachineCommand bsmc = BIAS_SM_COMMAND_INIT;
		code = tx2mBiasStateMachine(&bsmc);

		g_tx_initialized = TRUE;

		return( code);
	}

	void storeTransmitterValues(void)
	{
		saveAllTransmitterEEPROM();
	}


	void initializeTransmitterEEPROMVars(void)
	{
		if(eeprom_read_byte(&ee_eeprom_initialization_flag) == EEPROM_INITIALIZED_FLAG)
		{
			g_activeBand = eeprom_read_byte(&ee_active_band);
			g_2m_frequency = eeprom_read_dword(&ee_active_2m_frequency);
			g_2m_power_level_mW = eeprom_read_word(&ee_2m_power_level_mW);
			g_80m_frequency = eeprom_read_dword(&ee_active_80m_frequency);
			g_80m_power_level_mW = eeprom_read_word(&ee_80m_power_level_mW);
			g_rtty_offset = eeprom_read_dword(&ee_cw_offset_frequency);
			g_am_drive_level_high = eeprom_read_byte(&ee_am_drive_level_high);
			g_am_drive_level_low = eeprom_read_byte(&ee_am_drive_level_low);
//			g_cw_drive_level = eeprom_read_byte(&ee_cw_drive_level);
			g_2m_modulationFormat = eeprom_read_byte(&ee_active_2m_modulation);
		}
		else
		{
			eeprom_write_byte(&ee_eeprom_initialization_flag, EEPROM_INITIALIZED_FLAG);

			g_activeBand = EEPROM_BAND_DEFAULT;
			g_2m_frequency = DEFAULT_TX_2M_FREQUENCY;
			g_2m_power_level_mW = DEFAULT_TX_2M_POWER_MW;
			g_80m_frequency = DEFAULT_TX_80M_FREQUENCY;
			g_80m_power_level_mW = DEFAULT_TX_80M_POWER_MW;
			g_rtty_offset = DEFAULT_RTTY_OFFSET_FREQUENCY;
			g_am_drive_level_high = DEFAULT_AM_DRIVE_LEVEL_HIGH;
			g_am_drive_level_low = DEFAULT_AM_DRIVE_LEVEL_LOW;
//			g_cw_drive_level = DEFAULT_CW_DRIVE_LEVEL;
			g_2m_modulationFormat = DEFAULT_TX_2M_MODULATION;

			saveAllTransmitterEEPROM();
		}
	}

	void saveAllTransmitterEEPROM(void)
	{
		uint8_t table[22];

		eeprom_update_byte(&ee_active_band, g_activeBand);
		eeprom_update_dword((uint32_t*)&ee_active_2m_frequency, g_2m_frequency);
		eeprom_update_word(&ee_2m_power_level_mW, g_2m_power_level_mW);
		eeprom_update_dword((uint32_t*)&ee_active_80m_frequency, g_80m_frequency);
		eeprom_update_word(&ee_80m_power_level_mW, g_80m_power_level_mW);
		eeprom_update_dword((uint32_t*)&ee_cw_offset_frequency, g_rtty_offset);
		eeprom_update_dword((uint32_t*)&ee_si5351_ref_correction, si5351_get_correction());
		eeprom_update_byte(&ee_am_drive_level_high, g_am_drive_level_high);
		eeprom_update_byte(&ee_am_drive_level_high, g_am_drive_level_low);
//		eeprom_update_byte(&ee_cw_drive_level, g_cw_drive_level);
		eeprom_update_byte(&ee_active_2m_modulation, g_2m_modulationFormat);
		memcpy(table, DEFAULT_80M_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_80m_power_table, sizeof(table));
		memcpy(table, DEFAULT_2M_AM_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_am_power_table, sizeof(table));
		memcpy(table, DEFAULT_2M_AM_DRIVE_HIGH_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_am_drive_high_table, sizeof(table));
		memcpy(table, DEFAULT_2M_AM_DRIVE_LOW_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_am_drive_low_table, sizeof(table));
		memcpy(table, DEFAULT_2M_CW_POWER_TABLE, sizeof(table));
		eeprom_write_block(table, ee_2m_cw_power_table, sizeof(table));
		memcpy(table, DEFAULT_2M_CW_DRIVE_TABLE, sizeof(table));
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

EC txMilliwattsToSettings(uint16_t* powerMW, uint8_t* driveLevel, uint8_t* modLevelHigh, uint8_t* modLevelLow)
{
	EC ec = ERROR_CODE_NO_ERROR;
	RadioBand band = txGetBand();
	uint16_t maxPwr;
	uint8_t index;

	if(powerMW == NULL) return(ERROR_CODE_SW_LOGIC_ERROR);

	if(band == BAND_80M)
	{
		if(g_battery_type == BATTERY_4r2V)
		{
			maxPwr = MAX_TX_POWER_80M_4r2V_MW;
		}
		else
		{
			maxPwr = MAX_TX_POWER_80M_MW;
		}
	}
	else
	{
		if(g_battery_type == BATTERY_4r2V)
		{
			maxPwr = MAX_TX_POWER_2M_4r2V_MW;
		}
		else
		{
			maxPwr = MAX_TX_POWER_2M_MW;
		}
	}

	if(*powerMW > maxPwr) ec = ERROR_CODE_POWER_LEVEL_NOT_SUPPORTED;

	*powerMW = CLAMP(0, *powerMW, maxPwr);

	if(*powerMW < 5)
	{
		index = 0;
		*powerMW = 0;
	}
	else if(*powerMW < 50)
	{
		index = 1;
		*powerMW = 10;
	}
	else if(*powerMW < 150)
	{
		index = 2;
		*powerMW = 100;
	}
	else if(*powerMW < 250)
	{
		index = 3;
		*powerMW = 200;
	}
	else if(*powerMW < 350)
	{
		index = 4;
		*powerMW = 300;
	}
	else if(*powerMW < 450)
	{
		index = 5;
		*powerMW = 400;
	}
	else if(*powerMW < 550)
	{
		index = 6;
		*powerMW = 500;
	}
	else if(*powerMW < 650)
	{
		index = 7;
		*powerMW = 600;
	}
	else if(*powerMW < 900)
	{
		index = 8;
		*powerMW = 800;
	}
	else if(*powerMW < 1250)
	{
		index = 9;
		*powerMW = 1000;
	}
	else if(*powerMW < 1750)
	{
		index = 10;
		*powerMW = 1500;
	}
	else if(*powerMW < 2250)
	{
		index = 11;
		*powerMW = 2000;
	}
	else if(*powerMW < 2750)
	{
		index = 12;
		*powerMW = 2500;
	}
	else if(*powerMW < 3500)
	{
		index = 13;
		*powerMW = 3000;
	}
	else if(*powerMW < 4500)
	{
		index = 14;
		*powerMW = 4000;
	}
	else
	{
		index = 15;
		*powerMW = 5000;
	}

	if(band == BAND_80M)
	{
		*driveLevel = eeprom_read_byte(&ee_80m_power_table[index]);
		*modLevelHigh = 0;
		*modLevelLow = 0;
		*driveLevel = MIN(*driveLevel, MAX_80M_PWR_SETTING);
	}
	else
	{
		if(g_2m_modulationFormat == MODE_AM)
		{
			*driveLevel = eeprom_read_byte(&ee_2m_am_power_table[index]);
			*modLevelHigh = eeprom_read_byte(&ee_2m_am_drive_high_table[index]);
			*modLevelLow = eeprom_read_byte(&ee_2m_am_drive_low_table[index]);
		}
		else
		{
			*driveLevel = eeprom_read_byte(&ee_2m_cw_power_table[index]);
			*modLevelHigh = eeprom_read_byte(&ee_2m_cw_drive_table[index]);
			*modLevelLow = *modLevelHigh;
		}

		*driveLevel = MAX(*driveLevel, MIN_2M_BIAS_SETTING);
	}

	return(ec);
}

/**
 */
BOOL txIsAntennaForBand(void)
{
	BOOL result = FALSE;
	RadioBand b = txGetBand();

	if(b == BAND_INVALID) return result;

	switch(g_antenna_connect_state)
	{
		case ANT_2M_AND_80M_CONNECTED:
		{
			result = TRUE;
		}
		break;

		case ANT_80M_CONNECTED:
		{
			result = (b == BAND_80M);
		}
		break;

		case ANT_2M_CONNECTED:
		{
			result = (b == BAND_2M);
		}
		break;

		default:
		break;
	}

	return result;
}


BOOL txSleeping(BOOL enableSleep)
{
	BiasStateMachineCommand bsmc = BIAS_SM_IS_POWER_OFF;
	EC ec = tx2mBiasStateMachine(&bsmc);

	if(ec == ERROR_CODE_NO_ERROR)
	{
		uint8_t val;

		if(!dac081c_read_dac(&val, BIAS_DAC)) // ensure the DAC is set to zero
		{
			if(!val) return TRUE;
		}
	}
	else if(enableSleep)
	{
		g_new_2m_power_DAC_setting = 0;
		g_new_2m_power_level_received = TRUE;
		g_txTask = tx2mBiasStateMachine;
	}

	return FALSE;
}


/**
 *  State machine for setting 2m Power Amplifier bias.
 *  Note: This function is executed by the TIMER2_COMPB interrupt service routine.
 */
//EC __attribute__((optimize("O0"))) tx2mBiasStateMachine(BiasStateMachineCommand* smCommand)
EC tx2mBiasStateMachine(BiasStateMachineCommand* smCommand)
{
	static volatile BiasState bias_state = BIAS_POWERED_OFF;
	static volatile BOOL smFailed = FALSE;
	static volatile uint8_t hold_new_bias_DAC_setting = 0;
	static volatile uint16_t hold_new_power_setting_MW = 0;
	EC ec = ERROR_CODE_NO_ERROR;

	if(smCommand || smFailed)
	{
		if(smFailed)
		{
			dac081c_set_dac(BIAS_MINUS_2V, BIAS_DAC);   /* set negative bias for safety */
			PORTB &= ~(1 << PORTB6);                    /* Turn off Tx power */
			dac081c_set_dac(BUCK_0V, PA_DAC);
			bias_state = BIAS_POWERED_OFF;
		}

		if(*smCommand == BIAS_SM_COMMAND_INIT)
		{
			smFailed = FALSE;
			bias_state = BIAS_POWERED_OFF;
		}
		else if(*smCommand == BIAS_SM_STABILITY_CHECK)
		{
			if(!((bias_state == BIAS_POWERED_OFF) || (bias_state == BIAS_POWERED_UP)))
			{
				ec = ERROR_CODE_2M_BIAS_SM_NOT_READY;
			}
		}
		else if(*smCommand == BIAS_SM_IS_POWER_OFF)
		{
			if(bias_state != BIAS_POWERED_OFF)
			{
				ec = ERROR_CODE_2M_BIAS_SM_NOT_READY;
			}
		}
	}
	else
	{
		if(bias_state == BIAS_POWERED_OFF)
		{
			if(g_new_parameters_received)
			{
				g_new_parameters_received = FALSE;
				txSetParameters((uint16_t*)&g_new_power_level_mW, (uint8_t*)&g_new_band_setting, (BOOL*)&g_new_AM_mod_setting, (BOOL*)&g_new_power_enable_setting);
				return ec;
			}
			else if(g_new_2m_power_level_received)
			{
				g_new_2m_power_level_received = FALSE;
				if(g_new_2m_power_DAC_setting > 0)
				{
					dac081c_set_dac(BIAS_MINUS_2V, BIAS_DAC);    /* set negative bias for safety */
					hold_new_bias_DAC_setting = g_new_2m_power_DAC_setting;
					hold_new_power_setting_MW = g_new_power_level_mW;
					bias_state = BIAS_POWER_UP_STEP1;
				}
				return ec;
			}

			dac081c_set_dac(0, BIAS_DAC);   /* set negative bias to zero */
			g_txTask = NULL;
		}
		else if(bias_state == BIAS_POWER_UP_STEP1)
		{
			if(hold_new_power_setting_MW == 0) /* Leave power to PA turned off if power setting is zero */
			{
				bias_state = BIAS_POWER_UP_STEP2;
			}
			else
			{
				BOOL err = dac081c_set_dac(BUCK_5V, PA_DAC);    /* set to 9V max for the MAAP-011232 */

				if(err)
				{
					PORTB &= ~(1 << PORTB6);                    /* Turn off Tx power */
					bias_state = BIAS_POWERED_OFF;
					ec = ERROR_CODE_DAC1_NONRESPONSIVE;
				}
				else
				{
					PORTB |= (1 << PORTB6); /* Turn on Tx power */
					bias_state = BIAS_POWER_UP_STEP2;
				}
			}
		}
		else if (bias_state == BIAS_POWER_UP_STEP2)
		{
			dac081c_set_dac(hold_new_bias_DAC_setting, BIAS_DAC);   /* set negative bias for correct power output */
			g_2m_power_level_mW = hold_new_power_setting_MW;
			bias_state = BIAS_POWERED_UP;
		}
		else if(bias_state == BIAS_POWERED_UP)
		{
			if((g_new_parameters_received) || (g_new_2m_power_level_received))
			{
				bias_state = BIAS_POWER_OFF_STEP1;
			}
			else
			{
				g_txTask = NULL;
			}
		}
		else if(bias_state == BIAS_POWER_OFF_STEP1)
		{
			dac081c_set_dac(BIAS_MINUS_2V, BIAS_DAC);    /* set negative bias for safety */
			bias_state = BIAS_POWER_OFF_STEP2;
		}
		else if(bias_state == BIAS_POWER_OFF_STEP2)
		{
			dac081c_set_dac(BUCK_0V, PA_DAC); /* set to 0V for the MAAP-011232 */

			PORTB &= ~(1 << PORTB6);    /* Turn off Tx power */
			bias_state = BIAS_POWERED_OFF;
		}
	}

	return ec;
}

