pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import QtQuick.Controls
import QtQuick.Layouts

WorkbenchDialog {
    darkSurface: true
    id: root
    required property var backend
    required property string workspace
    property var rows: []
    property var document: ({})
    readonly property var states: ["未知","执行中","等待输入","已完成","失败","已停止"]
    title: "Agent 活动与仓库记忆"
    anchors.centerIn: parent
    sizeClass: WorkbenchDialog.Wide
    modal: true
    standardButtons: Dialog.Close
    function refresh() { backend.refreshActivity(workspace) }
    function limitText(window) {
        if (!window) return "未知"
        return window.percent + "%" + (window.reset ? "（" + new Date(window.reset * 1000).toLocaleString() + " 重置）" : "")
    }
    Connections {
        target: root.backend
        function onActivityReady(workspace, rows) { if (workspace === root.workspace) root.rows = rows }
    }
    Timer { interval: 3000; repeat: true; running: root.visible && tabs.currentIndex === 0; onTriggered: root.refresh() }
    onOpened: { document = ({}); refresh() }
    onWorkspaceChanged: { rows = []; document = ({}); if (visible) refresh() }
    contentItem: ColumnLayout {
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "状态与用量" }
            TabButton { text: "仓库记忆" }
        }
        ColumnLayout {
            visible: tabs.currentIndex === 0
            Label { text: "当前工作区 · 最近 50 条上报" }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: 250
                clip: true; model: root.rows
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.agent + "  ·  " + (root.states[modelData.status] || "未知")
                        + "  ·  " + String(modelData.session || "").slice(0,8)
                        + "  ·  " + new Date(modelData.updated).toLocaleTimeString()
                        + "\n最近一次上报的 token：" + (modelData.tokens ?? "未知")
                        + "\n主窗口：" + root.limitText(modelData.primary) + "    次窗口：" + root.limitText(modelData.secondary)
                }
                Label { visible: root.rows.length === 0; text: "此工作区尚无 Agent 上报" }
            }
            DialogAction { text: Design.UiText.get("刷新"); onClicked: root.refresh() }
        }
        ColumnLayout {
            visible: tabs.currentIndex === 1
            RowLayout {
                DialogInput {
                    id: path
                    Layout.fillWidth: true; text: "AGENTS.md"
                    placeholderText: "相对路径，如 src/CLAUDE.md"
                    onTextChanged: root.document = ({})
                    onAccepted: root.document = root.backend.readMemory(root.workspace,text)
                }
                DialogAction { text: "读取"; onClicked: root.document = root.backend.readMemory(root.workspace,path.text) }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 240
                TextArea { readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap; text: root.document.error || root.document.text || "" }
            }
            Label { visible: !!root.document.truncated; text: "显示前 64 KiB" }
        }
    }
}
