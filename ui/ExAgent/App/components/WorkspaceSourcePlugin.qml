pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design
ColumnLayout {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    property bool editing: true
    function togglePreview() { editing=!editing }
    spacing: 0
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 8
        visible: !!root.workspace.document && (root.workspace.document.editable ? !root.editing : root.workspace.document.state===EditorDocument.Conflict)
        Item { Layout.fillWidth: true }
        Design.DialogAction { text: Design.UiText.get("重新载入"); visible: !!root.workspace.document && !root.workspace.document.editable && root.workspace.document.state===EditorDocument.Conflict; onClicked: root.workspace.document.reload() }
        Design.DialogAction { visible: !!root.workspace.document && root.workspace.document.editable; text: Design.UiText.get("编辑"); enabled: !!root.workspace.document && root.workspace.document.editable; onClicked: root.editing=true }
    }
    Label { visible: !!text; text: root.workspace.document && !root.workspace.document.editable ? root.workspace.document.reason : ""; color: Design.Theme.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.margins: visible ? 12 : 0 }
    Loader {
        Layout.fillWidth: true; Layout.fillHeight: true
        sourceComponent: root.editing && root.workspace.document && root.workspace.document.editable ? editor : preview
    }
    Component { id: editor; Design.WorkspaceEditor { document: root.workspace.document; onPreviewRequested: root.editing=false } }
    Component { id: preview; Design.WorkspaceTextPreview { workspace: root.workspace; controller: root.controller } }
}
