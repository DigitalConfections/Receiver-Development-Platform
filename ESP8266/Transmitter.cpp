
#include "Transmitter.h"


Transmitter::Transmitter(bool debug) {
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
  else if (id.equalsIgnoreCase(CALLSIGN_SETTING))
  {
    callsignSetting = value;
  }
  else if (id.equalsIgnoreCase(MANUAL_OR_TIMED_SETTING))
  {
    manualTimingSetting = value;
  }
  else if (id.equalsIgnoreCase(CLASSIC_2M_ID_SETTING))
  {
    classic2mIDSetting = value;
  }
  else if (id.equalsIgnoreCase(CLASSIC_80M_ID_SETTING))
  {
    classic80mIDSetting = value;
  }
  else if (id.equalsIgnoreCase(SPRINT_80M_ID_SETTING))
  {
    sprint80mIDSetting = value;
  }
  else if (id.equalsIgnoreCase(FOX_O_80M_ID_SETTING))
  {
    foxO_80mIDSetting = value;
  }
  else if (id.equalsIgnoreCase(CLASSIC_2M_START_DATE_TIME_SETTING))
  {
    classic2MstartDateTimeSetting = value;
  }
  else if (id.equalsIgnoreCase(CLASSIC_2M_FINISH_DATE_TIME_SETTING))
  {
    classic2MfinishDateTimeSetting = value;
  }
  else if (id.equalsIgnoreCase(CLASSIC_80M_START_DATE_TIME_SETTING))
  {
    classic80MstartDateTimeSetting = value;
  }
  else if (id.equalsIgnoreCase(CLASSIC_80M_FINISH_DATE_TIME_SETTING))
  {
    classic80MfinishDateTimeSetting = value;
  }
  else if (id.equalsIgnoreCase(SPRINT_START_DATE_TIME_SETTING))
  {
    sprintStartDateTimeSetting = value;
  }
  else if (id.equalsIgnoreCase(SPRINT_FINISH_DATE_TIME_SETTING))
  {
    sprintFinishDateTimeSetting = value;
  }
  else if (id.equalsIgnoreCase(FOX_O_START_DATE_TIME_SETTING))
  {
    foxOstartDateTimeSetting = value;
  }
  else if (id.equalsIgnoreCase(FOX_O_FINISH_DATE_TIME_SETTING))
  {
    foxOfinishDateTimeSetting = value;
  }
  else if (id.equalsIgnoreCase(CLASSIC_2M_FOX_FREQUENCY_SETTING))
  {
    classic2mFoxFrequencySetting = value;
  }
  else if (id.equalsIgnoreCase(CLASSIC_2M_HOME_FREQUENCY_SETTING))
  {
    classic2mHomeFrequencySetting = value; // Classic event only (e.g., 144.900 MHz)
  }
  else if (id.equalsIgnoreCase(CLASSIC_80M_FOX_FREQUENCY_SETTING))
  {
    classic80mFoxFrequency1Setting = value; // Classic event only (e.g., 3.510 MHz)
  }
  else if (id.equalsIgnoreCase(CLASSIC_80M_HOME_FREQUENCY_SETTING))
  {
    classic80mHomeFrequencySetting = value; // Classic, Fox-O events only (e.g., 3.600 MHz)
  }
  else if (id.equalsIgnoreCase(FOX_O_80M_FOX_FREQUENCY1_SETTING))
  {
    fox_O_80mFoxFrequency1Setting = value; // Fox-O event only (e.g., 3.510 MHz)
  }
  else if (id.equalsIgnoreCase(FOX_O_80M_FOX_FREQUENCY2_SETTING))
  {
    fox_O_80mFoxFrequency2Setting = value; // Fox-O event only (e.g., 3.540 MHz)
  }
  else if (id.equalsIgnoreCase(FOX_O_80M_FOX_FREQUENCY3_SETTING))
  {
    fox_O_80mFoxFrequency3Setting = value; // Fox-O event only (e.g., 3.570 MHz)
  }
  else if (id.equalsIgnoreCase(FOX_O_80M_HOME_FREQUENCY_SETTING))
  {
    fox_O_80mHomeFrequencySetting = value; // Fox-O event only (e.g., 3.570 MHz)
  }
  else if (id.equalsIgnoreCase(SPRINT_80M_SPECTATOR_FREQUENCY_SETTING))
  {
    sprintSpectator80mFrequencySetting = value; // Sprint event only (e.g., 3.550 MHz)
  }
  else if (id.equalsIgnoreCase(SPRINT_80M_SLOW_FREQUENCY_SETTING))
  {
    sprintSlow80mFrequencySetting = value; // Sprint event only (e.g., 3.530 MHz)
  }
  else if (id.equalsIgnoreCase(SPRINT_80M_FAST_FREQUENCY_SETTING))
  {
    sprintFast80mFrequencySetting = value; // Sprint event only (e.g., 3.570 MHz)
  }
  else if (id.equalsIgnoreCase(SPRINT_80M_FINISH_FREQUENCY_SETTING))
  {
    sprintFinish80mFrequencySetting = value; // Sprint event only (e.g., 3.600 MHz)
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

