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
class py_arc_welder : public arc_welder
{
public:
	py_arc_welder(
		std::string guid,
		std::string source_path, 
		std::string target_path, 
		py_logger* logger, 
		double resolution_mm, 
		double path_tolerance_percent,
		double max_radius,
		int min_arc_segments,
		double mm_per_arc_segment,
		bool g90_g91_influences_extruder,
		bool allow_3d_arcs,
		bool allow_dynamic_precision,
		unsigned char default_xyz_precision,
		unsigned char default_e_precision,
		int buffer_size, 
		PyObject* py_progress_callback
		): arc_welder(
			source_path, 
			target_path, 
			logger, 
			resolution_mm, 
			path_tolerance_percent,
			max_radius,
			min_arc_segments,
			mm_per_arc_segment,
			g90_g91_influences_extruder,
			allow_3d_arcs,
			allow_dynamic_precision,
			default_xyz_precision,
			default_e_precision,
			buffer_size
  ){
		guid_ = guid;
		py_progress_callback_ = py_progress_callback;
	}
	virtual ~py_arc_welder() {
		
	}
	static PyObject* build_py_progress(const arc_welder_progress& progress, std::string guid);
protected:
	std::string guid_;
	virtual bool on_progress_(const arc_welder_progress& progress);
private:
	PyObject* py_progress_callback_;
};

