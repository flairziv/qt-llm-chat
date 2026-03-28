import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Page {
    id: settingsPage

    header: ToolBar {
        Material.background: "#1E1E2E"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4

            ToolButton {
                text: "←"
                font.pixelSize: 20
                onClicked: ApplicationWindow.window.closeSettings()
            }

            Label {
                text: "设置"
                font.pixelSize: 18
                font.bold: true
                color: "white"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Item { width: 48 }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.bottomMargin: Qt.inputMethod.visible ? Qt.inputMethod.keyboardRectangle.height / Screen.devicePixelRatio : 0
        Behavior on anchors.bottomMargin {
            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
        }
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            // ---- Provider 选择 ----
            Label {
                Layout.fillWidth: true
                topPadding: 20
                leftPadding: 16
                bottomPadding: 4
                text: "AI 提供商"
                color: Material.accent
                font.pixelSize: 12
                font.bold: true
            }

            RadioButton {
                leftPadding: 16
                text: "OpenAI / 兼容接口"
                checked: backend.activeProvider === "openai"
                onCheckedChanged: if (checked) backend.activeProvider = "openai"
            }
            RadioButton {
                leftPadding: 16
                text: "Claude (Anthropic)"
                checked: backend.activeProvider === "claude"
                onCheckedChanged: if (checked) backend.activeProvider = "claude"
            }

            // ---- OpenAI 配置 ----
            Label {
                Layout.fillWidth: true
                topPadding: 20
                leftPadding: 16
                bottomPadding: 4
                text: "OpenAI 配置"
                color: Material.accent
                font.pixelSize: 12
                font.bold: true
            }

            SettingsField { label: "API Key";   value: backend.openaiApiKey;   echoMode: TextInput.Password;          onValueEdited: v => backend.openaiApiKey = v }
            SettingsField { label: "Base URL";  value: backend.openaiBaseUrl;  onValueEdited: v => backend.openaiBaseUrl = v }
            SettingsField { label: "Model";     value: backend.openaiModel;    onValueEdited: v => backend.openaiModel = v }

            // ---- Claude 配置 ----
            Label {
                Layout.fillWidth: true
                topPadding: 20
                leftPadding: 16
                bottomPadding: 4
                text: "Claude 配置"
                color: Material.accent
                font.pixelSize: 12
                font.bold: true
            }

            SettingsField { label: "API Key";  value: backend.claudeApiKey;  echoMode: TextInput.Password;         onValueEdited: v => backend.claudeApiKey = v }
            SettingsField { label: "Base URL"; value: backend.claudeBaseUrl; onValueEdited: v => backend.claudeBaseUrl = v }
            SettingsField { label: "Model";    value: backend.claudeModel;   onValueEdited: v => backend.claudeModel = v }

            // ---- 通用参数 ----
            Label {
                Layout.fillWidth: true
                topPadding: 20
                leftPadding: 16
                bottomPadding: 4
                text: "通用参数"
                color: Material.accent
                font.pixelSize: 12
                font.bold: true
            }

            SettingsField { label: "System Prompt";        value: backend.systemPrompt;           onValueEdited: v => backend.systemPrompt = v }
            SettingsField { label: "Max Tokens";           value: backend.maxTokens.toString();   inputMethodHints: Qt.ImhDigitsOnly;          onValueEdited: v => backend.maxTokens = parseInt(v) || 4096 }
            SettingsField { label: "Temperature (0-2)";   value: backend.temperature.toFixed(2); inputMethodHints: Qt.ImhFormattedNumbersOnly; onValueEdited: v => backend.temperature = parseFloat(v) || 0.7 }

            Item { height: 40 }
        }
    }

}
