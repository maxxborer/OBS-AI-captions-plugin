#ifndef AI_CAPTION_PLUGIN_CAPTION_RESULT_H
#define AI_CAPTION_PLUGIN_CAPTION_RESULT_H

#include <chrono>
#include <string>
#include <utility>

struct CaptionResult {
    int index = 0;
    bool final = false;
    double stability = 0.0;
    std::string caption_text;
    std::string raw_message;
    std::chrono::steady_clock::time_point first_received_at;
    std::chrono::steady_clock::time_point received_at;

    CaptionResult() = default;

    CaptionResult(
            int index,
            bool final,
            double stability,
            std::string caption_text,
            std::string raw_message,
            std::chrono::steady_clock::time_point first_received_at,
            std::chrono::steady_clock::time_point received_at)
            : index(index),
              final(final),
              stability(stability),
              caption_text(std::move(caption_text)),
              raw_message(std::move(raw_message)),
              first_received_at(first_received_at),
              received_at(received_at) {}
};

#endif // AI_CAPTION_PLUGIN_CAPTION_RESULT_H
