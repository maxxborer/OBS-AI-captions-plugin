/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef AI_CAPTION_PLUGIN_ENGLISH_TERM_REPLACEMENTS_H
#define AI_CAPTION_PLUGIN_ENGLISH_TERM_REPLACEMENTS_H

#include <algorithm>
#include <string>
#include <vector>

#include "WordReplacer.h"

inline std::vector<WordReplacement> default_english_term_replacements() {
    // T-One is intentionally Russian-only. These whole-term replacements keep
    // common streaming product names in Latin script without enabling language
    // auto-detection or changing the recognition model.
    return {
            {"whole_word_case_insensitive", "обс", "OBS"},
            {"whole_word_case_insensitive", "твич", "Twitch"},
            {"whole_word_case_insensitive", "дискорд", "Discord"},
            {"whole_word_case_insensitive", "ютуб", "YouTube"},
            {"whole_word_case_insensitive", "стим", "Steam"},
            {"whole_word_case_insensitive", "чат гпт", "ChatGPT"},
            {"whole_word_case_insensitive", "стример бот", "Streamer.bot"},
    };
}

inline void append_missing_english_term_replacements(std::vector<WordReplacement> &replacements) {
    for (const auto &candidate: default_english_term_replacements()) {
        const auto existing = std::find_if(replacements.begin(), replacements.end(), [&](const WordReplacement &item) {
            return item.get_from() == candidate.get_from();
        });
        if (existing == replacements.end())
            replacements.push_back(candidate);
    }
}

#endif // AI_CAPTION_PLUGIN_ENGLISH_TERM_REPLACEMENTS_H
