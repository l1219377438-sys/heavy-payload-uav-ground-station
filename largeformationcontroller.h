#include <QObject>
#include <QMap>
#include <QTimer>
#include "SerialPortManager.h"
#include "MultiController.h"
#include "MissionPlanner.h"

// -----------------------------------------
// 存储单架无人机的位置信息、编队偏移等
// -----------------------------------------
struct DroneInfo {
    uint8_t id             = 0;       // 无人机ID
    float offsetX          = 0.0f;    // 编队在“纬度方向”上的偏移（单位：米）
    float offsetY          = 0.0f;    // 如果需要在经度方向做特殊处理，可自行再用
    float offsetZ          = 0.0f;    // 高度偏移

    double currentLat      = 0.0;     // 实时纬度
    double currentLon      = 0.0;     // 实时经度
    float  currentAlt      = 0.0f;    // 实时海拔高度

    double targetLat       = 0.0;     // 目标纬度
    double targetLon       = 0.0;     // 目标经度
    float  targetAlt       = 0.0f;    // 目标海拔高度

    bool hasPositionData   = false;   // 是否已收到此机的位置信息
};

// -----------------------------------------
// 大编队控制器
// -----------------------------------------
class LargeFormationController : public QObject
{
    Q_OBJECT

public:
    explicit LargeFormationController(SerialPortManager* fireSerialManager,
                                      MultiController* multiDroneController,
                                      MissionPlanner* missionPlanner,
                                      QObject* parent = nullptr);

    // -----------------------------------------
    // 编队逻辑
    // -----------------------------------------
    // 处理新检测到的无人机ID
    Q_INVOKABLE void handleNewDroneSystemIdDetected(uint8_t systemId);

    // 将无人机加入编队
    Q_INVOKABLE void addDroneToFormation(uint8_t systemId);

    // 清空全部偏移量
    Q_INVOKABLE void clearFormationOffsets();

    // 一字形编队（沿纬度）
    Q_INVOKABLE void arrangeLineFormation(float spacing);

    // 人字形编队（沿纬度）
    Q_INVOKABLE void arrangeVFormation(float spacing);

    // 川字形编队（沿纬度）
    Q_INVOKABLE void arrangeCustomFormation(float spacing);
    void setCustomFormationLatitude(float spacing); // 川字形内部调用

    // W形编队（沿纬度）
    Q_INVOKABLE void arrangeWFormation(float spacing);

    // 十字形编队（沿纬度）
    Q_INVOKABLE void arrangeCrossFormation(float spacing);

    // 圆形编队（基于川字形分布 + 旋转）
    Q_INVOKABLE void arrangeCircleFormation(float spacing,
                                            float speed,
                                            bool simulateOnly = false);

    // 拱形编队（沿纬度）
    Q_INVOKABLE void arrangeArchFormation(float spacing);


    //垂直一字型
    Q_INVOKABLE void arrangeVerticalLineFormation(float spacing);

    //5架飞机圆形编队
    Q_INVOKABLE void arrangeFiveCircleFormation(float spacing);

    //动态8字
    Q_INVOKABLE QVariantList generateFigure8Waypoints(double radius, double speed, int count);


    // -----------------------------------------
    // 无人机移动与飞行模式
    // -----------------------------------------
    Q_INVOKABLE void moveLeaderDrone(double latitude,
                                     double longitude,
                                     float altitude,
                                     float speed);

    Q_INVOKABLE void updateDronePosition(uint8_t droneId,
                                         double leaderLat,
                                         double leaderLon,
                                         float leaderAlt,
                                         float speed);

    Q_INVOKABLE void moveFormationAll(float speed);


    // 示例：若需要切换飞行模式（暂留示例）
    // Q_INVOKABLE void setFlightMode(uint8_t modeId);

    // -----------------------------------------
    // 点火信号
    // -----------------------------------------
    Q_INVOKABLE void sendFireSignal();

    Q_INVOKABLE void updateFormationWithOrder(float speed, int interval, QList<QList<uint8_t>> moveOrder); // 执行编队更新，按顺序移动








    // 偏移移动辅助
    void moveDroneToOffset(uint8_t droneId,
                           double latitude,
                           double longitude,
                           float altitude,
                           float speed,
                           const DroneInfo &offset);

    // -----------------------------------------
    // 调试与输出
    // -----------------------------------------
    Q_INVOKABLE void printOffsetAndPosition(int id);
    Q_INVOKABLE void printLeaderDroneInfo();

    // -----------------------------------------
    // 无人机实时位置更新
    // -----------------------------------------
    Q_INVOKABLE void onDronesPositionUpdated(uint8_t droneId,
                                             double latitude,
                                             double longitude,
                                             float altitude);

    // -----------------------------------------
    // 检查编队是否到位
    // -----------------------------------------
    void startCheckFormationArrival();
    bool areAllDronesAtFormationPositions();

    // -----------------------------------------
    // 编队整体旋转
    // -----------------------------------------
    void applyFormationRotation();
    void rotateEntireFormation(float angleDeg);

    // 设置编队偏航角（度）
    Q_INVOKABLE void setFormationYaw(float yawDeg);

    // 获取当前的无人机列表
    QMap<uint8_t, DroneInfo> getDroneMap() const;

    // -----------------------------------------
    // 长机位置更新
    // -----------------------------------------
    Q_INVOKABLE void handleSystemIdOnePosition(double latitude,
                                               double longitude,
                                               float altitude);

    // 如果外部需要动态更新长机位置（例如跟随模式）
    Q_INVOKABLE void updateLeaderPosition(double latitude,
                                          double longitude,
                                          float altitude);

    bool isAtTargetLocation(double targetLat,
                            double targetLon,
                            float targetAlt);



signals:
    // 用于在界面上显示状态或调试信息
    void statusMessage(const QString &message);

    // 无人机编队偏移结果（例如UI可监听显示）
    void formationStatusWithOffsetUpdated(const QString &formationType,
                                          int droneId,
                                          float offsetX,
                                          float offsetY,
                                          float offsetZ);

    // 当长机到达目标位置时
    void targetReached();


private:
    // -----------------------------------------
    // 数据成员
    // -----------------------------------------
    // 无人机ID -> DroneInfo
    QMap<uint8_t, DroneInfo> droneMap;

    // 长机ID（默认为0表示还未设置）
    uint8_t leaderSystemId = 0;

    // 长机的（初始/基准）位置
    double leaderLat = 0.0;
    double leaderLon = 0.0;
    float  leaderAlt = 0.0f;

    // 点火串口
    SerialPortManager* openserialFireManager = nullptr;

    // MultiController，用于发命令
    MultiController* m_multiDroneController = nullptr;

    MissionPlanner*      m_missionPlanner;
    // 计时器，用于检查所有无人机是否到达目标
    QTimer* m_checkTimer = nullptr;

    // 编队的整体朝向（偏航角，度）
    float m_formationHeading = 0.0f;

    // 如果需要动态更新长机位置
    double currentLatitude  = 0.0;
    double currentLongitude = 0.0;
    float  currentAltitude  = 0.0f;

    // 如果需要判断长机是否到达某目标位置
    double targetLatitude   = 0.0;
    double targetLongitude  = 0.0;
    float  targetAltitude   = 0.0f;


    bool m_isOrderedMovementRunning = false;
    QList<QList<uint8_t>> m_currentMoveOrder;
    int  m_currentBatchIndex = 0;
    int  m_currentDroneIndex = 0;
    float m_orderSpeed = 0.0f;
    int   m_orderInterval = 1000;

    void startNextOrderedMovement();

public:
/**
 * @brief generateWaveFormationWaypoints
 * @param droneId      系统 ID
 * @param amplitude    波峰振幅 A (m)
 * @param wavelength   空间波长 λ (m)
 * @param waveSpeed    相速度 v₁ (m/s)
 * @param moveSpeed    平移速度 v₂ (m/s，北正南负)
 * @param durationSec  整个运动持续时间 (s)
 * @param N            采样点数 (≥2)，决定离散精度
 */
    Q_INVOKABLE QVariantList generateWaveFormationWaypoints(
        uint8_t  droneId,
        float    amplitude,
        float    wavelength,
        float    waveSpeed,
        float    moveSpeed,
        float    durationSec,
        int      N );





};





