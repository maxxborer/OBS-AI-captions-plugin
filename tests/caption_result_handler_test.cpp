#include "CaptionResultHandler.h"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

CaptionResult caption(const char *text, bool final, std::chrono::steady_clock::time_point received_at) {
    return CaptionResult(0, final, final ? 1.0 : 0.7, text, "", received_at, received_at);
}
}

int main() {
    CaptionFormatSettings settings{96, 2, false, true, false, 3.0};
    CaptionResultHandler handler(settings);
    std::deque<std::shared_ptr<OutputCaptionResult>> history;
    const auto now = std::chrono::steady_clock::now();

    auto first = handler.prepare_caption_output(
            caption("первое обс", true, now),
            true,
            false,
            true,
            96,
            2,
            false,
            history);
    require(first && first->output_line == "Первое OBS", "First caption must be normalized");
    history.push_back(first);

    auto current = handler.prepare_caption_output(
            caption("второе твич", false, now),
            true,
            false,
            true,
            96,
            2,
            false,
            history);
    require(current && current->output_line == "Первое OBS. Второе Twitch", "History must be merged into the current caption");
    require(current->clean_caption_text == "второе Twitch", "Clean caption text must preserve the current phrase");

    auto without_history = handler.prepare_caption_output(
            caption("третье дискорд", false, now),
            false,
            false,
            true,
            96,
            2,
            false,
            history);
    require(without_history && without_history->output_line == "третье Discord", "History-free output must restore English terms");

    auto bilingual = handler.prepare_caption_output(
            caption("русский English 42 — только два алфавита", false, now),
            false,
            false,
            false,
            96,
            2,
            false,
            history);
    require(static_cast<bool>(bilingual), "Russian and English text must be accepted");
    require(bilingual->output_line == "русский English 42 — только два алфавита", "Bilingual text must be preserved");

    auto unsupported_script = handler.prepare_caption_output(
            caption("русский 中文", false, now),
            false,
            false,
            false,
            96,
            2,
            false,
            history);
    require(!unsupported_script, "Unsupported scripts must be rejected");

    auto unsupported_cyrillic = handler.prepare_caption_output(
            caption("українська", false, now),
            false,
            false,
            false,
            96,
            2,
            false,
            history);
    require(!unsupported_cyrillic, "Unsupported Cyrillic languages must be rejected");
    return 0;
}
