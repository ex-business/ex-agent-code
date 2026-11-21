pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.Chat
import ExAgent.App as Design

Design.ContextMenu {
    id: root
    required property ChatController controller
    Design.ContextMenuItem { text: "新聊天"; onTriggered: root.controller.newSession() }
    Design.ContextMenuItem { text: "预览消息样式"; onTriggered: root.controller.showPreview() }
    Design.ContextMenuItem { text: "当前 ChatGPT 对话"; onTriggered: root.controller.showConversationPreview() }
    Design.ContextMenuItem { text: "长代码与历史展示"; onTriggered: root.controller.showLongPreview() }
    Design.ContextMenuItem { text: "长聊天展示 · 300 轮"; onTriggered: root.controller.showLargeChatPreview() }
    Repeater {
        model: root.controller.resourceConversations
        delegate: Design.ContextMenuItem {
            required property var modelData
            text: modelData.title + " · " + modelData.messageCount + " 条"
            onTriggered: root.controller.loadResourceConversation(modelData.id)
        }
    }
    Design.ContextMenuItem { text: "复制本地会话文件路径"; onTriggered: root.controller.copyText(root.controller.storagePath) }
    MenuSeparator {}
    Design.ContextMenuItem { text: Design.Theme.dark ? "浅色主题" : "深色主题"; onTriggered: Design.Theme.dark = !Design.Theme.dark }
}
