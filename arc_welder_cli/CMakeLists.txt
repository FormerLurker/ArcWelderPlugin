cmake_minimum_required(VERSION 3.6)
project(arc_welder)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")

file(GLOB src
    "../octoprint_arc_welder/data/lib/c/arc_welder/*.cpp"
    "../octoprint_arc_welder/data/lib/c/arc_welder/*.h"
    "../octoprint_arc_welder/data/lib/c/gcode_processor_lib/*.cpp"
    "../octoprint_arc_welder/data/lib/c/gcode_processor_lib/*.h"
    "main.cpp"
    )

add_executable(arc_welder ${src})

# target_link_libraries(arc_welder )
target_include_directories(arc_welder PUBLIC ../octoprint_arc_welder/data/lib/c/gcode_processor_lib ../octoprint_arc_welder/data/lib/c/arc_welder)
