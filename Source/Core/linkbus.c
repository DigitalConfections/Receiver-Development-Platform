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
static BOOL g_lb_terminal_mode = FALSE;
static const char crlf[] = "\n";
static char lineTerm[8] = "\n";
static const char textPrompt[] = "RDP> ";
static const char textWDT[] = "*** WDT Reset! ***\n";
//static const char textHelp1[] = "\nCommands:\n";
//static const char textHelp2[] = ">  B                 - Battery\n";
//static const char textHelp3[] = ">  BND [2|80]        - Rx Band\n";
//static const char textHelp4[] = ">  FRE [Hz]          - Rx Freq\n";
//static const char textHelp5[] = ">  FRE M<1:5> [Hz]   - Rx Mem\n";
//static const char textHelp6[] = ">  S                 - RSSI\n";
//static const char textHelp7[] = ">  TIM [hh:mm:ss]    - RTC Time\n";
//static const char textHelp8[] = ">  VOL <M:T> [0-100] - Main/Tone Vol\n";
//static const char textHelp9[] = ">  ?                 - Info\n";

static const char textHelp[][40] = { "\nCommands:\n",
">  B                 - Battery\n",
">  BND [2|80]        - Rx Band\n",
">  FRE [Hz]          - Rx Freq\n",
">  FRE M<1:5> [Hz]   - Rx Mem\n",
">  O [Hz]            - CW Offset\n",
">  S[S]              - RSSI\n",
">  TIM [hh:mm:ss]    - RTC Time\n",
">  VOL <M:T> [0-100] - Main/Tone Vol\n",
">  P                 - Perm\n",
">  RST               - Reset\n",
">  ?                 - Info\n"
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

void linkbus_init(void)
{
	memset(rx_buffer, 0, sizeof(rx_buffer));
	/*Set baud rate */
	UBRR0H = ((unsigned char)(MYUBRR >> 8));
	UBRR0L = (unsigned char)MYUBRR;
	/* Enable receiver and transmitter and related interrupts */
	UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
/*	UCSR0B = (1<<RXEN0) | (1<<TXEN0); */
	/* Set frame format: 8data, 2stop bit */
	UCSR0C = (1 << USBS0) | (3 << UCSZ00);
}

BOOL linkbus_toggleTerminalMode(void)
{
	g_lb_terminal_mode = !g_lb_terminal_mode;

	if(g_lb_terminal_mode)
	{
		linkbus_setLineTerm("\n");
		linkbus_send_text(lineTerm);
	}
	else
	{
		linkbus_send_text((char*)crlf);
	}

	return(g_lb_terminal_mode);
}

BOOL linkbus_send_text(char* text)
{
	BOOL err = TRUE;

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
#ifdef DEBUG_FUNCTIONS_ENABLE
	sprintf(g_tempMsgBuff, "\n*** %s Debug Ver. %s ***", PRODUCT_NAME_LONG, SW_REVISION);
#else
	sprintf(g_tempMsgBuff, "\n*** %s Ver. %s ***", PRODUCT_NAME_LONG, SW_REVISION);
#endif
	
	while(linkbus_send_text(g_tempMsgBuff)); 
	while(linkbusTxInProgress());
	
	for(int i=0; i<12; i++)
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
	linkbus_send_text((char*)textPrompt);
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

void lb_poweroff_msg(uint8_t sec)
{
	sprintf(g_tempMsgBuff, "Power off in %d sec\n", sec);
	linkbus_send_text(g_tempMsgBuff);
}

void lb_send_value(uint16_t value, char* label)
{
	sprintf(g_tempMsgBuff, "> %s=%d%s", label, value, lineTerm);
	linkbus_send_text(g_tempMsgBuff);
}

/***********************************************************************************
 *  Support for creating and sending various Linkbus messages is provided below.
 ************************************************************************************/

#if PRODUCT_TEST_INSTRUMENT_HEAD

	void lb_send_CKn(LBMessageType msgType, Si5351_clock clock, Frequency_Hz freq, Si5351_clock_enable enabled, Si5351_drive drive)
	{
		BOOL valid = TRUE;
		char f[10] = "\0";
		char e[2] = "\0";
		char d[2] = "\0";
		char prefix = '$';
		char terminus = ';';

		if(freq > FREQUENCY_NOT_SPECIFIED)
		{
			sprintf(f, "%9ld", freq);
		}
		if(enabled != SI5351_ENABLE_NOT_SPECIFIED)
		{
			sprintf(e, "%d", enabled);
		}
		if(drive != SI5351_DRIVE_NOT_SPECIFIED)
		{
			sprintf(d, "%d", drive);
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
			sprintf(g_tempMsgBuff, "%cCK%d,%s,%s,%s%c", prefix, clock, f, e, d, terminus);
			linkbus_send_text(g_tempMsgBuff);
		}
	}

#endif  /* PRODUCT_TEST_INSTRUMENT_HEAD */

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


void lb_send_TIM(LBMessageType msgType, int32_t time)
{
	BOOL valid = TRUE;
	char t[10] = "\0";
	char prefix = '$';
	char terminus = ';';

	if(time != NO_TIME_SPECIFIED)
	{
		if(g_lb_terminal_mode)
		{
			timeValToString(t, time, HourMinuteSecondFormat);
		}
		else
		{
			sprintf(t, "%ld", time);
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
			sprintf(g_tempMsgBuff, "> TIME=%s%s", t, lineTerm);
		}
		else
		{
			sprintf(g_tempMsgBuff, "%cTIM,%s%c", prefix, t, terminus);
		}

		linkbus_send_text(g_tempMsgBuff);
	}
}


void lb_send_VOL(LBMessageType msgType, VolumeType type, VolumeSetting volume)
{
	BOOL valid = TRUE;
	char t[2] = "\0";
	char v[4] = "\0";
	char prefix = '$';
	char terminus = ';';

	if(type == TONE_VOLUME)
	{
		t[0] = 'T';
	}
	else if(type == MAIN_VOLUME)
	{
		t[0] = 'M';
	}
	else
	{
		valid = FALSE;
	}

	if(valid)
	{
		if(volume < DECREMENT_VOL)
		{
			sprintf(v, "%d", volume * 10);
		}
		else if(volume < VOL_NOT_SPECIFIED)
		{
			if(volume == INCREMENT_VOL)
			{
				v[0] = '+';
			}
			else
			{
				v[0] = '-';
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
			sprintf(g_tempMsgBuff, "%cVOL,%s,%s%c", prefix, t, v, terminus);
			linkbus_send_text(g_tempMsgBuff);
		}
	}
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

void lb_send_BCR(LBbroadcastType bcType, BOOL start)
{
	char t[4] = "\0";
	char prefix = '$';
	char terminus = ';';

	sprintf(t, "%d", bcType);

	if(start)
	{
		terminus = '?';
	}

	sprintf(g_tempMsgBuff, "%cBCR,%s%c", prefix, t, terminus);
	linkbus_send_text(g_tempMsgBuff);
}

void lb_send_ID(LBMessageType msgType, DeviceID myID, DeviceID otherID)
{
	char prefix = '$';
	char m[4] = "\0";
	char o[4] = "\0";

	if(msgType == LINKBUS_MSG_REPLY)
	{
		prefix = '!';
	}

	if(myID != NO_ID)
	{
		sprintf(m, "%d", myID);
	}
	if(otherID != NO_ID)
	{
		sprintf(o, "%d", otherID);
	}

	sprintf(g_tempMsgBuff, "%cID,%s,%s;", prefix, m, o);
	linkbus_send_text(g_tempMsgBuff);
}

void lb_send_sync(void)
{
	sprintf(g_tempMsgBuff, ".....");
	linkbus_send_text(g_tempMsgBuff);
}

void lb_broadcast_bat(uint16_t data)
{
	char t[4] = "\0";

	sprintf(t, "%d", data);

	if(g_lb_terminal_mode)
	{
		sprintf(g_tempMsgBuff, "> BAT=%s%s", t, lineTerm);
	}
	else
	{
		sprintf(g_tempMsgBuff, "!B,%s;", t);
	}

	linkbus_send_text(g_tempMsgBuff);
}

void lb_broadcast_rssi(uint16_t data)
{
	char t[4] = "\0";

	sprintf(t, "%d", data);

	if(g_lb_terminal_mode)
	{
		sprintf(g_tempMsgBuff, "> RSSI=%s%s", t, lineTerm);
	}
	else
	{
		sprintf(g_tempMsgBuff, "!S,%s;", t);
	}

	linkbus_send_text(g_tempMsgBuff);
}

void lb_broadcast_rf(uint16_t data)
{
	char t[4] = "\0";

	sprintf(t, "%d", data);

	if(g_lb_terminal_mode)
	{
		sprintf(g_tempMsgBuff, "> RF=%s%s", t, lineTerm);
	}
	else
	{
		sprintf(g_tempMsgBuff, "!R,%s;", t);
	}
	linkbus_send_text(g_tempMsgBuff);
}

void lb_broadcast_temp(uint16_t data)
{
	char t[4] = "\0";

	sprintf(t, "%d", data);

	if(g_lb_terminal_mode)
	{
		sprintf(g_tempMsgBuff, "> T=%s%s", t, lineTerm);
	}
	else
	{
		sprintf(g_tempMsgBuff, "!T,%s;", t);
	}

	linkbus_send_text(g_tempMsgBuff);
}

