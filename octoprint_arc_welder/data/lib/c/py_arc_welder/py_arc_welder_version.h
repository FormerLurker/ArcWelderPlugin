#pragma once
#include "utilities.h"
#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG
#else
#include <Python.h>
#endif

struct py_arc_welder_version : public utilities::gcode_processor_version
{
	py_arc_welder_version(std::string program_name, std::string sub_title = "", std::string description = "") : gcode_processor_version(program_name, sub_title, description) {};
	PyObject* build_py_arc_welder_version();
};

