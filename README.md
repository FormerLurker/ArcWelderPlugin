## Arc Welder: Anti-Stutter

Converts G0/G1 commands to G2/G3 commands.  This can greatly compress some gcode files and can reduce the number of gcodes sent to your printer when streaming gcode from OctoPrint.  This can reduce stuttering as long as your firmware correctly implements G2/G3 (arc) commands and is configured properly.

### Installation

#### Prerequisites

##### Python Development Package for Linux

Python is already installed if you are running Octoprint, but if you are running on Linux, the python-dev package is required.  If you are running OctoPi and Python 2.7, this will already be installed.  At the time this was written the dev package of Python 3 is not included in OctoPi.

Before installing the dev package, run this command from a terminal to update your system:

```
sudo apt-get update -y
```

Next, install the python 3 dev package with this command:
```
sudo apt-get install -y python3-dev
```

#### Finding the Installation URL

You can install Arc Welder directly from the plugin manager within OctoPrint, but you will need to find a URL to do so.  Typically you will want to install the latest release. 

##### Installation URL for the Latest Release 
1. Navigate to the [releases on Github](https://github.com/FormerLurker/ArcWelderPlugin/releases).
2. Find the most recent release (the one on top) and click on the version number.  Note that **Pre-Release** will be tagged, so avoid those if you are only interested in stable versions.  At the time of this writing there are no stable releases because **Arc Welder** is still in beta.
3. Scroll to the bottom of the release page and ensure the **Assets** are expanded. 
4. Right click on the **Source code (zip)** and copy the link into your clipboard.

##### Installation URL for a Specific Branch
Currently there are 3 branches:

* Master - The main branch which is typically updated with the latest stable release.  Versions in the master will look something like this: **v0.1.0**
* rc/devel - This branch typically contains the latest prerelease code for versions that end in *dev*.  An example would be **v0.1.0rc1.dev3**.
* devel - This version contains development code, and should not be installed unless you are asked to do so.

Eventually another branch, rc/maintenance, will be added for maintenance releases.  These versions typically look like this: **0.1.0rc1**.

#### Installing from the Plugin Manager
1. Open OctoPrint and click on the settings icon (wrench/spanner)
2. Click on the plugin manager in the left menu
3. Within the plugin manager, click **Get More...** 
4. Paste the installation URL you copied into the **... from URL** text box and click the **Install** button to the right of the text box.
5. After installation is complete (**it will take longer than average** since **Arc Welcer** must compile a c++ package).  Reboot your pi.
 
**Important Notes:** If you are using a pre-release version of **Arc Welder**, I recommend completely uninstalling and cleaning the files before trying to upgrade.  Once the installation has completed, make sure you reboot Octoprint before trying to use *Arc Welder**.

### Using Arc-Welder

By default, Arc-Welder will automatically convert any newly uploaded gcode files and will create a new file containing the converted gcode.  Using the default settings, this new file will end with **.aw.gcode**.  If your gcode file was called **print.gcode** the converted file will be **print.aw.gcode**.

You can also convert existing files from the file manager by clicking a new icon (compress icon - two arrows pointing towards eachother) that will be available within the file manager.  Note:  You cannot convert files that have already been converted by **Arc Welder**!  

### Settings and Setup

**Arc welder** is pre-configured and will work with no settings changes for most people.  However, there are a few settings you may want to investigate.  You can navigate to the settings either by navigating to the **Arc Welder** tab and clicking on the *Edit Settings* button, or by opening the OctoPrint settings and finding the **Arc Welder** plugin in the left side menu.

#### Preprocessor Settings
These settings control the main aspects of the plugin and how your gcode file will be compressed.

* **Arc Welder Enabled** - Check or uncheck to enable or disable the plugin.  This prevents **Arc Welder** from converting any files and adding buttons to the file browser.  It will not remove the plugin from the tabs or settings pages.  If you want to truly disable **Arc Welder** please do so in the plugin manager.
* **Resolution in MM** - This setting controls how much play Arc Welder has in converting gcode points into arcs.  If the arc deviates from the original points by + or - 1/2 of the resolution, the points will **not** be converted.  The default setting is 0.05 which means the arcs may not deviate by more than +- 0.025mm (which is a **really** tiny deviation).  Increasing the resolution will result in more arcs being converted, but will make the tool paths less accurate.  Decreasing the resolution will result in fewer arcs, but more accurate toolpaths.  I don't recommend going above 0.1MM.  Higher values than that may result in print failure. 
* **Maximum Arc Radius** - This is a safety feature to prevent unusually large arcs from being generated.  Internally Arc Welder uses a constant to prevent  arcs with a very large radius from being generated where the path is essentially (but not exactly) a straight line.  If it is not perfectly straight, and if my constant isn't conservative enough, an extremely large arc could be created that may have the wrong direction of rotation.  The default value works fine for all of the gcode I've tested (it is about 1/7th of the radius of the worst errant arc I've encountered).  If you discover that you need to adjust this setting because of errant arcs, please [create an issue](https://github.com/FormerLurker/ArcWelderPlugin/issues/new) and let me know!  The default setting is **1000000 mm** or **1KM**.
* **File Processing Type** - There are three options here:
  * *Automatic Processing Only* - Newly uploaded files will be compressed automatically.
  * *Manual Processing Only* - Convert files by clicking on the compress button in the file manager.  Files that are already compressed will have the compress button disabled.
  * *Automatic and Manual Processing* - Newly uploaded files will automatically be converted **and** you will be able to compress files by clicking the compress button in the file manager.

The default setting is *Automatic and Manual Processing*.
  
#### Output File Settings
Here you can control how **Arc Welder** will handle the output file.  It can either overwrite the source gcode file completely, or you can create a new file with a different name.

* **Overwrite Source File** - When selected, Arc Welder will overwrite the original file with the compressed version.  Default Value: disabled.
* **Target File Prefix** - When *Overwrite Source File* is disabled, **Arc Welder** will produce a new file with this prefix.  For example, if you use **AW_** as your prefix, and your source file is called **print.gcode** the output file would be called **AW_print.gcode**.  Default:  NO PREFIX
* **Target File Postfix** - When *Overwrite Source File* is disabled   **Arc Welder** will produce a new file with this postfix before the file extension.  For example, if you use **.aw** for your postfix, and your source file is called **print.gcode** the resulting file would be called **print.aw.gcode**.  Default: .aw

Note:  You can combine prefixes and postfixes if you like.

#### Printer Settings
Arc welder needs to know one property of your printer's firmware to guarantee accurate results in all slicers and with all start/end gcode: G90/G91 influences extruder.

* **Use Octoprint Printer Settings** -  Octoprint has a setting for *G90/G91 influences extruder*  in the *Features* tab.  Enabling *Use Octoprint Printer Settings* will cause **Arc Welder** to use OctoPrint's setting.  Default: Enabled
* **G90/G91 Influence Extruder** - If *Use Octoprint Printer Settings* is unchecked, **Arc Welder** will use this setting to determine if the G90/G91 command influences your extruder's axis mode.  In general, Marlin 2.0 and forks should have this box checked.  Many forks of Marlin 1.x should have this unchecked, like the Prusa MK2 and MK3.  I will try to add a list of printers and the proper value for this setting at some point, as well as a gcode test script you can use to determine what setting to use.  Keep in mind that most slicers produce code that will work fine no matter what setting you choose here.  Default: Disabled

#### Notification Settings
These settings allow you to control the notification toasts and progress display.

* **Show Pre-Processing Started Notifications** - When enabled **Arc Welder** will create a toast showing that pre-processing is initializing.  Default: Enabled
* **Show Progress Bar** - When enabled **Arc Welder** will create a progress bar showing statistics about the file being currently processed.  There is also a *Cancel* and *Cancel All* button on the progress display.  Default: Enabled
* **Show Pre-Processing Completed Notification** - When enabled **Arc Welder** will create a toast showing that pre-processing has completed, including some interesting statistics.  Default: Enabled

#### Logging Settings
Unfortunately **Arc Welder** currently does not use the logging settings from the OctoPrint Logging module.  The reasons for this is because **Arc Welder** uses module level logging, and needs to log from inside of a c++ extension module that I created to do the actual processing in a reasonable amount of time.  I already had some code to do this from my other project, Octolapse, so I reused it here.  In the future I may improve this, but for now if you want to log anything besides exceptions, you will need to adjust the **Arc Welder** settings to do so.  You will still find the resulting log files in the OctoPrint logging module, though you can also download from the **Arc Welder** settings page.

* **Clear Log** - Erases all information in the most recent ```plugin_arc_welder.log``` file.  Useful for creating a clean log file for debugging/github issues.
* **Clear All Logs** - Erases all arc welder log information.  Useful for recovering space and creating a clean log file for debugging/github issues.
* **Download Log** - Downloads the most recent log file.
* **Modules to Log** and **Logging Level** - IF you are asked to produce a log for debugging purposes, you will need to enable specific loggers.  For problems with the interface add the ```arc_welder.__init__``` module logger set to debug or verbose.  For issues with the gcode file itself, use the ```arc_welder.gcode_conversion``` module set to DEBUG or VERBOSE.  **Warning:** logging the gcode_conversion module in DEBUG or VERBOSE will likely increase processing time by 10-20x, and will produce a **huge** log file.  Only log if it is absolutely necessary.
* **Log to Console** - Prints the log to the console.  Only useful for running **Arc Welder** in a debugger really, so leave this disabled. 

## Troubleshooting

Arc Welder is in pre-release at the moment, so it is likely there are some undiscovered issues.  However, there are a few known problems that are described below.

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

If you are running Python 3, make sure you have the dev package installed.  Please see the installation instructions above for details.

If you are still having problems, please (create an issue)[https://github.com/FormerLurker/ArcWelderPlugin/issues/new], and be sure to include the plugin_pluginmanageer_console.log file, which you can find by opening the Octoprint Settings (wrench/spanner icon), clicking on the **Logging** menu, then finding and downloading the proper log file.  Please upload the log file to (gist.github.com)[https://gist.github.com] and place a link within the issue.  Also include the OctoPrint version, the version of python you are running, and the OS version (if you are using OctoPi, please include that version as well). 

**4.  I see no improvement in my prints.**

If you did not have any problems running the original gcode, **Arc Welder** isn't likely to improve your quality at all.  If you had stuttering before AND after using gocde produced by **Arc Welder** AND you have tested the new gcode by printing directly from SD, see issue 1 above.  You may need to create a support ticket. 

**5.  My print failed when using Arc Welder, but the original gcode printed fine.**

I recommend running the gcode through an analyzer that supports Arcs (Simplify3D does, and [nc viewer](https://ncviewer.com) is a good online tools, though it has some problems) to see if there are any obvious issues with the gcode.

If the code looks good, please try printing Arc Welder gcode again to see if it fails in the same spot.  If it does please report the issue!  See the *Reporting Issues* section below for info.

## Reporting Issues

If you have a problem using **Arc Welder**, please first check the open and closed issues.  If you find an existing issue that is close to your own, please read through it and see if there are any suggested fixes.  If your issue is unique, consider creating a new issue.  However, please don't use the github issues as general technical support.  They are for reporting potential bugs in the software.  When in doubt, go ahead and [create an issue here](https://github.com/formerlurker/arcwelderplugin/issues).  I do want to help no matter what your problem is, but I spend so much time time dealing with issues and I'd rather be improving the software if at all possible.  Bug reports improve software, tech support does not.  Thanks in advance!