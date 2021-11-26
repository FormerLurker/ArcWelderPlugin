////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arc Welder: Anti-Stutter Library
//
// Compresses many G0/G1 commands into G2/G3(arc) commands where possible, ensuring the tool paths stay within the specified resolution.
// This reduces file size and the number of gcodes per second.
//
// Uses the 'Gcode Processor Library' for gcode parsing, position processing, logging, and other various functionality.
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
#include "segmented_shape.h"
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
		bool allow_3d_arcs = DEFAULT_ALLOW_3D_ARCS,
		unsigned char default_xyz_precision = DEFAULT_XYZ_PRECISION,
		unsigned char default_e_precision = DEFAULT_E_PRECISION,
		int max_gcode_length = DEFAULT_MAX_GCODE_LENGTH
	);
	virtual ~segmented_arc();
	virtual bool try_add_point(printer_point p);
	virtual double get_shape_length();
	std::string get_shape_gcode() const;
	int get_shape_gcode_length();
	virtual bool is_shape() const;
	printer_point pop_front(double e_relative);
	printer_point pop_back(double e_relative);
	double get_max_radius() const;
	int get_min_arc_segments() const;
	double get_mm_per_arc_segment() const;
	int get_num_firmware_compensations() const;
	int get_num_gcode_length_exceptions() const;
private:
	bool try_add_point_internal_(printer_point p);
	arc current_arc_;
	double max_radius_mm_;
	int min_arc_segments_;
	double mm_per_arc_segment_;
	int num_firmware_compensations_;
	bool allow_3d_arcs_;
	int max_gcode_length_;
	int num_gcode_length_exceptions_;
};															

