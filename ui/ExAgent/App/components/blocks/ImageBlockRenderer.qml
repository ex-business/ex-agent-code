pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Design.BlockRendererContent {
    id: root
    readonly property var payloadData: root.block.data || ({})
    implicitHeight: image.status === Image.Ready ? Math.max(72, Math.min(root.detached ? 680 : 360, image.implicitHeight * Math.min(1, root.width / Math.max(1, image.implicitWidth)))) + 28 : 100
    Image {
        id: image
        width: parent.width
        height: Math.max(72, parent.height - 28)
        source: root.controller ? root.controller.mediaSource(root.block, root.payloadData.resourceId || "") : ""
        asynchronous: true
        cache: true
        sourceSize: Qt.size(2048, 2048)
        fillMode: Image.PreserveAspectFit
        Accessible.name: root.payloadData.label || "图片"
    }
    Text { visible: image.status !== Image.Ready; anchors.centerIn: image; text: image.status === Image.Error ? "图片无法读取" : (root.payloadData.resourceId ? "图片加载中…" : root.block.text || "等待图片"); color: Design.Theme.secondary }
    Text { anchors.bottom: parent.bottom; text: root.payloadData.label || ""; color: Design.Theme.secondary; font.pixelSize: 12 }
}
