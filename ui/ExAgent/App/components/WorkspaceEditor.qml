pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

ColumnLayout {
    id: root
    required property EditorDocument document
    signal previewRequested()
    property bool ready: false
    property bool locatingCursor: false
    property var boundDocument: null
    property bool showPreviewAction: true
    spacing: 0
    readonly property int selectionLength: largeLoader.item ? largeLoader.item.selectionLength : Math.abs(editor.selectionEnd-editor.selectionStart)
    function clearSelection() { if(largeLoader.item)largeLoader.item.clearSelection();else editor.deselect() }
    function selectedContent() { return largeLoader.item ? largeLoader.item.selectedContent() : editor.selectedText }
    function attachDocument() { if (root.ready && root.document && editor && editor.textDocument) { const changed=boundDocument!==root.document; if(!root.document.largeFile)root.document.attach(editor.textDocument,Design.Theme.dark); boundDocument=root.document; if(changed)Qt.callLater(root.locateCursor) } }
    function locateCursor() {
        if (!document || !ready) return
        if(document.largeFile){if(largeLoader.item)largeLoader.item.locate();return}
        locatingCursor=true
        editor.select(document.cursorStart,document.cursorEnd)
        const rect=editor.positionToRectangle(document.cursorStart)
        const view=scroll.contentItem
        view.contentY=Math.max(0,Math.min(view.contentHeight-view.height,rect.y-view.height/3))
        view.contentX=Math.max(0,Math.min(view.contentWidth-view.width,rect.x-view.width/4))
        locatingCursor=false
    }
    Connections { target: root.document; function onCursorRequested() { root.locateCursor(); root.focusEditor() } }
    function focusEditor() { if(largeLoader.item)largeLoader.item.focusEditor();else editor.forceActiveFocus() }
    Component.onCompleted: { ready=true; attachDocument() }
    onDocumentChanged: attachDocument()
    Connections { target: Design.Theme; function onDarkChanged() { root.attachDocument() } }
    Shortcut { sequences: [StandardKey.Save]; enabled: (editor.activeFocus || (largeLoader.item && largeLoader.item.editingFocus)) && !!root.document; onActivated: root.document.save() }
    Shortcut { sequence: Qt.platform.os === "osx" ? "Ctrl+Return" : "Ctrl+Space"; enabled: editor.activeFocus && !!languageTools.service; onActivated: languageTools.completion() }
    Shortcut { sequence: "F12"; enabled: editor.activeFocus && !!languageTools.service; onActivated: languageTools.definition() }
    Shortcut { sequence: "F2"; enabled: editor.activeFocus && !!languageTools.service; onActivated: languageTools.rename() }
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 8; spacing: 6
        Text { text: root.document && root.document.state===EditorDocument.Conflict ? Design.UiText.get("磁盘版本已变化") : root.document && root.document.state===EditorDocument.Modified ? Design.UiText.get("未保存") : Design.UiText.get("已保存"); color: root.document && root.document.state===EditorDocument.Conflict ? Design.Theme.warning : Design.Theme.secondary; font.pixelSize: 12; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
        Design.IconButton { text: Design.UiText.get("撤销"); iconName: "back"; tooltip: Design.UiText.get("撤销"); implicitWidth: 28; enabled: !!root.document && root.document.undoAvailable; focusPolicy: Qt.NoFocus; onClicked: root.document.largeFile ? root.document.undo() : editor.undo() }
        Design.IconButton { text: Design.UiText.get("重做"); iconName: "right"; tooltip: Design.UiText.get("重做"); implicitWidth: 28; enabled: !!root.document && root.document.redoAvailable; focusPolicy: Qt.NoFocus; onClicked: root.document.largeFile ? root.document.redo() : editor.redo() }
        DialogAction { visible: root.showPreviewAction; text: Design.UiText.get("预览"); onClicked: root.previewRequested() }
        DialogAction { text: Design.UiText.get("保存"); enabled: !!root.document && root.document.editable; primary: true; onClicked: { root.document.save(); root.focusEditor() } }
    }
    Label { visible: !!root.document && !!root.document.error; text: root.document ? Design.UiText.get(root.document.error) : ""; textFormat: Text.PlainText; color: Design.Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12 }
    RowLayout {
        visible: root.document && root.document.state===EditorDocument.Conflict; Layout.margins: 8
        DialogAction { text: Design.UiText.get("重新载入…"); onClicked: { decision.targetDocument=root.document; decision.overwrite=false; decision.open() } }
        DialogAction { text: Design.UiText.get("覆盖保存…"); onClicked: { decision.targetDocument=root.document; decision.overwrite=true; decision.open() } }
    }
    EditorLanguageTools {
        id: languageTools
        Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
        editor: editor; service: root.document && !root.document.largeFile ? root.document.languageService : null
    }
    Label {
        visible: !!root.document && root.document.largeFile
        text: Design.UiText.get("大文件模式：保留编辑、撤销与保存；关闭语法分析和 LSP，容量上限 8 MiB。")
        font.pixelSize: 11; color: Design.Theme.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
    }
    ScrollView {
        id: scroll
        visible: !root.document || !root.document.largeFile
        Layout.fillWidth: true; Layout.fillHeight: true
        clip: true
        TextArea {
            id: editor
            objectName: "workspaceTextEditor"
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.NoWrap
            selectByMouse: true; persistentSelection: true
            readOnly: !root.document || !root.document.editable
            font.family: Design.Theme.monoFont; font.pixelSize: 13
            padding: 12
            color: Design.Theme.text
            selectionColor: Design.Theme.selected; selectedTextColor: Design.Theme.text
            background: Rectangle { color: Design.Theme.background }
            Item {
                anchors.fill: parent; clip: true
                Repeater {
                    model: languageTools.service ? languageTools.service.diagnostics : []
                    Rectangle {
                        required property var modelData
                        readonly property rect beginRect: { const revision=editor.text; const width=editor.width; return editor.positionToRectangle(modelData.start) }
                        readonly property rect endRect: { const revision=editor.text; const width=editor.width; return editor.positionToRectangle(modelData.end) }
                        x: beginRect.x; y: beginRect.y+beginRect.height-2; height: 1
                        width: Math.max(4,beginRect.y===endRect.y ? endRect.x-beginRect.x : editor.width-beginRect.x-12)
                        color: modelData.severity===1 ? Design.Theme.danger : Design.Theme.warning
                    }
                }
            }
            onCursorPositionChanged: if (root.document && !root.document.largeFile && !root.locatingCursor) root.document.recordCaret(cursorPosition,cursorPosition)
            onActiveFocusChanged: if (activeFocus) SelectionCoordinator.activate(root)
            onSelectedTextChanged: SelectionCoordinator.changed(root)
        }
    }
    Loader {
        id: largeLoader
        Layout.fillWidth: true; Layout.fillHeight: true
        active: !!root.document && root.document.largeFile
        visible: active
        sourceComponent: Component { LargeFileEditor { document: root.document } }
    }
    WorkbenchConfirmDialog {
        id: decision
        property bool overwrite: false
        destructive: true
        property EditorDocument targetDocument
        title: Design.UiText.get(overwrite ? "覆盖磁盘文件？" : "重新载入磁盘文件？")
        message: overwrite ? "当前编辑内容会写入原路径，缺失文件会重建。" : "磁盘版本将替换当前编辑内容，撤销记录会重置。"
        contextText: targetDocument ? targetDocument.path : ""
        detail: targetDocument ? targetDocument.error : ""
        detailIsError: !!targetDocument && !!targetDocument.error
        primaryText: overwrite ? "覆盖保存" : "重新载入"
        onPrimaryRequested: {
            if (targetDocument && (overwrite ? targetDocument.save(true) : targetDocument.reload())) accept()
        }
    }
}
