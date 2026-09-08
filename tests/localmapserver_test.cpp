#include "localmapserver.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTimer>
#include <QDebug>
#include <stdexcept>

static void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

// Exercises the real loopback HTTP transport, including split TCP requests.
static QByteArray exchange(quint16 port, const QByteArray &first, const QByteArray &second = {})
{
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QByteArray response;
    bool timedOut = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() { timedOut = true; loop.quit(); });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&]() { response += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&]() {
        socket.write(first);
        if (!second.isEmpty())
            QTimer::singleShot(10, &socket, [&]() { socket.write(second); });
    });
    socket.connectToHost(QHostAddress::LocalHost, port);
    timeout.start(3000);
    loop.exec();
    response += socket.readAll();
    check(!timedOut, "HTTP request timed out");
    return response;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        LocalMapServer server;
        check(server.start(":/map/gaode.html"), "Bundled map startup failed");
        check(server.serverAddress() == QHostAddress::LocalHost, "Must bind loopback only");
        check(server.pageUrl().port() == server.serverPort(), "Page URL port mismatch");
        QFile page(":/map/gaode.html");
        check(page.open(QIODevice::ReadOnly), "Bundled resource missing");
        const auto expected = page.readAll();
        auto response = exchange(server.serverPort(), "GET /gaode.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
        check(response.startsWith("HTTP/1.1 200 OK"), "GET failed");
        check(response.mid(response.indexOf("\r\n\r\n") + 4) == expected, "Map content differs");
        check(response.contains("Content-Length: " + QByteArray::number(expected.size())), "Incorrect UTF-8 byte length");
        check(response.contains("Cache-Control: no-store"), "Map must not become stale in cache");
        response = exchange(server.serverPort(), "HEAD /gaode.html?v=2 HTTP/1.1\r\n\r\n");
        check(response.startsWith("HTTP/1.1 200 OK") && response.endsWith("\r\n\r\n"), "HEAD must return no body");
        response = exchange(server.serverPort(), "GET / HTTP/1.1\r\nHost:", " localhost\r\n\r\n");
        check(response.endsWith(expected), "Fragmented request failed");
        for (const QByteArray &path : {QByteArray("/main.cpp"), QByteArray("/../gaode.html"), QByteArray("/%2e%2e/gaode.html")}) {
            response = exchange(server.serverPort(), "GET " + path + " HTTP/1.1\r\n\r\n");
            check(response.startsWith("HTTP/1.1 404"), "Unexpected file exposure");
        }
        response = exchange(server.serverPort(), "POST /gaode.html HTTP/1.1\r\n\r\n");
        check(response.startsWith("HTTP/1.1 405"), "Unsupported method accepted");
        response = exchange(server.serverPort(), "BAD\r\n\r\n");
        check(response.startsWith("HTTP/1.1 400"), "Malformed request accepted");
        response = exchange(server.serverPort(), QByteArray(8192, 'x'));
        check(response.startsWith("HTTP/1.1 431"), "Oversized headers accepted");

        // Represents another application or the user's previously started server.bat.
        QTcpServer occupied;
        occupied.setProxy(QNetworkProxy::NoProxy);
        check(occupied.listen(QHostAddress::LocalHost, 0), "Could not reserve test port");
        quint16 releasedPort;
        {
            LocalMapServer second;
            check(second.start(":/map/gaode.html", occupied.serverPort()), "Port fallback failed");
            check(second.serverPort() != occupied.serverPort(), "Occupied port reused");
            check(occupied.isListening(), "Existing service was interrupted");
            releasedPort = second.serverPort();
            check(exchange(releasedPort, "GET /gaode.html HTTP/1.1\r\n\r\n").endsWith(expected), "Fallback port does not serve map");
        }
        QTcpServer reopened;
        reopened.setProxy(QNetworkProxy::NoProxy);
        check(reopened.listen(QHostAddress::LocalHost, releasedPort), "Server exit did not release its port");

        QTemporaryFile customPage;
        check(customPage.open(), "Temporary page creation failed");
        customPage.write("<!doctype html><title>Custom map</title>");
        customPage.flush();
        LocalMapServer custom;
        check(custom.start(customPage.fileName(), 0), "External map startup failed");
        check(exchange(custom.serverPort(), "GET /gaode.html HTTP/1.1\r\n\r\n").endsWith("<title>Custom map</title>"), "External map not served");
        LocalMapServer missing;
        check(!missing.start(":/map/missing.html", 0) && !missing.isListening(), "Missing page should fail clearly");
        qInfo() << "PASS: bundled/external map, HTTP, fragmentation, loopback, port conflict and shutdown checks";
        return 0;
    } catch (const std::exception &error) {
        qCritical() << "FAIL:" << error.what();
        return 1;
    }
}
