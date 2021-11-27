When enabled, ArcWelder will enforce a maximum length for any G2/G3 generated.  Commands larger than this will be rejected, and the original G0/G1 commands will be used.

When enabled, only values greater than 30 are allowed, as anything lower will prevent arcs completely.

Some firmware limits the maximum gcode length and/or the serial buffer width.  Sending commands larger than this to the printer will result in data corruption that can cause unexpected behavior.  In some extreme instances the printhead will travel around the extremeties of the build plate.  This behavior is erratic and is difficult to predict.

If you are printing via OctoPrint, be aware that the gcode will be expanded to include a line number and a checksum.  I'm not sure what the maximum number of characters added to the gcode is, but will update this document when that information is available.  Printing from the SD card will eliminate this overhead.

The gcode precision and maximum arc radius will affect the total gcode length, as will 3D arcs.  You can reduce the number of rejected gcodes by reducing the maximum arc radius, by reducing precision, or by turning off 3D arcs.  Arc welder will try to reduce the gcode length as much as possible before rejecting an arc.