pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: root
    required property ProjectNavigator navigator
    signal openHistory(string project, string workspace)
    property string provider
    property string project
    property string workspace
    property var report: ({})
    property bool confirming: false
    property bool pending: false
    readonly property var entries: navigator.rows.filter(row => row.project === project && row.kind === ProjectNavigator.WorkspaceRow)
    title: "工作树资源管理器"
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay.width - 24)
    modal: true
    standardButtons: Dialog.Close
    function showProject(plugin: string, projectId: string, selected: string) {
        provider = plugin
        project = projectId
        picker.currentIndex = Math.max(0, entries.findIndex(row => row.workspace === selected))
        open()
        inspectSelection()
    }
    function inspectSelection() {
        confirming = false
        report = ({})
        workspace = entries[picker.currentIndex]?.workspace ?? ""
        if (workspace) perform(ProjectNavigator.Inspect)
    }
    function perform(action: int) {
        pending = true
        navigator.manageResource(provider, project, workspace, action, report.branch || "")
    }
    Connections {
        target: root.navigator
        function onResourceReport(project: string, workspace: string, report: var) {
            if (root.project !== project || root.workspace !== workspace) return
            root.report = report
            root.pending = false
            root.confirming = false
        }
    }
    contentItem: ColumnLayout {
        DialogSelect {
            id: picker
            Layout.fillWidth: true
            model: root.entries
            textRole: "title"
            enabled: !root.pending
            onActivated: root.inspectSelection()
        }
        Label { text: root.pending ? "正在检查工作树…" : root.report.path || "项目尚无工作区"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
        Label { text: "保留分支　" + (root.report.branch || "—"); Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
        Label { text: "运行中的任务　" + ((root.report.tasks || []).join("\n") || "无"); Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
        Label { text: root.report.reason || ""; visible: !!text; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
        Label { text: "将移除上方工作树目录。分支和聊天记录会保留。"; visible: root.confirming; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            DialogAction { text: "查看聊天"; enabled: !!root.workspace && !root.pending; onClicked: { root.openHistory(root.project, root.workspace); root.close() } }
            DialogAction { text: "刷新"; enabled: !!root.workspace && !root.pending; onClicked: root.inspectSelection() }
            DialogAction {
                text: root.report.state === 1 ? "取消归档" : "归档"
                enabled: root.navigator.writable && !root.pending && root.report.managed === true && !(root.report.tasks || []).length
                onClicked: root.perform(root.report.state === 1 ? ProjectNavigator.Restore : ProjectNavigator.Archive)
            }
            DialogAction {
                text: root.confirming ? "确认清理目录" : "清理工作树…"
                enabled: root.navigator.writable && !root.pending && root.report.state === 1 && root.report.cleanable === true && !(root.report.tasks || []).length
                onClicked: { if (root.confirming) root.perform(ProjectNavigator.Clean); else root.confirming = true }
            }
        }
    }
}
