/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef AI_CAPTION_PLUGIN_BROWSER_OVERLAY_SETTINGS_H
#define AI_CAPTION_PLUGIN_BROWSER_OVERLAY_SETTINGS_H

#include <QRandomGenerator>
#include <QString>

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

inline BrowserOverlaySettings new_browser_overlay_settings() {
    BrowserOverlaySettings settings;
    settings.port = static_cast<std::uint16_t>(
            49152U + QRandomGenerator::system()->bounded(16383U));
    QString token;
    token.reserve(64);
    for (int index = 0; index < 8; ++index) {
        token += QString::number(QRandomGenerator::system()->generate(), 16)
                         .rightJustified(8, QLatin1Char('0'));
    }
    settings.access_token = token.toStdString();
    return settings;
}

#endif // AI_CAPTION_PLUGIN_BROWSER_OVERLAY_SETTINGS_H
