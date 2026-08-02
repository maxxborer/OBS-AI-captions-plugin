/******************************************************************************
Copyright (C) 2019 by <rat.with.a.compiler@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef OBS_STUDIO_SOURCECAPTIONER_H
#define OBS_STUDIO_SOURCECAPTIONER_H

#include <CaptionEngine.h>

#include "CaptionResultHandler.h"
#include "OutputAudioCaptureSession.h"
#include "SourceAudioCaptureSession.h"

#include <QObject>
#include <QTimer>

#include <chrono>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

using namespace std;
Q_DECLARE_METATYPE(std::string)
Q_DECLARE_METATYPE(shared_ptr<OutputCaptionResult>)
Q_DECLARE_METATYPE(CaptionResult)

enum CaptionSourceMuteType {
    CAPTION_SOURCE_MUTE_TYPE_FROM_OWN_SOURCE,
    CAPTION_SOURCE_MUTE_TYPE_ALWAYS_CAPTION,
    CAPTION_SOURCE_MUTE_TYPE_USE_OTHER_MUTE_SOURCE,
};

struct CaptionSourceSettings {
    string caption_source_name;
    CaptionSourceMuteType mute_when = CAPTION_SOURCE_MUTE_TYPE_FROM_OWN_SOURCE;
    string mute_source_name;

    bool operator==(const CaptionSourceSettings &rhs) const {
        return caption_source_name == rhs.caption_source_name &&
               mute_when == rhs.mute_when &&
               mute_source_name == rhs.mute_source_name;
    }

    bool operator!=(const CaptionSourceSettings &rhs) const {
        return !(rhs == *this);
    }

};

struct FileOutputSettings {
    bool enabled = false;
    uint line_length = 32;
    uint line_count = 3;
    bool insert_punctuation = true;
    string output_folder;
    string filename_custom = "captions.txt";

    bool isValid() const {
        return !output_folder.empty() && !filename_custom.empty() && line_count && line_length;
    }

    bool isValidEnabled() const {
        return enabled && isValid();
    }

    friend bool operator==(const FileOutputSettings &lhs, const FileOutputSettings &rhs) {
        return lhs.enabled == rhs.enabled &&
               lhs.line_length == rhs.line_length &&
               lhs.line_count == rhs.line_count &&
               lhs.insert_punctuation == rhs.insert_punctuation &&
               lhs.output_folder == rhs.output_folder &&
               lhs.filename_custom == rhs.filename_custom;
    }

    friend bool operator!=(const FileOutputSettings &lhs, const FileOutputSettings &rhs) {
        return !(lhs == rhs);
    }
};

struct SourceCaptionerSettings {
    bool native_stream_output_enabled = false;
    FileOutputSettings file_output_settings;
    CaptionSourceSettings caption_source_settings;
    CaptionFormatSettings format_settings;

    SourceCaptionerSettings(
            bool native_stream_output_enabled,
            const FileOutputSettings &file_output_settings,
            const CaptionSourceSettings &caption_source_settings,
            const CaptionFormatSettings &format_settings)
            : native_stream_output_enabled(native_stream_output_enabled),
              file_output_settings(file_output_settings),
              caption_source_settings(caption_source_settings),
              format_settings(format_settings) {}

    bool operator==(const SourceCaptionerSettings &rhs) const {
        return native_stream_output_enabled == rhs.native_stream_output_enabled &&
               file_output_settings == rhs.file_output_settings &&
               caption_source_settings == rhs.caption_source_settings &&
               format_settings == rhs.format_settings;
    }

    bool operator!=(const SourceCaptionerSettings &rhs) const {
        return !(rhs == *this);
    }

};

struct CaptionOutput {
    shared_ptr<OutputCaptionResult> output_result;
    bool is_clearance = false;

    CaptionOutput(shared_ptr<OutputCaptionResult> output_result, bool is_clearance)
            : output_result(std::move(output_result)), is_clearance(is_clearance) {}
    CaptionOutput() = default;
};

class CaptionOutputQueue {
public:
    void enqueue(CaptionOutput output) {
        bool wake_consumer;
        {
            std::lock_guard<std::mutex> lock(mutex);
            wake_consumer = queue.empty();
            // Only the newest pending caption matters for a live overlay/file.
            // Coalescing prevents an output delay from growing memory without bound.
            if (queue.empty())
                queue.push_back(std::move(output));
            else
                queue.back() = std::move(output);
        }
        if (wake_consumer)
            available.notify_one();
    }

    void wait_dequeue(CaptionOutput &output) {
        std::unique_lock<std::mutex> lock(mutex);
        available.wait(lock, [this] { return !queue.empty(); });
        output = std::move(queue.front());
        queue.pop_front();
    }

private:
    std::mutex mutex;
    std::condition_variable available;
    std::deque<CaptionOutput> queue;
};

template<typename T>
struct CaptionOutputControl {
    CaptionOutputQueue caption_queue;
    std::atomic_bool stop = false;
    T arg;

    explicit CaptionOutputControl(T arg) : arg(std::move(arg)) {}
    void stop_soon() {
        if (stop.exchange(true))
            return;
        caption_queue.enqueue(CaptionOutput());
        stop_changed.notify_all();
    }

    template<typename Rep, typename Period>
    bool wait_for_stop(const std::chrono::duration<Rep, Period> &duration) {
        std::unique_lock<std::mutex> lock(stop_mutex);
        return stop_changed.wait_for(lock, duration, [this] { return stop.load(); });
    }

private:
    std::mutex stop_mutex;
    std::condition_variable stop_changed;
};

template<typename T>
struct OutputWriter {
    std::mutex control_change_mutex;
    std::shared_ptr<CaptionOutputControl<T>> control;
    std::thread worker;

    void clear() {
        std::thread worker_to_join;
        {
            std::lock_guard<std::mutex> lock(control_change_mutex);
            if (control)
                control->stop_soon();
            control.reset();
            if (worker.joinable())
                worker_to_join = std::move(worker);
        }
        if (worker_to_join.joinable())
            worker_to_join.join();
    }

    template<typename WorkerFunction>
    void start(
            const std::shared_ptr<CaptionOutputControl<T>> &new_control,
            WorkerFunction worker_function) {
        clear();
        std::thread new_worker(std::move(worker_function), new_control);
        std::lock_guard<std::mutex> lock(control_change_mutex);
        control = new_control;
        worker = std::move(new_worker);
    }

    bool enqueue(const CaptionOutput &output) {
        std::lock_guard<std::mutex> lock(control_change_mutex);
        if (!control || control->stop)
            return false;
        control->caption_queue.enqueue(output);
        return true;
    }

    ~OutputWriter() {
        clear();
    }
};

enum SourceCaptionerStatusEvent {
    SOURCE_CAPTIONER_STATUS_EVENT_STOPPED,
    SOURCE_CAPTIONER_STATUS_EVENT_STARTED_OK,
    SOURCE_CAPTIONER_STATUS_EVENT_STARTED_ERROR,
    SOURCE_CAPTIONER_STATUS_EVENT_NEW_SETTINGS_STOPPED,
    SOURCE_CAPTIONER_STATUS_EVENT_AUDIO_CAPTURE_STATUS_CHANGE,
};

struct SourceCaptionerStatus {
    SourceCaptionerStatusEvent event_type;
    SourceCaptionerSettings settings;
    audio_source_capture_status audio_capture_status;

    SourceCaptionerStatus(
            SourceCaptionerStatusEvent event_type,
            const SourceCaptionerSettings &settings,
            audio_source_capture_status audio_capture_status)
            : event_type(event_type),
              settings(settings),
              audio_capture_status(audio_capture_status) {}
};

Q_DECLARE_METATYPE(std::shared_ptr<SourceCaptionerStatus>)

class SourceCaptioner : public QObject {
Q_OBJECT

    std::unique_ptr<SourceAudioCaptureSession> source_audio_capture_session;
    std::unique_ptr<OutputAudioCaptureSession> output_audio_capture_session;
    std::unique_ptr<CaptionEngine> caption_engine;

    SourceCaptionerSettings settings;
    std::unique_ptr<CaptionResultHandler> caption_result_handler;
    std::recursive_mutex settings_change_mutex;

    std::chrono::steady_clock::time_point last_caption_at;
    bool last_caption_cleared;
    QTimer timer;
    std::deque<std::shared_ptr<OutputCaptionResult>> results_history;

    OutputWriter<int> native_stream_output;
    OutputWriter<FileOutputSettings> file_output;
    int audio_capture_id = 0;
    string last_caption_text;
    bool last_caption_final = false;

    void caption_was_output();
    void store_result(shared_ptr<OutputCaptionResult> output_result);
    void on_audio_data_callback(int id, const uint8_t *data, size_t size);
    void on_audio_capture_status_change_callback(int id, audio_source_capture_status status);
    void on_caption_text_callback(
            int id,
            const CaptionResult &caption_result,
            bool interrupted);
    bool _start_caption_stream();
    void reset_caption_state_unlocked();

private slots:
    void clear_output_timer_cb();
    void process_caption_result(
            int id,
            const CaptionResult caption_result,
            bool interrupted);
    void process_audio_capture_status_change(int id, int new_status);

signals:
    void received_caption_result(
            int id,
            const CaptionResult caption_result,
            bool interrupted);
    void caption_result_received(
            shared_ptr<OutputCaptionResult> caption,
            bool cleared);
    void audio_capture_status_changed(int id, int new_status);
    void source_capture_status_changed(shared_ptr<SourceCaptionerStatus> status);

public:
    SourceCaptioner(
            const SourceCaptionerSettings &settings,
            bool start);
    ~SourceCaptioner() override;

    bool set_settings(const SourceCaptionerSettings &new_settings);
    bool start_caption_stream(const SourceCaptionerSettings &new_settings);
    void stop_caption_stream(bool send_signal = true);
    void stream_started_event();
    void stream_stopped_event();
};

#endif // OBS_STUDIO_SOURCECAPTIONER_H
