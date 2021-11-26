////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arc Welder: Anti-Stutter Python Extension for the OctoPrint Arc Welder plugin.
//
// Compresses many G0/G1 commands into G2/G3(arc) commands where possible, ensuring the tool paths stay within the specified resolution.
// This reduces file size and the number of gcodes per second.
//
// Copyright(C) 2021 - Brad Hochgesang
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
#include <arc_welder.h>
#include <string>
#include "py_logger.h"
#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG
#else
#include <Python.h>
#endif

struct py_gcode_arc_args : arc_welder_args {
	py_gcode_arc_args() : arc_welder_args() {
		log_level = INFO;
		py_progress_callback = NULL;
		guid = "";

	};
	py_gcode_arc_args(std::string source_path, std::string target_path, logger* log, std::string progress_guid, PyObject* progress_callback) : arc_welder_args(source_path, target_path, log) {
		guid = progress_guid;
		py_progress_callback = progress_callback;
	};

	static bool parse_args(PyObject* py_args, py_logger* p_py_logger, py_gcode_arc_args& args, PyObject** py_progress_callback);
	int log_level;
	std::string guid;
	PyObject* py_progress_callback;
};

class py_arc_welder : public arc_welder
{
public:
	py_arc_welder(py_gcode_arc_args args): arc_welder( (arc_welder_args)args)
  {
		guid_ = args.guid;
		py_progress_callback_ = args.py_progress_callback;
	}
	virtual ~py_arc_welder() {
		
	}
	static PyObject* build_py_progress(const arc_welder_progress& progress, std::string guid, bool include_detailed_statistics);
protected:
	std::string guid_;
	virtual bool on_progress_(const arc_welder_progress& progress);
private:
	PyObject* py_progress_callback_;
};

