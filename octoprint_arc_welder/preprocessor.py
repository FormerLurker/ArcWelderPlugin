# coding=utf-8
# #################################################################################
# Arc Welder: Anti-Stutter
#
# A plugin for OctoPrint that converts G0/G1 commands into G2/G3 commands where possible and ensures that the tool
# paths don't deviate by more than a predefined resolution.  This compresses the gcode file sice, and reduces reduces
# the number of gcodes per second sent to a 3D printer that supports arc commands (G2 G3)
#
# Copyright (C) 2020  Brad Hochgesang
# #################################################################################
# This program is free software:
# you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see the following:
# https://github.com/FormerLurker/ArcWelderPlugin/blob/master/LICENSE
#
# You can contact the author either through the git-hub repository, or at the
# following email address: FormerLurker@pm.me
##################################################################################
from __future__ import absolute_import
from __future__ import unicode_literals
import threading
import octoprint_arc_welder.utilities as utilities
import octoprint_arc_welder.log as log
import time
import os
import PyArcWelder as converter # must import AFTER log, else this will fail to log and may crasy
try:
    import queue
except ImportError:
    import Queue as queue

logging_configurator = log.LoggingConfigurator("arc_welder", "arc_welder.", "octoprint_arc_welder.")
root_logger = logging_configurator.get_root_logger()
# so that we can
logger = logging_configurator.get_logger(__name__)

class PreProcessorWorker(threading.Thread):
    """Watch for rendering jobs via a rendering queue.  Extract jobs from the queue, and spawn a rendering thread,
       one at a time for each rendering job.  Notify the calling thread of the number of jobs in the queue on demand."""
    def __init__(
        self,
        task_queue,
        is_printing_callback,
        save_callback,
        start_callback,
        progress_callback,
        cancel_callback,
        failed_callback,
        success_callback,
        completed_callback
    ):
        super(PreProcessorWorker, self).__init__()
        self._idle_sleep_seconds = 2.5 # wait at most 2.5 seconds for a rendering job from the queue
        self._task_queue = task_queue
        self._is_printing_callback = is_printing_callback
        self._save_callback = save_callback
        self._start_callback = start_callback
        self._progress_callback = progress_callback
        self._cancel_callback = cancel_callback
        self._failed_callback = failed_callback
        self._success_callback = success_callback
        self._completed_callback = completed_callback
        self._is_processing = False
        self._current_file_processing_path = None
        self._is_cancelled = False
        self.r_lock = threading.RLock()

    def is_processing(self):
        with self.r_lock:
            is_processing = (not self._task_queue.empty()) or self._is_processing
        return is_processing

    def run(self):
        while True:
            try:
                # see if there are any rendering tasks.
                time.sleep(self._idle_sleep_seconds)
                if self._is_printing_callback():
                    continue

                path, processor_args = self._task_queue.get(False)
                success = False
                try:
                    if not os.path.exists(processor_args["source_file_path"]):
                        message = "The source file path at '{0}' does not exist.  It may have been moved or deleted".\
                            format(processor_args["source_file_path"])
                        self._failed_callback(message)
                    self._process(path, processor_args)
                except Exception as e:
                    logger.exception("An unhandled exception occurred while preprocessing the gcode file.")
                    message = "An error occurred while preprocessing {0}.  Check plugin_arc_welder.log for details.".\
                        format(path)
                    self._failed_callback(message)
                finally:
                    self._completed_callback()
            except queue.Empty:
                pass
            
    def _process(self, path, processor_args):
        self._start_callback(path, processor_args)
        logger.info("Starting pre-processing with the following arguments:\n\tsource_file_path: "
                    "%s\n\ttarget_file_path: %s\n\tresolution_mm: %.3f\n\tg90_g91_influences_extruder: %r"
                    "\n\tlog_level: %d",
                    processor_args["source_file_path"], processor_args["target_file_path"],
                    processor_args["resolution_mm"], processor_args["g90_g91_influences_extruder"],
                    processor_args["log_level"])
        # Set the progress callback.
        processor_args["on_progress_received"] = self._progress_received
        # Convert the file via the C++ extension
        try:
            results = converter.ConvertFile(processor_args)
        except Exception as e:
            # It would be better to catch only specific errors here, but we will log them.  Any
            # unhandled errors that occur would shut down the worker thread until reboot.
            # Since exceptions are always logged, so this is reasonably safe.

            # Log the exception
            logger.exception(
                "An unexpected exception occurred while preprocessing %s.", processor_args["source_file_path"]
            )
            # create results that will be sent back to the client for notification of failure.
            results = {
                "cancelled": False,
                "success": False,
                "message": "An unexpected exception occurred while preprocessing the gcode file at {0}.  Please see "
                           "plugin_arc_welder.log for more details.".format(processor_args["source_file_path"])
            }
        # the progress payload will all be in bytes (str for python 2) format.
        # Make sure everything is in unicode (str for python3) because mixed encoding
        # messes with things.
        encoded_results = utilities.dict_encode(results)
        if encoded_results["cancelled"]:
            logger.info("Preprocessing of %s has been cancelled.", processor_args["source_file_path"])
            self._cancel_callback(path, processor_args)
        elif encoded_results["success"]:
            # Save the produced gcode file
            self._success_callback(encoded_results, path, processor_args)
        else:
            self._failed_callback(encoded_results["message"])

    def _progress_received(self, progress):
        # the progress payload will all be in bytes (str for python 2) format.
        # Make sure everything is in unicode (str for python3) because mixed encoding
        # messes with things.
        encoded_progresss = utilities.dict_encode(progress)
        logger.verbose("Progress Received: %s", encoded_progresss)
        return self._progress_callback(encoded_progresss)


