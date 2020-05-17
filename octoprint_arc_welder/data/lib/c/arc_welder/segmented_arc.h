////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arc Welder: Anti-Stutter Library
//
// Compresses many G0/G1 commands into G2/G3(arc) commands where possible, ensuring the tool paths stay within the specified resolution.
// This reduces file size and the number of gcodes per second.
//
// Uses the 'Gcode Processor Library' for gcode parsing, position processing, logging, and other various functionality.
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

#pragma once
#include "segmented_shape.h"
#include <iomanip>
#include <sstream>

#define GCODE_CHAR_BUFFER_SIZE 100
#define DEFAULT_MAX_RADIUS_MM 1000000.0 // 1km
class segmented_arc :
	public segmented_shape
{
public:
	segmented_arc();
	segmented_arc(int min_segments, int max_segments, double resolution_mm, double max_radius);
	virtual ~segmented_arc();
	virtual bool try_add_point(point p, double e_relative);
	std::string get_shape_gcode_absolute(double e, double f);
	std::string get_shape_gcode_relative(double f);
	
	virtual bool is_shape() const;
	point pop_front(double e_relative);
	point pop_back(double e_relative);
	bool try_get_arc(arc & target_arc);
	double get_max_radius() const;
	// static gcode buffer

private:
	bool try_add_point_internal_(point p, double pd);
	bool does_circle_fit_points_(circle& c) const;
	bool try_get_arc_(const circle& c, arc& target_arc);
	std::string get_shape_gcode_(bool has_e, double e, double f) const;
	circle arc_circle_;
	int test_count_ = 0;
	double max_radius_mm_;
};

