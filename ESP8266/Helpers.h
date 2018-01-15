#ifndef _HELPERS_H_
  #define _HELPERS_H_

#include <ESP8266WiFi.h>

const char * stringObjToConstCharString(String *val);
IPAddress stringToIP(String addr);
String formatBytes(size_t bytes);
String getContentType(String filename);

#endif //_HELPERS_H_
