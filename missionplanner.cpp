 /**************************************************************
 * MissionPlanner.cpp —— 实现（支持并行上传）
 *************************************************************/
#include "MissionPlanner.h"
#include <QDebug>
#include <QMetaObject>

static constexpr uint8_t GCS_SYS_ID  = 250;
static constexpr uint8_t GCS_COMP_ID = MAV_COMP_ID_MISSIONPLANNER;

/*=================== 构造 ===================*/
MissionPlanner::MissionPlanner(SerialPortManager *mgr,QObject *parent)
    : QObject(parent), m_port(mgr) {}

/*============================================================
 * uploadWaypoints —— 并行上传：每次调用为 sysid 创建独立会话
 *===========================================================*/
void MissionPlanner::uploadWaypoints(int sysid,const QVariantList &wps)
{
    if(!m_port || !m_port->isPortOpen() || wps.isEmpty()){
        qDebug()<<"[MissionPlanner] 串口未开或列表为空，上传取消"; return;
    }
    uint8_t sid = uint8_t(sysid);

    /* 1) QVariantList -> vector<Waypoint> */
    std::vector<Waypoint> wpv;
    wpv.reserve(wps.size());
    for(const QVariant &v:wps){
        QVariantMap m=v.toMap();
        Waypoint w;
        w.holdTime = m["holdTime"].toFloat();
        w.radius   = m["radius"].toFloat();
        w.unused   = m["unused"].toFloat();
        w.yaw      = m["yaw"].toFloat();
        w.lat      = m["lat"].toDouble();
        w.lon      = m["lon"].toDouble();
        w.alt      = m["alt"].toFloat();
        w.speed    = m["speed"].toFloat();
        wpv.push_back(w);
    }

    /* 2) 打包 ITEM_INT：178 + 16 */
    UploadSession sess;                       // ★★ 会话局部
    auto pack = [&](const mavlink_mission_item_int_t &pl){
        mavlink_message_t m;
        mavlink_msg_mission_item_int_encode(GCS_SYS_ID,GCS_COMP_ID,&m,&pl);
        sess.items.push_back(m);
    };
    for(const auto &w:wpv){
        mavlink_mission_item_int_t sp{};
        sp.target_system    = sid;
        sp.target_component = 1;
        sp.seq              = uint16_t(sess.items.size());
        sp.frame            = MAV_FRAME_MISSION;
        sp.command          = MAV_CMD_DO_CHANGE_SPEED;
        sp.param1           = 1;
        sp.param2           = w.speed;
        sp.autocontinue     = 1;
        pack(sp);

        mavlink_mission_item_int_t nav{};
        nav.target_system    = sid;
        nav.target_component = 1;
        nav.seq              = uint16_t(sess.items.size());
        nav.frame            = MAV_FRAME_GLOBAL_RELATIVE_ALT_INT;
        nav.command          = MAV_CMD_NAV_WAYPOINT;
        nav.param1           = w.holdTime;
        nav.param2           = w.radius;
        nav.param3           = w.unused;
        nav.param4           = w.yaw;
        nav.x = int32_t(w.lat*1e7);
        nav.y = int32_t(w.lon*1e7);
        nav.z = w.alt;
        nav.autocontinue     = 1;
        pack(nav);
    }
    sess.sent.assign(sess.items.size(), false);

    /* 3) 写入哈希表，若已存在则覆盖旧会话 */
    m_upSessions.insert(sid, std::move(sess));

    /* 4) 发送 MISSION_COUNT */
    mavlink_mission_count_t cnt{};
    cnt.target_system    = sid;
    cnt.target_component = 1;
    cnt.count            = uint16_t(m_upSessions[sid].items.size());
    cnt.mission_type     = MAV_MISSION_TYPE_MISSION;

    mavlink_message_t msgCnt;
    mavlink_msg_mission_count_encode(GCS_SYS_ID,GCS_COMP_ID,&msgCnt,&cnt);
    sendRaw(msgCnt);

    qDebug()<<"[MissionPlanner] (sys"<<sid<<") 发送 MISSION_COUNT ="<<cnt.count;
}

/*============================================================
 * downloadWaypoints —— 单机下载
 *===========================================================*/
void MissionPlanner::downloadWaypoints(int systemId)
{
    if(!m_port || !m_port->isPortOpen()){
        qDebug()<<"[MissionPlanner] 串口未打开，取消下载"; return;
    }
    d_sysid = systemId;
    d_state = WaitCount;
    d_buf.clear(); d_recv = 0;

    mavlink_mission_request_list_t req{};
    req.target_system    = uint8_t(d_sysid);
    req.target_component = 1;
    req.mission_type     = MAV_MISSION_TYPE_MISSION;

    mavlink_message_t msg{};
    mavlink_msg_mission_request_list_encode(GCS_SYS_ID,GCS_COMP_ID,&msg,&req);
    sendRaw(msg);

    qDebug()<<"[MissionPlanner] --> 发送 MISSION_REQUEST_LIST to sys"<<d_sysid;
}

/*============================================================
 * clearWaypoints —— 保持不变
 *===========================================================*/
void MissionPlanner::clearWaypoints(int sysid)
{
    if(!m_port || !m_port->isPortOpen()){
        qDebug()<<"[MissionPlanner] 串口未开，清除取消"; return;
    }
    mavlink_mission_clear_all_t clr{};
    clr.target_system    = uint8_t(sysid);
    clr.target_component = 1;
    clr.mission_type     = MAV_MISSION_TYPE_MISSION;

    mavlink_message_t m;
    mavlink_msg_mission_clear_all_encode(GCS_SYS_ID,GCS_COMP_ID,&m,&clr);
    sendRaw(m);
}

/*============================================================
 * handleMissionMessage —— 上传路由 + 下载完整逻辑
 *===========================================================*/
void MissionPlanner::handleMissionMessage(const mavlink_message_t &msg)
{
    uint8_t sid = msg.sysid;

    /* ---------- ★★ 上传路由 ---------- */
    if(m_upSessions.contains(sid))
    {
        UploadSession &S = m_upSessions[sid];

        if(msg.msgid == MAVLINK_MSG_ID_MISSION_REQUEST_INT){
            mavlink_mission_request_int_t r;
            mavlink_msg_mission_request_int_decode(&msg,&r);
            uint16_t s = r.seq;
            if(s < S.items.size() && !S.sent[s]){
                S.sent[s] = true;
                sendRaw(S.items[s]);
                qDebug()<<"[MissionPlanner] (sys"<<sid<<") ITEM_INT seq="<<s;
            }
            return;
        }
        if(msg.msgid == MAVLINK_MSG_ID_MISSION_ACK){
            m_upSessions.remove(sid);                 // 上传结束
            qDebug()<<"[MissionPlanner] (sys"<<sid<<") MISSION_ACK 上传完成";
            return;
        }
    }

    /* ---------- 下载阶段处理（完整保留） ---------- */
    switch (msg.msgid) {

    /* 1) 飞控返回 MISSION_COUNT */
    case MAVLINK_MSG_ID_MISSION_COUNT: {
        if (d_state != WaitCount || sid != d_sysid) break;

        mavlink_mission_count_t cnt;
        mavlink_msg_mission_count_decode(&msg,&cnt);
        d_expect = cnt.count;
        d_buf.clear();
        d_state = Downloading;
        qDebug()<<"[MissionPlanner] <-- GOT MISSION_COUNT ="<<d_expect;

        /* 请求第 0 条 ITEM_INT */
        mavlink_mission_request_int_t req0{};
        req0.target_system    = sid;
        req0.target_component = 1;
        req0.seq              = 0;
        req0.mission_type     = MAV_MISSION_TYPE_MISSION;

        mavlink_message_t mReq0;
        mavlink_msg_mission_request_int_encode(GCS_SYS_ID,GCS_COMP_ID,&mReq0,&req0);
        sendRaw(mReq0);
        break;
    }

    /* 2) ITEM_INT (ID=73) */
    case MAVLINK_MSG_ID_MISSION_ITEM_INT: {
        if (d_state != Downloading || sid != d_sysid) break;

        mavlink_mission_item_int_t it;
        mavlink_msg_mission_item_int_decode(&msg,&it);

        static float currentSpeed = 0.0f;
        if (it.command == MAV_CMD_DO_CHANGE_SPEED && it.param1 == 1) {
            currentSpeed = it.param2;
            qDebug()<<"[MissionPlanner] <-- DO_CHANGE_SPEED speed="<<currentSpeed;
        } else if (it.command == MAV_CMD_NAV_WAYPOINT) {
            Waypoint w;
            w.holdTime = it.param1;
            w.radius   = it.param2;
            w.unused   = it.param3;
            w.yaw      = it.param4;
            w.lat      = it.x/1e7;
            w.lon      = it.y/1e7;
            w.alt      = it.z;
            w.speed    = currentSpeed;
            d_buf.push_back(w);
            qDebug()<<"[MissionPlanner] <-- NAV_WAYPOINT seq="<<it.seq
                     <<" lat="<<w.lat<<" lon="<<w.lon<<" v="<<w.speed;
        }

        uint16_t nextSeq = it.seq + 1;
        if (nextSeq < d_expect) {
            mavlink_mission_request_int_t req{};
            req.target_system    = sid;
            req.target_component = 1;
            req.seq              = nextSeq;
            req.mission_type     = MAV_MISSION_TYPE_MISSION;

            mavlink_message_t mReq;
            mavlink_msg_mission_request_int_encode(GCS_SYS_ID,GCS_COMP_ID,&mReq,&req);
            sendRaw(mReq);
        } else {
            /* 全部收完 —— 发送 ACK */
            mavlink_mission_ack_t ack{};
            ack.target_system    = sid;
            ack.target_component = 1;
            ack.type             = MAV_MISSION_ACCEPTED;
            ack.mission_type     = MAV_MISSION_TYPE_MISSION;

            mavlink_message_t mAck;
            mavlink_msg_mission_ack_encode(GCS_SYS_ID,GCS_COMP_ID,&mAck,&ack);
            sendRaw(mAck);

            /* 打包回调 QML */
            QVariantList out;
            for(const Waypoint &w : d_buf){
                QVariantMap m;
                m["holdTime"]=w.holdTime; m["radius"]=w.radius; m["unused"]=w.unused;
                m["yaw"]=w.yaw; m["lat"]=w.lat; m["lon"]=w.lon; m["alt"]=w.alt; m["speed"]=w.speed;
                out<<m;
            }
            emit downloadFinished(out);
            d_state = Idle;
            qDebug()<<"[MissionPlanner] --> 下载完成，共"<<out.size()<<"条";
        }
        break;
    }

    /* 3) ITEM 浮点兼容 (ID=39) */
    case MAVLINK_MSG_ID_MISSION_ITEM: {
        if (d_state != Downloading || sid != d_sysid) break;

        mavlink_mission_item_t f;
        mavlink_msg_mission_item_decode(&msg,&f);

        static float currentSpeed = 0.0f;
        if (f.command == MAV_CMD_DO_CHANGE_SPEED && f.param1 == 1) {
            currentSpeed = f.param2;
            qDebug()<<"[MissionPlanner] <-- DO_CHANGE_SPEED speed="<<currentSpeed;
        } else if (f.command == MAV_CMD_NAV_WAYPOINT) {
            Waypoint w;
            w.holdTime = f.param1;
            w.radius   = f.param2;
            w.unused   = f.param3;
            w.yaw      = f.param4;
            w.lat      = f.x;
            w.lon      = f.y;
            w.alt      = f.z;
            w.speed    = currentSpeed;
            d_buf.push_back(w);
            qDebug()<<"[MissionPlanner] <-- NAV_WAYPOINT seq="<<f.seq
                     <<" lat="<<w.lat<<" lon="<<w.lon<<" v="<<w.speed;
        }

        uint16_t nextSeq = f.seq + 1;
        if (nextSeq < d_expect) {
            mavlink_mission_request_int_t req{};
            req.target_system    = sid;
            req.target_component = 1;
            req.seq              = nextSeq;
            req.mission_type     = MAV_MISSION_TYPE_MISSION;

            mavlink_message_t mReq;
            mavlink_msg_mission_request_int_encode(GCS_SYS_ID,GCS_COMP_ID,&mReq,&req);
            sendRaw(mReq);
        } else {
            mavlink_mission_ack_t ack{};
            ack.target_system    = sid;
            ack.target_component = 1;
            ack.type             = MAV_MISSION_ACCEPTED;
            ack.mission_type     = MAV_MISSION_TYPE_MISSION;

            mavlink_message_t mAck;
            mavlink_msg_mission_ack_encode(GCS_SYS_ID,GCS_COMP_ID,&mAck,&ack);
            sendRaw(mAck);

            QVariantList out;
            for(const Waypoint &w : d_buf){
                QVariantMap m;
                m["holdTime"]=w.holdTime; m["radius"]=w.radius; m["unused"]=w.unused;
                m["yaw"]=w.yaw; m["lat"]=w.lat; m["lon"]=w.lon; m["alt"]=w.alt; m["speed"]=w.speed;
                out<<m;
            }
            emit downloadFinished(out);
            d_state = Idle;
            qDebug()<<"[MissionPlanner] --> 下载完成，共"<<out.size()<<"条";
        }
        break;
    }

    default: break;
    } /* switch 结束 */
}

/*============================================================
 * sendRaw —— 统一串口发送
 *===========================================================*/
void MissionPlanner::sendRaw(const mavlink_message_t &m)
{
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buf,&m);
    QMetaObject::invokeMethod(m_port,"writeData",
                              Q_ARG(QByteArray,QByteArray(reinterpret_cast<char*>(buf),len)));
}
