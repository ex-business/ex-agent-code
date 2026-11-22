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
    signal linkActivated(string link)
    spacing: 14
    Repeater {
        model: root.blocks
        delegate: Design.BlockRenderer {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            block: modelData
            controller: root.controller
            messageId: root.messageId
            blockIndex: index
            outerWheel: root.outerWheel
            onLinkActivated: (link) => root.linkActivated(link)
        }
    }
}
