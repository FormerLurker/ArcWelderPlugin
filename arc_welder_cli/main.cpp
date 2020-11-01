#include <iostream>
#include <string>
#include <vector>
#include <stdlib.h>

#include "arc_welder.h"
#include "logger.h"

using namespace std;

bool arc_progress_callback(arc_welder_progress progress, logger* p_logger, int logger_type) {
    cout << progress.str() << "\n";
    return true;
}

int main(int argc, char* argv[]) {
    std::vector<string> args;
	cerr << "PyArcWelder V0.1.0rc1.dev2 imported - Copyright (C) 2019  Brad Hochgesang...\n";

    if (argc > 1) {
        args.assign(argv + 1, argv + argc);
    }

    if (args.size() < 2) {
        cerr << "Usage: arc_welder [options] <infile> <outfile>\n";
        cerr << "Available options:\n";
        cerr << "-r <mm>        --resolution <mm>   This setting controls how much play *Arc Welder* has in converting GCode points into arcs.  If the arc deviates from the original points by + or - 1/2 of the resolution, the points will **not** be converted.  The default setting is 0.05 which means the arcs may not deviate by more than +- 0.025mm (that's a **really** tiny deviation).  Increasing the resolution will result in more arcs being converted but will make the tool paths less accurate.  Decreasing the resolution will result in fewer arcs but more accurate tool paths.  I don't recommend going above 0.1MM.  Higher values than that may result in print failure.\n\n";
        cerr << "-i <mm>        --radius <mm>       This is a safety feature to prevent unusually large arcs from being generated.  Internally, *Arc Welder* uses a constant to prevent an arc with a very large radius from being generated where the path is essentially (but not exactly) a straight line.  If it is not perfectly straight and if my constant isn't conservative enough, an extremely large arc could be created that may have the wrong direction of rotation.  The default setting is **1000000 mm** or **1KM**.\n\n";
        cerr << "-g                                 This flag disables use of G90/G91.  *Arc Welder* will use this setting to determine if the G90/G91 command influences your extruder's axis mode.  In general, Marlin 2.0 and forks SHOULD NOT have this flag.  Many forks of Marlin 1.x SHOULD have this flag, like the Prusa MK2 and MK3.\n\n";
        return 1;
    }

    double resolution_mm = 0.05;
    double max_radius_mm = 1000.0*1000.0;
    bool g90_g91_influences_extruder = true;
    for (size_t i = 0; i < args.size() - 2; ++i) {
        string arg = args[i];
        if (arg == "-r" || arg == "--resolution") {
            if (i >= args.size() - 3) {
                cerr << "Missing mm for resolution, run `arc_welder` to see help.\n";
                return 1;
            }
            resolution_mm = strtod(args[i + 1].c_str(), NULL);
            if (resolution_mm <= 0.0 || resolution_mm > 1000*1000) {
                cerr << "Invalid mm for resolution, run `arc_welder` to see help.\n";
                return 1;
            }
        } else if (arg == "-i" || arg == "--radius") {
            if (i >= args.size() - 3) {
                cerr << "Missing mm for radius, run `arc_welder` to see help.\n";
                return 1;
            }
            max_radius_mm = strtod(args[i + 1].c_str(), NULL);
            if (max_radius_mm <= 0.0 || max_radius_mm > 1000*1000*1000) {
                cerr << "Invalid mm for radius, run `arc_welder` to see help.\n";
                return 1;
            }
        } else if (arg == "-g") {
            g90_g91_influences_extruder = false;
        } else {
            cerr << "Invalid flag: '" << arg << "', run `arc_welder` to see help.\n";
            return 1;
        }
    }
    std::vector<string> logger_names;
	logger_names.push_back("arc_welder.gcode_conversion");
	std::vector<int> logger_levels;
	logger_levels.push_back(log_levels::DEBUG);
	logger logs(logger_names, logger_levels);
    logs.set_log_level(log_levels::INFO);
    logs.set_streams(&cerr, &cerr);
    // std::string source_path, std::string target_path, logger* log, double resolution_mm, double max_radius, bool g90_g91_influences_extruder, int buffer_size, progress_callback callback = NULL
    arc_welder welder(args[0], args[1], &logs, resolution_mm, max_radius_mm, g90_g91_influences_extruder, 50, &arc_progress_callback);
    auto results = welder.process();
    if (results.success) {
        cout << results.progress.str() << "\n";
        if (results.message.length() > 0) {
            cerr << "Finished successfully with message: " << results.message << "\n";
        } else {
            cerr << "Finished successfully\n";
        }
        return 0;
    } else if (results.cancelled) {
        if (results.message.length() > 0) {
            cerr << "Cancelled with message: " << results.message << "\n";
        } else {
            cerr << "Cancelled\n";
        }
        return 1;
    } else {
        cerr << "Weird state, neither successful nor cancelled\n";
        return 1;
    }
}