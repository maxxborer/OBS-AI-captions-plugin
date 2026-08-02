/******************************************************************************
Copyright (C) 2019 by <rat.with.a.compiler@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#include "SourceCaptioner.h"

#include <SherpaTOneCaptionEngine.h>
#include <obs-module.h>
#include <util/util.hpp>

#include "caption_file_writer.h"
#include "caption_output_writer.h"
#include "log.c"

#include <exception>

namespace {
constexpr std::size_t kHistoryEntriesLowWatermark = 40;
constexpr std::size_t kHistoryEntriesHighWatermark = 80;
}

SourceCaptioner::SourceCaptioner(
        const SourceCaptionerSettings &settings,
        bool start)
        : QObject(),
          settings(settings),
          last_caption_at(std::chrono::steady_clock::now()),
          last_caption_cleared(true) {
    QObject::connect(&timer, &QTimer::timeout, this, &SourceCaptioner::clear_output_timer_cb);
    QObject::connect(
            this,
            &SourceCaptioner::received_caption_result,
            this,
            &SourceCaptioner::process_caption_result,
            Qt::QueuedConnection);
    QObject::connect(
            this,
            &SourceCaptioner::audio_capture_status_changed,
            this,
            &SourceCaptioner::process_audio_capture_status_change);

    if (start)
        start_caption_stream(settings);
}

void SourceCaptioner::stop_caption_stream(bool send_signal) {
    timer.stop();
    std::unique_ptr<SourceAudioCaptureSession> source_session_to_stop;
    std::unique_ptr<OutputAudioCaptureSession> output_session_to_stop;
    std::unique_ptr<CaptionEngine> engine_to_stop;
    SourceCaptionerSettings current_settings = settings;
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        current_settings = settings;
        source_session_to_stop = std::move(source_audio_capture_session);
        output_session_to_stop = std::move(output_audio_capture_session);
        engine_to_stop = std::move(caption_engine);
        caption_result_handler.reset();
        reset_caption_state_unlocked();
        ++audio_capture_id;
    }
    source_session_to_stop.reset();
    output_session_to_stop.reset();
    engine_to_stop.reset();
    file_output.clear();

    if (send_signal) {
        emit source_capture_status_changed(std::make_shared<SourceCaptionerStatus>(
                SOURCE_CAPTIONER_STATUS_EVENT_STOPPED,
                current_settings,
                AUDIO_SOURCE_NOT_STREAMED));
    }
}

bool SourceCaptioner::set_settings(const SourceCaptionerSettings &new_settings) {
    timer.stop();
    std::unique_ptr<SourceAudioCaptureSession> source_session_to_stop;
    std::unique_ptr<OutputAudioCaptureSession> output_session_to_stop;
    std::unique_ptr<CaptionEngine> engine_to_stop;
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        settings = new_settings;
        source_session_to_stop = std::move(source_audio_capture_session);
        output_session_to_stop = std::move(output_audio_capture_session);
        engine_to_stop = std::move(caption_engine);
        caption_result_handler.reset();
        reset_caption_state_unlocked();
        ++audio_capture_id;
    }
    source_session_to_stop.reset();
    output_session_to_stop.reset();
    engine_to_stop.reset();
    file_output.clear();

    emit source_capture_status_changed(std::make_shared<SourceCaptionerStatus>(
            SOURCE_CAPTIONER_STATUS_EVENT_NEW_SETTINGS_STOPPED,
            new_settings,
            AUDIO_SOURCE_NOT_STREAMED));
    return true;
}

bool SourceCaptioner::start_caption_stream(const SourceCaptionerSettings &new_settings) {
    timer.stop();
    bool started_ok = false;
    audio_source_capture_status audio_capture_status = AUDIO_SOURCE_NOT_STREAMED;
    const bool source_changed =
            settings.caption_source_settings != new_settings.caption_source_settings;
    std::unique_ptr<SourceAudioCaptureSession> source_session_to_stop;
    std::unique_ptr<OutputAudioCaptureSession> output_session_to_stop;
    std::unique_ptr<CaptionEngine> engine_to_stop;

    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        settings = new_settings;
        source_session_to_stop = std::move(source_audio_capture_session);
        output_session_to_stop = std::move(output_audio_capture_session);
        caption_result_handler.reset();
        reset_caption_state_unlocked();
        ++audio_capture_id;

        if (source_changed)
            engine_to_stop = std::move(caption_engine);
    }
    source_session_to_stop.reset();
    output_session_to_stop.reset();
    engine_to_stop.reset();
    file_output.clear();

    std::unique_ptr<SourceAudioCaptureSession> failed_source_session;
    std::unique_ptr<OutputAudioCaptureSession> failed_output_session;
    std::unique_ptr<CaptionEngine> failed_engine;
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        started_ok = _start_caption_stream();
        if (started_ok) {
            if (source_audio_capture_session)
                audio_capture_status = source_audio_capture_session->get_current_capture_status();
            else if (output_audio_capture_session)
                audio_capture_status = AUDIO_SOURCE_CAPTURING;
            else
                started_ok = false;
        }

        if (!started_ok) {
            failed_source_session = std::move(source_audio_capture_session);
            failed_output_session = std::move(output_audio_capture_session);
            failed_engine = std::move(caption_engine);
            caption_result_handler.reset();
            reset_caption_state_unlocked();
            ++audio_capture_id;
        }
    }
    failed_source_session.reset();
    failed_output_session.reset();
    failed_engine.reset();

    if (started_ok && new_settings.file_output_settings.isValidEnabled()) {
        const FileOutputSettings file_settings = new_settings.file_output_settings;
        auto control = std::make_shared<CaptionOutputControl<FileOutputSettings>>(file_settings);
        file_output.start(control, file_output_writer_loop);
    }

    if (started_ok)
        timer.start(1000);

    emit source_capture_status_changed(std::make_shared<SourceCaptionerStatus>(
            started_ok ? SOURCE_CAPTIONER_STATUS_EVENT_STARTED_OK
                       : SOURCE_CAPTIONER_STATUS_EVENT_STARTED_ERROR,
            new_settings,
            audio_capture_status));
    return started_ok;
}

bool SourceCaptioner::_start_caption_stream() {
    const CaptionSourceSettings &source_settings = settings.caption_source_settings;
    if (source_settings.caption_source_name.empty()) {
        warn_log("SourceCaptioner: no audio source selected");
        return false;
    }

    const bool use_output_audio =
            is_all_audio_output_capture_source_data(source_settings.caption_source_name);
    OBSSource caption_source;
    OBSSource mute_source;

    if (!use_output_audio) {
        caption_source = obs_get_source_by_name(source_settings.caption_source_name.c_str());
        obs_source_release(caption_source);
        if (!caption_source) {
            warn_log("SourceCaptioner: audio source '%s' was not found", source_settings.caption_source_name.c_str());
            return false;
        }

        if (source_settings.mute_when == CAPTION_SOURCE_MUTE_TYPE_USE_OTHER_MUTE_SOURCE) {
            mute_source = obs_get_source_by_name(source_settings.mute_source_name.c_str());
            obs_source_release(mute_source);
            if (!mute_source) {
                warn_log("SourceCaptioner: mute source '%s' was not found", source_settings.mute_source_name.c_str());
                return false;
            }
        }
    }

    if (!caption_engine) {
        try {
            LocalCaptionEngineSettings local_settings;
            BPtr<char> model_directory = obs_module_file(
                    "models/sherpa-onnx-streaming-t-one-russian-2025-09-08");
            if (model_directory)
                local_settings.model_directory = model_directory.Get();

            caption_engine = std::make_unique<SherpaTOneCaptionEngine>(local_settings);
        } catch (const std::exception &error) {
            warn_log("SourceCaptioner: local model initialization failed: %s", error.what());
            return false;
        } catch (...) {
            warn_log("SourceCaptioner: local model initialization failed");
            return false;
        }
    }

    const int callback_id = audio_capture_id;
    caption_engine->on_caption_cb_handle.set(
            [this, callback_id](const CaptionResult &result, bool interrupted) {
                on_caption_text_callback(callback_id, result, interrupted);
            });

    caption_result_handler = std::make_unique<CaptionResultHandler>(settings.format_settings);

    try {
        resample_info resample_to = {
                caption_engine->preferred_sample_rate(),
                AUDIO_FORMAT_16BIT,
                SPEAKERS_MONO};
        audio_chunk_data_cb audio_callback =
                [this](int id, const uint8_t *data, size_t size) {
                    on_audio_data_callback(id, data, size);
                };
        audio_capture_status_change_cb status_callback =
                [this](int id, audio_source_capture_status status) {
                    on_audio_capture_status_change_callback(id, status);
                };

        if (use_output_audio) {
            const int track_index =
                    all_audio_output_capture_source_track_index(source_settings.caption_source_name);
            if (track_index < 0)
                return false;
            output_audio_capture_session = std::make_unique<OutputAudioCaptureSession>(
                    track_index,
                    audio_callback,
                    status_callback,
                    resample_to,
                    audio_capture_id);
        } else {
            const source_capture_config mute_behavior =
                    source_settings.mute_when == CAPTION_SOURCE_MUTE_TYPE_ALWAYS_CAPTION
                            ? MUTED_SOURCE_STILL_CAPTURE
                            : MUTED_SOURCE_REPLACE_WITH_ZERO;
            source_audio_capture_session = std::make_unique<SourceAudioCaptureSession>(
                    caption_source,
                    mute_source,
                    audio_callback,
                    status_callback,
                    resample_to,
                    mute_behavior,
                    false,
                    audio_capture_id);
        }
    } catch (const std::string &error) {
        warn_log("SourceCaptioner: audio capture failed: %s", error.c_str());
        return false;
    } catch (...) {
        warn_log("SourceCaptioner: audio capture failed");
        return false;
    }

    return true;
}

void SourceCaptioner::on_audio_capture_status_change_callback(
        int id,
        audio_source_capture_status status) {
    emit audio_capture_status_changed(id, status);
}

void SourceCaptioner::process_audio_capture_status_change(int callback_id, int new_status) {
    std::unique_lock<recursive_mutex> lock(settings_change_mutex);
    if (callback_id != audio_capture_id)
        return;
    SourceCaptionerSettings current_settings = settings;
    lock.unlock();

    emit source_capture_status_changed(std::make_shared<SourceCaptionerStatus>(
            SOURCE_CAPTIONER_STATUS_EVENT_AUDIO_CAPTURE_STATUS_CHANGE,
            current_settings,
            static_cast<audio_source_capture_status>(new_status)));
}

void SourceCaptioner::on_audio_data_callback(int callback_id, const uint8_t *data, size_t size) {
    std::lock_guard<recursive_mutex> lock(settings_change_mutex);
    if (callback_id == audio_capture_id && caption_engine)
        caption_engine->queue_audio_data(reinterpret_cast<const char *>(data), static_cast<unsigned int>(size));
}

void SourceCaptioner::reset_caption_state_unlocked() {
    results_history.clear();
    last_caption_text.clear();
    last_caption_final = false;
    last_caption_at = std::chrono::steady_clock::now();
    last_caption_cleared = true;
}

void SourceCaptioner::clear_output_timer_cb() {
    bool clear_file = false;
    bool clear_stream = false;
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        if (!settings.format_settings.caption_timeout_enabled || last_caption_cleared)
            return;

        const double seconds_since_caption =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                        std::chrono::steady_clock::now() - last_caption_at)
                        .count();
        if (seconds_since_caption <= settings.format_settings.caption_timeout_seconds)
            return;

        last_caption_cleared = true;
        clear_stream = settings.native_stream_output_enabled;
        clear_file = file_output.control != nullptr;
    }

    const auto now = std::chrono::steady_clock::now();
    CaptionOutput clearance(
            std::make_shared<OutputCaptionResult>(
                    CaptionResult(0, false, 0, "", "", now, now),
                    false),
            true);
    if (clear_stream)
        native_stream_output.enqueue(clearance);
    if (clear_file)
        file_output.enqueue(clearance);
    emit caption_result_received(nullptr, true);
}

void SourceCaptioner::store_result(shared_ptr<OutputCaptionResult> output_result) {
    if (!output_result)
        return;
    if (output_result->caption_result.final)
        results_history.push_back(output_result);

    if (results_history.size() > kHistoryEntriesHighWatermark) {
        for (std::size_t index = 0; index < kHistoryEntriesLowWatermark; ++index)
            results_history.pop_front();
    }
}

void SourceCaptioner::on_caption_text_callback(
        int id,
        const CaptionResult &caption_result,
        bool interrupted) {
    emit received_caption_result(id, caption_result, interrupted);
}

void SourceCaptioner::process_caption_result(
        int callback_id,
        const CaptionResult caption_result,
        bool interrupted) {
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        if (callback_id != audio_capture_id)
            return;
    }

    if (last_caption_text == caption_result.caption_text && last_caption_final == caption_result.final)
        return;
    last_caption_text = caption_result.caption_text;
    last_caption_final = caption_result.final;

    shared_ptr<OutputCaptionResult> output_result;
    shared_ptr<OutputCaptionResult> file_output_result;
    bool send_to_stream = false;
    {
        std::lock_guard<recursive_mutex> lock(settings_change_mutex);
        if (!caption_result_handler)
            return;

        output_result = caption_result_handler->prepare_caption_output(
                caption_result,
                true,
                settings.format_settings.caption_insert_newlines,
                settings.format_settings.caption_insert_punctuation,
                settings.format_settings.caption_line_length,
                settings.format_settings.caption_line_count,
                interrupted,
                results_history);
        if (!output_result)
            return;

        send_to_stream = settings.native_stream_output_enabled;

        if (settings.file_output_settings.isValidEnabled() && file_output.control) {
            file_output_result = caption_result_handler->prepare_caption_output(
                    caption_result,
                    true,
                    true,
                    settings.file_output_settings.insert_punctuation,
                    settings.file_output_settings.line_length,
                    settings.file_output_settings.line_count,
                    interrupted,
                    results_history);
        }
        store_result(output_result);
    }

    if (send_to_stream)
        native_stream_output.enqueue(CaptionOutput(output_result, false));
    if (file_output_result)
        file_output.enqueue(CaptionOutput(file_output_result, false));

    caption_was_output();
    emit caption_result_received(output_result, false);
}

void SourceCaptioner::caption_was_output() {
    last_caption_at = std::chrono::steady_clock::now();
    last_caption_cleared = false;
}

void SourceCaptioner::stream_started_event() {
    auto control = std::make_shared<CaptionOutputControl<int>>(0);
    native_stream_output.start(control, caption_output_writer_loop);
}

void SourceCaptioner::stream_stopped_event() {
    native_stream_output.clear();
}

SourceCaptioner::~SourceCaptioner() {
    stream_stopped_event();
    stop_caption_stream(false);
}
