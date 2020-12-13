////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Gcode Processor Library
//
// Tools for parsing gcode and calculating printer state from parsed gcode commands.
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
#include "utilities.h"
#include <cmath>
#include <sstream>
#include <iostream>
#include <iomanip>
#include "fpconv.h"

const std::string utilities::WHITESPACE_ = " \n\r\t\f\v";
const char utilities::GUID_RANGE[] = "0123456789abcdef";
const bool utilities::GUID_DASHES[] = { 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0 };

bool utilities::is_zero(double x, double tolerance)
{
	return std::abs(x) < tolerance;
}

int utilities::round_up_to_int(double x, double tolerance)
{
	return int(x + tolerance);
}

bool utilities::is_equal(double x, double y, double tolerance)
{
	double abs_difference = std::abs(x - y);
	return abs_difference < tolerance;
}

bool utilities::greater_than(double x, double y, double tolerance)
{
	return x > y && !is_equal(x, y, tolerance);
}

bool utilities::greater_than_or_equal(double x, double y, double tolerance)
{
	return x > y || is_equal(x, y, tolerance);
}

bool utilities::less_than(double x, double y, double tolerance)
{
	return x < y && !is_equal(x, y, tolerance);
}

bool utilities::less_than_or_equal(double x, double y, double tolerance)
{
	return x < y || is_equal(x, y, tolerance);
}


double utilities::get_cartesian_distance(double x1, double y1, double x2, double y2)
{
	// Compare the saved points cartesian distance from the current point
	double xdif = x1 - x2;
	double ydif = y1 - y2;
	double dist_squared = xdif * xdif + ydif * ydif;
	return std::sqrt(dist_squared);
}

double utilities::get_cartesian_distance(double x1, double y1, double z1, double x2, double y2, double z2)
{
	// Compare the saved points cartesian distance from the current point
	double xdif = x1 - x2;
	double ydif = y1 - y2;
	double zdif = z1 - z2;
	double dist_squared = xdif * xdif + ydif * ydif + zdif * zdif;
	return std::sqrt(dist_squared);
}

std::string utilities::to_string(double value)
{
	std::ostringstream os;
	os << value;
	return os.str();
}

std::string utilities::to_string(int value)
{
	std::ostringstream os;
	os << value;
	return os.str();
}

std::string utilities::ltrim(const std::string& s)
{
	size_t start = s.find_first_not_of(WHITESPACE_);
	return (start == std::string::npos) ? "" : s.substr(start);
}

std::string utilities::rtrim(const std::string& s)
{
	size_t end = s.find_last_not_of(WHITESPACE_);
	return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string utilities::trim(const std::string& s)
{
	return rtrim(ltrim(s));
}

std::istream& utilities::safe_get_line(std::istream& is, std::string& t)
{
	t.clear();
	// The characters in the stream are read one-by-one using a std::streambuf.
	// That is faster than reading them one-by-one using the std::istream.
	// Code that uses streambuf this way must be guarded by a sentry object.
	// The sentry object performs various tasks,
	// such as thread synchronization and updating the stream state.

	std::istream::sentry se(is, true);
	std::streambuf* sb = is.rdbuf();

	for (;;) {
		const int c = sb->sbumpc();
		switch (c) {
		case '\n':
			return is;
		case '\r':
			if (sb->sgetc() == '\n')
				sb->sbumpc();
			return is;
		case EOF:
			// Also handle the case when the last line has no line ending
			if (t.empty())
				is.setstate(std::ios::eofbit);
			return is;
		default:
			t += static_cast<char>(c);
		}
	}
}

std::string utilities::center(std::string input, int width) 
{
	int input_width = (int)input.length();
	int difference = width - input_width;
	if (difference < 1)
	{
		return input;
	}
	int left_padding = difference /2;
	int right_padding = width - left_padding - input_width;
	return std::string(left_padding, ' ') + input + std::string(right_padding, ' ');
}

double utilities::get_percent_change(int v1, int v2)
{
	if (v1 != 0)
	{
		return (((double)v2 - (double)v1) / (double)v1) * 100.0;
	}
	return 0;
}

std::string utilities::get_percent_change_string(int v1, int v2, int precision)
{
	std::stringstream format_stream;
	format_stream.str(std::string());
	std::string percent_change_string;
	if (v1 == 0)
	{
		if (v2 > 0)
		{
			format_stream << "INF";
		}
		else
		{
			format_stream << std::fixed << std::setprecision(1) << 0.0 << "%";
		}
	}
	else
	{
		double percent_change = get_percent_change(v1, v2);
		format_stream << std::fixed << std::setprecision(precision) << percent_change << "%";
	}
	return format_stream.str();
}

int utilities::get_num_digits(int x)
{
	x = abs(x);
	return (x < 10 ? 1 :
		(x < 100 ? 2 :
			(x < 1000 ? 3 :
				(x < 10000 ? 4 :
					(x < 100000 ? 5 :
						(x < 1000000 ? 6 :
							(x < 10000000 ? 7 :
								(x < 100000000 ? 8 :
									(x < 1000000000 ? 9 :
										10)))))))));
}

int utilities::get_num_digits(double x)
{
	return get_num_digits((int) x);
}

// Nice utility function found here: https://stackoverflow.com/questions/8520560/get-a-file-name-from-a-path
std::vector<std::string> utilities::splitpath(const std::string& str)
{
	std::vector<std::string> result;

	char const* pch = str.c_str();
	char const* start = pch;
	for (; *pch; ++pch)
	{
		if (*pch == PATH_SEPARATOR_)
		{
			if (start != pch)
			{
				std::string str(start, pch);
				result.push_back(str);
			}
			else
			{
				result.push_back("");
			}
			start = pch + 1;
		}
	}
	result.push_back(start);

	return result;
}

bool utilities::get_file_path(const std::string& file_path, std::string & path)
{
	std::vector<std::string> file_parts = splitpath(file_path);
	if (file_parts.size() == 0)
		return false;
	for (int index = 0; index < file_parts.size() - 1; index++)
	{
		path += file_parts[index];
		path += PATH_SEPARATOR_;
	}
	return true;
}

std::string utilities::create_uuid() {
	std::string res;
	for (int i = 0; i < 16; i++) {
		if (GUID_DASHES[i]) res += "-";
		res += GUID_RANGE[(int)(rand() % 16)];
		res += GUID_RANGE[(int)(rand() % 16)];
	}
	return res;
}

bool utilities::get_temp_file_path_for_file(const std::string& file_path, std::string& temp_file_path)
{
	temp_file_path = "";
	if (!utilities::get_file_path(file_path, temp_file_path))
	{
		return false;
	}
	temp_file_path = temp_file_path;
	temp_file_path += utilities::create_uuid();
	temp_file_path += ".tmp";
	return true;
}

double utilities::hypot(double x, double y)
{
	if (x < 0) x = -x;
	if (y < 0) y = -y;
	if (x < y) {
		double tmp = x;
		x = y; y = tmp;
	}
	if (y == 0.0) return x;
	y /= x;
	return x * std::sqrt(1.0 + y * y);
}

std::string utilities::dtos(double x, unsigned char precision)
{
	static char buffer[FPCONV_BUFFER_LENGTH];
	char* p = buffer;
	buffer[fpconv_dtos(x, buffer, precision)] = '\0';
	return buffer;
}