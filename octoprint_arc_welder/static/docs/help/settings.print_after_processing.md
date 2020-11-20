 Arcwelder can automatically print the processed file after completion when possible.  The available options are:

 * *Always* - The target file will always be printed after processing.
  * *Print After Manual Processing* - The target file will be printed only after manual processing.
 * *Disabled* - The target file will never be printed.

If you wish to start your print directly from your slicer, using *Print After Automatic Processing* may be a good option.  However, realize that uploading a gcode file to OctoPrint will also trigger printing in this case.

Files that will print after processing will be labeled with a printer icon (<i class="fa fa-print"></i>).  Note that not all files processed by *Arc Welder* that are flagged for printing will actually print.  Only one file is allowed to print from the queue at a time, which will cancel the print option for all subsequent files.

Note: Arcwelder can only print the target file if OctoPrint is not printing.  If a print is started while arc-welder is processing, or if any files are queued for processing, this option will be disabled for those files to prevent a new print from starting immediately after the current print completes.

