/**********************************************************************************************
   Copyright © 2019 Digital Confections LLC

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

#include "Event.h"
#include <FS.h>
#include "Helpers.h"
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>

extern ESP8266WebServer* g_http_server;
extern WebSocketsServer g_webSocketServer;


Event::Event(bool debug) {

#ifdef EVENT_DEBUG_PRINTS_OVERRIDE
  debug = EVENT_DEBUG_PRINTS_OVERRIDE;
#endif
  debug_prints_enabled = debug;

  values_did_change = false;
  eventData = new EventDataStruct();

  for (int i = 0; i < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES; i++)
  {
    eventData->role[i] = new RoleDataStruct();
    if (eventData->role[i] == NULL) {
      Serial.println("Error! Out of memory?");
    }

    for (int j = 0; j < MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE; j++)
    {
      eventData->role[i]->tx[j] = new TxDataStruct();

      if (eventData->role[i]->tx[j] == NULL) {
        Serial.println("Error! Out of memory?");
      }
    }
  }
}

Event::~Event() {

  for (int i = 0; i < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES; i++)
  {
    for (int j = 0; j < MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE; j++)
    {
      delete eventData->role[i]->tx[j];
    }

    delete eventData->role[i];
  }

  delete eventData;
}

bool Event::extractLineData(String s, EventLineData* result) {
  if (s.indexOf(',') < 0)
  {
    if ((s.indexOf("EVENT_START") < 0) && (s.indexOf("EVENT_END") < 0)) {
      return true; // invalid line found
    } else {
      result->id = "";
      return false; // start or end found
    }
  }

  String settingID = s.substring(0, s.indexOf(','));
  String value = s.substring(s.indexOf(',') + 1);
  value.trim(); /* remove leading and trailing whitespace */

  if (value.charAt(0) == '"')
  {
    if (value.charAt(1) == '"') // handle empty string
    {
      value = "";
    }
    else // remove quotes
    {
      value = value.substring(1, value.length() - 1);
    }
  }

  result->id = settingID;
  result->value = value;

  return false;
}

bool Event::parseStringData(String s) {
  EventLineData data;
  if (extractLineData(s, &data))
  {
    return true; // flag error
  }
  else if ((data.id).equals(""))
  {
    return false; // no data but no error
  }

  return (setEventData(data.id, data.value));
}

/**
   Returns a boolean indicating if event a will commence sooner that b relative to the currentEpoch
*/
bool Event::isSoonerEvent(EventFileRef a, EventFileRef b, unsigned long currentEpoch)
{
  bool aRunsForever = a.startDateTimeEpoch >= a.finishDateTimeEpoch;
  bool bRunsForever = b.startDateTimeEpoch >= b.finishDateTimeEpoch;
  bool aStartedInThePast = a.startDateTimeEpoch <= currentEpoch;
  //bool bStartedInThePast = b.startDateTimeEpoch <= currentEpoch;
  bool aFinishedInThePast = (a.finishDateTimeEpoch <= currentEpoch) && !aRunsForever;
  bool bFinishedInThePast = (b.finishDateTimeEpoch <= currentEpoch) && !bRunsForever;   
    
  if (aStartedInThePast) /* a started in the past */
  {
    if (aFinishedInThePast) return false; /* a finished in the past so is disabled */
    if (bFinishedInThePast) return true; /* b finished in the past so is disabled, and a is still running */
    return (a.startDateTimeEpoch > b.startDateTimeEpoch); /* if a started later than b then it is closer */
  }
  
  /* a starts in the future */
  if (bFinishedInThePast) return true; /* b finished in the past so is disabled, and a has yet to start */
  return (a.startDateTimeEpoch < b.startDateTimeEpoch); /* a will start sooner than b */
}


String Event::getTxDescriptiveName(String role_tx) // role_tx = "r:t"
{
  String theName = "";
  int i = role_tx.indexOf(":");

  if (i < 1) return String("Error(:)");

  int roleIndex = (role_tx.substring(0, i)).toInt(); // r
  int txIndex = (role_tx.substring(i + 1)).toInt(); // t

  Serial.println(String("role = " + String(roleIndex) + "; tx = " + String(txIndex)));

  if (((roleIndex >= 0) && (roleIndex < this->eventData->event_number_of_tx_types)) && ((txIndex >= 0) && (txIndex < this->eventData->role[roleIndex]->numberOfTxs)))
  {
    int txsInRole = this->eventData->role[roleIndex]->numberOfTxs;

    theName = this->eventData->role[roleIndex]->rolename;

    if (txsInRole > 1) {
      theName = String(theName + " " + String(txIndex + 1));
    }

    theName = String(theName + " - " + this->eventData->role[roleIndex]->tx[txIndex]->pattern);
  }

  return (theName);
}

String Event::readMeFile(String path)
{
  if (path == NULL) return "";

  if (!path.startsWith("/")) path = "/" + path;

  if (path.endsWith(".event"))
  {
    int dot = path.lastIndexOf(".event");
    path = path.substring(0, dot);
  }

  if (!path.endsWith(".me")) path = path + ".me";

  if (SPIFFS.exists(path))
  {
    // Create an object to hold the file data
    File file = SPIFFS.open(path, "r"); // Open the file for reading
    String s = file.readStringUntil('\n');
    int count = 0;

    while (s.length() && count++ < MAXIMUM_NUMBER_OF_ME_FILE_LINES)
    {
      this->parseStringData(s);
      s = file.readStringUntil('\n');
    }

    file.close(); // Close the file
  }
  else
  {
    File file = SPIFFS.open(path, "w"); // Open the file for writing

    file.println(TX_ASSIGNMENT + String(",0:0"));
    file.println(String(TX_DESCRIPTIVE_NAME) + "," + getTxDescriptiveName("0:0"));
    file.println(String(TX_ROLE_FREQ) + "," + String(this->getFrequencyForRole(0)));
    file.println(TX_ASSIGNMENT_IS_DEFAULT + String(",true"));
    file.close(); // Close the file
    if (debug_prints_enabled) Serial.println(String("\tWrote file: ") + path);

    this->eventData->tx_assignment = "0:0";
    this->eventData->tx_assignment_is_default = true;
  }

  return path;
}

bool Event::extractMeFileData(String path, EventFileRef* eventRef)
{
  bool fail = true;

  if (path == NULL) return fail;

  if (!path.startsWith("/")) path = "/" + path;

  if (path.endsWith(".event"))
  {
    int dot = path.lastIndexOf(".event");
    path = path.substring(0, dot);
  }

  if (!path.endsWith(".me")) path = path + ".me";

  if (SPIFFS.exists(path))
  {
    // Create an object to hold the file data
    File file = SPIFFS.open(path, "r"); // Open the file for reading
    String s = file.readStringUntil('\n');
    int count = 0;

    EventLineData data;

    while (s.length() && count++ < MAXIMUM_NUMBER_OF_ME_FILE_LINES)
    {
      Event::extractLineData(s, &data);

      if (data.id.equalsIgnoreCase(TX_DESCRIPTIVE_NAME))
      {
        eventRef->role = data.value;
        fail = false;
      }
      else if (data.id.equalsIgnoreCase(TX_ROLE_FREQ))
      {
        eventRef->freq = data.value;
        fail = false;
      }
      // ignore TX_ASSIGNMENT "TX_ASSIGNMENT" /* Which role and time slot is assigned to this transmitter: "r:t" */

      s = file.readStringUntil('\n');
    }

    file.close(); // Close the file
  }
  else
  {
    File file = SPIFFS.open(path, "w"); // Open the file for writing

    file.println(TX_ASSIGNMENT + String(",0:0"));
    file.println(String(TX_DESCRIPTIVE_NAME) + ",Finish - MO");
    file.println(String(TX_ROLE_FREQ) + ",3550000");
    file.println(TX_ASSIGNMENT_IS_DEFAULT + String(",true"));
    file.close(); // Close the file

    eventRef->role = "?";
    eventRef->freq = "?";
    fail = false;
  }

  return fail;
}


bool Event::readEventFile(String path)
{
  if (!path.startsWith("/")) path = "/" + path;

  if (SPIFFS.exists(path))
  {
    this->eventData->tx_assignment = "";
    this->eventData->tx_assignment_is_default = "";
    this->eventData->event_file_version = "";
    this->eventData->event_band = "";
    this->eventData->event_antenna_port = "";
    this->eventData->event_callsign = "";
    this->eventData->event_callsign_speed = "";
    this->eventData->event_start_date_time = "";
    this->eventData->event_finish_date_time = "";
    this->eventData->event_modulation = "";
    this->eventData->event_number_of_tx_types = -1;
    this->myPath = path;
    // Create an object to hold the file data
    File file = SPIFFS.open(path, "r"); // Open the file for reading
    String s = file.readStringUntil('\n');
    int count = 0;

    while (s.length() && count++ < MAXIMUM_NUMBER_OF_EVENT_FILE_LINES)
    {
      this->parseStringData(s);
      s = file.readStringUntil('\n');
    }

    file.close(); // Close the file

    getTxAssignment();

    values_did_change = false;
  }
  else
  {
    return true;
  }

  if (debug_prints_enabled)
  {
    Serial.println(String("\tRead event: ") + path);
  }

  return false;
}


void Event::dumpData(void)
{
  if (eventData == NULL) return;

  Serial.println("=====");
  Serial.println("Event name: " + eventData->event_name);
  Serial.println("File ver: " + eventData->event_file_version);
  Serial.println("Band: " + eventData->event_band);
  Serial.println("Call: " + eventData->event_callsign);
  Serial.println("Start: " + eventData->event_start_date_time);
  Serial.println("Finish: " + eventData->event_finish_date_time);
  Serial.println("Mod: " + eventData->event_modulation);
  Serial.println("Types: " + String(eventData->event_number_of_tx_types));
  for (int i = 0; i < eventData->event_number_of_tx_types; i++)
  {
    Serial.println("  Name: " + eventData->role[i]->rolename);
    Serial.println("    No. txs: " + String(eventData->role[i]->numberOfTxs));
    Serial.println("    Freq: " + String(eventData->role[i]->frequency));
    Serial.println("    Pwr: " + String(eventData->role[i]->powerLevel_mW));
    Serial.println("    WPM: " + String(eventData->role[i]->code_speed));
    Serial.println("    ID int: " + String(eventData->role[i]->id_interval));

    for (int j = 0; j < eventData->role[i]->numberOfTxs; j++)
    {
      Serial.println("      Pattern: " + eventData->role[i]->tx[j]->pattern);
      Serial.println("      onTime: " + eventData->role[i]->tx[j]->onTime);
      Serial.println("      offTime: " + eventData->role[i]->tx[j]->offTime);
      Serial.println("      delayTime: " + eventData->role[i]->tx[j]->delayTime);
    }
  }
  Serial.println("=====");
}

/**
 * Returns TRUE if this event contains valid data
 */
bool Event::validateEvent(void)
{
    bool success;

    success = (this->eventData != NULL);
    
    if (success) 
    {
      success &= (this->eventData->event_name).length() > 0;
      success &= (this->eventData->event_file_version).length() > 0;
      success &= (this->eventData->event_band).length() > 0;
      //success &= (this->eventData->event_antenna_port);
      success &= (this->eventData->event_callsign).length() > 2;
      success &= (this->eventData->event_callsign_speed).length() > 0;
      success &= (this->eventData->event_start_date_time).length() > 19;
      success &= (this->eventData->event_finish_date_time).length() > 19;
      success &= (this->eventData->event_modulation).length() > 0;
      success &= ((this->eventData->event_number_of_tx_types) > 0);

      for (int i = 0; i < this->eventData->event_number_of_tx_types; i++)
      {
        success &= (this->eventData->role[i]->rolename).length() > 0;
        success &= (this->eventData->role[i]->numberOfTxs) > 0;
        success &= (this->eventData->role[i]->frequency) > 0;
        success &= (this->eventData->role[i]->powerLevel_mW) > 0;
        success &= (this->eventData->role[i]->code_speed) > 0;
        //success &= (this->eventData->role[i]->id_interval);
  
        for (int j = 0; j < this->eventData->role[i]->numberOfTxs; j++)
        {
          success &= (this->eventData->role[i]->tx[j]->pattern).length() > 0;
          success &= (this->eventData->role[i]->tx[j]->onTime).length() > 0;
          success &= (this->eventData->role[i]->tx[j]->offTime).length() > 0;
          success &= (this->eventData->role[i]->tx[j]->delayTime).length() > 0;
        }
      }
    }

    return success;
}

bool Event::writeEventFile(void)
{
  return (this->writeEventFile(this->myPath));
}

bool Event::writeEventFile(String path)
{
  if (this->eventData == NULL) return true;
  if (!validateEvent()) return true;
    
  if (this->values_did_change == false)
  {
    if (debug_prints_enabled) Serial.print("Not written: Event did not change."); Serial.println(path);
    return false; /* nothing new to save, return no error */
  }
  else if (debug_prints_enabled)
  {
    Serial.print("Writing Event changes to:"); Serial.println(path);
  }

  if (path == NULL) path = this->myPath;

  if (!path.startsWith("/")) path = "/" + path;
  //  if (SPIFFS.exists(path)) {  // version of that file must be deleted (if it exists)
  //    SPIFFS.remove(path);
  //  }

  if (debug_prints_enabled) Serial.print("Writing file: "); Serial.println(path);
  File eventFile = SPIFFS.open(path, "w");  // Open the file for writing in SPIFFS (create if it doesn't exist)
  this->myPath = path;

  if (eventFile) {
    String line = String(String(EVENT_NAME) + "," + this->eventData->event_name);
    eventFile.println(line);
    line = String(String(EVENT_FILE_VERSION) + "," + this->eventData->event_file_version);
    eventFile.println(line);
    line = String(String(EVENT_BAND) + "," + this->eventData->event_band);
    eventFile.println(line);
    line = String(String(EVENT_ANTENNA_PORT) + "," + this->eventData->event_antenna_port);
    eventFile.println(line);
    line = String(String(EVENT_CALLSIGN) + "," + this->eventData->event_callsign);
    eventFile.println(line);
    line = String(String(EVENT_CALLSIGN_SPEED) + "," + this->eventData->event_callsign_speed);
    eventFile.println(line);
    line = String(String(EVENT_START_DATE_TIME) + "," + this->eventData->event_start_date_time);
    eventFile.println(line);
    line = String(String(EVENT_FINISH_DATE_TIME) + "," + this->eventData->event_finish_date_time);
    eventFile.println(line);
    line = String(String(EVENT_MODULATION) + "," + this->eventData->event_modulation);
    eventFile.println(line);
    line = String(String(EVENT_NUMBER_OF_TX_TYPES) + "," + this->eventData->event_number_of_tx_types);
    eventFile.println(line);

    for (int i = 0; i < this->eventData->event_number_of_tx_types; i++)
    {
      String typenum = String("TYPE" + String(i + 1));
      line = String(typenum + TYPE_NAME + "," + this->eventData->role[i]->rolename);
      eventFile.println(line);
      line = String(typenum + TYPE_TX_COUNT + "," + String(this->eventData->role[i]->numberOfTxs));
      eventFile.println(line);
      line = String(typenum + TYPE_FREQ + "," + String(this->eventData->role[i]->frequency));
      eventFile.println(line);
      line = String(typenum + TYPE_POWER_LEVEL + "," + String(this->eventData->role[i]->powerLevel_mW));
      eventFile.println(line);
      line = String(typenum + TYPE_CODE_SPEED + "," + String(this->eventData->role[i]->code_speed));
      eventFile.println(line);
      line = String(typenum + TYPE_ID_INTERVAL + "," + String(this->eventData->role[i]->id_interval));
      eventFile.println(line);

      for (int j = 0; j < this->eventData->role[i]->numberOfTxs; j++)
      {
        String txnum = String("TX" + String(j + 1));
        line = String(typenum + "_" + txnum + TYPE_TX_PATTERN + "," + this->eventData->role[i]->tx[j]->pattern);
        eventFile.println(line);
        line = String(typenum + "_" + txnum + TYPE_TX_ON_TIME + "," + this->eventData->role[i]->tx[j]->onTime);
        eventFile.println(line);
        line = String(typenum + "_" + txnum + TYPE_TX_OFF_TIME + "," + this->eventData->role[i]->tx[j]->offTime);
        eventFile.println(line);
        line = String(typenum + "_" + txnum + TYPE_TX_DELAY_TIME + "," + this->eventData->role[i]->tx[j]->delayTime);
        eventFile.println(line);
      }
    }
  }
  else
  {
    return true;
  }

  saveMeData("");

  return false;
}

void Event::saveMeData(String newAssignment)
{
  if (this->eventData == NULL) return;
  if ((this->eventData->tx_assignment.length() < 3) || (this->eventData->tx_assignment.indexOf(":") < 1)) return;
  if (this->myPath.length() < 7) return;

  String holdTxAssignment;
  String holdRoleName = this->eventData->tx_role_name;
  String holdRoleFrequency = this->eventData->tx_role_freq;

  if (newAssignment.indexOf(":") < 1)
  {
    holdTxAssignment = this->eventData->tx_assignment;
  }
  else
  {
    holdTxAssignment = newAssignment;
  }


  String path = readMeFile(this->myPath); // assigns file value to this->eventData->tx_assignment

  if ((!holdTxAssignment.equals(this->eventData->tx_assignment)) || (!holdRoleName.equals(this->eventData->tx_role_name)) || (!holdRoleFrequency.equals(this->eventData->tx_role_freq)))
  {
    String role = holdTxAssignment.substring(0, holdTxAssignment.indexOf(":"));
    File file = SPIFFS.open(path, "w"); // Open the file for writing

    file.println(String(TX_ASSIGNMENT) + "," + holdTxAssignment);
    file.println(String(TX_DESCRIPTIVE_NAME) + "," + getTxDescriptiveName(holdTxAssignment));
    file.println(String(TX_ROLE_FREQ) + "," + String(this->getFrequencyForRole(role.toInt())));
    file.println(String(TX_ASSIGNMENT_IS_DEFAULT) + ",false");
    file.close(); // Close the file
    if (debug_prints_enabled) Serial.println(String("\tWrote file: ") + path);
  }

  this->eventData->tx_assignment = holdTxAssignment; // set tx_assignment to the latest value
}

/////////////////////////////////////////////////////////////////////////////////////////

bool Event::setTxAssignment(String role_slot)
{
  if (this->eventData == NULL) return true;
  role_slot.trim();
  int c = role_slot.indexOf(':');
  if (c < 1) return true;

  if (this->eventData->tx_assignment != role_slot)
  {
    String r = role_slot.substring(0, c - 1);
    this->eventData->tx_assignment = role_slot;
    this->eventData->tx_role_name = Event::getTxDescriptiveName(role_slot);
    this->eventData->tx_role_freq = Event::getFrequencyForRole(r.toInt());
    this->values_did_change = true;
    Serial.println("Set role: " + this->eventData->tx_assignment);
  }

  return false;
}

String Event::getTxAssignment(void)
{
  if ((this->eventData->tx_assignment.length() < 1) || (this->eventData->tx_assignment.indexOf(":") < 1))
  {
    Serial.println("Reading ME file...");
    readMeFile(this->myPath);
  }

  return this->eventData->tx_assignment;
}

bool Event::setTxFrequency(String frequency)
{
  if (this->eventData == NULL) return true;
  if (frequency == NULL) return true;

  if (!frequency.equals(this->eventData->tx_role_freq))
  {
    this->eventData->tx_role_freq = frequency;
    this->values_did_change = true;
    Serial.println("Set frequency: " + this->eventData->tx_role_freq);
  }

  return false;
}

String Event::getTxFrequency(void)
{
  if ((this->eventData->tx_assignment.length() < 1) || (this->eventData->tx_assignment.indexOf(":") < 1))
  {
    Serial.println("Reading ME file...");
    readMeFile(this->myPath);
  }

  return this->eventData->tx_role_freq;
}

int Event::getTxRoleIndex(void)
{
  int result = -1;
  String assign = this->eventData->tx_assignment;
  if (assign.indexOf(":") < 1) getTxAssignment();
  int colon = assign.indexOf(":");
  if (colon < 1) return result;
  result = (assign.substring(0, colon)).toInt(); // r
  return result;
}

int Event::getTxSlotIndex(void)
{
  int result = -1;
  String assign = this->eventData->tx_assignment;
  if (assign.indexOf(":") < 1) getTxAssignment();
  int colon = assign.indexOf(":");
  if (colon < 1) return result;
  result = (assign.substring(colon + 1)).toInt(); // t
  return result;
}

TxDataType* Event::getTxData(int roleIndex, int txIndex)
{
  return eventData->role[roleIndex]->tx[txIndex];
}

void Event::setEventName(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  if (this->eventData->event_name != str)
  {
    this->setEventData(EVENT_NAME, str);
    this->values_did_change = true;
  }
}

String Event::getEventName(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_name;
}


void Event::setEventFileVersion(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  if (this->eventData->event_file_version != str)
  {
    this->setEventData(EVENT_FILE_VERSION, str);
    this->values_did_change = true;
  }
}

String Event::getEventFileVersion(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_file_version;
}


void Event::setEventBand(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  if (this->eventData->event_band != str)
  {
    this->setEventData(EVENT_BAND, str);
    this->values_did_change = true;
  }
}

String Event::getEventBand(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_band;
}

void Event::setCallsign(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  if (this->eventData->event_callsign != str)
  {
    this->setEventData(EVENT_CALLSIGN, str);
    this->values_did_change = true;
  }
}

String Event::getCallsign(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_callsign;
}

void Event::setAntennaPort(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  if (this->eventData->event_antenna_port != str)
  {
    this->setEventData(EVENT_ANTENNA_PORT, str);
    this->values_did_change = true;
  }
}

String Event::getAntennaPort(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_antenna_port;
}

void Event::setCallsignSpeed(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  if (this->eventData->event_callsign_speed != str)
  {
    this->setEventData(EVENT_CALLSIGN_SPEED, str);
    this->values_did_change = true;
  }
}

String Event::getCallsignSpeed(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_callsign_speed;
}

void Event::setEventStartDateTime(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  // Lop off milliseconds if they exist
  if ((str.length() > 20) && (str.lastIndexOf('.') > 0))
  {
    int index = str.lastIndexOf('.');
    str = str.substring(0, index);
    str = str + "Z";
  }

  // Add seconds if they are missing
  if (str.lastIndexOf(':') == str.indexOf(':')) // if time contains only one ':'
  {
    int index = str.lastIndexOf(':') + 3;
    str = str.substring(0, index);
    str = str + ":00Z";
  }

  if (!(this->eventData->event_start_date_time.equals(str)))
  {
    this->setEventData(EVENT_START_DATE_TIME, str);
    this->values_did_change = true;
    //    Serial.println("setEventStartDateTime: str = " + str);
  }
}

String Event::getEventStartDateTime(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_start_date_time;
}


void Event::setEventFinishDateTime(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  // Lop off milliseconds if they exist
  if ((str.length() > 20) && (str.lastIndexOf('.') > 0))
  {
    int index = str.lastIndexOf('.');
    str = str.substring(0, index);
    str = str + "Z";
  }

  // Add seconds if they are missing
  if (str.lastIndexOf(':') == str.indexOf(':')) // if time contains only one ':'
  {
    int index = str.lastIndexOf(':') + 3;
    str = str.substring(0, index);
    str = str + ":00Z";
  }

  if (!(this->eventData->event_finish_date_time.equals(str)))
  {
    this->setEventData(EVENT_FINISH_DATE_TIME, str);
    this->values_did_change = true;
  }
}

String Event::getEventFinishDateTime(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_finish_date_time;
}


void Event::setEventModulation(String str)
{
  if (this->eventData == NULL) return;
  str.trim();

  if (this->eventData->event_modulation != str)
  {
    this->setEventData(EVENT_MODULATION, str);
    this->values_did_change = true;
  }
}

String Event::getEventModulation(void) const
{
  if (this->eventData == NULL) return "";
  return this->eventData->event_modulation;
}


void Event::setEventNumberOfTxTypes(int val)
{
  if (this->eventData == NULL) return;

  if (this->eventData->event_number_of_tx_types != val)
  {
    this->setEventData(EVENT_NUMBER_OF_TX_TYPES, val);
    this->values_did_change = true;
  }
}

void Event::setEventNumberOfTxTypes(String str)
{
  this->setEventNumberOfTxTypes(str.toInt());
  this->values_did_change = true;
}

int Event::getEventNumberOfTxTypes(void) const
{
  if (this->eventData == NULL) return -1;
  return this->eventData->event_number_of_tx_types;
}


bool Event::setRolename(int roleIndex, String str)
{
  if (this->eventData == NULL) return true;
  if (roleIndex < 0) return true;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return true;
  this->eventData->role[roleIndex]->rolename = str;
  //  if (debug_prints_enabled) Serial.println("Type" + String(roleIndex) + " role: " + str);
  this->values_did_change = true;
  return false;
}

String Event::getRolename(int roleIndex) const
{
  if (this->eventData == NULL) return "";
  if (roleIndex < 0) return "";
  if (roleIndex >= this->eventData->event_number_of_tx_types) return "";
  return this->eventData->role[roleIndex]->rolename;
}


bool Event::setNumberOfTxsForRole(int roleIndex, String str)
{
  int num = str.toInt();
  if (num <1) return true;
  if (this->eventData == NULL) return true;
  if (roleIndex < 0) return true;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return true;
  
  this->eventData->role[roleIndex]->numberOfTxs = num;
  //  if (debug_prints_enabled) Serial.println("Type" + String(roleIndex) + " No Txs: " + str);
  this->values_did_change = true;
  return false;
}

int Event::getNumberOfTxsForRole(int roleIndex) const
{
  if (this->eventData == NULL) return -1;
  if (roleIndex < 0) return -1;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return -1;
  return this->eventData->role[roleIndex]->numberOfTxs;
}


bool Event::setFrequencyForRole(int roleIndex, long freq)
{
  if (this->eventData == NULL) return true;
  if (roleIndex < 0) return true;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return true;
  this->eventData->role[roleIndex]->frequency = freq;
  //  if (debug_prints_enabled) Serial.println("Type" + String(roleIndex) + " freq: " + String(freq));
  this->values_did_change = true;

  /* If the transmitter is currently set to this role then update its frequency too */
  if (roleIndex == this->getTxRoleIndex() ) {
    this->setTxFrequency(String(freq));
  }
  return false;
}

long Event::getFrequencyForRole(int roleIndex) const
{
  if (this->eventData == NULL) return -1;
  if (roleIndex < 0) return -1;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return -1;
  return this->eventData->role[roleIndex]->frequency;
}


bool Event::setPowerlevelForRole(int roleIndex, String str)
{
  if (this->eventData == NULL) return true;
  if (roleIndex < 0) return true;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return true;
  this->eventData->role[roleIndex]->powerLevel_mW = str.toInt();
  //  if (debug_prints_enabled) Serial.println("Type" + String(roleIndex) + " freq: " + str);
  this->values_did_change = true;
  return false;
}

int Event::getPowerlevelForRole(int roleIndex) const
{
  if (this->eventData == NULL) return -1;
  if (roleIndex < 0) return -1;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return -1;
  return this->eventData->role[roleIndex]->powerLevel_mW;
}


bool Event::setCodeSpeedForRole(int roleIndex, String str)
{
  if (this->eventData == NULL) return true;
  if (roleIndex < 0) return true;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return true;
  this->eventData->role[roleIndex]->code_speed = str.toInt();
  //  if (debug_prints_enabled) Serial.println("Type" + String(roleIndex) + " freq: " + str);
  this->values_did_change = true;
  return false;
}

int Event::getCodeSpeedForRole(int roleIndex) const
{
  if (this->eventData == NULL) return -1;
  if (roleIndex < 0) return -1;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return -1;
  return this->eventData->role[roleIndex]->code_speed;
}


bool Event::setIDIntervalForRole(int roleIndex, String str)
{
  if (this->eventData == NULL) return true;
  if (roleIndex < 0) return true;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return true;
  this->eventData->role[roleIndex]->id_interval = str.toInt();
  //  if (debug_prints_enabled) Serial.println("Type" + String(roleIndex) + " freq: " + str);
  this->values_did_change = true;
  return false;
}

int Event::getIDIntervalForRole(int roleIndex) const
{
  if (this->eventData == NULL) return -1;
  if (roleIndex < 0) return -1;
  if (roleIndex >= this->eventData->event_number_of_tx_types) return -1;
  return this->eventData->role[roleIndex]->id_interval;
}



/////////////////////////////////////////////////////////////////////////////////////

bool Event::setEventData(String id, int value) {
  return this->setEventData(id, String(value));
}

bool Event::setEventData(String id, String value) {
  bool result = false;
  static String typeIndexStr = "";
  static int typeIndex = 0;
  static String txIndexStr = "";
  static int txIndex = 0;

  if (id.equalsIgnoreCase(TX_ASSIGNMENT))
  {
    //    if (debug_prints_enabled) Serial.println("Tx assignment: " + value);
    this->eventData->tx_assignment = value;
  }
  else if (id.equalsIgnoreCase(TX_DESCRIPTIVE_NAME))
  {
    this->eventData->tx_role_name = value;
  }
  else if (id.equalsIgnoreCase(TX_ROLE_FREQ))
  {
    this->eventData->tx_role_freq = value;
  }
  else if (id.equalsIgnoreCase(TX_ASSIGNMENT_IS_DEFAULT))
  {
    //    if (debug_prints_enabled) Serial.println("Tx is default: " + value);
    if (value.equalsIgnoreCase("TRUE") || value.equals("1"))
    {
      this->eventData->tx_assignment_is_default = true;
    }
    else
    {
      this->eventData->tx_assignment_is_default = false;
    }
  }
  else if (id.equalsIgnoreCase(EVENT_NAME))
  {
    //    if (debug_prints_enabled) Serial.println("Event name: " + value);
    this->eventData->event_name = value;
  }
  else if (id.equalsIgnoreCase(EVENT_FILE_VERSION))
  {
    //    if (debug_prints_enabled) Serial.println("File ver: " + value);
    this->eventData->event_file_version = value;
  }
  else if (id.equalsIgnoreCase(EVENT_BAND))
  {
    //    if (debug_prints_enabled) Serial.println("Event band: " + value);
    this->eventData->event_band = value;
  }
  else if (id.equalsIgnoreCase(EVENT_CALLSIGN))
  {
    //    if (debug_prints_enabled) Serial.println("Event callsign: " + value);
    this->eventData->event_callsign = value;
  }
  else if (id.equalsIgnoreCase(EVENT_ANTENNA_PORT))
  {
    //    if (debug_prints_enabled) Serial.println("Event antenna: " + value);
    this->eventData->event_antenna_port = value;
  }
  else if (id.equalsIgnoreCase(EVENT_CALLSIGN_SPEED))
  {
    //    if (debug_prints_enabled) Serial.println("Event ID speed: " + value);
    this->eventData->event_callsign_speed = value;
  }
  else if (id.equalsIgnoreCase(EVENT_START_DATE_TIME))
  {
    //    if (debug_prints_enabled) Serial.println("Event start: " + value);
    this->eventData->event_start_date_time = value;
  }
  else if (id.equalsIgnoreCase(EVENT_FINISH_DATE_TIME))
  {
    //    if (debug_prints_enabled) Serial.println("Event finish: " + value);
    this->eventData->event_finish_date_time = value;
  }
  else if (id.equalsIgnoreCase(EVENT_MODULATION))
  {
    //    if (debug_prints_enabled) Serial.println("Event modulation: " + value);
    this->eventData->event_modulation = value;
  }
  else if (id.equalsIgnoreCase(EVENT_NUMBER_OF_TX_TYPES))
  {
    //    if (debug_prints_enabled) Serial.println("Event tx types: " + value);
    this->eventData->event_number_of_tx_types = value.toInt();
  }
  else if (id.endsWith(TYPE_TX_COUNT))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt() - 1;
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      this->eventData->role[typeIndex]->numberOfTxs = value.toInt();
      //      if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + " count: " + value);
    }
  }
  else if (id.endsWith(TYPE_NAME))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt() - 1;
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      this->eventData->role[typeIndex]->rolename = value;
      //      if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + " role: " + value);
    }
  }
  else if (id.endsWith(TYPE_FREQ))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt() - 1;
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      this->eventData->role[typeIndex]->frequency = value.toInt();
      //      if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + " freq: " + value);
    }
  }
  else if (id.endsWith(TYPE_POWER_LEVEL))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt() - 1;
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      this->eventData->role[typeIndex]->powerLevel_mW = value.toInt();
      //      if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + " power: " + value + " mW");
    }
  }
  else if (id.endsWith(TYPE_ID_INTERVAL))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt() - 1;
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      this->eventData->role[typeIndex]->id_interval = value.toInt();
      //      if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + " ID interval: " + value + " sec");
    }
  }
  else if (id.endsWith(TYPE_CODE_SPEED))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt() - 1;
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      this->eventData->role[typeIndex]->code_speed = value.toInt();
      //      if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + " code speed: " + value + " WPM");
    }
  }
  else if (id.endsWith(TYPE_TX_PATTERN))
  {
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      int i = id.indexOf("TX") + 2;
      txIndexStr = id.substring(i, id.indexOf("_", i));
      txIndex = txIndexStr.toInt() - 1;

      if ((txIndex >= 0) && (txIndex < MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE))
      {
        this->eventData->role[typeIndex]->tx[txIndex]->pattern = value;
        //        if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + "Tx" + txIndexStr + " pattern: " + value);
      }
    }
  }
  else if (id.endsWith(TYPE_TX_ON_TIME))
  {
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      int i = id.indexOf("TX") + 2;
      txIndexStr = id.substring(i, id.indexOf("_", i));
      txIndex = txIndexStr.toInt() - 1;

      if ((txIndex >= 0) && (txIndex < MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE))
      {
        this->eventData->role[typeIndex]->tx[txIndex]->onTime = value;
        //        if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + "Tx" + txIndexStr + " on time: " + value + "sec");
      }
    }
  }
  else if (id.endsWith(TYPE_TX_OFF_TIME))
  {
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      int i = id.indexOf("TX") + 2;
      txIndexStr = id.substring(i, id.indexOf("_", i));
      txIndex = txIndexStr.toInt() - 1;

      if ((txIndex >= 0) && (txIndex < MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE))
      {
        this->eventData->role[typeIndex]->tx[txIndex]->offTime = value.toInt();
        //        if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + "Tx" + txIndexStr + " off time: " + value + "sec");
      }
    }
  }
  else if (id.endsWith(TYPE_TX_DELAY_TIME))
  {
    if ((typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
    {
      int i = id.indexOf("TX") + 2;
      txIndexStr = id.substring(i, id.indexOf("_", i));
      txIndex = txIndexStr.toInt() - 1;

      if ((txIndex >= 0) && (txIndex < MAXIMUM_NUMBER_OF_TXs_OF_A_TYPE))
      {
        this->eventData->role[typeIndex]->tx[txIndex]->delayTime = value;
        //        if (debug_prints_enabled) Serial.println("Type" + typeIndexStr + "Tx" + txIndexStr + " delay time: " + value + "sec");
      }
    }
  }
  else
  {
    if (debug_prints_enabled)
    {
      Serial.println("Error in file: SettingID = " + id + " Value = [" + value + "]");
    }

    result = true;
  }

  return result;
}

