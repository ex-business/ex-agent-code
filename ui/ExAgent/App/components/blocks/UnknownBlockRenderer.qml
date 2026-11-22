pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Design.BlockRendererContent {
    id: root
    implicitHeight: warning.implicitHeight + body.implicitHeight + 16
    Text {
        id: warning
        width: parent.width
        text: "此内容类型暂不支持：" + (root.block.rendererKind || "未知")
        wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Design.Theme.secondary
    }
    Design.SelectableText {
        id: body
        y: warning.implicitHeight + 8
        width: parent.width
        text: root.block.text || ""
        textFormat: TextEdit.PlainText
        messageId: root.messageId; blockIndex: root.blockIndex
        selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null
        selectionView: root.outerWheel ? root.outerWheel.view : null
    }
}
