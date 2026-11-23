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
        // 离屏预加载项先完成最终排版；只推迟正在阅读的可见项和回收项。
        holdLayout: root.pooled || (root.preparationLane === 0 && Boolean(root.renderPolicy && root.renderPolicy.scrolling))
        preparationPriority: root.renderPolicy ? root.renderPolicy.priorityForLane(root.preparationLane) : (root.pooled ? -1 : 100)
        font: messageText.font
        deferUncached: preparationPriority < 0
        mode: RichTextPreparation.User
        onLayoutAboutToChange: if (root.selectionManager) root.selectionManager.layoutChangeStarting()
        onLayoutChanged: if (root.selectionManager) root.selectionManager.layoutChangeFinished()
    }
    Rectangle {
        id: bubble
        anchors.right: parent.right
        // 字形前进宽度保留取整余量，避免末字被挤成单独一行。
        width: Math.min(Math.max(42, (preparation.state === RichTextPreparation.Ready ? Math.ceil(root.naturalTextWidth) + 36 : Number(preparation.layoutHint.bubbleWidth || root.width * 0.85))), root.width * 0.85)
        // 复用命中时先使用后台测量高度；首次未命中使用同宽原文占位，避免72px省略号气泡。
        height: (preparation.state === RichTextPreparation.Ready ? messageText.implicitHeight : Number(preparation.layoutHint.height || preview.implicitHeight)) + 24
        radius: 18
        color: Design.Theme.dark ? "#eeeeee" : "#000000"
        Text {
            id: preview
            x: 16; y: 12
            width: parent.width - 32
            visible: preparation.state !== RichTextPreparation.Ready
            text: root.displayText.slice(0, 800)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 24
            elide: Text.ElideRight
            font: messageText.font
            color: Design.Theme.dark ? "#202020" : "#ffffff"
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
