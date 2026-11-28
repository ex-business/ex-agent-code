pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import ExAgent.App as Design
import ExAgent.Chat

ColumnLayout {
    id: root
    required property ChatController controller
    property alias input: editor
    signal submitted()
    signal approvalRequested()
    spacing: 0
    function send() {
        if (!controller.agent.busy && controller.sendMessage()) submitted()
    }
    function setPrompt(prompt: string) {
        controller.draft = prompt
        editor.forceActiveFocus()
    }
    Design.AccountProfilesDialog { id: profileSettings; controller: root.controller }
    Design.AgentSettings { id: agentSettings; controller: root.controller }
    Connections {
        target: root.controller
        function onDraftChanged() { if (editor.text !== root.controller.draft) editor.text = root.controller.draft }
    }
    Rectangle {
        id: composerSurface
        Layout.fillWidth: true
        implicitHeight: inputColumn.implicitHeight + 20
        radius: 24
        color: Design.Theme.dark ? Design.Theme.surface : "#ffffff"
        border.width: 1
        border.color: editor.activeFocus ? (Design.Theme.dark ? "#4b4b4b" : "#dedee0") : Design.Theme.border
        // 阴影独立于聊天视口，使用解析圆角阴影，不对消息列表做离屏模糊。
        RectangularShadow {
            anchors.fill: parent
            z: -1
            radius: composerSurface.radius
            blur: 24
            offset: Qt.vector2d(0, 3)
            color: Design.Theme.dark ? "#22000000" : "#0c000000"
        }
        ColumnLayout {
            id: inputColumn
            visible: true
            x: 12; y: 10
            width: parent.width - 24
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                visible: root.controller.designCaptureBusy || !!root.controller.designSelection.selector
                spacing: 6
                Design.Icon { name: "select"; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                Text {
                    Layout.fillWidth: true
                    text: root.controller.designCaptureBusy ? Design.UiText.get("正在准备页面截图…") : root.controller.designSelection.selector || ""
                    color: Design.Theme.secondary; font.pixelSize: 12; textFormat: Text.PlainText; elide: Text.ElideMiddle
                }
                Design.IconButton { iconName: "close"; iconSize: 13; implicitHeight: 24; implicitWidth: 24; tooltip: Design.UiText.get("移除页面附件"); onClicked: root.controller.clearDesignSelection() }
            }
            ScrollView {
                id: inputScroll
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(150, Math.max(40, editor.implicitHeight))
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                TextArea {
                    readOnly: !root.controller.projects.writable
                    id: editor
                    function clearSelection() { deselect() }
                    onActiveFocusChanged: if (activeFocus) SelectionCoordinator.activate(null)
                    objectName: "chatComposer"
                    placeholderText: root.controller.agent.account === 3 ? "向 OpenAI API Agent 发送消息" : "向 Codex 发送消息"
                    placeholderTextColor: Design.Theme.dark ? Design.Theme.muted : "#c4c4c4"
                    text: root.controller.draft
                    color: Design.Theme.text
                    selectionColor: Design.Theme.selected
                    font.family: Design.Theme.font
                    font.pixelSize: 14
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    topPadding: 3
                    bottomPadding: 4
                    leftPadding: 0
                    rightPadding: 0
                    background: Item {}
                    Accessible.name: "聊天输入框"
                    onTextChanged: {
                        if (text !== root.controller.draft) root.controller.draft = text
                    }
                    // 中文输入法正在组词时让 Enter 确认候选，不能误发送。
                    Keys.onReturnPressed: (event) => {
                        if (!inputMethodComposing && !(event.modifiers & Qt.ShiftModifier)) {
                            root.send()
                            event.accepted = true
                        } else event.accepted = false
                    }
                    Keys.onEnterPressed: (event) => {
                        if (!inputMethodComposing && !(event.modifiers & Qt.ShiftModifier)) {
                            root.send()
                            event.accepted = true
                        } else event.accepted = false
                    }
                }
            }
            Label {
                visible: root.controller.agent.busy && (root.controller.agent.stopping || !!root.controller.agent.approval.token)
                text: root.controller.agent.stopping ? "正在停止…" : "等待你批准操作"
                color: Design.Theme.secondary
                font.pixelSize: 12
            }
            Label { visible: root.controller.agent.contextLabel.length > 0; text: root.controller.agent.contextLabel }
            ColumnLayout {
                Layout.fillWidth: true
                visible: !!root.controller.agent.approval.token
                Label {
                    Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    text: root.controller.agent.approval.detail || ""
                    maximumLineCount: 5; elide: Text.ElideRight
                }
                RowLayout {
                    DialogAction { text: "查看审批详情"; onClicked: root.approvalRequested() }
                    DialogAction { text: "拒绝"; onClicked: root.controller.agent.decide(root.controller.agent.approval.token, false) }
                }
            }
            Design.ComposerToolbar {
                agent: root.controller.agent
                configurable: root.controller.projects.writable && !root.controller.previewMode
                onSettingsRequested: agentSettings.open()
                onProfilesRequested: profileSettings.open()
                Layout.fillWidth: true
                hasText: editor.text.trim().length > 0 && !root.controller.designCaptureBusy
                canPaste: editor.canPaste
                onSendRequested: root.send()
                onPasteRequested: editor.paste()
                onNewChatRequested: root.controller.newSession()
                onPreviewRequested: root.controller.showPreview()
                onLongPreviewRequested: root.controller.showLongPreview()
            }
        }
    }
    Text {
        visible: false
        Layout.alignment: Qt.AlignHCenter
        text: root.controller.previewMode ? "展示样例 · 输入消息将创建本地聊天" : "Enter 发送  ·  Shift + Enter 换行  ·  会话保存在本机"
        color: Design.Theme.muted
        font.pixelSize: 10
    }
}
