This setting controls how much an arc path can deviate as a percentage of the original path.  Keep in mind that the firmware will convert the arcs back to segments, which may represent the original path more closely.  A value of 5% is recommended.

Going lower than 0.1% will reduce Arc Welder's effectiveness and is restricted by the GUI.  You can bypass this restriction by editing the config.yaml, but it is not recommended.

Note:  due to the addition of advanced arc error detection, a high value for the path tolerance percent should not result in bad arc generation that could be found occasionally in previous releases.

