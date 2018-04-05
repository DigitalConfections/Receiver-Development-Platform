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
 * util.h
 *
 * Various helper functions with no better home.
 *
 */


#ifndef UTIL_H_
#define UTIL_H_

#include "defs.h"

/***********************************************************************************************
 *  EEPROM Utility Functions
 ************************************************************************************************/

/**
 *  Writes a short (byte) to the EEPROM memory location specified by ee_var only if that value
 *  differs from what is already stored in EEPROM.
 *  ee_var is the pointer to EE prom memory
 *  val = byte value to be written
 */
void storeEEbyteIfChanged(uint8_t* ee_var, uint8_t val);

/**
 *  Writes a long (4 bytes) to the EEPROM memory location specified by ee_var only if that value
 *  differs from what is already stored in EEPROM.
 *  ee_var is the pointer to EE prom memory
 *  val = long value to be written
 */
void storeEEdwordIfChanged(uint32_t* ee_var, uint32_t val);

/***********************************************************************************************
 *  Print Formatting Utility Functions
 ************************************************************************************************/

/**
 *  str = pointer to char[] of size 10 or greater containing time string in format hh:mm:ss
 *  sets *timeVal to time in seconds since midnight
 *  returns non-zero if error occurs
 */
BOOL stringToSecondsSinceMidnight(char *str, int32_t *timeVal);


#endif  /* UTIL_H_ */