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
    ArcWelder.progressBar = function (initial_text, title, subtitle, cancel_callback, option_button_text, on_option) {
        var self = this;
        self.notice = null;
        self.$progress = null;
        self.$progressText = null;
        self.$subTitle = null;
        self.initial_text = initial_text;
        self.popup_margin = 15;
        self.popup_width_with_margin = 400;
        self.popup_width = self.popup_width_with_margin - self.popup_margin * 2;
        self.title = title;
        self.subtitle = subtitle;
        self.close = function () {
            if (self.loader != null)
                self.loader.remove();
        };

        self.update = function (percent_complete, progress_text) {
            self.notice.find(".remove_button").remove();

            if (self.$progress == null)
                return null;
            if (percent_complete < 0)
                percent_complete = 0;
            if (percent_complete > 100)
                percent_complete = 100;
            if (percent_complete === 100) {
                //console.log("Received 100% complete progress message, removing progress bar.");
                self.loader.remove();
                return null
            }
            var percent_complete_text = percent_complete.toFixed(1);
            self.$progress.width(percent_complete_text + "%").attr("aria-valuenow", percent_complete_text).find("span").html(percent_complete_text + "%");
            self.$progressText.html(progress_text);
            return self;
        };

        self.buttons = [{
            text: 'Close',
            click: self.close
        }];
        if (cancel_callback){
            self.buttons.push({
                text: 'Cancel',
                click: cancel_callback
            });
        }
        if (option_button_text && on_option)
        {
            self.buttons.push({
                text: option_button_text,
                click: on_option
            });
        }
        // create the pnotify loader
        self.loader = new PNotify({
            title: title,
            text: '<div class="progress-sub-title"></div><div class="progress progress-striped active" style="margin:0"><div class="arc-welder progress-bar" role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100" style="width: 0"></div></div><div class="progress-text" style="width:100%;"></div></div>',
            addclass: "arc-welder",
            icon: 'fa fa-cog fa-spin',
            width: self.popup_width.toString() + "px",
            confirm: {
                confirm: true,
                buttons: self.buttons
            },
            buttons: {
                closer: true,
                sticker: false
            },
            hide: false,
            history: {
                history: false
            },
            before_open: function (notice) {
                self.notice = notice.get();
                self.$progress = self.notice.find("div.progress-bar");
                self.$progressText = self.notice.find("div.progress-text");
                self.notice.find(".remove_button").remove();
                self.$subTitle = self.notice.find("div.progress-sub-title");
                self.$subTitle.html(self.subtitle);
                self.update(0, self.initial_text);
            }
        });
        return self;
    };

    ArcWelder.Popups = {};
    ArcWelder.displayPopupForKey = function (options, popup_key, remove_keys) {
        ArcWelder.closePopupsForKeys(remove_keys);
        var popup = new PNotify(options);
        ArcWelder.Popups[popup_key] = popup;
        return popup;
    };

    ArcWelder.closePopupsForKeys = function (remove_keys) {
        if (!$.isArray(remove_keys)) {
            remove_keys = [remove_keys];
        }
        for (var index = 0; index < remove_keys.length; index++) {
            var key = remove_keys[index];
            if (key in ArcWelder.Popups) {
                var notice = ArcWelder.Popups[key];
                if (notice.state === "opening") {
                    notice.options.animation = "none";
                }
                notice.remove();
                delete ArcWelder.Popups[key];
            }
        }
    };

    ArcWelder.removeKeyForClosedPopup = function (key) {
        if (key in ArcWelder.Popups) {
            var notice = ArcWelder.Popups[key];
            delete ArcWelder.Popups[key];
        }
    };

    ArcWelder.checkPNotifyDefaultConfirmButtons = function () {
        // check to see if exactly two default pnotify confirm buttons exist.
        // If we keep running into problems we might need to inspect the buttons to make sure they
        // really are the defaults.
        if (PNotify.prototype.options.confirm.buttons.length !== 2) {
            // Someone removed the confirmation buttons, darnit!  Report the error and re-add the buttons.
            var message = "Arc Welder detected the removal or addition of PNotify default confirmation buttons, " +
                "which should not be done in a shared environment.  Some plugins may show strange behavior.  Please " +
                "report this error at https://github.com/FormerLurker/ArcWelder/issues.  ArcWelder will now clear " +
                "and re-add the default PNotify buttons.";
            console.error(message);

            // Reset the buttons in case extra buttons were added.
            PNotify.prototype.options.confirm.buttons = [];

            var buttons = [
                {
                    text: "Ok",
                    addClass: "",
                    promptTrigger: true,
                    click: function (b, a) {
                        b.remove();
                        b.get().trigger("pnotify.confirm", [b, a])
                    }
                },
                {
                    text: "Cancel",
                    addClass: "",
                    promptTrigger: true,
                    click: function (b) {
                        b.remove();
                        b.get().trigger("pnotify.cancel", b)
                    }
                }
            ];
            PNotify.prototype.options.confirm.buttons = buttons;
        }
    };

    ArcWelder.ConfirmDialogs = {};
    ArcWelder.closeConfirmDialogsForKeys = function (remove_keys) {
        if (!$.isArray(remove_keys)) {
            remove_keys = [remove_keys];
        }
        for (var index = 0; index < remove_keys.length; index++) {
            var key = remove_keys[index];
            if (key in ArcWelder.ConfirmDialogs) {

                ArcWelder.ConfirmDialogs[key].remove();
                delete ArcWelder.ConfirmDialogs[key];
            }
        }
    };

    ArcWelder.showConfirmDialog = function (key, title, text, onConfirm, onCancel, onComplete, onOption, optionButtonText) {
        ArcWelder.closeConfirmDialogsForKeys([key]);
        // Make sure that the default pnotify buttons exist
        ArcWelder.checkPNotifyDefaultConfirmButtons();
        options = {
            title: title,
            text: text,
            icon: 'fa fa-question',
            hide: false,
            addclass: "arc-welder",
            confirm: {
                confirm: true,
            },
            buttons: {
                closer: false,
                sticker: false
            },
            history: {
                history: false
            }
        };
        if (onOption && optionButtonText) {
            var confirmButtons = [
                {
                    text: "Ok",
                    addClass: "",
                    promptTrigger: true,
                    click: function (b, a) {
                        b.remove();
                        b.get().trigger("pnotify.confirm", [b, a])
                    }
                },
                {
                    text: optionButtonText,
                    click: function () {
                        if (onOption)
                            onOption();
                        if (onComplete)
                            onComplete();
                        ArcWelder.closeConfirmDialogsForKeys([key]);
                    }
                },
                {
                    text: "Cancel",
                    addClass: "",
                    promptTrigger: true,
                    click: function (b) {
                        b.remove();
                        b.get().trigger("pnotify.cancel", b)
                    }
                }
            ];
            options.confirm.buttons = confirmButtons;
        }
        ArcWelder.ConfirmDialogs[key] = (
            new PNotify(options)
        ).get().on('pnotify.confirm', function () {
            if (onConfirm)
                onConfirm();
            if (onComplete) {
                onComplete();
            }
        }).on('pnotify.cancel', function () {
            if (onCancel)
                onCancel();
            if (onComplete) {
                onComplete();
            }
        });
    };

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

        self.current_files = null;

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
            return 'https://github.com/FormerLurker/ArcWelderPlugin/releases/tag/v' + self.version();
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

        self.onStartupComplete = function() {
            self.current_files = ko.computed( function() {
                self.addProcessButtonToFileManager(self.files.listHelper.paginatedItems(), self.printer_state.isPrinting());
            }, this);

        };

        self.closePreprocessingPopup = function(){
            if (self.pre_processing_progress != null) {
                self.pre_processing_progress.close();
            }
            self.preprocessing_job_guid = null;
            self.pre_processing_progress = null;
        };

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
                    ArcWelder.displayPopupForKey(options, data.key, data.close_keys);
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
                    ArcWelder.displayPopupForKey(options, "preprocessing-failed", []);
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
                    ArcWelder.displayPopupForKey(options, "preprocessing-cancelled", []);
                    self.closePreprocessingPopup();
                    break;
                case "preprocessing-success":
                    self.closePreprocessingPopup();
                    var progress = data.results.progress;
                    var seconds_elapsed = progress.seconds_elapsed;
                    var arcs_created = progress.arcs_created;
                    var points_compressed = progress.points_compressed;
                    var source_file_size = progress.source_file_size;
                    var target_file_size = progress.target_file_size;
                    var compression_ratio = 0;
                    if (target_file_size > 0)
                        compression_ratio = source_file_size / target_file_size;
                    var space_saved_percent = 0;
                    if (source_file_size > 0)
                        space_saved_percent = (1.0 - (target_file_size / source_file_size)) * 100.0;
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
                            + "<div class='row-fluid'><div class='span5'><strong>Compression</strong><br/> Ratio:" + compression_ratio.toFixed(1) + " - " + space_saved_percent.toFixed(1) + "%</div><div class='span7'><strong>Space</strong><br/>"+ source_size_string + " - " + space_saved_string + " = " + target_size_string + "</div></div>";
                        var options = {
                            title: "Arc Welder Preprocessing Complete",
                            text: progress_text,
                            type: "success",
                            hide: false,
                            addclass: "arc-welder",

                        };
                        ArcWelder.displayPopupForKey(options, "preprocessing-success");
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
                        ArcWelder.displayPopupForKey(options, "preprocessing-success-desktop", []);
                    break;
                case "preprocessing-complete":
                    self.closePreprocessingPopup();
                    break;
                case "preprocessing-progress":
                    // TODO: CHANGE THIS TO A PROGRESS INDICATOR
                    var percent_finished = data.percent_complete;
                    var seconds_elapsed = data.seconds_elapsed;
                    var seconds_remaining = data.seconds_remaining;
                    var arcs_created = data.arcs_created;
                    var points_compressed = data.points_compressed;
                    var source_file_size = data.source_file_size;
                    var target_file_size = data.target_file_size;
                    var compression_ratio = 0;
                    if (target_file_size > 0)
                        compression_ratio = source_file_size / target_file_size;
                    var space_saved_percent = 0;
                    if (source_file_size > 0)
                        space_saved_percent = (1.0 - (target_file_size / source_file_size)) * 100.0;
                    var space_saved_string = ArcWelder.toFileSizeString(source_file_size - target_file_size, 1);
                    var source_size_string = ArcWelder.toFileSizeString(source_file_size, 1);
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
                        + "<div class='span5'><strong>Compression</strong><br/> Ratio:" + compression_ratio.toFixed(1) + " - " + space_saved_percent.toFixed(1) + "%</div>"
                        + "<div class='span7'><strong>Space</strong><br/>"+ source_size_string + " - " + space_saved_string + " = " + target_size_string + "</div>"
                        + "</div>";
                        //+ "  Line:" + lines_processed.toString()
                        //+ "<div class='row-fluid'><span class='span6'><strong>Arcs Created</strong><br/>" + arcs_created.toString() + "</span>"
                        //+ "<span class='span6'><strong>Points Compressed</strong><br/>" + points_compressed.toString() + "</span><div/>";
                    self.pre_processing_progress = self.pre_processing_progress.update(
                        percent_finished, progress_text
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
                    ArcWelder.displayPopupForKey(options, "unknown_message_type", ["unknown_message_type"]);
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
            self.pre_processing_progress = ArcWelder.progressBar("Initializing...", "Arc Welder Progress",
                subtitle, cancel_callback, "Cancel All", cancel_all_callback);
        };

        self.cancelAllPreprocessing = function() {
            self.cancelPreprocessingRequest(true);
        };

        self.cancelPreprocessing = function () {
            self.cancelPreprocessingRequest(false);
        };

        self.cancelPreprocessingRequest = function(cancel_all)
        {
            var data = {
                "cancel_all": cancel_all,
                "preprocessing_job_guid": self.preprocessing_job_guid
            };
            $.ajax({
                url: "./plugin/arc_welder/cancelPreprocessing",
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
                    ArcWelder.displayPopupForKey(options,"cancel-popup-error", ["cancel-popup-error"]);
                    return false;
                }
            });
        }

        self.removeEditButtons = function() {
            $("#files div.gcode_files div.entry .action-buttons div.btn-mini.arc-welder").remove();
        };

        self.getEntryId = function(file){
            return "gcode_file_" + md5(file.origin + ":" + file.path);
        };

        self.addProcessButtonToFileManager = function(current_page, is_printing) {
            self.removeEditButtons();
            if (!self.plugin_settings.feature_settings.show_file_manager_buttons())
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

                var button_disabled = false;
                var title = "Weld Arcs";
                if (is_printing)
                {
                    button_disabled = true;
                    title = "Cannot weld arcs during a print, this would impact performance.";
                }
                else if (file.origin !== "local")
                {
                    button_disabled = true;
                    title = "Cannot weld arcs for files stored on your printer's SD card.";
                }
                else if (file.arc_welder)
                {
                    button_disabled = true;
                    title = "This file has already been processed by Arc Welder.";
                }
                // Create the button
                var $button = $('\
                    <div class="btn btn-mini arc-welder ' + (button_disabled ? "disabled" : "") +'" title="' + title + '">\
                        <i class="fa fa-compress"></i>\
                    </div>\
                ');
                // Add an on click event if the button is not disabled
                if (!button_disabled)
                {
                    var data = {path: file.path};
                    $button.click(data, function(e) {
                        self.processButtonClicked(e);
                    });
                }

                // Add the button to the file manager
                $(file_element).find("a.btn-mini").after($button);
            }

        };

        self.processButtonClicked = function(event) {
            // Get the path
            var path = event.data.path;
            console.log("Button Clicked: " + path);
            // disable the element
            $(event.target).addClass("disabled");
            // Request that the file be processed
            var data = { "path": encodeURI(path)};
            $.ajax({
                url: "./plugin/arc_welder/process",
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
                            text: "The file '" + path + "' has been queued for processing.",
                            type: 'info',
                            hide: true,
                            addclass: "arc-welder",
                            desktop: {
                                desktop: true
                            }
                        };
                        ArcWelder.displayPopupForKey(options,"process-error", ["process-error"]);
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
                        ArcWelder.displayPopupForKey(options,"process-error", ["process-error"]);
                    }
                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    var message = "Could not pre-process '"+ path +"'.  Check plugin_arc_welder.log for details.";
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
                    ArcWelder.displayPopupForKey(options,"process-error", ["process-error"]);
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

    OCTOPRINT_VIEWMODELS.push([
        ArcWelder.ArcWelderViewModel,
        ["settingsViewModel", "loginStateViewModel", "filesViewModel", "printerStateViewModel"],
        ["#tab_plugin_arc_welder_controls"]
    ]);
});

