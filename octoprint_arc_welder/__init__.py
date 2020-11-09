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

import time
import datetime
import uuid
import threading
from distutils.version import LooseVersion
from six import string_types
from past.builtins import xrange
from flask import request, jsonify
import os
import octoprint.plugin
import tornado
from shutil import copyfile
from octoprint.server.util.tornado import LargeResponseHandler
from octoprint.server import util, app
from octoprint.filemanager import FileDestinations
from octoprint.server.util.flask import restricted_access
from octoprint.events import Events
import octoprint_arc_welder.log as log
import octoprint_arc_welder.preprocessor as preprocessor
import octoprint_arc_welder.utilities as utilities
import octoprint_arc_welder.firmware_checker as firmware_checker
# stupid python 2/python 3 compatibility imports

try:
    import queue
except ImportError:
    import Queue as queue

try:
    import urllib.parse as urllibparse
except ImportError:
    import urllib as urllibparse

logging_configurator = log.LoggingConfigurator("arc_welder", "arc_welder.", "octoprint_arc_welder.")
root_logger = logging_configurator.get_root_logger()
# so that we can
logger = logging_configurator.get_logger("__init__")
from ._version import get_versions

__version__ = get_versions()["version"]
__git_version__ = get_versions()["full-revisionid"]
del get_versions


class ArcWelderPlugin(
    octoprint.plugin.StartupPlugin,
    octoprint.plugin.TemplatePlugin,
    octoprint.plugin.SettingsPlugin,
    octoprint.plugin.AssetPlugin,
    octoprint.plugin.BlueprintPlugin,
    octoprint.plugin.EventHandlerPlugin
):

    if LooseVersion(octoprint.server.VERSION) >= LooseVersion("1.4"):
        import octoprint.access.permissions as permissions

        admin_permission = permissions.Permissions.ADMIN
    else:
        import flask_principal

        admin_permission = flask_principal.Permission(flask_principal.RoleNeed("admin"))

    FILE_PROCESSING_BOTH = "both"
    FILE_PROCESSING_AUTO = "auto-only"
    FILE_PROCESSING_MANUAL = "manual-only"

    SOURCE_FILE_DELETE_BOTH = "both"
    SOURCE_FILE_DELETE_AUTO = "auto-only"
    SOURCE_FILE_DELETE_MANUAL = "manual-only"
    SOURCE_FILE_DELETE_DISABLED = "disabled"

    PRINT_AFTER_PROCESSING_BOTH = "both"
    PRINT_AFTER_PROCESSING_MANUAL = "manual-only"
    PRINT_AFTER_PROCESSING_AUTO = "auto-only"
    PRINT_AFTER_PROCESSING_DISABLED = "disabled"

    SELECT_FILE_AFTER_PROCESSING_BOTH = "both"
    SELECT_FILE_AFTER_PROCESSING_MANUAL = "manual-only"
    SELECT_FILE_AFTER_PROCESSING_AUTO = "auto-only"
    SELECT_FILE_AFTER_PROCESSING_DISABLED = "disabled"

    CHECK_FIRMWARE_ON_CONECT = "connection"
    CHECK_FIRMWARE_MANUAL_ONLY = "manual-only"
    CHECK_FIRMWARE_DISABLED = "disabled"

    def __init__(self):
        super(ArcWelderPlugin, self).__init__()
        self.preprocessing_job_guid = None
        self.preprocessing_job_source_file_path = ""
        self.preprocessing_job_target_file_name = ""
        self.is_cancelled = False
        # Note, you cannot count the number of items left to process using the
        # _processing_queue.  Items put into this queue will be inserted into
        # an internal dequeue by the preprocessor
        self._processing_queue = queue.Queue()
        self.settings_default = dict(
            use_octoprint_settings=True,
            g90_g91_influences_extruder=False,
            resolution_mm=0.05,
            path_tolerance_percent=5.0,
            max_radius_mm=1000*1000,  # 1KM, pretty big :)
            overwrite_source_file=False,
            target_prefix="",
            target_postfix=".aw",
            notification_settings=dict(
                show_started_notification=True,
                show_progress_bar=True,
                show_completed_notification=True
            ),
            feature_settings=dict(
                file_processing=ArcWelderPlugin.FILE_PROCESSING_BOTH,
                delete_source=ArcWelderPlugin.SOURCE_FILE_DELETE_DISABLED,
                select_after_processing=ArcWelderPlugin.SELECT_FILE_AFTER_PROCESSING_BOTH,
                print_after_processing=ArcWelderPlugin.PRINT_AFTER_PROCESSING_DISABLED,
                check_firmware=ArcWelderPlugin.CHECK_FIRMWARE_ON_CONECT,
                current_run_configuration_visible=True
            ),
            enabled=True,
            logging_configuration=dict(
                default_log_level=log.ERROR,
                log_to_console=False,
                enabled_loggers=[],
            ),
            version=__version__,
            git_version=__git_version__,
        )
        # preprocessor worker
        self._preprocessor_worker = None
        # track removed files to handle moves
        self._recently_moved_files = []
        # a wait period for clearing recently moved files
        self._recently_moved_file_wait_ms = 1000

        # firmware checker
        self._firmware_checker = None

    def on_after_startup(self):
        logging_configurator.configure_loggers(
            self._log_file_path, self._logging_configuration
        )
        self._preprocessor_worker = preprocessor.PreProcessorWorker(
            self.get_plugin_data_folder(),
            self._processing_queue,
            self._get_is_printing,
            self.preprocessing_started,
            self.preprocessing_progress,
            self.preprocessing_cancelled,
            self.preprocessing_failed,
            self.preprocessing_success,
            self.preprocessing_completed,
        )
        self._preprocessor_worker.daemon = True
        logger.info("Starting the Preprocessor worker thread.")
        self._preprocessor_worker.start()


        # start the firmware checker
        logger.info("Creating the firmware checker.")
        self._firmware_checker = firmware_checker.FirmwareChecker(self._printer, self._basefolder, self.get_plugin_data_folder(), self.check_firmware_response_received)

        if self._check_firmware_on_connect:
            if self._printer.is_operational():
                # The printer is connected, and we need to check the firmware
                logger.info("Checking Firmware Asynchronously.")
                self.check_firmware()
            else:
                logger.warning("The printer is not connected, cannot check firmware yet.")
        logger.info("Startup Complete.")

    # Events
    def get_settings_defaults(self):
        # plugin_version is not instantiated when __init__ is called.  Update it now.
        self.settings_default["version"] = self._plugin_version
        return self.settings_default

    def on_settings_save(self, data):
        octoprint.plugin.SettingsPlugin.on_settings_save(self, data)
        # reconfigure logging
        logging_configurator.configure_loggers(
            self._log_file_path, self._logging_configuration
        )

    def get_template_configs(self):
        return [
            dict(
                type="settings",
                custom_bindings=True,
                template="arc_welder_settings.jinja2",
            )
        ]

    # Blueprints
    @octoprint.plugin.BlueprintPlugin.route("/cancelPreprocessing", methods=["POST"])
    @restricted_access
    def cancel_preprocessing_request(self):
        with ArcWelderPlugin.admin_permission.require(http_exception=403):
            request_values = request.get_json()
            cancel_all = request_values["cancel_all"]
            preprocessing_job_guid = request_values["preprocessing_job_guid"]
            if cancel_all:
                self._preprocessor_worker.cancel_all()

            if self.preprocessing_job_guid is None or preprocessing_job_guid != str(
                self.preprocessing_job_guid
            ):
                # return without doing anything, this job is already over
                return jsonify({"success": True})

            logger.info("Cancelling Preprocessing for /cancelPreprocessing.")
            self.preprocessing_job_guid = None
            self.is_cancelled = True
            return jsonify({"success": True})

    @octoprint.plugin.BlueprintPlugin.route("/clearLog", methods=["POST"])
    @restricted_access
    def clear_log_request(self):
        with ArcWelderPlugin.admin_permission.require(http_exception=403):
            request_values = request.get_json()
            clear_all = request_values["clear_all"]
            if clear_all:
                logger.info("Clearing all log files.")
            else:
                logger.info("Rolling over most recent log.")

            logging_configurator.do_rollover(clear_all=clear_all)
            return jsonify({"success": True})

    # Preprocess from file sidebar
    @octoprint.plugin.BlueprintPlugin.route("/process", methods=["POST"])
    @restricted_access
    def process_request(self):
        with ArcWelderPlugin.admin_permission.require(http_exception=403):
            if self._enabled:
                request_values = request.get_json()
                path = request_values["path"]
                origin = request_values["origin"]
                # decode the path
                path = urllibparse.unquote(path)
                # get the metadata for the file
                metadata = self._file_manager.get_metadata(origin, path)
                if "arc_welder" not in metadata:
                    # Extract only the supported metadata from the added file
                    additional_metadata = self.get_additional_metadata(metadata)
                    # add the file and metadata to the processor queue
                    success = self.add_file_to_preprocessor_queue(path, additional_metadata, True)
                    if success:
                        return jsonify({"success": True})
                    else:
                        return jsonify({"success": False})
            return jsonify({"success": False, "message": "Arc Welder is Disabled."})

    @octoprint.plugin.BlueprintPlugin.route("/restoreDefaultSettings", methods=["POST"])
    @restricted_access
    def restore_default_settings_request(self):
        with ArcWelderPlugin.admin_permission.require(http_exception=403):
            self._settings.set([], self.settings_default)
            # force save the settings and trigger a SettingsUpdated event
            self._settings.save(trigger_event=True)
            return jsonify({"success": True})

    @octoprint.plugin.BlueprintPlugin.route("/checkFirmware", methods=["POST"])
    @restricted_access
    def check_firmware_request(self):
        with ArcWelderPlugin.admin_permission.require(http_exception=403):
            logger.debug("Manual firmware request received.")
            return jsonify({"success": self.check_firmware()})

    @octoprint.plugin.BlueprintPlugin.route("/getFirmwareVersion", methods=["POST"])
    @restricted_access
    def get_firmware_version_request(self):
        with ArcWelderPlugin.admin_permission.require(http_exception=403):
            current_firmware = {
                "firmware_info": None
            }
            if self._firmware_checker:
                current_firmware = self._firmware_checker.get_current_firmware()
            return jsonify({"success": True, "firmware_info": current_firmware})

    # Callback Handler for /downloadFile
    # uses the ArcWelderLargeResponseHandler
    def download_file_request(self, request_handler):
        download_file_path = None
        # get the args
        file_type = request_handler.get_query_arguments('type')[0]
        if file_type == 'log':
            full_path = self._log_file_path
        if full_path is None or not os.path.isfile(full_path):
            raise tornado.web.HTTPError(404)
        return full_path

    def send_notification_toast(
        self, toast_type, title, message, auto_hide, key=None, close_keys=[]
    ):
        data = {
            "message_type": "toast",
            "toast_type": toast_type,
            "title": title,
            "message": message,
            "auto_hide": auto_hide,
            "key": key,
            "close_keys": close_keys,
        }
        self._plugin_manager.send_plugin_message(self._identifier, data)

    def check_firmware_response_received(self, result):
        if not result["success"]:
            self.send_notification_toast(
                "error",
                "Unable To Check Firmware",
                result["error"],
                False,
                "check-firmware",
                "check-firmware"
            )
        else:
            self.send_firmware_info_updated_message(result["firmware_version"])

    def send_firmware_info_updated_message(self, firmware_info):
        data = {
            "message_type": "firmware-info-update",
            "firmware_info": firmware_info,
        }
        self._plugin_manager.send_plugin_message(self._identifier, data)


    def check_firmware(self):
        if self._firmware_checker is None or not self._enabled:
            if self._enabled:
                logger.error("The firmware checker is None, cannot check firmware!")
            return False
        if not self._printer.is_operational():
            logger.warning("Cannot check firmware, printer not operational.")
            return False
        logger.info("Checking Firmware Capabilities")
        return self._firmware_checker.check_firmware_async()


    # ~~ AssetPlugin mixin
    def get_assets(self):
        # Define your plugin's asset files to automatically include in the
        # core UI here.
        return dict(
            js=[
                "js/showdown.min.js",
                "js/pnotify_extensions.js",
                "js/markdown_help.js",
                "js/arc_welder.js",
                "js/arc_welder.settings.js",
            ],
            css=["css/arc_welder.css"],
        )

    def _select_file(self, path):
        if path and len(path) > 1 and path[0] == '/':
            path = path[1:]
        self._printer.select_file(path, False)

    def _get_is_file_selected(self, path, origin):
        current_job = self._printer.get_current_job()
        current_file = current_job.get("file", {'path': "", "origin": ""})
        current_file_path = current_file["path"]
        # ensure the current file path starts with a /
        if current_file_path and current_file_path[0] != '/':
            current_file_path = '/' + current_file_path
        current_file_origin = current_file["origin"]
        return path == current_file_path and origin == current_file_origin

    def _get_is_printing(self, path=None):
        # If the printer is NOT printing, always return false
        if not self._printer.is_printing():
            return False
        # If the path parameter is provided, check for a locally printing file of the same path
        if path:
            return self._get_is_file_selected(path, FileDestinations.LOCAL)
        return True

    # Properties
    @property
    def _log_file_path(self):
        return self._settings.get_plugin_logfile_path()

    # Settings Properties
    @property
    def _logging_configuration(self):
        logging_configurator = self._settings.get(["logging_configuration"])
        if logging_configurator is None:
            logging_configurator = self.settings_default["logging_configuration"]
        return logging_configurator

    @property
    def _gcode_conversion_log_level(self):
        if "enabled_loggers" not in self._logging_configuration:
            return log.ERROR
        else:
            enabled_loggers = self._logging_configuration["enabled_loggers"]
        log_level = log.CRITICAL
        for logger in enabled_loggers:
            if logger["name"] == "arc_welder.gcode_conversion":
                log_level = logger["log_level"]
                break
        return log_level

    @property
    def _enabled(self):
        enabled = self._settings.get_boolean(["enabled"])
        if enabled is None:
            enabled = self.settings_default["enabled"]
        return enabled

    @property
    def _delete_source_after_manual_processing(self):
        return self._settings.get(["feature_settings", "delete_source"]) in [
            ArcWelderPlugin.SOURCE_FILE_DELETE_BOTH, ArcWelderPlugin.SOURCE_FILE_DELETE_MANUAL
        ] and not self._overwrite_source_file

    @property
    def _delete_source_after_automatic_processing(self):
        return self._settings.get(["feature_settings", "delete_source"]) in [
            ArcWelderPlugin.SOURCE_FILE_DELETE_BOTH, ArcWelderPlugin.SOURCE_FILE_DELETE_AUTO
        ] and not self._overwrite_source_file

    @property
    def _auto_pre_processing_enabled(self):
        return self._settings.get(["feature_settings", "file_processing"]) in [
            ArcWelderPlugin.FILE_PROCESSING_BOTH, ArcWelderPlugin.FILE_PROCESSING_AUTO
        ]

    @property
    def _use_octoprint_settings(self):
        use_octoprint_settings = self._settings.get_boolean(["use_octoprint_settings"])
        if use_octoprint_settings is None:
            use_octoprint_settings = self.settings_default["use_octoprint_settings"]
        return use_octoprint_settings

    @property
    def _g90_g91_influences_extruder(self):
        if self._use_octoprint_settings:
            g90_g91_influences_extruder = self._settings.global_get(
                ["feature", "g90InfluencesExtruder"]
            )
        else:
            g90_g91_influences_extruder = self._settings.get_boolean(
                ["g90_g91_influences_extruder"]
            )
        if g90_g91_influences_extruder is None:
            g90_g91_influences_extruder = self.settings_default[
                "g90_g91_influences_extruder"
            ]
        return g90_g91_influences_extruder

    @property
    def _resolution_mm(self):
        resolution_mm = self._settings.get_float(["resolution_mm"])
        if resolution_mm is None:
            resolution_mm = self.settings_default["resolution_mm"]
        return resolution_mm

    @property
    def _path_tolerance_percent(self):
        path_tolerance_percent = self._settings.get_float(["path_tolerance_percent"])
        if path_tolerance_percent is None:
            path_tolerance_percent = self.settings_default["path_tolerance_percent"]
        return path_tolerance_percent

    @property
    def _path_tolerance_percent_decimal(self):
        return self._path_tolerance_percent / 100.0

    @property
    def _max_radius_mm(self):
        max_radius_mm = self._settings.get_float(["max_radius_mm"])
        if max_radius_mm is None:
            max_radius_mm = self.settings_default["max_radius_mm"]
        return max_radius_mm

    @property
    def _overwrite_source_file(self):
        overwrite_source_file = self._settings.get_boolean(["overwrite_source_file"])
        if overwrite_source_file is None:
            overwrite_source_file = self.settings_default["overwrite_source_file"]
        return overwrite_source_file

    @property
    def _target_prefix(self):
        target_prefix = self._settings.get(["target_prefix"])
        if target_prefix is None:
            target_prefix = self.settings_default["target_prefix"]
        else:
            target_prefix = target_prefix.strip()
        return target_prefix

    @property
    def _target_postfix(self):
        target_postfix = self._settings.get(["target_postfix"])
        if target_postfix is None:
            target_postfix = self.settings_default["target_postfix"]
        else:
            target_postfix = target_postfix.strip()
        return target_postfix

    @property
    def _select_file_after_automatic_processing(self):
        return self._settings.get(
            ["feature_settings", "select_after_processing"]
        ) in [
            ArcWelderPlugin.SELECT_FILE_AFTER_PROCESSING_BOTH, ArcWelderPlugin.SELECT_FILE_AFTER_PROCESSING_AUTO
        ]


    @property
    def _select_file_after_manual_processing(self):
        return self._settings.get(
            ["feature_settings", "select_after_processing"]
        ) in [
           ArcWelderPlugin.SELECT_FILE_AFTER_PROCESSING_BOTH, ArcWelderPlugin.SELECT_FILE_AFTER_PROCESSING_MANUAL
        ]

    @property
    def _check_firmware_on_connect(self):
        return self._settings.get(["feature_settings", "check_firmware"]) == ArcWelderPlugin.CHECK_FIRMWARE_ON_CONECT

    @property
    def _print_after_manual_processing(self):
        return self._settings.get(
            ["feature_settings", "print_after_processing"]
        ) in [
            ArcWelderPlugin.PRINT_AFTER_PROCESSING_BOTH, ArcWelderPlugin.PRINT_AFTER_PROCESSING_MANUAL
        ]

    @property
    def _print_after_automatic_processing(self):
        return self._settings.get(
            ["feature_settings", "print_after_processing"]
        ) in [
            ArcWelderPlugin.PRINT_AFTER_PROCESSING_BOTH, ArcWelderPlugin.PRINT_AFTER_PROCESSING_AUTO
        ]

    @property
    def _show_started_notification(self):
        return self._settings.get(["notification_settings", "show_started_notification"])

    @property
    def _show_progress_bar(self):
        return self._settings.get(["notification_settings", "show_progress_bar"])

    @property
    def _show_completed_notification(self):
        return self._settings.get(["notification_settings", "show_completed_notification"])

    @property
    def _delete_source_after_processing(self):
        return self._settings.get(["delete_source_after_processing"])

    def _get_print_after_processing(self, is_manual):
        if is_manual:
            return self._print_after_manual_processing
        else:
            return self._print_after_automatic_processing

    def _get_select_file_after_processing(self, is_manual):
        if is_manual:
            return self._select_file_after_manual_processing
        else:
            return self._select_file_after_automatic_processing

    def get_storage_path_and_name(self, storage_path, add_prefix_and_postfix):
        path, name = self._file_manager.split_path(FileDestinations.LOCAL, storage_path)
        if add_prefix_and_postfix:
            file_name = utilities.remove_extension_from_filename(name)
            file_extension = utilities.get_extension_from_filename(name)
            new_name = "{0}{1}{2}.{3}".format(self._target_prefix, file_name, self._target_postfix, file_extension)
        else:
            new_name = name
        new_path = self._file_manager.join_path(FileDestinations.LOCAL, path, new_name)
        return new_path, new_name

    def get_preprocessor_arguments(self, source_path_on_disk):
        return {
            "path": source_path_on_disk,
            "resolution_mm": self._resolution_mm,
            "path_tolerance_percent": self._path_tolerance_percent_decimal,
            "max_radius_mm": self._max_radius_mm,
            "g90_g91_influences_extruder": self._g90_g91_influences_extruder,
            "log_level": self._gcode_conversion_log_level
        }

    def save_preprocessed_file(self, path, preprocessor_args, results, additional_metadata):
        # get the file name and path
        new_path, new_name = self.get_storage_path_and_name(
            path, not self._overwrite_source_file
        )

        if self._get_is_printing(new_path):
            raise TargetFileSaveError("The source file will be overwritten, but it is currently printing, cannot overwrite.")

        if self._overwrite_source_file:
            logger.info("Overwriting source file at %s with the processed file.", path)
        else:
            logger.info("Arc compression complete, creating a new gcode file: %s", new_name)

        new_file_object = octoprint.filemanager.util.DiskFileWrapper(
            new_name, preprocessor_args["target_file_path"], move=True
        )

        self._file_manager.add_file(
            FileDestinations.LOCAL,
            new_path,
            new_file_object,
            allow_overwrite=True,
            display=new_name,
        )
        self._file_manager.set_additional_metadata(
            FileDestinations.LOCAL, new_path, "arc_welder", True, overwrite=True, merge=False
        )
        progress = results["progress"]
        metadata = {
            "source_file_total_length": progress["source_file_total_length"],
            "target_file_total_length": progress["target_file_total_length"],
            "source_file_total_count": progress["source_file_total_count"],
            "target_file_total_count": progress["target_file_total_count"],
            "segment_statistics_text": progress["segment_statistics_text"],
            "seconds_elapsed": progress["seconds_elapsed"],
            "gcodes_processed": progress["gcodes_processed"],
            "lines_processed": progress["lines_processed"],
            "points_compressed": progress["points_compressed"],
            "arcs_created": progress["arcs_created"],
            "source_file_size": progress["source_file_size"],
            "source_file_position": progress["source_file_position"],
            "target_file_size": progress["target_file_size"],
            "compression_ratio": progress["compression_ratio"],
            "compression_percent": progress["compression_percent"],
            "source_filename": results["source_filename"],
            "target_filename": new_name,
            "preprocessing_job_guid": self.preprocessing_job_guid
        }

        self._file_manager.set_additional_metadata(
            FileDestinations.LOCAL,
            new_path,
            "arc_welder_statistics",
            metadata,
            overwrite=True,
            merge=False
        )

        # Add compatibility for ultimaker thumbnail package
        has_ultimaker_format_package_thumbnail = (
            "thumbnail" in additional_metadata
            and isinstance(additional_metadata['thumbnail'], string_types)
            and additional_metadata['thumbnail'].startswith('plugin/UltimakerFormatPackage/thumbnail/')
        )
        # Add compatibility for PrusaSlicer thumbnail package
        has_prusa_slicer_thumbnail = (
                "thumbnail" in additional_metadata
                and isinstance(additional_metadata['thumbnail'], string_types)
                and additional_metadata['thumbnail'].startswith('plugin/prusaslicerthumbnails/thumbnail/')
        )

        # delete the thumbnail src element if it exists, we will add it later if necessary
        if "thumbnail_src" in additional_metadata:
            del additional_metadata["thumbnail_src"]

        if has_ultimaker_format_package_thumbnail and not "thumbnail_src" in additional_metadata:
            additional_metadata["thumbnail_src"] = "UltimakerFormatPackage"
        elif has_prusa_slicer_thumbnail and not "thumbnail_src" in additional_metadata:
            additional_metadata["thumbnail_src"] = "prusaslicerthumbnails"

        # add the additional metadata
        if "thumbnail" in additional_metadata:
            current_path = additional_metadata["thumbnail"]
            thumbnail_src = None
            thumbnail = None
            if has_ultimaker_format_package_thumbnail:
                thumbnail_src = "UltimakerFormatPackage"
            elif has_prusa_slicer_thumbnail:
                thumbnail_src = "prusaslicerthumbnails"

            if thumbnail_src is not None:
                thumbnail = self.copy_thumbnail(thumbnail_src, current_path, new_name)
            # set the thumbnail path and src.  It will not be copied to the final metadata if the value is none
            additional_metadata["thumbnail"] = thumbnail
            additional_metadata["thumbnail_src"] = thumbnail_src

        # add all the metadata items
        for key, value in additional_metadata.items():
            if value is not None:
                self._file_manager.set_additional_metadata(
                    FileDestinations.LOCAL,
                    new_path,
                    key,
                    value,
                    overwrite=True,
                    merge=False
                )

        return new_path, new_name, metadata

    def copy_thumbnail(self, thumbnail_src, thumbnail_path, gcode_filename):
        # get the plugin implementation
        plugin_implementation = self._plugin_manager.get_plugin_info(thumbnail_src, True)
        if plugin_implementation:
            thumbnail_uri_root = 'plugin/' + thumbnail_src + '/thumbnail/'
            data_folder = plugin_implementation.implementation.get_plugin_data_folder()
            # extract the file name from the path
            path = thumbnail_path.replace(thumbnail_uri_root, '')
            querystring_index = path.rfind('?')
            if querystring_index > -1:
                path = path[0: querystring_index]

            path = os.path.join(data_folder, path)
            # see if the thumbnail exists
            if os.path.isfile(path):
                # create a new path
                pre, ext = os.path.splitext(gcode_filename)
                new_thumb_name = pre + ".png"
                new_path = os.path.join(data_folder, new_thumb_name)
                new_metadata = (
                    thumbnail_uri_root + new_thumb_name + "?" + "{:%Y%m%d%H%M%S}".format(
                        datetime.datetime.now()
                    )
                )
                if path != new_path:
                    try:
                        copyfile(path, new_path)
                    except (IOError, OSError) as e:
                        logger.exception("An error occurred copying thumbnail from '%s' to '%s'", path, new_path)
                        new_metadata = None
                return new_metadata
        return None

    def preprocessing_started(self, task):
        path = task["path"]
        preprocessor_args = task["preprocessor_args"]
        print_after_processing = task["print_after_processing"]
        new_path, new_name = self.get_storage_path_and_name(
            path, not self._overwrite_source_file
        )
        self.preprocessing_job_guid = str(uuid.uuid4())
        self.preprocessing_job_source_file_path = path
        self.preprocessing_job_target_file_name = new_name
        self.is_cancelled = False

        logger.info(
            "Starting pre-processing with the following arguments:"
            "\n\tsource_file_path: %s"
            "\n\tresolution_mm: %.3f"
            "\n\tpath_tolerance_percent: %.3f"
            "\n\tg90_g91_influences_extruder: %r"
            "\n\tlog_level: %d",
            preprocessor_args["path"],
            preprocessor_args["resolution_mm"],
            preprocessor_args["path_tolerance_percent"],
            preprocessor_args["g90_g91_influences_extruder"],
            preprocessor_args["log_level"]
        )

        if print_after_processing:
            logger.info("The queued file will be printed after processing is complete.")

        if self._show_started_notification:
            # A bit of a hack.  Need to rethink the start notification.
            if self._show_progress_bar:
                data = {
                    "message_type": "preprocessing-start",
                    "source_filename": self.preprocessing_job_source_file_path,
                    "target_filename": self.preprocessing_job_target_file_name,
                    "preprocessing_job_guid": self.preprocessing_job_guid
                }
                self._plugin_manager.send_plugin_message(self._identifier, data)
            else:
                message = "Arc Welder is processing '{0}'.  Please wait...".format(
                    self.preprocessing_job_source_file_path
                )
                self.send_notification_toast(
                    "info", "Pre-Processing Gcode", message, True, "preprocessing_start", ["preprocessing_start"]
                )

    def preprocessing_progress(self, progress):
        if self._show_progress_bar:
            data = {
                "message_type": "preprocessing-progress",
                "percent_complete": progress["percent_complete"],
                "seconds_elapsed": progress["seconds_elapsed"],
                "seconds_remaining": progress["seconds_remaining"],
                "gcodes_processed": progress["gcodes_processed"],
                "lines_processed": progress["lines_processed"],
                "points_compressed": progress["points_compressed"],
                "arcs_created": progress["arcs_created"],
                "source_file_size": progress["source_file_size"],
                "source_file_position": progress["source_file_position"],
                "target_file_size": progress["target_file_size"],
                "compression_ratio": progress["compression_ratio"],
                "compression_percent": progress["compression_percent"],
                "source_filename": self.preprocessing_job_source_file_path,
                "target_filename": self.preprocessing_job_target_file_name,
                "preprocessing_job_guid": self.preprocessing_job_guid
            }
            self._plugin_manager.send_plugin_message(self._identifier, data)
            time.sleep(0.01)
        # return true if processing should continue.
        # If self.is_cancelled is true (This is set by the cancelled blueprint plugin)
        # or the we are currently printing, return false
        return not self.is_cancelled

    def preprocessing_cancelled(self, task, auto_cancelled):

        path = task["path"]
        preprocessor_args = task["preprocessor_args"]

        if auto_cancelled:
            message = "Cannot process while printing.  Arc Welding has been cancelled for '{0}'.  The file will be " \
                      "processed once printing has completed. "
        else:
            message = "Preprocessing has been cancelled for '{0}'."

        message = message.format(path)
        data = {
            "message_type": "preprocessing-cancelled",
            "source_filename": self.preprocessing_job_source_file_path,
            "target_filename": self.preprocessing_job_target_file_name,
            "preprocessing_job_guid": self.preprocessing_job_guid,
            "message": message
        }
        self._plugin_manager.send_plugin_message(self._identifier, data)

    def preprocessing_success(
            self, results, task
    ):
        # extract the task data
        path = task["path"]
        preprocessor_args = task["preprocessor_args"]
        additional_metadata = task["additional_metadata"]
        is_manual_request = task["is_manual_request"]
        print_after_processing = task["print_after_processing"]
        select_file_after_processing = task["select_file_after_processing"]
        # save the newly created file.  This must be done before
        # exiting this callback because the target file isn't
        # guaranteed to exist later.
        try:
            new_path, new_name, metadata = self.save_preprocessed_file(
                path, preprocessor_args, results, additional_metadata
            )
        except TargetFileSaveError as e:
            data = {
                "message_type": "preprocessing-failed",
                "source_filename": self.preprocessing_job_source_file_path,
                "target_filename": self.preprocessing_job_target_file_name,
                "preprocessing_job_guid": self.preprocessing_job_guid,
                "message": 'Unable to save the target file.  A file with the same name may be currently printing.'
            }
            self._plugin_manager.send_plugin_message(self._identifier, data)
            return
        if (
            (
                (is_manual_request and self._delete_source_after_manual_processing)
                or (not is_manual_request and self._delete_source_after_automatic_processing)
            )
            and self._file_manager.file_exists(FileDestinations.LOCAL, path)
            and not path == new_path
        ):
            if not self._get_is_printing(path):
                # if the file is selected, deselect it.
                if self._get_is_file_selected(path, FileDestinations.LOCAL):
                    self._printer.unselect_file()
                # delete the source file
                logger.info("Deleting source file at %s.", path)
                try:
                    self._file_manager.remove_file(FileDestinations.LOCAL, path)
                except octoprint.filemanager.storage.StorageError:
                    logger.exception("Unable to delete the source file at '%s'", path)
            else:
                logger.exception("Unable to delete the source file at '%s'.  It is currently printing.", path)

        if self._show_completed_notification:
            data = {
                "message_type": "preprocessing-success",
                "arc_welder_statistics": metadata,
                "path": new_path,
                "name": new_name,
                "origin": FileDestinations.LOCAL
            }
            self._plugin_manager.send_plugin_message(self._identifier, data)

        if select_file_after_processing or print_after_processing:
            try:
                self._select_file(new_path)
                # make sure the file is selected
                if (
                        self._get_is_file_selected(new_path, FileDestinations.LOCAL)
                        and print_after_processing
                        and not self._get_is_printing()
                ):
                    self._printer.start_print(tags=set('arc_welder'))
            except (octoprint.printer.InvalidFileType, octoprint.printer.InvalidFileLocation):
                # we don't care too much if OctoPrint can't select the file.  There's nothing
                # we can do about it anyway
                pass

    def preprocessing_completed(self):
        data = {
            "message_type": "preprocessing-complete"
        }
        self.preprocessing_job_guid = None
        self.preprocessing_job_source_file_path = None
        self.preprocessing_job_target_file_name = None
        self._plugin_manager.send_plugin_message(self._identifier, data)

    def preprocessing_failed(self, message, task):
        data = {
            "message_type": "preprocessing-failed",
            "source_filename": self.preprocessing_job_source_file_path,
            "target_filename": self.preprocessing_job_target_file_name,
            "preprocessing_job_guid": self.preprocessing_job_guid,
            "message": message
        }
        self._plugin_manager.send_plugin_message(self._identifier, data)

    def _clean_removed_files(self):
        # get the current date and time so we can remove old files
        cutoff_date_time = (
            datetime.datetime.now() -
            datetime.timedelta(milliseconds=self._recently_moved_file_wait_ms)
        )
        # iterate over the list in reverse, so we can remove elements
        for i in xrange(len(self._recently_moved_files) - 1, -1, -1):
            current_file = self._recently_moved_files[i]
            if current_file["date_removed"] < cutoff_date_time:
                del self._recently_moved_files[i]

    # first remove any old moved files
    def _add_removed_file(self, file_name):
        # first, clean up the removed file list
        self._clean_removed_files()
        # now, add the file
        self._recently_moved_files.append({
            "file_name": file_name,
            "date_removed": datetime.datetime.now()
        })

    # see if a file was recently removed to handle file move
    def _is_file_moved(self, file_name):
        # first, clean up the removed file list
        self._clean_removed_files()
        # now, iterate the list and see if the file_name exists
        for item in self._recently_moved_files:
            if item["file_name"] == file_name:
                return True
        return False

    def on_event(self, event, payload):

        if event == Events.PRINT_STARTED:
            printing_stopped = self._preprocessor_worker.prevent_printing_for_existing_jobs()
            if printing_stopped:
                self.send_notification_toast(
                    "warning", "Arc-Welder: Cannot Print",
                    "A print is running, but processing tasks are in the queue that are marked 'Print After "
                    "Processing'.  To protect your printer, Arc Welder will not automatically print when processing "
                    "is completed.",
                    True,
                    key="auto_print_cancelled", close_keys=["auto_print_cancelled"]
                )
        elif event == Events.FILE_ADDED:
            # Need to use file added event to catch uploads and other non-upload methods of adding a file.
            # skip the file if it was recently moved.  This COULD cause problems due to potential
            # race conditions, but in most cases it will not.
            if self._is_file_moved(payload["name"]):
                return

            if not self._enabled or not self._auto_pre_processing_enabled:
                return
            # Note, 'target' is the key for FILE_UPLOADED, but 'storage' is the key for FILE_ADDED
            target = payload["storage"]
            path = payload["path"]
            name = payload["name"]

            if path == self.preprocessing_job_source_file_path or name == self.preprocessing_job_target_file_name:
                return

            if not octoprint.filemanager.valid_file_type(
                    path, type="gcode"
            ):
                return

            if not target == FileDestinations.LOCAL:
                return

            metadata = self._file_manager.get_metadata(target, path)
            if "arc_welder" in metadata:
                return
            # Extract only the supported metadata from the added file
            additional_metadata = self.get_additional_metadata(metadata)
            # Add this file to the processor queue.
            self.add_file_to_preprocessor_queue(path, additional_metadata, False)
        elif event == Events.FILE_REMOVED:
            # add the file to the removed list
            self._add_removed_file(payload["name"])
        elif event == Events.PRINTER_STATE_CHANGED:
            if payload["state_id"] == "OPERATIONAL":
                if self._check_firmware_on_connect:
                    logger.info("Printer is connected and check firmware on connect is enabled.  Checking Firmware.")
                    self.check_firmware()
                else:
                    logger.verbose("Printer is connected, but check firmware on connect is disabled.  Skipping.")


    def get_additional_metadata(self, metadata):
        # list of supported metadata
        supported_metadata_keys = ['thumbnail', 'thumbnail_src']
        additional_metadata = {}
        # Create the additional metadata from the supported keys
        for key in supported_metadata_keys:
            if key in metadata:
                additional_metadata[key] = metadata[key]
        return additional_metadata

    def add_file_to_preprocessor_queue(self, path, additional_metadata, is_manual_request):
        # get the file by path
        # file = self._file_manager.get_file(FileDestinations.LOCAL, path)
        is_printing = self._get_is_printing()
        if is_printing:
            self.send_notification_toast(
                "warning", "Arc-Welder: Unable to Process",
                "Cannot preprocess gcode while a print is in progress because print quality may be affected.  The "
                "gcode will be processed as soon as the print has completed.",
                True,
                key="unable_to_process", close_keys=["unable_to_process"]
            )

        logger.info("Received a new gcode file for processing.  FileName: %s.", path)
        path_on_disk = self._file_manager.path_on_disk(FileDestinations.LOCAL, path)

        # make sure the path starts with a / for compatibility
        if path[0] != '/':
            path = '/' + path

        preprocessor_args = self.get_preprocessor_arguments(path_on_disk)
        print_after_processing = not is_printing and self._get_print_after_processing(is_manual_request)
        select_after_processing = self._get_select_file_after_processing(is_manual_request)
        task = {
            "path": path,
            "preprocessor_args": preprocessor_args,
            "additional_metadata": additional_metadata,
            "is_manual_request": is_manual_request,
            "print_after_processing": print_after_processing,
            "select_file_after_processing": select_after_processing
        }
        results = self._preprocessor_worker.add_task(task)
        if not results["success"]:
            self.send_notification_toast(
                "warning", "Arc-Welder: Unable To Queue File",
                results["error_message"],
                True,
                key="unable_to_queue", close_keys=["unable_to_queue"]
            )
            return False
        return True

    def register_custom_routes(self, server_routes, *args, **kwargs):
        # version specific permission validator
        if LooseVersion(octoprint.server.VERSION) >= LooseVersion("1.4"):
            admin_validation_chain = [
                util.tornado.access_validation_factory(app, util.flask.admin_validator),
            ]
        else:
            # the concept of granular permissions does not exist in this version of Octoprint.  Fallback to the
            # admin role
            def admin_permission_validator(flask_request):
                user = util.flask.get_flask_user_from_request(flask_request)
                if user is None or not user.is_authenticated() or not user.is_admin():
                    raise tornado.web.HTTPError(403)
            permission_validator = admin_permission_validator
            admin_validation_chain = [util.tornado.access_validation_factory(app, permission_validator), ]
        return [
            (
                r"/downloadFile",
                ArcWelderLargeResponseHandler,
                dict(
                    request_callback=self.download_file_request,
                    as_attachment=True,
                    access_validation=util.tornado.validation_chain(*admin_validation_chain)
                )

            )
        ]

        # ~~ software update hook

    arc_welder_update_info = dict(
        displayName="Arc Welder: Anti-Stutter",
        # version check: github repository
        type="github_release",
        user="FormerLurker",
        repo="ArcWelderPlugin",
        pip="https://github.com/FormerLurker/ArcWelderPlugin/archive/{target_version}.zip",
        stable_branch=dict(branch="master", commitish=["master"], name="Stable"),
        release_compare='custom',
        prerelease_branches=[
            dict(
                branch="rc/maintenance",
                commitish=["master", "rc/maintenance"],  # maintenance RCs (include master)
                name="Maintenance RCs"
            ),
            dict(
                branch="rc/devel",
                commitish=["master", "rc/maintenance", "rc/devel"],  # devel & maintenance RCs (include master)
                name="Devel RCs"
            )
        ],
    )

    def get_release_info(self):
        # Starting with V1.5.0 prerelease branches are supported!
        if LooseVersion(octoprint.server.VERSION) < LooseVersion("1.5.0"):
            # Hack to attempt to get pre-release branches to work prior to 1.5.0
            # get the checkout type from the software updater
            prerelease_channel = None
            is_prerelease = False
            # get this for reference.  Eventually I'll have to use it!
            # is the software update set to prerelease?

            if self._settings.global_get(["plugins", "softwareupdate", "checks", "octoprint", "prerelease"]):
                # If it's a prerelease, look at the channel and configure the proper branch for Arc Welder
                prerelease_channel = self._settings.global_get(
                    ["plugins", "softwareupdate", "checks", "octoprint", "prerelease_channel"]
                )
                if prerelease_channel == "rc/maintenance":
                    is_prerelease = True
                    prerelease_channel = "rc/maintenance"
                elif prerelease_channel == "rc/devel":
                    is_prerelease = True
                    prerelease_channel = "rc/devel"
            ArcWelderPlugin.arc_welder_update_info["displayVersion"] = self._plugin_version
            ArcWelderPlugin.arc_welder_update_info["current"] = self._plugin_version
            ArcWelderPlugin.arc_welder_update_info["prerelease"] = is_prerelease
            if prerelease_channel is not None:
                ArcWelderPlugin.arc_welder_update_info["prerelease_channel"] = prerelease_channel

        return dict(
            arc_welder=ArcWelderPlugin.arc_welder_update_info
        )

    def get_update_information(self):
        # moved most of the heavy lifting to get_latest, since I need to do a custom version compare.
        # AND I want to use the most recent software update release channel settings.
        return self.get_release_info()

    # noinspection PyUnusedLocal
    def on_gcode_sent(self, comm_instance, phase, cmd, cmd_type, gcode, *args, **kwargs):
        if self._enabled and self._firmware_checker is not None:
            try:
                self._firmware_checker.on_gcode_sending(comm_instance, phase, cmd, cmd_type, gcode, args, kwargs)
            except Exception as e:
                logger.exception("on_gcode_sent failed.")
        return None
    # noinspection PyUnusedLocal
    def on_gcode_received(self, comm, line, *args, **kwargs):
        if self._enabled and self._firmware_checker is not None:
            try:
                return self._firmware_checker.on_gcode_received(comm, line, args, kwargs)
            except Exception as e:
                logger.exception("on_gcode_received failed.")
        return line

__plugin_pythoncompat__ = ">=2.7,<4"
__plugin_implementation__ = ArcWelderPlugin()


def __plugin_load__():
    global __plugin_implementation__
    __plugin_implementation__ = ArcWelderPlugin()
    global __plugin_hooks__
    __plugin_hooks__ = {
        "octoprint.plugin.softwareupdate.check_config": __plugin_implementation__.get_update_information,
        "octoprint.server.http.routes": __plugin_implementation__.register_custom_routes,
        "octoprint.comm.protocol.gcode.received": (__plugin_implementation__.on_gcode_received, -1),
        "octoprint.comm.protocol.gcode.sent": (__plugin_implementation__.on_gcode_sent, -1),
    }


class ArcWelderLargeResponseHandler(LargeResponseHandler):

    def initialize(self, request_callback, as_attachment=False, access_validation=None, default_filename=None,
        on_before_request=None, on_after_request=None
    ):
        super(ArcWelderLargeResponseHandler, self).initialize(
            '', default_filename=default_filename, as_attachment=as_attachment, allow_client_caching=False,
            access_validation=access_validation, path_validation=None, etag_generator=None,
            name_generator=self.name_generator, mime_type_guesser=None)
        self.download_file_name = None
        self._before_request_callback = on_before_request
        self._request_callback = request_callback
        self._after_request_callback = on_after_request
        self.after_request_internal = None
        self.after_request_internal_args = None

    def name_generator(self, path):
        if self.download_file_name is not None:
            return self.download_file_name

    def prepare(self):
        if self._before_request_callback:
            self._before_request_callback()

    def get(self, include_body=True):
        if self._access_validation is not None:
            self._access_validation(self.request)

        if "cookie" in self.request.arguments:
            self.set_cookie(self.request.arguments["cookie"][0], "true", path="/")
        full_path = self._request_callback(self)
        self.root = os.path.dirname(full_path)

        # if the file does not exist, return a 404
        if not os.path.isfile(full_path):
            raise tornado.web.HTTPError(404)

        # return the file
        return tornado.web.StaticFileHandler.get(self, full_path, include_body=include_body)

    def on_finish(self):
            if self.after_request_internal:
                self.after_request_internal(**self.after_request_internal_args)

            if self._after_request_callback:
                self._after_request_callback()


class TargetFileSaveError(Exception):
    pass
