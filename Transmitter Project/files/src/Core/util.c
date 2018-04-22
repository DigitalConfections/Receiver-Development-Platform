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

void storeEEwordIfChanged(uint16_t* ee_var, uint16_t val)
{
	if(eeprom_read_word(ee_var) != val)
	{
		eeprom_write_word(ee_var, val);
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



/**
 * Returns parsed time structure from a string of format "yyyy-mm-ddThh:mm:ss"
 */
BOOL mystrptime(char* s, struct tm* ltm) {
  const int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  char temp[6];
  char str[20];
  int hold;
  BOOL isleap;
  char *ptr0;
  char *ptr1;
  BOOL noSeconds = FALSE;

  strcpy(str, s);
  
  ptr1 = strchr(str, 'Z');
  if(ptr1) *ptr1 = '\0'; // erase the 'Z' character at the end if one exists
  
  noSeconds = (strlen(str) < 17);
  
  ptr1 = strchr(str, '-');
  *ptr1 = '\0';
  strcpy(temp, str);
  ++ptr1;
  hold = atoi(temp);
  isleap = is_leap_year(hold);
  hold -= 1900;
  if((hold < 0) || (hold > 200)) return TRUE;
  ltm->tm_year = hold;
  
  ptr0 = ptr1;

  ptr1 = strchr(ptr0, '-');
  *ptr1 = '\0';
  strcpy(temp, ptr0);
  ++ptr1;
  hold = atoi(temp) - 1;
  if((hold > 11) || (hold < 0)) return TRUE;
  ltm->tm_mon = hold;
  
  ptr0 = ptr1;

  ptr1 = strchr(ptr0, 'T');
  *ptr1 = '\0';
  strcpy(temp, ptr0);
  ++ptr1;
  hold = atoi(temp);
  if((hold > 31) || (hold < 1)) return TRUE;
  ltm->tm_mday = hold;
  
  ptr0 = ptr1;

  ltm->tm_yday = 0;
  for(int i=0; i<ltm->tm_mon; i++)
  {
    ltm->tm_yday += month_days[i];
    if((i==1) && isleap) ltm->tm_yday++;
  }

  ltm->tm_yday += (ltm->tm_mday - 1);

  ptr1 = strchr(ptr0, ':');
  *ptr1 = '\0';
  strcpy(temp, ptr0);
  ++ptr1;
  hold = atoi(temp);
  if((hold > 23) || (hold < 0)) return TRUE;
  ltm->tm_hour = hold;
  
  ptr0 = ptr1;

  if(noSeconds)
  {
	  strcpy(temp, ptr0);
	  hold = atoi(temp);
	  if(hold > 59) return TRUE;
	  ltm->tm_min = hold;
  }
  else
  {
	  ptr1 = strchr(ptr0, ':');
	  *ptr1 = '\0';
	  strcpy(temp, ptr0);
	  ++ptr1;
	  hold = atoi(temp);
	  if(hold > 59) return TRUE;
	  ltm->tm_min = hold;

	  hold = atoi(ptr1);
	  if(hold > 59) return TRUE;
	  ltm->tm_sec = hold;
  }

  return FALSE;
}


#ifdef DATE_STRING_SUPPORT_ENABLED
/**
 * Converts a string of format "yyyy-mm-ddThh:mm:ss" to seconds since 1900
 */
uint32_t convertTimeStringToEpoch(char * s)
{
  unsigned long result = 0;
  struct tm ltm = {0};

  if (!mystrptime(s, &ltm)) {
    result = ltm.tm_sec + ltm.tm_min*60 + ltm.tm_hour*3600L + ltm.tm_yday*86400L +
    (ltm.tm_year-70)*31536000L + ((ltm.tm_year-69)/4)*86400L -
    ((ltm.tm_year-1)/100)*86400L + ((ltm.tm_year+299)/400)*86400L;
  }
  
  return result;
}
#endif //  DATE_STRING_SUPPORT_ENABLED