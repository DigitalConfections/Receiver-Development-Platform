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
 * pcf8574.h
 *
 * http://www.nxp.com/documents/data_sheet/PCF8574_PCF8574A.pdf
 * The PCF8574/74A provides general-purpose remote I/O expansion via the two-wire 
 * bidirectional I2C-bus (serial clock (SCL), serial data (SDA)).
 * The devices consist of eight quasi-bidirectional ports, 100 kHz I2C-bus interface, 
 * three hardware address inputs and interrupt output operating between 2.5 V and 6 V. 
 * The quasi-bidirectional port can be independently assigned as an input to monitor 
 * interrupt status or keypads, or as an output to activate indicator devices such as 
 * LEDs. System master can read from the input port or write to the output port through
 * a single register.
 *
 */ 


#ifndef PCF8574_H_
#define PCF8574_H_


#define PCF8574_SLAVE_ADDR_Ax_000 0x40
#define RECEIVER_REMOTE_PORT_ADDR PCF8574_SLAVE_ADDR_Ax_000

/**
*/
void writePort(uint8_t slaveAddress, uint8_t data);

/**
*/
BOOL readPort(uint8_t slaveAddress, uint8_t *portData);


#endif /* PCF8574_H_ */