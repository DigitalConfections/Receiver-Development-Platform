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
 * util.c
 *
 */

#include "util.h"
#include <avr/eeprom.h>
#include <stdio.h>
#include <stdlib.h>

/***********************************************************************************************
 *  EEPROM Utility Functions
 ************************************************************************************************/


void storeEEbyteIfChanged(uint8_t* ee_var, uint8_t val)
{
	if(eeprom_read_byte((uint8_t*)ee_var) != val)
	{
		eeprom_write_byte(ee_var, val);
	}
}

void storeEEdwordIfChanged(uint32_t* ee_var, uint32_t val)
{
	if(eeprom_read_dword(ee_var) != val)
	{
		eeprom_write_dword(ee_var, val);
	}
}


/***********************************************************************************************
 *  Print Formatting Utility Functions
 ************************************************************************************************/

void timeValToString(char *str, int32_t timeVal, TimeFormat tf)
{
	int32_t temp;
	uint8_t hold;
	uint8_t index = 7;
	BOOL done = FALSE;

	if(tf == Minutes_Seconds_Elapsed)
	{
		if(timeVal < 0)
		{
			timeVal += 86400L;  /* account for midnight rollover */

		}
		if(timeVal < 6000)
		{
			str[5] = '\0';
			index = 4;
		}
		else
		{
			if(timeVal < 60000)
			{
				sprintf(str, ">%ldm", timeVal / 60);
			}
			else
			{
				sprintf(str, "%ld.%1ldh", timeVal / 3600, (10 * (timeVal % 3600) / 3600));
			}

			done = TRUE;
		}
	}
	else
	{
		if(timeVal < 0)
		{
			timeVal = -timeVal;
			str[9] = '\0';
			str[0] = '-';
			index = 8;
		}
		else
		{
			str[8] = '\0';
		}
	}

	if(!done)
	{
		str[index--] = '0' + (timeVal % 10);    /* seconds */
		temp = timeVal / 10;
		str[index--] = '0' + (temp % 6);        /* 10s of seconds */
		temp /= 6;

		str[index--] = ':';

		str[index--] = '0' + (temp % 10);   /* minutes */
		temp /= 10;

		if(tf == Minutes_Seconds_Elapsed)
		{
			str[index--] = '0' + (temp % 10);   /* 10s of minutes */
		}
		else
		{
			str[index--] = '0' + (temp % 6);    /* 10s of minutes */
			temp /= 6;

			str[index--] = ':';

			hold = temp % 24;
			str[index--] = '0' + (hold % 10);   /* hours */
			hold /= 10;
			str[index--] = '0' + hold;          /* 10s of hours */
		}
	}
}

int32_t stringToTimeVal(char *str)
{
	int32_t time_sec = 0;
	BOOL missingTens = FALSE;
	uint8_t index = 0;
	char field[3];

	field[2] = '\0';
	field[1] = '\0';

	if(str[1] == ':')
	{
		missingTens = TRUE;
	}

	/* hh:mm:ss or h:mm:ss */
	field[0] = str[index++];        /* tens of hours or hours */
	if(!missingTens)
	{
		field[1] = str[index++];    /* hours */
	}
	
	time_sec = SecondsFromHours(atol(field));
	index++;

	field[0] = str[index++];
	field[1] = str[index++];    /* minutes */
	time_sec += SecondsFromMinutes(atol(field));
	index++;

	field[0] = str[index++];
	field[1] = str[index++];    /* seconds */
	time_sec += atoi(field);

	return(time_sec);
}
