#ifndef MULTICONTROLLER_H
#define MULTICONTROLLER_H

#include <QObject>
#include <QMap>
#include <QtGlobal>
#include <QSerialPort>
#include "SerialPortManager.h"   // 注意：这里包含你写好的 SerialPortManager.h
#include "MAVLinkParser.h"       // 注意：这里包含你写好的 MAVLinkParser.h
#include <mavlink/ardupilotmega/ardupilotmega.h>
#include <mavlink/common/mavlink.h>
#include "RtkClient.h"           // 新增：RTK 客户端


class MultiController : public QObject
{
    Q_OBJECT
public:
    // 构造函数：接收 SerialPortManager* 和 MAVLinkParser*
    explicit MultiController(SerialPortManager *serialManager,
                             MAVLinkParser *parser,
                             RtkClient *rtkClient,
                             QObject *parent = nullptr);

    // Q_INVOKABLE 方便在 QML 中直接调用
    Q_INVOKABLE void takeoffAll(float altitude);
    Q_INVOKABLE void landAll();
    Q_INVOKABLE void lockAll();



    // 检查串口是否打开
    bool isSerialPortOpen();
    // 自定义发送 MAVLink 消息（这里只是示例，使用逗号分隔）
    Q_INVOKABLE void sendCustomMavlinkMessage(quint8 target_system, quint8 target_component, quint16 command,
                                  float param1, float param2, float param3, float param4,
                                  float param5, float param6, float param7);

signals:
    // 用于反馈状态信息到 QML
    void statusMessage(const QString &msg);
    //转发id到编队类
    void droneSystemIdAvailable(uint8_t sysid);


private slots:
    // 当检测到新的无人机系统ID时自动添加
    void onMessageParsed(uint8_t sysid);
    void onRtcmReady(const QByteArray &rtcm);  // 新增：RTCM 回调


private:
    // 自动添加无人机到内部容器
    void addDrone(quint8 systemId);

    quint8 m_rtcmSeq = 0;           ///< 5 bit 序号，0-31 循环
    void sendGpsRtcmData(uint8_t seq,
                           uint8_t subSeq,
                           bool    fragmented,
                           const QByteArray &chunk);
    RtkClient         *m_rtkClient;            // 新增


    // 这里存的是 SerialPortManager*，不是 QSerialPort*
    SerialPortManager *m_serialManager = nullptr;
    // MAVLinkParser，用于接收其发出的 newDroneSystemIdDetected 信号
    MAVLinkParser *m_mavLinkParser = nullptr;

    // 无人机ID列表
    QMap<quint8, bool> m_droneSystemIds;
};

#endif // MULTICONTROLLER_H
