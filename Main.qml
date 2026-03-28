import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 420
    height: 820
    visible: true
    title: "MiChat"

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    // 错误提示 Snackbar
    Connections {
        target: backend
        function onErrorOccurred(message) {
            errorBar.text = message
            errorBar.open()
        }
    }

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: chatPageComponent
    }

    Component { id: chatPageComponent; ChatPage {} }
    Component { id: settingsPageComponent; SettingsPage {} }

    function openSettings() {
        stackView.push(settingsPageComponent)
    }
    function closeSettings() {
        stackView.pop()
    }

    // 底部错误提示条
    Popup {
        id: errorBar
        property alias text: errorLabel.text
        anchors.centerIn: parent
        width: parent.width * 0.88
        height: 56
        padding: 0
        modal: false
        closePolicy: Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#B00020"
            radius: 6
        }

        contentItem: Text {
            id: errorLabel
            color: "white"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignVCenter
            leftPadding: 16
            rightPadding: 16
        }

        Timer {
            running: errorBar.visible
            interval: 4000
            onTriggered: errorBar.close()
        }
    }
}
