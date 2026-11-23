pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: root
    required property string text
    property string messageId: ""
    property int blockIndex: 0
    property var selectionManager: null
    property Flickable selectionView: null
    property Design.ScrollRenderPolicy renderPolicy: null
    property bool pooled: false
    readonly property int preparationLane: pooled ? 3 : renderPolicy ? renderPolicy.laneFor(root) : 0
    onPreparationLaneChanged: Design.PerformanceTrace.contentPhase(root, 6, preparationLane)
    readonly property bool layoutReady: preparation.state === RichTextPreparation.Ready || preparation.state === RichTextPreparation.Failed
    signal linkActivated(string link)
    // 导入记录常带结束换行，只规范展示边缘，不改写资源原文或消息存储。
    readonly property string displayText: text.replace(/[\r\n]+$/, "")
    readonly property var presentation: preparation.value
    readonly property real naturalTextWidth: Number(presentation.width || 0)
    implicitHeight: bubble.height
    RichTextPreparation {
        id: preparation
        onStateChanged: if (state === RichTextPreparation.Running) Design.PerformanceTrace.contentPhase(root, 7, root.preparationLane)
        source: root.displayText
        layoutWidth: root.width * 0.85
        // 正文首帧直接发布完整富文本，避免持续滚动使可见消息长期空白。
        holdLayout: root.pooled
        preparationPriority: root.renderPolicy ? root.renderPolicy.priorityForLane(root.preparationLane) : (root.pooled ? -1 : 100)
        font: messageText.font
        // 负优先级已由准备队列延后执行；避免同一通知反复触发布局绑定。
        mode: RichTextPreparation.User
        onLayoutAboutToChange: if (root.selectionManager) root.selectionManager.layoutChangeStarting()
        onLayoutChanged: if (root.selectionManager) root.selectionManager.layoutChangeFinished()
    }
    Rectangle {
        id: bubble
        anchors.right: parent.right
        visible: root.layoutReady
        // 字形前进宽度保留取整余量，避免末字被挤成单独一行。
        width: Math.min(Math.max(42, (preparation.state === RichTextPreparation.Ready ? Math.ceil(root.naturalTextWidth) + 36 : Number(preparation.layoutHint.bubbleWidth || root.width * 0.85))), root.width * 0.85)
        // 未就绪时只预留空间，避免把截断的用户输入绘制成另一种气泡。
        readonly property real estimatedLines: Math.ceil(root.displayText.length / Math.max(1, (width - 32) / 8))
        readonly property real reservedHeight: Number(preparation.layoutHint.height || Math.max(20, Math.min(480, estimatedLines * 20)))
        height: (preparation.state === RichTextPreparation.Ready ? messageText.implicitHeight : reservedHeight) + 24
        radius: 18
        color: Design.Theme.dark ? "#eeeeee" : "#000000"
        Text {
            anchors.centerIn: parent
            visible: preparation.state === RichTextPreparation.Failed
            text: "内容准备失败"
            color: messageText.color
            font: messageText.font
        }
        Design.SelectableText {
            id: messageText
            visible: preparation.state === RichTextPreparation.Ready
            x: 16; y: 12
            width: parent.width - 32
            messageId: root.messageId
            blockIndex: root.blockIndex
            selectionManager: root.selectionManager
            selectionView: root.selectionView
            text: root.presentation.html || ""
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
