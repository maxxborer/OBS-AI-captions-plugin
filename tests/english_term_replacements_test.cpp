#include <cassert>
#include <string>

#include "EnglishTermReplacements.h"

int main() {
    assert(restore_common_english_terms("обс твич дискорд") == "OBS Twitch Discord");
    assert(restore_common_english_terms("ютуб, стим! чат гпт") == "YouTube, Steam! ChatGPT");
    assert(restore_common_english_terms("стример бот") == "Streamer.bot");
    assert(restore_common_english_terms("ОБС, ТвИч и обс") == "OBS, Twitch и OBS");
    assert(restore_common_english_terms("обстановка и стимуляция") == "обстановка и стимуляция");
    return 0;
}
