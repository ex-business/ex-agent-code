pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design
import "JumpTargets.js" as Targets

Design.WorkbenchDialog {
    id: panel
    required property ChatController chat
    required property WorkspaceController workspace
    required property var terminalRows
    required property var terminalTabs
    signal targetChosen(var row)
    property bool files: false
    property var targets: []
    property var results: []
    function updateResults(keepFile: bool) {
        if (!visible) return
        const selected = keepFile && list.currentIndex >= 0 ? results[list.currentIndex]?.file : ""
        results = files ? indexer.matches(input.text,workspace.tabs) : Targets.filter(targets,input.text)
        const kept = selected ? results.findIndex(row => row.file === selected) : -1
        list.currentIndex = kept >= 0 ? kept : results.length ? 0 : -1
    }
    function refreshTargets() {
        targets = Targets.collect(chat.projects.rows,chat.agent.workspaceActivity(true),terminalRows,terminalTabs)
        updateResults(false)
    }
    function begin(fileMode: bool) {
        files=fileMode
        input.clear()
        open()
        if (files) indexer.scan(workspace.writableWorkspace ? workspace.rootPath : "")
        else refreshTargets()
        input.forceActiveFocus()
    }
    function selectCurrent() {
        if (list.currentIndex >= 0 && list.currentIndex < results.length) targetChosen(results[list.currentIndex])
    }
    function move(step: int) {
        list.currentIndex=Math.max(0,Math.min(results.length-1,list.currentIndex+step))
        list.positionViewAtIndex(list.currentIndex,ListView.Contain)
    }
    QuickFileIndex { id: indexer; onChanged: if (panel.files) panel.updateResults(true) }
    onClosed: { indexer.scan(""); targets=[]; results=[] }
    parent: Overlay.overlay; modal: true; anchors.centerIn: Overlay.overlay
    sizeClass: Design.WorkbenchDialog.Wide
    height: Math.min(460, parent.height-32)
    title: files ? "快速打开文件" : "跳转工作树或会话"
    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Design.DialogInput {
                id: input; Layout.fillWidth: true
                placeholderText: Design.UiText.get(panel.files ? "搜索当前工作树的文件名或路径" : "搜索项目、工作树、会话或终端")
                onTextChanged: panel.updateResults(false)
                Keys.onDownPressed: panel.move(1)
                Keys.onUpPressed: panel.move(-1)
                onAccepted: panel.selectCurrent()
            }
            Design.DialogAction { text: Design.UiText.get(panel.files ? "切换工作树" : "查找文件"); onClicked: panel.begin(!panel.files) }
        }
        ListView {
            id: list; Layout.fillHeight: true; Layout.fillWidth: true
            clip: true; reuseItems: true; model: panel.results
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: list.width
                Accessible.name: modelData.title + " " + (modelData.detail || "")
                background: Rectangle { radius: 6; color: row.index === list.currentIndex ? Design.Theme.selected : row.hovered ? Design.Theme.hover : "transparent" }
                contentItem: RowLayout {
                    ColumnLayout {
                        spacing: 2; Layout.fillWidth: true; Layout.minimumWidth: 0
                        Label { text: row.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: row.modelData.detail || ""; textFormat: Text.PlainText; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11; color: Design.Theme.secondary }
                    }
                    Label { text: Design.UiText.get(row.modelData.type || "文件"); font.pixelSize: 11; color: row.modelData.pending ? Design.Theme.warning : Design.Theme.secondary }
                }
                onClicked: panel.targetChosen(modelData)
            }
            Label { anchors.centerIn: parent; visible: !list.count
                text: Design.UiText.get(panel.files && indexer.loading ? "正在索引文件…" : panel.files && !panel.workspace.writableWorkspace ? "请先打开工作树" : "没有匹配结果"); color: Design.Theme.secondary }
        }
        Label { visible: panel.files && indexer.limited; text: Design.UiText.get("当前索引达到十万项上限"); color: Design.Theme.secondary; font.pixelSize: 11 }
    }
}
