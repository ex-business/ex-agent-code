pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

Item {
    id: root
    required property ChatController controller
    required property int rowIndex
    required property int messageKind
    required property string content
    required property string messageTitle
    required property int messageStatus
    required property bool expanded
    required property var contentBlocks
    required property var changedFiles
    required property var activity
    required property var attachments
    signal fileRequested(string path)
    signal imageRequested(var image)
    function openLink(link) {
        if (link.startsWith("file:")) fileRequested(link)
        else if (link.startsWith("https://") || link.startsWith("http://")) Qt.openUrlExternally(link)
    }
    property var outerWheel: null
    readonly property bool isUser: messageKind === MessageListModel.User
    readonly property bool isTool: messageKind === MessageListModel.Tool
    readonly property bool isNotice: messageKind === MessageListModel.Notice
    implicitHeight: body.implicitHeight + (isTool ? (messageTitle === "文件修改" ? 40 : 8) : isUser ? 42 : isNotice ? 12 : 26)

    // 每条消息只实例化自己的类型，不能用 visible=false 留下其他类型的全文布局。
    Loader {
        id: body
        width: root.width
        sourceComponent: root.isUser ? userBody : root.isNotice ? noticeBody
                         : root.isTool ? (root.messageTitle === "文件修改" ? filesBody : toolBody) : assistantBody
    }
    Component {
        id: userBody
        ColumnLayout {
            spacing: 8
            Design.UserMessageBubble { Layout.fillWidth: true; text: root.content; onLinkActivated: (link) => root.openLink(link) }
            Design.MessageAttachments { visible: root.attachments.length > 0; Layout.fillWidth: true; attachments: root.attachments; onImageRequested: (image) => root.imageRequested(image) }
        }
    }

    Component {
        id: noticeBody
        Design.MessageContent { blocks: root.contentBlocks; controller: root.controller; outerWheel: root.outerWheel; onLinkActivated: (link) => root.openLink(link) }
    }

    Component {
        id: filesBody
        Design.ChangedFilesCard { files: root.changedFiles; onFileRequested: (path) => root.fileRequested(path) }
    }
    Component {
        id: toolBody
        Design.ToolActivityRow {
            activity: root.activity
            attachments: root.attachments
            title: root.messageTitle
            details: root.content
            status: root.messageStatus
            expanded: root.expanded
            controller: root.controller
            outerWheel: root.outerWheel
            onToggleRequested: root.controller.messages.toggleExpanded(root.rowIndex)
            onImageRequested: (image) => root.imageRequested(image)
        }
    }
    Component {
        id: assistantBody
        ColumnLayout {
            spacing: 14
            Design.MessageContent { Layout.fillWidth: true; blocks: root.contentBlocks; controller: root.controller; outerWheel: root.outerWheel; onLinkActivated: (link) => root.openLink(link) }
            Design.MessageAttachments { visible: root.attachments.length > 0; Layout.fillWidth: true; attachments: root.attachments; onImageRequested: (image) => root.imageRequested(image) }
            Design.MessageActions {
                text: root.content
                onCopyRequested: (text) => root.controller.copyText(text)
            }
        }
    }
}
