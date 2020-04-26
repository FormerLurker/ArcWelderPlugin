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
#include <sstream>
#include "math.h"
#include <stdio.h>

segmented_arc::segmented_arc() : segmented_shape()
{
	min_segments_ = 3;
	s_stream_ << std::fixed;
}

segmented_arc::segmented_arc(int max_segments, double resolution_mm) : segmented_shape(3, max_segments, resolution_mm)
{
	min_segments_ = 3;
	s_stream_ << std::fixed;
}

segmented_arc::~segmented_arc()
{
}

point segmented_arc::pop_front(double e_relative)
{
	e_relative_ -= e_relative;
	if (points_.count() == min_segments_)
	{
		set_is_shape(false);
	}
	return points_.pop_front();
}
point segmented_arc::pop_back(double e_relative)
{
	e_relative_ -= e_relative;
	return points_.pop_back();
	if (points_.count() == min_segments_)
	{
		set_is_shape(false);
	}
}

bool segmented_arc::is_shape()
{
	if (is_shape_)
	{
		arc a;
		bool is_arc = try_get_arc(a);
		return is_arc;
	}
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
		/*else if (utilities::greater_than(distance, max_segment_length_))
		{
			// we can't make an arc if the distance between points
			// is greater than the resolution.
			return false;
		}*/  // Test - see what happens without a max segment length.
	}
	if (points_.count() < min_segments_ - 1)
	{
		point_added = true;
	}
	else
	{
		// if we're here, we need to see if the new point can be included in the shape
		point_added = try_add_point_internal(p, distance);
	}
	if (point_added)
	{
		points_.push_back(p);
		original_shape_length_ += distance;
		if (points_.count() > 1)
		{
			// Only add the relative distance to the second point on up.
			e_relative_ += e_relative;
		}
		//std::cout << " success - " << points_.count() << " points.\n";
	}
	else if (points_.count() < min_segments_ && points_.count() > 1)
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

bool segmented_arc::try_add_point_internal(point p, double pd)
{
	// If we don't have enough points (at least min_segments) return false
	if (points_.count() < min_segments_ - 1)
		return false;
	
	// Create a test circle
	circle test_circle;
	bool circle_created;
	// Find a point in the middle of our list for p2
	int mid_point_index = ((points_.count() - 2) / 2)+1;
	circle_created = circle::try_create_circle(points_[0], points_[mid_point_index], p, test_circle);
	
	if (circle_created)
	{

		// If we got a circle, make sure all of the points fit within the tolerance.
		bool circle_fits_points;

		// the circle is new..  we have to test it now, which is expensive :(
		circle_fits_points = does_circle_fit_points(test_circle, p, pd);
		if (circle_fits_points)
		{
			arc_circle_ = test_circle;
		}
		
		// Only set is_shape if it goes from false to true
		if (!is_shape())
			set_is_shape(circle_fits_points);
		
		return circle_fits_points;
	}
	
	//std::cout << " failed - could not create a circle from the points.\n";
	return false;
	
}

bool segmented_arc::does_circle_fit_points(circle c, point p, double pd)
{
	// We know point 1 must fit (we used it to create the circle).  Check the other points
	// Note:  We have not added the current point, but that's fine since it is guaranteed to fit too.
	// If this works, it will be added.

	double distance_from_center;
	double difference_from_radius;
	
	// Check the endpoints to make sure they fit the current circle
	for (int index = 1; index < points_.count(); index++)
	{
		// Make sure the length from the center of our circle to the test point is 
		// at or below our max distance.
		distance_from_center = utilities::get_cartesian_distance(points_[index].x, points_[index].y, c.center.x, c.center.y);
		double difference_from_radius = abs(distance_from_center - c.radius);
		if (utilities::greater_than(difference_from_radius, resolution_mm_))
		{
			//std::cout << " failed - end points do not lie on circle.\n";
			return false;
		}
	}
	
	/*
	// Check the midpoints of the segments in the points_ to make sure they fit our circle.
	 for (int index = 0; index < points_.count() - 1; index++)
	{
		// Make sure the length from the center of our circle to the test point is 
		// at or below our max distance.
		point midpoint = point::get_midpoint(points_[index], points_[index + 1]);
		distance_from_center = utilities::get_cartesian_distance(midpoint.x, midpoint.y, c.center.x, c.center.y);
		difference_from_radius = abs(distance_from_center - c.radius);
		// Test allowing more play for the midpoints.
		if (utilities::greater_than(difference_from_radius, resolution_mm_))
		{
			//std::cout << " failed - midpoints do not lie on circle.\n";
			return false;
		}
	}
	*/
	// Check the point perpendicular from the segment to the circle's center, if any such point exists
	for (int index = 0; index < points_.count() - 1; index++)
	{
		point point_to_test;
		if (segment::get_closest_perpendicular_point(points_[index], points_[index + 1], c.center, point_to_test))
		{
			distance_from_center = utilities::get_cartesian_distance(point_to_test.x, point_to_test.y, c.center.x, c.center.y);
			difference_from_radius = abs(distance_from_center - c.radius);
			// Test allowing more play for the midpoints.
			if (utilities::greater_than(difference_from_radius, resolution_mm_))
			{
				return false;
			}
		}
		
	}

	// Check the midpoint of the new point and the final point
	point point_to_test;
	if (segment::get_closest_perpendicular_point(points_[points_.count() - 1], p, c.center, point_to_test))
	{
		distance_from_center = utilities::get_cartesian_distance(point_to_test.x, point_to_test.y, c.center.x, c.center.y);
		difference_from_radius = abs(distance_from_center - c.radius);
		// Test allowing more play for the midpoints.
		if (utilities::greater_than(difference_from_radius, resolution_mm_))
		{
			return false;
		}
	}
	
	// get the current arc and compare the total length to the original length
	arc a;
	return try_get_arc(c, p, pd, a );
	/*
	if (!a.is_arc || utilities::greater_than(abs(a.length - (original_shape_length_ + pd)), resolution_mm_*2))
	{
		//std::cout << " failed - final lengths do not match.\n";
		return false;
	}
	return true;
	*/
}

bool segmented_arc::try_get_arc(arc & target_arc)
{
	int mid_point_index = ((points_.count() - 2) / 2) + 1;
	return arc::try_create_arc(arc_circle_, points_[0], points_[mid_point_index], points_[points_.count() - 1], original_shape_length_, resolution_mm_, target_arc);
}

bool segmented_arc::try_get_arc(circle& c, point endpoint, double additional_distance, arc &target_arc)
{
	int mid_point_index = ((points_.count() - 1) / 2) + 1;
	return arc::try_create_arc(c, points_[0], points_[mid_point_index], endpoint, original_shape_length_ + additional_distance, resolution_mm_, target_arc);
}
/*

std::string segmented_arc::get_shape_gcode_absolute(double f, double e_abs_start)
{

	s_stream_.clear();
	s_stream_.str("");
	arc c;
	try_get_arc(c);
	
	double new_extrusion;
	// get the original ratio of filament extruded to length, but not for retractions
	if (utilities::greater_than(e_relative_, 0))
	{
		double extrusion_per_mm = e_relative_ / original_shape_length_;
		new_extrusion = c.length * extrusion_per_mm;
	}
	else
	{
		new_extrusion = e_relative_;
	}
	
	
	double i = c.center.x - c.start_point.x;
	double j = c.center.y - c.start_point.y;
	if (utilities::less_than(c.angle_radians, 0))
	{
		s_stream_ << "G2";
	}
	else
	{
		s_stream_ << "G3";
	}
	s_stream_ << std::setprecision(3);
	s_stream_ << " X" << c.end_point.x << " Y" << c.end_point.y << " I" << i << " J" << j;
	// Do not output for travel movements
	if (e_relative_ != 0)
	{
		s_stream_ << std::setprecision(5);
		s_stream_ << " E" << e_abs_start + new_extrusion;
	}
	
	if (utilities::greater_than(f, 0))
	{
		s_stream_ << std::setprecision(0) << " F" << f;
	}
	return s_stream_.str();
}*/

std::string segmented_arc::get_shape_gcode_absolute(double f, double e_abs_start)
{
	arc c;
	try_get_arc(c);

	double new_extrusion;
	// get the original ratio of filament extruded to length, but not for retractions
	if (utilities::greater_than(e_relative_, 0))
	{
		double extrusion_per_mm = e_relative_ / original_shape_length_;
		new_extrusion = c.length * extrusion_per_mm;
	}
	else
	{
		new_extrusion = e_relative_;
	}
	double i = c.center.x - c.start_point.x;
	double j = c.center.y - c.start_point.y;
	// Here is where the performance part kicks in (these are expensive calls) that makes things a bit ugly.
	// there are a few cases we need to take into consideration before choosing our sprintf string
	if (utilities::less_than(c.angle_radians, 0))
	{
		// G2
		if (e_relative_ != 0)
		{
			double e = e_abs_start + new_extrusion;
			// Add E param
			if (utilities::greater_than_or_equal(f, 1))
			{
				// Add F param
				snprintf(gcode_buffer_, sizeof(gcode_buffer_), "G2 X%.3f Y%.3f I%.3f J%.3f E%.5f F%.0f", c.end_point.x, c.end_point.y, i, j, e, f);
			}
			else
			{
				// No F param
				snprintf(gcode_buffer_, sizeof(gcode_buffer_), "G2 X%.3f Y%.3f I%.3f J%.3f E%.5f", c.end_point.x, c.end_point.y, i, j, e);
			}
		}
		else
		{
			// No E param
			// Add E param
			if (utilities::greater_than_or_equal(f, 1))
			{
				// Add F param
				snprintf(gcode_buffer_, sizeof(gcode_buffer_), "G2 X%.3f Y%.3f I%.3f J%.3f F%.0f", c.end_point.x, c.end_point.y, i, j, f);
			}
			else
			{
				// No F param
				snprintf(gcode_buffer_, sizeof(gcode_buffer_), "G2 X%.3f Y%.3f I%.3f J%.3f", c.end_point.x, c.end_point.y, i, j);
			}
		}
	}
	else
	{
		// G3
		if (e_relative_ != 0)
		{
			double e = e_abs_start + new_extrusion;
			// Add E param
			if (utilities::greater_than_or_equal(f, 1))
			{
				// Add F param
				snprintf(gcode_buffer_, sizeof(gcode_buffer_), "G3 X%.3f Y%.3f I%.3f J%.3f E%.5f F%.0f", c.end_point.x, c.end_point.y, i, j, e, f);
			}
			else
			{
				// No F param
				snprintf(gcode_buffer_, sizeof(gcode_buffer_), "G3 X%.3f Y%.3f I%.3f J%.3f E%.5f", c.end_point.x, c.end_point.y, i, j, e);
			}
		}
		else
		{
			// No E param
			// Add E param
			if (utilities::greater_than_or_equal(f, 1))
			{
				// Add F param
				snprintf(gcode_buffer_, sizeof(gcode_buffer_), "G3 X%.3f Y%.3f I%.3f J%.3f F%.0f", c.end_point.x, c.end_point.y, i, j, f);
			}
			else
			{
				// No F param
				snprintf(gcode_buffer_, GCODE_CHAR_BUFFER_SIZE, "G3 X%.3f Y%.3f I%.3f J%.3f", c.end_point.x, c.end_point.y, i, j);
			}
		}
	}
	return std::string(gcode_buffer_);

}

std::string segmented_arc::get_shape_gcode_relative(double f)
{
	return get_shape_gcode_absolute(f, 0.0);
}
