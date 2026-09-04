#include "AttitudeIndicator.h"
#include <QtMath>

AttitudeIndicator::AttitudeIndicator(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_roll(0)
    , m_pitch(0)
    , m_yaw(0)
    , m_altitude(0)
{
    setAntialiasing(true);
    // 定时器用于定时刷新界面（与 aeroDial 类似）
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(100);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() {
        this->update();
    });

    m_updateTimer->start();
}

AttitudeIndicator::~AttitudeIndicator()
{
}

void AttitudeIndicator::setRoll(float r)
{
    if (qFuzzyCompare(m_roll, r))
        return;
    m_roll = r;
    emit rollChanged();
    update();
}

void AttitudeIndicator::setPitch(float p)
{
    if (qFuzzyCompare(m_pitch, p))
        return;
    m_pitch = p;
    emit pitchChanged();
    update();
}

void AttitudeIndicator::setYaw(float y)
{
    if (qFuzzyCompare(m_yaw, y))
        return;
    m_yaw = y;
    emit yawChanged();
    update();
}

void AttitudeIndicator::setAltitude(float a)
{
    if (qFuzzyCompare(m_altitude, a))
        return;
    m_altitude = a;
    emit altitudeChanged();
    update();
}

void AttitudeIndicator::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    // 将坐标系移动到中心
    painter->translate(boundingRect().width()/2, boundingRect().height()/2);
    int side = qMin(boundingRect().width(), boundingRect().height());
    painter->scale(side / 500.0, side / 500.0);

    drawCrown(painter);
    drawYawShow(painter, m_yaw);
    drawRollNumericValue(painter, m_roll);
    drawBackground(painter, m_roll, m_pitch);
    drawPitchNumericValue(painter, m_roll, m_pitch);
    drawTextPie(painter, m_yaw, m_roll, m_pitch, m_altitude);
}

// 绘制外圈
void AttitudeIndicator::drawCrown(QPainter *painter)
{
    painter->save();
    int radius = 100;
    QLinearGradient lg1(0, -radius, 0, radius);
    lg1.setColorAt(0, Qt::darkBlue);
    lg1.setColorAt(1, Qt::darkGreen);
    painter->setBrush(lg1);
    painter->setPen(Qt::green);
    painter->drawEllipse(-100, -100, 200, 200);
    painter->restore();
}

// 绘制航向显示
void AttitudeIndicator::drawYawShow(QPainter *painter, float yaw)
{
    painter->save();
    painter->rotate(-yaw);
    QFont font("Microsoft YaHei", 7, QFont::Bold);
    painter->setFont(font);
    QFontMetricsF fm(painter->font());
    QPen pen(Qt::white);
    double angleStep = 180.0 / 40;
    for (int i = 0; i <= 80; i++) {
        if (i % 10 == 0) {
            pen.setWidth(1);
            painter->setPen(pen);
            painter->drawLine(0, 88, 0, 96);
            QString text;
            switch (i) {
            case 0: text = "N"; break;
            case 10: text = "NW"; break;
            case 20: text = "W"; break;
            case 30: text = "SW"; break;
            case 40: text = "S"; break;
            case 50: text = "SE"; break;
            case 60: text = "E"; break;
            case 70: text = "NE"; break;
            }
            double w = fm.size(Qt::TextSingleLine, text).width();
            painter->drawText(-0.75 * w, -78, text);
        } else {
            pen.setWidth(0);
            painter->setPen(pen);
            painter->drawLine(0, 88, 0, 91);
        }
        painter->rotate(-angleStep);
    }
    painter->restore();
}

// 绘制滚转角数值显示
void AttitudeIndicator::drawRollNumericValue(QPainter *painter, float roll)
{
    painter->save();
    painter->rotate(180);
    QLinearGradient lg1(0, 100, 0, -100);
    lg1.setColorAt(0, Qt::darkBlue);
    lg1.setColorAt(1, Qt::darkGreen);
    painter->setBrush(lg1);
    QRect rectangle(-100, -100, 200, 200);
    painter->drawPie(rectangle, 0, 16 * 180);
    painter->restore();

    painter->save();
    painter->setPen(Qt::white);
    QFont font("Microsoft YaHei", 7, QFont::Bold);
    painter->setFont(font);
    QFontMetricsF fm(painter->font());
    QPen pen = painter->pen();
    painter->rotate(-90 + m_roll);
    int temp_180 = 0;
    double angleStep = 180.0 / 40;
    for (float i = 0; i <= 40; i++) {
        if ((temp_180 == 1) && (4.5 * i + m_roll >= 0) && (4.5 * i + m_roll <= 180)) {
            painter->rotate(180);
            temp_180 = 0;
        }
        if ((temp_180 == 0) && ((4.5 * i + m_roll > 180) || (4.5 * i + m_roll < 0))) {
            painter->rotate(180);
            temp_180 = 1;
        }
        if (qRound(i) % 10 == 0) {
            pen.setWidth(1);
            painter->setPen(pen);
            painter->drawLine(0, 80, 0, 86);
            float tmpVal = 90 - 45 * (static_cast<int>(i) / 10);
            if ((temp_180 == 1) && (4.5 * i + m_roll > 180))
                tmpVal += 180;
            if ((temp_180 == 1) && (4.5 * i + m_roll < 0))
                tmpVal -= 180;
            QString str = QString::number(tmpVal);
            double w = fm.size(Qt::TextSingleLine, str).width();
            painter->drawText(-0.5 * w, 96, str);
        } else {
            pen.setWidth(0);
            painter->setPen(pen);
            painter->drawLine(0, 83, 0, 86);
        }
        painter->rotate(angleStep);
    }
    painter->restore();
}

// 绘制背景（根据 pitch 绘制不同的弧段）
void AttitudeIndicator::drawBackground(QPainter *painter, float roll, float pitch)
{
    painter->save();
    painter->rotate(-roll);
    painter->setBrush(Qt::blue);
    QRect rectangle(-75, -75, 150, 150);
    // 计算 pitch 对应的弧度偏移（此处比例可根据实际情况调整）
    double asinVal = qAsin(-3.5 * pitch / 75);
    int startAngle = 16 * (asinVal * 180.0 / M_PI);
    int spanAngle = 16 * (180 - 2 * (asinVal * 180.0 / M_PI));
    painter->drawChord(rectangle, startAngle, spanAngle);

    painter->setBrush(Qt::darkGreen);
    QRect rectangle2(-75, -75, 150, 150);
    int startAngle2 = 16 * (-180 - (asinVal * 180.0 / M_PI));
    int spanAngle2 = 16 * (180 + 2 * (asinVal * 180.0 / M_PI));
    painter->drawChord(rectangle2, startAngle2, spanAngle2);
    painter->restore();
}

// 绘制俯仰角数值显示
void AttitudeIndicator::drawPitchNumericValue(QPainter *painter, float roll, float pitch)
{
    painter->save();
    painter->rotate(-roll);
    QPen pen(Qt::white);
    QFont font = painter->font();
    font.setPixelSize(6);
    painter->setFont(font);
    QFontMetricsF fm(painter->font());

    // 翻转 pitch 方向
    pitch = -pitch;
    painter->translate(0, 3.5 * pitch);
    for (float i = -20 - pitch; i <= 20 - pitch; i++) {
        int i_temp = qRound(i);
        if (i_temp % 5 == 0) {
            pen.setWidth(1);
            painter->setPen(pen);
            painter->drawLine(-3, 3.5 * i, 3, 3.5 * i);
            float tmpVal = -i_temp;
            QString str = QString::number(tmpVal);
            double h = fm.size(Qt::TextSingleLine, str).height();
            painter->drawText(10, 3.5 * i + 0.15 * h, str);
        } else {
            pen.setWidth(0);
            painter->setPen(pen);
            painter->drawLine(-1.5, 3.5 * i, 1.5, 3.5 * i);
        }
    }
    painter->restore();
}

// 绘制文本信息区（显示 yaw、roll、pitch、altitude）
void AttitudeIndicator::drawTextPie(QPainter *painter, float yaw, float roll, float pitch, float altitude)
{
    QString string_Yaw = QString::number(yaw);
    QString string_Roll = QString::number(roll);
    QString string_Pitch = QString::number(pitch);
    QString string_Alt = QString::number(altitude);

    painter->save();
    painter->setPen(Qt::green);
    painter->setBrush(Qt::gray);
    QRect rectangle(-132, -40, 60, 80);
    painter->drawPie(rectangle, -16 * 74, 16 * 148);

    QRect rectangle2(72, -40, 60, 80);
    painter->drawPie(rectangle2, 16 * 106, 16 * 148);

    painter->setPen(Qt::black);
    QFont font("Microsoft YaHei", 4, QFont::Bold);
    painter->setFont(font);
    painter->setBrush(Qt::darkGray);
    painter->drawText(-93, -17, "YAW");
    painter->drawRect(-97, -15, 20, 10);
    painter->drawText(-93, 22, "HIG");
    painter->drawRect(-97, 5, 20, 10);
    painter->drawText(83, -17, "PIT");
    painter->drawRect(77, -15, 20, 10);
    painter->drawText(83, 22, "ROL");
    painter->drawRect(77, 5, 20, 10);

    QFont font2("Microsoft YaHei", 5, QFont::Bold);
    painter->setFont(font2);
    painter->setPen(Qt::white);
    painter->drawText(-92, -7, string_Yaw);
    painter->setPen(Qt::yellow);
    painter->drawText(82, 13, string_Roll);
    painter->setPen(Qt::red);
    painter->drawText(82, -7, string_Pitch);
    painter->setPen(Qt::green);
    painter->drawText(-92, 13, string_Alt);

    QPen pen = painter->pen();
    painter->setBrush(Qt::white);
    QRect rectangle_up(-15, -100, 30, 20);
    painter->drawPie(rectangle_up, 16 * 75, 16 * 30);
    QRect rectangle_down(-15, 75, 30, 20);
    painter->setBrush(Qt::yellow);
    painter->drawPie(rectangle_down, 16 * 75, 16 * 30);
    pen.setColor(QColor(255, 0, 0, 100));
    pen.setWidthF(0.1);
    painter->setPen(pen);
    painter->drawLine(0, 0, 0, 75);


    pen.setColor(QColor(255, 0, 0, 255));
    pen.setWidth(1);
    painter->setPen(pen);
    painter->drawLine(-55, 0, -40, 0);
    painter->drawLine(-15, 6, 0, 0);
    painter->drawLine(0, 0, 15, 6);
    painter->drawLine(40, 0, 55, 0);
    painter->restore();
}
void AttitudeIndicator::updateAttitude(int sysid ,float roll, float pitch, float yaw)
{

    setRoll(roll);
    setPitch(pitch);
    setYaw(yaw);
}
