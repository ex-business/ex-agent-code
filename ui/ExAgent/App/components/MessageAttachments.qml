pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.App as Design

// 只实例化可见附件；横纵比影响卡片宽度，长截图保留可辨识的预览尺寸。
ListView {
    id: root
    required property var attachments
    // 工具日志使用小方图，普通聊天附件保留按原图比例计算的宽度。
    property bool compact: false
    readonly property int tileHeight: compact ? 80 : 112
    signal imageRequested(var image)
    implicitHeight: attachments.length > 0 ? tileHeight + 4 : 0
    orientation: ListView.Horizontal
    spacing: 8; clip: true; reuseItems: true; cacheBuffer: 240
    boundsBehavior: Flickable.StopAtBounds
    model: attachments
    delegate: Design.ImageTile {
        required property var modelData
        width: root.compact ? root.tileHeight : Math.max(88, Math.min(210, root.tileHeight * Number(modelData.width || 1) / Math.max(1, Number(modelData.height || 1))))
        height: root.tileHeight
        source: modelData.url; label: modelData.name || "图片"
        onActivated: root.imageRequested(modelData)
    }
    ScrollBar.horizontal: ScrollBar { height: 3; policy: ScrollBar.AsNeeded }
}
