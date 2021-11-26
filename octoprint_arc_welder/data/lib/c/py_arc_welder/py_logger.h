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

#include <string>
#include <vector>
#include <map>
#include "logger.h"
#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG
#else
#include <Python.h>
#endif
#include <string>
#include "python_helpers.h"
#include <iostream>
enum py_loggers { GCODE_CONVERSION };
	
class py_logger : public logger {
public:
	py_logger(std::vector<std::string> names, std::vector<int> levels);
	virtual ~py_logger() {
	}
	void initialize_loggers();
	void set_internal_log_levels(bool check_real_time);
	virtual void log(const int logger_type, const int log_level, const std::string& message);
	virtual void log(const int logger_type, const int log_level, const std::string& message, bool is_exception);
	virtual void log_exception(const int logger_type, const std::string& message);
private:
	bool check_log_levels_real_time;
	PyObject* py_logging_module;
	PyObject* py_logging_configurator_name;
	PyObject* py_logging_configurator;
	PyObject* py_arc_welder_gcode_conversion_logger;
	long gcode_conversion_log_level;
	PyObject* py_info_function_name;
	PyObject* py_warn_function_name;
	PyObject* py_error_function_name;
	PyObject* py_debug_function_name;
	PyObject* py_verbose_function_name;
	PyObject* py_critical_function_name;
	PyObject* py_get_effective_level_function_name;
};

