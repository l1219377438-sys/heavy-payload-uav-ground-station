// FormationLanding.js
.import QtQuick 2.0 as QQ

// 这里定义多个函数
function landAllDronesByFormation(multiController, mode) {
    console.log("开始多机降落，模式=", mode)
    if (mode === "一字形" || mode === "人字形" ||
        mode === "拱形" || mode === "W形") {
        multiController.landAll()
    } else if (mode === "川字形") {
        landRiverInOrder(multiController)
    } else if (mode === "圆形") {
        landCircleInOrder(multiController)
    } else if (mode === "十字形") {
        landCrossInOrder(multiController)
    } else {
        console.log("未知编队，无法降落")
    }
}

function landRiverInOrder(multiController) {
    var landingOrder = [1,2,3,4,5,6,7,8,9,10]
    var intervalMs = 30000

    function landNext(index) {
        if (index >= landingOrder.length) {
            console.log("川字形降落完成")
            return
        }
        var droneId = landingOrder[index]
        // 这里 21 是 MAV_CMD_NAV_LAND
        multiController.sendCustomMavlinkMessage(droneId, 1, 21,
                                                0, 0, 0, 0, 0, 0, 0)
        console.log("Drone", droneId, "landing...")

        // 注意：没有 QEventLoop，只能异步 Timer
        var timer = QQ.Timer {
            interval: intervalMs
            repeat: false
            onTriggered: {
                landNext(index+1)
            }
        }
        timer.start();
    }

    landNext(0)
}

// 同理 landCircleInOrder(...) / landCrossInOrder(...) ...

