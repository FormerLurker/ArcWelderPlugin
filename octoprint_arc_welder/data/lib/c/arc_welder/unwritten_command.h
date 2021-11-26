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
#include "parsed_command.h"
#include "position.h"
struct unwritten_command
{
	unwritten_command() {
		is_extruder_relative = false;
		length = 0;
		is_g0_g1 = false;
		is_g2_g3 = false;
		is_travel = false;
		is_extrusion = false;
		is_retraction = false;
		gcode = "";
		comment = "";
	}
	unwritten_command(parsed_command &cmd, bool is_relative, bool is_extrusion, bool is_retraction, bool is_travel, double command_length) 
		: is_extruder_relative(is_relative), is_extrusion(is_extrusion), is_retraction(is_retraction), is_travel(is_travel), is_g0_g1(cmd.command == "G0" || cmd.command == "G1"), is_g2_g3(cmd.command == "G2" || cmd.command == "G3"), gcode(cmd.gcode), comment(cmd.comment), length(command_length)
	{

	}
	bool is_g0_g1;
	bool is_g2_g3;
	bool is_extruder_relative;
	bool is_travel;
	bool is_extrusion;
	bool is_retraction;
	double length;
	std::string gcode;
	std::string comment;

	std::string to_string()
	{
		if (comment.size() > 0)
		{
			return gcode + ";" + comment;
		}
		return gcode;
	}
};

