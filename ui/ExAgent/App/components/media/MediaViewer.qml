pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import QtCore as Core
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

FocusScope {
    id: viewer
    required property var block
    required property ChatController controller
    property bool overlayMode: false
    property var attachment: ({})
    signal closeRequested()
    function closeViewer() {
        if (attachment.url || !block.blockId) closeRequested()
        else controller.blockViews.reattach(block.blockId)
    }
    focus: true
    Design.MediaSession { id: playback; block: viewer.block; controller: viewer.controller; attachment: viewer.attachment; interactive: true }
    FileDialog {
        id: saveDialog
        property string resourceId: ""
        property url attachmentUrl
        title: "保存原始图片"; fileMode: FileDialog.SaveFile
        onAccepted: viewer.controller.saveMedia(viewer.block, resourceId, attachmentUrl, selectedFile)
    }
    Rectangle { anchors.fill: parent; color: viewer.overlayMode ? "transparent" : "#181818" }
    Design.MediaCanvas {
        id: canvas
        anchors.fill: parent; anchors.topMargin: 76
        anchors.bottomMargin: playback.sequence ? sequencePanel.height + 68 : 70
        session: playback; interactive: true; surfaceColor: "transparent"
    }
    Rectangle {
        id: sequencePanel
        visible: playback.sequence
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: sequenceContent.implicitHeight + 20; color: "#242424"
        ColumnLayout {
            id: sequenceContent
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10; spacing: 6
            Design.MediaControls { Layout.fillWidth: true; session: playback; inverted: true }
            Design.MediaThumbnails { Layout.fillWidth: true; session: playback }
        }
    }
    Design.MediaViewerChrome {
        anchors.fill: parent; anchors.bottomMargin: playback.sequence ? sequencePanel.height : 0
        title: playback.frame.label || viewer.block.title || "图片"
        subtitle: playback.frames.length ? (playback.frame.width || "—") + " × " + (playback.frame.height || "—") + (playback.sequence ? "  ·  " + (playback.frameIndex + 1) + " / " + playback.frames.length : "") : "正在准备资源"
        zoomScale: canvas.actualScale; canSave: canvas.ready
        onCloseRequested: viewer.closeViewer()
        onZoomRequested: (factor) => canvas.magnify(factor)
        onFitRequested: canvas.fit()
        onOriginalRequested: canvas.original()
        onSaveRequested: {
            saveDialog.resourceId = playback.frame.resourceId || ""
            saveDialog.attachmentUrl = viewer.attachment.url || ""
            saveDialog.currentFile = String(Core.StandardPaths.writableLocation(Core.StandardPaths.PicturesLocation)) + "/" + (playback.frame.label || "image.png").split(/[\\/]/).pop()
            saveDialog.open()
        }
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 70
        width: toast.implicitWidth + 28; height: 34; radius: 17; color: "#ee3b3b3b"; visible: savedTimer.running
        Text { id: toast; anchors.centerIn: parent; text: "原始图片已保存"; color: "white"; font.pixelSize: 12 }
    }
    Timer { id: savedTimer; interval: 2200 }
    Connections { target: viewer.controller; function onMediaSaved(path: string) { if (viewer.visible) savedTimer.restart() } }
    Shortcut { sequence: "Escape"; enabled: viewer.visible && viewer.Window.window && viewer.Window.window.active; onActivated: viewer.closeViewer() }
    Shortcut { sequence: "Left"; enabled: viewer.visible && viewer.Window.window && viewer.Window.window.active; onActivated: playback.seek(playback.frameIndex - 1) }
    Shortcut { sequence: "Right"; enabled: viewer.visible && viewer.Window.window && viewer.Window.window.active; onActivated: playback.seek(playback.frameIndex + 1) }
    Shortcut { sequence: "Space"; enabled: viewer.visible && viewer.Window.window && viewer.Window.window.active && playback.sequence; onActivated: playback.toggle() }
}
