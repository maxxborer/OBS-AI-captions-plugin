/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "SherpaTOneCaptionEngine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <sherpa-onnx/c-api/c-api.h>

namespace {
constexpr unsigned int kSampleRate = 8000;
constexpr unsigned int kFeatureDimension = 80;
constexpr float kEndpointTrailingSilenceSeconds = 1.2F;
constexpr float kEndpointMaximumUtteranceSeconds = 20.0F;

std::string checked_model_file(const std::string &directory, const char *filename) {
    const std::filesystem::path path = std::filesystem::path(directory) / filename;
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("Local Russian model is missing '" + path.string() + "'. Run Install-AICaptionPlugin.ps1 again.");
    return path.string();
}
}

SherpaTOneCaptionEngine::SherpaTOneCaptionEngine(const LocalCaptionEngineSettings &settings)
        : num_threads(std::max(1U, settings.num_threads)),
          max_pending_samples(kSampleRate * std::max(1U, settings.max_pending_audio_ms) / 1000U),
          model_directory(settings.model_directory),
          audio_ring(max_pending_samples),
          first_caption_at(std::chrono::steady_clock::now()) {
    if (model_directory.empty())
        throw std::runtime_error("Local Russian model directory is unavailable. Run Install-AICaptionPlugin.ps1 again.");

    const std::string model = checked_model_file(model_directory, "model.onnx");
    const std::string tokens = checked_model_file(model_directory, "tokens.txt");

    SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = static_cast<int32_t>(kSampleRate);
    config.feat_config.feature_dim = static_cast<int32_t>(kFeatureDimension);
    config.model_config.t_one_ctc.model = model.c_str();
    config.model_config.tokens = tokens.c_str();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = static_cast<int32_t>(num_threads);
    config.decoding_method = "greedy_search";
    config.enable_endpoint = 1;
    config.rule1_min_trailing_silence = 2.4F;
    config.rule2_min_trailing_silence = kEndpointTrailingSilenceSeconds;
    config.rule3_min_utterance_length = kEndpointMaximumUtteranceSeconds;

    recognizer = SherpaOnnxCreateOnlineRecognizer(&config);
    if (!recognizer)
        throw std::runtime_error("Unable to start the local Russian recognition model.");

    stream = SherpaOnnxCreateOnlineStream(recognizer);
    if (!stream) {
        SherpaOnnxDestroyOnlineRecognizer(recognizer);
        recognizer = nullptr;
        throw std::runtime_error("Unable to create a local recognition stream.");
    }

    worker = std::thread(&SherpaTOneCaptionEngine::worker_loop, this);
}

bool SherpaTOneCaptionEngine::queue_audio_data(const char *data, unsigned int data_size) {
    if (!data || data_size < sizeof(std::int16_t) || stopping.load())
        return false;

    const std::size_t sample_count = data_size / sizeof(std::int16_t);
    bool wake_worker = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        wake_worker = audio_ring_size == 0;
        if (sample_count >= max_pending_samples) {
            const char *latest_samples =
                    data + (sample_count - max_pending_samples) * sizeof(std::int16_t);
            std::memcpy(
                    audio_ring.data(),
                    latest_samples,
                    max_pending_samples * sizeof(std::int16_t));
            audio_ring_head = 0;
            audio_ring_size = max_pending_samples;
        } else {
            const std::size_t overflow =
                    audio_ring_size + sample_count > max_pending_samples
                            ? audio_ring_size + sample_count - max_pending_samples
                            : 0;
            audio_ring_head = (audio_ring_head + overflow) % max_pending_samples;
            audio_ring_size -= overflow;

            const std::size_t tail =
                    (audio_ring_head + audio_ring_size) % max_pending_samples;
            const std::size_t first_copy =
                    std::min(sample_count, max_pending_samples - tail);
            std::memcpy(
                    audio_ring.data() + tail,
                    data,
                    first_copy * sizeof(std::int16_t));
            if (first_copy < sample_count) {
                std::memcpy(
                        audio_ring.data(),
                        data + first_copy * sizeof(std::int16_t),
                        (sample_count - first_copy) * sizeof(std::int16_t));
            }
            audio_ring_size += sample_count;
        }
    }
    if (wake_worker)
        queue_cv.notify_one();
    return true;
}

unsigned int SherpaTOneCaptionEngine::preferred_sample_rate() const {
    return kSampleRate;
}

SherpaTOneCaptionEngine::~SherpaTOneCaptionEngine() {
    stopping.store(true);
    queue_cv.notify_all();
    if (worker.joinable())
        worker.join();

    if (stream)
        SherpaOnnxDestroyOnlineStream(stream);
    if (recognizer)
        SherpaOnnxDestroyOnlineRecognizer(recognizer);
}

void SherpaTOneCaptionEngine::worker_loop() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    std::vector<std::int16_t> audio;
    std::vector<float> samples;
    audio.reserve(max_pending_samples);
    samples.reserve(max_pending_samples);
    while (!stopping.load()) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] { return stopping.load() || audio_ring_size > 0; });
            if (stopping.load())
                break;

            audio.resize(audio_ring_size);
            const std::size_t first_copy =
                    std::min(audio_ring_size, max_pending_samples - audio_ring_head);
            std::memcpy(
                    audio.data(),
                    audio_ring.data() + audio_ring_head,
                    first_copy * sizeof(std::int16_t));
            if (first_copy < audio_ring_size) {
                std::memcpy(
                        audio.data() + first_copy,
                        audio_ring.data(),
                        (audio_ring_size - first_copy) * sizeof(std::int16_t));
            }
            audio_ring_head = (audio_ring_head + audio_ring_size) % max_pending_samples;
            audio_ring_size = 0;
        }
        decode_audio(audio, samples);
    }
}

void SherpaTOneCaptionEngine::decode_audio(
        const std::vector<std::int16_t> &audio,
        std::vector<float> &samples) {
    if (audio.empty())
        return;

    samples.resize(audio.size());
    std::transform(audio.begin(), audio.end(), samples.begin(), [](std::int16_t sample) {
        return static_cast<float>(sample) / 32768.0F;
    });

    SherpaOnnxOnlineStreamAcceptWaveform(
            stream,
            static_cast<int32_t>(kSampleRate),
            samples.data(),
            static_cast<int32_t>(samples.size()));

    bool decoded = false;
    while (SherpaOnnxIsOnlineStreamReady(recognizer, stream)) {
        SherpaOnnxDecodeOnlineStream(recognizer, stream);
        decoded = true;
    }

    if (decoded)
        publish_current_result(false);

    if (SherpaOnnxOnlineStreamIsEndpoint(recognizer, stream)) {
        publish_current_result(true);
        reset_utterance();
    }
}

void SherpaTOneCaptionEngine::publish_current_result(bool final) {
    const SherpaOnnxOnlineRecognizerResult *result = SherpaOnnxGetOnlineStreamResult(recognizer, stream);
    if (!result)
        return;

    const std::string text = result->text ? result->text : "";
    SherpaOnnxDestroyOnlineRecognizerResult(result);

    if (text.empty())
        return;
    if (!final && text == last_caption_text)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (last_caption_text.empty())
        first_caption_at = now;

    CaptionResult caption_result(
            caption_index,
            final,
            final ? 1.0 : 0.7,
            text,
            "local-sherpa-t-one",
            first_caption_at,
            now);
    last_caption_text = text;
    std::lock_guard<std::recursive_mutex> lock(on_caption_cb_handle.mutex);
    if (on_caption_cb_handle.callback_fn)
        on_caption_cb_handle.callback_fn(caption_result, false);
}

void SherpaTOneCaptionEngine::reset_utterance() {
    SherpaOnnxOnlineStreamReset(recognizer, stream);
    last_caption_text.clear();
    first_caption_at = std::chrono::steady_clock::now();
    ++caption_index;
}
