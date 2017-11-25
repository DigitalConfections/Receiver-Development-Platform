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
 * max5478.h
 *
 * MAX5478EUD: 256-Position I2C®-Compatible Digital Potentiometer
 * https://datasheets.maximintegrated.com/en/ds/MAX5477-MAX5479.pdf
 *
 * The MAX5477/MAX5478/MAX5479 nonvolatile, dual, linear-taper, digital potentiometers 
 * perform the function of a mechanical potentiometer, but replace the mechanics with a 
 * simple 2-wire digital interface. Each device performs the same function as a discrete 
 * potentiometer or variable resistor and has 256 tap points.
 *
 */ 


#ifndef MAX5478_H_
#define MAX5478_H_

#include "defs.h"

#define MAX5478_SLAVE_ADDR_A0_0 0x50
#define MAX5478_SLAVE_ADDR_A0_1 0x51

/**
   Set the potentiometer to the value passed in setting.
*/
void max5478_set_potentiometer_wiperB(uint8_t setting);


#endif /* MAX5478_H_ */