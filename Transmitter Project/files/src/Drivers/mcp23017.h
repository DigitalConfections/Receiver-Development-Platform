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
 * mcp23017.h
 *
 * http://ww1.microchip.com/downloads/en/DeviceDoc/20001952C.pdf
 * The MCP23017 device provides 16-bit, general
 *  purpose parallel I/O expansion for I2C bus applications.
 *
 */


#ifndef MCP23017_H_
#define MCP23017_H_

#define MCP23017_PORTA 0
#define MCP23017_PORTB 1

/**
 */
void mcp23017_init(void);

/**
 */
void mcp23017_writePort(uint8_t data, uint8_t port);

/**
 */
BOOL mcp23017_readPort(uint8_t *portData, uint8_t port);


#endif  /* MCP23017_H_ */