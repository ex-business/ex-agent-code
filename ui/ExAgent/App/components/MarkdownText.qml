pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 正文选择由 SelectableText 统一处理；表格分隔线是轻量装饰，不截获鼠标。
Item {
    id: root
    required property var block
    signal linkActivated(string link)
    implicitHeight: prose.implicitHeight
    Design.SelectableText {
        id: prose
        width: parent.width
        text: root.block.html || ""
        textFormat: TextEdit.RichText
        Accessible.name: "助手消息"
        onLinkActivated: (link) => root.linkActivated(link)
    }
    Repeater {
        model: root.block.tableRules || []
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
