//
// Created by Rat on 06.10.19.
//

#ifndef AI_CAPTION_PLUGIN_SETTINGS_H
#define AI_CAPTION_PLUGIN_SETTINGS_H


#include "SourceCaptioner.h"

struct CaptionPluginSettings {
    bool enabled;
    SourceCaptionerSettings source_cap_settings;

    CaptionPluginSettings(bool enabled, const SourceCaptionerSettings &source_cap_settings) :
            enabled(enabled),
            source_cap_settings(source_cap_settings) {}

    bool operator==(const CaptionPluginSettings &rhs) const {
        return enabled == rhs.enabled &&
               source_cap_settings == rhs.source_cap_settings;
    }

    bool operator!=(const CaptionPluginSettings &rhs) const {
        return !(rhs == *this);
    }

};


#endif // AI_CAPTION_PLUGIN_SETTINGS_H
