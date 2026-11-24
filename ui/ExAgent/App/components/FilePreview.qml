pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    property bool renderedMarkdown: true
    property var currentPlugin: viewLoader.item
    function togglePreview() { if (currentPlugin && currentPlugin.togglePreview) currentPlugin.togglePreview() }
    property int closeIndex: -1
    property string closeRoot: ""
    property EditorDocument closeDocument
    function finishClose(save) {
        if (root.workspace.rootPath!==closeRoot || root.workspace.documentAt(closeIndex)!==closeDocument) { closeDialog.close(); return }
        if (save && !closeDocument.save()) return
        root.workspace.closeTab(closeIndex,true)
        if (root.workspace.documentAt(closeIndex)!==closeDocument) closeDialog.close()
    }
    Loader {
        id: viewLoader
        anchors.fill: parent
        active: root.visible && !!root.workspace.viewer.component
        property string entry: !root.renderedMarkdown && root.workspace.viewer.storage===0 ? "WorkspaceSourcePlugin.qml" : root.workspace.viewer.component || ""
        function loadPlugin() {
            if (!entry) { source=""; return }
            setSource(Qt.resolvedUrl(entry), {workspace:root.workspace, controller:root.controller})
        }
        onEntryChanged: loadPlugin()
        Component.onCompleted: loadPlugin()
    }
    Connections {
        target: viewLoader.item; ignoreUnknownSignals: true
        function onOpenWindow(component: string, properties: var, resource: url, title: string) {
            detached.resource=resource
            detached.title=title || Design.UiText.get("独立查看器")
            properties.controller=root.controller
            detachedLoader.setSource(Qt.resolvedUrl(component),properties)
            detached.show(); detached.raise(); detached.requestActivate()
        }
    }
    Design.WorkbenchWindow {
        id: detached
        property url resource
        transientParent: root.Window.window
        width: 960; height: 680; minimumWidth: 320; minimumHeight: 280
        title: Design.UiText.get("独立查看器")
        Shortcut { sequence: "Escape"; enabled: detached.visible && detached.active; onActivated: detached.close() }
        onVisibleChanged: { if(!visible) entrance.entered=false }
        Design.WindowEntrance {
            id: entrance; anchors.fill: parent; ready: detached.visible
            Loader { id: detachedLoader; anchors.fill: parent; active: detached.visible }
        }
    }
    Connections { target: root.Window.window; function onVisibleChanged() { if(root.Window.window && !root.Window.window.visible) detached.close() } }
    Connections {
        target: root.workspace
        function onPreviewChanged() { if(detached.visible && !root.workspace.viewerResourceAvailable(detached.resource)) detached.close() }
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
