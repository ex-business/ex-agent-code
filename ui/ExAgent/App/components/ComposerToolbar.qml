pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.Chat
import QtQuick.Layouts
import ExAgent.App as Design

RowLayout {
    id: root
    required property AgentController agent
    property bool configurable
    signal settingsRequested()
    property bool hasText: false
    property bool canPaste: false
    signal sendRequested()
    signal pasteRequested()
    signal newChatRequested()
    signal previewRequested()
    signal longPreviewRequested()
    // 工具栏仅表达界面意图，由 Composer 连接编辑器和会话控制器。
    Layout.fillWidth: true
    spacing: 8
    Design.IconButton {
        id: moreButton
        iconName: "plus"; tooltip: "更多操作"
        onClicked: moreMenu.popup(moreButton, 0, -moreMenu.height)
        Design.ContextMenu {
            id: moreMenu
            Design.ContextMenuItem { text: "粘贴"; iconName: "copy"; shortcutLabel: Qt.platform.os === "osx" ? "⌘V" : ""; enabled: root.canPaste; onTriggered: root.pasteRequested() }
            Design.ContextMenuItem { text: Design.UiText.get("新聊天"); iconName: "new"; shortcutLabel: Qt.platform.os === "osx" ? "⌘N" : ""; onTriggered: root.newChatRequested() }
        }
    }
    RowLayout {
        spacing: 4
        Design.Icon { name: "shield"; color: Design.Theme.dark ? Design.Theme.warning : "#ae542c"; implicitWidth: 13; implicitHeight: 13 }
        Text { text: !root.agent.account ? "本地" : root.agent.fullAccess ? "完全访问" : root.agent.workspaceWrite ? "工作区写入" : "只读"; color: Design.Theme.dark ? Design.Theme.warning : "#ae542c"; font.pixelSize: 12 }
    }
    Item { Layout.fillWidth: true }
    Button {
        id: engineButton
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        Accessible.name: "会话执行设置"
        enabled: root.configurable && !root.agent.busy
        onClicked: root.settingsRequested()
        background: Rectangle { radius: 7; color: engineButton.hovered ? Design.Theme.hover : "transparent" }
        contentItem: RowLayout {
            spacing: 5
            Text { text: root.agent.account ? (root.agent.model || (root.agent.account === 3 ? "OpenAI API" : "Codex")) : "本地聊天"; color: Design.Theme.text; font.pixelSize: 12 }

            Design.Icon { name: "down"; implicitWidth: 12; implicitHeight: 12 }
        }

    }
    Design.IconButton { enabled: false; opacity: 0.45; iconName: "mic"; implicitWidth: 26; implicitHeight: 30; iconColor: Design.Theme.text; tooltip: "语音输入样式展示，尚未连接" }

    Design.SendButton {
        running: root.agent.busy
        stopping: root.agent.stopping
        objectName: "sendMessage"
        hasText: root.hasText
        onTriggered: { if (root.agent.busy) root.agent.stop(); else root.sendRequested() }
    }
}
