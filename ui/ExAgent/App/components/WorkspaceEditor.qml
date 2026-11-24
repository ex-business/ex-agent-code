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
    property bool showPreviewAction: true
    spacing: 0
    readonly property int selectionLength: Math.abs(editor.selectionEnd-editor.selectionStart)
    function clearSelection() { editor.deselect() }
    function selectedContent() { return editor.selectedText }
    function attachDocument() { if (root.ready && root.document && editor && editor.textDocument) root.document.attach(editor.textDocument,Design.Theme.dark) }
    Component.onCompleted: { ready=true; attachDocument() }
    onDocumentChanged: attachDocument()
    Connections { target: Design.Theme; function onDarkChanged() { root.attachDocument() } }
    Shortcut { sequences: [StandardKey.Save]; enabled: editor.activeFocus && !!root.document; onActivated: root.document.save() }
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 8; spacing: 6
        Text { text: root.document && root.document.state===EditorDocument.Conflict ? Design.UiText.get("磁盘版本已变化") : root.document && root.document.state===EditorDocument.Modified ? Design.UiText.get("未保存") : Design.UiText.get("已保存"); color: root.document && root.document.state===EditorDocument.Conflict ? Design.Theme.warning : Design.Theme.secondary; font.pixelSize: 12; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
        Design.IconButton { text: Design.UiText.get("撤销"); iconName: "back"; tooltip: Design.UiText.get("撤销"); implicitWidth: 28; enabled: !!root.document && root.document.undoAvailable; focusPolicy: Qt.NoFocus; onClicked: editor.undo() }
        Design.IconButton { text: Design.UiText.get("重做"); iconName: "right"; tooltip: Design.UiText.get("重做"); implicitWidth: 28; enabled: !!root.document && root.document.redoAvailable; focusPolicy: Qt.NoFocus; onClicked: editor.redo() }
        DialogAction { visible: root.showPreviewAction; text: Design.UiText.get("预览"); onClicked: root.previewRequested() }
        DialogAction { text: Design.UiText.get("保存"); enabled: !!root.document && root.document.editable; primary: true; onClicked: { root.document.save(); editor.forceActiveFocus() } }
    }
    Label { visible: !!root.document && !!root.document.error; text: root.document ? Design.UiText.get(root.document.error) : ""; textFormat: Text.PlainText; color: Design.Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12 }
    RowLayout {
        visible: root.document && root.document.state===EditorDocument.Conflict; Layout.margins: 8
        DialogAction { text: Design.UiText.get("重新载入…"); onClicked: { decision.targetDocument=root.document; decision.overwrite=false; decision.open() } }
        DialogAction { text: Design.UiText.get("覆盖保存…"); onClicked: { decision.targetDocument=root.document; decision.overwrite=true; decision.open() } }
    }
    ScrollView {
        id: scroll
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
            onActiveFocusChanged: if (activeFocus) SelectionCoordinator.activate(root)
            onSelectedTextChanged: SelectionCoordinator.changed(root)
        }
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
