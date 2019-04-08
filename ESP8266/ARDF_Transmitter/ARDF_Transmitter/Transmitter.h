/**********************************************************************************************
   Copyright � 2019 Digital Confections LLC

   Permission is hereby granted, free of charge, to any person obtaining a copy of
   this software and associated documentation files (the "Software"), to deal in the
   Software without restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
   and to permit persons to whom the Software is furnished to do so, subject to the
   following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
   FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

 **********************************************************************************************/

#ifndef _TRANSMITTER_H_
#define _TRANSMITTER_H_

#include <Arduino.h>

/*
      NORMAL WIFI POWER UP SEQUENCE

      WiFi wakes up
      WiFi reads all data from FS and completes initialization tasks
      WiFi sends WiFiReady message to ATMEGA

      ATMEGA replies with $TIM message containing the current time

      WiFi updates time
      WiFi checks to see if any events are scheduled

      If no event is scheduled
        WiFi sends EventConfig=NULL message to ATMEGA
      If event is scheduled
        WiFi sends EventConfig message to ATMEGA that includes start and finish times

      WiFi waits for WiFi connections, further commands, or power down.


      NORMAL ATMEGA POWER UP SEQUENCE

      ATMEGA wakes up
      ATMEGA reads all permed data and completes initialization tasks
      ATMEGA powers up WiFi
      ATMEGA receives WiFiReady message from WiFi
      ATMEGA replies with a message containing the current time
      ATMEGA receives EventConfig message from WiFi

      If no event is scheduled
        ATMEGA waits for two minutes of inactivity before shutting down WiFi
      If event is scheduled
        ATMEGA configures hardware for event

      If event is in progress
        ATMEGA requests WiFi to configure it for transmissions
        ATMEGA begins transmissions as instructed by data received from WiFi
      If event is scheduled for the future
        ATMEGA configures RTC alarm to awaken ATMEGA prior to event start time
        ATMEGA powers off WiFi and puts hardware into sleep state

      End of event time is reached
      ATMEGA powers off the transmitter hardware and places itself to sleep

      NORMAL ATMEGA WAKE-UP SEQUENCE

      Interrupt awakens ATMEGA
      Same as NORMAL ATMEGA POWER UP SEQUENCE

*/

#define TRANSMITTER_DEBUG_PRINTS_OVERRIDE false

#define FULLY_CHARGED_BATTERY_mV 4200.
#define FULLY_DEPLETED_BATTERY_mV 3200.

#define MASTER_OR_CLONE_SETTING "MASTER_SETTING"

#define TX_MAX_ALLOWED_POWER_MW 2000
#define TX_MAX_ALLOWED_FREQUENCY_HZ 148000000
#define TX_MIN_ALLOWED_FREQUENCY_HZ 3500000

/*******************************************************/
/* Error Codes                                                                   */
/*******************************************************/
typedef enum {
	ERROR_CODE_NO_ERROR = 0x00,
	ERROR_CODE_EVENT_MISSING_TRANSMIT_DURATION = 0xCB,
	ERROR_CODE_EVENT_MISSING_START_TIME = 0xCC,
	ERROR_CODE_EVENT_NOT_CONFIGURED = 0xCD,
    ERROR_CODE_ILLEGAL_COMMAND_RCVD = 0xCE,
    ERROR_CODE_SW_LOGIC_ERROR = 0xCF,
	ERROR_CODE_NO_ANTENNA_FOR_BAND = 0xF7,
	ERROR_CODE_WD_TIMEOUT = 0xF8,
	ERROR_CODE_SUPPLY_VOLTAGE_ERROR = 0xF9,
	ERROR_CODE_BUCK_REG_OUTOFSPEC = 0xFA,
	ERROR_CODE_CLKGEN_NONRESPONSIVE = 0xFB,
	ERROR_CODE_RTC_NONRESPONSIVE = 0xFC,
	ERROR_CODE_DAC3_NONRESPONSIVE = 0xFD,
	ERROR_CODE_DAC2_NONRESPONSIVE = 0xFE,
	ERROR_CODE_DAC1_NONRESPONSIVE = 0xFF
	} EC;

/*******************************************************/
/* Status Codes                                                                 */
/*******************************************************/
typedef enum {
	STATUS_CODE_IDLE = 0x00,
	STATUS_CODE_RETURNED_FROM_SLEEP = 0xF9,
	STATUS_CODE_BEGINNING_XMSN_THIS_CYCLE = 0xFA,
	STATUS_CODE_SENDING_ID = 0xFB,
	STATUS_CODE_EVENT_FINISHED = 0xFC,
	STATUS_CODE_EVENT_STARTED_NOW_TRANSMITTING = 0xFD,
	STATUS_CODE_EVENT_STARTED_WAITING_FOR_TIME_SLOT = 0xFE,
	STATUS_CODE_WAITING_FOR_EVENT_START = 0xFF
	} SC;

// Websocket Command Messages
#define SOCK_COMMAND_ERROR "ERR_CODE" 
#define SOCK_COMMAND_STATUS "STATUS"
#define SOCK_COMMAND_EVENT_NAME "EVENT_NAME" /* read only */
#define SOCK_COMMAND_EVENT_FILE_VERSION "FILE_VERSION" /* read only */
#define SOCK_COMMAND_EVENT_DATA "EVENT_DATA" /* read only */
#define SOCK_COMMAND_SYNC_TIME "SYNC"
#define SOCK_COMMAND_TEMPERATURE "TEMP" /* read only */
#define SOCK_COMMAND_SSID "SSID" /* read only */
#define SOCK_COMMAND_BATTERY "BAT" /* read only */
#define SOCK_COMMAND_CLONE "CLONE"
#define SOCK_COMMAND_VERSION "VERS" /* read only */
#define SOCK_COMMAND_MAC "MAC" /* read only */
#define SOCK_COMMAND_CALLSIGN "CALLSIGN"
#define SOCK_COMMAND_PATTERN "PATTERN"
#define SOCK_COMMAND_BAND "BAND" /* read only */
#define SOCK_COMMAND_START_TIME "START_TIME"
#define SOCK_COMMAND_FINISH_TIME "FINISH_TIME"
#define SOCK_COMMAND_TYPE_NAME "TYPE_NAME" /* read only */
//#define SOCK_COMMAND_TYPE_TX_COUNT "TX_COUNT"  /* read only */
#define SOCK_COMMAND_TYPE_PWR "POWER"
#define SOCK_COMMAND_TYPE_MODULATION "MOD"
#define SOCK_COMMAND_TYPE_FREQ "FREQ"
//#define SOCK_COMMAND_TYPE_WPM "CODE_SPEED" /* read only */
//#define SOCK_COMMAND_TYPE_ID_INTERVAL "ID_INT" /* read only */
#define SOCK_COMMAND_TX_ROLE "TX_ROLE"
#define SOCK_COMMAND_TEST "TEST"
#define SOCK_COMMAND_REFRESH "REFRESH"
#define SOCK_COMMAND_KEY_DOWN "KEY_DOWN"
#define SOCK_COMMAND_KEY_UP "KEY_UP"
#define SOCK_COMMAND_SLEEP "SLEEP"
#define SOCK_COMMAND_PASSTHRU "PASS"

// LinkBus Messages
#define LB_MESSAGE_ERROR_CODE "EC"
#define LB_MESSAGE_STATUS_CODE "SC"
#define LB_MESSAGE_ESP "ESP"
#define LB_MESSAGE_ESP_WAKEUP "$ESP,0;" /* Wake up from reset */
//#define LB_MESSAGE_ESP_ACTIVE "$ESP,1;" /* Ready with active event data */
#define LB_MESSAGE_ESP_SAVE "$ESP,2;" /* Save settings changes to file (keeps power up) */
//#define LB_MESSAGE_ESP_SHUTDOWN "$ESP,3;" /* Shut down in 3 seconds */
#define LB_MESSAGE_ESP_KEEPALIVE "$ESP,Z;" /* Keep alive for 2 minutes */
#define LB_MESSAGE_TIME "TIM"
#define LB_MESSAGE_TIME_SET "$TIM," /* Prefix for sending RTC time setting to ATMEGA */
#define LB_MESSAGE_TIME_REQUEST "$TIM?" /* Request the current time */
#define LB_MESSAGE_TEMP "TEM"
#define LB_MESSAGE_TEMP_REQUEST "$TEM?" /* Request the current temperature */
#define LB_MESSAGE_BATTERY "BAT"
#define LB_MESSAGE_BATTERY_REQUEST "$BAT?" /* Request the current battery level */
#define LB_MESSAGE_CALLSIGN "ID"
#define LB_MESSAGE_CALLSIGN_SET "$ID," /* Prefix for sending the callsign/ID to ATMEGA */
#define LB_MESSAGE_STARTFINISH_TIME "SF"
#define LB_MESSAGE_STARTFINISH_SET_START "$SF,S," /* Prefix for sending start time to ATMEGA */
#define LB_MESSAGE_STARTFINISH_SET_FINISH "$SF,F," /* Prefix for sending finish time to ATMEGA */
#define LB_MESSAGE_TX_POWER "POW"
#define LB_MESSAGE_TX_POWER_SET "$POW,M," /* Prefix for sending tx power level (mW) to ATMEGA */
#define LB_MESSAGE_TX_MOD "MOD"
#define LB_MESSAGE_TX_MOD_SET "$MOD," /* Prefix for sending 2m tx modulation format (AM or CW) to ATMEGA */
#define LB_MESSAGE_TX_BAND "BND"
#define LB_MESSAGE_TX_BAND_SET "$BND," /* Prefix for sending band set command (2 or 80) to ATMEGA */
#define LB_MESSAGE_TX_FREQ "FRE"
#define LB_MESSAGE_TX_FREQ_SET "$FRE," /* Prefix for sending frequency setting (Hz) to ATMEGA */
#define LB_MESSAGE_PATTERN "PA"
#define LB_MESSAGE_PATTERN_SET "$PA," /* Prefix for sending Morse code pattern (text) to ATMEGA */
#define LB_MESSAGE_CODE_SPEED "SPD"
#define LB_MESSAGE_CODE_SPEED_SETID "$SPD,I," /* Prefix for sending callsign/ID code speed (WPM) to ATMEGA */
#define LB_MESSAGE_CODE_SPEED_SETPAT "$SPD,P," /* Prefix for sending pattern code speed (WPM) to ATMEGA */
#define LB_MESSAGE_TIME_INTERVAL "T"
#define LB_MESSAGE_TIME_INTERVAL_SET0 "$T,0," /* Prefix for sending time interval (sec) for off-air time to ATMEGA */
#define LB_MESSAGE_TIME_INTERVAL_SET1 "$T,1," /* Prefix for sending time interval (sec) for on-air time to ATMEGA */
#define LB_MESSAGE_TIME_INTERVAL_SETD "$T,D," /* Prefix for sending time interval (sec) for time-slot delay to ATMEGA */
#define LB_MESSAGE_TIME_INTERVAL_SETID "$T,I," /* Prefix for sending time interval (sec) for station identification to ATMEGA */
#define LB_MESSAGE_WIFI_COMS_OFF "$WI,2;" /* Tell ATMEGA to disable linkbus to support ESP8266 programming (no exit without power cycle) */
#define LB_MESSAGE_KEY_DOWN "$GO,1;" /* Tell ATMEGA to key transmitter continuously */
#define LB_MESSAGE_KEY_UP "$GO,0;" /* Tell ATMEGA to stop continuous transmit */
#define LB_MESSAGE_SLEEP "$GO,S;" /* Tell ATMEGA to put all circuits to sleep (no exit without power cycle) */

typedef enum {
  TX_WAKE_UP,
  TX_INITIAL_TIME_RECEIVED,
  TX_HTML_PAGE_SERVED,
  TX_HTML_NEXT_EVENT,
  TX_HTML_REFRESH_EVENTS,
  TX_HTML_SAVE_CHANGES,
  TX_READ_ALL_EVENTS_FILES,
  TX_RECD_START_EVENT_REQUEST,
  TX_WAITING_FOR_INSTRUCTIONS
} TxCommState;

class Transmitter {
  public:
    String masterCloneSetting;
    bool debug_prints_enabled;

  public:
    Transmitter(bool);
    bool setXmtrData(String id, String value);
    bool parseStringData(String txt);
};


#endif // _TRANSMITTER_H_
