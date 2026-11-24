pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import ExAgent.Chat
import ExAgent.App as Design
ColumnLayout {
    id: root
    required property WorkspaceController workspace
    required property ChatController controller
    readonly property bool diagram: workspace.viewer.id==="mermaid"
    readonly property string documentIdentity: workspace.rootPath+"\n"+workspace.activePath
    onDocumentIdentityChanged: { safe.clear(); prepare.restart() }
    property bool sourceMode: false
    property bool dark: Design.Theme.dark
    onDarkChanged: prepare.restart()
    function togglePreview() { sourceMode=!sourceMode }
    spacing: 0
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 10; spacing: 6
        Label { text: root.diagram ? "Mermaid" : "HTML"; color: Design.Theme.text; font.bold: true }
        Item { Layout.fillWidth: true }
        Design.DialogAction { text: Design.UiText.get(root.sourceMode ? "预览" : "源码"); onClicked: root.togglePreview() }
        Design.DialogAction { implicitWidth: 32; leftPadding: 6; rightPadding: 6; text: "−"; enabled: web.zoomFactor>0.3; onClicked: web.zoomFactor=Math.max(0.25,web.zoomFactor-0.1); Accessible.name: Design.UiText.get("缩小") }
        Design.DialogAction { text: Math.round(web.zoomFactor*100)+"%"; onClicked: web.zoomFactor=1 }
        Design.DialogAction { implicitWidth: 32; leftPadding: 6; rightPadding: 6; text: "+"; enabled: web.zoomFactor<3; onClicked: web.zoomFactor=Math.min(3,web.zoomFactor+0.1); Accessible.name: Design.UiText.get("放大") }
    }
    Label { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.bottomMargin: 8; text: Design.UiText.get(root.diagram ? "本地渲染 · 禁用网络与交互脚本" : "静态预览 · 禁用脚本、表单提交与网络"); color: Design.Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap }
    RowLayout {
        visible: !root.sourceMode; Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10; Layout.bottomMargin: 8
        Design.DialogInput { id: search; placeholderText: Design.UiText.get("查找内容"); Layout.fillWidth: true; onAccepted: web.findText(text) }
        Design.DialogAction { text: Design.UiText.get("查找"); onClicked: web.findText(search.text) }
        Design.DialogAction { text: Design.UiText.get("复制"); enabled: web.action(WebEngineView.Copy).enabled; onClicked: web.triggerWebAction(WebEngineView.Copy) }
    }
    SafeWebProfile { id: safe }
    Timer { id: prepare; interval: 160; onTriggered: safe.configure(root.workspace.rootPath,root.workspace.rootPath+"/"+root.workspace.activePath,root.workspace.source,root.diagram,root.dark) }
    Connections { target: root.workspace; function onContentChanged() { prepare.restart() } function onPreviewChanged() { prepare.restart() } }
    Component.onCompleted: prepare.start()
    StackLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: root.sourceMode ? 1 : 0
        WebEngineView {
            id: web; profile: safe; url: safe.navigationUrl
            backgroundColor: Design.Theme.background
            settings.javascriptEnabled: root.diagram
            settings.dnsPrefetchEnabled: false
            settings.hyperlinkAuditingEnabled: false
            settings.localContentCanAccessRemoteUrls: false
            settings.localContentCanAccessFileUrls: false
            settings.pluginsEnabled: false
            settings.pdfViewerEnabled: false
            settings.fullScreenSupportEnabled: false
            settings.screenCaptureEnabled: false
            onNavigationRequested: request => { if(!safe.permits(request.url) && request.url.toString()!=="about:blank") request.reject() }
            onNewWindowRequested: request => {}
            onFileDialogRequested: request => { request.accepted=true; request.dialogReject() }
            onJavaScriptDialogRequested: request => { request.accepted=true; request.dialogReject() }
            onContextMenuRequested: request => request.accepted=true
            onPermissionRequested: permission => permission.deny()
            Rectangle { anchors.fill: parent; color: Design.Theme.background; visible: safe.navigationUrl.toString()==="about:blank" }
        }
        Design.WorkspaceSourcePlugin { workspace: root.workspace; controller: root.controller }
    }
}
