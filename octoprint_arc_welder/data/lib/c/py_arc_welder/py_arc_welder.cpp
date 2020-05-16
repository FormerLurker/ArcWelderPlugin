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
#include "py_arc_welder.h"


PyObject* py_arc_welder::build_py_progress(arc_welder_progress progress)
{
	PyObject* py_progress = Py_BuildValue("{s:d,s:d,s:d,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:f,s:f}",
		u8"percent_complete",
		progress.percent_complete,
		u8"seconds_elapsed",
		progress.seconds_elapsed,
		u8"seconds_remaining",
		progress.seconds_remaining,
		u8"gcodes_processed",
		progress.gcodes_processed,
		u8"lines_processed",
		progress.lines_processed,
		u8"points_compressed",
		progress.points_compressed,
		u8"arcs_created",
		progress.arcs_created,
		u8"source_file_position",
		progress.source_file_position,
		u8"source_file_size",
		progress.source_file_size,
		u8"target_file_size",
		progress.target_file_size,
		u8"compression_ratio",
		progress.compression_ratio,
		u8"compression_percent",
		progress.compression_percent
	);
	return py_progress;
}

bool py_arc_welder::on_progress_(const arc_welder_progress& progress)
{
	PyObject* py_dict = py_arc_welder::build_py_progress(progress);
	if (py_dict == NULL)
		return false;
	PyObject* func_args = Py_BuildValue("(O)", py_dict);
	if (func_args == NULL)
	{
		Py_DECREF(py_dict);
		return true;
	}
		
	PyGILState_STATE gstate = PyGILState_Ensure();
	PyObject* pContinueProcessing = PyObject_CallObject(py_progress_callback_, func_args);
	Py_DECREF(func_args);
	Py_DECREF(py_dict);
	bool continue_processing;
	if (pContinueProcessing == NULL)
	{
		// no return value was supply, assume true, but without decrefing pContinueProcessing
		continue_processing = true;
	}
	else 
	{
		if (pContinueProcessing == Py_None)
		{
			continue_processing = true;
		}
		else
		{
			continue_processing = PyLong_AsLong(pContinueProcessing) > 0;
		}
		Py_DECREF(pContinueProcessing);
	}
	PyGILState_Release(gstate);
	return continue_processing;
}
