pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat
import QtQuick.Dialogs

Design.WorkbenchWindow {
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
    property bool layoutReady: false
    property bool editorCloseApproved: false
    WorkspaceController { id: workspaceController; onExitRequested: { editorExit.open(); window.raise(); window.requestActivate() } }
    Design.GlobalNavigation {
        id: navigation
        chat: window.controller; workspace: workspaceController; terminals: consoleWindow
        onActivateHost: { window.raise(); window.requestActivate() }
        onRevealChat: dock.showPanel("chat")
        onRevealFiles: window.revealFiles()
        onApprovalRequested: token => agentApproval.present(token)
    }
    Design.AgentApprovalDialog { id: agentApproval; controller: window.controller }
    Design.WorkspaceActivityDialog {
        id: workspaceActivity
        terminalRows: consoleWindow.activityRows
        onOpenTerminal: id => { close(); consoleWindow.openActivityTab(id) }
        agent: window.controller.agent
        onOpenRun: (session, run, token) => {
            if (token && !agent.requestPending(session, run, token)) { refresh(); return }
            window.controller.selectSession(session)
            if (window.controller.currentSessionId !== session) { refresh(); return }
            dock.showPanel("chat")
            close()
            if (token) agentApproval.present(token)
        }
    }
    Design.ConversationTransferDialog { id: transferDialog; controller: window.controller }
    Design.SessionConsoleWindow {
        id: consoleWindow
        agent: window.controller.agent
        workspace: window.controller.workspacePath
        onHistoryChatRequested: source => { window.controller.importTerminalConversation(source); consoleWindow.close(); window.raise(); window.requestActivate() }
        onReturnRequested: conversation => {
            window.controller.selectSession(conversation)
            consoleWindow.close()
            window.raise()
            window.requestActivate()
        }
    }
    Design.MobileAccessDialog { id: mobileDialog; controller: window.controller }
    Design.SshWorkspaceDialog { id: sshDialog; controller: window.controller }
    Design.BotManagerDialog { id: botDialog; controller: window.controller }
    Design.PluginManagerDialog { id: pluginDialog; controller: window.controller }
    Design.SupervisionDialog { id: supervisionDialog; controller: window.controller }
    Design.SessionMailDialog { id: mailDialog; controller: window.controller }
    Design.ChatMenu { id: chatMenu; onSharingRequested: mobileDialog.begin(true); onMobileRequested: mobileDialog.begin(); onSshRequested: sshDialog.begin(); onBotsRequested: botDialog.begin(); onPluginsRequested: pluginDialog.begin(); onSupervisionRequested: supervisionDialog.begin(); onMailRequested: mailDialog.begin(); onConsoleRequested: consoleWindow.showConsole(); onOrganizeRequested: transferDialog.begin(); onAttachRequested: imageAttachment.open(); controller: window.controller; onImageSequenceRequested: imageFolder.open() }
    Design.BlockWindowManager { controller: window.controller; hostWindow: window }
    FileDialog {
        id: imageAttachment; title: "将图片加入会话"
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.webp *.gif *.bmp)"]
        onAccepted: window.controller.attachImage(selectedFile)
    }
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
        sessionTitle: window.diagnosticLabel ? window.diagnosticLabel + " · " + window.controller.sessionTitle : [window.controller.projectName, window.controller.workspacePath.split("/").pop(), window.controller.sessionTitle].filter(Boolean).join("  ›  ")
        navigationWidth: dock.customized ? 155 : 50 + (navSlot.visible ? navSlot.width + 7 : 0)
        filesWidth: !dock.customized && fileSlot.visible ? fileSlot.width + 7 : 0
        arranging: dock.arranging
        onArrangeRequested: dock.arranging = !dock.arranging
        onMoveRequested: dock.chooseMove()
        onResetRequested: dock.resetLayout()
        onSidebarRequested: dock.togglePanel("navigation")
        onFilesRequested: window.toggleFiles()
        onMenuRequested: chatMenu.popup(titleBar, Math.max(0, titleBar.width - filesWidth - 220), titleBar.height)
    }
    Design.LegacyPanelLayout { id: layoutSettings }
    function layoutScope() {
        return controller.previewMode ? "preview" : controller.currentWorkspaceId ? "workspace/" + controller.currentWorkspaceId : "project/" + controller.currentProjectId
    }
    function defaultPanels(reset: bool): var {
        // 首次迁移沿用旧版保存的两侧宽度；之后按工作区保存分支比例。
        const width = Math.max(1050,window.width-50)
        const nav = Math.min(width*0.40,reset ? 325 : layoutSettings.navWidth || 325)
        const files = Math.min((width-nav)*0.70,reset ? 600 : layoutSettings.previewWidth || 600)
        return {axis:1,fraction:nav/width,a:{tabs:["navigation"],active:"navigation"},
            b:{axis:1,fraction:1-files/(width-nav),a:{tabs:["chat"],active:"chat"},b:{tabs:["files","browser"],active:"files"}}}
    }
    function revealFiles() {
        if (dock.suppressed.includes("files")) dock.customized = true
        dock.showPanel("files")
    }
    function toggleFiles() {
        if (fileSlot.visible) dock.togglePanel("files")
        else revealFiles()
    }
    function saveWindowLayout() {
        dock.store()
        consoleWindow.saveLayout()
    }
    Connections {
        target: Qt.application
        function onAboutToQuit() { if (window.visible) window.saveWindowLayout() }
    }
    onClosing: event => {
        if (!window.editorCloseApproved && workspaceController.hasUnsaved) {
            event.accepted=false; editorExit.open(); return
        }
        saveWindowLayout()
        consoleWindow.hide()
    }
    Design.WorkbenchConfirmDialog {
        id: editorExit
        title: Design.UiText.get("退出前保存修改？")
        message: "可将修改保存到原文件，或保留恢复草稿以便下次继续。"
        detail: workspaceController.error
        detailIsError: !!detail
        primaryText: "保存并退出"
        secondaryText: "保留草稿退出"
        onPrimaryRequested: {
            if (workspaceController.saveAll()) {
                workspaceController.approveExit(); window.editorCloseApproved=true
                close(); window.close()
            }
        }
        onSecondaryRequested: {
            if (workspaceController.flushDrafts()) {
                workspaceController.approveExit(); window.editorCloseApproved=true
                close(); window.close()
            }
        }
    }
    Component.onCompleted: {
        layoutSettings.load()
        layoutReady = true
        dock.restore(layoutScope())
        Qt.callLater(() => consoleWindow.restoreWindow())
        controller.setDarkTheme(Design.Theme.dark)
        workspaceController.bindFolder(controller.workspacePath, controller.previewMode)
    }

    Shortcut { sequences: [StandardKey.New]; onActivated: window.controller.newSession() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: SelectionCoordinator.hasSelection; onActivated: SelectionCoordinator.copy() }
    Shortcut { sequence: "Ctrl+B"; onActivated: dock.togglePanel("navigation") }
    Shortcut {
        sequence: "Ctrl+K"
        onActivated: { dock.showPanel("navigation"); sidebar.showSearch() }
    }
    RowLayout {
        anchors.fill: parent
        anchors.topMargin: titleBar.height
        spacing: 0
        Design.NavigationRail {
            Layout.preferredWidth: 50
            Layout.fillHeight: true
            pendingCount: workspaceActivity.pendingCount
            unread: navigation.unread
            onJumpRequested: navigation.begin(false)
            onBrowserRequested: dock.showPanel("browser")
            onInboxRequested: navigation.showInbox()
            onActivityRequested: workspaceActivity.begin()
            onSidebarRequested: dock.togglePanel("navigation")
        }
        Design.DockSurface {
            id: dock
            Layout.fillWidth: true; Layout.fillHeight: true
            titles: ({"navigation":"项目","chat":"聊天","files":"文件","browser":Design.UiText.get("工作区浏览器")})
            minimumSizes: ({"navigation":[200,120],"chat":[360,240],"files":[260,160],"browser":[290,160]})
            optionalTabs: ({"browser":"files"})
            initialHidden: ["browser"].concat(layoutSettings.sidebarShown ? [] : ["navigation"]).concat(layoutSettings.filesShown ? [] : ["files"])
            suppressed: !customized && window.width < 1100 ? ["files"] : []
            makeDefault: window.defaultPanels
            Design.DockPanel {
                id: navSlot; host: dock; panelId: "navigation"
                Design.Sidebar {
                    id: sidebar; anchors.fill: parent
                    sleepingWorkspaces: consoleWindow.sleepingWorkspaces
                    controller: window.controller
                    onCollapseRequested: dock.togglePanel("navigation")
                }
            }
            Design.DockPanel {
                host: dock; panelId: "chat"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Design.BotSessionBar { Layout.fillWidth: true; controller: window.controller; onManageRequested: botDialog.begin() }
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
                            onFileRequested: (path) => { window.revealFiles(); workspaceController.open(path) }
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
                    }
                    // 为历史定位预留固定高度，按钮显隐不会覆盖文字或改变列表视口。
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: window.controller.messages.count > 0 ? 36 : 0
                        Design.IconButton {
                            anchors.centerIn: parent
                            visible: transcript.visible && !transcript.followingEnd && !transcript.atYEnd
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
                        onFileRequested: (path) => { window.revealFiles(); workspaceController.open(path) }
                    }
                    Design.Composer {
                        onApprovalRequested: agentApproval.present(window.controller.agent.approval.token || "")
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
            }
            Design.DockPanel {
                host: dock; panelId: "browser"
                Design.WorkspaceBrowser {
                    anchors.fill: parent
                    workspaceId: window.controller.currentWorkspaceId
                    sessionId: window.controller.currentSessionId
                    onElementPicked: (session,details,image) => {
                        if (session !== window.controller.currentSessionId) return
                        window.controller.captureDesignElement(session,details,image)
                        dock.showPanel("chat")
                        composer.input.forceActiveFocus()
                    }
                    writable: window.controller.projects.writable
                }
            }
            Design.DockPanel {
                id: fileSlot; host: dock; panelId: "files"
                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    Design.WorkspaceTabs {
                        visible: dock.customized && workspaceController.tabs.length > 0
                        Layout.fillWidth: true; Layout.preferredHeight: 32
                        workspace: workspaceController
                    }
                    Design.FilePanel {
                        id: filePanel
                        Layout.fillWidth: true; Layout.fillHeight: true
                        workspace: workspaceController; controller: window.controller
                    }
                }
            }
        }
    }
    Connections {
        target: Design.Theme
        function onDarkChanged() { window.controller.setDarkTheme(Design.Theme.dark); window.update() }
    }
    Connections {
        target: window.controller
        function onWorkspaceChanged() {
            if (window.layoutReady) dock.restore(window.layoutScope())
            workspaceController.bindFolder(window.controller.workspacePath, window.controller.previewMode)
        }
        function onSessionChanged() {
            // 定位任务属于原会话，切换后不能继续加载另一段历史。
            transcript.resetForSession()
        }
    }
}
