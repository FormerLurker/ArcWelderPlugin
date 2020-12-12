////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arc Welder: Anti-Stutter Python Extension for the OctoPrint Arc Welder plugin.
//
// Compresses many G0/G1 commands into G2/G3(arc) commands where possible, ensuring the tool paths stay within the specified resolution.
// This reduces file size and the number of gcodes per second.
//
// Copyright(C) 2020 - Brad Hochgesang
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This program is free software : you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU Affero General Public License for more details.
//
//
// You can contact the author at the following email address: 
// FormerLurker@pm.me
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG
#else
#include <Python.h>
#endif
#include <string>
#include "py_logger.h"
#include "arc_welder.h"
extern "C"
{
#if PY_MAJOR_VERSION >= 3
	PyMODINIT_FUNC PyInit_PyArcWelder(void);
#else
	extern "C" void initPyArcWelder(void);
#endif
	static PyObject* ConvertFile(PyObject* self, PyObject* args);
}

struct py_gcode_arc_args {
	py_gcode_arc_args() {
		guid = "";
		source_path = "";
		target_path = "";
		resolution_mm = DEFAULT_RESOLUTION_MM;
		path_tolerance_percent = ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT;
		max_radius_mm = DEFAULT_MAX_RADIUS_MM;
		min_arc_segments = DEFAULT_MIN_ARC_SEGMENTS;
		mm_per_arc_segment = DEFAULT_MM_PER_ARC_SEGMENT;
		g90_g91_influences_extruder = DEFAULT_G90_G91_INFLUENCES_EXTREUDER;
		allow_3d_arcs = DEFAULT_ALLOW_3D_ARCS;
		log_level = 0;
	}
	py_gcode_arc_args(
		std::string guid_,
		std::string source_path_, 
		std::string target_path_, 
		double resolution_mm_, 
		double path_tolerance_percent_,
		double max_radius_mm_,
		int min_arc_segments_,
		double mm_per_arc_segment_,
		bool g90_g91_influences_extruder_, 
		bool allow_3d_arcs_,
		bool allow_dynamic_precision_,
		unsigned char default_xyz_precision_,
		unsigned char default_e_precision_,
		int log_level_
	) {
		guid = guid_;
		source_path = source_path_;
		target_path = target_path_;
		resolution_mm = resolution_mm_;
		path_tolerance_percent = path_tolerance_percent_;
		max_radius_mm = max_radius_mm_;
		min_arc_segments = min_arc_segments_;
		mm_per_arc_segment = mm_per_arc_segment_;
		allow_3d_arcs = allow_3d_arcs_;
		allow_dynamic_precision = allow_dynamic_precision_;
		default_xyz_precision = default_xyz_precision_;
		default_e_precision = default_e_precision_;
		g90_g91_influences_extruder = g90_g91_influences_extruder_;
		log_level = log_level_;
	}
	std::string guid;
	std::string source_path;
	std::string target_path;
	double resolution_mm;
	double path_tolerance_percent;
	bool allow_3d_arcs;
	bool allow_dynamic_precision;
	unsigned char default_xyz_precision;
	unsigned char default_e_precision;
	bool g90_g91_influences_extruder;
	double max_radius_mm;
	int min_arc_segments;
	double mm_per_arc_segment;
	int log_level;
};

static bool ParseArgs(PyObject* py_args, py_gcode_arc_args& args, PyObject** p_py_progress_callback);

// global logger
py_logger* p_py_logger = NULL;
/*
static void AtExit()
{
	if (p_py_logger != NULL) delete p_py_logger;
}*/



	

