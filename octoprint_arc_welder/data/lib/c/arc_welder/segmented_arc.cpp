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
//#include <iomanip>
//#include <sstream>
#include <stdio.h>
#include <cmath>

segmented_arc::segmented_arc() : segmented_shape(DEFAULT_MIN_SEGMENTS, DEFAULT_MAX_SEGMENTS, DEFAULT_RESOLUTION_MM, ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT)
{
  max_radius_mm_ = DEFAULT_MAX_RADIUS_MM;
  min_arc_segments_ = DEFAULT_MIN_ARC_SEGMENTS,
  mm_per_arc_segment_ = DEFAULT_MM_PER_ARC_SEGMENT;
  allow_3d_arcs_ = DEFAULT_ALLOW_3D_ARCS;
  num_firmware_compensations_ = 0;
}

segmented_arc::segmented_arc(
  int min_segments,
  int max_segments,
  double resolution_mm,
  double path_tolerance_percent,
  double max_radius_mm,
  int min_arc_segments,
  double mm_per_arc_segment,
  bool allow_3d_arcs,
  unsigned char default_xyz_precision,
  unsigned char default_e_precision
) : segmented_shape(min_segments, max_segments, resolution_mm, path_tolerance_percent, default_xyz_precision, default_e_precision)
{
  max_radius_mm_ = max_radius_mm;
  if (max_radius_mm > DEFAULT_MAX_RADIUS_MM) {
    max_radius_mm_ = DEFAULT_MAX_RADIUS_MM;
  }
  mm_per_arc_segment_ = mm_per_arc_segment;
  if (mm_per_arc_segment_ < 0 || utilities::is_zero(mm_per_arc_segment_))
  {
    mm_per_arc_segment = 0;
  }
  min_arc_segments_ = min_arc_segments;
  if (min_arc_segments_ < 0)
  {
    min_arc_segments_ = 0;
  }
  allow_3d_arcs_ = allow_3d_arcs;
  num_firmware_compensations_ = 0;
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

int segmented_arc::get_min_arc_segments() const
{
  return min_arc_segments_;
}

int segmented_arc::get_num_firmware_compensations() const
{
  return num_firmware_compensations_;
}

double segmented_arc::get_mm_per_arc_segment() const
{
  return mm_per_arc_segment_;
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
    if (allow_3d_arcs_) {
      // If we can draw arcs in 3 space, add in the distance of the z axis changes
      distance = utilities::get_cartesian_distance(p1.x, p1.y, p1.z, p.x, p.y, p.z);
    }
    else {
      distance = utilities::get_cartesian_distance(p1.x, p1.y, p.x, p.y);
      if (!utilities::is_equal(p1.z, p.z))
      {
        // Z axis changes aren't allowed
        return false;
      }
    }

    if (utilities::is_zero(distance))
    {
      // there must be some distance between the points
      // to make an arc.
      return false;
    }

  }

  if (points_.count() < get_min_segments() - 1)
  {
    point_added = true;
    points_.push_back(p);
    original_shape_length_ += distance;
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
    if (allow_3d_arcs_) {
      original_shape_length_ -= utilities::get_cartesian_distance(old_initial_point.x, old_initial_point.y, old_initial_point.z, new_initial_point.x, new_initial_point.y, new_initial_point.z);
    }
    else {
      original_shape_length_ -= utilities::get_cartesian_distance(old_initial_point.x, old_initial_point.y, new_initial_point.x, new_initial_point.y);
    }

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
  arc original_arc = current_arc_;
  if (arc::try_create_arc(points_, current_arc_, original_shape_length_, max_radius_mm_, resolution_mm_, path_tolerance_percent_, min_arc_segments_, mm_per_arc_segment_, get_xyz_tolerance(), allow_3d_arcs_))
  {
    // See how many arcs will be interpolated
    bool abort_arc = false;
    if (min_arc_segments_ > 0 && mm_per_arc_segment_ > 0)
    {
      double circumference = 2.0 * PI_DOUBLE * current_arc_.radius;
      int num_segments = (int)std::floor(circumference / min_arc_segments_);
      if (num_segments < min_arc_segments_) {
        //num_segments = (int)std::ceil(circumference/approximate_length) * (int)std::ceil(approximate_length / mm_per_arc_segment);
        num_segments = (int)std::floor(circumference / original_shape_length_);
        if (num_segments < min_arc_segments_) {
          abort_arc = true;
          num_firmware_compensations_++; 
        }
      }
    }
    // check for I=0 and J=0
    if (!abort_arc && utilities::is_zero(current_arc_.get_i(), get_xyz_tolerance()) && utilities::is_zero(current_arc_.get_j(), get_xyz_tolerance()))
    {
      abort_arc = true;
    }
    if (!abort_arc && current_arc_.length < get_xyz_tolerance())
    {
      abort_arc = true;
    }
    if (abort_arc)
    {
      // This arc has been cancelled either due to firmware correction,
      // or because both I and J == 0
      current_arc_ = original_arc;
    }
    else if (!abort_arc)
    {
      if (!is_shape())
      {
        set_is_shape(true);
      }
      return true;
    }
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
  std::string gcode;
  // Calculate gcode size
  bool has_f = utilities::greater_than_or_equal(f, 1);
  bool has_z = allow_3d_arcs_ && !utilities::is_equal(
    current_arc_.start_point.z, current_arc_.end_point.z, get_xyz_tolerance()
  );
  gcode.reserve(96);

  if (current_arc_.angle_radians < 0)
  {
    gcode += "G2";
  }
  else
  {
    gcode += "G3";

  }
  // Add X, Y, I and J
  gcode += " X";
  gcode += utilities::dtos(current_arc_.end_point.x, get_xyz_precision());
  
  gcode += " Y";
  gcode += utilities::dtos(current_arc_.end_point.y, get_xyz_precision());
  
  if (has_z)
  {
    gcode += " Z";
    gcode += utilities::dtos(current_arc_.end_point.z, get_xyz_precision());
  }

  // Output I and J, but do NOT check for 0.  
  // Simplify 3d has issues visualizing G2/G3 with 0 for I or J
  // and until it is fixed, it is not worth the hassle.
  double i = current_arc_.get_i();
  gcode += " I";
  gcode += utilities::dtos(i, get_xyz_precision());

  double j = current_arc_.get_j();
  gcode += " J";
  gcode += utilities::dtos(j, get_xyz_precision());

  // Add E if it appears
  if (has_e)
  {
    gcode += " E";
    gcode += utilities::dtos(e, get_e_precision());
  }

  // Add F if it appears
  if (has_f)
  {
    gcode += " F";
    gcode += utilities::dtos(f, get_xyz_precision());
  }

  return gcode;

}

/*
* This is an older implementation using ostringstream.  It is substantially slower.
* Keep this around in case there are problems with the custom dtos function
std::string segmented_arc::get_shape_gcode_(bool has_e, double e, double f) const
{
  static std::ostringstream gcode;
  gcode.str("");
  gcode << std::fixed;

  if (current_arc_.angle_radians < 0)
  {
    gcode << "G2";
  }
  else
  {
    gcode << "G3";

  }
  gcode << std::setprecision(get_xyz_precision());
  // Add X, Y, I and J
  if (!utilities::is_zero(current_arc_.end_point.x, get_xyz_precision()))
  {
    gcode << " X" << current_arc_.end_point.x;
  }

  if (!utilities::is_zero(current_arc_.end_point.y, get_xyz_precision()))
  {
    gcode << " Y" << current_arc_.end_point.y;
  }

  if (allow_3d_arcs_)
  {
    // We may need to add a z coordinate
    double z_initial = current_arc_.start_point.z;
    double z_final = current_arc_.end_point.z;
    if (!utilities::is_equal(z_initial, z_final, get_xyz_tolerance()))
    {
      // The z axis has changed within the precision of the gcode coordinates
      gcode << " Z" << current_arc_.end_point.z;
    }
  }

  // Output I and J, but do NOT check for 0.
  // Simplify 3d has issues visualizing G2/G3 with 0 for I or J
  // and until it is fixed, it is not worth the hassle.
  double i = current_arc_.get_i();
  gcode << " I" << i;

  double j = current_arc_.get_j();
  gcode << " J" << j;

  // Add E if it appears
  if (has_e)
  {
    gcode << std::setprecision(get_e_precision());
    gcode << " E" << e;
  }

  // Add F if it appears
  if (utilities::greater_than_or_equal(f, 1))
  {
    gcode << std::setprecision(0);
    gcode << " F" << f;
  }

  return gcode.str();

}
*/