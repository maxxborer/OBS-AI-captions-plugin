#include "CaptionResultHandler.h"

#include <cassert>
#include <chrono>
#include <deque>
#include <memory>

namespace {
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
    assert(first->output_line == "Первое OBS");
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
    assert(current->output_line == "Первое OBS. Второе Twitch");
    assert(current->clean_caption_text == "второе Twitch");

    auto without_history = handler.prepare_caption_output(
            caption("третье дискорд", false, now),
            false,
            false,
            true,
            96,
            2,
            false,
            history);
    assert(without_history->output_line == "третье Discord");

    auto bilingual = handler.prepare_caption_output(
            caption("русский English 42 — только два алфавита", false, now),
            false,
            false,
            false,
            96,
            2,
            false,
            history);
    assert(bilingual);
    assert(bilingual->output_line == "русский English 42 — только два алфавита");

    auto unsupported_script = handler.prepare_caption_output(
            caption("русский 中文", false, now),
            false,
            false,
            false,
            96,
            2,
            false,
            history);
    assert(!unsupported_script);

    auto unsupported_cyrillic = handler.prepare_caption_output(
            caption("українська", false, now),
            false,
            false,
            false,
            96,
            2,
            false,
            history);
    assert(!unsupported_cyrillic);
    return 0;
}
