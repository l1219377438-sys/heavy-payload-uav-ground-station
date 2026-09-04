#ifndef RTKCLIENT_H
#define RTKCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

/*!
 * @brief 通过 NTRIP（TCP）连接 RTK Caster 并转发 RTCM 数据。
 *
 * 典型工作流程：
 *   1. setEndpoint(...)              // 设置主机、端口、挂载点
 *   2. setAuth(...)                  // 可选：设置 NTRIP 用户名/密码
 *   3. connectToCaster()             // 建立 TCP 连接并发送 NTRIP 请求
 *   4. 在 readyRead() 信号里接收 RTCM 数据
 *
 * 你可以直接把收到的字节流交给 GNSS/RTK 解算库（如 RTKLIB、PX4 GPS 驱动等）。
 */
class RtkClient : public QObject
{
    Q_OBJECT

public:
    explicit RtkClient(QObject *parent = nullptr);

    Q_INVOKABLE void setEndpoint(const QString &host, quint16 port, const QString &mountPoint);
    Q_INVOKABLE void setAuth(const QString &user, const QString &password);
    Q_INVOKABLE void connectToCaster();
    Q_INVOKABLE void disconnectFromCaster();

    // 设置 GGA 心跳位置信息
    Q_INVOKABLE void setGGAPosition(double lat, double lon, double alt = 0.0);
    Q_INVOKABLE void enableGGAHeartbeat(bool enable, int intervalMs = 10000);

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &err);      ///< 包含 QtSocket 错误描述
    void rtkDataReady(const QByteArray &data);   ///< 每当有 RTCM 数据到达
    void httpResponseReceived(const QString &response); ///< HTTP 响应信息

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError);
    void sendNtripRequest();
    void sendGGAHeartbeat();

private:
    QString calculateGGAChecksum(const QString &sentence);
    QString formatGGA();

    QString  m_host;
    quint16  m_port = 0;
    QString  m_mountPoint;
    QString  m_user;
    QString  m_password;

    // GGA 心跳相关
    double   m_latitude = 0.0;
    double   m_longitude = 0.0;
    double   m_altitude = 0.0;
    bool     m_ggaEnabled = false;

    QTcpSocket m_socket;
    QTimer     m_reconnectTimer;   ///< 简单断线重连
    QTimer     m_ggaTimer;         ///< GGA 心跳定时器

    bool       m_httpHeaderProcessed = false;  ///< 是否已处理 HTTP 响应头
    QByteArray m_httpBuffer;                   ///< HTTP 响应缓冲区
};

#endif // RTKCLIENT_H
