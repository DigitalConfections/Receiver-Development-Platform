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
 * linkbus.h - a simple serial inter-processor communication protocol.
 */

#ifndef LINKBUS_H_
#define LINKBUS_H_

#include "defs.h"
#include "receiver.h"
#include "si5351.h"

#define LINKBUS_MAX_MSG_LENGTH 50
#define LINKBUS_MIN_MSG_LENGTH 3    /* shortest message: $TTY; */
#define LINKBUS_MAX_MSG_FIELD_LENGTH 21
#define LINKBUS_MAX_MSG_NUMBER_OF_FIELDS 3
#define LINKBUS_NUMBER_OF_RX_MSG_BUFFERS 2
#define LINKBUS_NUMBER_OF_TX_MSG_BUFFERS 4

#define LINKBUS_MIN_TX_INTERVAL_MS 100

#define FOSC 8000000    /* Clock Speed */
#define BAUD 9600
#define MYUBRR (FOSC / 16 / BAUD - 1)

typedef enum
{
	EMPTY_BUFF,
	FULL_BUFF
} BufferState;

/*  Linkbus Messages
 *       Message formats:
 *               $id,f1,f2... fn;
 *               !id,f1,f2,... fn;
 *               $id,f1,f2,... fn?
 *
 *               where
 *                       $ = command - ! indicates a response or broadcast to subscribers
 *                       id = linkbus MessageID
 *                       fn = variable length fields
 *                       ; = end of message flag - ? = end of query
 *       Null fields in settings commands indicates no change should be applied
 *       All null fields indicates a polling request for current settings
 *       ? terminator indicates subscription request to value changes
 *       Sending a query with fields containing data, is the equivalent of sending
 *         a command followed by a query (i.e., a response is requested).
 *
 *       TEST EQUIPMENT MESSAGE FAMILY (DEVICE MESSAGING)
 *       $TST - Test message
 *       !ACK - Simple acknowledgment to a command (sent when required)
 *       $CK0 - Set Si5351 CLK0: field1 = freq (Hz); field2 = enable (BOOL)
 *       $CK1 - Set Si5351 CLK1: field1 = freq (Hz); field2 = enable (BOOL)
 *       $CK2 - Set Si5351 CLK2: field1 = freq (Hz); field2 = enable (BOOL)
 *       $VOL - Set audio volume: field1 = inc/decr (BOOL); field2 = % (int)
 *       $BAT? - Subscribe to battery voltage reports
 *
 *       DUAL-BAND RX MESSAGE FAMILY (FUNCTIONAL MESSAGING)
 *       $BND  - Set/Get radio band to 2m or 80m
 *       $S?   - Subscribe to signal strength reports
 *                 - Subscribe to gain setting reports
 *                 -
 */

typedef enum
{
	MESSAGE_EMPTY = 0,

	/* STARTUP MESSAGES */
	MESSAGE_ID = 'I' * 10 + 'D',                    /* Send self ID, and ack rec'd ID: $ID,,; // field1=MyID; field2=OtherID */

	/* TEST EQUIPMENT MESSAGE FAMILY (TEST DEVICE MESSAGING) */
	MESSAGE_BAND = 'B' * 100 + 'N' * 10 + 'D',      /* $BND,; / $BND? / !BND,; // Set band; field1 = RadioBand */
	MESSAGE_SETCLK0 = 'C' * 100 + 'K' * 10 + '0',   /* $CK0,Fhz,on; // FHz=freq_in_Hz nc=null; on=1 off=0 nc=null */
	MESSAGE_SETCLK1 = 'C' * 100 + 'K' * 10 + '1',   /* $CK1,Fhz,on; // FHz=freq_in_Hz nc=null; on=1 off=0 nc=null */
	MESSAGE_SETCLK2 = 'C' * 100 + 'K' * 10 + '2',   /* $CK2,Fhz,on; // FHz=freq_in_Hz nc=null; on=1 off=0 nc=null */
	MESSAGE_BATTERY = 'B' * 100 + 'A' * 10 + 'T',   /* $BAT? / !BAT; // Subscribe to battery voltage reports */
	MESSAGE_VOLUME  = 'V' * 100 + 'O' * 10 + 'L',   /* $VOL,,; / $VOL? / !VOL,,; // Set volume: field1 = VolumeType; field2 = Setting */
	MESSAGE_BCR = 'B' * 100 + 'C' * 10 + 'R',       /* Broadcast Request: $BCR,? $BCR,; // Start and stop broadcasts of data identified in field1 */
	MESSAGE_TTY = 'T' * 100 + 'T' * 10 + 'Y',       /* Adjust for PC communications interface (add crlf, etc.) */

	/* CONTROL HEAD MESSAGE FAMILY */

	/*	DUAL-BAND RX MESSAGE FAMILY (FUNCTIONAL MESSAGING) */
	MESSAGE_SET_FREQ = 'F' * 100 + 'R' * 10 + 'E',  /* $FRE,Fhz; / $FRE,FHz? / !FRE,; // Set/request current receive frequency */
	MESSAGE_TIME = 'T' * 100 + 'I' * 10 + 'M',      /* $TIM,time; / !TIM,time; / $TIM,? // Set/request RTC time */
	MESSAGE_BAT_BC = 'B',                           /* Battery broadcast data */
	MESSAGE_RSSI_BC = 'S',                          /* RSSI broadcast data */
	MESSAGE_RSSI_REPEAT_BC = 'S' * 10 + 'S',		/* RSSI repeat broadcast toggle */
	MESSAGE_RF_BC = 'R',                            /* RF level broadcast data */
	MESSAGE_TEMPERATURE_BC = 'T',                   /* Temperature broadcast data */

	/* TTY USER MESSAGES */
	MESSAGE_ALL_INFO = '?',                         /* Prints all receiver info */
	
	MESSAGE_PERM = 'P',
	MESSAGE_CW_OFFSET = 'O',
	MESSAGE_RESET = 'R' * 100 + 'S' * 10 + 'T',		/* Processor reset */

#ifdef DEBUG_FUNCTIONS_ENABLE
	MESSAGE_DEBUG = 'D',							/* Used for debug only */
#endif

	INVALID_MESSAGE = UINT16_MAX					/* This value must never overlap a valid message ID */
} LBMessageID;

typedef enum
{
	LINKBUS_MSG_UNKNOWN = 0,
	LINKBUS_MSG_COMMAND,
	LINKBUS_MSG_QUERY,
	LINKBUS_MSG_REPLY,
	LINKBUS_MSG_INVALID
} LBMessageType;

typedef enum
{
	FIELD1 = 0,
	FIELD2 = 1,
	FIELD3 = 2
} LBMessageField;

typedef enum
{
	BATTERY_BROADCAST = 0x0001,
	RSSI_BROADCAST = 0x0002,
	RF_BROADCAST = 0x0004,
	UPC_TEMP_BROADCAST = 0x0008,
	ALL_BROADCASTS = 0x000FF
} LBbroadcastType;

typedef enum
{
	NO_ID = 0,
	CONTROL_HEAD_ID = 1,
	RECEIVER_ID = 2
} DeviceID;

typedef char LinkbusTxBuffer[LINKBUS_MAX_MSG_LENGTH];

typedef struct
{
	LBMessageType type;
	LBMessageID id;
	char fields[LINKBUS_MAX_MSG_NUMBER_OF_FIELDS][LINKBUS_MAX_MSG_FIELD_LENGTH];
} LinkbusRxBuffer;

#define WAITING_FOR_UPDATE -1

/**
 */
void linkbus_init(void);

/**
 */
void linkbus_end_tx(void);

/**
 */
void linkbus_reset_rx(void);

/**
 */
LinkbusTxBuffer* nextEmptyTxBuffer(void);

/**
 */
LinkbusTxBuffer* nextFullTxBuffer(void);

/**
 */
BOOL linkbusTxInProgress(void);

/**
 */
LinkbusRxBuffer* nextEmptyRxBuffer(void);

/**
 */
LinkbusRxBuffer* nextFullRxBuffer(void);

/**
 */
void lb_send_sync(void);

/**
 */
BOOL linkbus_toggleTerminalMode(void);

/**
 */
void lb_send_ID(LBMessageType msgType, DeviceID myID, DeviceID otherID);

/**
 */
void lb_send_FRE(LBMessageType msgType, Frequency_Hz freq, BOOL isMemoryValue);

/**
 */
void lb_send_TIM(LBMessageType msgType, int32_t time);

/**
 */
void lb_send_BND(LBMessageType msgType, RadioBand band);

/**
 */
void lb_send_VOL(LBMessageType msgType, VolumeType type, VolumeSetting volume);

/**
 */
void lb_send_BCR(LBbroadcastType bcType, BOOL start);

/**
 */
void lb_broadcast_bat(uint16_t data);

/**
 */
void lb_broadcast_rssi(uint16_t data);

/**
 */
void lb_broadcast_rf(uint16_t data);

/**
 */
void lb_broadcast_temp(uint16_t data);

/**
 */
void lb_send_NewLine(void);

/**
 *  Sends RDP> to terminal
 */
void lb_send_NewPrompt(void);

/**
 */
void lb_send_Help(void);

/**
 */
void lb_send_WDTError(void);

/**
 */
void linkbus_setLineTerm(char* term);

/**
 */
void lb_poweroff_msg(uint8_t sec);

/**
 */
void lb_send_value(uint16_t value, char* label);


/**
 */
void lb_echo_char(uint8_t c);

#if PRODUCT_TEST_INSTRUMENT_HEAD
/**
 */
	void lb_send_CKn(LBMessageType msgType, Si5351_clock clock, Frequency_Hz freq, Si5351_clock_enable enabled, Si5351_drive drive);
#endif  /* PRODUCT_TEST_INSTRUMENT_HEAD */

#endif  /* LINKBUS_H_ */
