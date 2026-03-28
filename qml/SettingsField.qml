import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

ItemDelegate {
    id: root
    property string label: ""
    property string value: ""
    property int echoMode: TextInput.Normal
    property int inputMethodHints: Qt.ImhNone
    signal valueEdited(string v)

    Layout.fillWidth: true
    height: 64
    padding: 0

    contentItem: ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: 16
            rightMargin: 16
        }
        spacing: 2

        Label {
            text: root.label
            color: "#AAA"
            font.pixelSize: 11
        }

        TextField {
            Layout.fillWidth: true
            text: root.value
            echoMode: root.echoMode
            inputMethodHints: root.inputMethodHints
            color: "white"
            font.pixelSize: 14
            background: Item {}
            leftPadding: 0
            onEditingFinished: root.valueEdited(text)
        }
    }

    background: Rectangle {
        color: "#1E1E2E"
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: "#333"
        }
    }
}
