pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

// 只实例化可见附件；横纵比影响卡片宽度，长截图保留可辨识的预览尺寸。
ListView {
    id: root
    required property var attachments
    signal imageRequested(var image)
    implicitHeight: attachments.length > 0 ? 116 : 0
    orientation: ListView.Horizontal
    spacing: 8; clip: true; reuseItems: true; cacheBuffer: 240
    boundsBehavior: Flickable.StopAtBounds
    model: attachments
    delegate: Design.ImageTile {
        required property var modelData
        width: Math.max(88, Math.min(210, 112 * Number(modelData.width || 1) / Math.max(1, Number(modelData.height || 1))))
        height: 112
        source: modelData.url; label: modelData.name || "图片"
        onActivated: root.imageRequested(modelData)
    }
    ScrollBar.horizontal: ScrollBar { height: 3; policy: ScrollBar.AsNeeded }
}
