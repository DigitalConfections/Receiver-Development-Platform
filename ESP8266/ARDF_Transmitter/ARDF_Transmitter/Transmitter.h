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

// Websocket Command Messages
#define SOCK_COMMAND_EVENT_NAME "EVENT_NAME" /* read only */
#define SOCK_COMMAND_EVENT_FILE_VERSION "FILE_VERSION" /* read only */
#define SOCK_COMMAND_SYNC_TIME "SYNC"
#define SOCK_COMMAND_TEMPERATURE "TEMP" /* read only */
#define SOCK_COMMAND_SSID "SSID" /* read only */
#define SOCK_COMMAND_BATTERY "BAT" /* read only */
#define SOCK_COMMAND_CLONE "CLONE"
#define SOCK_COMMAND_VERSION "VERS" /* read only */
#define SOCK_COMMAND_MAC "MAC" /* read only */
#define SOCK_COMMAND_CALLSIGN "CALLSIGN"
#define SOCK_COMMAND_BAND "BAND" /* read only */
#define SOCK_COMMAND_START_TIME "START_TIME"
#define SOCK_COMMAND_FINISH_TIME "FINISH_TIME"
#define SOCK_COMMAND_TYPE_NAME "TYPE_NAME" /* read only */
#define SOCK_COMMAND_TYPE_TX_COUNT "TX_COUNT"  /* read only */
#define SOCK_COMMANT_TYPE_PWR "POWER" /* read only */
#define SOCK_COMMAND_TYPE_FREQ "FREQ"
#define SOCK_COMMAND_TYPE_WPM "CODE_SPEED" /* read only */
#define SOCK_COMMAND_TYPE_ID_INTERVAL "ID_INT" /* read only */
#define SOCK_COMMAND_TX_ROLE "TX_ROLE"
#define SOCK_COMMAND_TEST "TEST"

// LinkBus Messages
#define LB_MESSAGE_ESP "ESP"
#define LB_MESSAGE_ESP_WAKEUP "$ESP,0;" /* Wake up from reset */
#define LB_MESSAGE_ESP_ACTIVE "$ESP,1;" /* Ready with active event data */
#define LB_MESSAGE_ESP_SAVE "$ESP,2;" /* Save settings changes to file (keeps power up) */
#define LB_MESSAGE_ESP_SHUTDOWN "$ESP,3;" /* Shut down in 3 seconds */
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

typedef enum {
  TX_WAKE_UP,
  TX_INITIAL_TIME_RECEIVED,
  TX_HTML_PAGE_SERVED,
  TX_HTML_NEXT_EVENT,
  TX_RECD_EVENTS_QUERY,
  TX_READ_ALL_EVENTS_FILES,
  TX_RECD_START_EVENT_REQUEST,
  TX_WAITING_FOR_INSTRUCTIONS
} TxCommState;

class Transmitter {
  public:
    String masterCloneSetting;
    String callsignSetting;
    String classic2mIDSetting;
    String classic80mIDSetting;
    String sprint80mIDSetting;
    String foxO_80mIDSetting;
    String classic2MstartDateTimeSetting;
    String classic2MfinishDateTimeSetting;
    String classic80MstartDateTimeSetting;
    String classic80MfinishDateTimeSetting;
    String sprintStartDateTimeSetting;
    String sprintFinishDateTimeSetting;
    String foxOstartDateTimeSetting;
    String foxOfinishDateTimeSetting;
    String classic2mFoxFrequencySetting; // Classic event only (e.g., 144.500 MHz)
    String classic2mHomeFrequencySetting; // Classic event only (e.g., 144.900 MHz)
    String classic80mFoxFrequency1Setting; // Classic event only (e.g., 3.510 MHz)
    String classic80mHomeFrequencySetting; // Classic, Fox-O events only (e.g., 3.600 MHz)
    String fox_O_80mFoxFrequency1Setting; // Fox-O event only (e.g., 3.510 MHz)
    String fox_O_80mFoxFrequency2Setting; // Fox-O event only (e.g., 3.540 MHz)
    String fox_O_80mFoxFrequency3Setting; // Fox-O event only (e.g., 3.570 MHz)
    String fox_O_80mHomeFrequencySetting; // Fox-O event only (e.g., 3.570 MHz)
    String sprintSpectator80mFrequencySetting; // Sprint event only (e.g., 3.550 MHz)
    String sprintSlow80mFrequencySetting; // Sprint event only (e.g., 3.530 MHz)
    String sprintFast80mFrequencySetting; // Sprint event only (e.g., 3.570 MHz)
    String sprintFinish80mFrequencySetting; // Sprint event only (e.g., 3.600 MHz)
    bool debug_prints_enabled;

  public:
    Transmitter(bool);
    bool setXmtrData(String id, String value);
    bool parseStringData(String txt);
};


#endif // _TRANSMITTER_H_
