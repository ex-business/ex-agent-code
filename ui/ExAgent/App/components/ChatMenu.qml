pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.Chat
import ExAgent.App as Design

Design.ContextMenu {
    id: root
    required property ChatController controller
    signal imageSequenceRequested()
    signal consoleRequested()
    Design.ContextMenuItem { text: "Agent 历史与终端…"; iconName: "terminal"; onTriggered: root.consoleRequested() }
    signal organizeRequested()
    signal attachRequested()
    Design.ContextMenuItem { text: "添加图片…"; iconName: "image"; enabled: root.controller.projects.writable; onTriggered: root.attachRequested() }
    Design.ContextMenuItem {
        text: "整理会话…"; iconName: "folder"
        enabled: root.controller.projects.writable && !root.controller.previewMode && root.controller.messages.count > 0
        onTriggered: root.organizeRequested()
    }
    Design.ContextMenuItem { text: "新聊天"; iconName: "new"; shortcutLabel: Qt.platform.os === "osx" ? "⌘N" : ""; onTriggered: root.controller.newSession() }
    Design.ContextMenu {
        title: "本地会话与展示资源"
        Design.ContextMenuItem { text: "预览消息样式"; iconName: "chat"; onTriggered: root.controller.showPreview() }
        Design.ContextMenuItem { text: "当前 ChatGPT 对话"; onTriggered: root.controller.showConversationPreview() }
        Design.ContextMenuItem { text: "长代码与历史展示"; iconName: "code"; onTriggered: root.controller.showLongPreview() }
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
    }
    Design.ContextMenuItem { text: "核对最近一次 Codex 执行"; enabled: !root.controller.previewMode && root.controller.projects.writable && !root.controller.agent.busy; onTriggered: root.controller.agent.reconcile() }
    MenuSeparator {}
    Design.ContextMenuItem {
        text: "运行后台通信任务"; iconName: "terminal"
        enabled: root.controller.backgroundTaskState !== 0 && root.controller.backgroundTaskState !== 1
        onTriggered: root.controller.runBackgroundTask()
    }
    Design.ContextMenuItem {
        text: "取消后台任务"; iconName: "close"
        enabled: root.controller.backgroundTaskState === 0 || root.controller.backgroundTaskState === 1
        onTriggered: root.controller.cancelBackgroundTask()
    }
    MenuSeparator {}
    Design.ContextMenuItem {
        text: "统计已加载会话"; iconName: "activity"
        enabled: root.controller.internalTaskState !== 0 && root.controller.internalTaskState !== 1
        onTriggered: root.controller.analyzeLoadedConversation()
    }
    Design.ContextMenuItem { text: "打开图片序列…"; iconName: "image"; onTriggered: root.imageSequenceRequested() }
    Design.ContextMenuItem {
        text: "取消内部任务"; iconName: "close"
        enabled: root.controller.internalTaskState === 0 || root.controller.internalTaskState === 1
        onTriggered: root.controller.cancelInternalTask()
    }
    MenuSeparator {}
    Design.ContextMenuItem { iconName: "sun"; text: Design.Theme.dark ? "浅色主题" : "深色主题"; onTriggered: Design.Theme.dark = !Design.Theme.dark }
}
