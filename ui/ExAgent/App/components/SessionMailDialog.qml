pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: panel
    required property ChatController controller
    title: "会话通信"
    anchors.centerIn: Overlay.overlay
    modal: true
    property string source
    property var recipients: []
    property var letters: []
    function begin() {
        source = controller.currentSessionId
        recipients = controller.agent.correspondents
        letters = controller.agent.correspondence
        recipient.currentIndex = recipients.length ? 0 : -1
        input.clear()
        open()
    }
    Connections {
        target: panel.controller.agent
        function onMailChanged() {
            if (!panel.visible) return
            if (panel.source !== panel.controller.currentSessionId || panel.controller.previewMode) panel.close()
            else panel.letters = panel.controller.agent.correspondence
        }
    }
    contentItem: ColumnLayout {
        spacing: 8
        DialogSelect {
            id: recipient; Layout.fillWidth: true
            model: panel.recipients; textRole: "title"; valueRole: "id"
            Accessible.name: Design.UiText.get("目标会话")
            displayText: currentIndex < 0 ? Design.UiText.get("同项目下尚无其他会话") : currentText
        }
        RowLayout {
            Layout.fillWidth: true
            DialogInput {
                id: input; Layout.fillWidth: true; maximumLength: 8192
                placeholderText: Design.UiText.get("发给这个会话的 Agent…")
                onAccepted: if (send.enabled) send.clicked()
            }
            DialogAction {
                id: send; primary: true; text: Design.UiText.get("发送")
                enabled: recipient.currentIndex >= 0 && input.text.trim().length > 0
                onClicked: if (panel.controller.agent.sendMail(recipient.currentValue, input.text)) input.clear()
            }
        }
        Label {
            visible: !panel.letters.length; Layout.fillWidth: true
            text: Design.UiText.get("运行中的 Codex 会立即接收消息，空闲会话将在下一回合接收。")
            wrapMode: Text.Wrap; color: panel.palette.placeholderText
        }
        ListView {
            visible: panel.letters.length > 0; Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 275, Math.max(80, panel.maximumBodyHeight-100))
            clip: true; spacing: 9; model: panel.letters
            ScrollBar.vertical: ScrollBar {}
            delegate: Column {
                required property var modelData
                width: ListView.view.width - 12; spacing: 2
                Label {
                    width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText
                    text: modelData.sender + " → " + modelData.recipient + "  ·  " +
                          Design.UiText.get(["等待回合","投递中","引擎已接收","引擎已拒绝","投递待核对"][modelData.state] || "投递待核对")
                    color: panel.palette.placeholderText
                }
                Label { width: parent.width; text: modelData.text; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            }
        }
    }
}
