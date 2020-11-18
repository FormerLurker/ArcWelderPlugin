Arc support is enabled by default on all Prusa running Buddy firmware.  You should be able to use Arc Welder in most cases, with a few caveats.

Arc performance on Buddy is quite good except for very small arcs, which can be printed as having noticeably flat edges.  This will only be visible on some models (you can see it on the roof of a Benchy, for example) and can be mostly corrected by reducing the MM_PER_ARC_SEGMENT setting.  Lowering the value too much will cause performance problems (suttering on arcs).  I've not had an opportunity to try one of these printers, so if you want to change these settings, you will have to experiment.  Also, changing this setting requires you to recompile the firmware, so you may want to stick with models that don't have arcs with a radius greater than about 4mm for now.

You can find the latest configuration file for the Prusa Mini [here](https://github.com/prusa3d/Prusa-Firmware-Buddy/blob/master/include/marlin/Configuration_A3ides_2209_MINI_adv.h).
Here are the firmware settings that apply to Arc Welder:

```
//
// G2/G3 Arc Support
//
#define ARC_SUPPORT // Disable this feature to save ~3226 bytes
#if ENABLED(ARC_SUPPORT)
    #define MM_PER_ARC_SEGMENT 1 // Length of each arc segment
    #define N_ARC_CORRECTION 25 // Number of intertpolated segments between corrections
//#define ARC_P_CIRCLES         // Enable the 'P' parameter to specify complete circles
//#define CNC_WORKSPACE_PLANES  // Allow G2/G3 to operate in XY, ZX, or YZ planes
#endif
```

```#define ARC_SUPPORT```

This line above enables arc support.  If this line does not exist, or if it is commented out(```//#if ENABLED(ARC_SUPPORT)```), arc support is disabled, and G2/G3 commands will not function.  This is the most important setting as far as Arc Welder is concerned.

Enabling arc support takes memory, and some boards (even some very popupar ones) don't have enough memory available to enable arcs without disabling other features.  If you find yourself in this position, I'd recommend reaching out to someone who knows your printer and board for advice.  It's definitely possible in the vast majority of cases.

```#define MM_PER_ARC_SEGMENT      1 // (mm) Length (or minimum length) of each arc segment```

This setting represents the default length of an arc segment in mm.  The smaller this number, the more accurate G2/G3 commands will be.  However, this also will increase the number of total segments, which may impact performance.

```#define N_ARC_CORRECTION       25 // Number of interpolated segments between corrections```

In order to produce arcs quickly on slower hardware, several approximations are used.  Since each arc segment is typically very small, the errors are quite small.  However, they can add up over time.  This setting controls how often the approximations are corrected.  The default here is probably fine.

```//#define ARC_P_CIRCLES           // Enable the 'P' parameter to specify complete circles```

This option isn't very useful for 3D printers, so I wouldn't bother enabling it for one.  Drawing complete circles will cause an overlap the width of your nozzle, which is just messy and wrong.  Now for a laser engraer or a CNC it's probably another story.

```//#define CNC_WORKSPACE_PLANES    // Allow G2/G3 to operate in XY, ZX, or YZ planes```

This isn't going to be something you need for Arc Welder, and I'm pretty sure it would not work with it anyway.
