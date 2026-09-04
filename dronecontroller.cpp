#include "DroneController.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QtMath>

DroneController::DroneController(SerialPortManager *manager, QObject *parent)
    : QObject(parent),
    m_serialManager(manager),
    currentLatitude(0),
    currentLongitude(0),
    currentAltitude(0)
{
    commandAckTimer = new QTimer(this);
    commandAckTimer->setInterval(500); // 0.5秒重发间隔
    // connect(commandAckTimer, &QTimer::timeout, this, &DroneController::resendLastCommand);
}

void DroneController::takeoff(int systemId,float altitude)
{
    if (!isPortOpen()) return;

    sendMavlinkMessage(systemId, 1, MAV_CMD_NAV_TAKEOFF_LOCAL, 0, 0, 0, 0, 0, 0, altitude);
    qDebug() << "Takeoff command sent with altitude:" << altitude;

}

void DroneController::land(int systemId)
{
    if (!isPortOpen()) return;

    sendMavlinkMessage(systemId, 1, MAV_CMD_NAV_RETURN_TO_LAUNCH, 0, 0, 0, 0, 0, 0, 0);
    qDebug() << "Land command sent";
}

void DroneController::gotoLocation(int systemId, double latitude, double longitude, float altitude, float speed)
{
    if (!isPortOpen()) return;

    double wgsLat, wgsLon;
    gcj02ToWgs84(latitude, longitude, wgsLat, wgsLon);
    qDebug() << "Converted coordinates:" << wgsLat << wgsLon;

    // 发送飞行到目标位置命令
    sendMavlinkMessage(systemId, 1, MAV_CMD_DO_REPOSITION,
                       speed, MAV_DO_REPOSITION_FLAGS_CHANGE_MODE, 0, NAN,
                       wgsLat, wgsLon, altitude);

    // targetLatitude = wgsLat;
    // targetLongitude = wgsLon;
    // targetAltitude = altitude;

    // // 定时检查是否到达目标位置
    // QTimer *locationCheckTimer = new QTimer(this);
    // connect(locationCheckTimer, &QTimer::timeout, this, [this, locationCheckTimer]() {
    //     if (isAtTargetLocation(targetLatitude, targetLongitude, targetAltitude)) {
    //         emit statusMessage("Reached the target location");
    //         emit reachedLocation();
    //         locationCheckTimer->stop();
    //         locationCheckTimer->deleteLater();
    //     }
    // });
    // locationCheckTimer->start(500);
}

void DroneController::unlockMotors(int systemId)
{
    if (!isPortOpen()) return;

    sendMavlinkMessage(systemId, 1, MAV_CMD_COMPONENT_ARM_DISARM, 1, 0, 0, 0, 0, 0, 0);
    qDebug() << "Motors unlocked";
}

void DroneController::lockMotors(int systemId)
{
    if (!isPortOpen()) return;

    sendMavlinkMessage(systemId, 1, MAV_CMD_COMPONENT_ARM_DISARM, 0, 0, 0, 0, 0, 0, 0);
    qDebug() << "Motors locked";
}


void DroneController::switchToMissionMode(int systemId)//模式切换
{
    if (!isPortOpen()) return;

    // 切换到 自动模式 (主模式4)，子模式为 自动任务模式 (子模式4)
    sendMavlinkMessage(systemId, 1, MAV_CMD_DO_SET_MODE,
                       1,  // param1 固定填1，表示自定义模式
                       4,  // param2 主模式，自动模式
                       4,  // param3 子模式，自动任务模式
                       0, 0, 0, 0); // 其他参数填0
    qDebug() << "Switch to Mission Mode command sent";
}


void DroneController::switchToStayMode(int systemId) //暂停任务
{
    if (!isPortOpen()) return;


    sendMavlinkMessage(systemId, 1, MAV_CMD_DO_SET_MODE,
                       1,  // param1 固定填1，表示自定义模式
                       3,  // param2 主模式，定点模式
                       0,  // param3 子模式
                       0, 0, 0, 0); // 其他参数填0
    qDebug() << "Switch to Mission Mode command sent";
}

void DroneController::handleGlobalPosition(int systemId, double latitude, double longitude, float altitude)
{
    currentLatitude = latitude;
    currentLongitude = longitude;
    currentAltitude = altitude;
    emit globalPositionUpdated(latitude, longitude, altitude);
}

double DroneController::distanceToTarget(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000; // 地球半径，单位米
    double phi1 = lat1 * M_PI / 180;
    double phi2 = lat2 * M_PI / 180;
    double deltaPhi = (lat2 - lat1) * M_PI / 180;
    double deltaLambda = (lon2 - lon1) * M_PI / 180;

    double a = sin(deltaPhi / 2) * sin(deltaPhi / 2) +
               cos(phi1) * cos(phi2) *
                   sin(deltaLambda / 2) * sin(deltaLambda / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));

    double distance = R * c;
    qDebug() << "Distance:" << distance;
    return distance;
}

void DroneController::sendMavlinkMessage(uint8_t target_system, uint8_t target_component, uint16_t command,
                                         float param1, float param2, float param3, float param4,
                                         float param5, float param6, float param7)
{
    // 保存命令用于重发
    lastSentCommand = command;
    lastCommand.target_system = target_system;
    lastCommand.target_component = target_component;
    lastCommand.command = command;
    lastCommand.param1 = param1;
    lastCommand.param2 = param2;
    lastCommand.param3 = param3;
    lastCommand.param4 = param4;
    lastCommand.param5 = param5;
    lastCommand.param6 = param6;
    lastCommand.param7 = param7;

    mavlink_message_t msg;
    mavlink_msg_command_long_encode(0, 0, &msg, &lastCommand);
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &msg);

    QByteArray data(reinterpret_cast<const char*>(buffer), len);
    // 通过 SerialPortManager 发送数据（需确保 manager 内实现 writeData(QByteArray)）
    if (m_serialManager->isPortOpen()) {
        QMetaObject::invokeMethod(m_serialManager, "writeData", Q_ARG(QByteArray, data));
        commandAckTimer->start();
        emit statusMessage("Message sent, waiting for ACK");
    } else {
        emit statusMessage("Failed to send message: Serial port is not open");
    }
}

void DroneController::handleCommandAckReceived(uint16_t command)
{
    if (command == lastSentCommand) {
        commandAckTimer->stop();
        emit statusMessage("Command ACK received");
    }
}

void DroneController::resendLastCommand()
{
    emit statusMessage("Resending command due to no ACK");
    sendMavlinkMessage(lastCommand.target_system, lastCommand.target_component, lastCommand.command,
                       lastCommand.param1, lastCommand.param2, lastCommand.param3, lastCommand.param4,
                       lastCommand.param5, lastCommand.param6, lastCommand.param7);
}

bool DroneController::isPortOpen() const
{
    if (!m_serialManager || !m_serialManager->isPortOpen()) {
        // emit statusMessage("Serial port is not open");
        return false;
    }
    return true;
}

bool DroneController::isAtTargetLocation(double latitude, double longitude, float altitude)
{
    return distanceToTarget(currentLatitude, currentLongitude, latitude, longitude) < 1.0 &&
           qFabs(currentAltitude - altitude) < 1.0;
}

void DroneController::logCommandResult(uint16_t command, uint8_t result)
{
    QFile file("mavlink_commands_log.txt");
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << " - Command: " << command;
        switch(result) {
        case MAV_RESULT_ACCEPTED:
            out << " - Result: 0 (MAV_RESULT_ACCEPTED) - Command or operation successfully executed.\n";
            break;
        case MAV_RESULT_TEMPORARILY_REJECTED:
            out << " - Result: 1 (MAV_RESULT_TEMPORARILY_REJECTED) - Command or operation temporarily rejected.\n";
            break;
        case MAV_RESULT_DENIED:
            out << " - Result: 2 (MAV_RESULT_DENIED) - Command or operation denied.\n";
            break;
        case 255:
            out << " - Result: No response (Assumed)\n";
            break;
        default:
            out << " - Result: " << static_cast<int>(result) << " - Unknown result code.\n";
            break;
        }
        file.close();
    } else {
        emit statusMessage("Failed to open log file for writing.");
    }
}

void DroneController::gcj02ToWgs84(double gcjLat, double gcjLon, double &wgsLat, double &wgsLon)
{
    const double pi = 3.14159265358979323846;
    const double a = 6378245.0; // 地球长半轴
    const double ee = 0.00669342162296594323; // 偏心率平方

    double dLat, dLon;
    {
        double x = gcjLon - 105.0;
        double y = gcjLat - 35.0;
        dLat = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * sqrt(qAbs(x));
        dLat += (20.0 * sin(6.0 * x * pi) + 20.0 * sin(2.0 * x * pi)) * 2.0 / 3.0;
        dLat += (20.0 * sin(y * pi) + 40.0 * sin(y / 3.0 * pi)) * 2.0 / 3.0;
        dLat += (160.0 * sin(y / 12.0 * pi) + 320 * sin(y * pi / 30.0)) * 2.0 / 3.0;
        dLon = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * sqrt(qAbs(x));
        dLon += (20.0 * sin(6.0 * x * pi) + 20.0 * sin(2.0 * x * pi)) * 2.0 / 3.0;
        dLon += (20.0 * sin(x * pi) + 40.0 * sin(x / 3.0 * pi)) * 2.0 / 3.0;
        dLon += (150.0 * sin(x / 12.0 * pi) + 300.0 * sin(x / 30.0 * pi)) * 2.0 / 3.0;
    }

    double radLat = gcjLat / 180.0 * pi;
    double magic = sin(radLat);
    magic = 1 - ee * magic * magic;
    double sqrtMagic = sqrt(magic);

    dLat = (dLat * 180.0) / ((a * (1 - ee)) / (magic * sqrtMagic) * pi);
    dLon = (dLon * 180.0) / (a / sqrtMagic * cos(radLat) * pi);

    wgsLat = floor((gcjLat - dLat) * 1e6 + 0.5) / 1e6;
    wgsLon = floor((gcjLon - dLon) * 1e6 + 0.5) / 1e6;
}
