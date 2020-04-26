////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Gcode Processor Library
//
// Tools for parsing gcode and calculating printer state from parsed gcode commands.
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

#include "parsed_command_parameter.h"
#include "parsed_command.h"
parsed_command_parameter::parsed_command_parameter()
{
	value_type = 'N';
	name.reserve(1);
}

parsed_command_parameter::parsed_command_parameter(const std::string name, double value) : name(name), double_value(value)
{
	value_type = 'F';
}

parsed_command_parameter::parsed_command_parameter(const std::string name, const std::string value) : name(name), string_value(value)
{
	value_type = 'S';
}

parsed_command_parameter::parsed_command_parameter(const std::string name, const unsigned long value) : name(name), unsigned_long_value(value)
{
	value_type = 'U';
}
parsed_command_parameter::~parsed_command_parameter()
{

}
