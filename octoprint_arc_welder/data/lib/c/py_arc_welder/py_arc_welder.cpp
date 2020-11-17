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

PyObject* py_arc_welder::build_py_progress(const arc_welder_progress& progress, std::string guid)
{
	std::string segment_statistics = progress.segment_statistics.str();
	PyObject* pyGuid = gcode_arc_converter::PyUnicode_SafeFromString(guid);
	if (pyGuid == NULL)
		return NULL;
	PyObject* pyMessage = gcode_arc_converter::PyUnicode_SafeFromString(segment_statistics);
	if (pyMessage == NULL)
		return NULL;
	double total_count_reduction_percent = progress.segment_statistics.get_total_count_reduction_percent();
	PyObject* py_progress = Py_BuildValue("{s:d,s:d,s:d,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:f,s:f,s:f,s:f,s:i,s:i,s:f}",
		"percent_complete",
		progress.percent_complete,												//1
		"seconds_elapsed",
		progress.seconds_elapsed,													//2
		"seconds_remaining",
		progress.seconds_remaining,												//3
		"gcodes_processed",
		progress.gcodes_processed,												//4
		"lines_processed",
		progress.lines_processed,													//5
		"points_compressed",
		progress.points_compressed,												//6
		"arcs_created",
		progress.arcs_created,														//7
		"source_file_position",
		progress.source_file_position,										//8
		"source_file_size",
		progress.source_file_size,												//9
		"target_file_size",
		progress.target_file_size,												//10
		"compression_ratio",
		progress.compression_ratio,												//11
		"compression_percent",
		progress.compression_percent,											//12
		"source_file_total_length",
		progress.segment_statistics.total_length_source,	//13
		"target_file_total_length",
		progress.segment_statistics.total_length_target,	//14
		"source_file_total_count",
		progress.segment_statistics.total_count_source,		//15
		"target_file_total_count",
		progress.segment_statistics.total_count_target,   //16
		"total_count_reduction_percent",
		total_count_reduction_percent                     //17
		
	);

	if (py_progress == NULL)
	{
		return NULL;
	}
	// Due to a CRAZY issue, I have to add this item after building the py_progress object,
	// else it crashes in python 2.7.  Looking forward to retiring this backwards 
	// compatible code...
	PyDict_SetItemString(py_progress, "segment_statistics_text", pyMessage);
	PyDict_SetItemString(py_progress, "guid", pyGuid);
	return py_progress;
}

bool py_arc_welder::on_progress_(const arc_welder_progress& progress)
{
	PyObject* py_dict = py_arc_welder::build_py_progress(progress, guid_);
	if (py_dict == NULL)
	{
		return false;
	}
	PyObject* func_args = Py_BuildValue("(O)", py_dict);
	if (func_args == NULL)
	{
		Py_DECREF(py_dict);
		return false;	// This was returning true, I think it was a typo.  Making a note just in case.
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
