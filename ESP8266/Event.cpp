
#include "Event.h"


Event::Event(bool debug) {

#ifndef EVENT_DEBUG_PRINTS_OVERRIDE
  debug_prints_enabled = debug;
#else
  debug_prints_enabled = EVENT_DEBUG_PRINTS_OVERRIDE;
#endif
}

bool Event::extractLineData(String s, EventLineData* result) {
  if (s.indexOf(',') < 0)
  {
    return true; // invalid line found
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
  extractLineData(s, &data);
  return (setEventData(data.id, data.value));
}

/**
 * Returns a boolean indicating if event a will commence sooner that b relative to the currentEpoch
 */
bool Event::isSoonerEvent(EventFileRef a, EventFileRef b, unsigned long currentEpoch)
{
  if(a.startDateTimeEpoch <= currentEpoch) /* a started in the past */
  {
    if(a.finishDateTimeEpoch <= currentEpoch) return false; /* a finished in the past so is disabled */

    if(b.startDateTimeEpoch <= currentEpoch) /* b started in the past */
    {
       if(b.finishDateTimeEpoch < currentEpoch) return true; /* b finished in the past so is disabled */

       return(a.startDateTimeEpoch > b.startDateTimeEpoch); /* if a started later than b then it is closer */
    }

    return true; /* a is in progress and b hasn't started yet, so a is sooner */
  }
  else /* a starts in the future */
  {
    if(b.startDateTimeEpoch <= currentEpoch) /* b started in the past */
    {
       if(b.finishDateTimeEpoch < currentEpoch) return true; /* b finished in the past so is disabled */

       return false; /* a hasn't started yet, and b has, so b is sooner */
    }

    return(a.startDateTimeEpoch < b.startDateTimeEpoch); /* true if a will start sooner than b */
  }
}


bool Event::setEventData(String id, String value) {
  bool result = false;
  static String typeIndexStr = "";
  static int typeIndex = 0;
  static String txIndexStr = "";
  static int txIndex = 0;

  if (id.equalsIgnoreCase(EVENT_NAME))
  {
    if (debug_prints_enabled) Serial.println("Event name: " + value);
    event_name = value;
  }
  else if (id.equalsIgnoreCase(EVENT_BAND))
  {
    if (debug_prints_enabled) Serial.println("Event band: " + value);
    event_band = value;
  }
  else if (id.equalsIgnoreCase(EVENT_CALLSIGN))
  {
    if (debug_prints_enabled) Serial.println("Event callsign: " + value);
    event_callsign = value;
  }
  else if (id.equalsIgnoreCase(EVENT_CALLSIGN_SPEED))
  {
    if (debug_prints_enabled) Serial.println("Event ID speed: " + value);
    event_callsign_speed = value;
  }
  else if (id.equalsIgnoreCase(EVENT_START_DATE_TIME))
  {
    if (debug_prints_enabled) Serial.println("Event date/time: " + value);
    event_start_date_time = value;
  }
  else if (id.equalsIgnoreCase(EVENT_FINISH_DATE_TIME))
  {
    if (debug_prints_enabled) Serial.println("Event name: " + value);
    event_finish_date_time = value;
  }
  else if (id.equalsIgnoreCase(EVENT_MODULATION))
  {
    if (debug_prints_enabled) Serial.println("Event modulation: " + value);
    event_modulation = value;
  }
  else if (id.equalsIgnoreCase(EVENT_NUMBER_OF_TX_TYPES))
  {
    if (debug_prints_enabled) Serial.println("Event tx types: " + value);
    event_number_of_tx_types = value.toInt();
  }
  else if (id.endsWith(TYPE_TX_COUNT))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt();
    txType[typeIndex].numberOfTxs = value.toInt();
    if (debug_prints_enabled) Serial.println("Tx Type" + typeIndexStr + " count: " + value);
  }
  else if (id.endsWith(TYPE_FREQ))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt();
    txType[typeIndex].frequency = value.toInt();
    if (debug_prints_enabled) Serial.println("Tx Type" + typeIndexStr + " freq: " + value);
  }
  else if (id.endsWith(TYPE_POWER_LEVEL))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt();
    txType[typeIndex].powerLevel_mW = value.toInt();
    if (debug_prints_enabled) Serial.println("Tx Type" + typeIndexStr + " power: " + value + " mW");
  }
  else if (id.endsWith(TYPE_CODE_SPEED))
  {
    typeIndexStr = id.substring((id.indexOf("TYPE") + 4), id.indexOf("_"));
    typeIndex = typeIndexStr.toInt();
    txType[typeIndex].code_speed = value.toInt();
    if (debug_prints_enabled) Serial.println("Tx Type" + typeIndexStr + " code speed: " + value + " WPM");
  }
  else if (id.endsWith(TYPE_TX_PATTERN))
  {
    int i = id.indexOf("TX") + 2;
    txIndexStr = id.substring(i, id.indexOf("_", i));
    txIndex = txIndexStr.toInt();
    txType[typeIndex].tx[txIndex].pattern = value;
    if (debug_prints_enabled) Serial.println("Tx Type" + typeIndexStr + "Tx" + txIndexStr + " pattern: " + value);
  }
  else if (id.endsWith(TYPE_TX_ON_TIME))
  {
    int i = id.indexOf("TX") + 2;
    txIndexStr = id.substring(i, id.indexOf("_", i));
    txIndex = txIndexStr.toInt();
    txType[typeIndex].tx[txIndex].onTime = value.toInt();
    if (debug_prints_enabled) Serial.println("Tx Type" + typeIndexStr + "Tx" + txIndexStr + " on time: " + value + "sec");
  }
  else if (id.endsWith(TYPE_TX_OFF_TIME))
  {
    int i = id.indexOf("TX") + 2;
    txIndexStr = id.substring(i, id.indexOf("_", i));
    txIndex = txIndexStr.toInt();
    txType[typeIndex].tx[txIndex].offTime = value.toInt();
    if (debug_prints_enabled) Serial.println("Tx Type" + typeIndexStr + "Tx" + txIndexStr + " off time: " + value + "sec");
  }
  else if (id.endsWith(TYPE_TX_DELAY_TIME))
  {
    int i = id.indexOf("TX") + 2;
    txIndexStr = id.substring(i, id.indexOf("_", i));
    txIndex = txIndexStr.toInt();
    txType[typeIndex].tx[txIndex].delayTime = value.toInt();
    if (debug_prints_enabled) Serial.println("Tx Type" + typeIndexStr + "Tx" + txIndexStr + " delay time: " + value + "sec");
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

