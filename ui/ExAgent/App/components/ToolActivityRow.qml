pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

// 工具摘要、图片记录与详情共用布局；类型和文案来自会话资源，不伪装为终端输出。
ColumnLayout {
    id: root
    required property var activity
    required property var attachments
    required property string title
    required property string details
    required property int status
    required property bool expanded
    required property ChatController controller
    property var outerWheel: null
    property string messageId: ""
    signal toggleRequested()
    signal imageRequested(var image)
    readonly property int kind: Number(activity.kind || 0)
    readonly property bool hasDetails: details.length > 0 || attachments.length > 0
    readonly property string summary: activity.summary || (kind === 3 ? "已查看 " + attachments.length + " 张图像" : title.startsWith("运行命令") && status === 0 ? "已" + title : title)
    spacing: 6
    Button {
        id: toggle
        Layout.maximumWidth: root.width
        Layout.fillWidth: false
        Layout.alignment: Qt.AlignLeft
        implicitWidth: label.implicitWidth + (root.hasDetails ? 46 : 26)
        implicitHeight: 25
        padding: 0
        hoverEnabled: true
        Accessible.name: root.summary + (root.hasDetails ? root.expanded ? "，收起" : "，展开" : "")
        onClicked: if (root.hasDetails) root.toggleRequested()
        HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: root.hasDetails }
        background: Item {}
        contentItem: RowLayout {
            spacing: 7
            Design.Icon { name: root.kind === 3 ? "image" : root.kind === 4 || root.title.includes("搜索") ? "search" : "activity"; implicitWidth: 15; implicitHeight: 15; color: Design.Theme.muted }
            Design.SelectableText { id: label; text: root.summary; textFormat: TextEdit.PlainText; wrapMode: TextEdit.NoWrap; messageId: root.messageId; blockIndex: 0; selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null; Layout.fillWidth: true; color: Design.Theme.muted; font.pixelSize: 13; onTapped: if (root.hasDetails) root.toggleRequested() }
            Design.Icon { visible: root.hasDetails; name: root.expanded ? "down" : "right"; implicitWidth: 12; implicitHeight: 12; color: Design.Theme.muted }
        }
    }
    Design.MessageAttachments { visible: root.expanded && root.attachments.length > 0; Layout.fillWidth: true; attachments: root.attachments; onImageRequested: (image) => root.imageRequested(image) }
    Loader {
        active: root.expanded && root.details.length > 0
        Layout.fillWidth: true
        sourceComponent: Design.CodeBlock { code: root.details; messageId: root.messageId; blockIndex: 1; language: "text"; controller: root.controller; outerWheel: root.outerWheel }
    }
}
