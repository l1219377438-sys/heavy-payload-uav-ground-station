#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QWebChannel>
#include <QDir>
#include <QFileInfo>
#include "localmapserver.h"




#include "SerialPortManager.h"
#include "MAVLinkParser.h"
#include "DroneController.h"
#include "AttitudeIndicator.h"
#include "multicontroller.h"
#include "LargeFormationController.h"
#include "filemanager.h"
#include "MissionPlanner.h"
#include "TextToSpeechManager.h"
#include "rtkclient.h"




int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QtWebEngineQuick::initialize();

    // Listen before QML loads the page. No Python process or console window needed.
    LocalMapServer mapServer;
    const QString externalMap = QDir(QCoreApplication::applicationDirPath()).filePath("gaode.html");
    const QString mapPath = QFileInfo::exists(externalMap) ? externalMap : QStringLiteral(":/map/gaode.html");
    QString mapServerError;
    if (!mapServer.start(mapPath)) {
        mapServerError = QStringLiteral("地图服务启动失败，请检查 gaode.html 是否可读并重启程序。");
        qWarning() << mapServerError << mapServer.errorString();
    } else {
        qInfo() << "[Map]" << mapServer.pageUrl();
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("mapPageUrl", mapServer.pageUrl());
    engine.rootContext()->setContextProperty("mapServerError", mapServerError);


    // 注册 供 QML 中使用
    qmlRegisterType<AttitudeIndicator>("ShowAttitude", 1, 0, "AttitudeIndicator");
    qmlRegisterType<TextToSpeechManager>("TextToSpeech", 1, 0, "TextToSpeechManager");




    // 创建 SerialPortManager 实例
    SerialPortManager *serialManager = new SerialPortManager(&app);
    SerialPortManager *serialManagerFire = new SerialPortManager(&app);
    // 创建 MAVLinkParser 实例，并传入 serialManager
    MAVLinkParser *parser = new MAVLinkParser(serialManager, &app);
    // 创建 DroneController 实例，直接传入 serialManager（与 MAVLinkParser 类似）
    DroneController *droneController = new DroneController(serialManager, &app);

    RtkClient *rtkClient = new RtkClient(&app);

    MultiController *multiController = new MultiController(serialManager, parser,rtkClient, &app);

    MissionPlanner *missionPlanner  = new MissionPlanner(serialManager, &app);

    LargeFormationController *formationController = new LargeFormationController(serialManagerFire, multiController, missionPlanner,&app);

    FileManager *fileManager = new FileManager(&app);






    // 将实例暴露给 QML
    engine.rootContext()->setContextProperty("SerialPortManager", serialManager);
    engine.rootContext()->setContextProperty("SerialPortManagerFire", serialManagerFire);
    engine.rootContext()->setContextProperty("MAVLinkParser", parser);
    engine.rootContext()->setContextProperty("DroneController", droneController);
    engine.rootContext()->setContextProperty("multiController", multiController);
    engine.rootContext()->setContextProperty("formationController", formationController);
    engine.rootContext()->setContextProperty("MissionPlanner", missionPlanner);
    engine.rootContext()->setContextProperty("fileManager", fileManager);
    engine.rootContext()->setContextProperty("rtkClient", rtkClient);




    QObject::connect(parser,
                     &MAVLinkParser::systemIdOnePositionwgs,
                     formationController,
                     &LargeFormationController::handleSystemIdOnePosition);
    QObject::connect(parser,
                     &MAVLinkParser::missionMessageReceived,
                     missionPlanner,
                     &MissionPlanner::handleMissionMessage);
    QObject::connect(parser,
                     &MAVLinkParser::systemIdOnePositionwgs,
                     rtkClient,
                     &RtkClient::setGGAPosition);




    // rtkClient->setEndpoint("120.253.239.161",      // ← 换成你的 Caster 地址
    //                        8002,                         // 端口
    //                        "RTCM33_GRCE");                     // 挂载点
    // rtkClient->setAuth("csar9275", "sna74450");               // 没有账号可注释此行

    // // 初始坐标仅作示例）
    // rtkClient->setGGAPosition(30.572269, 104.066541, 35.0);

    // // 开启 GGA 心跳（5 s 一帧）
    // rtkClient->enableGGAHeartbeat(true, 5000);
    // rtkClient->connectToCaster();

    // // NTRIP 连接出错时打印日志，必要时可重试/报警
    // QObject::connect(rtkClient, &RtkClient::errorOccurred,
    //                  [](const QString &e){ qWarning() << "[RTK]" << e; });


    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("rebulid", "Main");

    return app.exec();
}
