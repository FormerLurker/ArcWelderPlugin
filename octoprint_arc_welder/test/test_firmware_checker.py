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
import unittest
import os
import re
import json
from octoprint_arc_welder.firmware_checker import FirmwareChecker, PrinterRequest


class TestFirmwareChecker(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super(TestFirmwareChecker, self).__init__(*args, **kwargs)
        self.base_folder = os.path.dirname(os.getcwd())
        self.data_directory = os.path.join(os.path.dirname(self.base_folder), 'octoprint_arc_welder', 'test')
        self.firmware_checker = None
        self.response = None

    def setUp(self):
        self.firmware_checker = FirmwareChecker(
            "1.1", None, self.base_folder, self.data_directory, None, load_defaults=True
        )

        # Add regex firmware tests
        regex_firmware_test = {
            "name": "Regex Firmware Test",
            "functions": {
                "is_firmware_type": {"regex": r"^(?:\s*)RegexFirmware(\s*)", "key": "FIRMWARE_NAME"},
                "version": {"regex": r"^(?:\s*)RegexFirmware(?:\s*)([0-9A-Za-z\.]+)", "key": "FIRMWARE_NAME"},
                "build_date": {"regex": r"^(?:\s*)RegexFirmware(?:\s*)(?:[0-9A-Za-z\.]+)(?:\s*)([0-9\-]{4,})", "key": "FIRMWARE_NAME"},
                "arcs_enabled": {"regex": r"(?i)(?:.*)(HASARCS:1)"},
                "arcs_not_enabled": {"regex": r"\s*(0)\s*", "key": "HASARCS"}
            },
            "version_compare_type": "semantic",
            "help_file": "firmware_regex_firmware_test.md",
            "versions": [
                {
                    "guid": "2c926d8d-b72c-419b-bf54-5d273b881cb8",
                    "version": "<=1.0.0",
                    "supported": True,
                    "recommended": True,
                    "g2_g3_supported": True,
                    "notes": "This is a test printer."
                },
                {
                    "guid": "2f6ebc41-055b-41d3-9f8c-f2e1d67f95d0",
                    "version": ">1.0.0",
                    "supported": True,
                    "recommended": True,
                    "g2_g3_supported": True,
                    "is_future": True
                }
            ]
        }
        # compile the regex functions
        for key in regex_firmware_test["functions"]:
            function = regex_firmware_test["functions"][key]
            if "regex" in function:
                function["regex"] = re.compile(function["regex"])
        self.firmware_checker._firmware_types["types"]["RegexFirmware"] = regex_firmware_test

        def _get_m115_response():
            return self.response

        self.firmware_checker._get_m115_response = _get_m115_response

    def tearDown(self):
        del self.firmware_checker

    def test_prusa_firmware_version_response(self):
        # Prusa Firmware <1.0.0
        firmware_guid = 'a555c60b-3b6c-4c60-acf6-ed7eb68edc07'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware 0.9.0-RC3 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # Try a release candidate, it should be <1.0.0
        firmware_guid = 'a555c60b-3b6c-4c60-acf6-ed7eb68edc07'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware 1.0.0-RC3 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Prusa Firmware >=1.0.0,<=3.9.1
        # try 1.0.0
        firmware_guid = 'a555c60b-3b6c-4c60-acf6-ed7eb68edc07'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware 1.0.0 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # try pre-release
        firmware_guid = '1105400b-1e39-4540-a1bb-64cc2a28bbc7'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware 3.9.1-RC3 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # try current
        firmware_guid = '1105400b-1e39-4540-a1bb-64cc2a28bbc7'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware 3.9.1 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Prusa Firmware >3.9.1
        firmware_guid = 'b8cf8ab2-333c-4812-a1af-ca2093ec9f0d'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware 3.9.2 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # Test is_future
        self.assertTrue(firmware_info.get("is_future", None))
        # Test the previous note
        self.assertEqual(firmware_info.get("previous_notes", ""),
                         "The default arc settings can cause flat edges for very small arcs with a radius around 1-5mm.")

    def test_prusa_buddy_firmware_version_response(self):
        # Prusa Buddy Firmware <4.0.3
        firmware_guid = '948344d5-1da6-401b-b28c-201ab9fb27a4'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware-Buddy 4.0.2 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # Try a release candidate
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware-Buddy 4.0.3-RC based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Prusa Buddy Firmware >=4.0.3,<=4.2.1
        # try 4.0.3
        firmware_guid = 'ac0e782e-2264-4433-a822-87aea93322d8'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware-Buddy 4.0.3 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # try pre-release
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware-Buddy 4.2.1.rc1 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        # Try high end
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # try current
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware-Buddy 4.2.1 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Prusa Buddy Firmware >4.2.1
        firmware_guid = 'be64a03b-8878-47c2-959d-4486c1e222b9'
        self.response = [
            "FIRMWARE_NAME:Prusa-Firmware-Buddy 4.2.2 based on Marlin " \
            "FIRMWARE_URL:https://github.com/prusa3d/Prusa-Firmware PROTOCOL_VERSION:1.0 " \
            "MACHINE_TYPE:Prusa i3 MK2.5 EXTRUDER_COUNT:1 UUID:00000000-0000-0000-0000-000000000000 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # Test is_future
        self.assertTrue(firmware_info.get("is_future", None))
        # Test the previous note
        self.assertEqual(
            firmware_info.get("previous_notes", ""),
            "This firmware has not been updated to include arc planning fixes implemented in Marlin 2.0.6."
        )

    def test_marlin_firmware_version_response(self):
        # Test "=bugfix-2.0.x"
        firmware_guid = '81848e9e-c41a-44dc-bddc-bf0e4df8f16b'
        self.response = [
            "FIRMWARE_NAME:Marlin bugfix-2.0.x (GitHub) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:3D Printer EXTRUDER_COUNT:1 UUID:11111111-2222-3333-4444-555555555555 "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Test "<1.0.0"
        firmware_guid = 'd2688900-92a6-411a-8d39-8374a44a478e'
        self.response = [
            "FIRMWARE_NAME:Marlin 0.0.1 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # try pre-release
        self.response = [
            "FIRMWARE_NAME:Marlin 1.0.0rc1 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Test ">=1.0.0,<2.0.0"
        firmware_guid = 'd2688900-92a6-411a-8d39-8374a44a478e'
        # test low end
        self.response = [
            "FIRMWARE_NAME:Marlin 1.0.0 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff"
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # try pre-release
        self.response = [
            "FIRMWARE_NAME:Marlin 2.0.0.rc1 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff"
        ]

        # Test ">=2.0.0,<=2.0.6"
        firmware_guid = '9ddfb585-4429-4c3e-96eb-cb3ecf9f1fec'
        # test low end
        self.response = [
            "FIRMWARE_NAME:Marlin 2.0.0 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # Test pre-release
        self.response = [
            "FIRMWARE_NAME:Marlin 2.0.6rc1 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Test ">=2.0.6,<=2.0.7.2"
        firmware_guid = '9fa65fea-2adc-4e35-94af-36dc555985f2'
        # test low end
        self.response = [
            "FIRMWARE_NAME:Marlin 2.0.6 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # Test high end
        self.response = [
            "FIRMWARE_NAME:Marlin 2.0.7.2 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Test ">2.0.7.2"
        firmware_guid = 'aa065880-71b5-4ebe-a90f-665433807758'
        self.response = [
            "FIRMWARE_NAME:Marlin 2.1.0.0 (Github) SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin "
            "PROTOCOL_VERSION:1.0 MACHINE_TYPE:RepRap EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff "
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Test Future
        self.assertTrue(firmware_info.get("is_future", None))
        # Test the previous note
        self.assertIsNone(firmware_info.get("previous_notes", None))

    def test_klipper_firmware_version_response(self):
        # Test "<0.8.0"
        firmware_guid = 'bae47fdc-d91a-465b-8f7b-c99a468e8153'
        self.response = ["FIRMWARE_NAME:Klipper FIRMWARE_VERSION:v0.7.0"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # try pre-release
        self.response = ["FIRMWARE_NAME:Klipper FIRMWARE_VERSION:v0.8.0.rc1"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Test ">=0.8.0,<0.9.0"
        firmware_guid = 'f691ee9a-fa2a-48ce-a11d-b97ebed177f7'
        # test low end
        self.response = ["FIRMWARE_NAME:Klipper FIRMWARE_VERSION:v0.8.0"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # try pre-release
        self.response = ["FIRMWARE_NAME:Klipper FIRMWARE_VERSION:v0.9.0rc1"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Test "=0.9.0"
        firmware_guid = '7da24715-4f3a-4dbf-8ab5-912221f3f26d'
        # test equals
        self.response = ["FIRMWARE_NAME:Klipper FIRMWARE_VERSION:v0.9.0"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

        # Test ">0.9.0"
        firmware_guid = 'aa065880-71b5-4ebe-a90f-665433807758'
        self.response = ["FIRMWARE_NAME:Klipper FIRMWARE_VERSION:v0.9.1"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        # Test Future
        self.assertTrue(firmware_info.get("is_future", None))
        # Test the previous note
        self.assertIsNone(firmware_info.get("previous_notes", None))

        # Tests From Users Submissions
        # Issue 58
        firmware_guid = 'f691ee9a-fa2a-48ce-a11d-b97ebed177f7'
        # test low end
        self.response = ["FIRMWARE_VERSION:v0.8.0-700-ge4f3f60e FIRMWARE_NAME:Klipper"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)

    def test_smoothieware_firmware_version_response(self):
        # Test "<Nov 30 2018 20:34:40"
        self.response = ["FIRMWARE_NAME:Smoothieware, FIRMWARE_URL:http%3A//smoothieware.org, "
                         "X-SOURCE_CODE_URL:https://github.com/Smoothieware/Smoothieware, "
                         "FIRMWARE_VERSION:edge-9348830, X-FIRMWARE_BUILD_DATE:Nov 02 2020 23:59:59, "
                         "X-SYSTEM_CLOCK:120MHz, X-AXES:5, X-GRBL_MODE:0, X-ARCS:1, X-CNC:0, X-MSD:1"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertIsNone(firmware_info["guid"])
        self.assertTrue(firmware_info["arcs_enabled"])
        self.assertTrue(firmware_info["g2_g3_supported"])
        self.assertEqual(firmware_info["build_date"], "Nov 02 2020 23:59:59")
        self.assertEqual(firmware_info["version"], "edge-9348830")
        self.assertEqual(firmware_info["type"], "Smoothieware")

        # test low end
        self.response = ["FIRMWARE_NAME:Smoothieware, FIRMWARE_URL:http%3A//smoothieware.org, "
                         "X-SOURCE_CODE_URL:https://github.com/Smoothieware/Smoothieware, "
                         "FIRMWARE_VERSION:edge-9348830, X-FIRMWARE_BUILD_DATE:Nov 4 2020 00:00:00, "
                         "X-SYSTEM_CLOCK:120MHz, X-AXES:5, X-GRBL_MODE:0, X-ARCS:1, X-CNC:0, X-MSD:1"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertIsNone(firmware_info["guid"])
        self.assertTrue(firmware_info["arcs_enabled"])
        self.assertTrue(firmware_info["g2_g3_supported"])
        self.assertEqual(firmware_info["build_date"], "Nov 4 2020 00:00:00")
        self.assertEqual(firmware_info["version"], "edge-9348830")
        self.assertEqual(firmware_info["type"], "Smoothieware")

        # test future
        firmware_guid = '3a166cc2-ff62-4011-aa70-ffa96950a105'
        self.response = ["FIRMWARE_NAME:Smoothieware, FIRMWARE_URL:http%3A//smoothieware.org, "
                         "X-SOURCE_CODE_URL:https://github.com/Smoothieware/Smoothieware, "
                         "FIRMWARE_VERSION:edge-9348830, X-FIRMWARE_BUILD_DATE:Nov 04 2020 00:00:01, "
                         "X-SYSTEM_CLOCK:120MHz, X-AXES:5, X-GRBL_MODE:0, X-CNC:0, X-MSD:1"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertIsNone(firmware_info["guid"])
        self.assertFalse(firmware_info["arcs_enabled"])
        self.assertFalse(firmware_info["g2_g3_supported"])
        self.assertEqual(firmware_info["build_date"], "Nov 04 2020 00:00:01")
        self.assertEqual(firmware_info["version"], "edge-9348830")
        self.assertEqual(firmware_info["type"], "Smoothieware")

    def test_regex_firmware_version_response(self):
        # Test RegexFirmware

        # <=1.0
        guid = "2c926d8d-b72c-419b-bf54-5d273b881cb8"
        self.response = ["FIRMWARE_NAME:RegexFirmware 0.0.1rc2 01-01-2002, HASARCS:1"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], guid)
        self.assertTrue(firmware_info["arcs_enabled"])
        self.assertTrue(firmware_info["g2_g3_supported"])
        self.assertEqual(firmware_info["build_date"], "01-01-2002")
        self.assertEqual(firmware_info["version"], "0.0.1rc2")
        self.assertEqual(firmware_info["type"], "RegexFirmware")
        # release candidate < 1.0
        self.response = ["FIRMWARE_NAME:RegexFirmware 1.0.0rc2 2020-02-03, HASARCS:0"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], guid)
        self.assertFalse(firmware_info["arcs_enabled"])
        self.assertTrue(firmware_info["g2_g3_supported"])
        self.assertEqual(firmware_info["build_date"], "2020-02-03")
        self.assertEqual(firmware_info["version"], "1.0.0rc2")
        self.assertEqual(firmware_info["type"], "RegexFirmware")
        # release == 1.0
        self.response = ["FIRMWARE_NAME:RegexFirmware 1.0.0 2020-02-04"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], guid)
        self.assertIsNone(firmware_info["arcs_enabled"])
        self.assertTrue(firmware_info["g2_g3_supported"])
        self.assertEqual(firmware_info["build_date"], "2020-02-04")
        self.assertEqual(firmware_info["version"], "1.0.0")
        self.assertEqual(firmware_info["type"], "RegexFirmware")

        # release > 1.0
        guid = "2f6ebc41-055b-41d3-9f8c-f2e1d67f95d0"
        self.response = ["FIRMWARE_NAME:RegexFirmware 10.0.0"]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], guid)
        self.assertIsNone(firmware_info["arcs_enabled"])
        self.assertTrue(firmware_info["g2_g3_supported"])
        self.assertIsNone(firmware_info["build_date"])
        self.assertEqual(firmware_info["version"], "10.0.0")
        self.assertEqual(firmware_info["type"], "RegexFirmware")


    def test_parse_extended_capabilities(self):
        firmware_guid = '9fa65fea-2adc-4e35-94af-36dc555985f2'
        self.response = [
            "FIRMWARE_NAME:Marlin 2.0.7.2 (Oct 31 2020 14:58:34) "
            "SOURCE_CODE_URL:https://github.com/MarlinFirmware/Marlin PROTOCOL_VERSION:1.0 MACHINE_TYPE:Ender-3 "
            "EXTRUDER_COUNT:1 UUID:cede2a2f-41a2-4748-9b12-c55c62f367ff",
            "Cap:SERIAL_XON_XOFF:0",
            "Cap:BINARY_FILE_TRANSFER:0",
            "Cap:EEPROM:1",
            "Cap:VOLUMETRIC:1",
            "Cap:AUTOREPORT_TEMP:1",
            "Cap:PROGRESS:0",
            "Cap:PRINT_JOB:1",
            "Cap:AUTOLEVEL:1",
            "Cap:RUNOUT:0",
            "Cap:Z_PROBE:1",
            "Cap:LEVELING_DATA:1",
            "Cap:BUILD_PERCENT:1",
            "Cap:SOFTWARE_POWER:0",
            "Cap:TOGGLE_LIGHTS:0",
            "Cap:CASE_LIGHT_BRIGHTNESS:0",
            "Cap:EMERGENCY_PARSER:1",
            "Cap:PROMPT_SUPPORT:0",
            "Cap:SDCARD:1",
            "Cap:AUTOREPORT_SD_STATUS:0",
            "Cap:LONG_FILENAME:1",
            "Cap:THERMAL_PROTECTION:1",
            "Cap:MOTION_MODES:0",
            "Cap:ARCS:1",
            "Cap:BABYSTEPPING:1",
            "Cap:CHAMBER_TEMPERATURE:0"
        ]
        firmware_info = self.firmware_checker._get_firmware_version()
        self.assertIsNotNone(firmware_info)
        self.assertEqual(firmware_info["guid"], firmware_guid)
        parsed_response = firmware_info.get("m115_parsed_response", None)
        self.assertIsNotNone(parsed_response)
        capabilities = parsed_response.get(FirmwareChecker.MARLIN_EXTENDED_CAPABILITIES_KEY, None)
        self.assertIsNotNone(capabilities)
        self.assertEqual(capabilities.get("SERIAL_XON_XOFF", None), "0")
        self.assertEqual(capabilities.get("BINARY_FILE_TRANSFER", None), "0")
        self.assertEqual(capabilities.get("EEPROM", None), "1")
        self.assertEqual(capabilities.get("VOLUMETRIC", None), "1")
        self.assertEqual(capabilities.get("AUTOREPORT_TEMP", None), "1")
        self.assertEqual(capabilities.get("PROGRESS", None), "0")
        self.assertEqual(capabilities.get("PRINT_JOB", None), "1")
        self.assertEqual(capabilities.get("AUTOLEVEL", None), "1")
        self.assertEqual(capabilities.get("RUNOUT", None), "0")
        self.assertEqual(capabilities.get("Z_PROBE", None), "1")
        self.assertEqual(capabilities.get("LEVELING_DATA", None), "1")
        self.assertEqual(capabilities.get("BUILD_PERCENT", None), "1")
        self.assertEqual(capabilities.get("SOFTWARE_POWER", None), "0")
        self.assertEqual(capabilities.get("TOGGLE_LIGHTS", None), "0")
        self.assertEqual(capabilities.get("CASE_LIGHT_BRIGHTNESS", None), "0")
        self.assertEqual(capabilities.get("EMERGENCY_PARSER", None), "1")
        self.assertEqual(capabilities.get("PROMPT_SUPPORT", None), "0")
        self.assertEqual(capabilities.get("SDCARD", None), "1")
        self.assertEqual(capabilities.get("AUTOREPORT_SD_STATUS", None), "0")
        self.assertEqual(capabilities.get("LONG_FILENAME", None), "1")
        self.assertEqual(capabilities.get("THERMAL_PROTECTION", None), "1")
        self.assertEqual(capabilities.get("MOTION_MODES", None), "0")
        self.assertEqual(capabilities.get("ARCS", None), "1")
        self.assertEqual(capabilities.get("BABYSTEPPING", None), "1")
        self.assertEqual(capabilities.get("CHAMBER_TEMPERATURE", None), "0")
        self.assertTrue(firmware_info["g2_g3_supported"])
        self.assertTrue(firmware_info["arcs_enabled"])
