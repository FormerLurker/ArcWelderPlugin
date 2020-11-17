Arc Welder can, in some limited cases, detect files that were uploaded from various slicers.  It does this by looking for a comment in the first 100 lines of the uploaded gcode file to see if they were transported by your slicer.

Not all upload plugins are known, and not all add a comment, so this won't work 100% of the time out-of-the-box.  However, you can add a comment manually to your start gcode that will trigger processing if this option is enabled, but it will also trigger processing if you manually upload sliced files with that comment.

Here is a comment you can manually add to your start (NOT END!!!) gcode:

```
; AutoArcWeld=True
```