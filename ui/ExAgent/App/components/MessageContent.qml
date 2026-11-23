pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

// 正文与过程说明共享 Markdown/代码渲染；操作栏由消息外层决定是否显示。
ColumnLayout {
    id: root
    required property var blocks
    required property ChatController controller
    required property string messageId
    property var outerWheel: null
    property bool pooled: false
    signal linkActivated(string link)
    property int slotRevision: 0
    readonly property bool layoutReady: {
        const revision = root.slotRevision
        for (let i = 0; i < slots.count; ++i) {
            const item = slots.itemAt(i) as Design.BlockRenderer
            if (!item || !item.layoutReady) return false
        }
        return slots.count === root.blocks.length
    }
    // 只在排版即将发布时查找当前消息的可见块；使用槽位，异步正文尚未创建也能定位。
    function readingBlock(view: Item): Item {
        for (let index = 0; index < slots.count; ++index) {
            const slot = slots.itemAt(index) as Design.BlockRenderer
            if (!slot || slot.pooled || !slot.visible || slot.height <= 0) continue
            const top = slot.mapToItem(view, 0, 0).y
            if (top >= view.height) break
            if (top + slot.height > 0) return slot
        }
        return null
    }
    // 只在显式覆盖率采样时遍历槽位，只统计与视口相交的展开正文。
    function proseCoverage(view: Item): vector3d {
        let expected = 0, loaded = 0, ready = 0
        for (let i = 0; i < slots.count; ++i) {
            const slot = slots.itemAt(i) as Design.BlockRenderer
            if (!slot || !slot.visible || slot.pooled || !slot.showContent || slot.height <= 0
                    || slot.rendererKind !== 1 || Number(slot.block.contentMode) === 2 || !slot.block.text) continue
            const y = slot.mapToItem(view, 0, 0).y
            if (y >= view.height || y + slot.height <= 0) continue
            ++expected
            if (slot.hasCurrentRenderer()) { ++loaded; if (slot.layoutReady) ++ready }
        }
        return Qt.vector3d(expected, loaded, ready)
    }
    spacing: 14
    Repeater {
        id: slots
        // Repeater的count可能先于item创建完成，显式跟踪槽位就绪，避免缓存永久停在占位高度。
        onItemAdded: ++root.slotRevision
        onItemRemoved: ++root.slotRevision
        // 按槽位数量复用组件；同样一个正文块换消息时只更新绑定，不销毁再新建整棵控件树。
        model: root.blocks.length
        delegate: Design.BlockRenderer {
            readonly property var modelData: root.blocks[index] || ({})
            required property int index
            Layout.fillWidth: true
            pooled: root.pooled
            block: modelData
            controller: root.controller
            messageId: root.messageId
            blockIndex: index
            outerWheel: root.outerWheel
            onLinkActivated: (link) => root.linkActivated(link)
        }
    }
}
