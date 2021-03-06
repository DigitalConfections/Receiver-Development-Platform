#ifndef _EVENT_H_
#define _EVENT_H_

#include <Arduino.h>

#define EVENT_DEBUG_PRINTS_OVERRIDE true

#define MAXIMUM_NUMBER_OF_EVENTS 20
#define MAXIMUM_NUMBER_OF_EVENT_FILE_LINES 200
#define MAXIMUM_NUMBER_OF_ME_FILE_LINES 2
#define MAXIMUM_NUMBER_OF_EVENT_TX_TYPES 4
#define MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE 10

#define EVENT_NAME "EVENT_NAME" /* Human readable event name. Should contain band that is used: 80M or 2M */
#define EVENT_FILE_VERSION "EVENT_VERSION" /* Human readable string for tracking event file changes */
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
#define TX_ASSIGNMENT "TX_ASSIGNMENT" /* Which role and time slot is assigned to this transmitter: "r:t" */
#define TX_ASSIGNMENT_IS_DEFAULT "TX_DEFAULT" /* Identifies the assignment as the default assignment */
typedef struct {
  String path;
  unsigned long startDateTimeEpoch;
  unsigned long finishDateTimeEpoch;
} EventFileRef;

typedef struct {
  String id;
  String value;
} EventLineData;

typedef struct TxDataStruct {
  String pattern;
  String onTime;
  String offTime;
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

typedef struct EventDataStruct {
  String tx_assignment; // <- Role and time slot assigned to this tx: "r:t" 
  bool   tx_assignment_is_default; // <- Indicates that the transmitter has never receieved a specific role assignment
  String event_name; // "Classic 2m"      <- Human-readable event name
  String event_file_version; // <- Free-form text for tracking event revisions
  String event_band; // 2         <- Band information to be used for restricting frequency settings
  String event_antenna_port; //   <- Which antenna port to associate with this event 2_0, 80_0, 80_1, or 80_2
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
    bool values_did_change;
    String myPath;
    
  private:
   EventType* eventData;

  public:

    Event(bool);
    ~Event();

    String readMeFile(String path);
    void saveTxAssignment(void);
    void setTxAssignment(String role_slot);
    String getTxAssignment(void);

    void setEventName(String str);
    String getEventName(void) const;
    void setEventFileVersion(String str);
    String getEventFileVersion(void) const;
    void setEventBand(String str);
    String getEventBand(void) const;
    void setCallsign(String str);
    String getCallsign(void) const;
    void setAntennaPort(String str);
    String getAntennaPort(void) const;
    void setCallsignSpeed(String str);
    String getCallsignSpeed(void) const;
    void setEventStartDateTime(String str);
    String getEventStartDateTime(void) const;
    void setEventFinishDateTime(String str);
    String getEventFinishDateTime(void) const;
    void setEventModulation(String str);
    String getEventModulation(void) const;
    void setEventNumberOfTxTypes(int value);
    void setEventNumberOfTxTypes(String str);
    int getEventNumberOfTxTypes(void) const;

    bool setRolename(int roleIndex, String str);
    String getRolename(int roleIndex) const;
    bool setNumberOfTxsForRole(int roleIndex, String str);
    int getNumberOfTxsForRole(int roleIndex) const;
    bool setFrequencyForRole(int roleIndex, long freq);
    long getFrequencyForRole(int roleIndex) const;
    bool setPowerlevelForRole(int roleIndex, String str);
    int getPowerlevelForRole(int roleIndex) const;
    bool setCodeSpeedForRole(int roleIndex, String str);
    int getCodeSpeedForRole(int roleIndex) const;
    bool setIDIntervalForRole(int roleIndex, String str);
    int getIDIntervalForRole(int roleIndex) const;

    bool readEventFile(String path);
    bool writeEventFile(void);

    String getTxDescriptiveName(String role_tx);

    static bool extractLineData(String s, EventLineData* result);
    static bool isSoonerEvent(EventFileRef a, EventFileRef b, unsigned long currentEpoch);
 
  private:
    
    bool parseStringData(String txt);
    bool writeEventFile(String fname);
    void dumpData(void);

    bool setEventData(String id, String value);
    bool setEventData(String id, int value);

};


#endif // _EVENT_H_
