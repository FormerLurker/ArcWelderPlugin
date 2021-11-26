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
#include <cmath>
#include <iomanip>
#include <sstream>

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#define ARC_WELDER_INFO_STRING "Arc Welder: Anti-Stutter\nConverts G0/G1 commands to G2/G3 (arc) commands. Reduces the number of gcodes per second sent to a 3D printer, which can reduce stuttering.";

static const int segment_statistic_lengths_count = 12;
const double segment_statistic_lengths[] = { 0.002f, 0.005f, 0.01f, 0.05f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f };

struct segment_statistic {
	segment_statistic(double min_length_mm, double max_length_mm)
	{
		count = 0;
		min_mm = min_length_mm;
		max_mm = max_length_mm;
	}

	double min_mm;
	double max_mm;
	int count;
};

struct source_target_segment_statistics {
	source_target_segment_statistics(const double segment_tracking_lengths[], const int num_lengths, logger* p_logger = NULL)
	{
		total_length_source = 0;
		total_length_target = 0;
		total_count_source = 0;
		total_count_target = 0;
		max_width = 0;
		max_precision = 3;
		num_segment_tracking_lengths = num_lengths;
		double current_min = 0;
		for (int index = 0; index < num_lengths; index++)
		{
			double current_max = segment_tracking_lengths[index];
			segment_statistic_lengths.push_back(segment_tracking_lengths[index]);
			source_segments.push_back(segment_statistic(current_min, segment_tracking_lengths[index]));
			target_segments.push_back(segment_statistic(current_min, segment_tracking_lengths[index]));
			current_min = current_max;
		}
		source_segments.push_back(segment_statistic(current_min, -1.0f));
		target_segments.push_back(segment_statistic(current_min, -1.0f));
		max_width = utilities::get_num_digits(current_min);
		p_logger_ = p_logger;
		logger_type_ = 0;
	}
	
	std::vector<double> segment_statistic_lengths;
	std::vector<segment_statistic> source_segments;
	std::vector<segment_statistic> target_segments;
	double total_length_source;
	double total_length_target;
	int max_width;
	int max_precision;
	int total_count_source;
	int total_count_target;
	int num_segment_tracking_lengths;
	
	double get_total_count_reduction_percent() const {
		return utilities::get_percent_change(total_count_source, total_count_target);
	}

	void update(double length, bool is_source)
	{
		if (length <= 0)
			return;

		std::vector<segment_statistic>* stats;
		if (is_source)
		{
			total_count_source++;
			total_length_source += length;
			stats = &source_segments;
		}
		else
		{
			total_count_target++;
			total_length_target += length;
			stats = &target_segments;
		}
		for (int index = 0; index < (*stats).size(); index++)
		{
			segment_statistic& stat = (*stats)[index];
			if ( (stat.min_mm <= length && stat.max_mm > length) || (index + 1) == (*stats).size())
			{
				stat.count++;
				break;
			}
		}
	}

	static source_target_segment_statistics add(source_target_segment_statistics stats1, const source_target_segment_statistics stats2)
	{

		double * lengths = &stats1.segment_statistic_lengths[0];
		std::copy(stats1.segment_statistic_lengths.begin(), stats1.segment_statistic_lengths.end(), lengths);
		source_target_segment_statistics combined_stats(lengths, segment_statistic_lengths_count, stats1.p_logger_);
		if (stats1.num_segment_tracking_lengths != stats2.num_segment_tracking_lengths)
		{
			// Todo:  throw a reasonable exception
			throw std::exception();
		}

		// Copy the segment statistics
		for (int index = 0; index <= stats1.num_segment_tracking_lengths; index++)
		{
			// Verify the stats are the same
			if (
				stats1.source_segments[index].min_mm != stats2.source_segments[index].min_mm
				|| stats1.source_segments[index].max_mm != stats2.source_segments[index].max_mm
				)
			{
				// Todo:  throw a reasonable exception
				throw std::exception();
			}
			combined_stats.source_segments[index].count = stats1.source_segments[index].count + stats2.source_segments[index].count;
			combined_stats.target_segments[index].count = stats1.target_segments[index].count + stats2.target_segments[index].count;
		}

		combined_stats.total_length_source = stats1.total_length_source + stats2.total_length_source;
		combined_stats.total_length_target = stats1.total_length_target + stats2.total_length_target;
		combined_stats.total_count_source = stats1.total_count_source + stats2.total_count_source;
		combined_stats.total_count_target = stats1.total_count_target + stats2.total_count_target;

		return combined_stats;
	}
	std::string str() const {
		return str("", utilities::box_drawing::BoxEncodingEnum::ASCII);
	}

	std::string str(std::string title, utilities::box_drawing::BoxEncodingEnum box_encoding) const {
		
		//if (p_logger_ != NULL) p_logger_->log(logger_type_, VERBOSE, "Building Segment Statistics.");
		
		std::stringstream output_stream;
		std::stringstream format_stream;
		const int min_column_size = 8;
		int mm_col_size = max_width + max_precision + 2; // Adding 2 for the mm
		int percent_precision = 1;
		int min_percent_col_size = 7;
		int min_max_label_col_size = 4;
		int percent_col_size = min_percent_col_size;
		int totals_row_label_size = 22;
		int source_col_size= 0;
		int target_col_size = 0;

		// Calculate the count columns and percent column sizes
		int max_source = 0;
		int max_target = 0;
		int max_percent = 0;  // We only need to hold the integer part
	
		//if (p_logger_ != NULL) p_logger_->log(logger_type_, VERBOSE, "Calculating Column Size.");

		for (int index = 0; index < source_segments.size(); index++)
		{
			int source_count = source_segments[index].count;
			int target_count = target_segments[index].count;
			int percent = 0;
			if (source_count > 0)
			{
				percent = (int)((((double)target_count - (double)source_count) / (double)source_count) * 100.0);
				if (percent > max_percent)
				{
					max_percent = percent;
				}
			}
			if (max_source < source_count)
			{
				max_source = source_count;
			}
			if (max_target < target_count)
			{
				max_target = target_count;
			}
		}
		// Get the number of digits in the max count
		source_col_size = utilities::get_num_digits(max_source);
		// enforce the minimum of 6
		if (source_col_size < min_column_size)
		{
			source_col_size = min_column_size;
		}
		// Get the number of digits in the max count
		target_col_size = utilities::get_num_digits(max_target);
		// enforce the minimum of 6
		if (target_col_size < min_column_size)
		{
			target_col_size = min_column_size;
		}
		// Get the percent column size, including one point of precision, the decimal point, a precent, and a space.
		percent_col_size = utilities::get_num_digits(max_percent) +  percent_precision + 3; // add two for . and %
		// enforce the minumum percent col size
		if (percent_col_size < min_percent_col_size)
		{
			percent_col_size = min_percent_col_size;
		}

		if (max_precision > 0)
		{
			// We need an extra space in our column for the decimal.
			mm_col_size++;
		}

		// enforce the min column size
		if (mm_col_size < min_column_size)
		{
			mm_col_size = min_column_size;
		}
		// Get the table width
		int table_width = mm_col_size + min_max_label_col_size + mm_col_size + source_col_size + target_col_size + percent_col_size;
		int table_left_padding = 0;
		int table_right_padding = 0;
		if (table_width < (int)title.length())
		{
			table_left_padding = ((int)title.length() - table_width) / 2;
			table_right_padding = ((int)title.length() - table_width - table_left_padding);
			table_width = (int)title.length();
			
		}
		utilities::box_drawing box(box_encoding, table_width);
		// Draw the top border
		box.top(output_stream);

		if (title != "")
		{
			// Draw the title
			box.row(output_stream, utilities::center(title, table_width));
			// Draw the title separator
			box.middle(output_stream);
		}
		
		// Output the centered column headers
		// start the row
		output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL);
		// add the left padding for the table
		output_stream << std::string(table_left_padding, ' ');
		output_stream << std::setfill(' ') << utilities::center("Min", mm_col_size);
		output_stream << std::setw(min_max_label_col_size) << "";
		output_stream << utilities::center("Max", mm_col_size);
		// right align the source, target and change columns
		output_stream << std::setw(source_col_size) << std::right << "Source";
		output_stream << std::setw(target_col_size) << std::right << "Target";
		output_stream << std::setw(percent_col_size) << std::right << "Change";
		// Add the right padding for the table
		output_stream << std::string(table_right_padding, ' ');
		// end the row
		output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL) << "\n";
		output_stream << std::fixed << std::setprecision(max_precision);
		// Add the separator
		box.middle(output_stream);
		for (int index = 0; index < source_segments.size(); index++) {
			// start the row
			output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL);
			// add the left padding for the table
			output_stream << std::string(table_left_padding, ' ');
			//extract the necessary variables from the source and target segments
			double min_mm = source_segments[index].min_mm;
			double max_mm = source_segments[index].max_mm;
			int source_count = source_segments[index].count;
			int target_count = target_segments[index].count;
			// Calculate the percent change	and create the string
			// Construct the percent_change_string
			std::string percent_change_string = utilities::get_percent_change_string(source_count, target_count, percent_precision);

			// Create the strings to hold the column values
			std::string min_mm_string;
			std::string max_mm_string;
			std::string source_count_string;
			std::string target_count_string;

			// Clear the format stream and construct the min_mm_string
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << min_mm << "mm";
			min_mm_string = format_stream.str();
			// Clear the format stream and construct the max_mm_string
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << max_mm << "mm";
			max_mm_string = format_stream.str();
			// Clear the format stream and construct the source_count_string
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(0) << source_count;
			source_count_string = format_stream.str();
			// Clear the format stream and construct the target_count_string
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(0) << target_count;
			target_count_string = format_stream.str();
			// The min and max columns and the label need to be handled differently if this is the last item
			if (index == source_segments.size() - 1)
			{
				// If we are on the last setment item, the 'min' value is the max, and there is no end
				// The is because the last item contains the count of all items above the max length provided
				// in the constructor

				// The 'min' column is empty here
				output_stream << std::setw(mm_col_size) << std::internal << "";
				// Add the min/max label
				output_stream << std::setw(min_max_label_col_size) << " >= ";
				// Add the min mm string
				output_stream << std::setw(mm_col_size) << std::internal << min_mm_string;
			}
			else
			{
				//if (p_logger_ != NULL) p_logger_->log(logger_type_, VERBOSE, "Adding row text.");

				// add the 'min' column
				output_stream << std::setw(mm_col_size) << std::internal << min_mm_string;
				// Add the min/max label
				output_stream << std::setw(min_max_label_col_size) << " to ";
				// Add the 'max' column				
				output_stream << std::setw(mm_col_size) << std::internal << max_mm_string;
			}
			// Add the source count
			output_stream << std::setw(source_col_size) << source_count_string;
			// Add the target count
			output_stream << std::setw(target_col_size) << target_count_string;
			// Add the percent change string
			output_stream << std::setw(percent_col_size) << percent_change_string;
			// Add the right padding for the table
			output_stream << std::string(table_right_padding, ' ');
			// end the row
			output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL) << "\n";
		}
		
		
		// Add the total rows;
		// Draw the totals separator
		box.middle(output_stream);
		if (utilities::is_equal(total_length_source, total_length_target, 0.001))
		{
			// start the row
			output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL);
			std::string total_distance_string;
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << total_length_source << "mm";
			total_distance_string = format_stream.str();
			output_stream << std::setw(totals_row_label_size) << std::right << "Total distance:";
			output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_distance_string << std::setfill(' ');
			// end the row
			output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL) << "\n";
		}
		else
		{
			// We need to output two different distances
			// start the row
			output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL);
			// Format the total source distance string
			std::string total_source_distance_string;
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << total_length_source << "mm";
			total_source_distance_string = format_stream.str();
			// Add the total source distance row
			output_stream << std::setw(totals_row_label_size) << std::right << "Total distance source:";
			output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_source_distance_string << std::setfill(' ');
			// end the row
			output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL) << "\n";

			// start the row
			output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL);
			// Format the total target distance string			
			std::string total_target_distance_string;
			format_stream.str(std::string());
			format_stream << std::fixed << std::setprecision(max_precision) << total_length_target << "mm";
			total_target_distance_string = format_stream.str();
			// Add the total target distance row
			output_stream << std::setw(totals_row_label_size) << std::right << "Total distance target:";
			output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_target_distance_string << std::setfill(' ');
			// end the row
			output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL) << "\n";
		}

		// Add the total count rows
		
		// start the row
		output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL);
		// Add the source count
		output_stream << std::setprecision(0) << std::setw(totals_row_label_size) << std::right << "Total count source:";
		output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_count_source << std::setfill(' ');
		// end the row
		output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL) << "\n";

		// start the row
		output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL);
		// Add the target count
		output_stream << std::setw(totals_row_label_size) << std::right << "Total count target:";
		output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_count_target << std::setfill(' ');
		// end the row
		output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL) << "\n";

		// start the row
		output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL);
		// Add the total percent change row
		std::string total_percent_change_string = utilities::get_percent_change_string(total_count_source, total_count_target, 1);
		output_stream << std::setw(totals_row_label_size) << std::right << "Total percent change:";
		output_stream << std::setw(table_width - totals_row_label_size) << std::setfill('.') << std::right << total_percent_change_string << std::setfill(' ');
		// end the row
		output_stream << box.get_box_replacement_element(utilities::box_drawing::BoxElementEnum::VERTICAL) << "\n";

		// Add the final separator
		box.bottom(output_stream);

		std::string output_string = output_stream.str();
		box.make_replacements(output_string);
		return output_string;
	}

private:
	
	logger* p_logger_;
	int logger_type_;
};

// Struct to hold the progress, statistics, and return values
struct arc_welder_progress {
	arc_welder_progress() :  segment_statistics(segment_statistic_lengths, segment_statistic_lengths_count, NULL), segment_retraction_statistics(segment_statistic_lengths, segment_statistic_lengths_count, NULL), travel_statistics(segment_statistic_lengths, segment_statistic_lengths_count, NULL) {
		percent_complete = 0.0;
		seconds_elapsed = 0.0;
		seconds_remaining = 0.0;
		gcodes_processed = 0;
		lines_processed = 0;
		points_compressed = 0;
		arcs_created = 0;
		arcs_aborted_by_flow_rate = 0;
		num_firmware_compensations = 0;
		num_gcode_length_exceptions = 0;
		source_file_size = 0;
		source_file_position = 0;
		target_file_size = 0;
		compression_ratio = 0;
		compression_percent = 0;
		combine_extrusion_and_retraction = true;
		box_encoding = utilities::box_drawing::BoxEncodingEnum::ASCII;
	}
	double percent_complete;
	double seconds_elapsed;
	double seconds_remaining;
	int gcodes_processed;
	int lines_processed;
	int points_compressed;
	int arcs_created;
	int arcs_aborted_by_flow_rate;
	int num_firmware_compensations;
	int num_gcode_length_exceptions;
	double compression_ratio;
	double compression_percent;
	long source_file_position;
	long source_file_size;
	long target_file_size;
	bool combine_extrusion_and_retraction;
	utilities::box_drawing::BoxEncodingEnum box_encoding;

	source_target_segment_statistics segment_statistics;
	source_target_segment_statistics segment_retraction_statistics;
	source_target_segment_statistics travel_statistics;

	std::string simple_progress_str() const {
		std::stringstream stream;
		if (percent_complete == 0) {
			stream << " 00.0% complete - Estimating remaining time.";
		}
		else if (percent_complete == 100)
		{
			stream << "100.0% complete - " << seconds_elapsed << " seconds total.";
		}
		else {
			stream << " " << std::fixed << std::setprecision(1) << std::setfill('0') << std::setw(4) << percent_complete << "% complete - Estimated " << std::setprecision(0) << std::setw(-1) << seconds_remaining << " of " << seconds_elapsed + seconds_remaining << " seconds remaing.";
		}
		
		return stream.str();
	}

	std::string str() const {

		std::stringstream stream;
		stream << std::fixed << std::setprecision(2);

		stream << " percent_complete:" << percent_complete << ", seconds_elapsed:" << seconds_elapsed << ", seconds_remaining:" << seconds_remaining;
		stream << ", gcodes_processed: " << gcodes_processed;
		stream << ", current_file_line: " << lines_processed;
		stream << ", points_compressed: " << points_compressed;
		stream << ", arcs_created: " << arcs_created;
		stream << ", arcs_aborted_by_flowrate: " << arcs_aborted_by_flow_rate;
		stream << ", num_firmware_compensations: " << num_firmware_compensations;
		stream << ", num_gcode_length_exceptions: " << num_gcode_length_exceptions;
		stream << ", compression_ratio: " << compression_ratio;
		stream << ", size_reduction: " << compression_percent << "% " ;
		return stream.str();
	}
	std::string detail_str() const {
		std::stringstream wstream;
		wstream << "\n";
		
		if (travel_statistics.total_count_source > 0)
		{
			wstream << travel_statistics.str("Target File Travel Statistics", box_encoding) << "\n";
		}

		if (combine_extrusion_and_retraction)
		{
			source_target_segment_statistics combined_stats = source_target_segment_statistics::add(segment_statistics, segment_retraction_statistics);
			wstream << combined_stats.str("Target File Extrusion/Retraction Statistics", box_encoding) << "\n";
		}
		else 
		{
			
			if (segment_retraction_statistics.total_count_source > 0)
			{
				wstream << segment_retraction_statistics.str("Target File Retraction Statistics", box_encoding) << "\n";
			}

			wstream << segment_statistics.str("Target File Extrusion Statistics", box_encoding) << "\n";
		}
		return wstream.str();
	}
	
};
// define the progress callback type 
typedef bool(*progress_callback)(arc_welder_progress, logger* p_logger, int logger_type);
// LOGGER_NAME
#define ARC_WELDER_LOGGER_NAME "arc_welder.gcode_conversion"
// Default argument values
#define DEFAULT_G90_G91_INFLUENCES_EXTRUDER false
#define DEFAULT_GCODE_BUFFER_SIZE 10
#define DEFAULT_G90_G91_INFLUENCES_EXTRUDER false
#define DEFAULT_ALLOW_DYNAMIC_PRECISION false
#define DEFAULT_ALLOW_TRAVEL_ARCS false
#define DEFAULT_EXTRUSION_RATE_VARIANCE_PERCENT 0.05
#define DEFAULT_NOTIFICATION_PERIOD_SECONDS 0.5

struct arc_welder_args
{
	arc_welder_args() {
		set_defaults();
	};
		

	arc_welder_args(std::string source, std::string target, logger* ptr_log)
	{
		set_defaults();
		source_path = source;
		target_path = target;
		log = ptr_log;
	}
		std::string source_path;
		std::string target_path;
		logger* log;
		double resolution_mm;
		double path_tolerance_percent;
		double max_radius_mm;
		int min_arc_segments;
		double mm_per_arc_segment;
		bool g90_g91_influences_extruder;
		bool allow_3d_arcs;
		bool allow_travel_arcs;
		bool allow_dynamic_precision;
		unsigned char default_xyz_precision;
		unsigned char default_e_precision;
		double extrusion_rate_variance_percent;
		int buffer_size;
		int max_gcode_length;
		double notification_period_seconds;
		utilities::box_drawing::BoxEncodingEnum box_encoding;
		
		progress_callback callback;

		std::string str() const {
			std::string log_level_name = "NO_LOGGING";
			if (log != NULL)
			{
				log_level_name = log->get_log_level_name(ARC_WELDER_LOGGER_NAME);
			}
			std::stringstream stream;
			stream << "Arc Welder Arguments\n";
			stream << std::fixed << std::setprecision(2);
			stream << "\tSource File Path             : " << source_path << "\n";
			if (source_path == target_path)
			{
				stream << "\tTarget File Path (overwrite) : " << target_path << "\n";
			}
			else
			{
				stream << "\tTarget File Path             : " << target_path << "\n";
			}
			stream << "\tResolution                   : " << resolution_mm << "mm (+-" << std::setprecision(5) << resolution_mm / 2.0 << "mm)\n";
			stream << "\tPath Tolerance               : " << std::setprecision(3) << path_tolerance_percent * 100.0 << "%\n";
			stream << "\tMaximum Arc Radius           : " << std::setprecision(1) << max_radius_mm << "mm\n";
			bool firmware_compensation_enabled = min_arc_segments > 0 && mm_per_arc_segment > 0.0;
			stream << "\tFirmware Compensation        : " << (firmware_compensation_enabled ? "True" : "False") << "\n";
			if (firmware_compensation_enabled)
			{
				stream << "\tMin Arc Segments             : " << std::setprecision(0) << min_arc_segments << "\n";
				stream << "\tMM Per Arc Segment           : " << std::setprecision(3) << mm_per_arc_segment << "\n";
			}
			
			stream << "\tAllow 3D Arcs                : " << (allow_3d_arcs ? "True" : "False") << "\n";
			stream << "\tAllow Travel Arcs            : " << (allow_travel_arcs ? "True" : "False") << "\n";
			stream << "\tAllow Dynamic Precision      : " << (allow_dynamic_precision ? "True" : "False") << "\n";
			stream << "\tDefault XYZ Precision        : " << std::setprecision(0) << static_cast<int>(default_xyz_precision) << "\n";
			stream << "\tDefault E Precision          : " << std::setprecision(0) << static_cast<int>(default_e_precision) << "\n";
			stream << "\tExtrusion Rate Variance      : ";
			if (extrusion_rate_variance_percent == 0)
			{
				stream << "Unlimited";
			}
			else
			{
				stream << extrusion_rate_variance_percent * 100.0 << "%";
			}
			stream << "\n";

			stream << "\tG90/G91 Influences Extruder  : " << (g90_g91_influences_extruder ? "True" : "False") << "\n";
			if (max_gcode_length == 0)
			{
				stream << "\tMax Gcode Length             : Unlimited\n";
			}
			else {
				stream << "\tMax Gcode Length             : " << std::setprecision(0) << max_gcode_length << " characters\n";
			}
			stream << "\tLog Level                    : " << log_level_name << "\n";
			stream << "\tHide Progress Updates        : " << (callback == NULL ? "True" : "False") << "\n";
			stream << "\tProgress Notification Period : " << std::setprecision(2) << notification_period_seconds << " seconds";
			return stream.str();
		};
		
private:
	void set_defaults()
	{
			source_path = "",
			target_path = "",
			log = NULL,
			resolution_mm = DEFAULT_RESOLUTION_MM,
			path_tolerance_percent = ARC_LENGTH_PERCENT_TOLERANCE_DEFAULT,
			max_radius_mm = DEFAULT_MAX_RADIUS_MM,
			min_arc_segments = DEFAULT_MIN_ARC_SEGMENTS,
			mm_per_arc_segment = DEFAULT_MM_PER_ARC_SEGMENT,
			g90_g91_influences_extruder = DEFAULT_G90_G91_INFLUENCES_EXTRUDER,
			allow_3d_arcs = DEFAULT_ALLOW_3D_ARCS,
			allow_travel_arcs = DEFAULT_ALLOW_TRAVEL_ARCS,
			allow_dynamic_precision = DEFAULT_ALLOW_DYNAMIC_PRECISION,
			default_xyz_precision = DEFAULT_XYZ_PRECISION,
			default_e_precision = DEFAULT_E_PRECISION,
			extrusion_rate_variance_percent = DEFAULT_EXTRUSION_RATE_VARIANCE_PERCENT,
			max_gcode_length = DEFAULT_MAX_GCODE_LENGTH,
			buffer_size = DEFAULT_GCODE_BUFFER_SIZE,
			notification_period_seconds = DEFAULT_NOTIFICATION_PERIOD_SECONDS,
			callback = NULL;
			box_encoding = utilities::box_drawing::BoxEncodingEnum::ASCII;
	}

};

struct arc_welder_results {
	arc_welder_results() : progress()
	{
		success = false;
		cancelled = false;
		message = "";
	}
	bool success;
	bool cancelled;
	std::string message;
	arc_welder_progress progress;
};

class arc_welder
{
public:
	
	arc_welder(arc_welder_args args);
	
	
	void set_logger_type(int logger_type);
	virtual ~arc_welder();
	arc_welder_results process();
	
protected:
	virtual bool on_progress_(const arc_welder_progress& progress);
private:
	
	arc_welder_progress get_progress_(long source_file_position, double start_clock);
	void add_arcwelder_comment_to_target();
	void reset();
	static gcode_position_args get_args_(bool g90_g91_influences_extruder, int buffer_size);
	progress_callback progress_callback_;
	int process_gcode(parsed_command cmd, bool is_end, bool is_reprocess);
	void write_arc_gcodes(double current_feedrate);
	int write_gcode_to_file(std::string gcode);
	std::string get_arc_gcode(const std::string comment);
	std::string get_comment_for_arc();
	int write_unwritten_gcodes_to_file();
	std::string create_g92_e(double absolute_e);
	std::string source_path_;
	std::string target_path_;
	double resolution_mm_;
	gcode_position_args gcode_position_args_;
	bool allow_dynamic_precision_;
	bool allow_3d_arcs_;
	bool allow_travel_arcs_;
	long file_size_;
	int lines_processed_;
	int gcodes_processed_;
	int last_gcode_line_written_;
	int points_compressed_;
	int arcs_created_;
	int arcs_aborted_by_flow_rate_;
	double notification_period_seconds_;
	source_target_segment_statistics segment_statistics_;
	source_target_segment_statistics segment_retraction_statistics_;
	source_target_segment_statistics travel_statistics_;
	long get_file_size(const std::string& file_path);
	double get_time_elapsed(double start_clock, double end_clock);
	double get_next_update_time() const;
	bool waiting_for_arc_;
	array_list<unwritten_command> unwritten_commands_;
	segmented_arc current_arc_;
	std::ofstream output_file_;

	// We don't care about the printer settings, except for g91 influences extruder.
	gcode_position* p_source_position_;
	double previous_feedrate_;
	double previous_extrusion_rate_;
	double extrusion_rate_variance_percent_;
	gcode_parser parser_;
	bool verbose_output_;
	int logger_type_;
	logger* p_logger_;
	bool debug_logging_enabled_;
	bool info_logging_enabled_;
	bool verbose_logging_enabled_;
	bool error_logging_enabled_;
	utilities::box_drawing::BoxEncodingEnum box_encoding_;
};
