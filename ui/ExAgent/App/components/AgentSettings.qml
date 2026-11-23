pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat

Dialog {
    id: root
    required property ChatController controller
    readonly property AgentController agent: controller.agent
    property var turns: []
    property var contextPreview: ({})
    title: "会话执行设置"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(440, parent ? parent.width - 24 : 440)
    standardButtons: Dialog.Ok | Dialog.Cancel
    onOpened: {
        turns = controller.transfer.turns(controller.currentSessionId)
        attachHistory.checked = false
        contextPreview = ({})
        mode.currentIndex = agent.account
        modelName.text = agent.model
        fullAccess.checked = agent.fullAccess
        writeAccess.checked = agent.workspaceWrite
        feedback.text = ""
    }
    onAccepted: {
        agent.configure(mode.currentIndex, modelName.text, writeAccess.checked, fullAccess.checked)
        if (attachHistory.checked && mode.currentIndex > 0 && contextPreview.text)
            agent.includeHistory(first.currentIndex,last.currentIndex)
        else agent.includeHistory(-1,-1)
    }
    onClosed: secret.clear()
    Binding {
        target: root.standardButton(Dialog.Ok); property: "enabled"
        value: !root.agent.authenticating && (!attachHistory.visible || !attachHistory.checked || !!root.contextPreview.text)
    }
    Connections {
        target: root.agent
        function onAccountUpdated(loggedIn: bool) { feedback.text = loggedIn ? "专用账号登录成功" : "专用账号已退出" }
        function onFailure(reason: string) { if (root.visible) feedback.text = reason }
    }
    contentItem: ColumnLayout {
        spacing: 9
        Label { text: "运行账号" }
        ComboBox { id: mode; Layout.fillWidth: true; model: ["仅本地聊天", "本机 Codex 账号", "EX 专用 OpenAI API Key"] }
        RowLayout {
            visible: mode.currentIndex === 2
            TextField {
                id: secret; Layout.fillWidth: true; placeholderText: "专用 API Key"
                echoMode: TextInput.Password; selectByMouse: true
            }
            Button {
                text: root.agent.authenticating ? "登录中" : "登录"
                enabled: secret.length > 0 && !root.agent.authenticating
                onClicked: { root.agent.login(secret.text); secret.clear() }
            }
        }
        Button {
            visible: mode.currentIndex === 2; enabled: !root.agent.authenticating
            text: "退出 EX 专用账号"; onClicked: root.agent.logout()
        }
        Label { id: feedback; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap }
        RowLayout {
            visible: mode.currentIndex > 0
            Button { text: "查询可用模型"; onClicked: root.agent.listModels(mode.currentIndex) }
            ComboBox {
                Layout.fillWidth: true; model: root.agent.models; enabled: count > 0
                onActivated: modelName.text = currentText
            }
        }
        TextField {
            id: modelName; visible: mode.currentIndex > 0; Layout.fillWidth: true
            placeholderText: "模型 ID（留空使用 Codex 默认模型）"; maximumLength: 128
        }
        CheckBox {
            id: attachHistory; visible: mode.currentIndex > 0 && root.turns.length > 0; text: "随下条消息发送所选历史"
        }
        RowLayout {
            visible: attachHistory.visible && attachHistory.checked
            ComboBox { id: first; Layout.preferredWidth: 125; model: root.turns; onCurrentIndexChanged: root.contextPreview = ({}) }
            ComboBox { id: last; Layout.preferredWidth: 125; model: root.turns; onCurrentIndexChanged: root.contextPreview = ({}) }
            Button { text: "预览"; onClicked: root.contextPreview = root.agent.previewHistory(first.currentIndex,last.currentIndex) }
        }
        ScrollView {
            visible: attachHistory.visible && attachHistory.checked && !!(root.contextPreview.text || root.contextPreview.error)
            Layout.fillWidth: true; Layout.preferredHeight: 96
            TextArea {
                readOnly: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText
                text: root.contextPreview.error || ((root.contextPreview.names || []).join("、") + "\n" + (root.contextPreview.text || ""))
            }
        }
        CheckBox { id: fullAccess; visible: mode.currentIndex > 0; text: "完全访问（文件、网络，执行时免审批）" }
        CheckBox { id: writeAccess; enabled: !fullAccess.checked; visible: mode.currentIndex > 0; text: "允许写入当前工作区" }
        Label {
            visible: mode.currentIndex > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: root.controller.workspacePath || (fullAccess.checked || writeAccess.checked
                ? "要写入文件，请先将会话移入工作区。" : "普通聊天 · 无需绑定工作区")
        }
        Label {
            visible: mode.currentIndex > 0; Layout.fillWidth: true; wrapMode: Text.Wrap
            text: "首次派发从新输入开始。继续同一引擎线程时，Codex 会使用该线程已有的上下文。"
        }
    }
}
