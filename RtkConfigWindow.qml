// 文件：RtkConfigDialog.qml
import QtQuick 2.15
import QtQuick.Controls 2.15

Dialog {
    id: rtkDialog
    title: "RTK 参数设置"
    modal: true
    standardButtons: Dialog.Cancel | Dialog.Ok
    visible: false     // 初始隐藏




    contentItem: Column {
        width: 360
        spacing: 10
        padding: 20

        Row { spacing: 5
            Text { text: "主机地址：" }
            TextField { id: hostField; placeholderText: "如 120.253.239.161" }
        }
        Row { spacing: 5
            Text { text: "端口号：" }
            TextField {
                id: portField
                placeholderText: "如 8002"
                inputMethodHints: Qt.ImhDigitsOnly
            }
        }
        Row { spacing: 5
            Text { text: "挂载点：" }
            TextField { id: mountField; placeholderText: "如 RTCM3_GREC" }
        }
        Row { spacing: 5
            Text { text: "账号：" }
            TextField { id: userField; placeholderText: "输入账号" }
        }
        Row { spacing: 5
            Text { text: "密码：" }
            TextField {
                id: pwdField
                placeholderText: "输入密码"
                echoMode: TextInput.Password
            }
        }
    }

    onAccepted: {
        // 参数校验
        var h = hostField.text.trim()
        var p = parseInt(portField.text)
        var m = mountField.text.trim()
        if (!h || isNaN(p) || !m) {
            console.log("RTK 参数不完整或非法")
            return
        }
        // 调用 C++ 端 rtkClient 接口
        rtkClient.setEndpoint(h, p, m)
        if (userField.text && pwdField.text) {
            rtkClient.setAuth(userField.text, pwdField.text)
        }
        rtkClient.connectToCaster()
        rtkClient.enableGGAHeartbeat(true, 1000)


        // 关闭对话框
        rtkDialog.close()
    }

    onRejected: {
        rtkDialog.close()
    }
}
