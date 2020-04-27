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
import uuid
import threading
from distutils.version import LooseVersion
from flask import request, jsonify
import os
import sys
import octoprint.plugin
import tornado
from octoprint.server.util.tornado import LargeResponseHandler
from octoprint.server import util, app
from octoprint.filemanager import FileDestinations
from octoprint.server.util.flask import restricted_access
from octoprint.events import Events
import octoprint_arc_welder.log as log
import octoprint_arc_welder.preprocessor as preprocessor
# stupid python 2/python 3 compatibility imports
try:
    import queue
except ImportError:
    import Queue as queue

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

    def __init__(self):
        super(ArcWelderPlugin, self).__init__()
        self.preprocessing_job_guid = None
        self.preprocessing_job_source_file_path = ""
        self.preprocessing_job_target_file_name = ""
        self.is_cancelled = False
        self._processing_queue = queue.Queue()
        self.settings_default = dict(
            use_octoprint_settings=True,
            g90_g91_influences_extruder=False,
            resolution_mm=0.05,
            overwrite_source_file=False,
            target_prefix="AS_",
            notification_settings=dict(
                show_started_notification=True,
                show_progress_bar=True,
                show_completed_notification=True
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
        # start the preprocessor worker
        self._preprocessor_worker = preprocessor.PreProcessorWorker(
            self._processing_queue,
            self._get_is_printing,
            self.save_preprocessed_file,
            self.preprocessing_started,
            self.preprocessing_progress,
            self.preprocessing_cancelled,
            self.preprocessing_failed,
            self.preprocessing_success,
            self.preprocessing_completed,
        )

    def on_after_startup(self):
        logging_configurator.configure_loggers(
            self._log_file_path, self._logging_configuration
        )
        self._preprocessor_worker.daemon = True
        self._preprocessor_worker.start()
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
            preprocessing_job_guid = request_values["preprocessing_job_guid"]
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
        # return true to continue, false to terminate

    # ~~ AssetPlugin mixin
    def get_assets(self):
        # Define your plugin's asset files to automatically include in the
        # core UI here.
        return dict(
            js=["js/arc_welder.js", "js/arc_welder.settings.js"],
            css=["css/arc_welder.css"],
        )

    def _get_is_printing(self):
        return self._printer.is_printing()
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
    def _overwrite_source_file(self):
        overwrite_source_file = self._settings.get_boolean(["overwrite_source_file"])
        if overwrite_source_file is None:
            overwrite_source_file = self.settings_default["overwrite_source_file"]
        return overwrite_source_file

    @property
    def _target_prefix(self):
        target_prefix = self._settings.get(["target_prefix"])
        if target_prefix is None:
            target_prefix = ""
        else:
            target_prefix = target_prefix.strip()
        if len(target_prefix) == 0:
            target_prefix = self.settings_default["target_prefix"]
        return target_prefix

    @property
    def _show_started_notification(self):
        return self._settings.get(["notification_settings", "show_started_notification"])

    @property
    def _show_progress_bar(self):
        return self._settings.get(["notification_settings", "show_progress_bar"])

    @property
    def _show_completed_notification(self):
        return self._settings.get(["notification_settings", "show_completed_notification"])

    def get_storage_path_and_name(self, storage_path, add_prefix):
        path, name = self._file_manager.split_path(FileDestinations.LOCAL, storage_path)
        if add_prefix:
            new_name = self._target_prefix + name
        else:
            new_name = name
        new_path = self._file_manager.join_path(FileDestinations.LOCAL, path, new_name)
        return new_path, new_name

    def get_preprocessor_arguments(self, source_path_on_disk):
        target_file_path = os.path.join(self.get_plugin_data_folder(), "temp.gcode")
        return {
            "source_file_path": source_path_on_disk,
            "target_file_path": target_file_path,
            "resolution_mm": self._resolution_mm,
            "g90_g91_influences_extruder": self._g90_g91_influences_extruder,
            "log_level": self._gcode_conversion_log_level
        }

    def save_preprocessed_file(self, path, preprocessor_args):
        # get the file name and path
        new_path, new_name = self.get_storage_path_and_name(
            path, not self._overwrite_source_file
        )
        if self._overwrite_source_file:
            logger.info("Overwriting source file at %s with the processed file.", path)
        else:
            logger.info("Arc compression complete, creating a new gcode file: %s", new_name)


        # TODO:  Look through the analysis queue, and stop analysis if the file is being analyzed.  Perhaps we need
        # to wait?
        # Create the new file object
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

    def preprocessing_started(self, path, preprocessor_args):
        new_path, new_name = self.get_storage_path_and_name(
            path, not self._overwrite_source_file
        )
        self.preprocessing_job_guid = str(uuid.uuid4())
        self.preprocessing_job_source_file_path = path
        self.preprocessing_job_target_file_name = new_name

        logger.info(
            "Starting pre-processing with the following arguments:\n\tsource_file_path: "
            "%s\n\ttarget_file_path: %s\n\tresolution_mm: %.3f\n\tg90_g91_influences_extruder: %r"
            "\n\tlog_level: %d",
            preprocessor_args["source_file_path"], preprocessor_args["target_file_path"],
            preprocessor_args["resolution_mm"], preprocessor_args["g90_g91_influences_extruder"],
            preprocessor_args["log_level"]
        )

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
                    preprocessor_args["source_file_path"])
                self.send_notification_toast(
                    "info", "Pre-Processing Gcode", message, True, "preprocessing_start", ["preprocessing_start"]
                )

    def preprocessing_progress(self, progress):
        if progress["percent_complete"] >= 100.0 or self._show_progress_bar:
            suppress_popup = False
            if progress["percent_complete"] == 100.0 and not self._show_completed_notification:
                suppress_popup = True
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
                "target_file_size": progress["target_file_size"],
                "source_filename": self.preprocessing_job_source_file_path,
                "target_filename": self.preprocessing_job_target_file_name,
                "suppress_popup": suppress_popup,
                "preprocessing_job_guid": self.preprocessing_job_guid
            }
            self._plugin_manager.send_plugin_message(self._identifier, data)
        # return true if processing should continue.  This is set by the cancelled blueprint plugin.
        return not self.is_cancelled

    def preprocessing_cancelled(self, path, preprocessor_args):
        message = "Preprocessing has been cancelled for '{0}'.".format(path)
        data = {
            "message_type": "preprocessing-cancelled",
            "source_filename": self.preprocessing_job_source_file_path,
            "target_filename": self.preprocessing_job_target_file_name,
            "preprocessing_job_guid": self.preprocessing_job_guid,
            "message": message
        }
        self._plugin_manager.send_plugin_message(self._identifier, data)

    def preprocessing_success(self, results, path, preprocessor_args):
        # save the newly created file.  This must be done before
        # exiting this callback because the target file isn't
        # guaranteed to exist later.
        self.save_preprocessed_file(path, preprocessor_args)
        data = {
            "message_type": "preprocessing-success",
            "results": results,
            "source_filename": self.preprocessing_job_source_file_path,
            "target_filename": self.preprocessing_job_target_file_name,
            "preprocessing_job_guid": self.preprocessing_job_guid,
        }
        self._plugin_manager.send_plugin_message(self._identifier, data)

    def preprocessing_completed(self):
        data = {
            "message_type": "preprocessing-complete"
        }
        self.preprocessing_job_guid = None
        self.preprocessing_job_source_file_path = None
        self.preprocessing_job_target_file_name = None
        self._plugin_manager.send_plugin_message(self._identifier, data)

    def preprocessing_failed(self, message):
        data = {
            "message_type": "preprocessing-failed",
            "source_filename": self.preprocessing_job_source_file_path,
            "target_filename": self.preprocessing_job_target_file_name,
            "preprocessing_job_guid": self.preprocessing_job_guid,
            "message": message
        }
        self._plugin_manager.send_plugin_message(self._identifier, data)

    def on_event(self, event, payload):
        if not self._enabled:
            return

        if event == Events.FILE_ADDED:
            storage = payload["storage"]
            path = payload["path"]
            name = payload["name"]
            type = payload["type"]

            if path == self.preprocessing_job_source_file_path or name == self.preprocessing_job_target_file_name:
                return

            if not octoprint.filemanager.valid_file_type(
                    path, type="gcode"
            ):
                return

            if not storage == FileDestinations.LOCAL:
                return

            metadata = self._file_manager.get_metadata(storage, path)
            if "arc_welder" in metadata:
                return

            if self._printer.is_printing():
                self.send_notification_toast(
                    "error", "Arc-Welder: Unable to Process",
                    "Cannot preprocess gcode while a print is in progress because print quality may be affected.  The "
                    "gcode will be processed as soon as the print has completed.",
                    False,
                    key="unable_to_process", close_keys=["unable_to_process"]
                )
                return

            logger.info("Received a new gcode file for processing.  FileName: %s.", name)

            self.is_cancelled = False
            path_on_disk = self._file_manager.path_on_disk(storage, path)
            preprocessor_args = self.get_preprocessor_arguments(path_on_disk)
            self._processing_queue.put((path, preprocessor_args))

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

    def get_update_information(self):
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

        arc_welder_info = dict(
            displayName="Arc Welder: Anti-Stutter",
            displayVersion=self._plugin_version,
            # version check: github repository
            type="github_release",
            user="FormerLurker",
            repo="ArcWelderPlugin",
            current=self._plugin_version,
            prerelease=is_prerelease,
            pip="https://github.com/FormerLurker/ArcWelderPlugin/archive/{target_version}.zip",
            stable_branch=dict(branch="master", commitish=["master"], name="Stable"),
            release_compare='semantic_version',
            prerelease_branches=[
                dict(
                    branch="rc/maintenance",
                    commitish=["rc/maintenance"],  # maintenance RCs
                    name="Maintenance RCs"
                ),
                dict(
                    branch="rc/devel",
                    commitish=["rc/maintenance", "rc/devel"],  # devel & maintenance RCs
                    name="Devel RCs"
                )
            ],
        )

        if prerelease_channel is not None:
            arc_welder_info["prerelease_channel"] = prerelease_channel
        # return the update config
        return dict(
            arc_welder=arc_welder_info
        )


__plugin_pythoncompat__ = ">=2.7,<4"
__plugin_implementation__ = ArcWelderPlugin()


def __plugin_load__():
    global __plugin_implementation__
    __plugin_implementation__ = ArcWelderPlugin()
    global __plugin_hooks__
    __plugin_hooks__ = {
        "octoprint.plugin.softwareupdate.check_config": __plugin_implementation__.get_update_information,
        "octoprint.server.http.routes": __plugin_implementation__.register_custom_routes
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
