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
WebSocketsServer g_webSocket = WebSocketsServer(81);  // create a websocket server on port 81

typedef struct webSocketClient {
  uint8_t webID;
  uint8_t socketID;
  char macAddr[20];
} WebSocketClient;

WebSocketClient g_webSocketClient[MAX_NUMBER_OF_WEB_CLIENTS]; // Keep track of active clients and their MAC addresses
uint8_t g_numberOfSocketClients = 0;
uint8_t g_numberOfWebClients = 0;

ESP8266WiFiMulti wifiMulti;     // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
File fsUploadFile;  // a File variable to temporarily store the received file

String g_softAP_IP_addr;
bool g_main_page_served = false;

void handleRoot();              // function prototypes for HTTP handlers
void handleLED();
void handleNotFound();

/*
   Main Program Support
*/

unsigned long g_relativeTimeSeconds;
unsigned long g_lastAccessToNISTServers = 0;
bool g_timeWasSet = false;
int g_blinkPeriodMillis = 500;

static WiFiEventHandler e1, e2;

Transmitter *g_xmtr;

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

  showSettings();

#if WIFI_DEBUG_PRINTS_ENABLED
  WiFi.onEvent(eventWiFi);      // Handle WiFi event
#endif

  g_xmtr = new Transmitter(g_debug_prints_enabled);
}

/********************************************************
  /*  Handle WiFi events                                  *
  /********************************************************/
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
  }
}


/********************************************************
  /*  WiFi Connection Status                              *
  /********************************************************/
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


void handleLED()
{ // If a POST request is made to URI /LED
  digitalWrite(RED_LED, !digitalRead(RED_LED));    /* Toggle red LED */
  g_http_server.sendHeader("Location", "/");       // Add a header to respond with a new location for the browser to go to the home page again
  g_http_server.send(303);                         // Send it back to the browser with an HTTP status 303 (See Other) to redirect
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
      g_main_page_served = true;
    }
  }
}


bool setupHTTP_AP()
{
  bool success = false;
  g_numberOfSocketClients = 0;
  g_numberOfWebClients = 0;
  Serial.setDebugOutput(true);

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
    g_http_server.on("/LED", HTTP_POST, handleLED);  // Call the 'handleLED' function when a POST request is made to URI "/LED"
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

    //    startWebSocket(); // Start a WebSocket server

    if (g_debug_prints_enabled)
    {
      Serial.print(ap_NameString + " server started: ");
      Serial.println(myIP);
    }
  }

  return (success);
}


// Manage incoming device connection on ESP access point
void onNewStation(WiFiEventSoftAPModeStationConnected sta_info) {
#if WIFI_DEBUG_PRINTS_ENABLED
  Serial.println("New Station :");
  Serial.println("Station List");
#endif

  if (g_numberOfWebClients > MAX_NUMBER_OF_WEB_CLIENTS)
  {
    if (g_debug_prints_enabled)
    {
      Serial.printf("ERROR: Number of web clients (%d) exceeds MAX_NUMBER_OF_WEB_CLIENTS.", g_numberOfWebClients);
      // TODO: attempt to recover gracefully
    }
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      for (int i = 0; i < g_numberOfWebClients; i++)
      {
        Serial.printf("WebID# %d. MAC address : %s\n", g_webSocketClient[i].webID, g_webSocketClient[i].macAddr);
      }
    }
    sprintf(g_webSocketClient[g_numberOfWebClients].macAddr, "%02X:%02X:%02X:%02X:%02X:%02X", MAC2STR(sta_info.mac));
    g_webSocketClient[g_numberOfWebClients].webID = sta_info.aid;

    if (g_debug_prints_enabled)
    {
      Serial.printf("WebID# %d. MAC address : %s\n", g_webSocketClient[g_numberOfWebClients].webID, g_webSocketClient[g_numberOfWebClients].macAddr);
    }

    g_numberOfWebClients++;
    startWebSocket(); // Start a WebSocket server
  }
}

void onStationDisconnect(WiFiEventSoftAPModeStationDisconnected sta_info) {
  if (g_numberOfWebClients) g_numberOfWebClients--;
  if (g_debug_prints_enabled)
  {
    Serial.println("Station Exit :");
  }

  if (g_numberOfWebClients > MAX_NUMBER_OF_WEB_CLIENTS)
  {
    if (g_debug_prints_enabled)
    {
      Serial.printf("ERROR: Number of web clients (%d) exceeds MAX_NUMBER_OF_WEB_CLIENTS.", g_numberOfWebClients);
      // TODO: attempt to recover gracefully
    }
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      for (int i = 0; i < g_numberOfWebClients; i++)
      {
        if (g_webSocketClient[i].webID == sta_info.aid)
        {
          Serial.printf("WebID# %d. sockID# %d. MAC address : %s\n", g_webSocketClient[i].webID, g_webSocketClient[i].socketID, g_webSocketClient[i].macAddr);
        }
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
        done = false;
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

#ifdef USE_UDP_FOR_TIME_RETRIEVAL

void getNistTime(void)
{
  bool success = false;
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

  success = false;
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

    //    tempStr = String("$TIM," + String(timeVal) + ";");
    //   Serial.println(tempStr);        /* Send command to set time */

    g_timeWasSet = true;
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
bool sendNTPpacket(String address, byte *packetBuffer)
{
  bool success = false;

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
        //       tempStr = String("$TIM," + String(timeVal) + ";");
        //
        //       if (g_debug_prints_enabled)
        //       {
        //         Serial.println(tempStr);
        //       }

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


void httpWebServerLoop()
{
  uint8_t i, j;
  char buf[1024];
  int bytesAvail, bytesIn;
  bool done = false;
  bool toggle = false;
  unsigned long holdTime;
  bool clientConnected = false;
  int escapeCount = 0;
  int numConnected = 0;
  int hold;
  String lb_message = "";
  int messageLength = 0;

  if (g_debug_prints_enabled)
  {
    Serial.println("Web Server loop started");
  }

  i = WiFi.softAPgetStationNum();
  if (i > 0)
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("Stations already connected:" + String(i) + " ...disconnecting..."));
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
    g_webSocket.loop();

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
              //             if (g_debug_prints_enabled)
              //             {
              Serial.println("Web Server closed");
              //             }

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

#ifdef FOOBAR
        else if ((buf[j] == 'H') || (buf[j] == 'h'))
        {
          time_t rawtime;
          struct tm * timeinfo;
          time ( &rawtime );
          timeinfo = localtime ( &rawtime );

          for (uint8_t i = 0; i < g_numberOfSocketClients; i++)
          {
            String msg = String("MAC," + String(g_webSocketClient[i].macAddr));
            g_webSocket.sendTXT(g_webSocketClient[i].socketID, stringObjToConstCharString(&msg), msg.length());

            msg = String("TIME," + String(asctime (timeinfo)));
            Serial.println("Time sent =" + msg);
            g_webSocket.sendTXT(g_webSocketClient[i].socketID, stringObjToConstCharString(&msg), msg.length());


          }
        }
        else if ((buf[j] == 'S') || (buf[j] == 's'))
        {
          for (uint8_t i = 0; i < g_numberOfSocketClients; i++)
          {
            String msg = String("START," + String("2018-01-02T00:52")); // yyyy-MM-ddThh:mm
            g_webSocket.sendTXT(g_webSocketClient[i].socketID, stringObjToConstCharString(&msg), msg.length());
          }
        }
        else if ((buf[j] == 'F') || (buf[j] == 'f'))
        {
          for (uint8_t i = 0; i < g_numberOfSocketClients; i++)
          {
            String msg = String("FINISH," + String("2018-01-02T00:52")); // yyyy-MM-ddThh:mm
            g_webSocket.sendTXT(g_webSocketClient[i].socketID, stringObjToConstCharString(&msg), msg.length());
          }
        }
        else
        {
          escapeCount = 0;
        }

#endif // FOOBAR
      }
    }

    g_relativeTimeSeconds = millis() / g_blinkPeriodMillis;
    if (holdTime != g_relativeTimeSeconds)
    {
      holdTime = g_relativeTimeSeconds;
      toggle = !toggle;
      if (g_LEDs_enabled)
      {
        digitalWrite(BLUE_LED, toggle); /* Blink blue LED */
      }
      else
      {
        digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */
      }

      if (g_numberOfSocketClients > 0)
      {
        if (toggle) Serial.printf("$TIM?"); // request latest time once per second
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

void startWebSocket()
{ // Start a WebSocket server
  g_webSocket.begin();      // start the websocket server
  //  g_webSocket.beginSSL(); // start secure wss support?
  g_webSocket.onEvent(webSocketEvent);  // if there's an incoming websocket message, go to function 'webSocketEvent'

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
        Serial.printf("[%u] Disconnected!\r\n", num);
      }

      if (g_numberOfSocketClients)
      {
        g_numberOfSocketClients--;

        if (g_numberOfSocketClients)
        {
          for (uint8_t i = 0; i < g_numberOfSocketClients; i++)
          {
            if (g_webSocketClient[i].socketID == num)
            {
              g_webSocketClient[i].socketID = g_webSocketClient[i + 1].socketID;
              g_webSocketClient[i].webID = g_webSocketClient[i + 1].webID;
              strcpy(g_webSocketClient[i].macAddr, g_webSocketClient[i + 1].macAddr);
              g_webSocketClient[i + 1].socketID = num;
            }
          }
        }
      }
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = g_webSocket.remoteIP(num);
        if (g_debug_prints_enabled)
        {
          Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        }
        g_webSocketClient[g_numberOfSocketClients].socketID = num;

        if (g_main_page_served)
        {
          time_t rawtime;
          struct tm * timeinfo;
          time ( &rawtime );
          timeinfo = localtime ( &rawtime );

          if (g_debug_prints_enabled)
          {
            Serial.println("Sending MAC address:");
          }
          String msg = String("MAC," + String(g_webSocketClient[g_numberOfSocketClients].macAddr));
          g_webSocket.sendTXT(g_webSocketClient[g_numberOfSocketClients].socketID, stringObjToConstCharString(&msg), msg.length());
          if (g_debug_prints_enabled)
          {
            Serial.println(msg);
          }

          msg = String("TIME," + String(asctime (timeinfo)));
          g_webSocket.sendTXT(g_webSocketClient[g_numberOfSocketClients].socketID, stringObjToConstCharString(&msg), msg.length());
          if (g_debug_prints_enabled)
          {
            Serial.println(msg);
          }

          //         Serial.printf("$TIM?"); // Request current time setting from Transmitter

          g_main_page_served = false;
        }

        g_numberOfSocketClients++;
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

        if (msgHeader == COMMAND_SYNC_TIME)
        {
          p = p.substring(p.indexOf(',') + 1);

          if (g_debug_prints_enabled)
          {
            Serial.printf(String("Time string: \"" + p + "\"\n").c_str());
          }
          String lbMsg = String("$TIM," + String(stringToTimeVal(p)) + ";");
          Serial.printf(stringObjToConstCharString(&lbMsg)); // Send time to Transmitter for synchronization
          Serial.println("Sent TIME message!");
        }

        // send data to all connected clients
        //g_webSocket.broadcastTXT(payload, length);
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
        g_webSocket.sendBIN(num, payload, length);
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
      Serial.println(String("\tSent file: ") + path);
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

bool readEventFile(String eventName)
{
  String path = String("/defaults" + eventName + ".txt");

  if (g_debug_prints_enabled)
  {
    Serial.println("Reading " + eventName + "settings...");
  }

  String contentType = getContentType(path);      // Get the MIME type
  if (SPIFFS.exists(path))
  { // If the file exists, either as a compressed archive, or normal
    // open file for reading
    File file = SPIFFS.open(path, "r"); // Open the file
    String s = file.readStringUntil('\n');
    int count = 0;

    while (s.length() && count++ < NUMBER_OF_TRANSMITTER_SETTINGS)
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

      g_xmtr->setXmtrData(settingID, value);

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

  if (g_debug_prints_enabled)
  {
    Serial.println(String("\tFile Not Found: ") + path);  // If the file doesn't exist, return false
  }

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
  //Serial.println(String("Message: " + message));

  bool isReply = message.charAt(0) == '!';
  String type = message.substring(1, 4);
  String payload = message.substring(5, message.indexOf(';'));

  //Serial.println(String("Reply? - ") + isReply ? "Yes" : "No");
  //Serial.println(String("Type: " + type));
  //Serial.println(String("Arg: " + payload));

  if (type == MESSAGE_TIME)
  {
    String timeinfo = timeValToString(payload.toInt());
    //   Serial.println(String("Time: ") + timeinfo);

    String msg = String(String(COMMAND_SYNC_TIME) + "," + timeinfo);

    for (int i = 0; i < g_numberOfSocketClients; i++)
    {
      g_webSocket.sendTXT(g_webSocketClient[i].socketID, stringObjToConstCharString(&msg), msg.length());
      //      if (g_debug_prints_enabled)
      //      {
      //  Serial.println(msg);
      //      }
    }
  }
}

