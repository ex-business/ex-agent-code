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
        Layout.fillWidth: false
        Layout.alignment: Qt.AlignLeft
        implicitWidth: Math.min(root.width, label.implicitWidth + (root.hasDetails ? 46 : 26))
        implicitHeight: Math.max(25, label.implicitHeight + 4)
        padding: 0
        hoverEnabled: true
        Accessible.name: root.summary + (root.hasDetails ? root.expanded ? "，收起" : "，展开" : "")
        onClicked: if (root.hasDetails) root.toggleRequested()
        HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: root.hasDetails }
        background: Item {}
        contentItem: RowLayout {
            spacing: 7
            Design.Icon { name: root.kind === 3 ? "image" : root.kind === 4 ? "search" : root.kind === 2 ? "terminal" : "activity"; implicitWidth: 15; implicitHeight: 15; color: Design.Theme.muted }
            Design.SelectableText { id: label; text: root.summary; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; messageId: root.messageId; blockIndex: root.blockOffset; selectionManager: root.outerWheel ? root.outerWheel.selectionManager : null; Layout.fillWidth: true; color: Design.Theme.muted; font.pixelSize: 13; onTapped: if (root.hasDetails) root.toggleRequested() }
            Design.Icon { visible: root.hasDetails; name: "right"; rotation: root.expanded ? 90 : 0; Design.DisclosureRotation on rotation {} implicitWidth: 12; implicitHeight: 12; color: Design.Theme.muted }
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
    Design.MessageAttachments { visible: (root.expanded || root.disclosureActive) && root.attachments.length > 0; Layout.fillWidth: true; attachments: root.attachments; onImageRequested: (image) => root.imageRequested(image) }
    Loader {
        id: detailsLoader
        asynchronous: true
        active: (root.expanded || root.disclosureActive || root.detailsRetained) && root.details.length > 0
        onStatusChanged: Design.PerformanceTrace.contentPhase(detailsLoader, 1, status)
        onLoaded: { root.detailsRetained = root.cacheableDetails; Design.PerformanceTrace.contentPhase(detailsLoader, 1, 4) }
        Layout.fillWidth: true
        sourceComponent: Design.BlockRenderer {
            block: ({blockId: root.activity.blockId || "", rendererKind: Number(root.activity.blockKind || 2), contentMode: Number(root.activity.contentMode || 1), sourceKind: Number(root.activity.sourceKind || 1), title: root.title, text: root.details, language: "text", data: root.activity.data || ({})})
            messageId: root.messageId; blockIndex: root.blockOffset + 1; controller: root.controller; outerWheel: root.outerWheel
        }
    }
        }
    }
}
