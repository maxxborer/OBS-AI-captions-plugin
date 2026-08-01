#include "TikTokCaptionServer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

QByteArray request_path(QCoreApplication &application, const QByteArray &path) {
    QTcpSocket client;
    client.connectToHost(QStringLiteral("127.0.0.1"), TikTokCaptionServer::port);
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

    const QByteArray json = TikTokCaptionServer::build_state_json(
            QString::fromUtf8("быстрые слова OBS"), false, 42);
    const QJsonObject state = QJsonDocument::fromJson(json).object();
    require(state.value("text").toString() == QString::fromUtf8("быстрые слова OBS"),
            "State must preserve Russian and English text");
    require(!state.value("final").toBool(), "Partial state must remain partial");
    require(state.value("revision").toString() == QStringLiteral("42"),
            "Revision must be serialized without numeric precision loss");

    const QByteArray html = TikTokCaptionServer::build_overlay_html();
    require(html.contains("slice(-8)"), "Overlay must limit the vertical caption to eight words");
    require(html.contains("index === words.length - 1") && html.contains("' active'"),
            "Overlay must mark the latest partial word as active");
    require(html.contains("textContent = word"), "Overlay must render recognized text without HTML injection");
    require(html.contains("setInterval(refresh, 70)"), "Overlay must refresh quickly enough for live captions");

    TikTokCaptionServer server;
    require(server.is_listening(), "TikTok caption server must listen on localhost");
    require(server.overlay_url() == QStringLiteral("http://127.0.0.1:37545/"),
            "Overlay URL must remain stable for the OBS Browser Source");

    const QByteArray page_response = request_path(application, "/");
    require(page_response.startsWith("HTTP/1.1 200 OK"), "Overlay page must be available over HTTP");
    require(page_response.contains("AI Caption Plugin"), "Overlay HTTP response must contain the page");

    const QByteArray state_response = request_path(application, "/state");
    require(state_response.startsWith("HTTP/1.1 200 OK"), "Caption state must be available over HTTP");
    require(state_response.contains("application/json"), "Caption state must have the JSON content type");

    return 0;
}
