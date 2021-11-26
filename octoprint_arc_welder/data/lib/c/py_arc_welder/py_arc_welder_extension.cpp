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
#include "py_arc_welder_extension.h"
#include "py_arc_welder.h"
#include <iomanip>
#include <sstream>
#include <iostream>
#include "arc_welder.h"
#include "py_logger.h"
#include "python_helpers.h"
#include "version.h"

#if PY_MAJOR_VERSION >= 3
int main(int argc, char* argv[])
{
	wchar_t* program = Py_DecodeLocale(argv[0], NULL);
	if (program == NULL) {
		fprintf(stderr, "Fatal error: cannot decode argv[0]\n");
		exit(1);
	}

	// Add a built-in module, before Py_Initialize
	PyImport_AppendInittab("PyArcWelder", PyInit_PyArcWelder);

	// Pass argv[0] to the Python interpreter
	Py_SetProgramName(program);

	// Initialize the Python interpreter.  Required.
	Py_Initialize();
	// We are not using threads, do not enable.
	//std::cout << "Initializing threads...";
	//if (!PyEval_ThreadsInitialized()) {
	//	PyEval_InitThreads();
	//}
	// Optionally import the module; alternatively, import can be deferred until the embedded script imports it.
	PyImport_ImportModule("PyArcWelder");
	PyMem_RawFree(program);
	return 0;
}

#else

int main(int argc, char* argv[])
{
	Py_SetProgramName(argv[0]);
	Py_Initialize();
	// We are not using threads, do not enable.
	/* std::cout << "Initializing threads...";
	if (!PyEval_ThreadsInitialized()) {
		PyEval_InitThreads();
	}
	*/
	initPyArcWelder();
	return 0;

}
#endif

struct module_state {
	PyObject* error;
};
#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#else
#define GETSTATE(m) (&_state)
static struct module_state _state;
#endif

// Python 2 module method definition
static PyMethodDef PyArcWelderMethods[] = {
	{ "ConvertFile", (PyCFunction)ConvertFile,  METH_VARARGS  ,"Converts segmented curve approximations to actual G2/G3 arcs within the supplied resolution." },
	{ NULL, NULL, 0, NULL }
};

// Python 3 module method definition
#if PY_MAJOR_VERSION >= 3
static int PyArcWelder_traverse(PyObject* m, visitproc visit, void* arg) {
	Py_VISIT(GETSTATE(m)->error);
	return 0;
}

static int PyArcWelder_clear(PyObject* m) {
	Py_CLEAR(GETSTATE(m)->error);
	return 0;
}

static struct PyModuleDef moduledef = {
		PyModuleDef_HEAD_INIT,
		"PyArcWelder",
		NULL,
		sizeof(struct module_state),
		PyArcWelderMethods,
		NULL,
		PyArcWelder_traverse,
		PyArcWelder_clear,
		NULL
};

#define INITERROR return NULL

PyMODINIT_FUNC
PyInit_PyArcWelder(void)

#else
#define INITERROR return

extern "C" void initPyArcWelder(void)
#endif
{
	std::cout << "Initializing PyArcWelder";
	std::cout << "\nVersion: " << GIT_TAGGED_VERSION << ", Branch: " << GIT_BRANCH << ", BuildDate: " << BUILD_DATE;
	std::cout << "\nCopyright(C) " << COPYRIGHT_DATE << " - " << AUTHOR;
#if PY_MAJOR_VERSION >= 3
	std::cout << " Python 3+ Detected...";
	PyObject* module = PyModule_Create(&moduledef);
#else
	std::cout << " Python 2 Detected...";
	PyObject* module = Py_InitModule("PyArcWelder", PyArcWelderMethods);
#endif

	if (module == NULL)
		INITERROR;
	struct module_state* st = GETSTATE(module);

	st->error = PyErr_NewException((char*)"PyArcWelder.Error", NULL, NULL);
	if (st->error == NULL) {
		Py_DECREF(module);
		INITERROR;
	}
	std::vector<std::string> logger_names;
	logger_names.push_back("arc_welder.gcode_conversion");
	std::vector<int> logger_levels;
	logger_levels.push_back(log_levels::NOSET);
	logger_levels.push_back(log_levels::VERBOSE);
	logger_levels.push_back(log_levels::DEBUG);
	logger_levels.push_back(log_levels::INFO);
	logger_levels.push_back(log_levels::WARNING);
	logger_levels.push_back(log_levels::ERROR);
	logger_levels.push_back(log_levels::CRITICAL);
	p_py_logger = new py_logger(logger_names, logger_levels);
	p_py_logger->initialize_loggers();
	p_py_logger->set_log_level(INFO);
	std::cout << " Initialization Complete\r\n";

#if PY_MAJOR_VERSION >= 3
	return module;
#endif
}

extern "C"
{
	static PyObject* ConvertFile(PyObject* self, PyObject* py_args)
	{
		PyObject* py_convert_file_args;
		if (!PyArg_ParseTuple(
			py_args,
			"O",
			&py_convert_file_args
			))
		{
			std::string message = "py_gcode_arc_converter.ConvertFile - Cound not extract the parameters dictionary.";
			p_py_logger->log_exception(GCODE_CONVERSION, message);
			return NULL;
		}

		py_gcode_arc_args args;
		PyObject* py_progress_callback = NULL;
		
		if (!py_gcode_arc_args::parse_args(py_convert_file_args, p_py_logger, args, &py_progress_callback))
		{
			return NULL;
		}
		p_py_logger->set_log_level((log_levels)args.log_level);

		std::string message = "py_gcode_arc_converter.ConvertFile - Beginning Arc Conversion.";
		p_py_logger->log(GCODE_CONVERSION, log_levels::INFO, message);

		args.py_progress_callback = py_progress_callback;
		args.log = p_py_logger;
		// Set the encoding to html for the progress output
		args.box_encoding = utilities::box_drawing::HTML;
		py_arc_welder arc_welder_obj(args);
		arc_welder_results results;
		results = arc_welder_obj.process();
		
		message = "py_gcode_arc_converter.ConvertFile - Arc Conversion Complete.";
		p_py_logger->log(GCODE_CONVERSION, log_levels::INFO, message);
		Py_XDECREF(py_progress_callback);
		// return the arguments
		PyObject* p_progress = py_arc_welder::build_py_progress(results.progress, args.guid, true);
		
		if (p_progress == NULL)
			p_progress = Py_None;

		PyObject* p_results = Py_BuildValue(
			"{s:i,s:i,s:s,s:O}",
			"success",
			(long int)(results.success ? 1 : 0),
			"is_cancelled",
			(long int)(results.cancelled ? 1 : 0),
			"message",
			results.message.c_str(),
			"progress",
			p_progress
		);
		return p_results;
	}
}



