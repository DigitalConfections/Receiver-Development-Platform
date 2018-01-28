#ifndef _HELPERS_H_
  #define _HELPERS_H_

#include <ESP8266WiFi.h>

#ifndef SecondsFromHours
  #define SecondsFromHours(hours) ((hours) * 3600)
#endif

#ifndef SecondsFromMinutes
  #define SecondsFromMinutes(min) ((min) * 60)
#endif

#ifndef HoursFromSeconds
  #define HoursFromSeconds(seconds) ((seconds) / 3600)
#endif

#ifndef MinutesFromSeconds
  #define MinutesFromSeconds(seconds) ((seconds) / 60)
#endif

#ifndef min
  #define min(x, y)  ((x) < (y) ? (x) : (y))
#endif

const char * stringObjToConstCharString(String *val);
IPAddress stringToIP(String addr);
String formatBytes(size_t bytes);
String getContentType(String filename);
int32_t stringToTimeVal(String string);
String timeValToString(int32_t secSinceMN);

#endif //_HELPERS_H_
