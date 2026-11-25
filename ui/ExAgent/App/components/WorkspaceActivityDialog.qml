pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Design.WorkbenchDialog {
    id: panel
    required property AgentController agent
    required property var terminalRows
    readonly property int pendingCount: agent.pendingApprovalCount + terminalRows.reduce((n, row) => n + row.pending, 0)
    signal openTerminal(string id)
    signal openRun(string session, string run, string token)
    property bool waitingOnly: false
    property var rows: []
    function refresh() {
        if (!visible) return
        const merged = agent.workspaceActivity(waitingOnly).concat(terminalRows.filter(row => !waitingOnly || row.pending))
        rows = merged.sort((a, b) => a.rank - b.rank || b.time - a.time)
    }
    function begin() {
        waitingOnly = pendingCount > 0
        open()
    }
    onVisibleChanged: refresh()
    onTerminalRowsChanged: refresh()
    onWaitingOnlyChanged: refresh()
    Connections {
        target: panel.agent
        function onChanged() { if (panel.visible) Qt.callLater(panel.refresh) }
    }
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    title: "工作区活动"
    sizeClass: Design.WorkbenchDialog.Wide
    height: Math.min(470, parent.height - 32)
    modal: true
    standardButtons: Dialog.Close
    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Design.DialogAction { text: Design.UiText.get("全部活动"); primary: !panel.waitingOnly; onClicked: panel.waitingOnly = false }
            Design.DialogAction { text: Design.UiText.get("等待处理") + " (" + panel.pendingCount + ")"; primary: panel.waitingOnly; onClicked: panel.waitingOnly = true }
        }
        ListView {
            id: list
            Layout.fillHeight: true; Layout.fillWidth: true
            clip: true; model: panel.rows; reuseItems: true
            ScrollBar.vertical: ScrollBar {}
            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: Design.UiText.get(panel.waitingOnly ? "没有等待处理的请求" : "暂无会话执行记录")
                color: Design.Theme.secondary
            }
            delegate: ItemDelegate {
                id: row
                required property var modelData
                readonly property string statusText: Design.UiText.get(modelData.idle ? "空闲" : modelData.pending ? "等待处理" :
                    (["准备中", "正在执行", "已完成", "执行失败", "已取消", "结果待核对"][modelData.state] || "结果待核对"))
                width: list.width; hoverEnabled: true
                leftPadding: 10; rightPadding: 14
                Accessible.name: modelData.title + " · " + statusText
                background: Rectangle { color: row.hovered || row.visualFocus ? Design.Theme.hover : "transparent"; radius: 6 }
                contentItem: RowLayout {
                    spacing: 12
                    ColumnLayout {
                        spacing: 3; Layout.fillWidth: true; Layout.minimumWidth: 0
                        Label { text: row.modelData.title || Design.UiText.get("新聊天"); textFormat: Text.PlainText; elide: Text.ElideRight; Layout.fillWidth: true; font.weight: Font.Medium }
                        Label { text: [row.modelData.project, row.modelData.workspace].filter(Boolean).join("  ·  "); textFormat: Text.PlainText; elide: Text.ElideMiddle; Layout.fillWidth: true; color: Design.Theme.secondary; font.pixelSize: 11 }
                    }
                    ColumnLayout {
                        spacing: 3
                        Label { Layout.alignment: Qt.AlignRight; text: row.statusText + (row.modelData.pending > 1 ? " ×" + row.modelData.pending : ""); font.pixelSize: 12; color: row.modelData.pending ? Design.Theme.warning : row.modelData.state === 3 ? Design.Theme.danger : Design.Theme.secondary }
                        Label { Layout.alignment: Qt.AlignRight; text: Qt.formatDateTime(new Date(row.modelData.time), "MM-dd hh:mm"); font.pixelSize: 11; color: Design.Theme.secondary }
                    }
                }
                onClicked: {
                    if (modelData.terminal) panel.openTerminal(modelData.terminal)
                    else panel.openRun(modelData.session, modelData.run, modelData.token || "")
                }
            }
        }
    }
}
