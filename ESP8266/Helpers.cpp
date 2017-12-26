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

