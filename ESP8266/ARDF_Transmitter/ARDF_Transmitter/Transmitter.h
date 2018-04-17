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
#define SOCK_COMMAND_FOX_ID "TX_ROLE"

// LinkBus Messages
#define MESSAGE_ESP "ESP"
#define MESSAGE_TIME "TIM"
#define MESSAGE_TEMP "TEM"
#define MESSAGE_BATTERY "BAT"
#define MESSAGE_CALLSIGN "ID"

typedef enum {
  TX_WAKE_UP,
  TX_INITIAL_TIME_RECEIVED,
  TX_HTML_PAGE_SERVED,
  TX_HTML_NEXT_EVENT,
  TX_RECD_EVENTS_QUERY,
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
