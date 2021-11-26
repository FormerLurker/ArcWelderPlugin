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
#include "python_helpers.h"
namespace gcode_arc_converter {

	int PyUnicode_SafeCheck(PyObject* py)
	{
#if PY_MAJOR_VERSION >= 3
		return PyUnicode_Check(py);
#else
		return PyUnicode_Check(py);
#endif
	}

	const char* PyUnicode_SafeAsString(PyObject* py)
	{
#if PY_MAJOR_VERSION >= 3
		return PyUnicode_AsUTF8(py);
#else
		return (char*)PyString_AsString(py);
#endif
	}

	PyObject* PyString_SafeFromString(const char* str)
	{
#if PY_MAJOR_VERSION >= 3
		return PyUnicode_FromString(str);
#else
		return PyString_FromString(str);
#endif
	}

	PyObject* PyUnicode_SafeFromString(std::string str)
	{
#if PY_MAJOR_VERSION >= 3
		return PyUnicode_FromString(str.c_str());
#else
		// TODO:  try PyUnicode_DecodeUnicodeEscape maybe?
		//return PyUnicode_DecodeUTF8(str.c_str(), NULL, "replace");
		PyObject* pyString = PyString_FromString(str.c_str());
		if (pyString == NULL)
		{
			PyErr_Print();
			std::string message = "Unable to convert the c_str to a python string: ";
			message += str;
			PyErr_SetString(PyExc_ValueError, message.c_str());
			return NULL;
		}
		PyObject* pyUnicode = PyUnicode_FromEncodedObject(pyString, NULL, "replace");
		Py_DECREF(pyString);
		return pyUnicode;
#endif
	}

	PyObject* PyBytesOrString_FromString(std::string str)
	{
#if PY_MAJOR_VERSION >= 3
		return PyBytes_FromString(str.c_str());
#else
		return PyString_FromString(str.c_str());
#endif
	}

	double PyFloatOrInt_AsDouble(PyObject* py_double_or_int)
	{
		if (PyFloat_CheckExact(py_double_or_int))
			return PyFloat_AsDouble(py_double_or_int);
#if PY_MAJOR_VERSION < 3
		else if (PyInt_CheckExact(py_double_or_int))
			return static_cast<double>(PyInt_AsLong(py_double_or_int));
#endif
		else if (PyLong_CheckExact(py_double_or_int))
			return static_cast<double>(PyLong_AsLong(py_double_or_int));
		return 0;
	}

	long PyIntOrLong_AsLong(PyObject* value)
	{
		long ret_val;
#if PY_MAJOR_VERSION < 3
		if (PyInt_Check(value))
		{
			ret_val = PyInt_AsLong(value);
		}
		else
		{
			ret_val = PyLong_AsLong(value);
		}
#else
		ret_val = PyLong_AsLong(value);
#endif
		return ret_val;
	}

	bool PyFloatLongOrInt_Check(PyObject* py_object)
	{
		return (
			PyFloat_Check(py_object) || PyLong_Check(py_object)
#if PY_MAJOR_VERSION < 3
			|| PyInt_Check(py_object)
#endif
			);

	}
}