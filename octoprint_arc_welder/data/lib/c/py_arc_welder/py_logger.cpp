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
#include "py_logger.h"

py_logger::py_logger(std::vector<std::string> names, std::vector<int> levels) : logger(names, levels)
{
	loggers_created_ = false;
	check_log_levels_real_time = true;
	py_logging_module = NULL;
	py_logging_configurator_name = NULL;
	py_logging_configurator = NULL;
	py_arc_welder_gcode_conversion_logger = NULL;
	gcode_conversion_log_level = 0;
	py_info_function_name = NULL;
	py_warn_function_name = NULL;
	py_error_function_name = NULL;
	py_debug_function_name = NULL;
	py_verbose_function_name = NULL;
	py_critical_function_name = NULL;
	py_get_effective_level_function_name = NULL;
}
void py_logger::initialize_loggers()
{
	// Create all of the objects necessary for logging
	// Import the arc_welder.log module
	py_logging_module = PyImport_ImportModuleNoBlock("octoprint_arc_welder.log");
	if (py_logging_module == NULL)
	{
		PyErr_SetString(PyExc_ImportError, "Could not import module 'arc_welder.log'.");
		return;
	}

	// Get the logging configurator attribute string
	py_logging_configurator_name = PyObject_GetAttrString(py_logging_module, "LoggingConfigurator");
	if (py_logging_configurator_name == NULL)
	{
		PyErr_SetString(PyExc_ImportError, "Could not acquire the LoggingConfigurator attribute string.");
		return;
	}

	// Create a logging configurator
	PyGILState_STATE gstate = PyGILState_Ensure();
	PyObject* funcArgs = Py_BuildValue("(s,s,s)", "arc_welder", "arc_welder.", "octoprint_arc_welder.");
	if (funcArgs == NULL)
	{
		std::cout << "Unable to create LoggingConfigurator arguments, exiting.\r\n";
		PyErr_SetString(PyExc_ImportError, "Could not create LoggingConfigurator arguments.");
		return;
	}
	
	py_logging_configurator = PyObject_CallObject(py_logging_configurator_name, funcArgs);
	std::cout << "Complete.\r\n";
	Py_DECREF(funcArgs);
	PyGILState_Release(gstate);
	if (py_logging_configurator == NULL)
	{
		std::cout << "The LoggingConfigurator is null, exiting.\r\n";
		PyErr_SetString(PyExc_ImportError, "Could not create a new instance of LoggingConfigurator.");
		return;
	}

	// Create the gcode_parser logging object
	py_arc_welder_gcode_conversion_logger = PyObject_CallMethod(py_logging_configurator, (char*)"get_logger", (char*)"s", "octoprint_arc_welder.gcode_conversion");
	if (py_arc_welder_gcode_conversion_logger == NULL)
	{
		std::cout << "No child logger was created, exiting.\r\n";
		PyErr_SetString(PyExc_ImportError, "Could not create the arc_welder.gcode_parser child logger.");
		return;
	}

	// create the function name py objects
	py_info_function_name = gcode_arc_converter::PyString_SafeFromString("info");
	py_warn_function_name = gcode_arc_converter::PyString_SafeFromString("warn");
	py_error_function_name = gcode_arc_converter::PyString_SafeFromString("error");
	py_debug_function_name = gcode_arc_converter::PyString_SafeFromString("debug");
	py_verbose_function_name = gcode_arc_converter::PyString_SafeFromString("verbose");
	py_critical_function_name = gcode_arc_converter::PyString_SafeFromString("critical");
	py_get_effective_level_function_name = gcode_arc_converter::PyString_SafeFromString("getEffectiveLevel");
	loggers_created_ = true;
}

void py_logger::set_internal_log_levels(bool check_real_time)
{
	check_log_levels_real_time = check_real_time;
	if (!check_log_levels_real_time)
	{

		PyObject* py_gcode_conversion_log_level = PyObject_CallMethodObjArgs(py_arc_welder_gcode_conversion_logger, py_get_effective_level_function_name, NULL);
		if (py_gcode_conversion_log_level == NULL)
		{
			PyErr_Print();
			PyErr_SetString(PyExc_ValueError, "Logging.arc_welder - Could not retrieve the log level for the gcode parser logger.");
		}
		gcode_conversion_log_level = gcode_arc_converter::PyIntOrLong_AsLong(py_gcode_conversion_log_level);

		Py_XDECREF(py_gcode_conversion_log_level);
	}
}
	
void py_logger::log_exception(const int logger_type, const std::string& message)
{
	log(logger_type, log_levels::ERROR, message, true);
}

void py_logger::log(const int logger_type, const int log_level, const std::string& message)
{
	log(logger_type, log_level, message, false);
}

void py_logger::log(const int logger_type, const int log_level, const std::string& message, bool is_exception)
{
	if (!loggers_created_)
		return;

	// Get the appropriate logger
	PyObject* py_logger;
	long current_log_level = 0;
	switch (logger_type)
	{
	case GCODE_CONVERSION:
		py_logger = py_arc_welder_gcode_conversion_logger;
		current_log_level = gcode_conversion_log_level;
		break;
	default:
		std::cout << "Logging.arc_welder_log - unknown logger_type.\r\n";
		PyErr_SetString(PyExc_ValueError, "Logging.arc_welder_log - unknown logger_type.");
		return;
	}

	if (!check_log_levels_real_time)
	{
		//std::cout << "Current Log Level: " << current_log_level << " requested:" << log_level;
		// For speed we are going to check the log levels here before attempting to send any logging info to Python.
		if (current_log_level > log_level)
		{
			return;
		}
	}

	PyObject* pyFunctionName = NULL;

	PyObject* error_type = NULL;
	PyObject* error_value = NULL;
	PyObject* error_traceback = NULL;
	bool error_occurred = false;
	if (is_exception)
	{
		// if an error has occurred, use the exception function to log the entire error
		pyFunctionName = py_error_function_name;
		if (PyErr_Occurred())
		{
			error_occurred = true;
			PyErr_Fetch(&error_type, &error_value, &error_traceback);
			PyErr_NormalizeException(&error_type, &error_value, &error_traceback);
		}
	}
	else
	{
		switch ((log_levels)log_level)
		{
		case log_levels::INFO:
			pyFunctionName = py_info_function_name;
			break;
		case log_levels::WARNING:
			pyFunctionName = py_warn_function_name;
			break;
		case log_levels::ERROR:
			pyFunctionName = py_error_function_name;
			break;
		case log_levels::DEBUG:
			pyFunctionName = py_debug_function_name;
			break;
		case log_levels::VERBOSE:
			pyFunctionName = py_verbose_function_name;
			break;
		case log_levels::CRITICAL:
			pyFunctionName = py_critical_function_name;
			break;
		default:
			std::cout << "An unknown log level of '" << log_level << " 'was supplied for the message: " << message.c_str() << "\r\n";
			PyErr_Format(PyExc_ValueError,
				"An unknown log level was supplied for the message %s.", message.c_str());
			return;
		}
	}
	//PyObject* pyMessage = gcode_arc_converter::PyBytesOrString_FromString(message);
	PyObject* pyMessage = gcode_arc_converter::PyUnicode_SafeFromString(message);
	
	if (pyMessage == NULL)
	{
		std::cout << "Unable to convert the log message '" << message.c_str() << "' to a PyString/Unicode message.\r\n";
		PyErr_Format(PyExc_ValueError,
			"Unable to convert the log message '%s' to a PyString/Unicode message.", message.c_str());
		return;
	}
	PyGILState_STATE state = PyGILState_Ensure();
	PyObject* ret_val = PyObject_CallMethodObjArgs(py_logger, pyFunctionName, pyMessage, NULL);
	// We need to decref our message so that the GC can remove it.  Maybe?
	Py_DECREF(pyMessage);
	PyGILState_Release(state);
	if (ret_val == NULL)
	{
		if (!PyErr_Occurred())
		{
			std::cout << "Logging.arc_welder_log - null was returned from the specified logger.\r\n";
			PyErr_SetString(PyExc_ValueError, "Logging.arc_welder_log - null was returned from the specified logger.");
		}
		else
		{
			std::cout << "Logging.arc_welder_log - null was returned from the specified logger and an error was detected.\r\n";
			std::cout << "\tLog Level: " << log_level <<", Logger Type: " << logger_type << ", Message: " << message.c_str() << "\r\n";
			
			// I'm not sure what else to do here since I can't log the error.  I will print it 
			// so that it shows up in the console, but I can't log it, and there is no way to 
			// return an error.
			PyErr_Print();
			PyErr_Clear();
		}
	}
	else
	{
		// Set the exception if we are doing exception logging.
		if (is_exception)
		{
			if (error_occurred)
				PyErr_Restore(error_type, error_value, error_traceback);
			else
				PyErr_SetString(PyExc_Exception, message.c_str());
		}
	}
	Py_XDECREF(ret_val);
}
