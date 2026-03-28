import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root
    property string role: "user"
    property string content: ""

    height: bubble.height + 16

    readonly property bool isUser: role === "user"
    readonly property bool isEmpty: content.trim().length === 0

    // 用户气泡：右对齐；助手气泡：左对齐
    Rectangle {
        id: bubble
        width: Math.min(contentText.implicitWidth + 24, root.width * 0.78)
        height: isEmpty ? 36 : contentText.implicitHeight + 20
        radius: 16
        color: isUser ? "#3D6FCC" : "#2A2A3E"
        anchors {
            right: isUser ? parent.right : undefined
            left: isUser ? undefined : parent.left
            rightMargin: isUser ? 12 : 0
            leftMargin: isUser ? 0 : 12
            verticalCenter: parent.verticalCenter
        }

        // 打字动画（仅空的助手消息）
        Row {
            anchors.centerIn: parent
            spacing: 6
            visible: isEmpty && !isUser

            Repeater {
                model: 3
                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: "#888"
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.2; duration: 400 }
                        NumberAnimation { to: 1.0; duration: 400 }
                        PauseAnimation  { duration: index * 200 }
                    }
                }
            }
        }

        Text {
            id: contentText
            text: root.content
            visible: !isEmpty
            wrapMode: Text.Wrap
            color: "white"
            font.pixelSize: 15
            lineHeight: 1.4
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                margins: 12
            }
            textFormat: Text.PlainText
            font.family: Qt.platform.os === "android" ? "Noto Color Emoji" : font.family
        }
    }
}
