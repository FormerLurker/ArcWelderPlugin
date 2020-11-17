Arc welder now has it's own repository of firmware information.  It uses this to help determine if your printer supports arc commands and to report any potential known issues and to steps for showing and resolving any issues.  I will try to keep this repository updated as new firmware information becomes available.

Clicking the *Check for Updates* button will cause Arc Welder to request more recent firmware information and will download any associated help files automatically.  The *Firmware Library Version* will update if any new information is found.  Please note that Arc Welder does an automatic check for new firmware information when OctoPrint starts.

If your firmware is not detected, consider creating an issue on the [ArcWelderPlugin repository](https://github.com/FormerLurker/ArcWelderPlugin/issues) including the following information:

* ```M115``` response from your printer - You can send ```M115``` via the OctoPrint terminal.  Just copy the printer's response including any additional lines that start with *Cap:*.  Here is an example:

```
Recv: FIRMWARE_NAME:Marlin 2.0.7.2 (Oct 31 2020 14:58:34) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin PROTOCOL_VERSION:1.0 MACHINE_TYPE:Ender-3 EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff
Recv: Cap:LEVELING_DATA:1
Recv: Cap:SERIAL_XON_XOFF:0
Recv: Cap:ARCS:1
Recv: ok
```
* Your printer's make and model.
* The firmware version and a link to the source code if it is available.

If you have had any failed prints using Arc Welder, but your firmware version is not known, please also include the following:

* A close up snapshot of the failed print.
* The source gcode file associated with any failed prints you have done.
* The welded gcode file you printed.

I will add new firmware information as soon as possible.

