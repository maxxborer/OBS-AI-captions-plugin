#include <cassert>
#include <string>

#include "EnglishTermReplacements.h"

int main() {
    Replacer replacer(default_english_term_replacements(), false);

    assert(replacer.replace("обс твич дискорд") == "OBS Twitch Discord");
    assert(replacer.replace("ютуб, стим! чат гпт") == "YouTube, Steam! ChatGPT");
    assert(replacer.replace("стример бот") == "Streamer.bot");
    assert(replacer.replace("обстановка и стимуляция") == "обстановка и стимуляция");

    std::vector<WordReplacement> migrated{{"whole_word_case_insensitive", "моё слово", "MyWord"}};
    append_missing_english_term_replacements(migrated);
    assert(migrated.size() == 8);
    append_missing_english_term_replacements(migrated);
    assert(migrated.size() == 8);
    return 0;
}
