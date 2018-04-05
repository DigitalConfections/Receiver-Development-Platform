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
 * ds3231.c
 *
 */

#include "defs.h"

#ifdef INCLUDE_DS3231_SUPPORT

   #include "ds3231.h"
   #include <util/twi.h>
   #include <stdio.h>
   #include "i2c.h"

   #define DS3231_I2C_SLAVE_ADDR 0xD0   /* corresponds to slave address = 0b1101000x */

   #define RTC_SECONDS                     0x00
   #define RTC_MINUTES                     0x01
   #define RTC_HOURS                       0x02
   #define RTC_DAY                         0x03
   #define RTC_DATE                        0x04
   #define RTC_MONTH_CENT                  0x05
   #define RTC_YEAR                        0x06
   #define RTC_ALARM1_SECONDS              0x07
   #define RTC_ALARM1_MINUTES              0x08
   #define RTC_ALARM1_HOUR                 0x09
   #define RTC_ALARM1_DAY_DATE             0x0A
   #define RTC_ALARM2_MINUTES              0x0B
   #define RTC_ALARM2_HOUR                 0x0C
   #define RTC_ALARM2_DAY_DATE             0x0D
   #define RTC_CONTROL                     0x0E
   #define RTC_CONTROL_STATUS              0x0F
   #define RTC_AGING                       0x10
   #define RTC_TEMP_MSB                    0x11
   #define RTC_TEMP_LSB                    0x12

#ifdef FOOBAR
const uint8_t wd(int year, int month, int day) 
{
	static const uint8_t weekdayname[] = {2 /*Mon*/, 3 /*Tue*/, 4 /*Wed*/, 5 /*Thu*/, 6 /*Fri*/, 7 /*Sat*/, 1 /*Sun*/};
	size_t JND =
	day
	+ ((153 * (month + 12 * ((14 - month) / 12) - 3) + 2) / 5)
	+ (365 * (year + 4800 - ((14 - month) / 12)))
	+ ((year + 4800 - ((14 - month) / 12)) / 4)
	- ((year + 4800 - ((14 - month) / 12)) / 100)
	+ ((year + 4800 - ((14 - month) / 12)) / 400)
	- 32045;
	return weekdayname[JND % 7];
}
#endif

	void ds3231_read_date_time(int32_t* val, char* buffer, TimeFormat format)
	{
		uint8_t data[7] = { 0, 0, 0, 0, 0, 0, 0 };
		uint8_t month;
		uint8_t date;
		uint16_t year = 2000;
		uint8_t second10;
		uint8_t second;
		uint8_t minute10;
		uint8_t minute;
		uint8_t hour10;
		uint8_t hour;
		BOOL am_pm;
		BOOL twelvehour;

		if(!i2c_device_read(DS3231_I2C_SLAVE_ADDR, RTC_SECONDS, data, 7))
		{
			second10 = ((data[0] & 0xf0) >> 4);
			second = (data[0] & 0x0f);

			minute10 = ((data[1] & 0xf0) >> 4);
			minute = (data[1] & 0x0f);

			am_pm = ((data[2] >> 5) & 0x01);
			hour10 = ((data[2] >> 4) & 0x01);
			hour = (data[2] & 0x0f);

			twelvehour = ((data[2] >> 6) & 0x01);

			if(!twelvehour && am_pm)
			{
				hour10 = 2;
			}

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
						if(twelvehour)  /* 12-hour */
						{
							sprintf(buffer, "%1d%1d:%1d%1d:%1d%1d%s", hour10, hour, minute10, minute, second10, second, am_pm ? "AM" : "PM");
						}
						else            /* 24 hour */
						{
							sprintf(buffer, "%1d%1d:%1d%1d:%1d%1d", hour10, hour, minute10, minute, second10, second);
						}
					}
					break;

					default:    /* Day_Month_Year_Hours_Minutes_Seconds: */
					{
						date = data[4] & 0x0f;
						date += 10*((data[4] & 0xf0) >> 4);
						month = data[5] & 0x0f;
						month += 10*((data[5] & 0xf0) >> 4);
						year += data[6] & 0x0f;
						year += 10*((data[6] & 0xf0) >> 4);
				
						sprintf(buffer, "%4d-%02d-%02dT%1d%1d:%1d%1d:%1d%1d", year, month, date, hour10, hour, minute10, minute, second10, second);
					}
					break;
				}
			}

			if(val)
			{
				*val = second + 10 * second10 + 60 * (int32_t)minute + 600 * (int32_t)minute10 +  3600 * (int32_t)hour + 36000 * (int32_t)hour10;
			}
		}
	}
	
	
	BOOL ds3231_get_temp(int16_t * val)
	{
		uint8_t data[2] = { 0, 0 };
		BOOL result = i2c_device_read(DS3231_I2C_SLAVE_ADDR, RTC_TEMP_MSB, data, 2);
		
		if(!result)
		{
			*val = data[0];
			*val = *val << 8;
			*val |= data[1];
		}
		
		return result;
	}


void ds3231_set_date_time(char * dateString, ClockSetting setting) /* "2018-03-23T18:00:00Z" */
{
	uint8_t data[7] = { 0, 0, 0, 1, 0, 0, 0 };
	int temp, year=2000, month, date;
	
	data[0] = dateString[18] - '0'; /* seconds */
	data[0] |= ((dateString[17] - '0') << 4); /*10s of seconds */
	data[1] = dateString[15] - '0'; /* minutes */
	data[1] |= ((dateString[14] - '0') << 4); /* 10s of minutes */
	data[2] = dateString[12] - '0'; /* hours */
	data[2] |= ((dateString[11] - '0') << 4); /* 10s of hours - sets 24-hour format (not AM/PM) */
	//data[3] = Skip day of week
	data[4] = dateString[9] - '0'; /* day of month digit 1 */
	date = data[4];
	temp = dateString[8] - '0';
	date += 10*temp;
	data[4] |= (temp << 4); /* day of month digit 10 */
	data[5] = dateString[6] - '0'; /* month digit 1 */
	month = data[5];
	temp = dateString[5] - '0';
	month += 10*temp;
	data[5] |= (temp << 4 ); /* month digit 10; century=0 */
	data[6] = dateString[3] - '0'; /* year digit 1 */
	year += data[6];
	temp = dateString[2] - '0';
	year += 10*temp;
	data[6] |= (temp << 4); /* year digit 10 */
	
	i2c_device_write(DS3231_I2C_SLAVE_ADDR, RTC_SECONDS+(setting*7), data, 7);
}

	void ds3231_1s_sqw(BOOL enable)
	{
		if(enable)
		{
			uint8_t byte = 0x00;
			i2c_device_write(DS3231_I2C_SLAVE_ADDR, RTC_CONTROL, &byte, 1);
		}
		else
		{
			uint8_t byte = 0x04;
			i2c_device_write(DS3231_I2C_SLAVE_ADDR, RTC_CONTROL, &byte, 1);
		}
	}


#endif  /* #ifdef INCLUDE_DS3231_SUPPORT */

