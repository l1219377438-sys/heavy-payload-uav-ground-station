#ifndef ATTITUDEINDICATOR_H
#define ATTITUDEINDICATOR_H

#include <QQuickPaintedItem>
#include <QTimer>
#include <QPainter>

class AttitudeIndicator : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(float roll READ roll WRITE setRoll NOTIFY rollChanged)
    Q_PROPERTY(float pitch READ pitch WRITE setPitch NOTIFY pitchChanged)
    Q_PROPERTY(float yaw READ yaw WRITE setYaw NOTIFY yawChanged)
    Q_PROPERTY(float altitude READ altitude WRITE setAltitude NOTIFY altitudeChanged)
public:
    explicit AttitudeIndicator(QQuickItem *parent = nullptr);
    ~AttitudeIndicator();

    float roll() const { return m_roll; }
    float pitch() const { return m_pitch; }
    float yaw() const { return m_yaw; }
    float altitude() const { return m_altitude; }

    void setRoll(float r);
    void setPitch(float p);
    void setYaw(float y);
    void setAltitude(float a);

    void paint(QPainter *painter) override;

signals:
    void rollChanged();
    void pitchChanged();
    void yawChanged();
    void altitudeChanged();


public slots:
    void updateAttitude(int sysid,float roll, float pitch, float yaw);


private:
    float m_roll;
    float m_pitch;
    float m_yaw;
    float m_altitude;

    QTimer *m_updateTimer;

    // 辅助绘图函数，基本沿用 aeroDial 的绘制思路
    void drawCrown(QPainter *painter);
    void drawYawShow(QPainter *painter, float yaw);
    void drawRollNumericValue(QPainter *painter, float roll);
    void drawBackground(QPainter *painter, float roll, float pitch);
    void drawPitchNumericValue(QPainter *painter, float roll, float pitch);
    void drawTextPie(QPainter *painter, float yaw, float roll, float pitch, float altitude);
};

#endif // ATTITUDEINDICATOR_H
