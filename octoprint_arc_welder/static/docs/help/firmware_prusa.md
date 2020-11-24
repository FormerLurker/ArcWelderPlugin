Arc support is enabled by default on all Prusa printers at least since the Mk2.  You should be able to use Arc Welder in most cases, with a few caveats.

Arc performance on the Mk2 and Mk3 is quite good except for very small arcs, which can be printed as having noticeably flat edges.  This will only be visible on some models (you can see it on the roof of a Benchy, for example) and can be mostly corrected by reducing the MM_PER_ARC_SEGMENT setting.  Going lower than about 0.25 will cause problems when printing fast.  There is no perfect setting here unfortunately.  Also, changing this setting requires you to recompile the firmware, so you may want to stick with models that don't have arcs with a radius greater than about 4mm for now.

At the time of this writing, firmware for the Mk2 and Mk3 run on a fork of Marlin 1, so the arc support enhancements made in Marlin 2.0.6 are not yet included.  I've been working on improving arc support in this firmware, and will update this document if there is any movement.

In the meantime, you can use the new *Firmware Compensation* functionality in the *Arc Welder* settings.  To use it, first enable *Firmware Compensation*, then set *MM Per Arc Segment* to 1.0 (the firmware default).  I recommend setting *Minimum Arc Segments* to 12.  You can adjust *Minimum Arc Segments* slightly up or down depending on your needs, but don't go too much higher or lower than that else you will see either poor compression (higher values) or will notice flat edges again for arcs of a small radius (lower values).

You can find the latest configuration file [here](https://github.com/prusa3d/Prusa-Firmware/blob/MK3/Firmware/Configuration_adv.h).
Here are the firmware settings that apply to Arc Welder:

```
// Arc interpretation settings:
#define MM_PER_ARC_SEGMENT 1
#define N_ARC_CORRECTION 25
```

```#define MM_PER_ARC_SEGMENT      1 // (mm) Length (or minimum length) of each arc segment``

This setting represents the default length of an arc segment in mm.  The smaller this number, the more accurate G2/G3 commands will be.  However, this also will increase the number of total segments, which may impact performance.

```#define N_ARC_CORRECTION       25 // Number of interpolated segments between corrections```

In order to produce arcs quickly on slower hardware, several approximations are used.  Since each arc segment is typically very small, the errors are quite small.  However, they can add up over time.  This setting controls how often the approximations are corrected.  The default here is probably fine.
