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
#include <bcrypt.h>
#endif

#include "SherpaTOneCaptionEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <sherpa-onnx/c-api/c-api.h>

namespace {
constexpr unsigned int kTOneSampleRate = 8000;
constexpr unsigned int kNemotronSampleRate = 16000;
constexpr unsigned int kFeatureDimension = 80;
constexpr float kEndpointTrailingSilenceSeconds = 1.2F;
constexpr float kEndpointMaximumUtteranceSeconds = 20.0F;
constexpr const char *kModelSha256 =
        "5ded080e2a6c86ecc11bcb0902d77524eb3e8b0844cb0c0754347f5aafb4dabc";
constexpr const char *kTokensSha256 =
        "27f7b3ba2096c572375fba1a6b29af1f80d86e08a329940612908112695f97e0";
constexpr const char *kNemotronEncoderSha256 =
        "012e9321373af99021415e0b0eb3ec827b4be3153be6f30d9b448fe65e896e68";
constexpr const char *kNemotronDecoderSha256 =
        "19f9c98fc6d0a2c33a65a43b36fdb2e914c26c0aa9764be3aebc502a1e982fb0";
constexpr const char *kNemotronJoinerSha256 =
        "4101c7c679a0bc30483794b27a059e34e79232aa2068d78d51231a22c8b0d7ce";
constexpr const char *kNemotronTokensSha256 =
        "729cc103155bafa785f9cd45746cd41cabe97eab7182fc04d594129587958f8a";

class LockedModelFile {
public:
    LockedModelFile(
            const std::filesystem::path &directory,
            const char *filename,
            const char *expected_sha256)
            : path(directory / filename), expected_sha256(expected_sha256) {
        handle = CreateFileW(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Local Russian model is missing '" + path.string() + "'. Run Install-AICaptionPlugin.ps1 again.");

        FILE_ATTRIBUTE_TAG_INFO attributes{};
        if (!GetFileInformationByHandleEx(
                    handle,
                    FileAttributeTagInfo,
                    &attributes,
                    sizeof(attributes)) ||
            (attributes.FileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Local model files must be regular files, not links or directories.");
        }
        try {
            verify();
            if (!GetFileInformationByHandle(handle, &verified_identity))
                throw std::runtime_error("Unable to record the verified local model identity.");
        } catch (...) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw;
        }
    }

    LockedModelFile(const LockedModelFile &) = delete;
    LockedModelFile &operator=(const LockedModelFile &) = delete;

    ~LockedModelFile() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    std::string filename() const {
        return path.string();
    }

    void verify() const {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<unsigned char> hash_object;
        std::array<unsigned char, 32> digest{};
        try {
            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
                throw std::runtime_error("Unable to initialize SHA-256 model verification.");
            }
            DWORD object_size = 0;
            DWORD result_size = 0;
            if (!BCRYPT_SUCCESS(BCryptGetProperty(
                        algorithm,
                        BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_size),
                        sizeof(object_size),
                        &result_size,
                        0))) {
                throw std::runtime_error("Unable to prepare SHA-256 model verification.");
            }
            hash_object.resize(object_size);
            if (!BCRYPT_SUCCESS(BCryptCreateHash(
                        algorithm,
                        &hash,
                        hash_object.data(),
                        static_cast<ULONG>(hash_object.size()),
                        nullptr,
                        0,
                        0))) {
                throw std::runtime_error("Unable to start SHA-256 model verification.");
            }

            LARGE_INTEGER beginning{};
            if (!SetFilePointerEx(handle, beginning, nullptr, FILE_BEGIN))
                throw std::runtime_error("Unable to read the local model for verification.");

            std::vector<unsigned char> buffer(1024 * 1024);
            for (;;) {
                DWORD bytes_read = 0;
                if (!ReadFile(
                            handle,
                            buffer.data(),
                            static_cast<DWORD>(buffer.size()),
                            &bytes_read,
                            nullptr)) {
                    throw std::runtime_error("Unable to read the local model for verification.");
                }
                if (bytes_read == 0)
                    break;
                if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), bytes_read, 0)))
                    throw std::runtime_error("Unable to calculate the local model checksum.");
            }
            if (!BCRYPT_SUCCESS(BCryptFinishHash(
                        hash,
                        digest.data(),
                        static_cast<ULONG>(digest.size()),
                        0))) {
                throw std::runtime_error("Unable to finish local model verification.");
            }
        } catch (...) {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
            throw;
        }
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);

        static constexpr char digits[] = "0123456789abcdef";
        std::string actual;
        actual.resize(digest.size() * 2);
        for (std::size_t index = 0; index < digest.size(); ++index) {
            actual[index * 2] = digits[digest[index] >> 4U];
            actual[index * 2 + 1] = digits[digest[index] & 0x0fU];
        }
        if (expected_sha256 && actual != expected_sha256) {
            throw std::runtime_error(
                    "Local Russian model integrity check failed for '" +
                    path.string() + "'. Run Install-AICaptionPlugin.ps1 again.");
        }
    }

    void verify_if_changed() {
        BY_HANDLE_FILE_INFORMATION current{};
        if (!GetFileInformationByHandle(handle, &current))
            throw std::runtime_error("Unable to re-check the local model identity.");
        if (current.dwVolumeSerialNumber == verified_identity.dwVolumeSerialNumber &&
            current.nFileIndexHigh == verified_identity.nFileIndexHigh &&
            current.nFileIndexLow == verified_identity.nFileIndexLow &&
            current.nFileSizeHigh == verified_identity.nFileSizeHigh &&
            current.nFileSizeLow == verified_identity.nFileSizeLow &&
            current.ftLastWriteTime.dwHighDateTime ==
                    verified_identity.ftLastWriteTime.dwHighDateTime &&
            current.ftLastWriteTime.dwLowDateTime ==
                    verified_identity.ftLastWriteTime.dwLowDateTime) {
            return;
        }
        verify();
        verified_identity = current;
    }

private:
    std::filesystem::path path;
    const char *expected_sha256;
    HANDLE handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION verified_identity{};
};

void destroy_recognizer(const SherpaOnnxOnlineRecognizer *&recognizer) {
    if (recognizer) {
        SherpaOnnxDestroyOnlineRecognizer(recognizer);
        recognizer = nullptr;
    }
}
}

SherpaTOneCaptionEngine::SherpaTOneCaptionEngine(const LocalCaptionEngineSettings &settings)
        : num_threads(std::max(1U, settings.num_threads)),
          sample_rate(settings.model == LocalCaptionModel::Nemotron560ms
                              ? kNemotronSampleRate
                              : kTOneSampleRate),
          max_pending_samples(sample_rate * std::max(1U, settings.max_pending_audio_ms) / 1000U),
          model_directory(settings.model_directory),
          model(settings.model),
          hotwords(settings.hotwords),
          engine_name(model == LocalCaptionModel::Nemotron560ms
                              ? "local-sherpa-nemotron-3.5-560ms"
                              : "local-sherpa-t-one"),
          audio_ring(max_pending_samples),
          first_caption_at(std::chrono::steady_clock::now()) {
    if (model_directory.empty())
        throw std::runtime_error("Local Russian model directory is unavailable. Run Install-AICaptionPlugin.ps1 again.");

    const bool use_nemotron = model == LocalCaptionModel::Nemotron560ms;
    LockedModelFile model_file(
            model_directory,
            use_nemotron ? "encoder.int8.onnx" : "model.onnx",
            use_nemotron ? kNemotronEncoderSha256 : kModelSha256);
    LockedModelFile tokens_file(
            model_directory,
            "tokens.txt",
            use_nemotron ? kNemotronTokensSha256 : kTokensSha256);
    const std::string primary_model = model_file.filename();
    const std::string tokens = tokens_file.filename();
    std::unique_ptr<LockedModelFile> decoder_file;
    std::unique_ptr<LockedModelFile> joiner_file;
    std::string decoder;
    std::string joiner;
    if (use_nemotron) {
        decoder_file = std::make_unique<LockedModelFile>(
                model_directory, "decoder.int8.onnx", kNemotronDecoderSha256);
        joiner_file = std::make_unique<LockedModelFile>(
                model_directory, "joiner.int8.onnx", kNemotronJoinerSha256);
        decoder = decoder_file->filename();
        joiner = joiner_file->filename();
    }

    SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = static_cast<int32_t>(sample_rate);
    config.feat_config.feature_dim = static_cast<int32_t>(kFeatureDimension);
    if (use_nemotron) {
        config.model_config.transducer.encoder = primary_model.c_str();
        config.model_config.transducer.decoder = decoder.c_str();
        config.model_config.transducer.joiner = joiner.c_str();
    } else {
        config.model_config.t_one_ctc.model = primary_model.c_str();
    }
    config.model_config.tokens = tokens.c_str();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = static_cast<int32_t>(num_threads);
    config.decoding_method = !use_nemotron && !hotwords.empty()
                                     ? "modified_beam_search"
                                     : "greedy_search";
    if (!use_nemotron && !hotwords.empty()) {
        config.max_active_paths = 4;
        config.hotwords_buf = hotwords.c_str();
        config.hotwords_buf_size = static_cast<int32_t>(hotwords.size());
        config.hotwords_score = 1.5F;
    }
    config.enable_endpoint = 1;
    config.rule1_min_trailing_silence = 2.4F;
    config.rule2_min_trailing_silence = kEndpointTrailingSilenceSeconds;
    config.rule3_min_utterance_length = kEndpointMaximumUtteranceSeconds;

    recognizer = SherpaOnnxCreateOnlineRecognizer(&config);
    if (!recognizer)
        throw std::runtime_error("Unable to start the local Russian recognition model.");

    try {
        model_file.verify_if_changed();
        tokens_file.verify_if_changed();
        if (decoder_file)
            decoder_file->verify_if_changed();
        if (joiner_file)
            joiner_file->verify_if_changed();
    } catch (...) {
        destroy_recognizer(recognizer);
        throw;
    }

    stream = SherpaOnnxCreateOnlineStream(recognizer);
    if (!stream) {
        destroy_recognizer(recognizer);
        throw std::runtime_error("Unable to create a local recognition stream.");
    }
    if (use_nemotron)
        SherpaOnnxOnlineStreamSetOption(stream, "language", "ru-RU");

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
    return sample_rate;
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
            static_cast<int32_t>(sample_rate),
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
            engine_name,
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
