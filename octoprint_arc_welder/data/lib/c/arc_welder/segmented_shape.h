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
#include <string>
#include <limits>

#include <list> 
#include "utilities.h"
#include "array_list.h"
// The minimum theta value allowed between any two arc in order for an arc to be
// created.  This prevents sign calculation issues for very small values of theta
#define DEFAULT_XYZ_PRECISION 3
#define DEFAULT_XYZ_TOLERANCE 0.001
#define DEFAULT_E_PRECISION 5
#define ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT 0.05  // one percent
#define DEFAULT_MAX_GCODE_LENGTH 0 // the maximum gcode length ( < 1 = unlimited)
struct point
{
public:
	point() : x(0), y(0), z(0) {};
	point(double x, double y, double z) : x(x), y(y), z(z) {};
	double x;
	double y;
	double z;
	static point get_midpoint(point p1, point p2);
	static bool is_near_collinear(const point& p1, const point& p2, const point& p3, double percent_tolerance);
	static double cartesian_distance(const point& p1, const point& p2);
};

struct printer_point : point
{
public:
	printer_point() :point(0, 0, 0), e_relative(0), distance(0), is_extruder_relative(false), e_offset(0), f(0) {}
	printer_point(double x, double y, double z, double e_offset, double e_relative, double f, double distance, bool is_extruder_relative)
		: point(x,y,z), e_offset(e_offset), e_relative(e_relative), f(f), distance(distance), is_extruder_relative(is_extruder_relative) {}
	bool is_extruder_relative;
	double e_offset;
	double e_relative;
	double f;
	double distance;
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
	static bool get_closest_perpendicular_point(const point& p1, const point& p2, const point& c, point& d);
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

#define DEFAULT_MAX_RADIUS_MM 9999.0 // 9.999m
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

	static bool try_create_circle(const point &p1, const point &p2, const point &p3, const double max_radius, circle& new_circle);
	
	static bool try_create_circle(const array_list<printer_point>& points, const double max_radius, const double resolutino_mm, const double xyz_tolerance, bool allow_3d_arcs, circle& new_circle);

	double get_polar_radians(const point& p1) const;

	point get_closest_point(const point& p) const;

	bool is_over_deviation(const array_list<printer_point>& points, const double resolution_mm, const double xyz_tolerance, const bool allow_3d_arcs);
	
	bool get_deviation_sum_squared(const array_list<printer_point>& points, const double resolution_mm, const double xyz_tolerance, const bool allow_3d_arcs, double& sum_deviation);
};

#define DEFAULT_RESOLUTION_MM 0.05
#define DEFAULT_ALLOW_3D_ARCS false
#define DEFAULT_MIN_ARC_SEGMENTS 0
#define DEFAULT_MM_PER_ARC_SEGMENT 0
enum DirectionEnum { UNKNOWN = 0, COUNTERCLOCKWISE = 1, CLOCKWISE = 2};
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
		max_deviation = 0;
		direction = DirectionEnum::UNKNOWN;
	}
	
	bool is_arc;
	double length;
	double angle_radians;
	double polar_start_theta;
	double polar_end_theta;
	double max_deviation;
	printer_point start_point;
	printer_point end_point;
	DirectionEnum direction;
	double get_i() const;
	double get_j() const;
	static bool try_create_arc(
		const array_list<printer_point>& points, 
		arc& target_arc, 
		double approximate_length, 
		double max_radius = DEFAULT_MAX_RADIUS_MM,
		double resolution = DEFAULT_RESOLUTION_MM, 
		double path_tolerance_percent = ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT,
		int min_arc_segments = DEFAULT_MIN_ARC_SEGMENTS,
		double mm_per_arc_segment = DEFAULT_MM_PER_ARC_SEGMENT,
		double xyz_tolerance = DEFAULT_XYZ_TOLERANCE,
		bool allow_3d_arcs = DEFAULT_ALLOW_3D_ARCS);
	static bool are_points_within_slice(const arc& test_arc, const array_list<printer_point>& points);
	static bool ray_intersects_segment(const point rayOrigin, const point rayDirection, const printer_point point1, const printer_point point2);
	private:
		static bool try_create_arc(
			const circle& c,
			const printer_point& start_point,
			const printer_point& mid_point,
			const printer_point& end_point,
			arc& target_arc,
			double approximate_length,
			double resolution = DEFAULT_RESOLUTION_MM,
			double path_tolerance_percent = ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT,
			bool allow_3d_arcs = DEFAULT_ALLOW_3D_ARCS);
};

#define DEFAULT_MIN_SEGMENTS 3
#define DEFAULT_MAX_SEGMENTS 50

class segmented_shape
{
public:
	
	segmented_shape(int min_segments = DEFAULT_MIN_SEGMENTS,
		int max_segments = DEFAULT_MAX_SEGMENTS, 
		double resolution_mm = DEFAULT_RESOLUTION_MM, 
		double path_tolerance_percent = ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT,
		unsigned char default_xyz_precision = DEFAULT_XYZ_PRECISION,
		unsigned char default_e_precision = DEFAULT_E_PRECISION
	);
	segmented_shape& operator=(const segmented_shape& pos);
	virtual ~segmented_shape();
	int get_num_segments();
	int get_min_segments();
	int get_max_segments();
	double get_resolution_mm();
	double get_path_tolerance_percent();
	virtual double get_shape_length();
	double get_shape_e_relative();
	void set_resolution_mm(double resolution_mm);
	void reset_precision();
	void update_xyz_precision(unsigned char precision);
	void update_e_precision(unsigned char precision);
	virtual bool is_shape() const;
	// public virtual functions
	virtual void clear();
	virtual printer_point pop_front();
	virtual printer_point pop_back();
	virtual bool try_add_point(printer_point p, double e_relative);
	virtual std::string get_shape_gcode_absolute(double e_abs_start);
	virtual std::string get_shape_gcode_relative();
	bool is_extruding();
	unsigned char get_xyz_precision() const;
	unsigned char get_e_precision() const;
	double get_xyz_tolerance() const;
protected:
	array_list<printer_point> points_;
	void set_is_shape(bool value);
	double original_shape_length_;	
	double e_relative_;
	bool is_extruding_;
	double resolution_mm_;
	bool is_shape_;
	double path_tolerance_percent_;
private:
	int min_segments_;
	int max_segments_;
	unsigned char xyz_precision_;
	double xyz_tolerance_;
	unsigned char e_precision_;
	void set_xyz_tolerance_from_precision();
	void set_xyz_precision(unsigned char precision);
	
};
