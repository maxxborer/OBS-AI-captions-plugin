/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#include "BrowserCaptionServer.h"

#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr int kMaximumRequestBytes = 16 * 1024;
constexpr qint64 kMaximumPendingEventBytes = 64 * 1024;
constexpr int kMaximumConnections = 16;
constexpr int kMaximumEventClients = 8;
constexpr int kRequestTimeoutMilliseconds = 5000;
constexpr int kMaximumCaptionCharacters = 4096;

bool valid_access_token(const QString &token) {
    if (token.size() != 64)
        return false;
    for (const QChar character : token) {
        if (!character.isDigit() &&
            !(character >= QLatin1Char('a') && character <= QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

QByteArray status_text(int status) {
    switch (status) {
        case 200:
            return "200 OK";
        case 204:
            return "204 No Content";
        case 404:
            return "404 Not Found";
        case 503:
            return "503 Service Unavailable";
        default:
            return "400 Bad Request";
    }
}
}

BrowserCaptionServer::BrowserCaptionServer(
        const BrowserOverlaySettings &settings,
        QObject *parent)
        : QObject(parent),
          access_token(QString::fromStdString(settings.access_token)) {
    server.setMaxPendingConnections(kMaximumConnections);
    connect(&server, &QTcpServer::newConnection, this, &BrowserCaptionServer::accept_connections);
    if (valid_access_token(access_token))
        listen(settings.port);
}

bool BrowserCaptionServer::listen(quint16 requested_port) {
    if (server.listen(QHostAddress::LocalHost, requested_port))
        return true;
    return server.listen(QHostAddress::LocalHost, 0);
}

bool BrowserCaptionServer::is_listening() const {
    return server.isListening();
}

bool BrowserCaptionServer::has_browser_consumer() const {
    return browser_consumer_active;
}

BrowserOverlaySettings BrowserCaptionServer::configure(
        const BrowserOverlaySettings &settings) {
    const QSet<QTcpSocket *> sockets = active_sockets;
    for (QTcpSocket *socket : sockets)
        socket->abort();
    active_sockets.clear();
    event_clients.clear();
    update_browser_consumer_presence();
    server.close();
    access_token = QString::fromStdString(settings.access_token);
    if (valid_access_token(access_token))
        listen(settings.port);
    return this->settings();
}

QString BrowserCaptionServer::overlay_url() const {
    if (!server.isListening())
        return {};
    return QStringLiteral("http://127.0.0.1:%1/%2/")
            .arg(server.serverPort())
            .arg(access_token);
}

QString BrowserCaptionServer::designer_url() const {
    const QString overlay = overlay_url();
    return overlay.isEmpty() ? QString() : overlay + QStringLiteral("setup");
}

BrowserOverlaySettings BrowserCaptionServer::settings() const {
    BrowserOverlaySettings current;
    current.port = server.isListening() ? server.serverPort() : 0;
    current.access_token = access_token.toStdString();
    return current;
}

void BrowserCaptionServer::update_browser_consumer_presence() {
    const bool active = !event_clients.isEmpty();
    if (active == browser_consumer_active)
        return;

    browser_consumer_active = active;
    emit browser_consumer_presence_changed(active);
}

void BrowserCaptionServer::update_caption(
        const std::shared_ptr<OutputCaptionResult> &caption,
        bool cleared) {
    if (cleared || !caption) {
        caption_text.clear();
        caption_current_text.clear();
        caption_index = 0;
        caption_final = true;
    } else {
        caption_text = QString::fromStdString(caption->output_line)
                               .simplified()
                               .left(kMaximumCaptionCharacters);
        caption_current_text = QString::fromStdString(caption->clean_caption_text)
                                       .simplified()
                                       .left(kMaximumCaptionCharacters);
        caption_index = caption->caption_result.index;
        caption_final = caption->caption_result.final;
    }
    ++revision;
    broadcast_state();
}

QByteArray BrowserCaptionServer::build_state_json(
        const QString &text,
        const QString &current_text,
        int caption_index,
        bool final,
        std::uint64_t revision) {
    QJsonObject state;
    state.insert(QStringLiteral("text"), text);
    state.insert(QStringLiteral("currentText"), current_text);
    state.insert(QStringLiteral("index"), caption_index);
    state.insert(QStringLiteral("final"), final);
    state.insert(QStringLiteral("revision"), QString::number(revision));
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
}

QByteArray BrowserCaptionServer::build_overlay_html() {
    return QByteArrayLiteral(R"HTML(<!doctype html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>AI Caption Plugin — browser captions</title>
  <style>
    @font-face {
      font-family: "Geologica";
      src: url("assets/geologica.ttf") format("truetype");
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

    function numberParameter(name, defaultValue, minimum, maximum) {
      const value = Number.parseFloat(parameters.get(name));
      return Number.isFinite(value) ? Math.min(maximum, Math.max(minimum, value)) : defaultValue;
    }

    function colorParameter(name, defaultValue) {
      const value = parameters.get(name) || '';
      return /^#[0-9a-f]{6}$/iu.test(value) ? value : defaultValue;
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
    let activeIndex = null;
    let activeText = '';
    const historyWords = [];
    let events = null;
    let hideTimer = 0;

    function splitWords(text) {
      return text.trim().split(/\s+/u).filter(Boolean);
    }

    function resetVisibleHistory() {
      historyWords.length = 0;
      activeIndex = null;
      activeText = '';
      caption.replaceChildren();
      caption.classList.remove('visible');
    }

    function render(words, final) {
      caption.replaceChildren();
      words.forEach((word, index) => {
        const span = document.createElement('span');
        span.className = 'word' + (!final && index === words.length - 1 ? ' active' : '');
        span.textContent = word;
        caption.appendChild(span);
      });
      caption.classList.toggle('visible', words.length > 0);
    }

    function consumeState(state) {
      if (state.revision === currentRevision)
        return;
      currentRevision = state.revision;
      clearTimeout(hideTimer);
      const currentText = typeof state.currentText === 'string' ? state.currentText.trim() : '';
      if (!currentText) {
        resetVisibleHistory();
        return;
      }
      const nextIndex = Number.isInteger(state.index) ? state.index : String(state.index ?? '');
      if (activeIndex !== null && nextIndex !== activeIndex && activeText) {
        historyWords.push(...splitWords(activeText));
        if (historyWords.length > maximumWords)
          historyWords.splice(0, historyWords.length - maximumWords);
      }
      activeIndex = nextIndex;
      activeText = currentText;
      const visibleWords = [...historyWords, ...splitWords(activeText)].slice(-maximumWords);
      render(visibleWords, Boolean(state.final));
      hideTimer = setTimeout(resetVisibleHistory, timeoutMs);
    }

    function connectEvents() {
      if (document.hidden || events)
        return;
      events = new EventSource('events');
      events.onmessage = event => {
        try {
          consumeState(JSON.parse(event.data));
        } catch (_) {
          caption.classList.remove('visible');
        }
      };
      events.onerror = () => caption.classList.remove('visible');
    }

    function disconnectEvents() {
      if (events) {
        events.close();
        events = null;
      }
      clearTimeout(hideTimer);
      resetVisibleHistory();
    }

    document.addEventListener('visibilitychange', () => {
      if (document.hidden)
        disconnectEvents();
      else
        connectEvents();
    });
    connectEvents();
  </script>
</body>
</html>)HTML");
}

QByteArray BrowserCaptionServer::build_designer_html() {
    return QByteArrayLiteral(R"HTML(<!doctype html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Конструктор субтитров — AI Caption Plugin</title>
  <style>
    @font-face {
      font-family: "Geologica";
      src: url("assets/geologica.ttf") format("truetype");
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
    <label class="wide">Восстановить настройки из уже добавленного Browser Source
      <div class="result-row"><input id="restore" placeholder="Вставьте текущий URL Browser Source"><button id="restoreButton" class="secondary">Восстановить</button></div>
    </label>
    <p class="hint">Вставьте полный URL источника из OBS, чтобы поменять один-два параметра без настройки с нуля. Ссылка не открывается в сети: страница читает только её параметры оформления.</p>
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
  const restore = document.getElementById('restore');
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
    const overlay = new URL('.', location.href);
    overlay.search = parameters.toString();
    url.value = overlay.href;
    copied.textContent = '';
  }

  ids.forEach(id => fields[id].addEventListener('input', update));
  document.getElementById('copy').addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(url.value);
      copied.textContent = 'URL скопирован.';
    } catch (_) {
      copied.textContent = 'Не удалось скопировать. Выделите URL вручную.';
    }
  });
  document.getElementById('open').addEventListener('click', () => window.open(url.value, '_blank', 'noopener'));
  document.getElementById('restoreButton').addEventListener('click', () => {
    let source;
    try {
      source = new URL(restore.value.trim(), location.href);
    } catch (_) {
      copied.textContent = 'Не удалось прочитать URL Browser Source.';
      return;
    }
    const parameters = source.searchParams;
    ids.filter(id => id !== 'uppercase').forEach(id => {
      const value = parameters.get(id);
      if (value === null)
        return;
      const field = fields[id];
      const previous = field.value;
      field.value = value;
      if (!field.checkValidity() || field.value !== value)
        field.value = previous;
    });
    fields.uppercase.checked = parameters.get('uppercase') === '1';
    update();
    copied.textContent = 'Настройки восстановлены. Измените нужное и скопируйте новый URL.';
  });
  update();
</script>
</body>
</html>)HTML");
}

void BrowserCaptionServer::accept_connections() {
    while (server.hasPendingConnections()) {
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket)
            continue;

        if (active_sockets.size() >= kMaximumConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }

        socket->setParent(this);
        active_sockets.insert(socket);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read_request(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            active_sockets.remove(socket);
            if (event_clients.remove(socket))
                update_browser_consumer_presence();
            socket->deleteLater();
        });
        QPointer<QTcpSocket> guarded_socket(socket);
        QTimer::singleShot(kRequestTimeoutMilliseconds, socket, [guarded_socket] {
            if (guarded_socket &&
                !guarded_socket->property("captionEventStream").toBool()) {
                guarded_socket->abort();
            }
        });
    }
}

bool BrowserCaptionServer::authorize_path(QByteArray &path) const {
    const QByteArray prefix = "/" + access_token.toLatin1();
    if (path == prefix) {
        path = "/";
        return true;
    }
    if (!path.startsWith(prefix + "/"))
        return false;
    path.remove(0, prefix.size());
    return true;
}

void BrowserCaptionServer::read_request(QTcpSocket *socket) {
    if (socket->property("captionEventStream").toBool()) {
        socket->readAll();
        return;
    }

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

    if (!authorize_path(path)) {
        send_response(socket, 404, "text/plain; charset=utf-8", "Not found");
        return;
    }

    if (path == "/" || path == "/index.html") {
        send_response(socket, 200, "text/html; charset=utf-8", build_overlay_html());
    } else if (path == "/setup" || path == "/designer") {
        send_response(socket, 200, "text/html; charset=utf-8", build_designer_html());
    } else if (path == "/events") {
        start_event_stream(socket);
    } else if (path == "/assets/geologica.ttf") {
        QFile font(QStringLiteral(":/fonts/geologica.ttf"));
        if (font.open(QIODevice::ReadOnly))
            send_response(socket, 200, "font/ttf", font.readAll(), true);
        else
            send_response(socket, 404, "text/plain; charset=utf-8", "Font not found");
    } else if (path == "/licenses/geologica") {
        QFile license(QStringLiteral(":/licenses/geologica-ofl.txt"));
        if (license.open(QIODevice::ReadOnly))
            send_response(socket, 200, "text/plain; charset=utf-8", license.readAll(), true);
        else
            send_response(socket, 404, "text/plain; charset=utf-8", "License not found");
    } else if (path == "/favicon.ico") {
        send_response(socket, 204, "image/x-icon", {});
    } else {
        send_response(socket, 404, "text/plain; charset=utf-8", "Not found");
    }
}

void BrowserCaptionServer::start_event_stream(QTcpSocket *socket) {
    if (event_clients.size() >= kMaximumEventClients) {
        send_response(socket, 503, "text/plain; charset=utf-8", "Too many caption consumers");
        return;
    }
    socket->setProperty("captionEventStream", true);
    socket->setProperty("captionRequest", QByteArray());
    event_clients.insert(socket);

    QByteArray response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/event-stream; charset=utf-8\r\n";
    response += "Cache-Control: no-cache, no-transform\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    response += "Referrer-Policy: no-referrer\r\n";
    response += "Connection: keep-alive\r\n\r\n";
    response += "retry: 1000\n\n";
    response += "data: " +
            build_state_json(
                    caption_text,
                    caption_current_text,
                    caption_index,
                    caption_final,
                    revision) +
            "\n\n";
    socket->write(response);
    update_browser_consumer_presence();
}

void BrowserCaptionServer::broadcast_state() {
    if (event_clients.isEmpty())
        return;

    const QByteArray event =
            "data: " +
            build_state_json(
                    caption_text,
                    caption_current_text,
                    caption_index,
                    caption_final,
                    revision) +
            "\n\n";
    QList<QTcpSocket *> stalled_clients;
    for (QTcpSocket *socket : event_clients) {
        if (socket->bytesToWrite() > kMaximumPendingEventBytes) {
            stalled_clients.push_back(socket);
        } else if (socket->state() == QAbstractSocket::ConnectedState) {
            socket->write(event);
        }
    }
    for (QTcpSocket *socket : stalled_clients)
        socket->abort();
}

void BrowserCaptionServer::send_response(
        QTcpSocket *socket,
        int status,
        const QByteArray &content_type,
        const QByteArray &body,
        bool immutable) const {
    QByteArray response = "HTTP/1.1 " + status_text(status) + "\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += immutable
            ? "Cache-Control: public, max-age=31536000, immutable\r\n"
            : "Cache-Control: no-store, no-cache, must-revalidate\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    response += "Referrer-Policy: no-referrer\r\n";
    if (content_type.startsWith("text/html")) {
        response += "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; "
                    "script-src 'unsafe-inline'; font-src 'self'; connect-src 'self'\r\n";
    }
    response += "Connection: close\r\n\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}
