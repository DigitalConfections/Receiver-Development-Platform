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
 * st7036.c
 *
 */

#include "defs.h"

#ifdef INCLUDE_ST7036_SUPPORT

#include <string.h>			//needed for strlen()
#include <inttypes.h>
#include <util/twi.h>
#include <avr/wdt.h>

#include "st7036.h"
#include "i2c.h"

/////////////////////////////////////////////////////////////////////////////////////
// private constants and definitions
// ----------------------------------------------------------------------------
const int     CMD_DELAY           = 1;  // Command delay in miliseconds
const int     PIXEL_ROWS_PER_CHAR = 8;  // Number of pixel rows in the LCD character
const int     MAX_USER_CHARS      = 16; // Maximun number of user defined characters

// LCD Command set
const uint8_t DISP_CMD       = 0x0;  // Command for the display
const uint8_t RAM_WRITE_CMD  = 0x40; // Write to display RAM
const uint8_t CLEAR_DISP_CMD = 0x01; // Clear display command
const uint8_t HOME_CMD       = 0x02; // Set cursor to (0,0)
const uint8_t DISP_ON_CMD    = 0x0C; // Display on command
const uint8_t DISP_OFF_CMD   = 0x08; // Display off Command
const uint8_t SET_DDRAM_CMD  = 0x80; // Set DDRAM address command
const uint8_t CONTRAST_CMD   = 0x70; // Set contrast LCD command
const uint8_t FOLLOWER_CMD   = 0x60; // Set follower circuit
const uint8_t FUNC_SET_TBL0  = 0x38; // Function set - 8 bit, 2 line display 5x8, inst table 0
const uint8_t FUNC_SET_TBL1  = 0x39; // Function set - 8 bit, 2 line display 5x8, inst table 1

// LCD bitmap definition
const uint8_t CURSOR_ON_BIT  = ( 1 << 1 );// Cursor selection bit in Display on cmd.
const uint8_t BLINK_ON_BIT   = ( 1 << 0 );// Blink selection bit on Display on cmd. 

static int8_t g_display_number_of_rows;
static uint8_t g_display_number_of_columns;
static uint8_t g_i2c_slave_addr;
static int g_command_delay_ms;
static int g_write_delay_ms;
static BOOL g_initialized = FALSE;
static uint8_t g_display_status;

const uint8_t dispAddr[][3] =
{
   { 0x00, 0x00, 0x00 },  // One line display address
   { 0x00, 0x40, 0x00 },  // Two line display address
   { 0x00, 0x10, 0x20 }   // Three line display address
};


// Private support functions
// ----------------------------------------------------------------------------
   /**
    Send a command to the display
    
    @param value[in] Command to be sent to the display
    @return None
    
    void command(uint8_t value);
    */
	void command(uint8_t value);

   /**
    Initialises the display and sets the contrast passed in by the argument.
    
    void initialize(ContrastType contrast);
    */
	void initialize(ContrastType contrast);

  /**
    Prints a string of characters to the display.
    
    @param buffer[in] buffer to write to the current LCD write position
    @param size[in] size of the buffer
    @return size_t
    
    void st7036_write_str(char *buffer, size_t size);
    */
    void st7036_write_str(char *buffer, size_t size);
	
    /**
    Turn on the cursor "_". 
    
    void cursor_on();
    */
	void st7036_cursor_on(void);
   
   /**
    Turn off the cursor. This is the default state when the display is
    initialised.
    
    void cursor_off();
    */   
	void st7036_cursor_off(void);
	
   /**
    Deactivate cursor blinking. This is the default state when the display is
    initialised.
    
    void blink_off();
    */
	void st7036_blink_off(void);
	
   /**
    Activate cursor blinking.
    
    void blink_on();
    */
	void st7036_blink_on(void);
	
   /**
    Set the cursor at the specified coordinates (Line, Col).
    
    @param Line[in] Line for cursor, range (0, max display lines-1)
    @param Col[in]  Column for cursor, range (0, max width-1)
    @return None
    
    void setCursor(uint8_t Line, uint8_t Col);
    */
	void st7036_setCursor(uint8_t Line, uint8_t Col);
 	
	/**
	Simple (and probably unnecessary) wrapper for _delay_ms()
	*/
	void delay(int);

   

// Initializers:
// ---------------------------------------------------------------------------
void LCD_init(uint8_t num_lines, uint8_t num_cols, 
               uint8_t i2cAddr, ContrastType contrast)
{
	g_display_number_of_rows = num_lines;
	g_display_number_of_columns = num_cols;
	g_i2c_slave_addr = i2cAddr; // was ( i2cAddr >> 1 );
	g_command_delay_ms = CMD_DELAY;
	g_write_delay_ms = 1;
	g_initialized = FALSE;
   
	initialize(contrast);
}


#ifdef SELECTIVELY_DISABLE_OPTIMIZATION
void __attribute__((optimize("O0"))) initialize(ContrastType contrast)
#else
void initialize(ContrastType contrast) 
#endif
{
// The following three lines issue a reset pulse to the LCD. Ordinarily this shouldn't be necessary,
// but can effectively simulate powering up for the first time. When doing target debugging the
// reset pulse provides more realistic power up reset timing.
//	PORTD &= 0b01111111;
//	_delay_ms(30);
//	PORTD |= 0b10000000;
	
	i2c_init();
	g_initialized = TRUE;
	
#ifdef I2C_TIMEOUT_SUPPORT
	while(i2c_start());
#else
	i2c_start();
#endif

	i2c_write_success(g_i2c_slave_addr, TW_MT_SLA_ACK);	
	i2c_write_success(0x00, TW_MT_DATA_ACK); // Comsend = 0x00
	i2c_write_success(0x38, TW_MT_DATA_ACK); // I2C_out(0x38);
	delay(10); // delay(10);
	i2c_write_success(0x39, TW_MT_DATA_ACK); // I2C_out(0x39);
	delay(10); // delay(10);
	i2c_write_success(0x14, TW_MT_DATA_ACK); // Set BIAS - 1/5
	i2c_write_success(CONTRAST_CMD | contrast, TW_MT_DATA_ACK); // Set contrast
	i2c_write_success(0x5E, TW_MT_DATA_ACK); // ICON disp on, Booster on, Contrast high byte 
	
	wdt_reset();	
	delay(200);
	wdt_reset();	
	
	i2c_write_success(0x6D, TW_MT_DATA_ACK); // Follower circuit (internal), amp ratio (6)
	wdt_reset();	
	delay(200);
	wdt_reset();	

	i2c_write_success(DISP_ON_CMD, TW_MT_DATA_ACK); // Display on
	delay(200);
	wdt_reset();	

	i2c_write_success(CLEAR_DISP_CMD, TW_MT_DATA_ACK); // Clear display
	i2c_write_success(0x06, TW_MT_DATA_ACK); // Entry mode set - increment
	delay(100); // delay(10);
	i2c_stop(); // I2C_Stop();
	delay(100);
}


void command(uint8_t value)
{
	i2c_device_write(g_i2c_slave_addr, DISP_CMD, &value, 1);
}

void st7036_write_str(char *buffer, size_t size)
{
   if(g_initialized)
   {
		i2c_device_write(g_i2c_slave_addr, RAM_WRITE_CMD, (uint8_t*)buffer, size);
		g_display_status = 0;
		delay(g_write_delay_ms);
   }
 }


size_t LCD_print_row(char *buffer, LcdRowType row)
{
	return LCD_print_row_col(buffer, row, 0);
}


size_t LCD_print_row_col(char *buffer, LcdRowType row, LcdColType col)
{
	delay(10);
	st7036_setCursor(row, col);
	delay(10);
	int len = strlen(buffer);
	st7036_write_str(buffer, len);
	return len;
}

void LCD_print_screen(char buffer[NUMBER_OF_LCD_ROWS][DISPLAY_WIDTH_STRING_SIZE])
{
	for(LcdRowType i=ROW0; i<NUMBER_OF_LCD_ROWS; i++)
	{
		LCD_print_row(buffer[i], i);
	}
}

void LCD_blink_cursor_row_col(BOOL on, LcdRowType row, LcdColType col)
{
	if(!on)
	{
		st7036_cursor_off();
		st7036_setCursor(1,0);
		st7036_blink_off();
	}
	else
	{
		st7036_blink_on(); 
		st7036_setCursor(row, col);
	}
}

void LCD_set_cursor_row_col(LcdRowType row, LcdColType col)
{
	st7036_setCursor(row, col);
}

void LCD_set_contrast(ContrastType contrast)
{
   if(g_initialized)
   {
		command(contrast | CONTRAST_CMD); // Set contrast
   }
}

void st7036_cursor_on()
{
   command(DISP_ON_CMD | CURSOR_ON_BIT);
}

void st7036_cursor_off()
{
   command(DISP_ON_CMD & ~(CURSOR_ON_BIT));
}

void st7036_blink_on()
{
   command(DISP_ON_CMD | BLINK_ON_BIT);
}

void st7036_blink_off()
{
   command(DISP_ON_CMD & ~(BLINK_ON_BIT)); 
}


void st7036_setCursor(uint8_t line_num, uint8_t x)
{
   if(g_initialized)
   {
		uint8_t base;
		// set the baseline address with respect to the number of lines of the display 
		base = dispAddr[g_display_number_of_rows-1][line_num] + SET_DDRAM_CMD + x;
		command(base);
		delay(1);
   }
}

void __attribute__((optimize("O1"))) delay(int millisecs)
{
	_delay_ms(millisecs);
}

#endif // #ifdef INCLUDE_ST7036_SUPPORT

