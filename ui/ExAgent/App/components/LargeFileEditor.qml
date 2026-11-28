pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design
ColumnLayout {
    id: root
    required property EditorDocument document
    readonly property EditorPage page: document ? document.page : null
    property bool ready: false
    readonly property int selectionLength: Math.abs(content.selectionEnd-content.selectionStart)
    readonly property bool editingFocus: content.activeFocus
    function focusEditor() { content.forceActiveFocus() }
    function selectedContent() { return content.selectedText }
    function clearSelection() { content.deselect() }
    function locate() { if (ready && page) page.locate(document.cursorStart) }
    spacing: 4
    function attachPage() { if (ready && page && content.textDocument) { const desired=document.cursorStart; page.attach(content.textDocument); page.locate(desired) } }
    Component.onCompleted: { ready=true; attachPage() }
    onPageChanged: attachPage()
    Connections {
        target: root.page
        function onWindowChanged() {
            content.cursorPosition=root.page.cursor
            const rect=content.positionToRectangle(root.page.cursor)
            const view=scroll.contentItem
            view.contentY=Math.max(0,Math.min(view.contentHeight-view.height,rect.y-view.height/3))
            view.contentX=Math.max(0,Math.min(view.contentWidth-view.width,rect.x-view.width/4))
        }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
        DialogAction { text: Design.UiText.get("上一页"); enabled: !!root.page && root.page.number>1; onClicked: { root.page.show(root.page.number-1); root.focusEditor() } }
        Text { Layout.fillWidth: true; text: root.page ? root.page.number + " / " + root.page.count + " · " + Design.UiText.get("起始行") + " " + root.page.firstLine : ""; color: Design.Theme.secondary; font.pixelSize: 12 }
        DialogInput { id: targetLine; Layout.preferredWidth: 105; placeholderText: Design.UiText.get("跳转行号"); validator: IntValidator { bottom: 1; top: root.page ? root.page.lines : 1 }
            onAccepted: { root.page.goToLine(Number(text)); root.focusEditor() } }
        DialogAction { text: Design.UiText.get("下一页"); enabled: !!root.page && root.page.number<root.page.count; onClicked: { root.page.show(root.page.number+1); root.focusEditor() } }
    }
    Label { text: root.page ? root.page.error : ""; visible: !!text; textFormat: Text.PlainText; color: Design.Theme.warning; Layout.fillWidth: true; wrapMode: Text.Wrap }
    ScrollView {
        id: scroll
        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
        TextArea {
            id: content
            objectName: "largeFilePageEditor"
            // 分页视图不持有撤销栈，必须在 TextArea 默认处理前转交完整文档。
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: event => {
                if (event.matches(StandardKey.Undo)) { root.document.undo(); event.accepted=true }
                else if (event.matches(StandardKey.Redo)) { root.document.redo(); event.accepted=true }
            }
            textFormat: TextEdit.PlainText; wrapMode: TextEdit.NoWrap
            selectByMouse: true; persistentSelection: true; readOnly: !root.document.editable
            font.family: Design.Theme.monoFont; font.pixelSize: 13
            padding: 12; color: Design.Theme.text; selectionColor: Design.Theme.selected; selectedTextColor: Design.Theme.text
            background: Rectangle { color: Design.Theme.background }
            onCursorPositionChanged: if (root.ready && root.page) root.page.recordCursor(cursorPosition)
            onActiveFocusChanged: if (activeFocus) SelectionCoordinator.activate(root)
            onSelectedTextChanged: SelectionCoordinator.changed(root)
        }
    }
}
