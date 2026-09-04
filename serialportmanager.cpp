#include "SerialPortManager.h"
#include <QDebug>
#include <qserialport.h>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>

SerialPortManager::SerialPortManager(QObject *parent)
    : QObject(parent)
{
    //加在缓存数据
    m_availablePorts = loadPortsCache();

    updatePorts();
    // 当串口有数据可读时，调用槽函数
    connect(&m_serialPort, &QSerialPort::readyRead, this, &SerialPortManager::handleReadyRead);
}

QStringList SerialPortManager::availablePorts() const
{
    return m_availablePorts;
}

bool SerialPortManager::isPortOpen() const
{
    return m_serialPort.isOpen();
}

QString SerialPortManager::buttonText() const
{
    return isPortOpen() ? "关闭串口" : "打开串口";
}

void SerialPortManager::refreshPorts()
{
    updatePorts();
}

void SerialPortManager::updatePorts()
{
    QStringList ports;
    // 遍历所有可用串口
    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts()) {
        QSerialPort port;
        port.setPort(info);
        // 尝试以读写模式打开，成功后添加进列表
        if (port.open(QIODevice::ReadWrite)) {
            ports << info.portName();
            port.close();
        }
    }
    if (ports != m_availablePorts) {
        m_availablePorts = ports;
        emit availablePortsChanged();
        savePortsCache(m_availablePorts);
    }
}

void SerialPortManager::togglePort(const QString &portName, const QString &baudRate)
{
    if (m_serialPort.isOpen()) {
        m_serialPort.close();
        qDebug() << "串口关闭";
    } else {
        m_serialPort.setPortName(portName);
        bool ok;
        int rate = baudRate.toInt(&ok);
        if (!ok) {
            qDebug() << "无效的波特率:" << baudRate;
            return;
        }
        m_serialPort.setBaudRate(rate);
        if (m_serialPort.open(QIODevice::ReadWrite)) {
            qDebug() << "串口打开:" << portName << "波特率:" << rate;
        } else {
            qDebug() << "打开串口失败:" << m_serialPort.errorString();
        }
    }
    emit isPortOpenChanged();
}

void SerialPortManager::handleReadyRead()
{
    QByteArray data = m_serialPort.readAll();
    emit dataReceived(data);
}
qint64 SerialPortManager::writeData(const QByteArray &data)
{
    return m_serialPort.write(data);
}
// 获取缓存文件的完整路径，使用 QStandardPaths 获取应用数据目录
QString SerialPortManager::cacheFilePath() const
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // 如果目录不存在，则创建目录
    QDir dir(dataPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dataPath + "/SerialPortCache.txt";
}

// 保存串口列表到缓存文件
void SerialPortManager::savePortsCache(const QStringList &ports)
{
    QString filePath = cacheFilePath();
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        for (const QString &port : ports) {
            out << port << "\n";
        }
        file.close();
    } else {
        qDebug() << "无法打开缓存文件以写入:" << file.errorString();
    }
}

// 从缓存文件中加载串口列表
QStringList SerialPortManager::loadPortsCache()
{
    QStringList ports;
    QString filePath = cacheFilePath();
    QFile file(filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty()) {
                ports << line;
            }
        }
        file.close();
    }
    return ports;
}
