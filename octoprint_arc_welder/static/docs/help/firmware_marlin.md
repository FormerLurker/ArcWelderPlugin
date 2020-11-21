Marlin version since v2.0.6 are fully recommended for use with Arc Welder as long as arc support is enabled.  If you are running an older version, I highly recommend you upgrade to the latest version.

If the *EXTENDED_CAPABILITIES_REPORT* firmware setting is enabled, Arc Welder will be able to detect if arcs are enabled.

## Configuring Marlin

In the most recent version of Marlin, as of the time of this writing, arc support can be enabled within the [configuration_adv.h](https://github.com/MarlinFirmware/Marlin/blob/2.0.x/Marlin/Configuration_adv.h) file.  You can find many [examples within a zip file](https://github.com/MarlinFirmware/Marlin/tree/2.0.x/config) on the Marlin github page.  The default settings are as follows:

```
//
// G2/G3 Arc Support
//
#define ARC_SUPPORT                 // Disable this feature to save ~3226 bytes
#if ENABLED(ARC_SUPPORT)
  #define MM_PER_ARC_SEGMENT      1 // (mm) Length (or minimum length) of each arc segment
  //#define ARC_SEGMENTS_PER_R    1 // Max segment length, MM_PER = Min
  #define MIN_ARC_SEGMENTS       24 // Minimum number of segments in a complete circle
  //#define ARC_SEGMENTS_PER_SEC 50 // Use feedrate to choose segment length (with MM_PER_ARC_SEGMENT as the minimum)
  #define N_ARC_CORRECTION       25 // Number of interpolated segments between corrections
  //#define ARC_P_CIRCLES           // Enable the 'P' parameter to specify complete circles
  //#define CNC_WORKSPACE_PLANES    // Allow G2/G3 to operate in XY, ZX, or YZ planes
  //#define SF_ARC_FIX              // Enable only if using SkeinForge with "Arc Point" fillet procedure
#endif
```

*Note:* In older versions of Marlin you may only have access to some of the settings, like MM_PER_ARC_SEGMENT and N_ARC_CORRECTION.  If so, I strongly recommend that you upgrade to the latest version.

This file must be configured before [compiling and installing Marlin 2.0](https://marlinfw.org/docs/basics/install.html).  Many of the settings below interact, so I don't recommend changing the defaults before you understand the effects.

Here are the setting that apply to Arc Welder:

```#define ARC_SUPPORT```

This line above enables arc support.  If this line does not exist, or if it is commented out(```//#if ENABLED(ARC_SUPPORT)```), arc support is disabled, and G2/G3 commands will not function.  This is the most important setting as far as Arc Welder is concerned.

Enabling arc support takes memory, and some boards (even some very popupar ones) don't have enough memory available to enable arcs without disabling other features.  If you find yourself in this position, I'd recommend reaching out to someone who knows your printer and board for advice.  It's definitely possible in the vast majority of cases.

```#define MM_PER_ARC_SEGMENT      1 // (mm) Length (or minimum length) of each arc segment```

This setting represents the default length of an arc segment in mm.  The smaller this number, the more accurate G2/G3 commands will be.  However, this also will increase the number of total segments, which may impact performance.  In general, I recommend leaving this setting alone because of some of the other settings below.

```//#define ARC_SEGMENTS_PER_R    1 // Max segment length, MM_PER = Min```

I'm not sure what this does at the moment, but I will research and update when I know.  Probably safe to leve it disabled.

```#define MIN_ARC_SEGMENTS       24 // Minimum number of segments in a complete circle```

This is a nifty feature that will add more detail as the radius of the arcs gets smaller.  This allows you get very high quality long AND short radius arcs.  Larger numbers can cause slowdown for very tiny arcs, but the default setting here works very well.  When this setting is enabled the maximum arc length is controled by the ```MM_PER_ARC_SEGMENT``` setting.  This is the combo I have used the most and recommend.

```//#define ARC_SEGMENTS_PER_SEC 50 // Use feedrate to choose segment length (with MM_PER_ARC_SEGMENT as the minimum)```

This setting is disabled by default, but I find it very interesting and want to try it out.  It basically keeps the number of segments constant over time.  If your printer is moving faster, the arc segments will be longer.  If you are printing slow they will be smaller.  In theory, your printer could produce the maximum number of arc segments possible without causing any slowdown.  When this feature is enabled the minimum arc length is governed by the ```MM_PER_ARC_SEGMENT``` setting.

```#define N_ARC_CORRECTION       25 // Number of interpolated segments between corrections```

In order to produce arcs quickly on slower hardware, several approximations are used.  Since each arc segment is typically very small, the errors are quite small.  However, they can add up over time.  This setting controls how often the approximations are corrected.  The default here is probably fine.

**Note**: The approximations for sin and cos were greatly enhanced since version 2.0.6, making this correction less important.  Still, the default is typically fine, but you may be able to get away with a higher number in later versions.

```//#define ARC_P_CIRCLES           // Enable the 'P' parameter to specify complete circles```

This option isn't very useful for 3D printers, so I wouldn't bother enabling it for one.  Drawing complete circles will cause an overlap the width of your nozzle, which is just messy and wrong.  Now for a laser engraer or a CNC it's probably another story.

```//#define CNC_WORKSPACE_PLANES    // Allow G2/G3 to operate in XY, ZX, or YZ planes```

This isn't going to be something you need for Arc Welder, and I'm pretty sure it would not work with it anyway.

```//#define SF_ARC_FIX              // Enable only if using SkeinForge with "Arc Point" fillet procedure```

If you are using SkeinForge you may want to look into this setting.  This is another one I don't know anything about.

