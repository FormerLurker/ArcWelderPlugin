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

#include "segmented_shape.h"
#include <stdio.h>
#include "utilities.h"
#include <cmath>
#include <iostream>
#pragma region Operators for Vector and Point

point operator +(point lhs, const vector rhs) {
  point p(
    lhs.x + rhs.x,
    lhs.y + rhs.y,
    lhs.z + rhs.z
  );
  return p;
}

point operator -(point lhs, const vector rhs) {
  return point(
    lhs.x - rhs.x,
    lhs.y - rhs.y,
    lhs.z - rhs.z
  );
} 
vector operator -(point& lhs, point& rhs) {
  return vector(
    lhs.x - rhs.x,
    lhs.y - rhs.y,
    lhs.z - rhs.z
  );
}

vector operator -(const point& lhs, const point& rhs) {
  return vector(
    lhs.x - rhs.x,
    lhs.y - rhs.y,
    lhs.z - rhs.z
  );
}

vector operator *(vector lhs, const double& rhs) {
  return vector(
    lhs.x * rhs,
    lhs.y * rhs,
    lhs.z * rhs
  );
}
#pragma endregion Operators for Vector and Point

#pragma region Point Functions
point point::get_midpoint(point p1, point p2)
{
  double x = (p1.x + p2.x) / 2.0;
  double y = (p1.y + p2.y) / 2.0;
  double z = (p1.z + p2.z) / 2.0;

  return point(x, y, z);
}

bool point::is_near_collinear(const point& p1, const point& p2, const point& p3, double tolerance)
{
  return utilities::abs((p1.y - p2.y) * (p1.x - p3.x) - (p1.y - p3.y) * (p1.x - p2.x)) <= 1e-9;
}

double point::cartesian_distance(const point& p1, const point& p2)
{
  return utilities::get_cartesian_distance(p1.x, p1.y, p2.x, p2.y);
}

#pragma endregion Point Functions

#pragma region Segment Functions
bool segment::get_closest_perpendicular_point(point c, point& d)
{
  return segment::get_closest_perpendicular_point(p1, p2, c, d);
}

bool segment::get_closest_perpendicular_point(const point& p1, const point& p2, const point& c, point& d)
{
  // [(Cx - Ax)(Bx - Ax) + (Cy - Ay)(By - Ay)] / [(Bx - Ax) ^ 2 + (By - Ay) ^ 2]
  double num = (c.x - p1.x) * (p2.x - p1.x) + (c.y - p1.y) * (p2.y - p1.y);
  double x_dif = p2.x - p1.x;
  double y_dif = p2.y - p1.y;
  double denom = (x_dif * x_dif) + (y_dif * y_dif);
  double t = num / denom;

  // We're considering this a failure if t == 0 or t==1 within our tolerance.  In that case we hit the endpoint, which is OK.
  // Why are we using the CIRCLE_GENERATION_A_ZERO_TOLERANCE tolerance here??
  if (utilities::less_than_or_equal(t, 0) || utilities::greater_than_or_equal(t, 1))
    return false;

  d.x = p1.x + t * (p2.x - p1.x);
  d.y = p1.y + t * (p2.y - p1.y);

  return true;
}

#pragma endregion

#pragma region Vector Functions
double vector::get_magnitude()
{
  return utilities::sqrt(x * x + y * y + z * z);
}

double vector::cross_product_magnitude(vector v1, vector v2)
{
  return (v1.x * v2.y - v1.y * v2.x);
}
#pragma endregion Vector Functions

#pragma region Distance Calculation Source
// Distance Calculation code taken from the following source:
// Copyright for distance calculations:
// Copyright 2001 softSurfer, 2012 Dan Sunday
// This code may be freely used, distributed and modified for any purpose
// providing that this copyright notice is included with it.
// SoftSurfer makes no warranty for this code, and cannot be held
// liable for any real or imagined damage resulting from its use.
// Users of this code must verify correctness for their application.
// dot product (3D) which allows vector operations in arguments
#define dot(u,v)   ((u).x * (v).x + (u).y * (v).y + (u).z * (v).z)
//#define dotxy(u,v)   ((u).x * (v).x + (u).y * (v).y)
//#define norm(v)     utilities::sqrt(dot(v,v))     // norm = length of  vector
//#define d(u,v)      norm(u-v)          // distance = norm of difference

#pragma endregion Distance Calculation Source


#pragma region Circle Functions

bool circle::try_create_circle(const point& p1, const point& p2, const point& p3, const double max_radius, circle& new_circle)
{
  if (point::is_near_collinear(p1,p2,p3, 0.001))
  {
    return false;
  }
  double x1 = p1.x;
  double y1 = p1.y;
  double x2 = p2.x;
  double y2 = p2.y;
  double x3 = p3.x;
  double y3 = p3.y;

  double a = x1 * (y2 - y3) - y1 * (x2 - x3) + x2 * y3 - x3 * y2;
  //  Take out to figure out how we handle very small values for a
  if (utilities::is_zero(a, 0.000000001))
  {
    return false;
  }
  double b = (x1 * x1 + y1 * y1) * (y3 - y2)
    + (x2 * x2 + y2 * y2) * (y1 - y3)
    + (x3 * x3 + y3 * y3) * (y2 - y1);

  double c = (x1 * x1 + y1 * y1) * (x2 - x3)
    + (x2 * x2 + y2 * y2) * (x3 - x1)
    + (x3 * x3 + y3 * y3) * (x1 - x2);

  double x = -b / (2.0 * a);
  double y = -c / (2.0 * a);

  double radius = utilities::get_cartesian_distance(x, y, x1, y1);
  if (radius > max_radius)
    return false;

  new_circle.center.x = x;
  new_circle.center.y = y;
  new_circle.center.z = p1.z;
  new_circle.radius = radius;

  return true;
}

bool circle::try_create_circle(const array_list<printer_point>& points, const double max_radius, const double resolution_mm, const double xyz_tolerance, bool allow_3d_arcs, circle& new_circle)
{
  int count = points.count();
  int middle_index = count / 2;
  int end_index = count - 1;
  

  
  if (circle::try_create_circle(points[0], points[middle_index], points[end_index], max_radius, new_circle) && !new_circle.is_over_deviation(points, resolution_mm, xyz_tolerance, allow_3d_arcs))
  {
    return true;
  }
  
       /*
  // This could be a near complete circle.  In that case, the endpoints might be too close together to generate an accurate circle with the 
  // precision we have to work with.  Let's adjust our circle into thirds and test those points as a last ditch effort.
  if (count > 5)
  {
    middle_index = count / 3;
    end_index = middle_index + middle_index;
    if (circle::try_create_circle(points[0], points[middle_index], points[end_index], max_radius, test_circle) && !test_circle.is_over_deviation(points, resolution_mm, xyz_tolerance, allow_3d_arcs))
    {
      new_circle = test_circle;
      return true;
    }
  }
  return false;
         */
  
  // Find the circle with the least deviation, if one exists.
  // Note, this could possibly take a LONG time in the worst case, but it's a pretty unlikely.
  // However, if the midpoint check doesn't pass, it's worth it to spend a bit more time 
  // finding the best fit for the circle (least squares deviation) 
  
  double least_deviation;
  bool found_circle=false;

  for (int index = 1; index < count - 1; index++)
  {
    
    if (index == middle_index)
    {
      // We already checked this one, and it failed, continue.
      continue;
    }
    circle test_circle;
    double current_deviation;
    if (circle::try_create_circle(points[0], points[index], points[count - 1], max_radius, test_circle) && test_circle.get_deviation_sum_squared(points, resolution_mm, xyz_tolerance, allow_3d_arcs, current_deviation))
    {
      
      if (!found_circle || current_deviation < least_deviation)
      {
        found_circle = true;
        least_deviation = current_deviation;
        new_circle = test_circle;
      }
    }
  }
  return found_circle;
  
}

double circle::get_polar_radians(const point& p1) const
{
  double polar_radians = utilities::atan2(p1.y - center.y, p1.x - center.x);
  if (polar_radians < 0)
    polar_radians = (2.0 * PI_DOUBLE) + polar_radians;
  return polar_radians;
}

bool circle::get_deviation_sum_squared(const array_list<printer_point>& points, const double resolution_mm, const double xyz_tolerance, const bool allow_3d_arcs, double &total_deviation)
{
  // We need to ensure that the Z steps are constand per linear travel unit
  double z_step_per_distance = 0;
  total_deviation = 0;
  // Skip the first and last points since they will fit perfectly.
  for (int index = 1; index < points.count() - 1; index++)
  {
    // Make sure the length from the center of our circle to the test point is 
    // at or below our max distance.
    double distance_from_center = utilities::get_cartesian_distance(points[index].x, points[index].y, center.x, center.y);
    if (allow_3d_arcs) {
      double z1 = points[index - 1].z;
      double z2 = points[index].z;

      double current_z_stepper_distance = (z2 - z1) / distance_from_center;
      if (index == 1) {
        z_step_per_distance = current_z_stepper_distance;
      }
      else if (!utilities::is_equal(z_step_per_distance, current_z_stepper_distance, xyz_tolerance))
      {
        // The z step is uneven, can't create arc				
        return false;
      }
    }
    double deviation = utilities::abs(distance_from_center - radius);
    total_deviation += deviation * deviation;
    if (deviation > resolution_mm)
    {
      // Too much deviation
      return false;
    }
  }
  // Check the point perpendicular from the segment to the circle's center, if any such point exists
  for (int index = 0; index < points.count() - 1; index++)
  {
    point point_to_test;
    if (segment::get_closest_perpendicular_point(points[index], points[index + 1], center, point_to_test))
    {
      double distance = utilities::get_cartesian_distance(point_to_test.x, point_to_test.y, center.x, center.y);
      double deviation = utilities::abs(distance - radius);
      total_deviation += deviation * deviation;
      if (deviation > resolution_mm)
      {
        return false;
      }
    }
  }
  return true;
}

bool circle::is_over_deviation(const array_list<printer_point>& points, const double resolution_mm, const double xyz_tolerance, const bool allow_3d_arcs)
{
  // We need to ensure that the Z steps are constand per linear travel unit
  double z_step_per_distance = 0;
  // shared point to test
  point point_to_test;
  int max_index = points.count() - 1;
  // Skip the first and last points since they will fit perfectly.
  for (int index = 0; index < max_index; index++)
  {
    point current_point(points[index]);
    if (index != 0)
    {
      // Make sure the length from the center of our circle to the test point is 
      // at or below our max distance.
      double distance_from_center = utilities::get_cartesian_distance(current_point.x, current_point.y, center.x, center.y);
      if (allow_3d_arcs) {
        double z1 = points[index - 1].z;
        double z2 = current_point.z;

        double current_z_stepper_distance = (z2 - z1) / distance_from_center;
        if (index == 1) {
          z_step_per_distance = current_z_stepper_distance;
        }
        else if (!utilities::is_equal(z_step_per_distance, current_z_stepper_distance, xyz_tolerance))
        {
          // The z step is uneven, can't create arc				
          return true;
        }
      }
      if (utilities::abs(distance_from_center - radius) > resolution_mm)
      {
        return true;
      }
    }
    
    // Check the point perpendicular from the segment to the circle's center, if any such point exists
    
    if (segment::get_closest_perpendicular_point(current_point, points[index + 1], center, point_to_test))
    {
      double distance = utilities::get_cartesian_distance(point_to_test.x, point_to_test.y, center.x, center.y);
      if (utilities::abs(distance - radius) > resolution_mm)
      {
       return true;
      }
    }
  }
  return false;
}
#pragma endregion Circle Functions

#pragma region Arc Functions
double arc::get_i() const
{
  return center.x - start_point.x;
}

double arc::get_j() const
{
  return center.y - start_point.y;
}

bool arc::try_create_arc(
  const circle& c,
  const printer_point& start_point,
  const printer_point& mid_point,
  const printer_point& end_point,
  arc& target_arc,
  double approximate_length,
  double resolution,
  double path_tolerance_percent,
  bool allow_3d_arcs)
{
  double polar_start_theta = c.get_polar_radians(start_point);
  double polar_mid_theta = c.get_polar_radians(mid_point);
  double polar_end_theta = c.get_polar_radians(end_point);

  // variable to hold radians
  double angle_radians = 0;
  DirectionEnum direction = DirectionEnum::UNKNOWN;  // 1 = counter clockwise, 2 = clockwise, 3 = unknown.
  // Determine the direction of the arc
  if (polar_end_theta > polar_start_theta)
  {
    if (polar_start_theta < polar_mid_theta && polar_mid_theta < polar_end_theta) {
      direction = DirectionEnum::COUNTERCLOCKWISE;
      angle_radians = polar_end_theta - polar_start_theta;
    }
    else if (
      (0.0 <= polar_mid_theta && polar_mid_theta < polar_start_theta) ||
      (polar_end_theta < polar_mid_theta && polar_mid_theta < (2.0 * PI_DOUBLE))
      )
    {
      direction = DirectionEnum::CLOCKWISE;
      angle_radians = polar_start_theta + ((2.0 * PI_DOUBLE) - polar_end_theta);
    }
  }
  else if (polar_start_theta > polar_end_theta)
  {
    if (
      (polar_start_theta < polar_mid_theta && polar_mid_theta < (2.0 * PI_DOUBLE)) ||
      (0.0 < polar_mid_theta && polar_mid_theta < polar_end_theta)
      )
    {
      direction = DirectionEnum::COUNTERCLOCKWISE;
      angle_radians = polar_end_theta + ((2.0 * PI_DOUBLE) - polar_start_theta);
    }
    else if (polar_end_theta < polar_mid_theta && polar_mid_theta < polar_start_theta)
    {
      direction = DirectionEnum::CLOCKWISE;
      angle_radians = polar_start_theta - polar_end_theta;
    }
  }

  // this doesn't always work..  in rare situations, the angle may be backward
  if (direction == 0 || utilities::is_zero(angle_radians)) return false;

  // Let's check the length against the original length
  // This can trigger simply due to the differing path lengths
  // but also could indicate that our vector calculation above
  // got the direction wrong
  double arc_length = c.radius * angle_radians;

  if (allow_3d_arcs)
  {
    // We may be traveling in 3 space, calculate the arc_length of the spiral
    if (start_point.z != end_point.z)
    {
      arc_length = utilities::hypot(arc_length, end_point.z - start_point.z);
    }
  }
  // Calculate the percent difference of the original path
  double path_difference_percent = utilities::get_percent_change(arc_length, approximate_length);
  if (!utilities::is_zero(path_difference_percent, path_tolerance_percent))
  {
    // So it's possible our vector calculation above got the direction wrong.
    // This can happen if there is a crazy arrangement of points
    // extremely close to eachother.  They have to be close enough to 
    // break our other checks.  However, we may be able to salvage this.
    // see if an arc moving in the opposite direction had the correct length.

    // Find the rest of the angle across the circle
    double test_radians = utilities::abs(angle_radians - 2 * PI_DOUBLE);
    // Calculate the length of that arc
    double test_arc_length = c.radius * test_radians;
    if (allow_3d_arcs)
    {
      // We may be traveling in 3 space, calculate the arc_length of the spiral
      if (start_point.z != end_point.z)
      {
        test_arc_length = utilities::hypot(test_arc_length, end_point.z - start_point.z);
      }
    }
    path_difference_percent = utilities::get_percent_change(test_arc_length,approximate_length);
    if (!utilities::is_zero(path_difference_percent, path_tolerance_percent))
    {
      return false;
    }
    // So, let's set the new length and flip the direction (but not the angle)!
    arc_length = test_arc_length;
    direction = direction == DirectionEnum::COUNTERCLOCKWISE ? DirectionEnum::CLOCKWISE : DirectionEnum::COUNTERCLOCKWISE;
  }

  if (allow_3d_arcs)
  {
    // Ensure the perimeter of the arc is less than that of a full circle
    double perimeter = utilities::hypot(c.radius * 2.0 * PI_DOUBLE, end_point.z - start_point.z);
    if (perimeter <= approximate_length) {
      return false;
    }

  }

  if (direction == 2) {
    angle_radians *= -1.0;
  }
  target_arc.direction = direction;
  target_arc.center.x = c.center.x;
  target_arc.center.y = c.center.y;
  target_arc.center.z = c.center.z;
  target_arc.radius = c.radius;
  target_arc.start_point = start_point;
  target_arc.end_point = end_point;
  target_arc.length = arc_length;
  target_arc.angle_radians = angle_radians;
  target_arc.polar_start_theta = polar_start_theta;
  target_arc.polar_end_theta = polar_end_theta;

  return true;

}

bool arc::try_create_arc(
  const array_list<printer_point>& points,
  arc& target_arc,
  double approximate_length,
  double max_radius_mm,
  double resolution_mm,
  double path_tolerance_percent,
  int min_arc_segments,
  double mm_per_arc_segment,
  double xyz_tolerance,
  bool allow_3d_arcs)
{
  circle test_circle = (circle)target_arc;

  if (!circle::try_create_circle(points, max_radius_mm, resolution_mm, xyz_tolerance, allow_3d_arcs, test_circle))
  {
    return false;
  }
  
  // We could save a bit of processing power and do our firmware compensation here, but we won't be able to track statistics for this easily.
  // moved check to segmented_arc.cpp
  int mid_point_index = ((points.count() - 2) / 2) + 1;
  arc test_arc;
  if (!arc::try_create_arc(test_circle, points[0], points[mid_point_index], points[points.count() - 1], test_arc, approximate_length, resolution_mm, path_tolerance_percent, allow_3d_arcs))
  {
    return false;
  }

  if (arc::are_points_within_slice(test_arc, points))
  {
    target_arc = test_arc;
    return true;
  }
  return false;
}

bool arc::are_points_within_slice(const arc& test_arc, const array_list<printer_point>& points)
{


  // Loop through the points and see if they fit inside of the angles
  double previous_polar = test_arc.polar_start_theta;
  bool will_cross_zero = false;
  bool crossed_zero = false;
  const int point_count = points.count();

  point start_norm((test_arc.start_point.x - test_arc.center.x) / test_arc.radius, (test_arc.start_point.y - test_arc.center.y) / test_arc.radius, 0.0);
  point end_norm((test_arc.end_point.x - test_arc.center.x) / test_arc.radius, (test_arc.end_point.y - test_arc.center.y) / test_arc.radius, 0.0);

  if (test_arc.direction == DirectionEnum::COUNTERCLOCKWISE)
  {
    will_cross_zero = test_arc.polar_start_theta > test_arc.polar_end_theta;
  }
  else
  {
    will_cross_zero = test_arc.polar_start_theta < test_arc.polar_end_theta;
  }

  // Need to see if point 1 to point 2 cross zero
  for (int index = point_count - 2; index < point_count; index++)
  {
    double polar_test;
    if (index < point_count - 1)
    {
      polar_test = test_arc.get_polar_radians(points[index]);
    }
    else
    {
      polar_test = test_arc.polar_end_theta;
    }

    // First ensure the test point is within the arc
    if (test_arc.direction == DirectionEnum::COUNTERCLOCKWISE)
    {
      // Only check to see if we are within the arc if this isn't the endpoint
      if (index < point_count - 1)
      {
        if (will_cross_zero)
        {
          if (!(polar_test > test_arc.polar_start_theta || polar_test < test_arc.polar_end_theta))
          {
            return false;
          }
        }
        else if (!(test_arc.polar_start_theta < polar_test && polar_test < test_arc.polar_end_theta))
        {
          return false;
        }
      }
      // Now make sure the angles are increasing
      if (previous_polar > polar_test)
      {
        if (!will_cross_zero)
        {
          return false;
        }

        // Allow the angle to cross zero once
        if (crossed_zero)
        {
          return false;
        }
        crossed_zero = true;
      }
    }
    else
    {
      if (index < point_count - 1)
      {
        if (will_cross_zero)
        {
          if (!(polar_test < test_arc.polar_start_theta || polar_test > test_arc.polar_end_theta))
          {
            return false;
          }
        }
        else if (!(test_arc.polar_start_theta > polar_test && polar_test > test_arc.polar_end_theta))
        {
          return false;
        }
      }
      // Now make sure the angles are decreasing
      if (previous_polar < polar_test)
      {
        if (!will_cross_zero)
        {
          return false;
        }
        // Allow the angle to cross zero once
        if (crossed_zero)
        {
          return false;
        }
        crossed_zero = true;
      }
    }

    // Now see if the segment intersects either of the vector from the center of the circle to the endpoints of the arc
    if ((index != 1 && ray_intersects_segment(test_arc.center, start_norm, points[index - 1], points[index])) || (index != point_count - 1 && ray_intersects_segment(test_arc.center, end_norm, points[index - 1], points[index])))
      return false;
    previous_polar = polar_test;
  }
  // Ensure that all arcs that cross zero do, and that all arcs that should not did not.
  if (will_cross_zero != crossed_zero)
  {
    return false;
  }

  return true;
}

// return the distance of ray origin to intersection point
bool arc::ray_intersects_segment(const point rayOrigin, const point rayDirection, const printer_point point1, const printer_point point2)
{
  vector v1 = rayOrigin - point1;
  vector v2 = point2 - point1;
  vector v3 = vector(-rayDirection.y, rayDirection.x, 0);

  double dot = dot(v2, v3);
  if (utilities::abs(dot) < 0.000001)
    return false;

  double t1 = vector::cross_product_magnitude(v2, v1) / dot;
  double t2 = dot(v1, v3) / dot;

  if (t1 >= 0.0 && (t2 >= 0.0 && t2 <= 1.0))
    return true;

  return false;
}

#pragma endregion

segmented_shape::segmented_shape(int min_segments, int max_segments, double resolution_mm, double path_tolerance_percnet, unsigned char default_xyz_precision, unsigned char default_e_precision) : points_(max_segments)
{

  set_xyz_precision(default_xyz_precision);
  e_precision_ = default_e_precision;
  max_segments_ = max_segments;
  path_tolerance_percent_ = path_tolerance_percnet;
  resolution_mm_ = resolution_mm / 2.0; // divide by 2 because it is + or - 1/2 of the desired resolution.
  e_relative_ = 0;
  is_shape_ = false;
  // min segments can never be lower than 3 (the default) else there could be no compression.
  if (min_segments < DEFAULT_MIN_SEGMENTS) min_segments_ = DEFAULT_MIN_SEGMENTS;
  else min_segments_ = min_segments;

  original_shape_length_ = 0;
  is_extruding_ = true;
}

segmented_shape::~segmented_shape()
{

}

unsigned char segmented_shape::get_xyz_precision() const
{
  return xyz_precision_;
}

double segmented_shape::get_xyz_tolerance() const
{
  return xyz_tolerance_;
}

unsigned char segmented_shape::get_e_precision() const
{
  return e_precision_;
}

void segmented_shape::set_xyz_precision(unsigned char precision)
{
  xyz_precision_ = precision;
  set_xyz_tolerance_from_precision();
}

void segmented_shape::set_xyz_tolerance_from_precision()
{
  xyz_tolerance_ = utilities::pow(10, -1.0 * static_cast<double>(xyz_precision_));
}

void segmented_shape::reset_precision()
{
  set_xyz_precision(DEFAULT_XYZ_PRECISION);
  e_precision_ = DEFAULT_E_PRECISION;
}

void segmented_shape::update_xyz_precision(unsigned char precision)
{
  if (xyz_precision_ < precision)
  {
    set_xyz_precision(precision);
  }
}

void segmented_shape::update_e_precision(unsigned char precision)
{
  if (e_precision_ < precision)
  {
    e_precision_ = precision;
  }

}

bool segmented_shape::is_extruding()
{
  return is_extruding_;
}

segmented_shape& segmented_shape::operator=(const segmented_shape& obj)
{
  points_.clear();
  if (obj.max_segments_ != max_segments_)
  {
    max_segments_ = obj.max_segments_;

    points_.resize(max_segments_);
  }
  points_.copy(obj.points_);

  original_shape_length_ = obj.original_shape_length_;
  e_relative_ = obj.e_relative_;
  is_shape_ = obj.is_shape_;
  max_segments_ = obj.max_segments_;
  resolution_mm_ = obj.resolution_mm_;
  return *this;
}

int segmented_shape::get_num_segments()
{
  return points_.count();
}

double segmented_shape::get_shape_length()
{
  return original_shape_length_;
}

double segmented_shape::get_shape_e_relative()
{
  return e_relative_;
}

void segmented_shape::clear()
{
  points_.clear();
  is_shape_ = false;
  e_relative_ = 0;
  original_shape_length_ = 0;
}
bool segmented_shape::is_shape() const
{
  // return the pre-calculated value.  This should be updated by the plugin
  return is_shape_;
}
void segmented_shape::set_is_shape(bool value)
{
  is_shape_ = value;
}

int segmented_shape::get_min_segments()
{
  return min_segments_;
}
int segmented_shape::get_max_segments()
{
  return max_segments_;
}

double segmented_shape::get_resolution_mm()
{
  return resolution_mm_;
}

double segmented_shape::get_path_tolerance_percent()
{
  return path_tolerance_percent_;
}

void segmented_shape::set_resolution_mm(double resolution_mm)
{
  resolution_mm_ = resolution_mm;

}
printer_point segmented_shape::pop_front()
{
  return points_.pop_front();
}
printer_point segmented_shape::pop_back()
{
  return points_.pop_back();
}

bool segmented_shape::try_add_point(printer_point p, double e_relative)
{
  throw std::exception();
}

std::string segmented_shape::get_shape_gcode_absolute(double e_abs_start)
{
  throw std::exception();
}

std::string segmented_shape::get_shape_gcode_relative()
{
  throw std::exception();
}