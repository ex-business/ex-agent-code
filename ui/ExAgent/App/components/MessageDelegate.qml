pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

Item {
    id: root
    required property ChatController controller
    required property string messageId
    required property int rowIndex
    required property int messageKind
    required property string content
    required property string messageTitle
    required property int messageStatus
    required property bool expanded
    required property var contentBlocks
    required property var changedFiles
    required property var activity
    required property var activityGroup
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
    // 旧版媒体任务的辅助首帧仍保留在会话里，界面合并进序列预览，不重复占用聊天区域。
    readonly property bool auxiliaryMedia: isTool && Number(activity.sourceKind) === 2 && Number(activity.blockKind) === 5 && messageTitle === "首帧预览" && Boolean(activity.taskId)
    readonly property bool grouped: Number(activityGroup.count || 0) > 1
    readonly property bool groupHead: grouped && Number(activityGroup.first) === rowIndex
    readonly property bool groupCollapsed: grouped && Number(activityGroup.expansion) === 2
    readonly property bool groupHidden: groupCollapsed && !groupHead
    readonly property bool groupTransition: grouped && outerWheel && outerWheel.view.disclosureGroupId === String(activityGroup.id || "") && !outerWheel.animating
    visible: !auxiliaryMedia && implicitHeight > 0
    clip: groupTransition
    property bool headAnimating: false
    readonly property bool disclosureAnimating: rowDisclosure.running || headAnimating
    // 每行只保留一层高度补间，防止父层不断追赶子层高度而残留空白。
    Design.DisclosureHeight on implicitHeight { id: rowDisclosure; allowed: root.groupTransition && !root.groupHead }
    implicitHeight: auxiliaryMedia || groupHidden ? 0 : body.implicitHeight + (isTool ? (messageTitle === "文件修改" ? 40 : 8) : isUser ? 42 : isNotice ? 12 : 26)

    // 每条消息只实例化自己的类型，不能用 visible=false 留下其他类型的全文布局。
    Loader {
        id: body
        active: !root.auxiliaryMedia && (!root.groupHidden || root.groupTransition)
        width: root.width
        sourceComponent: root.isUser ? userBody : root.isNotice ? noticeBody
                         : root.isTool ? (root.messageTitle === "文件修改" ? filesBody : toolBody) : assistantBody
    }
    Component {
        id: userBody
        ColumnLayout {
            spacing: 8
            Design.UserMessageBubble { Layout.fillWidth: true; text: root.content; messageId: root.messageId; selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null; selectionView: root.outerWheel ? root.outerWheel.view : null; onLinkActivated: (link) => root.openLink(link) }
            Design.MessageAttachments { visible: root.attachments.length > 0; Layout.fillWidth: true; attachments: root.attachments; onImageRequested: (image) => root.imageRequested(image) }
        }
    }

    Component {
        id: noticeBody
        Design.MessageContent { blocks: root.contentBlocks; messageId: root.messageId; controller: root.controller; outerWheel: root.outerWheel; onLinkActivated: (link) => root.openLink(link) }
    }

    Component {
        id: filesBody
        Design.ChangedFilesCard { files: root.changedFiles; messageId: root.messageId; selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null; outerWheel: root.outerWheel; onFileRequested: (path) => root.fileRequested(path) }
    }
    Component {
        id: toolBody
        ColumnLayout {
            spacing: 3
            Design.ActivityGroupHeader {
                Layout.fillWidth: true
                visible: root.groupHead
                group: root.activityGroup; messageId: root.messageId
                selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null
                onToggleRequested: {
                    if (root.outerWheel && root.outerWheel.view) root.outerWheel.view.pinMessageAnchor(root.messageId, root.mapToItem(root.outerWheel.view, 0, 0).y)
                    if (root.outerWheel && root.outerWheel.selectionManager) root.outerWheel.selectionManager.layoutChangeStarting()
                    if (root.outerWheel && root.outerWheel.view) root.outerWheel.view.beginGroupDisclosure(String(root.activityGroup.id || ""))
                    root.controller.messages.toggleActivityGroup(root.rowIndex)
                    if (root.outerWheel && root.outerWheel.selectionManager) Qt.callLater(root.outerWheel.selectionManager.layoutChangeFinished)
                    if (root.outerWheel && root.outerWheel.view) root.outerWheel.view.settlePinnedMessage()
                }
            }
            Item {
                Layout.fillWidth: true
                // 非组头保留正文自然高度，由整行唯一的动画视口裁切，避免先清空再收缩。
                implicitHeight: root.groupCollapsed && root.groupHead ? 0 : toolRow.implicitHeight
                clip: true
                visible: !root.groupCollapsed || root.groupTransition
                Design.DisclosureHeight on implicitHeight { allowed: root.groupTransition && root.groupHead; onRunningChanged: root.headAnimating = running }
                Rectangle { visible: root.grouped; x: 6; width: 1; height: parent.height; color: Design.Theme.border }
                Design.ToolActivityRow {
                    id: toolRow
                    x: root.grouped ? 22 : 0; width: parent.width - x
                    messageId: root.messageId
                    blockOffset: root.groupHead ? 1 : 0
                    activity: root.activity; attachments: root.attachments; title: root.messageTitle
                    details: root.content; status: root.messageStatus; expanded: root.expanded
                    controller: root.controller; outerWheel: root.outerWheel
                    onToggleRequested: { if (root.outerWheel) root.outerWheel.view.beginContentDisclosure(); root.controller.messages.toggleExpanded(root.rowIndex) }
                    onImageRequested: (image) => root.imageRequested(image)
                }
            }
        }
    }

    Component {
        id: assistantBody
        ColumnLayout {
            spacing: 14
            Design.MessageContent { Layout.fillWidth: true; blocks: root.contentBlocks; messageId: root.messageId; controller: root.controller; outerWheel: root.outerWheel; onLinkActivated: (link) => root.openLink(link) }
            Design.MessageAttachments { visible: root.attachments.length > 0; Layout.fillWidth: true; attachments: root.attachments; onImageRequested: (image) => root.imageRequested(image) }
            Design.MessageActions {
                text: root.content
                onCopyRequested: (text) => root.controller.copyText(text)
            }
        }
    }
}
