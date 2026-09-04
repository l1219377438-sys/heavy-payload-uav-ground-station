#ifndef DRONECONTROLLER_H
#define DRONECONTROLLER_H

#include <QObject>
#include <QTimer>
#include "SerialPortManager.h"
#include <mavlink/common/mavlink.h>
#include<mavlink/ardupilotmega/ardupilotmega.h>

class DroneController : public QObject
{
    Q_OBJECT
public:
    explicit DroneController(SerialPortManager *manager, QObject *parent = nullptr);

    Q_INVOKABLE void takeoff(int systemId,float altitude);
    Q_INVOKABLE void land(int systemId);
    Q_INVOKABLE void gotoLocation(int systemId, double latitude, double longitude, float altitude, float speed);
    Q_INVOKABLE void unlockMotors(int systemId);
    Q_INVOKABLE void lockMotors(int systemId);
    void handleGlobalPosition(int systemId, double latitude, double longitude, float altitude);

    //切换模式（开始任务、暂停任务)
    Q_INVOKABLE void switchToMissionMode(int systemId);
    Q_INVOKABLE void switchToStayMode(int sysItemd);

    Q_INVOKABLE void gcj02ToWgs84(double gcjLat, double gcjLon, double &wgsLat, double &wgsLon);




signals:
    void statusMessage(const QString &msg);
    void globalPositionUpdated(double latitude, double longitude, float altitude);
    void reachedLocation();

public slots:
    void handleCommandAckReceived(uint16_t command);

private slots:
    void resendLastCommand();

private:
    bool isPortOpen() const;
    bool isAtTargetLocation(double latitude, double longitude, float altitude);
    double distanceToTarget(double lat1, double lon1, double lat2, double lon2);
    void sendMavlinkMessage(uint8_t target_system, uint8_t target_component, uint16_t command,
                            float param1, float param2, float param3, float param4,
                            float param5, float param6, float param7);
    void logCommandResult(uint16_t command, uint8_t result);

    SerialPortManager *m_serialManager;
    QTimer *commandAckTimer;

    // 当前位置信息
    double currentLatitude;
    double currentLongitude;
    double currentAltitude;
    // 目标位置信息
    double targetLatitude;
    double targetLongitude;
    float targetAltitude;

    uint16_t lastSentCommand;
    mavlink_command_long_t lastCommand;
};

#endif // DRONECONTROLLER_H
