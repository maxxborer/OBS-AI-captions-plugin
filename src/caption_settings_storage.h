#ifndef AI_CAPTION_PLUGIN_CAPTION_SETTINGS_STORAGE_H
#define AI_CAPTION_PLUGIN_CAPTION_SETTINGS_STORAGE_H

/******************************************************************************
Copyright (C) 2019 by <rat.with.a.compiler@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#include "CaptionPluginSettings.h"
#include "storage_utils.h"

#include <QFileInfo>

#include <obs-module.h>

constexpr auto kSettingsSaveEntryName = "ai_caption_plugin";

static CaptionFormatSettings default_CaptionFormatSettings() {
    return {
            32,
            3,
            false,
            false,
            true,
            15.0,
    };
}

static FileOutputSettings default_FileOutputSettings() {
    return {};
}

static CaptionSourceSettings default_CaptionSourceSettings() {
    return {"", CAPTION_SOURCE_MUTE_TYPE_FROM_OWN_SOURCE, ""};
}

static SourceCaptionerSettings default_SourceCaptionerSettings() {
    return SourceCaptionerSettings(
            false,
            default_FileOutputSettings(),
            default_CaptionSourceSettings(),
            default_CaptionFormatSettings());
}

static CaptionPluginSettings default_CaptionPluginSettings() {
    return CaptionPluginSettings(false, default_SourceCaptionerSettings());
}

static void enforce_settings(CaptionPluginSettings &settings) {
    FileOutputSettings &file = settings.source_cap_settings.file_output_settings;
    if (file.line_length == 0 || file.line_length > 200)
        file.line_length = 32;
    if (file.line_count == 0 || file.line_count > 6)
        file.line_count = 3;
    const QString safe_name = QFileInfo(QString::fromStdString(file.filename_custom)).fileName();
    file.filename_custom = safe_name.isEmpty() ? "captions.txt" : safe_name.toStdString();
}

static CaptionPluginSettings get_CaptionPluginSettings_from_data(obs_data_t *load_data) {
    CaptionPluginSettings settings = default_CaptionPluginSettings();
    if (!load_data)
        return settings;

    SourceCaptionerSettings &source = settings.source_cap_settings;
    CaptionSourceSettings &audio = source.caption_source_settings;
    FileOutputSettings &file = source.file_output_settings;

    obs_data_set_default_bool(load_data, "enabled", settings.enabled);
    obs_data_set_default_bool(load_data, "streaming_output_enabled", source.native_stream_output_enabled);
    obs_data_set_default_string(load_data, "source_name", audio.caption_source_name.c_str());
    obs_data_set_default_string(load_data, "mute_source_name", audio.mute_source_name.c_str());
    obs_data_set_default_string(load_data, "source_caption_when", "own_source");
    obs_data_set_default_bool(load_data, "file_output_enabled", file.enabled);
    obs_data_set_default_string(load_data, "file_output_folder", file.output_folder.c_str());
    obs_data_set_default_string(load_data, "file_output_filename_custom", file.filename_custom.c_str());

    settings.enabled = obs_data_get_bool(load_data, "enabled");
    source.native_stream_output_enabled = obs_data_get_bool(load_data, "streaming_output_enabled");
    audio.caption_source_name = obs_data_get_string(load_data, "source_name");
    audio.mute_source_name = obs_data_get_string(load_data, "mute_source_name");
    audio.mute_when = string_to_mute_setting(
            obs_data_get_string(load_data, "source_caption_when"));
    file.enabled = obs_data_get_bool(load_data, "file_output_enabled");
    file.output_folder = obs_data_get_string(load_data, "file_output_folder");
    file.filename_custom = obs_data_get_string(load_data, "file_output_filename_custom");

    enforce_settings(settings);
    return settings;
}

static void set_CaptionPluginSettings_on_data(
        obs_data_t *save_data,
        const CaptionPluginSettings &settings) {
    const SourceCaptionerSettings &source = settings.source_cap_settings;
    const CaptionSourceSettings &audio = source.caption_source_settings;
    const FileOutputSettings &file = source.file_output_settings;

    obs_data_set_bool(save_data, "enabled", settings.enabled);
    obs_data_set_bool(save_data, "streaming_output_enabled", source.native_stream_output_enabled);
    obs_data_set_string(save_data, "source_name", audio.caption_source_name.c_str());
    obs_data_set_string(save_data, "mute_source_name", audio.mute_source_name.c_str());
    const string caption_when = mute_setting_to_string(audio.mute_when);
    obs_data_set_string(save_data, "source_caption_when", caption_when.c_str());
    obs_data_set_bool(save_data, "file_output_enabled", file.enabled);
    obs_data_set_string(save_data, "file_output_folder", file.output_folder.c_str());
    obs_data_set_string(save_data, "file_output_filename_custom", file.filename_custom.c_str());
    obs_data_set_string(save_data, "plugin_version", VERSION_STRING);
}

static CaptionPluginSettings load_CaptionPluginSettings(obs_data_t *load_data) {
    obs_data_t *object = obs_data_get_obj(load_data, kSettingsSaveEntryName);
    CaptionPluginSettings settings = get_CaptionPluginSettings_from_data(object);
    obs_data_release(object);
    return settings;
}

static void save_CaptionPluginSettings(
        obs_data_t *save_data,
        const CaptionPluginSettings &settings) {
    obs_data_t *object = obs_data_create();
    set_CaptionPluginSettings_on_data(object, settings);
    obs_data_set_obj(save_data, kSettingsSaveEntryName, object);
    obs_data_release(object);
}

#endif // AI_CAPTION_PLUGIN_CAPTION_SETTINGS_STORAGE_H
