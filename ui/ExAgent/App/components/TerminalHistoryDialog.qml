pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import QtQuick.Controls
import QtQuick.Layouts

WorkbenchDialog {
    darkSurface: true
    id: root
    required property var backend
    required property string terminalId
    property int request: 0
    property bool loading: false
    property string notice
    title: Design.UiText.get("搜索终端回滚")
    anchors.centerIn: parent
    modal: true
    sizeClass: WorkbenchDialog.Wide
    standardButtons: Dialog.Close
    function refresh() {
        history.text = ""
        notice = ""
        loading = true
        request = backend.requestScrollback(terminalId)
    }
    function findMatch(forward, first) {
        const needle = query.text
        if (!needle || loading) { notice = ""; return }
        const full = history.text
        let at
        if (forward) {
            at = full.indexOf(needle, first ? 0 : history.selectionEnd)
            if (at < 0) at = full.indexOf(needle)
        } else {
            at = history.selectionStart > 0 ? full.lastIndexOf(needle,history.selectionStart - 1) : -1
            if (at < 0) at = full.lastIndexOf(needle)
        }
        if (at < 0) { history.deselect(); notice = "没有匹配"; return }
        history.select(at, at + needle.length)
        const match = history.positionToRectangle(at)
        const view = historyScroll.contentItem
        view.contentY = Math.max(0, Math.min(view.contentHeight - view.height, match.y - view.height / 3))
        view.contentX = Math.max(0, Math.min(view.contentWidth - view.width, match.x - view.width / 4))
        notice = "第 " + (full.slice(0,at).split("\n").length) + " 行"
    }
    onOpened: { refresh(); query.forceActiveFocus() }
    onTerminalIdChanged: if (visible) close()
    Connections {
        target: root.backend
        function onScrollbackReady(serial, text) {
            if (serial !== root.request || !root.visible) return
            root.loading = false
            history.text = text
            root.findMatch(true,true)
        }
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            DialogInput { id: query; Layout.fillWidth: true; placeholderText: Design.UiText.get("查找文本")
                onTextChanged: root.findMatch(true,true); onAccepted: root.findMatch(true,false) }
            DialogAction { text: "上一处"; enabled: !!query.text && !root.loading; onClicked: root.findMatch(false,false) }
            DialogAction { text: "下一处"; enabled: !!query.text && !root.loading; onClicked: root.findMatch(true,false) }
            DialogAction { text: Design.UiText.get("刷新"); onClicked: root.refresh() }
        }
        Label { text: root.loading ? "正在读取…" : root.notice || "最近 1500 行回滚及当前屏幕，最多 512 KiB" }
        ScrollView {
            id: historyScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.parent ? root.parent.height * 0.45 : 280, 420)
            TextArea {
                id: history
                readOnly: true; selectByMouse: true; persistentSelection: true
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.NoWrap
                color: root.darkSurface ? "#e3e8ef" : "#24272c"; padding: 10
                background: Rectangle { radius: 6; color: root.palette.base; border.color: root.palette.mid }
                font.family: Qt.fontFamilies().includes("MesloLGS NF") ? "MesloLGS NF" : "Menlo"
            }
        }
    }
}
