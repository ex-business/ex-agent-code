pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat

WorkbenchDialog {
    id: root
    required property ChatController controller
    readonly property AgentController agent: controller.agent
    property var turns: []
    property var contextPreview: ({})
    title: Design.UiText.get("会话执行设置")
    modal: true
    anchors.centerIn: Overlay.overlay
    sizeClass: WorkbenchDialog.Compact
    standardButtons: Dialog.Ok | Dialog.Cancel
    onOpened: {
        turns = controller.transfer.turns(controller.currentSessionId)
        attachHistory.checked = false
        contextPreview = ({})
        mode.currentIndex = agent.account
        modelName.text = agent.model
        access.currentIndex = agent.fullAccess ? 2 : agent.workspaceWrite ? 1 : 0
        feedback.text = ""
    }
    onAccepted: {
        agent.configure(mode.currentIndex, modelName.text, access.currentIndex === 2 ? agent.workspaceWrite : access.currentIndex === 1, access.currentIndex === 2)
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
        function onAccountUpdated(loggedIn: bool) { feedback.text = loggedIn ? Design.UiText.get("专用账号登录成功") : Design.UiText.get("专用账号已退出") }
        function onFailure(reason: string) { if (root.visible) feedback.text = reason }
    }
    contentItem: ScrollView {
        id: body
        clip: true
        contentWidth: availableWidth
        contentHeight: form.implicitHeight
        implicitHeight: Math.min(contentHeight, root.maximumBodyHeight)
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
        id: form
        width: body.availableWidth
        spacing: 9
        Label { text: Design.UiText.get("运行账号") }
        DialogSelect { id: mode; Layout.fillWidth: true; translateOptions: true; model: ["仅本地聊天", "本机 Codex 账号", "EX 专用 OpenAI API Key"] }
        RowLayout {
            visible: mode.currentIndex === 2
            DialogInput {
                id: secret; Layout.fillWidth: true; placeholderText: Design.UiText.get("专用 API Key")
                echoMode: TextInput.Password; selectByMouse: true
            }
            DialogAction {
                text: root.agent.authenticating ? Design.UiText.get("登录中") : Design.UiText.get("登录")
                enabled: secret.length > 0 && !root.agent.authenticating
                onClicked: { root.agent.login(secret.text); secret.clear() }
            }
        }
        DialogAction {
            visible: mode.currentIndex === 2; enabled: !root.agent.authenticating
            text: Design.UiText.get("退出 EX 专用账号"); onClicked: root.agent.logout()
        }
        Label { id: feedback; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { visible: mode.currentIndex > 0; text: Design.UiText.get("模型"); Layout.topMargin: 4 }
        RowLayout {
            visible: mode.currentIndex > 0; Layout.fillWidth: true
            DialogInput {
                id: modelName; Layout.fillWidth: true
                Accessible.name: Design.UiText.get("模型 ID"); placeholderText: Design.UiText.get("Codex 默认模型"); maximumLength: 128
            }
            DialogAction { text: Design.UiText.get("查询模型"); onClicked: root.agent.listModels(mode.currentIndex) }
        }
        DialogSelect {
            visible: mode.currentIndex > 0 && count > 0; Layout.fillWidth: true
            model: root.agent.models; displayText: Design.UiText.get("选择已查询的模型")
            onActivated: modelName.text = currentText
        }
        Label { visible: mode.currentIndex > 0; text: Design.UiText.get("执行权限"); Layout.topMargin: 4 }
        DialogSelect {
            id: access; visible: mode.currentIndex > 0; Layout.fillWidth: true
            translateOptions: true; model: ["只读", "工作区写入", "完全访问"]
        }
        Label {
            visible: mode.currentIndex > 0; Layout.fillWidth: true; wrapMode: Text.Wrap
            color: Design.Theme.tokens.panelPlaceholderText; font.pixelSize: 12
            text: access.currentIndex === 2 ? Design.UiText.get("可访问文件和网络，执行时无需审批。")
                : access.currentIndex === 1 ? Design.UiText.get("允许修改当前工作区中的文件。") : Design.UiText.get("允许读取文件。")
        }
        Label {
            visible: mode.currentIndex > 0; Layout.fillWidth: true; elide: Text.ElideMiddle
            color: Design.Theme.tokens.panelPlaceholderText; font.pixelSize: 12; textFormat: Text.PlainText
            text: root.controller.workspacePath ? Design.UiText.get("工作区　") + root.controller.workspacePath
                : access.currentIndex > 0 ? Design.UiText.get("写入文件前，请先绑定工作区。") : Design.UiText.get("普通聊天 · 未绑定工作区")
        }
        Rectangle { visible: mode.currentIndex > 0; Layout.fillWidth: true; implicitHeight: 1; color: Design.Theme.tokens.panelMid; Layout.topMargin: 4 }
        CheckBox {
            id: attachHistory; padding: 0; spacing: 8
            indicator: Rectangle {
                implicitWidth: 16; implicitHeight: 16; radius: 4; y: (attachHistory.height - height) / 2
                color: attachHistory.checked ? root.palette.windowText : root.palette.base
                border.color: attachHistory.visualFocus ? root.palette.highlight : root.palette.mid
                Text { anchors.centerIn: parent; text: "✓"; visible: attachHistory.checked; color: root.palette.window }
            }
            visible: mode.currentIndex > 0 && root.turns.length > 0; text: Design.UiText.get("随下条消息发送所选历史")
        }
        RowLayout {
            visible: attachHistory.visible && attachHistory.checked
            DialogSelect { id: first; Layout.preferredWidth: 125; model: root.turns; onCurrentIndexChanged: root.contextPreview = ({}) }
            DialogSelect { id: last; Layout.preferredWidth: 125; model: root.turns; onCurrentIndexChanged: root.contextPreview = ({}) }
            DialogAction { text: Design.UiText.get("预览"); onClicked: root.contextPreview = root.agent.previewHistory(first.currentIndex,last.currentIndex) }
        }
        ScrollView {
            visible: attachHistory.visible && attachHistory.checked && !!(root.contextPreview.text || root.contextPreview.error)
            Layout.fillWidth: true; Layout.preferredHeight: 96
            TextArea {
                readOnly: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText
                text: root.contextPreview.error || ((root.contextPreview.names || []).join("、") + "\n" + (root.contextPreview.text || ""))
            }
        }
        Label {
            visible: mode.currentIndex > 0; Layout.fillWidth: true; wrapMode: Text.Wrap
            color: Design.Theme.tokens.panelPlaceholderText; font.pixelSize: 12
            text: Design.UiText.get("首次运行从新输入开始；续接线程时沿用已有上下文。")
        }
        }
    }
}
