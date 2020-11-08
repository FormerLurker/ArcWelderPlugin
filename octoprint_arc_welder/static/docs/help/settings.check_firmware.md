*Arc Welder* can test your firmware for any known issues.  It is **highly** recommended that you allow *Arc Welder* to check your firmware each time your printer connects.  It is not possible to check all potential issues, but a great deal of effort was made to make this check as thorough as possible.

When *Arc Welder* performs a firmware test, it sends an ```M115``` to determine the firmware type and version.  It also examines any extended capabilities if your printer reports them.  If your printer's type and version can be determined, *Arc Welder* will see if it has any information about your firmware, and will report this to you.  Even if no information about your firmware is known, *Arc Welder* may be able to detect potential issues.  It will display any errors or warnings on the *Arc Welder* tab depending on the options you select.

If *Arc Welder* cannot determine if arcs are enabled or supported, it will send a naked ```G2``` command and will see how your printer responds.  It isn't always possible to detect arc support this way, but it is not intrusive, and will not affect your printer in any way.

The available options are:

* **Automatically When Printer Connects** - Each time your printer connects the firmware will be tested.  This is especially useful if you have more than one printer, or if you get a new printer.
* **Only Check Manually** - *Arc Welder* will not automatically test your firmware.  You can test it manually by using the **Check Firmware** button on the *Arc Welder* tab.
* **Disabled** - *Arc Welder* will not automatically test your firmware, and the firmware information will be hidden on the *Arc Welder* tab.

If your firmware is not known, please send me your printer's response to ```M115```, and the response to a naked ```G2``` command, as well as any other information about how your printer handles arc commands, and I will add it to the list!

