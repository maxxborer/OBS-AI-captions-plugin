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
#include "CaptionFileName.h"
#include "EnglishTermReplacements.h"
#include "storage_utils.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <obs-module.h>

constexpr auto kSettingsSaveEntryName = "ai_caption_plugin";
constexpr auto kLegacySettingsSaveEntryName = "cloud_closed_caption_rat";

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
    return CaptionPluginSettings(
            false,
            default_SourceCaptionerSettings(),
            new_browser_overlay_settings());
}

static std::string normalized_local_hotwords(const std::string &input) {
    QStringList phrases;
    int bytes = 0;
    for (const QString &line : QString::fromStdString(input).split('\n')) {
        const QString phrase = line.trimmed();
        const int phrase_bytes = phrase.toUtf8().size();
        if (phrase.isEmpty() || phrase_bytes > 160 || phrases.size() >= 64 ||
            bytes + phrase_bytes + (phrases.isEmpty() ? 0 : 1) > 2048) {
            continue;
        }
        bytes += phrase_bytes + (phrases.isEmpty() ? 0 : 1);
        phrases.push_back(phrase);
    }
    return phrases.join('\n').toStdString();
}

static std::vector<TextReplacement> get_TextReplacements(obs_data_t *load_data) {
    std::vector<TextReplacement> replacements;
    if (!load_data)
        return replacements;

    obs_data_array_t *array = obs_data_get_array(load_data, "word_replacements");
    if (!array)
        return replacements;
    const size_t count = std::min(
            obs_data_array_count(array),
            static_cast<size_t>(kMaximumTextReplacements));
    replacements.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        obs_data_t *item = obs_data_array_item(array, index);
        if (!item)
            continue;
        TextReplacement replacement{
                obs_data_get_string(item, "type"),
                obs_data_get_string(item, "from"),
                obs_data_get_string(item, "to")};
        if (text_replacement_is_valid(replacement))
            replacements.push_back(std::move(replacement));
        obs_data_release(item);
    }
    obs_data_array_release(array);
    return replacements;
}

static void set_TextReplacements(
        obs_data_t *save_data,
        const std::vector<TextReplacement> &input) {
    obs_data_array_t *array = obs_data_array_create();
    for (const TextReplacement &replacement : normalized_text_replacements(input)) {
        obs_data_t *item = obs_data_create();
        obs_data_set_string(item, "type", replacement.type.c_str());
        obs_data_set_string(item, "from", replacement.from.c_str());
        obs_data_set_string(item, "to", replacement.to.c_str());
        obs_data_array_push_back(array, item);
        obs_data_release(item);
    }
    obs_data_set_array(save_data, "word_replacements", array);
    obs_data_array_release(array);
}

static std::vector<TextReplacement> get_legacy_text_replacements(obs_data_t *legacy_data) {
    std::vector<TextReplacement> replacements = get_TextReplacements(legacy_data);
    replacements.erase(
            std::remove_if(
                    replacements.begin(),
                    replacements.end(),
                    is_builtin_english_term_replacement),
            replacements.end());
    if (!replacements.empty() || !legacy_data)
        return replacements;

    const QString banned_words = QString::fromUtf8(
            obs_data_get_string(legacy_data, "manual_banned_words"));
    for (const QString &word : banned_words.split(
                 QRegularExpression(QStringLiteral("\\s+")),
                 Qt::SkipEmptyParts)) {
        replacements.push_back(TextReplacement{
                "text_case_insensitive",
                word.toStdString(),
                ""});
        if (replacements.size() >= kMaximumTextReplacements)
            break;
    }
    return normalized_text_replacements(replacements);
}

static void enforce_settings(CaptionPluginSettings &settings) {
    FileOutputSettings &file = settings.source_cap_settings.file_output_settings;
    if (file.line_length == 0 || file.line_length > 200)
        file.line_length = 32;
    if (file.line_count == 0 || file.line_count > 6)
        file.line_count = 3;
    file.filename_custom = sanitize_caption_filename_template(
                                   QString::fromStdString(file.filename_custom))
                                   .toStdString();
    settings.source_cap_settings.format_settings.text_replacements =
            normalized_text_replacements(
                    settings.source_cap_settings.format_settings.text_replacements);
    if (settings.source_cap_settings.local_caption_model != LocalCaptionModel::TOne &&
        settings.source_cap_settings.local_caption_model != LocalCaptionModel::Nemotron560ms) {
        settings.source_cap_settings.local_caption_model = LocalCaptionModel::TOne;
    }
    settings.source_cap_settings.local_hotwords =
            normalized_local_hotwords(settings.source_cap_settings.local_hotwords);

    const QString token = QString::fromStdString(settings.browser_overlay.access_token);
    static const QRegularExpression valid_token(QStringLiteral("^[0-9a-f]{64}$"));
    if (settings.browser_overlay.port < 49152 ||
        !valid_token.match(token).hasMatch()) {
        settings.browser_overlay = new_browser_overlay_settings();
    }
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
    obs_data_set_default_int(load_data, "browser_overlay_port", 0);
    obs_data_set_default_string(load_data, "browser_overlay_token", "");
    obs_data_set_default_string(load_data, "local_caption_model", "t_one");
    obs_data_set_default_string(load_data, "local_hotwords", "");
    obs_data_set_default_bool(load_data, "legacy_word_replacements_migrated", false);

    settings.enabled = obs_data_get_bool(load_data, "enabled");
    source.native_stream_output_enabled = obs_data_get_bool(load_data, "streaming_output_enabled");
    audio.caption_source_name = obs_data_get_string(load_data, "source_name");
    audio.mute_source_name = obs_data_get_string(load_data, "mute_source_name");
    audio.mute_when = string_to_mute_setting(
            obs_data_get_string(load_data, "source_caption_when"));
    file.enabled = obs_data_get_bool(load_data, "file_output_enabled");
    file.output_folder = obs_data_get_string(load_data, "file_output_folder");
    file.filename_custom = obs_data_get_string(load_data, "file_output_filename_custom");
    source.format_settings.text_replacements = get_TextReplacements(load_data);
    settings.browser_overlay.port = static_cast<std::uint16_t>(
            obs_data_get_int(load_data, "browser_overlay_port"));
    settings.browser_overlay.access_token =
            obs_data_get_string(load_data, "browser_overlay_token");
    settings.source_cap_settings.local_caption_model =
            std::string(obs_data_get_string(load_data, "local_caption_model")) == "nemotron_560ms"
                    ? LocalCaptionModel::Nemotron560ms
                    : LocalCaptionModel::TOne;
    settings.source_cap_settings.local_hotwords =
            obs_data_get_string(load_data, "local_hotwords");

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
    obs_data_set_string(
            save_data,
            "local_caption_model",
            source.local_caption_model == LocalCaptionModel::Nemotron560ms
                    ? "nemotron_560ms"
                    : "t_one");
    obs_data_set_string(save_data, "local_hotwords", source.local_hotwords.c_str());
    set_TextReplacements(save_data, source.format_settings.text_replacements);
    obs_data_set_bool(save_data, "legacy_word_replacements_migrated", true);
    obs_data_set_int(save_data, "browser_overlay_port", settings.browser_overlay.port);
    obs_data_set_string(
            save_data,
            "browser_overlay_token",
            settings.browser_overlay.access_token.c_str());
    obs_data_set_string(save_data, "plugin_version", VERSION_STRING);
}

static CaptionPluginSettings load_CaptionPluginSettings(obs_data_t *load_data) {
    obs_data_t *object = obs_data_get_obj(load_data, kSettingsSaveEntryName);
    CaptionPluginSettings settings = get_CaptionPluginSettings_from_data(object);
    const bool replacements_migrated =
            object && obs_data_get_bool(object, "legacy_word_replacements_migrated");
    if (!replacements_migrated &&
        settings.source_cap_settings.format_settings.text_replacements.empty()) {
        obs_data_t *legacy_object = obs_data_get_obj(
                load_data,
                kLegacySettingsSaveEntryName);
        settings.source_cap_settings.format_settings.text_replacements =
                get_legacy_text_replacements(legacy_object);
        obs_data_release(legacy_object);
    }
    enforce_settings(settings);
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
