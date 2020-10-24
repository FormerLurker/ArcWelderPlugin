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
#include <string>
#include <limits>
#define PI_DOUBLE 3.14159265358979323846
//#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.0000000001 // fail
//#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.001 // pass
//#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.0001 // pass
#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.00001 // PASS
//#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.000001 // pass 
//#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.0000001 // fail
//#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.0000005 // fail
//#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.00000075 // fail
//#define CIRCLE_GENERATION_A_ZERO_TOLERANCE 0.000000875 // fail


#include <list> 
#include "utilities.h"
#include "array_list.h"
// The minimum theta value allowed between any two arc in order for an arc to be
// created.  This prevents sign calculation issues for very small values of theta

//#define MIN_ALLOWED_ARC_THETA     0.0000046875f // Lowest discovered value for full theta

struct point
{
public:
	point() {
		x = 0;
		y = 0;
		z = 0;
		e_relative = 0;
	}
	point(double p_x, double p_y, double p_z, double p_e_relative) {
		x = p_x;
		y = p_y;
		z = p_z;
		e_relative = p_e_relative;
	}
	double x;
	double y;
	double z;
	double e_relative;
	static point get_midpoint(point p1, point p2);
};

struct segment
{
	segment()
	{
		
	}
	segment(point p_1, point p_2)
	{
		p1.x = p_1.x;
		p1.y = p_1.y;
		p1.z = p_1.z;

		p2.x = p_2.x;
		p2.y = p_2.y;
		p2.z = p_2.z;
	}
	point p1;
	point p2;

	bool get_closest_perpendicular_point(point c, point& d);
	static bool get_closest_perpendicular_point(point p1, point p2, point c, point& d);
};

struct vector : point
{
	vector() {
		x = 0;
		y = 0;
		z = 0;
	}
	vector(double p_x, double p_y, double p_z) {
		x = p_x;
		y = p_y;
		z = p_z;
	}

	double get_magnitude();
	static double cross_product_magnitude(vector v1, vector v2);
	
};

struct circle {
	circle() {
		center.x = 0;
		center.y = 0;
		center.z = 0;
		radius = 0;
	};
	circle(point p, double r)
	{
		center.x = p.x;
		center.y = p.y;
		center.z = p.z;
		radius = r;
	}
	point center;
	double radius;

	bool is_point_on_circle(point p, double resolution_mm);
	static bool try_create_circle(point p1, point p2, point p3, double max_radius, circle& new_circle);
	
	double get_radians(const point& p1, const point& p2) const;

	double get_polar_radians(const point& p1) const;

	point get_closest_point(const point& p) const;
};

struct arc : circle
{
	arc() {
		center.x = 0;
		center.y = 0;
		center.z = 0;
		radius = 0;
		length = 0;
		angle_radians = 0;
		start_point.x = 0;
		start_point.y = 0;
		start_point.z = 0;
		end_point.x = 0;
		end_point.y = 0;
		end_point.z = 0;
		is_arc = false;
		polar_start_theta = 0;
		polar_end_theta = 0;
			
	}
	
	bool is_arc;
	double length;
	double angle_radians;
	double polar_start_theta;
	double polar_end_theta;
	point start_point;
	point end_point;
	static bool try_create_arc(const circle& c, const point& start_point, const point& mid_point, const point& end_point, double approximate_length, double resolution, arc& target_arc);
	static bool try_create_arc(const circle& c, const array_list<point>& points, double approximate_length, double resolution, arc& target_arc);
};
double distance_from_segment(segment s, point p);

#define DEFAULT_MIN_SEGMENTS 3
#define DEFAULT_MAX_SEGMENTS 50
#define DEFAULT_RESOLUTION_MM 0.05
class segmented_shape
{
public:
	
	segmented_shape(int min_segments = DEFAULT_MIN_SEGMENTS, int max_segments = DEFAULT_MAX_SEGMENTS, double resolution_mm = DEFAULT_RESOLUTION_MM);
	segmented_shape& operator=(const segmented_shape& pos);
	virtual ~segmented_shape();
	int get_num_segments();
	int get_min_segments();
	int get_max_segments();
	double get_resolution_mm();
	double get_shape_length();
	double get_shape_e_relative();
	void set_resolution_mm(double resolution_mm);
	virtual bool is_shape() const;
	// public virtual functions
	virtual void clear();
	virtual point pop_front();
	virtual point pop_back();
	virtual bool try_add_point(point p, double e_relative);
	virtual std::string get_shape_gcode_absolute(double e_abs_start);
	virtual std::string get_shape_gcode_relative();
	bool is_extruding();
protected:
	array_list<point> points_;
	void set_is_shape(bool value);
	double original_shape_length_;
	double e_relative_;
	bool is_extruding_;
	double resolution_mm_;
	bool is_shape_;
private:
	int min_segments_;
	int max_segments_;
	
};
