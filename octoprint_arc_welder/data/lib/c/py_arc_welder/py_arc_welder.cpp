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
#include "py_arc_welder.h"

PyObject* py_arc_welder::build_py_progress(const arc_welder_progress& progress, std::string guid, bool include_detailed_statistics)
{
  PyObject* pyGuid = gcode_arc_converter::PyUnicode_SafeFromString(guid);
  if (pyGuid == NULL)
    return NULL;

  std::string segment_statistics = "";
  std::string segment_travel_statistics = "";

  if (include_detailed_statistics)
  {
      // Extrusion Statistics
      source_target_segment_statistics combined_stats = source_target_segment_statistics::add(progress.segment_statistics, progress.segment_retraction_statistics);
      segment_statistics = combined_stats.str("", utilities::box_drawing::HTML);
      // Travel Statistics
      segment_travel_statistics = progress.travel_statistics.str("", utilities::box_drawing::HTML);
  }
  PyObject* pyMessage = gcode_arc_converter::PyUnicode_SafeFromString(segment_statistics);
  if (pyMessage == NULL)
    return NULL;
  double total_count_reduction_percent = progress.segment_statistics.get_total_count_reduction_percent();
  
  PyObject* pyTravelMessage = gcode_arc_converter::PyUnicode_SafeFromString(segment_travel_statistics);
  if (pyTravelMessage == NULL)
    return NULL;
  double total_travel_count_reduction_percent = progress.travel_statistics.get_total_count_reduction_percent();
  PyObject* py_progress = Py_BuildValue("{s:d,s:d,s:d,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:f,s:f,s:f,s:f,s:i,s:i,s:f,s:f,s:f,s:i,s:i,s:f}",
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
    "arcs_aborted_by_flowrate",
    progress.arcs_aborted_by_flow_rate,	  						//8
    "num_firmware_compensations",
    progress.num_firmware_compensations,							//9
    "num_gcode_length_exceptions",
    progress.num_gcode_length_exceptions,							//10
    "source_file_position",
    progress.source_file_position,										//11
    "source_file_size",
    progress.source_file_size,												//12
    "target_file_size",
    progress.target_file_size,												//13
    "compression_ratio",
    progress.compression_ratio,												//14
    "compression_percent",
    progress.compression_percent,											//15
    "source_file_total_length",
    progress.segment_statistics.total_length_source,	//16
    "target_file_total_length",
    progress.segment_statistics.total_length_target,	//17
    "source_file_total_count",
    progress.segment_statistics.total_count_source,		//18
    "target_file_total_count",
    progress.segment_statistics.total_count_target,   //19
    "total_count_reduction_percent",
    total_count_reduction_percent,                    //20
    "source_file_total_travel_length",
    progress.travel_statistics.total_length_source,	  //21
    "target_file_total_travel_length",
    progress.travel_statistics.total_length_target,	  //22
    "source_file_total_travel_count",
    progress.travel_statistics.total_count_source,		//23
    "target_file_total_travel_count",
    progress.travel_statistics.total_count_target,    //24
    "total_travel_count_reduction_percent",
    total_travel_count_reduction_percent              //25

  );

  if (py_progress == NULL)
  {
    return NULL;
  }
  // Due to a CRAZY issue, I have to add this item after building the py_progress object,
  // else it crashes in python 2.7.  Looking forward to retiring this backwards 
  // compatible code...
  PyDict_SetItemString(py_progress, "segment_statistics_text", pyMessage);
  PyDict_SetItemString(py_progress, "segment_travel_statistics_text", pyTravelMessage);
  PyDict_SetItemString(py_progress, "guid", pyGuid);
  return py_progress;
}

bool py_arc_welder::on_progress_(const arc_welder_progress& progress)
{
  PyObject* py_dict = py_arc_welder::build_py_progress(progress, guid_, false);
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
  PyGILState_Release(gstate);
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

  return continue_processing;
}

bool py_gcode_arc_args::parse_args(PyObject* py_args, py_logger* p_py_logger, py_gcode_arc_args& args, PyObject** py_progress_callback)
{
  p_py_logger->log(
    GCODE_CONVERSION, INFO,
    "Parsing GCode Conversion Args."
  );

#pragma region Required_Arguments
#pragma region guid
  // Extract the job guid
  PyObject* py_guid = PyDict_GetItemString(py_args, "guid");
  if (py_guid == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve required parameter 'guid' from the args.";
    p_py_logger->log_exception(GCODE_CONVERSION, message);
    return false;
  }
  args.guid = gcode_arc_converter::PyUnicode_SafeAsString(py_guid);
#pragma endregion guid
#pragma region source_path
  // Extract the source file path
  PyObject* py_source_path = PyDict_GetItemString(py_args, "source_path");
  if (py_source_path == NULL)
  {
    std::string message = "ParseArgs -Unable to retrieve required parameter 'source_path' from the args.";
    p_py_logger->log_exception(GCODE_CONVERSION, message);
    return false;
  }
  args.source_path = gcode_arc_converter::PyUnicode_SafeAsString(py_source_path);
#pragma endregion source_path
#pragma region target_path
  // Extract the target file path
  PyObject* py_target_path = PyDict_GetItemString(py_args, "target_path");
  if (py_target_path == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve required parameter 'target_path' from the args.";
    p_py_logger->log_exception(GCODE_CONVERSION, message);
    return false;
  }
  args.target_path = gcode_arc_converter::PyUnicode_SafeAsString(py_target_path);
#pragma endregion target_path
#pragma region on_progress_received
  // on_progress_received
  PyObject* py_on_progress_received = PyDict_GetItemString(py_args, "on_progress_received");
  if (py_on_progress_received == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve required parameter 'on_progress_received' from the args.";
    p_py_logger->log_exception(GCODE_CONVERSION, message);
    return false;
  }
  // need to incref this so it doesn't vanish later (borrowed reference we are saving)
  Py_XINCREF(py_on_progress_received);
  *py_progress_callback = py_on_progress_received;
#pragma endregion on_progress_received
#pragma endregion Required_Arguments

#pragma region Optional_Arguments
#pragma region resolution_mm
  // Extract the resolution in millimeters
  PyObject* py_resolution_mm = PyDict_GetItemString(py_args, "resolution_mm");
  if (py_resolution_mm == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'resolution_mm' parameter from the args.";
    p_py_logger->log(GCODE_CONVERSION, WARNING, message);
  }
  else {
    args.resolution_mm = gcode_arc_converter::PyFloatOrInt_AsDouble(py_resolution_mm);
    if (args.resolution_mm <= 0)
    {
      args.resolution_mm = 0.05; // Set to the default if no resolution is provided, or if it is less than 0.
    }
  }
#pragma endregion resolution_mm
#pragma region allow_dynamic_precision
  // extract allow_dynamic_precision
  PyObject* py_allow_dynamic_precision = PyDict_GetItemString(py_args, "allow_dynamic_precision");
  if (py_allow_dynamic_precision == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve 'allow_dynamic_precision' from the args.";
    p_py_logger->log(GCODE_CONVERSION, WARNING, message);
  }
  else {
    args.allow_dynamic_precision = PyLong_AsLong(py_allow_dynamic_precision) > 0;
  }
#pragma endregion allow_dynamic_precision
#pragma region default_xyz_precision
  // extract default_xyz_precision
  PyObject* py_default_xyz_precision = PyDict_GetItemString(py_args, "default_xyz_precision");
  if (py_default_xyz_precision == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'default_xyz_precision' parameter from the args.";
    p_py_logger->log(GCODE_CONVERSION, WARNING, message);
  }
  else {
    args.default_xyz_precision = (unsigned char)gcode_arc_converter::PyFloatOrInt_AsDouble(py_default_xyz_precision);
    if (args.default_xyz_precision < 3)
    {
      std::string message = "ParseArgs - The default XYZ precision received was less than 3, which could cause problems printing arcs.  Setting to 3.";
      p_py_logger->log(WARNING, GCODE_CONVERSION, message);
      args.default_xyz_precision = 3;
    }
    else if (args.default_xyz_precision > 6)
    {
      std::string message = "ParseArgs - The default XYZ precision received was greater than 6, which could can cause checksum errors depending on your firmware.  Setting to 6.";
      p_py_logger->log(WARNING, GCODE_CONVERSION, message);
      args.default_xyz_precision = 6;
    }
  }
#pragma endregion default_xyz_precision
#pragma region default_e_precision
  // extract default_e_precision
  PyObject* py_default_e_precision = PyDict_GetItemString(py_args, "default_e_precision");
  if (py_default_e_precision == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'default_e_precision parameter' from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else {
    args.default_e_precision = (unsigned char)gcode_arc_converter::PyFloatOrInt_AsDouble(py_default_e_precision);
    if (args.default_e_precision < 3)
    {
      std::string message = "ParseArgs - The default E precision received was less than 3, which could cause extrusion problems.  Setting to 3.";
      p_py_logger->log(WARNING, GCODE_CONVERSION, message);
      args.default_e_precision = 3;
    }
    else if (args.default_e_precision > 6)
    {
      std::string message = "ParseArgs - The default E precision received was greater than 6, which could can cause checksum errors depending on your firmware.  Setting to 6.";
      p_py_logger->log(WARNING, GCODE_CONVERSION, message);
      args.default_e_precision = 6;
    }
  }
#pragma endregion default_e_precision
#pragma region extrusion_rate_variance_percent
  // Extract the extrusion_rate_variance
  PyObject* py_extrusion_rate_variance_percent = PyDict_GetItemString(py_args, "extrusion_rate_variance_percent");
  if (py_extrusion_rate_variance_percent == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'extrusion_rate_variance_percent' parameter from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.extrusion_rate_variance_percent = gcode_arc_converter::PyFloatOrInt_AsDouble(py_extrusion_rate_variance_percent);
    if (args.extrusion_rate_variance_percent < 0)
    {
      args.extrusion_rate_variance_percent = DEFAULT_EXTRUSION_RATE_VARIANCE_PERCENT; // Set to the default if no resolution is provided, or if it is less than 0.
    }
  }
#pragma endregion extrusion_rate_variance_percent
#pragma region path_tolerance_percent
  // Extract the path tolerance_percent
  PyObject* py_path_tolerance_percent = PyDict_GetItemString(py_args, "path_tolerance_percent");
  if (py_path_tolerance_percent == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'path_tolerance_percent' parameter from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.path_tolerance_percent = gcode_arc_converter::PyFloatOrInt_AsDouble(py_path_tolerance_percent);
    if (args.path_tolerance_percent < 0)
    {
      args.path_tolerance_percent = ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT; // Set to the default if no resolution is provided, or if it is less than 0.
    }
  }
#pragma endregion path_tolerance_percent
#pragma region max_radius_mm
  // Extract the max_radius in mm
  PyObject* py_max_radius_mm = PyDict_GetItemString(py_args, "max_radius_mm");
  if (py_max_radius_mm == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'max_radius_mm' parameter from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.max_radius_mm = gcode_arc_converter::PyFloatOrInt_AsDouble(py_max_radius_mm);
    if (args.max_radius_mm > DEFAULT_MAX_RADIUS_MM)
    {
      args.max_radius_mm = DEFAULT_MAX_RADIUS_MM; // Set to the default if no resolution is provided, or if it is less than 0.
    }
  }
#pragma endregion max_radius_mm
#pragma region mm_per_arc_segment
  // Extract the mm_per_arc_segment
  PyObject* py_mm_per_arc_segment = PyDict_GetItemString(py_args, "mm_per_arc_segment");
  if (py_mm_per_arc_segment == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'mm_per_arc_segment' parameter from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.mm_per_arc_segment = gcode_arc_converter::PyFloatOrInt_AsDouble(py_mm_per_arc_segment);
    if (args.mm_per_arc_segment < 0)
    {
      args.mm_per_arc_segment = DEFAULT_MM_PER_ARC_SEGMENT;
    }
  }
#pragma endregion mm_per_arc_segment
#pragma region min_arc_segments
  // Extract min_arc_segments
  PyObject* py_min_arc_segments = PyDict_GetItemString(py_args, "min_arc_segments");
  if (py_min_arc_segments == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'min_arc_segments' parameter from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.min_arc_segments = (int)gcode_arc_converter::PyIntOrLong_AsLong(py_min_arc_segments);
    if (args.min_arc_segments < 0)
    {
      args.min_arc_segments = DEFAULT_MIN_ARC_SEGMENTS; // Set to the default if no resolution is provided, or if it is less than 0.
    }
  }
#pragma endregion min_arc_segments
#pragma region max_gcode_length
  // Extract max_gcode_length
  PyObject* py_max_gcode_length = PyDict_GetItemString(py_args, "max_gcode_length");
  if (py_max_gcode_length == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve the 'max_gcode_length' parameter from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.max_gcode_length = (int)gcode_arc_converter::PyIntOrLong_AsLong(py_max_gcode_length);
    if (args.max_gcode_length < 0)
    {
      args.max_gcode_length = DEFAULT_MAX_GCODE_LENGTH;
    }
  }
#pragma endregion max_gcode_length
#pragma region allow_3d_arcs
  // extract allow_3d_arcs
  PyObject* py_allow_3d_arcs = PyDict_GetItemString(py_args, "allow_3d_arcs");
  if (py_allow_3d_arcs == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve 'allow_3d_arcs' from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.allow_3d_arcs = PyLong_AsLong(py_allow_3d_arcs) > 0;
  }
#pragma endregion allow_3d_arcs
#pragma region allow_travel_arcs
  // extract allow_travel_arcs
  PyObject* py_allow_travel_arcs = PyDict_GetItemString(py_args, "allow_travel_arcs");
  if (py_allow_travel_arcs == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve 'allow_travel_arcs' from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.allow_travel_arcs = PyLong_AsLong(py_allow_travel_arcs) > 0;
  }
#pragma endregion allow_travel_arcs
#pragma region g90_g91_influences_extruder
  // Extract G90/G91 influences extruder
  // g90_influences_extruder
  PyObject* py_g90_g91_influences_extruder = PyDict_GetItemString(py_args, "g90_g91_influences_extruder");
  if (py_g90_g91_influences_extruder == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve 'g90_g91_influences_extruder' from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    args.g90_g91_influences_extruder = PyLong_AsLong(py_g90_g91_influences_extruder) > 0;
  }
#pragma endregion g90_g91_influences_extruder
#pragma region log_level
  // Extract log_level
  PyObject* py_log_level = PyDict_GetItemString(py_args, "log_level");
  if (py_log_level == NULL)
  {
    std::string message = "ParseArgs - Unable to retrieve 'log_level' from the args.";
    p_py_logger->log(WARNING, GCODE_CONVERSION, message);
  }
  else
  {
    int log_level_value = static_cast<int>(PyLong_AsLong(py_log_level));
    // determine the log level as an index rather than as a value
    args.log_level = p_py_logger->get_log_level_for_value(log_level_value);
  }
#pragma endregion log_level
#pragma endregion Optional_Arguments

  return true;
}