#ifndef _EVENT_H_
#define _EVENT_H_

#include <Arduino.h>

#define EVENT_DEBUG_PRINTS_OVERRIDE true

#define MAXIMUM_NUMBER_OF_EVENTS 20
#define MAXIMUM_NUMBER_OF_EVENT_FILE_LINES 200
#define MAXIMUM_NUMBER_OF_EVENT_TX_TYPES 4
#define MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE 10

#define EVENT_NAME "EVENT_NAME" /* Human readable event name. Should contain band that is used: 80M or 2M */
#define EVENT_BAND "EVENT_BAND" /* Band that is used: 80M or 2M - used to restrict frequency choices */
#define EVENT_ANTENNA_PORT "EVENT_ANT_PORT" /* Used to determine whether the correct antenna is attached: ANT_80M_1, ANT_80M_2, ANT_80M_3, ANT_2M */
#define EVENT_CALLSIGN "EVENT_CALLSIGN" /* For station ID */ 
#define EVENT_CALLSIGN_SPEED "EVENT_CALLSIGN_SPEED" /* CW speed (WPM) at which ID is sent */
#define EVENT_START_DATE_TIME "EVENT_START_DATE_TIME" /* Start date and time in yyyy-mm-ddThh:mm:ssZ format */
#define EVENT_FINISH_DATE_TIME "EVENT_FINISH_DATE_TIME" /* Finish date and time in yyyy-mm-ddThh:mm:ssZ format */
#define EVENT_MODULATION "EVENT_MODULATION" /* AM or CW for 2m events, only CW for 80m events */
#define EVENT_NUMBER_OF_TX_TYPES "EVENT_NUMBER_OF_TX_TYPES" /* Quantity of different "roles": Home, Foxes, Fast Foxes, Slow Foxes, etc. */
#define TYPE_NAME "_ROLE_NAME" /* Human readable "role" name: "Home", "Fox", "Fast Fox", "Slow Fox", "Spectator", etc. */
#define TYPE_TX_COUNT "_TX_COUNT" /* Number of transmitters in a particular "role" */
#define TYPE_FREQ "_FREQ" /* Frequency used by transmitters in that "role" */
#define TYPE_POWER_LEVEL "_POWER_LEVEL" /* Power level used by transmitters in that "role" */
#define TYPE_CODE_SPEED "_CODE_SPEED" /* Code speed used by transmitters in that "role" */
#define TYPE_ID_INTERVAL "_ID_INTERVAL" /* How frequently (seconds) should transmitters in that "role" send the station ID: 0 = never; */
#define TYPE_TX_PATTERN "_PATTERN" /* What pattern of characters should a particular transmitter send */ 
#define TYPE_TX_ON_TIME  "_ON_TIME" /* For what period of time (seconds) should a particular transmitter remain on the air sending its pattern */
#define TYPE_TX_OFF_TIME "_OFF_TIME" /* For what period of time (seconds) should a particular transmitter remain off the air before tranmitting again */
#define TYPE_TX_DELAY_TIME "_DELAY_TIME" /* For what period of time (seconds) should a transmitter wait prior to beginning to send its first transmission */

typedef struct {
  String path;
  unsigned long startDateTimeEpoch;
  unsigned long finishDateTimeEpoch;
} EventFileRef;

typedef struct {
  String id;
  String value;
} EventLineData;

typedef struct TxDataStruct{
  String pattern;
  int onTime;
  int offTime;
  int delayTime;
} TxDataType;

typedef struct RoleDataStruct {
  String rolename;
  int  numberOfTxs;
  int  frequency;
  int  powerLevel_mW;
  int  code_speed;
  int  id_interval;
  TxDataStruct *tx[MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE];
} RoleDataType;

typedef struct EventDataStruct{
  String event_name; // "Classic 2m"      <- Human-readable event name
  String event_band; // 2         <- Band information to be used for restricting frequency settings
  String event_callsign; // "DE NZ0I"     <- Callsign used by all transmitters (blank if none)
  String event_callsign_speed; // 20      <- Code speed at which all transmitters should send their callsign ID
  String event_start_date_time; // 2018-03-23T18:00:00Z <- Date and time of event start (transmitters on)
  String event_finish_date_time; // 2018-03-23T20:00:00Z  <- Date and time of event finish (transmitters off)
  String event_modulation; // AM        <- Modulation format to be used by all transmitters
  int event_number_of_tx_types; // 2     <- How many different transmitter roles there are (e.g., foxes and home)
  RoleDataStruct *role[MAXIMUM_NUMBER_OF_EVENT_TX_TYPES];
} EventType;

class Event {
  public:
    bool debug_prints_enabled;

    Event(bool);
    ~Event();
    bool setEventData(String id, String value);
    bool parseStringData(String txt);
    static bool extractLineData(String s, EventLineData* result);
    static bool isSoonerEvent(EventFileRef a, EventFileRef b, unsigned long currentEpoch);
    void dumpData(void);

    EventType* eventData;
};


#endif // _EVENT_H_
