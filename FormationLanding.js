// FormationLanding.js
// 定义 MAVLink 指令常量（请根据实际情况确认常量数值）
var CMD_RTL = 20; // MAV_CMD_NAV_RETURN_TO_LAUNCH（返航指令）
var CMD_LAND = 21; // MAV_CMD_NAV_LAND（降落指令）

/*
 * 主降落函数：
 * @param multiController  QML 中暴露的多机控制对象
 * @param mode             当前选中的编队模式（字符串，例如 "一字形"、"川字形"、"圆形"、"十字形"）
 * @param parent           用于创建 Timer 的父对象（通常传入 QML 顶层对象，如 root）
 */
function landAllDronesByFormation(multiController, mode, parent) {
    console.log("开始多机降落，编队模式:", mode);
    if (mode === "一字形" || mode === "人字形" ||
        mode === "拱形" || mode === "W形") {
        // 直接调用多机降落
        multiController.landAll();
    } else if (mode === "川字形") {
        landRiverInOrder(multiController, parent);
    } else if (mode === "圆形") {
        landCircleInOrder(multiController, parent);
    } else if (mode === "十字形") {
        landCrossInOrder(multiController, parent);
    } else {
        console.log("未知编队模式:", mode, "无法执行降落。");
    }
}

/*
 * 川字形降落：依次单架降落
 * 降落顺序：数组 landingOrder 中的顺序
 * 每架无人机之间延迟 intervalMs 毫秒后继续降落下一架
 */
function landRiverInOrder(multiController, parent) {
    var landingOrder = [1,2,3,4,5,6,7,8,9,10];
    var intervalMs = 20000; // 延迟 20000 毫秒（20 秒）

    function landNext(index) {
        if (index >= landingOrder.length) {
            console.log("川字形降落完成。");
            return;
        }
        var droneId = landingOrder[index];
        // 发送返航指令（使用 CMD_RTL 指令）
        multiController.sendCustomMavlinkMessage(
            droneId, 1, CMD_RTL,
            0, 0, 0, 0, 0, 0, 0
        );
        console.log("Drone", droneId, "发送返航指令。");

        // 创建一个单次 Timer，延迟后执行下一架降落
        var timer = Qt.createQmlObject('import QtQuick 2.0; Timer {}', parent);
        timer.interval = intervalMs;
        timer.repeat = false;
        timer.triggered.connect(function() {
            timer.destroy();
            landNext(index + 1);
        });
        timer.start();
    }
    landNext(0);
}

/*
 * 圆形降落：按组降落
 * 这里假设每一组只有一架无人机，你可以根据需要修改分组逻辑
 */
function landCircleInOrder(multiController, parent) {
    var landingOrder = [
        [1],
        [2],
        [3],
        [4],
        [5],
        [6],
        [7],
        [8],
        [9],
        [10]
    ];
    var intervalMs = 20000; // 每组之间延迟 20000 毫秒

    function landGroup(groupIndex) {
        if (groupIndex >= landingOrder.length) {
            console.log("圆形降落完成。");
            return;
        }
        var group = landingOrder[groupIndex];
        for (var i = 0; i < group.length; i++) {
            var droneId = group[i];
            // 使用降落指令
            multiController.sendCustomMavlinkMessage(
                droneId, 1, CMD_RTL,
                0, 0, 0, 0, 0, 0, 0
            );
            console.log("Drone", droneId, "发送降落指令。");
        }
        var timer = Qt.createQmlObject('import QtQuick 2.0; Timer {}', parent);
        timer.interval = intervalMs;
        timer.repeat = false;
        timer.triggered.connect(function() {
            timer.destroy();
            landGroup(groupIndex + 1);
        });
        timer.start();
    }
    landGroup(0);
}

/*
 * 十字形降落：按组降落
 * 分组示例与 C++ 原有逻辑类似
 */
function landCrossInOrder(multiController, parent) {
    var landingOrder = [
        [9],
        [7],
        [4, 2, 1, 3, 5],
        [6],
        [8],
        [10]
    ];
    var intervalMs = 20000; // 每组之间延迟 20000 毫秒

    function landGroup(groupIndex) {
        if (groupIndex >= landingOrder.length) {
            console.log("十字形降落完成。");
            return;
        }
        var group = landingOrder[groupIndex];
        for (var i = 0; i < group.length; i++) {
            var droneId = group[i];
            multiController.sendCustomMavlinkMessage(
                droneId, 1, CMD_RTL,
                0, 0, 0, 0, 0, 0, 0
            );
            console.log("Drone", droneId, "发送降落指令。");
        }
        var timer = Qt.createQmlObject('import QtQuick 2.0; Timer {}', parent);
        timer.interval = intervalMs;
        timer.repeat = false;
        timer.triggered.connect(function() {
            timer.destroy();
            landGroup(groupIndex + 1);
        });
        timer.start();
    }
    landGroup(0);
}
