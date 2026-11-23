pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design
import ExAgent.Chat

// 正文选择由 SelectableText 统一处理；表格分隔线是轻量装饰，不截获鼠标。
Item {
    id: root
    required property var block
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
    readonly property bool fastPreview: preparation.state !== RichTextPreparation.Ready && preparation.state !== RichTextPreparation.Failed
    readonly property string layoutKey: root.messageId + ':' + (root.block.blockId || root.blockIndex)
    readonly property real rememberedHeight: root.renderPolicy ? root.renderPolicy.heightFor(layoutKey, width) : 0
    // 减速后异步排版尚未完成时继续保留预估高度，避免预览突然缩回一行。
    implicitHeight: preparation.state === RichTextPreparation.Ready ? prose.implicitHeight : Math.max(24, Number(preparation.layoutHint.height || 0) || rememberedHeight || Math.min(640, Math.ceil((root.block.text || "").length / Math.max(20, width / 7)) * 22))
    clip: true
    function rememberLayout() {
        if (preparation.state === RichTextPreparation.Ready && root.renderPolicy)
            root.renderPolicy.remember(layoutKey, width, prose.implicitHeight)
    }
    RichTextPreparation {
        id: preparation
        onStateChanged: if (state === RichTextPreparation.Running) Design.PerformanceTrace.contentPhase(root, 7, root.preparationLane)
        source: root.block.text || ""
        font: prose.font
        layoutWidth: root.width
        // 离屏预加载项先完成最终排版；只推迟正在阅读的可见项和回收项。
        holdLayout: root.pooled || (root.preparationLane === 0 && Boolean(root.renderPolicy && root.renderPolicy.scrolling))
        preparationPriority: root.renderPolicy ? root.renderPolicy.priorityForLane(root.preparationLane) : (root.pooled ? -1 : 100)
        deferUncached: preparationPriority < 0
        dark: Design.Theme.dark
        onLayoutAboutToChange: if (root.selectionManager) root.selectionManager.layoutChangeStarting()
        onLayoutChanged: { root.rememberLayout(); if (root.selectionManager) root.selectionManager.layoutChangeFinished() }
    }
    Text {
        visible: preparation.state === RichTextPreparation.Failed
        text: preparation.state === RichTextPreparation.Failed ? "内容准备失败，原文仍保留" : "正在准备内容…"
        color: Design.Theme.muted
        font.pixelSize: 12
    }
    Text {
        visible: root.fastPreview
        width: parent.width
        height: parent.height
        text: (root.block.text || "").slice(0, 800)
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        maximumLineCount: Math.max(1, Math.floor(height / 22))
        elide: Text.ElideRight
        font.pixelSize: 14
        color: Design.Theme.secondary
        // 未就绪时保留有界原文预览和测量高度，不把整段正文替换成一行加载文案。
    }
    Design.SelectableText {
        id: prose
        visible: preparation.state === RichTextPreparation.Ready
        width: parent.width
        messageId: root.messageId
        blockIndex: root.blockIndex
        selectionManager: root.selectionManager
        selectionView: root.selectionView
        text: preparation.value.html || ""
        textFormat: TextEdit.RichText
        Accessible.name: "助手消息"
        onImplicitHeightChanged: root.rememberLayout()
        onLinkActivated: (link) => root.linkActivated(link)
    }
    Repeater {
        model: preparation.state === RichTextPreparation.Ready ? preparation.value.tableRules || [] : []
        Rectangle {
            required property var modelData
            readonly property rect endRect: prose.positionToRectangle(Math.min(prose.length, modelData.end))
            readonly property rect nextRect: prose.positionToRectangle(Math.min(prose.length, modelData.next))
            y: Math.max(endRect.y + endRect.height + 3, (endRect.y + endRect.height + nextRect.y) / 2)
            width: root.width
            height: 1
            color: Design.Theme.border
        }
    }
}
