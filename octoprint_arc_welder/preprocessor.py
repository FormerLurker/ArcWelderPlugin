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
import shutil
import os
import PyArcWelder as converter # must import AFTER log, else this will fail to log and may crasy
from collections import deque
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
        data_folder,
        task_queue,
        is_printing_callback,
        start_callback,
        progress_callback,
        cancel_callback,
        failed_callback,
        success_callback,
        completed_callback
    ):
        super(PreProcessorWorker, self).__init__()
        self._source_file_path = os.path.join(data_folder, "source.gcode")
        self._target_file_path = os.path.join(data_folder, "target.gcode")
        self._processing_file_path = None
        self._idle_sleep_seconds = 2.5  # wait at most 2.5 seconds for a rendering job from the queue
        # holds incoming tasks that need to be added to the _task_deque
        self._incoming_task_queue = task_queue
        self._task_deque = deque()
        self._is_printing_callback = is_printing_callback
        self.print_after_processing = False
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

    def cancel_all(self):
        with self.r_lock:
            while not self._incoming_task_queue.empty():
                task = self._incoming_task_queue.get(False)
                path = task["path"]
                preprocessor_args = task["preprocessor_args"]
                logger.info("Preprocessing of %s has been cancelled.", preprocessor_args["path"])
                self._cancel_callback(path, preprocessor_args)
            while not len(self._task_deque) == 0:
                task = self._task_deque.pop()
                path = task["path"]
                preprocessor_args = task["preprocessor_args"]
                logger.info("Preprocessing of %s has been cancelled.", preprocessor_args["path"])
                self._cancel_callback(path, preprocessor_args)

    def is_processing(self):
        with self.r_lock:
            return (
                not self._incoming_task_queue.empty()
                or self._is_processing
                or len(self._task_deque) != 0
            )

    def add_task(self, new_task):
        with self.r_lock:
            results = {
                "success": False,
                "error_message": ""
            }

            path = new_task["preprocessor_args"]["path"]
            logger.info("Adding a new task to the processor queue at %s.", path)
            # make sure the task isn't already being processed
            if new_task["path"] == self._current_file_processing_path:
                results["error_message"] = "This file is currently processing and cannot be added again until " \
                                           "processing completes. "
                logger.info(results["error_message"])
                return results
            for existing_task in self._task_deque:
                if existing_task["path"] == new_task["path"]:
                    results["error_message"] = "This file is already queued for processing and cannot be added again."
                    logger.info(results["error_message"])
                    return results

            if new_task["print_after_processing"]:
                if self._is_printing_callback():
                    new_task["print_after_processing"] = False
                    logger.info("The task was marked for printing after completion, but this has been cancelled "
                                "because a print is currently running")
                else:
                    logger.info("This task will be printed after preprocessing is complete.")
            self._task_deque.appendleft(new_task)
            results["success"] = True
            logger.info("The task was added successfully.")
            return results

    # set print_after_processing to False for all tasks
    def prevent_printing_for_existing_jobs(self):
        with self.r_lock:
            # first make sure any internal queue items are added to the queue
            has_cancelled_print_after_processing = False
            for task in self._task_deque:
                if task["print_after_processing"]:
                    task["print_after_processing"] = False
                    has_cancelled_print_after_processing = True
                    path = task["preprocessor_args"]["path"]
                    logger.info("Print after processing has been cancelled for %s.", path)
            # make sure the current task does not print after it is complete
            if self.print_after_processing:
                self.print_after_processing = False
                has_cancelled_print_after_processing = True
                logger.info("Print after processing has been cancelled for the file currently processing.")
            return has_cancelled_print_after_processing

    def run(self):
        while True:
            try:
                # see if there are any rendering tasks.
                time.sleep(self._idle_sleep_seconds)

                # see if we are printing
                is_printing = self._is_printing_callback()

                if is_printing:
                    # if we are printing, do not process anything
                    continue

                # We want this next step to be atomic
                with self.r_lock:
                    # the _task_deque is not thread safe. only use within a lock
                    if len(self._task_deque) < 1:
                        continue
                    # get the task
                    try:
                        task = self._task_deque.pop()
                    except IndexError:
                        # no items, they could have been cleared or cancelled
                        continue
                    self.print_after_processing = task["print_after_processing"]
                    self._current_file_processing_path = task["path"]
                    self._processing_cancelled_while_printing = False

                success = False
                try:
                    self._process(task)
                except Exception as e:
                    logger.exception("An unhandled exception occurred while preprocessing the gcode file.")
                    message = "An error occurred while preprocessing {0}.  Check plugin_arc_welder.log for details.".\
                        format(task["path"])
                    self._failed_callback(message)
                finally:
                    self._completed_callback()
                    with self.r_lock:
                        self._current_file_processing_path = None
                        self.print_after_processing = None
                        self._processing_cancelled_while_printing = False
            except queue.Empty:
                pass
            
    def _process(self, task):
        self._start_callback(task)
        logger.info(
            "Copying source gcode file at %s to %s for processing.",
            task["preprocessor_args"]["path"],
            self._source_file_path
        )
        if not os.path.exists(task["preprocessor_args"]["path"]):
            message = "The source file path at '{0}' does not exist.  It may have been moved or deleted". \
                format(task["preprocessor_args"]["path"])
            self._failed_callback(message)
            return
        shutil.copy(task["preprocessor_args"]["path"], self._source_file_path)
        source_filename = utilities.get_filename_from_path(task["preprocessor_args"]["path"])
        # Add arguments to the preprocessor_args dict
        task["preprocessor_args"]["on_progress_received"] = self._progress_received
        task["preprocessor_args"]["source_file_path"] = self._source_file_path
        task["preprocessor_args"]["target_file_path"] = self._target_file_path
        # Convert the file via the C++ extension
        logger.info(
            "Calling conversion routine on copied source gcode file to target at %s.", self._source_file_path
        )
        try:
            results = converter.ConvertFile(task["preprocessor_args"])
        except Exception as e:
            # It would be better to catch only specific errors here, but we will log them.  Any
            # unhandled errors that occur would shut down the worker thread until reboot.
            # Since exceptions are always logged, so this is reasonably safe.

            # Log the exception
            logger.exception(
                "An unexpected exception occurred while preprocessing %s.", task["preprocessor_args"]["path"]
            )
            # create results that will be sent back to the client for notification of failure.
            results = {
                "cancelled": False,
                "success": False,
                "message": "An unexpected exception occurred while preprocessing the gcode file at {0}.  Please see "
                           "plugin_arc_welder.log for more details.".format(task["preprocessor_args"]["path"])
            }
        # the progress payload will all be in bytes (str for python 2) format.
        # Make sure everything is in unicode (str for python3) because mixed encoding
        # messes with things.

        encoded_results = utilities.dict_encode(results)
        encoded_results["source_filename"] = source_filename
        if encoded_results["cancelled"]:
            auto_cancelled = self._processing_cancelled_while_printing
            self._processing_cancelled_while_printing = False
            if auto_cancelled:
                logger.info(
                    "Preprocessing of %s has been cancelled automatically because printing has started.  Readding "
                    "task to the queue. "
                    , task["preprocessor_args"]["path"])
                with self.r_lock:
                    task["print_after_processing"] = self.print_after_processing
                    self._task_deque.appendleft(task)
            else:
                logger.info(
                    "Preprocessing of %s has been cancelled by the user."
                    , task["preprocessor_args"]["path"])
            self._cancel_callback(task, auto_cancelled)
        elif encoded_results["success"]:
            logger.info("Preprocessing of %s completed.", task["preprocessor_args"]["path"])
            with self.r_lock:
                # It is possible, but unlikely that this file was marked as print_after_processing, but
                # a print started in the meanwhile
                # this must be done within a lock since print_after_processing can be modified by another
                # thread
                if not self.print_after_processing:
                    task["print_after_processing"] = False
                # Clear out info about the current job
                self._current_file_processing_path = None
                self.print_after_processing = None
            self._success_callback(
                encoded_results, task
            )
        else:
            self._failed_callback(encoded_results["message"], task)

        logger.info("Deleting temporary source.gcode file.")
        if os.path.isfile(self._source_file_path):
            os.unlink(self._source_file_path)
        logger.info("Deleting temporary target.gcode file.")
        if os.path.isfile(self._target_file_path):
            os.unlink(self._target_file_path)

    def _progress_received(self, progress):
        # the progress payload will all be in bytes (str for python 2) format.
        # Make sure everything is in unicode (str for python3) because mixed encoding
        # messes with things.
        encoded_progresss = utilities.dict_encode(progress)
        logger.verbose("Progress Received: %s", encoded_progresss)
        progress_return = self._progress_callback(encoded_progresss)

        if self._is_printing_callback():
            self._processing_cancelled_while_printing = True
            progress_return = False

        return progress_return




