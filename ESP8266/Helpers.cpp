#include "Helpers.h"
/*
 * Removes extraneous characters that tend to clutter the ends of String object contents. Then
 * returns a const char * to the C string contained in the String.
 */
const char * stringObjToConstCharString(String *val)
{
  char str[50]; 

  strcpy(str, (*val).c_str());
  
  for(int i=0; i<strlen(str); i++)
  {
    char c = str[i];
    if((!(isalnum(c) || isprint(c))) || (c == 13))
    {
      str[i] = '\0';
      break;
    }
  }

  *val = str;

  return((const char*)(*val).c_str());
}


/*
   Converts a String containing a valid IP address into an IPAddress.
   Returns the IPAddress contained in addr, or returns "-1,-1,-1,-1" if the address is invalid
*/
IPAddress stringToIP(String addr)
{
  IPAddress ipAddr = IPAddress(-1, -1, -1, -1);
  int dot, nextDot;
  /* 129.6.15.28 = NIST, Gaithersburg, Maryland */
  String o1 = "129", o2 = "6", o3 = "15", o4 = "28";  /* default to some reasonable IP address */

  dot = addr.indexOf(".", 0);

  if (dot > 0)
  {
    o1 = addr.substring(0, dot);

    if ((o1.toInt() >= 0) && (o1.toInt() <= 255))
    {
      dot++;
      nextDot = addr.indexOf(".", dot);

      if (nextDot > 0)
      {
        o2 = addr.substring(dot, nextDot);

        if ((o2.toInt() >= 0) && (o2.toInt() <= 255))
        {
          nextDot++;
          dot = addr.indexOf(".", nextDot);

          if (dot > 0)
          {
            o3 = addr.substring(nextDot, dot);
            dot++;
            o4 = addr.substring(dot);

            if (((o3.toInt() >= 0) && (o3.toInt() <= 255)) && ((o4.toInt() >= 0) && (o4.toInt() <= 255)))
            {

              ipAddr = IPAddress(o1.toInt(), o2.toInt(), o3.toInt(), o4.toInt());
            }
          }
        }
      }
    }
  }

  return (ipAddr);
}


String formatBytes(size_t bytes)
{ // convert sizes in bytes to KB and MB
  if (bytes < 1024)
  {
    return String(bytes) + "B";
  }
  else if (bytes < (1024 * 1024))
  {
    return String(bytes / 1024.0) + "KB";
  }
  else if (bytes < (1024 * 1024 * 1024))
  {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

String getContentType(String filename)
{ // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}




