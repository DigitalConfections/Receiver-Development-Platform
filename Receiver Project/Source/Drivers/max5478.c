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
 * ad5245.c
 *
 */

#include "max5478.h"
#include "i2c.h"
#include <util/twi.h>

#define MAX_5478_WIPER_A_VREG_COMMAND 0x11
#define MAX_5478_WIPER_A_NVREG_COMMAND 0x21
#define MAX_5478_WIPER_A_NVREG_TO_VREG_COMMAND 0x61
#define MAX_5478_WIPER_A_VREG_TO_NVREG_COMMAND 0x51

#define MAX_5478_WIPER_B_VREG_COMMAND 0x12
#define MAX_5478_WIPER_B_NVREG_COMMAND 0x22
#define MAX_5478_WIPER_B_NVREG_TO_VREG_COMMAND 0x62
#define MAX_5478_WIPER_B_VREG_TO_NVREG_COMMAND 0x52

void max5478_set_dualpotentiometer_wipers(uint16_t setting)
{
	uint8_t data = setting & 0xFF;
	i2c_device_write(MAX5478_SLAVE_ADDR_A0_0, MAX_5478_WIPER_A_VREG_COMMAND, &data, 1); // Fine attenuation
	data = (setting >> 8);
	i2c_device_write(MAX5478_SLAVE_ADDR_A0_0, MAX_5478_WIPER_B_VREG_COMMAND, &data, 1); // Coarse attenuation
}