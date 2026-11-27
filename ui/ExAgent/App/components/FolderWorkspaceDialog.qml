pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

WorkbenchDialog {
    id: panel
    required property var chat
    property string group: ""
    property var members: []
    property var workspaces: []
    property string error: ""
    property bool detached: false
    function reload() {
        const r = chat.projectGroups.operate("list")
        if (!r.ok) { error = r.error; return }
        members = r.result.items.filter(p => p.group === group)
        detached = chat.projects.rows.some(p=>p.kind===ProjectNavigator.ProjectRow && p.project===group && p.detachedFolders)
        workspaces = chat.projects.rows.filter(p => p.kind === ProjectNavigator.WorkspaceRow && p.project === group && (p.launch?.provider==="folder.workspace" || p.launch?.provider==="folder.detached"))
    }
    function present(id) { group = id; error = ""; name.text = ""; task.text = ""; reload(); open() }
    title: "文件夹工作区"
    modal: true; anchors.centerIn: Overlay.overlay
    sizeClass: WorkbenchDialog.Wide
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; text: Design.UiText.get("Agent 与文件树使用父目录，Git 操作使用任务来源仓库。多个文件夹工作区共享实际文件；需要独立分支时，请在子仓库创建任务工作树。"); wrapMode: Text.Wrap; color: Design.Theme.secondary }
        DialogInput { id: name; visible: !panel.detached; Layout.fillWidth: true; maximumLength: 80; placeholderText: Design.UiText.get("工作区名称") }
        Label { visible: !panel.detached; text: Design.UiText.get("任务来源仓库"); color: Design.Theme.text }
        DialogSelect { id: source; visible: !panel.detached; Layout.fillWidth: true; model: panel.members; textRole: "name"; valueRole: "id" }
        DialogInput { id: task; visible: !panel.detached; Layout.fillWidth: true; maximumLength: 2048; placeholderText: Design.UiText.get("关联任务链接（可选）") }
        ListView {
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(160, contentHeight)
            clip: true; model: panel.workspaces; spacing: 4
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width; height: 38
                Label { Layout.fillWidth: true; text: modelData.title + " · " + modelData.taskSourceTitle; elide: Text.ElideRight; textFormat: Text.PlainText; color: Design.Theme.text }
                DialogAction {
                    text: Design.UiText.get(parent.modelData.state === 0 ? "归档" : "恢复")
                    onClicked: {
                        const r = panel.chat.projectGroups.operate(parent.modelData.state === 0 ? "archive" : "restore", {group: panel.group, workspace: parent.modelData.workspace})
                        panel.error = r.ok ? "" : r.error; panel.reload()
                    }
                }
                DialogAction { text: Design.UiText.get("打开"); onClicked: { panel.chat.openGroup(panel.group, parent.modelData.workspace); panel.close() } }
            }
        }
        Label { Layout.fillWidth: true; visible: !!panel.error; text: panel.error; wrapMode: Text.Wrap; color: Design.Theme.text; textFormat: Text.PlainText }
    }
    footer: WorkbenchDialogActions {
        primaryText: panel.detached ? "完成" : "创建工作区"
        primaryEnabled: panel.detached || (!!name.text.trim() && source.currentIndex >= 0)
        onCancelled: panel.reject()
        onPrimaryRequested: {
            if(panel.detached){panel.close();return}
            const r = panel.chat.projectGroups.operate("create-folder", {group: panel.group, source: source.currentValue, name: name.text, task: task.text})
            if (!r.ok) panel.error = r.error
            else { panel.chat.openGroup(panel.group, r.result.id); panel.close() }
        }
    }
}
