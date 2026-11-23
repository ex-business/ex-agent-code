pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design

// 后台读页与加入可排版范围分开。反向后可保留读完的一页，但不再插入历史行。
Timer {
    id: preloader
    required property ListView view
    required property MessageListModel messages
    required property Design.ScrollRenderPolicy policy
    required property bool suspended
    readonly property bool towardHistory: !suspended && policy.scrolling && policy.direction < 0 && messages.hasOlder
    readonly property real distanceToStart: view.contentY - view.originY
    readonly property bool readAhead: towardHistory && distanceToStart <= policy.aheadPixels + (policy.fast ? view.height : 0)
    readonly property bool insertAhead: towardHistory && distanceToStart <= policy.aheadPixels
    readonly property bool canPublish: insertAhead && messages.olderReady
    interval: 0
    repeat: false

    onReadAheadChanged: if (readAhead && !messages.olderReady) messages.prefetchOlder()
    onCanPublishChanged: if (canPublish) start()
    onTriggered: {
        // 避免在位移或数据通知栈内修改模型。执行时再检查方向、会话和就绪状态。
        if (!canPublish) return
        const added = messages.loadOlder()
        if (added <= 0) return
        // ListView 在可见行之前插入时保留已有行的位置，惯性继续沿当前轨迹推进。
        view.forceLayout()
        Design.PerformanceTrace.contentPhase(preloader, 8, messages.count)
        // 一页若全是折叠记录，仍可能处于同一个预读区；每次只处理一页。
        if (readAhead && !messages.olderReady) messages.prefetchOlder()
        if (canPublish) start()
    }
}
