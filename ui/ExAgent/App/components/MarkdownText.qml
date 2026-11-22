pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import ExAgent.Chat

// 正文选择由 SelectableText 统一处理；表格分隔线是轻量装饰，不截获鼠标。
Item {
    id: root
    required property var block
    property string messageId: ""
    property int blockIndex: 0
    property var selectionManager: null
    property Flickable selectionView: null
    signal linkActivated(string link)
    implicitHeight: preparation.state === RichTextPreparation.Ready ? prose.implicitHeight : 24
    RichTextPreparation {
        id: preparation
        source: root.block.text || ""
        dark: Design.Theme.dark
        onLayoutAboutToChange: if (root.selectionManager) root.selectionManager.layoutChangeStarting()
        onLayoutChanged: if (root.selectionManager) root.selectionManager.layoutChangeFinished()
    }
    Text {
        visible: preparation.state !== RichTextPreparation.Ready
        text: preparation.state === RichTextPreparation.Failed ? "内容准备失败，原文仍保留" : "正在准备内容…"
        color: Design.Theme.muted
        font.pixelSize: 12
    }
    Design.SelectableText {
        id: prose
        visible: preparation.state === RichTextPreparation.Ready
        width: parent.width
        messageId: root.messageId
        blockIndex: root.blockIndex
        selectionManager: root.selectionManager
        selectionView: root.selectionView
        text: preparation.value.html || ""
        textFormat: TextEdit.RichText
        Accessible.name: "助手消息"
        onLinkActivated: (link) => root.linkActivated(link)
    }
    Repeater {
        model: preparation.value.tableRules || []
        Rectangle {
            required property var modelData
            readonly property rect endRect: prose.positionToRectangle(Math.min(prose.length, modelData.end))
            readonly property rect nextRect: prose.positionToRectangle(Math.min(prose.length, modelData.next))
            y: Math.max(endRect.y + endRect.height + 3, (endRect.y + endRect.height + nextRect.y) / 2)
            width: root.width
            height: 1
            color: Design.Theme.border
        }
    }
}
