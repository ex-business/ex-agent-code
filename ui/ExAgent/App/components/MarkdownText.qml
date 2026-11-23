pragma ComponentBehavior: Bound
import QtQuick
import "BlockGeometry.js" as Geometry
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
    readonly property string layoutKey: root.messageId + ':' + (root.block.blockId || root.blockIndex)
    readonly property real rememberedHeight: root.renderPolicy ? root.renderPolicy.heightFor(layoutKey, width) : 0
    implicitHeight: preparation.state === RichTextPreparation.Ready ? prose.implicitHeight
                  : Math.max(24, rememberedHeight || Number(preparation.layoutHint.height || 0) || Geometry.estimatedProse(block.text || "", width))
    clip: true
    function rememberLayout() {
        if (preparation.state === RichTextPreparation.Ready && preparation.value.html && root.renderPolicy && !root.pooled && root.messageId && width > 0)
            root.renderPolicy.remember(layoutKey, width, prose.implicitHeight)
    }
    // 在发布边界采同一正文起点的视口坐标；回收或换宽后丢弃失配样本。
    property var pendingProbe: null
    function beginLayoutProbe() {
        if (!Design.PerformanceTrace.sink || !selectionView || !messageId || pooled || preparation.state === RichTextPreparation.Ready) return
        let row = root.parent
        while (row && row.parent !== selectionView.contentItem) row = row.parent
        if (!row) return
        pendingProbe = {row: row, rowY: row.y, key: layoutKey, width: width, height: implicitHeight,
                        y: mapToItem(selectionView, 0, 0).y, scrollY: selectionView.contentY,
                        cached: Number(preparation.layoutHint.height || 0) > 0}
    }
    function finishLayoutProbe() {
        const probe = pendingProbe
        pendingProbe = null
        if (!probe || !probe.row || probe.key !== layoutKey || pooled || !selectionView || probe.width !== width || !layoutReady) return
        Design.PerformanceTrace.sink.recordTextLayout(layoutKey, probe.cached, probe.height, implicitHeight,
                probe.y, mapToItem(selectionView, 0, 0).y, selectionView.contentY - probe.scrollY, selectionView.height, probe.row.y - probe.rowY, blockIndex)
    }
    RichTextPreparation {
        id: preparation
        onStateChanged: if (state === RichTextPreparation.Running) Design.PerformanceTrace.contentPhase(root, 7, root.preparationLane)
        source: root.block.text || ""
        font: prose.font
        layoutWidth: root.width
        // 正文首帧直接发布完整富文本，避免持续滚动使可见消息长期空白。
        holdLayout: root.pooled
        preparationPriority: root.renderPolicy ? root.renderPolicy.priorityForLane(root.preparationLane) : (root.pooled ? -1 : 100)
        // 负优先级已由准备队列延后执行；避免同一通知反复触发布局绑定。
        dark: Design.Theme.dark
        onLayoutAboutToChange: { root.beginLayoutProbe(); if (root.selectionManager) root.selectionManager.layoutChangeStarting() }
        onLayoutChanged: { Qt.callLater(root.rememberLayout); if (root.selectionManager) root.selectionManager.layoutChangeFinished(); if (root.pendingProbe) Qt.callLater(root.finishLayoutProbe) }
    }
    Text {
        visible: preparation.state === RichTextPreparation.Failed
        text: "内容准备失败，原文仍保留"
        color: Design.Theme.muted
        font.pixelSize: 12
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
        // 在当前布局完成后读取最新身份和高度，避免测量过程中反写缓存触发绑定环。
        onImplicitHeightChanged: Qt.callLater(root.rememberLayout)
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
