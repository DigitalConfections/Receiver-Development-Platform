
#include "Transmitter.h"


Transmitter::Transmitter(bool debug) {
#ifdef TRANSMITTER_DEBUG_PRINTS_OVERRIDE
  debug = TRANSMITTER_DEBUG_PRINTS_OVERRIDE;
#endif
  debug_prints_enabled = debug;
}

bool Transmitter::parseStringData(String s) {
  if (s.indexOf(',') < 0)
  {
    return true; // invalid line found
  }

  String settingID = s.substring(0, s.indexOf(','));
  String value = s.substring(s.indexOf(',') + 1, s.indexOf('\n'));

  if (value.charAt(0) == '"')
  {
    if (value.charAt(1) == '"') // handle empty string
    {
      value = "";
    }
    else // remove quotes
    {
      value = value.substring(1, value.length() - 2);
    }
  }

  return (setXmtrData(settingID, value));
}

bool Transmitter::setXmtrData(String id, String value) {
  bool result = false;

  if (id.equalsIgnoreCase(MASTER_OR_CLONE_SETTING))
  {
    masterCloneSetting = value;
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

