pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat
import QtCore as Core
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    required property ChatController controller
    readonly property bool disclosureMotionSuppressed: Design.InteractionMotion.suppressed
    readonly property int activeDisclosureMotions: Design.InteractionMotion.activeCount
    width: 1600
    height: 960
    minimumWidth: 740
    minimumHeight: 560
    visible: true
    flags: Qt.Window | (Qt.platform.os === "osx" ? Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint : 0)
    topPadding: 0
    property string diagnosticLabel: ""
    title: diagnosticLabel ? "EX Agent Code · " + diagnosticLabel : "EX Agent Code"
    color: Design.Theme.background
    font.family: Design.Theme.font
    property bool sidebarVisible: true
    property bool filesVisible: true
    WorkspaceController { id: workspaceController }
    Design.ChatMenu { id: chatMenu; controller: window.controller; onImageSequenceRequested: imageFolder.open() }
    Design.BlockWindowManager { controller: window.controller; hostWindow: window }
    FolderDialog { id: imageFolder; title: "选择图片序列目录"; onAccepted: window.controller.openImageSequence(selectedFolder) }
    FileDialog {
        id: exportDialog
        property var blockSnapshot: ({})
        title: "导出内容数据"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["JSON 数据 (*.json)"]
        onAccepted: window.controller.exportBlock(blockSnapshot, selectedFile)
    }
    MessageDialog { id: exportNotice; title: "内容已导出" }
    Connections {
        target: window.controller
        function onBlockExportRequested(block: var) { exportDialog.blockSnapshot = block; exportDialog.open() }
        function onBlockExported(path: string) { exportNotice.text = path; exportNotice.open() }
    }
    Design.ImagePreview { id: imagePreview; controller: window.controller }
    header: Design.WorkbenchTitleBar {
        id: titleBar
        windowHandle: window
        workspace: workspaceController
        sessionTitle: window.diagnosticLabel ? window.diagnosticLabel + " · " + window.controller.sessionTitle : window.controller.sessionTitle
        navigationWidth: 50 + (sidebar.visible ? sidebar.width + 8 : 0)
        filesWidth: filePanel.visible ? filePanel.width + 8 : 0
        onSidebarRequested: window.sidebarVisible = !window.sidebarVisible
        onFilesRequested: window.filesVisible = !window.filesVisible
        onMenuRequested: chatMenu.popup(titleBar, Math.max(0, titleBar.width - filesWidth - 220), titleBar.height)
    }
    Core.Settings { id: layoutSettings; property var splitState }
    onClosing: layoutSettings.splitState = workspaceSplit.saveState()
    Component.onCompleted: {
        if (layoutSettings.splitState) workspaceSplit.restoreState(layoutSettings.splitState)
        controller.setDarkTheme(Design.Theme.dark)
        // 真实会话资源保存在本机；新安装没有资源时正常显示空聊天。
        if (controller.resourceConversations.some(entry => entry.id === "webchat-go")) controller.loadResourceConversation("webchat-go")
    }

    Shortcut { sequence: StandardKey.New; onActivated: window.controller.newSession() }
    Shortcut { sequence: StandardKey.Copy; enabled: SelectionCoordinator.hasSelection; onActivated: SelectionCoordinator.copy() }
    Shortcut { sequence: "Ctrl+B"; onActivated: window.sidebarVisible = !window.sidebarVisible }
    Shortcut {
        sequence: "Ctrl+K"
        onActivated: { window.sidebarVisible = true; sidebar.searchField.forceActiveFocus() }
    }
    RowLayout {
        anchors.fill: parent
        anchors.topMargin: titleBar.height
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
                HoverHandler { cursorShape: Qt.SplitHCursor }
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
                Design.ChatScrollView {
                    id: transcript
                    anchors.fill: parent
                    controller: window.controller
                    onFileRequested: (path) => { window.filesVisible = true; workspaceController.open(path) }
                    onImageRequested: (image) => imagePreview.openImage(image)
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
            Item {
                visible: window.controller.activityStatus === 1
                Layout.fillWidth: true
                implicitHeight: 40
                Rectangle {
                    anchors.centerIn: parent
                    width: 36; height: 32; radius: 16
                    color: Design.Theme.background; border.color: Design.Theme.border
                    Design.Icon { anchors.centerIn: parent; name: "dots"; width: 20; height: 20; color: Design.Theme.muted }
                    Accessible.name: "正在处理"
                }
            }
            Design.ChangesSummary {
                visible: Number(window.controller.changeSummary.fileCount || 0) > 0
                Layout.fillWidth: true
                summary: window.controller.changeSummary
                onFileRequested: (path) => { window.filesVisible = true; workspaceController.open(path) }
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
            id: filePanel
            workspace: workspaceController
            controller: window.controller
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
        function onSessionChanged() {
            // 定位任务属于原会话，切换后不能继续加载另一段历史。
            transcript.resetForSession()
        }
    }
}
