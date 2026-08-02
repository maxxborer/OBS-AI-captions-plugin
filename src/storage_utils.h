#ifndef AI_CAPTION_PLUGIN_STORAGE_UTILS_H
#define AI_CAPTION_PLUGIN_STORAGE_UTILS_H

#include "SourceCaptioner.h"

static CaptionSourceMuteType string_to_mute_setting(
        const string &setting) {
    if (setting == "own_source")
        return CAPTION_SOURCE_MUTE_TYPE_FROM_OWN_SOURCE;
    if (setting == "always")
        return CAPTION_SOURCE_MUTE_TYPE_ALWAYS_CAPTION;
    if (setting == "other_mute_source")
        return CAPTION_SOURCE_MUTE_TYPE_USE_OTHER_MUTE_SOURCE;
    return CAPTION_SOURCE_MUTE_TYPE_FROM_OWN_SOURCE;
}

static string mute_setting_to_string(CaptionSourceMuteType setting) {
    switch (setting) {
        case CAPTION_SOURCE_MUTE_TYPE_FROM_OWN_SOURCE:
            return "own_source";
        case CAPTION_SOURCE_MUTE_TYPE_ALWAYS_CAPTION:
            return "always";
        case CAPTION_SOURCE_MUTE_TYPE_USE_OTHER_MUTE_SOURCE:
            return "other_mute_source";
    }
    return "own_source";
}

#endif // AI_CAPTION_PLUGIN_STORAGE_UTILS_H
