#include <cstdlib>
#include <iostream>
#include <string>

#include "EnglishTermReplacements.h"

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}
}

int main() {
    require(restore_common_english_terms("обс твич дискорд") == "OBS Twitch Discord", "Known English terms must be restored");
    require(restore_common_english_terms("ютуб, стим! чат гпт") == "YouTube, Steam! ChatGPT", "Product terms must preserve punctuation");
    require(restore_common_english_terms("стример бот") == "Streamer.bot", "Multi-word terms must be restored");
    require(restore_common_english_terms("ОБС, ТвИч и обс") == "OBS, Twitch и OBS", "Replacement must be case-insensitive");
    require(restore_common_english_terms("обстановка и стимуляция") == "обстановка и стимуляция", "Substrings inside Russian words must not change");
    return 0;
}
