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
    property int blockOffset: 0
    property string messageId: ""
    signal toggleRequested()
    signal imageRequested(var image)
    readonly property int kind: Number(activity.kind || 0)
    // 命令使用整行排版，长参数自然换行，展开箭头在不同长度的命令间对齐。
    readonly property bool commandRow: kind === 2 && Number(activity.blockKind || 3) === 3
    // 摘要保留原始命令用于选择复制，只裁切折叠态的可见行。
    FontMetrics { id: commandMetrics; font: commandLabel.font }
    readonly property real previewLimit: Math.ceil(commandMetrics.lineSpacing * 3)
    readonly property bool longCommand: commandRow && commandLabel.implicitHeight > previewLimit + 1
    readonly property bool canExpand: hasDetails || longCommand
    readonly property bool hasDetails: details.length > 0 || attachments.length > 0
    readonly property string summary: activity.summary || (kind === 3 ? "已查看 " + attachments.length + " 张图像" : title.startsWith("运行命令") && status === 0 ? "已" + title : title)
    // 只缓存当前消息的短详情；长输出仍按需释放，不预建历史记录。
    property bool detailsRetained: false
    readonly property bool cacheableDetails: details.length <= 64 * 1024
    onCacheableDetailsChanged: if (!cacheableDetails) detailsRetained = false
    property bool disclosureReady: false
    property bool disclosureActive: false
    Component.onCompleted: disclosureReady = true
    onExpandedChanged: if (disclosureReady) { disclosureActive = true; disclosureReset.restart() }
    onMessageIdChanged: { detailsRetained = false; disclosureActive = false; disclosureReset.stop() }
    Timer { id: disclosureReset; interval: 16; onTriggered: { if (toolDisclosure.running) restart(); else root.disclosureActive = false } }
    spacing: 6
    Button {
        id: toggle
        Layout.maximumWidth: root.width
        Layout.fillWidth: root.commandRow
        Layout.alignment: Qt.AlignLeft
        implicitWidth: root.commandRow ? root.width : Math.min(root.width, commandLabel.implicitWidth + (root.hasDetails ? 46 : 26))
        implicitHeight: Math.max(30, preview.implicitHeight + 10)
        leftPadding: 5
        rightPadding: 7
        topPadding: 5
        bottomPadding: 5
        hoverEnabled: true
        Accessible.name: root.summary + (root.canExpand ? root.expanded ? "，收起" : "，展开" : "")
        onClicked: if (root.canExpand) root.toggleRequested()
        background: Rectangle {
            radius: 6
            color: toggle.hovered && root.canExpand ? Design.Theme.code : "transparent"
            border.color: toggle.visualFocus ? Design.Theme.secondary : "transparent"
        }
        contentItem: RowLayout {
            spacing: 8
            Design.Icon {
                name: root.activity.readPath ? "book" : root.kind === 3 ? "image" : root.kind === 4 ? "search" : root.kind === 2 ? "terminal" : "activity"
                implicitWidth: 14; implicitHeight: 14
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                color: Design.Theme.muted
            }
            Item {
                id: preview
                Layout.fillWidth: true
                implicitWidth: commandLabel.implicitWidth
                implicitHeight: root.longCommand && !root.expanded ? root.previewLimit : commandLabel.implicitHeight
                clip: true
                Design.SelectableText {
                    id: commandLabel
                    width: parent.width
                    text: root.summary
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    messageId: root.messageId; blockIndex: root.blockOffset
                    selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null
                    color: Design.Theme.secondary
                    font.family: root.commandRow && !root.activity.readPath ? Design.Theme.monoFont : Design.Theme.font
                    font.pixelSize: root.commandRow ? 12 : 13
                    Loader {
                        anchors.fill: parent
                        active: root.status === 1 && root.commandRow
                        sourceComponent: Design.ExecutionIndicator { label: commandLabel; running: true }
                    }
                    onTapped: if (root.canExpand) root.toggleRequested()
                }
            }
            Design.Icon {
                visible: root.canExpand
                name: "right"; rotation: root.expanded ? 90 : 0
                Design.DisclosureRotation on rotation {}
                implicitWidth: 12; implicitHeight: 12
                color: Design.Theme.muted
            }
        }
    }
    Item {
        id: detailsViewport
        Layout.fillWidth: true
        implicitHeight: root.expanded ? detailsLayout.implicitHeight : 0
        visible: implicitHeight > 0
        clip: true
        Design.DisclosureHeight on implicitHeight { id: toolDisclosure; allowed: root.disclosureActive && !root.expanded && !(root.outerWheel && root.outerWheel.animating) }
        Design.DisclosureEntrance {
            surface: detailsViewport
            contentKey: root.messageId
            expanded: root.expanded
            contentReady: detailsLoader.status !== Loader.Loading
            allowed: !(root.outerWheel && root.outerWheel.animating)
        }
        ColumnLayout {
            id: detailsLayout
            width: parent.width
            spacing: 6
            Design.MessageAttachments {
                compact: true
                visible: (root.expanded || root.disclosureActive) && root.attachments.length > 0
                Layout.fillWidth: true
                attachments: root.attachments
                onImageRequested: (image) => root.imageRequested(image)
            }
    Loader {
        id: detailsLoader
        asynchronous: true
        active: (root.expanded || root.disclosureActive || root.detailsRetained) && root.details.length > 0
        onStatusChanged: Design.PerformanceTrace.contentPhase(detailsLoader, 1, status)
        onLoaded: { root.detailsRetained = root.cacheableDetails; Design.PerformanceTrace.contentPhase(detailsLoader, 1, 4) }
        Layout.fillWidth: true
        sourceComponent: Design.BlockRenderer {
            block: ({blockId: root.activity.blockId || "", rendererKind: Number(root.activity.blockKind || 2), contentMode: Number(root.activity.contentMode || 1), sourceKind: Number(root.activity.sourceKind || 1), shell: root.commandRow, command: root.title, messageStatus: root.status, title: root.title, text: root.details, language: "text", data: root.activity.data || ({})})
            messageId: root.messageId; blockIndex: root.blockOffset + 1; controller: root.controller; outerWheel: root.outerWheel
        }
    }
        }
    }
}
