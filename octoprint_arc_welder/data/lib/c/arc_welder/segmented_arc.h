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

#define GCODE_CHAR_BUFFER_SIZE 1000

class segmented_arc :
	public segmented_shape
{
public:
	segmented_arc();
	segmented_arc(
		int min_segments = DEFAULT_MIN_SEGMENTS, 
		int max_segments = DEFAULT_MAX_SEGMENTS, 
		double resolution_mm = DEFAULT_RESOLUTION_MM, 
		double path_tolerance_percnet = ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT, 
		double max_radius_mm = DEFAULT_MAX_RADIUS_MM,
		int min_arc_segments = DEFAULT_MIN_ARC_SEGMENTS,
		double mm_per_arc_segment = DEFAULT_MM_PER_ARC_SEGMENT,
		bool allow_z_axis_changes = DEFAULT_ALLOW_Z_AXIS_CHANGES
	);
	virtual ~segmented_arc();
	virtual bool try_add_point(point p, double e_relative);
	std::string get_shape_gcode_absolute(double e, double f);
	std::string get_shape_gcode_relative(double f);
	
	virtual bool is_shape() const;
	point pop_front(double e_relative);
	point pop_back(double e_relative);
	double get_max_radius() const;
	int get_min_arc_segments() const;
	double get_mm_per_arc_segment() const;

private:
	bool try_add_point_internal_(point p, double pd);
	std::string get_shape_gcode_(bool has_e, double e, double f) const;
	std::string get_g1(double x, double y, double z, double e, double f, bool has_z);
	std::string interpolate_arc(double f, bool is_relative, double start_e = 0);
	//circle arc_circle_;
	arc current_arc_;
	double max_radius_mm_;
	int min_arc_segments_;
	double mm_per_arc_segment_;
	bool allow_z_axis_changes_;
};															

