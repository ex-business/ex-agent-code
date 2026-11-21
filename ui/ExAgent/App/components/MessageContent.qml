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
    property var outerWheel: null
    signal linkActivated(string link)
    spacing: 14
    Repeater {
        model: root.blocks
        delegate: Loader {
            id: blockLoader
            required property var modelData
            Layout.fillWidth: true
            sourceComponent: modelData.kind === 1 ? code : prose
            Component {
                id: prose
                Design.MarkdownText { block: blockLoader.modelData; onLinkActivated: (link) => root.linkActivated(link) }
            }
            Component {
                id: code
                Design.CodeBlock { code: blockLoader.modelData.text; language: blockLoader.modelData.language; controller: root.controller; outerWheel: root.outerWheel }
            }
        }
    }
}
