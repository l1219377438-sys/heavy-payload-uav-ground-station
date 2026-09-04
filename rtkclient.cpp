#include "rtkclient.h"
#include <QDateTime>
#include <QDebug>
#include <QStringList>

static QByteArray buildBasicAuth(const QString &user, const QString &pwd)
{
    QByteArray token = (user + ":" + pwd).toUtf8();
    return "Authorization: Basic " + token.toBase64() + "\r\n";
}

RtkClient::RtkClient(QObject *parent) : QObject(parent)
{
    // 连接 Qt socket 信号
    connect(&m_socket, &QTcpSocket::connected,
            this, &RtkClient::onConnected);
    connect(&m_socket, &QTcpSocket::readyRead,
            this, &RtkClient::onReadyRead);
    connect(&m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &RtkClient::onError);

    // 断线自动重连
    m_reconnectTimer.setInterval(3000);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout,
            this, &RtkClient::connectToCaster);

    // GGA 心跳定时器
    m_ggaTimer.setSingleShot(false);
    connect(&m_ggaTimer, &QTimer::timeout,
            this, &RtkClient::sendGGAHeartbeat);
}

void RtkClient::setEndpoint(const QString &host, quint16 port, const QString &mountPoint)
{
    m_host = host;
    m_port = port;
    m_mountPoint = mountPoint;
}

void RtkClient::setAuth(const QString &user, const QString &password)
{
    m_user = user;
    m_password = password;
}

void RtkClient::setGGAPosition(double lat, double lon, double alt)
{
    m_latitude = lat;
    m_longitude = lon;
    m_altitude = alt;
    // qDebug() << "[RTK] GGA Position set:" << lat << lon << alt;
}

void RtkClient::enableGGAHeartbeat(bool enable, int intervalMs)
{
    m_ggaEnabled = enable;
    if (enable) {
        m_ggaTimer.setInterval(intervalMs);
        if (m_socket.state() == QAbstractSocket::ConnectedState) {
            m_ggaTimer.start();
        }
        qDebug() << "[RTK] GGA heartbeat enabled, interval:" << intervalMs << "ms";
    } else {
        m_ggaTimer.stop();
        qDebug() << "[RTK] GGA heartbeat disabled";
    }
}

void RtkClient::connectToCaster()
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }

    // 重置状态
    m_httpHeaderProcessed = false;
    m_httpBuffer.clear();

    qDebug() << "[RTK] Connecting to" << m_host << m_port << "mount:" << m_mountPoint;
    m_socket.connectToHost(m_host, m_port);
}

void RtkClient::disconnectFromCaster()
{
    m_ggaTimer.stop();
    m_socket.disconnectFromHost();
}

void RtkClient::onConnected()
{
    qDebug() << "[RTK] TCP connected, sending NTRIP request.";
    sendNtripRequest();
    emit connected();
}

void RtkClient::sendNtripRequest()
{
    // 构造 HTTP/1.1 请求，保持长连接
    QString req =
        "GET /" + m_mountPoint + " HTTP/1.1\r\n"
                                 "Host: " + m_host + ":" + QString::number(m_port) + "\r\n"
                                                   "User-Agent: NTRIP-QtClient/1.0\r\n"
                                                   "Accept: */*\r\n"
                                                   "Connection: keep-alive\r\n";

    if (!m_user.isEmpty()) {
        req += buildBasicAuth(m_user, m_password);
    }
    req += "\r\n";

    qDebug() << "[RTK] Sending NTRIP request:";
    qDebug().noquote() << req;

    m_socket.write(req.toUtf8());
    m_socket.flush();
}

void RtkClient::onReadyRead()
{
    QByteArray data = m_socket.readAll();
    if (data.isEmpty()) {
        return;
    }

    // 如果还没处理 HTTP 响应头
    if (!m_httpHeaderProcessed) {
        m_httpBuffer.append(data);

        qDebug() << "[RTK] Received data, buffer size:" << m_httpBuffer.size();
        qDebug() << "[RTK] Raw data (hex):" << m_httpBuffer.toHex(' ');
        qDebug() << "[RTK] Raw data (text):" << QString::fromLatin1(m_httpBuffer);

        // 查找 HTTP 响应头结束标志 \r\n\r\n 或 \n\n 或单独的 \r\n (对于简单的ICY响应)
        int headerEndPos = m_httpBuffer.indexOf("\r\n\r\n");
        if (headerEndPos == -1) {
            headerEndPos = m_httpBuffer.indexOf("\n\n");
            if (headerEndPos != -1) {
                headerEndPos += 2; // \n\n 的长度
            }
        } else {
            headerEndPos += 4; // \r\n\r\n 的长度
        }

        // 对于简单的 ICY 响应，可能只有一行
        if (headerEndPos == -1 && m_httpBuffer.contains("ICY 200 OK")) {
            int simpleEndPos = m_httpBuffer.indexOf("\r\n");
            if (simpleEndPos != -1 && simpleEndPos + 2 == m_httpBuffer.size()) {
                // 这是一个简单的 ICY 200 OK\r\n 响应
                headerEndPos = simpleEndPos + 2;
            }
        }

        // 如果找到了响应头结束标志，或者数据看起来是简单的 ICY 响应
        if (headerEndPos != -1) {
            // 提取 HTTP/ICY 响应头
            int headerLength = headerEndPos;
            if (m_httpBuffer.mid(headerEndPos-4, 4) == "\r\n\r\n") {
                headerLength = headerEndPos - 4;
            } else if (m_httpBuffer.mid(headerEndPos-2, 2) == "\n\n") {
                headerLength = headerEndPos - 2;
            } else if (m_httpBuffer.mid(headerEndPos-2, 2) == "\r\n") {
                headerLength = headerEndPos - 2;
            }

            QByteArray httpHeader = m_httpBuffer.left(headerLength);
            QString headerStr = QString::fromUtf8(httpHeader);

            qDebug() << "[RTK] HTTP Response Header:";
            qDebug().noquote() << headerStr;

            emit httpResponseReceived(headerStr);

            // 检查 HTTP/ICY 状态码
            if (headerStr.contains("200 OK") || headerStr.contains("HTTP/1.1 200") ||
                headerStr.contains("HTTP/1.0 200") || headerStr.contains("ICY 200")) {
                qDebug() << "[RTK] ICY/HTTP 200 OK - Connection established";
                m_httpHeaderProcessed = true;

                // 启动 GGA 心跳
                if (m_ggaEnabled) {
                    m_ggaTimer.start();
                    // 立即发送一次 GGA
                    sendGGAHeartbeat();
                }

                // 处理响应头后剩余的数据（如果有的话）
                QByteArray remainingData = m_httpBuffer.mid(headerEndPos);
                if (!remainingData.isEmpty()) {
                    emit rtkDataReady(remainingData);
                    qDebug() << "[RTK DATA]" << QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
                             << "bytes=" << remainingData.size();
                }
            } else {
                qWarning() << "[RTK] HTTP Error Response - Connection failed";
                if (headerStr.contains("401")) {
                    emit errorOccurred("HTTP 401 Unauthorized - Check username/password");
                } else if (headerStr.contains("404")) {
                    emit errorOccurred("HTTP 404 Not Found - Check mount point");
                } else if (headerStr.contains("403")) {
                    emit errorOccurred("HTTP 403 Forbidden - Access denied");
                } else {
                    emit errorOccurred("HTTP Error: " + headerStr.split('\n').first());
                }
                m_socket.abort();
                return;
            }

            m_httpBuffer.clear();
        } else {
            // 检查是否可能不是标准 HTTP 响应
            if (m_httpBuffer.size() >= 12) {
                // 如果 12 字节后还没找到响应头结束符，可能是错误响应或直接的 RTCM 数据
                if (!m_httpBuffer.startsWith("HTTP/") && !m_httpBuffer.startsWith("ICY")) {
                    qDebug() << "[RTK] Non-HTTP response detected, treating as error";
                    QString errorMsg = QString::fromLatin1(m_httpBuffer);
                    emit errorOccurred("Server response: " + errorMsg);
                    m_socket.abort();
                    return;
                } else if (m_httpBuffer.size() == 12 && !m_httpBuffer.contains('\n')) {
                    // 可能是被截断的 HTTP 响应，等待更多数据
                    qDebug() << "[RTK] Waiting for more HTTP response data...";
                    // 设置一个超时，如果 2 秒内没有更多数据就认为是错误
                    QTimer::singleShot(2000, this, [this]() {
                        if (!m_httpHeaderProcessed && m_httpBuffer.size() <= 12) {
                            qWarning() << "[RTK] HTTP response timeout, treating as error";
                            QString errorMsg = QString::fromLatin1(m_httpBuffer);
                            emit errorOccurred("Incomplete HTTP response: " + errorMsg);
                            m_socket.abort();
                        }
                    });
                }
            }

            if (m_httpBuffer.size() > 4096) {
                // 防止恶意的超长响应头
                qWarning() << "[RTK] HTTP header too long, aborting";
                emit errorOccurred("HTTP header too long");
                m_socket.abort();
            }
        }
    } else {
        // HTTP 响应头已处理，这是 RTCM 数据
        emit rtkDataReady(data);
        qDebug() << "[RTK DATA]" << QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
                 << "bytes=" << data.size();
    }
}

void RtkClient::sendGGAHeartbeat()
{
    if (!m_ggaEnabled || m_socket.state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QString gga = formatGGA();
    m_socket.write(gga.toUtf8());
    m_socket.flush();

    qDebug() << "[RTK] heartbeat GGA sent:" << gga.trimmed();
}

QString RtkClient::formatGGA()
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    QString timeStr = now.toString("HHmmss.zzz");

    // 转换纬度格式 (度 -> 度分)
    double latDeg = qAbs(m_latitude);
    int latDegInt = (int)latDeg;
    double latMin = (latDeg - latDegInt) * 60.0;
    QString latStr = QString("%1%2").arg(latDegInt, 2, 10, QChar('0')).arg(latMin, 7, 'f', 4, QChar('0'));
    QString latDir = (m_latitude >= 0) ? "N" : "S";

    // 转换经度格式 (度 -> 度分)
    double lonDeg = qAbs(m_longitude);
    int lonDegInt = (int)lonDeg;
    double lonMin = (lonDeg - lonDegInt) * 60.0;
    QString lonStr = QString("%1%2").arg(lonDegInt, 3, 10, QChar('0')).arg(lonMin, 7, 'f', 4, QChar('0'));
    QString lonDir = (m_longitude >= 0) ? "E" : "W";

    // 构造 GGA 语句（不含校验码）
    QString ggaBase = QString::asprintf("GPGGA,%s,%s,%s,%s,%s,1,08,1.0,%.1f,M,50.0,M,,",
                                        timeStr.toUtf8().constData(),
                                        latStr.toUtf8().constData(),
                                        latDir.toUtf8().constData(),
                                        lonStr.toUtf8().constData(),
                                        lonDir.toUtf8().constData(),
                                        m_altitude);


    // 计算并添加校验码
    QString checksum = calculateGGAChecksum(ggaBase);
    QString gga = "$" + ggaBase + "*" + checksum + "\r\n";

    return gga;
}

QString RtkClient::calculateGGAChecksum(const QString &sentence)
{
    quint8 checksum = 0;
    QByteArray data = sentence.toUtf8();

    for (char c : data) {
        checksum ^= static_cast<quint8>(c);
    }

    return QString("%1").arg(checksum, 2, 16, QChar('0')).toUpper();
}

void RtkClient::onError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)
    QString err = m_socket.errorString();
    qWarning() << "[RTK] Socket error:" << err;

    // 停止定时器
    m_ggaTimer.stop();

    emit errorOccurred(err);
    m_socket.abort();

    // 如果不是主动断开，则尝试重连
    if (socketError != QAbstractSocket::RemoteHostClosedError ||
        !err.contains("closed", Qt::CaseInsensitive)) {
        m_reconnectTimer.start();
    }

    emit disconnected();
}
