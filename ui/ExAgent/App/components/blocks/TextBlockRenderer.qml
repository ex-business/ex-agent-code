pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Design.BlockRendererContent {
    id: root
    layoutReady: Boolean(content.item) && (root.block.contentMode === 2 || markdownItem && markdownItem.layoutReady)
    readonly property var markdownItem: content.item
    implicitHeight: (content.item as Item)?.implicitHeight ?? 24
    Loader {
        id: content
        width: parent.width
        // 工具记录的选区使用原文偏移，动态文本保持纯文本展示，不能悄悄吞掉Markdown字符。
        sourceComponent: root.block.contentMode === 2 ? plain : markdown
    }
    Component {
        id: plain
        Design.CodeBlock {
            width: root.width
            code: root.block.text || ""
            language: "text"
            controller: root.controller
            messageId: root.messageId; blockIndex: root.blockIndex; outerWheel: root.outerWheel
            maximumCodeHeight: root.detached ? 640 : 330
        }
    }
    Component {
        id: markdown
        Design.MarkdownText {
            pooled: root.pooled
            width: root.width
            block: root.block; messageId: root.messageId; blockIndex: root.blockIndex
            selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null
            selectionView: root.outerWheel ? root.outerWheel.view : null
            renderPolicy: root.outerWheel && root.outerWheel.view ? root.outerWheel.view.renderPolicy : null
            onLinkActivated: (link) => root.linkActivated(link)
        }
    }
}
