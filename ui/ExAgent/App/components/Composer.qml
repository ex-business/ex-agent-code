pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

ColumnLayout {
    id: root
    required property ChatController controller
    property alias input: editor
    signal submitted()
    spacing: 0
    function send() {
        if (controller.sendMessage()) submitted()
    }
    function setPrompt(prompt: string) {
        controller.draft = prompt
        editor.forceActiveFocus()
    }
    Connections {
        target: root.controller
        function onDraftChanged() { if (editor.text !== root.controller.draft) editor.text = root.controller.draft }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: inputColumn.implicitHeight + 20
        radius: 23
        color: Design.Theme.dark ? Design.Theme.surface : "#ffffff"
        border.width: 1
        border.color: editor.activeFocus ? (Design.Theme.dark ? "#4b4b4b" : "#c8c8c8") : Design.Theme.border
        ColumnLayout {
            id: inputColumn
            visible: true
            x: 12; y: 10
            width: parent.width - 24
            spacing: 5
            ScrollView {
                id: inputScroll
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(150, Math.max(40, editor.implicitHeight))
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                TextArea {
                    id: editor
                    objectName: "chatComposer"
                    placeholderText: "帮我打开程序，我看下效果"
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
            RowLayout {
                Layout.fillWidth: true
                spacing: 5
                Design.IconButton {
                    id: moreButton
                    iconName: "plus"; tooltip: "更多操作"
                    onClicked: moreMenu.popup(moreButton, 0, -moreMenu.height)
                    Design.ContextMenu {
                        id: moreMenu
                        Design.ContextMenuItem { text: "粘贴"; enabled: editor.canPaste; onTriggered: editor.paste() }
                        Design.ContextMenuItem { text: "新聊天"; onTriggered: root.controller.newSession() }
                        MenuSeparator {}
                        Design.ContextMenuItem { text: "预览消息样式"; onTriggered: root.controller.showPreview() }
                        Design.ContextMenuItem { text: "长代码与历史展示"; onTriggered: root.controller.showLongPreview() }
                    }
                }
                Text { text: "♧ 完全访问"; color: Design.Theme.dark ? Design.Theme.warning : "#ae542c"; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Button {
                    id: engineButton
                    Accessible.name: "引擎状态：尚未连接"
                    onClicked: engineMenu.popup(engineButton, 0, -engineMenu.height)
                    background: Rectangle { radius: 7; color: engineButton.hovered ? Design.Theme.hover : "transparent" }
                    contentItem: RowLayout {
                        Text { text: "GPT-6.1 Sol 轻度"; color: Design.Theme.text; font.pixelSize: 12 }
                        Design.Icon { name: "down"; implicitWidth: 12; implicitHeight: 12 }
                    }
                    Design.ContextMenu {
                        id: engineMenu
                        Design.ContextMenuItem { text: "Codex · 尚未接入"; enabled: false }
                        Design.ContextMenuItem { text: "Pi · 后续扩展"; enabled: false }
                    }
                }
                Design.Icon { name: "mic"; implicitWidth: 17; implicitHeight: 17; color: Design.Theme.text }
                AbstractButton {
                    id: sendButton
                    objectName: "sendMessage"
                    implicitWidth: 30
                    implicitHeight: 30
                    enabled: true
                    Accessible.name: editor.text.trim().length > 0 ? "发送消息" : "语音样式展示，未连接"
                    onClicked: { if (editor.text.trim().length > 0) root.send(); else engineMenu.popup(engineButton, 0, -engineMenu.height) }
                    background: Rectangle { radius: 15; color: Design.Theme.accent }
                    contentItem: Item { Design.Icon { anchors.centerIn: parent; name: editor.text.trim().length > 0 ? "arrow" : "wave"; implicitWidth: 19; implicitHeight: 19; color: Design.Theme.onAccent } }
                    ToolTip.visible: hovered
                    ToolTip.text: editor.text.trim().length > 0 ? "发送 · Enter" : "语音展示 · 未连接 Agent"
                }
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
