import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: chatPage

    // 顶部工具栏
    header: ToolBar {
        Material.background: "#1E1E2E"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4

            ToolButton {
                icon.source: "qrc:/qt/qml/MiChat/icons/menu.svg"
                icon.width: 22
                icon.height: 22
                onClicked: sessionDrawer.open()
            }

            Label {
                Layout.fillWidth: true
                text: "MiChat"
                font.pixelSize: 18
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                color: "white"
            }

            ToolButton {
                icon.source: "qrc:/qt/qml/MiChat/icons/settings.svg"
                icon.width: 22
                icon.height: 22
                onClicked: ApplicationWindow.window.openSettings()
            }
        }
    }

    // 会话侧边栏
    SessionDrawer {
        id: sessionDrawer
    }

    // 消息列表
    ListView {
        id: messageList
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: inputBar.top
            bottomMargin: 8
        }
        clip: true
        spacing: 4
        model: backend.messages
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        // 新消息时滚动到底部
        onCountChanged: Qt.callLater(() => positionViewAtEnd())

        delegate: MessageBubble {
            role: modelData.role
            content: (modelData.role === "assistant" && index === messageList.count - 1 && backend.responding)
                     ? backend.streamingText
                     : modelData.content
            width: messageList.width
        }

        // 空状态提示
        Text {
            anchors.centerIn: parent
            text: "开始一段新对话"
            color: "#555"
            font.pixelSize: 16
            visible: messageList.count === 0
        }
    }

    // 流式回复时也滚到底
    Connections {
        target: backend
        function onStreamingTextChanged() {
            Qt.callLater(() => messageList.positionViewAtEnd())
        }
    }

    // 底部输入栏
    Rectangle {
        id: inputBar
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: Math.min(inputField.implicitHeight + 20, 160)
        color: "#1E1E2E"

        RowLayout {
            anchors {
                fill: parent
                margins: 8
            }
            spacing: 8

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                TextArea {
                    id: inputField
                    placeholderText: "输入消息..."
                    wrapMode: TextArea.Wrap
                    font.pixelSize: 15
                    background: Rectangle {
                        color: "#2A2A3E"
                        radius: 20
                    }
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 10
                    bottomPadding: 10
                    color: "white"

                    Keys.onReturnPressed: (event) => {
                        if (event.modifiers & Qt.ShiftModifier) {
                            event.accepted = false
                        } else {
                            event.accepted = true
                            sendBtn.clicked()
                        }
                    }
                }
            }

            // 发送/停止按钮
            RoundButton {
                id: sendBtn
                width: 48
                height: 48
                icon.source: backend.responding
                    ? "qrc:/qt/qml/MiChat/icons/stop.svg"
                    : "qrc:/qt/qml/MiChat/icons/send.svg"
                icon.width: 22
                icon.height: 22
                Material.background: backend.responding ? Material.Red : Material.Blue

                onClicked: {
                    if (backend.responding) {
                        backend.stopResponding()
                    } else {
                        var txt = inputField.text.trim()
                        if (txt.length > 0) {
                            backend.sendMessage(txt)
                            inputField.clear()
                        }
                    }
                }
            }
        }
    }

}
