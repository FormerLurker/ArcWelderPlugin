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
#if _MSC_VER > 1200
#define _CRT_SECURE_NO_DEPRECATE
#endif

#include "arc_welder.h"
#include <vector>
#include <sstream>
#include "utilities.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <version.h>




arc_welder::arc_welder(arc_welder_args args) : current_arc_(
        DEFAULT_MIN_SEGMENTS,
        args.buffer_size,
        args.resolution_mm,
        args.path_tolerance_percent,
        args.max_radius_mm,
        args.min_arc_segments,
        args.mm_per_arc_segment,
        args.allow_3d_arcs,
        args.default_xyz_precision,
        args.default_e_precision,
        args.max_gcode_length
    ),
    segment_statistics_(
        segment_statistic_lengths,
        segment_statistic_lengths_count,
        args.log
    ),
    segment_retraction_statistics_(
        segment_statistic_lengths,
        segment_statistic_lengths_count,
        args.log
    ),
    travel_statistics_(
        segment_statistic_lengths,
        segment_statistic_lengths_count,
        args.log
    )
{
    p_logger_ = args.log;
    debug_logging_enabled_ = false;
    info_logging_enabled_ = false;
    error_logging_enabled_ = false;
    verbose_logging_enabled_ = false;

    logger_type_ = 0;
    resolution_mm_ = args.resolution_mm;
    progress_callback_ = args.callback;
    verbose_output_ = false;
    source_path_ = args.source_path;
    target_path_ = args.target_path;
    gcode_position_args_ = get_args_(args.g90_g91_influences_extruder, args.buffer_size);
    allow_3d_arcs_ = args.allow_3d_arcs;
    allow_travel_arcs_ = args.allow_travel_arcs;
    allow_dynamic_precision_ = args.allow_dynamic_precision;
    extrusion_rate_variance_percent_ = args.extrusion_rate_variance_percent;
    lines_processed_ = 0;
    gcodes_processed_ = 0;
    file_size_ = 0;
    notification_period_seconds_ = args.notification_period_seconds;
    last_gcode_line_written_ = 0;
    points_compressed_ = 0;
    arcs_created_ = 0;
    arcs_aborted_by_flow_rate_ = 0;
    waiting_for_arc_ = false;
    previous_feedrate_ = -1;
    gcode_position_args_.set_num_extruders(8);
    previous_extrusion_rate_ = 0;
    box_encoding_ = args.box_encoding;
    for (int index = 0; index < 8; index++)
    {
        gcode_position_args_.retraction_lengths[0] = .0001;
        gcode_position_args_.z_lift_heights[0] = 0.001;
        gcode_position_args_.x_firmware_offsets[0] = 0.0;
        gcode_position_args_.y_firmware_offsets[0] = 0.0;
    }

    // We don't care about the printer settings, except for g91 influences extruder.

    p_source_position_ = new gcode_position(gcode_position_args_);
}

gcode_position_args arc_welder::get_args_(bool g90_g91_influences_extruder, int buffer_size)
{
  gcode_position_args args;
  // Configure gcode_position_args
  args.g90_influences_extruder = g90_g91_influences_extruder;
  if (buffer_size < 2)
  {
    buffer_size = 2;
  }
  args.position_buffer_size = buffer_size;
  args.autodetect_position = true;
  args.home_x = 0;
  args.home_x_none = true;
  args.home_y = 0;
  args.home_y_none = true;
  args.home_z = 0;
  args.home_z_none = true;
  args.shared_extruder = true;
  args.zero_based_extruder = true;


  args.default_extruder = 0;
  args.xyz_axis_default_mode = "absolute";
  args.e_axis_default_mode = "absolute";
  args.units_default = "millimeters";
  args.location_detection_commands = std::vector<std::string>();
  args.is_bound_ = false;
  args.is_circular_bed = false;
  args.x_min = -9999;
  args.x_max = 9999;
  args.y_min = -9999;
  args.y_max = 9999;
  args.z_min = -9999;
  args.z_max = 9999;
  return args;
}

arc_welder::~arc_welder()
{
  delete p_source_position_;
}

void arc_welder::set_logger_type(int logger_type)
{
  logger_type_ = logger_type;
}

void arc_welder::reset()
{
  p_logger_->log(logger_type_, log_levels::DEBUG, "Resetting all tracking variables.");
  lines_processed_ = 0;
  gcodes_processed_ = 0;
  last_gcode_line_written_ = 0;
  file_size_ = 0;
  points_compressed_ = 0;
  arcs_created_ = 0;
  waiting_for_arc_ = false;
}

long arc_welder::get_file_size(const std::string& file_path)
{
  // Todo:  Fix this function.  This is a pretty weak implementation :(
  std::ifstream file(file_path.c_str(), std::ios::in | std::ios::binary);
  const long l = (long)file.tellg();
  file.seekg(0, std::ios::end);
  const long m = (long)file.tellg();
  file.close();
  return (m - l);
}

double arc_welder::get_next_update_time() const
{
  return clock() + (notification_period_seconds_ * CLOCKS_PER_SEC);
}

double arc_welder::get_time_elapsed(double start_clock, double end_clock)
{
  return static_cast<double>(end_clock - start_clock) / CLOCKS_PER_SEC;
}

arc_welder_results arc_welder::process()
{
  arc_welder_results results;
  p_logger_->log(logger_type_, log_levels::DEBUG, "Configuring logging settings.");
  verbose_logging_enabled_ = p_logger_->is_log_level_enabled(logger_type_, log_levels::VERBOSE);
  debug_logging_enabled_ = p_logger_->is_log_level_enabled(logger_type_, log_levels::DEBUG);
  info_logging_enabled_ = p_logger_->is_log_level_enabled(logger_type_, log_levels::INFO);
  error_logging_enabled_ = p_logger_->is_log_level_enabled(logger_type_, log_levels::ERROR);

  std::stringstream stream;
  // reset tracking variables
  reset();
  // local variable to hold the progress update return.  If it's false, we will exit.
  bool continue_processing = true;

  p_logger_->log(logger_type_, log_levels::DEBUG, "Configuring progress updates.");
  int read_lines_before_clock_check = 1000;
  double next_update_time = get_next_update_time();
  const clock_t start_clock = clock();
  p_logger_->log(logger_type_, log_levels::DEBUG, "Getting source file size.");
  file_size_ = get_file_size(source_path_);
  stream.clear();
  stream.str("");
  stream << "Source file size: " << file_size_;
  p_logger_->log(logger_type_, log_levels::DEBUG, stream.str());

  // Determine if we need to overwrite the source file
  bool overwrite_source_file = false;
  std::string temp_file_path;
  if (source_path_ == target_path_)
  {
    overwrite_source_file = true;
    if (!utilities::get_temp_file_path_for_file(source_path_, temp_file_path))
    {
      results.success = false;
      results.message = "The source and target path are the same, but a temporary file path could not be created.  Are the paths empty?";
      p_logger_->log_exception(logger_type_, results.message);
      return results;
    }

    stream.clear();
    stream.str("");
    stream << "Source and target path are the same.  The source file will be overwritten.  Temporary file path: " << temp_file_path;
    p_logger_->log(logger_type_, log_levels::DEBUG, stream.str());
    target_path_ = temp_file_path;
  }

  // Create the source file read stream and target write stream
  std::ifstream gcodeFile;
  p_logger_->log(logger_type_, log_levels::DEBUG, "Opening the source file for reading.");
  gcodeFile.open(source_path_.c_str(), std::ifstream::in);
  if (!gcodeFile.is_open())
  {
    results.success = false;
    results.message = "Unable to open the source file.";
    p_logger_->log_exception(logger_type_, results.message);
    return results;
  }
  p_logger_->log(logger_type_, log_levels::DEBUG, "Source file opened successfully.");

  p_logger_->log(logger_type_, log_levels::DEBUG, "Opening the target file for writing.");

  output_file_.open(target_path_.c_str(), std::ios_base::binary | std::ios_base::out);
  if (!output_file_.is_open())
  {
    results.success = false;
    results.message = "Unable to open the target file.";
    p_logger_->log_exception(logger_type_, results.message);
    gcodeFile.close();
    return results;
  }

  p_logger_->log(logger_type_, log_levels::DEBUG, "Target file opened successfully.");
  std::string line;
  int lines_with_no_commands = 0;
  parsed_command cmd;
  // Communicate every second
  p_logger_->log(logger_type_, log_levels::DEBUG, "Sending initial progress update.");
  continue_processing = on_progress_(get_progress_(static_cast<long>(gcodeFile.tellg()), static_cast<double>(start_clock)));
  p_logger_->log(logger_type_, log_levels::DEBUG, "Processing source file.");

  bool arc_Welder_comment_added = false;
  while (std::getline(gcodeFile, line) && continue_processing)
  {
    lines_processed_++;
    // Check the first line of gcode and see if it = ;FLAVOR:UltiGCode
// This comment MUST be preserved as the first line for ultimakers, else things won't work
    if (lines_processed_ == 1)
    {
      bool isUltiGCode = line == ";FLAVOR:UltiGCode";
      bool isPrusaSlicer = line.rfind("; generated by PrusaSlicer", 0) == 0;
      if (isUltiGCode || isPrusaSlicer)
      {
        write_gcode_to_file(line);
      }
      add_arcwelder_comment_to_target();
      if (isUltiGCode || isPrusaSlicer)
      {
        lines_with_no_commands++;
        continue;
      }
    }


    cmd.clear();
    if (verbose_logging_enabled_)
    {
      stream.clear();
      stream.str("");
      stream << "Parsing: " << line;
      p_logger_->log(logger_type_, log_levels::VERBOSE, stream.str());
    }
    parser_.try_parse_gcode(line.c_str(), cmd, true);
    bool has_gcode = false;
    if (cmd.gcode.length() > 0)
    {
      has_gcode = true;
      gcodes_processed_++;
    }
    else
    {
      lines_with_no_commands++;
    }

    // Always process the command through the printer, even if no command is found
    // This is important so that comments can be analyzed
    //std::cout << "stabilization::process_file - updating position...";
    process_gcode(cmd, false, false);

    // Only continue to process if we've found a command and either a progress_callback_ is supplied, or debug loggin is enabled.
    if (has_gcode)
    {
      if ((lines_processed_ % read_lines_before_clock_check) == 0 && next_update_time < clock())
      {
        if (verbose_logging_enabled_)
        {
          p_logger_->log(logger_type_, log_levels::VERBOSE, "Sending progress update.");
        }
        continue_processing = on_progress_(get_progress_(static_cast<long>(gcodeFile.tellg()), static_cast<double>(start_clock)));
        next_update_time = get_next_update_time();
      }
    }
  }

  if (current_arc_.is_shape() && waiting_for_arc_)
  {
    p_logger_->log(logger_type_, log_levels::DEBUG, "Processing the final line.");
    process_gcode(cmd, true, false);
  }
  p_logger_->log(logger_type_, log_levels::DEBUG, "Writing all unwritten gcodes to the target file.");
  write_unwritten_gcodes_to_file();

  p_logger_->log(logger_type_, log_levels::DEBUG, "Fetching the final progress struct.");

  arc_welder_progress final_progress = get_progress_(static_cast<long>(file_size_), static_cast<double>(start_clock));
  if (debug_logging_enabled_)
  {
    p_logger_->log(logger_type_, log_levels::DEBUG, "Sending final progress update message.");
  }
  on_progress_(final_progress);

  p_logger_->log(logger_type_, log_levels::DEBUG, "Closing source and target files.");
  output_file_.close();
  gcodeFile.close();

  if (overwrite_source_file)
  {
    stream.clear();
    stream.str("");
    stream << "Deleting the original source file at '" << source_path_ << "'.";
    p_logger_->log(logger_type_, log_levels::DEBUG, stream.str());
    stream.clear();
    stream.str("");
    std::remove(source_path_.c_str());
    stream << "Renaming temporary file at '" << target_path_ << "' to '" << source_path_ << "'.";
    p_logger_->log(0, log_levels::DEBUG, stream.str());
    std::rename(target_path_.c_str(), source_path_.c_str());
  }

  results.success = continue_processing;
  results.cancelled = !continue_processing;
  results.progress = final_progress;
  p_logger_->log(logger_type_, log_levels::DEBUG, "Returning processing results.");

  return results;
}

bool arc_welder::on_progress_(const arc_welder_progress& progress)
{
  if (progress_callback_ != NULL)
  {
    return progress_callback_(progress, p_logger_, logger_type_);
  }
  else if (info_logging_enabled_)
  {
    p_logger_->log(logger_type_, log_levels::INFO, progress.str());
  }

  return true;
}

arc_welder_progress arc_welder::get_progress_(long source_file_position, double start_clock)
{
  arc_welder_progress progress;
  progress.gcodes_processed = gcodes_processed_;
  progress.lines_processed = lines_processed_;
  progress.points_compressed = points_compressed_;
  progress.arcs_created = arcs_created_;
  progress.arcs_aborted_by_flow_rate = arcs_aborted_by_flow_rate_;
  progress.source_file_position = source_file_position;
  progress.target_file_size = static_cast<long>(output_file_.tellp());
  progress.source_file_size = file_size_;
  long bytesRemaining = file_size_ - static_cast<long>(source_file_position);
  progress.percent_complete = static_cast<double>(source_file_position) / static_cast<double>(file_size_) * 100.0;
  progress.seconds_elapsed = get_time_elapsed(start_clock, clock());
  double bytesPerSecond = static_cast<double>(source_file_position) / progress.seconds_elapsed;
  progress.seconds_remaining = bytesRemaining / bytesPerSecond;
  
  if (source_file_position > 0) {
    progress.compression_ratio = (static_cast<float>(source_file_position) / static_cast<float>(progress.target_file_size));
    progress.compression_percent = (1.0 - (static_cast<float>(progress.target_file_size) / static_cast<float>(source_file_position))) * 100.0f;
  }
  else {
    progress.compression_ratio = 0;
    progress.compression_percent = 0;
  }
  progress.num_firmware_compensations = current_arc_.get_num_firmware_compensations();
  progress.num_gcode_length_exceptions = current_arc_.get_num_gcode_length_exceptions();
  progress.segment_statistics = segment_statistics_;
  progress.segment_retraction_statistics = segment_retraction_statistics_;
  progress.travel_statistics = travel_statistics_;
  progress.box_encoding = box_encoding_;
  return progress;

}

int arc_welder::process_gcode(parsed_command cmd, bool is_end, bool is_reprocess)
{

  
  // Update the position for the source gcode file
  p_source_position_->update(cmd, lines_processed_, gcodes_processed_, -1);
  position* p_cur_pos = p_source_position_->get_current_position_ptr();
  position* p_pre_pos = p_source_position_->get_previous_position_ptr();
  bool is_previous_extruder_relative = p_pre_pos->is_extruder_relative;
  extruder extruder_current = p_cur_pos->get_current_extruder();
  extruder previous_extruder = p_pre_pos->get_current_extruder();

  // Determine if this is a G0, G1, G2 or G3
  bool is_g0_g1 = cmd.command == "G0" || cmd.command == "G1";
  bool is_g2_g3 = cmd.command == "G2" || cmd.command == "G3";
  //std::cout << lines_processed_ << " - " << cmd.gcode << ", CurrentEAbsolute: " << cur_extruder.e <<", ExtrusionLength: " << cur_extruder.extrusion_length << ", Retraction Length: " << cur_extruder.retraction_length << ", IsExtruding: " << cur_extruder.is_extruding << ", IsRetracting: " << cur_extruder.is_retracting << ".\n";

  int lines_written = 0;
  // see if this point is an extrusion

  bool arc_added = false;
  bool clear_shapes = false;
  double movement_length_mm = 0;
  bool is_extrusion = extruder_current.e_relative > 0;
  bool is_retraction = extruder_current.e_relative < 0;
  bool is_travel = !(is_extrusion || is_retraction) && (is_g0_g1 || is_g2_g3);
  
  // Update the source file statistics
  if (p_cur_pos->has_xy_position_changed)
  {
    // If this is a g2/g3 command, we need to do a bit more to get the length of the arc.
      // The movement_length_mm variable will contain the chord length, which we will need
    if (is_g2_g3)
    {
      // Determine the radius of the arc, which is necessary to calculate the arc length from the chord length.
      double i = 0;
      double j = 0;
      double r = 0;
      // Iterate through the parameters and fill in I, J and R;
      for (std::vector<parsed_command_parameter>::iterator it = cmd.parameters.begin(); it != cmd.parameters.end(); ++it)
      {
        switch ((*it).name[0])
        {
        case 'I':
          i = (*it).double_precision;
          break;
        case 'J':
          j = (*it).double_precision;
          break;
          // Note that the R form isn't fully implemented!
        case 'R':
          r = (*it).double_precision;
          break;
        }
      }

      // Calculate R
      if (r == 0)
      {
        r = utilities::sqrt(i * i + j * j);
      }
      // Now we know the radius and the chord length;
      movement_length_mm = utilities::get_arc_distance(p_pre_pos->x, p_pre_pos->y, p_pre_pos->z, p_cur_pos->x, p_cur_pos->y, p_cur_pos->z, i, j, r, p_cur_pos->command.command == "G2");

    }
    else if (allow_3d_arcs_) {
      movement_length_mm = utilities::get_cartesian_distance(p_pre_pos->x, p_pre_pos->y, p_pre_pos->z, p_cur_pos->x, p_cur_pos->y, p_cur_pos->z);
    }
    else {
      movement_length_mm = utilities::get_cartesian_distance(p_pre_pos->x, p_pre_pos->y, p_cur_pos->x, p_cur_pos->y);
    }

    if (movement_length_mm > 0)
    {
      if (!is_reprocess)
      {
        if (is_extrusion)
        {
          segment_statistics_.update(movement_length_mm, true);
        }
        else if (is_retraction)
        {
            segment_retraction_statistics_.update(movement_length_mm, true);
        }
        else if (allow_travel_arcs_ && is_travel)
        {
          travel_statistics_.update(movement_length_mm, true);
        }

      }
    }
  }

  // calculate the extrusion rate (mm/mm) and see how much it changes
  double mm_extruded_per_mm_travel = 0;
  double extrusion_rate_change_percent = 0;
  bool aborted_by_flow_rate = false;
  if (extrusion_rate_variance_percent_ != 0)
  {
      // TODO:  MAKE SURE THIS WORKS FOR TRANSITIONS FROM TRAVEL TO NON TRAVEL MOVES
      if (movement_length_mm > 0 && (is_extrusion || is_retraction))
      {
          mm_extruded_per_mm_travel = extruder_current.e_relative / movement_length_mm;
          if (previous_extrusion_rate_ > 0)
          {
              extrusion_rate_change_percent = utilities::abs(utilities::get_percent_change(previous_extrusion_rate_, mm_extruded_per_mm_travel));
          }
      }
      if (previous_extrusion_rate_ != 0 && utilities::greater_than(extrusion_rate_change_percent, extrusion_rate_variance_percent_))
      {
          arcs_aborted_by_flow_rate_++;
          aborted_by_flow_rate = true;
      }
  }
  

  // We need to make sure the printer is using absolute xyz, is extruding, and the extruder axis mode is the same as that of the previous position
  // TODO: Handle relative XYZ axis.  This is possible, but maybe not so important.
  
  if (allow_dynamic_precision_ && is_g0_g1)
  {
    for (std::vector<parsed_command_parameter>::iterator it = cmd.parameters.begin(); it != cmd.parameters.end(); ++it)
    {
      switch ((*it).name[0])
      {
      case 'X':
      case 'Y':
      case 'Z':
        current_arc_.update_xyz_precision((*it).double_precision);
        break;
      case 'E':
        current_arc_.update_e_precision((*it).double_precision);
        break;
      }
    }
  }

  bool z_axis_ok = allow_3d_arcs_ ||
    utilities::is_equal(p_cur_pos->z, p_pre_pos->z);
  
  if (
    !is_end && cmd.is_known_command && !cmd.is_empty && (
      is_g0_g1 && z_axis_ok &&
      utilities::is_equal(p_cur_pos->x_offset, p_pre_pos->x_offset) &&
      utilities::is_equal(p_cur_pos->y_offset, p_pre_pos->y_offset) &&
      utilities::is_equal(p_cur_pos->z_offset, p_pre_pos->z_offset) &&
      utilities::is_equal(p_cur_pos->x_firmware_offset, p_pre_pos->x_firmware_offset) &&
      utilities::is_equal(p_cur_pos->y_firmware_offset, p_pre_pos->y_firmware_offset) &&
      utilities::is_equal(p_cur_pos->z_firmware_offset, p_pre_pos->z_firmware_offset) &&
      (previous_extrusion_rate_ == 0 || utilities::less_than_or_equal(extrusion_rate_change_percent, extrusion_rate_variance_percent_)) &&
      !p_cur_pos->is_relative &&
      (
        !waiting_for_arc_ ||
        extruder_current.is_extruding ||
        extruder_current.is_retracting ||
        // Test for travel conversion
        (allow_travel_arcs_ && p_cur_pos->is_travel())
        //|| (previous_extruder.is_extruding && extruder_current.is_extruding) // Test to see if 
        // we can get more arcs.
        // || (previous_extruder.is_retracting && extruder_current.is_retracting) // Test to see if 
        // we can get more arcs.
        ) &&
      p_cur_pos->is_extruder_relative == is_previous_extruder_relative &&
      (!waiting_for_arc_ || p_pre_pos->f == p_cur_pos->f) && // might need to skip the waiting for arc check...
      (!waiting_for_arc_ || p_pre_pos->feature_type_tag == p_cur_pos->feature_type_tag)
      )
    ) {

    // Record the extrusion rate
    previous_extrusion_rate_ = mm_extruded_per_mm_travel;
    printer_point p(p_cur_pos->get_gcode_x(), p_cur_pos->get_gcode_y(), p_cur_pos->get_gcode_z(), extruder_current.get_offset_e(), extruder_current.e_relative, p_cur_pos->f, movement_length_mm, p_pre_pos->is_extruder_relative);
    if (!waiting_for_arc_)
    {
      if (debug_logging_enabled_)
      {
        p_logger_->log(logger_type_, log_levels::DEBUG, "Starting new arc from Gcode:" + cmd.gcode);
      }
      write_unwritten_gcodes_to_file();
      // add the previous point as the starting point for the current arc
      printer_point previous_p(p_pre_pos->get_gcode_x(), p_pre_pos->get_gcode_y(), p_pre_pos->get_gcode_z(), previous_extruder.get_offset_e(), previous_extruder.e_relative, p_pre_pos->f, 0, p_pre_pos->is_extruder_relative);
      // Don't add any extrusion, or you will over extrude!
      //std::cout << "Trying to add first point (" << p.x << "," << p.y << "," << p.z << ")...";

      current_arc_.try_add_point(previous_p);
    }

    double e_relative = extruder_current.e_relative;
    int num_points = current_arc_.get_num_segments();
    arc_added = current_arc_.try_add_point(p);
    if (arc_added)
    {
      // Make sure our position list is large enough to handle all the segments
      if (current_arc_.get_num_segments() + 2 > p_source_position_->get_max_positions())
      {
        p_source_position_->grow_max_positions(p_source_position_->get_max_positions() * 2);
      }
      if (!waiting_for_arc_)
      {
        waiting_for_arc_ = true;
        previous_feedrate_ = p_pre_pos->f;
      }
      else
      {
        if (debug_logging_enabled_)
        {
          if (num_points + 1 == current_arc_.get_num_segments())
          {
            p_logger_->log(logger_type_, log_levels::DEBUG, "Adding point to arc from Gcode:" + cmd.gcode);
          }

        }
      }
    }
  }
  else {

    if (debug_logging_enabled_) {
      if (is_end)
      {
        p_logger_->log(logger_type_, log_levels::DEBUG, "Procesing final shape, if one exists.");
      }
      else if (!cmd.is_empty)
      {
        if (!cmd.is_known_command)
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "Command '" + cmd.command + "' is Unknown.  Gcode:" + cmd.gcode);
        }
        else if (cmd.command != "G0" && cmd.command != "G1")
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "Command '" + cmd.command + "' is not G0/G1, skipping.  Gcode:" + cmd.gcode);
        }
        else if (!allow_3d_arcs_ && !utilities::is_equal(p_cur_pos->z, p_pre_pos->z))
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "Z axis position changed, cannot convert:" + cmd.gcode);
        }
        else if (p_cur_pos->is_relative)
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "XYZ Axis is in relative mode, cannot convert:" + cmd.gcode);
        }
        else if (
          waiting_for_arc_ && !(
            (previous_extruder.is_extruding && extruder_current.is_extruding) ||
            (previous_extruder.is_retracting && extruder_current.is_retracting)
            )
          )
        {
          std::string message = "Extruding or retracting state changed, cannot add point to current arc: " + cmd.gcode;
          if (verbose_logging_enabled_)
          {

            message.append(
              " - Verbose Info\n\tCurrent Position Info - Absolute E:" + utilities::to_string(extruder_current.e) +
              ", Offset E:" + utilities::to_string(extruder_current.get_offset_e()) +
              ", Mode:" + (p_cur_pos->is_extruder_relative_null ? "NULL" : p_cur_pos->is_extruder_relative ? "relative" : "absolute") +
              ", Retraction: " + utilities::to_string(extruder_current.retraction_length) +
              ", Extrusion: " + utilities::to_string(extruder_current.extrusion_length) +
              ", Retracting: " + (extruder_current.is_retracting ? "True" : "False") +
              ", Extruding: " + (extruder_current.is_extruding ? "True" : "False")
            );
            message.append(
              "\n\tPrevious Position Info - Absolute E:" + utilities::to_string(previous_extruder.e) +
              ", Offset E:" + utilities::to_string(previous_extruder.get_offset_e()) +
              ", Mode:" + (p_pre_pos->is_extruder_relative_null ? "NULL" : p_pre_pos->is_extruder_relative ? "relative" : "absolute") +
              ", Retraction: " + utilities::to_string(previous_extruder.retraction_length) +
              ", Extrusion: " + utilities::to_string(previous_extruder.extrusion_length) +
              ", Retracting: " + (previous_extruder.is_retracting ? "True" : "False") +
              ", Extruding: " + (previous_extruder.is_extruding ? "True" : "False")
            );
            p_logger_->log(logger_type_, log_levels::VERBOSE, message);
          }
          else
          {
            p_logger_->log(logger_type_, log_levels::DEBUG, message);
          }

        }
        else if (p_cur_pos->is_extruder_relative != p_pre_pos->is_extruder_relative)
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "Extruder axis mode changed, cannot add point to current arc: " + cmd.gcode);
        }
        else if (waiting_for_arc_ && p_pre_pos->f != p_cur_pos->f)
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "Feedrate changed, cannot add point to current arc: " + cmd.gcode);
        }
        else if (waiting_for_arc_ && p_pre_pos->feature_type_tag != p_cur_pos->feature_type_tag)
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "Feature type changed, cannot add point to current arc: " + cmd.gcode);
        }
        else if (aborted_by_flow_rate)
        {
          std::stringstream stream;
          stream << std::fixed << std::setprecision(5);
          stream << "Arc Canceled - The extrusion rate variance of " << extrusion_rate_variance_percent_ << "% exceeded by " << extrusion_rate_change_percent - extrusion_rate_variance_percent_ << "% on line " << lines_processed_ << ".  Extruded " << extruder_current.e_relative << "mm over " << movement_length_mm << "mm of travel (" << mm_extruded_per_mm_travel << "mm/mm).  Previous rate: " << previous_extrusion_rate_ << "mm/mm.";
          p_logger_->log(logger_type_, log_levels::DEBUG, stream.str());
        }
        else
        {
          // Todo:  Add all the relevant values
          p_logger_->log(logger_type_, log_levels::DEBUG, "There was an unknown issue preventing the current point from being added to the arc: " + cmd.gcode);
        }
      }
    }

    // Reset the previous extrusion rate
    previous_extrusion_rate_ = 0;
  }

  if (!arc_added && !(cmd.is_empty && cmd.comment.length() == 0))
  {
    if (current_arc_.get_num_segments() < current_arc_.get_min_segments()) {
      if (debug_logging_enabled_ && !cmd.is_empty)
      {
        if (current_arc_.get_num_segments() != 0)
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "Not enough segments, resetting. Gcode:" + cmd.gcode);
        }

      }
      waiting_for_arc_ = false;
      current_arc_.clear();
    }
    else if (waiting_for_arc_)
    {

      if (current_arc_.is_shape())
      {
        // update our statistics
        points_compressed_ += current_arc_.get_num_segments() - 1;
        arcs_created_++; // increment the number of generated arcs
        write_arc_gcodes(p_pre_pos->f);
        // Now clear the arc and flag the processor as not waiting for an arc
        waiting_for_arc_ = false;
        current_arc_.clear();
        p_cur_pos = NULL;
        p_pre_pos = NULL;

        // Reprocess this line
        if (!is_end)
        {
          return process_gcode(cmd, false, true);
        }
        else
        {
          if (debug_logging_enabled_)
          {
            p_logger_->log(logger_type_, log_levels::DEBUG, "Final arc created, exiting.");
          }
          return 0;
        }

      }
      else
      {
        if (debug_logging_enabled_)
        {
          p_logger_->log(logger_type_, log_levels::DEBUG, "The current arc is not a valid arc, resetting.");
        }
        current_arc_.clear();
        waiting_for_arc_ = false;
      }
    }
    else if (debug_logging_enabled_)
    {
      p_logger_->log(logger_type_, log_levels::DEBUG, "Could not add point to arc from gcode:" + cmd.gcode);
    }

  }

  if (waiting_for_arc_ || !arc_added)
  {
    // This might not work....
    //position* cur_pos = p_source_position_->get_current_position_ptr();
    unwritten_commands_.push_back(unwritten_command(cmd, is_previous_extruder_relative, is_extrusion, is_retraction, is_travel, movement_length_mm));

  }
  else if (!waiting_for_arc_)
  {
    write_unwritten_gcodes_to_file();
    current_arc_.clear();
  }
  return lines_written;
}

void arc_welder::write_arc_gcodes(double current_feedrate)
{

  std::string comment = get_comment_for_arc();
  // remove the same number of unwritten gcodes as there are arc segments, minus 1 for the start point
  // Which isn't a movement
  // note, skip the first point, it is the starting point
  int num_segments = current_arc_.get_num_segments() - 1;
  for (int index = 0; index < num_segments; index++)
  {
    while (!unwritten_commands_.pop_back().is_g0_g1);
  }

  // Undo the current command, since it isn't included in the arc
  p_source_position_->undo_update();

  // Set the current feedrate if it is different, else set to 0 to indicate that no feedrate should be included
  if (previous_feedrate_ > 0 && previous_feedrate_ == current_feedrate) {
    current_feedrate = 0;
  }

  // Craete the arc gcode
  std::string gcode = get_arc_gcode(comment);

  if (debug_logging_enabled_)
  {
    char buffer[20];
    std::string message = "Arc created with ";
    sprintf(buffer, "%d", current_arc_.get_num_segments());
    message += buffer;
    message += " segments: ";
    message += gcode;
    p_logger_->log(logger_type_, log_levels::DEBUG, message);
  }

  // Write everything that hasn't yet been written	
  write_unwritten_gcodes_to_file();

  // Update the current extrusion statistics for the current arc gcode
  double shape_e_relative = current_arc_.get_shape_e_relative();
  bool is_retraction = shape_e_relative < 0;
  bool is_extrusion = shape_e_relative > 0;
  if (is_extrusion)
  {
    segment_statistics_.update(current_arc_.get_shape_length(), false);

  }
  else if (is_retraction)
  {
      segment_retraction_statistics_.update(current_arc_.get_shape_length(), false);
  }
  else if (allow_travel_arcs_ ) {
    travel_statistics_.update(current_arc_.get_shape_length(), false);
  }
  // now write the current arc to the file 
  write_gcode_to_file(gcode);
}

std::string arc_welder::get_comment_for_arc()
{
  // build a comment string from the commands making up the arc
        // We need to start with the first command entered.
  int comment_index = unwritten_commands_.count() - (current_arc_.get_num_segments() - 1);
  std::string comment;
  for (; comment_index < unwritten_commands_.count(); comment_index++)
  {
    std::string old_comment = unwritten_commands_[comment_index].comment;
    if (old_comment != comment && old_comment.length() > 0)
    {
      if (comment.length() > 0)
      {
        comment += " - ";
      }
      comment += old_comment;
    }
  }
  return comment;
}

std::string arc_welder::create_g92_e(double absolute_e)
{
  std::stringstream stream;
  stream << std::fixed << std::setprecision(5);
  stream << "G92 E" << absolute_e;
  return stream.str();
}

int arc_welder::write_gcode_to_file(std::string gcode)
{
  output_file_ << gcode << "\n";
  return 1;
}

int arc_welder::write_unwritten_gcodes_to_file()
{
  int size = unwritten_commands_.count();
  std::string lines_to_write;

  for (int index = 0; index < size; index++)
  {
    // The the current unwritten position and remove it from the list
    unwritten_command p = unwritten_commands_.pop_front();
    if ((p.is_g0_g1 || p.is_g2_g3) && p.length > 0)
    {

      if (p.is_extrusion)
      {
        segment_statistics_.update(p.length, false);
      }
      else if (p.is_retraction)
      {
          segment_retraction_statistics_.update(p.length, false);
      }
      else if (p.is_travel && allow_travel_arcs_) {
        travel_statistics_.update(p.length, false);
      }
    }
    lines_to_write.append(p.to_string()).append("\n");
  }

  output_file_ << lines_to_write;
  return size;
}

std::string arc_welder::get_arc_gcode(const std::string comment)
{
  // Write gcode to file
  std::string gcode;

  gcode = current_arc_.get_shape_gcode();

  if (comment.length() > 0)
  {
    gcode += ";" + comment;
  }
  return gcode;

}

void arc_welder::add_arcwelder_comment_to_target()
{
  p_logger_->log(logger_type_, log_levels::DEBUG, "Adding ArcWelder comment to the target file.");
  std::stringstream stream;
  stream << std::fixed;
  stream << "; Postprocessed by [ArcWelder](https://github.com/FormerLurker/ArcWelderLib)\n";
  stream << "; Copyright(C) 2021 - Brad Hochgesang\n";
  stream << "; Version: " << GIT_TAGGED_VERSION << ", Branch: " << GIT_BRANCH << ", BuildDate: " << BUILD_DATE << "\n";
  stream << "; resolution=" << std::setprecision(2) << resolution_mm_ << "mm\n";
  stream << "; path_tolerance=" << std::setprecision(1) << (current_arc_.get_path_tolerance_percent() * 100.0) << "%\n";
  stream << "; max_radius=" << std::setprecision(2) << (current_arc_.get_max_radius()) << "mm\n";
  if (gcode_position_args_.g90_influences_extruder)
  {
    stream << "; g90_influences_extruder=True\n";
  }
  if (current_arc_.get_mm_per_arc_segment() > 0 && current_arc_.get_min_arc_segments() > 0)
  {
    stream << "; firmware_compensation=True\n";
    stream << "; mm_per_arc_segment=" << std::setprecision(2) << current_arc_.get_mm_per_arc_segment() << "mm\n";
    stream << "; min_arc_segments=" << std::setprecision(0) << current_arc_.get_min_arc_segments() << "\n";
  }
  if (allow_3d_arcs_)
  {
    stream << "; allow_3d_arcs=True\n";
  }
  if (allow_travel_arcs_)
  {
      stream << "; allow_travel_arcs=True\n";
  }
  if (allow_dynamic_precision_)
  {
    stream << "; allow_dynamic_precision=True\n";
  }
  stream << "; default_xyz_precision=" << std::setprecision(0) << static_cast<int>(current_arc_.get_xyz_precision()) << "\n";
  stream << "; default_e_precision=" << std::setprecision(0) << static_cast<int>(current_arc_.get_e_precision()) << "\n";
  if (extrusion_rate_variance_percent_ > 0)
  {
      stream << "; extrusion_rate_variance=" << std::setprecision(1) << (extrusion_rate_variance_percent_ * 100.0) << "%\n";
  }
  stream << "\n";

  output_file_ << stream.str();
}


