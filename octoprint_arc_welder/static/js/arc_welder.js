/*################################################################################
# Arc Welder: Anti-Stutter
#
# A plugin for OctoPrint that converts G0/G1 commands into G2/G3 commands where possible and ensures that the tool
# paths don't deviate by more than a predefined resolution.  This compresses the gcode file sice, and reduces reduces
# the number of gcodes per second sent to a 3D printer that supports arc commands (G2 G3)
#
# Copyright (C) 2020  Brad Hochgesang
# #################################################################################
# This program is free software:
# you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see the following:
# https://github.com/FormerLurker/ArcWelderPlugin/blob/master/LICENSE
#
# You can contact the author either through the git-hub repository, or at the
# following email address: FormerLurker@pm.me
################################################################################### */
$(function () {
    // ArcWelder Global
    ArcWelder = {};
    ArcWelder.PLUGIN_ID = "arc_welder";

    ArcWelder.setLocalStorage = function (name, value) {
        localStorage.setItem("arc_welder_" + name, value)
    };

    ArcWelder.getLocalStorage = function (name, value) {
        return localStorage.getItem("arc_welder_" + name)
    };

    ArcWelder.parseFloat = function (value) {
        var ret = parseFloat(value);
        if (!isNaN(ret))
            return ret;
        return null;
    };
    // extenders

    ko.extenders.arc_welder_numeric = function (target, precision) {
        var result = ko.dependentObservable({
            read: function () {
                var val = target();
                val = ArcWelder.parseFloat(val);
                if (val == null)
                    return val;
                try {
                    // safari doesn't seem to like toFixed with a precision > 20
                    if (precision > 20)
                        precision = 20;
                    return val.toFixed(precision);
                } catch (e) {
                    console.error("Error converting toFixed");
                }

            },
            write: target
        });

        result.raw = target;
        return result;
    };
    //ArcWelder.pnotify = PNotifyExtensions({});
    ArcWelder.APIURL = function(fn){
        return "./plugin/" + ArcWelder.PLUGIN_ID + "/" + fn;
    };
    ArcWelder.HelpDocumentRootUrl = ArcWelder.APIURL("static/docs/help/");
    ArcWelder.PopupKey = function(key) {
        if (Array.isArray(key))
        {
            var keys = [];
            for (var index=0; index < key.length; index++)
            {
                keys.push(ArcWelder.PLUGIN_ID + "_" + key);
            }
            return keys;
        }
        return ArcWelder.PLUGIN_ID + "_" + key;
    };
    ArcWelder.Help = new MarkdownHelp({
        plugin_id: ArcWelder.PLUGIN_ID,
        plugin_name: "Arc Welder: Anti Stutter",
        missing_file_text: "No help file is available, please report this issue within the github repository.",
        add_class: 'arc-welder-pnotify-help',
        document_root_url: ArcWelder.HelpDocumentRootUrl
    });

    ArcWelder.ToTimer = function (seconds) {
        if (seconds == null)
            return "";
        if (seconds <= 0)
            return "0:00";

        seconds = Math.round(seconds);

        var hours = Math.floor(seconds / 3600).toString();
        if (hours > 0) {
            return ("" + hours).slice(-2) + " Hrs";
        }

        seconds %= 3600;
        var minutes = Math.floor(seconds / 60).toString();
        seconds = (seconds % 60).toString();
        return ("0" + minutes).slice(-2) + ":" + ("0" + seconds).slice(-2);
    };

    var byte = 1024;
    ArcWelder.toFileSizeString = function (bytes, precision) {
        precision = precision || 0;

        if (Math.abs(bytes) < byte) {
            return bytes + ' B';
        }
        var units = ['kB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'];
        var u = -1;
        do {
            bytes /= byte;
            ++u;
        } while (Math.abs(bytes) >= byte && u < units.length - 1);
        return bytes.toFixed(precision) + ' ' + units[u];
    };

    ArcWelder.FILE_PROCESSING_BOTH = "both";
    ArcWelder.FILE_PROCESSING_AUTO = "auto-only";
    ArcWelder.FILE_PROCESSING_MANUAL = "manual-only";
    ArcWelder.FILE_PROCESSING_OPTIONS = [
        {name:"Automatic and Manual Processing", value: ArcWelder.FILE_PROCESSING_BOTH},
        {name:"Automatic Processing Only", value: ArcWelder.FILE_PROCESSING_AUTO},
        {name:"Manual Processing Only", value: ArcWelder.FILE_PROCESSING_MANUAL}
    ];

    ArcWelder.ArcWelderViewModel = function (parameters) {
        var self = this;
        // variable to hold the settings view model.
        self.settings = parameters[0];
        // variables to hold info from external viewmodels
        self.login_state = parameters[1];
        self.files = parameters[2];
        self.printer_state = parameters[3];

        self.plugin_settings = null;
        self.preprocessing_job_guid = null;
        self.pre_processing_progress = null;
        self.version = ko.observable();
        self.git_version = ko.observable();
        self.selected_filename = ko.observable();
        self.selected_file_is_new = ko.observable(false);
        self.statistics_shown = ko.observable(null);
        self.current_statistics_file = ko.observable();
        self.statistics = {};
        self.statistics.gcodes_processed = ko.observable();
        self.statistics.lines_processed = ko.observable();
        self.statistics.points_compressed = ko.observable();
        self.statistics.arcs_created = ko.observable();
        self.statistics.source_file_size = ko.observable();
        self.statistics.target_file_size = ko.observable();
        self.statistics.compression_ratio = ko.observable().extend({arc_welder_numeric: 1});
        self.statistics.compression_percent = ko.observable().extend({arc_welder_numeric: 1});
        self.statistics.source_filename = ko.observable();
        self.statistics.target_filename = ko.observable();

        self.statistics.segment_statistics_text = ko.observable();
        self.current_files = null;

        self.statistics_shown.subscribe(
        function(newValue)
        {
            self.toggleStatistics(newValue);
        });
        self.current_statistics_file.subscribe(
        function(newValue) {
            self.loadStats(newValue);
            ArcWelder.setLocalStorage("stat_file_path", newValue.path)
            ArcWelder.setLocalStorage("stat_file_origin", newValue.origin);
        });

        self.selected_filename_title = ko.pureComputed(function() {
            var title = "Selected";
            if (self.selected_file_is_new())
            {
                title = "Processed";
            }
            return title;
        })
        self.auto_pre_processing_enabled = ko.pureComputed(function(){
            var file_processing_type = self.plugin_settings.feature_settings.file_processing();
            return file_processing_type === ArcWelder.FILE_PROCESSING_AUTO ||
                file_processing_type === ArcWelder.FILE_PROCESSING_BOTH;
        });

        self.manual_pre_processing_enabled = ko.pureComputed(function(){
            var file_processing_type = self.plugin_settings.feature_settings.file_processing();
            return file_processing_type === ArcWelder.FILE_PROCESSING_MANUAL ||
                file_processing_type === ArcWelder.FILE_PROCESSING_BOTH;
        });

        self.github_link = ko.pureComputed(function(){
            var git_version = self.git_version();
            if (!git_version)
                return null;
            // If this is a commit, link to the commit
            if (self.version().includes("+"))
            {
                return  'https://github.com/FormerLurker/ArcWelderPlugin/commit/' + git_version;
            }
            // This is a release, link to the tag
            return 'https://github.com/FormerLurker/ArcWelderPlugin/releases/tag/' + self.version();
        });

        self.version_text = ko.pureComputed(function () {
            if (self.version() && self.version() !== "unknown") {
                return "v" + self.version();
            }
            return "unknown";
        });

        self.onBeforeBinding = function () {
            // Make plugin setting access a little more terse
            self.plugin_settings = self.settings.settings.plugins.arc_welder;
            self.version(self.plugin_settings.version());
            self.git_version(self.plugin_settings.git_version());
        };

        self.onAfterBinding = function() {
            ArcWelder.Help.bindHelpLinks("#tab_plugin_arc_welder_controls");
        };

        self.onStartupComplete = function() {
            var startupComplete = false;
            self.current_files = ko.computed( function() {

                if (self.current_statistics_file())
                {
                    // reload the currently selected stats
                    self.loadStats(self.current_statistics_file());
                }
                // Add buttons to the file manager for arcwelder.
                self.addProcessButtonToFileManager(self.files.listHelper.paginatedItems(), self.printer_state.isPrinting());
                if (!startupComplete && self.files.allItems() && self.files.allItems().length > 0)
                {
                    startupComplete = true;
                    var stat_data = {
                        path: ArcWelder.getLocalStorage("stat_file_path") || self.printer_state.filepath(),
                        origin: ArcWelder.getLocalStorage("stat_file_origin") || !self.printer_state.sd() ? 'local' : 'sdcard'
                    };

                    if (stat_data.origin == "local")
                    {
                        self.current_statistics_file(stat_data);
                    }
                }
            }, this);
            var show_stats = ArcWelder.getLocalStorage("show_stats") == "true";
            self.statistics_shown(show_stats);
        };

        self.closePreprocessingPopup = function(){
            if (self.pre_processing_progress != null) {
                self.pre_processing_progress.close();
            }
            self.preprocessing_job_guid = null;
            self.pre_processing_progress = null;
        };

        self.showHideStatsBtnClicked = function(){
            var shown = !self.statistics_shown(); // what????
            self.statistics_shown(shown);
            ArcWelder.setLocalStorage("show_stats", shown);
        };

        self.toggleStatistics = function(show) {
            var $statsButton = $("#arc-welder-show-statistics-btn");
            var $statsDiv = $("#arc-welder-stats");
            var $statsContainer = $("#arc-welder-stats-container");

            if (show)
            {
                $statsContainer.hide();
                $statsButton.text("Show Stats")

            }
            else
            {
                $statsContainer.show();
                $statsButton.text("Hide Stats")

            }
        };

        self.loadStats = function(file_data) {
            // Hide everything to start.
            var $statsButton = $("#arc-welder-show-statistics-btn");
            var $statsDiv = $("#arc-welder-stats");
            var $statsNoStatsDiv = $statsDiv.find("#arc-welder-no-stats-div");
            var $statsTextDiv = $statsDiv.find("#arc-welder-stats-text-div");

            // Hide all the divs and clear text
            $statsTextDiv.hide();
            $statsDiv.hide();
            $statsNoStatsDiv.hide();
            $statsButton.hide();

            var filename, is_welded, statistics, is_new;

            if (file_data.arc_welder_statistics) {
                var filename = file_data.name;
                var is_welded = true;
                var statistics = file_data.arc_welder_statistics;
                is_new = true;
            } else
            {
                is_new = false;
                var file = self.getFile(file_data);
                if (!file)
                {
                    self.toggleStatistics(false);
                    ArcWelder.setLocalStorage("show_stats", false);
                    return;
                }
                filename = file.name;
                is_welded = file.arc_welder;
                statistics = file.arc_welder_statistics;
            }


            // Update the UI
            self.selected_filename(filename);
            self.selected_file_is_new(is_new);
            if (statistics)
            {
                self.statistics.gcodes_processed(statistics.gcodes_processed);
                self.statistics.lines_processed(statistics.lines_processed);
                self.statistics.points_compressed(statistics.points_compressed);
                self.statistics.arcs_created(statistics.arcs_created);
                self.statistics.source_file_size(ArcWelder.toFileSizeString(statistics.source_file_size, 1));
                self.statistics.target_file_size(ArcWelder.toFileSizeString(statistics.target_file_size));
                self.statistics.compression_ratio(statistics.compression_ratio);
                self.statistics.compression_percent(statistics.compression_percent);
                self.statistics.source_filename(statistics.source_filename);
                self.statistics.target_filename(statistics.target_filename);
                self.statistics.segment_statistics_text(statistics.segment_statistics_text);
            }
            if (is_welded)
            {
                $statsDiv.show();
                $statsButton.show();
                if (statistics)
                {
                    $statsTextDiv.show();
                }
                else
                {
                    $statsNoStatsDiv.show();
                }
            }

        };

        // receive events
        self.onEventFileSelected = function(payload) {
            self.current_statistics_file(payload);
        }
        // Handle Plugin Messages from Server

        self.onDataUpdaterPluginMessage = function (plugin, data) {
            if (plugin !== "arc_welder") {
                return;
            }
            switch (data.message_type) {
                case "toast":
                    var options = {
                        title: data.title,
                        text: data.message,
                        type: data.toast_type,
                        hide: data.auto_hide,
                        addclass: "arc-welder",
                        desktop: {
                            desktop: true
                        }
                    };

                    PNotifyExtensions.displayPopupForKey(
                        options,
                        ArcWelder.PopupKey(data.key),
                        ArcWelder.PopupKey(data.close_keys)
                    );
                    break;
                case "preprocessing-start":
                    self.createProgressPopup(data.preprocessing_job_guid, data.source_filename);
                    break;
                case "preprocessing-failed":
                    var options = {
                        title: "Arc Welder - Failed",
                        text: data.message,
                        type: "error",
                        hide: true,
                        addclass: "arc-welder",
                        desktop: {
                            desktop: true
                        }
                    };
                    PNotifyExtensions.displayPopupForKey(options, ArcWelder.PopupKey("preprocessing-failed"), []);
                    self.closePreprocessingPopup();
                    break;
                case "preprocessing-cancelled":
                    var options = {
                        title: "Arc Welder - Cancelled",
                        text: data.message,
                        type: "info",
                        hide: true,
                        addclass: "arc-welder",
                        desktop: {
                            desktop: true
                        }
                    };
                    PNotifyExtensions.displayPopupForKey(options, ArcWelder.PopupKey("preprocessing-cancelled"), []);
                    self.closePreprocessingPopup();
                    break;
                case "preprocessing-success":
                    self.closePreprocessingPopup();
                    //  Load all stats for the newly processed file
                    self.current_statistics_file(data);

                    var metadata = data.arc_welder_statistics;
                    var seconds_elapsed = metadata.seconds_elapsed;
                    var arcs_created = metadata.arcs_created;
                    var points_compressed = metadata.points_compressed;
                    var source_file_size = metadata.source_file_size;
                    var target_file_size = metadata.target_file_size;
                    var compression_ratio = metadata.compression_ratio;
                    var compression_percent = metadata.compression_percent;
                    var space_saved_string = ArcWelder.toFileSizeString(source_file_size - target_file_size, 1);
                    var source_size_string = ArcWelder.toFileSizeString(source_file_size, 1);
                    var target_size_string = ArcWelder.toFileSizeString(target_file_size, 1);
                    var file_name_html = "";
                    if (data.source_filename == data.target_filename)
                    {
                        file_name_html = "<div><strong>File:</strong> " + data.source_filename + "<div>";
                    }
                    else
                    {
                        file_name_html = "<div><strong>Source File:</strong> " + data.source_filename + "<div>" +
                            "<div><strong>Target File:</strong> " + data.target_filename + "<div>";
                    }
                    var progress_text =
                            file_name_html + "<div><strong>Time:</strong> " + ArcWelder.ToTimer(seconds_elapsed) + "</div><div class='row-fluid'><span class='span5'><strong>Arcs Created</strong><br/>" + arcs_created.toString() + "</span>"
                            + "<span class='span7'><strong>Points Compressed</strong><br/>" + points_compressed.toString() + "</span></div>"
                            + "<div class='row-fluid'><div class='span5'><strong>Compression</strong><br/> Ratio: " + compression_ratio.toFixed(1) + " - " + compression_percent.toFixed(1) + "%</div><div class='span7'><strong>Space</strong><br/>"+ source_size_string + " - " + space_saved_string + " = " + target_size_string + "</div></div>";
                    var options = {
                        title: "Arc Welder Preprocessing Complete",
                        text: progress_text,
                        type: "success",
                        hide: true,
                        addclass: "arc-welder",

                    };
                    PNotifyExtensions.displayPopupForKey(options, ArcWelder.PopupKey("preprocessing-success"));
                    progress_text =
                        "Preprocessing completed in " + ArcWelder.ToTimer(seconds_elapsed) + " seconds.  " + arcs_created.toString() + " arcs were created and "
                        + points_compressed.toString() + " points were compressed.";
                    options = {
                        title: "Arc Welder Preprocessing Complete",
                        text: "Preprocessing Completed",
                        type: "success",
                        hide: false,
                        desktop: {
                            desktop: true,
                            fallback: false
                        }
                    };
                    PNotifyExtensions.displayPopupForKey(
                        options,
                        ArcWelder.PopupKey("preprocessing-success-desktop"),
                        []
                    );
                    break;
                case "preprocessing-complete":
                    self.closePreprocessingPopup();
                    break;
                case "preprocessing-progress":
                    // TODO: CHANGE THIS TO A PROGRESS INDICATOR
                    var progress = data;
                    var seconds_elapsed = progress.seconds_elapsed;
                    var seconds_remaining = progress.seconds_remaining;
                    var percent_complete = progress.percent_complete;
                    var arcs_created = progress.arcs_created;
                    var points_compressed = progress.points_compressed;
                    var source_file_size = progress.source_file_size;
                    var target_file_size = progress.target_file_size;
                    var compression_ratio = progress.compression_ratio;
                    var compression_percent = progress.compression_percent;
                    var source_file_position = progress.source_file_position;
                    var space_saved_string = ArcWelder.toFileSizeString(source_file_size - target_file_size, 1);
                    var source_position_string = ArcWelder.toFileSizeString(source_file_position, 1);
                    var target_size_string = ArcWelder.toFileSizeString(target_file_size, 1);

                    if (self.pre_processing_progress == null){
                        self.createProgressPopup(data.preprocessing_job_guid, data.source_filename);
                    }

                    var progress_text =
                          "<div class='row-fluid'>"
                        + "<span class='span5'><strong>Remaining:&nbsp;</strong>" + ArcWelder.ToTimer(seconds_remaining) + "<br/> <strong>Arcs Created</strong><br/>" + arcs_created.toString() + "</span>"
                        + "<span class='span7'><strong>Elapsed:</strong>&nbsp;" + ArcWelder.ToTimer(seconds_elapsed) + "<br/><strong>Points Compressed</strong><br/>" + points_compressed.toString() + "</span>"
                        + "<div/>"
                        + "<div class='row-fluid'>"
                        + "<div class='span5'><strong>Compression</strong><br/> Ratio: " + compression_ratio.toFixed(1) + " - " + compression_percent.toFixed(1) + "%</div>"
                        + "<div class='span7'><strong>Space</strong><br/>"+ source_position_string + " - " + space_saved_string + " = " + target_size_string + "</div>"
                        + "</div>";
                        //+ "  Line:" + lines_processed.toString()
                        //+ "<div class='row-fluid'><span class='span6'><strong>Arcs Created</strong><br/>" + arcs_created.toString() + "</span>"
                        //+ "<span class='span6'><strong>Points Compressed</strong><br/>" + points_compressed.toString() + "</span><div/>";
                    self.pre_processing_progress = self.pre_processing_progress.update(
                        percent_complete, progress_text
                    );
                    break;
                default:
                    var options = {
                        title: "Arc Welder Error",
                        text: "An unknown message was received.  This popup should have been removed prior to release.",
                        type: "error",
                        hide: false,
                        addclass: "arc-welder",
                        desktop: {
                            desktop: true
                        }
                    };
                    PNotifyExtensions.displayPopupForKey(
                        options,
                        ArcWelder.PopupKey("unknown_message_type"),
                        ArcWelder.PopupKey(["unknown_message_type"])
                    );
            }
        };

        self.createProgressPopup = function(preprocessing_job_guid, source_filename) {
            if (self.pre_processing_progress == null)
                self.closePreprocessingPopup();
            self.preprocessing_job_guid = preprocessing_job_guid;
            var cancel_callback = null;
            var cancel_all_callback = null;
            if (self.login_state.isAdmin()){
                cancel_callback = self.cancelPreprocessing;
                cancel_all_callback = self.cancelAllPreprocessing;
            }
            var subtitle = "<strong>Processing:</strong> " + source_filename;
            self.pre_processing_progress = PNotifyExtensions.progressBar("Initializing...", "Arc Welder Progress",
                subtitle, cancel_callback, "Cancel All", cancel_all_callback);
        };

        self.cancelAllPreprocessing = function() {
            self.cancelPreprocessingRequest(true);
        };

        self.cancelPreprocessing = function () {
            self.cancelPreprocessingRequest(false);
        };

        self.cancelPreprocessingRequest = function(cancel_all){
            var data = {
                "cancel_all": cancel_all,
                "preprocessing_job_guid": self.preprocessing_job_guid
            };
            $.ajax({
                url: ArcWelder.APIURL("cancelPreprocessing"),
                type: "POST",
                tryCount: 0,
                retryLimit: 3,
                contentType: "application/json",
                data: JSON.stringify(data),
                dataType: "json",
                success: function() {
                    self.closePreprocessingPopup();
                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    var message = "Could not cancel preprocessing.  Status: " + textStatus + ".  Error: " + errorThrown;
                    var options = {
                        title: 'Error Cancelling Process',
                        text: message,
                        type: 'error',
                        hide: false,
                        addclass: "arc-welder",
                        desktop: {
                            desktop: true
                        }
                    };
                    PNotifyExtensions.displayPopupForKey(
                        options,
                        ArcWelder.PopupKey("cancel-popup-error"),
                        ArcWelder.PopupKey(["cancel-popup-error"])
                    );
                    return false;
                }
            });
        }

        self.removeEditButtons = function() {
            $("#files div.gcode_files div.entry .action-buttons div.arc-welder").remove();
        };

        self.getEntryId = function(file){
            return "gcode_file_" + md5(file.origin + ":" + file.path);
        };

        self.addProcessButtonToFileManager = function(current_page, is_printing) {
            self.removeEditButtons();
            if (!self.manual_pre_processing_enabled())
                return;
            console.log("Adding Buttons");
            for(var file_index=0; file_index < current_page.length; file_index++)
            {
                // Get the current file
                var file = current_page[file_index];
                console.log("Adding Buttons for file with hash: " + file.hash);
                // Only process machine code
                if (file.type !== "machinecode")
                    continue;
                // Construct the ID of the file;
                var current_file_id = self.getEntryId(file);
                // Get the element
                var file_element = $("#" + current_file_id);
                if (file_element.length !== 1)
                    continue;

                var is_welded = false;
                var title = "Weld Arcs";
                if (is_printing)
                {
                    is_welded = true;
                    title = "Cannot weld arcs during a print, this would impact performance.";
                }
                else if (file.origin !== "local")
                {
                    is_welded = true;
                    title = "Cannot weld arcs for files stored on your printer's SD card.";
                }
                else if (file.arc_welder)
                {
                    is_welded = true;
                    title = "View Arc-Welder statistics for this file.";
                }
                // Create the button

                var $button = $('\
                    <div class="btn btn-mini arc-welder" title="' + title + '">\
                        <i class="fa ' + (is_welded ? "fa-file-text" : "fa-compress") + '"></i>\
                    </div>\
                ');
                // Add an on click event if the button is not disabled
                //if (!is_welded)
                //{

                    var data = {path: file.path, origin: file.origin};
                    $button.click(data, function(e) {
                        self.processButtonClicked(e);
                    });
                //}

                // Add the button to the file manager
                $(file_element).find("a.btn-mini").after($button);
            }

        };

        self.recursiveFileSearch = function(root, path, origin) {
            if (root === undefined) {
                return false;
            }

            for (var index = 0; index < root.length; index++)
            {
                var file = root[index];
                if ( file.type == "machinecode" && file.path == path && file.origin == origin)
                    return file;
                else if(file.type == "folder")
                {
                    var result = self.recursiveFileSearch(file.children, path, origin);
                    if (result)
                        return result;
                }
            }
            return null;
        };

        self.getFile = function(data) {
            var path = data.path;
            if (!path)
                return null;
            if (path.length > 0 && path[0] == "/")
                path = path.substring(1);
            var origin = data.origin;
            var allItems = self.files.allItems();
            if (!allItems)
            {
                return;
            }
            return self.recursiveFileSearch(allItems, path, origin);
        };

        self.processButtonClicked = function(event) {
            // Get the path
            var file_data = event.data;
            var element = self.getFile({path: file_data.path, origin: file_data.origin});
            if (!element)
            {
                console.error("ArcWelder - Unable to find file: " + file_data.path);
                return;
            }
            var is_welded = element.arc_welder;

            if (is_welded)
            {
                // Open the statistics for the file
                self.current_statistics_file(file_data);
                // Select the arc-welder tab
                ArcWelder.openTab();
                return;
            }
            console.log("Button Clicked: " + file_data.path);
            // disable the element
            $(event.target).addClass("disabled");
            // Request that the file be processed
            var data = { "path": encodeURI(file_data.path), "origin": file_data.origin};
            $.ajax({
                url: ArcWelder.APIURL("process"),
                type: "POST",
                tryCount: 0,
                retryLimit: 3,
                contentType: "application/json",
                data: JSON.stringify(data),
                dataType: "json",
                success: function(results) {
                    if (results.success)
                    {
                        var options = {
                            title: 'Arc Welder File Queued',
                            text: "The file '" + file_data.path + "' has been queued for processing.",
                            type: 'info',
                            hide: true,
                            addclass: "arc-welder",
                            desktop: {
                                desktop: true
                            }
                        };
                        PNotifyExtensions.displayPopupForKey(
                            options,
                            ArcWelder.PopupKey("process-error"),
                            ArcWelder.PopupKey(["process-error"])
                        );
                    }
                    else
                    {
                        var options = {
                            title: 'Arc Welder Error',
                            text: results.message,
                            type: 'error',
                            hide: false,
                            addclass: "arc-welder",
                            desktop: {
                                desktop: true
                            }
                        };
                        PNotifyExtensions.displayPopupForKey(
                            options,
                            ArcWelder.PopupKey("process-error"),
                            ArcWelder.PopupKey(["process-error"])
                        );
                    }
                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    var message = "Could not pre-process '"+ file_data.path +"'.  Check plugin_arc_welder.log for details.";
                    var options = {
                        title: 'Arc Welder Error',
                        text: message,
                        type: 'error',
                        hide: false,
                        addclass: "arc-welder",
                        desktop: {
                            desktop: true
                        }
                    };
                    PNotifyExtensions.displayPopupForKey(
                        options,
                        ArcWelder.PopupKey("process-error"),
                        ArcWelder.PopupKey(["process-error"])
                    );
                    // Enable the button so the user can try again
                    $(event.target).removeClass("disabled");
                    return false;
                }
            });

        }
    };

    ArcWelder.openArcWelderSettings = function(tab_name) {
        $('a#navbar_show_settings').click();
        $('li#settings_plugin_arc_welder_link a').click();
        if(tab_name)
        {
            var query= "#arc_welder_settings_nav a[data-settings-tab='"+tab_name+"']";
            $(query).click();
        }
    };
    ArcWelder.openTab = function()
    {
        $('#tab_plugin_arc_welder_link a').click();
    }

    OCTOPRINT_VIEWMODELS.push([
        ArcWelder.ArcWelderViewModel,
        ["settingsViewModel", "loginStateViewModel", "filesViewModel", "printerStateViewModel"],
        ["#tab_plugin_arc_welder_controls"]
    ]);
});

