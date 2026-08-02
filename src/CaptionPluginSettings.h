//
// Created by Rat on 06.10.19.
//

#ifndef AI_CAPTION_PLUGIN_SETTINGS_H
#define AI_CAPTION_PLUGIN_SETTINGS_H


#include "SourceCaptioner.h"

#include <cstdint>

struct BrowserOverlaySettings {
    std::uint16_t port = 0;
    string access_token;

    bool operator==(const BrowserOverlaySettings &rhs) const {
        return port == rhs.port && access_token == rhs.access_token;
    }

    bool operator!=(const BrowserOverlaySettings &rhs) const {
        return !(*this == rhs);
    }
};

struct CaptionPluginSettings {
    bool enabled;
    SourceCaptionerSettings source_cap_settings;
    BrowserOverlaySettings browser_overlay;

    CaptionPluginSettings(
            bool enabled,
            const SourceCaptionerSettings &source_cap_settings,
            const BrowserOverlaySettings &browser_overlay = {}) :
            enabled(enabled),
            source_cap_settings(source_cap_settings),
            browser_overlay(browser_overlay) {}

    bool operator==(const CaptionPluginSettings &rhs) const {
        return enabled == rhs.enabled &&
               source_cap_settings == rhs.source_cap_settings &&
               browser_overlay == rhs.browser_overlay;
    }

    bool operator!=(const CaptionPluginSettings &rhs) const {
        return !(rhs == *this);
    }

};


#endif // AI_CAPTION_PLUGIN_SETTINGS_H
