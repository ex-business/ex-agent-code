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
        delegate: Loader {
            id: blockLoader
            required property var modelData
            required property int index
            Layout.fillWidth: true
            Layout.preferredHeight: (item as Item)?.implicitHeight ?? 24
            asynchronous: true
            sourceComponent: modelData.kind === 1 ? code : prose
            Component {
                id: prose
                Design.MarkdownText { block: blockLoader.modelData; messageId: root.messageId; blockIndex: blockLoader.index; selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null; selectionView: root.outerWheel ? root.outerWheel.view : null; onLinkActivated: (link) => root.linkActivated(link) }
            }
            Component {
                id: code
                Design.CodeBlock { code: blockLoader.modelData.text; messageId: root.messageId; blockIndex: blockLoader.index; language: blockLoader.modelData.language; controller: root.controller; outerWheel: root.outerWheel }
            }
        }
    }
}
