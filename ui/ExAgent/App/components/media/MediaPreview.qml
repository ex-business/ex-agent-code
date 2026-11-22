pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design

Item {
    id: preview
    required property Design.MediaSession session
    implicitWidth: 340
    implicitHeight: 226
    ColumnLayout {
        width: Math.min(parent.width, 340); height: parent.height
        spacing: 6
        Design.ImageTile {
            Layout.fillWidth: true; Layout.fillHeight: true
            source: preview.session.sourceForFrame(preview.session.frameIndex)
            label: preview.session.frame.label || "图片"
            count: preview.session.sequence ? preview.session.frames.length : 1
            onActivated: preview.session.controller.blockViews.detach(preview.session.block)
        }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: preview.session.sequence ? "图片序列 · " + preview.session.frames.length + " 张" : preview.session.frame.label || "图片"; elide: Text.ElideMiddle; color: Design.Theme.secondary; font.pixelSize: 12 }
            Text { text: "点击查看 ↗"; color: Design.Theme.muted; font.pixelSize: 11 }
        }
    }
}
