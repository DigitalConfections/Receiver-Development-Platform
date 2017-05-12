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
 * pcf2129.h
 *
 * PCF2129: Accurate RTC with integrated quartz crystal for industrial applications
 * http://www.nxp.com/documents/data_sheet/PCF2129.pdf
 * The PCF2129 is a CMOS1 Real Time Clock (RTC) and calendar with an integrated Temperature 
 * Compensated Crystal (Xtal) Oscillator (TCXO) and a 32.768 kHz quartz crystal optimized 
 * for very high accuracy and very low power consumption. The PCF2129 has a selectable 
 * I2C-bus or SPI-bus, a backup battery switch-over circuit, a programmable watchdog function, 
 * a timestamp function, and many other features.
 *
 */ 

#include "defs.h"

#ifndef PCF2129_H_
#define PCF2129_H_

#ifdef INCLUDE_PCF2129_SUPPORT

#define PCF2129_BUS_BASE_ADDR				0xA2  /* corresponds to slave address = 0b1010001x */

/**
Reads hours, minutes and seconds from the DS3231 and returns them in the memory location pointed to by
the first two arguments.
*val - if non-NULL will receive a dword value of the time in seconds since midnight.
*char - if non-NULL will receive a string representation of the time
format - specifies the string format to be used for the string time representation
*/
BOOL pcf2129_read_time(int32_t* val, char* buffer, TimeFormat format);

/**
Set hours, minutes and seconds of the DS3231 to the time passed in the argument.
offset - time in seconds since midnight
 */
void pcf2129_set_time(int32_t offset, BOOL applyAsOffset);

/**
Turn on/off 1-second square wave on the INT/SQW pin.
*/
void pcf2129_1s_sqw(BOOL enable);

/**
Clear all RTC interrupt flags.
*/void pcf2129_clear_int(void);

/**
*/
void pcf2129_init(void);

#endif // #ifdef INCLUDE_PCF2129_SUPPORT

#endif /* PCF2129_H_ */