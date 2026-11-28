pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

ColumnLayout {
    id: root
    required property var imageInfo
    required property ChatController controller
    property bool allowDetach: true
    signal detachRequested()
    WorkspaceImageMonitor { id: monitor; objectName: "imageMonitor"; path: root.imageInfo.path || ""; rootPath: root.imageInfo.root || "" }
    readonly property var currentImage: monitor.info
    spacing: 0
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 8; spacing: 4
        Text { text: (root.currentImage.width||0)+" × "+(root.currentImage.height||0); color: Design.Theme.secondary; font.pixelSize: 12; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
        Design.IconButton { iconName: "minus"; tooltip: Design.UiText.get("缩小"); onClicked: canvas.magnify(1/1.25) }
        Design.IconButton { iconName: "plus"; tooltip: Design.UiText.get("放大"); onClicked: canvas.magnify(1.25) }
        DialogAction { text: Design.UiText.get("适应"); onClicked: canvas.fit() }
        DialogAction { text: "100%"; onClicked: canvas.original() }
        Design.IconButton { visible: root.allowDetach; iconName: "expand"; tooltip: Design.UiText.get("独立窗口"); onClicked: root.detachRequested() }
    }
    Text { visible: !!monitor.error; Layout.fillWidth: true; Layout.margins: 8; text: Design.UiText.get(monitor.error); color: Design.Theme.warning; wrapMode: Text.Wrap }
    Design.MediaSession { id: session; block: ({}); controller: root.controller; attachment: root.currentImage; interactive: true }
    Design.MediaCanvas {
        id: canvas
        objectName: "workspaceImageCanvas"
        Layout.fillWidth: true; Layout.fillHeight: true
        session: session; interactive: true
        Connections { target: monitor; function onChanged() { canvas.fit() } }
    }
    Text { Layout.fillWidth: true; Layout.margins: 8; text: Math.round(canvas.actualScale*100)+"%  ·  "+Design.UiText.get("滚轮缩放 · 拖动平移 · 双击切换比例"); wrapMode: Text.Wrap; color: Design.Theme.muted; font.pixelSize: 11 }
}
