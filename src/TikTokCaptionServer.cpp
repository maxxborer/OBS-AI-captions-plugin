/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#include "TikTokCaptionServer.h"

#include <QFile>
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
    @font-face {
      font-family: "Geologica";
      src: url("/assets/geologica.ttf") format("truetype");
      font-style: normal;
      font-weight: 100 900;
      font-display: swap;
    }
    :root {
      color-scheme: only light;
      --caption-font: "Geologica", sans-serif;
      --caption-font-size: 72px;
      --caption-font-weight: 800;
      --caption-color: #fff;
      --caption-outline-color: #070707;
      --caption-outline-width: 5px;
      --caption-shadow: 0 6px 3px rgba(0, 0, 0, .8);
      --caption-transform: none;
      --caption-align: center;
      --caption-column-gap: .24em;
      --caption-row-gap: .16em;
      --active-color: #fff;
      --active-background: #8b5cf6;
      --active-radius: .18em;
      --active-padding: .05em .15em .09em;
      --active-scale: 1.06;
    }
    * { box-sizing: border-box; }
    html, body { width: 100%; height: 100%; margin: 0; overflow: hidden; background: transparent; }
    body {
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 0;
      font-family: var(--caption-font);
    }
    #caption {
      display: flex;
      max-width: 100%;
      width: 100%;
      justify-content: var(--caption-align);
      align-items: center;
      flex-wrap: wrap;
      column-gap: var(--caption-column-gap);
      row-gap: var(--caption-row-gap);
      opacity: 0;
      transform: translateY(12px);
      transition: opacity 120ms ease, transform 120ms ease;
    }
    #caption.visible { opacity: 1; transform: translateY(0); }
    .word {
      color: var(--caption-color);
      font-size: var(--caption-font-size);
      font-weight: var(--caption-font-weight);
      line-height: 1.08;
      letter-spacing: .01em;
      text-align: center;
      text-transform: var(--caption-transform);
      paint-order: stroke fill;
      -webkit-text-stroke: var(--caption-outline-width) var(--caption-outline-color);
      filter: drop-shadow(var(--caption-shadow));
      white-space: nowrap;
    }
    .word.active {
      color: var(--active-color);
      background: var(--active-background);
      border-radius: var(--active-radius);
      padding: var(--active-padding);
      -webkit-text-stroke: 0 transparent;
      filter: drop-shadow(var(--caption-shadow));
      transform: scale(var(--active-scale));
    }
  </style>
</head>
<body>
  <div id="caption" aria-live="polite"></div>
  <script>
    const caption = document.getElementById('caption');
    const parameters = new URLSearchParams(location.search);
    const maximumWords = Math.min(30, Math.max(1, Number.parseInt(parameters.get('words') || '8', 10) || 8));
    const timeoutMs = Math.min(15000, Math.max(250, Number.parseInt(parameters.get('timeout') || '2400', 10) || 2400));
    const rootStyle = document.documentElement.style;

    function numberParameter(name, fallback, minimum, maximum) {
      const value = Number.parseFloat(parameters.get(name));
      return Number.isFinite(value) ? Math.min(maximum, Math.max(minimum, value)) : fallback;
    }

    function colorParameter(name, fallback) {
      const value = parameters.get(name) || '';
      return /^#[0-9a-f]{6}$/iu.test(value) ? value : fallback;
    }

    const font = (parameters.get('font') || 'Geologica').replace(/["'\\]/gu, '').slice(0, 80);
    const alignment = ['flex-start', 'center', 'flex-end'].includes(parameters.get('align'))
      ? parameters.get('align')
      : 'center';
    rootStyle.setProperty('--caption-font', `"${font}", "Geologica", sans-serif`);
    rootStyle.setProperty('--caption-font-size', `${numberParameter('size', 72, 20, 180)}px`);
    rootStyle.setProperty('--caption-font-weight', `${numberParameter('weight', 800, 100, 900)}`);
    rootStyle.setProperty('--caption-color', colorParameter('color', '#ffffff'));
    rootStyle.setProperty('--caption-outline-color', colorParameter('outline', '#070707'));
    rootStyle.setProperty('--caption-outline-width', `${numberParameter('outlineWidth', 5, 0, 16)}px`);
    rootStyle.setProperty('--caption-align', alignment);
    rootStyle.setProperty('--caption-transform', parameters.get('uppercase') === '1' ? 'uppercase' : 'none');
    rootStyle.setProperty('--active-color', colorParameter('activeColor', '#ffffff'));
    rootStyle.setProperty('--active-background', colorParameter('activeBg', '#8b5cf6'));
    rootStyle.setProperty('--active-radius', `${numberParameter('radius', 14, 0, 50)}px`);
    rootStyle.setProperty('--active-scale', `${numberParameter('scale', 1.06, 1, 1.5)}`);
    let currentRevision = '';
    let lastChange = 0;

    function render(state) {
      caption.replaceChildren();
      const words = state.text.trim().split(/\s+/u).filter(Boolean).slice(-maximumWords);
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
        if (lastChange && performance.now() - lastChange > timeoutMs)
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

QByteArray TikTokCaptionServer::build_designer_html() {
    return QByteArrayLiteral(R"HTML(<!doctype html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Конструктор субтитров — AI Caption Plugin</title>
  <style>
    @font-face {
      font-family: "Geologica";
      src: url("/assets/geologica.ttf") format("truetype");
      font-style: normal;
      font-weight: 100 900;
      font-display: swap;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      color: #f7f5ff;
      background: #100d18;
      font-family: "Geologica", sans-serif;
    }
    main { width: min(1120px, calc(100% - 32px)); margin: 32px auto 56px; }
    h1 { margin: 0 0 8px; font-size: clamp(28px, 4vw, 46px); }
    .lead { margin: 0 0 28px; color: #bcb5ca; line-height: 1.5; }
    .layout { display: grid; grid-template-columns: minmax(300px, 420px) 1fr; gap: 24px; }
    .panel { padding: 22px; border: 1px solid #332b43; border-radius: 20px; background: #1a1624; }
    .controls { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
    label { display: grid; gap: 7px; color: #d8d2e2; font-size: 14px; }
    label.wide { grid-column: 1 / -1; }
    input, select, button {
      min-height: 42px;
      border: 1px solid #443a57;
      border-radius: 10px;
      color: #fff;
      background: #241e31;
      font: inherit;
    }
    input, select { width: 100%; padding: 8px 10px; }
    input[type="color"] { padding: 4px; cursor: pointer; }
    input[type="checkbox"] { width: 22px; min-height: 22px; accent-color: #8b5cf6; }
    .check { display: flex; align-items: center; gap: 10px; }
    .preview-shell {
      position: sticky;
      top: 24px;
      display: grid;
      min-height: 390px;
      place-items: center;
      overflow: hidden;
      border-radius: 20px;
      background:
        linear-gradient(135deg, rgba(139, 92, 246, .18), transparent 55%),
        #292331;
    }
    #preview { display: flex; flex-wrap: wrap; justify-content: center; align-items: center; gap: .18em .24em; padding: 28px; }
    .word {
      color: var(--caption-color, #fff);
      font-family: var(--caption-font, "Geologica", sans-serif);
      font-size: var(--caption-font-size, 72px);
      font-weight: var(--caption-font-weight, 800);
      line-height: 1.08;
      text-transform: var(--caption-transform, none);
      paint-order: stroke fill;
      -webkit-text-stroke: var(--caption-outline-width, 5px) var(--caption-outline-color, #070707);
      filter: drop-shadow(0 6px 3px rgba(0, 0, 0, .8));
      white-space: nowrap;
    }
    .word.active {
      color: var(--active-color, #fff);
      background: var(--active-background, #8b5cf6);
      border-radius: var(--active-radius, 14px);
      padding: .05em .15em .09em;
      -webkit-text-stroke: 0 transparent;
      transform: scale(var(--active-scale, 1.06));
    }
    .result { margin-top: 24px; }
    .result-row { display: grid; grid-template-columns: 1fr auto auto; gap: 10px; }
    #url { min-width: 0; font-family: Consolas, monospace; }
    button { padding: 8px 16px; cursor: pointer; border-color: #7454b8; background: #6d43ca; font-weight: 700; }
    button.secondary { background: #292236; }
    .hint { margin: 12px 0 0; color: #aaa1b7; font-size: 13px; line-height: 1.45; }
    #copied { min-height: 22px; margin-top: 8px; color: #b9f6cf; font-size: 13px; }
    @media (max-width: 820px) {
      .layout { grid-template-columns: 1fr; }
      .preview-shell { position: static; min-height: 300px; }
      .result-row { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
<main>
  <h1>Конструктор субтитров</h1>
  <p class="lead">Настрой оформление, скопируй готовый URL в Browser Source, а сам источник поставь в любое место вертикальной сцены через Transform.</p>
  <div class="layout">
    <section class="panel controls">
      <label class="wide">Шрифт<input id="font" value="Geologica" maxlength="80"></label>
      <label>Размер, px<input id="size" type="number" min="20" max="180" value="72"></label>
      <label>Насыщенность<input id="weight" type="number" min="100" max="900" step="50" value="800"></label>
      <label>Цвет текста<input id="color" type="color" value="#ffffff"></label>
      <label>Цвет обводки<input id="outline" type="color" value="#070707"></label>
      <label>Толщина обводки<input id="outlineWidth" type="number" min="0" max="16" value="5"></label>
      <label>Выравнивание
        <select id="align"><option value="center">По центру</option><option value="flex-start">Слева</option><option value="flex-end">Справа</option></select>
      </label>
      <label>Фон активного слова<input id="activeBg" type="color" value="#8b5cf6"></label>
      <label>Текст активного слова<input id="activeColor" type="color" value="#ffffff"></label>
      <label>Скругление, px<input id="radius" type="number" min="0" max="50" value="14"></label>
      <label>Увеличение активного слова<input id="scale" type="number" min="1" max="1.5" step="0.01" value="1.06"></label>
      <label>Слов на экране<input id="words" type="number" min="1" max="30" value="8"></label>
      <label>Исчезновение, мс<input id="timeout" type="number" min="250" max="15000" step="50" value="2400"></label>
      <label class="wide check"><input id="uppercase" type="checkbox">Все буквы заглавные</label>
    </section>
    <section class="preview-shell">
      <div id="preview"><span class="word">Субтитры</span><span class="word">прямо</span><span class="word active">сейчас</span></div>
    </section>
  </div>
  <section class="panel result">
    <label class="wide">Готовый URL для Browser Source</label>
    <div class="result-row">
      <input id="url" readonly>
      <button id="copy">Скопировать URL</button>
      <button id="open" class="secondary">Открыть оверлей</button>
    </div>
    <div id="copied" role="status"></div>
    <p class="hint">Рекомендуемый размер источника: примерно 1000 × 320. Это только область текста — её можно свободно перемещать и масштабировать в OBS/Aitum. Для полной ручной настройки по-прежнему доступно поле Custom CSS и классы <code>.word</code> / <code>.word.active</code>.</p>
  </section>
</main>
<script>
  const ids = ['font', 'size', 'weight', 'color', 'outline', 'outlineWidth', 'align', 'activeBg', 'activeColor', 'radius', 'scale', 'words', 'timeout', 'uppercase'];
  const fields = Object.fromEntries(ids.map(id => [id, document.getElementById(id)]));
  const preview = document.getElementById('preview');
  const url = document.getElementById('url');
  const copied = document.getElementById('copied');

  function update() {
    const font = fields.font.value.trim() || 'Geologica';
    preview.style.setProperty('--caption-font', `"${font.replace(/["'\\]/gu, '')}", "Geologica", sans-serif`);
    preview.style.setProperty('--caption-font-size', `${fields.size.value || 72}px`);
    preview.style.setProperty('--caption-font-weight', fields.weight.value || 800);
    preview.style.setProperty('--caption-color', fields.color.value);
    preview.style.setProperty('--caption-outline-color', fields.outline.value);
    preview.style.setProperty('--caption-outline-width', `${fields.outlineWidth.value || 0}px`);
    preview.style.setProperty('--caption-transform', fields.uppercase.checked ? 'uppercase' : 'none');
    preview.style.justifyContent = fields.align.value;
    preview.style.setProperty('--active-background', fields.activeBg.value);
    preview.style.setProperty('--active-color', fields.activeColor.value);
    preview.style.setProperty('--active-radius', `${fields.radius.value || 0}px`);
    preview.style.setProperty('--active-scale', fields.scale.value || 1);

    const parameters = new URLSearchParams({
      font,
      size: fields.size.value || '72',
      weight: fields.weight.value || '800',
      color: fields.color.value,
      outline: fields.outline.value,
      outlineWidth: fields.outlineWidth.value || '0',
      align: fields.align.value,
      activeBg: fields.activeBg.value,
      activeColor: fields.activeColor.value,
      radius: fields.radius.value || '0',
      scale: fields.scale.value || '1',
      words: fields.words.value || '8',
      timeout: fields.timeout.value || '2400'
    });
    if (fields.uppercase.checked)
      parameters.set('uppercase', '1');
    url.value = `${location.origin}/?${parameters}`;
    copied.textContent = '';
  }

  ids.forEach(id => fields[id].addEventListener('input', update));
  document.getElementById('copy').addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(url.value);
    } catch (_) {
      url.select();
      document.execCommand('copy');
    }
    copied.textContent = 'URL скопирован.';
  });
  document.getElementById('open').addEventListener('click', () => window.open(url.value, '_blank', 'noopener'));
  update();
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
    } else if (path == "/setup" || path == "/designer") {
        send_response(socket, 200, "text/html; charset=utf-8", build_designer_html());
    } else if (path == "/state") {
        send_response(socket, 200, "application/json; charset=utf-8",
                      build_state_json(caption_text, caption_final, revision));
    } else if (path == "/assets/geologica.ttf") {
        QFile font(QStringLiteral(":/fonts/geologica.ttf"));
        if (font.open(QIODevice::ReadOnly))
            send_response(socket, 200, "font/ttf", font.readAll());
        else
            send_response(socket, 404, "text/plain; charset=utf-8", "Font not found");
    } else if (path == "/licenses/geologica") {
        QFile license(QStringLiteral(":/licenses/geologica-ofl.txt"));
        if (license.open(QIODevice::ReadOnly))
            send_response(socket, 200, "text/plain; charset=utf-8", license.readAll());
        else
            send_response(socket, 404, "text/plain; charset=utf-8", "License not found");
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
