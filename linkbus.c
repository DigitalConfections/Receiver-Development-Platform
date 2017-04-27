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
#include <string.h>
#include <stdio.h>


// Local function prototypes
BOOL linkbus_start_tx(void);

// Module global variables
static BOOL linkbus_tx_active = FALSE;
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
		if(bufferIndex >= LINKBUS_NUMBER_OF_TX_MSG_BUFFERS) bufferIndex = 0;
	}
	
	if(found) return &tx_buffer[bufferIndex];
	
	return NULL;
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
		if(bufferIndex >= LINKBUS_NUMBER_OF_TX_MSG_BUFFERS) bufferIndex = 0;
	}
	
	if(found) return &tx_buffer[bufferIndex];
	
	return NULL;
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
		if(bufferIndex >= LINKBUS_NUMBER_OF_RX_MSG_BUFFERS) bufferIndex = 0;
	}
	
	if(found) return &rx_buffer[bufferIndex];
	
	return NULL;
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
		if(bufferIndex >= LINKBUS_NUMBER_OF_RX_MSG_BUFFERS) bufferIndex = 0;
	}
	
	if(found) return &rx_buffer[bufferIndex];
	
	return NULL;
}


BOOL linkbusTxInProgress(void)
{
	return linkbus_tx_active;
}

BOOL linkbus_start_tx(void)
{
	BOOL success = !linkbus_tx_active;
	
	if(success) // message will be lost if transmit is busy
	{
		UCSR0B |= (1 << UDRIE0);
		linkbus_tx_active = TRUE;
	}
	
	return success;
}

void linkbus_end_tx(void)
{
	UCSR0B &= ~(1 << UDRIE0);
	linkbus_tx_active = FALSE;
}

void linkbus_reset_rx(void)
{
	if(UCSR0B & (1 << RXEN0)) // perform only if rx is currently enabled
	{
		UCSR0B &= ~(1 << RXEN0);
//		uint16_t s = sizeof(rx_buffer); // test
		memset(rx_buffer, 0, sizeof(rx_buffer));
//		if(s) s = 0; // test
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
//	UCSR0B = (1<<RXEN0) | (1<<TXEN0);
	/* Set frame format: 8data, 2stop bit */
	UCSR0C = (1<<USBS0)|(3<<UCSZ00);
}

/***********************************************************************************
Support for creating and sending various messages is provided below.
************************************************************************************/

#if PRODUCT_TEST_INSTRUMENT_HEAD

void lb_send_CKn(LBMessageType msgType, Si5351_clock clock, Frequency_Hz freq, Si5351_clock_enable enabled, Si5351_drive drive)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	BOOL valid = TRUE;
	char f[10] = "\0";
	char e[2] = "\0";
	char d[2] = "\0";
	char prefix = '$';
	char terminus = ';';
				
	if(freq > FREQUENCY_NOT_SPECIFIED) sprintf(f, "%9ld", freq);
	if(enabled != SI5351_ENABLE_NOT_SPECIFIED) sprintf(e, "%d", enabled);
	if(drive != SI5351_DRIVE_NOT_SPECIFIED) sprintf(d, "%d", drive);
	
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
		sprintf(*buff, "%cCK%d,%s,%s,%s%c", prefix, clock, f, e, d, terminus);
		linkbus_start_tx();
	}
}

#endif // PRODUCT_TEST_INSTRUMENT_HEAD

void lb_send_SFQ(LBMessageType msgType, Frequency_Hz freq, BOOL isMemoryValue)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	BOOL valid = TRUE;
	char f[10] = "\0";
	char prefix = '$';
	char terminus = ';';
	
	if(freq != FREQUENCY_NOT_SPECIFIED)
	{
		if(freq < ILLEGAL_MEMORY) // Memory locations are MEM1, MEM2, MEM3, ... ILLEGAL_MEMORY-1
		{
			sprintf(f, "M%d", (int)freq);
		}
		else
		{
			sprintf(f, "%9ld", freq);
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
		sprintf(*buff, "%cSFQ,%s,%s%c", prefix, f, isMemoryValue ? "M":NULL, terminus);
		linkbus_start_tx();
	}
}


void lb_send_TIM(LBMessageType msgType, int32_t time)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	BOOL valid = TRUE;
	char t[10] = "\0";
	char prefix = '$';
	char terminus = ';';
	
	if(time != NO_TIME_SPECIFIED)
	{
		sprintf(t, "%ld", time);
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
		sprintf(*buff, "%cTIM,%s%c", prefix, t, terminus);
		linkbus_start_tx();
	}
}


void lb_send_VOL(LBMessageType msgType, VolumeType type, VolumeSetting volume)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
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
			sprintf(v, "%d", volume*10);
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
			sprintf(*buff, "%cVOL,%s,%s%c", prefix, t, v, terminus);
			linkbus_start_tx();
		}
	}
}

void lb_send_BND(LBMessageType msgType, RadioBand band)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	char b[2];
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
	
	sprintf(b, "%d", band);
	
	if(valid)
	{
		sprintf(*buff, "%cBND,%s%c", prefix, b, terminus);
		linkbus_start_tx();
	}
}

void lb_send_BCR(LBbroadcastType bcType, BOOL start)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	char t[4] = "\0";
	char prefix = '$';
	char terminus = ';';
	
	sprintf(t, "%d", bcType);
	
	if(start)
	{
		terminus = '?';
	}
	
	sprintf(*buff, "%cBCR,%s%c", prefix, t, terminus);
	linkbus_start_tx();
}

void lb_send_ID(LBMessageType msgType, DeviceID myID, DeviceID otherID)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent

	char prefix = '$';
	char m[4] = "\0";
	char o[4] = "\0";	
	
	if(msgType == LINKBUS_MSG_REPLY)
	{
		prefix = '!';
	}
	
	if(myID != NO_ID) sprintf(m, "%d", myID);
	if(otherID != NO_ID) sprintf(o, "%d", otherID);
	
	sprintf(*buff, "%cID,%s,%s;", prefix, m, o);
	linkbus_start_tx();
}

void lb_send_sync(void)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	sprintf(*buff, ".....");
	linkbus_start_tx();
}

void lb_broadcast_bat(uint16_t data)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	char t[4] = "\0";
	sprintf(t, "%d", data);
	sprintf(*buff, "!B,%s;", t);
	linkbus_start_tx();
}

void lb_broadcast_rssi(uint16_t data)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	char t[4] = "\0";
	sprintf(t, "%d", data);
	sprintf(*buff, "!S,%s;", t);
	linkbus_start_tx();
}

void lb_broadcast_rf(uint16_t data)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	char t[4] = "\0";
	sprintf(t, "%d", data);
	sprintf(*buff, "!R,%s;", t);
	linkbus_start_tx();
}

void lb_broadcast_temp(uint16_t data)
{
	LinkbusTxBuffer* buff = nextEmptyTxBuffer();
	if(!buff) return; // message will not get sent
	
	char t[4] = "\0";
	sprintf(t, "%d", data);
	sprintf(*buff, "!T,%s;", t);
	linkbus_start_tx();
}

