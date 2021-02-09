## Arc Welder: Anti-Stutter

Converts G0/G1 commands to G2/G3 commands.  This can greatly compress some GCode files and can reduce the number of GCodes sent to your printer when streaming GCode from OctoPrint.  This can reduce stuttering as long as your firmware correctly implements G2/G3 (arc) commands and is configured properly.

### Installation

#### Prerequisites

##### Python Development Package for Linux

Python is already installed if you are running OctoPrint, but if you are running on Linux, the python-dev package is required.

* If you are running OctoPi earlier than 0.17 and Python 2.7, this will already be installed.
* If you are running OctoPi 0.18+ and Python 3.7, this will already be installed.
* If you upgraded OctoPi 0.17 to Python 3 using the [upgrade script](https://octoprint.org/blog/2020/09/10/upgrade-to-py3/) this will already be installed.

The only time you will need to install it is if you have installed OctoPrint manually.

Before installing the dev package, run this command from a terminal to update your system:

```
sudo apt-get update -y
```

Next, install the Python 3 dev package with this command:
```
sudo apt-get install -y python3-dev
```
#### Installing from the Plugin Manager
Typically, you will want to install *Arc Welder* from the plugin repository.  This will ensure that you are using the latest release build.

1. If you are running Python 3, first look at the prerequisites section above to make sure you have all of the necessary files.
2. Open OctoPrint and click on the settings icon (wrench/spanner).
3. Click on the plugin manager in the left menu.
4. Within the plugin manager, click **Get More...**.
5. Enter *Arc Welder* in the search box, and click the *Install* button next to the plugin.  If you do not see the plugin listed in the search results, please see the next section to install from a specific URL.
6. After installation is complete (**it will take a lot longer than most other plugins** since **Arc Welder** must compile a custom python extension written in c++), reboot your pi when prompted.

#### Installing a Specific Version of Arc Welder

You can install a specific version of *Arc Welder* from a URL within the plugin manager, but you will need to find the right URL to do so.  This should only be done in special cases and is not recommended for general use.

##### Installation from a URL

1. Navigate to the [releases on Github](https://github.com/FormerLurker/ArcWelderPlugin/releases).
2. Select the release you are interested in (the most recent release is at the top), and click on the version number.  Note that **Pre-Releases** will be tagged, so avoid those if you are only interested in stable versions.
3. Read the release notes carefully.  It may contain important information!
4. Scroll to the bottom of the release page and ensure the **Assets** are expanded.
5. Right-click on the **Source code (zip)** and copy the link into your clipboard.
6. Open OctoPrint and click on the settings icon (wrench/spanner).
7. Click on the plugin manager in the left menu.
8. Within the plugin manager, click **Get More...** .
9. Paste the installation URL you copied into the **... from URL** text box and click the **Install** button to the right of the text box.
10. After installation is complete (**it will take longer than average** since **Arc Welder** must compile a c++ package).  Reboot your pi.

### Using Arc-Welder

By default, Arc-Welder will automatically convert any newly added GCode files and will create a new file containing the converted GCode.  Using the default settings, this new file will end with **.aw.gcode**.  If your GCode file was called **print.gcode**, the converted file will be **print.aw.gcode**.

You can also convert existing files from the file manager by clicking a new icon (compress icon - two arrows pointing towards each other) that will now be available within the file manager.  Note: You cannot convert files that have already been converted by *Arc Welder*!

When a new file is queued, it will display within the collapsable *Processor Tasks* section within the *Arc Welder* tab.  You can cancel any queued tasks by clicking on the red X icon.  When the file begins processing, a progress bar will display along with a bunch of cool statistics about the conversion process.  If your file is marked for printing (see the *print after processing* setting below), a printer icon will appear next to it.  Don't worry, Arc Welder will not print any queued files if a print starts while it is processing or queued.  In fact, to protect your print, arc welder will automatically cancel and re-queue any files that are currently processing.  You can also cancel a file that is currently processing by clicking on the red X next to its name.

Once a file has been converted, the *Arc Welder* tab will show detailed statistics within the tab about the conversion, including before/after file sizes, compression information, before and after segment length statistics, and more.  You can view these statistics at any time by selecting a welded file or by clicking the statistics icon in the file manager.

### Settings and Setup

*Arc Welder* is pre-configured and will work with no settings changes for most people.  However, there are a few settings you may want to investigate (see the *Use OctoPrint Settings* and *G90/G91 Influence Extruder* extruder settings in particular).  You can navigate to the settings either by navigating to the *Arc Welder* tab and clicking on the *Edit Settings* button or by opening the OctoPrint settings and finding the *Arc Welder* plugin in the left side menu.

#### Preprocessor Settings
These settings control the main aspects of the plugin and how your GCode file will be compressed.

* **Arc Welder Enabled** - Check or uncheck to enable or disable the plugin.  This prevents *Arc Welder* from converting any files and adding buttons to the file browser.  It will not remove the plugin from the tabs or settings pages.  If you want to truly disable *Arc Welder*, please do so in the plugin manager.
* **Resolution in MM** - This setting controls how much play *Arc Welder* has in converting GCode points into arcs.  If the arc deviates from the original points by + or - 1/2 of the resolution, the points will **not** be converted.  The default setting is 0.05 which means the arcs may not deviate by more than +- 0.025mm (that's a **really** tiny deviation).  Increasing the resolution will result in more arcs being converted but will make the tool paths less accurate.  Decreasing the resolution will result in fewer arcs but more accurate tool paths.  I don't recommend going above 0.1MM.  Higher values than that may result in print failure.
* **Path Tolerance %** - This feature controls the maximum allowable deviation in path length compared to the original length.  This setting can be higher than you'd expect (5% by default) especially for larger arcs due to the way the firmware implements arc interpolation.  However, future firmware versions may add corrections to the arc path to correct for a slight reduction in path length caused by inscribing the interpolated segments inside of the arc.  This setting would then be userful, at least until an extrusion correction factor can be added to the algorithm.
* **Maximum Arc Radius** - This is a safety feature to prevent unusually large arcs from being generated.  Internally, *Arc Welder* uses a constant to prevent an arc with a very large radius from being generated where the path is essentially (but not exactly) a straight line.  If it is not perfectly straight and if my constant isn't conservative enough, an extremely large arc could be created that may have the wrong direction of rotation.  The default value works fine for all of the gCode I've tested (it is about 1/7th of the radius of the worst errant arc I've encountered).  If you discover that you need to adjust this setting because of errant arcs, please [create an issue](https://github.com/FormerLurker/ArcWelderPlugin/issues/new) and let me know!  The default setting is **1000000 mm** or **1KM**.
* **Allow 3D Arcs (for vase mode)** - This is an experimental setting that allows you to use *Arc Welder* while printing in spiral vase mode.  Note that not all firmware that supports Arc commands (G2/G3) will also support z axis changes while printing arcs.  Use with caution.
* **File Processing Type** - There are three options here:
  * *All New Files* - All files added to OctoPrint will be automatically converted.
  * *Manual Processing Only* - Convert files by clicking on the compress button in the file manager.  Files that are already compressed will have the compress button disabled.

The default setting is *Automatic and Manual Processing*.
  
#### Output File Settings
Here you can control how *Arc Welder* will handle the output file.  It can either overwrite the source GCode file completely, or you can create a new file with a different name.

* **Overwrite Source File** - When selected, *Arc Welder* will overwrite the original file with the compressed version.  This will never occur if the source file is overwritten by the target. Default Value: disabled.
* **Target File Prefix** - When *Overwrite Source File* is disabled, *Arc Welder* will produce a new file with this prefix.  For example, if you use **AW_** as your prefix and your source file is called **print.gcode** the output file would be called **AW_print.gcode**.  Default:  NO PREFIX
* **Target File Postfix** - When *Overwrite Source File* is disabled, *Arc Welder* will produce a new file with this postfix before the file extension.  For example, if you use **.aw** for your postfix and your source file is called **print.gcode**, the resulting file would be called **print.aw.gcode**.  Default: .aw
Note: You can combine prefixes and postfixes if you like.
* **Print After Processing** - *Arc Welder* can start printing the processed file after it is finished processing.  Arc welder will not automatically start if your printer is currently printing, or if a previously queued file started printing automatically.  This is to prevent a new print from starting right after one finished.  Options:
  * *Always* - The target file will always be printed after processing.
  * *Print After Manual Processing* - The target file will be printed only after manual processing.
  * *Disabled* - The target file will never be printed.
* **Select After Processing** - *Arc Welder* can automatically select files after processing is complete.  Arc Welder cannot select files while printing.  Options:
  * *Always* - The target file will always be selected after processing.
  * *Uploaded Files* - Only uploaded files will be selected after processing.
  * *Disabled* - The target file will never be selected.

#### Source File Options
* **Source File Deletion** - *Arc Welder* can delete the source file in most situations.  It will never delete the source file if it is currently printing or if the source file is overwritten by the welded GCode.  The options are:
  * *Always* - The source file will always be deleted after processing is completed if possible.
  * *Disabled* - The source file will not be deleted.

#### Printer Settings
*Arc Welder* needs to know a few things about your printer's firmware to work properly.  It can also run some basic firmware tests to see if things are configured correctly.

* **Use OctoPrint Settings** - Octoprint has a setting for *G90/G91 influences extruder* in the *Features* tab.  Enabling *Use Octoprint Printer Settings* will cause *Arc Welder* to use OctoPrint's setting.  Default: Enabled
* **G90/G91 Influence Extruder** - If *Use Octoprint Feature Settings* is unchecked, *Arc Welder* will use this setting to determine if the G90/G91 command influences your extruder's axis mode.  This setting can be extremely important depending on your start gcode.  In general, Marlin 2.0 and forks should have this box checked.  Many forks of Marlin 1.x should have this unchecked, like the Prusa MK2 and MK3.  I will try to add a list of printers and the proper value for this setting at some point, as well as a GCode test script you can use to determine what setting to use.  Keep in mind that most slicers produce code that will work fine no matter what setting you choose here.  Default: Disabled
* **Check Firmware** *Arc welder* also has a nifty firmware checker (new in version 1.1) that can run some basic tests to see if arcs are supported and/or enabled.  It will display errors (arcs not enabled, firmware not supported) or warnings (arc generation has some issues) along with help files that should assist getting these issues solved.  The firmware library is updated from a separate github repository, so additional firmware info can be added without updating the *Arc Welder* plugin.  The plugin checks for updates each time OctoPrint boots.  Octoprint cannot check your firmware while printing.  The options here are:
  * **Automatically When Printer Connects ** - *Arc Welder* will run a firmware check any time a printer connects to OctoPrint.
  * **Manual Only** - *Arc Welder* will only check your firmware when you manually click the *Check Firmware* button on the tab.
  * **Disabled** - No firmware checking will be done and the firmware section will be hidden on the tab.  This is useful if you know your firmware works fine, but *Arc Welder* reports possible problems.
* **Firmware Library** - *Arc Welder* automatically checks for new firmware information when OctoPrint boots, but you can also check for updates to the firmware library by clicking on the *Check for Updates* button.  You must be online for this to work.
* **Library Version** - This is just informational, and shows the current firmware library version.

#### Tab Options

* **Show Current Run Configuration** - By default, *Arc Welder* will display a summary of your settings on the tab.  Unchecking this option will hide that completely.

#### Notification Settings

These settings allow you to control the notification toasts and progress display.

* **Show Task Queued Notification** - When enabled, *Arc Welder* will create a toast showing that a new task has queued.  Default: Enabled
* **Show Pre-Processing Started Notification** - When enabled, *Arc Welder* will create a toast showing that preprocessing has started.  Default: Disabled
* **Show Pre-Processing Completed Notification** - When enabled *Arc Welder* will create a toast showing that pre-processing has completed.  Default: Enabled

#### Logging Settings
Unfortunately, *Arc Welder* currently does not use the logging settings from the OctoPrint Logging module.  This is because *Arc Welder* uses module-level logging and needs to log from inside of a c++ extension module that I created to do the actual processing in a reasonable amount of time.  I already had some code to do this from my other project, Octolapse, so I reused it here.  In the future I may improve this, but for now if you want to log anything besides exceptions, you will need to adjust the *Arc Welder* settings to do so.  You will still find the resulting log files in the OctoPrint logging module, though you can also download from the *Arc Welder* settings page.

* **Clear Log** - Erases all information in the most recent ```plugin_arc_welder.log``` file.  Useful for creating a clean log file for debugging/Github issues.
* **Clear All Logs** - Erases all *Arc Welder* log information.  Useful for recovering space and creating a clean log file for debugging/github issues.
* **Download Log** - Downloads the most recent log file.
* **Modules to Log** and **Logging Level** - If you are asked to produce a log for debugging purposes, you will need to enable specific loggers.  For problems with the interface, add the ```arc_welder.__init__``` module logger set to DEBUG or VERBOSE.  For issues with the GCode file itself, use the ```arc_welder.gcode_conversion``` module set to DEBUG or VERBOSE.  **Warning:** logging the gcode_conversion module in DEBUG or VERBOSE will likely increase processing time by 10-20x and will produce a **huge** log file.  Only log if it is absolutely necessary.
* **Log to Console** - Prints the log to the console.  Only useful for running *Arc Welder* in a debugger really, so leave this disabled.

## Firmware Considerations
Your printer's firmware must be capable of printing G2/G3 commands to use the GCode produced by *Arc Welder*.  Additionally, arc support must be enabled and properly configured.  Firmware support varies, and many older versions produce arcs less accurately and more slowly than expected.

### Marlin
[Marlin](https://github.com/MarlinFirmware/Marlin/) has supported arc commands for a long time.  However, starting with [version 2.0.6](https://github.com/MarlinFirmware/Marlin/releases/tag/2.0.6) arc support has been greatly enhanced.  I recommend you upgrade to at least this version before using *Arc Welder* because your experience will be much better.  Arc support must be enabled in your Configuration_adv.h file.

For recent versions of Marlin (2.0.6 and above), you can send an ```M115``` to see if your firmware has *ARC_SUPPORT* enabled.  For earlier versions you can send an empty ```G2``` or ```G3``` command.  If your printer responds with ```unknown command```, arc support is not enabled.

If your printer is running a fork of Marlin, but arc support is not enabled or is buggy, I recommend creating an issue within the fork's repository.

> "A plugin that can convert curves into arcs will be massively welcome and should make a great improvement in performance and print results."
>
> **[`Scott Lahteine`](https://www.patreon.com/thinkyhead)** - _Creator of [Marlin Firmware](https://github.com/MarlinFirmware/Marlin/)_

### Prusa Firmware
[Prusa's fork of Marlin](https://github.com/prusa3d/Prusa-Firmware) does support G2/G3 commands, however the default settings can produce sharp corners for very small arcs.  I've only noticed this in a few of my test prints, so it is not a particularly common issue.  You should be able to see it on the roof of a Benchy if you look closely.  Reducing the *MM_PER_ARC_SEGMENT* setting slightly can correct this but can also introduce stuttering.  Reducing the value massively (say to 0.1mm) will introduce a LOT of stutter and is NOT recommended.  Please note that adjusting this setting currently requires a manual firmware recompile.

I have been toying with the firmware and have submitted a pull request to enhance the capabilities, but it hasn't made it into the firmware yet and may require further modifications.  I am planning to add some enhancements from Marlin 2.0.6 as well.  I also added some new GCodes for adjusting arc interpolating and for retrieving the firmware settings for arc generation.  You can view the pull request [here](https://github.com/prusa3d/Prusa-Firmware/pull/2657).  Feel free to give this pull request a thumbs up, but realize that it needs some work and that the good folks at [Prusa Research]([https://www.prusa3d.com/) have a lot on their plates.

Also, some very old versions of Prusa's firmware (I'm not sure exactly how old) do not support bed leveling adjustments during arc movements.  Please make sure you are using a recent version of the firmware so that interpolated movements are properly leveled.

### Klipper
[Klipper](https://github.com/KevinOConnor/klipper/) seems to handle G2/G3 commands with ease, as long as the *GCode_arcs* config section is enabled.  G2/G3 support was added on September 13, 2019, so make sure you update Klipper if you are using an older version.

### Other Firmware
Though G2/G3 support is not universal, nor are all implementations equal, it is relatively easy to test.  You can do so in the OctoPrint terminal by sending the the following commands, one at a time:

```
G90
G28 X Y
G0 X0 Y0
G2 X40 I20
```

If your printer supports arc commands, it should move across a small arc from the origin.  Please feel free to let me know if your firmware supports arc movements, and I may add it to the list.

**Warning**:  The above GCode has not been tested on all printers.  Please use it with caution and [report any issues here](https://github.com/FormerLurker/ArcWelderPlugin/issues).

### Other Firmware Considerations

Most firmware will convert G2/G3 commands to many small segments through a process called interpolation.  The length of these segments varies by implementation.  In most cases the interpolated segments are much closer together than the linear segments you will find within your GCode file, but it's impossible to know for sure without examining the firmware in detail.

All firmware that I am aware of will inscribe these interpolated segments within the arc.  These segments will be entirely within the arc, only touching it at the endpoints.  This will reduce the average radius slightly.  In most cases, this effect is minimal and has no practical impact.  However, in some odd cases, like a snap fitting that is extremely sensitive to changes in diameter, the effect may be noticeable.  The smaller the interpolated segments (all firmware controlled), the less of an effect there is.  In general, it will be a much smaller effect than normal variations in filament diameter.  I hope to find a solution to this problem.

G2/G3 support is not perfect at the moment, but I suspect things will start to improve as they become more common.  If you are willing and have the skills to improve G2/G3 support in any way, please do!

## Release Channel Support

If you are interested in testing release candidates (which I really appreciate), you can easily do so via the built-in *Software Update* plugin.

### Change Release Channel for OctoPrint 1.5.x and Above
Starting with Octoprint 1.5.0, release channels can be selected on a per-plugin basis.  I have not seen this feature in action, but I believe *Arc Welder* is properly configured to use it once it becomes available.  I will will add instructions as soon as the feature becomes available as a release candidate.

### Change Release Channel for OctoPrint 1.4.x and Below

1. Open the Octoprint Settings (wrench/spanner icon).
2. Click on the *Software Update* link in the left menu.  This will open the *Software Update* plugin.
3. Edit the *Software Update* settings by clicking on the wrench/spanner icon in the upper right hand corner of the *Software Update* plugin.
4. Switch the *Octoprint Release Channel* drop down box to one of the following options:
   * *Stable* - Automatically update to the most stable release version.  This is recommended for most users.
   * *Maintenance RCs* - Well-tested, but not perfected.  This branch often contains hot-fixes and new features.  Recommended for advanced users who want to help improve *Arc Welder*, knowing there may be some additional bugs.
   * *Devel RCs* - Contains less tested code, but bleeding edge features and fixes.  Not recommended for most users.  If you submit a feature request or a bug, I may direct you to install from this branch at some point.

## Troubleshooting

*Arc Welder* is a pretty new concept, so it is likely there are some undiscovered issues.  However, there are a few known problems that are described below.

**1. The resulting GCode stutters even when printing directly from SD**

If you experience stuttering when printing from OctoPrint, first try printing the *Arc Welder* GCode directly from your SD card.  If you STILL experience stuttering, this is probably a firmware issue of some kind (though maybe not).

For people using Marlin 1.x and forks of Marlin 1.X (Including Prusa firmware for MK2/MK3), you may need to alter your configuration_adv.h file.  Check ``MM_PER_ARC_SEGMENT`` and slowly increase the value until the stuttering goes away.  A value of 1 (1MM) generally works well from a performance perspective but may result in some flat curves if they are very small.

For people using Marlin 2.0, please upgrade to version 2.0.6 or above.  Several critical enhancements were added that will improve arc accuracy and speed.

**2. My printer does not support G2/G3.**

If you are running Marlin or a fork, make sure Arc Support is enabled by looking at configuration_adv.h. Then make sure the following line exists and is not commented out:

```#define ARC_SUPPORT                 // Disable this feature to save ~3226 bytes```

Verify that you have enough memory to enable this feature if it is disabled.  You may have to disable some other feature to free up some memory.

**3. I cannot install the plugin.**

If you are running Python 3, confirm that you have the dev package installed.  Please see the installation instructions above for details.

If you are still having problems, please (create an issue)[https://github.com/FormerLurker/ArcWelderPlugin/issues/new] and be sure to include the plugin_pluginmanager_console.log file, which you can find by opening the Octoprint Settings (wrench/spanner icon), clicking on the **Logging** menu, then finding and downloading the proper log file.  Please upload the log file to (gist.github.com)[https://gist.github.com] and place a link within the issue.  Also include the OctoPrint version, the version of python you are running, and the OS version. If you are using OctoPi, please include that version as well.

**4.  I see no improvement in my prints.**

If you did not have any problems running the original GCode, *Arc Welder* isn't likely to improve your quality at all.  If you had stuttering before AND after using GCode produced by *Arc Welder* AND you have tested the new GCode by printing directly from SD, see issue 1 above.  You may need to create a support ticket.

**5.  My print failed when using *Arc Welder*, but the original GCode printed fine.**

I recommend running the GCode through an analyzer that supports arcs (Simplify3D does, and [nc viewer](https://ncviewer.com) is a good online tool, though it has some problems) to see if there are any obvious issues with the GCode.

If the code looks good, please try printing *Arc Welder* GCode again to see if it fails in the same spot.  If it does, please report the issue!  See the *Reporting Issues* section below for info.

**6.  I'm having trouble installing the *ArcWelder* plugin.**

I will update this as solutions to installation problems present themselves, so check back or create an issue if your specific problem has no solution.or

First, if you are trying to install to a linux machine (this will not be necessary when running OctoPi), you may not have a c++ compiler installed, or your compiler might not meet the minimum requirements.  If you see an error like this, you will need to install a compiler:  ```686-linux-gnu-gcc: error trying to exec 'cc1plus': execvp: No such file or directory```

Try connecting to a terminal and running the following command:  ```sudo apt-get install g++```

## Reporting Issues

If you have a problem using *Arc Welder*, please first check the open and closed issues.  If you find an existing issue that is close to your own, please read through it and see if there are any suggested fixes.  If your issue is unique, consider creating a new issue.  However, please don't use the Github issues as general technical support.  They are for reporting potential bugs in the software.  When in doubt, go ahead and [create an issue here](https://github.com/formerlurker/arcwelderplugin/issues).  I do want to help no matter what your problem is, but I spend so much time time dealing with issues and I'd rather be improving the software if at all possible.  Bug reports improve software, tech support does not.  Thanks in advance!

## License

View the [*Arc Welder* license](https://raw.githubusercontent.com/FormerLurker/ArcWelderPlugin/master/LICENSE).
