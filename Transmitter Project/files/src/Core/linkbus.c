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
 * linkbus.c
 *
 */

#include "linkbus.h"
#include "defs.h"
#include "util.h"
#include <string.h>
#include <stdio.h>
#include <avr/wdt.h>

/* Global Variables */
static volatile BOOL g_bus_disabled = TRUE;
static volatile BOOL g_lb_terminal_mode = LINKBUS_TERMINAL_MODE_DEFAULT;
static const char crlf[] = "\n";
static char lineTerm[8] = "\n";
static const char textPrompt[] = "TX> ";
static const char textWDT[] = "*** WDT Reset! ***\n";


static const char textHelp[][35] = { "\nCommands:\n",
" BAT   - Battery\n",
" TIM [hh:mm:ss]- RTC Time\n",
" BND [2|80] - Band\n",
" FRE [M<1:5>] [Hz] - Freq\n",
" DRI U|D 0-255 - 2m Drive\n",
" POW [0-255] - PA Power\n",
" ID [text]   - Station ID\n",
" PA [text]   - Pattern\n",
" SPD [ID|PA] s - Speed WPM\n",
" T [0|1|D] s - On/Off/Dly times\n",
" SF [S|F] [hh:mm:ss] - Start/Fin\n",
" P             - Perm\n",
" RST           - Reset\n",
" WI [1|2]      - WiFi \n",
" ?             - Info\n"
};

static char g_tempMsgBuff[LINKBUS_MAX_MSG_LENGTH];

/* Local function prototypes */
BOOL linkbus_send_text(char* text);
BOOL linkbus_start_tx(void);

/* Module global variables */
static volatile BOOL linkbus_tx_active = FALSE; // volatile is required to ensure optimizer handles this properly
static LinkbusTxBuffer tx_buffer[LINKBUS_NUMBER_OF_TX_MSG_BUFFERS];
static LinkbusRxBuffer rx_buffer[LINKBUS_NUMBER_OF_RX_MSG_BUFFERS];

LinkbusTxBuffer* nextFullTxBuffer(void)
{
	BOOL found = TRUE;
	static uint8_t bufferIndex = 0;
	uint8_t count = 0;

	while(tx_buffer[bufferIndex][0] == '\0')
	{
		if(++count >= LINKBUS_NUMBER_OF_TX_MSG_BUFFERS)
		{
			found = FALSE;
			break;
		}

		bufferIndex++;
		if(bufferIndex >= LINKBUS_NUMBER_OF_TX_MSG_BUFFERS)
		{
			bufferIndex = 0;
		}
	}

	if(found)
	{
		return( &tx_buffer[bufferIndex]);
	}

	return(NULL);
}

LinkbusTxBuffer* nextEmptyTxBuffer(void)
{
	BOOL found = TRUE;
	static uint8_t bufferIndex = 0;
	uint8_t count = 0;

	while(tx_buffer[bufferIndex][0] != '\0')
	{
		if(++count >= LINKBUS_NUMBER_OF_TX_MSG_BUFFERS)
		{
			found = FALSE;
			break;
		}

		bufferIndex++;
		if(bufferIndex >= LINKBUS_NUMBER_OF_TX_MSG_BUFFERS)
		{
			bufferIndex = 0;
		}
	}

	if(found)
	{
		return( &tx_buffer[bufferIndex]);
	}

	return(NULL);
}

LinkbusRxBuffer* nextEmptyRxBuffer(void)
{
	BOOL found = TRUE;
	static uint8_t bufferIndex = 0;
	uint8_t count = 0;

	while(rx_buffer[bufferIndex].id != MESSAGE_EMPTY)
	{
		if(++count >= LINKBUS_NUMBER_OF_RX_MSG_BUFFERS)
		{
			found = FALSE;
			break;
		}

		bufferIndex++;
		if(bufferIndex >= LINKBUS_NUMBER_OF_RX_MSG_BUFFERS)
		{
			bufferIndex = 0;
		}
	}

	if(found)
	{
		return( &rx_buffer[bufferIndex]);
	}

	return(NULL);
}

LinkbusRxBuffer* nextFullRxBuffer(void)
{
	BOOL found = TRUE;
	static uint8_t bufferIndex = 0;
	uint8_t count = 0;

	while(rx_buffer[bufferIndex].id == MESSAGE_EMPTY)
	{
		if(++count >= LINKBUS_NUMBER_OF_RX_MSG_BUFFERS)
		{
			found = FALSE;
			break;
		}

		bufferIndex++;
		if(bufferIndex >= LINKBUS_NUMBER_OF_RX_MSG_BUFFERS)
		{
			bufferIndex = 0;
		}
	}

	if(found)
	{
		return( &rx_buffer[bufferIndex]);
	}

	return(NULL);
}


/***********************************************************************
 * linkbusTxInProgress(void)
 ************************************************************************/
BOOL linkbusTxInProgress(void)
{
	return(linkbus_tx_active);
}

BOOL linkbus_start_tx(void)
{
	BOOL success = !linkbus_tx_active;

	if(success) /* message will be lost if transmit is busy */
	{
		linkbus_tx_active = TRUE;
		UCSR0B |= (1 << UDRIE0);
	}

	return(success);
}

void linkbus_end_tx(void)
{
	if(linkbus_tx_active)
	{
		UCSR0B &= ~(1 << UDRIE0);
		linkbus_tx_active = FALSE;
	}
}

void linkbus_reset_rx(void)
{
	if(UCSR0B & (1 << RXEN0))   /* perform only if rx is currently enabled */
	{
		UCSR0B &= ~(1 << RXEN0);
/*		uint16_t s = sizeof(rx_buffer); // test */
		memset(rx_buffer, 0, sizeof(rx_buffer));
/*		if(s) s = 0; // test */
		UCSR0B |= (1 << RXEN0);
	}
}

void linkbus_init(uint32_t baud)
{
	memset(rx_buffer, 0, sizeof(rx_buffer));
	/*Set baud rate */
	uint16_t myubrr = MYUBRR(baud);
	UBRR0H = (uint8_t)(myubrr >> 8);
	UBRR0L = (uint8_t)myubrr;
	/* Enable receiver and transmitter and related interrupts */
	UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
/*	UCSR0B = (1<<RXEN0) | (1<<TXEN0); */
	/* Set frame format: 8data, 2stop bit */
	UCSR0C = (1 << USBS0) | (3 << UCSZ00);
	g_bus_disabled = FALSE;
}

void linkbus_disable(void)
{
	uint8_t bufferIndex;
	
	g_bus_disabled = TRUE;
	UCSR0B = 0;
	linkbus_end_tx();
	memset(rx_buffer, 0, sizeof(rx_buffer));
	
	for(bufferIndex=0; bufferIndex<LINKBUS_NUMBER_OF_TX_MSG_BUFFERS; bufferIndex++)
	{
		tx_buffer[bufferIndex][0] = '\0';
	}
}

void linkbus_setTerminalMode(BOOL on)
{
	g_lb_terminal_mode = on;

	if(g_lb_terminal_mode)
	{
		linkbus_setLineTerm("\n");
		linkbus_send_text(lineTerm);
	}
	else
	{
		linkbus_send_text((char*)crlf);
	}
}

BOOL linkbus_send_text(char* text)
{
	BOOL err = TRUE;
	
	if(g_bus_disabled) return err;

	if(text)
	{
		LinkbusTxBuffer* buff = nextEmptyTxBuffer();

		while(!buff)
		{
			while(linkbusTxInProgress())
			{
				;   /* wait until transmit finishes */
			}
			buff = nextEmptyTxBuffer();
		}

		if(buff)
		{
			sprintf(*buff, text);

			linkbus_start_tx();
			err = FALSE;
		}
	}

	return(err);
}

void lb_send_WDTError(void)
{
	linkbus_send_text((char*)textWDT);
}

/***********************************************************************
 * lb_send_Help(void)
 ************************************************************************/
void lb_send_Help(void)
{
	if(g_bus_disabled) return;
	if(!g_lb_terminal_mode) return;

#ifdef DEBUG_FUNCTIONS_ENABLE
	sprintf(g_tempMsgBuff, "\n*** %s Debug Ver. %s ***", PRODUCT_NAME_LONG, SW_REVISION);
#else
	sprintf(g_tempMsgBuff, "\n*** %s Ver. %s ***", PRODUCT_NAME_LONG, SW_REVISION);
#endif
	
	while(linkbus_send_text(g_tempMsgBuff));
	while(linkbusTxInProgress());

#ifdef TRANQUILIZE_WATCHDOG
	sprintf(g_tempMsgBuff, "\nNote: Watchdog disabled in this build!");
	while(linkbus_send_text(g_tempMsgBuff)); 
	while(linkbusTxInProgress());
#endif // TRANQUILIZE_WATCHDOG
	
	int rows = sizeof(textHelp)/sizeof(textHelp[0]);
	for(int i=0; i<rows; i++)
	{
		while(linkbus_send_text((char*)textHelp[i])); 
		while(linkbusTxInProgress());
	}
	
	lb_send_NewLine();
}


/***********************************************************************************
 *  Support for creating and sending various Terminal Mode Linkbus messages is provided below.
 ************************************************************************************/

void lb_send_NewPrompt(void)
{
	if(g_lb_terminal_mode)
	{
		linkbus_send_text((char*)textPrompt);
	}
	else
	{
		linkbus_send_text((char*)crlf);
	}
}

void lb_send_NewLine(void)
{
	linkbus_send_text((char*)crlf);
}

void linkbus_setLineTerm(char* term)
{
	sprintf(lineTerm, term);
}

void lb_echo_char(uint8_t c)
{
	g_tempMsgBuff[0] = c;
	g_tempMsgBuff[1] = '\0';
	linkbus_send_text(g_tempMsgBuff);
}

BOOL lb_send_string(char* str)
{
	if(str == NULL) return TRUE;
	if(strlen(str) > LINKBUS_MAX_MSG_LENGTH) return TRUE;
	strncpy(g_tempMsgBuff, str, LINKBUS_MAX_MSG_LENGTH);
	linkbus_send_text(g_tempMsgBuff);
	return FALSE;
}

void lb_send_value(uint16_t value, char* label)
{
	sprintf(g_tempMsgBuff, "> %s=%d%s", label, value, lineTerm);
	linkbus_send_text(g_tempMsgBuff);
}

/***********************************************************************************
 *  Support for creating and sending various Linkbus messages is provided below.
 ************************************************************************************/

void lb_send_FRE(LBMessageType msgType, Frequency_Hz freq, BOOL isMemoryValue)
{
	BOOL valid = TRUE;
	char f[10] = "\0";
	char prefix = '$';
	char terminus = ';';

	if(freq != FREQUENCY_NOT_SPECIFIED)
	{
		if(freq < ILLEGAL_MEMORY)   /* Memory locations are MEM1, MEM2, MEM3, ... ILLEGAL_MEMORY-1 */
		{
			sprintf(f, "M%d", (int)freq);
		}
		else
		{
			sprintf(f, "%ld", freq);
		}
	}

	if(msgType == LINKBUS_MSG_REPLY)
	{
		prefix = '!';
	}
	else if(msgType == LINKBUS_MSG_QUERY)
	{
		terminus = '?';
	}
	else if(msgType != LINKBUS_MSG_COMMAND)
	{
		valid = FALSE;
	}

	if(valid)
	{
		if(g_lb_terminal_mode)
		{
			if(isMemoryValue)
			{
				sprintf(g_tempMsgBuff, "> %s (MEM)%s", f, lineTerm);
			}
			else
			{
				sprintf(g_tempMsgBuff, "> %s%s", f, lineTerm);
			}
		}
		else
		{
			sprintf(g_tempMsgBuff, "%cFRE,%s,%s%c", prefix, f, isMemoryValue ? "M" : NULL, terminus);
		}

		linkbus_send_text(g_tempMsgBuff);
	}
}


void lb_send_msg(LBMessageType msgType, char* msgLabel, char* msgStr)
{
	char prefix = '$';
	char terminus = ';';

	if(msgType == LINKBUS_MSG_REPLY)
	{
		prefix = '!';
	}
	else if(msgType == LINKBUS_MSG_QUERY)
	{
		terminus = '?';
	}

	if(g_lb_terminal_mode)
	{
		sprintf(g_tempMsgBuff, "> %s=%s%s", msgLabel, msgStr, lineTerm);
	}
	else
	{
		sprintf(g_tempMsgBuff, "%c%s,%s%c", prefix, msgLabel, msgStr, terminus);
	}

	linkbus_send_text(g_tempMsgBuff);
}


void lb_send_BND(LBMessageType msgType, RadioBand band)
{
	char b[4];
	BOOL valid = TRUE;
	char prefix = '$';
	char terminus = ';';

	if(msgType == LINKBUS_MSG_REPLY)
	{
		prefix = '!';
	}
	else if(msgType == LINKBUS_MSG_QUERY)
	{
		terminus = '?';
	}
	else if(msgType != LINKBUS_MSG_COMMAND)
	{
		valid = FALSE;
	}

	if(g_lb_terminal_mode)
	{
		sprintf(b, "%s", band == BAND_2M ? "2m" : "80m");
	}
	else
	{
		sprintf(b, "%d", band);
	}

	if(valid)
	{
		if(g_lb_terminal_mode)
		{
			sprintf(g_tempMsgBuff, "> BND=%s%s", b, lineTerm);
		}
		else
		{
			sprintf(g_tempMsgBuff, "%cBND,%s%c", prefix, b, terminus);
		}

		linkbus_send_text(g_tempMsgBuff);
	}
}

void lb_send_sync(void)
{
	sprintf(g_tempMsgBuff, ".....");
	linkbus_send_text(g_tempMsgBuff);
}


void lb_broadcast_num(uint16_t data, char* str)
{
	char t[6] = "\0";

	sprintf(t, "%u", data);
	g_tempMsgBuff[0] = '\0';

	if(g_lb_terminal_mode)
	{
		if(str)
		{
			sprintf(g_tempMsgBuff, "> %s=%s%s", str, t, lineTerm);
		}
		else
		{
			sprintf(g_tempMsgBuff, "> %s%s", t, lineTerm);
		}
	}
	else
	{
		if(str)
		{
			sprintf(g_tempMsgBuff, "%s,%s;", str, t);
		}
	}

	if(g_tempMsgBuff[0]) linkbus_send_text(g_tempMsgBuff);
}

