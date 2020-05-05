##Arc Welder: Anti-Stutter

Converts G0/G1 commands to G2/G3 commands.  This can greatly compress some gcode files and can reduce the number of gcodes sent to your printer when streaming gcode from OctoPrint.  This can reduce stuttering as long as your firmware correctly implements G2/G3 (arc) commands and is configured properly.

### Installation
If you are using a pre-release version of **Arc Welder**, I recommend completely uninstalling and cleaning the files before trying to upgrade.  Once the installation has completed, make sure you reboot Octoprint before trying to use *Arc Welder**.

### Settings and Setup

**Arc welder** is pre-configured and will work with no settings changes for most people.  However, there are a few settings you may want to investigate.  You can navigate to the settings either by navigating to the **Arc Welder** tab and clicking on the *Edit Settings* button, or by opening the OctoPrint settings and finding the **Arc Welder** plugin in the left side menu.

#### Preprocessor Settings
These settings control the main aspects of the plugin and how your gcode file will be compressed.

* **Arc Welder Enabled** - Check or uncheck to enable or disable the plugin.  This prevents **Arc Welder** from converting any files and adding buttons to the file browser.  It will not remove the plugin from the tabs or settings pages.  If you want to truly disable **Arc Welder** please do so in the plugin manager.
* **Resolution in MM** - This setting controls how much play Arc Welder has in converting gcode points into arcs.  If the arc deviates from the original points by + or - 1/2 of the resolution, the points will **not** be converted.  The default setting is 0.05 which means the arcs may not deviate by more than +- 0.025mm (which is a **really** tiny deviation).  Increasing the resolution will result in more arcs being converted, but will make the tool paths less accurate.  Decreasing the resolution will result in fewer arcs, but more accurate toolpaths.  I don't recommend going above 0.1MM.  Higher values than that may result in print failure. 
* **File Processing Type** 
  * *Automatic Processing Only* - Newly uploaded files will be compressed automatically.
  * *Manual Processing Only* - Convert files by clicking on the compress button in the file manager.  Files that are already compressed will have the compress button disabled.
  * *Automatic and Manual Processing* - Newly uploaded files will automatically be converted **and** you will be able to compress files by clicking the compress button in the file manager.
  
#### Output File Settings
Here you can control how **Arc Welder** will handle the output file.  It can either overwrite the source gcode file completely, or you can create a new file with a different name.

* **Overwrite Source File** - When selected, Arc Welder will overwrite the original file with the compressed version.
* **Target File Prefix** - When *Overwrite Source File* is disabled, **Arc Welder** will produce a new file with this prefix.  For example, if you use **AW_** as your prefix, and your source file is called **print.gcode** the output file would be called **AW_print.gcode.
* **Target File Postfix** - When *Overwrite Source File* is disabled   **Arc Welder** will produce a new file with this postfix before the file extension.  For example, if you use **.aw** for your postfix, and your source file is called **print.gcode** the resulting file would be called **print.aw.gcode**.  You can combine prefixes and postfixes if you like.

#### Printer Settings
Arc welder needs to know one property of your printer's firmware to guarantee accurate results in all slicers and with all start/end gcode: G90/G91 influences extruder.

* **Use Octoprint Printer Settings** -  Octoprint has a setting for *G90/G91 influences extruder*  in the *Features* tab.  Enabling *Use Octoprint Printer Settings* will cause **Arc Welder** to use OctoPrint's setting.
* **G90/G91 Influence Extruder** - If *Use Octoprint Printer Settings* is unchecked, **Arc Welder** will use this setting to determine if the G90/G91 command influences your extruder's axis mode.  In general, Marlin 2.0 and forks should have this box checked.  Many forks of Marlin 1.x should have this unchecked, like the Prusa MK2 and MK3.  I will try to add a list of printers and the proper value for this setting at some point, as well as a gcode test script you can use to determine what setting to use.  Keep in mind that most slicers produce code that will work fine no matter what setting you choose here.

#### Notification Settings
These settings allow you to control the notification toasts and progress display.

* **Show Pre-Processing Started Notifications** - When enabled **Arc Welder** will create a toast showing that pre-processing is initializing.
* **Show Progress Bar** - When enabled **Arc Welder** will create a progress bar showing statistics about the file being currently processed.  There is also a *Cancel* and *Cancel All* button on the progress display.
* **Show Pre-Processing Completed Notification** - When enabled **Arc Welder** will create a toast showing that pre-processing has completed, including some interesting statistics.

#### Logging Settings
Unfortunately **Arc Welder** currently does not use the logging settings from the OctoPrint Logging module.  The reasons for this is because **Arc Welder** uses module level logging, and needs to log from inside of a c++ extension module that I created to do the actual processing in a reasonable amount of time.  I already had some code to do this from my other project, Octolapse, so I reused it here.  In the future I may improve this, but for now if you want to log anything besides exceptions, you will need to adjust the **Arc Welder** settings to do so.  You will still find the resulting log files in the OctoPrint logging module, though you can also download from the **Arc Welder** settings page.

* **Clear Log** - Erases all information in the most recent ```plugin_arc_welder.log``` file.  Useful for creating a clean log file for debugging/github issues.
* **Clear All Logs** - Erases all arc welder log information.  Useful for recovering space and creating a clean log file for debugging/github issues.
* **Download Log** - Downloads the most recent log file.
* Modules to Log and Logging Level - IF you are asked to produce a log for debugging purposes, you will need to enable specific loggers.  For problems with the interface add the ```arc_welder.__init__``` module logger set to debug or verbose.  For issues with the gcode file itself, use the ```arc_welder.gcode_conversion``` module set to DEBUG or VERBOSE.  **Warning:** logging the gcode_conversion module in DEBUG or VERBOSE will likely increase processing time by 10-20x, and will produce a **huge** log file.  Only log if it is absolutely necessary.
* **Log to Console** - Prints the log to the console.  Only useful for running **Arc Welder** in a debugger really, so leave this disabled. 

## Troubleshooting

**1. The resulting gcode stutters even when printing directly from SD**

If you experience stuttering when printing from OctoPrint, first try printing the Arc Welder gcode directly from your SD card.  If you STILL experience stuttering, this is probably a firmware issue of some kind (though maybe not).

For people using Marlin 1.x and forks of Marlin 1.X (Including Prusa firmware for MK2/MK3), you may need to alter your configuration_adv.h file.  Check ``MM_PER_ARC_SEGMENT`` and slowly increase the value until the stuttering goes away.  A value of 1 (1MM) generally works well from a performance perspective, but may result in some flat curves if they are very small. 

For people using Marlin 2.0, your results should generally be pretty good with the default settings.  However, there are a few bugs that you may need to fix if you encounter stuttering (I fully expect these to be resolved on the master branch, and one already is).  Check Marlin/src/gcode/motion/G2_G3.cpp on lines [219](https://github.com/MarlinFirmware/Marlin/blob/e0e87ca19a57dc42e9eb5e22d044b8f3c1116544/Marlin/src/gcode/motion/G2_G3.cpp#L219
) and [239](https://github.com/MarlinFirmware/Marlin/blob/e0e87ca19a57dc42e9eb5e22d044b8f3c1116544/Marlin/src/gcode/motion/G2_G3.cpp#L239) and replace ```seg_length``` with ```0``` (zero) if it appears.

**2. My printer does not support G2/G3.**

If you are running Marlin or a fork, make sure Arc Support is enabled by looking at configuration_adv.h and making sure the following line exists and is not commented out:
```#define ARC_SUPPORT                 // Disable this feature to save ~3226 bytes```

Make sure you have enough memory to enable this feature if it is disabled.  You may have to disable some other feature to free up some memory.

**3. I cannot install the plugin.**

This is tricky.  You may have to create a support ticket since the installation is quite a complicated one.  See *Reporting Issues* below.

**4.  I see no improvement in my prints.**

If you did not have any problems running the original gcode, **Arc Welder** isn't likely to improve your quality at all.  If you had stuttering before AND after using gocde produced by **Arc Welder** AND you have tested the new gcode by printing directly from SD, see issue 1 above.  You may need to create a support ticket. 

**5.  My print failed when using Arc Welder, but the original gcode printed file.**

I recommend running the gcode through an analyzer that supports Arcs (Simplify3D does, and [nc viewer](https://ncviewer.com) is a good online tools, though it has some problems) to see if there are any obvious issues with the gcode.

If the code looks good, please try printing Arc Welder gcode again to see if it fails in the same spot.  If it does please report the issue!  See the *Reporting Issues* section below for info.

## Reporting Issues

If you have a problem using **Arc Welder**, please first check the open and closed issues.  If you find an existing issue that is close to your own, please read through it and see if there are any suggested fixes.  If your issue is unique, consider creating a new issue.  However, please don't use the github issues as general technical support.  They are for reporting potential bugs in the software.  When in doubt, go ahead and [create an issue here](https://github.com/formerlurker/arcwelderplugin/issues).  I do want to help no matter what your problem is, but I spend so much time time dealing with issues and I'd rather be improving the software if at all possible.  Bug reports improve software, tech support does not.  Thanks in advance!