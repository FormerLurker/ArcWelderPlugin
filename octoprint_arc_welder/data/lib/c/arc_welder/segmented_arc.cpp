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

printer_point segmented_arc::pop_front(double e_relative)
{
  e_relative_ -= e_relative;
  if (points_.count() == get_min_segments())
  {
    set_is_shape(false);
  }
  return points_.pop_front();
}
printer_point segmented_arc::pop_back(double e_relative)
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
double segmented_arc::get_shape_length()
{
  return current_arc_.length;
}
bool segmented_arc::try_add_point(printer_point p)
{

  bool point_added = false;
  // if we don't have enough segnemts to check the shape, just add
  if (points_.count() > get_max_segments() - 1)
  {
    // Too many points, we can't add more
    return false;
  }
  if (points_.count() > 0)
  {
    printer_point p1 = points_[points_.count() - 1];
    if (!allow_3d_arcs_ && !utilities::is_equal(p1.z, p.z))
    {
      // Z axis changes aren't allowed
      return false;
    }

    if (utilities::is_zero(p.distance))
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
    original_shape_length_ += p.distance;
  }
  else
  {
    // if we're here, we need to see if the new point can be included in the shape
    point_added = try_add_point_internal_(p);
  }
  if (point_added)
  {

    if (points_.count() > 1)
    {
      // Only add the relative distance to the second point on up.
      e_relative_ += p.e_relative;
    }
    //std::cout << " success - " << points_.count() << " points.\n";
  }
  else if (points_.count() < get_min_segments() && points_.count() > 1)
  {
    // If we haven't added a point, and we have exactly min_segments_,
    // pull off the initial arc point and try again
    points_.pop_front();
    // Get the new initial point
    printer_point new_initial_point = points_[0];
    // The length and e_relative distance of the arc has been reduced 
    // by removing the front point.  Calculate this.
    original_shape_length_ -= new_initial_point.distance;
    e_relative_ -= new_initial_point.e_relative;
    return try_add_point(p);
  }

  return point_added;
}

bool segmented_arc::try_add_point_internal_(printer_point p)
{
  // If we don't have enough points (at least min_segments) return false
  if (points_.count() < get_min_segments() - 1)
    return false;

  // Create a test circle
  circle target_circle;

  // the circle is new..  we have to test it now, which is expensive :(
  points_.push_back(p);
  double previous_shape_length = original_shape_length_;
  original_shape_length_ += p.distance;
  arc original_arc = current_arc_;
  
  if (arc::try_create_arc(points_, current_arc_, original_shape_length_, e_relative_ + p.e_relative, max_radius_mm_, resolution_mm_, path_tolerance_percent_, min_arc_segments_, mm_per_arc_segment_, get_xyz_tolerance(), allow_3d_arcs_))
  {
    bool abort_arc = false;
    if (min_arc_segments_ > 0 && mm_per_arc_segment_ > 0)
    {
      // Apply firmware compensation
      // See how many arcs will be interpolated
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
    if (!abort_arc)
    {
      if (utilities::is_zero(current_arc_.get_i(), get_xyz_tolerance()) && utilities::is_zero(current_arc_.get_j(), get_xyz_tolerance()))
      {
        // I and J are both 0, which is invalid!  Abort!
        abort_arc = true;
      }
      else if (current_arc_.length < get_xyz_tolerance())
      {
        // the arc length is below our tolerance, abort!
        abort_arc = true;
      }
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


bool segmented_arc::get_next_arc(arc& arc, int& num_points, bool test_current_arc)
{
  num_points = 0;
  if (points_.count() < 3)
  {
    points_.pop_front();
    return false;
  }
  // Note:  We have the stored total e_relative value for the entire arc, and we have to continue to 
  // update it as we return arcs and pop points.

  // Test for the most common case, which is a valid arc.  This will speed things up a ton for very large circles.
  if (test_current_arc && arc::are_points_within_slice(current_arc_, points_, points_.count()))
  {
    arc = current_arc_;
    arc.e_relative = e_relative_;
    arc.offset_e = points_[points_.count() - 1].offset_e;
    num_points = points_.count();
    points_.clear();
    e_relative_ = 0;
    original_shape_length_ = 0;
    is_shape_ = false;
    return true;
  }

  // Now the difficult part.  Because the arc did not pass testing, we need to get the next arc that can be created, and update all of the appropriate
  // internals.

  if (arc::try_create_first_arc(points_, arc, num_points, max_radius_mm_, resolution_mm_, path_tolerance_percent_, min_arc_segments_, mm_per_arc_segment_, get_xyz_tolerance(), allow_3d_arcs_))
  {
    // We need to adjust e_relative based on the arc points we were able to add.
    e_relative_ = e_relative_ - arc.e_relative;
    original_shape_length_ -= arc.original_shape_length;
    
    // Remove the points we've added to the arc
    for (int index = 0; index < num_points-1; index++)
    {
      points_.pop_front();
    }

    return true;
  }
  
  e_relative_ -= points_[0].e_relative;
  points_.pop_front();
  
  // It didn't work :(
  return false;

}

std::string segmented_arc::get_shape_gcode(const arc& arc, bool is_relative, double f, unsigned char xyz_precision, unsigned char e_precision, double xyz_tolerance)
{
  bool has_e = arc.e_relative != 0;
  double e;
  if (is_relative)
  {
    // If we're in relative mode, use the e_relative value of the arc
    e = arc.e_relative;    
  }
  else
  {
    e = arc.offset_e;
  }
  std::string gcode;
  // Calculate gcode size
  bool has_f = utilities::greater_than_or_equal(f, 1);
  bool has_z = !utilities::is_equal(arc.start_point.z, arc.end_point.z, xyz_tolerance );
  gcode.reserve(96);

  if (arc.angle_radians < 0)
  {
    gcode += "G2";
  }
  else
  {
    gcode += "G3";

  }
  // Add X, Y, I and J
  gcode += " X";
  gcode += utilities::dtos(arc.end_point.x, xyz_precision);

  gcode += " Y";
  gcode += utilities::dtos(arc.end_point.y, xyz_precision);

  if (has_z)
  {
    gcode += " Z";
    gcode += utilities::dtos(arc.end_point.z, xyz_precision);
  }

  // Output I and J, but do NOT check for 0.  
  // Simplify 3d has issues visualizing G2/G3 with 0 for I or J
  // and until it is fixed, it is not worth the hassle.
  double i = arc.get_i();
  gcode += " I";
  gcode += utilities::dtos(i, xyz_precision);

  double j = arc.get_j();
  gcode += " J";
  gcode += utilities::dtos(j, xyz_precision);

  // Add E if it appears
  if (has_e)
  {
    gcode += " E";
    gcode += utilities::dtos(e, e_precision);
  }

  // Add F if it appears
  if (has_f)
  {
    gcode += " F";
    gcode += utilities::dtos(f, 0);
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