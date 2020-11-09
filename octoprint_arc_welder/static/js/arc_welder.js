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
    ArcWelder.toggleContentFunction = function ($elm, options, updateObservable) {

        if (options.toggle_observable) {
            //console.log("Toggling element.");
            if (updateObservable) {
                options.toggle_observable(!options.toggle_observable());
                //console.log("Observable updated - " + options.toggle_observable())
            }
            if (options.toggle_observable()) {
                if (options.class_showing) {
                    $elm.children('[class^="icon-"]').addClass(options.class_showing);
                    $elm.children('[class^="fa"]').addClass(options.class_showing);
                }
                if (options.class_hiding) {
                    $elm.children('[class^="icon-"]').removeClass(options.class_hiding);
                    $elm.children('[class^="fa"]').removeClass(options.class_hiding);
                }
                if (options.container) {
                    if (options.parent) {
                        $elm.parents(options.parent).find(options.container).stop().slideDown('fast', options.onComplete);
                    } else {
                        $(options.container).stop().slideDown('fast', options.onComplete);
                    }
                }
            } else {
                if (options.class_hiding) {
                    $elm.children('[class^="icon-"]').addClass(options.class_hiding);
                    $elm.children('[class^="fa"]').addClass(options.class_hiding);
                }
                if (options.class_showing) {
                    $elm.children('[class^="icon-"]').removeClass(options.class_showing);
                    $elm.children('[class^="fa"]').removeClass(options.class_showing);
                }
                if (options.container) {
                    if (options.parent) {
                        $elm.parents(options.parent).find(options.container).stop().slideUp('fast', options.onComplete);
                    } else {
                        $(options.container).stop().slideUp('fast', options.onComplete);
                    }
                }
            }
        } else {
            if (options.class) {
                $elm.children('[class^="icon-"]').toggleClass(options.class_hiding + ' ' + options.class_showing);
                $elm.children('[class^="fa"]').toggleClass(options.class_hiding + ' ' + options.class_showing);
            }
            if (options.container) {
                if (options.parent) {
                    $elm.parents(options.parent).find(options.container).stop().slideToggle('fast', options.onComplete);
                } else {
                    $(options.container).stop().slideToggle('fast', options.onComplete);
                }
            }
        }

    };
    ArcWelder.toggle = {
        init: function (element, valueAccessor) {
            var $elm = $(element),
                options = $.extend({
                    class_showing: null,
                    class_hiding: null,
                    container: null,
                    parent: null,
                    toggle_observable: null,
                    onComplete: function () {
                        $(document).trigger("slideCompleted");
                    }
                }, valueAccessor());

            if (options.toggle_observable) {
                ArcWelder.toggleContentFunction($elm, options, false);
            }


            $elm.on("click", function (e) {
                e.preventDefault();
                ArcWelder.toggleContentFunction($elm, options, true);

            });
        }
    };
    ko.bindingHandlers.arc_welder_toggle = ArcWelder.toggle;
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

    ArcWelder.boolToPureComputedProperty = function(target, options){
        var true_value = options.true_value ?? "true";
        var false_value = options.false_value ?? "false";
        var null_value = options.null_value ?? "null";
        var property_name = options.property_name ?? "boolToText"
        target[property_name] = ko.pureComputed( function(){
            var val = target();
            if (val === null)
                return null_value;
            return val ? true_value : false_value;
        });
        return target;
    }

    ko.extenders.arc_welder_bool_formatted = function (target, options) {
        options.property_name = "formatted";
        return ArcWelder.boolToPureComputedProperty(target, options)
    };
    // I hate writing basically the same extender twice, but I can
    // an extender only once to an observable, so it can't be super generic
    ko.extenders.arc_welder_bool_class = function (target, options) {
        options.property_name = "class";
        return ArcWelder.boolToPureComputedProperty(target, options)
    };

    ArcWelder.GetVisibilityStyle = function(visible)
    {
        return visible ? "show" : "hidden";
    }
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

    SOURCE_FILE_DELETE_BOTH = "both"
    SOURCE_FILE_DELETE_AUTO = "auto-only"
    SOURCE_FILE_DELETE_MANUAL = "manual-only"
    SOURCE_FILE_DELETE_NONE = "none"

    ArcWelder.SOURCE_FILE_DELETE_BOTH = "both";
    ArcWelder.SOURCE_FILE_DELETE_AUTO = "auto-only";
    ArcWelder.SOURCE_FILE_DELETE_MANUAL = "manual-only";
    ArcWelder.SOURCE_FILE_DELETE_DISABLED = "disabled";
    ArcWelder.SOURCE_FILE_DELETE_OPTIONS = [
        {name:"Always Delete Source File", value: ArcWelder.SOURCE_FILE_DELETE_BOTH},
        {name:"Delete After Automatic Processing", value: ArcWelder.SOURCE_FILE_DELETE_AUTO},
        {name:"Delete After Manual Processing", value: ArcWelder.SOURCE_FILE_DELETE_MANUAL},
        {name:"Disabled", value: ArcWelder.SOURCE_FILE_DELETE_DISABLED}
    ];

    ArcWelder.PRINT_AFTER_PROCESSING_BOTH = "both";
    ArcWelder.PRINT_AFTER_PROCESSING_AUTO = "auto-only";
    ArcWelder.PRINT_AFTER_PROCESSING_MANUAL = "manual-only";
    ArcWelder.PRINT_AFTER_PROCESSING_DISABLED = "disabled";
    ArcWelder.PRINT_AFTER_PROCESSING_OPTIONS = [
        {name:"Always Print After Processing", value: ArcWelder.PRINT_AFTER_PROCESSING_BOTH},
        {name:"Print After Automatic Processing", value: ArcWelder.PRINT_AFTER_PROCESSING_AUTO},
        {name:"Print After Manual Processing", value: ArcWelder.PRINT_AFTER_PROCESSING_MANUAL},
        {name:"Disabled", value: ArcWelder.PRINT_AFTER_PROCESSING_DISABLED}
    ];

    ArcWelder.SELECT_FILE_AFTER_PROCESSING_BOTH = "both";
    ArcWelder.SELECT_FILE_AFTER_PROCESSING_AUTO = "auto-only";
    ArcWelder.SELECT_FILE_AFTER_PROCESSING_MANUAL = "manual-only";
    ArcWelder.SELECT_FILE_AFTER_PROCESSING_DISABLED = "disabled";
    ArcWelder.SELECT_FILE_AFTER_PROCESSING_OPTIONS = [
        {name:"Always Select File After Processing", value: ArcWelder.SELECT_FILE_AFTER_PROCESSING_BOTH},
        {name:"Select File After Automatic Processing", value: ArcWelder.SELECT_FILE_AFTER_PROCESSING_AUTO},
        {name:"Select File After Manual Processing", value: ArcWelder.SELECT_FILE_AFTER_PROCESSING_MANUAL},
        {name:"Disabled", value: ArcWelder.SELECT_FILE_AFTER_PROCESSING_DISABLED}
    ];

    ArcWelder.CHECK_FIRMWARE_ON_CONECT = "connection"
    ArcWelder.CHECK_FIRMWARE_MANUAL_ONLY = "manual-only"
    ArcWelder.CHECK_FIRMWARE_DISABLED = "disabled"
    ArcWelder.CHECK_FIRMWARE_OPTIONS = [
        {name:"Automatically When Printer Connects", value: ArcWelder.CHECK_FIRMWARE_ON_CONECT},
        {name:"Only Check Manually", value: ArcWelder.CHECK_FIRMWARE_MANUAL_ONLY},
        {name:"Disabled", value: ArcWelder.CHECK_FIRMWARE_DISABLED},
    ]

    ArcWelder.FirmwareViewModel = function (firmware){
        var self = this;
        self.bool_display_options = {
            true_value: "Yes",
            false_value: "No",
            null_value: "Unknown"
        }
        self.bool_class_options = {
            true_value: "text-success",
            false_value: "text-error",
            null_value: "text-warning"
        }
        self.bool_class_recommended_options = {
            true_value: "text-success",
            false_value: "text-warning",
            null_value: "text-warning"
        }
        self.loaded = ko.observable(false);
        self.success = ko.observable();
        self.type = ko.observable();
        self.version = ko.observable();
        self.build_date = ko.observable();
        self.version_range = ko.observable();
        self.version_guid = ko.observable();
        self.printer = ko.observable();
        self.supported = ko.observable().extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });
        self.recommended = ko.observable().extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_recommended_options
        });
        self.notes = ko.observable();
        self.previous_notes = ko.observable();
        self.error = ko.observable();
        self.m115_response = ko.observable();
        self.g2_g3_supported = ko.observable().extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });
        self.arcs_enabled = ko.observable().extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });
        self.g90_g91_influences_extruder = ko.observable().extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });
        self.arc_settings = ko.observable();
        self.errors = ko.observableArray([]);
        self.warnings = ko.observableArray([]);
        self.has_warnings = ko.observable(null).extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });
        self.has_errors = ko.observable(null).extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });

        self.checking_firmware = ko.observable(false);

        self.update = function(data){
            data = data??{};
            self.loaded(true)
            self.success(data.success ?? null);
            self.type(data.type ?? null);
            self.version(data.version ?? null);
            self.build_date(data.build_date ?? null);
            self.version_range(data.version_range ?? null);
            self.version_guid(data.version_guid ?? null);
            self.printer(data.printer ?? null);
            self.supported(data.supported ?? null);
            self.recommended(data.recommended ?? null);
            self.notes(data.notes ?? null);
            self.previous_notes(data.previous_notes ?? null);
            self.error(data.error ?? null);
            self.m115_response(data.m115_response ?? null);
            self.g2_g3_supported(data.g2_g3_supported ?? null);
            self.arcs_enabled(data.arcs_enabled ?? null);
            self.g90_g91_influences_extruder(data.g90_g91_influences_extruder ?? null);
            self.arc_settings(data.arc_settings ?? null);
            self.fill_warnings();
            self.fill_errors();
            self.has_errors(self.errors().length > 0);
            self.has_warnings(self.warnings().length > 0);
        };

        self.fill_warnings = function(){
            var warnings = [];
            if (self.success() === null)
            {
                warnings.push("No firmware check has been completed.  Please make sure your printer is connected, then click 'Check Firmware'.");
            }
            else if (self.success()) {
                if (self.type() === null){
                    warnings.push("Arc welder was unable to identity this firmware.  It might not support arc commands, or it may have bugs, but it may work fine.  Use with caution");
                }
                else if (self.version() === null)
                {
                    warnings.push("Arc welder was unable to identity firmware version.  It might not support arc commands, or it may have bugs, but it may work fine.  Use with caution");
                }
                else
                {
                    if (self.supported()===false && self.arcs_enabled())
                    {
                        warnings.push("Your firmware version indicates that it is not supported, but arcs appear to be supported and enabled.  Use with caution.");
                    }

                    if ((self.supported()===true || self.arcs_enabled()===true) && self.recommended()===false)
                    {
                        // Not Recommended (only if supported)
                        warnings.push("Your printer's firmware is not recommended for use with Arc Welder.  Quality and speed may be reduced.");
                    }
                    if (self.g2_g3_supported()===null)
                    {
                        // G2/G3 support unknown
                        warnings.push("Cannot determine if arc commands (G2/G3) are supported by your firmware.");
                    }
                    if (self.arcs_enabled() ===null)
                    {
                        // Arcs enabled unknown
                        warnings.push("Cannot determine if arc commands are enabled in your firmware.");
                    }
                }

            }
            self.warnings(warnings);
        }
        self.fill_errors = function(){
            var errors = [];
            if (!self.success())
            {
                if (self.success() !== null)
                {
                    errors.push("The last firmware check failed.  Please try again.  Click the help link for troubleshooting tips.");
                }
            }
            else
            {
                if (self.supported()===false && !self.arcs_enabled())
                {
                    // Not Supported
                    errors.push("Your printer's firmware is not supported.");
                }
                else if (self.g2_g3_supported()===false)
                {
                    // G2/G3 not supported
                    errors.push("Your printer's firmware does not support G2/G3 (arc) commands.");
                }
                else if (self.arcs_enabled()===false)
                {
                    // Arcs not Enabled
                    errors.push("Arcs are not enabled in your printer's firmware.");
                }
            }
            self.errors(errors);

        }

        self.checkFirmware = function(){
            if (self.checking_firmware())
                return;
            self.checking_firmware(true);
            $.ajax({
                url: ArcWelder.APIURL("checkFirmware"),
                type: "POST",
                contentType: "application/json",
                success: function(data) {
                    if (data.success)
                    {
                        // The request was successful, but the results are pending
                        self.checking_firmware(true);
                    }
                    else
                    {
                        // The request failed, we are parobably already checking.
                        self.checking_firmware(false);
                        // Show an error since the check failed.
                        self.update(false);
                    }
                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    self.checking_firmware(false);
                    var message = "Unable to check the firmware.  Status: " + textStatus + ".  Error: " + errorThrown;
                    var options = {
                        title: 'Check Firmware Error',
                        text: message,
                        type: 'error',
                        hide: false,
                        addclass: "arc_welder",
                        desktop: {
                            desktop: true
                        }
                    };
                    PNotifyExtensions.displayPopupForKey(
                        options,
                        ArcWelder.PopupKey("check_firmware_error"),
                        ArcWelder.PopupKey("check_firmware_error")
                    );
                }
            });
        }

        self.getFirmwareVersion = function() {
            $.ajax({
                url: ArcWelder.APIURL("getFirmwareVersion"),
                type: "POST",
                tryCount: 0,
                retryLimit: 3,
                contentType: "application/json",
                success: function(data) {
                    if (!data.success)
                    {
                       self.update(false);
                    }
                    else
                    {
                        self.update(data.firmware_info);
                    }
                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    var message = "Could not retrieve firmware data.  Status: " + textStatus + ".  Error: " + errorThrown;
                    var options = {
                        title: 'Arc Welder: Error Loading Firmware Data',
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
    }

    ArcWelder.ArcWelderViewModel = function (parameters) {
        var self = this;
        ArcWelder.Tab = self;
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
        self.is_selected_file_welded = ko.observable(null)
        self.statistics_available = ko.observable(null);
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
        self.firmware_info = new ArcWelder.FirmwareViewModel()
        self.statistics.source_filename = ko.observable();
        self.statistics.target_filename = ko.observable();
        var initial_run_configuration_visible = ArcWelder.getLocalStorage("run_configuration_visible") !== "false";
        self.run_configuration_visible = ko.observable(initial_run_configuration_visible);
        self.run_configuration_visible.subscribe(function(newValue){
            var storage_value = newValue ? 'true' : 'false';
            ArcWelder.setLocalStorage("run_configuration_visible", storage_value);
        });

        var initial_firmware_compatibility_visible = ArcWelder.getLocalStorage("firmware_compatibility_visible") !== "false";
        self.firmware_compatibility_visible = ko.observable(initial_firmware_compatibility_visible);
        self.firmware_compatibility_visible.subscribe(function(newValue){
            var storage_value = newValue ? 'true' : 'false';
            ArcWelder.setLocalStorage("firmware_compatibility_visible", storage_value);
        });

        var initial_file_statistics_visible = ArcWelder.getLocalStorage("file_statistics_visible") !== "false";
        self.file_statistics_visible = ko.observable(initial_file_statistics_visible);
        self.file_statistics_visible.subscribe(function(newValue){
            var storage_value = newValue ? 'true' : 'false';
            ArcWelder.setLocalStorage("file_statistics_visible", storage_value);
        });

        self.statistics.segment_statistics_text = ko.observable();
        self.current_files = null;

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
        // Auto Select
        self.select_auto_processed_file = ko.pureComputed(function(){
            var auto_select_type = self.plugin_settings.feature_settings.select_after_processing();
            return auto_select_type === ArcWelder.SELECT_FILE_AFTER_PROCESSING_AUTO ||
                auto_select_type === ArcWelder.SELECT_FILE_AFTER_PROCESSING_BOTH;
        });

        self.select_manual_processed_file = ko.pureComputed(function(){
            var auto_select_type = self.plugin_settings.feature_settings.select_after_processing();
            return auto_select_type === ArcWelder.SELECT_FILE_AFTER_PROCESSING_MANUAL ||
                auto_select_type === ArcWelder.SELECT_FILE_AFTER_PROCESSING_BOTH;
        });
        // Auto Print
        self.print_auto_processed_file = ko.pureComputed(function(){
            var auto_select_type = self.plugin_settings.feature_settings.print_after_processing();
            return auto_select_type === ArcWelder.PRINT_AFTER_PROCESSING_AUTO ||
                auto_select_type === ArcWelder.PRINT_AFTER_PROCESSING_BOTH;
        });

        self.print_manual_processed_file = ko.pureComputed(function(){
            var auto_select_type = self.plugin_settings.feature_settings.print_after_processing();
            return auto_select_type === ArcWelder.PRINT_AFTER_PROCESSING_MANUAL ||
                auto_select_type === ArcWelder.PRINT_AFTER_PROCESSING_BOTH;
        });



        self.source_file_delete_description = ko.pureComputed(function(){
            delete_setting = self.plugin_settings.feature_settings.delete_source();
            switch(delete_setting)
            {
                case ArcWelder.SOURCE_FILE_DELETE_AUTO:
                    return "After Automatic Processing Only";
                case ArcWelder.SOURCE_FILE_DELETE_MANUAL:
                    return "After Manual Processing Only";
                case ArcWelder.SOURCE_FILE_DELETE_BOTH:
                    return "Always";
                default:
                   return "Disabled";
            }
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
            self.plugin_settings.path_tolerance_percent.extend({arc_welder_numeric: 1});
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
            self.firmware_info.getFirmwareVersion();
        };

        self.closePreprocessingPopup = function(){
            if (self.pre_processing_progress != null) {
                self.pre_processing_progress.close();
            }
            self.preprocessing_job_guid = null;
            self.pre_processing_progress = null;
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

            var filename, is_welded, statistics, is_new;
            self.statistics_available(false);
            self.selected_file_is_new(false);
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
                    return;
                }
                filename = file.name;
                is_welded = file.arc_welder ?? false;
                statistics = file.arc_welder_statistics;
            }
            // Update the UI
            self.selected_filename(filename);
            self.selected_file_is_new(is_new);
            self.is_selected_file_welded(is_welded);
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
                if (statistics)
                {
                    self.statistics_available(true);
                }
                else
                {
                    self.statistics_available(false);
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
                    if (metadata.source_filename == metadata.target_filename)
                    {
                        file_name_html = "<div><strong>File:</strong> " + metadata.source_filename + "<div>";
                    }
                    else
                    {
                        file_name_html = "<div><strong>Source File:</strong> " + metadata.source_filename + "<div>" +
                            "<div><strong>Target File:</strong> " + metadata.target_filename + "<div>";
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
                    var space_saved_string = ArcWelder.toFileSizeString(source_file_position - target_file_size, 1);
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
                case "firmware-info-update":
                    // Update the firmware info
                    self.firmware_info.update(data.firmware_info)
                    // signal that the check is finished.
                    self.firmware_info.checking_firmware(false);
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
                var disable = false;
                var title = "Weld Arcs";
                if (is_printing)
                {
                    title = "Cannot weld arcs during a print, this would impact performance.";
                    disable = true;
                }

                if (file.origin !== "local")
                {
                    disable = true;
                    title = "Cannot weld arcs for files stored on your printer's SD card.";
                }
                if (file.arc_welder)
                {
                    disable = false;
                    is_welded = true;
                    title = "View Arc-Welder statistics for this file.";
                }


                // Create the button

                var $button = $('\
                    <div class="btn btn-mini arc-welder' + (disable ? " disabled" : "") + '" title="' + title + '">\
                        <i class="fa ' + (is_welded ? "fa-file-text" : "fa-compress") + '"></i>\
                    </div>\
                ');

                // Add an on click handler for the arc welder filemanager if it is not disabled
                var data = {path: file.path, origin: file.origin};
                if (!disable)
                {
                    $button.click(data, function(e) {
                        self.processButtonClicked(e);
                    });
                }


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

            if (element.origin != 'local') {
                // Ignore non-local files
                return
            }
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
                        if (results.message) {
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

    ArcWelder.openTab = function() {
        $('#tab_plugin_arc_welder_link a').click();
    }

    OCTOPRINT_VIEWMODELS.push([
        ArcWelder.ArcWelderViewModel,
        ["settingsViewModel", "loginStateViewModel", "filesViewModel", "printerStateViewModel"],
        ["#tab_plugin_arc_welder_controls"]
    ]);
});

