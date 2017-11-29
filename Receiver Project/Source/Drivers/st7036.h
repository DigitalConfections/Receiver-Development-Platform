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
 * st7036.h
 *
 */

#ifndef ST7036_h
#define ST7036_h

#include "defs.h"

#ifdef INCLUDE_ST7036_SUPPORT

#include <inttypes.h>

#define LCD_I2C_SLAVE_ADDRESS   0x78

/**
 */
void LCD_init(uint8_t num_lines, uint8_t num_col, uint8_t i2cAddr, ContrastType contrast );

/**
 */
void LCD_print_screen(char buffer[NUMBER_OF_LCD_ROWS][DISPLAY_WIDTH_STRING_SIZE]);

/**
 */
size_t LCD_print_row(char *buffer, LcdRowType row);                     /* Print buffer at row, col=0 */

/**
 */
size_t LCD_print_row_col(char *buffer, LcdRowType row, LcdColType col); /* Print buffer at row, col */

/**
 */
void LCD_set_cursor_row_col(LcdRowType row, LcdColType col);

/**
 */
void LCD_blink_cursor_row_col(BOOL on, LcdRowType row, LcdColType col); /* Move cursor to row,col and turn blinking on/off */

/**
 */
void LCD_set_contrast(ContrastType contrast);

#endif  /* #ifdef INCLUDE_ST7036_SUPPORT */

#endif
