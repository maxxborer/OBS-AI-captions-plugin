#include "BrowserCaptionServer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

QByteArray request_path(
        QCoreApplication &application,
        quint16 port,
        const QByteArray &path) {
    QTcpSocket client;
    client.connectToHost(QStringLiteral("127.0.0.1"), port);
    require(client.waitForConnected(1000), "Client must connect to the local overlay server");
    client.write("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(client.waitForBytesWritten(1000), "Client must send an HTTP request");

    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 && client.state() != QAbstractSocket::UnconnectedState) {
        application.processEvents(QEventLoop::AllEvents, 20);
        if (client.waitForReadyRead(20))
            response.append(client.readAll());
    }
    response.append(client.readAll());
    return response;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);

    const BrowserOverlaySettings generated_browser_settings = new_browser_overlay_settings();
    require(generated_browser_settings.port >= 49152,
            "Fresh settings must reserve a private dynamic browser port");
    require(generated_browser_settings.access_token.size() == 64,
            "Fresh settings must contain a 256-bit browser access token");

    BrowserOverlaySettings invalid_browser_settings;
    invalid_browser_settings.access_token = "predictable";
    BrowserCaptionServer invalid_server(invalid_browser_settings);
    require(!invalid_server.is_listening(),
            "Browser caption server must fail closed without a 256-bit token");

    const QByteArray json = BrowserCaptionServer::build_state_json(
            QString::fromUtf8("старые слова. быстрые слова OBS"),
            QString::fromUtf8("быстрые слова OBS"),
            7,
            false,
            42);
    const QJsonObject state = QJsonDocument::fromJson(json).object();
    require(state.value("text").toString() == QString::fromUtf8("старые слова. быстрые слова OBS"),
            "State must preserve the complete server caption text");
    require(state.value("currentText").toString() == QString::fromUtf8("быстрые слова OBS"),
            "State must expose the current utterance separately from server history");
    require(state.value("index").toInt() == 7,
            "State must identify the current utterance for browser-local history");
    require(!state.value("final").toBool(), "Partial state must remain partial");
    require(state.value("revision").toString() == QStringLiteral("42"),
            "Revision must be serialized without numeric precision loss");

    const QByteArray html = BrowserCaptionServer::build_overlay_html();
    require(html.contains("parameters.get('words')") && html.contains("slice(-maximumWords)"),
            "Overlay word count must be configurable in the Browser Source URL");
    require(html.contains("parameters.get('timeout')") && html.contains("timeoutMs"),
            "Overlay timeout must be configurable in the Browser Source URL");
    require(html.contains("historyWords.length = 0") && html.contains("activeIndex = null"),
            "Overlay timeout must discard browser-local caption history");
    require(html.contains("state.currentText") && html.contains("state.index"),
            "Overlay must rebuild visible history only from post-timeout utterances");
    require(html.contains("index === words.length - 1") && html.contains("' active'"),
            "Overlay must mark the latest partial word as active");
    require(html.contains("textContent = word"), "Overlay must render recognized text without HTML injection");
    require(html.contains("new EventSource('events')"),
            "Overlay must receive captions immediately over one persistent connection");
    require(!html.contains("setInterval(refresh, 70)"),
            "Overlay must not poll the local server continuously");
    require(html.contains("document.hidden") && html.contains("visibilitychange"),
            "A hidden Browser Source must close its event stream and release its recognition consumer");
    require(html.contains("--caption-font-size") && html.contains("--active-background"),
            "Overlay appearance must be customizable with Browser Source CSS variables");
    require(html.contains("Geologica") && html.contains("assets/geologica.ttf"),
            "Overlay must use the bundled Geologica font by default");
    require(html.contains("#8b5cf6"), "Overlay must use the purple active-word preset by default");
    require(html.contains("colorParameter('activeBg'") && html.contains("parameters.get('font')"),
            "Overlay style must be configurable through the generated Browser Source URL");
    require(!html.contains("15vh"), "Overlay must not force a position inside the vertical canvas");

    const QByteArray designer = BrowserCaptionServer::build_designer_html();
    require(designer.contains("Конструктор субтитров"), "A visual overlay designer must be available in Russian");
    require(designer.contains("type=\"color\"") && designer.contains("Скопировать URL"),
            "Designer must provide visual color controls and a generated URL");
    require(designer.contains("value=\"#8b5cf6\"") && designer.contains("value=\"Geologica\""),
            "Designer must start with the requested purple and Geologica preset");

    BrowserOverlaySettings browser_settings;
    browser_settings.port = 0;
    browser_settings.access_token = std::string(64, 'a');
    BrowserCaptionServer server(browser_settings);
    require(server.is_listening(), "Browser caption server must listen on localhost");
    require(!server.has_browser_consumer(),
            "Browser captions must stay idle until an overlay requests caption events");
    bool consumer_activated = false;
    QObject::connect(
            &server,
            &BrowserCaptionServer::browser_consumer_presence_changed,
            [&](bool active) {
                consumer_activated = consumer_activated || active;
            });
    const BrowserOverlaySettings active_settings = server.settings();
    require(active_settings.port != 0, "Overlay server must report its selected local port");
    require(active_settings.access_token == browser_settings.access_token,
            "Overlay server must preserve its unguessable access token");
    const QUrl overlay_url(server.overlay_url());
    require(overlay_url.port() == active_settings.port &&
                    overlay_url.path() == QStringLiteral("/") +
                            QString::fromStdString(browser_settings.access_token) + QStringLiteral("/"),
            "Overlay URL must include its protected endpoint");

    const QByteArray protected_root =
            "/" + QByteArray::fromStdString(browser_settings.access_token) + "/";

    const QByteArray unauthorized_response = request_path(application, active_settings.port, "/");
    require(unauthorized_response.startsWith("HTTP/1.1 404 Not Found"),
            "An untrusted local process must not discover the overlay endpoint");

    const QByteArray page_response = request_path(application, active_settings.port, protected_root);
    require(page_response.startsWith("HTTP/1.1 200 OK"), "Overlay page must be available over HTTP");
    require(page_response.contains("AI Caption Plugin"), "Overlay HTTP response must contain the page");

    const QByteArray designer_response = request_path(
            application, active_settings.port, protected_root + "setup");
    require(designer_response.startsWith("HTTP/1.1 200 OK"), "Visual designer must be available over HTTP");
    require(designer_response.contains("Скопировать URL"), "Designer HTTP response must contain its controls");

    const QByteArray font_response = request_path(
            application, active_settings.port, protected_root + "assets/geologica.ttf");
    require(font_response.startsWith("HTTP/1.1 200 OK"), "Bundled Geologica font must be available over HTTP");
    require(font_response.contains("font/ttf"), "Bundled Geologica font must use the font content type");
    require(font_response.contains("max-age=31536000, immutable"),
            "Immutable overlay assets should use the browser cache");
    require(!server.has_browser_consumer(),
            "Opening the overlay, designer, or font must not start recognition without an event consumer");

    QTcpSocket event_client;
    event_client.connectToHost(QStringLiteral("127.0.0.1"), active_settings.port);
    require(event_client.waitForConnected(1000), "Event client must connect to the local overlay server");
    event_client.write(
            "GET " + protected_root +
            "events HTTP/1.1\r\nHost: 127.0.0.1\r\nAccept: text/event-stream\r\n\r\n");
    require(event_client.waitForBytesWritten(1000), "Event client must request the caption stream");
    QElapsedTimer event_timer;
    event_timer.start();
    QByteArray event_response;
    while (event_timer.elapsed() < 1000 && !event_response.contains("\n\ndata: ")) {
        application.processEvents(QEventLoop::AllEvents, 20);
        if (event_client.waitForReadyRead(20))
            event_response.append(event_client.readAll());
    }
    event_response.append(event_client.readAll());
    require(event_response.startsWith("HTTP/1.1 200 OK"), "Caption event stream must be available over HTTP");
    require(event_response.contains("text/event-stream"), "Caption event stream must use the SSE content type");
    require(server.has_browser_consumer(), "An open caption event stream must register an active consumer");
    require(consumer_activated, "An event stream must notify the caption manager to start recognition");

    const auto now = std::chrono::steady_clock::now();
    auto pushed_caption = std::make_shared<OutputCaptionResult>(
            CaptionResult(1, false, 0.7, "русский OBS", "", now, now),
            false);
    pushed_caption->output_line = "русский OBS";
    pushed_caption->clean_caption_text = "русский OBS";
    server.update_caption(pushed_caption, false);
    QByteArray pushed_event;
    event_timer.restart();
    while (event_timer.elapsed() < 1000 && !pushed_event.contains("русский OBS")) {
        application.processEvents(QEventLoop::AllEvents, 20);
        if (event_client.waitForReadyRead(20))
            pushed_event.append(event_client.readAll());
    }
    pushed_event.append(event_client.readAll());
    require(pushed_event.contains("русский OBS"),
            "Caption updates must be pushed without waiting for another HTTP request");

    event_client.disconnectFromHost();
    event_client.waitForDisconnected(1000);
    event_timer.restart();
    while (event_timer.elapsed() < 1000 && server.has_browser_consumer())
        application.processEvents(QEventLoop::AllEvents, 20);
    require(!server.has_browser_consumer(),
            "Recognition must return to idle immediately after the event consumer disconnects");

    const QByteArray removed_state_response = request_path(
            application, active_settings.port, protected_root + "state");
    require(removed_state_response.startsWith("HTTP/1.1 404 Not Found"),
            "The removed polling endpoint must not remain as a compatibility path");

    return 0;
}
