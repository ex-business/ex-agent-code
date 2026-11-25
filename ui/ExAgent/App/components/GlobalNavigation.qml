pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: hub
    required property ChatController chat
    required property WorkspaceController workspace
    required property Design.SessionConsoleWindow terminals
    readonly property int unread: notices.unread
    signal revealChat()
    signal revealFiles()
    signal activateHost()
    NavigationGuard { id: guard }
    function begin(files: bool) { if (!guard.blocked(jumpDialog)) { hub.activateHost(); jumpDialog.begin(files) } }
    function showInbox() { if (!guard.blocked(inbox)) { hub.activateHost(); inbox.begin() } }
    function jump(row, source): bool {
        if (guard.blocked(source)) return false
        if (row.file) {
            if (workspace.rootPath !== row.root || !workspace.writableWorkspace) return false
            workspace.openWorkspaceFile(row.file)
            hub.revealFiles()
        } else if (row.terminal) {
            if (!terminals.navigationTabs.some(tab=>tab.id === row.terminal)) return false
            terminals.openActivityTab(row.terminal)
        } else {
            if (row.session) {
                if (!chat.projects.rows.some(r=>r.kind === 3 && r.session === row.session)) return false
                chat.selectSession(row.session)
            } else {
                if (!chat.projects.rows.some(r=>r.kind === (row.workspace ? 2 : 1) &&
                        r.project === row.project && r.workspace === row.workspace)) return false
                chat.openGroup(row.project,row.workspace)
            }
            hub.revealChat()
            const request=chat.agent.approval
            // 旧通知可返回聊天；只有仍然相同的请求才展示审批面板。
            if (request.token && request.runId === row.run &&
                    (row.token === request.token || notices.matchesRequest(row, request.token)))
                Qt.callLater(hub.approvalRequested, request.token)
        }
        return true
    }
    signal approvalRequested(string token)
    NotificationCenter {
        id: notices
        managesBadge: hub.chat.projects.writable
        desktopBody: Design.UiText.get("打开 EX Agent Code 查看活动")
        onActivated: id => {
            if (guard.blocked(inbox)) return
            hub.showInbox()
            const entry=entries.find(r=>r.id === id)
            if (entry) inbox.visit(entry)
        }
    }
    Connections {
        target: hub.chat.agent
        function onApprovalArrived(session: string, run: string, token: string, title: string) {
            if (hub.chat.projects.writable) notices.record(NotificationCenter.Attention,session,run,token,title)
        }
        function onRunEnded(session: string, run: string, state: int, title: string) {
            if (hub.chat.projects.writable) notices.record(state===2 ? NotificationCenter.Completed :
                state===4 ? NotificationCenter.Stopped : state===5 ? NotificationCenter.Interrupted : NotificationCenter.Failed,session,run,"",title)
        }
    }
    Connections {
        target: hub.terminals
        function onActivityRowsChanged() { if (hub.chat.projects.writable) notices.observeTerminals(hub.terminals.activityRows) }
    }
    Design.QuickOpenDialog {
        id: jumpDialog; chat: hub.chat; workspace: hub.workspace
        terminalRows: hub.terminals.activityRows; terminalTabs: hub.terminals.navigationTabs
        onTargetChosen: row => {
            if (hub.jump(row,jumpDialog)) close()
            else if (files) begin(true)
            else refreshTargets()
        }
    }
    Design.NotificationInbox {
        id: inbox; center: notices; writable: hub.chat.projects.writable
        onVisit: entry => {
            if (hub.jump(entry,inbox)) {
                if (writable) notices.mark(entry.id,true)
                close()
            } else unavailable=true
        }
    }
    Shortcut { sequence: "Ctrl+P"; context: Qt.ApplicationShortcut; onActivated: hub.begin(true) }
    Shortcut { sequence: "Ctrl+J"; context: Qt.ApplicationShortcut; onActivated: hub.begin(false) }
}
