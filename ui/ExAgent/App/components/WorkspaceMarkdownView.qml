pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

// 源码和预览共享同一文档，不经保存或复制文件来实现联动。
ColumnLayout {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    function togglePreview() { viewMode=viewMode===2 ? 1 : 2 }
    property int viewMode: 3 // 1 源码、2 预览、3 联动。
    property bool syncing: false
    function synchronize(target, line, fraction) {
        if(viewMode!==3 || syncing || !target)return
        syncing=true;target.scrollToSourceLine(line,fraction)
        Qt.callLater(()=>Qt.callLater(()=>{root.syncing=false}))
    }
    spacing: 0
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 8; spacing: 6
        DialogAction { text: Design.UiText.get("源码"); primary: root.viewMode===1; onClicked: root.viewMode=1 }
        DialogAction { text: Design.UiText.get("预览"); primary: root.viewMode===2; onClicked: root.viewMode=2 }
        DialogAction { text: Design.UiText.get("联动"); primary: root.viewMode===3; onClicked: root.viewMode=3 }
        Item { Layout.fillWidth: true }
        DialogAction { visible: !!root.workspace.document && !root.workspace.document.editable && root.workspace.document.state===EditorDocument.Conflict; text: Design.UiText.get("重新载入"); onClicked: root.workspace.document.reload() }
    }
    SplitView {
        Layout.fillWidth: true; Layout.fillHeight: true
        orientation: root.width>=700 ? Qt.Horizontal : Qt.Vertical
        handle: Rectangle { implicitWidth: 5; implicitHeight: 5; color: SplitHandle.hovered || SplitHandle.pressed ? Design.Theme.border : Design.Theme.surface }
        Loader {
            id: sourceLoader
            visible: root.viewMode!==2
            SplitView.preferredWidth: root.width/2; SplitView.preferredHeight: root.height/2
            SplitView.minimumWidth: 160; SplitView.minimumHeight: 100
            sourceComponent: root.workspace.document ? editable : readOnly
            Component { id: editable; Design.WorkspaceEditor { objectName: "workspaceMarkdownSource"; document: root.workspace.document; showPreviewAction: false; onSourceViewportChanged: (line,fraction)=>root.synchronize(markdownPreview,line,fraction) } }
            Component { id: readOnly; Design.WorkspaceTextPreview { workspace: root.workspace; controller: root.controller; markdown: false } }
        }
        Design.WorkspaceTextPreview {
            id: markdownPreview
            onSourceViewportChanged: (line,fraction)=>root.synchronize(sourceLoader.item,line,fraction)
            // 显式锚点导航不能被上一轮滚动联动的防重入保护吞掉。
            onAnchorLocated: line => { root.syncing=true; if(sourceLoader.item)sourceLoader.item.scrollToSourceLine(line,0);Qt.callLater(()=>Qt.callLater(()=>{root.syncing=false})) }
            onLayoutReady: { if(sourceLoader.item && sourceLoader.item.sourceLocation){const at=sourceLoader.item.sourceLocation();root.synchronize(markdownPreview,at.line,at.fraction)} }
            visible: root.viewMode!==1
            workspace: root.workspace; controller: root.controller; markdown: true
            SplitView.fillWidth: true; SplitView.fillHeight: true
            SplitView.minimumWidth: 160; SplitView.minimumHeight: 100
        }
    }
}
