<h2>Receiver Development Platform</h2>
<ul>This project contains source code for the <a href="https://groups.google.com/forum/#!forum/receiver-development-platform">Receiver Development Platform</a> and <a href="http://openardf.org/index.php/ardf-open-equipment-project/">Open ARDF Equipment Project</a>. When this software is built using Atmel Studio 7 it will generate object code to run on the processors utilized in that project. The following project components contain processors utilizing this software:</ul>

<ul><strong>Control Head </strong>- A user interface device capable of communicating over a serial bus (Linkbus)</ul>

<ul><strong>Digital Interface </strong>- A small board that provides a digital interface between a serial bus (Linkbus) to a radio receiver (or other device) to which it is connected.</ul>
<ul>This project is written using the Atmel Studio 7 integrated development environment, and is built to run on an ATmega328P microprocessor. This project is written in C.</ul>

<ul>Due to the limited memory resources of the target processor, all software in this project was written to minimize the size of the object file. Driver files are written to contain only the functionality required for this project, so they do not necessarily support all the features of the devices for which they are written. This is an embedded project, with limited opportunities for the user to enter illegal values or perform illegal actions, so error checking is often omitted in order to improve performance and reduce object file size.</ul>

<ul>Main program entry and all interrupt service routines (ISRs) are contained in main.c. Most program control pre-compiler definitions are contained in defs.h. Certain major features are held in their own file modules, including:</ul>
<ul>
	<li>
		<strong>Linkbus</strong> (linkbus.c) - a simple serial inter-processor communication protocol
	</li>
	<li>
		<strong>Receiver</strong> (receiver.c) - support specific to the dual-band ARDF receiver board
	</li>
</ul>
<ul>Minimalist driver support is contained in files named for the supported hardware device:</ul>
<ul>
	<li>
		<strong>Si5351</strong>: I2C configurable clock generator - http://www.silabs.com/documents/public/data-sheets/Si5351-B.pdf
	</li>
	<li>
		<strong>PCF8574</strong>: General-purpose remote I/O expansion via I2C-bus - http://www.nxp.com/documents/data_sheet/PCF8574_PCF8574A.pdf
	</li>
	<li>
		<strong>PCF2129</strong>: Accurate RTC with integrated quartz crystal - http://www.nxp.com/documents/data_sheet/PCF2129.pdf
	</li>
	<li>
		<strong>DS3231</strong>: Extremely Accurate I2C-Integrated RTC/TCXO/Crystal - http://datasheets.maximintegrated.com/en/ds/DS3231-DS3231S.pdf
	</li>
	<li>
		<strong>AD5245</strong>: 256-Position I2C®-Compatible Digital Potentiometer - http://www.analog.com/media/en/technical-documentation/data-sheets/AD5245.pdf
	</li>
	<li>
		<strong>ST7036</strong>: LCD controller with I2C - http://www.newhavendisplay.com/app_notes/ST7036.pdf
		with support for the NHD‐C0220BiZ‐FSW‐FBW‐3V3M 2x20 character LCD - http://www.newhavendisplay.com/specs/NHD-C0220BiZ-FSW-FBW-3V3M.pdf
	</li>
</ul>
// TODO: Add LSM303DLHC compass module support

// TODO: Add DAC081C085 support

// TODO: Add MAX5478EUD+ support

// TODO: Add AT24CS01-STUM support

This project builds with a minimum of effort under the Atmel Studio 7 Integrated Development Environment - http://www.atmel.com/microsite/atmel-studio/

TODO: Configuration information and import instructions for using this code under Atmel Studio 7:

All original source code is released under the "MIT License" - https://opensource.org/licenses/MIT
