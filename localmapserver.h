#pragma once

#include <QTcpServer>
#include <QUrl>

// Only serves the single map page on loopback; lifetime follows the application.
class LocalMapServer : public QTcpServer
{
public:
    explicit LocalMapServer(QObject *parent = nullptr);
    bool start(const QString &pagePath, quint16 preferredPort = 8000);
    QUrl pageUrl() const;

private:
    QByteArray m_page;
};
