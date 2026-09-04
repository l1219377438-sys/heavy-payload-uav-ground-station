#include "LargeFormationController.h"
#include <QDebug>
#include <QThread>
#include <QEventLoop>
#include <QTimer>
#include <cmath>
#include <QElapsedTimer>


// -----------------------------------------
// 构造函数
// -----------------------------------------
LargeFormationController::LargeFormationController(SerialPortManager* fireSerialManager,
                                                   MultiController* multiDroneController,
                                                   MissionPlanner* missionPlanner,

                                                   QObject* parent)
    : QObject(parent),
    m_multiDroneController(multiDroneController),
    openserialFireManager(fireSerialManager),
    m_missionPlanner(missionPlanner)

{
    // 如果在 MultiController 中发射了 droneSystemIdAvailable 信号，则可在此直接连接
    connect(m_multiDroneController, &MultiController::droneSystemIdAvailable,
            this, &LargeFormationController::handleNewDroneSystemIdDetected);

    m_isOrderedMovementRunning = false;
    m_currentBatchIndex = 0;
    m_currentDroneIndex = 0;
    m_orderSpeed = 0.0f;
    m_orderInterval = 1000;  // 默认1秒，具体再改

}

// -----------------------------------------
// 当检测到新的无人机 ID
// -----------------------------------------
void LargeFormationController::handleNewDroneSystemIdDetected(uint8_t systemId)
{
    // 简化逻辑：只要没有，就添加
    if (!droneMap.contains(systemId)) {
        addDroneToFormation(systemId);
        qDebug() << "检测到无人机ID" << systemId;
    }
}

// -----------------------------------------
// 添加无人机到编队
// -----------------------------------------
void LargeFormationController::addDroneToFormation(uint8_t systemId)
{
    if (droneMap.contains(systemId)) {
        return; // 已存在就不重复添加
    }

    DroneInfo info;
    info.id       = systemId;
    info.offsetX  = 0;
    info.offsetY  = 0;
    info.offsetZ  = 0;

    droneMap[systemId] = info;

    // 如果是 1 号机则视为长机
    if (systemId == 1) {
        leaderSystemId = systemId;
        emit statusMessage("Leader drone (System ID 1) is set as leader");
    } else {
        emit statusMessage(QString("Drone with ID %1 added to formation").arg(systemId));
    }
}

// -----------------------------------------
// 清空编队偏移
// -----------------------------------------
void LargeFormationController::clearFormationOffsets()
{
    for (auto &info : droneMap) {
        info.offsetX = 0;
        info.offsetY = 0;
        info.offsetZ = 0;
    }
}

// -----------------------------------------
// 一字形编队（仅沿“纬度”排布）
// -----------------------------------------
void LargeFormationController::arrangeLineFormation(float spacing)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }

    clearFormationOffsets();
    printLeaderDroneInfo();

    // 先对所有无人机ID排序
    auto wingmanIdList = droneMap.keys();
    std::sort(wingmanIdList.begin(), wingmanIdList.end());

    int offsetIndex = 1;
    for (int id : wingmanIdList) {
        if (id == 1) continue; // 长机不动

        // 只保留“沿纬度排”
        if (offsetIndex % 2 != 0) {
            droneMap[id].offsetX = -spacing * (offsetIndex / 2 + 1);
        } else {
            droneMap[id].offsetX =  spacing * (offsetIndex / 2);
        }
        droneMap[id].offsetZ = 0;

        offsetIndex++;
    }

    // 根据编队偏航角进行整体旋转
    applyFormationRotation();

    // 计算目标坐标并发射信号
    for (int id : wingmanIdList) {
        DroneInfo &info = droneMap[id];
        if (id == 1) {
            info.targetLat = leaderLat;
            info.targetLon = leaderLon;
            info.targetAlt = leaderAlt;
        } else {
            double newLat = leaderLat + info.offsetY / 111139.0;
            double newLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
            float newAlt  = leaderAlt + info.offsetZ;

            info.targetLat = newLat;
            info.targetLon = newLon;
            info.targetAlt = newAlt;

            emit formationStatusWithOffsetUpdated(
                QStringLiteral("一字形"),
                id,
                info.offsetX,
                info.offsetY,
                info.offsetZ
                );
        }
    }

    // 开启到位检测
    startCheckFormationArrival();
}

// -----------------------------------------
// 人字形编队（仅沿“纬度”排布）
// -----------------------------------------
void LargeFormationController::arrangeVFormation(float spacing)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }

    clearFormationOffsets();
    printLeaderDroneInfo();

    auto wingmanIdList = droneMap.keys();
    std::sort(wingmanIdList.begin(), wingmanIdList.end());

    int wingmanCount = wingmanIdList.size();
    for (int i = 0; i < wingmanCount; i++) {
        int id = wingmanIdList[i];
        if (id == 1) continue;

        // 只保留“沿纬度排”
        if (i % 2 == 0) {
            droneMap[id].offsetX =  spacing * (i / 2);
            droneMap[id].offsetY = -spacing * (i / 2);
        } else {
            droneMap[id].offsetX = -spacing * ((i / 2) + 1);
            droneMap[id].offsetY = -spacing * ((i / 2) + 1);
        }
    }

    applyFormationRotation();

    for (int id : wingmanIdList) {
        DroneInfo &info = droneMap[id];
        if (id == 1) {
            info.targetLat = leaderLat;
            info.targetLon = leaderLon;
            info.targetAlt = leaderAlt;
        } else {
            double newLat = leaderLat + info.offsetY / 111139.0;
            double newLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
            float newAlt  = leaderAlt + info.offsetZ;

            info.targetLat = newLat;
            info.targetLon = newLon;
            info.targetAlt = newAlt;

            emit formationStatusWithOffsetUpdated(QStringLiteral("人字形"),
                                                  id,
                                                  info.offsetX,
                                                  info.offsetY,
                                                  info.offsetZ);
        }
    }

    startCheckFormationArrival();
}

// -----------------------------------------
// 川字形编队（仅沿“纬度”排布）
// -----------------------------------------
void LargeFormationController::arrangeCustomFormation(float spacing)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }

    clearFormationOffsets();
    printLeaderDroneInfo();

    // 只保留“沿纬度排列”版本
    setCustomFormationLatitude(spacing);

    applyFormationRotation();

    for (auto id : droneMap.keys()) {
        auto &info = droneMap[id];
        info.targetLat = leaderLat + info.offsetY / 111139.0;
        info.targetLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
        info.targetAlt = leaderAlt + info.offsetZ;

        emit formationStatusWithOffsetUpdated(QStringLiteral("川字形"),
                                              id,
                                              info.offsetX,
                                              info.offsetY,
                                              info.offsetZ);
    }

    startCheckFormationArrival();
}

// -----------------------------------------
// 川字形 - 沿纬度排列
// -----------------------------------------
void LargeFormationController::setCustomFormationLatitude(float spacing)
{
    // 可根据你需要的川字形布局进行配置
    // 以下是示例（同时假设你可能有10架机）
    droneMap[2].offsetX = -spacing; droneMap[2].offsetY = 0;
    droneMap[1].offsetX = 0;        droneMap[1].offsetY = 0;
    droneMap[3].offsetX = spacing;  droneMap[3].offsetY = 0;

    droneMap[4].offsetX = -spacing; droneMap[4].offsetY = -spacing;
    droneMap[8].offsetX = 0;        droneMap[8].offsetY = -spacing;
    droneMap[5].offsetX = spacing;  droneMap[5].offsetY = -spacing;

    droneMap[6].offsetX = -spacing; droneMap[6].offsetY = -2*spacing;
    droneMap[10].offsetX = 0;       droneMap[10].offsetY = -2*spacing;
    droneMap[7].offsetX = spacing;  droneMap[7].offsetY = -2*spacing;

    droneMap[9].offsetX = spacing;  droneMap[9].offsetY = -3*spacing;
}

// -----------------------------------------
// W形编队（仅沿“纬度”排布）
// -----------------------------------------
void LargeFormationController::arrangeWFormation(float spacing)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }

    clearFormationOffsets();
    printLeaderDroneInfo();

    // 保留原先“沿纬度排”的写法
    droneMap[1].offsetX = 0;   droneMap[1].offsetZ = 0;
    droneMap[6].offsetX = -3*spacing; droneMap[6].offsetZ = -spacing;
    droneMap[2].offsetX = -spacing;   droneMap[2].offsetZ = -spacing;
    droneMap[3].offsetX =  spacing;   droneMap[3].offsetZ = -spacing;
    droneMap[7].offsetX =  3*spacing; droneMap[7].offsetZ = -spacing;
    droneMap[4].offsetX = -2*spacing; droneMap[4].offsetZ = -2*spacing;
    droneMap[5].offsetX =  2*spacing; droneMap[5].offsetZ = -2*spacing;
    droneMap[8].offsetX = -4*spacing; droneMap[8].offsetZ = 0;
    droneMap[10].offsetX= -5*spacing; droneMap[10].offsetZ= 0;
    droneMap[9].offsetX =  4*spacing; droneMap[9].offsetZ = 0;

    applyFormationRotation();

    for (auto id : droneMap.keys()) {
        auto &info = droneMap[id];
        info.targetLat = leaderLat + info.offsetY / 111139.0;
        info.targetLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
        info.targetAlt = leaderAlt + info.offsetZ;

        emit formationStatusWithOffsetUpdated(QStringLiteral("W形"),
                                              id,
                                              info.offsetX,
                                              info.offsetY,
                                              info.offsetZ);
    }

    startCheckFormationArrival();
}

// -----------------------------------------
// 十字形编队（仅沿“纬度”排布）
// -----------------------------------------
void LargeFormationController::arrangeCrossFormation(float spacing)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }

    clearFormationOffsets();
    printLeaderDroneInfo();

    // 只保留“沿纬度排”
    droneMap[1].offsetX = 0;  droneMap[1].offsetZ = 0;
    droneMap[2].offsetX = -spacing;   droneMap[2].offsetZ = 0;
    droneMap[3].offsetX =  spacing;   droneMap[3].offsetZ = 0;
    droneMap[4].offsetX = -2*spacing; droneMap[4].offsetZ = 0;
    droneMap[5].offsetX =  2*spacing; droneMap[5].offsetZ = 0;

    droneMap[10].offsetX = 0; droneMap[10].offsetZ = 3*spacing;
    droneMap[8].offsetX  = 0; droneMap[8].offsetZ  = 2*spacing;
    droneMap[6].offsetX  = 0; droneMap[6].offsetZ  = spacing;

    droneMap[7].offsetX = 0;  droneMap[7].offsetZ = -spacing;
    droneMap[9].offsetX = 0;  droneMap[9].offsetZ = -2*spacing;

    applyFormationRotation();

    for (auto id : droneMap.keys()) {
        auto &info = droneMap[id];
        info.targetLat = leaderLat + info.offsetY / 111139.0;
        info.targetLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
        info.targetAlt = leaderAlt + info.offsetZ;

        emit formationStatusWithOffsetUpdated(QStringLiteral("十字形"),
                                              id,
                                              info.offsetX,
                                              info.offsetY,
                                              info.offsetZ);
    }

    startCheckFormationArrival();
}
// -----------------------------------------
// 垂直一字型编队
// -----------------------------------------
void LargeFormationController::arrangeVerticalLineFormation(float spacing)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }
    clearFormationOffsets();
    printLeaderDroneInfo();

    droneMap[10].offsetZ = 5*spacing;
    droneMap[8].offsetZ = 4*spacing;
    droneMap[6].offsetZ = 3*spacing;
    droneMap[4].offsetZ = 2*spacing;
    droneMap[2].offsetZ = 1*spacing;
    droneMap[1].offsetZ = 0;
    droneMap[3].offsetZ = -1*spacing;
    droneMap[5].offsetZ = -2*spacing;
    droneMap[7].offsetZ = -3*spacing;
    droneMap[9].offsetZ = -4*spacing;

    applyFormationRotation();

    for (auto id : droneMap.keys()) {
        auto &info = droneMap[id];
        info.targetLat = leaderLat + info.offsetY / 111139.0;
        info.targetLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
        info.targetAlt = leaderAlt + info.offsetZ;

        emit formationStatusWithOffsetUpdated(QStringLiteral("垂直一字"),
                                              id,
                                              info.offsetX,
                                              info.offsetY,
                                              info.offsetZ);
    }

    startCheckFormationArrival();
}
//5圆编队
void LargeFormationController::arrangeFiveCircleFormation(float spacing)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }
    clearFormationOffsets();
    printLeaderDroneInfo();

    droneMap[1].offsetX = 0;  droneMap[1].offsetY = 0;droneMap[1].offsetZ = 0;
    droneMap[2].offsetX = -2*spacing;  droneMap[2].offsetY = 0;droneMap[2].offsetZ = -spacing;
    droneMap[3].offsetX = 2*spacing;  droneMap[3].offsetY = 0;droneMap[3].offsetZ = -spacing;
    droneMap[4].offsetX = -spacing;  droneMap[4].offsetY = 0;droneMap[4].offsetZ = -2*spacing;
    droneMap[5].offsetX = spacing;  droneMap[5].offsetY = 0;droneMap[5].offsetZ = -2*spacing;
    for (auto id : droneMap.keys()) {
        auto &info = droneMap[id];
        info.targetLat = leaderLat + info.offsetY / 111139.0;
        info.targetLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
        info.targetAlt = leaderAlt + info.offsetZ;

        emit formationStatusWithOffsetUpdated(QStringLiteral("垂直一字"),
                                              id,
                                              info.offsetX,
                                              info.offsetY,
                                              info.offsetZ);
    }

    startCheckFormationArrival();
}

void LargeFormationController::updateFormationWithOrder(float speed,
                                                        int interval,
                                                        QList<QList<uint8_t>> moveOrder)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }

    // 如有必要，可检查是否还有任务在执行
    if (m_isOrderedMovementRunning) {
        emit statusMessage("Another ordered movement is already in progress");
        return;
    }
    m_isOrderedMovementRunning = true;

    // 保存参数到成员变量
    m_currentMoveOrder = moveOrder;    // 任务批次列表
    m_currentBatchIndex = 0;           // 初始批次索引
    m_currentDroneIndex = 0;           // 当前批次内的无人机索引
    m_orderSpeed   = speed;
    m_orderInterval= interval;

    // 启动异步移动
    startNextOrderedMovement();
}

/**
 * @brief LargeFormationController::startNextOrderedMovement
 * 非阻塞地分批移动无人机
 */
void LargeFormationController::startNextOrderedMovement()
{
    // 检查是否所有批次都执行完
    if (m_currentBatchIndex >= m_currentMoveOrder.size()) {
        // 所有批次完成
        m_isOrderedMovementRunning = false;
        // emit statusMessage("Formation update completed with ordered movement.");

        return;
    }

    // 当前批次
    QList<uint8_t> &currentBatch = m_currentMoveOrder[m_currentBatchIndex];

    //  批次切换 如果当前批次内的所有无人机都移动完，就跳到下一个批次
    if (m_currentDroneIndex >= currentBatch.size()) {
        m_currentBatchIndex++;
        m_currentDroneIndex = 0;
        // 等待 interval 再进入下一批次
        // QTimer::singleShot(m_orderInterval, this, &LargeFormationController::startNextOrderedMovement);
        return;
    }

    // 当前批次内的某架无人机
    uint8_t droneId = currentBatch[m_currentDroneIndex];
    updateDronePosition(droneId, leaderLat, leaderLon, leaderAlt, m_orderSpeed);
    qDebug() << "Current target drone ID:" << droneId;

    // 移动到下一个无人机
    m_currentDroneIndex++;

    // 间隔后再继续调度下一个无人机
    QTimer::singleShot(m_orderInterval, this, &LargeFormationController::startNextOrderedMovement);
}





// -----------------------------------------
// 圆形编队（仅沿“川字形”+“纬度”分布，然后再做圆形）
// -----------------------------------------
void LargeFormationController::arrangeCircleFormation(float spacing,
                                                      float speed,
                                                      bool simulateOnly)
{
    qDebug() << "========== 开始配置圆形编队 ==========";
    qDebug() << "参数设置: 间距=" << spacing << ", 速度=" << speed << ", 仅模拟=" << simulateOnly;

    // 为简化，先用"川字形(纬度)"当作初始分布
    setCustomFormationLatitude(spacing);

    // 添加调试信息：显示所有无人机初始偏移量
    qDebug() << "设置川字形后的初始偏移量:";
    for (auto droneId : droneMap.keys()) {
        qDebug() << "无人机 #" << droneId << ": X偏移=" << droneMap[droneId].offsetX
                 << ", Y偏移=" << droneMap[droneId].offsetY
                 << ", Z偏移=" << droneMap[droneId].offsetZ;
    }

    // "8号机"做圆心
    double centerX = droneMap[8].offsetX;
    double centerY = droneMap[8].offsetY;
    qDebug() << "圆心位置(相对于长机): X=" << centerX << ", Y=" << centerY;

    // 取出8号机的 offset（相对于1号机的偏移量）
    float offsetX = droneMap[8].offsetX;  // X 方向偏移(沿经度)
    float offsetY = droneMap[8].offsetY;  // Y 方向偏移(沿纬度)
    float offsetZ = droneMap[8].offsetZ;
    double centerLat = leaderLat + (offsetY / 111139.0);
    double centerLon = leaderLon + (offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0)));
    float  centerAlt = leaderAlt + offsetZ;
    qDebug() << "[圆形编队] 8号机作为圆心，其当前坐标为:"
             << "纬度:" << QString::number(centerLat, 'f', 7)
             << "经度:" << QString::number(centerLon, 'f', 7)
             << "高度:" << QString::number(centerAlt, 'f', 2);

    // 记录1号机起始位置
    qDebug() << "长机(1号机)初始位置:"
             << "纬度:" << QString::number(leaderLat, 'f', 7)
             << "经度:" << QString::number(leaderLon, 'f', 7)
             << "高度:" << QString::number(leaderAlt, 'f', 2);

    // 半径
    float radius = 2.0f * spacing;
    qDebug() << "圆半径:" << radius << "米";

    // 逆时针排列顺序，可自行修改
    QList<uint8_t> order = {1, 2, 4, 6, 10, 9, 7, 5, 3};
    int n = order.size();
    double angleStep = 2.0 * M_PI / double(n);
    qDebug() << "圆上无人机数量:" << n << ", 角度步长:" << angleStep * 180.0 / M_PI << "度";

    // 逐个计算新的 offset
    qDebug() << "计算圆形编队中每架无人机的新位置:";
    for (int i = 0; i < n; ++i) {
        uint8_t droneId = order[i];
        if (!droneMap.contains(droneId)) {
            qDebug() << "跳过无人机 #" << droneId << "- 不在无人机列表中";
            continue;
        }

        double theta = angleStep * i;
        float localX = -radius * std::sin(theta);
        float localY = radius * std::cos(theta);

        // 特别关注1号机
        if (droneId == 1) {
            qDebug() << "1号机圆周位置计算:";
            qDebug() << "  - 圆上索引位置:" << i;
            qDebug() << "  - 角度(theta):" << theta << "弧度 (" << theta * 180.0 / M_PI << "度)";
            qDebug() << "  - 相对于圆心的局部偏移: X=" << localX << ", Y=" << localY;
            qDebug() << "  - 之前的偏移量: X=" << droneMap[droneId].offsetX << ", Y=" << droneMap[droneId].offsetY;
        }

        // 计算相对于圆心的新偏移量
        droneMap[droneId].offsetX = centerX + localX;
        droneMap[droneId].offsetY = centerY + localY;
        droneMap[droneId].offsetZ = 0;

        if (droneId == 1) {
            qDebug() << "  - 1号机的新偏移量: X=" << droneMap[droneId].offsetX
                     << ", Y=" << droneMap[droneId].offsetY
                     << ", Z=" << droneMap[droneId].offsetZ;
        }
    }
    // 8号机保持不动（圆心）
    qDebug() << "8号机(圆心)保持在: X=" << droneMap[8].offsetX
             << ", Y=" << droneMap[8].offsetY
             << ", Z=" << droneMap[8].offsetZ;

    // 整体旋转
    qDebug() << "应用编队整体旋转: 航向角=" << m_formationHeading << "度";
    rotateEntireFormation(m_formationHeading);

    // 特别检查旋转后1号机的位置
    qDebug() << "旋转后1号机的位置: X=" << droneMap[1].offsetX
             << ", Y=" << droneMap[1].offsetY
             << ", Z=" << droneMap[1].offsetZ;

    // 添加一个辅助函数计算两点之间的距离（米）
    auto calculateDistance = [](double lat1, double lon1, double lat2, double lon2) -> double {
        // Haversine公式计算距离
        double earthRadius = 6371000; // 地球半径，单位米
        double dLat = (lat2 - lat1) * M_PI / 180.0;
        double dLon = (lon2 - lon1) * M_PI / 180.0;
        double a = sin(dLat/2) * sin(dLat/2) +
                  cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                  sin(dLon/2) * sin(dLon/2);
        double c = 2 * atan2(sqrt(a), sqrt(1-a));
        return earthRadius * c; // 返回距离，单位米
    };

    // 计算目标坐标并发送命令
    qDebug() << "计算最终GPS坐标并发送命令:";
    for (auto droneId : droneMap.keys()) {
        auto &info = droneMap[droneId];

        // 根据圆心位置和偏移量计算新的GPS坐标
        double newLat = leaderLat + info.offsetY / 111139.0;
        double newLon = leaderLon + info.offsetX / (111139.0 * std::cos(centerLat * M_PI / 180.0));
        float newAlt = centerAlt + info.offsetZ;

        // 设置目标坐标
        info.targetLat = newLat;
        info.targetLon = newLon;
        info.targetAlt = newAlt;

        // 特别记录1号机的移动信息
        // 在arrangeCircleFormation函数中，修改1号机的调试代码，增加精度

        // 特别记录1号机的移动信息
        if (droneId == 1) {
            double moveDistance = calculateDistance(leaderLat, leaderLon, newLat, newLon);
            qDebug() << "1号机移动详情:";
            qDebug() << "  - 当前位置: 纬度=" << QString::number(leaderLat, 'f', 10)
                     << ", 经度=" << QString::number(leaderLon, 'f', 10);
            qDebug() << "  - 目标位置: 纬度=" << QString::number(newLat, 'f', 10)
                     << ", 经度=" << QString::number(newLon, 'f', 10);
            qDebug() << "  - 纬度差值=" << QString::number(newLat - leaderLat, 'f', 10);
            qDebug() << "  - 经度差值=" << QString::number(newLon - leaderLon, 'f', 10);
            qDebug() << "  - 需要移动距离:" << QString::number(moveDistance, 'f', 4) << "米";

            // 检查距离是否太小
            if (moveDistance < 0.5) {
                qDebug() << "警告: 1号机移动距离太小 (小于0.5米)!";
                qDebug() << "这可能低于无人机的最小移动阈值，导致无人机不响应。";
            }
        }

        if (simulateOnly) {
            // 仅模拟：发射信号而不发真正指令
            emit formationStatusWithOffsetUpdated(
                QStringLiteral("圆形(纬度分布)"),
                droneId,
                info.offsetX,
                info.offsetY,
                info.offsetZ
            );
            qDebug() << "模拟无人机 #" << droneId << " 移动到:"
                     << "纬度=" << newLat << ", 经度=" << newLon << ", 高度=" << newAlt;
        } else {
            // 真实发指令
            emit formationStatusWithOffsetUpdated(
                QStringLiteral("圆形(纬度分布)"),
                droneId,
                info.offsetX,
                info.offsetY,
                info.offsetZ
            );

            if (m_multiDroneController && m_multiDroneController->isSerialPortOpen()) {
                // 记录发送的命令
                qDebug() << "向无人机 #" << droneId << "发送移动指令"
                         << "-> 纬度:" << newLat << ", 经度:" << newLon
                         << ", 高度:" << newAlt << ", 速度:" << speed;

                // 发送实际移动指令
                m_multiDroneController->sendCustomMavlinkMessage(droneId,
                                                               1, // 组件ID
                                                               MAV_CMD_DO_REPOSITION, // 命令类型
                                                               speed, // 速度
                                                               MAV_DO_REPOSITION_FLAGS_CHANGE_MODE, // 模式标志
                                                               0, NAN, // 其他参数
                                                               newLat, newLon, newAlt); // 目标坐标
            } else {
                qDebug() << "错误: 无法向无人机 #" << droneId
                         << "发送命令 - 串口未打开或控制器不可用";
            }
        }
    }

    qDebug() << "========== 圆形编队设置完成 ==========";

    if (!simulateOnly) {
        startCheckFormationArrival();
    }
}

// -----------------------------------------
// 拱形编队(改为5架效果)
// -----------------------------------------
void LargeFormationController::arrangeArchFormation(float spacing)
{
    if (leaderSystemId != 1) {
        emit statusMessage("Leader drone is not set to System ID 1");
        return;
    }

    clearFormationOffsets();
    printLeaderDroneInfo();


    // leader(1) 不动
    droneMap[1].offsetX = 0;
    droneMap[1].offsetZ = 0;

    droneMap[2].offsetX = -spacing;   // Y 方向偏移 -1 个间距
    droneMap[2].offsetZ = -1/2*spacing;

    droneMap[3].offsetX = spacing;   // Y 方向偏移 1 个间距
    droneMap[3].offsetZ = -1/2*spacing;


    droneMap[4].offsetX = -spacing * 2;
    droneMap[4].offsetZ = -2*spacing;

    droneMap[5].offsetX = spacing * 2;    // X 方向偏移 2 个间距
    droneMap[5].offsetZ = -2*spacing;       // Z 方向下降一个间距


//暂时忽略6-10（保留 不用注释）
    droneMap[6].offsetX = -spacing * 3;   // X 方向偏移 -3 个间距
    droneMap[6].offsetZ = -spacing;       // Z 方向下降一个间距

    droneMap[8].offsetX = -spacing * 4;   // X 方向偏移 -4 个间距
    droneMap[8].offsetZ = -2 * spacing;   // Z 方向下降两个间距

    droneMap[10].offsetX = -spacing * 5;  // X 方向偏移 -5 个间距
    droneMap[10].offsetZ = -3 * spacing;  // Z 方向下降三个间距

    droneMap[7].offsetX = spacing * 3;    // X 方向偏移 3 个间距
    droneMap[7].offsetZ = -2 * spacing;   // Z 方向下降两个间距

    droneMap[9].offsetX = spacing * 4;    // X 方向偏移 4 个间距
    droneMap[9].offsetZ = -3 * spacing;   // Z 方向下降三个间距

    applyFormationRotation();

    // 计算目标坐标
    for (auto id : droneMap.keys()) {
        auto &info = droneMap[id];
        info.targetLat =


            + info.offsetY / 111139.0;
        info.targetLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
        info.targetAlt = leaderAlt + info.offsetZ;

        emit formationStatusWithOffsetUpdated(QStringLiteral("拱形"),
                                              id,
                                              info.offsetX,
                                              info.offsetY,
                                              info.offsetZ);
    }

    startCheckFormationArrival();
}

// -----------------------------------------
// 移动长机
// -----------------------------------------
void LargeFormationController::moveLeaderDrone(double latitude,
                                               double longitude,
                                               float altitude,
                                               float speed)
{
    if (!m_multiDroneController) return;
    if (leaderSystemId == 0) return;

    m_multiDroneController->sendCustomMavlinkMessage(leaderSystemId,
                                                     1,
                                                     MAV_CMD_DO_REPOSITION,
                                                     speed,
                                                     MAV_DO_REPOSITION_FLAGS_CHANGE_MODE,
                                                     0,
                                                     NAN,
                                                     latitude,
                                                     longitude,
                                                     altitude);

    targetLatitude  = latitude;
    targetLongitude = longitude;
    targetAltitude  = altitude;

    // 简单定时检测是否到目标
    QTimer *locationCheckTimer = new QTimer(this);
    connect(locationCheckTimer, &QTimer::timeout, this, [=]() {
        if (isAtTargetLocation(targetLatitude, targetLongitude, targetAltitude)) {
            emit statusMessage("Leader reached the target location");
            emit targetReached();
            locationCheckTimer->stop();
            locationCheckTimer->deleteLater();
        }
    });
    locationCheckTimer->start(500);
}

// -----------------------------------------
// 更新单机位置(相对于长机的offset)
// -----------------------------------------
void LargeFormationController::updateDronePosition(uint8_t droneId,
                                                   double leaderLat,
                                                   double leaderLon,
                                                   float leaderAlt,
                                                   float speed)
{
    auto &info = droneMap[droneId];
    double newLat = leaderLat + info.offsetY / 111139.0;
    double newLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
    float newAlt  = leaderAlt + info.offsetZ;

    if (m_multiDroneController && m_multiDroneController->isSerialPortOpen()) {
        m_multiDroneController->sendCustomMavlinkMessage(droneId,
                                                         1,
                                                         MAV_CMD_DO_REPOSITION,
                                                         speed,
                                                         MAV_DO_REPOSITION_FLAGS_CHANGE_MODE,
                                                         0,
                                                         NAN,
                                                         newLat,
                                                         newLon,
                                                         newAlt);

        emit statusMessage(
            QString("Drone ID %1 moving to new position at speed %2 m/s")
                .arg(droneId)
                .arg(speed)
            );
    }
}





// -----------------------------------------
// 发送点火信号
// -----------------------------------------
void LargeFormationController::sendFireSignal()
{
    if (openserialFireManager && openserialFireManager->isPortOpen()) {
        QString hexString = "FFFF0601";
        QByteArray dataToSend = QByteArray::fromHex(hexString.toUtf8());
        openserialFireManager->writeData(dataToSend);
        qDebug() << "[Fire] Signal sent via serial port!";
    } else {
        qWarning() << "[Fire] Serial port not open, cannot send signal.";
    }
}

// -----------------------------------------
// 偏移移动辅助函数
// -----------------------------------------
void LargeFormationController::moveDroneToOffset(uint8_t droneId,
                                                 double latitude,
                                                 double longitude,
                                                 float altitude,
                                                 float speed,
                                                 const DroneInfo &offset)
{
    if (!m_multiDroneController || !m_multiDroneController->isSerialPortOpen()) return;

    double newLat = latitude + offset.offsetY / 111139.0;
    double newLon = longitude + offset.offsetX / (111139.0 * std::cos(latitude * M_PI / 180.0));
    float newAlt  = altitude + offset.offsetZ;

    m_multiDroneController->sendCustomMavlinkMessage(droneId,
                                                     1,
                                                     MAV_CMD_DO_REPOSITION,
                                                     speed,
                                                     MAV_DO_REPOSITION_FLAGS_CHANGE_MODE,
                                                     0,
                                                     NAN,
                                                     newLat,
                                                     newLon,
                                                     newAlt);
}

// -----------------------------------------
// 打印偏移和新的经纬度
// -----------------------------------------
void LargeFormationController::printOffsetAndPosition(int id)
{
    if (!droneMap.contains(id)) return;

    DroneInfo &info = droneMap[id];
    double newLat = leaderLat + info.offsetY / 111139.0;
    double newLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
    float newAlt  = leaderAlt + info.offsetZ;

    qDebug() << QString("Drone ID %1: OffsetX: %2, OffsetY: %3, OffsetZ: %4, NewLat: %5, NewLon: %6, Alt: %7")
                    .arg(id)
                    .arg(info.offsetX)
                    .arg(info.offsetY)
                    .arg(info.offsetZ)
                    .arg(newLat, 0, 'f', 6)
                    .arg(newLon, 0, 'f', 6)
                    .arg(newAlt, 0, 'f', 2);
}

// -----------------------------------------
// 打印长机信息
// -----------------------------------------
void LargeFormationController::printLeaderDroneInfo()
{
    if (leaderSystemId == 1) {
        qDebug() << QString("Leader Drone ID %1: Latitude: %2, Longitude: %3, Altitude: %4")
        .arg(leaderSystemId)
            .arg(leaderLat, 0, 'f', 6)
            .arg(leaderLon, 0, 'f', 6)
            .arg(leaderAlt, 0, 'f', 6);
    }
}

void LargeFormationController::moveFormationAll(float speed)
{
    // 首先确保 leaderSystemId 已经确定（如果需要）
    if (leaderSystemId == 0) {
        emit statusMessage("Leader drone is not set. Cannot move formation.");
        return;
    }

    // 遍历所有无人机
    for (auto droneId : droneMap.keys()) {

        // 先获取该无人机的目标经纬度
        DroneInfo &info = droneMap[droneId];

        // 目标经纬度在前面 arrangeXxxFormation 时已计算并存储在 info.targetLat、info.targetLon
        double targetLat = info.targetLat;
        double targetLon = info.targetLon;
        float  targetAlt = info.targetAlt;

        // 在终端打印对应飞机的目标经纬度
        qDebug() << QString("[moveFormationAll] DroneID=%1 -> TargetLat= %2, TargetLon= %3, TargetAlt= %4")
                        .arg(droneId)
                        .arg(targetLat, 0, 'f', 6)
                        .arg(targetLon, 0, 'f', 6)
                        .arg(targetAlt, 0, 'f', 2);

        // 调用 updateDronePosition(...) 发送移动指令
        updateDronePosition(droneId, leaderLat, leaderLon, leaderAlt, speed);
    }

    // 输出提示信息
    emit statusMessage(QString("moveFormationAll: 已向 %1 架无人机发送移动指令, 速度=%2")
                           .arg(droneMap.size())
                           .arg(speed));
}





// -----------------------------------------
// 接收无人机位置(更新 DroneMap 中对应无人机当前位置)
// -----------------------------------------
void LargeFormationController::onDronesPositionUpdated(uint8_t droneId,
                                                       double latitude,
                                                       double longitude,
                                                       float altitude)
{
    if (droneMap.contains(droneId)) {
        DroneInfo &info = droneMap[droneId];
        info.currentLat       = latitude;
        info.currentLon       = longitude;
        info.currentAlt       = altitude;
        info.hasPositionData  = true;
    }
}

// -----------------------------------------
// 启动编队到位检查定时器
// -----------------------------------------
void LargeFormationController::startCheckFormationArrival()
{
    if (m_checkTimer) {
        m_checkTimer->stop();
        m_checkTimer->deleteLater();
        m_checkTimer = nullptr;
    }

    m_checkTimer = new QTimer(this);
    connect(m_checkTimer, &QTimer::timeout, this, [this]() {
        if (areAllDronesAtFormationPositions()) {
            QString successMsg = QStringLiteral("所有无人机已到达编队位置");
            emit statusMessage(successMsg);
            qDebug() << successMsg;

            // 停止定时器并释放
            m_checkTimer->stop();
            m_checkTimer->deleteLater();
            m_checkTimer = nullptr;

            // 如果需要，在这里自动触发点火
            sendFireSignal();
        }
    });

    // 每 0.5 秒检查一次
    m_checkTimer->start(500);
}

// -----------------------------------------
// 判断所有无人机是否到达各自编队目标位置
// -----------------------------------------
bool LargeFormationController::areAllDronesAtFormationPositions()
{
    constexpr double LAT_LON_THRESHOLD = 0.00001;
    constexpr float ALT_THRESHOLD      = 1.0f;

    for (auto it = droneMap.begin(); it != droneMap.end(); ++it) {
        const DroneInfo &info = it.value();
        if (!info.hasPositionData) {
            // 如果还没有位置数据，则认为还没到达
            return false;
        }

        double latDiff = std::abs(info.currentLat - info.targetLat);
        double lonDiff = std::abs(info.currentLon - info.targetLon);
        float altDiff  = std::abs(info.currentAlt - info.targetAlt);

        if (latDiff > LAT_LON_THRESHOLD ||
            lonDiff > LAT_LON_THRESHOLD ||
            altDiff > ALT_THRESHOLD)
        {
            return false;
        }
    }
    return true;
}

// -----------------------------------------
// 设置编队整体偏航角(度)
// -----------------------------------------
void LargeFormationController::setFormationYaw(float yawDeg)
{
    m_formationHeading = yawDeg;
    qDebug() << "Formation yaw set to:" << m_formationHeading << "degrees.";
}

// -----------------------------------------
// 对整个编队进行一次旋转
// -----------------------------------------
void LargeFormationController::applyFormationRotation()
{
    double yawRad = m_formationHeading * M_PI / 180.0;
    for (auto &info : droneMap) {
        float oldX = info.offsetX;
        float oldY = info.offsetY;

        float newX = oldX * std::cos(yawRad) - oldY * std::sin(yawRad);
        float newY = oldX * std::sin(yawRad) + oldY * std::cos(yawRad);

        info.offsetX = newX;
        info.offsetY = newY;
    }
}

// -----------------------------------------
// 强行旋转编队(可外部直接调用)
// -----------------------------------------
void LargeFormationController::rotateEntireFormation(float angleDeg)
{
    double theta = angleDeg * M_PI / 180.0;
    for (auto &info : droneMap) {
        float oldX = info.offsetX;
        float oldY = info.offsetY;

        float newX = oldX * std::cos(theta) - oldY * std::sin(theta);
        float newY = oldX * std::sin(theta) + oldY * std::cos(theta);

        info.offsetX = newX;
        info.offsetY = newY;
    }
}

// -----------------------------------------
// 获取当前 droneMap
// -----------------------------------------
QMap<uint8_t, DroneInfo> LargeFormationController::getDroneMap() const
{
    return droneMap;
}

// -----------------------------------------
// 处理长机(1号机)位置更新(如果外面监听到1号机的经纬度...)
// -----------------------------------------
void LargeFormationController::handleSystemIdOnePosition(double latitude,
                                                         double longitude,
                                                         float altitude)
{
    leaderLat = latitude;
    leaderLon = longitude;
    leaderAlt = altitude;
    emit statusMessage(
        QString("Leader Position Updated: Lat %1, Lon %2, Alt %3")
            .arg(latitude, 0, 'f', 6)
            .arg(longitude, 0, 'f', 6)
            .arg(altitude, 0, 'f', 2)
        );
}

// -----------------------------------------
// 如果有跟随模式，可用此函数更新长机动态位置
// -----------------------------------------
void LargeFormationController::updateLeaderPosition(double latitude,
                                                    double longitude,
                                                    float altitude)
{
    currentLatitude  = latitude;
    currentLongitude = longitude;
    currentAltitude  = altitude;
}

// -----------------------------------------
// 判断长机是否到达目标
// -----------------------------------------
bool LargeFormationController::isAtTargetLocation(double targetLat,
                                                  double targetLon,
                                                  float targetAlt)
{
    constexpr double LAT_LON_THRESHOLD = 0.00001;
    constexpr float ALT_THRESHOLD      = 1.0f;

    bool latClose = std::abs(currentLatitude  - targetLat ) < LAT_LON_THRESHOLD;
    bool lonClose = std::abs(currentLongitude - targetLon ) < LAT_LON_THRESHOLD;
    bool altClose = std::abs(currentAltitude  - targetAlt ) < ALT_THRESHOLD;

    return (latClose && lonClose && altClose);
}


//动态八字（半成品）
QVariantList LargeFormationController::generateFigure8Waypoints(double R, double speed, int N) {
    QVariantList waypoints;
    if (N <= 0) N = 100;
    for (int i = 0; i < N; ++i) {
        double phi = 2 * M_PI * i / N;
        double localX = R * std::sin(2 * phi);
        double localY = R * std::sin(phi);
        // 转为地理坐标
        double lat = leaderLat + localY / 111139.0;
        double lon = leaderLon + localX / (111139.0 * std::cos(leaderLat * M_PI/180.0));
        QVariantMap wp;
        wp["holdTime"] = 0;
        wp["radius"]   = 0;
        wp["unused"]   = 0;
        wp["yaw"]      = 0;
        wp["lat"]      = lat;
        wp["lon"]      = lon;
        wp["alt"]      = leaderAlt;
        wp["speed"]    = speed;
        waypoints.append(wp);
    }
    return waypoints;
}



QVariantList LargeFormationController::generateWaveFormationWaypoints(
    uint8_t  droneId,
    float    amplitude,
    float    wavelength,
    float    waveSpeed,
    float    moveSpeed,
    float    durationSec,
    int      N)
{
    QVariantList waypoints;

    /* ----------- 参数合法性检查 ----------- */
    if (!droneMap.contains(droneId) || N < 2 || wavelength <= 0 || durationSec <= 0) {
        qWarning() << "[WaveGen] 参数错误：无人机不存在或 N/λ/时间 非法";
        return waypoints;
    }

    /* ----------- ① 一字形基准经纬高 ----------- */
    const DroneInfo &info = droneMap[droneId];
    double baseLat = leaderLat + info.offsetY / 111139.0;
    double baseLon = leaderLon + info.offsetX / (111139.0 * std::cos(leaderLat * M_PI / 180.0));
    float  baseAlt = leaderAlt + info.offsetZ;

    /* ----------- ② 预计算波动 / 平移参数 ----------- */
    double k    = 2.0 * M_PI / wavelength;   // 波数
    double phi0 = k * info.offsetX;          // 初始空间相位
    double dt   = durationSec / (N - 1);     // 采样时间步

    /* ----------- ③ 生成 N 个航点 ----------- */
    for (int i = 0; i < N; ++i) {
        double t       = i * dt;                           // 当前时刻
        double zOffset = amplitude * std::sin(k * waveSpeed * t - phi0);
        double latOff  = (moveSpeed * t) / 111139.0;       // 北正南负

        QVariantMap wp;
        wp["holdTime"] = 0;
        wp["radius"]   = 0;
        wp["unused"]   = 0;
        wp["yaw"]      = 0;
        wp["lat"]      = baseLat + latOff;
        wp["lon"]      = baseLon;
        wp["alt"]      = baseAlt + zOffset;
        wp["speed"]    = waveSpeed;

        waypoints.append(wp);
    }

    qDebug() << "[WaveGen] Drone" << droneId
             << ": A=" << amplitude
             << " λ=" << wavelength
             << " v波=" << waveSpeed
             << " v移=" << moveSpeed
             << " T=" << durationSec
             << " N=" << N;

    return waypoints;
}




