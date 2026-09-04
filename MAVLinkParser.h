#ifndef MAVLINKPARSER_H
#define MAVLINKPARSER_H

#include <QObject>
#include <QByteArray>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <cstring>
#include <mavlink/common/mavlink.h>
#include "SerialPortManager.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

class MAVLinkParser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int selectedSystemId READ selectedSystemId WRITE setSelectedSystemId NOTIFY selectedSystemIdChanged)
public:
    explicit MAVLinkParser(SerialPortManager *manager, QObject *parent = nullptr);


    int selectedSystemId() const;
    Q_INVOKABLE void setSelectedSystemId(int systemId);

    // 解析 MAVLink 消息，供外部调用（例如 SerialPortManager 读取数据后调用）
    Q_INVOKABLE void parseMAVLinkMessage(const QByteArray &data);

signals:
    void selectedSystemIdChanged();
    // 姿态数据（系统ID、Roll、Pitch、Yaw）
    void newAttitudeData(int sysid, double roll, double pitch, double yaw);
    // 电池状态（系统ID、电压，单位：伏特）
    void newBatteryData(int sysid, double batteryVoltage);
    // 全局位置数据（系统ID、纬度、经度、高度），这里直接使用 WGS-84 坐标
    void newGlobalPositionData(int sysid, double lat, double lon, double alt,double realt);
    // 全局位置数据（系统ID、WGS-84 坐标），备用信号
    void newGlobalPositionDataWGS(int sysid, double lat, double lon, double alt);
    // 卫星数据信号（系统ID、卫星数量）
    void newSatelliteData(int sysid, int satellites);
    // 命令确认信号（系统ID、命令、结果）
    void commandAckReceived(int sysid, int command, int result);
    // 检测到新的无人机系统 ID信号
    void newDroneSystemIdDetected(uint8_t systemId);
    // 发送系统 ID 为 1 的无人机 (长机) 位置信息
    void systemIdOnePosition(double latitude, double longitude, float altitude);
    void systemIdOnePositionwgs(double wgslatitude, double wgslongitude, float altitude);

    void dronesPositionUpdated(int sysid, double latitude, double longitude, float altitude);
    void wgsPositionUpdated(uint8_t sysid,double lat,double lon,double alt);

    /* 仅转发航点上传握手消息 */
    void missionMessageReceived(const mavlink_message_t &msg);



public slots:
    // 接收串口数据
    void onSerialDataReceived(const QByteArray &data);

private:
    int m_selectedSystemId;
    mavlink_status_t status;
    mavlink_message_t msg;
    SerialPortManager *m_serialPortManager; // 用于接收串口数据

    // 内部消息处理函数
    void __handleAttitude(const mavlink_message_t &msg);//姿态
    void __handleBatteryStatus(const mavlink_message_t &msg);//
    void __handleGlobalPositionInt(const mavlink_message_t &msg);
    void __handleCommandAck(const mavlink_message_t &msg);
    void __handleStatustext(const mavlink_message_t &msg);
    void __handleGPSRawInt(const mavlink_message_t &msg);
    void __handleHeartbeat(const mavlink_message_t &msg);
    void __handleGpsRtk(const mavlink_message_t &msg);



    void wgs84ToGcj02(double wgsLat, double wgsLon, double &gcjLat, double &gcjLon);
    QNetworkAccessManager *m_netManager;
    QString                m_gaodeKey;

    QMap<int, QPair<double,double>> m_lastGcj02Coords;

};

#endif // MAVLINKPARSER_H
