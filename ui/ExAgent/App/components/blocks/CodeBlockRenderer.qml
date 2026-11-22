pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Design.BlockRendererContent {
    id: root
    implicitHeight: code.implicitHeight
    Design.CodeBlock {
        id: code
        width: parent.width
        code: root.block.text || ""
        language: root.block.language || (root.block.data || {}).language || (root.block.rendererKind === 7 ? "diff" : "text")
        controller: root.controller
        messageId: root.messageId
        blockIndex: root.blockIndex
        outerWheel: root.outerWheel
        maximumCodeHeight: root.detached ? 640 : 330
    }
}
