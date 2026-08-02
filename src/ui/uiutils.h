#ifndef AI_CAPTION_PLUGIN_UI_UTILS_H
#define AI_CAPTION_PLUGIN_UI_UTILS_H

#include "../CaptionPluginManager.h"
#include "../data.h"
#include "../log.c"

#include <obs.h>

#include <string>
#include <tuple>
#include <vector>

using ObsSourceTup = std::tuple<string, string, uint32_t>;

static vector<ObsSourceTup> get_obs_sources() {
    vector<ObsSourceTup> sources;
    obs_enum_sources(
            [](void *parameter, obs_source_t *source) {
                auto *result = reinterpret_cast<vector<ObsSourceTup> *>(parameter);
                const char *type = obs_source_get_unversioned_id(source);
                const char *name = obs_source_get_name(source);
                if (result && type && name)
                    result->emplace_back(name, type, obs_source_get_output_flags(source));
                return true;
            },
            &sources);
    return sources;
}

static vector<string> get_audio_sources() {
    vector<string> audio_sources;
    for (const auto &source : get_obs_sources()) {
        if (std::get<2>(source) & OBS_SOURCE_AUDIO)
            audio_sources.push_back(std::get<0>(source));
    }
    return audio_sources;
}

static bool captioning_status_string(
        bool enabled,
        const CaptioningState &captioning_state,
        const SourceCaptionerStatus &status,
        string &output) {
    const string source_name = corrected_streaming_audio_output_capture_source_name(
            status.settings.caption_source_settings.caption_source_name);

    if (!enabled) {
        output = "Субтитры выключены";
        return true;
    }
    if (source_name.empty()) {
        output = "Выберите источник звука";
        return true;
    }
    if (status.event_type == SOURCE_CAPTIONER_STATUS_EVENT_STARTED_ERROR) {
        output = "Не удалось запустить локальное распознавание";
        return true;
    }
    if (status.event_type == SOURCE_CAPTIONER_STATUS_EVENT_STOPPED ||
        status.event_type == SOURCE_CAPTIONER_STATUS_EVENT_NEW_SETTINGS_STOPPED) {
        output = "Ожидание потребителя";
        return true;
    }
    if (status.audio_capture_status == AUDIO_SOURCE_MUTED) {
        output = "Источник заглушен";
        return true;
    }
    if (status.audio_capture_status == AUDIO_SOURCE_NOT_STREAMED) {
        output = "Источник не попадает в активный микс";
        return true;
    }
    if (status.audio_capture_status != AUDIO_SOURCE_CAPTURING) {
        output = "Неизвестное состояние";
        return false;
    }

    vector<string> consumers;
    if (captioning_state.is_captioning_browser_overlay)
        consumers.emplace_back("экран");
    if (captioning_state.is_captioning_file_output)
        consumers.emplace_back("файл");
    if (captioning_state.is_captioning_streaming)
        consumers.emplace_back("стрим");
    if (captioning_state.is_captioning_preview)
        consumers.emplace_back("превью");

    output = "Распознавание: " + source_name;
    if (!consumers.empty()) {
        output += " → ";
        for (size_t index = 0; index < consumers.size(); ++index) {
            if (index)
                output += ", ";
            output += consumers[index];
        }
    }
    return true;
}

#endif // AI_CAPTION_PLUGIN_UI_UTILS_H
