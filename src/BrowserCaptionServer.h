/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef AI_CAPTION_PLUGIN_BROWSER_CAPTION_SERVER_H
#define AI_CAPTION_PLUGIN_BROWSER_CAPTION_SERVER_H

#include "BrowserOverlaySettings.h"
#include "CaptionResultHandler.h"

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTcpServer>

#include <cstdint>
#include <memory>

class QTcpSocket;

class BrowserCaptionServer final : public QObject {
Q_OBJECT

public:
    explicit BrowserCaptionServer(
            const BrowserOverlaySettings &settings,
            QObject *parent = nullptr);

    bool is_listening() const;
    bool has_browser_consumer() const;
    BrowserOverlaySettings configure(const BrowserOverlaySettings &settings);
    QString overlay_url() const;
    QString designer_url() const;
    BrowserOverlaySettings settings() const;

    void update_caption(
            const std::shared_ptr<OutputCaptionResult> &caption,
            bool cleared);

    static QByteArray build_state_json(
            const QString &text,
            const QString &current_text,
            int caption_index,
            bool final,
            std::uint64_t revision);
    static QByteArray build_overlay_html();
    static QByteArray build_designer_html();

signals:
    void browser_consumer_presence_changed(bool active);

private:
    QTcpServer server;
    QSet<QTcpSocket *> active_sockets;
    QSet<QTcpSocket *> event_clients;
    QString access_token;
    bool browser_consumer_active = false;
    QString caption_text;
    QString caption_current_text;
    int caption_index = 0;
    bool caption_final = true;
    std::uint64_t revision = 0;

    void accept_connections();
    bool listen(quint16 requested_port);
    void update_browser_consumer_presence();
    bool authorize_path(QByteArray &path) const;
    void read_request(QTcpSocket *socket);
    void start_event_stream(QTcpSocket *socket);
    void broadcast_state();
    void send_response(
            QTcpSocket *socket,
            int status,
            const QByteArray &content_type,
            const QByteArray &body,
            bool immutable = false) const;
};

#endif // AI_CAPTION_PLUGIN_BROWSER_CAPTION_SERVER_H
