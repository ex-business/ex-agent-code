pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Design.WorkbenchDialog {
    id: dialog
    required property ChatController chat
    readonly property var workspaces: visible ? chat.projects.rows.filter(row => row.kind === ProjectNavigator.WorkspaceRow) : []
    readonly property var tasks: remote.taskRows(workspaces)
    property string selectedWorkspace: ""
    property string selectedUrl: ""
    readonly property var selected: tasks.find(row => row.ws.workspace === selectedWorkspace && row.link.url === selectedUrl)
    property string notice: ""
    function selectTask(ws: string, url: string) {
        selectedWorkspace = ws
        selectedUrl = url
        remote.reset()
    }
    function present() {
        input.clear()
        notice = ""
        open()
        const active = chat.currentWorkspaceId
        const index = workspaces.findIndex(ws => ws.workspace === active)
        workspaceSelect.currentIndex = index >= 0 ? index : workspaces.length ? 0 : -1
        const first = tasks.find(row => row.ws.workspace === active) || tasks[0]
        selectTask(first ? first.ws.workspace : "",first ? first.link.url : "")
    }
    TaskStatus { id: remote }
    parent: Overlay.overlay
    anchors.centerIn: parent
    title: Design.UiText.get("关联任务")
    sizeClass: Design.WorkbenchDialog.Wide
    modal: true
    onClosed: remote.reset()
    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            visible: dialog.chat.projects.writable
            Layout.fillWidth: true
            Label { text: Design.UiText.get("目标工作区"); color: Design.Theme.secondary }
            Design.DialogSelect {
                id: workspaceSelect
                Layout.fillWidth: true
                Accessible.name: Design.UiText.get("目标工作区")
                model: dialog.workspaces.map(ws => ws.projectTitle + "  /  " + ws.path)
                readonly property var workspace: dialog.workspaces[currentIndex]
                displayText: workspace ? workspace.projectTitle + "  /  " + workspace.title : ""
                ToolTip.visible: hovered && currentIndex >= 0
                ToolTip.text: dialog.workspaces[currentIndex]?.path || ""
                ToolTip.delay: 500
            }
        }
        RowLayout {
            visible: dialog.chat.projects.writable
            Layout.fillWidth: true
            Design.DialogInput {
                id: input
                Layout.fillWidth: true
                maximumLength: 2048
                placeholderText: Design.UiText.get("GitHub、Linear 或 Jira 任务链接")
                onTextEdited: dialog.notice = ""
                onAccepted: if (bind.enabled) bind.clicked()
            }
            Design.DialogAction {
                id: bind
                text: Design.UiText.get("关联")
                enabled: workspaceSelect.currentIndex >= 0 && !!input.text.trim()
                onClicked: {
                    const link = remote.describe(input.text)
                    if (!link.url) { dialog.notice = Design.UiText.get("请输入有效的 PR 或工单链接。"); return }
                    const ws = dialog.workspaces[workspaceSelect.currentIndex]
                    if (!ws || !dialog.chat.linkWorkspaceTask(ws.project,ws.workspace,link.url)) {
                        dialog.notice = Design.UiText.get(dialog.chat.errorMessage) || Design.UiText.get("未能保存关联。")
                        return
                    }
                    dialog.selectTask(ws.workspace,link.url)
                    const i = dialog.tasks.findIndex(row => row.ws.workspace === ws.workspace && row.link.url === link.url)
                    if (i >= 0) list.positionViewAtIndex(i,ListView.Contain)
                    input.clear()
                    dialog.notice = ""
                }
            }
        }
        Label {
            Layout.fillWidth: true; visible: !!text
            text: dialog.notice || (!dialog.workspaces.length ? Design.UiText.get("请先在项目中添加工作区。") : "")
            textFormat: Text.PlainText; wrapMode: Text.Wrap
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(225,dialog.maximumBodyHeight * 0.42,contentHeight)
            clip: true; reuseItems: true; model: dialog.tasks
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: list.width; height: 45
                highlighted: dialog.selectedUrl === modelData.link.url && dialog.selectedWorkspace === modelData.ws.workspace
                Accessible.name: modelData.link.label + " · " + modelData.ws.path
                onClicked: dialog.selectTask(modelData.ws.workspace,modelData.link.url)
                contentItem: Column {
                    Label { width: parent.width; text: row.modelData.link.label; textFormat: Text.PlainText; elide: Text.ElideMiddle }
                    Label { width: parent.width; text: row.modelData.ws.projectTitle + "  /  " + row.modelData.ws.path; textFormat: Text.PlainText; elide: Text.ElideMiddle; font.pixelSize: 11; color: Design.Theme.secondary }
                }
                background: Rectangle {
                    radius: 6
                    color: row.highlighted ? Design.Theme.selected : row.hovered ? Design.Theme.hover : "transparent"
                }
            }
        }
        Label {
            visible: !!text
            Layout.fillWidth: true
            text: remote.error ? Design.UiText.get(remote.error) : remote.status ? remote.status + " · " + remote.title : ""
            textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; maximumLineCount: 3; elide: Text.ElideRight
        }
        RowLayout {
            visible: !!dialog.selected
            Design.DialogAction {
                text: Design.UiText.get(remote.busy ? "正在刷新…" : "刷新任务状态")
                enabled: !remote.busy
                onClicked: remote.refresh(dialog.selectedUrl)
            }
            Design.DialogAction {
                text: Design.UiText.get(dialog.selected && dialog.selected.link.kind === 1 ? "打开 PR 审查" : "打开工单")
                onClicked: if (!remote.openSource(dialog.selectedUrl)) dialog.notice = Design.UiText.get("无法打开任务链接。")
            }
        }
    }
    footer: Design.WorkbenchDialogActions {
        primaryText: Design.UiText.get("进入工作区")
        primaryEnabled: !!dialog.selected
        onPrimaryRequested: {
            const ws = dialog.selected.ws
            dialog.chat.openGroup(ws.project,ws.workspace)
            if (dialog.chat.currentWorkspaceId === ws.workspace && !dialog.chat.previewMode) dialog.close()
            else dialog.notice = dialog.chat.errorMessage
        }
        onCancelled: dialog.reject()
    }
}
