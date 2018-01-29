#ifndef _TRANSMITTER_H_
#define _TRANSMITTER_H_

#include <Arduino.h>

#define FULLY_CHARGED_BATTERY_mV 4200.
#define FULLY_DEPLETED_BATTERY_mV 3200.

#define MASTER_OR_CLONE_SETTING "MASTER_CLONE"
#define CALLSIGN_SETTING "CALLSIGN"
#define CLASSIC_2M_ID_SETTING "CLASSIC_2M_FOX_ID"
#define CLASSIC_80M_ID_SETTING "CLASSIC_80M_FOX_ID"
#define SPRINT_80M_ID_SETTING "SPRINT_80M_FOX_ID" 
#define FOX_O_80M_ID_SETTING "FOX_O_80M_FOX_ID" 
#define CLASSIC_2M_START_DATE_TIME_SETTING "CLASSIC_2M_START_DATE_TIME"
#define CLASSIC_2M_FINISH_DATE_TIME_SETTING "CLASSIC_2M_FINISH_DATE_TIME"
#define CLASSIC_80M_START_DATE_TIME_SETTING "CLASSIC_80M_START_DATE_TIME"
#define CLASSIC_80M_FINISH_DATE_TIME_SETTING "CLASSIC_80M_FINISH_DATE_TIME"
#define SPRINT_START_DATE_TIME_SETTING "SPRINT_START_DATE_TIME"
#define SPRINT_FINISH_DATE_TIME_SETTING "SPRINT_FINISH_DATE_TIME"
#define FOX_O_START_DATE_TIME_SETTING "FOX_O_START_DATE_TIME"
#define FOX_O_FINISH_DATE_TIME_SETTING "FOX_O_FINISH_DATE_TIME"
#define CLASSIC_2M_FOX_FREQUENCY_SETTING "CLASSIC_2M_FOX_FREQ"
#define CLASSIC_2M_FINISH_FREQUENCY_SETTING "CLASSIC_2M_FINISH_FREQ"
#define CLASSIC_80M_FOX_FREQUENCY_SETTING "CLASSIC_80M_FOX_FREQ"
#define CLASSIC_80M_FINISH_FREQUENCY_SETTING "CLASSIC_80M_FINISH_FREQ"
#define FOX_O_80M_FOX_FREQUENCY1_SETTING "FOX_O_80M_FOX1_FREQ"
#define FOX_O_80M_FOX_FREQUENCY2_SETTING "FOX_O_80M_FOX2_FREQ"
#define FOX_O_80M_FOX_FREQUENCY3_SETTING "FOX_O_80M_FOX3_FREQ"
#define FOX_O_80M_FINISH_FREQUENCY_SETTING "FOX_O_80M_FINISH_FREQ"
#define SPRINT_80M_SPECTATOR_FREQUENCY_SETTING "SPRINT_80M_SPCECTATOR_FREQ"
#define SPRINT_80M_SLOW_FREQUENCY_SETTING "SPRINT_80M_SLOW_FREQ"
#define SPRINT_80M_FAST_FREQUENCY_SETTING "SPRINT_80M_FAST_FREQ"
#define SPRINT_80M_FINISH_FREQUENCY_SETTING "SPRINT_80M_FINISH_FREQ"
#define NUMBER_OF_TRANSMITTER_SETTINGS 26

// UI States and Actions
#define EVENT_DISPLAYED_STATE "EVENT_STATE"
#define COMMAND_SYNC_TIME "SYNC"
#define COMMAND_TEMPERATURE "TEMP"
#define COMMAND_SSID "SSID"
#define COMMAND_BATTERY "BAT"
#define COMMAND_CLONE "CLONE"
#define COMMAND_VERSION "VERS"
#define COMMAND_MAC "MAC"

// LinkBus Commands
#define MESSAGE_TIME "TIM"
#define MESSAGE_TEMP "TEM"
#define MESSAGE_BATTERY "BAT"

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
