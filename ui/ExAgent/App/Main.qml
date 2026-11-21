pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat
import QtCore as Core
import QtQml.Models

ApplicationWindow {
    id: window
    required property ChatController controller
    width: 1600
    height: 960
    minimumWidth: 740
    minimumHeight: 560
    visible: true
    title: "EX Agent Code"
    color: Design.Theme.background
    font.family: Design.Theme.font
    property bool sidebarVisible: true
    property bool filesVisible: true
    Core.Settings { id: layoutSettings; property var splitState }
    onClosing: layoutSettings.splitState = workspaceSplit.saveState()
    Component.onCompleted: {
        if (layoutSettings.splitState) workspaceSplit.restoreState(layoutSettings.splitState)
        controller.setDarkTheme(Design.Theme.dark)
        controller.loadResourceConversation("webchat-go")
    }

    Shortcut { sequence: StandardKey.New; onActivated: window.controller.newSession() }
    Shortcut { sequence: "Ctrl+B"; onActivated: window.sidebarVisible = !window.sidebarVisible }
    Shortcut {
        sequence: "Ctrl+K"
        onActivated: { window.sidebarVisible = true; sidebar.searchField.forceActiveFocus() }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Design.NavigationRail {
            Layout.preferredWidth: 50
            Layout.fillHeight: true
            onSidebarRequested: window.sidebarVisible = !window.sidebarVisible
        }
        SplitView {
            id: workspaceSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 8
                Accessible.role: Accessible.Separator
                Accessible.name: "调整面板宽度"
                color: SplitHandle.pressed ? Design.Theme.selected : SplitHandle.hovered ? Design.Theme.hover : "transparent"
                Rectangle { width: 1; height: parent.height; anchors.horizontalCenter: parent.horizontalCenter; color: Design.Theme.border }
            }
        Design.Sidebar {
            id: sidebar
            visible: window.sidebarVisible
            SplitView.preferredWidth: 325
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 480
            controller: window.controller
            onCollapseRequested: window.sidebarVisible = false
        }
        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 360
            spacing: 0
            Item {
                Layout.fillWidth: true
                implicitHeight: 36
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 15
                    anchors.rightMargin: 16
                    spacing: 9
                    Design.IconButton {
                        visible: !window.sidebarVisible
                        iconName: "sidebar"; tooltip: "展开侧栏 · ⌘B"
                        onClicked: window.sidebarVisible = true
                    }
                    Design.Icon { name: "folder"; implicitWidth: 15; implicitHeight: 15 }
                    Text { text: window.controller.sessionTitle; color: Design.Theme.text; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { visible: window.controller.messages.totalCount > 30; text: window.controller.messages.count + " / " + window.controller.messages.totalCount; color: Design.Theme.muted; font.pixelSize: 10 }
                    Design.IconButton { iconName: "sidebar"; tooltip: "显示或收起文件面板"; onClicked: window.filesVisible = !window.filesVisible }
                    Design.IconButton {
                        id: chatMenuButton
                        iconName: "dots"; tooltip: "聊天操作"
                        onClicked: chatMenu.popup(chatMenuButton, 0, chatMenuButton.height)
                        Design.ContextMenu {
                            id: chatMenu
                            Design.ContextMenuItem { text: "新聊天"; onTriggered: window.controller.newSession() }
                            Design.ContextMenuItem { text: "预览消息样式"; onTriggered: window.controller.showPreview() }
                            Design.ContextMenuItem { text: "当前 ChatGPT 对话"; onTriggered: window.controller.showConversationPreview() }
                            Design.ContextMenuItem { text: "长代码与历史展示"; onTriggered: window.controller.showLongPreview() }
                            Design.ContextMenuItem { text: "长聊天展示 · 300 轮"; onTriggered: window.controller.showLargeChatPreview() }
                            Repeater {
                                model: window.controller.resourceConversations
                                delegate: Design.ContextMenuItem {
                                    required property var modelData
                                    text: modelData.title + " · " + modelData.messageCount + " 条"
                                    onTriggered: window.controller.loadResourceConversation(modelData.id)
                                }
                            }
                            Design.ContextMenuItem { text: "复制本地会话文件路径"; onTriggered: window.controller.copyText(window.controller.storagePath) }
                            MenuSeparator {}
                            Design.ContextMenuItem { text: Design.Theme.dark ? "浅色主题" : "深色主题"; onTriggered: Design.Theme.dark = !Design.Theme.dark }
                        }
                    }
                }
            }
            Rectangle {
                visible: window.controller.errorMessage.length > 0
                Layout.fillWidth: true
                implicitHeight: errorRow.implicitHeight + 16
                color: Design.Theme.surface
                RowLayout {
                    id: errorRow
                    anchors.fill: parent
                    anchors.margins: 8
                    Text { text: window.controller.errorMessage; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Design.Theme.warning; font.pixelSize: 12 }
                    Design.IconButton { iconName: "close"; tooltip: "关闭提示"; onClicked: window.controller.clearError() }
                }
            }
            Item {
                id: transcriptContainer
                Layout.fillWidth: true
                Layout.fillHeight: true
                Design.EmptyState {
                    anchors.fill: parent
                    visible: window.controller.messages.count === 0
                    onPromptSelected: (prompt) => composer.setPrompt(prompt)
                    onPreviewRequested: window.controller.showPreview()
                }
                ListView {
                    id: transcript
                    objectName: "messageList"
                    property bool pixelWheelEnabled: true
                    signal pixelScrollStarted()
                    anchors.fill: parent
                    visible: window.controller.messages.count > 0
                    clip: true
                    pixelAligned: true
                    reuseItems: true
                    cacheBuffer: Math.min(height, 600)
                    spacing: 5
                    topMargin: 24
                    bottomMargin: 15
                    // 参考图最后一条回复下方留白，历史消息仍使用原有虚拟列表。
                    footer: Item { width: transcript.width; height: window.controller.previewMode && window.controller.sessionTitle === "评估用 Qt 开发 Codex 类 Agent" ? Math.max(180, transcript.height - 420) : 0 }
                    boundsBehavior: Flickable.StopAtBounds
                    acceptedButtons: Qt.NoButton
                    model: window.controller.messages
                    property bool followingEnd: true
                    property bool loadingOlder: false
                    property int jumpTarget: -1
                    function seekFraction(fraction: real) {
                        transcriptWheel.stop()
                        followingEnd = false
                        jumpTarget = Math.round(fraction * Math.max(0, window.controller.messages.totalCount - 1))
                        jumpPage.restart()
                    }
                    Timer {
                        id: jumpPage
                        interval: 16
                        onTriggered: {
                            const first = window.controller.messages.totalCount - window.controller.messages.count
                            if (transcript.jumpTarget < first && window.controller.messages.hasOlder) {
                                if (window.controller.messages.loadOlder() > 0) { restart(); return }
                            }
                            transcript.forceLayout()
                            transcript.positionViewAtIndex(Math.max(0, transcript.jumpTarget - (window.controller.messages.totalCount - window.controller.messages.count)), ListView.Beginning)
                            transcript.jumpTarget = -1
                        }
                    }
                    Design.SmoothWheel {
                        id: transcriptWheel
                        view: transcript
                        onScrollingStarted: transcript.followingEnd = false
                        onScrollingSettled: {
                            if (transcriptScrollBar.pressed) return
                            if (transcript.atYEnd) transcript.followingEnd = true
                            if (transcript.contentY - transcript.originY < 120) transcript.loadPreviousPage()
                        }
                    }
                    function loadPreviousPage() {
                        if (loadingOlder || jumpTarget >= 0 || transcriptScrollBar.pressed || transcriptWheel.animating || !window.controller.messages.hasOlder) return
                        loadingOlder = true
                        followingEnd = false
                        const row = Math.max(0, indexAt(width / 2, contentY + 2))
                        const item = itemAtIndex(row)
                        const inset = item ? contentY - item.y : 0
                        const added = window.controller.messages.loadOlder()
                        forceLayout()
                        positionViewAtIndex(row + added, ListView.Beginning)
                        const retained = itemAtIndex(row + added)
                        if (retained) contentY = retained.y + inset
                        Qt.callLater(() => { transcript.loadingOlder = false })
                    }
                    header: Item {
                        width: transcript.width
                        height: window.controller.messages.hasOlder ? 48 : 0
                        Button {
                            id: olderMessagesButton
                            anchors.centerIn: parent
                            visible: window.controller.messages.hasOlder
                            text: "加载更早的消息 · 还有 " + (window.controller.messages.totalCount - window.controller.messages.count) + " 条"
                            flat: true
                            onClicked: transcript.loadPreviousPage()
                            contentItem: Text { text: olderMessagesButton.text; color: Design.Theme.secondary; font.pixelSize: 12 }
                            background: Rectangle { radius: 8; color: olderMessagesButton.hovered ? Design.Theme.hover : "transparent" }
                        }
                    }
                    onMovementStarted: followingEnd = false
                    onMovementEnded: {
                        if (!transcriptScrollBar.pressed && atYEnd) followingEnd = true
                        if (!transcriptScrollBar.pressed && !transcriptWheel.animating && contentY - originY < 120 && window.controller.messages.hasOlder) loadPreviousPage()
                    }
                    ScrollBar.vertical: ScrollBar {
                        id: transcriptScrollBar
                        width: 6
                        policy: ScrollBar.AsNeeded
                        // cancelFlick 会同步触发 movementEnded，必须最后再关闭自动贴底。
                        onPressedChanged: if (pressed) { transcriptWheel.stop(); transcript.followingEnd = false }
                    }
                    function revealLatest() {
                        followingEnd = true
                        Qt.callLater(() => transcript.positionViewAtEnd())
                    }
                    onCountChanged: { if (followingEnd) Qt.callLater(() => transcript.positionViewAtEnd()) }
                    onContentHeightChanged: {
                        // 富文本完成布局后再贴齐底部，避免误显示“回到最新消息”。
                        if (followingEnd) latestLayout.restart()
                    }
                    Timer {
                        id: latestLayout
                        interval: 0
                        onTriggered: if (transcript.followingEnd) { transcript.forceLayout(); transcript.positionViewAtEnd() }
                    }
                    // 不同消息类型分别复用，避免工具记录把已创建的长回复控件反复替换掉。
                    delegate: DelegateChooser {
                        role: "messageKind"
                        DelegateChoice { roleValue: 1; delegate: MessageRow {} }
                        DelegateChoice { roleValue: 2; delegate: MessageRow {} }
                        DelegateChoice { roleValue: 3; delegate: MessageRow {} }
                        DelegateChoice { roleValue: 4; delegate: MessageRow {} }
                    }
                    component MessageRow: Item {
                        id: messageRow
                        required property int index
                        required property int messageKind
                        required property string content
                        required property string messageTitle
                        required property int messageStatus
                        required property bool expanded
                        required property var contentBlocks
                        width: ListView.view.width
                        height: messageView.implicitHeight
                        Design.MessageDelegate {
                            id: messageView
                            width: Math.min(messageRow.width - 48, 760)
                            anchors.horizontalCenter: parent.horizontalCenter
                            controller: window.controller
                            outerWheel: transcriptWheel
                            rowIndex: messageRow.index
                            messageKind: messageRow.messageKind
                            content: messageRow.content
                            messageTitle: messageRow.messageTitle
                            messageStatus: messageRow.messageStatus
                            expanded: messageRow.expanded
                            contentBlocks: messageRow.contentBlocks
                        }
                    }
                }
                Design.ChatOverview {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 18
                    anchors.bottomMargin: 18
                    view: transcript
                    messageCount: window.controller.messages.totalCount
                    loadedCount: window.controller.messages.count
                    currentFraction: {
                        const row = Math.max(0, transcript.indexAt(transcript.width / 2, transcript.contentY + 2))
                        return (messageCount - loadedCount + row) / Math.max(1, messageCount - 1)
                    }
                    onSeekRequested: (fraction) => transcript.seekFraction(fraction)
                }
                Design.IconButton {
                    visible: transcript.visible && !transcript.followingEnd && !transcript.atYEnd
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                    iconName: "down"
                    tooltip: "回到最新消息"
                    background: Rectangle { radius: 16; color: Design.Theme.surface; border.color: Design.Theme.border }
                    onClicked: transcript.revealLatest()
                }
            }
            Design.Composer {
                id: composer
                Layout.fillWidth: true
                Layout.maximumWidth: Math.min(parent.width - 30, 780)
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 15
                Layout.topMargin: 8
                controller: window.controller
                onSubmitted: transcript.revealLatest()
            }
        }
        Design.FilePanel {
            visible: window.filesVisible && window.width >= 1100
            SplitView.preferredWidth: 600
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: 1100
        }
        }
    }
    Connections {
        target: Design.Theme
        function onDarkChanged() { window.controller.setDarkTheme(Design.Theme.dark); window.update() }
    }
    Connections {
        target: window.controller
        function onSessionChanged() { transcriptWheel.stop(); transcript.revealLatest() }
    }
}
