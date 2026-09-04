#include "MultiController.h"
#include <QDebug>
#include <QThread>

MultiController::MultiController(SerialPortManager *serialManager,
                                 MAVLinkParser *parser,
                                 RtkClient *rtkClient,
                                 QObject *parent)
    : QObject(parent),
    m_serialManager(serialManager),
    m_mavLinkParser(parser),
    m_rtkClient(rtkClient)  // 初始化
{
    // 连接 MAVLinkParser 的信号到 onMessageParsed 槽
    if (m_mavLinkParser) {
        // 你的 MAVLinkParser.h 里假设有 signal:
        //   void newDroneSystemIdDetected(uint8_t sysid, uint16_t msgid);
        connect(m_mavLinkParser,
                &MAVLinkParser::newDroneSystemIdDetected,
                this,
                &MultiController::onMessageParsed);
        connect(m_rtkClient,
                &RtkClient::rtkDataReady,
                this,
                &MultiController::onRtcmReady);
    }
}

void MultiController::addDrone(quint8 systemId)
{
    if (m_droneSystemIds.contains(systemId)) {
        // 如果这个 sysid 已存在，就不重复添加
        return;
    }
    m_droneSystemIds[systemId]= true;
    // emit statusMessage(QString("自动添加无人机ID %1").arg(systemId));
}

bool MultiController::isSerialPortOpen()
{
    // 取决于你 SerialPortManager 的写法，一般会有 isPortOpen()
    if (!m_serialManager || !m_serialManager->isPortOpen()) {
        emit statusMessage("串口未打开");
        return false;
    }
    return true;
}

void MultiController::sendCustomMavlinkMessage(quint8 target_system,
                                               quint8 target_component,
                                               quint16 command,
                                               float param1, float param2, float param3, float param4,
                                               float param5, float param6, float param7)
{
    // 检查串口是否已打开
    if (!isSerialPortOpen()) return;

    // 准备MAVLink消息，配置命令参数
    mavlink_message_t msg;
    mavlink_command_long_t cmd;
    cmd.target_system = target_system; // 目标无人机系统ID
    cmd.target_component = target_component; // 目标无人机组件ID
    cmd.command = command; // 指定的MAVLink命令
    cmd.confirmation = 0; // 不需要确认
    cmd.param1 = param1; // 命令参数1
    cmd.param2 = param2; // 命令参数2
    cmd.param3 = param3; // 命令参数3
    cmd.param4 = param4; // 命令参数4
    cmd.param5 = param5; // 命令参数5
    cmd.param6 = param6; // 命令参数6
    cmd.param7 = param7; // 命令参数7

    // 将命令编码为MAVLink消息
    mavlink_msg_command_long_encode(0, 0, &msg, &cmd);
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);
    QByteArray data(reinterpret_cast<const char*>(buffer), len);

    // 通过串口发送消息，并检查发送结果
    if (m_serialManager->isPortOpen()) {
        // 这里的 commandAckTimer 如果和第一个命令相同，可能需要你自行管理
        QMetaObject::invokeMethod(m_serialManager, "writeData", Q_ARG(QByteArray, data));
    } else {
        emit statusMessage("串口未打开，无法发送 secondCommand");
    }
}

void MultiController::takeoffAll(float altitude)
{
    if (!isSerialPortOpen()) return;


    for (auto sysid : m_droneSystemIds.keys()) {
        sendCustomMavlinkMessage(sysid, 1, MAV_CMD_NAV_TAKEOFF_LOCAL, 0, 0, 0, 0, 0, 0, altitude);
        qDebug() << "向无人机" << sysid << "发送起飞命令";
        QThread::msleep(25);
    }
    emit statusMessage("所有无人机起飞命令已发送");
}

void MultiController::landAll()
{
    if (!isSerialPortOpen()) return;

    for (auto sysid : m_droneSystemIds.keys()) {
        sendCustomMavlinkMessage(sysid, 1, MAV_CMD_NAV_RETURN_TO_LAUNCH, 0, 0, 0, 0, 0, 0, 0);
        qDebug() << "向无人机" << sysid << "发送降落命令";
        QThread::msleep(25);
    }
    emit statusMessage("所有无人机降落命令已发送");
}

void MultiController::lockAll()
{
    if (!isSerialPortOpen()) return;

    // 假设上锁命令是 MAV_CMD_COMPONENT_ARM_DISARM = 400, param1=0 表示上锁
    const quint16 MAV_CMD_COMPONENT_ARM_DISARM = 400;
    for (auto sysid : m_droneSystemIds.keys()) {
        sendCustomMavlinkMessage(sysid, 1, MAV_CMD_COMPONENT_ARM_DISARM, 0, 0, 0, 0, 0, 0, 0);
        qDebug() << "向无人机" << sysid << "发送上锁命令";
        QThread::msleep(25);
    }
    emit statusMessage("所有无人机上锁命令已发送");
}

// 槽函数：当 MAVLinkParser 解析到新的系统ID时调用
void MultiController::onMessageParsed(uint8_t sysid)
{
    // 如果 m_droneSystemIds 里还没有这个 sysid，就添加
    if (!m_droneSystemIds.contains(sysid)) {
        addDrone(sysid);
    }
    emit droneSystemIdAvailable(sysid); // 转发到编队

}




void MultiController::onRtcmReady(const QByteArray &rtcm)
{
    if (rtcm.isEmpty() || !isSerialPortOpen())   // <-- 删掉 m_droneSystemIds.isEmpty() 判断
        return;

    constexpr int MAX_LEN = MAVLINK_MSG_GPS_RTCM_DATA_FIELD_DATA_LEN; // 180B
    const bool  fragmented = rtcm.size() > MAX_LEN;
    quint8      seq        = m_rtcmSeq & 0x1F;                        // 5 bit

    for (int offset = 0, subSeq = 0;
         offset < rtcm.size();
         offset += MAX_LEN, ++subSeq)
    {
        const int len = qMin(MAX_LEN, rtcm.size() - offset);
        const QByteArray chunk = rtcm.mid(offset, len);

        // ✨ 只发一次，广播给所有飞控（sysid=255）
        sendGpsRtcmData(seq, subSeq, fragmented, chunk);
    }

    m_rtcmSeq = (m_rtcmSeq + 1) & 0x1F;          // 0-31 回卷
}



void MultiController::sendGpsRtcmData(uint8_t   seq,
                                          uint8_t   subSeq,
                                          bool      fragmented,
                                          const QByteArray &chunk)
{
    if (!isSerialPortOpen() || chunk.isEmpty())
        return;

    mavlink_gps_rtcm_data_t pkt {};
    pkt.flags = static_cast<uint8_t>((seq   & 0x1F) << 3 |
                                     (subSeq & 0x03) << 1 |
                                     (fragmented     ? 1 : 0));
    pkt.len   = static_cast<uint8_t>(chunk.size());
    memset(pkt.data, 0, sizeof(pkt.data));
    memcpy(pkt.data, chunk.constData(), chunk.size());

    mavlink_message_t msg;
    // ❶ sysid 固定写 255（广播），component 用 0
    mavlink_msg_gps_rtcm_data_encode(/*sysid*/255, /*comp*/0, &msg, &pkt);

    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    int bytes = mavlink_msg_to_send_buffer(buf, &msg);
    QByteArray out(reinterpret_cast<char*>(buf), bytes);

    QMetaObject::invokeMethod(m_serialManager, "writeData",
                              Qt::QueuedConnection,
                              Q_ARG(QByteArray, out));
}

