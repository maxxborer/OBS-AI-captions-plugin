/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef AI_CAPTION_PLUGIN_BROWSER_OVERLAY_SETTINGS_H
#define AI_CAPTION_PLUGIN_BROWSER_OVERLAY_SETTINGS_H

#include <cstdint>
#include <string>

struct BrowserOverlaySettings {
    std::uint16_t port = 0;
    std::string access_token;

    bool operator==(const BrowserOverlaySettings &rhs) const {
        return port == rhs.port && access_token == rhs.access_token;
    }

    bool operator!=(const BrowserOverlaySettings &rhs) const {
        return !(*this == rhs);
    }
};

#endif // AI_CAPTION_PLUGIN_BROWSER_OVERLAY_SETTINGS_H
