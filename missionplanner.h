/**************************************************************
 * MissionPlanner.h —— 多机航点上传 / 下载 / 清除（含速度）
 *************************************************************/
#ifndef MISSIONPLANNER_H
#define MISSIONPLANNER_H

#include <QObject>
#include <QVariantList>
#include <QHash>
#include <vector>

#include "SerialPortManager.h"
#include <mavlink/common/mavlink.h>
#include <mavlink/ardupilotmega/ardupilotmega.h>

/* ====== 下载结果专用结构 ====== */
struct Waypoint {
    float  holdTime {0};
    float  radius   {0};
    float  unused   {0};
    float  yaw      {0};
    double lat      {0};
    double lon      {0};
    float  alt      {0};
    float  speed    {0};
};

/* ====== ★★ 上传会话结构 —— 新增 ====== */
struct UploadSession
{
    std::vector<mavlink_message_t> items;   ///< 178/16 已打包
    std::vector<bool>              sent;    ///< 去重标记
};

class MissionPlanner : public QObject
{
    Q_OBJECT
public:
    explicit MissionPlanner(SerialPortManager *mgr,
                            QObject *parent = nullptr);

    Q_INVOKABLE void uploadWaypoints (int sysid, const QVariantList &wps);
    Q_INVOKABLE void downloadWaypoints(int sysid);
    Q_INVOKABLE void clearWaypoints   (int sysid);

signals:
    void downloadFinished(const QVariantList &payload);

public slots:
    void handleMissionMessage(const mavlink_message_t &msg);

private:
    void sendRaw(const mavlink_message_t &m);

    /* ---------- 成员 ---------- */
    SerialPortManager *m_port {nullptr};

    /* ★★ 上传改为会话表 (sysid → UploadSession) */
    QHash<uint8_t, UploadSession>   m_upSessions;

    /* 下载流程仍按一次只拉一架机处理（如需并行再扩展） */
    enum DState { Idle, WaitCount, Downloading } d_state { Idle };
    int                              d_sysid   {1};
    uint16_t                         d_expect  {0};
    uint16_t                         d_recv    {0};
    std::vector<Waypoint>            d_buf;
};

#endif // MISSIONPLANNER_H
