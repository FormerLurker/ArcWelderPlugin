 Arcwelder can automatically print the processed file after completion when possible.  The available options are:

 * *Always Print After Processing* - The target file will always be printed after processing.
 * *Print After Automatic Processing* - The target file will be printed only after automatic processing.
 * *Print After Manual Processing* - The target file will be printed only after manual processing.
 * *Disabled* - The target file will never be printed.

If you wish to start your print directly from your slicer, using *Print After Automatic Processing* may be a good option.  However, realize that uploading a gcode file to OctoPrint will also trigger printing in this case.

Note: Arcwelder can only select the target file if OctoPrint is not printing.  If a print is started while arc-welder is processing, or if any files are queued for processing, this option will be disabled for those files to prevent a new print from starting immediately after the current print completes.