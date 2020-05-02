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
import six
import os
import ntpath


def remove_extension_from_filename(filename):
    return os.path.splitext(filename)[0]


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