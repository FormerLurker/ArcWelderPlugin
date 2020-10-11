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

#include "arc_welder.h"
#include <vector>
#include <sstream>
#include "utilities.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
arc_welder::arc_welder(std::string source_path, std::string target_path, logger * log, double resolution_mm, double max_radius, gcode_position_args args) : current_arc_(DEFAULT_MIN_SEGMENTS, gcode_position_args_.position_buffer_size - 5, resolution_mm, max_radius), segment_statistics_(segment_statistic_lengths)
{
	p_logger_ = log;
	debug_logging_enabled_ = false;
	info_logging_enabled_ = false;
	error_logging_enabled_ = false;
	verbose_logging_enabled_ = false;

	logger_type_ = 0;
	progress_callback_ = NULL;
	verbose_output_ = false;
	source_path_ = source_path;
	target_path_ = target_path;
	resolution_mm_ = resolution_mm;
	gcode_position_args_ = args;
	notification_period_seconds = 1;
	lines_processed_ = 0;
	gcodes_processed_ = 0;
	file_size_ = 0;
	last_gcode_line_written_ = 0;
	points_compressed_ = 0;
	arcs_created_ = 0;
	waiting_for_arc_ = false;
	previous_feedrate_ = -1;
	previous_is_extruder_relative_ = false;
	gcode_position_args_.set_num_extruders(8);
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

arc_welder::arc_welder(std::string source_path, std::string target_path, logger* log, double resolution_mm, double max_radius, bool g90_g91_influences_extruder, int buffer_size)
	: arc_welder(source_path, target_path, log, resolution_mm, max_radius, arc_welder::get_args_(g90_g91_influences_extruder, buffer_size))
{
	
}

arc_welder::arc_welder(std::string source_path, std::string target_path, logger * log, double resolution_mm, double max_radius, bool g90_g91_influences_extruder, int buffer_size, progress_callback callback)
	: arc_welder(source_path, target_path, log, resolution_mm, max_radius, arc_welder::get_args_(g90_g91_influences_extruder, buffer_size))
{
	progress_callback_ = callback;
}

gcode_position_args arc_welder::get_args_(bool g90_g91_influences_extruder, int buffer_size)
{
	gcode_position_args args;
	// Configure gcode_position_args
	args.g90_influences_extruder = g90_g91_influences_extruder;
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
	p_logger_->log(logger_type_, DEBUG, "Resetting all tracking variables.");
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
	return clock() + (notification_period_seconds * CLOCKS_PER_SEC);
}

double arc_welder::get_time_elapsed(double start_clock, double end_clock)
{
	return static_cast<double>(end_clock - start_clock) / CLOCKS_PER_SEC;
}

arc_welder_results arc_welder::process()
{
arc_welder_results results;
	p_logger_->log(logger_type_, DEBUG, "Configuring logging settings.");
	verbose_logging_enabled_ = p_logger_->is_log_level_enabled(logger_type_, VERBOSE);
	debug_logging_enabled_ = p_logger_->is_log_level_enabled(logger_type_, DEBUG);
	info_logging_enabled_ = p_logger_->is_log_level_enabled(logger_type_, INFO);
	error_logging_enabled_ = p_logger_->is_log_level_enabled(logger_type_, ERROR);

	std::stringstream stream;
	stream << std::fixed << std::setprecision(5);
	stream << "py_gcode_arc_converter.ConvertFile - Parameters received: source_file_path: '" <<
		source_path_ << "', target_file_path:'" << target_path_ << "', resolution_mm:" <<
		resolution_mm_ << "mm (+-" << current_arc_.get_resolution_mm() << "mm), max_radius_mm:" << current_arc_.get_max_radius()
		 << "mm, g90_91_influences_extruder: " << (p_source_position_->get_g90_91_influences_extruder() ? "True" : "False") << "\n";
	p_logger_->log(logger_type_, INFO, stream.str());


	// reset tracking variables
	reset();
	// local variable to hold the progress update return.  If it's false, we will exit.
	bool continue_processing = true;
	
	p_logger_->log(logger_type_, DEBUG, "Configuring progress updates.");
	int read_lines_before_clock_check = 5000;
	double next_update_time = get_next_update_time();
	const clock_t start_clock = clock();
	p_logger_->log(logger_type_, DEBUG, "Getting source file size.");
	file_size_ = get_file_size(source_path_);
	stream.clear();
	stream.str("");
	stream << "Source file size: " << file_size_;
	p_logger_->log(logger_type_, DEBUG, stream.str());
	// Create the source file read stream and target write stream
	std::ifstream gcodeFile;
	p_logger_->log(logger_type_, DEBUG, "Opening the source file for reading.");
	gcodeFile.open(source_path_.c_str(), std::ifstream::in);
	if (!gcodeFile.is_open())
	{
		results.success = false;
		results.message = "Unable to open the source file.";
		p_logger_->log_exception(logger_type_, results.message);
		return results;
	}
	p_logger_->log(logger_type_, DEBUG, "Source file opened successfully.");

	p_logger_->log(logger_type_, DEBUG, "Opening the target file for writing.");
	output_file_.open(target_path_.c_str(), std::ifstream::out);
	if (!output_file_.is_open())
	{
		results.success = false;
		results.message = "Unable to open the target file.";
		p_logger_->log_exception(logger_type_, results.message);
		gcodeFile.close();
		return results;
	}
	p_logger_->log(logger_type_, DEBUG, "Target file opened successfully.");
	std::string line;
	int lines_with_no_commands = 0;
	//gcodeFile.sync_with_stdio(false);
	//output_file_.sync_with_stdio(false);
	
	add_arcwelder_comment_to_target();
	
	parsed_command cmd;
	// Communicate every second
	p_logger_->log(logger_type_, DEBUG, "Processing source file.");
	while (std::getline(gcodeFile, line) && continue_processing)
	{
		lines_processed_++;

		cmd.clear();
		if (verbose_logging_enabled_)
		{
			stream.clear();
			stream.str("");
			stream << "Parsing: " << line;
			p_logger_->log(logger_type_, VERBOSE, stream.str());
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
		if (has_gcode && (progress_callback_ != NULL || info_logging_enabled_))
		{
			if ((lines_processed_ % read_lines_before_clock_check) == 0 && next_update_time < clock())
			{
				if (verbose_logging_enabled_)
				{
					p_logger_->log(logger_type_, VERBOSE, "Sending progress update.");
				}
				continue_processing = on_progress_(get_progress_(static_cast<long>(gcodeFile.tellg()), static_cast<double>(start_clock)));
				next_update_time = get_next_update_time();
			}
		}
	}

	if (current_arc_.is_shape() && waiting_for_arc_)
	{
		p_logger_->log(logger_type_, DEBUG, "The target file opened successfully.");
		process_gcode(cmd, true, false);
	}
	p_logger_->log(logger_type_, DEBUG, "Writing all unwritten gcodes to the target file.");
	write_unwritten_gcodes_to_file();

	p_logger_->log(logger_type_, DEBUG, "Processing complete, closing source and target file.");
	arc_welder_progress final_progress = get_progress_(static_cast<long>(file_size_), static_cast<double>(start_clock));
	if (progress_callback_ != NULL || info_logging_enabled_)
	{
		// Sending final progress update message
		on_progress_(final_progress);
	}
	
	output_file_.close();
	gcodeFile.close();
	const clock_t end_clock = clock();
	
	results.success = continue_processing;
	results.cancelled = !continue_processing;
	results.progress = final_progress;
	return results;
}

bool arc_welder::on_progress_(const arc_welder_progress& progress)
{
	if (progress_callback_ != NULL)
	{
		return progress_callback_(progress);
	}
	else if (info_logging_enabled_)
	{
		p_logger_->log(logger_type_, INFO, progress.str());
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

	progress.segment_statistics = segment_statistics_;
	return progress;
	
}

int arc_welder::process_gcode(parsed_command cmd, bool is_end, bool is_reprocess)
{
	// Update the position for the source gcode file
	p_source_position_->update(cmd, lines_processed_, gcodes_processed_, -1);
	position* p_cur_pos = p_source_position_->get_current_position_ptr();
	position* p_pre_pos = p_source_position_->get_previous_position_ptr();
	extruder extruder_current = p_cur_pos->get_current_extruder();
	extruder previous_extruder = p_pre_pos->get_current_extruder();
	point p(p_cur_pos->get_gcode_x(), p_cur_pos->get_gcode_y(), p_cur_pos->get_gcode_z(), extruder_current.e_relative);
	//std::cout << lines_processed_ << " - " << cmd.gcode << ", CurrentEAbsolute: " << cur_extruder.e <<", ExtrusionLength: " << cur_extruder.extrusion_length << ", Retraction Length: " << cur_extruder.retraction_length << ", IsExtruding: " << cur_extruder.is_extruding << ", IsRetracting: " << cur_extruder.is_retracting << ".\n";

	int lines_written = 0;
	// see if this point is an extrusion
	
	bool arc_added = false;
	bool clear_shapes = false;
	
	// Update the source file statistics
	if (p_cur_pos->has_xy_position_changed && (extruder_current.is_extruding || extruder_current.is_retracting) && !is_reprocess)
	{
		double movement_length_mm = utilities::get_cartesian_distance(p_pre_pos->x, p_pre_pos->y, p_cur_pos->x, p_cur_pos->y);
		if (movement_length_mm > 0)
		{
			segment_statistics_.update(movement_length_mm, true);
		}
	}

	// We need to make sure the printer is using absolute xyz, is extruding, and the extruder axis mode is the same as that of the previous position
	// TODO: Handle relative XYZ axis.  This is possible, but maybe not so important.
	if (
		!is_end && cmd.is_known_command && !cmd.is_empty && (
			(cmd.command == "G0" || cmd.command == "G1") &&
			utilities::is_equal(p_cur_pos->z, p_pre_pos->z) &&
			utilities::is_equal(p_cur_pos->x_offset, p_pre_pos->x_offset) &&
			utilities::is_equal(p_cur_pos->y_offset, p_pre_pos->y_offset) &&
			utilities::is_equal(p_cur_pos->z_offset, p_pre_pos->z_offset) &&
			utilities::is_equal(p_cur_pos->x_firmware_offset, p_pre_pos->x_firmware_offset) &&
			utilities::is_equal(p_cur_pos->y_firmware_offset, p_pre_pos->y_firmware_offset) &&
			utilities::is_equal(p_cur_pos->z_firmware_offset, p_pre_pos->z_firmware_offset) &&
			!p_cur_pos->is_relative &&
			(
				!waiting_for_arc_ ||
				(previous_extruder.is_extruding && extruder_current.is_extruding) ||
				(previous_extruder.is_retracting && extruder_current.is_retracting)
			) &&
			p_cur_pos->is_extruder_relative == p_pre_pos->is_extruder_relative &&
			(!waiting_for_arc_ || p_pre_pos->f == p_cur_pos->f) &&
			(!waiting_for_arc_ || p_pre_pos->feature_type_tag == p_cur_pos->feature_type_tag)
			)
	) {
		
		if (!waiting_for_arc_)
		{
			previous_is_extruder_relative_ = p_pre_pos->is_extruder_relative;
			if (debug_logging_enabled_)
			{
				p_logger_->log(logger_type_, DEBUG, "Starting new arc from Gcode:" + cmd.gcode);
			}
			write_unwritten_gcodes_to_file();
			// add the previous point as the starting point for the current arc
			point previous_p(p_pre_pos->get_gcode_x(), p_pre_pos->get_gcode_y(), p_pre_pos->get_gcode_z(), previous_extruder.e_relative);
			// Don't add any extrusion, or you will over extrude!
			//std::cout << "Trying to add first point (" << p.x << "," << p.y << "," << p.z << ")...";
			current_arc_.try_add_point(previous_p, 0);
		}
		
		double e_relative = extruder_current.e_relative;
		int num_points = current_arc_.get_num_segments();
		arc_added = current_arc_.try_add_point(p, e_relative);
		if (arc_added)
		{
			if (!waiting_for_arc_)
			{
				waiting_for_arc_ = true;
				previous_feedrate_ = p_pre_pos->f;
			}
			else
			{
				if (debug_logging_enabled_)
				{
					if (num_points+1 == current_arc_.get_num_segments())
					{
						p_logger_->log(logger_type_, DEBUG, "Adding point to arc from Gcode:" + cmd.gcode);
					}
					{
						p_logger_->log(logger_type_, DEBUG, "Removed start point from arc and added a new point from Gcode:" + cmd.gcode);
					}
				}
			}
		}
	}
	else if (debug_logging_enabled_ ){
		if (is_end)
		{
			p_logger_->log(logger_type_, DEBUG, "Procesing final shape, if one exists.");
		}
		else if (!cmd.is_empty)
		{
			if (!cmd.is_known_command)
			{
				p_logger_->log(logger_type_, DEBUG, "Command '" + cmd.command + "' is Unknown.  Gcode:" + cmd.gcode);
			}
			else if (cmd.command != "G0" && cmd.command != "G1")
			{
				p_logger_->log(logger_type_, DEBUG, "Command '"+ cmd.command + "' is not G0/G1, skipping.  Gcode:" + cmd.gcode);
			}
			else if (!utilities::is_equal(p_cur_pos->z, p_pre_pos->z))
			{
				p_logger_->log(logger_type_, DEBUG, "Z axis position changed, cannot convert:" + cmd.gcode);
			}
			else if (p_cur_pos->is_relative)
			{
				p_logger_->log(logger_type_, DEBUG, "XYZ Axis is in relative mode, cannot convert:" + cmd.gcode);
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
					p_logger_->log(logger_type_, VERBOSE, message);
				}
				else
				{
					p_logger_->log(logger_type_, DEBUG, message);
				}
				
			}
			else if (p_cur_pos->is_extruder_relative != p_pre_pos->is_extruder_relative)
			{
				p_logger_->log(logger_type_, DEBUG, "Extruder axis mode changed, cannot add point to current arc: " + cmd.gcode);
			}
			else if (waiting_for_arc_ && p_pre_pos->f != p_cur_pos->f)
			{
				p_logger_->log(logger_type_, DEBUG, "Feedrate changed, cannot add point to current arc: " + cmd.gcode);
			}
			else if (waiting_for_arc_ && p_pre_pos->feature_type_tag != p_cur_pos->feature_type_tag)
			{
				p_logger_->log(logger_type_, DEBUG, "Feature type changed, cannot add point to current arc: " + cmd.gcode);
			}
			else
			{
				// Todo:  Add all the relevant values
				p_logger_->log(logger_type_, DEBUG, "There was an unknown issue preventing the current point from being added to the arc: " + cmd.gcode);
			}
		}
	}
	
	if (!arc_added)
	{
		if (current_arc_.get_num_segments() < current_arc_.get_min_segments()) {
			if (debug_logging_enabled_ && !cmd.is_empty)
			{
				if (current_arc_.get_num_segments() != 0)
				{
					p_logger_->log(logger_type_, DEBUG, "Not enough segments, resetting. Gcode:" + cmd.gcode);
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
				points_compressed_ += current_arc_.get_num_segments()-1;
				arcs_created_++; // increment the number of generated arcs

				//std::cout << "Arc shape found.\n";
				// Get the comment now, before we remove the previous comments
				std::string comment = get_comment_for_arc();
				// remove the same number of unwritten gcodes as there are arc segments, minus 1 for the start point
				// Which isn't a movement
				// note, skip the first point, it is the starting point
				for (int index = 0; index < current_arc_.get_num_segments() - 1; index++)
				{
					unwritten_commands_.pop_back();
				}
				// get the feedrate for the previous position (the last command that was turned into an arc)
				double current_f = p_pre_pos->f;
				
				// Undo the current command, since it isn't included in the arc
				p_source_position_->undo_update();
				// IMPORTANT NOTE: p_cur_pos and p_pre_pos will NOT be usable beyond this point.
				p_pre_pos = NULL;
				p_cur_pos = p_source_position_->get_current_position_ptr();
				extruder_current = p_cur_pos->get_current_extruder();

				// Set the current feedrate if it is different, else set to 0 to indicate that no feedrate should be included
				if(previous_feedrate_ > 0 && previous_feedrate_ == current_f){
					current_f = 0;
				}

				// Craete the arc gcode
				std::string gcode;
				if (previous_is_extruder_relative_){
					gcode = get_arc_gcode_relative(current_f, comment);
				}
					
				else { 
					gcode = get_arc_gcode_absolute(extruder_current.get_offset_e(), current_f, comment);
				}
				

				if (debug_logging_enabled_)
				{
					p_logger_->log(logger_type_, DEBUG, "Arc created with " + std::to_string(current_arc_.get_num_segments()) + " segments: " + gcode);
				}

				// Get and alter the current position so we can add it to the unwritten commands list
				parsed_command arc_command = parser_.parse_gcode(gcode.c_str());
				double arc_extrusion_length = current_arc_.get_shape_length();
				
				unwritten_commands_.push_back(
					unwritten_command(arc_command, p_cur_pos->is_extruder_relative, arc_extrusion_length)
				);
				
				// write all unwritten commands (if we don't do this we'll mess up absolute e by adding an offset to the arc)
				// including the most recent arc command BEFORE updating the absolute e offset
				write_unwritten_gcodes_to_file();
				
				// Now clear the arc and flag the processor as not waiting for an arc
				waiting_for_arc_ = false;
				current_arc_.clear();
				

				// Reprocess this line
				if (!is_end)
				{
					return process_gcode(cmd, false, true);
				}
				else
				{
					if (debug_logging_enabled_)
					{
						p_logger_->log(logger_type_, DEBUG, "Final arc created, exiting.");
					}
					return 0;
				}
					
			}
			else
			{
				if (debug_logging_enabled_)
				{
					p_logger_->log(logger_type_, DEBUG, "The current arc is not a valid arc, resetting.");
				}
				current_arc_.clear();
				waiting_for_arc_ = false;
			}
		}
		else if (debug_logging_enabled_)
		{
			p_logger_->log(logger_type_, DEBUG, "Could not add point to arc from gcode:" + cmd.gcode);
		}

	}


	if (waiting_for_arc_ || !arc_added)
	{
		position* cur_pos = p_source_position_->get_current_position_ptr();
		extruder& cur_extruder = cur_pos->get_current_extruder();

		double length = 0;
		if (p_cur_pos->has_xy_position_changed && (cur_extruder.is_extruding || cur_extruder.is_retracting))
		{
			position* prev_pos = p_source_position_->get_previous_position_ptr();
			length = utilities::get_cartesian_distance(cur_pos->x, cur_pos->y, prev_pos->x, prev_pos->y);
		}
		
		unwritten_commands_.push_back(unwritten_command(cur_pos, length));
		
	}
	if (!waiting_for_arc_)
	{
		write_unwritten_gcodes_to_file();
	}
	return lines_written;
}

std::string arc_welder::get_comment_for_arc()
{
	// build a comment string from the commands making up the arc
				// We need to start with the first command entered.
	int comment_index = unwritten_commands_.count() - (current_arc_.get_num_segments() - 1);
	std::string comment;
	for (; comment_index < unwritten_commands_.count(); comment_index++)
	{
		std::string old_comment = unwritten_commands_[comment_index].command.comment;
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
	std::string gcode_to_write;
	
	
	for (int index = 0; index < size; index++)
	{
		// The the current unwritten position and remove it from the list
		unwritten_command p = unwritten_commands_.pop_front();
		if (p.extrusion_length > 0)
		{
			segment_statistics_.update(p.extrusion_length, false);
		}
		write_gcode_to_file(p.command.to_string());
	}
	
	return size;
}

std::string arc_welder::get_arc_gcode_relative(double f, const std::string comment)
{
	// Write gcode to file
	std::string gcode;

	gcode = current_arc_.get_shape_gcode_relative(f);
	
	if (comment.length() > 0)
	{
		gcode += ";" + comment;
	}
	return gcode;
	
}

std::string arc_welder::get_arc_gcode_absolute(double e, double f, const std::string comment)
{
	// Write gcode to file
	std::string gcode;

	gcode = current_arc_.get_shape_gcode_absolute(e, f);

	if (comment.length() > 0)
	{
		gcode += ";" + comment;
	}
	return gcode;

}

void arc_welder::add_arcwelder_comment_to_target()
{
	p_logger_->log(logger_type_, DEBUG, "Adding ArcWelder comment to the target file.");
	std::stringstream stream;
	stream << std::fixed << std::setprecision(2);
	stream <<	"; Postprocessed by [ArcWelder](https://github.com/FormerLurker/ArcWelderLib)\n";
	stream << "; Copyright(C) 2020 - Brad Hochgesang\n";
	stream << "; arc_welder_resolution_mm = " << resolution_mm_ << "\n";
	stream << "; arc_welder_g90_influences_extruder = " << (gcode_position_args_.g90_influences_extruder ? "True" : "False") << "\n\n";
	
	output_file_ << stream.str();
}


