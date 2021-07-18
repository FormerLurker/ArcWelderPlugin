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
from multiprocessing import Process, Pipe
import octoprint_arc_welder.utilities as utilities
import octoprint_arc_welder.log as log
import time
import shutil
import copy
import os
import uuid
import PyArcWelder as converter # must import AFTER log, else this will fail to log and may crash
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
        completed_callback,
        get_cancellations_callback
    ):
        super(PreProcessorWorker, self).__init__()
        self._source_path = os.path.join(data_folder, "source.gcode")
        self._target_path = os.path.join(data_folder, "target.gcode")
        self._processing_file_path = None
        self._idle_sleep_seconds = 2.5  # wait at most 2.5 seconds for a rendering job from the queue
        # holds incoming tasks that need to be added to the _task_deque
        self._incoming_task_queue = task_queue
        self._task_deque = deque()
        self._is_printing_callback = is_printing_callback
        self._start_callback = start_callback
        self._progress_callback = progress_callback
        self._cancel_callback = cancel_callback
        self._failed_callback = failed_callback
        self._success_callback = success_callback
        self._completed_callback = completed_callback
        self._get_cancellations_callback = get_cancellations_callback
        self._is_processing = False
        self._current_task = None
        self._is_cancelled = False
        self.r_lock = threading.RLock()

    def cancel_all(self):
        with self.r_lock:
            # cancel the current task if it exists
            if self._current_task:
                self._current_task["is_cancelled"] = True
                self._current_task["is_cancelled_all"] = True
            # cancel all incoming tasks
            while not self._incoming_task_queue.empty():
                task = self._incoming_task_queue.get(False)
                logger.info("Preprocessing of %s has been cancelled.", task["processor_args"]["source_path"])
            # cancel all tasks in the dequeue
            while not len(self._task_deque) == 0:
                task = self._task_deque.pop()
                logger.info("Preprocessing of %s has been cancelled.", task["processor_args"]["source_path"])

    def is_processing(self):
        with self.r_lock:
            return (
                not self._incoming_task_queue.empty()
                or self._is_processing
                or len(self._task_deque) != 0
            )

    def get_tasks(self):
        results = []
        with self.r_lock:
            if self._current_task:
                # copy the task so the receiver can't mess with it
                temp_task = copy.deepcopy(self._current_task)
                # remove the progress callback, it is not json serializable
                task = {
                    "is_processing": True,
                    "task": temp_task
                }
                results.append(task)
            for existing_task in reversed(self._task_deque):
                # copy the task so the receiver can't mess with it
                temp_task = copy.deepcopy(existing_task)
                # remove the progress callback, it is not json serializable
                task = {
                    "is_processing": False,
                    "task": temp_task
                }
                results.append(task)
        return results

    def add_task(self, new_task):
        with self.r_lock:
            # first check for any tasks to cancel
            self._check_for_cancelled_tasks()
            results = {
                "success": False,
                "error_message": ""
            }
            new_task["guid"] = str(uuid.uuid4())
            source_path_on_disk = new_task["processor_args"]["source_path"]
            logger.info("Adding a new task to the processor queue at %s.", source_path_on_disk)
            # make sure the task isn't already being processed
            if (
                    self._current_task and
                    self._current_task["octoprint_args"]["source_path"] == new_task["octoprint_args"]["source_path"]
            ):
                results["error_message"] = "This file is currently processing and cannot be added again until " \
                                           "processing completes. "
                logger.info(results["error_message"])
                return results
            for existing_task in self._task_deque:
                existing_source_path_on_disk = existing_task["processor_args"]["source_path"]
                if existing_task["octoprint_args"]["source_path"] == new_task["octoprint_args"]["source_path"]:
                    results["error_message"] = "This file is already queued for processing and cannot be added again."
                    logger.info(results["error_message"])
                    return results

            if new_task["octoprint_args"]["print_after_processing"]:
                if self._is_printing_callback():
                    new_task["octoprint_args"]["print_after_processing"] = False
                    logger.info("The task was marked for printing after completion, but this has been cancelled "
                                "because a print is currently running")
                else:
                    logger.info("This task will be printed after preprocessing is complete.")
            self._task_deque.appendleft(new_task)
            results["success"] = True
            logger.info("The task was added successfully.")
            return results

    def remove_task(self, guid):
        with self.r_lock:
            if self._current_task and self._current_task["guid"] == guid:
                self._current_task["is_cancelled"] = True
                return self._current_task
            for existing_task in self._task_deque:
                if existing_task["guid"] == guid:
                    self._task_deque.remove(existing_task)
                    return existing_task
        return False

    # set print_after_processing to False for all tasks
    def prevent_printing_for_existing_jobs(self):
        with self.r_lock:
            # first make sure any internal queue items are added to the queue
            has_cancelled_print_after_processing = False
            current_process_cancelled = False
            for task in self._task_deque:
                if task["octoprint_args"]["print_after_processing"]:
                    task["octoprint_args"]["print_after_processing"] = False
                    task["print_after_processing_cancelled"] = True
                    has_cancelled_print_after_processing = True
                    path = task["processor_args"]["source_path"]
                    logger.info("Print after processing has been cancelled for %s.", path)
            # make sure the current task does not print after it is complete
            if self._current_task:
                current_process_cancelled = True
                if self._current_task["octoprint_args"]["print_after_processing"]:
                    self._current_task["octoprint_args"]["print_after_processing"] = False
                    has_cancelled_print_after_processing = True
                    self._current_task["print_after_processing_cancelled"] = True
                self._current_task["cancelled_on_print_start"] = True
                logger.info("The current task has been cancelled because a print has started.")
            return current_process_cancelled, has_cancelled_print_after_processing

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
                    self._current_task = task

                success = False
                try:
                    self._process(task)
                except Exception as e:
                    logger.exception("An unhandled exception occurred while preprocessing the gcode file.")
                    message = "An error occurred while preprocessing {0}.  Check plugin_arc_welder.log for details.".\
                        format(task["processor_args"]["source_path"])
                    self._failed_callback(task, message)
                finally:
                    with self.r_lock:
                        self._current_task = None
                    # send the completed callback
                    self._completed_callback(task)

            except queue.Empty:
                pass
            
    def _process(self, task):
        self._start_callback(task)
        logger.info(
            "Copying source gcode file at %s to %s for processing.",
            task["processor_args"]["source_path"],
            self._source_path
        )
        if not os.path.exists(task["processor_args"]["source_path"]):
            message = "The source file path at '{0}' does not exist.  It may have been moved or deleted". \
                format(task["processor_args"]["source_path"])
            self._failed_callback(task, message)
            return
        shutil.copy(task["processor_args"]["source_path"], self._source_path)
        source_name = utilities.get_filename_from_path(task["processor_args"]["source_path"])
        # Add arguments to the processor_args dict
        original_source_path = task["processor_args"]["source_path"]
        task["processor_args"]["source_path"] = self._source_path
        task["processor_args"]["target_path"] = self._target_path
        # Convert the file via the C++ extension
        logger.info(
            "Calling conversion routine on copied source gcode file to target at %s.", self._source_path
        )
        try:
            # create a new dict with the progress callback.  If this is not done we will have trouble
            # reporting the tasks later due to a deepcopy threading lock error
            # this is a bit ugly for python2+python3 compatibility
            processor_args = {}
            processor_args.update(task["processor_args"])
            processor_args.update({"on_progress_received": self._progress_received, "guid": task["guid"]})
            results = converter.ConvertFile(processor_args)
        except Exception as e:
            # It would be better to catch only specific errors here, but we will log them.  Any
            # unhandled errors that occur would shut down the worker thread until reboot.
            # Since exceptions are always logged, so this is reasonably safe.

            # Log the exception
            logger.exception(
                "An unexpected exception occurred while preprocessing %s.", task["processor_args"]["source_path"]
            )
            # create results that will be sent back to the client for notification of failure.
            results = {
                "is_cancelled": False,
                "success": False,
                "message": "An unexpected exception occurred while preprocessing the gcode file at {0}.  Please see "
                           "plugin_arc_welder.log for more details.".format(task["processor_args"]["source_path"])
            }
        # the progress payload will all be in bytes (str for python 2) format.
        # Make sure everything is in unicode (str for python3) because mixed encoding
        # messes with things.

        results["source_name"] = source_name
        if results.get("is_cancelled", False):
            if task.get("cancelled_on_print_start", False):
                # Restore the original source path and reset the target
                task["processor_args"]["source_path"] = original_source_path
                task["processor_args"]["target_path"] = ""
                # need to reset cancelled_on_print_start else it will just cancel over and over
                task["cancelled_on_print_start"] = False
                with self.r_lock:
                    self._task_deque.appendleft(task)
                logger.info(
                    "Preprocessing of %s has been cancelled automatically because printing has started.  Re-adding "
                    "task to the queue. "
                    , task["processor_args"]["source_path"])
                self._cancel_callback(task, True)
            elif task.get("is_cancelled_all", False):
                logger.info(
                    "Preprocessing of %s has been cancelled via the Cancel All button."
                    , task["processor_args"]["source_path"])
                self._cancel_callback(None, False)
            else:
                self._cancel_callback(task, False)
        elif results["success"]:
            logger.info("Preprocessing of %s completed.", task["processor_args"]["source_path"])
            with self.r_lock:
                # Clear out info about the current job
                self._current_task = None
            self._success_callback(
                task, results
            )
        else:
            self._failed_callback(task, results["message"])

        logger.info("Deleting temporary source.gcode file.")
        if os.path.isfile(self._source_path):
            os.unlink(self._source_path)
        logger.info("Deleting temporary target.gcode file.")
        if os.path.isfile(self._target_path):
            os.unlink(self._target_path)

    def _get_task(self, guid):
        with self.r_lock:
            if self._current_task and self._current_task["guid"] == guid:
                return self._current_task
            for existing_task in self._task_deque:
                if existing_task["guid"] == guid:
                    return existing_task
        return False

    def _check_for_cancelled_tasks(self):
        cancel_all, guids_to_cancel = self._get_cancellations_callback()
        cancelled_items = False
        if cancel_all:
            logger.info("Cancelling all processing tasks.")
            self.cancel_all()
        elif len(guids_to_cancel) > 0:
            cancelled_items = True
            for job_guid in guids_to_cancel:
                removed_task = self.remove_task(job_guid)
                if removed_task:
                    logger.info("Cancelled job with guid %s.", job_guid)
                    if not removed_task.get("is_cancelled", False):
                        self._cancel_callback(removed_task, False)
                else:
                    logger.info("Unable to cancel  job with guid %s.  It may be completed or it may have already been cancelled.", job_guid)

    def _progress_received(self, progress):
        is_cancelled = False
        logger.verbose("Progress Received: %s", progress)
        current_task = None
        try:
            with self.r_lock:
                self._check_for_cancelled_tasks()
                # the progress payload will all be in bytes (str for python 2) format.
                # Make sure everything is in unicode (str for python3) because mixed encoding
                # messes with things.
                #encoded_progresss = utilities.dict_encode(progress)
                #logger.verbose("Progress Received: %s", encoded_progresss)

                current_task = self._get_task(progress["guid"])
                is_cancelled = current_task.get("is_cancelled", False)
                #self._progress_callback(encoded_progresss, current_task)
            self._progress_callback(progress, current_task)
            if current_task.get("cancelled_on_print_start", False) or is_cancelled:
                return False
        except Exception as e:
            logger.exception("An error occurred receiving progress from the py_arc_welder.")
            return False
        finally:
            # allow other threads to process
            time.sleep(0.1)
        return not is_cancelled




