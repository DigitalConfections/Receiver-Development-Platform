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
#include <string.h>

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

BOOL stringToSecondsSinceMidnight(char *str, int32_t *timeVal)
{
	int32_t time_sec = 0;
	uint8_t index = 0;
	char field[3];
	char* loc1;

	field[2] = '\0';
	field[1] = '\0';

	loc1 = strchr(str, ':');
	if(loc1 == NULL) return 1; // format error
	
	str = loc1 - 2; /* point str to beginning of time string */

	/* hh:mm:ss */
	field[0] = str[index++];    /* tens of hours or hours */
	field[1] = str[index++];    /* hours */
	
	time_sec = SecondsFromHours(atol(field));
	index++;

	field[0] = str[index++];
	field[1] = str[index++];    /* minutes */
	time_sec += SecondsFromMinutes(atol(field));
	index++;

	field[0] = str[index++];
	field[1] = str[index++];    /* seconds */
	time_sec += atoi(field);

	*timeVal = time_sec;
	return FALSE; 
}
