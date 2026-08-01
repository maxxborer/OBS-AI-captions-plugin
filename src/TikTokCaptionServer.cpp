/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#include "TikTokCaptionServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

namespace {
constexpr int kMaximumRequestBytes = 16 * 1024;

QByteArray status_text(int status) {
    switch (status) {
        case 200:
            return "200 OK";
        case 204:
            return "204 No Content";
        case 404:
            return "404 Not Found";
        default:
            return "400 Bad Request";
    }
}
}

TikTokCaptionServer::TikTokCaptionServer(QObject *parent) : QObject(parent) {
    connect(&server, &QTcpServer::newConnection, this, &TikTokCaptionServer::accept_connections);
    server.listen(QHostAddress::LocalHost, port);
}

bool TikTokCaptionServer::is_listening() const {
    return server.isListening();
}

QString TikTokCaptionServer::overlay_url() const {
    return QStringLiteral("http://127.0.0.1:%1/").arg(port);
}

void TikTokCaptionServer::update_caption(
        const std::shared_ptr<OutputCaptionResult> &caption,
        bool cleared,
        const std::string &) {
    if (cleared || !caption) {
        caption_text.clear();
        caption_final = true;
    } else {
        caption_text = QString::fromStdString(caption->clean_caption_text).simplified();
        caption_final = caption->caption_result.final;
    }
    ++revision;
}

QByteArray TikTokCaptionServer::build_state_json(const QString &text, bool final, std::uint64_t revision) {
    QJsonObject state;
    state.insert(QStringLiteral("text"), text);
    state.insert(QStringLiteral("final"), final);
    state.insert(QStringLiteral("revision"), QString::number(revision));
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
}

QByteArray TikTokCaptionServer::build_overlay_html() {
    return QByteArrayLiteral(R"HTML(<!doctype html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>AI Caption Plugin — TikTok captions</title>
  <style>
    :root { color-scheme: only light; }
    * { box-sizing: border-box; }
    html, body { width: 100%; height: 100%; margin: 0; overflow: hidden; background: transparent; }
    body {
      display: flex;
      align-items: flex-end;
      justify-content: center;
      padding: 0 5vw 15vh;
      font-family: "Arial Black", Inter, Montserrat, Arial, sans-serif;
    }
    #caption {
      display: flex;
      max-width: 100%;
      justify-content: center;
      align-items: center;
      flex-wrap: wrap;
      column-gap: .24em;
      row-gap: .16em;
      opacity: 0;
      transform: translateY(12px);
      transition: opacity 120ms ease, transform 120ms ease;
    }
    #caption.visible { opacity: 1; transform: translateY(0); }
    .word {
      color: #fff;
      font-size: clamp(46px, 6.2vw, 78px);
      font-weight: 900;
      line-height: 1.08;
      letter-spacing: .01em;
      text-align: center;
      text-transform: none;
      paint-order: stroke fill;
      -webkit-text-stroke: .075em #070707;
      filter: drop-shadow(0 .08em .04em rgba(0, 0, 0, .8));
      white-space: nowrap;
    }
    .word.active {
      color: #101010;
      background: #ffd400;
      border-radius: .18em;
      padding: .05em .15em .09em;
      -webkit-text-stroke: 0 transparent;
      filter: drop-shadow(0 .08em .04em rgba(0, 0, 0, .65));
      transform: scale(1.06);
    }
  </style>
</head>
<body>
  <div id="caption" aria-live="polite"></div>
  <script>
    const caption = document.getElementById('caption');
    let currentRevision = '';
    let lastChange = 0;

    function render(state) {
      caption.replaceChildren();
      const words = state.text.trim().split(/\s+/u).filter(Boolean).slice(-8);
      words.forEach((word, index) => {
        const span = document.createElement('span');
        span.className = 'word' + (!state.final && index === words.length - 1 ? ' active' : '');
        span.textContent = word;
        caption.appendChild(span);
      });
      caption.classList.toggle('visible', words.length > 0);
    }

    async function refresh() {
      try {
        const response = await fetch('/state', { cache: 'no-store' });
        const state = await response.json();
        if (state.revision !== currentRevision) {
          currentRevision = state.revision;
          lastChange = performance.now();
          render(state);
        }
        if (lastChange && performance.now() - lastChange > 2400)
          caption.classList.remove('visible');
      } catch (_) {
        caption.classList.remove('visible');
      }
    }

    refresh();
    setInterval(refresh, 70);
  </script>
</body>
</html>)HTML");
}

void TikTokCaptionServer::accept_connections() {
    while (server.hasPendingConnections()) {
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket)
            continue;

        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read_request(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void TikTokCaptionServer::read_request(QTcpSocket *socket) {
    QByteArray request = socket->property("captionRequest").toByteArray();
    request.append(socket->readAll());

    if (request.size() > kMaximumRequestBytes) {
        send_response(socket, 400, "text/plain; charset=utf-8", "Request too large");
        return;
    }

    socket->setProperty("captionRequest", request);
    if (!request.contains("\r\n\r\n"))
        return;

    const QList<QByteArray> request_line = request.left(request.indexOf("\r\n")).split(' ');
    if (request_line.size() < 2 || request_line[0] != "GET") {
        send_response(socket, 400, "text/plain; charset=utf-8", "Bad request");
        return;
    }

    QByteArray path = request_line[1];
    const qsizetype query_at = path.indexOf('?');
    if (query_at >= 0)
        path.truncate(query_at);

    if (path == "/" || path == "/index.html") {
        send_response(socket, 200, "text/html; charset=utf-8", build_overlay_html());
    } else if (path == "/state") {
        send_response(socket, 200, "application/json; charset=utf-8",
                      build_state_json(caption_text, caption_final, revision));
    } else if (path == "/favicon.ico") {
        send_response(socket, 204, "image/x-icon", {});
    } else {
        send_response(socket, 404, "text/plain; charset=utf-8", "Not found");
    }
}

void TikTokCaptionServer::send_response(
        QTcpSocket *socket,
        int status,
        const QByteArray &content_type,
        const QByteArray &body) const {
    QByteArray response = "HTTP/1.1 " + status_text(status) + "\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store, no-cache, must-revalidate\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}
