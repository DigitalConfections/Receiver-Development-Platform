Version 0.7.15 - 11 Dec 2017
Hardware Compatibility: Rev X1
Changes:
o Added functionality to the existing PRE (preamp) command, allowing
it to set the 80m front end VGA DAC controlling its gain (0-255)
o Added driver files for DAC081C085.


Version 0.7.14 - 2 Dec 2017
Hardware Compatibility: Rev X1
Changes:
o Adds "PRE" command to support turning 2m preamp on/off
o Attenuation now is settable from [0 to 100] with 100=max attenuation
o Minor bug fixes


Version 0.7.11 - 29 Nov 2017
Hardware Compatibility: Rev X1
Changes:
o Fixes issues with setting and restoring volume settings.
o Tone volume setting now ranges from 0 to 15, like Main volume.


Version 0.7.10 - 25 Nov 2017
Hardware Compatibility: Rev X1
Changes:
o Add the "A" command for setting receiver attenuation
o Improves filtering of RSSI when "SS" command is used
o Fixes a strange optimizer issue with the "BND" command by disabling
optimization for that command.