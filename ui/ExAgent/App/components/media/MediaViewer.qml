pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

FocusScope {
    id: viewer
    required property var block
    required property ChatController controller
    property var attachment: ({})
    signal closeRequested()
    function closeViewer() {
        if (attachment.url || !block.blockId) closeRequested()
        else controller.blockViews.reattach(block.blockId)
    }
    focus: true
    Design.MediaSession { id: playback; block: viewer.block; controller: viewer.controller; attachment: viewer.attachment; interactive: true }
    Rectangle { anchors.fill: parent; color: Design.Theme.background }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 8
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            ColumnLayout {
                Layout.fillWidth: true; spacing: 3
                Text { Layout.fillWidth: true; text: playback.frame.label || viewer.block.title || "图片查看器"; elide: Text.ElideMiddle; font.pixelSize: 13; font.weight: Font.Medium; color: Design.Theme.text }
                Text { text: playback.frames.length ? (playback.frame.width || "—") + " × " + (playback.frame.height || "—") + "  ·  " + (playback.frameIndex + 1) + " / " + playback.frames.length : "正在准备资源"; color: Design.Theme.secondary; font.pixelSize: 11 }
            }
            Design.MediaButton { text: "−"; Accessible.name: "缩小图片"; onClicked: canvas.magnify(1 / 1.25) }
            Text { text: Math.round(canvas.actualScale * 100) + "%"; color: Design.Theme.secondary; font.pixelSize: 11; Layout.minimumWidth: 38; horizontalAlignment: Text.AlignHCenter }
            Design.MediaButton { text: "+"; Accessible.name: "放大图片"; onClicked: canvas.magnify(1.25) }
            Design.MediaButton { text: "适应窗口"; onClicked: canvas.fit() }
            Design.MediaButton { text: "原始尺寸"; onClicked: canvas.original() }
            Design.MediaButton { text: viewer.attachment.url ? "关闭预览" : "返回聊天"; onClicked: viewer.closeViewer() }
        }
        Design.MediaCanvas { id: canvas; Layout.fillWidth: true; Layout.fillHeight: true; session: playback; interactive: true }
        Text { Layout.alignment: Qt.AlignHCenter; text: "滚轮缩放 · 拖动平移 · 双击切换原图 · Esc 返回"; color: Design.Theme.muted; font.pixelSize: 11 }
        Design.MediaControls { visible: playback.sequence; Layout.fillWidth: true; session: playback }
        Design.MediaThumbnails { visible: playback.sequence; Layout.fillWidth: true; session: playback }
    }
    // 查看器快捷键按当前窗口限定，不依赖Loader或按钮恰好持有键盘焦点。
    Shortcut { sequence: "Escape"; enabled: viewer.visible && viewer.Window.window && viewer.Window.window.active; onActivated: viewer.closeViewer() }
    Shortcut { sequence: "Left"; enabled: viewer.visible && viewer.Window.window && viewer.Window.window.active; onActivated: playback.seek(playback.frameIndex - 1) }
    Shortcut { sequence: "Right"; enabled: viewer.visible && viewer.Window.window && viewer.Window.window.active; onActivated: playback.seek(playback.frameIndex + 1) }
    Shortcut { sequence: "Space"; enabled: viewer.visible && viewer.Window.window && viewer.Window.window.active && playback.sequence; onActivated: playback.toggle() }
}
