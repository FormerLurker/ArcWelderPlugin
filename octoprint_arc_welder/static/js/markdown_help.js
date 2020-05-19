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

/*#################################################################################
# Prerequisites:  Requires showdown.min.js and pnotify_extensions.js
################################################################################### */

$(function () {
    MarkdownHelp = function (options) {
        var self = this;
        self.missing_file_text = options.missing_file_text;
        self.plugin_id = options.plugin_id;
        self.plugin_name = options.plugin_name;
        self.add_class = options.add_class;
        self.document_root_url = options.document_root_url;
        self.html_text = "unknown";
        self.popup_margin = 15;
        self.popup_width_with_margin = 900;
        self.popup_width = self.popup_width_with_margin - self.popup_margin*2;
        self.PopupKey = function(key) {
            if (Array.isArray(key))
            {
                var keys = [];
                for (var index=0; index < key.length; index++)
                {
                    keys.push(self.plugin_id + "_" + key);
                }
                return keys;
            }
            return self.plugin_id + "_" + key;
        };
        self.stack_center = {
            "dir1": "down",
            "dir2": "right",
            "firstpos1": self.popup_margin,
            "firstpos2": (document.body.clientWidth / 2) - (self.popup_width / 2 + self.popup_margin),
            "modal": true
        };

        self.options = {
            title: self.plugin_name + " Help",
            text: "unknown",
            hide: false,
            stack: self.stack_center,
            width: self.popup_width.toString() + "px",
            addclass: self.add_class,
            type: "info",
            icon: 'fa fa-question-circle fa-lg',
            Buttons: {
              closer: false,
              sticker: false
            },
            confirm: {
                confirm: true,
                buttons: [{
                    text: 'Close',
                    primary: true,
                    click: function(notice) {
                      notice.remove();
                    }},{
                        text: 'Close',
                        addClass: 'remove_button'
                    }
                ]
            },
            before_open: function(notice) {
                // We want to remove the default button, else two will show
                var notice = notice.get();
                notice.find(".remove_button").remove();

            },
            after_open: function(notice) {

                var $popup_item = $(notice.elem);
                var $help_container = $popup_item.find("div.ui-pnotify-container");
                var $help_text = $popup_item.find(".ui-pnotify-text");
                $help_text.html(self.options.text);
                var body_color = $("body").css('color');
                var body_background_color = $("body").css('background-color');
                var body_font_weight = $("body").css('font-weight');

                $help_container.css("background-color", body_background_color)
                    .css("border-color", "#000000")
                    .css("border-width", "3px")
                    .css("color", body_color)
                    .css("font-weight", body_font_weight);

                // Now we want to put the notice inside another div so we can add an overlay effect
                // since modal doesn't seem to be working in this version of pnotify (I could be wrong
                // but the docs I can find are definitely not for this version.)
                // first create a div and insert it before the notice
                var $overlay = $('<div class="' + self.plugin_id + '-pnotify-overlay modal-backdrop fade in" style="z-index:1070"></div>');
                // now move our notice inside of the new div
                $overlay.appendTo($(notice.elem).parent());
                $overlay.click(function(){
                    notice.remove();
                });
                self.resize_handler(null, notice.elem);
                window.addEventListener('resize', function() { self.resize_handler(this, notice.elem);})
            },
            after_close: function(notice){
                var $overlay = $(notice.elem).parent().find("." + self.plugin_id + "-pnotify-overlay");
                $overlay.remove();
                PNotifyExtensions.removeKeyForClosedPopup(self.PopupKey('help'));
                //console.log("Removing resize handler.");
                window.removeEventListener('resize', self.resize_handler)
            }
        };

        self.resize_timer = null;
        self.resize_handler = function(event, elem) {
            //console.log("Resizing Help.");
            if(self.resize_timer)
            {
                clearTimeout(self.resize_timer);
                self.resize_timer = null;
            }
            self.resize_timer = setTimeout(resize_help_popup, 100);
            function resize_help_popup (){
                var width = self.popup_width.toString();

                if (document.body.clientWidth < self.popup_width_with_margin) {
                    self.stack_center.firstpos2 = self.popup_margin;
                    width = (document.body.clientWidth - (self.popup_margin * 2)).toString();
                }
                else {
                    self.stack_center.firstpos2 = (document.body.clientWidth / 2) - (self.popup_width / 2);
                }
                // get the left position
                var left = (document.body.clientWidth - width)/2;
                $(elem).css("width", width)
                    .css("left", left)
                    .css("top", "15px")
            }

        };

        self.converter = new showdown.Converter({
            openLinksInNewWindow: true,
            simpleLineBreaks: false
        });

        self.converter.setFlavor('github');

        self.showHelpForLink = function (doc, title, custom_not_found_message){
            url = self.document_root_url + doc + "?nonce=" + Date.now().toString();
            $.ajax({
                url: url,
                type: "GET",
                dataType: "text",
                success: function (results) {

                    //console.log(results);
                    self.options.text = self.converter.makeHtml(results);
                    // Set the option text to a known token so that we can replace it with our markdown
                    self.options.title = title;
                    PNotifyExtensions.displayPopupForKey(
                        self.options,
                        self.PopupKey("help"),
                        self.PopupKey([ self.plugin_id + "help"])
                    );
                },
                error: function (XMLHttpRequest, textStatus, errorThrown) {
                    if (errorThrown === "NOT FOUND")
                    {
                        var body_color = $("body").css('color');
                        var body_background_color = $("body").css('background-color');
                        var body_font_weight = $("body").css('font-weight');
                        var missing_file_text = self.missing_file_text;
                        if (custom_not_found_message)
                            self.options.text = self.converter.makeHtml(custom_not_found_message);
                        else
                            self.options.text = self.converter.makeHtml(missing_file_text);

                        self.options.title = "Help Could Not Be Found";

                        var popup = PNotifyExtensions.displayPopupForKey(
                            self.options,
                            self.PopupKey("help"),
                            self.PopupKey(["help"])
                        );
                        var $popup_item = $(popup.elem);
                        $popup_item.find("." + self.plugin_id + "-pnotify-help div.ui-pnotify-container")
                            .css("background-color", body_background_color)
                            .css("border-color", "#000000")
                            .css("border-width", "3px")
                            .css("color", body_color)
                            .css("font-weight", body_font_weight);
                    }
                    else {
                        var options = {
                            title: 'Error',
                            text: "Unable to retrieve help for '" + title + "'!  Status: " + textStatus + ".  Error: " + errorThrown,
                            type: 'error',
                            hide: true,
                            addclass: self.add_class
                        };
                        PNotifyExtensions.displayPopupForKey(
                            options,
                            self.PopupKey( "help"),
                            self.PopupKey(["help"])
                        );
                    }
                }
            });
        };

        self.bindHelpLinks = function(selector){
            var default_selector = "." + self.plugin_id + "_help[data-help-url]";
            selector = selector + " " + default_selector;
            $(selector).each(function(){
               if (!$(this).attr('data-help-title'))
                   $(this).attr('data-help-title',"Click for help with this");
               if($(this).children().length == 0) {
                   var icon = $('<span class="fa fa-question-circle fa-lg"></span>');
                   $(this).append(icon);
               }
            });
            $(selector).unbind("click");
            $(selector).click( function(e) {
               // get the data group data
                var url = $(this).attr('data-help-url');
                var title = $(this).attr('data-help-title');
                var custom_not_found_error = $(this).attr('data-help-not-found');
                if (!title)
                    title = "Help";
                self.showHelpForLink(url, title, custom_not_found_error);
                e.preventDefault();
            });
        };

        self.showPopupForErrors = function(options, popup_key, remove_keys, errors)
        {
            var error_popup = this;
            error_popup.original_title = options.title;
            error_popup.errors = errors;
            error_popup.current_error_index = 0;
            error_popup.$error_element = null;
            error_popup.$error_title = "";
            error_popup.$error_text = null;
            error_popup.$previous_button = null;
            error_popup.$next_button = null;
            error_popup.$help_button = null;
            error_popup.$close_button = null;
            error_popup.$button_container = null;
            error_popup.$button_row = null;
            error_popup.$button_left_column = null;
            error_popup.$button_right_column = null;

            error_popup.configure_notice = function(notice){
                // Find notification elements for later use
                error_popup.$error_element = notice.get();
                error_popup.$error_title = error_popup.$error_element.find(".ui-pnotify-title");
                error_popup.$error_text = error_popup.$error_element.find("div.ui-pnotify-text");
                error_popup.$previous_button = error_popup.$error_element.find("button.error_previous");
                error_popup.$next_button = error_popup.$error_element.find("button.error_next");
                error_popup.$help_button = error_popup.$error_element.find("button.error_help");
                error_popup.$close_button = error_popup.$error_element.find("button.error_close");
                error_popup.$button_container = error_popup.$close_button.parent();
                // create the button row, left column and right column
                error_popup.$button_row = $('<div class="row-fluid"></div>');
                error_popup.$button_left_column = $('<div class="span6 text-left"></div>');
                error_popup.$button_right_column = $('<div class="span6 text-right"></div>');
                // add the button row
                error_popup.$button_container.append(error_popup.$button_row);
                // Add the columns
                error_popup.$button_row.append(error_popup.$button_left_column);
                error_popup.$button_row.append(error_popup.$button_right_column);
                // add the buttons to the columns
                error_popup.$button_left_column.append(error_popup.$previous_button);

                if(error_popup.errors.length == 1) {
                    // If there is only one error, add the help button to the right column
                    error_popup.$button_right_column.append(error_popup.$help_button);
                }
                else {
                    // If there are multiple errors, add the help button to the left column.
                    error_popup.$button_left_column.append(error_popup.$help_button);
                }
                error_popup.$button_left_column.append(error_popup.$next_button);
                error_popup.$button_right_column.append(error_popup.$close_button);

                // configure next/previous button icons
                error_popup.$previous_button.html('<span class="fa fa-caret-left"></span>');
                error_popup.$next_button.html('<span class="fa fa-caret-right"></span>');

                // Show/Hide next/previous buttons as required
                if(error_popup.errors.length == 1)
                {
                    // hide next/previous buttons
                    error_popup.$previous_button.hide();
                    error_popup.$next_button.hide();
                }
                else {
                    // show next/previous buttons
                    error_popup.$previous_button.show();
                    error_popup.$next_button.show();
                }
            };

            error_popup.current_error = function()
            {
                return errors[error_popup.current_error_index];
            };

            error_popup.set_title = function(){
                var title_html = "";
                //var title_text = error_popup.original_title;
                var current_error = error_popup.current_error();
                var error_title_text = current_error.name;
                if (error_popup.errors.length == 1)
                {
                    title_html = '<h4>' + error_popup.original_title + '<h4/><h5>'+ error_title_text +'</h5>';
                }
                else
                {
                    title_html =
                        '<h4>' + error_popup.original_title +
                        ' - ' + (error_popup.current_error_index+1).toString() + ' of ' + error_popup.errors.length +
                        '<h4/><h5>'+ error_title_text +'</h5>';
                }
                error_popup.$error_title.html(title_html);
            };
            error_popup.show_current_error = function()
            {
                var current_error = error_popup.current_error();
                error_popup.$error_text.text(current_error.description);
                error_popup.set_title();
                // enable/disable buttons
                // if we only have 1 error we don't want any previous/next buttons
                if(error_popup.errors.length > 1)
                {
                    // disable buttons as necessary
                    error_popup.$previous_button.prop('disabled', error_popup.current_error_index == 0);
                    error_popup.$next_button.prop('disabled', error_popup.current_error_index + 1 == self.errors.length);
                }
            };
            error_popup.display_help_for_current_error = function()
            {
                var current_error = error_popup.current_error();
                showHelpForLink(
                    current_error.help_link,
                    current_error.name,
                    self.missing_file_text);
            };

            error_popup.next_error = function()
            {
                error_popup.current_error_index = (error_popup.errors.length+error_popup.current_error_index+1) % error_popup.errors.length;
                error_popup.show_current_error();
            };

            error_popup.previous_error = function()
            {
                error_popup.current_error_index = (error_popup.errors.length+error_popup.current_error_index-1) % error_popup.errors.length;
                error_popup.show_current_error();
            };

            PNotifyExtensions.closeConfirmDialogsForKeys(self.PopupKey([remove_keys]));
            // Make sure that the default pnotify buttons exist
            PNotifyExtensions.checkPNotifyDefaultConfirmButtons();
            PNotifyExtensions.ConfirmDialogs[popup_key] = (
                new PNotify({
                    title: options.title,
                    text: options.text,
                    icon: options.icon,
                    hide: options.hide,
                    desktop: options.desktop,
                    type: options.type,
                    addclass: options.addclass,
                    confirm: {
                        confirm: true,
                        buttons: [{
                            text: 'Ok',
                            addClass: 'remove_button',
                        },{
                            text: 'Cancel',
                            addClass: 'remove_button',
                        },{
                            // Previous Error
                            text: '',
                            addClass: 'error_previous',
                            click: function(){
                                //console.log("Showing the previous error");
                                error_popup.previous_error();
                            }
                        },{
                            text: 'Help',
                            addClass: 'error_help',
                            click: function() {
                                var current_error = error_popup.current_error();
                                MarkdownHelp.showHelpForLink(
                                    current_error.help_link,
                                    current_error.name,
                                    "No help could be found for this error.");
                            }
                        },{
                            // Next Error
                            text: '',
                            addClass: 'error_next',
                            click: function(){
                                //console.log("Showing the next error");
                                error_popup.next_error();
                            }
                        },{
                            text: 'Close',
                            addClass: 'error_close',
                            click: function(){
                                //console.log("Closing popup with key: " + popup_key.toString());
                                PNotifyExtensions.closeConfirmDialogsForKeys(self.PopupKey([popup_key]));
                            }
                        }]
                    },
                    buttons: {
                        closer: false,
                        sticker: false
                    },
                    history: {
                        history: false
                    },
                    before_open: function(notice) {
                        notice.get().find(".remove_button").remove();
                        error_popup.configure_notice(notice);
                        error_popup.show_current_error();
                    }
                })
            );
        };
    }
});
