  import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtWebEngine 1.9
import Qt.labs.platform 1.1 as Platform
import QtQuick.Layouts 1.15   // 放在文件头部
import TextToSpeech 1.0  // 引入 QtSpeech 模块
import QtWebChannel 1.0
import "coordinateUtils.js" as Coord

import "FormationLanding.js" as Landing
import "Theme.js" as Theme



import ShowAttitude 1.0

Window {
    visible: true
    id: root
    width: 1280
    height: 800
    minimumWidth: 1024
    minimumHeight: 640
    color: Theme.background
    title: qsTr("无人机烟花地面站")
    property var missionList: []    // 存放所有待上传的航点
    // missionList 是 JS 数组，push 不会触发绑定，用计数驱动按钮状态
    property int missionCount: 0

    // 窄窗口时收紧布局（仪表区隐藏经纬度、任务面板变窄）
    readonly property bool compact: width < 1360
    // 抽屉可用高度：顶部状态栏以下，留出上下边距
    readonly property real drawerMaxHeight: height - 56 - 32

    // ============ 操作反馈：底部提示条 ============
    function showToast(message, level) {
        toast.text = message
        toast.level = level || "info"
        toast.opacity = 1
        toastTimer.restart()
    }

    // ============ 危险操作二次确认 ============
    function confirmAction(title, message, action) {
        confirmDialog.title = title
        confirmDialog.message = message
        confirmDialog.action = action
        confirmDialog.open()
    }

    // 串口未打开时阻止发送指令并提示
    function requireLink() {
        if (SerialPortManager.isPortOpen)
            return true
        showToast("飞控串口未打开，请先在“链路设置”中连接", "error")
        return false
    }

    function currentSystemId() {
        return parseInt(aircraftIdComboBox.currentText)
    }

    RtkConfigWindow { id: rtkConfigWindow }

    // RTK 状态，显示在顶部状态栏
    property string rtkStateText: "未连接"
    property color rtkStateColor: Theme.idle

    Connections {
        target: rtkClient

        function onConnected() {
            console.log("[UI] RTK 已连接")
            rtkStateText = "已连接"
            rtkStateColor = Theme.success
        }

        function onErrorOccurred(err) {
            console.warn("[UI] RTK 错误：" + err)
            rtkStateText = "错误"
            rtkStateColor = Theme.danger
        }

        function onDisconnected() {
            if (rtkStateText !== "错误") {
                rtkStateText = "已断开"
                rtkStateColor = Theme.warning
            }
        }
    }



    TextToSpeechManager {
        id: tts
    }




    // 声明“桥对象”和 WebChannel
    QtObject {
        id: coordinateHandler
        WebChannel.id: "coordinateHandler"

      // 当 JS 调用 window.bridge.sendCoordinates 时，会触发这里
        function sendCoordinates(lat, lon) {
        // 保留 6 位小数
            waypointLatitudeField.text = lat.toFixed(7)
            waypointLongitudeField.text = lon.toFixed(7)
      }
    }
    // —— WebChannel 实例 ——
    WebChannel {
        id: webChannel
        registeredObjects: [ coordinateHandler ]
    }

    // 缓存最新位置：网页加载期间不丢数据，100 ms 批量刷新，避免频繁跨进程调用。
    property var mapDronePositions: ({})
    property var mapPendingDrones: ({})
    property var mapFormationPositions: ({})
    property bool mapPositionsDirty: false
    property bool mapMissionDirty: true

    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            if (!gaodeMapView.pageReady)
                return
            var script = ""
            if (mapPositionsDirty) {
                for (var id in mapPendingDrones)
                    script += "window.updateDronePosition && window.updateDronePosition.apply(null," + JSON.stringify(mapPendingDrones[id]) + ");"
                for (var formationId in mapFormationPositions)
                    script += "window.updateDronePositionWithFormation && window.updateDronePositionWithFormation.apply(null," + JSON.stringify(mapFormationPositions[formationId]) + ");"
                mapPositionsDirty = false
                mapPendingDrones = ({})
                // 模拟只刷新有变化的点，避免清除后被遥测刷新重新绘制。
                mapFormationPositions = ({})
            }
            if (mapMissionDirty) {
                script += "window.setMissionWaypoints && window.setMissionWaypoints(" + JSON.stringify(missionList) + ");"
                mapMissionDirty = false
            }
            if (script.length > 0)
                gaodeMapView.runJavaScript(script)
        }
    }

    // 地图显示区域，页面自行加载 Qt 的 qwebchannel.js。
    WebEngineView {
        id: gaodeMapView
        anchors.fill: parent
        url: mapPageUrl
        property bool pageReady: false

        webChannel: webChannel
        onLoadingChanged: function(request) {
            if (request.status === WebEngineView.LoadStartedStatus)
                pageReady = false
            if (request.status === WebEngineView.LoadSucceededStatus) {
                pageReady = true
                mapPendingDrones = mapDronePositions
                mapPositionsDirty = true
                mapMissionDirty = true
                runJavaScript("window.connectQtBridge && window.connectQtBridge();")
            } else if (request.status === WebEngineView.LoadFailedStatus) {
                pageReady = false
                console.warn("地图加载失败，请检查地图文件并重启程序：", request.errorString)
            }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 420)
        height: mapErrorText.implicitHeight + 24
        color: Theme.panelSolid
        border.color: Theme.danger
        radius: Theme.radius
        visible: mapServerError.length > 0
        Label {
            id: mapErrorText
            anchors.centerIn: parent
            width: parent.width - 24
            text: mapServerError
            color: "#fca5a5"
            wrapMode: Text.WordWrap
        }
    }


    // 保存文件对话框（QML FileDialog）
    Platform.FileDialog {
        id: saveFileDialog
        title: "保存任务"
        fileMode: Platform.FileDialog.SaveFile
        nameFilters: ["Text files (*.txt)", "All files (*)"]
        onAccepted: {
            var path = file
            if (!path) {
                console.log("用户未选择文件")
                return
            }

            var localPath = path.toString()
            if (localPath.startsWith("file://")) {
                if (Qt.platform.os === "windows") {
                    localPath = localPath.replace("file:///", "")
                } else {
                    localPath = localPath.replace("file://", "")
                }
            }

            console.log("保存到: " + localPath)
            if (fileManager.saveText(localPath, formationTaskList.text)) {
                console.log("保存成功")
            } else {
                console.log("保存失败")
            }
        }
    }


    // 加载文件对话框（QML FileDialog）
    Platform.FileDialog {
        id: loadFileDialog
        title: "加载任务"
        fileMode: Platform.FileDialog.OpenFile
        nameFilters: ["Text files (*.txt)", "All files (*)"]
        onAccepted: {
            var path = file
            if (!path) {
                console.log("用户未选择文件")
                return
            }
            var localPath = path.toString()
            if (localPath.startsWith("file://")) {
                if (Qt.platform.os === "windows") {
                    localPath = localPath.replace("file:///", "")
                } else {
                    localPath = localPath.replace("file://", "")
                }
            }
            console.log("加载任务文件路径: " + localPath)
            var content = fileManager.readText(localPath)
            if (content !== "") {
                // 解析文本并更新 formationTasks 数组
                parseTasks(content)
                console.log("任务加载并解析成功")
            } else {
                console.log("任务加载失败")
            }
        }
    }



    // 保存长机(1号机)位置
    property double currentLatitude: 0
    property double currentLongitude: 0
    property double currentAltitude: 0


    // 用于保存多个编队任务的列表
    property var formationTasks: []

    // ==============================
    // 1. 用于连续编队的属性
    // ==============================
    property bool isMultiFormationRunning: false    // 是否在进行多机编队任务
    property int  currentTaskIndex: 0              // 当前执行到第几个任务
    // ==============================

    //对应id的飞机显示
    Connections {
        target: MAVLinkParser
        function onSystemIdOnePositionwgs(lat, lon, alt) {
            currentLatitude  = lat
            currentLongitude = lon
            currentAltitude  = alt

        }
        function onNewAttitudeData(sysid, roll, pitch, yaw) {
            attitudeIndicator.updateAttitude(sysid, roll, pitch, yaw)
        }
        function onNewSatelliteData(sysid, satellites) {
            positionDisplay.satellites = satellites.toFixed(0)
        }
        function onNewBatteryData(sysid, voltage) {
            positionDisplay.voltage = voltage.toFixed(2)
        }
        function onNewGlobalPositionData(sysid, lat, lon, alt, realt) {
            positionDisplay.latitude = lat.toFixed(7)
            positionDisplay.longitude = lon.toFixed(7)
            positionDisplay.altitude = alt.toFixed(2)
            positionDisplay.relativeAltitude = realt.toFixed(2)
            // gaodeMapView.runJavaScript("updateDronePosition(" + lat + ", " + lon + ", " + aircraftIdComboBox.currentText + ")")
        }
        function onDronesPositionUpdated(sysid,lat,lon,alt){
            // 只保留 1~10 号机，其余直接忽略
            if (sysid < 1 || sysid > 10)
                return;
            mapDronePositions[sysid] = [sysid, lat, lon, alt]
            mapPendingDrones[sysid] = mapDronePositions[sysid]
            mapPositionsDirty = true
        }
    }




    /* ==================  动态波浪航点生成 Drawer  ================== */
    Drawer {
        id: pathPlanningDrawer
        width: 300
        edge: Qt.RightEdge
        height: pathPlanningDrawer.implicitHeight + 40
        y: parent.height / 2 - height / 2
        modal: false
        interactive: true

        background: Rectangle {
            radius: Theme.radius; border.width: 1; border.color: Theme.border
            color: Theme.panel
        }

        /* ======== UI ======== */
        contentItem: Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            Label { text: "动态波浪航点生成" ; font.pixelSize: 16; font.bold: true }

            /* 振幅 */
            Row {
                spacing: 5
                Label { text: "振幅 A (m):" ; font.pixelSize: 14 }
                TextField { id: amplitudeField; width: 120; text: "1.0"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 波长 */
            Row {
                spacing: 5
                Label { text: "波长 λ (m):" ; font.pixelSize: 14 }
                TextField { id: wavelengthField; width: 120; text: "20"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 波相速度 */
            Row {
                spacing: 5
                Label { text: "波速 v₁ (m/s):" ; font.pixelSize: 14 }
                TextField { id: waveSpeedField; width: 120; text: "0.5"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 北/南平移速度 */
            Row {
                spacing: 5
                Label { text: "平移 v₂ (m/s):" ; font.pixelSize: 14 }
                TextField { id: moveSpeedField; width: 120; text: "0"; placeholderText: "正北负南"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 持续时间 */
            Row {
                spacing: 5
                Label { text: "持续时间 T (s):" ; font.pixelSize: 14 }
                TextField { id: durationField; width: 120; text: "12"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 离散点数 */
            Row {
                spacing: 5
                Label { text: "航点数 N:" ; font.pixelSize: 14 }
                TextField { id: countField; width: 120; text: "40"; inputMethodHints: Qt.ImhDigitsOnly }
            }

            /* ======== 操作按钮 ======== */
            Column {
                spacing: 10

                Button {
                    text: "生成并上传 4-2-1-3-5 波浪航点"
                    icon.source: "qrc:/icons/wave.svg"
                    onClicked: {
                        /* 读取参数 */
                        var A = parseFloat(amplitudeField.text)
                        var L = parseFloat(wavelengthField.text)
                        var v1 = parseFloat(waveSpeedField.text)
                        var v2 = parseFloat(moveSpeedField.text)
                        var T  = parseFloat(durationField.text)
                        var N  = parseInt(countField.text)

                        if ([A,L,v1,v2,T].some(isNaN) || isNaN(N) || N < 2) {
                            console.log("参数无效，无法生成")
                            return
                        }

                        var droneIds = [4, 2, 1, 3, 5]

                        for (var i = 0; i < droneIds.length; ++i) {
                            var id  = droneIds[i]
                            var wps = formationController.generateWaveFormationWaypoints(
                                        id, A, L, v1, v2, T, N)
                            console.log("Drone", id, "生成航点", wps.length)
                            MissionPlanner.uploadWaypoints(id, wps)
                        }
                        console.log("已全部上传波浪航点")
                    }
                }

                Button {
                    text: "全部开始任务"
                    icon.source: "qrc:/icons/play.svg"
                    onClicked: {
                        var ids = [4, 2, 1, 3, 5]
                        for (var i = 0; i < ids.length; ++i)
                            MissionPlanner.startMission(ids[i])
                        console.log("已发送 startMission 到 4-2-1-3-5")
                    }
                }
            }
        }
    }



    // 接收下载完成后的列表，打印出每个 waypoint.speed
    Connections {
        target: MissionPlanner
        function onDownloadFinished(payload) {
            console.log("下载回读成功，共", payload.length, "条")
            for (var i = 0; i < payload.length; i++) {
                console.log("WP", i, "speed=", payload[i].speed)
            }
        }
    }

    // 右侧抽屉：单机控制
    Drawer {
        id: singleControlDrawer
        width: 240
        height: Math.min(singleControlContent.implicitHeight + 32, drawerMaxHeight)
        y: 56 + 16
        edge: Qt.RightEdge
        modal: false
        interactive: true

        background: Rectangle {
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
            color: Theme.panel
        }

        // 内容超出窗口高度时可滚动
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            padding: 16

        Column {
            id: singleControlContent
            width: parent.width
            spacing: 12

            // 起飞高度输入
            Row {
                spacing: 5
                Label {
                    text: "起飞高度:"
                    font.pixelSize: 14
                }
                TextField {
                    id: takeoffAltitudeField
                    width: 100
                    placeholderText: "默认1米"
                    font.pixelSize: 14
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    text: "1"
                }
            }

            // 起飞、降落、上锁、模式切换（2×2 网格，带图标；串口未开时提示，危险操作二次确认）
            Grid {
                columns: 2
                spacing: 8
                Button {
                    width: 96
                    text: "起飞"
                    icon.source: "qrc:/icons/takeoff.svg"
                    font.pixelSize: 15
                    highlighted: true
                    onClicked: {
                        if (!requireLink()) return
                        var systemId = currentSystemId()
                        var altitude = parseFloat(takeoffAltitudeField.text)
                        if (isNaN(altitude) || altitude <= 0) {
                            showToast("起飞高度无效，请输入大于 0 的数值", "error")
                            return
                        }
                        confirmAction("确认起飞", systemId + " 号机将起飞到 " + altitude + " 米。", function() {
                            DroneController.takeoff(systemId, altitude)
                            console.log("发送起飞指令，ID:", systemId, "高度:", altitude)
                            tts.speak("起飞，高度， " + altitude + " 米")
                            showToast(systemId + " 号机起飞指令已发送", "success")
                        })
                    }
                }
                Button {
                    width: 96
                    text: "降落"
                    icon.source: "qrc:/icons/land.svg"
                    font.pixelSize: 15
                    onClicked: {
                        if (!requireLink()) return
                        var systemId = currentSystemId()
                        DroneController.land(systemId)
                        console.log("发送降落指令，ID:", systemId)
                        showToast(systemId + " 号机降落指令已发送", "success")
                    }
                }
                Button {
                    width: 96
                    text: "上锁"
                    icon.source: "qrc:/icons/lock.svg"
                    font.pixelSize: 15
                    Material.background: Theme.danger
                    onClicked: {
                        if (!requireLink()) return
                        var systemId = currentSystemId()
                        confirmAction("确认上锁", "上锁会立即停转电机。空中执行会导致 " + systemId + " 号机坠落，确认继续？", function() {
                            DroneController.lockMotors(systemId)
                            console.log("发送上锁指令，ID:", systemId)
                            showToast(systemId + " 号机上锁指令已发送", "warning")
                        })
                    }
                }
                Button {
                    width: 96
                    text: "任务模式"
                    icon.source: "qrc:/icons/route.svg"
                    font.pixelSize: 15
                    onClicked: {
                        if (!requireLink()) return
                        var systemId = currentSystemId()
                        DroneController.switchToMissionMode(systemId)
                        showToast(systemId + " 号机已请求切换到任务模式", "success")
                    }
                }
            }

            // 航点任务输入区域
            Column {
                id: waypointTaskSection
                spacing: 5

                Label {
                    text: "航点任务参数"
                    font.pixelSize: 16
                    font.bold: true
                }

                Row {
                    spacing: 5
                    Label {
                        text: "纬度:"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: waypointLatitudeField
                        width: 100
                        placeholderText: "输入纬度"
                        font.pixelSize: 14
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                }
                Row {
                    spacing: 5
                    Label {
                        text: "经度:"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: waypointLongitudeField
                        width: 100
                        placeholderText: "输入经度"
                        font.pixelSize: 14
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                }
                Row {
                    spacing: 5
                    Label {
                        text: "海拔:"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: waypointAltitudeField
                        width: 100
                        placeholderText: "输入海拔"
                        font.pixelSize: 14
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                }
                Row {
                    spacing: 5
                    Label {
                        text: "停留时间:"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: waypointDwellField
                        width: 100
                        placeholderText: "输入停留时间"
                        font.pixelSize: 14
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                }
                Row {
                    spacing: 5
                    Label {
                        text: "飞行速度:"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: waypointSpeedField
                        width: 100
                        placeholderText: "输入速度"
                        font.pixelSize: 14
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                }
                Row {
                    spacing: 8

                    Button {
                        width: 100
                        text: "添加航点"
                        icon.source: "qrc:/icons/plus.svg"
                        onClicked: {
                            var gcjlat   = parseFloat(waypointLatitudeField.text)
                            var gcjlon   = parseFloat(waypointLongitudeField.text)
                            var alt   = parseFloat(waypointAltitudeField.text)
                            var dwell = parseFloat(waypointDwellField.text)
                            var speed = parseFloat(waypointSpeedField.text)
                            if (isNaN(gcjlat)||isNaN(gcjlon)||isNaN(alt)||isNaN(dwell)||isNaN(speed)) {
                                showToast("航点参数不完整：请填写纬度、经度、海拔、停留时间和速度", "error")
                                return
                            }

                            var coord = Coord.gcj02ToWgs84(gcjlat, gcjlon)
                            if (!coord || isNaN(coord.lat) || isNaN(coord.lon)) {
                                showToast("坐标转换失败，请检查经纬度", "error")
                                return
                            }
                            missionList.push({
                                holdTime: dwell,
                                radius:   0,
                                unused:   0,
                                yaw:      0,
                                lat:      coord.lat,
                                lon:      coord.lon,
                                alt:      alt,
                                speed:    speed
                            })
                            console.log("已添加航点，当前列表长度=", missionList.length)
                            mapMissionDirty = true
                            missionCount = missionList.length
                            showToast("已添加航点 " + missionList.length, "success")
                        }
                    }

                    Button {
                        width: 100
                        text: "上传航点"
                        icon.source: "qrc:/icons/upload.svg"
                        enabled: missionCount > 0
                        onClicked: {
                            if (!requireLink()) return
                            var systemId = currentSystemId()
                            MissionPlanner.uploadWaypoints(systemId, missionList)
                            showToast("正在向 " + systemId + " 号机上传 " + missionList.length + " 个航点", "info")
                        }
                    }
                }

                // 第二行：下载航点 & 清除航点
                Row {
                    spacing: 8

                    Button {
                        width: 100
                        text: "下载航点"
                        icon.source: "qrc:/icons/download.svg"
                        onClicked: {
                            if (!requireLink()) return
                            MissionPlanner.downloadWaypoints(currentSystemId())
                            showToast("正在从 " + currentSystemId() + " 号机下载航点", "info")
                        }
                    }

                    Button {
                        width: 100
                        text: "清除航点"
                        icon.source: "qrc:/icons/delete.svg"
                        flat: true
                        Material.foreground: Theme.danger
                        onClicked: {
                            var systemId = currentSystemId()
                            confirmAction("清除航点", "将清除 " + systemId + " 号机上的航点任务和本地航点列表，确认继续？", function() {
                                if (SerialPortManager.isPortOpen)
                                    MissionPlanner.clearWaypoints(systemId)
                                missionList = []
                                missionCount = 0
                                mapMissionDirty = true
                                showToast("航点已清除", "success")
                            })
                        }
                    }
                }

                // 接收下载完成后的列表
                Connections {
                    target: MissionPlanner
                    function onDownloadFinished(payload) {
                        missionList = payload
                        missionCount = payload.length
                        mapMissionDirty = true
                        showToast("已下载 " + payload.length + " 个航点", "success")
                    }
                }

                Label {
                    text: missionCount > 0 ? "本地航点：" + missionCount + " 个" : "本地暂无航点，可点击地图选取坐标"
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
            }
        }
        }
    }

    // 右侧抽屉：编队/多机控制
    Drawer {
        id: formationDrawer
        width: 300
        height: Math.min(formationContent.implicitHeight + 32, drawerMaxHeight)
        y: 56 + 16
        edge: Qt.RightEdge
        modal: false
        interactive: true

        background: Rectangle {
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
            color: Theme.panel
        }

        // 内容超出窗口高度时可滚动
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            padding: 16

        Column {
            id: formationContent
            width: parent.width
            spacing: 12

            // 起飞高度输入
            Row {
                spacing: 5
                Label {
                    text: "起飞高度:"
                    font.pixelSize: 14
                }
                TextField {
                    id: multiTakeoffAltitude
                    width: 100
                    placeholderText: "默认1米"
                    font.pixelSize: 14
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    text: "1"
                }
            }

            // 多机起飞、降落、上锁（串口未开时提示，起飞/上锁二次确认）
            Row {
                spacing: 8
                Button {
                    width: 96
                    text: "起飞"
                    icon.source: "qrc:/icons/takeoff.svg"
                    font.pixelSize: 14
                    highlighted: true
                    onClicked: {
                        if (!requireLink()) return
                        var altitude = parseFloat(multiTakeoffAltitude.text)
                        if (isNaN(altitude) || altitude <= 0) {
                            showToast("起飞高度无效，请输入大于 0 的数值", "error")
                            return
                        }
                        confirmAction("确认多机起飞", "所有已连接的飞机将起飞到 " + altitude + " 米。", function() {
                            multiController.takeoffAll(altitude)
                            console.log("调用 multiController 进行多机起飞，高度 = " + altitude)
                            showToast("多机起飞指令已发送", "success")
                        })
                    }
                }
                Button {
                    width: 96
                    text: "降落"
                    icon.source: "qrc:/icons/land.svg"
                    font.pixelSize: 14
                    onClicked: {
                        if (!requireLink()) return
                        var mode = formationModeComboBox.currentText
                        Landing.landAllDronesByFormation(multiController, mode, root)
                        showToast("按“" + mode + "”顺序执行多机降落", "success")
                    }
                }
            }
            Button {
                width: 200
                text: "全部上锁"
                icon.source: "qrc:/icons/lock.svg"
                font.pixelSize: 14
                Material.background: Theme.danger
                onClicked: {
                    if (!requireLink()) return
                    confirmAction("确认全部上锁", "上锁会立即停转所有飞机的电机。空中执行会导致坠落，确认继续？", function() {
                        multiController.lockAll()
                        console.log("调用 multiController 进行多机上锁")
                        showToast("多机上锁指令已发送", "warning")
                    })
                }
            }

            // 编队参数输入
            Row {
                spacing: 5
                Label {
                    text: "编队模式:"
                    font.pixelSize: 14
                }
                ComboBox {
                    id: formationModeComboBox
                    width: 120
                    font.pixelSize: 14
                    model: ["一字形", "垂直一字", "人字形", "拱形", "竖圆形","W形", "十字形", "川字形", "圆形"]
                    currentIndex: 0
                }
            }
            Row {
                spacing: 5
                Label {
                    text: "编队间距:"
                    font.pixelSize: 14
                }
                TextField {
                    id: formationSpacingField
                    width: 100
                    placeholderText: "默认10"
                    font.pixelSize: 14
                    text: "10"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }
            Row {
                spacing: 5
                Label {
                    text: "编队速度:"
                    font.pixelSize: 14
                }
                TextField {
                    id: formationSpeedField
                    width: 100
                    placeholderText: "0.5"
                    font.pixelSize: 14
                    text: "0.5"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }
            Row {
                spacing: 5
                Label {
                    text: "编队角度:"
                    font.pixelSize: 14
                }
                TextField {
                    id: formationHeadingField
                    width: 100
                    placeholderText: "0"
                    font.pixelSize: 14
                    text: "0"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }

            // 新增触发时间输入与“开始任务”等按钮
            Row {
                spacing: 10

                Button {
                    text: "开始任务"
                    font.pixelSize: 13
                    onClicked: {
                        //=========================
                        // 2. 点击后开始连续编队
                        //=========================
                        currentTaskIndex = 0
                        isMultiFormationRunning = true
                        // 若计时器没开，先开
                        if (!timer.running) {
                            timer.running = true
                        }
                        console.log("开始连续编队任务，共 " + formationTasks.length + " 个。")
                    }
                }
                Button {
                    text: "编队飞行"
                    font.pixelSize: 13
                    padding: 8
                    onClicked: {
                        var yaw = parseFloat(formationHeadingField.text)
                        if (isNaN(yaw)) {
                            yaw = 0
                            console.log("编队角度无效，默认0")
                        }
                        formationController.setFormationYaw(yaw)
                        console.log("设置编队角度为:" + yaw)

                        var mode = formationModeComboBox.currentText
                        var spacing = parseFloat(formationSpacingField.text)
                        var speed = parseFloat(formationSpeedField.text)

                        if (isNaN(spacing) || spacing <= 0) {
                            console.log("请输入有效的正数作为编队间距")
                            return
                        }
                        if (isNaN(speed) || speed <= 0) {
                            console.log("请输入有效的正数作为编队速度")
                            return
                        }

                        if (mode === "圆形") {
                            formationController.arrangeCircleFormation(spacing, speed, false)
                            console.log("调用 圆形编队")
                            return
                        } else if (mode === "川字形") {
                            formationController.arrangeCustomFormation(spacing)
                            console.log("调用 川字形编队")
                            var moveOrder = [[10], [9], [8], [7], [6], [5], [4]]
                            formationController.updateFormationWithOrder(speed, 10000, moveOrder)
                            return
                        } else if (mode === "一字形") {
                            formationController.arrangeLineFormation(spacing)
                            console.log("调用 一字形编队")
                        } else if (mode === "垂直一字") {
                            formationController.arrangeVerticalLineFormation(spacing)
                            console.log("调用垂直一字编队")
                        } else if (mode === "人字形") {
                            formationController.arrangeVFormation(spacing)
                            console.log("调用 人字形编队")
                        } else if (mode === "拱形") {
                            formationController.arrangeArchFormation(spacing)
                            console.log("调用 拱形编队")
                        } else if (mode === "竖圆形") {
                            formationController.arrangeFiveCircleFormation(spacing)
                            console.log("调用 竖圆形编队")
                        } else if (mode === "W形") {
                            formationController.arrangeWFormation(spacing)
                            console.log("调用 W形编队")
                        } else if (mode === "十字形") {
                            formationController.arrangeCrossFormation(spacing)
                            console.log("调用 十字形编队")
                        } else {
                            console.log("请选择有效的编队模式")
                            return
                        }
                        formationController.moveFormationAll(speed)
                    }
                }

                Button {
                    text: "模拟编队"
                    font.pixelSize: 13
                    padding: 8
                    onClicked: {
                        var spacing = parseFloat(formationSpacingField.text)
                        var speed = parseFloat(formationSpeedField.text)
                        var yaw   = parseFloat(formationHeadingField.text)
                        formationController.setFormationYaw(isNaN(yaw) ? 0 : yaw)
                        var mode = formationModeComboBox.currentText
                        if (mode === "圆形") {
                            formationController.arrangeCircleFormation(spacing, speed, true)
                        } else if (mode === "川字形") {
                            formationController.arrangeCustomFormation(spacing)
                        } else if (mode === "一字形") {
                            formationController.arrangeLineFormation(spacing)
                        } else if (mode === "人字形") {
                            formationController.arrangeVFormation(spacing)
                        } else if (mode === "拱形") {
                            formationController.arrangeArchFormation(spacing)
                        } else if (mode === "垂直一字") {
                            formationController.arrangeVerticalLineFormation(spacing)
                        } else if (mode === "竖圆形") {
                            formationController.arrangeFiveCircleFormation(spacing)
                        } else if (mode === "W形") {
                            formationController.arrangeWFormation(spacing)
                        } else if (mode === "十字形") {
                            formationController.arrangeCrossFormation(spacing)
                        } else {
                            console.log("无效编队模式,无法模拟")
                            return
                        }
                    }
                }
            }

        }
        }
    }

    Connections {
        target: formationController
        function onFormationStatusWithOffsetUpdated(formationType, droneId, offsetX, offsetY, offsetZ) {
            var baseLat = currentLatitude
            var baseLon = currentLongitude
            if (!isFinite(baseLat) || !isFinite(baseLon) || (baseLat === 0 && baseLon === 0)) {
                console.warn("尚未收到长机 GPS 位置，无法显示编队模拟")
                return
            }
            var lat = baseLat + offsetY / 111139.0
            var lon = baseLon + offsetX / (111139.0 * Math.cos(baseLat * Math.PI / 180.0))
            console.log("[Simulate] DroneID=", droneId,
                        " => Lat=", lat.toFixed(6),
                        " Lon=", lon.toFixed(6),
                        " Z=", offsetZ)
            mapFormationPositions[droneId] = [droneId, lat, lon, offsetZ]
            mapPositionsDirty = true
        }
    }

    // 状态栏单项：状态灯 + 标题 + 数值
    component StatusItem: Row {
        id: statusItem
        property string label
        property string value
        property color dotColor: Theme.idle
        spacing: 8
        Rectangle {
            width: 8; height: 8; radius: 4
            color: statusItem.dotColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            Label { text: statusItem.label; font.pixelSize: 11; color: Theme.textMuted }
            Label {
                text: statusItem.value
                width: Math.min(implicitWidth, 180)
                elide: Text.ElideRight
                font.pixelSize: 14; font.bold: true
            }
        }
    }

    // 仪表区数值：小标题 + 大号等宽数字
    component TelemetryValue: Column {
        id: telemetryValue
        property string label
        property string value
        property string unit: ""
        spacing: 2
        Label { text: telemetryValue.label; font.pixelSize: 11; color: Theme.textMuted }
        Label {
            text: telemetryValue.value + (telemetryValue.unit.length > 0 && telemetryValue.value !== "N/A" ? " " + telemetryValue.unit : "")
            font.pixelSize: 18; font.bold: true
            font.family: Theme.mono
        }
    }

    // 叠加层：顶部状态栏、左侧工具栏、底部仪表与任务面板
    Item {
        id: overlay
        anchors.fill: parent
        z: 1

        // ============ 顶部状态栏 ============
        Rectangle {
            id: topBar
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 56
            color: Theme.panel

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Theme.border
            }

            Row {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 24

                Label {
                    text: "无人机烟花地面站"
                    font.pixelSize: 17; font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle { width: 1; height: 28; color: Theme.border; anchors.verticalCenter: parent.verticalCenter }

                StatusItem {
                    label: "飞控链路"
                    value: SerialPortManager.isPortOpen ? "已连接" : "未连接"
                    dotColor: SerialPortManager.isPortOpen ? Theme.success : Theme.idle
                }
                StatusItem {
                    label: "点火链路"
                    value: SerialPortManagerFire.isPortOpen ? "已连接" : "未连接"
                    dotColor: SerialPortManagerFire.isPortOpen ? Theme.success : Theme.idle
                }
                StatusItem {
                    label: "RTK"
                    value: rtkStateText
                    dotColor: rtkStateColor
                }
                StatusItem {
                    property int sats: parseInt(positionDisplay.satellites)
                    label: "卫星"
                    value: positionDisplay.satellites
                    dotColor: isNaN(sats) ? Theme.idle
                            : sats >= 10 ? Theme.success
                            : sats >= 6 ? Theme.warning : Theme.danger
                }
                StatusItem {
                    label: "电压"
                    value: positionDisplay.voltage === "N/A" ? "N/A" : positionDisplay.voltage + " V"
                    dotColor: positionDisplay.voltage === "N/A" ? Theme.idle : Theme.accent
                }
            }

            Row {
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 12

                Label {
                    text: "当前飞机"
                    color: Theme.textMuted
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
                ComboBox {
                    id: aircraftIdComboBox
                    font.pixelSize: 14
                    width: 96
                    anchors.verticalCenter: parent.verticalCenter
                    model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10"]
                    currentIndex: 0
                    displayText: "#" + currentText
                    onCurrentTextChanged: {
                        MAVLinkParser.setSelectedSystemId(parseInt(currentText))
                    }
                    Component.onCompleted: {
                        MAVLinkParser.setSelectedSystemId(parseInt(currentText))
                    }
                }
                Button {
                    text: "链路设置"
                    icon.source: "qrc:/icons/link.svg"
                    font.pixelSize: 14
                    highlighted: connectionPopup.opened
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: connectionPopup.opened ? connectionPopup.close() : connectionPopup.open()
                }
            }
        }

        // ============ 链路设置弹窗（串口） ============
        Popup {
            id: connectionPopup
            x: parent.width - width - 16
            y: topBar.height + 8
            padding: 16
            background: Rectangle {
                radius: Theme.radius
                color: Theme.panelSolid
                border.color: Theme.border
            }

            Column {
                spacing: 12

                Label { text: "链路设置"; font.pixelSize: 15; font.bold: true }

                // 飞机串口部分
                Row {
                    spacing: 8
                    Label {
                        text: "飞控串口"
                        width: 64
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        id: serialportComboBox
                        font.pixelSize: 14
                        width: 120
                        model: SerialPortManager.availablePorts
                    }
                    ComboBox {
                        id: baudComboBox
                        font.pixelSize: 14
                        width: 110
                        model: ["9600", "57600", "115200"]
                    }
                    Button {
                        id: btnAirplane
                        text: SerialPortManager.buttonText
                        font.pixelSize: 14
                        width: 100
                        Material.background: SerialPortManager.isPortOpen ? Theme.danger : Theme.success
                        onClicked: {
                            SerialPortManager.togglePort(serialportComboBox.currentText, baudComboBox.currentText)
                        }
                    }
                }

                // 点火串口部分
                Row {
                    spacing: 8
                    Label {
                        text: "点火串口"
                        width: 64
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        id: serialportComboBoxFire
                        font.pixelSize: 14
                        width: 120
                        model: SerialPortManagerFire.availablePorts
                    }
                    ComboBox {
                        id: baudComboBoxFire
                        font.pixelSize: 14
                        width: 110
                        model: ["9600", "57600", "115200"]
                    }
                    Button {
                        id: btnIgnition
                        text: SerialPortManagerFire.buttonText
                        font.pixelSize: 14
                        width: 100
                        Material.background: SerialPortManagerFire.isPortOpen ? Theme.danger : Theme.success
                        onClicked: {
                            SerialPortManagerFire.togglePort(serialportComboBoxFire.currentText, baudComboBoxFire.currentText)
                        }
                    }
                }

                Label {
                    text: "波特率需与数传/点火模块一致"
                    font.pixelSize: 12
                    color: Theme.textFaint
                }
            }
        }

        // ============ 左侧工具栏 ============
        Rectangle {
            anchors {
                left: parent.left
                top: topBar.bottom
                leftMargin: 12
                topMargin: 16
            }
            width: leftButtonColumn.implicitWidth + 16
            height: leftButtonColumn.implicitHeight + 16
            radius: Theme.radius
            color: Theme.panel
            border.color: Theme.border

            Column {
                id: leftButtonColumn
                anchors.centerIn: parent
                spacing: 4

                Button {
                    width: 104
                    text: "单机控制"
                    icon.source: "qrc:/icons/drone.svg"
                    font.pixelSize: 15
                    highlighted: singleControlDrawer.opened
                    onClicked: {
                        formationDrawer.close()
                        pathPlanningDrawer.close()
                        singleControlDrawer.open()
                    }
                }
                Button {
                    width: 104
                    text: "多机控制"
                    icon.source: "qrc:/icons/formation.svg"
                    font.pixelSize: 15
                    highlighted: formationDrawer.opened
                    onClicked: {
                        singleControlDrawer.close()
                        pathPlanningDrawer.close()
                        formationDrawer.open()
                    }
                }
                Button {
                    width: 104
                    text: "动态编队"
                    icon.source: "qrc:/icons/wave.svg"
                    font.pixelSize: 15
                    highlighted: pathPlanningDrawer.opened
                    onClicked: {
                        singleControlDrawer.close()
                        formationDrawer.close()
                        pathPlanningDrawer.open()
                    }
                }
                Rectangle { width: 104; height: 1; color: Theme.border }
                Button {
                    width: 104
                    text: "RTK 设置"
                    icon.source: "qrc:/icons/rtk.svg"
                    font.pixelSize: 15
                    flat: true
                    onClicked: rtkConfigWindow.open()
                }
            }
        }

        // ============ 左下仪表区：姿态仪 + 遥测数值 ============
        Rectangle {
            id: positionDisplay
            anchors {
                left: parent.left
                bottom: parent.bottom
                margins: 16
            }
            width: instrumentRow.implicitWidth + 24
            height: instrumentRow.implicitHeight + 24
            radius: Theme.radius
            color: Theme.panel
            border.color: Theme.border

            property string voltage: "N/A"
            property string latitude: "N/A"
            property string longitude: "N/A"
            property string altitude: "N/A"
            property string relativeAltitude: "N/A"
            property string satellites: "N/A"

            Row {
                id: instrumentRow
                anchors.centerIn: parent
                spacing: 20

                // 姿态显示控件
                AttitudeIndicator {
                    id: attitudeIndicator
                    width: 180
                    height: 180
                    roll: 0
                    pitch: 0
                    yaw: 0
                    altitude: 0

                    function updateAttitude(sysid, roll, pitch, yaw) {
                        attitudeIndicator.roll = roll
                        attitudeIndicator.pitch = pitch
                        attitudeIndicator.yaw = yaw
                    }
                }

                GridLayout {
                    anchors.verticalCenter: parent.verticalCenter
                    columns: 3
                    columnSpacing: 28
                    rowSpacing: 16

                    TelemetryValue { label: "海拔"; value: positionDisplay.altitude; unit: "m" }
                    TelemetryValue { label: "对地高度"; value: positionDisplay.relativeAltitude; unit: "m" }
                    TelemetryValue { label: "电压"; value: positionDisplay.voltage; unit: "V" }
                    TelemetryValue { label: "卫星数量"; value: positionDisplay.satellites }
                    TelemetryValue { label: "纬度"; value: positionDisplay.latitude }
                    TelemetryValue { label: "经度"; value: positionDisplay.longitude }
                }
            }
        }

        // ============ 右下任务时间轴：计时器及编队任务 ============
        Rectangle {
            id: timerItem
            anchors {
                right: parent.right
                bottom: parent.bottom
                margins: 16
            }
            width: missionColumn.implicitWidth + 24
            height: missionColumn.implicitHeight + 24
            radius: Theme.radius
            color: Theme.panel
            border.color: Theme.border

            // 记录累计时间（秒）
            property double elapsedTime: 0
            property bool expanded: true

            // 以真实时钟计时：界面卡顿时不会少计时间，刷新频率也从 100 Hz 降到 50 Hz
            property double runStartMs: 0
            property double runBaseTime: 0

            Timer {
                id: timer
                interval: 20
                repeat: true
                running: false

                onRunningChanged: {
                    if (running) {
                        timerItem.runStartMs = Date.now()
                        timerItem.runBaseTime = timerItem.elapsedTime
                    }
                }

                onTriggered: {
                    timerItem.elapsedTime = timerItem.runBaseTime + (Date.now() - timerItem.runStartMs) / 1000.0
                    var now = timerItem.elapsedTime

                    // ================================
                    // 3. 在计时器里检查触发时机
                    // ================================
                    if (isMultiFormationRunning && currentTaskIndex < formationTasks.length) {
                        var task = formationTasks[currentTaskIndex]
                        if (!task.done && now >= task.triggerTime) {
                            doFormationTask(task)
                            task.done = true
                            currentTaskIndex=currentTaskIndex+1
                            if (currentTaskIndex >= formationTasks.length) {
                                isMultiFormationRunning = false
                            }
                        }
                    }

                }
            }

            Column {
                id: missionColumn
                anchors.centerIn: parent
                spacing: 10

                // 标题行：计时文本、继续/暂停/重置、折叠
                Row {
                    spacing: 12
                    Label {
                        text: "任务时间轴"
                        font.pixelSize: 15; font.bold: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        id: timerDisplay
                        width: 130
                        text: timerItem.elapsedTime.toFixed(3) + " s"
                        font.pixelSize: 26
                        font.family: Theme.mono
                        color: timer.running ? Theme.accent : Theme.text
                        horizontalAlignment: Text.AlignRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Button {
                        text: "继续"
                        icon.source: "qrc:/icons/play.svg"
                        font.pixelSize: 15
                        highlighted: true
                        onClicked: { timer.running = true }
                    }
                    Button {
                        text: "暂停"
                        icon.source: "qrc:/icons/pause.svg"
                        font.pixelSize: 15
                        onClicked: { timer.running = false }
                    }
                    Button {
                        text: "重置"
                        icon.source: "qrc:/icons/reset.svg"
                        font.pixelSize: 15
                        flat: true
                        onClicked: { timer.running = false
                            timerItem.elapsedTime = 0
                        }
                    }
                    ToolButton {
                        icon.source: timerItem.expanded ? "qrc:/icons/chevron-down.svg" : "qrc:/icons/chevron-up.svg"
                        ToolTip.visible: hovered
                        ToolTip.text: timerItem.expanded ? "收起" : "展开"
                        font.pixelSize: 18
                        onClicked: timerItem.expanded = !timerItem.expanded
                    }
                }

                // 添加编队任务
                Row {
                    spacing: 10
                    visible: timerItem.expanded
                    TextField {
                        id: formationTaskNameField
                        width: 140
                        placeholderText: "编队任务名称"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: triggerTimeField
                        width: 120
                        placeholderText: "触发时间(秒)"
                        font.pixelSize: 14
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    Button {
                        text: "添加编队任务"
                        icon.source: "qrc:/icons/plus.svg"
                        font.pixelSize: 15
                        onClicked: {
                            var mode    = formationModeComboBox.currentText
                            var spacing = parseFloat(formationSpacingField.text)
                            var speed   = parseFloat(formationSpeedField.text)
                            var yaw     = parseFloat(formationHeadingField.text)
                            var triggerTime = parseFloat(triggerTimeField.text)

                            if (isNaN(yaw))          yaw = 0
                            if (isNaN(spacing))      spacing = 10
                            if (isNaN(speed))        speed = 0.5
                            if (isNaN(triggerTime))  triggerTime = 0

                            var task = {
                                name: formationTaskNameField.text || "无名称",
                                mode: mode,
                                spacing: spacing,
                                speed: speed,
                                yaw: yaw,
                                triggerTime: triggerTime,
                                done: false
                            }
                            formationTasks.push(task)
                            console.log("添加编队任务:", JSON.stringify(task))
                            updateFormationTaskListText()
                            formationTaskNameField.text = ""
                            triggerTimeField.text = ""  // 清空触发时间
                        }
                    }
                }

                // 保存、加载、清除任务按钮
                Row {
                    spacing: 10
                    visible: timerItem.expanded
                    Button {
                        text: "保存任务"
                        icon.source: "qrc:/icons/save.svg"
                        flat: true
                        onClicked: saveFileDialog.open()
                    }
                    Button {
                        text: "加载任务"
                        icon.source: "qrc:/icons/open.svg"
                        flat: true
                        onClicked: loadFileDialog.open()
                    }
                    Button {
                        text: "清除任务"
                        icon.source: "qrc:/icons/delete.svg"
                        flat: true
                        Material.foreground: Theme.danger
                        onClicked: {
                            formationTaskList.text = ""
                            formationTasks = []  // 如果你有保存任务列表的数组，也一起清除
                            console.log("任务已清除")
                        }
                    }
                }

                // 任务列表
                Rectangle {
                    visible: timerItem.expanded
                    width: 560
                    height: 140
                    color: Theme.input
                    radius: 6
                    border.color: Theme.border
                    ScrollView {
                        anchors.fill: parent
                        TextArea {
                            id: formationTaskList
                            width: parent.width
                            height: parent.height
                            readOnly: true
                            placeholderText: "暂无编队任务"
                            wrapMode: TextEdit.WrapAnywhere
                            font.pixelSize: 14
                        }
                    }
                }
            }
        }
    }

    // ============ 底部提示条 ============
    Rectangle {
        id: toast
        property string text: ""
        property string level: "info"   // info | success | warning | error
        z: 100
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 56 + 16
        width: Math.min(toastRow.implicitWidth + 32, parent.width - 64)
        height: 40
        radius: 20
        color: Theme.panelSolid
        border.width: 1
        border.color: level === "error" ? Theme.danger
                    : level === "warning" ? Theme.warning
                    : level === "success" ? Theme.success : Theme.accent
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Row {
            id: toastRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                width: 8; height: 8; radius: 4
                color: toast.border.color
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                text: toast.text
                font.pixelSize: 14
                elide: Text.ElideRight
                width: Math.min(implicitWidth, root.width - 120)
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: toast.opacity = 0
        }

        Timer {
            id: toastTimer
            interval: 3000
            onTriggered: toast.opacity = 0
        }
    }

    // ============ 危险操作确认对话框 ============
    Dialog {
        id: confirmDialog
        property string message: ""
        property var action: null
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(400, root.width - 64)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        Label {
            width: parent.width
            text: confirmDialog.message
            wrapMode: Text.WordWrap
            font.pixelSize: 14
        }

        onAccepted: {
            var fn = action
            action = null
            if (typeof fn === "function")
                fn()
        }
        onRejected: action = null
    }

    // 更新任务列表显示文本
    function updateFormationTaskListText() {
        var textContent = ""
        for (var i = 0; i < formationTasks.length; i++) {
            var t = formationTasks[i]
            textContent += "任务" + (i + 1) + ": " +
                           t.name + " 模式：" + t.mode + "  " +
                           "间距：" + t.spacing + "  " +
                           "速度：" + t.speed + "  " +
                           "角度：" + t.yaw + "  " +
                           "触发时间：" + String(t.triggerTime) + "秒\n"
        }
        formationTaskList.text = textContent
    }



    // ================================
    // 4. 用于执行编队任务的函数
    // ================================
    function doFormationTask(task) {

        // 设置编队角度
        formationController.setFormationYaw(task.yaw)


        // 根据不同模式进行调度
        if (task.mode === "圆形") {
            formationController.arrangeCircleFormation(task.spacing, task.speed, false)
            return
        } else if (task.mode === "川字形") {
            formationController.arrangeCustomFormation(task.spacing)
            var moveOrder = [[10],[9],[8],[7],[6],[5],[4]]
            formationController.updateFormationWithOrder(task.speed, 10000, moveOrder)
            return
        } else if (task.mode === "一字形") {
            formationController.arrangeLineFormation(task.spacing)
        } else if (task.mode === "垂直一字") {
            formationController.arrangeVerticalLineFormation(task.spacing)
        } else if (task.mode === "人字形") {
            formationController.arrangeVFormation(task.spacing)
        } else if (task.mode === "拱形") {
            formationController.arrangeArchFormation(task.spacing)
        } else if (task.mode === "竖圆形") {
            formationController.arrangeFiveCircleFormation(task.spacing)
        } else if (task.mode === "W形") {
            formationController.arrangeWFormation(task.spacing)
        } else if (task.mode === "十字形") {
            formationController.arrangeCrossFormation(task.spacing)
        } else {
            console.log("未知编队模式:", task.mode, "，无法执行。")
            return
        }
        formationController.moveFormationAll(task.speed)
    }
    function parseTasks(text) {
        // 清空原有任务数组
        formationTasks = []
        var lines = text.split("\n")
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i].trim()
            if (line.length === 0) continue

            // 假定格式为：
            // "任务1: 任务名称 模式：模式值  间距：数值  速度：数值  角度：数值  触发时间：数值秒"
            try {
                var colonIndex = line.indexOf(":")
                var name = line.substring(colonIndex + 1, line.indexOf(" 模式：")).trim()
                var mode = line.substring(line.indexOf("模式：") + 3, line.indexOf("间距：")).trim()
                var spacing = parseFloat(line.substring(line.indexOf("间距：") + 3, line.indexOf("速度：")).trim())
                var speed = parseFloat(line.substring(line.indexOf("速度：") + 3, line.indexOf("角度：")).trim())
                var yaw = parseFloat(line.substring(line.indexOf("角度：") + 3, line.indexOf("触发时间：")).trim())
                var triggerTimeStr = line.substring(line.indexOf("触发时间：") + 5).replace("秒", "").trim()
                var triggerTime = parseFloat(triggerTimeStr)

                var task = {
                    name: name,
                    mode: mode,
                    spacing: spacing,
                    speed: speed,
                    yaw: yaw,
                    triggerTime: triggerTime,
                    done: false
                }
                formationTasks.push(task)
            } catch (e) {
                console.log("解析任务失败：", line, e)
            }
        }
        // 调用更新任务显示的函数（如果已有）
        updateFormationTaskListText()
    }

}
