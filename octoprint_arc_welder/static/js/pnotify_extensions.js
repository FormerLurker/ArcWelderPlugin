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
    var createPNotifyExtensions = function (options) {
        var self = this;
        self.progressBar = function (initial_text, title, subtitle, cancel_callback, option_button_text, on_option) {
            var self = this;
            self.notice = null;
            self.$progress = null;
            self.$progressText = null;
            self.$progressBarText = null;
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
                self.$progressBarText.text (percent_complete_text + "%");
                self.$progressText.html(progress_text);
                return self;
            };

            self.buttons = [{
                text: 'Close',
                click: self.close
            }];
            if (cancel_callback) {
                self.buttons.push({
                    text: 'Cancel',
                    click: cancel_callback
                });
            }
            if (option_button_text && on_option) {
                self.buttons.push({
                    text: option_button_text,
                    click: on_option
                });
            }
            // create the pnotify loader
            self.loader = new PNotify({
                title: title,
                /*text: '<div class="progress-sub-title"></div><div class="progress progress-striped active" style="margin:0"><div class="arc-welder progress-bar" role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100" style="width: 0"></div></div><div class="progress-text" style="width:100%;"></div></div>',*/
                text: '<div class="progress-bar-container" style="margin:0"><div class="progress-bar-info text-center"><span class="progress-bar-text"></span></div><div class="progress-bar" style="margin:0" role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100" style="width: 0"></div></div><div class="progress-text" style="width:100%;"></div></div>',
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
                    self.$progressBarText = self.notice.find("span.progress-bar-text")
                    self.notice.find(".remove_button").remove();
                    self.$subTitle = self.notice.find("div.progress-sub-title");
                    self.$subTitle.html(self.subtitle);
                    self.update(0, self.initial_text);
                }
            });
            return self;
        };
        self.Popups = {};
        self.displayPopupForKey = function (options, popup_key, remove_keys) {
            self.closePopupsForKeys(remove_keys);
            var popup = new PNotify(options);
            self.Popups[popup_key] = popup;
            return popup;
        };
        self.closePopupsForKeys = function (remove_keys) {
            if (!$.isArray(remove_keys)) {
                remove_keys = [remove_keys];
            }
            for (var index = 0; index < remove_keys.length; index++) {
                var key = remove_keys[index];
                if (key in self.Popups) {
                    var notice = self.Popups[key];
                    if (notice.state === "opening") {
                        notice.options.animation = "none";
                    }
                    notice.remove();
                    delete self.Popups[key];
                }
            }
        };
        self.removeKeyForClosedPopup = function (key) {
            if (key in self.Popups) {
                var notice = self.Popups[key];
                delete self.Popups[key];
            }
        };
        self.checkPNotifyDefaultConfirmButtons = function () {
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

        self.ConfirmDialogs = {};
        self.closeConfirmDialogsForKeys = function (remove_keys) {
            if (!$.isArray(remove_keys)) {
                remove_keys = [remove_keys];
            }
            for (var index = 0; index < remove_keys.length; index++) {
                var key = remove_keys[index];
                if (key in self.ConfirmDialogs) {

                    self.ConfirmDialogs[key].remove();
                    delete self.ConfirmDialogs[key];
                }
            }
        };

        self.showConfirmDialog = function (key, title, text, onConfirm, onCancel, onComplete, onOption, optionButtonText) {
            self.closeConfirmDialogsForKeys([key]);
            // Make sure that the default pnotify buttons exist
            self.checkPNotifyDefaultConfirmButtons();
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
                            self.closeConfirmDialogsForKeys([key]);
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
            self.ConfirmDialogs[key] = (
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
    }
    PNotifyExtensions = new createPNotifyExtensions({});
});