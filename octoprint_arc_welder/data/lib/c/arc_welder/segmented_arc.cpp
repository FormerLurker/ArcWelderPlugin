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

#include "segmented_arc.h"
#include "utilities.h"
#include "segmented_shape.h"
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <cmath>

segmented_arc::segmented_arc() : segmented_shape(DEFAULT_MIN_SEGMENTS, DEFAULT_MAX_SEGMENTS, DEFAULT_RESOLUTION_MM, ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT)
{
	
}

segmented_arc::segmented_arc(
	int min_segments, 
	int max_segments, 
	double resolution_mm, 
	double path_tolerance_percent, 
	double max_radius_mm
	) : segmented_shape(min_segments, max_segments, resolution_mm, path_tolerance_percent)
{
	if (max_radius_mm > DEFAULT_MAX_RADIUS_MM) max_radius_mm_ = DEFAULT_MAX_RADIUS_MM;
	else max_radius_mm_ = max_radius_mm;

}

segmented_arc::~segmented_arc()
{
}

point segmented_arc::pop_front(double e_relative)
{
	e_relative_ -= e_relative;
	if (points_.count() == get_min_segments())
	{
		set_is_shape(false);
	}
	return points_.pop_front();
}
point segmented_arc::pop_back(double e_relative)
{
	e_relative_ -= e_relative;
	return points_.pop_back();
	if (points_.count() == get_min_segments())
	{
		set_is_shape(false);
	}
}
double segmented_arc::get_max_radius() const
{
	return max_radius_mm_;
}



bool segmented_arc::is_shape() const
{
	return is_shape_;
}

bool segmented_arc::try_add_point(point p, double e_relative)
{
	
	bool point_added = false;
	// if we don't have enough segnemts to check the shape, just add
	if (points_.count() > get_max_segments() - 1)
	{
		// Too many points, we can't add more
		return false;
	}
	double distance = 0;
	if (points_.count() > 0)
	{
		point p1 = points_[points_.count() - 1];
		distance = utilities::get_cartesian_distance(p1.x, p1.y, p.x, p.y);
		if (!utilities::is_equal(p1.z, p.z))
		{
			// Arcs require that z is equal for all points
			//std::cout << " failed - z change.\n";

			return false;
		}

		if (utilities::is_zero(distance))
		{
			// there must be some distance between the points
			// to make an arc.
			//std::cout << " failed - no distance change.\n";
			return false;
		}
		
	}
	
	if (points_.count() < get_min_segments() - 1)
	{
		point_added = true;
		points_.push_back(p);
		original_shape_length_ += distance;
		if (points_.count() == get_min_segments())
		{
			if (!arc::try_create_arc(points_, current_arc_, original_shape_length_, max_radius_mm_, resolution_mm_, path_tolerance_percent_))
			{
				point_added = false;
				points_.pop_back();
				original_shape_length_ -= distance;
			}
		}
	}
	else
	{
		// if we're here, we need to see if the new point can be included in the shape
		point_added = try_add_point_internal_(p, distance);
	}
	if (point_added)
	{
		
		if (points_.count() > 1)
		{
			// Only add the relative distance to the second point on up.
			e_relative_ += e_relative;
		}
		//std::cout << " success - " << points_.count() << " points.\n";
	}
	else if (points_.count() < get_min_segments() && points_.count() > 1)
	{
		// If we haven't added a point, and we have exactly min_segments_,
		// pull off the initial arc point and try again
		point old_initial_point = points_.pop_front();
		// We have to remove the distance and e relative value
		// accumulated between the old arc start point and the new
		point new_initial_point = points_[0];
		original_shape_length_ -= utilities::get_cartesian_distance(old_initial_point.x, old_initial_point.y, new_initial_point.x, new_initial_point.y);
		e_relative_ -= new_initial_point.e_relative;
		//std::cout << " failed - removing start point and retrying current point.\n";
		return try_add_point(p, e_relative);
	}

	return point_added;
}

bool segmented_arc::try_add_point_internal_(point p, double pd)
{
	// If we don't have enough points (at least min_segments) return false
	if (points_.count() < get_min_segments() - 1)
		return false;
	
	// Create a test circle
	circle target_circle;

	// the circle is new..  we have to test it now, which is expensive :(
	points_.push_back(p);
	double previous_shape_length = original_shape_length_;
	original_shape_length_ += pd;

	if (arc::try_create_arc(points_, current_arc_, original_shape_length_, max_radius_mm_, resolution_mm_, path_tolerance_percent_))
	{
		if (!is_shape())
		{	
			set_is_shape(true);
		}
		return true;
	}
	// Can't create the arc.  Remove the point and remove the previous segment length.
	points_.pop_back();
	original_shape_length_ = previous_shape_length;
	return false;	
}

std::string segmented_arc::get_shape_gcode_absolute(double e, double f)
{
	bool has_e = e_relative_ != 0;
	return get_shape_gcode_(has_e, e, f);
}

std::string segmented_arc::get_shape_gcode_relative(double f)
{
	bool has_e = e_relative_ != 0;
	return get_shape_gcode_(has_e, e_relative_, f);
}

std::string segmented_arc::get_shape_gcode_(bool has_e, double e, double f) const
{
	
	char buf[20];
	std::string gcode;
	
	double i = current_arc_.center.x - current_arc_.start_point.x;
	double j = current_arc_.center.y - current_arc_.start_point.y;
	// Here is where the performance part kicks in (these are expensive calls) that makes things a bit ugly.
	// there are a few cases we need to take into consideration before choosing our sprintf string
	// create the XYZ portion
	
	if (current_arc_.angle_radians < 0)
	{
		gcode = "G2";
	}
	else
	{
		gcode = "G3";
	
	}
	// Add X, Y, I and J
	gcode += " X";
	gcode += utilities::to_string(current_arc_.end_point.x, xyz_precision_, buf, false);

	gcode += " Y";
	gcode += utilities::to_string(current_arc_.end_point.y, xyz_precision_, buf, false);

	gcode += " I";
	gcode += utilities::to_string(i, xyz_precision_, buf, false);

	gcode += " J";
	gcode += utilities::to_string(j, xyz_precision_, buf, false);
	
	// Add E if it appears
	if (has_e)
	{
		gcode += " E";
		gcode += utilities::to_string(e, e_precision_, buf, false);
	}

	// Add F if it appears
	if (utilities::greater_than_or_equal(f, 1))
	{
		gcode += " F";
		gcode += utilities::to_string(f, 0, buf, true);
	}

	return gcode;

}

