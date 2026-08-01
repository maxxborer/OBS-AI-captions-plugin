/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef AI_CAPTION_PLUGIN_TIKTOKCAPTIONSERVER_H
#define AI_CAPTION_PLUGIN_TIKTOKCAPTIONSERVER_H

#include "CaptionResultHandler.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpServer>

#include <cstdint>
#include <memory>
#include <string>

class QTcpSocket;

class TikTokCaptionServer final : public QObject {
Q_OBJECT

public:
    static constexpr quint16 port = 37545;

    explicit TikTokCaptionServer(QObject *parent = nullptr);

    bool is_listening() const;
    QString overlay_url() const;

    void update_caption(
            const std::shared_ptr<OutputCaptionResult> &caption,
            bool cleared,
            const std::string &recent_caption_text);

    static QByteArray build_state_json(const QString &text, bool final, std::uint64_t revision);
    static QByteArray build_overlay_html();

private:
    QTcpServer server;
    QString caption_text;
    bool caption_final = true;
    std::uint64_t revision = 0;

    void accept_connections();
    void read_request(QTcpSocket *socket);
    void send_response(QTcpSocket *socket, int status, const QByteArray &content_type, const QByteArray &body) const;
};

#endif // AI_CAPTION_PLUGIN_TIKTOKCAPTIONSERVER_H
