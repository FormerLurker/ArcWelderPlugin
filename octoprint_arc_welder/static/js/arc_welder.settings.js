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
###################################################################################*/
$(function() {

    function ArcWelderSettingsViewModel(parameters) {
        var self = this;

        self.settings = parameters[0];
        self.plugin_settings = ko.observable(null);
        self.data = ko.observable();
        self.data.logging_levels = [
            {name:"Verbose", value: 5},
            {name:"Debug", value: 10},
            {name:"Info", value: 20},
            {name:"Warning", value: 30},
        ];
        self.logger_name_add = ko.observable();
        self.logger_level_add = ko.observable();
        self.data.all_logger_names = ["arc_welder.__init__", "arc_welder.gcode_conversion", "arc_welder.firmware_checker"];
        self.data.default_log_level = 20;

        self.onBeforeBinding = function() {
            // Make plugin setting access a little more terse
            self.plugin_settings(self.settings.settings.plugins.arc_welder);
            self.available_loggers = ko.computed(function () {
                var available_loggers = [];
                for (var logger_index = 0; logger_index < self.data.all_logger_names.length; logger_index++) {
                    var logger_name = self.data.all_logger_names[logger_index];
                    var found_logger_index = self.get_enabled_logger_index_by_name(logger_name);
                    if (found_logger_index === -1) {
                        available_loggers.push({'name': logger_name, 'log_level': self.data.default_log_level});
                    }
                }
                return available_loggers;
            }, self);

            self.available_loggers_sorted = ko.computed(function () {
                return self.loggerNameSort(self.available_loggers)
            }, self);
            self.firmware_info = ArcWelder.Tab.firmware_info;
        };

        self.onAfterBinding = function() {
            ArcWelder.Help.bindHelpLinks("div#arc_welder_settings");
            //self.firmware_info = ArcWelder.Tab.firmware_info;
        };

        self.get_enabled_logger_index_by_name = function (name) {
            for (var index = 0; index < self.plugin_settings().logging_configuration.enabled_loggers().length; index++) {
                var logger = self.plugin_settings().logging_configuration.enabled_loggers()[index];
                var cur_name = "";
                if(ko.isObservable(logger.name))
                    cur_name = logger.name();
                else
                    cur_name = logger.name;
                if (cur_name === name) {
                    return index;
                }
            }
            return -1;
        };

        self.loggerNameSort = function (observable) {
            return observable().sort(
                function (left, right) {
                    var leftName = left.name.toLowerCase();
                    var rightName = right.name.toLowerCase();
                    return leftName === rightName ? 0 : (leftName < rightName ? -1 : 1);
                });
        };

        self.removeLogger = function (logger) {
            //console.log("removing logger.");
            self.plugin_settings().logging_configuration.enabled_loggers.remove(logger);
        };

        self.addLogger = function () {
            //console.log("Adding logger");
            var index = self.get_enabled_logger_index_by_name(self.logger_name_add());
            if (index === -1) {
                self.plugin_settings().logging_configuration.enabled_loggers.push({'name': self.logger_name_add(), 'log_level': self.logger_level_add()});
            }
        };

        self.restoreDefaultSettings = function() {
            PNotifyExtensions.showConfirmDialog(
                "restore_default_settings",
                "Restore Arc Welder Default Settings",
                "This will restore all of the default settings, are you sure?",
                function () {
                    $.ajax({
                        url: ArcWelder.APIURL("restoreDefaultSettings"),
                        type: "POST",
                        contentType: "application/json",
                        success: function (data) {
                            var options = {
                                title: "Arc Welder Default Settings Restored",
                                text: "The settings have been restored.",
                                type: 'success',
                                hide: true,
                                addclass: "arc_welder",
                                desktop: {
                                    desktop: true
                                }
                            };
                            PNotifyExtensions.displayPopupForKey(
                                options,
                                ArcWelder.PopupKey("settings_restored"),
                                ArcWelder.PopupKey("settings_restored")
                            );
                        },
                        error: function (XMLHttpRequest, textStatus, errorThrown) {
                            var message = "Unable to restore the default settings.  Status: " + textStatus + ".  Error: " + errorThrown;
                            var options = {
                                title: 'Restore Default Settings Error',
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
                                ArcWelder.PopupKey("settings_restore_error"),
                                ArcWelder.PopupKey("settings_restore_error")
                            );
                        }
                    });
                }
            );
        };

        self.clearLog = function (clear_all) {
            var title;
            var message;
            if (clear_all) {
                title = "Clear All Logs";
                message = "All Arc Welder log files will be cleared and deleted.  Are you sure?";
            } else {
                title = "Clear Log";
                message = "The most recent Arc Welder log file will be cleared.  Are you sure?";
            }
            PNotifyExtensions.showConfirmDialog(
                "clear_log",
                title,
                message,
                function () {
                    if (clear_all) {
                        title = "Logs Cleared";
                        message = "All Arc Welder log files have been cleared.";
                    } else {
                        title = "Most Recent Log Cleared";
                        message = "The most recent Arc Welder log file has been cleared.";
                    }
                    var data = {
                        clear_all: clear_all
                    };
                    $.ajax({
                        url: ArcWelder.APIURL("clearLog"),
                        type: "POST",
                        data: JSON.stringify(data),
                        contentType: "application/json",
                        dataType: "json",
                        success: function (data) {
                            var options = {
                                title: title,
                                text: message,
                                type: 'success',
                                hide: true,
                                addclass: "arc_welder",
                                desktop: {
                                    desktop: true
                                }
                            };
                            PNotifyExtensions.displayPopupForKey(
                                options,
                                ArcWelder.PopupKey("log_file_cleared"),
                                ArcWelder.PopupKey("log_file_cleared")
                            );
                        },
                        error: function (XMLHttpRequest, textStatus, errorThrown) {
                            var message = "Unable to clear the log.:(  Status: " + textStatus + ".  Error: " + errorThrown;
                            var options = {
                                title: 'Clear Log Error',
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
                                ArcWelder.PopupKey("log_file_cleared"),
                                ArcWelder.PopupKey("log_file_cleared")
                            );
                        }
                    });
                }
            );

        };
    }

    OCTOPRINT_VIEWMODELS.push([
        ArcWelderSettingsViewModel,
        ["settingsViewModel"],
        ["#arc_welder_settings"]
    ]);
});
