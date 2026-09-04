#ifndef SERIALPORTMANAGER_H
#define SERIALPORTMANAGER_H

#include <QObject>
#include <QStringList>
#include <QSerialPortInfo>
#include <QSerialPort>


class SerialPortManager : public QObject
{
    Q_OBJECT
    // 提供可用串口列表给 QML
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY availablePortsChanged)
    // 记录当前串口是否已打开
    Q_PROPERTY(bool isPortOpen READ isPortOpen NOTIFY isPortOpenChanged)
    // 根据 isPortOpen 返回按钮显示的文本
    Q_PROPERTY(QString buttonText READ buttonText NOTIFY isPortOpenChanged)

public:
    explicit SerialPortManager(QObject *parent = nullptr);

    QStringList availablePorts() const;
    bool isPortOpen() const;
    QString buttonText() const;

    // 刷新串口列表
    Q_INVOKABLE void refreshPorts();
    // 切换串口打开/关闭状态，参数为串口名称和波特率
    Q_INVOKABLE void togglePort(const QString &portName, const QString &baudRate);
    // writeData 方法，供 DroneController 调用
    Q_INVOKABLE qint64 writeData(const QByteArray &data);

signals:
    void availablePortsChanged();
    void isPortOpenChanged();
    void dataReceived(const QByteArray &data);

private slots:
    // 处理串口数据接收
    void handleReadyRead();
    void updatePorts();

private:
    QStringList m_availablePorts;
    QSerialPort m_serialPort;

private:
    // 缓存相关函数
    void savePortsCache(const QStringList &ports);
    QStringList loadPortsCache();
    QString cacheFilePath() const;


};

#endif // SERIALPORTMANAGER_H
