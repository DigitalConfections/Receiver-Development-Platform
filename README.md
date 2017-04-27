# Receiver-Development-Platform
This project contains source code for the hardware project described here: http://openardf.org/index.php/ardf-open-equipment-project/equipment-concept/. When built using Atmel Studio 7 it will generate object code to run on any of the processors utilized in that project. The following hardware devices contain processors utilizing this software:

Control Head - A user interface device capable of communicating over a serial bus (Linkbus)
Digital Interface - A small board that provides a digital interface between a serial bus (Linkbus) to a radio receiver (or other device) to which it is conncected.

The project was written as an Atmel Studio 7 project to build object code to run on an ATmega328P microprocessor. This project is written in C.

Due to the limited memory resources of the target processor, all software in this project was written to minimize the size of the object file. Driver files are written to contain only the functionality required for this project, so they do not necessarily support all the features of the devices for which they are written. This is an embedded project, with limited opportunities for the user to enter illegal values or perform illegal actions, so error checking is often omitted in order to improve performance (speed) and reduce object file size.

Main program entry and all interrupt service routines (ISRs) are contained in main.c. Most program control pre-compiler definitions are contained in defs.h. Minimalist driver support is contained in files named for the supported hardware device. Certain major features are held in their own file modules, including:

o Linkbus (linkbus.c) - a simple serial inter-processor communication protocol
o Receiver (receiver.c) - support specific to the dual-band ARDF receiver board

Sufficient (but limited) device driver support is provided for the following hardware products:

o Si5351: I2C configurable clock generator - http://www.silabs.com/documents/public/data-sheets/Si5351-B.pdf
o PCF8574: General-purpose remote I/O expansion via I2C-bus - http://www.nxp.com/documents/data_sheet/PCF8574_PCF8574A.pdf
o PCF2129: Accurate RTC with integrated quartz crystal - http://www.nxp.com/documents/data_sheet/PCF2129.pdf
o DS3231: Extremely Accurate I2C-Integrated RTC/TCXO/Crystal - http://datasheets.maximintegrated.com/en/ds/DS3231-DS3231S.pdf
o AD5245: 256-Position I2C®-Compatible Digital Potentiometer - http://www.analog.com/media/en/technical-documentation/data-sheets/AD5245.pdf
o ST7036: LCD controller with I2C - http://www.newhavendisplay.com/app_notes/ST7036.pdf
  with support for the NHD‐C0220BiZ‐FSW‐FBW‐3V3M 2x20 character LCD - http://www.newhavendisplay.com/specs/NHD-C0220BiZ-FSW-FBW-3V3M.pdf
 
// TODO: Add LSM303DLHC compass module support
// TODO: Add DAC081C085 support
// TODO: Add MAX5478EUD+ support
// TODO: Add AT24CS01-STUM support

This project builds with a minimum of effort under the Atmel Studio 7 Integrated Development Environment - http://www.atmel.com/microsite/atmel-studio/

TODO: Configuration information and import instructions for using this code under Atmel Studio 7:

All original source code is released under the "MIT License" - https://opensource.org/licenses/MIT
