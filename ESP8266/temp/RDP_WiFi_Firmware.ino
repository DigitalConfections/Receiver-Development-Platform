/**********************************************************************************************
   Copyright © 2017 Digital Confections LLC

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

 **********************************************************************************************

    Basic WiFi Functionality for Receiver Development Platform

    Hardware Target: Adafruit HUZZAH ESP8266

    This sketch provides the following functionality on the target hardware:

      1. Receives commands over the UART0 port. Commands must start with $$$ as the escape sequence.

      2. $$$m,#,val;  <- this command allows the ATmega328p to set variables that the WiFi board
         will use; such as the SSID and password for a hotspot for Internet access.

      3. $$$t; <- this tells the WiFi to connect to an Internet hotspot and attempt to read the current
         NIST time; if successful the WiFi sends a command to the ATmega328p telling it to update the time
         stored in the real-time clock.

      4. $$$b; <- this tells the WiFi to set itself up as a UART-to-TCP bridge, and accept TCP connections
         from any clients (like a smartphone or PC)

      5. $$$w; <- starts a web server that accepts connections from WiFi devices.

*/
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include "esp8266.h"
#include <ArduinoOTA.h>
#include <FS.h>
#include <WebSocketsServer.h>
/* #include <Wire.h> */
#include "Helpers.h"


/* Global variables are always prefixed with g_ */

/*
   HUZZAH-specific Defines
*/
#define RED_LED (0)
#define BLUE_LED (2)

BOOL g_debug_prints_enabled = DEBUG_PRINTS_ENABLE_DEFAULT;
BOOL g_LEDs_enabled = LEDS_ENABLE_DEFAULT;

/*
   TCP to UART Bridge
*/

#define MAX_SRV_CLIENTS 3   /*how many clients should be able to telnet to this ESP8266 */
WiFiServer g_tcpServer(BRIDGE_TCP_PORT_DEFAULT.toInt());
WiFiClient g_tcpServerClients[MAX_SRV_CLIENTS];

String g_bridgeIPaddr = BRIDGE_IP_ADDR_DEFAULT;
String g_bridgeSSID = BRIDGE_SSID_DEFAULT;
String g_bridgePW = BRIDGE_PW_DEFAULT;  /* minimum 8 characters */
String g_bridgeTCPport = BRIDGE_TCP_PORT_DEFAULT;


/* Access Point */
String g_hotspotSSID1 = HOTSPOT_SSID1_DEFAULT;
String g_hotspotPW1 = HOTSPOT_PW1_DEFAULT;
String g_hotspotSSID2 = HOTSPOT_SSID2_DEFAULT;
String g_hotspotPW2 = HOTSPOT_PW2_DEFAULT;
String g_hotspotSSID3 = HOTSPOT_SSID3_DEFAULT;
String g_hotspotPW3 = HOTSPOT_PW3_DEFAULT;
String g_mDNS_responder = MDNS_RESPONDER_DEFAULT;

/* Time Retrieval */
/*
   NIST Time Sync
   See https://www.arduino.cc/en/Tutorial/UdpNTPClient
   See http://tf.nist.gov/tf-cgi/servers.cgi
*/
String g_timeHost = TIME_HOST_DEFAULT;
String g_timeHTTPport = TIME_HTTP_PORT_DEFAULT;

/* UDP time query */
#define NTP_PACKET_SIZE (48)        /* NTP time stamp is in the first 48 bytes of the message */
WiFiUDP g_UDP;                      /* A UDP instance to let us send and receive packets over UDP */
unsigned int g_localPort = 2390;    /* local port to listen for UDP  packets */

/* HTTP web server */
ESP8266WebServer g_http_server(80); /* HTTP server on port 80 */
WebSocketsServer webSocket = WebSocketsServer(81);  // create a websocket server on port 81
ESP8266WiFiMulti wifiMulti;     // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
File fsUploadFile;  // a File variable to temporarily store the received file

String g_softAP_IP_addr;

void handleRoot();              // function prototypes for HTTP handlers
void handleLED();
void handleNotFound();

/*
   Main Program Support
*/

unsigned long g_relativeTimeSeconds;
unsigned long g_lastAccessToNISTServers = 0;
BOOL g_timeWasSet = FALSE;
int g_blinkPeriodMillis = 500;

void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);
  while (!Serial)
  {
    ;                           /* Wait for UART to initialize */
  }
  pinMode(RED_LED, OUTPUT);       /* Allow the red LED to be controlled */
  pinMode(BLUE_LED, OUTPUT);      /* Initialize the BUILTIN_LED pin as an output */
  digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
  digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */

  startSPIFFS();    // Start the SPIFFS and list all contents
  if (!readDefaultsFile())  // Read default settings from file system
  {
    saveDefaultsFile();
  }

  showSettings();
}


bool setupTCP2UART()
{
  bool success = false;
  WiFi.mode(WIFI_AP);

  /* Create a somewhat unique SSID */
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String ap_NameString = g_bridgeSSID + macID;

  IPAddress apIP = stringToIP(g_bridgeIPaddr);

  if (apIP == IPAddress(-1, -1, -1, -1))
  {
    apIP = stringToIP(String(SOFT_AP_IP_ADDR_DEFAULT));   /* some reasonable default address */
  }

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  success = WiFi.softAP(stringObjToConstCharString(&ap_NameString), stringObjToConstCharString(&g_bridgePW));

  if (success)
  {
    /* Start TCP listener on port TCP_PORT */
    g_tcpServer.begin();
    g_tcpServer.setNoDelay(true);

    if (g_debug_prints_enabled)
    {
      Serial.println(String("Ready! TCP to ") + g_bridgeIPaddr + String(" port ") + g_bridgeTCPport + String(" to connect"));
    }
  }

  return (success);
}

//===============================================================
// This routine is executed when you open its IP in browser
//===============================================================
//void handleRoot()
//{
//String s = MAIN_page; //Read HTML contents
//g_http_server.send(200, "text/html", s); //Send web page
//sendSettings();
//}

//void handleRoot()
//{                         // When URI / is requested, send a web page with a button to toggle the LED
//  g_http_server.send(200, "text/html", "<form action=\"/LED\" method=\"POST\"><input type=\"submit\" value=\"Toggle LED\"></form>");
//}

void handleRoot()
{ // When URI / is requested, send a web page with a button to toggle the LED
  g_http_server.send(200, "text/html", "<form action=\"/login\" method=\"POST\"><input type=\"text\" name=\"username\" placeholder=\"Username\"></br><input type=\"password\" name=\"password\" placeholder=\"Password\"></br><input type=\"submit\" value=\"Login\"></form><p>Try 'John Doe' and 'password123' ...</p>");
}

void handleLogin()
{ // If a POST request is made to URI /login
  if ( ! g_http_server.hasArg("username") || ! g_http_server.hasArg("password")
       || g_http_server.arg("username") == NULL || g_http_server.arg("password") == NULL)
  { // If the POST request doesn't have username and password data
    g_http_server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
    return;
  }

  if (g_http_server.arg("username") == "John Doe" && g_http_server.arg("password") == "password123")
  { // If both the username and the password are correct
    g_http_server.send(200, "text/html", "<h1>Welcome, " + g_http_server.arg("username") + "!</h1><p>Login successful</p>");
  }
  else
  { // Username and password don't match
    g_http_server.send(401, "text/plain", "401: Unauthorized");
  }
}


void handleLED()
{ // If a POST request is made to URI /LED
  digitalWrite(RED_LED, !digitalRead(RED_LED));    /* Toggle red LED */
  g_http_server.sendHeader("Location", "/");       // Add a header to respond with a new location for the browser to go to the home page again
  g_http_server.send(303);                         // Send it back to the browser with an HTTP status 303 (See Other) to redirect
}

//void handleNotFound()
//{
//  g_http_server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
//}
void handleNotFound()
{ // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(g_http_server.uri()))
  { // check if the file exists in the flash memory (SPIFFS), if so, send it
    g_http_server.send(404, "text/plain", "404: File Not Found");
  }
}



bool setupHTTP_AP()
{
  bool success = false;
  startWebSocket(); // Start a WebSocket server

  //  WiFi.mode(WIFI_AP_STA);
  WiFi.mode(WIFI_AP);

  /* Create a somewhat unique SSID */
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String ap_NameString = String("Tx_" + macID);

  IPAddress apIP = stringToIP(g_softAP_IP_addr);

  if (apIP == IPAddress(-1, -1, -1, -1))
  {
    apIP = stringToIP(String(SOFT_AP_IP_ADDR_DEFAULT));   /* some reasonable default address */
  }

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  if (g_bridgePW.length() > 0)
  {
    success = WiFi.softAP(stringObjToConstCharString(&ap_NameString), stringObjToConstCharString(&g_bridgePW));
  }
  else
  {
    success = WiFi.softAP(stringObjToConstCharString(&ap_NameString));
  }

  if (success)
  {
    IPAddress myIP = WiFi.softAPIP(); //Get IP address

    if (MDNS.begin(stringObjToConstCharString(&g_mDNS_responder)))
    { // Start the mDNS responder for e.g., "esp8266.local"
      Serial.println("mDNS responder started: " + g_mDNS_responder + ".local");
    }
    else
    {
      Serial.println("Error setting up MDNS responder!");
    }

    /* Start TCP listener on port TCP_PORT */
    //  g_http_server.on("/", handleRoot); /* serve the root web page */
    //  g_http_server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"
    //  g_http_server.on("/", HTTP_GET, handleRoot);     // Call the 'handleRoot' function when a client requests URI "/"
    g_http_server.on("/LED", HTTP_POST, handleLED);  // Call the 'handleLED' function when a POST request is made to URI "/LED"
    g_http_server.on("/login", HTTP_POST, handleLogin); // Call the 'handleLogin' function when a POST request is made to URI "/login"
   // g_http_server.on("/edit.html",  HTTP_POST, []() 
   // {  // If a POST request is sent to the /edit.html address,
   //   g_http_server.send(200, "text/plain", ""); 
   // }, handleFileUpload);                       // go to 'handleFileUpload'

    g_http_server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"
    g_http_server.begin();

    if (g_debug_prints_enabled)
    {
      Serial.print(ap_NameString + " server started: ");
      Serial.println(myIP);
    }
  }

  return (success);
}


bool setupWiFiAPConnection()
{
  BOOL err = FALSE;
  int tries = 0;

  /* We start by connecting to a WiFi network */

#ifdef MULTI_ACCESS_POINT_SUPPORT
  if (strlen(stringObjToConstCharString(&g_hotspotPW1)) > 0)
  {
    wifiMulti.addAP(stringObjToConstCharString(&g_hotspotSSID1), stringObjToConstCharString(&g_hotspotPW1));   // add Wi-Fi networks you want to connect to
  }
  else
  {
    wifiMulti.addAP(stringObjToConstCharString(&g_hotspotSSID1));
  }

  if (strlen(stringObjToConstCharString(&g_hotspotPW2)) > 0)
  {
    wifiMulti.addAP(stringObjToConstCharString(&g_hotspotSSID2), stringObjToConstCharString(&g_hotspotPW2));   // add Wi-Fi networks you want to connect to
  }
  else
  {
    wifiMulti.addAP(stringObjToConstCharString(&g_hotspotSSID2));
  }

  if (strlen(stringObjToConstCharString(&g_hotspotPW3)) > 0)
  {
    wifiMulti.addAP(stringObjToConstCharString(&g_hotspotSSID3), stringObjToConstCharString(&g_hotspotPW3));   // add Wi-Fi networks you want to connect to
  }
  else
  {
    wifiMulti.addAP(stringObjToConstCharString(&g_hotspotSSID3));
  }

  Serial.println("Connecting ...");
  while (wifiMulti.run() != WL_CONNECTED)
  {
    delay(250);
    if (g_debug_prints_enabled)
    {
      Serial.print(".");
    }

    tries++;

    if (tries > 60)
    {
      err = TRUE;
      break;
    }
  }

#else

  if (g_debug_prints_enabled)
  {
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(g_hotspotSSID);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect();
    delay(2500);
  }

  if (g_hotspotPW1.length() < 1)
  {
    WiFi.begin(stringObjToConstCharString(&g_hotspotSSID1);
  }
  else
  {
    WiFi.begin(stringObjToConstCharString(&g_hotspotSSID1), stringObjToConstCharString(&g_hotspotPW1));
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    if (g_debug_prints_enabled)
    {
      Serial.print(".");
    }

    tries++;

    if (tries > 60)
    {
      err = TRUE;
      break;
    }
    else if ((tries == 20) || (tries == 40))
    {
      if (g_hotspotPW.length() < 1)
      {
        WiFi.begin(stringObjToConstCharString(&g_hotspotSSID));
      }
      else
      {
        WiFi.begin(stringObjToConstCharString(&g_hotspotSSID), stringObjToConstCharString(&g_hotspotPW));
      }
    }
  }
#endif // MULTI_ACCESS_POINT_SUPPORT


  if (err)
  {
    if (g_debug_prints_enabled)
    {
      Serial.println('\n');
      Serial.println("Unable to find any access points.");
    }
  }
  else
  {
    if (g_LEDs_enabled)
    {
      digitalWrite(BLUE_LED, LOW);    /* Turn on blue LED */
    }

    if (g_debug_prints_enabled)
    {
      Serial.println('\n');
      Serial.print("Connected to ");
      Serial.println(WiFi.SSID());  // Tell us what network we're connected to
      Serial.print("IP address:\t");
      Serial.println(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer
    }
  }

  return (err);
}


void loop()
{
  int value = 0;
  BOOL toggle = FALSE;
  unsigned long holdTime;
  int commaLoc;
  int nextCommaLoc;
  String arg1, arg2;
  BOOL argumentsRcvd = FALSE;
  BOOL commandInProgress = FALSE;
  int escapeCount = 0;
  BOOL done = FALSE;

  while (!done)
  {
    if (commandInProgress)
    {
      int len;
      String command;

      command = Serial.readStringUntil(';');
      len = command.length();

      if (len)
      {
        value = command.c_str()[0];

        if (g_debug_prints_enabled)
        {
          Serial.println("Command received...");
        }

        /* Extract arguments from commands containing them */
        if ((value == 'M') || (value == 'm'))
        {
          argumentsRcvd = FALSE;

          if (len > 1)
          {
            arg1 = "";
            arg2 = "";

            if (g_debug_prints_enabled)
            {
              Serial.println("Parsing command...");
            }

            commaLoc = 1 + command.indexOf(",");

            if (commaLoc > 1)
            {
              nextCommaLoc = command.indexOf(",", commaLoc);

              if (nextCommaLoc > 0)
              {
                arg1 = command.substring(commaLoc, nextCommaLoc);

                commaLoc = 1 + command.indexOf(",", nextCommaLoc);

                if (commaLoc > 0)
                {
                  nextCommaLoc = command.indexOf(",", commaLoc);

                  if (nextCommaLoc > 0)
                  {
                    arg2 = command.substring(commaLoc, nextCommaLoc);
                  }
                  else
                  {
                    arg2 = command.substring(commaLoc);
                  }
                }
              }
              else
              {
                arg1 = command.substring(commaLoc);
              }
            }

            if (g_debug_prints_enabled)
            {
              if (arg1.length() > 0)
              {
                Serial.println(arg1);
              }

              if (arg2.length() > 0)
              {
                Serial.println(arg2);
              }
            }

            argumentsRcvd = ((arg1.length() > 0) && (arg2.length() > 0));
          }
        }

        if (g_debug_prints_enabled)
        {
          Serial.println(command + " " + String(value));
        }
        done = TRUE;
      }
      else
      {
        if (g_debug_prints_enabled)
        {
          Serial.println("NULL command received...");
        }

        value = 'H';
        done = TRUE;
      }

      commandInProgress = FALSE;
    }
    else if (Serial.available() > 0) /* search for escape sequency $$$ */
    {
      value = Serial.read();

      if (value == '$')
      {
        escapeCount++;
        if (g_debug_prints_enabled)
        {
          Serial.println("$..." + String(escapeCount));
        }

        if (escapeCount == 3)
        {
          commandInProgress = TRUE;
          escapeCount = 0;
          g_blinkPeriodMillis = 250;
        }
      }
      else
      {
        escapeCount = 0;
      }
    }

    if (g_LEDs_enabled)
    {
      g_relativeTimeSeconds = millis() / g_blinkPeriodMillis;
      if (holdTime != g_relativeTimeSeconds)
      {
        holdTime = g_relativeTimeSeconds;
        toggle = !toggle;
        digitalWrite(RED_LED, toggle);  /* Blink red LED */
      }
    }
    else
    {
      digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
    }
  }

  switch (value)
  {
    /* Time: Sync to NIST */
    case 'T':
    case 't':
      {
        if (g_debug_prints_enabled)
        {
          Serial.println("Processing T command...");
        }
        if (g_LEDs_enabled)
        {
          digitalWrite(RED_LED, LOW); /* Turn on red LED */
        }

        if (setupWiFiAPConnection())
        {
          if (g_debug_prints_enabled)
          {
            Serial.println("Error connecting to router!");
          }
        }
        else
        {
          getNistTime();
        }

        if (g_timeWasSet)
        {
          digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
        }

        WiFi.disconnect();
        digitalWrite(BLUE_LED, HIGH);       /* Turn off blue LED */
        done = FALSE;
      }
      break;

    /* Bridge TCP to UART */
    case 'b':
    case 'B':
      {
        if (g_debug_prints_enabled)
        {
          Serial.println("Processing B command...");
        }

        digitalWrite(RED_LED, HIGH);    /* Turn off red LED */

        if (setupTCP2UART())
        {
          tcp2UARTbridgeLoop();
        }

        done = FALSE;
      }
      break;

    /* HTTP Server */
    case 'w':
    case 'W':
      {
        if (g_debug_prints_enabled)
        {
          Serial.println("Processing W command...");
        }

        digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
        if (setupHTTP_AP())
        {
          httpWebServerLoop();
        }

        digitalWrite(BLUE_LED, HIGH);    /* Turn off blue LED */
        done = FALSE;
      }
      break;

    /* Memory: Set value of variables */
    case 'M':
    case 'm':
      {
        if (g_debug_prints_enabled)
        {
          Serial.println("Processing M command...");
        }

        if (argumentsRcvd)
        {
          /* Allow text to substitute for numeric values */
          if (arg1.equalsIgnoreCase("HOTSPOT_SSID1"))
          {
            arg1 = String(HOTSPOT_SSID1);
          }
          else if (arg1.equalsIgnoreCase("HOTSPOT_PW1"))
          {
            arg1 = String(HOTSPOT_PW1);
          }
          else if (arg1.equalsIgnoreCase("HOTSPOT_SSID2"))
          {
            arg1 = String(HOTSPOT_SSID2);
          }
          else if (arg1.equalsIgnoreCase("HOTSPOT_PW2"))
          {
            arg1 = String(HOTSPOT_PW2);
          }
          else if (arg1.equalsIgnoreCase("HOTSPOT_SSID3"))
          {
            arg1 = String(HOTSPOT_SSID3);
          }
          else if (arg1.equalsIgnoreCase("HOTSPOT_PW3"))
          {
            arg1 = String(HOTSPOT_PW3);
          }
          else if (arg1.equalsIgnoreCase("MDNS_RESPONDER"))
          {
            arg1 = String(MDNS_RESPONDER);
          }
          else if (arg1.equalsIgnoreCase("SOFT_AP_IP_ADDR"))
          {
            arg1 = String(SOFT_AP_IP_ADDR);
          }
          else if (arg1.equalsIgnoreCase("TIME_HOST"))
          {
            arg1 = String(TIME_HOST);
          }
          else if (arg1.equalsIgnoreCase("TIME_HTTP_PORT"))
          {
            arg1 = String(TIME_HTTP_PORT);
          }
          else if (arg1.equalsIgnoreCase("BRIDGE_IP_ADDR"))
          {
            arg1 = String(BRIDGE_IP_ADDR);
          }
          else if (arg1.equalsIgnoreCase("BRIDGE_SSID"))
          {
            arg1 = String(BRIDGE_SSID);
          }
          else if (arg1.equalsIgnoreCase("BRIDGE_PW"))
          {
            arg1 = String(BRIDGE_PW);
          }
          else if (arg1.equalsIgnoreCase("BRIDGE_TCP_PORT"))
          {
            arg1 = String(BRIDGE_TCP_PORT);
          }
          else if (arg1.equalsIgnoreCase("LEDS_ENABLE"))
          {
            arg1 = String(LEDS_ENABLE);
          }
          else if (arg1.equalsIgnoreCase("DEBUG_PRINTS_ENABLE"))
          {
            arg1 = String(DEBUG_PRINTS_ENABLE);
          }

          if (arg2.length()) // Handle empty strings
          {
            if ((arg2.charAt(0) == '"') && (arg2.charAt(1) == '"'))
            {
              arg2 = "";
            }
          }

          switch (arg1.toInt())
          {
            case HOTSPOT_SSID1:
              {
                g_hotspotSSID1 = arg2;
              }
              break;

            case HOTSPOT_PW1:
              {
                g_hotspotPW1 = arg2;
              }
              break;

            case HOTSPOT_SSID2:
              {
                g_hotspotSSID2 = arg2;
              }
              break;

            case HOTSPOT_PW2:
              {
                g_hotspotPW2 = arg2;
              }
              break;

            case HOTSPOT_SSID3:
              {
                g_hotspotSSID3 = arg2;
              }
              break;

            case HOTSPOT_PW3:
              {
                g_hotspotPW3 = arg2;
              }
              break;

            case MDNS_RESPONDER:
              {
                g_mDNS_responder = arg2;
              }
              break;

            case SOFT_AP_IP_ADDR:
              {
                g_softAP_IP_addr = arg2;
              }
              break;

            case TIME_HOST:
              {
                g_timeHost = arg2;
              }
              break;

            case TIME_HTTP_PORT:
              {
                g_timeHTTPport = arg2;
              }
              break;

            case BRIDGE_IP_ADDR:
              {
                g_bridgeIPaddr = arg2;
              }
              break;

            case BRIDGE_SSID:
              {
                g_bridgeSSID = arg2;
              }
              break;

            case BRIDGE_PW:
              {
                g_bridgePW = arg2;
              }
              break;

            case BRIDGE_TCP_PORT:
              {
                g_bridgeTCPport = arg2;
              }
              break;

            case LEDS_ENABLE:
              {
                g_LEDs_enabled = (BOOL)arg2.toInt();

                if (g_LEDs_enabled != 0)
                {
                  g_LEDs_enabled = 1; /* avoid strange behavior for non 0/1 values */
                }
              }
              break;

            case DEBUG_PRINTS_ENABLE:
              {
                g_debug_prints_enabled = (BOOL)arg2.toInt();

                if (g_debug_prints_enabled != 0)
                {
                  g_debug_prints_enabled = 1;  /* avoid strange behavior for non 0/1 values */
                }
              }

            default:
              {
                showSettings();
              }
              break;
          }

          saveDefaultsFile();
        }

        if (g_debug_prints_enabled)
        {
          Serial.println("Finished M command.");
        }

        argumentsRcvd = FALSE;
        done = FALSE;
      }
      break;

    case 'h':
    case 'H':
    default:
      {
        if (g_debug_prints_enabled)
        {
          showSettings();
        }

        done = FALSE;
      }
      break;
  }
}

#ifdef USE_UDP_FOR_TIME_RETRIEVAL

void getNistTime(void)
{
  BOOL success = FALSE;
  int bytesRead = 0;
  int tries = 10;
  int32_t timeVal = 0;
  byte packetBuffer[NTP_PACKET_SIZE]; /*buffer to hold incoming and outgoing packets */

  success = g_UDP.begin(g_localPort);

  if (success)
  {
    if (g_debug_prints_enabled)
    {
      Serial.println("Successfully started WiFi UDP socket...");
    }
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      Serial.println("Failed to start WiFi UDP socket. Aborting...");
    }
    return;
  }

  success = FALSE;
  while (!success && tries--)
  {
    if (g_debug_prints_enabled)
    {
      Serial.println("Sending NTP packet to " + g_timeHost + "...");
    }
    success = sendNTPpacket(g_timeHost, packetBuffer);  /* send an NTP packet to a time server */

    if (success)
    {
      int timeout = 0;

      /* wait to see if a reply is available */
      delay(1000);

      while ((bytesRead < 48) && (timeout < 40))
      {
        bytesRead = g_UDP.parsePacket();
        delay(250);
        timeout++;
        if (g_debug_prints_enabled)
        {
          Serial.print(".");
        }
      }

      success = (bytesRead >= 48);

      if (!success)
      {
        if (g_debug_prints_enabled)
        {
          Serial.println("Timeout waiting for reply.");
        }
      }
    }
    else
    {
      if (g_debug_prints_enabled)
      {
        Serial.println("Failed to send NTP packet.");
      }
    }

    if (g_debug_prints_enabled)
    {
      if (!success)
      {
        if (tries)
        {
          Serial.println("Retrying...");
        }
        else
        {
          Serial.println("Failed!");
        }
      }
    }
  }

  if (success)
  {
    String utcTimeSinceMidnight = "";
    String tempStr;

    if (g_debug_prints_enabled)
    {
      Serial.println(String("Time packet received (") + String(bytesRead) + String(") bytes..."));
    }

    /* We've received a packet, read the data from it */
    bytesRead = g_UDP.read(packetBuffer, NTP_PACKET_SIZE);  /* read the packet into the buffer */

    if (g_debug_prints_enabled)
    {
      Serial.println(String("Bytes retrieved: ") + String(bytesRead));

      /*the timestamp starts at byte 40 of the received packet and is four bytes,
         or two words, long. First, esxtract the two words: */
      Serial.print("Here's what was received: ");

      int i;
      for (i = 0; i < NTP_PACKET_SIZE; i++)
      {
        Serial.print(String(packetBuffer[i]) + " ");
      }

      Serial.println();
    }

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    /* combine the four bytes (two words) into a long integer NTP time (seconds since Jan 1 1900): */
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    if (g_debug_prints_enabled)
    {
      Serial.println("Seconds since 1 Jan 1900 = " + String(secsSince1900));
    }

    /* Convert NTP time into common time formats: */
    const unsigned long seventyYears = 2208988800UL;
    unsigned long epoch = secsSince1900 - seventyYears; /* subtract seventy years: */

    if (g_debug_prints_enabled)
    {
      Serial.println("Unix time: " + String(epoch));  /* print Unix time: */

    }

    /* Calculate UTC in hh:mm:ss */
    utcTimeSinceMidnight += String((epoch % 86400L) / 3600) + ":";

    if (((epoch % 3600) / 60) < 10)
    {
      /* In the first 10 minutes of each hour, we'll want a leading '0' */
      utcTimeSinceMidnight += "0";
    }

    utcTimeSinceMidnight += String((epoch % 3600) / 60) + ":";

    if ((epoch % 60) < 10)
    {
      /* In the first 10 seconds of each minute, we'll want a leading '0' */
      utcTimeSinceMidnight += "0";
    }

    utcTimeSinceMidnight += String(epoch % 60);

    if (g_debug_prints_enabled)
    {
      Serial.println("UTC: " + utcTimeSinceMidnight);
    }

    timeVal = stringToTimeVal(utcTimeSinceMidnight);

    tempStr = String("$TIM," + String(timeVal) + ";");
    Serial.println(tempStr);        /* Send command to set time */

    g_timeWasSet = TRUE;
    digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      Serial.println("Failed to receive response. Bytes read = " + String(bytesRead));
    }
  }
}

/* Send an NTP request to the time server at the given address
   The NIST servers listen for a NTP request on port 123,and respond by sending a udp/ip
   data packet in the NTP format. The data packet includes a 64-bit timestamp containing
   the time in UTC seconds since January 1, 1900 with a resolution of 200 ps. */
BOOL sendNTPpacket(String address, byte *packetBuffer)
{
  BOOL success = FALSE;

  /* set all bytes in the buffer to 0 */
  memset(packetBuffer, 0, NTP_PACKET_SIZE);

  /* Initialize values needed to form NTP request
     (see URL above for details on the packets)
    Serial.println("2"); */
  packetBuffer[0] = 0b11100011;   /* LI, Version, Mode */
  packetBuffer[1] = 0;            /* Stratum, or type of clock */
  packetBuffer[2] = 6;            /* Polling Interval */
  packetBuffer[3] = 0xEC;         /* Peer Clock Precision */
  /* 8 bytes of zero for Root Delay & Root Dispersion */
  packetBuffer[12]     = 49;
  packetBuffer[13]     = 0x4E;
  packetBuffer[14]     = 49;
  packetBuffer[15]     = 52;

  if (g_debug_prints_enabled)
  {
    Serial.println(address.c_str());
  }

  /* Prevent accessing NIST servers more often the once every 10 seconds */
  g_relativeTimeSeconds = millis() / 1000;
  while ((g_relativeTimeSeconds - g_lastAccessToNISTServers) < 10) /* prevent servers being accessed more than once every 10 seconds */
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("Waiting..." + String(10 - (g_relativeTimeSeconds - g_lastAccessToNISTServers))));
    }
    delay(1000);
    g_relativeTimeSeconds = millis() / 1000;
  }

  g_lastAccessToNISTServers = millis() / 1000;

  /* all NTP fields have been given values, now
     you can send a packet requesting a timestamp: */

  //could mDNS be messing with the address?
  IPAddress timeServer(129, 6, 15, 28);
  //if (g_UDP.beginPacket(address.c_str(), 123)) /*NTP requests are to port 123 */
  if (g_UDP.beginPacket(timeServer, 123)) /*NTP requests are to port 123 */
  {
    int bytesWritten;

    bytesWritten = g_UDP.write(packetBuffer, NTP_PACKET_SIZE);
    success = g_UDP.endPacket();

    if (g_debug_prints_enabled)
    {
      Serial.println("g_UDP.beginPacket: UDP remote connection successful!");
    }

    if (g_debug_prints_enabled)
    {
      Serial.println(String("Bytes written: ") + String(bytesWritten));
    }

    if (success)
    {
      if (g_debug_prints_enabled)
      {
        Serial.println("Packet was sent successfully!");
      }
    }
    else
    {
      if (g_debug_prints_enabled)
      {
        Serial.println("Packet failed to send!");
      }
    }
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      Serial.println("g_UDP.beginPacket: UDP remote connection failed!");
    }
  }

  return (success);
}

#else   /* USE_UDP_FOR_TIME_RETRIEVAL */

void getNistTime(void)
{
  String tempStr;

  g_relativeTimeSeconds = millis() / 1000;
  while ((g_relativeTimeSeconds - g_lastAccessToNISTServers) < 10) /* prevent servers being accessed more than once every 10 seconds */
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("Waiting..." + String(10 - (g_relativeTimeSeconds - g_lastAccessToNISTServers))));
    }

    delay(1000);
    g_relativeTimeSeconds = millis() / 1000;
  }

  tempStr = String("Connecting to " + g_timeHost + "...");

  if (g_debug_prints_enabled)
  {
    Serial.println(tempStr);
  }

  /* Use WiFiClient class to create TCP connections */
  WiFiClient client;
  while (!client.connect((const char*)g_timeHost.c_str(), g_timeHTTPport.toInt()))
  {
    if (g_debug_prints_enabled)
    {
      Serial.println("Initial connection failed! Retrying...");
    }

    client.stop();
    delay(5000);
  }

  do
  {
    delay(500); /* This delay seems to be required to ensure reliable return of the time string */

    if (g_debug_prints_enabled)
    {
      Serial.println("Requesting time...");
    }

    /* Read all the lines of the reply from server and print them to Serial
       Example: 57912 17-06-08 18:28:35 50 0 0 206.3 UTC(NIST)
                JJJJJ YR-MO-DA HH:MM:SS TT L H msADV UTC(NIST) OTM
          where:
             JJJJJ is the Modified Julian Date (MJD). The MJD has a starting point of midnight on November 17, 1858.
             You can obtain the MJD by subtracting exactly 2 400 000.5 days from the Julian Date, which is an integer
             day number obtained by counting days from the starting point of noon on 1 January 4713 B.C. (Julian Day zero).

             YR-MO-DA is the date. It shows the last two digits of the year, the month, and the current day of month.

             HH:MM:SS is the time in hours, minutes, and seconds. The time is always sent as Coordinated Universal Time (UTC).
             An offset needs to be applied to UTC to obtain local time. For example, Mountain Time in the U. S. is 7 hours behind
             UTC during Standard Time, and 6 hours behind UTC during Daylight Saving Time.

             TT is a two digit code (00 to 99) that indicates whether the United States is on Standard Time (ST) or Daylight
             Saving Time (DST). It also indicates when ST or DST is approaching. This code is set to 00 when ST is in effect,
             or to 50 when DST is in effect. During the month in which the time change actually occurs, this number will decrement
             every day until the change occurs. For example, during the month of November, the U.S. changes from DST to ST.
             On November 1, the number will change from 50 to the actual number of days until the time change. It will decrement
             by 1 every day until the change occurs at 2 a.m. local time when the value is 1. Likewise, the spring change is at
             2 a.m. local time when the value reaches 51.

             L is a one-digit code that indicates whether a leap second will be added or subtracted at midnight on the last day of the
             current month. If the code is 0, no leap second will occur this month. If the code is 1, a positive leap second will be added
             at the end of the month. This means that the last minute of the month will contain 61 seconds instead of 60. If the code is
             2, a second will be deleted on the last day of the month. Leap seconds occur at a rate of about one per year. They are used
             to correct for irregularity in the earth's rotation. The correction is made just before midnight UTC (not local time).

             H is a health digit that indicates the health of the server. If H = 0, the server is healthy. If H = 1, then the server is
             operating properly but its time may be in error by up to 5 seconds. This state should change to fully healthy within 10 minutes.
             If H = 2, then the server is operating properly but its time is known to be wrong by more than 5 seconds. If H = 3, then a hardware
             or software failure has occurred and the amount of the time error is unknown. If H = 4 the system is operating in a special maintenance
             mode and both its accuracy and its response time may be degraded. This value is not used for production servers except in special
             circumstances. The transmitted time will still be correct to within  Ãƒâ€šÃ‚Â±1 second in this mode.

             msADV displays the number of milliseconds that NIST advances the time code to partially compensate for network delays. The advance
             is currently set to 50.0 milliseconds.

             The label UTC(NIST) is contained in every time code. It indicates that you are receiving Coordinated Universal Time (UTC) from the
             National Institute of Standards and Technology (NIST).

             OTM (on-time marker) is an asterisk (*). The time values sent by the time code refer to the arrival time of the OTM. In other words,
             if the time code says it is 12:45:45, this means it is 12:45:45 when the OTM arrives. */

    done = 0;
    int count = 0;

    while (!done)
    {
      String tempStr;
      String line = "";

      while (client.available())
      {
        line = client.readStringUntil('\r');
      }

      if (line == "") /* no response from time server */
      {
        count++;
        if (g_debug_prints_enabled)
        {
          Serial.println("Request Timedout! Retrying...");
        }

        if (count > 2)  /* If sending CR didn't help, try reconnecting */
        {
          if (g_debug_prints_enabled)
          {
            Serial.println("Reconnecting...");
          }

          client.stop();
          delay(2000);

          while (!client.connect((const char*)g_timeHost.c_str(), g_timeHTTPport.toInt())) /* reconnect to the same host */
          {
            if (g_debug_prints_enabled)
            {
              Serial.println("Connection failed! Retrying...");
            }

            client.stop();
            delay(5000);
          }

          count = 0;
        }
        else    /* Not sure if this helps, but try sending a CR to server */
        {
          if (g_debug_prints_enabled)
          {
            Serial.println("Sending CR...");
          }

          client.write('\r');
          delay(1000);
        }
      }
      else
      {
        if (g_debug_prints_enabled)
        {
          Serial.print("Received:");
        }

        if (g_debug_prints_enabled)
        {
          Serial.print(line);
        }

        timeVal = stringToTimeVal(line);
        tempStr = String("$TIM," + String(timeVal) + ";");

        if (g_debug_prints_enabled)
        {
          Serial.println(tempStr);
        }

        done = 1;
        g_timeWasSet = TRUE;
        digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
      }
    }

    if (g_debug_prints_enabled)
    {
      Serial.println("Closing connection");
    }

    if (g_debug_prints_enabled)
    {
      Serial.println();
    }

    client.stop();
    g_lastAccessToNISTServers = millis() / 1000;
  }
  while (0);
}

#endif  /* USE_UDP_FOR_TIME_RETRIEVAL */


int32_t stringToTimeVal(String string)
{
  int32_t time_sec = 0;
  BOOL missingTens = FALSE;
  uint8_t index = 0;
  char field[3];
  char *instr, *str;
  char c_str[10];

  strcpy(c_str, string.c_str());
  str = c_str;

  field[2] = '\0';
  field[1] = '\0';

  instr = strchr(str, ':');

  if (instr == NULL)
  {
    return ( time_sec);
  }

  if (str > (instr - 2))  /* handle case of time format #:##:## */
  {
    missingTens = TRUE;
    str = instr - 1;
  }
  else
  {
    str = instr - 2;
  }

  /* hh:mm:ss or h:mm:ss */
  field[0] = str[index++];        /* tens of hours or hours */
  if (!missingTens)
  {
    field[1] = str[index++];    /* hours */
  }

  time_sec = SecondsFromHours(atol(field));
  index++;

  field[0] = str[index++];
  field[1] = str[index++];    /* minutes */
  time_sec += SecondsFromMinutes(atol(field));
  index++;

  field[0] = str[index++];
  field[1] = str[index++];    /* seconds */
  time_sec += atoi(field);

  return (time_sec);
}

void tcp2UARTbridgeLoop()
{
  uint8_t i, j;
  char buf[1024];
  int bytesAvail, bytesIn;
  BOOL done = FALSE;
  BOOL toggle = FALSE;
  unsigned long holdTime;
  BOOL clientConnected = FALSE;
  int escapeCount = 0;

  if (g_debug_prints_enabled)
  {
    Serial.println("Bridge started");
  }

  while (!done)
  {
    /*check if there are any new clients */
    if (g_tcpServer.hasClient())
    {
      for (i = 0; i < MAX_SRV_CLIENTS; i++)
      {
        /*find free/disconnected spot */
        if (!g_tcpServerClients[i] || !g_tcpServerClients[i].connected())
        {
          if (g_tcpServerClients[i])
          {
            g_tcpServerClients[i].stop();
          }

          g_tcpServerClients[i] = g_tcpServer.available();

          if (g_tcpServerClients[i].connected())
          {
            if (g_debug_prints_enabled)
            {
              Serial.println("New client: #" + String(i + 1));
            }
          }

          continue;
        }
      }

      /* no free/disconnected spot so reject */
      WiFiClient g_tcpServerClient = g_tcpServer.available();
      g_tcpServerClient.stop();
    }

    /*check clients for data */
    clientConnected = FALSE;
    for (i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      if (g_tcpServerClients[i] && g_tcpServerClients[i].connected())
      {
        /*get data from the telnet client and push it to the UART */
        while ((bytesAvail = g_tcpServerClients[i].available()) > 0)
        {
          bytesIn = g_tcpServerClients[i].readBytes(buf, min(sizeof(buf), bytesAvail));
          if (bytesIn > 0)
          {
            Serial.write(buf, bytesIn);
            delay(0);
          }
        }

        clientConnected = TRUE;
      }
    }

    /*check UART for data */
    while ((bytesAvail = Serial.available()) > 0)
    {
      bytesIn = Serial.readBytes(buf, min(sizeof(buf), bytesAvail));

      if (bytesIn > 0)
      {
        /*push UART data to all connected telnet clients */
        for (i = 0; i < MAX_SRV_CLIENTS; i++)
        {
          if (g_tcpServerClients[i] && g_tcpServerClients[i].connected())
          {
            g_tcpServerClients[i].write((uint8_t*)buf, bytesIn);
            delay(0);
          }

          for (j = 0; j < bytesIn; j++)
          {
            if (buf[j] == '$')
            {
              escapeCount++;

              if (escapeCount == 3)
              {
                done = TRUE;
                escapeCount = 0;
              }
            }
            else
            {
              escapeCount = 0;
            }
          }
        }
      }
    }

    if (clientConnected && g_LEDs_enabled)
    {
      g_relativeTimeSeconds = millis() / g_blinkPeriodMillis;
      if (holdTime != g_relativeTimeSeconds)
      {
        holdTime = g_relativeTimeSeconds;
        toggle = !toggle;
        digitalWrite(BLUE_LED, toggle); /* Blink blue LED */
      }
    }
    else
    {
      digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */
    }
  }
}

void httpWebServerLoop()
{
  uint8_t i, j;
  char buf[1024];
  int bytesAvail, bytesIn;
  BOOL done = FALSE;
  BOOL toggle = FALSE;
  unsigned long holdTime;
  BOOL clientConnected = FALSE;
  int escapeCount = 0;
  int numConnected = 0;
  int hold;

  if (g_debug_prints_enabled)
  {
    Serial.println("Web Server started");
  }

  while (!done)
  {
    /*check if there are any new clients */
    g_http_server.handleClient();
//    webSocket.loop();

    hold = WiFi.softAPgetStationNum();
    if (numConnected != hold)
    {
      numConnected = hold;

      if (g_debug_prints_enabled)
      {
        Serial.println(String("Clients: ") + numConnected);
      }
    }

    /*check UART for data */
    while ((bytesAvail = Serial.available()) > 0)
    {
      bytesIn = Serial.readBytes(buf, min(sizeof(buf), bytesAvail));

      if (bytesIn > 0)
      {
        /*push UART data to all connected telnet clients */
        {
          for (j = 0; j < bytesIn; j++)
          {
            if (buf[j] == '$')
            {
              escapeCount++;

              if (escapeCount == 3)
              {
                done = TRUE;
                escapeCount = 0;
                if (g_debug_prints_enabled)
                {
                  Serial.println("Web Server closed");
                }

                WiFi.softAPdisconnect(true);
              }
            }
            else
            {
              escapeCount = 0;
            }
          }
        }
      }
    }

    if (g_LEDs_enabled)
    {
      g_relativeTimeSeconds = millis() / g_blinkPeriodMillis;
      if (holdTime != g_relativeTimeSeconds)
      {
        holdTime = g_relativeTimeSeconds;
        toggle = !toggle;
        digitalWrite(BLUE_LED, toggle); /* Blink blue LED */
      }
    }
    else
    {
      digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */
    }
  }
}


void startSPIFFS()
{ // Start the SPIFFS and list all contents
  SPIFFS.begin();   // Start the SPI Flash File System (SPIFFS)
  Serial.println("SPIFFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    { // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }

    Serial.printf("\n");
  }
}

void startWebSocket()
{ // Start a WebSocket server
  webSocket.begin();      // start the websocket server
  webSocket.onEvent(webSocketEvent);  // if there's an incomming websocket message, go to function 'webSocketEvent'
  Serial.println("WebSocket server started.");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght)
{ // When a WebSocket message is received
  switch (type)
  {
    case WStype_DISCONNECTED:   // if the websocket is disconnected
      Serial.printf("[%u] Disconnected!\n", num);
      break;

    case WStype_CONNECTED:
      { // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        //        rainbow = false;  // Turn rainbow off when a new connection is established
      }
      break;

    case WStype_TEXT: // if new text data is received
      Serial.printf("[%u] get Text: %s\n", num, payload);
      if (payload[0] == '#')
      { // we get RGB data
        uint32_t rgb = (uint32_t) strtol((const char *) &payload[1], NULL, 16);   // decode rgb data
        int r = ((rgb >> 20) & 0x3FF);    // 10 bits per color, so R: bits 20-29
        int g = ((rgb >> 10) & 0x3FF);    // G: bits 10-19
        int b = rgb & 0x3FF;              // B: bits Ã‚Â 0-9

        //        analogWrite(LED_RED, r);  // write it to the LED output pins
        //        analogWrite(LED_GREEN, g);
        //        analogWrite(LED_BLUE, b);
      }
      else if (payload[0] == 'R')
      { // the browser sends an R when the rainbow effect is enabled
        //       rainbow = true;
      }
      else if (payload[0] == 'N')
      { // the browser sends an N when the rainbow effect is disabled
        //       rainbow = false;
      }
      break;
  }
}

bool handleFileRead(String path)
{ // send the right file to the client (if it exists)
  Serial.println("\nhandleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";   // If a folder is requested, send the index file
  String contentType = getContentType(path);      // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(stringObjToConstCharString(&pathWithGz)) || SPIFFS.exists(stringObjToConstCharString(&path)))
  { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))      // If there's a compressed version available
      path += ".gz";                    // Use the compressed verion
    File file = SPIFFS.open(path, "r"); // Open the file
    size_t sent = g_http_server.streamFile(file, contentType);  // Send it to the client
    file.close();                       // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  else
  {
    Serial.println(String("File not found in SPIFFS: ") + path);
  }

  Serial.println(String("\tFile Not Found: ") + path);  // If the file doesn't exist, return false
  return false;
}

bool readDefaultsFile()
{ // send the right file to the client (if it exists)
  String path = "/defaults.txt";

  if (g_debug_prints_enabled)
  {
    Serial.println("Reading defaults...");
  }

  String contentType = getContentType(path);      // Get the MIME type
  if (SPIFFS.exists(path))
  { // If the file exists, either as a compressed archive, or normal
    // open file for reading
    File file = SPIFFS.open(path, "r"); // Open the file
    String s = file.readStringUntil('\n');
    int count = 0;

    while (s.length() && count++ < NUMBER_OF_SETTABLE_VARIABLES)
    {
      if (s.indexOf(',') < 0)
      {
        if (g_debug_prints_enabled)
        {
          Serial.println("Error: illegal entry found in defaults file at line " + count);
        }

        break; // invalid line found
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

      if (settingID.equalsIgnoreCase("HOTSPOT_SSID1_DEFAULT"))
      {
        g_hotspotSSID1 = value;
      }
      else if (settingID.equalsIgnoreCase("HOTSPOT_PW1_DEFAULT"))
      {
        g_hotspotPW1 = value;
      }
      else if (settingID.equalsIgnoreCase("HOTSPOT_SSID2_DEFAULT"))
      {
        g_hotspotSSID2 = value;
      }
      else if (settingID.equalsIgnoreCase("HOTSPOT_PW2_DEFAULT"))
      {
        g_hotspotPW2 = value;
      }
      else if (settingID.equalsIgnoreCase("HOTSPOT_SSID3_DEFAULT"))
      {
        g_hotspotSSID3 = value;
      }
      else if (settingID.equalsIgnoreCase("HOTSPOT_PW3_DEFAULT"))
      {
        g_hotspotPW3 = value;
      }
      else if (settingID.equalsIgnoreCase("MDNS_RESPONDER_DEFAULT"))
      {
        g_mDNS_responder = value;
      }
      else if (settingID.equalsIgnoreCase("SOFT_AP_IP_ADDR_DEFAULT"))
      {
        g_softAP_IP_addr = value;
      }
      else if (settingID.equalsIgnoreCase("TIME_HOST_DEFAULT"))
      {
        g_timeHost = value;
      }
      else if (settingID.equalsIgnoreCase("TIME_HTTP_PORT_DEFAULT"))
      {
        g_timeHTTPport = value;
      }
      else if (settingID.equalsIgnoreCase("BRIDGE_IP_ADDR_DEFAULT"))
      {
        g_bridgeIPaddr = value;
      }
      else if (settingID.equalsIgnoreCase("BRIDGE_SSID_DEFAULT"))
      {
        g_bridgeSSID = value;
      }
      else if (settingID.equalsIgnoreCase("BRIDGE_PW_DEFAULT"))
      {
        g_bridgePW = value;  /* minimum 8 characters */
      }
      else if (settingID.equalsIgnoreCase("BRIDGE_TCP_PORT_DEFAULT"))
      {
        g_bridgeTCPport = value;
      }
      else if (settingID.equalsIgnoreCase("LEDS_ENABLE_DEFAULT"))
      {
        if (value.charAt(0) == 'T' || value.charAt(0) == 't' || value.charAt(0) == '1')
        {
          g_LEDs_enabled = 1;
        }
        else
        {
          g_LEDs_enabled = 0;
        }
      }
      else if (settingID.equalsIgnoreCase("DEBUG_PRINTS_ENABLE_DEFAULT"))
      {
        if (value.charAt(0) == 'T' || value.charAt(0) == 't' || value.charAt(0) == '1')
        {
          g_debug_prints_enabled = 1;
        }
        else
        {
          g_debug_prints_enabled = 0;
        }
      }
      else
      {
        if (g_debug_prints_enabled)
        {
          Serial.println("Error in file: SettingID = " + settingID + " Value = [" + value + "]");
        }
      }

      if (g_debug_prints_enabled)
      {
        Serial.println("[" + s + "]");
      }

      s = file.readStringUntil('\n');
    }

    file.close(); // Close the file
    Serial.println(String("\tRead file: ") + path);
    return true;
  }

  Serial.println(String("\tFile Not Found: ") + path);  // If the file doesn't exist, return false
  return false;
}

void saveDefaultsFile()
{
  String path = "/defaults.txt";
  Serial.println("Writing defaults...");
  String contentType = getContentType(path);      // Get the MIME type
  //  if(SPIFFS.exists(path))
  { // If the file exists, either as a compressed archive, or normal
    // open file for reading
    File file = SPIFFS.open(path, "w"); // Open the file for writing
    String s;

    for (int i = 0; i < NUMBER_OF_SETTABLE_VARIABLES; i++)
    {
      switch (i)
      {
        case HOTSPOT_SSID1:
          {
            file.println(String("HOTSPOT_SSID1_DEFAULT," + g_hotspotSSID1));
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote HOTSPOT_SSID1_DEFAULT");
            }
          }
          break;

        case HOTSPOT_PW1:
          {
            file.println("HOTSPOT_PW1_DEFAULT," + g_hotspotPW1);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote HOTSPOT_PW1_DEFAULT");
            }
          }
          break;

        case HOTSPOT_SSID2:
          {
            file.println(String("HOTSPOT_SSID2_DEFAULT," + g_hotspotSSID2));
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote HOTSPOT_SSID2_DEFAULT");
            }
          }
          break;

        case HOTSPOT_PW2:
          {
            file.println("HOTSPOT_PW2_DEFAULT," + g_hotspotPW2);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote HOTSPOT_PW2_DEFAULT");
            }
          }
          break;

        case HOTSPOT_SSID3:
          {
            file.println(String("HOTSPOT_SSID3_DEFAULT," + g_hotspotSSID3));
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote HOTSPOT_SSID3_DEFAULT");
            }
          }
          break;

        case HOTSPOT_PW3:
          {
            file.println("HOTSPOT_PW3_DEFAULT," + g_hotspotPW3);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote HOTSPOT_PW3_DEFAULT");
            }
          }
          break;

        case MDNS_RESPONDER:
          {
            file.println("MDNS_RESPONDER_DEFAULT," + g_mDNS_responder);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote MDNS_RESPONDER_DEFAULT");
            }
          }
          break;

        case SOFT_AP_IP_ADDR:
          {
            file.println("SOFT_AP_IP_ADDR_DEFAULT," + g_softAP_IP_addr);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote SOFT_AP_IP_ADDR_DEFAULT");
            }
          }
          break;

        case TIME_HOST:
          {
            file.println("TIME_HOST_DEFAULT," + g_timeHost);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote TIME_HOST_DEFAULT");
            }
          }
          break;

        case TIME_HTTP_PORT:
          {
            file.println("TIME_HTTP_PORT_DEFAULT," + g_timeHTTPport);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote TIME_HTTP_PORT_DEFAULT");
            }
          }
          break;

        case BRIDGE_SSID:
          {
            file.println("BRIDGE_SSID_DEFAULT," + g_bridgeSSID);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote BRIDGE_SSID_DEFAULT");
            }
          }
          break;

        case BRIDGE_IP_ADDR:
          {
            file.println("BRIDGE_IP_ADDR_DEFAULT," + g_bridgeIPaddr);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote BRIDGE_IP_ADDR_DEFAULT");
            }
          }
          break;

        case BRIDGE_PW:
          {
            file.println("BRIDGE_PW_DEFAULT," + g_bridgePW);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote BRIDGE_PW_DEFAULT");
            }
          }
          break;

        case BRIDGE_TCP_PORT:
          {
            file.println("BRIDGE_TCP_PORT_DEFAULT," + g_bridgeTCPport);
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote BRIDGE_TCP_PORT_DEFAULT");
            }
          }
          break;

        case LEDS_ENABLE:
          {
            if (g_LEDs_enabled)
            {
              file.println("LEDS_ENABLE_DEFAULT,TRUE");
            }
            else
            {
              file.println("LEDS_ENABLE_DEFAULT,FALSE");
            }
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote LEDS_ENABLE_DEFAULT");
            }
          }
          break;

        case DEBUG_PRINTS_ENABLE:
          {
            if (g_debug_prints_enabled)
            {
              file.println("DEBUG_PRINTS_ENABLE_DEFAULT,TRUE");
            }
            else
            {
              file.println("DEBUG_PRINTS_ENABLE_DEFAULT,FALSE");
            }
            if (g_debug_prints_enabled)
            {
              Serial.println("Wrote DEBUG_PRINTS_ENABLE_DEFAULT");
            }
          }

        default:
          {
          }
          break;
      }
    }

    file.close(); // Close the file
    Serial.println(String("\Wrote file: ") + path);
  }

  Serial.println(String("\tFile Not Found: ") + path);  // If the file doesn't exist, return false
}

void handleFileUpload()
{ // upload a new file to the SPIFFS
  HTTPUpload& upload = g_http_server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START)
  {
    path = upload.filename;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith(".gz"))
    { // The file server always prefers a compressed version of a file
      String pathWithGz = path + ".gz"; // So if an uploaded file is not compressed, the existing compressed
      if (SPIFFS.exists(pathWithGz))  // version of that file must be deleted (if it exists)
        SPIFFS.remove(pathWithGz);
    }
    Serial.print("handleFileUpload Name: "); Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w");  // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (fsUploadFile)
    { // If the file was successfully created
      fsUploadFile.close();   // Close the file again
      Serial.print("handleFileUpload Size: ");
      Serial.println(upload.totalSize);
      g_http_server.sendHeader("Location", "/success.html");  // Redirect the client to the success page
      g_http_server.send(303);
    }
    else
    {
      g_http_server.send(500, "text/plain", "500: couldn't create file");
    }
  }
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



void showSettings()
{
  int i;

  if (!g_debug_prints_enabled)
  {
    return;
  }

  Serial.println("RDP WiFi firmware - v" + String(WIFI_SW_VERSION));
  Serial.println("Baud Rate: " + String(SERIAL_BAUD_RATE));
  Serial.println();
  Serial.println("Valid Commands:");
  Serial.println("$$$m,id,value;         - Set WiFi memory variable <id> to <value>; ex: $$$m,HOTSPOT_SSID1,MyRouterSSID;");
  Serial.println("$$$t;                  - Retrieve NIST time from Time Server");
  Serial.println("$$$b;                  - Start/Cancel UART-to-TCP bridge");
  Serial.println("$$$w;                  - Start/Cancel HTTP server");
  Serial.println("$$$h;                  - This help message");
  Serial.println();
  Serial.println("HUZZAH Settings");
  for (i = 0; i < NUMBER_OF_SETTABLE_VARIABLES; i++)
  {
    switch (i)
    {
      case HOTSPOT_SSID1:
        {
          Serial.println(String("Local Hotspot1 SSID (HOTSPOT_SSID1) = \"") + g_hotspotSSID1 + "\"");
        }
        break;

      case HOTSPOT_PW1:
        {
          Serial.println("Local Hotspot1 Password (HOTSPOT_PW1) = \"" + g_hotspotPW1 + "\"");
        }
        break;

      case HOTSPOT_SSID2:
        {
          Serial.println(String("Local Hotspot2 SSID (HOTSPOT_SSID2) = \"") + g_hotspotSSID2 + "\"");
        }
        break;

      case HOTSPOT_PW2:
        {
          Serial.println("Local Hotspot2 Password (HOTSPOT_PW2) = \"" + g_hotspotPW2 + "\"");
        }
        break;

      case HOTSPOT_SSID3:
        {
          Serial.println(String("Local Hotspot3 SSID (HOTSPOT_SSID3) = \"") + g_hotspotSSID3 + "\"");
        }
        break;

      case HOTSPOT_PW3:
        {
          Serial.println("Local Hotspot3 Password (HOTSPOT_PW3) = \"" + g_hotspotPW3 + "\"");
        }
        break;

      case MDNS_RESPONDER:
        {
          Serial.println("Local mDNS responder name (MDNS_RESPONDER) = \"" + g_mDNS_responder + "\"");
        }
        break;

      case SOFT_AP_IP_ADDR:
        {
          Serial.println("Soft AP IP Address (SOFT_AP_IP_ADDR) = \"" + g_softAP_IP_addr + "\"");
        }
        break;

      case TIME_HOST:
        {
          Serial.println("Time Server URL (TIME_HOST) = " + g_timeHost);
        }
        break;

      case TIME_HTTP_PORT:
        {
          Serial.println("Time Server Port Number (TIME_HTTP_PORT) = " + g_timeHTTPport);
        }
        break;

      case BRIDGE_SSID:
        {
          Serial.println("UART-to-TCP Bridge SSID (BRIDGE_SSID) = \"" + g_bridgeSSID + "\"");
        }
        break;

      case BRIDGE_IP_ADDR:
        {
          Serial.println("UART-to-TCP Bridge IP Address (BRIDGE_IP_ADDR) = \"" + g_bridgeIPaddr + "\"");
        }
        break;

      case BRIDGE_PW:
        {
          Serial.println("UART-to-TCP Bridge Password (BRIDGE_PW) = \"" + g_bridgePW + "\"");
        }
        break;

      case BRIDGE_TCP_PORT:
        {
          Serial.println("UART-to-TCP Bridge TCP Port number (BRIDGE_TCP_PORT) = " + g_bridgeTCPport);
        }
        break;

      case LEDS_ENABLE:
        {
          Serial.println("LEDs Enabled (LEDS_ENABLE) = " + String(g_LEDs_enabled));
        }
        break;

      case DEBUG_PRINTS_ENABLE:
        {
          Serial.println("Debug Prints Enabled (DEBUG_PRINTS_ENABLE) = " + String(g_debug_prints_enabled));
        }

      default:
        {
        }
        break;
    }
  }
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



