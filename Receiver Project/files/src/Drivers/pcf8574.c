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
 * pcf8574.c
 *
 */

#include "defs.h"

#ifdef INCLUDE_PCF8574_SUPPORT

#include "pcf8574.h"
#include <util/twi.h>
#include "i2c.h"

#define PCF8574_SLAVE_ADDR_A000_0 0x70
#define PCF8574_SLAVE_ADDR_A000_1 0x71

BOOL pcf8574_write(uint8_t addr, uint8_t data);
BOOL pcf8574_read(uint8_t addr, uint8_t *data);


void pcf8574_writePort( uint8_t data)
{
	pcf8574_write(PCF8574_SLAVE_ADDR_A000_0, data);
}

BOOL pcf8574_readPort(uint8_t *portData)
{
	BOOL failure;

	failure = pcf8574_read(PCF8574_SLAVE_ADDR_A000_1, portData);

	return(failure);
}

#ifdef SELECTIVELY_DISABLE_OPTIMIZATION
		BOOL __attribute__((optimize("O0"))) pcf8574_write(uint8_t slaveAddr, uint8_t data)
#else
		BOOL pcf8574_write(uint8_t slaveAddr, uint8_t data)
#endif
	{
		#ifndef DEBUG_WITHOUT_I2C
			i2c_start();
			if(i2c_status(TW_START))
			{
				return(TRUE);
			}
			
			if(i2c_write_success(slaveAddr, TW_MT_SLA_ACK))
			{
				return(TRUE);
			}
			
			if(i2c_write_success(data, TW_MT_DATA_ACK))
			{
				return(TRUE);
			}
			
			i2c_stop();
		#endif  /* #ifndef DEBUG_WITHOUT_I2C */

		return(FALSE);
	}

#ifdef SELECTIVELY_DISABLE_OPTIMIZATION
		BOOL __attribute__((optimize("O0"))) pcf8574_read(uint8_t slaveAddr, uint8_t *data)
#else
		BOOL pcf8574_read(uint8_t slaveAddr, uint8_t *data)
#endif
	{
		#ifndef DEBUG_WITHOUT_I2C

			i2c_start();
			if(i2c_status(TW_START))
			{
				return(TRUE);
			}

			if(i2c_write_success(slaveAddr, TW_MT_SLA_ACK))
			{
				return(TRUE);
			}

			i2c_start();
			if(i2c_status(TW_START))
			{
				return(TRUE);
			}

			*data = i2c_read_nack();
			if(i2c_status(TW_MR_DATA_NACK))
			{
				return(TRUE);
			}

			i2c_stop();

		#endif  /* #ifndef DEBUG_WITHOUT_I2C */

		return(FALSE);
	}

#endif  /* #ifdef INCLUDE_PCF8574_SUPPORT */
