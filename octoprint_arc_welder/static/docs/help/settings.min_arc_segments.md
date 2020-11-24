This setting can be used as a workaround for firmware that tends to draw arc with a very small radius as a flat edge or an obvious polygon.  It controls how many segments should appear for a full circul of the same radius as the arc.

For older versions and forks of Marlin (including all current Prusa firmware), I recommend a value of 12 here.  Don't forget to also set *MM Per Arc Segment*!

This will only work if *mm per arc segment* is also greater than zero.  **Only set this if you know you need it!**.  Default: 0 = disabled