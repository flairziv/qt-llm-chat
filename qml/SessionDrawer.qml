import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Drawer {
    id: drawer
    width: Math.min(parent.width * 0.78, 300)
    height: parent.height
    edge: Qt.LeftEdge

    Material.background: "#181824"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 顶部标题 + 新建按钮
        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: "#1E1E2E"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8

                Label {
                    text: "历史会话"
                    font.pixelSize: 16
                    font.bold: true
                    color: "white"
                    Layout.fillWidth: true
                }

                RoundButton {
                    icon.source: "qrc:/qt/qml/MiChat/icons/add.svg"
                    icon.width: 22
                    icon.height: 22
                    Material.background: Material.Blue
                    width: 40; height: 40
                    onClicked: {
                        backend.newSession()
                        drawer.close()
                    }
                }
            }
        }

        // 会话列表
        ListView {
            id: sessionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.sessions
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: ItemDelegate {
                width: sessionList.width
                height: 64
                highlighted: modelData.id === backend.activeSessionId

                Material.background: highlighted ? "#2A2A4E" : "transparent"

                onClicked: {
                    backend.selectSession(modelData.id)
                    drawer.close()
                }

                ColumnLayout {
                    anchors {
                        left: parent.left
                        right: deleteBtn.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: 16
                        rightMargin: 4
                    }
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: modelData.title
                        color: "white"
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.updatedAt
                        color: "#888"
                        font.pixelSize: 11
                    }
                }

                // 删除按钮
                ToolButton {
                    id: deleteBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 4
                    icon.source: "qrc:/qt/qml/MiChat/icons/close.svg"
                    icon.width: 16
                    icon.height: 16
                    opacity: 0.6
                    onClicked: {
                        backend.deleteSession(modelData.id)
                    }
                    // 阻止点击事件传递到 ItemDelegate
                    MouseArea {
                        anchors.fill: parent
                        onClicked: backend.deleteSession(modelData.id)
                    }
                }
            }
        }
    }
}
