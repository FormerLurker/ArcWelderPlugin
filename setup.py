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
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
#from distutils.command.build_ext import build_ext
from distutils.ccompiler import CCompiler
from distutils.unixccompiler import UnixCCompiler
from distutils.msvccompiler import MSVCCompiler
from distutils.bcppcompiler import BCPPCompiler
from distutils.cygwinccompiler import CygwinCCompiler
from distutils.version import LooseVersion
from distutils.sysconfig import customize_compiler
from octoprint_arc_welder_setuptools import NumberedVersion
import sys
import platform
import versioneer

########################################################################################################################
# The plugin's identifier, has to be unique
plugin_identifier = "arc_welder"
# The plugin's python package, should be "octoprint_<plugin identifier>", has to be unique
plugin_package = "octoprint_arc_welder"
# The plugin's human readable name. Can be overwritten within OctoPrint's internal data via __plugin_name__ in the
# plugin module
plugin_name = "Arc Welder"
# The plugin's fallback version, in case versioneer can't extract the version from _version.py.
# This can happen if the user installs from one of the .zip links in github, not generated with git archive
fallback_version = "1.0.1rc1.dev2"
plugin_version = versioneer.get_version()
if plugin_version == "0+unknown" or NumberedVersion(plugin_version) < NumberedVersion(
    fallback_version
):
    plugin_version = fallback_version
    try:
        # This generates version in the following form:
        #   0.1.0rc1+?.GUID_GOES_HERE
        plugin_version += "+u." + versioneer.get_versions()["full-revisionid"][0:7]
    except:
        pass
    print("Unknown Version, falling back to " + plugin_version + ".")

plugin_cmdclass = versioneer.get_cmdclass()
# The plugin's description. Can be overwritten within OctoPrint's internal data via __plugin_description__ in the plugin
# module
plugin_description = """Converts line segments to curves, which reduces the number of gcodes per second, hopefully eliminating stuttering."""
# The plugin's author. Can be overwritten within OctoPrint's internal data via __plugin_author__ in the plugin module
plugin_author = "Brad Hochgesang"
# The plugin's author's mail address.
plugin_author_email = "FormerLurker@pm.me"

# The plugin's homepage URL. Can be overwritten within OctoPrint's internal data via __plugin_url__ in the plugin module
plugin_url = "https://github.com/FormerLurker/ArcWelderPlugin"

# The plugin's license. Can be overwritten within OctoPrint's internal data via __plugin_license__ in the plugin module
plugin_license = "AGPLv3"

# Any additional requirements besides OctoPrint should be listed here
plugin_requires = ["six", "OctoPrint>1.3.8", "setuptools>=6.0"]

import octoprint.server

if LooseVersion(octoprint.server.VERSION) < LooseVersion("1.4"):
    plugin_requires.extend(["flask_principal>=0.4,<1.0"])

# enable faulthandler for python 3.
if (3, 0) < sys.version_info < (3, 3):
    print("Adding faulthandler requirement.")
    plugin_requires.append("faulthandler>=3.1")

# --------------------------------------------------------------------------------------------------------------------
# More advanced options that you usually shouldn't have to touch follow after this point
# --------------------------------------d------------------------------------------------------------------------------

# Additional package data to install for this plugin. The subfolders "templates", "static" and "translations" will
# already be installed automatically if they exist. Note that if you add something here you'll also need to update
# MANIFEST.in to match to ensure that python setup.py sdist produces a source distribution that contains all your
# files. This is sadly due to how python's setup.py works, see also http://stackoverflow.com/a/14159430/2028598
plugin_additional_data = [
    "data/*.json",
    "data/firmware/*.json",
    "data/lib/c/*.cpp",
    "data/lib/c/*.h"
]
# Any additional python packages you need to install with your plugin that are not contained in <plugin_package>.*
plugin_additional_packages = ["octoprint_arc_welder_setuptools"]

# Any python packages within <plugin_package>.* you do NOT want to install with your plugin
plugin_ignored_packages = []

# C++ Extension compiler options
# Set debug mode
DEBUG = False
# define compiler flags
compiler_opts = {
    CCompiler.compiler_type: {
        "extra_compile_args": ["-O3", "-std=c++11"],
        "extra_link_args": [],
        "define_macros": [],
    },
    MSVCCompiler.compiler_type: {
        "extra_compile_args": ["/O2", "/GL", "/Gy", "/MD", "/EHsc"],
        "extra_link_args": [],
        "define_macros": [],
    },
    UnixCCompiler.compiler_type: {
        "extra_compile_args": ["-O3", "-std=c++11", "-Wno-unknown-pragmas", '-v'],
        "extra_link_args": [],
        "define_macros": [],
    },
    BCPPCompiler.compiler_type: {
        "extra_compile_args": ["-O3", "-std=c++11"],
        "extra_link_args": [],
        "define_macros": [],
    },
    CygwinCCompiler.compiler_type: {
        "extra_compile_args": ["-O3", "-std=c++11"],
        "extra_link_args": [],
        "define_macros": [],
    },
}

if DEBUG:
    compiler_opts = {
        CCompiler.compiler_type: {
            "extra_compile_args": [],
            "extra_link_args": [],
            "define_macros": [("DEBUG_chardet", "1")],
        },
        MSVCCompiler.compiler_type: {
            "extra_compile_args": ["/EHsc", "/Z7"],
            "extra_link_args": ["/DEBUG"],
            "define_macros": [],
        },
        UnixCCompiler.compiler_type: {
            "extra_compile_args": ["-g"],
            "extra_link_args": ["-g"],
            "define_macros": [],
        },
        BCPPCompiler.compiler_type: {
            "extra_compile_args": [],
            "extra_link_args": [],
            "define_macros": [],
        },
        CygwinCCompiler.compiler_type: {
            "extra_compile_args": [],
            "extra_link_args": [],
            "define_macros": [],
        },
    }

# OS Specific Flags
os_compiler_opts = {
    'Darwin': {
        "extra_compile_args": ["-mmacosx-version-min=10.8", "-stdlib=libc++"],
        "extra_link_args": ["-lc++"],
        "define_macros": [],
    }
}

class build_ext_subclass(build_ext):
    def build_extensions(self):
        print("Compiling PyGcodeArcConverter Extension with {0}.".format(self.compiler))
        # get rid of -Wstrict-prototypes option, it just generates pointless warnings
        customize_compiler(self.compiler)
        try:
            self.compiler.compiler_so.remove("-Wstrict-prototypes")
        except (AttributeError, ValueError):
            print(
                "Unable to remove -Wstrict-prototypes or to add -Wno-unknown-pragmas."
            )
            pass
        c = self.compiler
        opts = [v for k, v in compiler_opts.items() if c.compiler_type == k]

        # Add OS Specific Flags
        if platform.system() in os_compiler_opts:
            os_flags = os_compiler_opts[platform.system()]
            for o in opts:
                o["extra_compile_args"].extend(os_flags["extra_compile_args"])
                o["extra_link_args"].extend(os_flags["extra_compile_args"])
                o["define_macros"].extend(os_flags["define_macros"])

        for e in self.extensions:
            for o in opts:
                for attrib, value in o.items():
                    getattr(e, attrib).extend(value)

        for extension in self.extensions:
            print(
                "Building Extensions for {0} - extra_compile_args:{1} - extra_link_args:{2}".format(
                    extension.name,
                    extension.extra_compile_args,
                    extension.extra_link_args,
                )
            )

        build_ext.build_extensions(self)


## Build our c++ parser extension
plugin_ext_sources = [
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/array_list.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/circular_buffer.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/extruder.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/gcode_comment_processor.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/gcode_parser.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/gcode_position.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/parsed_command.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/parsed_command_parameter.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/position.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/utilities.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/logger.cpp",
    "octoprint_arc_welder/data/lib/c/gcode_processor_lib/fpconv.cpp",
    "octoprint_arc_welder/data/lib/c/arc_welder/arc_welder.cpp",
    "octoprint_arc_welder/data/lib/c/arc_welder/segmented_arc.cpp",
    "octoprint_arc_welder/data/lib/c/arc_welder/segmented_shape.cpp",
    "octoprint_arc_welder/data/lib/c/py_arc_welder/py_logger.cpp",
    "octoprint_arc_welder/data/lib/c/py_arc_welder/py_arc_welder.cpp",
    "octoprint_arc_welder/data/lib/c/py_arc_welder/py_arc_welder_extension.cpp",
    "octoprint_arc_welder/data/lib/c/py_arc_welder/python_helpers.cpp",
]
cpp_gcode_parser = Extension(
    "PyArcWelder",
    sources=plugin_ext_sources,
    language="c++",
    include_dirs=[
        "octoprint_arc_welder/data/lib/c/arc_welder",
        "octoprint_arc_welder/data/lib/c/gcode_processor_lib",
        "octoprint_arc_welder/data/lib/c/py_arc_welder",
    ],
)


additional_setup_parameters = {
    "ext_modules": [cpp_gcode_parser],
    "cmdclass": {"build_ext": build_ext_subclass},
}

########################################################################################################################


try:
    import octoprint_setuptools
except:
    print(
        "Could not import OctoPrint's setuptools, are you sure you are running that under "
        "the same python installation that OctoPrint is installed under?"
    )
    import sys

    sys.exit(-1)

setup_parameters = octoprint_setuptools.create_plugin_setup_parameters(
    identifier=plugin_identifier,
    package=plugin_package,
    name=plugin_name,
    version=plugin_version,
    description=plugin_description,
    author=plugin_author,
    mail=plugin_author_email,
    url=plugin_url,
    license=plugin_license,
    requires=plugin_requires,
    additional_packages=plugin_additional_packages,
    ignored_packages=plugin_ignored_packages,
    additional_data=plugin_additional_data,
    cmdclass=plugin_cmdclass,
)

if len(additional_setup_parameters):
    from octoprint.util import dict_merge

    setup_parameters = dict_merge(setup_parameters, additional_setup_parameters)

setup(**setup_parameters)
