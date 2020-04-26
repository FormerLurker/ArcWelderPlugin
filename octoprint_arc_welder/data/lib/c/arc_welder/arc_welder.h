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
#include <vector>
#include <set>
#include "gcode_position.h"
#include "position.h"
#include "gcode_parser.h"
#include "segmented_arc.h"
#include <iostream>
#include <fstream>
#include "array_list.h"
#include "unwritten_command.h"
#include "logger.h"
// define the progress callback type 
typedef bool(*progress_callback)(double percentComplete, double seconds_elapsed, double estimatedSecondsRemaining, int gcodesProcessed, int linesProcessed, int points_compressed, int arcs_created);

class arc_welder
{
public:
	arc_welder(std::string source_path, std::string target_path, logger * log, double resolution_mm, gcode_position_args args);
	arc_welder(std::string source_path, std::string target_path, logger * log, double resolution_mm, bool g90_g91_influences_extruder, int buffer_size);
	arc_welder(std::string source_path, std::string target_path, logger * log, double resolution_mm, bool g90_g91_influences_extruder, int buffer_size, progress_callback callback);
	void set_logger_type(int logger_type);
	virtual ~arc_welder();
	void process();
	double notification_period_seconds;
protected:
	virtual bool on_progress_(double percentComplete, double seconds_elapsed, double estimatedSecondsRemaining, int gcodesProcessed, int linesProcessed, int points_compressed, int arcs_created);
private:
	void reset();
	static gcode_position_args get_args_(bool g90_g91_influences_extruder, int buffer_size);
	progress_callback progress_callback_;
	int process_gcode(parsed_command cmd, bool is_end);
	int write_gcode_to_file(std::string gcode);
	std::string get_arc_gcode(double f, const std::string comment);
	std::string get_comment_for_arc();
	int write_unwritten_gcodes_to_file();
	std::string create_g92_e(double absolute_e);
	std::string source_path_;
	std::string target_path_;
	double resolution_mm_;
	double max_segments_;
	gcode_position_args gcode_position_args_;
	long file_size_;
	int lines_processed_;
	int gcodes_processed_;
	int last_gcode_line_written_;
	int points_compressed_;
	int arcs_created_;
	long get_file_size(const std::string& file_path);
	double get_time_elapsed(double start_clock, double end_clock);
	double get_next_update_time() const;
	bool waiting_for_line_;
	bool waiting_for_arc_;
	array_list<unwritten_command> unwritten_commands_;
	array_list<parsed_command> undo_commands_;
	segmented_arc current_arc_;
	std::ofstream output_file_;
	
	// We don't care about the printer settings, except for g91 influences extruder.
	gcode_position * p_source_position_;
	double absolute_e_offset_;
	std::set<std::string> absolute_e_rewrite_commands_;
	gcode_parser parser_;
	double absolute_e_offset_total_;
	bool verbose_output_;
	int logger_type_;
	logger* p_logger_;
	bool debug_logging_enabled_;
	bool info_logging_enabled_;
	bool verbose_logging_enabled_;
	bool error_logging_enabled_;

};
