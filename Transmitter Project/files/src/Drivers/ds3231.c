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


	void ds3231_read_time(int32_t* val, char* buffer, TimeFormat format)
	{
		uint8_t data[7] = { 0, 0, 0, 0, 0, 0, 0 };
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
						sprintf(buffer, "%1d%1d:%1d%1d", minute10, minute, second10, second);
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


	void ds3231_set_time(int32_t offsetSeconds)
	{
		int32_t timeVal;
		uint8_t data[7] = { 0, 0, 0, 0, 0, 0, 0 };
		int32_t temp;
		uint8_t hold;

		ds3231_read_time(&timeVal, NULL, Time_Format_Not_Specified);
		timeVal += offsetSeconds;

		data[0] = timeVal % 10;     /* seconds */
		temp = timeVal / 10;
		data[0] |= (temp % 6) << 4; /* 10s of seconds */
		temp /= 6;
		data[1] = temp % 10;        /* minutes */
		temp /= 10;
		data[1] |= (temp % 6) << 4; /* 10s of minutes */
		temp /= 6;
		hold = temp % 24;
		data[2] = hold % 10;        /* hours */
		hold /= 10;
		data[2] |= hold << 4;       /* 10s of hours */

		i2c_device_write(DS3231_I2C_SLAVE_ADDR, RTC_SECONDS, data, 3);

/*	temp /= 24; */

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

