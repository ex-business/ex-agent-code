pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design

// 槽位进入列表缓冲区即可排版；首次创建、反向和复用共享已有的有界后台队列。
RichTextPreparation {
    id: job
    required property Item slot
    property Design.ScrollRenderPolicy policy: null
    property bool pooled: false
    readonly property int lane: pooled ? 3 : policy ? policy.laneFor(slot) : 0
    layoutWidth: slot.width
    font.family: Design.Theme.font
    font.pixelSize: 14
    dark: Design.Theme.dark
    holdLayout: pooled
    preparationPriority: policy ? policy.priorityForLane(lane) : (pooled ? -1 : 100)
    onLaneChanged: Design.PerformanceTrace.contentPhase(slot, 6, lane)
    onStateChanged: if (state === RichTextPreparation.Running) Design.PerformanceTrace.contentPhase(slot, 7, lane)
}
