#include "localmapserver.h"

#include <QFile>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTimer>

LocalMapServer::LocalMapServer(QObject *parent) : QTcpServer(parent)
{
    setProxy(QNetworkProxy::NoProxy);
    connect(this, &QTcpServer::newConnection, this, [this]() {
        while (hasPendingConnections()) {
            auto *socket = nextPendingConnection();
            socket->setReadBufferSize(8192);
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(5000, socket, [socket]() { socket->abort(); socket->deleteLater(); });
            connect(socket, &QTcpSocket::readyRead, socket,
                    [this, socket, buffer = QByteArray(), responded = false]() mutable {
                if (responded)
                    return;
                buffer += socket->readAll();
                const auto headerEnd = buffer.indexOf("\r\n\r\n");
                if (headerEnd < 0 && buffer.size() < 8192)
                    return;
                responded = true;
                QByteArray code = "200 OK";
                QByteArray body = m_page;
                QByteArray type = "text/html; charset=utf-8";
                const auto request = buffer.left(buffer.indexOf("\r\n")).split(' ');
                const bool head = !request.isEmpty() && request[0] == "HEAD";
                if (headerEnd < 0 || headerEnd > 8192) {
                    code = "431 Request Header Fields Too Large";
                    body = "Request headers too large";
                } else if (request.size() != 3 ||
                           (request[2] != "HTTP/1.1" && request[2] != "HTTP/1.0")) {
                    code = "400 Bad Request";
                    body = "Bad request";
                } else if (request[0] != "GET" && !head) {
                    code = "405 Method Not Allowed";
                    body = "Only GET and HEAD are supported";
                } else {
                    const auto path = request[1].split('?').first();
                    if (path != "/" && path != "/gaode.html") {
                        code = "404 Not Found";
                        body = "Not found";
                    }
                }
                if (code != "200 OK")
                    type = "text/plain; charset=utf-8";
                QByteArray response = "HTTP/1.1 " + code + "\r\nContent-Type: " + type
                    + "\r\nContent-Length: " + QByteArray::number(body.size())
                    + "\r\nCache-Control: no-store\r\nConnection: close\r\n";
                if (code.startsWith("405"))
                    response += "Allow: GET, HEAD\r\n";
                response += "\r\n";
                if (!head)
                    response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });
}

bool LocalMapServer::start(const QString &pagePath, quint16 preferredPort)
{
    if (isListening())
        return true;
    QFile file(pagePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    m_page = file.readAll();
    if (m_page.isEmpty() || file.error() != QFileDevice::NoError)
        return false;
    return listen(QHostAddress::LocalHost, preferredPort)
        || (preferredPort != 0 && listen(QHostAddress::LocalHost, 0));
}

QUrl LocalMapServer::pageUrl() const
{
    if (!isListening())
        return {};
    return QUrl(QStringLiteral("http://127.0.0.1:%1/gaode.html").arg(serverPort()));
}
