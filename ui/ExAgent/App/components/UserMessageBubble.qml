pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: root
    required property string text
    property Flickable selectionView: null
    signal linkActivated(string link)
    // 导入记录常带结束换行，只规范展示边缘，不改写资源原文或消息存储。
    readonly property string displayText: text.replace(/[\r\n]+$/, "")
    readonly property var presentation: MarkdownFormatter.userPresentation(displayText, metrics.font)
    readonly property real naturalTextWidth: presentation.width
    implicitHeight: bubble.height
    FontMetrics { id: metrics; font: messageText.font }
    Rectangle {
        id: bubble
        anchors.right: parent.right
        // 字形前进宽度保留取整余量，避免末字被挤成单独一行。
        width: Math.min(Math.max(42, Math.ceil(root.naturalTextWidth) + 36), root.width * 0.85)
        height: messageText.implicitHeight + 24
        radius: 18
        color: Design.Theme.dark ? "#eeeeee" : "#000000"
        Design.SelectableText {
            id: messageText
            x: 16; y: 12
            width: parent.width - 32
            selectionView: root.selectionView
            text: root.presentation.html
            textFormat: TextEdit.RichText
            wrapMode: TextEdit.Wrap
            color: Design.Theme.dark ? "#202020" : "#ffffff"
            selectedTextColor: Design.Theme.dark ? "#202020" : "#ffffff"
            font.family: Design.Theme.font
            font.pixelSize: 14
            Accessible.name: "用户消息"
            onLinkActivated: (link) => root.linkActivated(link)
        }
    }
}
