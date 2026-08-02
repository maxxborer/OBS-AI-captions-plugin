#include "CaptionResultHandler.h"

#include "EnglishTermReplacements.h"
#include "stringutils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>

namespace {
bool decode_utf8(
        const std::string &text,
        std::size_t &position,
        std::uint32_t &code_point) {
    if (position >= text.size())
        return false;

    const auto first = static_cast<unsigned char>(text[position]);
    std::size_t length;
    std::uint32_t minimum;
    if (first <= 0x7f) {
        length = 1;
        minimum = 0;
        code_point = first;
    } else if ((first & 0xe0U) == 0xc0U) {
        length = 2;
        minimum = 0x80;
        code_point = first & 0x1fU;
    } else if ((first & 0xf0U) == 0xe0U) {
        length = 3;
        minimum = 0x800;
        code_point = first & 0x0fU;
    } else if ((first & 0xf8U) == 0xf0U) {
        length = 4;
        minimum = 0x10000;
        code_point = first & 0x07U;
    } else {
        return false;
    }

    if (position + length > text.size())
        return false;
    for (std::size_t index = 1; index < length; ++index) {
        const auto continuation = static_cast<unsigned char>(text[position + index]);
        if ((continuation & 0xc0U) != 0x80U)
            return false;
        code_point = (code_point << 6U) | (continuation & 0x3fU);
    }
    if (code_point < minimum || code_point > 0x10ffffU ||
        (code_point >= 0xd800U && code_point <= 0xdfffU)) {
        return false;
    }
    position += length;
    return true;
}

bool is_supported_caption_code_point(std::uint32_t code_point) {
    if ((code_point >= 0x20U && code_point <= 0x7eU) ||
        code_point == '\n' || code_point == '\r' || code_point == '\t') {
        return true;
    }
    if ((code_point >= 0x0410U && code_point <= 0x044fU) ||
        code_point == 0x0401U || code_point == 0x0451U) {
        return true;
    }

    switch (code_point) {
        case 0x00a0U: // non-breaking space
        case 0x00abU: // left guillemet
        case 0x00bbU: // right guillemet
        case 0x2010U:
        case 0x2011U:
        case 0x2012U:
        case 0x2013U:
        case 0x2014U:
        case 0x2015U:
        case 0x2018U:
        case 0x2019U:
        case 0x201cU:
        case 0x201dU:
        case 0x2026U: // ellipsis
        case 0x202fU: // narrow non-breaking space
        case 0x20bdU: // ruble sign
        case 0x2116U: // numero sign
            return true;
        default:
            return false;
    }
}

bool contains_only_russian_english_characters(const std::string &text) {
    std::size_t position = 0;
    while (position < text.size()) {
        std::uint32_t code_point = 0;
        if (!decode_utf8(text, position, code_point) ||
            !is_supported_caption_code_point(code_point)) {
            return false;
        }
    }
    return true;
}

void uppercase_supported_at(std::string &text, std::size_t position) {
    if (position >= text.size())
        return;

    const std::size_t start = position;
    std::uint32_t code_point = 0;
    if (!decode_utf8(text, position, code_point))
        return;

    if (code_point >= 'a' && code_point <= 'z') {
        text[start] = static_cast<char>(std::toupper(static_cast<unsigned char>(code_point)));
        return;
    }

    if (code_point >= 0x0430U && code_point <= 0x044fU)
        code_point -= 0x20U;
    else if (code_point == 0x0451U)
        code_point = 0x0401U;
    else
        return;

    // Every Russian Cyrillic code point above uses exactly two UTF-8 bytes.
    text[start] = static_cast<char>(0xc0U | (code_point >> 6U));
    text[start + 1] = static_cast<char>(0x80U | (code_point & 0x3fU));
}
}

std::shared_ptr<OutputCaptionResult> CaptionResultHandler::prepare_caption_output(
        const CaptionResult &caption_result,
        bool fillup_with_previous,
        bool insert_newlines,
        bool punctuation,
        uint line_length,
        uint targeted_line_count,
        bool interrupted,
        const std::deque<std::shared_ptr<OutputCaptionResult>> &result_history) {
    auto output = std::make_shared<OutputCaptionResult>(caption_result, interrupted);
    output->clean_caption_text = restore_common_english_terms(
            text_replacer.replace(caption_result.caption_text));
    lstrip(output->clean_caption_text);
    if (!contains_only_russian_english_characters(output->clean_caption_text))
        return nullptr;

    std::string text;
    if (fillup_with_previous) {
        const std::size_t maximum_length =
                static_cast<std::size_t>(targeted_line_count) * line_length;
        std::size_t assembled_size = output->clean_caption_text.size();
        std::size_t history_start = result_history.size();
        std::chrono::steady_clock::time_point now;
        if (settings.caption_timeout_enabled)
            now = std::chrono::steady_clock::now();
        for (auto item = result_history.rbegin();
             assembled_size < maximum_length && item != result_history.rend();
             ++item) {
            if (!*item || !(*item)->caption_result.final)
                break;
            if (settings.caption_timeout_enabled) {
                const double age_seconds =
                        std::chrono::duration_cast<std::chrono::duration<double>>(
                                now - (*item)->caption_result.received_at)
                                .count();
                if (age_seconds > settings.caption_timeout_seconds)
                    break;
            }

            --history_start;
            assembled_size += (*item)->clean_caption_text.size() + (punctuation ? 2 : 1);
        }

        text.reserve(assembled_size);
        for (std::size_t index = history_start; index < result_history.size(); ++index) {
            const std::size_t segment_start = text.size();
            text.append(result_history[index]->clean_caption_text);
            if (punctuation)
                uppercase_supported_at(text, segment_start);
            text.append(punctuation ? ". " : " ");
        }
        const std::size_t current_start = text.size();
        text.append(output->clean_caption_text);
        if (punctuation)
            uppercase_supported_at(text, current_start);
    } else {
        text = output->clean_caption_text;
    }

    std::vector<std::string> all_lines;
    split_into_lines(all_lines, text, line_length);
    const std::size_t line_count = std::min<std::size_t>(targeted_line_count, all_lines.size());
    output->output_lines.assign(all_lines.end() - static_cast<std::ptrdiff_t>(line_count), all_lines.end());
    join_strings(output->output_lines, insert_newlines ? "\n" : " ", output->output_line);
    return output;
}
