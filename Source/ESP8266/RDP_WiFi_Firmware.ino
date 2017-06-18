/**********************************************************************************************
 * Copyright © 2017 Digital Confections LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in the
 * Software without restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 **********************************************************************************************
 *
 *  Basic WiFi Functionality for Receiver Development Platform
 * 
 *  Hardware Target: Adafruit HUZZAH ESP8266
 *
 *  This sketch provides the following functionality on the target hardware:
 *
 *    1. Receives commands over the UART0 port. Commands must start with $$$ as the escape sequence.
 * 
 *    2. $$$m,#,val;  <- this command allows the ATmega328p to set variables that the WiFi board 
 *       will use; such as the SSID and password for a hotspot for Internet access.
 * 
 *    3. $$$t; <- this tells the WiFi to connect to an Internet hotspot and attempt to read the current
 *       NIST time; if successful the WiFi sends a command to the ATmega328p telling it to update the time 
 *       stored in the real-time clock.
 *  
 *    4. $$$b; <- this tells the WiFi to set itself up as a UART-to-TCP bridge, and accept TCP connections 
 *       from any clients (like a smartphone or PC)
 *
 */

#include <ESP8266WiFi.h>
#include "esp8266.h"
/* #include <Wire.h> */

/*
 * TCP to UART
 */

#define TCP_PORT (73)
#define MAX_SRV_CLIENTS 3   /*how many clients should be able to telnet to this ESP8266 */
WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpServerClients[MAX_SRV_CLIENTS];

IPAddress apIP(192, 168, 1, 1);
String bridgeSSID = "Fox1";     /* Choose any SSID */
String bridgePW = "password";   /* minimum 8 characters. !!!CHANGE THIS!!! */
String tcpPort = String(TCP_PORT);

/* #define SerialDebug Serial1  */   /* Debug on GPIO02 */
#define SerialUART   Serial      /* ESP8266 UART0 */

/*
 * NIST Time Sync
 */

String hotspotSSID = "MyRouter";
String hotspotPW = "password";
String tempStr;
String timeHost = "time.nist.gov";
String timeHTTPport = "13";
int sda = 0;
int scl = 2;

/* TwoWire i2c; */

void setup()
{
	Serial.begin(9600);
	delay(100);
	pinMode(0, OUTPUT);     /* Allow the red LED to be controlled */
	digitalWrite(0, HIGH);  /* Turn off red LED */
}


void setupTCP2UART()
{
	WiFi.mode(WIFI_AP);
	WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
	WiFi.softAP(bridgeSSID.c_str(), bridgePW.c_str());

	/*start UART. Be sure to set the speed to match the speed of whatever is
	 *connected  to the UART. */
	SerialUART.begin(9600);

	/* Start TCP listener on port TCP_PORT */
	tcpServer.begin();
	tcpServer.setNoDelay(true);
	Serial.print("Ready! Use 'telnet or nc ");
	Serial.print(WiFi.localIP());
	Serial.print(' ');
	Serial.print(tcpPort);
	Serial.println("' to connect");
}

BOOL setupWiFiAPConnection()
{
	BOOL err = FALSE;
	int tries = 0;

	/* We start by connecting to a WiFi network */

	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(hotspotSSID);

	WiFi.disconnect();
	delay(1500);

	WiFi.begin((const char*)hotspotSSID.c_str(), (const char*)hotspotPW.c_str());

	while(WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
		tries++;
		if(tries > 30)
		{
			err = TRUE;
			break;
		}
	}

	if(!err)
	{
		Serial.println("");
		Serial.println("WiFi connected!");
		Serial.print("My assigned LAN IP address: ");
		Serial.println(WiFi.localIP());
		Serial.println();
	}

	return( err);
}


int value = 0;
BOOL done = FALSE;
int count = 0;
int32_t timeVal = 0;
BOOL timeWasSet = FALSE;
unsigned long relativeTimeSeconds;
unsigned long lastAccessToNISTServers = 0;

int escapeCount = 0;
BOOL commandInProgress = FALSE;

int blinkPeriodMillis = 500;

int commaLoc;
int nextCommaLoc;
String arg1;
String arg2;
BOOL argumentsRcvd = FALSE;

void loop()
{
	while(!done)
	{
		unsigned long holdTime;
		BOOL toggle;

		if(commandInProgress)
		{
			int len;
			String command;

			Serial.println("Command received...");
			command = Serial.readStringUntil(';');
			len = command.length();

			if(len)
			{
				value = command.c_str()[0];

				/* Extract arguments from commands containing them */
				if((value == 'M') || (value == 'm'))
				{
					argumentsRcvd = FALSE;

					if(len > 1)
					{
						arg1 = "";
						arg2 = "";

						Serial.println("Parsing command...");

						commaLoc = 1 + command.indexOf(",");

						if(commaLoc > 0)
						{
							nextCommaLoc = command.indexOf(",", commaLoc);

							if(nextCommaLoc > 0)
							{
								arg1 = command.substring(commaLoc, nextCommaLoc);

								commaLoc = 1 + command.indexOf(",", nextCommaLoc);

								if(commaLoc > 0)
								{
									nextCommaLoc = command.indexOf(",", commaLoc);

									if(nextCommaLoc > 0)
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

						if(arg1.length() > 0)
						{
							Serial.println(arg1);
						}
						if(arg2.length() > 0)
						{
							Serial.println(arg2);
						}

						argumentsRcvd = ((arg1.length() > 0) && (arg2.length() > 0));
					}
				}

				done = TRUE;
			}

			Serial.println(command + " " + String(value));

			commandInProgress = FALSE;
		}
		else if(Serial.available() > 0) /* search for escape sequency $$$ */
		{
			value = Serial.read();

			Serial.println("$...");
			if(value == '$')
			{
				escapeCount++;

				if(escapeCount == 3)
				{
					commandInProgress = TRUE;
					escapeCount = 0;
					blinkPeriodMillis = 250;
				}
			}
			else
			{
				escapeCount = 0;
			}
		}

		relativeTimeSeconds = millis() / blinkPeriodMillis;
		if(holdTime != relativeTimeSeconds)
		{
			holdTime = relativeTimeSeconds;
			toggle = !toggle;
			digitalWrite(0, toggle);    /* Blink red LED */
		}
	}

	switch(value)
	{
		/* Time: Sync to NIST */
		case 'T':
		case 't':
		{
			Serial.println("Processing T command...");
			digitalWrite(0, LOW);   /* Turn on red LED */

			if(setupWiFiAPConnection())
			{
				Serial.println("Error connecting to router!");
			}
			else
			{
				getNistTime();
			}

			if(timeWasSet)
			{
				digitalWrite(0, HIGH);  /* Turn off red LED */
			}

			WiFi.disconnect();
			done = FALSE;
		}
		break;

		/* Bridge TCP to UART */
		case 'b':
		case 'B':
		{
			Serial.println("Processing B command...");
			setupTCP2UART();
			tcp2UARTbridgeLoop();
			done = FALSE;
		}
		break;

		/* Memory: Set value of variables */
		case 'M':
		case 'm':
		{
			Serial.println("Processing M command...");

			if(argumentsRcvd)
			{
				/* Allow text to substitute for numeric values */
				if(arg1.equalsIgnoreCase("HOTSPOT_SSID"))
				{
					arg1 = String(HOTSPOT_SSID);
				}
				else if(arg1.equalsIgnoreCase("HOTSPOT_PW"))
				{
					arg1 = String(HOTSPOT_PW);
				}
				else if(arg1.equalsIgnoreCase("TIME_HOST"))
				{
					arg1 = String(TIME_HOST);
				}
				else if(arg1.equalsIgnoreCase("TIME_HTTP_PORT"))
				{
					arg1 = String(TIME_HTTP_PORT);
				}
				else if(arg1.equalsIgnoreCase("BRIDGE_SSID"))
				{
					arg1 = String(BRIDGE_SSID);
				}
				else if(arg1.equalsIgnoreCase("BRIDGE_PW"))
				{
					arg1 = String(BRIDGE_PW);
				}
				else if(arg1.equalsIgnoreCase("BRIDGE_TCP_PORT"))
				{
					arg1 = String(BRIDGE_TCP_PORT);
				}

				switch(arg1.toInt())
				{
					case HOTSPOT_SSID:
					{
						hotspotSSID = arg2;
					}
					break;

					case HOTSPOT_PW:
					{
						hotspotPW = arg2;
					}
					break;

					case TIME_HOST:
					{
						timeHost = arg2;
					}
					break;

					case TIME_HTTP_PORT:
					{
						timeHTTPport = arg2;
					}
					break;

					case BRIDGE_SSID:
					{
						bridgeSSID = arg2;
					}
					break;

					case BRIDGE_PW:
					{
						bridgePW = arg2;
					}
					break;

					case BRIDGE_TCP_PORT:
					{
						tcpPort = arg2;
					}
					break;

					default:
					{
					}
					break;
				}
			}

			Serial.println("Finished M command.");
			argumentsRcvd = FALSE;
			done = FALSE;
		}
		break;

		case 'U':
		case 'u':
		{
			Serial.println("Processing U command...");
/*      i2c.pins(sda, scl);
 *      i2c.begin(0xF8); */

			done = FALSE;
		}
		break;

		default:
		{
			if(value != 0)
			{
				Serial.println("Illegal command...");
			}
			done = FALSE;
		}
		break;
	}
}


void getNistTime(void)
{
	relativeTimeSeconds = millis() / 1000;
	while((relativeTimeSeconds - lastAccessToNISTServers) < 10) /* prevent servers being accessed more than once every 10 seconds */
	{
		Serial.println(String("Waiting..." + String(10 - (relativeTimeSeconds - lastAccessToNISTServers))));
		delay(1000);
		relativeTimeSeconds = millis() / 1000;
	}

	tempStr = String("Connecting to " + timeHost + "...");
	Serial.println(tempStr);

	/* Use WiFiClient class to create TCP connections */
	WiFiClient client;
	while(!client.connect((const char*)timeHost.c_str(), timeHTTPport.toInt()))
	{
		Serial.println("Initial connection failed!");
		client.stop();
		delay(5000);
	}

	do
	{
		delay(500); /* This delay seems to be required to ensure reliable return of the time string */

		Serial.println("Requesting time...");

		/* Read all the lines of the reply from server and print them to Serial
		 * Example: 57912 17-06-08 18:28:35 50 0 0 206.3 UTC(NIST) *
		 *          JJJJJ YR-MO-DA HH:MM:SS TT L H msADV UTC(NIST) OTM
		 *    where:
		 *       JJJJJ is the Modified Julian Date (MJD). The MJD has a starting point of midnight on November 17, 1858.
		 *       You can obtain the MJD by subtracting exactly 2 400 000.5 days from the Julian Date, which is an integer
		 *       day number obtained by counting days from the starting point of noon on 1 January 4713 B.C. (Julian Day zero).
		 *
		 *       YR-MO-DA is the date. It shows the last two digits of the year, the month, and the current day of month.
		 *
		 *       HH:MM:SS is the time in hours, minutes, and seconds. The time is always sent as Coordinated Universal Time (UTC).
		 *       An offset needs to be applied to UTC to obtain local time. For example, Mountain Time in the U. S. is 7 hours behind
		 *       UTC during Standard Time, and 6 hours behind UTC during Daylight Saving Time.
		 *
		 *       TT is a two digit code (00 to 99) that indicates whether the United States is on Standard Time (ST) or Daylight
		 *       Saving Time (DST). It also indicates when ST or DST is approaching. This code is set to 00 when ST is in effect,
		 *       or to 50 when DST is in effect. During the month in which the time change actually occurs, this number will decrement
		 *       every day until the change occurs. For example, during the month of November, the U.S. changes from DST to ST.
		 *       On November 1, the number will change from 50 to the actual number of days until the time change. It will decrement
		 *       by 1 every day until the change occurs at 2 a.m. local time when the value is 1. Likewise, the spring change is at
		 *       2 a.m. local time when the value reaches 51.
		 *
		 *       L is a one-digit code that indicates whether a leap second will be added or subtracted at midnight on the last day of the
		 *       current month. If the code is 0, no leap second will occur this month. If the code is 1, a positive leap second will be added
		 *       at the end of the month. This means that the last minute of the month will contain 61 seconds instead of 60. If the code is
		 *       2, a second will be deleted on the last day of the month. Leap seconds occur at a rate of about one per year. They are used
		 *       to correct for irregularity in the earth's rotation. The correction is made just before midnight UTC (not local time).
		 *
		 *       H is a health digit that indicates the health of the server. If H = 0, the server is healthy. If H = 1, then the server is
		 *       operating properly but its time may be in error by up to 5 seconds. This state should change to fully healthy within 10 minutes.
		 *       If H = 2, then the server is operating properly but its time is known to be wrong by more than 5 seconds. If H = 3, then a hardware
		 *       or software failure has occurred and the amount of the time error is unknown. If H = 4 the system is operating in a special maintenance
		 *       mode and both its accuracy and its response time may be degraded. This value is not used for production servers except in special
		 *       circumstances. The transmitted time will still be correct to within ±1 second in this mode.
		 *
		 *       msADV displays the number of milliseconds that NIST advances the time code to partially compensate for network delays. The advance
		 *       is currently set to 50.0 milliseconds.
		 *
		 *       The label UTC(NIST) is contained in every time code. It indicates that you are receiving Coordinated Universal Time (UTC) from the
		 *       National Institute of Standards and Technology (NIST).
		 *
		 *       OTM (on-time marker) is an asterisk (*). The time values sent by the time code refer to the arrival time of the OTM. In other words,
		 *       if the time code says it is 12:45:45, this means it is 12:45:45 when the OTM arrives. */

		done = 0;
		count = 0;

		while(!done)
		{
			String line = "";

			while(client.available())
			{
				line = client.readStringUntil('\r');
			}

			if(line == "")  /* no response from time server */
			{
				count++;
				Serial.println("Request Timeout!");

				if(count > 2)   /* If sending CR didn't help, try reconnecting */
				{
					Serial.println("Reconnecting...");
					client.stop();
					delay(2000);
					while(!client.connect((const char*)timeHost.c_str(), timeHTTPport.toInt())) /* reconnect to the same host */
					{
						Serial.println("Connection failed!");
						client.stop();
						delay(5000);
					}

					count = 0;
				}
				else    /* Not sure if this helps, but try sending a CR to server */
				{
					Serial.println("Sending CR...");
					client.write('\r');
					delay(1000);
				}
			}
			else
			{
				Serial.print("Received:");
				Serial.print(line);
				timeVal = stringToTimeVal((char*)line.c_str());
				tempStr = String("$TIM," + String(timeVal) + ";");
				Serial.println(tempStr);
				done = 1;
				timeWasSet = TRUE;
				digitalWrite(0, HIGH);  /* Turn off red LED */
			}
		}

		Serial.println("Closing connection");
		Serial.println();
		client.stop();
		lastAccessToNISTServers = millis() / 1000;
	}
	while(0);
}


int32_t stringToTimeVal(char *str)
{
	int32_t time_sec = 0;
	BOOL missingTens = FALSE;
	uint8_t index = 0;
	char field[3];
	char* instr;

	field[2] = '\0';
	field[1] = '\0';

	instr = strchr(str, ':');

	if(instr == NULL)
	{
		return( time_sec);
	}

	if(str > (instr - 2))   /* handle case of time format #:##:## */
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
	if(!missingTens)
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

	return(time_sec);
}

void tcp2UARTbridgeLoop()
{
	uint8_t i, j;
	char buf[1024];
	int bytesAvail, bytesIn;
	BOOL done = FALSE;

	escapeCount = 0;

	Serial.println("Bridge started");

	while(!done)
	{
		/*check if there are any new clients */
		if(tcpServer.hasClient())
		{
			for(i = 0; i < MAX_SRV_CLIENTS; i++)
			{
				/*find free/disconnected spot */
				if(!tcpServerClients[i] || !tcpServerClients[i].connected())
				{
					if(tcpServerClients[i])
					{
						tcpServerClients[i].stop();
					}
					tcpServerClients[i] = tcpServer.available();
					Serial.print("New client: "); Serial.println(i + 1);
					continue;
				}
			}

			/*no free/disconnected spot so reject */
			WiFiClient tcpServerClient = tcpServer.available();
			tcpServerClient.stop();
		}

		/*check clients for data */
		for(i = 0; i < MAX_SRV_CLIENTS; i++)
		{
			if(tcpServerClients[i] && tcpServerClients[i].connected())
			{
				/*get data from the telnet client and push it to the UART */
				while((bytesAvail = tcpServerClients[i].available()) > 0)
				{
					bytesIn = tcpServerClients[i].readBytes(buf, min(sizeof(buf), bytesAvail));
					if(bytesIn > 0)
					{
						SerialUART.write(buf, bytesIn);
						delay(0);
					}
				}
			}
		}

		/*check UART for data */
		while((bytesAvail = SerialUART.available()) > 0)
		{
			bytesIn = SerialUART.readBytes(buf, min(sizeof(buf), bytesAvail));

			if(bytesIn > 0)
			{
				/*push UART data to all connected telnet clients */
				for(i = 0; i < MAX_SRV_CLIENTS; i++)
				{
					if(tcpServerClients[i] && tcpServerClients[i].connected())
					{
						tcpServerClients[i].write((uint8_t*)buf, bytesIn);
						delay(0);
					}

					for(j = 0; j < bytesIn; j++)
					{
						if(buf[j] == '$')
						{
							escapeCount++;

							if(escapeCount == 3)
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
	}
}

