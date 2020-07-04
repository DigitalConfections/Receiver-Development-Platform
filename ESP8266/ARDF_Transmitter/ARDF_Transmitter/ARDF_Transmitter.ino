/**********************************************************************************************
    Copyright © 2019 Digital Confections LLC

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
/*#include <GDBStub.h> */
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFiMulti.h>
/*#include <ESP8266mDNS.h> */
#include <user_interface.h>
#include "esp8266.h"
/*#include <ArduinoOTA.h> */
#include <LittleFS.h> // LittleFS is declared
/*#include <WebSocketsClient.h> */
#include <Hash.h>
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>
#include <ESP8266WiFiType.h>
#include <time.h>
#include "Transmitter.h"
#include "Event.h"
/* #include <Wire.h> */
#include "Helpers.h"
#include "CircularStringBuff.h"

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
/*WiFiServer g_tcpServer(BRIDGE_TCP_PORT_DEFAULT.toInt());
  WiFiClient g_tcpServerClients[MAX_SRV_CLIENTS]; */

// String g_bridgeIPaddr = BRIDGE_IP_ADDR_DEFAULT;
///String g_bridgeSSID = BRIDGE_SSID_DEFAULT;
String g_bridgePW = BRIDGE_PW_DEFAULT;  /* minimum 8 characters */
// String g_bridgeTCPport = BRIDGE_TCP_PORT_DEFAULT;


/* Access Point */
String g_masterSSID = MASTER_SSID;
String g_masterSSID_PWD = BRIDGE_PW_DEFAULT;
bool g_masterEnabled = MASTER_ENABLE_DEFAULT;

/* Time Retrieval */
/*
    NIST Time Sync
    See https://www.arduino.cc/en/Tutorial/UdpNTPClient
    See http://tf.nist.gov/tf-cgi/servers.cgi
*/
// String g_timeHost = TIME_HOST_DEFAULT;
// String g_timeHTTPport = TIME_HTTP_PORT_DEFAULT;

/* UDP time query */
#define NTP_PACKET_SIZE 48                                  /* NTP time stamp is in the first 48 bytes of the message */
WiFiUDP g_UDP;                                              /* A UDP instance to let us send and receive packets over UDP */
unsigned int g_localPort = 2390;                            /* local port to listen for UDP  packets */

/* HTTP web server */
ESP8266WebServer g_http_server(80);                         /* HTTP server on port 80 */
WebSocketsServer g_webSocketServer = WebSocketsServer(81);  /* create a websocket server on port 81 */
String g_AP_NameString;
WebSocketsClient g_webSocketLocalClient;

typedef struct webSocketClient
{
  int8_t webID;
  int8_t socketID;
  String macAddr;
} WebSocketClient;

WebSocketClient g_webSocketClient[MAX_NUMBER_OF_WEB_CLIENTS];   /* Keep track of active clients and their MAC addresses */
uint8_t g_numberOfSocketClients = 0;
uint8_t g_numberOfWebClients = 0;
uint8_t g_socket_timeout = 0;

ESP8266WiFiMulti wifiMulti; /* Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti' */
File fsUploadFile;          /* a File variable to temporarily store the received file */

String g_softAP_IP_addr;

void handleRoot();          /* function prototypes for HTTP handlers */
void handleNotFound();

/*
    Main Program Support
*/

unsigned long g_relativeTimeSeconds;
unsigned long holdSeconds = 0;
unsigned long g_blinkTimeSeconds;
unsigned long g_lastAccessToNISTServers = 0;
bool g_timeWasSet = false;
int g_blinkPeriodMillis = 500;
bool g_justPoweredUp = true;

WebSocketSlaveState g_webSocketSlaveState = WSClientConnecting;
bool g_slave_connected = false;
bool g_slave_released = true;

#define NO_ACTIVITY_TIMEOUT (60 * 5)
int g_noActivityTimeoutSeconds = NO_ACTIVITY_TIMEOUT;

unsigned long g_timeOfDayFromTx = 0;

static WiFiEventHandler e1, e2;

Transmitter *g_xmtr = NULL;
Event* g_activeEvent = NULL;
String g_selectedEventName = "";
int g_activeEventIndex = 0;
EventFileRef g_eventList[20];
int g_eventsRead = 0;
int g_numberOfScheduledEvents = 0;
TxCommState g_ESP_Comm_State = TX_WAKE_UP;

CircularStringBuff *g_LBOutputBuff = NULL;
CircularStringBuff *g_fileDataBuff = NULL;
int g_linkBusAckPending = 0;
int g_linkBusAckTimeout = 10;

int g_saveEventToFile = 0;

bool populateEventFileList(void);
bool readDefaultsFile(void);
void saveDefaultsFile(void);
void showSettings(void);
void handleFileUpload(void);
void handleFileDownload(void);
void fileDelete(void);
void fileDeleteWithMessage(String msg);
void handleFileDelete(void);
void handleLBMessage(String message);
void handleFS(void);
bool checkUART(void);
bool sendLBMessages(void);


void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);

  pinMode(RED_LED, OUTPUT);       /* Allow the red LED to be controlled */
  pinMode(BLUE_LED, OUTPUT);      /* Initialize the BUILTIN_LED pin as an output */
  digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
  digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */

  startLittleFS();                  /* Start the LittleFS and list all contents */
  if (!readDefaultsFile())        /* Read default settings from file system */
  {
    saveDefaultsFile();
  }

  /* populateEventFileList(); */

  /*  showSettings(); */

#if WIFI_DEBUG_PRINTS_ENABLED
  WiFi.onEvent(eventWiFi);    /* Handle WiFi event */
#endif /* WIFI_DEBUG_PRINTS_ENABLED */

  g_xmtr = new Transmitter(g_debug_prints_enabled);

#define LB_OUTPUT_BUFF_SIZE 25
  g_LBOutputBuff = new CircularStringBuff(LB_OUTPUT_BUFF_SIZE);
}

/*******************************************************
    Handle WiFi events
 ********************************************************/
#if WIFI_DEBUG_PRINTS_ENABLED
void eventWiFi(WiFiEvent_t event)
{
  String e = String(event);

  switch (event)
  {
    case WIFI_EVENT_STAMODE_CONNECTED:
      {
        Serial.println(String("[WiFi] " + e + ", Connected"));
      }
      break;

    case WIFI_EVENT_STAMODE_DISCONNECTED:
      {
        Serial.println(String("[WiFi] " + e + ", Disconnected - Status " + String( WiFi.status()) + String(connectionStatus( WiFi.status() ).c_str()) ));
      }
      break;

    case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:
      {
        Serial.println(String("[WiFi] " + e + ", AuthMode Change"));
      }
      break;

    case WIFI_EVENT_STAMODE_GOT_IP:
      {
        Serial.println(String("[WiFi] " + e + ", Got IP"));
      }
      break;

    case WIFI_EVENT_STAMODE_DHCP_TIMEOUT:
      {
        Serial.println(String("[WiFi] " + e + ", DHCP Timeout"));
      }
      break;

    case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
      {
        Serial.println(String("[WiFi] " + e + ", Client Connected"));
      }
      break;

    case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
      {
        Serial.println(String("[AP] " + e + ", Client Disconnected"));
      }
      break;

    case WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED:
      {
        Serial.println(String("[AP] " + e + ", Probe Request Recieved"));
      }
      break;

    case WIFI_EVENT_ANY:
      {
        Serial.println(String("[AP] " + e + ", WIFI_EVENT_ANY Recieved"));
      }
      break;

    case WIFI_EVENT_MODE_CHANGE:
      {
        Serial.println(String("[AP] " + e + ", WIFI_EVENT_MODE_CHANGE Recieved"));
      }
      break;

    default:
      {
        /*    case WIFI_EVENT_MAX: */
        Serial.println(String("[AP] " + e + ", WIFI_EVENT_MAX Recieved"));
      }
      break;
  }
}
#endif  /* WIFI_DEBUG_PRINTS_ENABLED */


/*******************************************************
    WiFi Connection Status
 ********************************************************/
String connectionStatus( int which )
{
  switch ( which )
  {
    case WL_CONNECTED:
      {
        return ( "Connected");
      }
      break;

    case WL_NO_SSID_AVAIL:
      {
        return ( "Network not availible");
      }
      break;

    case WL_CONNECT_FAILED:
      {
        return ( "Wrong password");
      }
      break;

    case WL_IDLE_STATUS:
      {
        return ( "Idle status");
      }
      break;

    case WL_DISCONNECTED:
      {
        return ( "Disconnected");
      }
      break;

    default:
      {
        return ( "Unknown");
      }
      break;
  }
}

/*===============================================================
   This routine is executed when you open its IP in browser
  =============================================================== */
void handleRoot()
{
  g_http_server.send(200, "text/html", "<h2 style=\"font-family:verdana; font-size:30px; color:Black; text-align:left;\">Options</h2><p style=\"font-family:verdana; font-size:20px; color:Black; text-align:left;\">Configure events: <a href=\"/events.html\">73.73.73.73/events.html</a></p> <p style=\"font-family:verdana; font-size:20px; color:Black; text-align:left;\">Upload a file: <a href=\"/upload.html\">73.73.73.73/upload.html</a></p> <p style=\"font-family:verdana; font-size:20px; color:Black; text-align:left;\">Download a file: <a href=\"/fs.html\">73.73.73.73/fs.html</a></p> <p style=\"font-family:verdana; font-size:20px; color:Black; text-align:left;\">Delete a file: <a href=\"/fileDelete.html\">73.73.73.73/fileDelete.html</a> <- Use with caution!</p> <p style=\"font-family:verdana; font-size:20px; color:Black; text-align:left;\">Testing support: <a href=\"/test.html\">73.73.73.73/test.html</a></p> <p style=\"font-size:12\">ESP8266 SW Date: 22-Jan-2020</p> ");
}

void handleFS()
{
  String message = "<p style=\"text-align:left;\"><a href=\\ \"73.73.73.73\">[HOME]</a></p> <h2 style=\"font-family:verdana; font-size:30px; color:Black; text-align:left;\">File System Contents</h2>";
  String line;
  Dir dir = LittleFS.openDir("/");

  while (dir.next())
  { /* List the file system contents */
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    line = String("<p>" + fileName + ", size: " + formatBytes(fileSize) + "</p>");
    message += line;
  }

  message += "<form method=\"post\" enctype=\"multipart/form-data\">\r\n";
  message += "File name:<input type=\"text\" name=\"name\">\r\n";
  message += "<input class=\"button\" type=\"submit\" value=\"Download\">\r\n";
  message += "</form>";

  g_http_server.send(200, "text/html", message);
}

void handleUpload()
{
  String message = "<p style=\"text-align:left;\"><a href=\\ \"73.73.73.73\">[HOME]</a></p><form method=\"post\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"name\"><input class=\"button\" type=\"submit\" value=\"Upload\"></form>";

  g_http_server.send(200, "text/html", message);
}

void handleSuccess()
{
  String message = "<html><head><title>Success</title></head><body><p><a href=\\ \"73.73.73.73\">[HOME]</a></p><h1 style=\"font-family:verdana;font-size:30px;color:Black;text-align:left;\">File uploaded successfully!</h1></body></html>";

  g_http_server.send(200, "text/html", message);
}

void fileDelete()
{
  fileDeleteWithMessage("");
}

void fileDeleteWithMessage(String msg)
{
  String message = "<p style=\"text-align:left;\"><a href=\\ \"73.73.73.73\">[HOME]</a></p> <h2 style=\"font-family:verdana; font-size:30px; color:Black; text-align:left;\">File System Contents</h2>";
  String line;
  Dir dir = LittleFS.openDir("/");

  while (dir.next())
  { /* List the file system contents */
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    line = String("<p>" + fileName + ", size: " + formatBytes(fileSize) + "</p>");
    message += line;
  }

  message += "<form method=\"post\" enctype=\"multipart/form-data\">\r\n";
  message += "File name:<input type=\"text\" name=\"name\">\r\n";
  message += "<input class=\"button\" type=\"submit\" value=\"Delete\">\r\n";
  message += "</form>";

  if (msg.length() > 0)
  {
    message += msg;
  }

  g_http_server.send(200, "text/html", message);
}

void handleNotFound()
{ /* if the requested file or page doesn't exist, return a 404 not found error */
  if (!handleFileRead(g_http_server.uri()))
  { /* check if the file exists in the flash memory (LittleFS), if so, send it */
    String message = "<p style=\"text-align:left;\"><a href=\\ \"73.73.73.73\">[HOME]</a></p><p>File Not Found</p>";
    message += "URI: ";
    message += g_http_server.uri();
    message += "\nMethod: ";
    message += (g_http_server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += g_http_server.args();
    message += "\n";
    for (uint8_t i = 0; i < g_http_server.args(); i++)
    {
      message += " " + g_http_server.argName(i) + ": " + g_http_server.arg(i) + "\n";
    }
    g_http_server.send(404, "text/html", message);
  }
  else
  {
    String filename = String(g_http_server.uri());

    if (g_debug_prints_enabled)
    {
      Serial.println("File read:" + filename);
    }

    /*    if (filename.indexOf("events.html") >= 0)
          {
            g_ESP_Comm_State = TX_HTML_PAGE_SERVED;
          } */
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
  /* Event subscription */
  e1 = WiFi.onSoftAPModeStationConnected(onNewStation);
  e2 = WiFi.onSoftAPModeStationDisconnected(onStationDisconnect);

  /*  WiFi.mode(WIFI_AP); */

  if (g_masterEnabled)
  {
    g_AP_NameString = MASTER_SSID;
  }
  else
  {
    /* Create a somewhat unique SSID */
    uint8_t mac[WL_MAC_ADDR_LENGTH];
    WiFi.softAPmacAddress(mac);
    String macID = String(mac[WL_MAC_ADDR_LENGTH - 4], HEX) +
                   String(mac[WL_MAC_ADDR_LENGTH - 3], HEX) +
                   String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                   String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
    macID.toUpperCase();
    g_AP_NameString = String("Tx_" + macID);
  }

  IPAddress apIP = stringToIP(g_softAP_IP_addr);

  if (apIP == IPAddress(-1, -1, -1, -1))
  {
    apIP = stringToIP(String(SOFT_AP_IP_ADDR_DEFAULT)); /* some reasonable default address */
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
    IPAddress myIP = WiFi.softAPIP();   /*Get IP address */

    /*    if (MDNS.begin(stringObjToConstCharString(&g_mDNS_responder)))
          { // Start the mDNS responder for e.g., "esp8266.local"
            Serial.println("mDNS responder started: " + g_mDNS_responder + ".local");
          }
          else
          {
            Serial.println("Error setting up MDNS responder!");
          } */

    /* Start TCP listener on port TCP_PORT */
    g_http_server.on("/", HTTP_GET, handleRoot);                        /* Call the 'handleRoot' function when a client requests URI "/" */

    g_http_server.on("/fs", HTTP_GET, handleFS);                        /* Provide utility to help manage the file system */
    g_http_server.on("/fs.html", HTTP_GET, handleFS);                   /* Provide utility to help manage the file system */

    g_http_server.on("/fs", HTTP_POST, handleFileDownload);             /* go to 'handleFileDownload' */
    g_http_server.on("/fs.html", HTTP_POST, handleFileDownload);        /* go to 'handleFileDownload' */

    g_http_server.on("/fileDelete", HTTP_GET, fileDelete);              /* Provide utility to help manage the file system */
    g_http_server.on("/fileDelete.html", HTTP_GET, fileDelete);         /* Provide utility to help manage the file system */

    g_http_server.on("/fileDelete", HTTP_POST, handleFileDelete);       /* go to 'handleFileDelete' */
    g_http_server.on("/fileDelete.html", HTTP_POST, handleFileDelete);  /* go to 'handleFileDelete' */

    g_http_server.on("/upload", HTTP_GET, handleUpload);
    g_http_server.on("/upload.html", HTTP_GET, handleUpload);

    g_http_server.on("/upload.html", HTTP_POST, [] ()
    { /* If a POST request is sent to the /upload.html address, */
      g_http_server.send(200, "text/plain", "");
    }, handleFileUpload);                       /* go to 'handleFileUpload' */

    g_http_server.on("/upload", HTTP_POST, [] ()
    { /* If a POST request is sent to the /upload.html address, */
      g_http_server.send(200, "text/plain", "");
    }, handleFileUpload);                       /* go to 'handleFileUpload' */

    g_http_server.onNotFound(handleNotFound);   /* When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound" */
    g_http_server.begin();

    /*    startWebSocketServer(); // Start a WebSocket server */

    if (g_debug_prints_enabled)
    {
      Serial.print(g_AP_NameString + " server started: ");
      Serial.println(myIP);
    }
  }

  return (success);
}


/* Manage incoming device connection on ESP access point */
void onNewStation(WiFiEventSoftAPModeStationConnected sta_info)
{
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
      if (g_debug_prints_enabled)
      {
        Serial.println("Station found in existing list.");
      }
      g_webSocketClient[i].webID = sta_info.aid;
      g_webSocketClient[i].socketID = WEBSOCKETS_SERVER_CLIENT_MAX;
      break;
    }
  }

  g_numberOfWebClients = WiFi.softAPgetStationNum();
  g_numberOfSocketClients = g_webSocketServer.connectedClients(false);

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

    for (int i = 0; i < g_numberOfSocketClients; i++)
    {
      Serial.printf("%d. WebSocketClient: WebID# %d. MAC address : %s\n", i, g_webSocketClient[i].webID, (g_webSocketClient[i].macAddr).c_str());
    }
  }

  g_noActivityTimeoutSeconds = NO_ACTIVITY_TIMEOUT;
  startWebSocketServer(); /* Start a WebSocket server */
}

void onStationDisconnect(WiFiEventSoftAPModeStationDisconnected sta_info)
{
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
      if (g_debug_prints_enabled)
      {
        Serial.println("Station removed from list.");
      }
      break;
    }
  }

  g_numberOfWebClients = WiFi.softAPgetStationNum();
  g_numberOfSocketClients = g_webSocketServer.connectedClients(false);
  /* g_LBOutputBuff->put(LB_MESSAGE_ESP_RETAINPOWER); */

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


bool teardownWiFiAPConnection()
{
  bool err = !WiFi.softAPdisconnect(true);
  return err;
}


bool setupWiFiAPConnection()
{
  bool err = false;
  int tries = 0;

  if (g_LEDs_enabled)
  {
    digitalWrite(RED_LED, LOW);    /* Turn off red LED */
    digitalWrite(BLUE_LED, LOW); /* Turn off blue LED */
  }

  /* We start by connecting to a WiFi network */

#ifdef MULTI_ACCESS_POINT_SUPPORT
  if (strlen(stringObjToConstCharString(&g_masterSSID_PWD)) > 0)
  {
    wifiMulti.addAP(stringObjToConstCharString(&g_masterSSID), stringObjToConstCharString(&g_masterSSID_PWD));    /* add Wi-Fi networks you want to connect to */
  }
  else
  {
    wifiMulti.addAP(stringObjToConstCharString(&g_masterSSID));
  }

  // if (g_debug_prints_enabled)
  {
    Serial.println("Connecting ...");
  }

  while (wifiMulti.run() != WL_CONNECTED)
  {
    delay(250);
    //   if (g_debug_prints_enabled)
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
    Serial.println(g_masterSSID);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect();
    delay(2500);
  }

  if (g_masterSSID_PWD.length() < 1)
  {
    WiFi.begin(stringObjToConstCharString(&g_masterSSID);
  }
  else
  {
    WiFi.begin(stringObjToConstCharString(&g_masterSSID), stringObjToConstCharString(&g_masterSSID_PWD));
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
      err = true;
      break;
    }
    else if ((tries == 20) || (tries == 40))
    {
      if (g_hotspotPW.length() < 1)
      {
        WiFi.begin(stringObjToConstCharString(&g_masterSSID));
      }
      else
      {
        WiFi.begin(stringObjToConstCharString(&g_masterSSID), stringObjToConstCharString(&g_masterSSID_PWD));
      }
    }
  }
#endif  /* MULTI_ACCESS_POINT_SUPPORT */


  if (err)
  {
    //    if (g_debug_prints_enabled)
    {
      Serial.println('\n');
      Serial.println("Unable to find master tx.");
    }
  }
  else
  {
    if (g_LEDs_enabled)
    {
      digitalWrite(BLUE_LED, LOW);    /* Turn on blue LED */
    }

    //   if (g_debug_prints_enabled)
    {
      Serial.println('\n');
      Serial.print("Connected to ");
      Serial.println(WiFi.SSID());    /* Tell us what network we're connected to */
      Serial.print("IP address:\t");
      Serial.println(WiFi.localIP()); /* Send the IP address of the ESP8266 to the computer */
    }
  }

  return (err);
}


void loop()
{
  bool toggle = false;
  unsigned long holdTime = 0;
  String arg1, arg2;

  g_debug_prints_enabled = DEBUG_PRINTS_ENABLE_DEFAULT;   /* kluge override of saved settings */

  /* HTTP Server */
  if (g_debug_prints_enabled)
  {
    Serial.println("Starting loop()...");
  }

  digitalWrite(RED_LED, HIGH);    /* Turn off red LED */


  if (!g_masterEnabled)
  {
    if (!setupWiFiAPConnection())
    {
      File tempFile;
      String updatedFileName;
      String temp = String("");

      g_webSocketLocalClient.begin("73.73.73.73", 81);
      g_webSocketLocalClient.onEvent(webSocketClientEvent);

      Serial.println("Client started\n");

      static unsigned long last = 0;
      g_slave_released = true;
      String msg;
      bool busy = true;
      int times2try = 5;
      holdTime = 0;
      g_blinkPeriodMillis = 500;

      while (busy)
      {
        if (g_LEDs_enabled)
        {
          g_blinkTimeSeconds = millis() / g_blinkPeriodMillis;
          if (holdTime != g_blinkTimeSeconds)
          {
            holdTime = g_blinkTimeSeconds;
            toggle = !toggle;
            digitalWrite(RED_LED, toggle);  /* Blink red LED */
            digitalWrite(BLUE_LED, toggle);  /* Blink red LED */
          }
        }
        else
        {
          digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
          digitalWrite(BLUE_LED, HIGH); /* Turn off blue LED */
        }

        g_webSocketLocalClient.loop();
        checkUART();
        sendLBMessages();
        yield();

        switch (g_webSocketSlaveState)
        {
          case WSClientConnecting:
            {
              if (g_slave_released && times2try)
              {
                if (abs(millis() - last) > 1000)
                {
                  msg = String(SOCK_COMMAND_SLAVE) + "," + SLAVE_CONNECT;
                  g_webSocketLocalClient.sendTXT(stringObjToConstCharString(&msg)); /* Connect to Master */
                  last = millis();
                  times2try--;
                }
              }
              else
              {
                if (!g_slave_released)
                {
                  times2try = 5;
                  g_webSocketSlaveState = WSClientSyncClock;
                  Serial.println("WSc: Active");
                }
                else
                {
                  g_webSocketSlaveState = WSClientClose;
                  Serial.println("WSc: failed connection");
                }
              }
            }
            break;

          case WSClientSyncClock:
            {
              if (g_slave_released)
              {
                g_webSocketSlaveState = WSClientClose;
                Serial.println("WSc: failed connection");
              }
              else
              {
                times2try = 5;
                /* Wait until an incoming sync message changes the state */
              }
            }
            break;

          case WSClientWaitForSyncAck:
            {
              if (g_slave_released)
              {
                g_webSocketSlaveState = WSClientClose;
                Serial.println("WSc: failed connection");
              }
              else if (!g_linkBusAckPending)
              {
                msg = String(SOCK_COMMAND_SLAVE) + "," + SLAVE_WAITING_FOR_UPDATES;
                g_webSocketLocalClient.sendTXT(stringObjToConstCharString(&msg)); /* Connect to Master */
                g_webSocketSlaveState = WSClientWaitForUpdates;
                times2try = 5;
              }
              else
              {
                if (times2try)
                {
                  if (abs(millis() - last) > 1000)
                  {
                    last = millis();
                    times2try--;
                  }
                }
                else
                {
                  g_webSocketSlaveState = WSClientClose;
                  Serial.println("WSc: ACK timeout");
                }
              }
            }
            break;

          case WSClientWaitForUpdates:
            {
              if (g_slave_released)
              {
                g_webSocketSlaveState = WSClientClose;
                Serial.println("WSc: failed connection");
              }
              else
              {
                if (times2try)
                {
                  if (abs(millis() - last) > 1000)
                  {
                    last = millis();
                    times2try--;
                  }
                }
                else
                {
                  g_webSocketSlaveState = WSClientClose;
                  Serial.println("WSc: Update timeout");
                }
              }
            }
            break;

          case WSClientReceiveFileData:
            {
              if (g_slave_released)
              {
                g_webSocketSlaveState = WSClientClose;
                Serial.println("WSc: failed connection");
              }
              else
              {
                if (tempFile && g_fileDataBuff)
                {
                  if (g_fileDataBuff->empty())
                  {
                    Serial.println("MT");
                    if (times2try)
                    {
                      if (abs(millis() - last) > 1000)
                      {
                        last = millis();
                        times2try--;
                      }
                    }
                    else
                    {
                      g_webSocketSlaveState = WSClientClose;
                      Serial.println("WSc: Timeout waiting for data");
                    }
                  }
                  else
                  {
                    temp = g_fileDataBuff->get();
                    Serial.println(String("l: " + temp));
                    if (temp.length() > 0)
                    {
                      tempFile.println(temp);
                    }
                  }

                  if (temp.equals("EOF"))
                  {
                    g_webSocketSlaveState = WSClientClose;
                  }
                }
                else
                {
                  Serial.println("File or buff error!");
                }
              }
            }
            break;

          case WSClientPrepForFileData:
            {
              if (g_slave_released)
              {
                g_webSocketSlaveState = WSClientClose;
                Serial.println("WSc: failed connection");
              }
              else
              {
                if (g_fileDataBuff == NULL)
                {
                  g_fileDataBuff = new CircularStringBuff(2 + EVENT_FILE_DATA_SIZE);
                }

                if (g_fileDataBuff)
                {
                  if (!tempFile)
                  {
                    tempFile = LittleFS.open("/Temp", "w");
                  }

                  if (tempFile)
                  {
                    g_webSocketSlaveState = WSClientReceiveFileData;
                    msg = String(SOCK_COMMAND_SLAVE) + "," + SLAVE_READY_FOR_FILE;
                    g_webSocketLocalClient.sendTXT(stringObjToConstCharString(&msg)); /* Send to Master */
                    times2try = 5;
                    Serial.println("File ready...");
                  }
                  else
                  {
                    g_webSocketSlaveState = WSClientClose;
                  }
                }
                else
                {
                  Serial.println("File data buff error!");
                }
              }
            }
            break;

          default:
            //  case WSClientClose:
            {
              /* Close web session */
              g_webSocketLocalClient.disconnect();
              teardownWiFiAPConnection();
              if (g_fileDataBuff)
              {
                delete g_fileDataBuff;
              }

              if (tempFile)
              {
                tempFile.close();
              }

              WiFi.disconnect();
              g_slave_released = true;

              Serial.println("WSc: Closed");
              busy = false;
            }
            break;
        }
      }
    }
  }

  if (setupHTTP_AP())
  {
    httpWebServerLoop();
  }

  digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */
}


void httpWebServerLoop()
{
  bool done = false;
  File openFile;
  bool toggle = false;
  bool firstPageLoad = true;
  unsigned long holdTime = 0;
  int holdWebClients = 0;
  int holdWebSocketClients = 0;

  int serialIndex = 0;
  int role = 0, tx = 0;
  TxDataType* txData = NULL;
  String msgOut;

  bool progButtonPressed = false;
  bool sentComsOFF = false;
  int debounceProgButton = 3;
  uint8_t i;

  if (g_debug_prints_enabled)
  {
    Serial.println("httpWebServerLoop() started");
  }

  g_numberOfWebClients = WiFi.softAPgetStationNum();

  if (g_numberOfWebClients > 0)
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("Stations already connected:" + String(g_numberOfWebClients)));
      Serial.println(String("Existing socket connections:" + String(g_numberOfSocketClients)));
    }
    /*    WiFi.disconnect();
          ESP.restart();
          WiFi.mode(WIFI_OFF); */
    WiFi.mode(WIFI_STA);
    WiFi.mode(WIFI_AP_STA);
    /* Event subscription */
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
    g_numberOfSocketClients = g_webSocketServer.connectedClients(false);

    if ((g_numberOfWebClients != holdWebClients) || (g_numberOfSocketClients != holdWebSocketClients ))
    {
      holdWebClients = g_numberOfWebClients;
      holdWebSocketClients = g_numberOfSocketClients;

      if (g_debug_prints_enabled)
      {
        Serial.println("Web Clients: " + String(g_numberOfWebClients));
        Serial.println("Socket Clients: " + String(g_numberOfSocketClients));
      }

      if (g_numberOfWebClients == 0)
      {
        g_webSocketServer.disconnect(); /* ensure all web socket clients are disconnected - this might not happen if WiFi connection was broken */
      }
      //			else
      //			{
      //				g_noActivityTimeoutSeconds = NO_ACTIVITY_TIMEOUT; // keep alive while a station is connected
      //			}
    }

    done = checkUART();

    g_relativeTimeSeconds = millis() / 1000;

    if (g_relativeTimeSeconds != holdSeconds)
    {
      holdSeconds = g_relativeTimeSeconds;

      if (g_numberOfWebClients)
      {
        if (!(holdSeconds % 25))   /* Ask ATMEGA to wait longer before shutting down WiFi */
        {
          g_LBOutputBuff->put(LB_MESSAGE_ESP_KEEPALIVE);
        }
      }

      if (!g_slave_connected) /* Never timeout if a slave is connected */
      {
        if (g_noActivityTimeoutSeconds)
        {
          g_noActivityTimeoutSeconds--;

          if (!g_noActivityTimeoutSeconds)
          {
            g_LBOutputBuff->put(LB_MESSAGE_ESP_SHUTDOWN);   /* shut down immediately */
          }
        }
      }

      if (!g_slave_connected) /* Never timeout if a slave is connected */
      {
        if (g_socket_timeout)
        {
          g_socket_timeout--;

          if (!g_socket_timeout)
          {
            if (g_debug_prints_enabled)
            {
              Serial.println("Socket TO!");
            }

            g_webSocketServer.disconnect(); /* ensure all web socket clients are disconnected */
          }
        }
      }
    }

    g_blinkTimeSeconds = millis() / g_blinkPeriodMillis;

    if (holdTime != g_blinkTimeSeconds)
    {
      holdTime = g_blinkTimeSeconds;
      toggle = !toggle;

      if (!sendLBMessages())
      {
        if (!g_linkBusAckTimeout && g_linkBusAckPending)
        {
          g_linkBusAckPending = 0;
          if (g_debug_prints_enabled)
          {
            Serial.println("ACK Timeout");
          }
        }

        if (!g_linkBusAckPending)
        {
          g_blinkPeriodMillis = 500;
        }
      }

      if (progButtonPressed)
      {
        digitalWrite(BLUE_LED, LOW);    /* Turn on blue LED */
        digitalWrite(RED_LED, LOW);     /* Turn on red LED */
      }
      else if (!g_LEDs_enabled)
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
          /*          Serial.println("web sockets: " + String(g_numberOfSocketClients)); */
          if (toggle)
          {
            pinMode(RED_LED, INPUT);    /* Allow GPIO0 to be read */
            if (!digitalRead(RED_LED))
            {
              debounceProgButton--;
              if (!debounceProgButton)
              {
                progButtonPressed = true;
                debounceProgButton = 3;
              }
            }
            else
            {
              debounceProgButton = 3;
            }

            pinMode(RED_LED, OUTPUT);
          }
        }
        else if (g_numberOfWebClients)
        {
          digitalWrite(BLUE_LED, toggle); /* Blink blue LED */
          /*   digitalWrite(RED_LED, HIGH); / * Turn off red LED * /
                      Serial.println("web clients: " + String(g_numberOfWebClients)); */

          digitalWrite(RED_LED, LOW);     /* Turn on red LED */
          pinMode(RED_LED, INPUT);        /* Allow GPIO0 to be read */
          if (!digitalRead(RED_LED))
          {
            debounceProgButton--;
            if (!debounceProgButton)
            {
              progButtonPressed = true;
              debounceProgButton = 3;
            }
          }
          else
          {
            debounceProgButton = 3;
          }

          pinMode(RED_LED, OUTPUT);
          digitalWrite(RED_LED, HIGH);    /* Turn off red LED */
        }
        else
        {
          digitalWrite(RED_LED, toggle);  /* Blink red LED */
          digitalWrite(BLUE_LED, HIGH);   /* Turn off blue LED */

          if (!toggle)
          {
            pinMode(RED_LED, INPUT);    /* Allow GPIO0 to be read */
            if (!digitalRead(RED_LED))
            {
              debounceProgButton--;
              if (!debounceProgButton)
              {
                progButtonPressed = true;
                debounceProgButton = 3;
              }
            }
            else
            {
              debounceProgButton = 3;
            }

            pinMode(RED_LED, OUTPUT);
          }
        }

        if (progButtonPressed)
        {
          if (!sentComsOFF)
          {
            Serial.printf(LB_MESSAGE_WIFI_COMS_OFF);    /* send immediate */
            sentComsOFF = true;
          }
        }

        if (g_linkBusAckTimeout)
        {
          g_linkBusAckTimeout--;
        }
      }

      if (!g_slave_connected)
      {
        if (g_numberOfSocketClients)
        {
          if (!(holdTime % 61))
          {
            g_LBOutputBuff->put(LB_MESSAGE_TEMP_REQUEST);
          }
          else if (!(holdTime % 17))
          {
            g_LBOutputBuff->put(LB_MESSAGE_BATTERY_REQUEST);
          }
        }
      }
    }

    switch (g_ESP_Comm_State)
    {
      case TX_WAKE_UP:
        {
          /* Inform the ATMEGA that WiFi power up is complete */
          g_ESP_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
          g_LBOutputBuff->put(LB_MESSAGE_ESP_WAKEUP);
          g_linkBusAckTimeout = 10;
        }
        break;

      case TX_INITIAL_TIME_RECEIVED:
        {
          g_LBOutputBuff->put(LB_MESSAGE_ESP_KEEPALIVE);
          g_ESP_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
        }
        break;

      case TX_READ_ALL_EVENTS_FILES:
        {
          /*					if(g_debug_prints_enabled) */
          /*					{ */
          /*						Serial.println("S=TX_READ_ALL_EVENTS_FILES"); */
          /*					} */
          populateEventFileList();
          g_numberOfScheduledEvents = numberOfEventsScheduled(g_timeOfDayFromTx);
          g_ESP_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
        }
        break;

      case TX_HTML_SAVE_CHANGES:
        {
          /*					if(g_debug_prints_enabled) */
          /*					{ */
          /*						Serial.println("S=TX_HTML_SAVE_CHANGES"); */
          /*					} */
          if (g_activeEvent != NULL)
          {
            Serial.printf(LB_MESSAGE_ESP_RETAINPOWER);
            g_socket_timeout = 10;  /* allow extra time before declaring socket dead */
          }
          g_ESP_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
        }
        break;

      case TX_HTML_REFRESH_EVENTS:
        {
          /*					if(g_debug_prints_enabled) */
          /*					{ */
          /*						Serial.println("S=TX_HTML_REFRESH_EVENTS"); */
          /*					} */
          if (g_activeEvent != NULL)
          {
            int tries = 10;
            bool fail = true;

            while (tries && fail)
            {
              tries--;
              fail = g_activeEvent->writeEventFile();    /* save any changes made to the active event */
            }
          }
          populateEventFileList();                /* read any values that might have changed into the event list */
          g_numberOfScheduledEvents = numberOfEventsScheduled(g_timeOfDayFromTx);
          /* order the list appropriately */

          if (g_eventsRead)
          {
            if (g_activeEvent == NULL)
            {
              g_activeEvent = new Event(g_debug_prints_enabled);
              g_activeEventIndex = 0;
              g_activeEvent->readEventFile(g_eventList[0].path);
            }

            /* Send commands to refresh the HTML table of events
               Send a command to reselect the appropriate event table row */

            if (g_eventsRead)
            {
              if ((g_selectedEventName != "") && (g_activeEvent != NULL))
              {
                bool found = false;

                for (int i = 0; i < g_eventsRead; i++)
                {
                  if (g_selectedEventName.equals(g_eventList[i].ename))
                  {
                    g_activeEventIndex = i;
                    found = true;
                    break;
                  }
                }

                if (!found)
                {
                  g_activeEventIndex = (g_activeEventIndex + 1) % g_eventsRead;
                }

                g_activeEvent->readEventFile(g_eventList[g_activeEventIndex].path);
              }
              else
              {
                if (g_activeEvent == NULL)
                {
                  g_activeEventIndex = 0;
                  g_activeEvent = new Event(g_debug_prints_enabled);
                  g_activeEvent->readEventFile(g_eventList[0].path);
                }
                else if (!firstPageLoad)
                {
                  g_activeEventIndex = (g_activeEventIndex + 1) % g_eventsRead;
                  g_activeEvent->readEventFile(g_eventList[g_activeEventIndex].path);
                }
                else
                {
                  firstPageLoad = false;
                }
              }

              String msg;
              for (int i = 0; i < g_eventsRead; i++)
              {
                msg = String(String(SOCK_COMMAND_REFRESH) + "," + g_eventList[i].ename + "," + g_eventList[i].vers + "," +  g_eventList[i].startDateTimeEpoch + "," +  g_eventList[i].finishDateTimeEpoch + "," + g_eventList[i].role + "," + g_eventList[i].callsign + "," + g_eventList[i].power + "," + g_eventList[i].freq);
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
              }

              msg = String(String(SOCK_COMMAND_REFRESH) + ",Done");
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());

              g_http_server.handleClient();
              g_webSocketServer.loop();

              for (int i = 0; i < g_activeEvent->getEventNumberOfTxTypes(); i++)
              {
                msg = String(String(SOCK_COMMAND_TYPE_FREQ) + "," + String(g_activeEvent->getFrequencyForRole(i)) + "," + g_activeEvent->getRolename(i));
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                g_webSocketServer.loop();
              }

              g_http_server.handleClient();


              for (int i = 0; i < g_activeEvent->getEventNumberOfTxTypes(); i++)
              {
                msg = String(String(SOCK_COMMAND_TYPE_PWR) + "," + String(g_activeEvent->getPowerlevelForRole(i)) + "," + g_activeEvent->getRolename(i));
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                g_webSocketServer.loop();
              }

              g_http_server.handleClient();

              msg = String(String(SOCK_COMMAND_TX_ROLE) + "," + g_activeEvent->getTxAssignment());
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
              if (g_debug_prints_enabled)
              {
                Serial.println(msg);
              }
            }
            else if (g_debug_prints_enabled)
            {
              Serial.println("No events found!");
            }
          }

          g_ESP_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
        }
        break;

      case TX_HTML_PAGE_SERVED:
        {
          /*					if(g_debug_prints_enabled) */
          /*					{ */
          /*						Serial.println("S=TX_HTML_PAGE_SERVED"); */
          /*					} */
          firstPageLoad = true;

          /*            populateEventFileList();
                        g_numberOfScheduledEvents = numberOfEventsScheduled(g_timeOfDayFromTx); */

          /*            if (g_eventsRead) {
                          if (g_activeEvent == NULL) g_activeEvent = new Event(g_debug_prints_enabled);
                          g_activeEventIndex = 0;
                          g_activeEvent->readEventFile(g_eventList[0].path);
                        } */
        }
      /* Intentional Fall-through
         break; */

      case TX_HTML_NEXT_EVENT:
        {
          /*					if(g_debug_prints_enabled) */
          /*					{ */
          /*						Serial.println("S=TX_HTML_NEXT_EVENT"); */
          /*					} */
          if (g_activeEvent != NULL)
          {
            int tries = 10;
            bool fail = true;

            while (tries && fail)
            {
              tries--;
              fail = g_activeEvent->writeEventFile();    /* save any changes made to the active event */
            }
          }
          populateEventFileList();                /* read any values that might have changed into the event list */
          g_numberOfScheduledEvents = numberOfEventsScheduled(g_timeOfDayFromTx);

          if (firstPageLoad)
          {
            if (g_eventsRead)
            {
              if (g_activeEvent == NULL)
              {
                g_activeEvent = new Event(g_debug_prints_enabled);
              }
              g_activeEventIndex = 0;
              g_activeEvent->readEventFile(g_eventList[0].path);
            }
          }

          if (g_numberOfSocketClients)
          {
            String msg;

            g_http_server.handleClient();
            g_webSocketServer.loop();

            if (g_eventsRead)
            {
              if ((g_selectedEventName != "") && (g_activeEvent != NULL))
              {
                bool found = false;

                for (int i = 0; i < g_eventsRead; i++)
                {
                  if (g_selectedEventName.equals(g_eventList[i].ename))
                  {
                    g_activeEventIndex = i;
                    found = true;
                    break;
                  }
                }

                if (!found)
                {
                  g_activeEventIndex = (g_activeEventIndex + 1) % g_eventsRead;
                }

                g_activeEvent->readEventFile(g_eventList[g_activeEventIndex].path);
              }
              else
              {
                if (g_activeEvent == NULL)
                {
                  g_activeEventIndex = 0;
                  g_activeEvent = new Event(g_debug_prints_enabled);
                  g_activeEvent->readEventFile(g_eventList[0].path);
                }
                else if (!firstPageLoad)
                {
                  g_activeEventIndex = (g_activeEventIndex + 1) % g_eventsRead;
                  g_activeEvent->readEventFile(g_eventList[g_activeEventIndex].path);
                }
                else
                {
                  firstPageLoad = false;
                }
              }

              String msg = String(String(SOCK_COMMAND_EVENT_NAME) + "," + g_activeEvent->getEventName() );
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
              if (g_debug_prints_enabled)
              {
                Serial.println(msg);
              }

              g_http_server.handleClient();
              g_webSocketServer.loop();

              for (int i = 0; i < g_eventsRead; i++)
              {
                //								msg = String(String(SOCK_COMMAND_EVENT_DATA) + "," + g_eventList[i].ename + "," + g_eventList[i].vers + "," +  g_eventList[i].startDateTimeEpoch + "," +  g_eventList[i].finishDateTimeEpoch + "," + g_eventList[i].role + "," + g_eventList[i].callsign + "," + g_eventList[i].power + "," + g_eventList[i].freq);

                msg = String(String(SOCK_COMMAND_EVENT_DATA) + "," + g_eventList[i].ename + "," + g_eventList[i].vers + "," +  g_eventList[i].startDateTimeEpoch + "," +  g_eventList[i].finishDateTimeEpoch + ",*," + g_eventList[i].callsign + ",*,*");
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
              }

              g_http_server.handleClient();
              g_webSocketServer.loop();

              for (int i = 0; i < g_activeEvent->getEventNumberOfTxTypes(); i++)
              {
                for (int j = 0; j < g_activeEvent->getNumberOfTxsForRole(i); j++)
                {
                  String role_tx = String(String(i) + ":" + String(j));
                  msg = String(String(SOCK_COMMAND_TYPE_NAME) + "," + g_activeEvent->getTxDescriptiveName(role_tx) + "," + role_tx);
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                  g_webSocketServer.loop();
                  g_http_server.handleClient();
                }
              }

              msg = String(String(SOCK_COMMAND_TYPE_NAME) + ",Done,Done");
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
              g_webSocketServer.loop();
              g_http_server.handleClient();

              for (int i = 0; i < g_activeEvent->getEventNumberOfTxTypes(); i++)
              {
                msg = String(String(SOCK_COMMAND_TYPE_FREQ) + "," + String(g_activeEvent->getFrequencyForRole(i)) + "," + g_activeEvent->getRolename(i));
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                g_webSocketServer.loop();
              }

              g_http_server.handleClient();


              for (int i = 0; i < g_activeEvent->getEventNumberOfTxTypes(); i++)
              {
                msg = String(String(SOCK_COMMAND_TYPE_PWR) + "," + String(g_activeEvent->getPowerlevelForRole(i)) + "," + g_activeEvent->getRolename(i));
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
                g_webSocketServer.loop();
              }

              g_http_server.handleClient();

              msg = String(String(SOCK_COMMAND_TX_ROLE) + "," + g_activeEvent->getTxAssignment());
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
              if (g_debug_prints_enabled)
              {
                Serial.println(msg);
              }
            }
            else if (g_debug_prints_enabled)
            {
              Serial.println("No events found!");
            }
          }
          g_ESP_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
        }
        break;

      case TX_RECD_START_EVENT_REQUEST:
        {
          g_http_server.handleClient();
          g_webSocketServer.loop();
        yield();

          /*
               Configure the ATMEGA appropriately for its role in the scheduled event.
          */
          switch (serialIndex)
          {
            case 0: /* Prepare ATMEGA to receive event data */
              {
                g_blinkPeriodMillis = 100;
                g_LBOutputBuff->put(LB_MESSAGE_KEY_UP);
              }
              break;

            case 1: /* send finish time */
              {
                tx = g_activeEvent->getTxSlotIndex();
                role = g_activeEvent->getTxRoleIndex();

                if ((tx >= 0) && (role >= 0))
                {
                  txData = g_activeEvent->getTxData(role, tx);
                }

                /* Finish time should be sent first */
                msgOut = String(LB_MESSAGE_STARTFINISH_SET_FINISH + String(convertTimeStringToEpoch(g_activeEvent->getEventFinishDateTime())) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 2: /* Message pattern */
              {
                msgOut = String(LB_MESSAGE_PATTERN_SET + (*txData).pattern + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 3: /* Off time */
              {
                msgOut = String(LB_MESSAGE_TIME_INTERVAL_SET0 + String((*txData).offTime) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 4: /* On time */
              {
                msgOut = String(LB_MESSAGE_TIME_INTERVAL_SET1 + String((*txData).onTime) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 5: /* Offset time */
              {
                msgOut = String(LB_MESSAGE_TIME_INTERVAL_SETD + String((*txData).delayTime) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 6: /* Station ID transmit interval */
              {
                msgOut = String(LB_MESSAGE_TIME_INTERVAL_SETID + String(g_activeEvent->getIDIntervalForRole(role)) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 7: /* Event band */
              {
                String b;

                if (g_activeEvent->getFrequencyForRole(role) <= 4000000L)
                {
                  b = "80";
                }
                else
                {
                  b = "2";
                }

                msgOut = String(LB_MESSAGE_TX_BAND_SET + b + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 8: /* Transmit power for role */
              {
                msgOut = String(LB_MESSAGE_TX_POWER_SET + String(g_activeEvent->getPowerlevelForRole(role)) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 9: /* Modulation format */
              {
                msgOut = String(LB_MESSAGE_TX_MOD_SET + String(g_activeEvent->getEventModulation()) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 10:    /* Frequency for role */
              {
                msgOut = String(LB_MESSAGE_TX_FREQ_SET + String(g_activeEvent->getFrequencyForRole(role)) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 11:    /* Station ID */
              {
                msgOut = String(LB_MESSAGE_CALLSIGN_SET + g_activeEvent->getCallsign() + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 12:    /* Message code speed */
              {
                msgOut = String(LB_MESSAGE_CODE_SPEED_SETPAT + String(g_activeEvent->getCodeSpeedForRole(role)) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 13:    /* ID code speed */
              {
                msgOut = String(LB_MESSAGE_CODE_SPEED_SETID + g_activeEvent->getCallsignSpeed() + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 14:    /* Start time */
              {
                /* Start time is sent last */
                msgOut = String(LB_MESSAGE_STARTFINISH_SET_START + String(convertTimeStringToEpoch(g_activeEvent->getEventStartDateTime())) + ";");
                g_LBOutputBuff->put(msgOut);
              }
              break;

            case 15:    /* Perm - Have ATMega save everything in EEPROM */
              {
                g_LBOutputBuff->put(LB_MESSAGE_PERM);
              }
              break;

            default:    /* Tell ATMEGA to begin executing the event */
              {
                g_ESP_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
                g_LBOutputBuff->put(LB_MESSAGE_ACTIVATE_EVENT);
              }
              break;
          }

          serialIndex++;
        }
        break;

      case MASTER_SEND_ACTIVE_FILE:
        {
          g_http_server.handleClient();
          g_webSocketServer.loop();
          yield();

          switch (serialIndex)
          {
            case 0: /* Send File Name and open file for read */
              {
                if (g_activeEvent)
                {
                  String fn = String(g_activeEvent->myPath);

                  if (fn.length() > 0)
                  {
                    openFile = LittleFS.open(fn, "r"); /* Open the file for reading */

                    if (!openFile)
                    {
                      serialIndex = 100; /* abort */
                      Serial.println(String("File error: " + fn));
                    }
                    else
                    {
                      Serial.println(String("Sending file: " + fn));
                      g_blinkPeriodMillis = 100;
                      msgOut = String(SOCK_COMMAND_FILE_DATA) + "," + fn;
                      g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msgOut), msgOut.length());
                    }
                  }
                  else
                  {
                    Serial.println("Err: No active event");
                    serialIndex = 100; /* abort */
                  }
                }
                else
                {
                  Serial.println(String("Err: No file name."));
                  serialIndex = 100; /* abort */
                }
              }
              break;

            case 1: /* Read each line of the file and send it */
              {
                if (openFile)
                {
                  String s = openFile.readStringUntil('\n');
                  Serial.println(s);
                  s.trim();
                  msgOut = String(SOCK_COMMAND_FILE_DATA) + "," + s;
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msgOut), msgOut.length());

                  if (s.equalsIgnoreCase(String(EVENT_FILE_END)))
                  {
                    openFile.close();
                  }
                  else
                  {
                    serialIndex = 0;
                  }
                }
                else
                {
                  Serial.println("Err: No open file");
                }
              }
              break;

            case 2:  /* Send EOF */
              {
                msgOut = String(SOCK_COMMAND_FILE_DATA) + ",EOF";
                g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msgOut), msgOut.length());
              }
              break;

            default:    /* Done */
              {
                if (serialIndex >= 100) /* Abort flag */
                {
                  msgOut = String(SOCK_COMMAND_SLAVE) + "," + SLAVE_FREE;
                  g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msgOut), msgOut.length());
                }

                g_ESP_Comm_State = TX_WAITING_FOR_INSTRUCTIONS;
                g_blinkPeriodMillis = 500;
              }
              break;
          }

          serialIndex++;
        }
        break;


      default:
      case TX_WAITING_FOR_INSTRUCTIONS:
        {
          serialIndex = 0;
        }
        break;
    }
  }
}


bool sendLBMessages()
{
  bool messagesSent = (!g_LBOutputBuff->empty() && !g_linkBusAckPending);
  if (messagesSent)
  {
    String msg = g_LBOutputBuff->get();
    Serial.println(stringObjToConstCharString(&msg));
    g_linkBusAckPending++;
    g_linkBusAckTimeout = 10;
  }
  return messagesSent;
}

bool checkUART()
{
  size_t bytesAvail, bytesIn;
  static int escapeCount = 0;
  static int messageLength = 0;
  static String lb_message = "";
  static char buf[1024];
  uint8_t j;
  bool done = false;

  while ((bytesAvail = Serial.available()) > 0) /*check UART for data */
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
          messageLength = 1;
          lb_message = "$";

          if (escapeCount == 3)
          {
            done = true;
            escapeCount = 0;
            /*             Serial.println("Web Server closed"); */
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
          escapeCount = 0;
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

  return (done);
}

void startLittleFS()
{ /* Start the LittleFS and list all contents */
  LittleFS.begin(); /* Start the SPI Flash File System (LittleFS) */

  if (g_debug_prints_enabled)
  {
    Serial.println("LittleFS started. Contents:");
    {
      Dir dir = LittleFS.openDir("/");
      while (dir.next())
      { /* List the file system contents */
        String fileName = dir.fileName();
        size_t fileSize = dir.fileSize();
        Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
      }

      Serial.printf("\n");
    }
  }
}

void startWebSocketServer()
{ /* Start a WebSocket server */
  if (g_webSocketServer.isRunning())
  {
    return;                                 /* Don't attempt to start a webSocket server that has already begun */

  }
  g_webSocketServer.begin();                  /* start the websocket server */
  /*  g_webSocketServer.beginSSL(); // start secure wss support? */
  g_webSocketServer.onEvent(webSocketEvent);  /* if there's an incoming websocket message, go to function 'webSocketEvent' */

  if (g_debug_prints_enabled)
  {
    Serial.println("WebSocket server start tasks complete.");
  }
}

void webSocketClientEvent(WStype_t type, uint8_t * payload, size_t length)
{
  switch (type)
  {
    case WStype_DISCONNECTED:
      Serial.printf("[WSc] Disconnected!\n");
      g_slave_released = true;
      break;

    case WStype_CONNECTED:
      {
        Serial.printf("[WSc] Connected to url: %s\n",  payload);
      }
      break;

    case WStype_TEXT:
      {
        if (!payload)
        {
          Serial.println("Null payload");
        }
        else
        {
          Serial.printf("[WSc] got text: %s\n", payload);

          String p = String((char*)payload);
          int commaIndex = p.indexOf(',');

          String msgHeader;

          if (commaIndex >= 0)
          {
            msgHeader = p.substring(0, p.indexOf(','));
          }
          else
          {
            msgHeader = p;
          }

          if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_SLAVE))
          {
            p = p.substring(p.indexOf(',') + 1);

            if (p.equals(SLAVE_FREE))
            {
              g_slave_released = true;
            }
            else if (p.equals(SLAVE_CONFIRMED))
            {
              g_slave_released = false;
              g_webSocketSlaveState = WSClientSyncClock;
              Serial.println("WSc: sync clock");
            }
            else if (p.equals(SLAVE_WAITING_FOR_UPDATES))
            {
              if (g_webSocketSlaveState == WSClientWaitForUpdates)
              {
                g_webSocketSlaveState = WSClientPrepForFileData;
                Serial.println("WSc: rcv data");
              }
            }
          }
          else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_SYNC_TIME)) /* From connected Master */
          {
            if (g_webSocketSlaveState == WSClientSyncClock)
            {
              p = p.substring(p.indexOf(',') + 1);

              if ((p.length() > 9) && (p.length() < 12))
              {
                time_t t = strtol(stringObjToConstCharString(&p), NULL, 10);

                if (t > 962452800) /* Avoid obviously wrong dates */
                {
                  g_timeOfDayFromTx = t;
                  p = convertEpochToTimeString(g_timeOfDayFromTx);

                  if (g_debug_prints_enabled)
                  {
                    Serial.println(String("WSc: Calcd time string: \"" + p + "\" from ") + String(g_timeOfDayFromTx));
                  }

                  String msgOut = String(String(LB_MESSAGE_TIME_SET) + p + ";");
                  g_LBOutputBuff->put(msgOut); /* Send time to Transmitter for synchronization */
                  sendLBMessages();
                  g_webSocketSlaveState = WSClientWaitForSyncAck;
                }
                else
                {
                  Serial.println(String("WSc: Rcvd invalid time: \"" + p + "\""));
                  g_webSocketSlaveState = WSClientClose;
                }
              }
              else
              {
                Serial.println(String("WSc: Rcvd invalid time: \"" + p + "\""));
                g_webSocketSlaveState = WSClientClose;
              }
            }
          }
          else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_FILE_DATA))
          {
            if (g_webSocketSlaveState == WSClientReceiveFileData)
            {
              if (g_fileDataBuff)
              {
                p = p.substring(p.indexOf(',') + 1);

                if ((p.length() > 0) && (p.length() < 100))
                {
                  g_fileDataBuff->put(p);
                }
                else
                {
                  Serial.println("BD");
                }
              }
              else
              {
                Serial.println(String("WSc: File buff not ready!"));
                g_webSocketSlaveState = WSClientClose;
              }
            }
          }
        }
      }
      break;

    case WStype_BIN:
      Serial.printf("[WSc] get binary length: %u\n", length);
      //     hexdump(payload, length);
      break;

    case WStype_ERROR:
      Serial.printf("[WSc] error\n");
      break;

    default:
      break;
  }
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  switch (type)
  {
    case WStype_DISCONNECTED:
      {
        //        if (g_debug_prints_enabled)
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

          g_numberOfSocketClients = g_webSocketServer.connectedClients(false);

          if (!g_numberOfSocketClients)
          {
            g_socket_timeout = 0;
          }
        }

        /*        g_LBOutputBuff->put(LB_MESSAGE_ESP_RETAINPOWER); */
      }
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = g_webSocketServer.remoteIP(num);
        //        if (g_debug_prints_enabled)
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

        g_numberOfSocketClients = g_webSocketServer.connectedClients(false);
        g_LBOutputBuff->put(LB_MESSAGE_TEMP_REQUEST);
        g_LBOutputBuff->put(LB_MESSAGE_BATTERY_REQUEST);
        g_noActivityTimeoutSeconds = NO_ACTIVITY_TIMEOUT;
      }
      break;

    case WStype_TEXT:
      {
        g_noActivityTimeoutSeconds = NO_ACTIVITY_TIMEOUT;

        String p = String((char*)payload);
        int commaIndex = p.indexOf(',');

        String msgHeader;

        if (commaIndex >= 0)
        {
          msgHeader = p.substring(0, p.indexOf(','));
        }
        else
        {
          msgHeader = p;
        }

        if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_ALIVE))
        {
          g_socket_timeout = 5;
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_SLAVE))
        {
          p = p.substring(p.indexOf(',') + 1);

          String msg;

          if (p.equals(SLAVE_CONNECT))
          {
            g_slave_connected = true;
            msg = String(String(SOCK_COMMAND_SLAVE) + "," + SLAVE_CONFIRMED);
            Serial.println("WSs: ack slave");

            if (!g_activeEvent)
            {
              g_ESP_Comm_State = TX_HTML_REFRESH_EVENTS;
            }
          }
          else if (p.equals(SLAVE_FREE))
          {
            g_slave_connected = false;
            msg = String(String(SOCK_COMMAND_SLAVE) + "," + SLAVE_FREE);
            Serial.println("WSs: free slave");
          }
          else if (p.equals(SLAVE_WAITING_FOR_UPDATES))
          {
            if (g_activeEvent)
            {
              msg = String(String(SOCK_COMMAND_SLAVE) + "," + SLAVE_WAITING_FOR_UPDATES);
              Serial.println("WSs: data waiting for slave");
            }
            else
            {
              msg = String(String(SOCK_COMMAND_SLAVE) + "," + SLAVE_FREE);
              Serial.println("WSs: no active event (1)");
            }
          }
          else if (p.equals(SLAVE_READY_FOR_FILE))
          {
            if (g_activeEvent)
            {
              g_ESP_Comm_State = MASTER_SEND_ACTIVE_FILE;
              Serial.println("WSs: sending data to slave");
            }
            else
            {
              msg = String(String(SOCK_COMMAND_SLAVE) + "," + SLAVE_FREE);
              Serial.println("WSs: no active event (2)");
            }
          }

          if (msg) g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_SYNC_TIME)) /* From connected browser */
        {
          p = p.substring(p.indexOf(',') + 1);

          /* Lop off milliseconds */
          if ((p.length() > 20) && (p.lastIndexOf('.') > 0))
          {
            int index = p.lastIndexOf('.');
            p = p.substring(0, index);
            p = p + "Z";
          }

          if (g_debug_prints_enabled)
          {
            Serial.println(String("Time string: \"" + p + "\""));
          }
          String msgOut = String(String(LB_MESSAGE_TIME_SET) + p + ";");
          Serial.printf(stringObjToConstCharString(&msgOut));  /* Send time to Transmitter for synchronization */
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_TX_ROLE))
        {
          int c = p.indexOf(':');
          p = p.substring(c - 1, c + 2);

          if (g_activeEvent != NULL)
          {
            g_activeEvent->setTxAssignment(p);
            g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_EVENT_NAME))
        {
          if (p.indexOf("NEW!") >= 0)
          {
            g_selectedEventName = "";
            g_ESP_Comm_State = TX_HTML_PAGE_SERVED;
          }
          else
          {
            p = p.substring(p.indexOf(',') + 1);
            g_selectedEventName = p;
            g_ESP_Comm_State = TX_HTML_NEXT_EVENT;
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_CALLSIGN))
        {
          p = p.substring(p.indexOf(',') + 1);
          p.toUpperCase();

          if (g_activeEvent != NULL)
          {
            g_activeEvent->setCallsign(p);
            g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;
          }
          else
          {
            if (g_debug_prints_enabled)
            {
              Serial.printf(String("Callsign: \"" + p + "\"\n").c_str());
            }

            String msgOut = String(LB_MESSAGE_CALLSIGN_SET + p + ";");
            g_LBOutputBuff->put(msgOut);
          }

          String msg = String(String(SOCK_COMMAND_CALLSIGN) + "," + p);

          g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
          g_activeEvent->setCallsign(p);
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_PATTERN))
        {
          int firstComma = p.indexOf(',');
          int lastComma = p.lastIndexOf(',');

          String pat = p.substring(lastComma + 1);
          pat.toUpperCase();

          p = p.substring(firstComma + 1, lastComma);

          firstComma = p.indexOf(',');

          if (firstComma > 0) /* PATTERN,type,tx,pattern */
          {
            int typeIndex = p.substring(0, firstComma).toInt();
            int txIndex = p.substring(firstComma + 1).toInt();

            if (g_activeEvent != NULL)
            {
              g_activeEvent->setPatternForTx(typeIndex, txIndex, pat);
              g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;
            }
          }
          else if (g_activeEvent == NULL) /* PATTERN,pattern */
          {
            if (g_debug_prints_enabled)
            {
              Serial.printf(String("Pattern: \"" + pat + "\"\n").c_str());
            }

            String msgOut = String(LB_MESSAGE_PATTERN_SET + pat + ";");
            g_LBOutputBuff->put(msgOut);
          }

        }
        else if (msgHeader.startsWith(SOCK_COMMAND_START_TIME))
        {
          p = p.substring(p.indexOf(',') + 1);

          if (g_activeEvent != NULL)
          {
            g_activeEvent->setEventStartDateTime(p);
            g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;
          }
          else
          {
            if (g_debug_prints_enabled)
            {
              Serial.printf(String("Start Time: \"" + p + "\"\n").c_str());
            }

            String msgOut = String(LB_MESSAGE_STARTFINISH_SET_START + p + ";");
            Serial.printf(stringObjToConstCharString(&msgOut));  /* Send to Transmitter */
          }
        }
        else if (msgHeader.startsWith(SOCK_COMMAND_FINISH_TIME))
        {
          p = p.substring(p.indexOf(',') + 1);

          if (g_activeEvent != NULL)
          {
            g_activeEvent->setEventFinishDateTime(p);
            g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;
          }
          else
          {
            if (g_debug_prints_enabled)
            {
              Serial.printf(String("Finish Time: \"" + p + "\"\n").c_str());
            }
            String msgOut = String(LB_MESSAGE_STARTFINISH_SET_FINISH + p + ";");
            g_LBOutputBuff->put(msgOut);
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_CLEAR_ACTIVE_EVENT))
        {
          g_LBOutputBuff->put(LB_MESSAGE_KEY_UP);
          if (g_activeEvent)
          {
            delete(g_activeEvent);
          }
          g_activeEvent = NULL;
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_TYPE_FREQ))
        {
          int typeIndex;
          long freq;
          int firstComma = p.indexOf(',');
          int secondComma = p.lastIndexOf(',');
          bool indexOnly = (secondComma == firstComma);

          if (indexOnly)
          {
            typeIndex = (p.substring(firstComma + 1)).toInt();
          }
          else
          {
            typeIndex = (p.substring(firstComma + 1, secondComma)).toInt();
            freq = (p.substring(secondComma + 1)).toInt();
          }

          if (indexOnly)  /* request for frequency of typeIndex */
          {
            if (g_activeEvent != NULL)
            {
              freq = g_activeEvent->getFrequencyForRole(typeIndex);

              String msg = String(String(SOCK_COMMAND_TYPE_FREQ) + "," + freq);
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());

              if (g_debug_prints_enabled)
              {
                Serial.println("Freq" + String(typeIndex) + ": " + freq);
              }
            }
          }
          else if ((freq >= TX_MIN_ALLOWED_FREQUENCY_HZ) && (freq <= TX_MAX_ALLOWED_FREQUENCY_HZ) && (typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
          {
            if (g_activeEvent != NULL)
            {
              g_activeEvent->setFrequencyForRole(typeIndex, freq);
              g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;

              if (g_debug_prints_enabled)
              {
                Serial.println("Freq" + String(typeIndex) + ": " + String(freq));
              }
            }
            else
            {
              String msgOut = String(LB_MESSAGE_TX_FREQ_SET + String(freq) + ";");
              g_LBOutputBuff->put(msgOut);
            }
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_TYPE_PWR))
        {
          int typeIndex;
          String pwr;
          int firstComma = p.indexOf(',');
          int secondComma = p.lastIndexOf(',');
          bool indexOnly = (secondComma == firstComma);

          if (indexOnly)
          {
            typeIndex = (p.substring(firstComma + 1)).toInt();
          }
          else
          {
            typeIndex = (p.substring(firstComma + 1, secondComma)).toInt();
            pwr = (p.substring(secondComma + 1));
          }

          if (indexOnly)  /* request for power level of typeIndex */
          {
            if (g_activeEvent != NULL)
            {
              pwr = g_activeEvent->getPowerlevelForRole(typeIndex);

              String msg = String(String(SOCK_COMMAND_TYPE_PWR) + "," + pwr);
              g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());

              if (g_debug_prints_enabled)
              {
                Serial.println("Pwr" + String(typeIndex) + ": " + pwr);
              }
            }
          }
          else if ((pwr.toInt() >= 0) && (pwr.toInt() < TX_MAX_ALLOWED_POWER_MW) && (typeIndex >= 0) && (typeIndex < MAXIMUM_NUMBER_OF_EVENT_TX_TYPES))
          {
            if (g_activeEvent != NULL)
            {
              g_activeEvent->setPowerlevelForRole(typeIndex, pwr);
              g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;

              if (g_debug_prints_enabled)
              {
                Serial.println("Pwr" + String(typeIndex) + ": " + pwr);
              }
            }
            else
            {
              String msgOut = String(LB_MESSAGE_TX_POWER_SET + pwr + ";");
              g_LBOutputBuff->put(msgOut);
            }
          }
          else
          {
            if (g_debug_prints_enabled)
            {
              Serial.println("Illegal socket message: POWER," + pwr);
            }
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_TYPE_MODULATION))
        {
          String mod;
          int lastComma = p.lastIndexOf(',');
          mod = (p.substring(lastComma + 1));

          if ((mod == "CW") || (mod == "AM"))
          {
            if (g_activeEvent != NULL)
            {
              g_activeEvent->setEventModulation(mod);
              g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;

              if (g_debug_prints_enabled)
              {
                Serial.println("Modulation: " + mod);
              }
            }
            else
            {
              String msgOut = String(LB_MESSAGE_TX_MOD_SET + mod + ";");
              g_LBOutputBuff->put(msgOut);
            }
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_BAND))
        {
          String band;
          int lastComma = p.lastIndexOf(',');
          band = (p.substring(lastComma + 1));

          if ((band == "80") || (band == "2"))
          {
            if (g_activeEvent != NULL)
            {
              g_activeEvent->setEventBand(band);
              g_ESP_Comm_State = TX_HTML_SAVE_CHANGES;

              if (g_debug_prints_enabled)
              {
                Serial.println("Band: " + band);
              }
            }
            else
            {
              String msgOut = String(LB_MESSAGE_TX_BAND_SET + band + ";");
              g_LBOutputBuff->put(msgOut);
            }
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_REFRESH))
        {
          g_ESP_Comm_State = TX_HTML_REFRESH_EVENTS;
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_EXECUTE_EVENT))
        {
          if (g_activeEvent)
          {
            int tries = 10;
            bool fail = true;

            while (tries && fail)
            {
              tries--;
              fail = g_activeEvent->writeEventFile();    /* save any changes made to the active event */
            }
            g_ESP_Comm_State = TX_RECD_START_EVENT_REQUEST;
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_MAC))
        {
          for (int i = 0; i < MAX_NUMBER_OF_WEB_CLIENTS; i++)
          {
            if ((g_webSocketClient[i].macAddr).length() && (g_webSocketClient[i].socketID < WEBSOCKETS_SERVER_CLIENT_MAX))
            {
              String msg = String( String(SOCK_COMMAND_MAC) + "," + g_webSocketClient[i].macAddr );
              g_webSocketServer.sendTXT(g_webSocketClient[i].socketID, stringObjToConstCharString(&msg), msg.length());
              if (g_debug_prints_enabled)
              {
                Serial.println(msg);
              }
            }
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_SSID))
        {
          String msg = String( String(SOCK_COMMAND_SSID) + "," + g_AP_NameString );
          g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
          if (g_debug_prints_enabled)
          {
            Serial.println(msg);
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_KEY_DOWN))
        {
          String msgOut = String(LB_MESSAGE_KEY_DOWN);
          g_LBOutputBuff->put(msgOut);
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_KEY_UP))
        {
          String msgOut = String(LB_MESSAGE_KEY_UP);
          g_LBOutputBuff->put(msgOut);
        }
        else if (msgHeader == SOCK_COMMAND_WIFI_OFF)
        {
          String msgOut = String(LB_MESSAGE_WIFI_OFF);
          g_LBOutputBuff->put(msgOut);
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_MASTER))
        {
          String setting;
          int lastComma = p.lastIndexOf(',');
          setting = (p.substring(lastComma + 1));

          if ((setting.equalsIgnoreCase("1") || setting.equalsIgnoreCase("TRUE")))
          {
            g_masterEnabled = true;
            saveDefaultsFile();
            ESP.reset();
          }
          else if ((setting.equalsIgnoreCase("0") || setting.equalsIgnoreCase("FALSE")))
          {
            g_masterEnabled = false;
            saveDefaultsFile();
          }

          String msg = String( String(SOCK_COMMAND_MASTER) + "," + String(g_masterEnabled));
          g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());

          if (g_debug_prints_enabled)
          {
            Serial.println(msg);
          }
        }
        else if (msgHeader.equalsIgnoreCase(SOCK_COMMAND_PASSTHRU))
        {
          int firstComma = p.indexOf(',');
          String msgOut = (p.substring(firstComma + 1));
          g_LBOutputBuff->put(msgOut);
        }
        else if (g_debug_prints_enabled)
        {
          Serial.println("[WS]err: " + p);
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

        /* echo data back to browser */
        g_webSocketServer.sendBIN(num, payload, length);
      }
      break;

    default:
      {
        if (g_debug_prints_enabled)
        {
          Serial.printf("Invalid WStype [%d]\r\n", type);
        }
      }
      break;
  }
}

bool handleFileRead(String path)
{
  if (path == NULL) return false;

  /* send the right file to the client (if it exists) */
  if (g_debug_prints_enabled)
  {
    Serial.println("\nhandleFileRead: " + path);
  }

  if (path.endsWith("/"))
  {
    path += "index.html";                   /* If a folder is requested, send the index file */
  }
  String contentType = getContentType(path);  /* Get the MIME type */
  String pathWithGz = path + ".gz";
  String pathWithHTML = path + ".html";

  if (LittleFS.exists(stringObjToConstCharString(&pathWithGz)) || LittleFS.exists(stringObjToConstCharString(&path)) || LittleFS.exists(stringObjToConstCharString(&pathWithHTML)))
  { /* If the file exists, either as a compressed archive, or normal */
    if (LittleFS.exists(pathWithGz))          /* If there's a compressed version available */
    {
      path += ".gz";                      /* Use the compressed verion */

    }
    if (LittleFS.exists(pathWithHTML))
    {
      path += ".html";
      contentType = getContentType(path);
    }

    File file = LittleFS.open(path, "r");                             /* Open the file */
    if (file)
    {
      size_t sent = g_http_server.streamFile(file, contentType);  /* Send it to the client */
      file.close();                                               /* Close the file again */
      if (g_debug_prints_enabled)
      {
        Serial.println(String("\tSent " + String(sent) + "bytes to file: ") + path);
      }
    }
    return ( true);
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("File not found in LittleFS: ") + path);
    }
  }

  return ( false);
}


int numberOfEventsScheduled(unsigned long epoch)
{
  int numberScheduled = 0;
  EventFileRef a;

  if (g_eventsRead < 1)
  {
    return ( 0);    /* no events means none is scheduled */

  }
  if (g_eventsRead > 1)
  {
    /* binary sort the events list from soonest to latest */
    for (int i = 0; i < g_eventsRead; ++i)
    {
      for (int j = i + 1; j < g_eventsRead; ++j)
      {
        if (Event::isSoonerEvent(g_eventList[j], g_eventList[i], epoch))
        {
          a = g_eventList[i];
          g_eventList[i] = g_eventList[j];
          g_eventList[j] = a;
        }
      }
    }

    /*    if (g_debug_prints_enabled) {
            Serial.println("Ordered Events:");
            for (int i = 0; i < g_eventsRead; i++)
            {
              Serial.println( String(i) + ". " + g_eventList[i].path);
              Serial.println( "    Start: " + String(g_eventList[i].startDateTimeEpoch));
              Serial.println( "    Finish:" + String(g_eventList[i].finishDateTimeEpoch));
            }
          } */
  }

  for (int i = 0; i < g_eventsRead; i++)
  {
    bool iRunsForever = a.startDateTimeEpoch >= a.finishDateTimeEpoch;
    bool iStartedInThePast = a.startDateTimeEpoch <= epoch;
    bool iFinishedInThePast = (a.finishDateTimeEpoch <= epoch) && !iRunsForever;

    if (!iStartedInThePast)         /* scheduled to start in the future */
    {
      numberScheduled++;
    }
    else if (!iFinishedInThePast)   /* event is now in progress */
    {
      numberScheduled++;
    }
    else                            /* the sorted list will not contain any additional scheduled events */
    {
      break;
    }
  }

  return ( numberScheduled);
}

bool readEventTimes(String path, EventFileRef * fileRef)
{
  fileRef->path = path;
  int data_count = 0;

  if (LittleFS.exists(path))
  {
    File file = LittleFS.open(path, "r"); /* Open the file for reading */
    if (file)
    {
      String s = file.readStringUntil('\n');
      int count = 0;

      while (s.length() && (count++ < MAXIMUM_NUMBER_OF_EVENT_FILE_LINES) && (data_count < 5))
      {
        if (s.indexOf("EVENT_START_DATE_TIME") >= 0)
        {
          EventLineData data;
          Event::extractLineData(s, &data);
          fileRef->startDateTimeEpoch = convertTimeStringToEpoch(data.value);
          /*        startRead = true; */
          data_count++;

          if ( g_debug_prints_enabled )
          {
            Serial.println("Start epoch: " + String(fileRef->startDateTimeEpoch));
          }
        }
        else if (s.indexOf("EVENT_FINISH_DATE_TIME") >= 0)
        {
          EventLineData data;
          Event::extractLineData(s, &data);
          fileRef->finishDateTimeEpoch = convertTimeStringToEpoch(data.value);
          data_count++;

          if ( g_debug_prints_enabled )
          {
            Serial.println("Finish epoch: " + String(fileRef->finishDateTimeEpoch));
          }
        }
        else if ( s.indexOf("EVENT_NAME") >= 0)
        {
          EventLineData data;
          Event::extractLineData(s, &data);
          fileRef->ename = data.value;
          data_count++;

          if ( g_debug_prints_enabled )
          {
            Serial.println("Event name: " + fileRef->ename);
          }
        }
        else if ( s.indexOf(EVENT_FILE_VERSION) >= 0)
        {
          EventLineData data;
          Event::extractLineData(s, &data);
          fileRef->vers = data.value;
          data_count++;

          if ( g_debug_prints_enabled )
          {
            Serial.println("File version: " + fileRef->vers);
          }
        }
        else if (s.indexOf(EVENT_CALLSIGN) >= 0)
        {
          EventLineData data;
          Event::extractLineData(s, &data);
          fileRef->callsign = data.value;
          data_count++;

          if ( g_debug_prints_enabled )
          {
            Serial.println("Callsign: " + fileRef->callsign);
          }
        }

        s = file.readStringUntil('\n');
      }

      file.close();   /* Close the file */
    }
    Event::extractMeFileData(path, fileRef);

    if (g_debug_prints_enabled)
    {
      Serial.println(String("Times read for file: ") + path);
    }
  }

  return (data_count != 5);
}

bool populateEventFileList(void)
{
  if ( g_debug_prints_enabled )
  {
    Serial.println("Reading events...");
  }

  g_eventsRead = 0;
  Dir dir = LittleFS.openDir("/");
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
      Serial.println( "    " + String(g_eventList[i].startDateTimeEpoch));
      Serial.println( "    " + String(g_eventList[i].finishDateTimeEpoch));
      Serial.println( "    " + String(g_eventList[i].vers));
      Serial.println( "    " + String(g_eventList[i].ename));
      Serial.println( "    " + String(g_eventList[i].role));
      Serial.println( "    " + String(g_eventList[i].callsign));
      Serial.println( "    " + String(g_eventList[i].power));
      Serial.println( "    " + String(g_eventList[i].freq));
    }
    Serial.printf("\n");
  }

  return ( true);
}


bool readDefaultsFile()
{ /* send the right file to the client (if it exists) */
  String path = "/defaults.txt";

  if (g_debug_prints_enabled)
  {
    Serial.println("Reading defaults...");
  }

  String contentType = getContentType(path);  /* Get the MIME type */
  if (LittleFS.exists(path))
  { /* If the file exists, either as a compressed archive, or normal
      open file for reading */
    File file = LittleFS.open(path, "r");     /* Open the file */
    if (file)
    {
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

          break;  /* invalid line found */
        }

        String settingID = s.substring(0, s.indexOf(','));
        String value = s.substring(s.indexOf(',') + 1, s.indexOf('\n'));

        if (value.charAt(0) == '"')
        {
          if (value.charAt(1) == '"') /* handle empty string */
          {
            value = "";
          }
          else                        /* remove quotes */
          {
            value = value.substring(1, value.length() - 2);
          }
        }

        if (settingID.equalsIgnoreCase("MASTER_ENABLE_DEFAULT"))
        {
          if (value.charAt(0) == 'T' || value.charAt(0) == 't' || value.charAt(0) == '1')
          {
            g_masterEnabled = 1;
          }
          else
          {
            g_masterEnabled = 0;
          }
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
          /*        if (value.charAt(0) == 'T' || value.charAt(0) == 't' || value.charAt(0) == '1')
                    {
                      g_debug_prints_enabled = 1;
                    }
                    else
                    {
                      g_debug_prints_enabled = 0;
                    } */
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

      file.close();   /* Close the file */

      if (g_debug_prints_enabled)
      {
        Serial.println(String("\tRead file: ") + path);
      }
    }

    return ( true);
  }

  Serial.println(String("\tFile Not Found: ") + path);    /* If the file doesn't exist, return false */
  return ( false);
}

void saveDefaultsFile()
{
  String path = "/defaults.txt";

  Serial.println("Writing defaults...");
  String contentType = getContentType(path);  /* Get the MIME type */
  /*  if(LittleFS.exists(path)) */
  { /* If the file exists, either as a compressed archive, or normal
      open file for reading */
    File file = LittleFS.open(path, "w");     /* Open the file for writing */
    if (file)
    {
      String s;

      for (int i = 0; i < NUMBER_OF_SETTABLE_VARIABLES; i++)
      {
        switch (i)
        {
          case MASTER_ENABLE:
            {
              if (g_masterEnabled)
              {
                file.println("MASTER_ENABLE_DEFAULT,TRUE");
              }
              else
              {
                file.println("MASTER_ENABLE_DEFAULT,FALSE");
              }

              if (g_debug_prints_enabled)
              {
                Serial.println("Wrote MASTER_ENABLE_DEFAULT");
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

      file.close();   /* Close the file */
      Serial.println(String("\tWrote file: ") + path);
    }
  }

  Serial.println(String("\tFile Not Found: ") + path);    /* If the file doesn't exist, return false */
}

void handleFileDelete()
{
  String path = g_http_server.arg(0);
  bool abort = false;

  if (g_debug_prints_enabled)
  {
    Serial.println("\nhandleFileDelete: " + path);
  }

  if (path.endsWith("/"))
  {
    abort = true;   /* If a folder is requested consider it an error */

  }
  if (!abort && LittleFS.exists(stringObjToConstCharString(&path)))
  { /* If the file exists */
    LittleFS.remove(path);

    if (g_debug_prints_enabled)
    {
      Serial.println(String(path + " Deleted."));
    }

    fileDeleteWithMessage("<p>File deleted successfully!</p>");
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("File not found in LittleFS: ") + path);
    }

    fileDeleteWithMessage("<p>File deletion failed!</p>");
  }


  return;
}


void handleFileDownload()
{
  String path = g_http_server.arg(0);

  if (g_debug_prints_enabled)
  {
    Serial.println("\nhandleFileDownload: " + path);
  }

  if (path.endsWith("/"))
  {
    path += "index.html";                                           /* If a folder is requested, send the index file */
  }
  String contentType = "text/plain";                                  /* Always use text MIME type */
  if (LittleFS.exists(stringObjToConstCharString(&path)))
  { /* If the file exists, either as a compressed archive, or normal */
    File file = LittleFS.open(path, "r");                             /* Open the file */

    if (file)
    {
      size_t sent = g_http_server.streamFile(file, contentType);  /* Send it to the client */
      file.close();                                               /* Close the file */


      if (g_debug_prints_enabled)
      {
        Serial.println(String("\tSent " + String(sent) + "bytes from file: ") + path);
      }
    }
  }
  else
  {
    if (g_debug_prints_enabled)
    {
      Serial.println(String("File not found in LittleFS: ") + path);
    }
  }

  return;
}

void handleFileUpload()
{ /* upload a new file to the LittleFS */
  HTTPUpload& upload = g_http_server.upload();
  String path;

  if (upload.status == UPLOAD_FILE_START)
  {
    path = upload.filename;
    if (!path.startsWith("/"))
    {
      path = "/" + path;
    }
    if (!path.endsWith(".gz"))
    { /* The file server always prefers a compressed version of a file */
      String pathWithGz = path + ".gz";   /* So if an uploaded file is not compressed, the existing compressed */
      if (LittleFS.exists(pathWithGz))      /* version of that file must be deleted (if it exists) */
      {
        LittleFS.remove(pathWithGz);
      }
    }
    Serial.print("handleFileUpload Name: "); Serial.println(path);
    fsUploadFile = LittleFS.open(path, "w");  /* Open the file for writing in LittleFS (create if it doesn't exist) */
    path = String();
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (fsUploadFile)
    {
      fsUploadFile.write(upload.buf, upload.currentSize); /* Write the received bytes to the file */
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (fsUploadFile)
    { /* If the file was successfully created */
      fsUploadFile.close();                                   /* Close the file again */
      Serial.print("handleFileUpload Size: ");
      Serial.println(upload.totalSize);
      handleSuccess();
      //	        g_http_server.sendHeader("Location", "/success.html");  /* Redirect the client to the success page */
      //			g_http_server.send(303);

      if (path.endsWith(".event"))
      {
        g_ESP_Comm_State = TX_READ_ALL_EVENTS_FILES; /* have the transmitter re-initialize event file list */
      }
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
  Serial.println("HUZZAH Settings");
  for (i = 0; i < NUMBER_OF_SETTABLE_VARIABLES; i++)
  {
    switch (i)
    {
      case MASTER_ENABLE:
        {
          Serial.println(String("Master Enabled (MASTER_ENABLE) = ") + String(g_masterEnabled));
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
  if (message == NULL) return;
  if (message.length() < 3) return;

  /* e.g., "$EC,247;" */
  int firstDelimit = message.indexOf(',');

  if (firstDelimit < 0)
  {
    firstDelimit = message.indexOf(';');
  }

  if (firstDelimit < 0)
  {
    firstDelimit = message.length() - 1;
  }

  if (firstDelimit < 0)
  {
    Serial.println("Bad LB msg rcvd!");
    return;
  }

  String type = message.substring(1, firstDelimit);
  String payload = "";
  if (message.indexOf(',') >= 0)
  {
    payload = message.substring(firstDelimit + 1, message.indexOf(';'));
  }

  if (!g_slave_released) /* If connected to Master ignore most messages */
  {
    if (!type.equals(LB_MESSAGE_ACK))
    {
      return;
    }
  }

  if (type.equals(LB_MESSAGE_ACK))
  {
    if (g_linkBusAckPending)
    {
      g_linkBusAckPending--;
    }

    if (g_debug_prints_enabled)
    {
      Serial.println("ACK");
    }
  }
  else if (type.equals(LB_MESSAGE_ESP))
  {
    if (payload.equals("1")) /* Atmega is asking to receive the next active event */
    {
      if (g_numberOfScheduledEvents && (g_activeEvent != NULL))
      {
        g_ESP_Comm_State = TX_RECD_START_EVENT_REQUEST;
        g_LBOutputBuff->put(LB_MESSAGE_ESP_KEEPALIVE);
      }
    }
    else if (payload.equals("2"))
    {
      g_saveEventToFile = 5;
    }
  }
  else if (type.equals(LB_MESSAGE_TIME))
  {
    String timeinfo = payload;
    g_timeOfDayFromTx = payload.toInt();
    unsigned long epoch = g_timeOfDayFromTx;

    if (epoch)
    {
      if (g_justPoweredUp)
      {
        g_ESP_Comm_State = TX_INITIAL_TIME_RECEIVED;
        if (g_linkBusAckPending)
        {
          g_linkBusAckPending--; /* This msg serves as an ACK */
        }
        Serial.println("W/U ack rcvd.");
        g_justPoweredUp = false;
      }

      if (g_numberOfSocketClients)
      {
        String msg = String(String(SOCK_COMMAND_SYNC_TIME) + "," + timeinfo);
        g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
      }
    }
  }
  else if (type.equals(LB_MESSAGE_ERROR_CODE))
  {
    String code = payload;

    if (g_debug_prints_enabled)
    {
      Serial.println("err=" + String(code));
    }

    if (g_numberOfSocketClients)
    {
      String msg = String(String(SOCK_COMMAND_ERROR) + "," + code);
      g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
    }
  }
  else if (type.equals(LB_MESSAGE_STATUS_CODE))
  {
    String code = payload;

    /*    if (g_debug_prints_enabled)
          {
              Serial.println("status=" + String(code));
          } */

    if (g_numberOfSocketClients)
    {
      String msg = String(String(SOCK_COMMAND_STATUS) + "," + code);
      g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
    }
  }
  else if (type.equals(LB_MESSAGE_TX_POWER))
  {
    String code = payload;
    int mLocation = code.indexOf('M');

    if (mLocation >= 0)
    {
      code = code.substring(mLocation + 2);
    }

    /*      if (g_debug_prints_enabled)
            {
                Serial.println("tx power=" + code);
            } */

    if (g_numberOfSocketClients)
    {
      String msg = String(String(SOCK_COMMAND_TYPE_PWR) + "," + code);
      g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
    }
  }
  else if (type.equals(LB_MESSAGE_TEMP))
  {
    int16_t rawtemp = payload.toInt();
    bool negative =  rawtemp & 0x8000;
    if (negative)
    {
      rawtemp &= 0x7FFF;
    }
    float temp = (rawtemp >> 8) + (0.25 * ((rawtemp & 0x00C0) >> 6)) + 0.05;
    if (negative)
    {
      temp = -temp;
    }

    char dataStr[6];    /* allow for possible negative sign */
    dtostrf(temp, 4, 1, dataStr);
    dataStr[5] = '\0';

    if (g_numberOfSocketClients)
    {
      String msg = String(String(SOCK_COMMAND_TEMPERATURE) + "," + dataStr + "C");

      g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
      /*      if (g_debug_prints_enabled)
              {
                Serial.println(msg);
              } */
    }
  }
  else if (type.equals(LB_MESSAGE_BATTERY))
  {
    float temp = payload.toInt();

    /*    if (g_debug_prints_enabled)
          {
            Serial.println("B=" + String(temp));
          } */

    char dataStr[4];
    dtostrf(temp, 3, 0, dataStr);
    dataStr[3] = '\0';

    if (g_numberOfSocketClients)
    {
      String msg = String(String(SOCK_COMMAND_BATTERY) + "," + dataStr + "%");

      g_webSocketServer.broadcastTXT(stringObjToConstCharString(&msg), msg.length());
      /*      if (g_debug_prints_enabled)
              {
                Serial.println(msg);
              } */
    }
  }
}
