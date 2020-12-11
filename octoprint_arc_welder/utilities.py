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
from builtins import dict, str
from pkg_resources import parse_version
import six
import os
import sys
import ntpath
from datetime import datetime
import time
import octoprint_arc_welder.log as log

logging_configurator = log.LoggingConfigurator("arc_welder", "arc_welder.", "octoprint_arc_welder.")
root_logger = logging_configurator.get_root_logger()
# so that we can
logger = logging_configurator.get_logger(__name__)


def remove_extension_from_filename(filename):
    return os.path.splitext(filename)[0]


def get_filename_from_path(filepath):
    head, tail = ntpath.split(filepath)
    return tail or ntpath.basename(head)


def get_extension_from_filename(filename):
    head, tail = ntpath.split(filename)
    file_name = tail or ntpath.basename(head)
    split_filename = os.path.splitext(file_name)
    if len(split_filename) > 1:
        extension = split_filename[1]
        if len(split_filename) > 1:
            return extension[1:]
    return ""

def dict_encode(d):
    # helpers for dealing with bytes (string) values delivered by the converter
    # socks.js doesn't like mixed encoding
    def dict_key_value_encode(s):
        if isinstance(s, dict):
            return dict_encode(s)
        try:
            if isinstance(s, str):
                return unicode(s, errors='ignore', encoding='utf-8')
        except NameError:  # Python 3
            if isinstance(s, bytes):
                return str(s, errors='ignore', encoding='utf-8')
        return s
    return {dict_key_value_encode(k): dict_key_value_encode(v) for k, v in six.iteritems(d)}


def does_file_contain_text(file_path, search_text, lines_to_search=100, convert_case=True):
    """
    Search a file for text
    :param file_path: Path to the file on disk
    :param search_text: Either a string or list of strings to search for
    :param lines_to_search: The maximum number of lines that will be searched
    :param convert_case: perform a case insensitive search (could cause problems with unicode, need to test)
    :return:
    """
    if not isinstance(search_text, list):
        search_text = [search_text]
    try:
        if convert_case:
            for i in range(len(search_text)):
                search_text[i] = search_text[i].upper()
        with open(file_path, "r") as file_to_search:
            lines_read = 0
            while lines_read < lines_to_search:
                lines_read += 1
                line = file_to_search.readline(1000)
                if convert_case:
                    line = line.upper()
                for text in search_text:
                    if text in line:
                        return True
    except (IOError, OSError, ValueError) as e:
        logger.exception("Error searching `%s` for '%s'", file_path, search_text)
    return False


COMMENT_SEARCH_TYPE_SETTINGS = "settings_comment"
COMMENT_SEARCH_TYPE_CONTAINS = "contains"


def search_gcode_file(path_on_disk, search_functions):
    try:
        logger.debug("Searching '%s' for processing info.", path_on_disk)
        with open(path_on_disk, "rb") as f:
            return _search_gcode_file(f, search_functions)
    except (IOError, OSError) as e:
        logger.exception("Could not read the added gcode file at %s.", path_on_disk)
    return None


def _search_gcode_file(gcode_file, search_function_list, lines_to_search=100):
    result = {}
    lines_read = 0
    num_functions = len(search_function_list)
    while lines_read < lines_to_search:

        if num_functions == 0:
            # break if we have nothing to search for
            break

        # increment our line counter and read the next line
        lines_read += 1
        line = gcode_file.readline(1000)
        line = line.decode("UTF-8", 'replace')

        # find the first index of the comment
        comment_start_index = str(line).find(";")+1
        if comment_start_index == 0:
            # no comment, continue
            continue

        # Loop through each search function in reverse so we
        # can remove elements from the function list
        fn_index = num_functions - 1
        while fn_index >= 0:
            fn_def = search_function_list[fn_index]
            # perform the search, record the result
            fn_result = None
            if fn_def["type"] == COMMENT_SEARCH_TYPE_SETTINGS:
                fn_result = parse_settings_comment(
                    line[comment_start_index:], fn_def["tag"], fn_def["settings"]
                )
            elif fn_def["type"] == COMMENT_SEARCH_TYPE_CONTAINS:
                if fn_def["find"] in line.upper():
                    fn_result = True
            if fn_result:
                # set the fn_result to the fn_def["value"] if the key exists
                fn_result = fn_def.get("value", fn_result)
                if fn_def.get("if_found", None) == "return_this":
                    # we should stop and return this now
                    return {fn_def["name"]: fn_result}
                # add the search function result to the result dict
                result[fn_def["name"]] = fn_result
                # remove the function from the list and decrement the
                # function count
                del search_function_list[fn_index]
                num_functions -= 1
            fn_index -= 1
    if len(result) == 0:
        return False
    return result


def search_string(string_to_search, strings_to_find, start_index=0):
    """
    Searches for all lines in the strings_to_find list in order and returns the index right after the
    final element.

    This needs to be unusually fast since it is for file parsing via python, which is already
    slow enough.  For that reason it is a bit ugly.  Please excuse this.  However, this is way faster
    than regex and is the result of a lot of testing..

    :param string_to_search: The source string
    :param strings_to_find: A list of elements to find, in order.  Must be upper case
    :param start_index: The index of the string_to_search at which to begin searching for test.
    :return: The index of the match or -1 if it is not found
    """
    index = start_index
    search_strlen = len(string_to_search)
    for item in strings_to_find:
        # skip spaces
        if start_index > search_strlen - 1:
            return -1
        item_len = len(item)
        item_max_start_index = search_strlen - item_len
        while start_index <= item_max_start_index and string_to_search[start_index] == " ":
            start_index += 1

        if start_index > search_strlen - item_len:
            # We ran out of string to search
            return -1

        #Extract the possible match
        string_part = string_to_search[start_index: start_index+item_len].upper()
        if string_part == item:
            start_index += item_len
        else:
            return -1

    return start_index


def parse_settings_comment(line, tag, settings_dict):
    """
        Searches a single line of text for lines in the following form:
        {any text}{spaces};{spaces}{tag}{spaces}:
        If found, it will search the remainder of the line for settings
        in the following form:
        {spaces}{settings_dict_key}{spaces}{=}{spaces}{value},{more settings}
        It will then parse the setting according to the type in the settings_dict
        and return all the key value pairs.
        Note that the search is not case sensitive.  You can escape commas with a
        backslash, and you can quote key value pairs.

    :param line: the line to search
    :param tag: The tag to locate before beginning settings search
    :param settings_dict: A dict of settings keys in the following form:
        { "KEY": {"TYPE": "TYPE_NAME"}, "KEY"... }
        The available TYPE_NAMES are boolean, string, float, percent.
    :return: A dict of parsed key value pairs or false if no settings were found.
    """
    # quick check to see if this is necessary
    if line.upper().find(tag) == -1:
        return
    search_strings = [
        tag, ":"
    ]
    index = search_string(line, search_strings)
    if index < 0 or len(line) <= index+1:
        return False
    # We have found the tag.  Extract the parameters
    parameters_string = line[index:].strip()
    import csv
    if sys.version_info[0] < 3:
        from StringIO import StringIO
    else:
        from io import StringIO
    try:
        separated_parameters = csv.reader(
            StringIO(parameters_string),
            delimiter=str(','),
            quotechar=str('"'),
            escapechar =str('\\'),
            doublequote=True,
            skipinitialspace=True,
            quoting=csv.QUOTE_MINIMAL
        )
    except (csv.Error, TypeError) as e:
        logger.exception("Failed to parse arc welder gcode tag")
        return False
    results = {}
    for row in separated_parameters:
        for parameter in row:
            # find the =
            equal_index = parameter.find("=")
            if equal_index < 1 or len(parameter) < equal_index + 1:
                logger.error("Skipping invalid ArcWelder GCode Parameter: %s", parameter)
                continue
            key = parameter[0:equal_index].strip().upper()
            value = parameter[equal_index + 1:].strip()

            param_def = settings_dict.get(key, None)
            if param_def is None:
                logger.error("Skipping unknown ArcWelder GCode Parameter: key=%s, value=%s", key, value)
                continue
            param_type = param_def["type"]
            # param types: float, percent, boolean, string
            try:
                result_value = None
                if param_type == "float":
                    result_value = float(value)
                elif param_type == "percent":
                    result_value = float(value.replace("%",""))
                elif param_type == "boolean":
                    result_value = value.upper() in ["1", "TRUE", "YES", "Y"]
                elif param_type == "string":
                    # strip leading quotes if there are any
                    if len(value) > 2 and value[0] == '"' and value[len(value)-1] == '"':
                        result_value = value[1:len(value) - 1]
                    else:
                        result_value = value.strip()
                if result_value is not None:
                    results[key] = result_value
                else:
                    logger.error("Failed to parse %s parameter value of %s", key, value)
            except ValueError as e:
                logger.exception("Failed to parse %s parameter value of %s", key, value)
    if len(results) == 0:
        return False
    return results


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


def is_version_in_versions(current_version_string, version_checks, compare_type="semantic"):
    if not current_version_string:
        return False
    if compare_type == "date":
        current_value = parse_datetime(current_version_string)
    elif compare_type == "semantic":
        # Note that the version string must already be cleaned, if that is necessary
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
            compare_value = parse_datetime(compare_string)
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


UTC_DATE_TIME_FORMAT = "%m-%d-%Y %H:%M:%S"
LOCAL_DATE_TIME_FORMAT = "%x %X"

def utc_to_local(utc_datetime):
    now_timestamp = time.time()
    offset = datetime.fromtimestamp(now_timestamp) - datetime.utcfromtimestamp(now_timestamp)
    return utc_datetime + offset


def get_utc_time_string(utc_date_time):
    return utc_date_time.strftime(UTC_DATE_TIME_FORMAT)


def to_local_date_time_string(utc_date_string):
    utc_datetime = datetime.strptime(utc_date_string, UTC_DATE_TIME_FORMAT)
    return utc_to_local(utc_datetime).strftime(LOCAL_DATE_TIME_FORMAT)