#include "MAVLinkParser.h"
#include <QtMath>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>


MAVLinkParser::MAVLinkParser(SerialPortManager *manager, QObject *parent)
    : QObject(parent),
    m_selectedSystemId(1),
    m_serialPortManager(manager),
    m_gaodeKey(QStringLiteral("a7c9f09966ffb2c876ef7215fb60be8d")) //高德api
{
    memset(&status, 0, sizeof(mavlink_status_t));

    // 使用传入的 SerialPortManager 连接信号
    connect(m_serialPortManager, &SerialPortManager::dataReceived,
            this, &MAVLinkParser::onSerialDataReceived);
    m_netManager = new QNetworkAccessManager(this);

}

int MAVLinkParser::selectedSystemId() const
{
    return m_selectedSystemId;
}

void MAVLinkParser::setSelectedSystemId(int systemId)
{
    if (m_selectedSystemId != systemId) {
        m_selectedSystemId = systemId;
        emit selectedSystemIdChanged();
    }
}

void MAVLinkParser::parseMAVLinkMessage(const QByteArray &data)
{
    int mavlinkChannel = MAVLINK_COMM_0;

    for (int i = 0; i < data.size(); ++i) {
        uint8_t byte = static_cast<uint8_t>(data.at(i));
        if (mavlink_parse_char(mavlinkChannel, byte, &msg, &status)) {
            // 只转发 Mission 相关的消息给 MissionPlanner
            switch (msg.msgid) {
            case MAVLINK_MSG_ID_MISSION_COUNT:        // 44: 总数
            case MAVLINK_MSG_ID_MISSION_REQUEST_INT:  // 51: 请求某条
            case MAVLINK_MSG_ID_MISSION_ITEM_INT:     // 73: 整型项
            case MAVLINK_MSG_ID_MISSION_ITEM:         // 39: 浮点项
                emit missionMessageReceived(msg);
                break;
            case MAVLINK_MSG_ID_MISSION_ACK:          // 47: 结束 ACK
                qDebug() << "[Parser] 收到 MISSION_ACK msgid=" <<  msg.msgid<< "系统id"<< msg.sysid;
                emit missionMessageReceived(msg);
                break;
            // 如需兼容旧固件，可额外保留：
            // case MAVLINK_MSG_ID_MISSION_REQUEST:   // 40: 浮点请求
            // case MAVLINK_MSG_ID_MISSION_CLEAR_ALL: // 45: 清空所有
            default:
                break;
            }

            // 其他非 Mission 的 MAVLink 消息继续处理
            emit newDroneSystemIdDetected(msg.sysid);
            switch (msg.msgid) {
            case MAVLINK_MSG_ID_COMMAND_ACK:
                __handleCommandAck(msg);
                break;
            case MAVLINK_MSG_ID_ATTITUDE:
                __handleAttitude(msg);
                break;
            case MAVLINK_MSG_ID_BATTERY_STATUS:
                __handleBatteryStatus(msg);
                break;
            case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
                __handleGlobalPositionInt(msg);
                break;
            case MAVLINK_MSG_ID_STATUSTEXT:
                __handleStatustext(msg);
                break;
            case MAVLINK_MSG_ID_GPS_RAW_INT:
                __handleGPSRawInt(msg);
                break;
            case MAVLINK_MSG_ID_HEARTBEAT:
                __handleHeartbeat(msg);
                break;
            case MAVLINK_MSG_ID_GPS_RTK:               // 新增：监听 RTK 状态
                 __handleGpsRtk(msg);
                break;
            default:
                break;
            }
        }
    }
}


void MAVLinkParser::onSerialDataReceived(const QByteArray &data)
{
    parseMAVLinkMessage(data);
}

// 以下各个函数示例保持不变

void MAVLinkParser::__handleAttitude(const mavlink_message_t &msg)
{
    mavlink_attitude_t attitude;
    mavlink_msg_attitude_decode(&msg, &attitude);

    double roll = attitude.roll * 180.0 / M_PI;
    double pitch = attitude.pitch * 180.0 / M_PI;
    double yaw = attitude.yaw * 180.0 / M_PI;
    // 过滤掉不是当前选中飞机的消息
    if (msg.sysid == m_selectedSystemId) {
        emit newAttitudeData(msg.sysid, roll, pitch, yaw);
    }
}


void MAVLinkParser::__handleBatteryStatus(const mavlink_message_t &msg)
{
    mavlink_battery_status_t batteryStatus;
    mavlink_msg_battery_status_decode(&msg, &batteryStatus);
    double batteryVoltage = batteryStatus.voltages[0] / 1000.0;
    // 过滤掉非当前选中飞机的消息
    if (msg.sysid == m_selectedSystemId) {
        emit newBatteryData(msg.sysid, batteryVoltage);
    }
}

void MAVLinkParser::__handleGlobalPositionInt(const mavlink_message_t &msg)
{
    mavlink_global_position_int_t globalPosition;
    mavlink_msg_global_position_int_decode(&msg, &globalPosition);

    double wgsLatitude  = globalPosition.lat / 1E7;
    double wgsLongitude = globalPosition.lon / 1E7;
    double altitude     = globalPosition.alt / 1000.0;
    double relAlt       = globalPosition.relative_alt / 1000.0;

    // 转换
    double newLat = 0.0, newLon = 0.0;
    wgs84ToGcj02(wgsLatitude, wgsLongitude, newLat, newLon);

    // 如果调用被限流、出错或返回 0，就保留上次数据
    double gcjLat, gcjLon;
    if (newLat == 0.0 && newLon == 0.0) {
        // 没有新值，取上次保存的，若不存在则用原 GPS
        auto prev = m_lastGcj02Coords.value(msg.sysid,
                                            qMakePair(wgsLatitude, wgsLongitude));
        gcjLat = prev.first;
        gcjLon = prev.second;
    } else {
        // 有新值，更新缓存
        gcjLat = newLat;
        gcjLon = newLon;
        m_lastGcj02Coords[msg.sysid] = qMakePair(gcjLat, gcjLon);
    }

    // 发射信号
    emit dronesPositionUpdated(msg.sysid, gcjLat, gcjLon, altitude);
    emit wgsPositionUpdated(msg.sysid,wgsLatitude,wgsLongitude,altitude);
    // qDebug()<<msg.sysid;

    if (msg.sysid == m_selectedSystemId) {
        emit newGlobalPositionData(msg.sysid,
                                   gcjLat, gcjLon,
                                   altitude, relAlt);
    }
    if (msg.sysid == 1) {
        emit systemIdOnePosition(gcjLat, gcjLon, altitude);
        emit systemIdOnePositionwgs(wgsLatitude, wgsLongitude, altitude);
    }
}

// void MAVLinkParser::__handleGlobalPositionInt(const mavlink_message_t &msg)
// {
//     mavlink_global_position_int_t globalPosition;
//     mavlink_msg_global_position_int_decode(&msg, &globalPosition);

//     double wgsLatitude = globalPosition.lat / 1E7;
//     double wgsLongitude = globalPosition.lon / 1E7;
//     double altitude = globalPosition.alt / 1000.0;
//     double relativeAltitude = globalPosition.relative_alt / 1000.0; // 相对高度（米）


//     double latitude, longitude;
//     wgs84ToGcj02(wgsLatitude, wgsLongitude, latitude, longitude);
//     emit dronesPositionUpdated(msg.sysid, latitude, longitude, altitude);

//     if (msg.sysid == m_selectedSystemId) {
//         emit newGlobalPositionData(msg.sysid, latitude, longitude, altitude, relativeAltitude);//高德坐标
//         // emit newGlobalPositionDataWGS(msg.sysid, wgsLatitude, wgsLongitude, wgsLongitude);//gps坐标
//     }
//     if (msg.sysid == 1) {
//         emit systemIdOnePosition(latitude, longitude, altitude);//高德 显示用
//         emit systemIdOnePositionwgs(wgsLatitude, wgsLongitude, altitude);//gps 计算用

//     }
// }

void MAVLinkParser::__handleCommandAck(const mavlink_message_t &msg)
{
    mavlink_command_ack_t commandAck;
    mavlink_msg_command_ack_decode(&msg, &commandAck);
    uint16_t command = commandAck.command;
    uint8_t result = commandAck.result;
    qDebug() << "SysID:" << msg.sysid << "Command:" << command << "Result:" << result;
}

void MAVLinkParser::__handleStatustext(const mavlink_message_t &msg)
{
    mavlink_statustext_t statusText;
    mavlink_msg_statustext_decode(&msg, &statusText);
    QString text = QString::fromUtf8((char*)statusText.text);
}

void MAVLinkParser::__handleGPSRawInt(const mavlink_message_t &msg)
{
    mavlink_gps_raw_int_t gpsRawInt;
    mavlink_msg_gps_raw_int_decode(&msg, &gpsRawInt);

    int satellitesVisible = gpsRawInt.satellites_visible;
    uint8_t fixType = gpsRawInt.fix_type;
    uint16_t eph = gpsRawInt.eph;  // 水平精度估算 * 100 (cm)
    uint16_t epv = gpsRawInt.epv;  // 垂直精度估算 * 100 (cm)

    // GPS定位类型解析
    QString fixTypeStr;
    switch (fixType) {
    case 0: fixTypeStr = "无定位"; break;
    case 1: fixTypeStr = "单点定位"; break;
    case 2: fixTypeStr = "DGPS"; break;
    case 3: fixTypeStr = "3D定位"; break;
    case 4: fixTypeStr = "RTK固定解"; break;  // 最高精度
    case 5: fixTypeStr = "RTK浮点解"; break; // RTK工作中
    case 6: fixTypeStr = "惯性导航"; break;
    default: fixTypeStr = QString("未知(%1)").arg(fixType); break;
    }

    // 打印详细的GPS状态
    qDebug() << "[GPS_RAW_INT] 系统ID:" << msg.sysid
             << "定位类型:" << fixTypeStr << "(" << fixType << ")"
             << "卫星数:" << satellitesVisible
             << "水平精度:" << (eph == UINT16_MAX ? "未知" : QString("%1cm").arg(eph))
             << "垂直精度:" << (epv == UINT16_MAX ? "未知" : QString("%1cm").arg(epv));

    // 判断RTK是否工作
    if (fixType == 4) {
        qDebug() << "[RTK] ✓ RTK固定解 - 厘米级精度定位";
    } else if (fixType == 5) {
        qDebug() << "[RTK] ⚡ RTK浮点解 - 正在收敛到固定解";
    } else if (fixType >= 2) {
        qDebug() << "[GPS] 标准GPS定位 - 米级精度";
    } else {
        qDebug() << "[GPS] ✗ GPS定位异常";
    }

    // 发射信号给UI
    emit newSatelliteData(msg.sysid, satellitesVisible);
}


void MAVLinkParser::__handleHeartbeat(const mavlink_message_t &msg)
{
    mavlink_heartbeat_t heartbeat;
    mavlink_msg_heartbeat_decode(&msg, &heartbeat);

    // 只处理选中的飞机
    if (msg.sysid == m_selectedSystemId) {
        uint32_t custom_mode = heartbeat.custom_mode;
        uint8_t base_mode = heartbeat.base_mode;
        uint8_t system_status = heartbeat.system_status;

        int mainMode = (custom_mode >> 16) & 0xFF;   // 主模式
        int subMode  = (custom_mode >> 24) & 0xFF;   // 子模式

        bool isArmed = (base_mode & MAV_MODE_FLAG_SAFETY_ARMED); // 是否解锁

        // 打印输出或发送信号给上层
        // qDebug() << "Heartbeat - MainMode:" << mainMode << "SubMode:" << subMode << "Armed:" << isArmed;

        // emit newFlightModeData(msg.sysid, mainMode, subMode, isArmed);
    }
}

// void MAVLinkParser::wgs84ToGcj02(double wgsLat, double wgsLon,
//                                  double &gcjLat, double &gcjLon)
// {
//     // —— 简单限流：每秒最多 2 次 ——
//     static QElapsedTimer  s_timer;
//     static int            s_count = 0;
//     if (!s_timer.isValid() || s_timer.elapsed() > 1000) {
//         s_timer.restart();
//         s_count = 0;
//     }
//     if (++s_count > 2) {
//         // qWarning() << "[wgs84ToGcj02] QPS 限流（>2/秒），跳过网络调用";
//         return;
//     }

//     // —— 构造 7 位小数的 locations 字符串 ——
//     QString lonStr = QString::number(wgsLon, 'f', 7);
//     QString latStr = QString::number(wgsLat, 'f', 7);
//     QString locStr = QString("%1,%2").arg(lonStr).arg(latStr);

//     // —— 构造 URL 并打印 ——
//     QUrl url("https://restapi.amap.com/v3/assistant/coordinate/convert");
//     QUrlQuery query;
//     query.addQueryItem("key",       m_gaodeKey);
//     query.addQueryItem("locations", locStr);
//     query.addQueryItem("coordsys",  "gps");
//     query.addQueryItem("output",    "JSON");
//     url.setQuery(query);

//     // qDebug() << "[wgs84ToGcj02] Request URL:" << url.toString();

//     // —— 发起同步 GET 请求 ——
//     QNetworkReply *reply = m_netManager->get(QNetworkRequest(url));
//     QEventLoop     loop;
//     connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
//     loop.exec();

//     // —— 读取并打印回复 ——
//     QByteArray body = reply->readAll();
//     // qDebug() << "[wgs84ToGcj02] Reply body:" << body;

//     // —— 解析 JSON ——
//     if (reply->error() == QNetworkReply::NoError) {
//         QJsonObject obj = QJsonDocument::fromJson(body).object();
//         if (obj.value("status").toString() == QLatin1String("1")) {
//             QStringList list = obj.value("locations").toString().split(',');
//             if (list.size() == 2) {
//                 bool okLon = false, okLat = false;
//                 double lon = list.at(0).toDouble(&okLon);
//                 double lat = list.at(1).toDouble(&okLat);
//                 if (okLon && okLat) {
//                     gcjLon = lon;
//                     gcjLat = lat;
//                 } else {
//                     qWarning() << "[wgs84ToGcj02] 字符串转 double 失败";
//                 }
//             } else {
//                 qWarning() << "[wgs84ToGcj02] locations 格式异常";
//             }
//         } else {
//             qWarning() << "[wgs84ToGcj02] API 返回失败:"
//                        << obj.value("info").toString();
//         }
//     } else {
//         qWarning() << "[wgs84ToGcj02] 网络错误:"
//                    << reply->errorString();
//     }

//     reply->deleteLater();
// }
void MAVLinkParser::__handleGpsRtk(const mavlink_message_t &msg)
{
    mavlink_gps_rtk_t rtk;
    mavlink_msg_gps_rtk_decode(&msg, &rtk);

    // 判断RTK状态
    bool hasRtkData = (rtk.rtk_rate > 0);           // 是否有RTK数据流
    bool hasBaseline = (rtk.baseline_coords_type != 0); // 基线坐标类型
    bool isHealthy = (rtk.rtk_health == 0);         // RTK健康状态

    // 更详细的状态判断
    QString rtkStatus;
    if (hasRtkData && hasBaseline && isHealthy) {
        rtkStatus = "RTK正常工作 ✓";
    } else if (hasRtkData && !hasBaseline) {
        rtkStatus = "RTK数据接收中，等待基线建立...";
    } else if (hasRtkData && !isHealthy) {
        rtkStatus = "RTK数据异常 ✗";
    } else {
        rtkStatus = "未接收到RTK数据 ✗";
    }

    // 打印详细信息
    qDebug() << "[GPS_RTK] 系统ID:" << msg.sysid
             << "RTK频率:" << int(rtk.rtk_rate) << "Hz"
             << "基线类型:" << int(rtk.baseline_coords_type)
             << "健康状态:" << int(rtk.rtk_health)
             << "精度:" << rtk.accuracy << "mm"
             << "状态:" << rtkStatus;

}



void MAVLinkParser::wgs84ToGcj02(double wgsLat, double wgsLon, double &gcjLat, double &gcjLon)
{
    const double pi = 3.14159265358979323846;
    const double a = 6378245.0; // 地球长半轴
    const double ee = 0.00669342162296594323; // 偏心率平方

    auto transformLat = [pi](double x, double y) -> double {
        double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * std::sqrt(std::abs(x));
        ret += (20.0 * std::sin(6.0 * x * pi) + 20.0 * std::sin(2.0 * x * pi)) * 2.0 / 3.0;
        ret += (20.0 * std::sin(y * pi) + 40.0 * std::sin(y / 3.0 * pi)) * 2.0 / 3.0;
        ret += (160.0 * std::sin(y / 12.0 * pi) + 320 * std::sin(y * pi / 30.0)) * 2.0 / 3.0;
        return ret;
    };

    auto transformLon = [pi](double x, double y) -> double {
        double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * std::sqrt(std::abs(x));
        ret += (20.0 * std::sin(6.0 * x * pi) + 20.0 * std::sin(2.0 * x * pi)) * 2.0 / 3.0;
        ret += (20.0 * std::sin(x * pi) + 40.0 * std::sin(x / 3.0 * pi)) * 2.0 / 3.0;
        ret += (150.0 * std::sin(x / 12.0 * pi) + 300.0 * std::sin(x / 30.0 * pi)) * 2.0 / 3.0;
        return ret;
    };

    double dLat = transformLat(wgsLon - 105.0, wgsLat - 35.0);
    double dLon = transformLon(wgsLon - 105.0, wgsLat - 35.0);

    double radLat = wgsLat / 180.0 * pi;
    double magic = std::sin(radLat);
    magic = 1 - ee * magic * magic;
    double sqrtMagic = std::sqrt(magic);

    dLat = (dLat * 180.0) / ((a * (1 - ee)) / (magic * sqrtMagic) * pi);
    dLon = (dLon * 180.0) / (a / sqrtMagic * std::cos(radLat) * pi);

    gcjLat = wgsLat + dLat;
    gcjLon = wgsLon + dLon;
}
