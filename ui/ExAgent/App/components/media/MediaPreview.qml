pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design

Item {
    id: preview
    required property Design.MediaSession session
    implicitHeight: 226
    implicitWidth: 380
    Rectangle {
        id: card
        width: Math.min(parent.width, 380); height: parent.height
        radius: 12; color: Design.Theme.background; border.color: Design.Theme.border
        Rectangle {
            id: stage
            x: 1; y: 1; width: card.width - 2; height: 172
            radius: 11; color: Design.Theme.dark ? "#141414" : "#f4f4f5"
            Image {
                id: thumbnail
                anchors.centerIn: parent
                readonly property real scaleToFit: Math.min(1, (stage.width - 24) / Math.max(1, Number(preview.session.frame.width) || implicitWidth), (stage.height - 20) / Math.max(1, Number(preview.session.frame.height) || implicitHeight))
                width: (Number(preview.session.frame.width) || implicitWidth) * scaleToFit
                height: (Number(preview.session.frame.height) || implicitHeight) * scaleToFit
                source: preview.session.sourceForFrame(preview.session.frameIndex)
                sourceSize: Qt.size(960, 960); asynchronous: true; cache: false; fillMode: Image.PreserveAspectFit
            }
            Text { anchors.centerIn: parent; visible: thumbnail.status !== Image.Ready; text: thumbnail.status === Image.Error ? "图片不可用 · 点击重试" : "正在准备预览…"; font.pixelSize: 12; color: Design.Theme.secondary }
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; y: 172; height: 1; color: Design.Theme.border }
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12; spacing: 10
            ColumnLayout {
                Layout.fillWidth: true; spacing: 3
                Text { Layout.fillWidth: true; text: preview.session.sequence ? "图片序列 · " + preview.session.frames.length + " 张" : preview.session.frame.label || "图片"; elide: Text.ElideMiddle; font.pixelSize: 12; color: Design.Theme.text }
                Text { text: preview.session.sequence ? "点击浏览和播放" : "点击查看原图"; color: Design.Theme.secondary; font.pixelSize: 11 }
            }
            Text { text: "查看图片  ↗"; color: Design.Theme.secondary; font.pixelSize: 12 }
        }
        MouseArea {
            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.Button
            Accessible.name: "查看图片"
            Accessible.onPressAction: preview.session.controller.blockViews.detach(preview.session.block)
            onClicked: preview.session.controller.blockViews.detach(preview.session.block)
        }
        Keys.onReturnPressed: preview.session.controller.blockViews.detach(preview.session.block)
        Keys.onSpacePressed: preview.session.controller.blockViews.detach(preview.session.block)
        activeFocusOnTab: true
    }
}
