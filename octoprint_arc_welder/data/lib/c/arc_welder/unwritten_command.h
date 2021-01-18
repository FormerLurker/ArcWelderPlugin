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
#include "parsed_command.h"
#include "position.h"
struct unwritten_command
{
	unwritten_command() {
		is_extruder_relative = false;
		e_relative = 0;
		offset_e = 0;
		extrusion_length = 0;
	}
	unwritten_command(parsed_command &cmd, bool is_relative, double command_length) {
		is_extruder_relative = is_relative;
		gcode = cmd.gcode;
		comment = cmd.comment;
		extrusion_length = command_length;
	}
	unwritten_command(position* p, double command_length) {
	  
		e_relative = p->get_current_extruder().e_relative;
		offset_e = p->get_current_extruder().get_offset_e();
		is_extruder_relative = p->is_extruder_relative;
		gcode = p->command.gcode;
		comment = p->command.comment;
		extrusion_length = command_length;
	}
	bool is_extruder_relative;
	double e_relative;
	double offset_e;
	double extrusion_length;
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

