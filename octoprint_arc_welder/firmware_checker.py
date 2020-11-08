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
import threading
import re
import json
import os
import shutil
from datetime import datetime
from pkg_resources import parse_version
import octoprint_arc_welder.log as log

logging_configurator = log.LoggingConfigurator("arc_welder", "arc_welder.", "octoprint_arc_welder.")
root_logger = logging_configurator.get_root_logger()
# so that we can
logger = logging_configurator.get_logger(__name__)


class FirmwareChecker:

    DEFAULT_TIMEOUT_MS = 100000
    ARCWELDER_TAG = 'arc_welder'
    FIRMWARE_TYPES_JSON_PATH = ["firmware", "types.json"]
    FIRMWARE_TYPES_DEFAULT_JSON_PATH = ["data", "firmware", "types_default.json"]
    CURRENT_FIRMWARE_JSON_PATH = ["firmware", "current.json"]

    def __init__(self, printer, base_folder, data_directory, request_complete_callback, load_defaults=False):
        self._firmware_types_default_path = os.path.join(
            base_folder, *self.FIRMWARE_TYPES_DEFAULT_JSON_PATH
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
        self._firmware_types = self._load_firmware_types(load_defaults)

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
        # Make sure only one thread can access with self._current_firmware_info:
        self._current_firmware_rlock = threading.RLock()
        # The most recent firmware version check
        self._current_firmware_info = None
        # Load the most recent firmware info if it exists.
        self._load_current_firmware_info()

    def _load_firmware_types(self, load_defaults):
        logger.info("Loading firmware types from: %s", self._firmware_types_path)
        if not load_defaults:
            try:
                with open(self._firmware_types_path) as f:
                    return json.load(f)
            except (IOError, OSError) as e:
                logger.info("The firmwware types file does not exist.  Creating from defaults.")
            except ValueError as e:
                logger.error("Could not parse the firmware types file.  Recreating from the defaults.")

        # The firmware types file either does not exist, or it is corrupt.  Recreate from the defaults
        if not os.path.exists(os.path.dirname(self._firmware_types_path)):
            firmware_types_directory = os.path.dirname(self._firmware_types_path)
            logger.info("Creating firmware types folder at: %s", firmware_types_directory)
            os.makedirs(firmware_types_directory)
        shutil.copy(self._firmware_types_default_path, self._firmware_types_path)
        with open(self._firmware_types_path) as f:
            return json.load(f)

    def _load_current_firmware_info(self):
        if not os.path.isfile(self._current_firmware_path):
            logger.info("No current firmware info exists at: %s", self._current_firmware_path)
            # no file exists, return none
            return None
        logger.info("Loading current firmware info from: %s.", self._current_firmware_path)
        try:
            with open(self._current_firmware_path) as f:
                with self._current_firmware_rlock:
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
            is_firmware_type_name = firmware["functions"]["is_firmware_type"]
            # get the check firmware function
            is_firmware_type = getattr(self, is_firmware_type_name, None)
            if not is_firmware_type:
                # the check firmware function does not exist!
                logger.error(
                    "Could not find the check firmware function '%s'.  You may be running an old version.",
                     is_firmware_type_name
                )
                continue
            if is_firmware_type(parsed_response):
                result["type"] = firmware_key
                firmware_type = firmware
                break

        if not firmware_type:
            error = "Arc Welder does not recognize this firmware."
            return result

        get_version_function_name = firmware["functions"]["get_version"]

        # get the check firmware function if we've not already found one
        if not firmware_version:
            get_version = getattr(self, get_version_function_name, None)
            firmware_version = get_version(parsed_response)
            result["version"] = firmware_version

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
                if result["is_future"] and index > 0:
                    result["previous_notes"] = firmware_type["versions"][index-1].get("notes", None)
                break

        return result

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

        if current_value is None:
            # either we have no version info, or parsing failed
            return False

        for version_check in [x.strip() for x in version_checks.split(",")]:
            # see if this is a single or two character compare (they all end with =)
            # then get the logical operator and the value to compare
            if len(version_check) > 1 and version_check[1] == "=":
                logical_operator = version_check[0:2]
                compare_string = version_check[2:]
            else:
                logical_operator = version_check[0:1]
                compare_string = version_check[1:]

            # make sure we have something to compare and a method of comparison.
            if not compare_string:
                logger.error("No value to compare in version check '%s'", version_check)
                return False
            if logical_operator not in ["<=", ">=", "!=", ">", "<", "="]:
                logger.error("Unknown logical operation '%s' in version check '%s'", logical_operator, version_check)
                return False

            # Convert the compare string to something we can use logical operators on
            if compare_type == "date":
                # parse the date into a datetime
                compare_value = FirmwareChecker.parse_datetime(compare_string)
            elif compare_type == "semantic":
                # parse the version number
                compare_value = parse_version(compare_string)
            else:
                # this shouldn't happen, but handle it gracefully in case there are typos
                logger.error(
                    "Unknown compare type '%s' for the version check '%s'.",
                    compare_type,
                    version_check
                )

            if compare_value is None:
                # this shouldn't happen, but typos occur.  Handle them gracefully
                logger.error(
                    "Could not parse the compare value '%s' via the %s compare type.",
                    compare_string,
                    compare_type
                )
                continue

            # see what kind of compare we will do
            # test the longest ones first
            # Note that we've already validated the logical operator
            if logical_operator == "<=":
                if current_value <= compare_value:
                    continue
            elif logical_operator == ">=":
                if current_value >= compare_value:
                    continue
            elif logical_operator == "!=":
                if current_value != compare_value:
                    continue
            elif logical_operator == ">":
                if current_value > compare_value:
                    continue
            elif logical_operator == "<":
                if current_value < compare_value:
                    continue
            elif logical_operator == "=":
                if current_value == compare_value:
                    continue
            # either there is an unknown logical operator, or the compare failed
            return False
        # all checks have passed, this is the right version
        return True

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

    def get_current_firmware(self):
        with self._current_firmware_rlock:
            return self._current_firmware_info

    def check_firmware_async(self):
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

        thread = threading.Thread(
            target=check_firmware
        )
        thread.daemon = True
        thread.start()

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
        return (
            response_text.startswith("ok")
            or response_text.startswith("OK")
            or response_text.startswith("Ok")
        )

    @staticmethod
    def _check_for_unknown_command_response(response_text):
        return (
            response_text.startswith("Unknown")
            or response_text.startswith("unknown")
            or response_text.startswith("UNKNOWN")
        )

    @staticmethod
    def _check_for_bad_parameter_response(response_text):
        return response_text.startswith("G2/G3 bad parameters")

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
                    self._printer_request = request

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
                logger.verbose(
                    "on_gcode_received: Response Received: %s", line
                )
                # test the printer's response, throw no exceptions
                try:
                    success = False
                        # recheck to ensure printer request is not none now that we've acquired the lock
                    if self._printer_request is not None:
                        if not self._get_request_waiting_for_send():
                            if self._printer_request.response_started == False:
                                logger.verbose("on_gcode_received: checking response '%s'.", line)
                                # ensure atomic writes here
                                success = self._printer_request.check_response(line)
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
                                if line in ["ok", "OK"]:
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
                            self._printer_request.response.append(line)
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


class PrintingException(Exception):
    pass
