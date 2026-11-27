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
    Design.ContextMenuItem { text: Design.UiText.get("Agent 历史与终端…"); iconName: "terminal"; onTriggered: root.consoleRequested() }
    signal pluginsRequested()
    Design.ContextMenuItem { text: Design.UiText.get("插件管理"); iconName: "grid"; enabled: !!root.controller.plugins; onTriggered: root.pluginsRequested() }
    signal supervisionRequested()
    Design.ContextMenuItem { text: Design.UiText.get("监督任务"); iconName: "activity"; enabled: root.controller.projects.writable && !root.controller.previewMode; onTriggered: root.supervisionRequested() }
    signal mailRequested()
    Design.ContextMenuItem { text: Design.UiText.get("会话通信"); iconName: "chat"; enabled: root.controller.projects.writable && !root.controller.previewMode; onTriggered: root.mailRequested() }
    signal organizeRequested()
    signal attachRequested()
    Design.ContextMenuItem { text: Design.UiText.get("添加图片…"); iconName: "image"; enabled: root.controller.projects.writable; onTriggered: root.attachRequested() }
    Design.ContextMenuItem {
        text: Design.UiText.get("整理会话…"); iconName: "folder"
        enabled: root.controller.projects.writable && !root.controller.previewMode && root.controller.messages.count > 0
        onTriggered: root.organizeRequested()
    }
    Design.ContextMenuItem { text: Design.UiText.get("新聊天"); iconName: "new"; shortcutLabel: Qt.platform.os === "osx" ? "⌘N" : ""; onTriggered: root.controller.newSession() }
    Design.ContextMenu {
        title: Design.UiText.get("本地会话与展示资源")
        Design.ContextMenuItem { text: Design.UiText.get("预览消息样式"); iconName: "chat"; onTriggered: root.controller.showPreview() }
        Design.ContextMenuItem { text: Design.UiText.get("当前 ChatGPT 对话"); onTriggered: root.controller.showConversationPreview() }
        Design.ContextMenuItem { text: Design.UiText.get("长代码与历史展示"); iconName: "code"; onTriggered: root.controller.showLongPreview() }
        Design.ContextMenuItem { text: Design.UiText.get("长聊天展示 · 300 轮"); onTriggered: root.controller.showLargeChatPreview() }
        Repeater {
            model: root.controller.resourceConversations
            delegate: Design.ContextMenuItem {
                required property var modelData
                text: modelData.title + " · " + modelData.messageCount + " 条"
                onTriggered: root.controller.loadResourceConversation(modelData.id)
            }
        }
        Design.ContextMenuItem { text: Design.UiText.get("复制本地会话文件路径"); onTriggered: root.controller.copyText(root.controller.storagePath) }
    }
    Design.ContextMenuItem { text: Design.UiText.get("核对最近一次 Codex 执行"); enabled: !root.controller.previewMode && root.controller.projects.writable && !root.controller.agent.busy; onTriggered: root.controller.agent.reconcile() }
    MenuSeparator {}
    Design.ContextMenuItem {
        text: Design.UiText.get("运行后台通信任务"); iconName: "terminal"
        enabled: root.controller.backgroundTaskState !== 0 && root.controller.backgroundTaskState !== 1
        onTriggered: root.controller.runBackgroundTask()
    }
    Design.ContextMenuItem {
        text: Design.UiText.get("取消后台任务"); iconName: "close"
        enabled: root.controller.backgroundTaskState === 0 || root.controller.backgroundTaskState === 1
        onTriggered: root.controller.cancelBackgroundTask()
    }
    MenuSeparator {}
    Design.ContextMenuItem {
        text: Design.UiText.get("统计已加载会话"); iconName: "activity"
        enabled: root.controller.internalTaskState !== 0 && root.controller.internalTaskState !== 1
        onTriggered: root.controller.analyzeLoadedConversation()
    }
    Design.ContextMenuItem { text: Design.UiText.get("打开图片序列…"); iconName: "image"; onTriggered: root.imageSequenceRequested() }
    Design.ContextMenuItem {
        text: Design.UiText.get("取消内部任务"); iconName: "close"
        enabled: root.controller.internalTaskState === 0 || root.controller.internalTaskState === 1
        onTriggered: root.controller.cancelInternalTask()
    }
    MenuSeparator {}
    Design.ContextMenu {
        id: themeMenu
        title: Design.UiText.get("主题")
        Instantiator {
            model: AppearanceRegistry.themes
            delegate: Design.ContextMenuItem {
                required property var modelData
                text: (AppearanceRegistry.theme === modelData.id ? "✓  " : "") + modelData.name
                onTriggered: AppearanceRegistry.theme = modelData.id
            }
            onObjectAdded: (index, item) => themeMenu.insertItem(index, item)
            onObjectRemoved: (index, item) => themeMenu.removeItem(item)
        }
    }
    Design.ContextMenu {
        id: languageMenu
        title: Design.UiText.get("语言")
        Instantiator {
            model: AppearanceRegistry.languages
            delegate: Design.ContextMenuItem {
                required property var modelData
                text: (AppearanceRegistry.language === modelData.id ? "✓  " : "") + modelData.name
                onTriggered: AppearanceRegistry.language = modelData.id
            }
            onObjectAdded: (index, item) => languageMenu.insertItem(index, item)
            onObjectRemoved: (index, item) => languageMenu.removeItem(item)
        }
    }
}
