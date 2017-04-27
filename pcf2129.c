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
 * pcf2129.c
 *
 */ 

#include "defs.h"

#ifdef INCLUDE_PCF2129_SUPPORT

#include "pcf2129.h"
#include <util/twi.h>
#include <stdio.h>
#include "i2c.h"

#define RTC_CONTROL_1			0x00
#define RTC_CONTROL_2			0x01
#define RTC_CONTROL_3			0x02
#define RTC_SECONDS				0x03
#define RTC_MINUTES				0x04
#define RTC_HOURS				0x05
#define RTC_DAYS				0x06
#define RTC_WEEKDAYS			0x07
#define RTC_MONTHS				0x08
#define RTC_YEARS				0x09
#define RTC_SECOND_ALARM		0x0A
#define RTC_MINUTE_ALARM		0x0B
#define RTC_HOUR_ALARM			0x0C
#define RTC_DAY_ALARM			0x0D
#define RTC_WEEKDAY_ALARM		0x0E
#define RTC_CLKOUT_CTL			0x0F
#define RTC_WATCHDOG_TIME_CTL	0x10
#define RTC_WATCHDOG_TIME_VAL	0x11
#define RTC_TIMESTAMP_CTL		0x12
#define RTC_SEC_TIMESTAMP		0x13
#define RTC_MIN_TIMESTAMP		0x14
#define RTC_HOUR_TIMESTAMP		0x15
#define RTC_DAY_TIMESTAMP		0x16
#define RTC_MONTH_TIMESTAMP		0x17
#define RTC_YEAR_TIMESTAMP		0x18
#define RTC_AGING_OFFSET		0x19
#define RTC_INTERNAL_REG1		0x1A
#define RTC_INTERNAL_REG2		0x1B


BOOL pcf2129_read_time(int32_t* val, char* buffer, TimeFormat format)
{
	uint8_t data[10] = {0,0,0,0,0,0,0,0,0,0};
	uint8_t second10;
	uint8_t second;
	uint8_t minute10;
	uint8_t minute;
	uint8_t hour10;
	uint8_t hour;
	BOOL am_pm = FALSE;
	BOOL twelvehour;
	BOOL osf = TRUE;
	uint8_t temp;
	
	uint8_t offset = 1; // Kluge: Strangely, reads result in an offset, or the registers do not agree with the data sheet
	
	if(!i2c_device_read(PCF2129_BUS_BASE_ADDR, RTC_CONTROL_1, data, 10))
	{
		twelvehour = data[0] & 0x04;
		
		if(twelvehour)
		{
			am_pm = data[5-offset] & 0x20;
		}

		temp = data[3-offset];
		osf = temp >> 7;
		second10 = (temp & 0x70) >> 4;
		second = temp & 0x0F;
		
		temp = data[4-offset] & 0x7F;
		minute10 = temp >> 4;
		minute = temp & 0x0F;
		
		temp = data[5-offset] & 0x3F;
		hour10 = temp >> 4;
		hour = temp & 0x0F;
		
		if(buffer)
		{
			switch(format)
			{
				case Minutes_Seconds:
				{
					sprintf(buffer, "%1d%1d:%1d%1d", minute10, minute, second10, second);
				}
				break;
				
				case Hours_Minutes_Seconds:
				{
					if(twelvehour) // 12-hour
					{
						sprintf(buffer, "%1d%1d:%1d%1d:%1d%1d%s", hour10, hour, minute10, minute, second10, second, am_pm ? "AM":"PM");
					}
					else // 24 hour
					{
						sprintf(buffer, "%1d%1d:%1d%1d:%1d%1d", hour10, hour, minute10, minute, second10, second);
					}
				}
				break;
				
				default: // Day_Month_Year_Hours_Minutes_Seconds:
				{
					sprintf(buffer, "%1d%1d:%1d%1d", minute10, minute, second10, second);
				}
				break;
			}
		}
		
		if(val)
		{
			*val = second + 10*second10 + 60*(int32_t)minute + 600*(int32_t)minute10 +  3600*(int32_t)hour + 36000*(int32_t)hour10;
		}
	}
	
	return osf;
}

void timeValToString(char *str, int32_t timeVal)
{
	int32_t temp;
	uint8_t hold;
	uint8_t index=7;
	
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
	
	
	str[index--] = '0' + (timeVal % 10); // seconds
	temp = timeVal / 10;
	str[index--] = '0' + (temp % 6); // 10s of seconds
	temp /= 6;
	
	str[index--] = ':';
	
	str[index--] = '0' + (temp % 10); // minutes
	temp /= 10;
	str[index--] = '0' + (temp % 6); // 10s of minutes
	temp /= 6;
	
	str[index--] = ':';
	
	hold = temp % 24;
	str[index--] = '0' + (hold % 10); // hours
	hold /= 10;
	str[index--] = '0' + hold; // 10s of hours
}

void pcf2129_init(void)
{
	uint8_t d = 0;
	i2c_device_write(PCF2129_BUS_BASE_ADDR, RTC_CONTROL_1, &d, 1); // Clear POR_ OVRD
}

void pcf2129_set_time(int32_t offsetSeconds, BOOL applyAsOffset)
{
	int32_t timeVal;
	uint8_t data[7] = {0,0,0,0,0,0,0};
	int32_t temp;
	uint8_t hold;
	
	if(applyAsOffset)
	{
		pcf2129_read_time(&timeVal, NULL, Time_Format_Not_Specified);
		timeVal += offsetSeconds;
	}
	else
	{
		timeVal = offsetSeconds;
	}
	
	data[0] = timeVal % 10; // seconds
	temp = timeVal / 10;
	data[0] |= (temp % 6) << 4; // 10s of seconds
	temp /= 6;
	data[1] = temp % 10; // minutes
	temp /= 10;
	data[1] |= (temp % 6) << 4; // 10s of minutes
	temp /= 6;
	hold = temp % 24;
	data[2] = hold % 10; // hours
	hold /= 10;
	data[2] |= hold << 4; // 10s of hours
	
	i2c_device_write(PCF2129_BUS_BASE_ADDR, RTC_SECONDS, data, 3);
	
	//	temp /= 24;
	
}


#endif // #ifdef INCLUDE_PCF2129_SUPPORT
