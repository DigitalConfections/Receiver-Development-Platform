/**********************************************************************************************
   Copyright © 2018 Digital Confections LLC

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



#include <Arduino.h>
//#include <GDBStub.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFiMulti.h>
//#include <ESP8266mDNS.h>
#include <user_interface.h>
#include "esp8266.h"
//#include <ArduinoOTA.h>
#include <FS.h>
//#include <WebSocketsClient.h>
#include <Hash.h>
#include <WebSocketsServer.h>
#include <ESP8266WiFiType.h>
#include <time.h>
#include "Transmitter.h"
#include "Event.h"


/* #include <Wire.h> */
#include "Helpers.h"


/* Global variables are always prefixed with g_ */

/*
   HUZZAH-specific Defines
*/
#define RED_LED (0)
#define BLUE_LED (2)

bool g_debug_prints_enabled = DEBUG_PRINTS_ENABLE_DEFAULT;
uint8_t g_debug_level_enabled = 0;
bool g_LEDs_enabled = LEDS_ENABLE_DEFAULT;

/*
   TCP to UART Bridge
*/

#define MAX_SRV_CLIENTS 3   /*how many clients should be able to telnet to this ESP8266 */
//WiFiServer g_tcpServer(BRIDGE_TCP_PORT_DEFAULT.toInt());
//WiFiClient g_tcpServerClients[MAX_SRV_CLIENTS];

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
WebSocketsServer g_webSocketServer = WebSocketsServer(81);  // create a websocket server on port 81
String g_AP_NameString;

typedef struct webSocketClient {
  int8_t webID;
  int8_t socketID;
  String macAddr;
} WebSocketClient;

WebSocketClient g_webSocketClient[MAX_NUMBER_OF_WEB_CLIENTS]; // Keep track of active clients and their MAC addresses
uint8_t g_numberOfSocketClients = 0;
uint8_t g_numberOfWebClients = 0;

ESP8266WiFiMulti wifiMulti;     // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
File fsUploadFile;  // a File variable to temporarily store the received file

String g_softAP_IP_addr;

void handleRoot();              // function prototypes for HTTP handlers
void handleNotFound();

/*
   Main Program Support
*/

unsigned long g_relativeTimeSeconds;
unsigned long g_lastAccessToNISTServers = 0;
bool g_timeWasSet = false;
int g_blinkPeriodMillis = 500;

String g_timeOfDayFromTx = "";

static WiFiEventHandler e1, e2;

Transmitter *g_xmtr;
Event* g_activeEvent;
int g_activeEventIndex = 0;
EventFileRef g_eventList[20];
int g_eventsRead = 0;
TxCommState g_ESP_ATMEGA_Comm_State = TX_WAKE_UP;

bool populateEventFileList(void);
bool readDefaultsFile(void);
void saveDefaultsFile(void);
void showSettings();
void handleFileUpload();
void handleLBMessage(String message);

void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);
  //Serial.setDebugOutput(false);
  //delay(10);
  //system_set_os_print(false);
  //delay(10);
  //system_uart_swap();
  //delay(10);
  //  system_set_os_print(0);
  //  Serial.begin(SERIAL_BAUD_RATE);
  //  while (!Serial)
  //  {
  //    ;                           /* Wait for UART to initialize */
  //  }

  pinMode(RED_LED, OUTPUT);       /* Allow the red LED to be controlled */
  pinMode(BLUE_LED, OUTPUT);      /* Initialize the BUILTIN_LED pin as an output */
  digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
  digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */

  startSPIFFS();    // Start the SPIFFS and list all contents
  if (!readDefaultsFile())  // Read default settings from file system
  {
    saveDefaultsFile();
  }

  populateEventFileList();

  showSettings();

#if WIFI_DEBUG_PRINTS_ENABLED
  WiFi.onEvent(eventWiFi);      // Handle WiFi event
#endif

  g_xmtr = new Transmitter(g_debug_prints_enabled);
}

/*******************************************************
*  Handle WiFi events                                  *
********************************************************/
void eventWiFi(WiFiEvent_t event) {
  String e = String(event);

  switch (event) {
    case WIFI_EVENT_STAMODE_CONNECTED:
      Serial.println(String("[WiFi] " + e + ", Connected"));
      break;

    case WIFI_EVENT_STAMODE_DISCONNECTED:
      Serial.println(String("[WiFi] " + e + ", Disconnected - Status " + String( WiFi.status()) + String(connectionStatus( WiFi.status() ).c_str()) ));
      break;

    case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:
      Serial.println(String("[WiFi] " + e + ", AuthMode Change"));
      break;

    case WIFI_EVENT_STAMODE_GOT_IP:
      Serial.println(String("[WiFi] " + e + ", Got IP"));
      break;

    case WIFI_EVENT_STAMODE_DHCP_TIMEOUT:
      Serial.println(String("[WiFi] " + e + ", DHCP Timeout"));
      break;

    case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
      Serial.println(String("[WiFi] " + e + ", Client Connected"));
      break;

    case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
      Serial.println(String("[AP] " + e + ", Client Disconnected"));
      break;

    case WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED:
      Serial.println(String("[AP] " + e + ", Probe Request Recieved"));
      break;

    case WIFI_EVENT_ANY:
      Serial.println(String("[AP] " + e + ", WIFI_EVENT_ANY Recieved"));
      break;
      
    case WIFI_EVENT_MODE_CHANGE:
      Serial.println(String("[AP] " + e + ", WIFI_EVENT_MODE_CHANGE Recieved"));
      break;

    default:
//    case WIFI_EVENT_MAX:
      Serial.println(String("[AP] " + e + ", WIFI_EVENT_MAX Recieved"));
      break;
  }
}


/*******************************************************
*  WiFi Connection Status                              *
********************************************************/
String connectionStatus ( int which )
{
  switch ( which )
  {
    case WL_CONNECTED:
      return "Connected";
      break;

    case WL_NO_SSID_AVAIL:
      return "Network not availible";
      break;

    case WL_CONNECT_FAILED:
      return "Wrong password";
      break;

    case WL_IDLE_STATUS:
      return "Idle status";
      break;

    case WL_DISCONNECTED:
      return "Disconnected";
      break;

    default:
      return "Unknown";
      break;
  }
}

//===============================================================
// This routine is executed when you open its IP in browser
//===============================================================
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

void handleNotFound()
{ // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(g_http_server.uri()))
  { // check if the file exists in the flash memory (SPIFFS), if so, send it
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += g_http_server.uri();
    message += "\nMethod: ";
    message += (g_http_server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += g_http_server.args();
    message += "\n";
    for (uint8_t i = 0; i < g_http_server.args(); i++) {
      message += " " + g_http_server.argName(i) + ": " + g_http_server.arg(i) + "\n";
    }
    g_http_server.send(404, "text/plain", message);
  }
  else
  {
    String filename = String(g_http_server.uri());

    if (g_debug_prints_enabled)
    {
      Serial.println("File read:" + filename);
    }

    if (filename.indexOf("tx.html") >= 0)
    {
      g_ESP_ATMEGA_Comm_State = TX_HTML_PAGE_SERVED;
    }
  }
}


bool setupHTTP_AP()
{
  bool success = false;
  g_numberOfSocketClients = 0;
  g_numberOfWebClients = 0;
  Serial.setDebugOutput(false);

#if WIFI_DEBUG_PRINTS_ENABLED
  Serial.printf("Initial connection status: %d\n", WiFi.status());
  WiFi.printDiag(Serial);
#endif

  WiFi.mode(WIFI_AP_STA);
  // Event subscription
  e1 = WiFi.onSoftAPModeStationConnected(onNewStation);
  e2 = WiFi.onSoftAPModeStationDisconnected(onStationDisconnect);

  //  WiFi.mode(WIFI_AP);

  /* Create a somewhat unique SSID */
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 4], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 3], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  g_AP_NameString = String("Tx_" + macID);

  IPAddress apIP = stringToIP(g_softAP_IP_addr);

  if (apIP == IPAddress(-1, -1, -1, -1))
  {
    apIP = stringToIP(String(SOFT_AP_IP_ADDR_DEFAULT));   /* some reasonable default address */
  }

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  if (g_bridgePW.length() > 0)
  {
    success = WiFi.softAP(stringObjToConstCharString(&g_AP_NameString), stringObjToConstCharString(&g_bridgePW));
  }
  else
  {
    success = WiFi.softAP(stringObjToConstCharString(&g_AP_NameString));
  }

  if (success)
  {
    IPAddress myIP = WiFi.softAPIP(); //Get IP address

    //    if (MDNS.begin(stringObjToConstCharString(&g_mDNS_responder)))
    //    { // Start the mDNS responder for e.g., "esp8266.local"
    //      Serial.println("mDNS responder started: " + g_mDNS_responder + ".local");
    //    }
    //    else
    //    {
    //      Serial.println("Error setting up MDNS responder!");
    //    }

    /* Start TCP listener on port TCP_PORT */
    g_http_server.on("/", HTTP_GET, handleRoot);     // Call the 'handleRoot' function when a client requests URI "/"
    g_http_server.on("/login", HTTP_POST, handleLogin); // Call the 'handleLogin' function when a POST request is made to URI "/login"

    g_http_server.on("/upload", HTTP_GET, []() {                 // if the client requests the upload page
      if (!handleFileRead("/upload.html"))                // send it if it exists
        g_http_server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
    });

    g_http_server.on("/upload", HTTP_POST,                       // if the client posts to the upload page
    []() {
      g_http_server.send(200);
    },                          // Send status 200 (OK) to tell the client we are ready to receive
    handleFileUpload                                    // Receive and save the file
                    );


    // g_http_server.on("/edit.html",  HTTP_POST, []()
    // {  // If a POST request is sent to the /edit.html address,
    //   g_http_server.send(200, "text/plain", "");
    // }, handleFileUpload);                       // go to 'handleFileUpload'

    g_http_server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"
    g_http_server.begin();

    //    startWebSocketServer(); // Start a WebSocket server

    if (g_debug_prints_enabled)
    {
      Serial.print(g_AP_NameString + " server started: ");
      Serial.println(myIP);
    }
  }

  return (success);
}


// Manage incoming device connection on ESP access point
void onNewStation(WiFiEventSoftAPModeStationConnected sta_info) {
  char newMac[20];
  sprintf(newMac, MACSTR, MAC2STR(sta_info.mac));
  String newMacStr = String(newMac);
  newMacStr.toUpperCase();
  bool found = false;

  for (int i = 0; i < MAX_NUMBER_OF_WEB_CLIENTS; i++)
  {
    if (newMacStr.equals(g_webSocketClient[i].macAddr))
    {
      found = true;
      if (g_debug_prints_enabled) Serial.println("Station found in existing list.");
      g_webSocketClient[i].webID = sta_info.aid;
      g_webSocketClient[i].socketID = WEBSOCKETS_SERVER_CLIENT_MAX;
      break;
    }
  }

  g_numberOfWebClients = WiFi.softAPgetStationNum();
  g_numberOfSocketClients = min(g_numberOfWebClients, g_webSocketServer.clientConnections());

  if (!found)
  {
    int top = max(0, g_numberOfWebClients - 1);
    g_webSocketClient[top].macAddr = newMacStr;
    g_webSocketClient[top].webID = sta_info.aid;
    g_webSocketClient[top].socketID = WEBSOCKETS_SERVER_CLIENT_MAX;
  }

  if (g_debug_prints_enabled)
  {
    Serial.println("New Station: " + newMacStr);
    Serial.println("  Total stations: " + String(g_numberOfWebClients) + "; Socket clients: " + String(g_numberOfSocketClients));
  }

  if (g_debug_prints_enabled)
  {
    for (int i = 0; i < g_numberOfSocketClients; i++)
    {
      Serial.printf("%d. WebSocketClient: WebID# %d. MAC address : %s\n", i, g_webSocketClient[i].webID, (g_webSocketClient[i].macAddr).c_str());
    }
  }

  startWebSocketServer(); // Start a WebSocket server
}

void onStationDisconnect(WiFiEventSoftAPModeStationDisconnected sta_info) {
  char newMac[20];
  sprintf(newMac, MACSTR, MAC2STR(sta_info.mac));
  String oldMacStr = String(newMac);
  oldMacStr.toUpperCase();

  g_webSocketServer.disconnect(); /* disconnect all web sockets to prevent serious problems if the wrong socket were to be left connected */

  for (int i = 0; i < MAX_NUMBER_OF_WEB_CLIENTS; i++)
  {
    if (oldMacStr.equals(g_webSocketClient[i].macAddr))
    {
      g_webSocketClient[i].webID = 0;
      g_webSocketClient[i].macAddr = "";
      g_webSocketClient[i].socketID = WEBSOCKETS_SERVER_CLIENT_MAX;
      if (g_debug_prints_enabled) Serial.println("Station removed from list.");
      break;
    }
  }

  g_numberOfWebClients = WiFi.softAPgetStationNum();
  g_numberOfSocketClients = min(g_numberOfWebClients, g_webSocketServer.clientConnections());

  if (g_debug_prints_enabled)
  {
    Serial.println("Station Exit:");
    Serial.printf("  Remaining stations: %d\n", g_numberOfWebClients);
  }


  if (g_debug_prints_enabled)
  {
    for (int i = 0; i < MAX_NUMBER_OF_WEB_CLIENTS; i++)
    {
      if ((g_webSocketClient[i].macAddr).length())
      {
        Serial.printf("WebID# %d. sockID# %d. MAC address : %s\n", g_webSocketClient[i].webID, g_webSocketClient[i].socketID, (g_webSocketClient[i].macAddr).c_str());
      }
    }
  }
}



bool setupWiFiAPConnection()
{
  bool err = false;
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
      err = true;
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
  int value = 'W'; // start out functioning as a web server
  bool skipInitialCommand = true;
  bool toggle = false;
  unsigned long holdTime;
  int commaLoc;
  int nextCommaLoc;
  String arg1, arg2;
  bool argumentsRcvd = false;
  bool commandInProgress = false;
  int escapeCount = 0;
  bool done = false;

  g_debug_prints_enabled = DEBUG_PRINTS_ENABLE_DEFAULT; // kluge override of saved settings

  if (!skipInitialCommand)
  {
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
            argumentsRcvd = false;

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
          done = true;
        }
        else
        {
          if (g_debug_prints_enabled)
          {
            Serial.println("NULL command received...");
          }

          value = 'H';
          done = true;
        }

        commandInProgress = false;
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
            commandInProgress = true;
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
  } // skipInitialCommand

  skipInitialCommand = false;

  switch (value)
  {
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
        done = false;
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
                g_LEDs_enabled = (bool)arg2.toInt();

                if (g_LEDs_enabled != 0)
                {
                  g_LEDs_enabled = 1; /* avoid strange behavior for non 0/1 values */
                }
              }
              break;

            case DEBUG_PRINTS_ENABLE:
              {
                g_debug_prints_enabled = (bool)arg2.toInt();

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

        argumentsRcvd = false;
        done = false;
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

        done = false;
      }
      break;
  }
}


void httpWebServerLoop()
{
  uint8_t i, j;
  char buf[1024];
  size_t bytesAvail, bytesIn;
  bool done = false;
  bool toggle = false;
  unsigned long holdTime = 0;
  int escapeCount = 0;
  int hold = 0;
  String lb_message = "";
  int messageLength = 0;

  if (g_debug_prints_enabled)
  {
    Serial.println("Web Server loop started");
  }

  g_numberOfWebClients = WiFi.softAPgetStationNum();

  if (g_numberOfWebClients > 0)
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("Stations already connected:" + String(g_numberOfWebClients)));
      Serial.println(String("Existing socket connections:" + String(g_numberOfSocketClients)));
    }
    //    WiFi.disconnect();
    //    ESP.restart();
    //    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    WiFi.mode(WIFI_AP_STA);
    // Event subscription
    e1 = WiFi.onSoftAPModeStationConnected(onNewStation);
    e2 = WiFi.onSoftAPModeStationDisconnected(onStationDisconnect);
    i = WiFi.softAPgetStationNum();
    if (g_debug_prints_enabled)
    {
      Serial.println(String("Stations already connected:" + String(i)));
    }
  }

  while (!done)
  {
    /*check if there are any new clients */
    g_http_server.handleClient();
    g_webSocketServer.loop();

    g_numberOfWebClients = WiFi.softAPgetStationNum();
    g_numberOfSocketClients = min(g_numberOfWebClients, g_webSocketServer.clientConnections());

    if (g_numberOfWebClients != hold)
    {
      hold = g_numberOfWebClients;

      if (g_debug_prints_enabled)
      {
        Serial.println("Web Clients: " + String(g_numberOfWebClients));
        Serial.println("Socket Clients: " + String(g_numberOfSocketClients));
      }

      if (g_numberOfWebClients == 0)
      {
        g_webSocketServer.disconnect(); // ensure all web socket clients are disconnected - this might not happen if WiFi connection was broken
      }
    }

    /*check UART for data */
    while ((bytesAvail = Serial.available()) > 0)
    {
      bytesIn = Serial.readBytes(buf, min(sizeof(buf), bytesAvail));

      if (bytesIn > 0)
      {
        buf[bytesIn] = '\0';

        for (j = 0; j < bytesIn; j++)
        {
          if (buf[j] == '$')
          {
            escapeCount++;

            if (escapeCount == 3)
            {
              done = true;
              escapeCount = 0;
              Serial.println("Web Server closed");
              WiFi.softAPdisconnect(true);
            }
          }
          else if (buf[j] == '!')
          {
            lb_message = "!";
            messageLength = 1;
            escapeCount = 0;
          }
          else if ( messageLength > 0 )
          {
            lb_message += buf[j];
            messageLength++;

            if (buf[j] == ';')
            {
              messageLength = 0;
              handleLBMessage(lb_message);
            }
          }
          else
          {
            escapeCount = 0;
          }
        }
      }
    }

    g_relativeTimeSeconds = millis() / g_blinkPeriodMillis;
    if (holdTime != g_relativeTimeSeconds)
    {
      holdTime = g_relativeTimeSeconds;
      toggle = !toggle;

      if (!g_LEDs_enabled)
      {
        digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */
        digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
      }
      else
      {
        if (g_numberOfSocketClients)
        {
          digitalWrite(BLUE_LED, toggle); /* Blink blue LED */
          digitalWrite(RED_LED, !toggle); /* Blink red LED */
          Serial.println("web sockets: " + String(g_numberOfSocketClients));
        }
        else if (g_numberOfWebClients)
        {
          digitalWrite(BLUE_LED, toggle); /* Blink blue LED */
          digitalWrite(RED_LED, HIGH); /* Turn off red LED */
          Serial.println("web clients: " + String(g_numberOfWebClients));
        }
        else
        {
          digitalWrite(RED_LED, toggle); /* Blink red LED */
          digitalWrite(BLUE_LED, HIGH); /* Turn off blue LED */
        }
      }

      if (g_numberOfSocketClients)
      {
        if (toggle) Serial.printf("$TIM?"); // request latest time once per second

        if (!(holdTime % 61))
        {
          Serial.printf("$TEM?"); // request temperature reading
        }
        else if (!(holdTime % 17))
        {
          Serial.printf("$BAT?"); // request battery level reading
        }
      }

      switch (g_ESP_ATMEGA_Comm_State)
      {
        case TX_WAKE_UP:
          {
            /* Inform the ATMEGA that WiFi power up is complete */
            Serial.printf("$ESP,0;"); // Send ESP message to ATMEGA
          }
          break;

        case TX_TIME_RECEIVED:
          {
            unsigned long epoch = convertTimeStringToEpoch(g_timeOfDayFromTx);
            int numScheduledEvents = numberOfEventsScheduled(epoch);

            /* check to see if an event is scheduled for this time */
            if (numScheduledEvents)
            {
              /* Send messages to ATMEGA informing it of the time of the next scheduled event */
              if (g_activeEvent == NULL) g_activeEvent = new Event(g_debug_prints_enabled);
              g_activeEvent->readEventFile(g_eventList[0].path);
              g_activeEventIndex = 0;
              String lbMsg = String(String("$ESP,1,") + g_activeEvent->eventData->event_start_date_time  + ";");
              Serial.printf(stringObjToConstCharString(&lbMsg)); // Send ESP message to ATMEGA
            }
            else
            {
              /* Inform the ATMEGA that no events are scheduled */
              Serial.printf("$ESP,Z;"); // Send ESP message to ATMEGA
            }

            g_ESP_ATMEGA_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
          }
          break;

        case TX_HTML_PAGE_SERVED:
          {
            if (g_numberOfSocketClients) {
              String msg;

              if (g_debug_prints_enabled)
              {
                Serial.println("Sending MAC addresses:");
              }

              for (int i = 0; i < MAX_NUMBER_OF_WEB_CLIENTS; i++)
              {
                if ((g_webSocketClient[i].macAddr).length() && (g_webSocketClient[i].socketID < WEBSOCKETS_SERVER_CLIENT_MAX))
                {
                  msg = String( String(SOCK_COMMAND_MAC) + "," + g_webSocketClient[i].macAddr );
                  g_webSocketServer.sendTXT(g_webSocketClient[i].socketID, stringObjToConstCharString(&msg), msg.length());
                  if (g_debug_prints_enabled)
                  {
                    Serial.println(msg);
                  }
                }
              }

              if (g_debug_prints_enabled)
              {
                Serial.println("Sending SSID:");
              }

              msg = String( String(SOCK_COMMAND_SSID) + "," + g_AP_NameString );
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
              if (g_debug_prints_enabled)
              {
                Serial.println(msg);
              }

              g_ESP_ATMEGA_Comm_State = TX_HTML_NEXT_EVENT;
            }
            else {
              g_ESP_ATMEGA_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
            }
          }
          break;


        case TX_HTML_NEXT_EVENT:
          {
            if (g_numberOfSocketClients) {
              String msg;
              if (g_debug_prints_enabled)
              {
                Serial.println("Sending MAC addresses:");
              }

              for (int i = 0; i < MAX_NUMBER_OF_WEB_CLIENTS; i++)
              {
                if ((g_webSocketClient[i].macAddr).length() && (g_webSocketClient[i].socketID < WEBSOCKETS_SERVER_CLIENT_MAX))
                {
                  msg = String( String(SOCK_COMMAND_MAC) + "," + g_webSocketClient[i].macAddr );
                  g_webSocketServer.sendTXT(g_webSocketClient[i].socketID, stringObjToConstCharString(&msg), msg.length());
                  if (g_debug_prints_enabled)
                  {
                    Serial.println(msg);
                  }
                }
              }

              if (g_debug_prints_enabled)
              {
                Serial.println("Sending SSID:");
              }

              msg = String( String(SOCK_COMMAND_SSID) + "," + g_AP_NameString );
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
              if (g_debug_prints_enabled)
              {
                Serial.println(msg);
              }

              if (g_eventsRead)
              {
                if (g_activeEvent == NULL)
                {
                  g_activeEventIndex = 0;
                  g_activeEvent = new Event(g_debug_prints_enabled);
                  g_activeEvent->readEventFile(g_eventList[0].path);
                }
                else
                {
                  g_activeEventIndex = (g_activeEventIndex + 1) % g_eventsRead;
                  g_activeEvent->readEventFile(g_eventList[g_activeEventIndex].path);
                }

 //               g_activeEvent->dumpData();

                String msg = String(String(SOCK_COMMAND_EVENT_NAME) + "," + g_activeEvent->eventData->event_name );
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                if (g_debug_prints_enabled)
                {
                  Serial.println(msg);
                }

                msg = String(String(SOCK_COMMAND_CALLSIGN) + "," + g_activeEvent->eventData->event_callsign);
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                if (g_debug_prints_enabled)
                {
                  Serial.println(msg);
                }

                msg = String(String(SOCK_COMMAND_START_TIME) + "," + g_activeEvent->eventData->event_start_date_time);
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                if (g_debug_prints_enabled)
                {
                  Serial.println(msg);
                }

                msg = String(String(SOCK_COMMAND_FINISH_TIME) + "," + g_activeEvent->eventData->event_finish_date_time);
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                if (g_debug_prints_enabled)
                {
                  Serial.println(msg);
                }

                for (int i = 0; i < g_activeEvent->eventData->event_number_of_tx_types; i++)
                {
                  msg = String(String(SOCK_COMMAND_TYPE_NAME) + "," + g_activeEvent->eventData->role[i]->rolename);
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                  if (g_debug_prints_enabled)
                  {
                    Serial.println(msg);
                  }
                }

                for (int i = 0; i < g_activeEvent->eventData->event_number_of_tx_types; i++)
                {
                  msg = String(String(SOCK_COMMAND_TYPE_TX_COUNT) + "," + String(g_activeEvent->eventData->role[i]->numberOfTxs));
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                  if (g_debug_prints_enabled)
                  {
                    Serial.println(msg);
                  }
                }

                for (int i = 0; i < g_activeEvent->eventData->event_number_of_tx_types; i++)
                {
                  msg = String(String(SOCK_COMMAND_TYPE_FREQ) + "," + String(g_activeEvent->eventData->role[i]->frequency));
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                  if (g_debug_prints_enabled)
                  {
                    Serial.println(msg);
                  }
                }

                for (int i = 0; i < g_activeEvent->eventData->event_number_of_tx_types; i++)
                {
                  msg = String(String(SOCK_COMMANT_TYPE_PWR) + "," + String(g_activeEvent->eventData->role[i]->powerLevel_mW));
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                  if (g_debug_prints_enabled)
                  {
                    Serial.println(msg);
                  }
                }

                for (int i = 0; i < g_activeEvent->eventData->event_number_of_tx_types; i++)
                {
                  msg = String(String(SOCK_COMMAND_TYPE_WPM) + "," + String(g_activeEvent->eventData->role[i]->code_speed));
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                  if (g_debug_prints_enabled)
                  {
                    Serial.println(msg);
                  }
                }

                for (int i = 0; i < g_activeEvent->eventData->event_number_of_tx_types; i++)
                {
                  msg = String(String(SOCK_COMMAND_TYPE_ID_INTERVAL) + "," + String(g_activeEvent->eventData->role[i]->id_interval));
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                  if (g_debug_prints_enabled)
                  {
                    Serial.println(msg);
                  }
                }
              }
            }
            g_ESP_ATMEGA_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
          }
          break;

        case TX_RECD_EVENTS_QUERY:
          {
            /* Update event file list ? */
            /* check for a scheduled event with the requested antenna setting ? */
            g_ESP_ATMEGA_Comm_State = TX_WAKE_UP;
          }
          break;

        case TX_RECD_START_EVENT_REQUEST:
          {
            /*  ATMEGA has determined it is time to start the countdown to the event, so WiFi needs to
                configure the ATMEGA appropriately for its role in the scheduled event.
            */
            g_ESP_ATMEGA_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
          }
          break;

        case TX_WAITING_FOR_INSTRUCTIONS:
          {
            //          if(g_debug_prints_enabled)
            //          {
            //            if(!g_numberOfWebClients) Serial.println("WiFi idle");
            //          }

            if (g_numberOfWebClients)
            {
              if (!(holdTime % 25))             /* Ask ATMEGA to wait longer before shutting down WiFi */
              {
                String lbMsg = String("$ESP,Z;");
                Serial.printf(stringObjToConstCharString(&lbMsg)); // Send ESP message to ATMEGA
              }
            }
          }
          break;
      }
    }
  }
}

void startSPIFFS()
{ // Start the SPIFFS and list all contents
  SPIFFS.begin();   // Start the SPI Flash File System (SPIFFS)

  if (g_debug_prints_enabled)
  {
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
}

void startWebSocketServer()
{ // Start a WebSocket server
  if (g_webSocketServer.isReady()) return; /* Don't attempt to start a webSocket server that has already begun */

  g_webSocketServer.begin();      // start the websocket server
  //  g_webSocketServer.beginSSL(); // start secure wss support?
  g_webSocketServer.onEvent(webSocketEvent);  // if there's an incoming websocket message, go to function 'webSocketEvent'

  if (g_debug_prints_enabled)
  {
    Serial.println("WebSocket server start tasks complete.");
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  if (g_debug_prints_enabled)
  {
    Serial.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  }

  switch (type) {
    case WStype_DISCONNECTED:
      if (g_debug_prints_enabled)
      {
        Serial.printf("[%u] Web Socket disconnected!\r\n", num);
      }

      if (g_numberOfSocketClients)
      {
        /* Invalidate the socket ID of the disconnected client */
        if (g_numberOfSocketClients)
        {
          for (uint8_t i = 0; i < MAX_NUMBER_OF_WEB_CLIENTS; i++)
          {
            if (g_webSocketClient[i].socketID == num)
            {
              g_webSocketClient[i].socketID = WEBSOCKETS_SERVER_CLIENT_MAX;
              break;
            }
          }
        }

        g_numberOfSocketClients = min(g_numberOfWebClients, g_webSocketServer.clientConnections());
      }
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = g_webSocketServer.remoteIP(num);
        if (g_debug_prints_enabled)
        {
          Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        }

        /* Assign the socket ID to the web client that does not yet have a socket ID assigned */
        if (g_numberOfWebClients)
        {
          for (uint8_t i = 0; i < MAX_NUMBER_OF_WEB_CLIENTS; i++)
          {
            if ((g_webSocketClient[i].socketID >= WEBSOCKETS_SERVER_CLIENT_MAX) && ((g_webSocketClient[i].macAddr).length() > 0))
            {
              g_webSocketClient[i].socketID = num;
              break;
            }
          }
        }

        g_numberOfSocketClients = min(g_numberOfWebClients, g_webSocketServer.clientConnections());
        g_ESP_ATMEGA_Comm_State = TX_HTML_PAGE_SERVED;
      }
      break;

    case WStype_TEXT:
      {
        if (g_debug_prints_enabled)
        {
          Serial.printf("[%u] get Text: %s\r\n", num, payload);
        }

        String p = String((char*)payload);
        String msgHeader = p.substring(0, p.indexOf(','));

        if (msgHeader == SOCK_COMMAND_SYNC_TIME)
        {
          p = p.substring(p.indexOf(',') + 1);

          if (g_debug_prints_enabled)
          {
            Serial.printf(String("Time string: \"" + p + "\"\n").c_str());
          }
          String lbMsg = String("$TIM," + p + ";");
          Serial.printf(stringObjToConstCharString(&lbMsg)); // Send time to Transmitter for synchronization
        }
        else if (msgHeader == SOCK_COMMAND_FOX_ID)
        {
          int c = p.indexOf(':');
          p = p.substring(c - 1, c + 2);

          String msg = String(String(SOCK_COMMAND_FOX_ID) + "," + g_activeEvent->getTxDescriptiveName(p));
          g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());

          if (g_debug_prints_enabled)
          {
            Serial.println(msg);
          }
        }
        else if (msgHeader == SOCK_COMMAND_EVENT_NAME)
        {
          g_ESP_ATMEGA_Comm_State = TX_HTML_NEXT_EVENT;
        }
        else if (msgHeader == SOCK_COMMAND_CALLSIGN)
        {
          p = p.substring(p.indexOf(',') + 1);
          p.toUpperCase();

          if (g_debug_prints_enabled)
          {
            Serial.printf(String("Callsign: \"" + p + "\"\n").c_str());
          }
          String lbMsg = String("$ID," + p + ";");
          Serial.printf(stringObjToConstCharString(&lbMsg)); // Send to Transmitter
          Serial.println("Sent CALLSIGN message!");

          String msg = String(String(SOCK_COMMAND_CALLSIGN) + "," + p);

          g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());

          g_activeEvent->eventData->event_callsign = p;
        }
        else if (msgHeader.startsWith(SOCK_COMMAND_START_TIME))
        {
          p = p.substring(p.indexOf(',') + 1);

          if (g_debug_prints_enabled)
          {
            Serial.printf(String("Start Time: \"" + p + "\"\n").c_str());
          }
          String lbMsg = String("$SF,S," + p + ";");
          Serial.printf(stringObjToConstCharString(&lbMsg)); // Send to Transmitter

          g_activeEvent->eventData->event_start_date_time = p;
        }
        else if (msgHeader.startsWith(SOCK_COMMAND_FINISH_TIME))
        {
          p = p.substring(p.indexOf(',') + 1);

          if (g_debug_prints_enabled)
          {
            Serial.printf(String("Finish Time: \"" + p + "\"\n").c_str());
          }
          String lbMsg = String("$SF,F," + p + ";");
          Serial.printf(stringObjToConstCharString(&lbMsg)); // Send to Transmitter

          g_activeEvent->eventData->event_finish_date_time = p;
        }
        else if (msgHeader == SOCK_COMMAND_TYPE_FREQ)
        {
          int typeIndex;
          long freq;
          int firstComma = p.indexOf(',');
          int secondComma = p.indexOf(',', firstComma+1);
          typeIndex = (p.substring(firstComma+1, secondComma)).toInt();
          freq = (p.substring(secondComma)).toInt();
          (g_activeEvent->eventData->role[typeIndex])->frequency = freq;
          
          if (g_debug_prints_enabled)
          {
            Serial.println("Freq" + String(typeIndex) + ": " + String(freq));
          }
        }
      }
      break;

    case WStype_BIN:
      {
        if (g_debug_prints_enabled)
        {
          Serial.printf("[%u] get binary length: %u\r\n", num, length);
        }

        hexdump(payload, length);

        // echo data back to browser
        g_webSocketServer.sendBIN(num, payload, length);
      }
      break;

    default:
      if (g_debug_prints_enabled)
      {
        Serial.printf("Invalid WStype [%d]\r\n", type);
      }
      break;
  }
}

bool handleFileRead(String path)
{ // send the right file to the client (if it exists)
  if (g_debug_prints_enabled)
  {
    Serial.println("\nhandleFileRead: " + path);
  }

  if (path.endsWith("/")) path += "index.html";   // If a folder is requested, send the index file
  String contentType = getContentType(path);      // Get the MIME type
  String pathWithGz = path + ".gz";
  String pathWithHTML = path + ".html";
  if (SPIFFS.exists(stringObjToConstCharString(&pathWithGz)) || SPIFFS.exists(stringObjToConstCharString(&path)) || SPIFFS.exists(stringObjToConstCharString(&pathWithHTML)))
  { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))      // If there's a compressed version available
      path += ".gz";                    // Use the compressed verion
    File file = SPIFFS.open(path, "r"); // Open the file
    size_t sent = g_http_server.streamFile(file, contentType);  // Send it to the client
    file.close();                       // Close the file again
    if (g_debug_prints_enabled)
    {
      Serial.println(String("\tSent " + String(sent) + "bytes to file: ") + path);
    }
    return true;
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("File not found in SPIFFS: ") + path);
    }
  }

  return false;
}


int numberOfEventsScheduled(unsigned long epoch)
{
  int numberScheduled = 0;
  EventFileRef a;

  if (g_eventsRead < 1) return 0; /* no events means none is scheduled */

  if (g_eventsRead > 1)
  {
    /* binary sort the events list from soonest to latest */
    for (int i = 0; i < g_eventsRead; ++i)
    {
      for (int j = i + 1; j < g_eventsRead; ++j)
      {
        if (Event::isSoonerEvent(g_eventList[i], g_eventList[j], epoch))
        {
          a = g_eventList[i];
          g_eventList[i] = g_eventList[j];
          g_eventList[j] = a;
        }
      }
    }
  }

  for (int i=0; i < g_eventsRead; i++)
  {
    if (g_eventList[i].startDateTimeEpoch > epoch) /* scheduled to start in the future */
    {
      numberScheduled++;
    }
    else if ((g_eventList[i].startDateTimeEpoch <= epoch) && (g_eventList[i].finishDateTimeEpoch > epoch)) /* event is now in progress */
    {
      numberScheduled++;
    }
    else /* the sorted list will not contain any additional scheduled events */
    {
      break;
    }
  }

  return numberScheduled;
}

bool readEventTimes(String path, EventFileRef* fileRef)
{
  fileRef->path = path;
  bool startRead = false, finishRead = false;

  if (SPIFFS.exists(path))
  {
    File file = SPIFFS.open(path, "r"); // Open the file for reading
    String s = file.readStringUntil('\n');
    int count = 0;

    while (s.length() && (count++ < MAXIMUM_NUMBER_OF_EVENT_FILE_LINES) && (!startRead || !finishRead))
    {
      if (s.indexOf("EVENT_START_DATE_TIME") >= 0)
      {
        EventLineData data;
        Event::extractLineData(s, &data);
        fileRef->startDateTimeEpoch = convertTimeStringToEpoch(data.value);
        startRead = true;

        if ( g_debug_prints_enabled )
        {
          Serial.printf("Start epoch: %s\r\n", String(fileRef->startDateTimeEpoch).c_str());
        }
      }
      else if (s.indexOf("EVENT_FINISH_DATE_TIME") >= 0)
      {
        EventLineData data;
        Event::extractLineData(s, &data);
        fileRef->finishDateTimeEpoch = convertTimeStringToEpoch(data.value);
        finishRead = true;

        if ( g_debug_prints_enabled )
        {
          Serial.printf("Finish epoch: %s\r\n", String(fileRef->finishDateTimeEpoch).c_str());
        }
      }

      s = file.readStringUntil('\n');
    }

    file.close(); // Close the file

    if (g_debug_prints_enabled)
    {
      Serial.println(String("\tRead file: ") + path);
    }
  }

  return (!startRead || !finishRead);
}

bool populateEventFileList(void)
{
  String path = "/defaults.txt";

  if ( g_debug_prints_enabled ) Serial.println("Reading events...");
  Dir dir = SPIFFS.openDir("/");
  while (dir.next())
  {
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();

    if (fileName.endsWith(".event"))
    {
      if ( g_debug_prints_enabled )
      {
        Serial.printf("FS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
      }

      EventFileRef fileRef;
      if (!readEventTimes(fileName, &fileRef))
      {
        g_eventList[g_eventsRead] = fileRef;
        g_eventsRead++;
      }
    }
  }

  if ( g_debug_prints_enabled )
  {
    for (int i = 0; i < g_eventsRead; i++)
    {
      Serial.println( String(i) + ". " + g_eventList[i].path);
      Serial.println( "    " + g_eventList[i].startDateTimeEpoch);
      Serial.println( "    " + g_eventList[i].finishDateTimeEpoch);
    }
    Serial.printf("\n");
  }

  return true;
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
        //        if (value.charAt(0) == 'T' || value.charAt(0) == 't' || value.charAt(0) == '1')
        //        {
        //          g_debug_prints_enabled = 1;
        //        }
        //        else
        //        {
        //          g_debug_prints_enabled = 0;
        //        }
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

    if (g_debug_prints_enabled)
    {
      Serial.println(String("\tRead file: ") + path);
    }

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
    Serial.println(String("\tWrote file: ") + path);
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

void handleLBMessage(String message)
{
//  bool isReply = message.charAt(0) == '!';
  String type = message.substring(1, 4);
  String payload = message.substring(5, message.indexOf(';'));

  if (type == MESSAGE_ESP)
  {
    //    g_timeOfDayFromTx = payload;
  }
  else if (type == MESSAGE_TIME)
  {
    String timeinfo = payload;
    g_timeOfDayFromTx = payload;
    unsigned long epoch = convertTimeStringToEpoch(g_timeOfDayFromTx);

    if (g_debug_prints_enabled)
    {
      Serial.println("TIM message received from ATMEGA!");
      Serial.println(g_timeOfDayFromTx + " epoch = " + String(epoch));
    }

    if (epoch)
    {
      if (g_ESP_ATMEGA_Comm_State == TX_WAKE_UP) g_ESP_ATMEGA_Comm_State = TX_TIME_RECEIVED;

      if (g_numberOfSocketClients) {
        String msg = String(String(SOCK_COMMAND_SYNC_TIME) + "," + timeinfo);
        g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());

        if (g_debug_prints_enabled)
        {
          Serial.println("Broadcast to sockets: " + msg);
        }
      }
    }
  }
  else if (type == MESSAGE_TEMP)
  {
    int16_t rawtemp = payload.toInt();
    bool negative =  rawtemp & 0x8000;
    if (negative) rawtemp &= 0x7FFF;
    float temp = (rawtemp >> 8) + (0.25 * ((rawtemp & 0x00C0) >> 6)) + 0.05;
    if (negative) temp = -temp;

    char dataStr[6]; // allow for possible negative sign
    dtostrf(temp, 4, 1, dataStr);
    dataStr[5] = '\0';

    if (g_numberOfSocketClients) {
      String msg = String(String(SOCK_COMMAND_TEMPERATURE) + "," + dataStr + "C");

      g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
      if (g_debug_prints_enabled)
      {
        Serial.println(msg);
      }
    }
  }
  else if (type == MESSAGE_BATTERY)
  {
    float temp = payload.toFloat();

    if (temp > FULLY_CHARGED_BATTERY_mV)
    {
      temp = 100.;
    }
    else if (temp < FULLY_DEPLETED_BATTERY_mV)
    {
      temp = 0;
    }
    else
    {
      temp = (100. * ((payload.toFloat() - FULLY_DEPLETED_BATTERY_mV) / (FULLY_CHARGED_BATTERY_mV - FULLY_DEPLETED_BATTERY_mV) )) + 0.5;
    }

    char dataStr[4];
    dtostrf(temp, 3, 0, dataStr);
    dataStr[3] = '\0';

    if (g_numberOfSocketClients) {
      String msg = String(String(SOCK_COMMAND_BATTERY) + "," + dataStr + "%");

      g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
      if (g_debug_prints_enabled)
      {
        Serial.println(msg);
      }
    }
  }
}

