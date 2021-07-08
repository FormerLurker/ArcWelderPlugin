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
    ArcWelder.toggleContentFunction = function ($elm, options, updateObservable, set_visible=null) {

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
                options.toggle_observable.update_visible = function(){

                    ArcWelder.toggleContentFunction($elm, options, false);
                };
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

    ArcWelder.getPercentChange = function(source, target){
        if (target != 0)
        {
            return ((target - source) /target) * 100.0;
        }
        return 0;
    };

    ArcWelder.getPercent = function(numerator, denominator){
        if (denominator != 0)
        {
            return (numerator/denominator) * 100.0;
        }
        return 0;
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
        var true_value = options.true_value ?? "True";
        var false_value = options.false_value ?? "False";
        var null_value = options.null_value ?? "Null";
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
        return ArcWelder.boolToPureComputedProperty(target, options);
    };

    ArcWelder.ToTimer = function (seconds, format){
        var total_seconds = seconds
        if (!format)
        {
            format = "timer"
        }
        if (seconds == null)
            return "";
        if (seconds <= 0)
            seconds = 0;
        seconds = Math.round(seconds);

        var hours = Math.floor(seconds / 3600);
        seconds = seconds % 3600;
        var minutes = Math.floor(seconds/60);
        seconds = seconds % 60;

        if (format == "estimate")
        {
            var message = "";
            if (hours > 0)
            {
                message += "About " + hours.toString() + " hours";
                if (minutes > 0)
                {
                    message += " and " + minutes.toString() + " minutes";
                }
            }
            else if (minutes > 0){
                message += "About " + minutes.toString() + " minutes";
                if (seconds > 0)
                {
                    message += " and " + seconds.toString() + " seconds";
                }
            }
            else if (seconds > 0){
                message += "About " + seconds.toString() + " seconds";
            }
            else
            {
                message = "less than 1 second";
            }
            return message;

        }
        else if (format == "long")
        {
            var longTime = "";
            if (hours < 1 && minutes < 1 && seconds < 1)
            {
                return "Less than 1 second";
            }
            if (hours>0)
            {
                longTime += hours.toString() + " hours";
            }
            if (minutes>0)
            {
                if (longTime.length > 0){
                    if (seconds == 0)
                    {
                        longTime += " and ";
                    }
                    else
                    {
                        longTime += " ";
                    }
                }
                longTime += minutes.toString() + " minutes";
            }
            if (seconds>0)
            {
                if (longTime.length > 0){
                    longTime += " and ";
                }
                longTime += seconds.toString() + " seconds";
            }
            return longTime

        }

        // Default: 00:00:00 if < 99 hrs else it will mess up
        if (hours > 99)
        {
            return hours.toString() + "99hrs";
        }
        return ("0" + hours.toString()).slice(-2) + ":"  + ("0" + minutes.toString()).slice(-2) + ":" + ("0" + seconds.toString()).slice(-2);

    };

    ko.extenders.arc_welder_timer = function (target, options) {
        var property_name = "formatted";
        var format = options.format;
        target[property_name] = ko.pureComputed( function(){
            var val = target();
            if (val === null)
            {
                val = 0;
            }
            return ArcWelder.ToTimer(val, format);
        });
        return target;
    };

    ArcWelder.toShortNumber = function (number, precision){
        precision = precision || 0;
        if (Math.abs(number) < 1000) {
            return number;
        }
        var units = ['K', 'M', 'B', 'T'];
        var u = -1;
        do {
            number /= 1000;
            ++u;
        } while (Math.abs(number) >= 1000 && u < units.length - 1);
        return number.toFixed(precision) + units[u];
    };

    ko.extenders.arc_welder_short_number = function (target, options) {
        var property_name = "formatted";
        var precision = options.precision;
        target[property_name] = ko.pureComputed( function(){
            var val = target();
            if (val === null)
            {
                val = 0;
            }
            return ArcWelder.toShortNumber(val, precision);
        });
        return target;
    };

    var byte = 1024;
    ArcWelder.toFileSizeString = function (bytes, precision) {
        precision = precision || 0;

        if (Math.abs(bytes) < byte) {
            return bytes + ' B';
        }
        var units = ['K', 'M', 'B', 'T'];
        var u = -1;
        do {
            bytes /= byte;
            ++u;
        } while (Math.abs(bytes) >= byte && u < units.length - 1);
        return bytes.toFixed(precision) + ' ' + units[u];
    };

    ko.extenders.arc_welder_file_size = function (target, precision) {
        var property_name = "formatted";
        target[property_name] = ko.pureComputed( function(){
            var val = target();
            if (val === null)
            {
                val = 0;
            }
            return ArcWelder.toFileSizeString(val, precision);
        });
        return target;
    };

    ArcWelder.GetVisibilityStyle = function(visible){
        return visible ? "show" : "hidden";
    }
    //ArcWelder.pnotify = PNotifyExtensions({});
    ArcWelder.APIURL = function(fn){
        return "./plugin/" + ArcWelder.PLUGIN_ID + "/" + fn;
    };
    ArcWelder.HelpDocumentRootUrl = ArcWelder.APIURL("static/docs/help/");
    ArcWelder.HelpDataDocumentRootUrl = ArcWelder.APIURL("data/docs/help/")
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
        document_root_url: ArcWelder.HelpDocumentRootUrl,
        data_document_root_url: ArcWelder.HelpDataDocumentRootUrl
    });

    ArcWelder.PROCESS_OPTION_ALWAYS = "always";
    ArcWelder.PROCESS_OPTION_UPLOADS_ONLY = "uploads-only";
    ArcWelder.PROCESS_OPTION_SLICER_UPLOADS = "slicer-uploads";
    ArcWelder.PROCESS_OPTION_DISABLED = "disabled";
    ArcWelder.PROCESS_OPTION_MANUAL_ONLY = "manual-only";

    ArcWelder.FILE_PROCESSING_OPTIONS = [
        {name:"All New Files", value: ArcWelder.PROCESS_OPTION_ALWAYS},
        // This option won't work at the moment.  Maybe in the future
        // {name:"Direct Slicer Uploads", value: ArcWelder.PROCESS_OPTION_SLICER_UPLOADS},
        {name:"Manual Processing Only", value: ArcWelder.PROCESS_OPTION_MANUAL_ONLY},
    ];
    ArcWelder.SOURCE_FILE_DELETE_OPTIONS = [
        {name:"Always Delete Source", value: ArcWelder.PROCESS_OPTION_ALWAYS},
        {name:"Disabled", value: ArcWelder.PROCESS_OPTION_DISABLED}
    ];
    ArcWelder.PRINT_AFTER_PROCESSING_OPTIONS = [
        {name:"Always", value: ArcWelder.PROCESS_OPTION_ALWAYS},
        // This option won't work ATM, maybe in the future.
        //{name:"After Slicer Upload", value: ArcWelder.PROCESS_OPTION_SLICER_UPLOADS},
        {name:"After Manual Processing", value: ArcWelder.PROCESS_OPTION_MANUAL_ONLY},
        {name:"Disabled", value: ArcWelder.PROCESS_OPTION_DISABLED}
    ];
    ArcWelder.SELECT_FILE_AFTER_PROCESSING_OPTIONS = [
        {name:"Always", value: ArcWelder.PROCESS_OPTION_ALWAYS},
        {name:"Uploaded Files", value: ArcWelder.PROCESS_OPTION_UPLOADS_ONLY},
        {name:"Disabled", value: ArcWelder.PROCESS_OPTION_DISABLED}
    ];

    ArcWelder.CHECK_FIRMWARE_ON_CONECT = "connection"
    ArcWelder.CHECK_FIRMWARE_MANUAL_ONLY = "manual-only"
    ArcWelder.CHECK_FIRMWARE_DISABLED = "disabled"
    ArcWelder.CHECK_FIRMWARE_OPTIONS = [
        {name:"Automatically When Printer Connects", value: ArcWelder.CHECK_FIRMWARE_ON_CONECT},
        {name:"Only Check Manually", value: ArcWelder.CHECK_FIRMWARE_MANUAL_ONLY},
        {name:"Disabled", value: ArcWelder.CHECK_FIRMWARE_DISABLED},
    ]

    ArcWelder.getOptionNameForValue = function (options, value){
        for (var index=0; index < options.length; index++)
        {
            var item = options[index];
            if (item.value === value)
            {
                return item.name;
            }
        }
        logger.error("Could not find value '" + value + "' for options.");
        return "Unknown";
    }

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

        self.loaded = ko.observable(false);
        self.success = ko.observable();
        self.type = ko.observable();
        self.type_help_file = ko.observable();
        self.version = ko.observable();
        self.checking_for_firmware_info_updates = ko.observable(false);
        self.firmware_types_version = ko.observable("unknown");
        self.version_help_file = ko.observable();
        self.previous_version_help_file = ko.observable();
        self.build_date = ko.observable();
        self.version_range = ko.observable();
        self.guid = ko.observable();
        self.printer = ko.observable();
        self.supported = ko.observable().extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });
        self.known_issues = ko.observableArray();
        self.is_future = ko.observable();
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
        self.g2_g3_z_parameter_supported = ko.observable().extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });
        self.g90_g91_influences_extruder = ko.observable().extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });
        self.last_check_datetime = ko.observable()
        self.firmware_types_info_default = {
            last_check_success:null,
            last_checked_date:null,
            version:null
        };
        self.firmware_types_info = ko.observable(self.firmware_types_info_default);
        self.arc_settings = ko.observable();

        self.checking_firmware = ko.observable(false);

        self.has_known_issues = ko.pureComputed(function(){
            return self.known_issues().length > 0;
        });
        self.update = function(data, firmware_types_version){
            self.firmware_types_info(firmware_types_version ?? self.firmware_types_info_default)
            data = data??{};
            self.loaded(true);
            self.success(data.success ?? null);
            self.type(data.type ?? null);
            self.type_help_file(data.type_help_file ?? null);
            self.version(data.version ?? null);
            self.version_help_file(data.version_help_file ?? null);
            self.previous_version_help_file(data.previous_version_help_file ?? null);
            self.build_date(data.build_date ?? null);
            self.version_range(data.version_range ?? null);
            self.guid(data.guid ?? null);
            self.printer(data.printer ?? null);
            self.supported(data.supported ?? null);
            self.known_issues(data.known_issues ?? []);
            self.is_future(data.is_future ?? null)
            self.notes(data.notes ?? null);
            self.previous_notes(data.previous_notes ?? null);
            self.error(data.error ?? null);
            self.m115_response(data.m115_response ?? null);
            self.g2_g3_supported(data.g2_g3_supported ?? null);
            self.arcs_enabled(data.arcs_enabled ?? null);
            self.g2_g3_z_parameter_supported(data.g2_g3_z_parameter_supported ?? null);
            self.g90_g91_influences_extruder(data.g90_g91_influences_extruder ?? null);
            self.last_check_datetime(data.last_check_datetime ?? null);
            self.arc_settings(data.arc_settings ?? null);
            ArcWelder.Help.bindHelpLinks("#arc_welder_firmware_compatibility");
        };

        self.has_warnings = ko.pureComputed(function(){
            return self.warnings().length > 0 || self.has_known_issues();
        }).extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });;

        self.warnings = ko.pureComputed(function(){
            var success = self.success();
            var type = self.type();
            var version = self.version();
            var supported = self.supported();
            var arcs_enabled = self.arcs_enabled();
            var g2_g3_supported = self.g2_g3_supported;
            var g2_g3_z_parameter_supported = self.g2_g3_z_parameter_supported();
            var allow_3d_arcs = ArcWelder.Tab.plugin_settings.allow_3d_arcs()
            var warnings = [];
            if (success === null)
            {
                warnings.push("No firmware check has been completed.  Please make sure your printer is connected, then click 'Check Firmware'.");
            }
            else if (success) {
                if (type === null){
                    warnings.push("Arc welder was unable to identity this firmware.  It might not support arc commands, or it may have bugs, but it may work fine.  Use with caution");
                }
                else if (version === null)
                {
                    warnings.push("Arc welder was unable to identity firmware version.  It might not support arc commands, or it may have bugs, but it may work fine.  Use with caution");
                }
                else
                {
                    if (supported===false && arcs_enabled)
                    {
                        warnings.push("Your firmware version indicates that it is not supported, but arcs appear to be supported and enabled.  Use with caution.");
                    }
                    if (g2_g3_supported===null)
                    {
                        // G2/G3 support unknown
                        warnings.push("Cannot determine if arc commands (G2/G3) are supported by your firmware.");
                    }
                    if (g2_g3_z_parameter_supported===null)
                    {
                        // g2_g3_z_parameter_supported support unknown
                        if (allow_3d_arcs){
                            warnings.push("Cannot determine if 3D Arc commands are supported (for use with vase mode), but 3D Arcs are currently enabled in the Arc Welder settings.  Please use with extreme caution!");
                        }
                        else {
                            warnings.push("Cannot determine if 3D Arc commands are supported (for use with vase mode).  Since 3D arcs are currently disabled, this should be OK.");
                        }
                    }
                    if (g2_g3_z_parameter_supported===false && !allow_3d_arcs)
                    {
                        // g2_g3_z_parameter_supported support unknown
                        warnings.push("3D Arcs are not supported by your firmware.  Since 3D arcs are currently disabled, this should be OK.");
                    }
                    if (arcs_enabled ===null)
                    {
                        // Arcs enabled unknown
                        warnings.push("Cannot determine if arc commands are enabled in your firmware.");
                    }
                }

            }
            return warnings;
        });

        self.has_errors = ko.pureComputed(function(){
            return self.errors().length > 0;
        }).extend({
            arc_welder_bool_formatted:self.bool_display_options, arc_welder_bool_class: self.bool_class_options
        });

        self.errors = ko.pureComputed(function(){
            var errors = [];
            // Call the observables up front so this is recalculated properly.
            var success = self.success();
            var supported = self.supported();
            var arcs_enabled = self.arcs_enabled();
            var g2_g3_supported = self.g2_g3_supported();
            var g90_influences_extruder_setting_correct = self.g90_influences_extruder_setting_correct();
            var g90_g91_influences_extruder = self.g90_g91_influences_extruder();
            var allow_3d_arcs = ArcWelder.Tab.plugin_settings.allow_3d_arcs();
            var g2_g3_z_parameter_supported = self.g2_g3_z_parameter_supported();

            if (!success)
            {
                if (success !== null)
                {
                    errors.push("The last firmware check failed.  Please try again.  Click the help link for troubleshooting tips.");
                }
            }
            else
            {
                if (supported===false && !arcs_enabled)
                {
                    // Not Supported
                    errors.push("Your printer's firmware is not supported.");
                }
                else if (g2_g3_supported===false)
                {
                    // G2/G3 not supported
                    errors.push("Your printer's firmware does not support G2/G3 (arc) commands.");
                }
                else if (arcs_enabled===false)
                {
                    // Arcs not Enabled
                    errors.push("Arcs are not enabled in your printer's firmware.");
                }
                // Check G2/G3 influences extruder:

                if (!g90_influences_extruder_setting_correct)
                {
                    var correct_setting = "DISABLED";
                    if(g90_g91_influences_extruder) {
                        correct_setting = "ENABLED";
                    }

                    var setting_location = "Edit the Arc Welder setting 'G90/G91 Influences Extruder'";
                    if (self.use_octoprint_settings()) {
                        setting_location = "Edit the Ocotprint feature setting 'G90/G91 overrides relative extruder mode'"
                    }

                    var error_string = "Your firmware requires the 'G90/G91 Influences Extruder' setting to be " + correct_setting + ".  " + setting_location + ", set the value to " + correct_setting + ", and run the firmware check again.";
                    errors.push(error_string);

                }
                if (allow_3d_arcs && g2_g3_z_parameter_supported===false)
                {
                   errors.push("3D Arcs are enabled, but they are not supported by your firmware.  Edit the Arc Welder settings, uncheck 'Allow 3D Arcs', and run the firmware check again.");
                }

            }
            return errors;
        });

        self.use_octoprint_settings = ko.pureComputed(function() {
            return ArcWelder.Tab.plugin_settings.use_octoprint_settings();
        });

        self.g90_influences_extruder_setting = ko.pureComputed(function() {
            var g90_g91_influences_extruder_plugin = ArcWelder.Tab.plugin_settings.g90_g91_influences_extruder();
            var g90_g91_influences_extruder_octoprint = ArcWelder.Tab.octoprint_settings.feature.g90InfluencesExtruder();
            if (self.use_octoprint_settings())
            {
                return g90_g91_influences_extruder_octoprint;
            }
            return g90_g91_influences_extruder_plugin;
        });

        self.g90_influences_extruder_setting_correct = ko.pureComputed(function(){
            var g90_g91_influences_extruder_firmware = self.g90_g91_influences_extruder();
            var g90_g91_influences_extruder_current = self.g90_influences_extruder_setting();

            if (
                g90_g91_influences_extruder_firmware !== null
                && g90_g91_influences_extruder_firmware != g90_g91_influences_extruder_current)
            {
                return false;
            }
            return true;
        });

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
                    var firmware_info = false;
                    var firmware_types_info = data.firmware_types_info;
                    if (data.success)
                    {
                       firmware_info = data.firmware_info;
                    }
                    self.update(firmware_info, firmware_types_info);

                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    var message = "Could not retrieve firmware data.  Status: " + textStatus + ".  Error: " + errorThrown;
                    console.error(message);
                    return false;
                }
            });
        }

        self.checkForFirmwareInfoUpdates = function(){
            if (self.checking_for_firmware_info_updates())
                return;

            self.checking_for_firmware_info_updates(true)

            $.ajax({
                url: ArcWelder.APIURL("checkForFirmwareInfoUpdates"),
                type: "POST",
                contentType: "application/json",
                success: function(data) {
                    self.checking_for_firmware_info_updates(false)
                    if (data.success)
                    {
                        self.update(data.firmware_info, data.firmware_types_info);
                    }
                    else if (!data.success)
                    {
                        var options = {
                            title: 'Update Firmware Info Failed',
                            text: data.error,
                            type: 'error',
                            hide: false,
                            addclass: "arc_welder",
                            desktop: {
                                desktop: true
                            }
                        };
                        PNotifyExtensions.displayPopupForKey(
                            options,
                            ArcWelder.PopupKey("update_firmware_info_error"),
                            ArcWelder.PopupKey("update_firmware_info_error")
                        );
                    }
                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    self.checking_for_firmware_info_updates(false);
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
        self.pre_processing_progress = null;
        self.version = ko.observable();
        self.git_version = ko.observable();
        self.selected_filename = ko.observable();
        self.selected_file_is_new = ko.observable(false);
        self.is_selected_file_welded = ko.observable(null);
        self.welded_no_statistics = ko.observable(false);
        self.statistics_available = ko.observable(null);
        self.current_statistics_file = ko.observable();

        /* Statistics Observabled */
        self.has_statistics = ko.observable(false);
        self.statistics_total_count_reduction_percent = ko.observable().extend({arc_welder_numeric: 1});
        self.statistics_source_file_total_length = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_target_file_total_length = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_source_file_total_count = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_target_file_total_count = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_segment_statistics_text = ko.observable();
        self.statistics_total_travel_count_reduction_percent = ko.observable().extend({arc_welder_numeric: 1});
        self.statistics_source_file_total_travel_length = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_target_file_total_travel_length = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_source_file_total_travel_count = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_target_file_total_travel_count = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_segment_travel_statistics_text = ko.observable();
        self.statistics_seconds_elapsed = ko.observable().extend({arc_welder_timer: {format:"long"}});
        self.statistics_gcodes_processed = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_lines_processed = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_points_compressed = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_arcs_created = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_num_firmware_compensations = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_num_gcode_length_exceptions = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_arcs_aborted_by_flowrate = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.statistics_source_file_size = ko.observable().extend({arc_welder_file_size: 1});
        self.statistics_source_file_position = ko.observable();
        self.statistics_target_file_size = ko.observable().extend({arc_welder_file_size: 1});
        self.statistics_compression_ratio = ko.observable().extend({arc_welder_numeric: 1});
        self.statistics_compression_percent = ko.observable().extend({arc_welder_numeric: 1});
        self.statistics_source_name = ko.observable();
        self.statistics_target_name = ko.observable();
        self.statistics_target_display_name = ko.observable();
        self.statistics_guid = ko.observable();

        /* Progress Observables */
        self.progress_percent_complete = ko.observable(0).extend({arc_welder_numeric: 1});
        self.progress_seconds_elapsed = ko.observable().extend({arc_welder_timer: {format:"timer"}});
        self.progress_seconds_remaining = ko.observable().extend({arc_welder_timer: {format:"estimate"}});
        self.progress_arcs_created = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.progress_num_firmware_compensations = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.progress_num_gcode_length_exceptions = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.progress_arcs_aborted_by_flowrate = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.progress_points_compressed = ko.observable().extend({arc_welder_short_number: {precision:1}});
        self.progress_source_file_size = ko.observable().extend({arc_welder_file_size: 1});
        self.progress_target_file_size = ko.observable().extend({arc_welder_file_size: 1});
        self.progress_compression_ratio = ko.observable().extend({arc_welder_numeric: 1});
        self.progress_compression_percent = ko.observable().extend({arc_welder_numeric: 1});
        self.progress_source_file_position = ko.observable();
        self.progress_space_saved = ko.observable().extend({arc_welder_file_size: 1});
        self.progress_source_position = ko.observable().extend({arc_welder_file_size: 1});
        self.progress_source_file_total_count = ko.observable(0).extend({arc_welder_short_number: {precision:1}});
        self.progress_target_file_total_count = ko.observable(0).extend({arc_welder_short_number: {precision:1}});
        self.progress_total_count_reduction_percent = ko.observable(0).extend({arc_welder_numeric: 1});
        self.progress_source_file_total_travel_count = ko.observable(0).extend({arc_welder_short_number: {precision:1}});
        self.progress_target_file_total_travel_count = ko.observable(0).extend({arc_welder_short_number: {precision:1}});
        self.progress_total_travel_count_reduction_percent = ko.observable(0).extend({arc_welder_numeric: 1});
        /* Firmware Observables */
        self.firmware_info = new ArcWelder.FirmwareViewModel()
        self.is_processing = ko.observable(false);
        self.queued_not_processing = ko.observable(false);
        self.preprocessing_tasks = ko.observableArray([]);
        var initial_preprocessing_tasks_visible = ArcWelder.getLocalStorage("show_preprocessing_tasks") !== "false";
        self.current_tasks_visible = ko.observable(initial_preprocessing_tasks_visible);
        self.current_tasks_visible.subscribe(function(newValue) {
            ArcWelder.setLocalStorage("show_preprocessing_tasks", newValue ? "true" : "false")
        });

        self.preprocessing_tasks.subscribe(function(newValue){
            var is_processing;
            for(var index=0; index < newValue.length; index++)
            {
                if (newValue[index].is_processing)
                {
                    is_processing = true;
                    break;
                }
            }
            self.is_processing(is_processing);
            self.queued_not_processing(!is_processing && newValue.length > 0);
        });

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

        self.current_files = null;

        self.current_statistics_file.subscribe(function(newValue) {
            self.loadStats(newValue);
            ArcWelder.setLocalStorage("stat_file_path", newValue.path)
            ArcWelder.setLocalStorage("stat_file_origin", newValue.origin);
        });

        self.firmware_compensation_enabled = ko.pureComputed(function(){
            return self.plugin_settings.firmware_compensation_enabled()
                && self.plugin_settings.min_arc_segments() > 0
                && self.plugin_settings.mm_per_arc_segment() > 0;
        });

        self.statistics_space_saved_string = ko.pureComputed(function(){
            return ArcWelder.toFileSizeString(
                self.statistics_source_file_size() - self.statistics_target_file_size(), 1
            );
        });

        self.statistics_target_file_lines = ko.pureComputed(function(){
            return ArcWelder.toShortNumber(
                self.statistics_lines_processed() - self.statistics_points_compressed(), 1);
        });

        self.selected_filename_title = ko.pureComputed(function() {
            var title = "Selected";
            if (self.selected_file_is_new())
            {
                title = "Processed";
            }
            return title;
        });

        self.processing_task = ko.pureComputed(function(){
            for (var index=0; index < self.preprocessing_tasks().length; index++)
            {
                if (self.preprocessing_tasks()[index].is_processing)
                {
                    return self.preprocessing_tasks()[index];
                }
            }
            return false;
        });

        self.processing_task_file_name = ko.pureComputed(function(){
            if (self.processing_task())
            {
                return self.processing_task().task.octoprint_args.source_name;
            }
            return "";
        });

        self.queued_tasks = ko.pureComputed(function(){
            var tasks = []
            for (var index=0; index < self.preprocessing_tasks().length; index++)
            {
                if (self.preprocessing_tasks()[index].is_processing)
                {
                    continue;
                }
                tasks.push(self.preprocessing_tasks()[index])
            }
            return tasks;
        });

        self.default_xyz_precision = ko.pureComputed(function(){
            var precision = self.plugin_settings.default_xyz_precision();
            if (!precision)
            {
                precision = 3;
            }

            return self.get_precision(precision);
        });

        self.default_e_precision = ko.pureComputed(function(){
            var precision = self.plugin_settings.default_e_precision();
            if (!precision)
            {
                precision = 5;
            }
            return self.get_precision(precision);
        });

        self.get_precision = function (default_precision)
        {
            if (default_precision < 3)
            {
                default_precision = 3;
            }
            else if (default_precision > 6)
            {
                default_precision = 6;
            }
            return default_precision;
        };

        self.max_gcode_length_string = ko.pureComputed(function(){
            var max_gcode_length_string = "Unlimited";
            var max_gcode_length = self.plugin_settings.max_gcode_length();
            if (max_gcode_length && max_gcode_length > 0) {
                max_gcode_length_string = max_gcode_length.toString();
                if (max_gcode_length < 55)
                {
                    max_gcode_length_string += " (values below 55 are not recommended)"
                }
            }
            return max_gcode_length_string;
        });
        self.max_radius_mm_string = ko.pureComputed(function(){
            var max_radius_mm_string = "Default";
            var max_radius_mm = self.plugin_settings.max_radius_mm();
            if (max_radius_mm && max_radius_mm > 0) {
                max_radius_mm_string = max_radius_mm.toString() + "mm";
                if (max_radius_mm > 9999)
                {
                    max_radius_mm_string += " (values above 9999 are not recommended)"
                }
                else if (max_radius_mm < 999)
                {
                    max_radius_mm_string += " (values below 999 are not recommended)"
                }
            }
            return max_radius_mm_string;
        });

        self.file_processing_setting_name = ko.pureComputed(function(){
            return ArcWelder.getOptionNameForValue(
                ArcWelder.FILE_PROCESSING_OPTIONS
                ,self.plugin_settings.feature_settings.file_processing()
            );
        });
        // Auto Select
        self.select_file_setting_name = ko.pureComputed(function(){
            return ArcWelder.getOptionNameForValue(
                ArcWelder.SELECT_FILE_AFTER_PROCESSING_OPTIONS
                ,self.plugin_settings.feature_settings.select_after_processing()
            );
        });

        self.print_file_setting_name = ko.pureComputed(function(){
            return ArcWelder.getOptionNameForValue(
                ArcWelder.PRINT_AFTER_PROCESSING_OPTIONS
                ,self.plugin_settings.feature_settings.print_after_processing()
            );
        });

        self.source_file_delete_setting_name = ko.pureComputed(function(){
            var name = ArcWelder.getOptionNameForValue(
                ArcWelder.SOURCE_FILE_DELETE_OPTIONS
                ,self.plugin_settings.feature_settings.delete_source()
            );
            return name;
        });

        self.overwrite_source_file = ko.pureComputed(function(){
            return (
                self.plugin_settings.overwrite_source_file() ||
                (self.plugin_settings.target_prefix() == "" && self.plugin_settings.target_postfix() == "")
            );
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
            self.octoprint_settings = self.settings.settings;
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
            self.getPreprocessingTasks();
        };

        self.onDataUpdaterReconnect = function () {
            self.firmware_info.getFirmwareVersion();
            self.getPreprocessingTasks();
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
                var is_welded = true;
                var statistics = file_data.arc_welder_statistics;
                var filename = statistics.target_name;
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
                is_welded = file.arc_welder

                statistics = file.arc_welder_statistics;
            }
            // Update the UI
            self.selected_filename(filename);
            self.selected_file_is_new(is_new);
            self.is_selected_file_welded(is_welded);
            if (statistics)
            {
                self.has_statistics(true);
                self.statistics_total_count_reduction_percent(statistics.total_count_reduction_percent);
                self.statistics_source_file_total_length(statistics.source_file_total_length);
                self.statistics_target_file_total_length(statistics.target_file_total_length);
                self.statistics_source_file_total_count(statistics.source_file_total_count);
                self.statistics_target_file_total_count(statistics.target_file_total_count);
                self.statistics_segment_statistics_text(statistics.segment_statistics_text);
                self.statistics_total_travel_count_reduction_percent(statistics.total_travel_count_reduction_percent);
                self.statistics_source_file_total_travel_length(statistics.source_file_total_travel_length);
                self.statistics_target_file_total_travel_length(statistics.target_file_total_travel_length);
                self.statistics_source_file_total_travel_count(statistics.source_file_total_travel_count);
                self.statistics_target_file_total_travel_count(statistics.target_file_total_travel_count);
                self.statistics_segment_travel_statistics_text(statistics.segment_travel_statistics_text);
                self.statistics_seconds_elapsed(statistics.seconds_elapsed);
                self.statistics_gcodes_processed(statistics.gcodes_processed);
                self.statistics_lines_processed(statistics.lines_processed);
                self.statistics_points_compressed(statistics.points_compressed);
                self.statistics_arcs_created(statistics.arcs_created);
                self.statistics_num_firmware_compensations(statistics.num_firmware_compensations);
                self.statistics_num_gcode_length_exceptions(statistics.num_gcode_length_exceptions);
                self.statistics_arcs_aborted_by_flowrate(statistics.arcs_aborted_by_flowrate);
                self.statistics_source_file_size(statistics.source_file_size);
                self.statistics_source_file_position(statistics.source_file_position);
                self.statistics_target_file_size(statistics.target_file_size);
                self.statistics_compression_ratio(statistics.compression_ratio);
                self.statistics_compression_percent(statistics.compression_percent);
                self.statistics_source_name(statistics.source_name || statistics.source_filename);
                self.statistics_target_name(statistics.target_name || statistics.target_filename);
                self.statistics_target_display_name(statistics.target_display_name || self.statistics_target_name());
                self.statistics_guid(statistics.guid);
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
                case "task-queued":
                    if (self.plugin_settings.notification_settings.show_queued_notification()) {
                        var options = {
                            title: "Arc Welder - Task Queued",
                            text: data.message,
                            type: "info",
                            hide: true,
                            addclass: "arc-welder",
                            desktop: {
                                desktop: true
                            }
                        };
                        PNotifyExtensions.displayPopupForKey(options, ArcWelder.PopupKey("task-queued"), []);
                    }
                    break;
                case "preprocessing-start":
                    if (self.plugin_settings.notification_settings.show_started_notification()){
                        var options = {
                            title: "Arc Welder - Processing Started",
                            text: data.message,
                            type: "info",
                            hide: true,
                            addclass: "arc-welder",
                            desktop: {
                                desktop: true
                            }

                        };
                        PNotifyExtensions.displayPopupForKey(
                            options,
                            ArcWelder.PopupKey("preprocessing-start"),
                            [
                                ArcWelder.PopupKey('task-queued'),
                                ArcWelder.PopupKey('preprocessing-start')
                        ]);
                    }

                    self.progress_percent_complete(0);
                    // This file should be processing now hopefully...  will see
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
                    break;
                case "preprocessing-cancelled":
                    var options = {
                        title: "Arc Welder - Cancelled",
                        text: data.message,
                        type: "warning",
                        hide: true,
                        addclass: "arc-welder",
                        desktop: {
                            desktop: true
                        }
                    };
                    PNotifyExtensions.displayPopupForKey(options, ArcWelder.PopupKey("preprocessing-cancelled"), []);
                    break;
                case "preprocessing-success":
                    if (self.plugin_settings.notification_settings.show_completed_notification()) {
                        //  Load all stats for the newly processed file
                        var message = "Sucessfully welded file:  " + data.task.octoprint_args.target_name;
                        var options = {
                            title: "Arc Welder - Processing Success",
                            text: message,
                            type: "success",
                            hide: true,
                            addclass: "arc-welder",
                            desktop: {
                                desktop: true
                            }
                        };
                        PNotifyExtensions.displayPopupForKey(options, ArcWelder.PopupKey("preprocessing-success"), []);
                    }
                    self.current_statistics_file(data);
                    break;
                case "preprocessing-complete":
                    self.updatePreprocessingTasks(data.preprocessing_tasks);
                    break;
                case "preprocessing-progress":
                    var progress = data;
                    self.updateProcessProgress(progress);

                    break;
                case "preprocessing-tasks-changed":
                    self.updatePreprocessingTasks(data.preprocessing_tasks);
                    break;
                case "firmware-info-update":
                    // Update the firmware info
                    self.firmware_info.update(data.firmware_info, data.firmware_types_info)
                    // signal that the check is finished.
                    self.firmware_info.checking_firmware(false);
                    break;
                default:
                    loger.error("Arc Welder receied an unknown event: " + data.message_type);
            }
        };

        self.updateProcessProgress = function(progress){
            self.progress_seconds_elapsed(progress.seconds_elapsed);
            self.progress_seconds_remaining(progress.seconds_remaining);
            self.progress_arcs_created(progress.arcs_created);
            self.progress_num_firmware_compensations(progress.num_firmware_compensations);
            self.progress_num_gcode_length_exceptions(progress.num_gcode_length_exceptions);
            self.progress_arcs_aborted_by_flowrate(progress.arcs_aborted_by_flowrate);
            self.progress_points_compressed(progress.points_compressed);
            self.progress_compression_ratio(progress.compression_ratio);
            self.progress_compression_percent(progress.compression_percent);
            self.progress_space_saved(progress.source_file_size - progress.target_file_size);
            self.progress_source_position(progress.source_file_size);
            self.progress_target_file_size(progress.target_file_size);
            self.progress_percent_complete(progress.percent_complete);
            self.progress_source_file_total_count(progress.source_file_total_count);
            self.progress_target_file_total_count(progress.target_file_total_count);
            self.progress_total_count_reduction_percent(progress.total_count_reduction_percent);
            self.progress_total_travel_count_reduction_percent(progress.total_travel_count_reduction_percent);
            self.progress_source_file_total_travel_count(progress.source_file_total_travel_count);
            self.progress_target_file_total_travel_count(progress.target_file_total_travel_count);
        }

        self.getPreprocessingTasks = function() {
            $.ajax({
                url: ArcWelder.APIURL("getPreprocessingTasks"),
                type: "POST",
                tryCount: 0,
                retryLimit: 3,
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    var message = "Could not retrieve preprocessing tasks.  Status: " + textStatus + ".  Error: " + errorThrown;
                    console.error(message);
                    return false;
                }
            });
        }

        self.updatePreprocessingTasks = function(preprocessing_tasks){
            self.preprocessing_tasks(preprocessing_tasks);
            self.addProcessButtonToFileManager(self.files.listHelper.paginatedItems(), self.printer_state.isPrinting());
        }

        self.getFirmwareVersion = function() {
            $.ajax({
                url: ArcWelder.APIURL("getFirmwareVersion"),
                type: "POST",
                tryCount: 0,
                retryLimit: 3,
                contentType: "application/json",
                success: function(data) {
                    var firmware_info = false;
                    var firmware_types_version = data.firmware_types_version;
                    if (data.success)
                    {
                       firmware_info = data.firmware_info;
                    }
                    self.update(firmware_info, firmware_types_version);

                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    var message = "Could not retrieve firmware data.  Status: " + textStatus + ".  Error: " + errorThrown;
                    console.error(message);
                    return false;
                }
            });
        }

        self.cancelAllPreprocessing = function() {
            self.cancelPreprocessingRequest({"cancel_all": true});
        };

        self.cancelPreprocessing = function (task_info) {

            self.cancelPreprocessingRequest(task_info);
            return false;
        };

        self.cancelPreprocessingRequest = function(task_info){
            var data = {};
            if ("cancel_all" in task_info)
            {
                data = {
                    "cancel_all": true,
                };
            }
            else {
                data = {
                    "guid": task_info.task.guid,
                };
            }

            $.ajax({
                url: ArcWelder.APIURL("cancelPreprocessing"),
                type: "POST",
                tryCount: 0,
                retryLimit: 3,
                contentType: "application/json",
                data: JSON.stringify(data),
                dataType: "json",
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

        self.removeFileManagerItems = function() {
            $("#files div.gcode_files div.entry .action-buttons div.arc-welder").remove();
            $("#files div.gcode_files div.entry span.arc-welder.info+br").remove();
            $("#files div.gcode_files div.entry span.arc-welder.info").remove();
        };

        self.getEntryId = function(file){
            return "gcode_file_" + md5(file.origin + ":" + file.path);
        };

        self.addProcessButtonToFileManager = function(current_page, is_printing) {
            self.removeFileManagerItems();
            //console.log("Adding Buttons");
            for(var file_index=0; file_index < current_page.length; file_index++)
            {
                // Get the current file
                var file = current_page[file_index];
                // console.log("Adding Buttons for file with hash: " + file.hash);
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
                var is_queued = false;
                var is_processing = false;
                var disable = false;
                var skip_button = false;
                var title = "Weld Arcs";
                /* We are going to try letting them queue the file, but we have to show a list of
                // queued items.
                if (is_printing)
                {
                    title = "Cannot weld arcs during a print, this would impact performance.";
                    disable = true;
                }*/

                if (file.origin !== "local")
                {
                    disable = true;
                    title = "Cannot weld arcs for files stored on your printer's SD card.";
                }
                if (file.arc_welder)
                {
                    disable = false;
                    is_welded = true;
                    // add additional info
                    var reduction_string = "";

                    if (file.arc_welder_statistics)
                    {
                        var percent = file.arc_welder_statistics.total_count_reduction_percent;
                        if (percent)
                        {
                            is_down = percent < 0;
                            percent = Math.abs(percent);
                            reduction_string = ": " + (is_down? "<strong>&darr;</strong>" : "") + percent.toFixed(1) + "%";
                        }
                    }
                    var $weldedInfo = $('<span class="arc-welder info">Arc Welded'
                        + reduction_string
                        +'</span><br/>');
                    $(file_element).find(".additionalInfo").append($weldedInfo);
                    if (!file.arc_welder_statistics)
                    {
                        skip_button = true;
                    }
                    title = "View Arc-Welder statistics for this file.";
                }
                else {
                    // This file is not welded, see if it is queued or processing
                    if (self.preprocessing_tasks().length > 0)
                    {
                        for (var index = 0; index < self.preprocessing_tasks().length; index++)
                        {
                            var task_info = self.preprocessing_tasks()[index];
                            if ('/'+file.path == task_info.task.octoprint_args.source_path)
                            {
                                disable = true;
                                if (task_info.is_processing)
                                {
                                    is_processing = true;
                                    title = "Arc welder is currently processing this file.  See the tab for details."
                                }
                                else
                                {
                                    is_queued = true;
                                    title = "Arc welder has queued this file.  See the tab for details."
                                }
                            }
                        }
                    }
                }

                // Create the button
                if (skip_button)
                {
                    continue;
                }
                if (self.plugin_settings.enabled() || is_welded)
                {
                    // if arc welder is disabled, but the file is welded, we still want to show the statistics
                    // button.
                    var icon = "fa-compress"
                    if (is_welded) {
                        icon = "fa-file-text";
                    }
                    else if (is_queued)
                    {
                        icon = "fa-hourglass"
                    }
                    else if (is_processing)
                    {
                        icon = "fa-spinner fa-spin"
                    }

                    var $button = $('\
                        <div class="btn btn-mini arc-welder' + (disable ? " disabled" : "") + '" title="' + title + '">\
                            <i class="fa ' + icon + '"></i>\
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
                // Make sure the statistics panel is shown
                self.file_statistics_visible(true);
                self.file_statistics_visible.update_visible();
                return;
            }
            //console.log("Button Clicked: " + file_data.path);
            // disable the element
            $(event.target).addClass("disabled");
            // Request that the file be processed
            var data = { "path": encodeURI(file_data.path), "origin": file_data.origin, "name": encodeURI(file_data.name)};
            $.ajax({
                url: ArcWelder.APIURL("process"),
                type: "POST",
                tryCount: 0,
                retryLimit: 3,
                contentType: "application/json",
                data: JSON.stringify(data),
                dataType: "json",
                success: function(results) {
                    if (!results.success)
                    {
                        if (results.message) {
                            var options = {
                                title: 'Arc Welder Queue Error',
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

