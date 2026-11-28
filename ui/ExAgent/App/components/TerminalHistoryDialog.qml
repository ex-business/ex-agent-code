pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import ExAgent.Chat
import QtQuick.Controls
import QtQuick.Layouts

WorkbenchDialog {
    darkSurface: true
    id: root
    required property var backend
    required property string terminalId
    property int request: 0
    property bool loading: false
    property bool clipped: false
    property string readError: ""
    property string notice: ""
    readonly property color historyForeground: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelText : darkSurface ? "#e3e8ef" : "#24272c"
    readonly property color historyMuted: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelPlaceholderText : darkSurface ? "#a2abb8" : "#737a84"
    readonly property color historyBase: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelBase : darkSurface ? "#171c23" : "#fafafb"
    readonly property color historyBorder: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelMid : darkSurface ? "#424c59" : "#d9dce1"
    readonly property color historyHighlight: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelHighlight : darkSurface ? "#88a9ca" : "#526d89"
    readonly property color historySelected: darkSurface === Design.Theme.dark ? Design.Theme.tokens.panelHighlightedText : darkSurface ? "#14202d" : "#ffffff"
    title: Design.UiText.get("搜索终端回滚")
    anchors.centerIn: parent
    modal: true
    sizeClass: WorkbenchDialog.Wide
    standardButtons: Dialog.Close
    function refresh() {
        if (loading) return
        readError = ""; notice = ""; loading = true
        request = backend.requestHistory(terminalId)
    }
    function search() {
        notice = ""
        history.deselect()
        matches.search(history.text, query.text, caseOption.checked, wordOption.checked)
    }
    function findMatch(forward) {
        const match = matches.step(forward)
        if (match.start === undefined) return
        history.select(match.start, match.start + match.length)
        const rect = history.positionToRectangle(match.start)
        const view = historyScroll.contentItem
        view.contentY = Math.max(0, Math.min(view.contentHeight - view.height, rect.y - view.height / 3))
        view.contentX = Math.max(0, Math.min(view.contentWidth - view.width, rect.x - view.width / 4))
        notice = Design.UiText.get("第 %1 行").arg(match.line) + (match.wrapped ? " · " + Design.UiText.get("已循环") : "")
    }
    onOpened: { refresh(); query.forceActiveFocus() }
    onClosed: { loading = false; request = 0; matches.search("", "", false, false) }
    onTerminalIdChanged: if (visible) close()
    TerminalSearch {
        id: matches
        onReady: if (root.visible && !root.loading) root.findMatch(true)
    }
    Connections {
        target: root.backend
        function onHistoryReady(serial, snapshot) {
            if (serial !== root.request || !root.visible) return
            root.loading = false
            root.clipped = !!snapshot.limited
            root.readError = snapshot.error || ""
            history.text = snapshot.text || ""
            root.search()
        }
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            DialogInput {
                id: query; Layout.fillWidth: true; placeholderText: Design.UiText.get("查找文本")
                maximumLength: 4096
                onTextChanged: if (!root.loading) root.search()
                Keys.onReturnPressed: event => { root.findMatch(!(event.modifiers & Qt.ShiftModifier)); event.accepted = true }
                Keys.onEnterPressed: event => { root.findMatch(!(event.modifiers & Qt.ShiftModifier)); event.accepted = true }
                Keys.onPressed: event => { if (event.key === Qt.Key_F3) { root.findMatch(!(event.modifiers & Qt.ShiftModifier)); event.accepted = true } }
            }
            DialogAction { text: Design.UiText.get("上一处"); enabled: !root.loading && !matches.busy && matches.count > 0; onClicked: root.findMatch(false) }
            DialogAction { text: Design.UiText.get("下一处"); enabled: !root.loading && !matches.busy && matches.count > 0; onClicked: root.findMatch(true) }
            DialogAction { text: Design.UiText.get("刷新"); enabled: !root.loading; onClicked: root.refresh() }
        }
        RowLayout {
            DialogAction { id: caseOption; text: Design.UiText.get("区分大小写"); checkable: true; primary: checked; onToggled: root.search() }
            DialogAction { id: wordOption; text: Design.UiText.get("全词匹配"); checkable: true; primary: checked; onToggled: root.search() }
            Item { Layout.fillWidth: true }
            Label {
                text: root.loading ? Design.UiText.get("正在读取…") : matches.busy ? Design.UiText.get("正在查找…") : !query.text ? "" : matches.count ? matches.current + " / " + matches.count + (matches.limited ? "+" : "") : Design.UiText.get("没有匹配")
                color: root.historyForeground
                Accessible.name: text
            }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: root.readError || root.notice || Design.UiText.get("最近 20000 行回滚及当前屏幕；自动折行合并，最多 4 MiB")
            color: root.readError ? "#efaa92" : root.historyMuted
            font.pixelSize: 12
        }
        Label { visible: root.clipped && !root.readError; text: Design.UiText.get("已达到读取上限，仅搜索本次加载的尾部内容"); color: root.historyMuted; font.pixelSize: 12 }
        ScrollView {
            id: historyScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.parent ? root.parent.height * 0.45 : 280, 420)
            TextArea {
                id: history
                readOnly: true; selectByMouse: true; persistentSelection: true
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.NoWrap
                color: root.historyForeground; padding: 10
                selectedTextColor: root.historySelected; selectionColor: root.historyHighlight
                background: Rectangle { radius: 6; color: root.historyBase; border.color: root.historyBorder }
                font.family: Qt.fontFamilies().includes("MesloLGS NF") ? "MesloLGS NF" : "Menlo"
            }
        }
    }
}
