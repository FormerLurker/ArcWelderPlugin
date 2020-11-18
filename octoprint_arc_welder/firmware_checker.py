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
from __future__ import unicode_literals
import requests
import uuid
import threading
import re
import json
import os
import shutil
from datetime import datetime
from pkg_resources import parse_version
import octoprint_arc_welder.utilities as utilities
import octoprint_arc_welder.log as log

logging_configurator = log.LoggingConfigurator("arc_welder", "arc_welder.", "octoprint_arc_welder.")
root_logger = logging_configurator.get_root_logger()
# so that we can
logger = logging_configurator.get_logger(__name__)


class FirmwareChecker:

    DEFAULT_TIMEOUT_MS = 600000  # 1 minute
    ARCWELDER_TAG = 'arc_welder'
    FIRMWARE_TYPES_JSON_PATH = ["firmware", "types.json"]
    FIRMWARE_DOCS_PAATH = ["static", "docs", "help"]
    FIRMWARE_TYPES_DEFAULT_JSON_PATH = ["data", "firmware", "types_default.json"]
    CURRENT_FIRMWARE_JSON_PATH = ["firmware", "current.json"]

    def __init__(self, plugin_version, printer, base_folder, data_directory, request_complete_callback, load_defaults=False):
        try:
            self._plugin_version = plugin_version
            self._firmware_types_default_path = os.path.join(
                base_folder, *self.FIRMWARE_TYPES_DEFAULT_JSON_PATH
            )
            self._firmware_docs_path = os.path.join(
                base_folder, *self.FIRMWARE_DOCS_PAATH
            )
            self._firmware_types_path = os.path.join(
                data_directory, *self.FIRMWARE_TYPES_JSON_PATH
            )
            self._current_firmware_path = os.path.join(
                data_directory, *self.CURRENT_FIRMWARE_JSON_PATH
            )
            self._printer = printer

            self._request_complete_callback = request_complete_callback
            # the types of firmware we will be looking for
            # load from the /data/firmware/types.json or /data/firmware/types_default.json path
            self._firmware_types_version = None
            self._firmware_types = {"version": None, "types": {}}

            # Create an rlock for any shared variables
            self._shared_data_rlock = threading.RLock()

            # create an event that can be used to query the printer and get a response
            self._request_signal = threading.Event()
            self._request_signal.set()
            # variable for holding the most recent printer request and response
            self._printer_request = None
            self._printer_response = None

            # We can only send one request at a time, create a lock for this
            self._send_request_lock = threading.Lock()
            # The request itself can only be modified by a single thread
            self._request_lock = threading.Lock()
            # The most recent firmware version check
            self._current_firmware_info = None
            # Create a flag to show that we are checking firmware
            self._is_checking = False

            # load the current firmware types file
            self._load_firmware_types(False)

            # check for updates
            # this will also load default firmware types if the update fails
            self.check_for_updates()

            # Load the most recent firmware info if it exists.
            self._load_current_firmware_info()
        except Exception as e:
            logger.exception("Unable to start the firmware checker.")
            # throw the exception
            raise e

    def _load_firmware_types(self, load_defaults):
        logger.info("Loading firmware types from: %s", self._firmware_types_path)
        types = None
        if not load_defaults:
            try:
                with open(self._firmware_types_path) as f:
                    types = json.load(f)
            except (IOError, OSError) as e:
                logger.info("The firmware types file does not exist.  Creating from defaults.")
            except ValueError as e:
                logger.error("Could not parse the firmware types file.  Recreating from the defaults.")
        if not types:
            # The firmware types file either does not exist, or it is corrupt.  Recreate from the defaults
            if not os.path.exists(os.path.dirname(self._firmware_types_path)):
                firmware_types_directory = os.path.dirname(self._firmware_types_path)
                logger.info("Creating firmware types folder at: %s", firmware_types_directory)
                os.makedirs(firmware_types_directory)
            shutil.copy(self._firmware_types_default_path, self._firmware_types_path)
            with open(self._firmware_types_path) as f:
                types = json.load(f)
        if types:
            self._firmware_types_version = types["version"]
            self._firmware_types = types["types"]
            # compile all regex functions
            for firmware_key in self._firmware_types:
                firmware_type = self._firmware_types[firmware_key]
                functions = firmware_type.get("functions", {})
                for function_key in functions:
                    function = functions[function_key]
                    if isinstance(function, dict):
                        function["regex"] = re.compile(function.get("regex", ""))
        return types

    def _load_current_firmware_info(self):
        if not os.path.isfile(self._current_firmware_path):
            logger.info("No current firmware info exists at: %s", self._current_firmware_path)
            # no file exists, return none
            return None
        logger.info("Loading current firmware info from: %s.", self._current_firmware_path)
        try:
            with open(self._current_firmware_path) as f:
                with self._shared_data_rlock:
                    self._current_firmware_info = json.load(f)
        except ValueError as e:
            logger.error("Error loading the current firmware info from '%s'.  Could not parse JSON.", self._current_firmware_path)

    def _save_current_firmware_info(self, firmware_info):
        try:
            if not os.path.exists(os.path.dirname(self._current_firmware_path)):
                firmware_folder = os.path.dirname(self._current_firmware_path)
                logger.info("Creating current firmware folder at: %s", firmware_folder)
                os.makedirs(firmware_folder)
            with open(self._current_firmware_path, "w") as f:
                json.dump(firmware_info, f)
                logger.info("Current firmware info saved to: %s", self._current_firmware_path)
        except (IOError, OSError) as e:
            logger.exception("Error saving current firmware info to: %s", self._current_firmware_path)
        except ValueError as e:
            logger.exception("Error saving current firmware to '%s': Could not convert to JSON.", self._current_firmware_path)

    def check_for_updates(self):
        result = {
            "success": False,
            "error": None,
            "new_version": None
        }
        with self._shared_data_rlock:
            if self._is_checking:
                result["error"] = "A firmware check is underway, cannot check for updates"
                return result
            # set flag that we are checking for updates
            self._is_checking = True
            logger.info("Checking for firmware info updates.  Current version: %s", self._firmware_types_version)
            try:
                update_result = FirmwareFileUpdater.update_firmware_info(
                    self._plugin_version,
                    self._firmware_types_version,
                    self._firmware_types_path,
                    self._firmware_docs_path
                )
            except FirmwareFileUpdaterError as e:
                result["error"] = e.message
            except Exception as E:
                logger.exception("an unknown exception occurred while checking for firmware info updates.")
                result["error"] = "An unexpected exception occurred while checking for firmware updates.  See plugin.arcwelder.log for details."
            if update_result["success"]:
                result["new_version"] = update_result["new_version"]
                result["success"] = True
            else:
                result["error"] = update_result["error"]
            # load the retrieved firmware info, or load defaults if none exist.
            self._load_firmware_types(False)

            # set flag that we are no longer checking for updates
            self._is_checking = False
        if result["success"]:
            logger.info("Firmware completed successfully. Updated to version: %s", self._firmware_types_version)
        return result

    # properties
    @property
    def _is_printing(self):
        return self._printer.is_printing()

    def _get_firmware_version(self):
        # get the m115 response from the printer
        response_lines = self._get_m115_response()
        # split the response line into the first line and the rest

        result = {
            "success": False,
            "type": None,
            "version": None,
            "build_date": None,
            "version_range": None,
            "version_guid": None,
            "printer": None,
            "supported": None,
            "recommended": None,
            "notes": None,
            "previous_notes": None,
            "type_help_file": None,
            "version_help_file": None,
            "previous_version_help_file": None,
            "error": None,
            "m115_response": response_lines,
            "m115_parsed_response": None,
            "g2_g3_supported": None,
            "arcs_enabled": None,
            "g90_g91_influences_extruder": None,
            "arc_settings": None
        }

        if not response_lines or len(response_lines) < 1:
            # no response, exit
            result["error"] = "Your printer did not respond to M115, or the request timed out.  Unable to detect firmware."
            return result

        # parse the response
        parsed_response = FirmwareChecker.parse_m115_response(response_lines)
        result["m115_parsed_response"] = parsed_response
        if not parsed_response:
            result["error"] = "Could not parse the M115 response.  Unable to detect firmware"
            return result
        # we got a parsed response, so this will be considered a success
        result["success"] = True
        # We want to try to extract what we can now before we dig any deeper
        # see if arcs are enabled.

        # fill in the printer type if it is available
        result["printer"] = parsed_response.get("MACHINE_TYPE", None)
        result["type"] = parsed_response.get("FIRMWARE_NAME", None)
        # fill in the firmware version if provided
        firmware_version = parsed_response.get("FIRMWARE_VERSION", None)
        result["version"] = firmware_version
        # Extract whatever extended capabilities we have
        capabilities = parsed_response.get(FirmwareChecker.MARLIN_EXTENDED_CAPABILITIES_KEY, dict())
        # see if arcs are enabled
        arcs_enabled_capability = capabilities.get("ARCS", None)
        if arcs_enabled_capability in ["1", "0"]:
            result["arcs_enabled"] = arcs_enabled_capability == "1"
        # if arcs are enabled, g2/g3 is supported
        result["g2_g3_supported"] = True if result["arcs_enabled"] else None
        # get the firmware build date
        firmware_build_date = capabilities.get("FIRMWARE_BUILD_DATE", None)
        result["build_date"] = firmware_build_date

        # extract the firmware type
        firmware_type = None
        # do we know what kind of firmware this is?
        for firmware_key in self._firmware_types:
            # get the firmware type
            firmware = self._firmware_types[firmware_key]
            # get the check function name
            if self._is_firmware_type(result, firmware):
                result["type"] = firmware_key
                firmware_type = firmware
                break
            # is_firmware_type_info = firmware["functions"]["is_firmware_type"]
            # is_regex = False
            # if isinstance(is_firmware_type_info, dict):
            #     is_regex = True
            #     regex = is_firmware_type_info["regex"]
            #     regex_key = is_firmware_type_info.get("key", None)
            #     # this is a regex function
            # else:
            #     # get the check firmware function
            #     is_firmware_type = getattr(self, is_firmware_type_info, None)
            #
            # if not is_firmware_type:
            #     # the check firmware function does not exist!
            #     logger.error(
            #         "Could not find the check firmware function '%s'.  You may be running an old version.",
            #          is_firmware_type_info
            #     )
            #     continue
            #
            # if not is_regex and is_firmware_type(parsed_response):
            #     result["type"] = firmware_key
            #     firmware_type = firmware
            #     break
            #
            # elif is_regex and FirmwareChecker.get_regex_check(result, regex, regex_key):
            #
            #     break

        if not firmware_type:
            error = "Arc Welder does not recognize this firmware."
            return result
        # Get the help file for this firmware type
        result["type_help_file"] = firmware["help_file"]

        get_version_function_name = firmware["functions"]["get_version"]

        # get the check firmware function if we've not already found one
        if not firmware_version:
            firmware_version = self._get_firmware_version_match(result, firmware)
            result["version"] = firmware_version
            # get_version = getattr(self, get_version_function_name, None)
            # firmware_version = get_version(parsed_response)
            # result["version"] = firmware_version

        # call any custom arcs_enabled function
        if result.get("arcs_enabled", None) is None:
            result["arcs_enabled"] = self._get_arcs_enabled_match(result, firmware)

        version_compare_type = firmware_type.get("version_compare_type", "semantic")

        # Get the value we're going to use to compare the current version with the
        # firmware type list.
        if firmware_version and version_compare_type == "semantic":
            version_compare_string = firmware_version
        elif firmware_build_date and version_compare_type == "date":
            version_compare_string = firmware_build_date
        else:
            return result

        # try to find the version_info in our dict
        for index in list(range(len(firmware_type["versions"]))):
            version = firmware_type["versions"][index]
            matches = FirmwareChecker.is_version_in_versions(
                version_compare_string, version["version"], firmware_type, version_compare_type
            )

            if matches:
                version_info = version
                result["version_range"] = version.get("version", version.get("date", None))
                result["version_guid"] = version_info.get("guid", None)
                result["is_future"] = version_info.get("is_future", None)
                result["supported"] = version_info.get("supported", None)
                result["recommended"] = version_info.get("recommended", None)
                if result["arcs_enabled"] is None:
                    arcs_enabled = version_info.get("arcs_enabled", None)
                    result["arcs_enabled"] = arcs_enabled
                    if arcs_enabled and not result["g2_g3_supported"]:
                        result["g2_g3_supported"] = arcs_enabled
                if result["g2_g3_supported"] is None:
                    result["g2_g3_supported"] = version_info.get("g2_g3_supported", None)
                result["notes"] = version_info.get("notes", None)
                result["version_help_file"] = version_info.get("help_file", None)
                if result["is_future"] and index > 0:
                    result["previous_notes"] = firmware_type["versions"][index-1].get("notes", None)
                    result["previous_version_help_file"] = version_info.get("help_file", None)
                break

        return result

    def _is_firmware_type(self, firmware_check_result, firmware_type):
        is_firmware_type_info = firmware_type.get("functions", {}).get("is_firmware_type", None)
        if not is_firmware_type_info:
            logger.error(
                "Could not find is_firmware_type in firmware/types.json for %s",
                firmware_type.get("name", "unknown firmware")
            )
            return False
        parsed_response = firmware_check_result.get("m115_parsed_response", None)
        is_regex = False
        if isinstance(is_firmware_type_info, dict):
            is_regex = True
            regex = is_firmware_type_info.get("regex", None)
            regex_key = is_firmware_type_info.get("key", None)
        else:
            # get the check firmware function
            is_firmware_type = getattr(self, is_firmware_type_info, None)
            if not is_firmware_type:
                # the check firmware function does not exist!
                logger.error(
                    "Could not find the check firmware function '%s'.  You may be running an old version.",
                    is_firmware_type_info
                )
                return False
        if not is_regex and parsed_response and is_firmware_type(parsed_response):
            return True
        elif is_regex and regex and FirmwareChecker.get_regex_check(
                firmware_check_result, regex, regex_key
        ):
            return True
        return False

    def _get_firmware_version_match(self, firmware_check_result, firmware_type):
        get_version_info = firmware_type.get("functions", {}).get("get_version", None)
        parsed_response = firmware_check_result.get("m115_parsed_response", None)
        is_regex = False
        if isinstance(get_version_info, dict):
            is_regex = True
            regex = get_version_info.get("regex", None)
            regex_key = get_version_info.get("key", None)
        else:
            # get the check firmware function
            get_version_info = getattr(self, get_version_info, None)
            if not get_version_info:
                # the check firmware function does not exist!
                logger.error(
                    "Could not find the get_version firmware function '%s'.  You may be running an old version.",
                    get_version_info
                )
                return False
        if not is_regex and parsed_response:
            return get_version_info(parsed_response)
        elif is_regex and regex:
            return FirmwareChecker.get_regex_match(firmware_check_result, regex, regex_key)
        return False

    def _get_arcs_enabled_match(self, firmware_check_result, firmware_type):
        get_arcs_enabled_info = firmware_type.get("functions", {}).get("arcs_enabled", None)
        get_arcs_not_enabled_info = firmware_type.get("functions", {}).get("arcs_not_enabled", None)
        parsed_response = firmware_check_result.get("m115_parsed_response", None)
        is_regex = False
        arcs_enabled = None
        arcs_not_enabled = None
        # Check Arcs Enabled
        if get_arcs_not_enabled_info:
            if isinstance(get_arcs_enabled_info, dict):
                is_regex = True
                regex = get_arcs_enabled_info.get("regex", None)
                regex_key = get_arcs_enabled_info.get("key", None)
            else:
                # check the arcs_enabled function
                get_arcs_enabled = getattr(self, get_arcs_enabled_info, None)

            if not is_regex and parsed_response and get_arcs_enabled:
                arcs_enabled = get_arcs_enabled(parsed_response)
            elif is_regex and regex:
                arcs_enabled = FirmwareChecker.get_regex_check(firmware_check_result, regex, regex_key)

            if arcs_enabled:
                return True

        # check arcs not enabled
        if get_arcs_not_enabled_info:
            if isinstance(get_arcs_not_enabled_info, dict):
                is_regex = True
                regex = get_arcs_not_enabled_info.get("regex", None)
                regex_key = get_arcs_not_enabled_info.get("key", None)
            else:
                # check the arcs_enabled function
                get_arcs_not_enabled = getattr(self, get_arcs_not_enabled_info, None)

            if not is_regex and parsed_response and get_arcs_not_enabled:
                arcs_not_enabled = get_arcs_not_enabled(parsed_response)
            elif is_regex and regex:
                arcs_not_enabled = FirmwareChecker.get_regex_check(firmware_check_result, regex, regex_key)

            if arcs_not_enabled:
                return False

        return None

    @staticmethod
    def _try_extract_arcs_enabled(parsed_m115_response):
        capabilities = parsed_m115_response.get(FirmwareChecker.MARLIN_EXTENDED_CAPABILITIES_KEY, dict())
        arcs = capabilities.get("ARCS", None)
        if arcs is not None:
            arcs = arcs == "1"
        return arcs

    @staticmethod
    def is_version_in_versions(current_version_string, version_checks, firmware_type, compare_type):
        if compare_type == "date":
            current_value = FirmwareChecker.parse_datetime(current_version_string)
        elif compare_type == "semantic":
            if "clean_version" in firmware_type["functions"]:
                clean_version_name = firmware_type["functions"]["clean_version"]
                clean_version = getattr(FirmwareChecker, clean_version_name, None)
                current_version_string = clean_version(current_version_string)
            current_value = parse_version(current_version_string)

        return utilities.is_version_in_versions(current_version_string, version_checks, compare_type)

    @staticmethod
    def parse_datetime(datetime_string):
        date_formats = [
            "%b %d %Y %H:%M:%S",
            "%b %d %Y %H:%M:%S",
        ]
        for date_format in date_formats:
            try:
                return datetime.strptime(datetime_string, date_format)
            except ValueError:
                pass
        return None

    ################
    # Regex dynamic matching functions
    ################
    @staticmethod
    def get_firmware_regex_string(firmware_info, key=None):
        match_text = None
        if key is not None:
            match_text = firmware_info.get("m115_parsed_response", {}).get(key, None)
        else:
            match_text = firmware_info.get("m115_response", None)
            if isinstance(match_text, list):
                match_text = "\n".join(match_text)
        return match_text

    @staticmethod
    def get_regex_match(firmware_info, compiled_regex, key=None):
        match_text = FirmwareChecker.get_firmware_regex_string(firmware_info, key)
        if match_text is None:
            return False
        result = compiled_regex.match(match_text)
        if result:
            return result.group(1)
        return False

    @staticmethod
    def get_regex_check(firmware_info, compiled_regex, key=None):
        match_text = FirmwareChecker.get_firmware_regex_string(firmware_info, key)
        if match_text is None:
            return False
        return compiled_regex.search(match_text)

    ################
    # Dynamically called static functions for specific firmware
    ################
    # Prusa Firmware Functions
    @staticmethod
    def check_firmware_prusa(parsed_firmware_response):
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return False
        firmware_name = parsed_firmware_response["FIRMWARE_NAME"]
        if not firmware_name:
            return False
        return firmware_name.startswith("Prusa-Firmware") and not firmware_name.startswith("Prusa-Firmware-Buddy")

    REGEX_PRUSA_VERSION = re.compile(r"^Prusa-Firmware\s([^\s]+)")
    @staticmethod
    def get_version_prusa(parsed_firmware_response):
        if "FIRMWARE_VERSION" in parsed_firmware_response:
            return parsed_firmware_response["FIRMWARE_VERSION"]
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return None
        result = FirmwareChecker.REGEX_PRUSA_VERSION.match(parsed_firmware_response["FIRMWARE_NAME"])
        if result:
            return result.group(1)

    # Prusa Buddy Firmware Functions
    @staticmethod
    def check_firmware_prusa_buddy(parsed_firmware_response):
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return False
        firmware_name = parsed_firmware_response["FIRMWARE_NAME"]
        if not firmware_name:
            return False
        return firmware_name.startswith("Prusa-Firmware-Buddy")

    REGEX_MARLIN_PRUSA_BUDDY = re.compile(r"^Prusa-Firmware-Buddy\s([^\s]+)")

    @staticmethod
    def get_version_prusa_buddy(parsed_firmware_response):
        if "FIRMWARE_VERSION" in parsed_firmware_response:
            return parsed_firmware_response["FIRMWARE_VERSION"]
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return None
        result = FirmwareChecker.REGEX_MARLIN_PRUSA_BUDDY.match(parsed_firmware_response["FIRMWARE_NAME"])
        if result:
            return result.group(1)

    # Marlin Firmware Functions
    @staticmethod
    def check_firmware_marlin(parsed_firmware_response):
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return False
        firmware_name = parsed_firmware_response["FIRMWARE_NAME"]
        if not firmware_name:
            return False
        return firmware_name.startswith("Marlin")

    REGEX_MARLIN_VERSION = re.compile(r"^Marlin\s([^\s]+)")
    @staticmethod
    def get_version_marlin(parsed_firmware_response):
        if "FIRMWARE_VERSION" in parsed_firmware_response:
            return parsed_firmware_response["FIRMWARE_VERSION"]
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return None
        result = FirmwareChecker.REGEX_MARLIN_VERSION.match(parsed_firmware_response["FIRMWARE_NAME"])
        if result:
            return result.group(1)

    @staticmethod
    def get_arcs_enabled_marlin(parsed_firmware_response):
        return FirmwareChecker._try_extract_arcs_enabled(parsed_firmware_response)

    # Virtual Marlin Functions
    @staticmethod
    def check_firmware_virtual_marlin(parsed_firmware_response):
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return False
        firmware_name = parsed_firmware_response["FIRMWARE_NAME"]
        if not firmware_name:
            return False
        return firmware_name.startswith("Virtual Marlin")

    REGEX_VIRTUAL_MARLIN_VERSION = re.compile(r"^Virtual Marlin\s([^\s]+)")

    @staticmethod
    def get_version_virtual_marlin(parsed_firmware_response):
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return None
        result = FirmwareChecker.REGEX_VIRTUAL_MARLIN_VERSION.match(parsed_firmware_response["FIRMWARE_NAME"])
        if result:
            return result.group(1)

    @staticmethod
    def get_arcs_enabled_virtual_marlin(parsed_firmware_response):
        return FirmwareChecker._try_extract_arcs_enabled(parsed_firmware_response)

    # Klipper Firmware Functions
    @staticmethod
    def check_firmware_klipper(parsed_firmware_response):
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return False
        firmware_name = parsed_firmware_response["FIRMWARE_NAME"]
        if not firmware_name:
            return False
        return firmware_name.startswith("Klipper")

    REGEX_KLIPPER_VERSION = re.compile(r"^Klipper\s([^\s]+)")

    @staticmethod
    def get_version_klipper(parsed_firmware_response):
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return None
        result = FirmwareChecker.REGEX_MARLIN_VERSION.match(parsed_firmware_response["FIRMWARE_NAME"])
        if result:
            return result.group(1)

    @staticmethod
    def clean_version_klipper(version):
        try:
            pos = version.index('-', version.index('.', version.index('.') + 1)+1)
            version = version[:pos] + '+' + version[pos + 1:]
        except ValueError:
            pass
        return version

    # Smoothieware Firmware Functions
    @staticmethod
    def check_firmware_smoothieware(parsed_firmware_response):
        if "FIRMWARE_NAME" not in parsed_firmware_response:
            return False
        firmware_name = parsed_firmware_response["FIRMWARE_NAME"]

        return firmware_name.startswith("Smoothieware")

    # Parts of this function were copied from the Octoprint Source within util.com
    REGEX_PARSE_CAPABILITIES = re.compile(r"Cap:([A-Z0-9_]+):([A-Z0-9_]+)\s*")
    MARLIN_EXTENDED_CAPABILITIES_KEY = "EXTENDED_CAPABILITIES_REPORT"
    # regex copied from octoprint source at util.comm
    REGEX_FIRMWARE_RESPONSE_SPLITTER = re.compile(r"\s*([A-Z0-9_]+):\s*")
    @staticmethod
    def parse_m115_response(response_lines):
        # Separate the first element from the rest, it is often special
        response_text = response_lines[0]
        capabilities_text = None
        if len(response_lines) > 1:
            # combine all of the remaining elements into a single string
            capabilities_text = "\n".join(response_lines[1:])

        if response_text.startswith("NAME."):
            # Good job Malyan. Why use a : when you can also just use a ., right? Let's revert that.
            response_text = list(response_text)
            response_text[4] = ":"
            response_text = "".join(response_text)

        result = dict()
        capabilities = dict()
        # check to see if this is smoothieware.  We need to parse it differently than the others
        if "FIRMWARE_NAME:Smoothieware" in response_text:
            # first split with commas
            split  = response_text.split(",")
            for param in split:
                # find the first colon, which separates the key from the value
                index = param.find(":")
                key = param[0:index].strip()
                value = None
                # get the value if there is any
                if len(param)>index+1:
                    value = param[index+1:].strip()

                if len(key) > 2 and key.startswith("X-"):
                    # if the key starts with X-, it is an extended capability
                    # remove the X- and add to the capabilities
                    key = key[2:].strip()
                    capabilities[key] = value
                else:
                    # this is a normal key/value
                    result[key] = value
        else:
            split_regex = FirmwareChecker.REGEX_FIRMWARE_RESPONSE_SPLITTER
            split_line = split_regex.split(response_text.strip())[1:]  # first entry is empty start of trimmed string

            for i in range(0, len(split_line), 2):
                key = split_line[i]
                value = split_line[i+1]
                result[key] = value.strip()

        if capabilities_text and FirmwareChecker.MARLIN_EXTENDED_CAPABILITIES_KEY not in result:
            # parse the capabilities
            matches = FirmwareChecker.REGEX_PARSE_CAPABILITIES.findall(capabilities_text)

            for match in matches:
                capabilities[match[0]] = match[1]

        # add the capabilities, if there are any
        if len(capabilities) > 0:
            result[FirmwareChecker.MARLIN_EXTENDED_CAPABILITIES_KEY] = capabilities
        return result

    @property
    def is_checking(self):
        with self._shared_data_rlock:
            return self._is_checking

    @property
    def firmware_types_version(self):
        with self._shared_data_rlock:
            return self._firmware_types_version

    @property
    def current_firmware_info(self):
        with self._shared_data_rlock:
            return self._current_firmware_info

    def check_firmware_async(self):
        with self._shared_data_rlock:
            if self._is_checking:
                return False
            self._is_checking = True



        def check_firmware():
            result = {
                "success": False,
                "error": "",
                "firmware_version": None
            }
            logger.info("Checking firmware version");
            firmware_version = self._get_firmware_version()

            if firmware_version["success"]:
                result["success"] = True
                if (
                        firmware_version.get("arcs_enabled", None) is None
                        or firmware_version.get("g2_g3_supported") is None
                ):
                    firmware_version = self._get_g2_response(firmware_version)

                result["firmware_version"] = firmware_version
            else:
                logger.error(firmware_version["error"])
                result["error"] = firmware_version["error"]

            if result["success"]:
                self._save_current_firmware_info(firmware_version)
                self._current_firmware_info = firmware_version
            logger.info("Firmware check complete.")
            self._request_complete_callback(result)
            with self._shared_data_rlock:
                self._is_checking = False

        thread = threading.Thread(
            target=check_firmware
        )
        thread.daemon = True
        thread.start()

        return True

    def _get_m115_response(self):
        # this can be tried for all firmware.
        # first create a request
        request = PrinterRequest("Get M115 Response", ["M115"], self._check_m115_response, wait_for_ok=True)
        result = self._get_printer_response(request)
        if result is not None:
            return result.response

    def _get_g2_response(self, firmware_info):
        """
        If arc support or arcs enabled is unknown, this may clear things up.
        :param version_info: parsed firmware version information
        :return: modified version_info
        """
        # this can be tried for all firmware.
        # first create a request
        request = PrinterRequest(
            "Get G2 Response", ["G2"], self._check_g2_response,
            check_sent_function=FirmwareChecker._check_g2_sent,
            wait_for_ok=False,
            append_final_response=True
        )

        result = self._get_printer_response(request)

        arcs_enabled = None
        if result.response is not None:
            firmware_info["arcs_enabled"] = FirmwareChecker._g2_g3_response_enabled(result.response[0], firmware_info)

        # If we found that arcs are enabled, g2_g3 is definitely supported
        if firmware_info["arcs_enabled"] and not firmware_info["g2_g3_supported"]:
            firmware_info["g2_g3_supported"] = True

        return firmware_info

    @staticmethod
    def _g2_g3_response_enabled(response_text, version_info):
        # right now we can only know for sure if g2_g3 is unknown, not if it is enabled.
        # we may be able to get confirmation for certain known firmware.
        if FirmwareChecker._check_for_unknown_command_response(response_text):
            return False
        elif FirmwareChecker._check_for_bad_parameter_response(response_text):
            return True
        return None

    @staticmethod
    def _check_g2_sent(gcode):
        return gcode == "G2"

    @staticmethod
    def _check_g2_response(response_text):
        return (
            FirmwareChecker._check_for_ok_response(response_text)
            or FirmwareChecker._check_for_unknown_command_response(response_text)
            or FirmwareChecker._check_for_bad_parameter_response(response_text)
        )

    @staticmethod
    def _check_for_ok_response(response_text):
        return response_text.strip().upper().startswith("OK")

    @staticmethod
    def _check_for_unknown_command_response(response_text):
        return response_text.strip().upper().startswith("UNKNOWN")

    @staticmethod
    def _check_for_bad_parameter_response(response_text):
        return response_text.strip().upper().startswith("G2/G3 BAD PARAMETER")

    @staticmethod
    def _check_m115_response(response_text):
        return "FIRMWARE_NAME" in response_text

    def _get_is_request_open(self):
        return self._printer_request is not None and not self._printer_request.response_ended

    def _get_request_waiting_for_send(self):
        return self._printer_request.wait_for_gcode_sent() and not self._printer_request.gcode_sent
    
    def _get_printer_response(self, request, timeout_ms=None):
        '''Sends a request, gets a response.'''
        # acquire the request lock in case we run this with threads in the future
        with self._send_request_lock:
            if timeout_ms is None:
                timeout_ms = FirmwareChecker.DEFAULT_TIMEOUT_MS

            logger.info(
                "Arc Welder is sending a %s request to your printer with a %ims timeout.", request.name, timeout_ms
            )

            # make sure we are not printing
            if self._is_printing:
                logger.error("Arc Welder cannot send a request while printing")
                raise PrintingException

            # we want to handle any exceptions that may happen here
            try:
                #### CANNOT SET JOB_ON_HOLD FOR SOME REASON!
                # set the job_on_hold lock to prevent any jobs from printing
                #logger.info("Acquiring the job_on_hold lock.")
                #with self._printer.job_on_hold(True):
                # set the current request
                with self._request_lock:
                    self._printer_request = request

                # get the tags
                tags = request.tags
                # make sure the ARCWELDER_TAG is added
                tags.add(FirmwareChecker.ARCWELDER_TAG)

                if self._request_signal.is_set():
                    # the event is set.  Clear it and send the request
                    self._request_signal.clear()

                    # send the request thread
                    logger.info(
                        "Arc Welder is sending the following commands for the %s request: \n%s"
                        , request.name
                        , "\n\t".join(request.commands)
                    )
                    self._printer.commands(request.commands, tags=tags)

                    # wait for a response
                    event_is_set = self._request_signal.wait(timeout_ms/1000.0)
                    if not event_is_set:
                        # we ran into a timeout while waiting for a response from the printer
                        logger.error("A timeout occurred while waiting for a response from the printer.")
                        self._request_signal.set()
                else:
                    # the event is NOT set.  This is bad.
                    logger.error("An existing event is not set!  This indicates a request was sent, but no "
                                 "response was received and the original event was not cleared!")

                # return the request.  It may be None
                return self._printer_request

            except Exception as e:
                # log and re-raise the exception
                logger.exception("A problem occurred sending a request to the printer.")
                raise e
            finally:
                # clear the current request
                with self._request_lock:
                    self._printer_request = None

    # noinspection PyUnusedLocal
    def on_gcode_sending(self, comm_instance, phase, cmd, cmd_type, gcode, *args, **kwargs):

        # ensure that each hook must wait for the other to complete
        with self._request_lock as r:
            if self._get_is_request_open():
                logger.verbose(
                    "on_gcode_sent: Gcode Sent: %s", cmd
                )
                if self._get_request_waiting_for_send():
                    logger.verbose("on_gcode_sent:  Gcode sending for request: %s, gcode: %s", self._printer_request.name, cmd)
                    self._printer_request.check_sent(cmd)
                    if self._printer_request.gcode_sent:
                        logger.verbose("on_gcode_sent: Gcode sent for request: %s", self._printer_request.name)
                    else:
                        logger.verbose(
                            "on_gcode_sent: Gcode sent check failed for request: %s", self._printer_request.name
                        )
                else:
                    logger.verbose(
                        "on_gcode_sent: Not waiting for send for request: %s.  gcode: %s", self._printer_request.name, cmd
                    )

    # noinspection PyUnusedLocal
    def on_gcode_received(self, comm, line, *args, **kwargs):
        '''Must be called when gcodes are about to be sent by the owner.'''
        # ensure that each hook must wait for the other to complete
        with self._request_lock as r:
            # see if there is a pending request.  Do this without a lock for speed. I think this is OK
            if self._get_is_request_open():
                clean_line = line.strip()
                logger.verbose(
                    "on_gcode_received: Response Received: %s", clean_line
                )

                # test the printer's response, throw no exceptions
                try:
                    success = False
                        # recheck to ensure printer request is not none now that we've acquired the lock
                    if self._printer_request is not None:
                        if not self._get_request_waiting_for_send():
                            if self._printer_request.response_started == False:
                                logger.verbose("on_gcode_received: checking response '%s'.", clean_line)
                                # ensure atomic writes here
                                success = self._printer_request.check_response(clean_line)
                                self._printer_request.response_started = success
                                if success:
                                    logger.verbose("on_gcode_received: Response found.")
                                    if not self._printer_request.wait_for_ok:
                                        logger.verbose("on_gcode_received: Not waiting for OK, response ended.")
                                        self._printer_request.response_ended = True
                                    else:
                                        logger.verbose("on_gcode_received: Waiting for OK.")
                            else:
                                success = True
                                if self._check_for_ok_response(clean_line):
                                    logger.verbose("on_gcode_received: OK found, response ended.")
                                    self._printer_request.response_ended = True
                        else:
                            logger.verbose("on_gcode_received: Waiting for request to be sent.")
                    if success:
                        # re got a response, set the signal
                        if not self._printer_request.response:
                            logger.verbose("on_gcode_received: First response received.")
                            self._printer_request.response = []

                        # we want to append the response in two scenarios:
                        # 1.  The response has not ended
                        # 2.  The response has ended and the request indicates that we should append the
                        #     final response
                        if (
                                not self._printer_request.response_ended
                                or (
                                    self._printer_request.response_ended
                                    and self._printer_request.append_final_response
                                )
                        ):
                            logger.verbose("on_gcode_received: Appending line to response.")
                            self._printer_request.response.append(clean_line)
                        if self._printer_request.response_ended:
                            logger.verbose("on_gcode_received: Triggering event.")
                            self._request_signal.set()
                except Exception as e:
                    logger.exception("on_gcode_received: An error occurred while checking the printer response.")
        # ALWAYS return the line
        return line


class PrinterRequest:
    def __init__(
            self, name, commands, check_response_function, check_sent_function=None, wait_for_ok=False,
            append_final_response=False, tags=set()
    ):
        self.name = name
        self.commands = commands
        self.check_sent_function = check_sent_function
        self.check_response_function = check_response_function
        self.wait_for_ok = wait_for_ok
        self.append_final_response = append_final_response
        self.gcode_sent = False
        self.response = None
        self.response_started = False
        self.response_ended = False
        self.tags = tags

    def wait_for_gcode_sent(self):
        return self.check_sent_function is not None

    def check_sent(self, gcode):
        self.gcode_sent = self.check_sent_function(gcode)
        return self.gcode_sent

    def check_response(self, response_string):
        return self.check_response_function(response_string) or False


class FirmwareFileUpdater:

    @staticmethod
    def update_firmware_info(plugin_vesrion, settings_version, firmware_types_path, firmware_docs_path):
        logger.info("Updating firmware info from server.")
        results = {
            "success": False,
            "warning": None,
            "error": None,
            "error_type": None,
            "cause": None,
            "new_version": False
        }
        # first get the available firmware versions
        try:
            version_info = FirmwareFileUpdater._get_best_settings_version(plugin_vesrion, settings_version)
        except FirmwareFileUpdaterError as e:
            logger.exception("An error occurred getting the best settings version.")
            results.error = e.message
            return results
        if version_info is None:
            results["success"] = True
            return results

        logger.info("A newer version (%s) is available.  Loading version info.", version_info["version"])

        try:
            firmware_types = FirmwareFileUpdater._get_firmware_types_for_version(version_info)
        except FirmwareFileUpdaterError as e:
            results["error"] = "Could not find the firmware type file for version {0}".format(
                version_info["version"]
            )
            logger.exception(results["error"])
            return results

        logger.info("Retrieved firmware types file version %s.", firmware_types["version"])
        try:
            version_docs = FirmwareFileUpdater._get_docs_for_version(version_info, firmware_types)
        except FirmwareFileUpdaterError as e:
            results["error"] = "Could not find documents for version {0}".format(
                version_info["version"])
            logger.exception(results["error"])
            return results
        logger.info("Fetched %d firmware help files.", len(version_docs))

        # make sure the firmware types path exists
        if not os.path.exists(os.path.dirname(firmware_types_path)):
            firmware_types_directory = os.path.dirname(firmware_types_path)
            logger.info("Creating firmware types folder at: %s", firmware_types_directory)
            os.makedirs(firmware_types_directory)

        # save the firmware types
        with open(firmware_types_path, 'w') as firmware_type_file:
            firmware_type_file.write(json.dumps(firmware_types, sort_keys=True, indent=4))

        # save the docs to the static folder, with overwrite
        for document in version_docs:
            with open(os.path.join(firmware_docs_path, document["name"]), 'w') as firmware_type_file:
                firmware_type_file.write(document["data"])

        results["new_version"] = firmware_types["version"]
        results["success"] = "True"
        return results

    @staticmethod
    def _get_best_settings_version(plugin_version, current_version):
        # load the available versions for the current settings version.  This number will be incremented as the
        # settings change enough to not be backwards compatible
        versions = FirmwareFileUpdater._get_versions()["versions"]
        if not versions:
            return None
        versions = sorted(versions, key=lambda k: parse_version(k['version']), reverse=True)
        settings_version = None
        for version_info in versions:
            # make sure the settings plugin version is good.
            if (
                    "plugin_compatibility" in version_info and
                    not utilities.is_version_in_versions(plugin_version, version_info["plugin_compatibility"])
             ):
                continue
            if parse_version(str(version_info["version"])) >= parse_version(str(current_version)):
                # found a version!  Normally, this will be the first entry that passes the
                # plugin compatibility test, but there could be some oddballs
                settings_version = version_info
                break
            elif parse_version(str(version_info["version"])) < parse_version(str(current_version)):
                # this version is OLDER than the current version, exit.
                break

        if settings_version and parse_version(settings_version["version"]) == parse_version(current_version):
            return None
        return settings_version

    @staticmethod
    def _get_versions():
        try:
            # load the available versions
            r = requests.get(
                (
                    "https://raw.githubusercontent.com/FormerLurker/ArcWelderPluginFirmwareInfo/main/versions.json"
                    "?nonce={0}".format(uuid.uuid4().hex)
                ),
                timeout=float(10)
            )
            r.raise_for_status()
        except (
                requests.exceptions.HTTPError,
                requests.exceptions.ConnectionError,
                requests.exceptions.ConnectTimeout
        ) as e:
            message = "An error occurred while retrieving firmware types versions from the server."
            raise FirmwareFileUpdaterError('profiles-retrieval-error', message, cause=e)
        if 'content-length' in r.headers and r.headers["content-length"] == 0:
            message = "No Octolapse version data was returned while requesting profiles"
            raise FirmwareFileUpdaterError('no-data', message)
        # if we're here, we've had great success!
        return r.json()

    @staticmethod
    def _get_url_for_version(version_info):
        # build up keys string
        return (
            "https://raw.githubusercontent.com/FormerLurker/ArcWelderPluginFirmwareInfo/main/{0}/types.json?nonce={1}"
                .format(version_info["version_folder"], uuid.uuid4().hex)
        )

    @staticmethod
    def _get_firmware_types_for_version(version_info):
        url = FirmwareFileUpdater._get_url_for_version(version_info)
        r = requests.get(url, timeout=float(5))
        if r.status_code != requests.codes.ok:
            message = (
                "An invalid status code or {0} was returned while getting available firmware versions at {1}."
                    .format(r.status_code, url)
            )
            raise FirmwareFileUpdaterError('invalid-status-code', message)
        if 'content-length' in r.headers and r.headers["content-length"] == 0:
            message = "No profile data was returned for a request at {0}.".format(url)
            raise FirmwareFileUpdaterError('no-data', message)
        # if we're here, we've had great success!
        return r.json()

    @staticmethod
    def _get_url_for_document(version_info, doc_name):
        # build up keys string
        return (
            "https://raw.githubusercontent.com/FormerLurker/ArcWelderPluginFirmwareInfo/main/{0}/docs/{1}?nonce={2}"
                .format(version_info["version_folder"], doc_name, uuid.uuid4().hex)
        )

    @staticmethod
    def _get_docs_for_version(version_info, firmware_types):
        document_names = []
        # iterate the firmware type and versions and extract all of the help file names
        for firmware_type_key, firmware_type in firmware_types["types"].items():
            if "help_file" in firmware_type:
                document_names.append(firmware_type["help_file"])
            for version in firmware_type["versions"]:
                if "help_file" in version:
                    document_names.append(version["help_file"])

        # now download all the individual files
        documents = []
        for document_name in document_names:
            url = FirmwareFileUpdater._get_url_for_document(version_info, document_name)
            r = requests.get(url, timeout=float(5))
            if r.status_code != requests.codes.ok:
                message = (
                    "An invalid status code or {0} was returned while getting available firmware document at {1}."
                    .format(r.status_code, url)
                )
                logger.error(message)
                continue
            if 'content-length' in r.headers and r.headers["content-length"] == 0:
                message = "No version document data was returned for a request at {0}.".format(url)
                logger.error(message)
                continue
            # if we're here, we've had great success!
            documents.append({
                "name": document_name,
                "data": r.content
            })
        return documents


class FirmwareFileUpdaterError(Exception):
    def __init__(self, error_type, message, cause=None):
        super(FirmwareFileUpdaterError, self).__init__()
        self.error_type = error_type
        self.cause = cause if cause is not None else None
        self.message = message

    def __str__(self):
        if self.cause is None:
            return "{0}: {1}".format(self.error_type, self.message)
        return "{0}: {1} - Inner Exception: {2}".format(self.error_type, self.message, "{}".format(self.cause))


class PrintingException(Exception):
    pass
