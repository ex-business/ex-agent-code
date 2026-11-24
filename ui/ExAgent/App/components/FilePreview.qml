pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    property bool renderedMarkdown: true
    readonly property bool markdown: ["md","markdown","mdown"].includes(workspace.language) && renderedMarkdown
    property bool editing: true
    function togglePreview() { if (markdown && viewLoader.item) viewLoader.item.viewMode=viewLoader.item.viewMode===2 ? 1 : 2; else editing=!editing }
    property int closeIndex: -1
    property string closeRoot: ""
    property EditorDocument closeDocument
    function finishClose(save) {
        if (root.workspace.rootPath!==closeRoot || root.workspace.documentAt(closeIndex)!==closeDocument) { closeDialog.close(); return }
        if (save && !closeDocument.save()) return
        root.workspace.closeTab(closeIndex,true)
        if (root.workspace.documentAt(closeIndex)!==closeDocument) closeDialog.close()
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            visible: !root.markdown && !!root.workspace.document && (root.workspace.document.editable ? !root.editing : root.workspace.document.state===EditorDocument.Conflict)
            Layout.fillWidth: true; Layout.margins: 8
            Item { Layout.fillWidth: true }
            DialogAction { text: "重新载入"; visible: !!root.workspace.document && !root.workspace.document.editable && root.workspace.document.state===EditorDocument.Conflict; onClicked: root.workspace.document.reload() }
            DialogAction { text: "编辑"; visible: !!root.workspace.document && root.workspace.document.editable; enabled: !!root.workspace.document && root.workspace.document.editable; onClicked: root.editing=!root.editing }
        }
        Label { visible: !!root.workspace.document && !root.workspace.document.editable; text: root.workspace.document ? root.workspace.document.reason : ""; wrapMode: Text.Wrap; color: Design.Theme.secondary; Layout.fillWidth: true; Layout.margins: 12 }
        Loader {
            id: viewLoader
            Layout.fillWidth: true; Layout.fillHeight: true
            active: root.visible && (!!root.workspace.document || !!root.workspace.imageInfo.url)
            sourceComponent: root.workspace.imageInfo.url ? imageViewer : root.markdown ? markdownEditor : root.editing && root.workspace.document && root.workspace.document.editable ? editable : readOnlyPreview
        }
    }
    Component {
        id: editable
        Design.WorkspaceEditor { document: root.workspace.document; onPreviewRequested: root.editing=false }
    }
    Component { id: readOnlyPreview; Design.WorkspaceTextPreview { workspace: root.workspace; controller: root.controller; markdown: root.markdown } }
    Component { id: markdownEditor; Design.WorkspaceMarkdownView { workspace: root.workspace; controller: root.controller } }
    Component { id: imageViewer; Design.WorkspaceImageViewer { imageInfo: root.workspace.imageInfo; controller: root.controller; onDetachRequested: { imageWindow.info=root.workspace.imageInfo; imageWindow.show(); imageWindow.raise(); imageWindow.requestActivate() } } }
    Design.WorkbenchWindow {
        id: imageWindow
        transientParent: root.Window.window
        Shortcut { sequence: "Escape"; enabled: imageWindow.visible && imageWindow.active; onActivated: imageWindow.close() }
        property var info: ({})
        width: 960; height: 680; minimumWidth: 320; minimumHeight: 280
        title: info.name || Design.UiText.get("图片")
        color: Design.Theme.background
        Loader { anchors.fill: parent; active: imageWindow.visible; sourceComponent: Design.WindowEntrance { ready: true; Design.WorkspaceImageViewer { anchors.fill: parent; imageInfo: imageWindow.info; controller: root.controller; allowDetach: false } } }
    }
    // 宿主确认关闭后收回附属查看窗口，取消退出时不影响查看器。
    Connections { target: root.Window.window; function onVisibleChanged() { if (root.Window.window && !root.Window.window.visible) imageWindow.close() } }
    Connections {
        target: root.workspace
        function onPreviewChanged() { if (imageWindow.visible && !root.workspace.imageAvailable(imageWindow.info.url)) imageWindow.close() }
        function onCloseRequested(index: int) {
            root.closeIndex=index; root.closeRoot=root.workspace.rootPath; root.closeDocument=root.workspace.documentAt(index)
            closeDialog.open()
        }
    }
    WorkbenchConfirmDialog {
        id: closeDialog
        title: Design.UiText.get("关闭前保存修改？")
        contextText: root.closeDocument ? root.closeDocument.path : ""
        detail: root.closeDocument ? root.closeDocument.error : ""
        detailIsError: !!detail
        primaryText: "保存并关闭"
        secondaryText: "放弃修改"
        secondaryDestructive: true
        onPrimaryRequested: root.finishClose(true)
        onSecondaryRequested: root.finishClose(false)
    }
}
