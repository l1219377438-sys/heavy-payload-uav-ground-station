  import QtQuick 2.15
import QtQuick.Controls 2.15
import QtWebEngine 1.9
import Qt.labs.platform 1.1
import QtQuick.Layouts 1.15   // 放在文件头部
import TextToSpeech 1.0  // 引入 QtSpeech 模块
import QtWebChannel 1.0
import "coordinateUtils.js" as Coord

import "FormationLanding.js" as Landing



import ShowAttitude 1.0

Window {
    visible: true
    width: 640
    height: 480
    title: qsTr("无人机烟花地面站")
    property var missionList: []    // 存放所有待上传的航点

    RtkConfigWindow { id: rtkConfigWindow }
    // 在主 QML 文件里，任意合适位置添加：
    Connections {
        target: rtkClient

        function onConnected() {
            console.log("[UI] RTK 已连接")
            rtkStatusText.text = "RTK: 已连接"
        }

        function onErrorOccurred(err) {
            console.warn("[UI] RTK 错误：" + err)
            rtkStatusText.text = "RTK 错误：" + err
        }

        function onRtkDataReady(data) {
            // data 是 ArrayBuffer
            console.log("[UI] 收到 RTCM 数据，长度=", data.byteLength)
        }
    }


    Text {
        id: rtkStatusText
        text: "RTK: 未连接"
        anchors.top: parent.top; anchors.right: parent.right
        font.pixelSize: 14; color: "green"
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

    // 地图显示区域 在 WebEngineView 中绑定 WebChannel 并注入桥初始化脚本
    WebEngineView {
        id: gaodeMapView
        anchors.fill: parent
        url: "http://localhost:8000/try.html"

        webChannel: webChannel
        onLoadingChanged: function(request) {
            // 1. 对比 WebEngineView 自带的枚举
            if (request.status === WebEngineView.LoadSucceededStatus) {
                // 2. 注入桥初始化脚本
                runJavaScript(
                    "new QWebChannel(qt.webChannelTransport, function(channel){" +
                        "window.bridge = channel.objects.coordinateHandler;" +
                    "});"
                )
            }
        }
    }


    // 保存文件对话框（QML FileDialog）
    FileDialog {
        id: saveFileDialog
        title: "保存任务"
        fileMode: FileDialog.SaveFile
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
    FileDialog {
        id: loadFileDialog
        title: "加载任务"
        fileMode: FileDialog.OpenFile
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
            gaodeMapView.runJavaScript(
                "updateDronePosition("
                + sysid + ", "
                + lat   + ", "
                + lon   + ", "
                + alt   + ")"
            );
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
            radius: 8; border.width: 1; border.color: "#999999"
            gradient: Gradient {
                GradientStop { position: 0; color: "#F0F8FF" }
                GradientStop { position: 1; color: "#4682B4" }
            }
        }

        /* ======== UI ======== */
        contentItem: Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            Text { text: "动态波浪航点生成" ; font.pixelSize: 16; font.bold: true }

            /* 振幅 */
            Row {
                spacing: 5
                Text { text: "振幅 A (m):" ; font.pixelSize: 14 }
                TextField { id: amplitudeField; width: 120; text: "1.0"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 波长 */
            Row {
                spacing: 5
                Text { text: "波长 λ (m):" ; font.pixelSize: 14 }
                TextField { id: wavelengthField; width: 120; text: "20"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 波相速度 */
            Row {
                spacing: 5
                Text { text: "波速 v₁ (m/s):" ; font.pixelSize: 14 }
                TextField { id: waveSpeedField; width: 120; text: "0.5"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 北/南平移速度 */
            Row {
                spacing: 5
                Text { text: "平移 v₂ (m/s):" ; font.pixelSize: 14 }
                TextField { id: moveSpeedField; width: 120; text: "0"; placeholderText: "正北负南"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 持续时间 */
            Row {
                spacing: 5
                Text { text: "持续时间 T (s):" ; font.pixelSize: 14 }
                TextField { id: durationField; width: 120; text: "12"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }

            /* 离散点数 */
            Row {
                spacing: 5
                Text { text: "航点数 N:" ; font.pixelSize: 14 }
                TextField { id: countField; width: 120; text: "40"; inputMethodHints: Qt.ImhDigitsOnly }
            }

            /* ======== 操作按钮 ======== */
            Column {
                spacing: 10

                Button {
                    text: "生成并上传 4-2-1-3-5 波浪航点"
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
        width: 220
        height: singleControlContent.implicitHeight + 40
        y: parent.height/2 - height/2
        edge: Qt.RightEdge
        modal: false
        interactive: true

        background: Rectangle {
            radius: 8
            border.width: 1
            border.color: "#999999"
            gradient: Gradient {
                GradientStop { position: 0; color: "#B0C4DE" }
                GradientStop { position: 1; color: "#4682B4" }
            }
        }

        contentItem: Column {
            id: singleControlContent
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            // 起飞高度输入
            Row {
                spacing: 5
                Text {
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

            // 起飞、降落、上锁按钮
            Row {
                spacing: 10
                Button {
                    text: "起飞"
                    font.pixelSize: 16
                    padding: 8
                    onClicked: {
                        var systemId = parseInt(aircraftIdComboBox.currentText)
                        var altitude = parseFloat(takeoffAltitudeField.text)
                        if (!isNaN(systemId) && !isNaN(altitude)) {
                            DroneController.takeoff(systemId, altitude)
                            console.log("发送起飞指令，ID:", systemId, "高度:", altitude)
                            var speakText = "起飞，高度， " + altitude + " 米"
                            tts.speak(speakText)  // 播放语音
                        } else {
                            console.log("起飞指令参数非法")
                        }
                    }
                }
                Button {
                    text: "降落"
                    font.pixelSize: 16
                    padding: 8
                    onClicked: {
                        var systemId = parseInt(aircraftIdComboBox.currentText)
                        DroneController.land(systemId)
                        console.log("发送降落指令，ID:", systemId)
                    }
                }
                Button {
                    text: "上锁"
                    font.pixelSize: 16
                    padding: 8
                    onClicked: {
                        var systemId = parseInt(aircraftIdComboBox.currentText)
                        DroneController.lockMotors(systemId)
                        console.log("发送上锁指令，ID:", systemId)
                    }
                }
                Button {
                    text: "模式切换"
                    font.pixelSize: 13
                    padding: 8
                    onClicked: {
                        var systemId = parseInt(aircraftIdComboBox.currentText)
                        DroneController.switchToMissionMode(systemId)
                    }
                }
            }

            // 航点任务输入区域
            Column {
                id: waypointTaskSection
                spacing: 5

                Text {
                    text: "航点任务参数"
                    font.pixelSize: 16
                    font.bold: true
                }

                Row {
                    spacing: 5
                    Text {
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
                    Text {
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
                    Text {
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
                    Text {
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
                    Text {
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
                    spacing: 10
                    anchors.horizontalCenter: parent.horizontalCenter

                    Button {
                        text: "添加航点"
                        Layout.fillWidth: true
                        onClicked: {
                            var gcjlat   = parseFloat(waypointLatitudeField.text)
                            var gcjlon   = parseFloat(waypointLongitudeField.text)
                            var alt   = parseFloat(waypointAltitudeField.text)
                            var dwell = parseFloat(waypointDwellField.text)
                            var speed = parseFloat(waypointSpeedField.text)
                            if (isNaN(gcjlat)||isNaN(gcjlon)||isNaN(alt)||isNaN(dwell)||isNaN(speed)) {
                                console.log("输入无效")
                                return
                            }

                            var coord = Coord.gcj02ToWgs84(gcjlat, gcjlon)
                            if (!coord || isNaN(coord.lat) || isNaN(coord.lon)) {
                                console.log("坐标转换失败")
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
                        }
                    }

                    Button {
                        text: "上传航点"
                        Layout.fillWidth: true
                        onClicked: {
                            if (missionList.length === 0) {
                                console.log("没有航点可上传")
                                return
                            }
                            MissionPlanner.uploadWaypoints(
                                parseInt(aircraftIdComboBox.currentText),
                                missionList
                            )
                        }
                    }
                }

                // 第二行：下载航点 & 清除航点
                Row {
                    spacing: 10
                    anchors.horizontalCenter: parent.horizontalCenter

                    Button {
                        text: "下载航点"
                        Layout.fillWidth: true
                        onClicked: {
                            MissionPlanner.downloadWaypoints(
                                parseInt(aircraftIdComboBox.currentText)
                            )
                        }
                    }

                    Button {
                        text: "清除航点"
                        Layout.fillWidth: true
                        onClicked: {
                            MissionPlanner.clearWaypoints(
                                parseInt(aircraftIdComboBox.currentText)
                            )
                        }
                    }
                }

                // 接收下载完成后的列表
                Connections {
                    target: MissionPlanner
                    function onDownloadFinished(payload) {
                        console.log("Downloaded waypoints:", JSON.stringify(payload, null, 2))
                        missionList = payload
                    }
                }

                // 最下方：开始路径规划任务
                Button {
                    text: "开始路径规划任务"
                    font.pixelSize: 14
                    padding: 8
                    onClicked: {
                        console.log("开始任务，飞机ID=" + aircraftIdComboBox.currentText)
                    }
                }
            }
        }
    }

    // 右侧抽屉：编队/多机控制
    Drawer {
        id: formationDrawer
        width: 220
        height: formationContent.implicitHeight + 40
        y: parent.height/2 - height/2
        edge: Qt.RightEdge
        modal: false
        interactive: true

        background: Rectangle {
            radius: 8
            border.width: 1
            border.color: "#999999"
            gradient: Gradient {
                GradientStop { position: 0; color: "#B0C4DE" }
                GradientStop { position: 1; color: "#4682B4" }
            }
        }

        contentItem: Column {
            id: formationContent
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            // 起飞高度输入
            Row {
                spacing: 5
                Text {
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

            // 多机起飞、降落、上锁按钮并排显示
            Row {
                spacing: 10
                Button {
                    text: "多机起飞"
                    font.pixelSize: 13
                    padding: 8
                    onClicked: {
                        var altitude = parseFloat(multiTakeoffAltitude.text)
                        if (!isNaN(altitude)) {
                            multiController.takeoffAll(altitude)
                            console.log("调用 multiController 进行多机起飞，高度 = " + altitude)
                        } else {
                            console.log("多机起飞: 无效的起飞高度")
                        }
                    }
                }
                Button {
                    text: "多机降落"
                    font.pixelSize: 13
                    padding: 8
                    onClicked: {
                        var mode = formationModeComboBox.currentText
                        Landing.landAllDronesByFormation(multiController, mode, this);
                    }
                }
                Button {
                    text: "多机上锁"
                    font.pixelSize: 13
                    padding: 8
                    onClicked: {
                        multiController.lockAll()
                        console.log("调用 multiController 进行多机上锁")
                    }
                }
            }

            // 编队参数输入
            Row {
                spacing: 5
                Text {
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
                Text {
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
                Text {
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
                Text {
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

    Connections {
        target: formationController
        function onFormationStatusWithOffsetUpdated(formationType, droneId, offsetX, offsetY, offsetZ) {
            var baseLat = currentLatitude
            var baseLon = currentLongitude
            var lat = baseLat + offsetY / 111139.0
            var lon = baseLon + offsetX / (111139.0 * Math.cos(baseLat * Math.PI / 180.0))
            console.log("[Simulate] DroneID=", droneId,
                        " => Lat=", lat.toFixed(6),
                        " Lon=", lon.toFixed(6),
                        " Z=", offsetZ)
            gaodeMapView.runJavaScript(
                        "updateDronePositionWithFormation(" +
                        droneId + ", " +
                        lat.toFixed(6) + ", " +
                        lon.toFixed(6) + ", " +
                        offsetZ + ")"
                        )
        }
    }

    // 叠加层，包含各类控制和显示控件
    Item {
        id: overlay
        anchors.fill: parent
        z: 1

        // 左侧按钮区域
        Column {
            id: leftButtonColumn
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                leftMargin: 10
            }
            spacing: 10

            Button {
                text: "单机控制"
                font.pixelSize: 16
                padding: 10
                onClicked: {
                    formationDrawer.close()
                    singleControlDrawer.open()
                }
            }
            Button {
                text: "多机控制"
                font.pixelSize: 16
                padding: 10
                onClicked: {
                    singleControlDrawer.close()
                    formationDrawer.open()
                }
            }
            // 然后在 overlay 或左侧按钮区加一个入口：
            Button {
                text: "动态编队"
                font.pixelSize: 16; padding: 10
                onClicked: {
                    singleControlDrawer.close()
                    formationDrawer.close()
                    pathPlanningDrawer.open()
                }
            }
            Button {
                text: "连接 RTK"
                font.pixelSize: 16
                padding: 10
                Button {
                    text: "连接 RTK"
                    font.pixelSize: 16; padding: 10
                    onClicked: {
                        rtkConfigWindow.open()
                    }
                }
            }
        }

        // 姿态显示控件
        AttitudeIndicator {
            id: attitudeIndicator
            width: 200
            height: 200
            anchors.bottom: parent.bottom
            anchors.left: parent.left
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

        // 经纬度、海拔、电压显示控件（位于仪表盘右侧）
        Column {
            id: positionDisplay
            anchors {
                left: attitudeIndicator.right
                bottom: attitudeIndicator.bottom
                leftMargin: 10
            }
            spacing: 4

            property string voltage: "N/A"
            property string latitude: "N/A"
            property string longitude: "N/A"
            property string altitude: "N/A"
            property string relativeAltitude: "N/A"
            property string satellites: "N/A"

            Text {
                text: "卫星数量: " + positionDisplay.satellites
                font.pixelSize: 14
            }
            Text {
                text: "电压: " + positionDisplay.voltage
                font.pixelSize: 14
            }
            Text {
                text: "纬度: " + positionDisplay.latitude
                font.pixelSize: 14
            }
            Text {
                text: "经度: " + positionDisplay.longitude
                font.pixelSize: 14
            }
            Text {
                text: "海拔: " + positionDisplay.altitude
                font.pixelSize: 14
            }
            Text {
                text: "对地高度: " + positionDisplay.relativeAltitude
                font.pixelSize: 14
            }
        }

        // 串口、飞机ID设置区域
        Column {
            spacing: 10
            anchors {
                top: parent.top
                left: parent.left
                topMargin: 10
                leftMargin: 10
            }

            // 飞机串口部分
            Row {
                spacing: 8
                Text {
                    text: "飞机串口:"
                    font.pixelSize: 14
                }
                ComboBox {
                    id: serialportComboBox
                    font.pixelSize: 14
                    width: 120
                    model: SerialPortManager.availablePorts
                }
                Text {
                    text: "波特率:"
                    font.pixelSize: 14
                }
                ComboBox {
                    id: baudComboBox
                    font.pixelSize: 14
                    width: 120
                    model: ["9600", "57600", "115200"]
                }
                Button {
                    id: btnAirplane
                    text: SerialPortManager.buttonText
                    font.pixelSize: 14
                    width: 100
                    padding: 6
                    onClicked: {
                        SerialPortManager.togglePort(serialportComboBox.currentText, baudComboBox.currentText)
                    }
                }
            }

            // 点火串口部分
            Row {
                spacing: 8
                Text {
                    text: "点火串口:"
                    font.pixelSize: 14
                }
                ComboBox {
                    id: serialportComboBoxFire
                    font.pixelSize: 14
                    width: 120
                    model: SerialPortManagerFire.availablePorts
                }
                Text {
                    text: "波特率:"
                    font.pixelSize: 14
                }
                ComboBox {
                    id: baudComboBoxFire
                    font.pixelSize: 14
                    width: 120
                    model: ["9600", "57600", "115200"]
                }
                Button {
                    id: btnIgnition
                    text: SerialPortManagerFire.buttonText
                    font.pixelSize: 14
                    width: 100
                    padding: 6
                    onClicked: {
                        SerialPortManagerFire.togglePort(serialportComboBoxFire.currentText, baudComboBoxFire.currentText)
                    }
                }
            }

            // 飞机ID选择
            Row {
                spacing: 8
                Text {
                    text: "飞机ID:"
                    font.pixelSize: 14
                }
                ComboBox {
                    id: aircraftIdComboBox
                    font.pixelSize: 14
                    width: 120
                    model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10"]
                    currentIndex: 0
                    onCurrentTextChanged: {
                        MAVLinkParser.setSelectedSystemId(parseInt(currentText))
                    }
                    Component.onCompleted: {
                        MAVLinkParser.setSelectedSystemId(parseInt(currentText))
                    }
                }
            }
        }

        // 计时器及编队任务显示/添加区域
        Item {
            id: timerItem
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 30

            // 记录累计时间（秒）
            property double elapsedTime: 0

            Timer {
                id: timer
                interval: 10
                repeat: true
                running: false

                onTriggered: {
                    timerItem.elapsedTime += interval / 1000.0
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
                anchors.centerIn: parent
                spacing: 10

                // 第一行：计时文本、开始/暂停按钮、任务名称输入和添加任务按钮
                Row {
                    spacing: 20
                    Text {
                        id: timerDisplay
                        text: timerItem.elapsedTime.toFixed(3)
                        font.pixelSize: 28
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Row {
                        spacing: 20
                        Button {
                            text: "继续"
                            font.pixelSize: 16
                            onClicked: { timer.running = true }
                        }
                        Button {
                            text: "暂停"
                            font.pixelSize: 16
                            onClicked: { timer.running = false }
                        }
                        Button {
                            text: "重置"
                            font.pixelSize: 16
                            onClicked: { timer.running = false
                                timerItem.elapsedTime = 0
                            }
                        }
                    }
                    Row {
                        spacing: 10
                        TextField {
                            id: formationTaskNameField
                            width: 100
                            placeholderText: "编队任务"
                            font.pixelSize: 14
                        }
                        Button {
                            text: "添加编队任务"
                            font.pixelSize: 16
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
                }
                // 新增一行：保存、加载、清除任务按钮
                Row {
                    spacing: 10
                    Button {
                        text: "保存任务"
                        onClicked: {
                            saveFileDialog.open()

                        }
                    }


                    Button {
                        text: "加载任务"
                        onClicked: {
                            loadFileDialog.open()

                        }
                    }
                    Button {
                        text: "清除任务"
                        onClicked: {
                            formationTaskList.text = ""
                            formationTasks = []  // 如果你有保存任务列表的数组，也一起清除
                            console.log("任务已清除")
                        }
                    }
                }

                // 第二行：任务列表显示，使用 Rectangle 包裹 ScrollView 实现半透明背景效果
                Rectangle {
                    width: 600
                    height: 200
                    color: "white"
                    opacity: 0.5
                    radius: 4
                    border.color: "gray"
                    ScrollView {
                        anchors.fill: parent
                        TextArea {
                            id: formationTaskList
                            width: parent.width
                            height: parent.height
                            readOnly: true
                            wrapMode: TextEdit.WrapAnywhere
                            font.pixelSize: 14
                        }
                    }
                }
            }
        }
    }

    // 更新任务列表显示文本
    function updateFormationTaskListText() {
        var textContent = ""
        for (var i = 0; i < formationTasks.length; i++) {
            var t = formationTasks[i]
            textContent += "任务" + (i + 1) + ": " +
                           t.name + " 模式：" + t.mode + "  " +
                           "间距：" + t.spacing + "  " +
                           "速+-度：" + t.speed + "  " +
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
