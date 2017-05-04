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
 * menu.h
 *
 */


#ifndef MENU_H_
#define MENU_H_

typedef enum menu_type
{
	MENU_MAIN,

#if PRODUCT_TEST_INSTRUMENT_HEAD
	MENU_BAND,
	MENU_SI5351,
#endif

	MENU_VOLUME,
	MENU_LCD,
	MENU_STATUS,
	MENU_POWER_OFF,
	MENU_LOW_BATTERY,
	MENU_REMOTE_DEVICE,
	MENU_SET_TIME,
	NUMBER_OF_MENUS,
	LEAVE_MENU_UNCHANGED,
	UPDATE_DISPLAY_WITH_LB_DATA,
	UPDATE_DISPLAY_WITH_BUTTON1_COL_DATA,
	UPDATE_DISPLAY_WITH_BUTTON2_COL_DATA,
	UPDATE_DISPLAY_WITH_BUTTON3_COL_DATA,
	UPDATE_DISPLAY_WITH_BUTTON4_COL_DATA,
	MENU_NO_MENU
} MenuType;

#define NUMBER_OF_SI5351_SUBMENUS 3
#define NUMBER_OF_VOLUME_SUBMENUS 2
#define NUMBER_OF_LCD_SUBMENUS 2
#define NUMBER_OF_STATUS_SUBMENUS 3


#endif /* MENU_H_ */