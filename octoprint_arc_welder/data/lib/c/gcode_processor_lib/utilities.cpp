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

// Had to increase the zero tolerance because prusa slicer doesn't always retract enough while wiping.
const double ZERO_TOLERANCE = 0.000005;
const std::string utilities::WHITESPACE_ = " \n\r\t\f\v";

bool utilities::is_zero(double x)
{
	return std::abs(x) < ZERO_TOLERANCE;
}

int utilities::round_up_to_int(double x)
{
	return int(x + ZERO_TOLERANCE);
}

bool utilities::is_equal(double x, double y)
{
	double abs_difference = std::abs(x - y);
	return abs_difference < ZERO_TOLERANCE;
}

bool utilities::greater_than(double x, double y)
{
	return x > y && !is_equal(x, y);
}

bool utilities::greater_than_or_equal(double x, double y)
{
	return x > y || is_equal(x, y);
}

bool utilities::less_than(double x, double y)
{
	return x < y && !is_equal(x, y);
}

bool utilities::less_than_or_equal(double x, double y)
{
	return x < y || is_equal(x, y);
}

// custom tolerance functions
bool utilities::is_zero(double x, double tolerance)
{
	return std::abs(x) < tolerance;
}

bool utilities::is_equal(double x, double y, double tolerance)
{
	double abs_difference = std::abs(x - y);
	return abs_difference < tolerance;
}

int utilities::round_up_to_int(double x, double tolerance)
{
	return int(x + tolerance);
}
bool utilities::greater_than(double x, double y, double tolerance)
{
	return x > y && !utilities::is_equal(x, y, tolerance);
}
bool utilities::greater_than_or_equal(double x, double y, double tolerance)
{
	return x > y || utilities::is_equal(x, y, tolerance);
}
bool utilities::less_than(double x, double y, double tolerance)
{
	return x < y && !utilities::is_equal(x, y, tolerance);
}
bool utilities::less_than_or_equal(double x, double y, double tolerance)
{
	return x < y || utilities::is_equal(x, y, tolerance);
}

double utilities::get_cartesian_distance(double x1, double y1, double x2, double y2)
{
	// Compare the saved points cartesian distance from the current point
	double xdif = x1 - x2;
	double ydif = y1 - y2;
	double dist_squared = xdif * xdif + ydif * ydif;
	return sqrt(dist_squared);
}

double utilities::get_cartesian_distance(double x1, double y1, double z1, double x2, double y2, double z2)
{
	// Compare the saved points cartesian distance from the current point
	double xdif = x1 - x2;
	double ydif = y1 - y2;
	double zdif = z1 - z2;
	double dist_squared = xdif * xdif + ydif * ydif + zdif * zdif;
	return sqrt(dist_squared);
}

std::string utilities::to_string(double value)
{
	std::ostringstream os;
	os << value;
	return os.str();
}

char * utilities::to_string(double value, unsigned short precision, char * str)
{
	char reversed_int[20];
	
	int char_count = 0, int_count = 0;
	bool is_negative = false;
	double integer_part, fractional_part;
	fractional_part = std::abs(std::modf(value, &integer_part)); //Separate integer/fractional parts
	if (value < 0)
	{
		str[char_count++] = '-';
		integer_part *= -1;
		is_negative = true;
	}

	if (integer_part == 0)
	{
		str[char_count++] = '0';
	}
	else
	{
		while (integer_part > 0) //Convert integer part, if any
		{
			reversed_int[int_count++] = '0' + (int)std::fmod(integer_part, 10);
			integer_part = std::floor(integer_part / 10);
		}
	}
	int start = is_negative ? 1 : 0;
	int end = char_count - start;
	for (int i = 0; i < int_count; i++)
	{
		str[char_count++] = reversed_int[int_count - i - 1];
	}
	if (precision > 0)
	{
		str[char_count++] = '.'; //Decimal point

		while (fractional_part > 0 && precision-- > 0) //Convert fractional part, if any
		{
			fractional_part *= 10;
			fractional_part = std::modf(fractional_part, &integer_part);
			str[char_count++] = '0' + (int)integer_part;
		}
	}
	str[char_count] = 0; //String terminator
	return str;
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
