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


bool py_arc_welder::on_progress_(double percent_complete, double seconds_elapsed, double estimated_seconds_remaining, int gcodes_processed, int current_line, int points_compressed, int arcs_created)
{
	PyObject* funcArgs = Py_BuildValue("(d,d,d,i,i,i,i)", percent_complete, seconds_elapsed, estimated_seconds_remaining, gcodes_processed, current_line, points_compressed, arcs_created);
	if (funcArgs == NULL)
	{
		return false;
	}
	
	PyGILState_STATE gstate = PyGILState_Ensure();
	PyObject* pContinueProcessing = PyObject_CallObject(py_progress_callback_, funcArgs);
	Py_DECREF(funcArgs);
	bool continue_processing = PyLong_AsLong(pContinueProcessing) > 0;
	Py_DECREF(pContinueProcessing);
	PyGILState_Release(gstate);

	if (pContinueProcessing == NULL || pContinueProcessing == Py_None)
	{
		// no return value was supply, assume true
		
		return true;
	}
	return continue_processing;
}
