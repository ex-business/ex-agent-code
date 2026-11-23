pragma ComponentBehavior: Bound
import QtQuick

// 每个聊天视口独立管理快速模式和有界预排版高度；不预建整个会话。
QtObject {
    id: policy
    required property Flickable view
    // 定时器重启会短暂切换 running，滚动期间保持发布门锁，直到真正静止。
    property bool scrolling: false
    property bool adjustingLayout: false
    property bool fast: false
    // -1向更早消息，0静止，1向更新消息；方向反转不等待速度平滑。
    property int direction: 0
    property real speed: 0
    readonly property int aheadPixels: preloadPixels
    readonly property int behindPixels: preloadPixels
    property int slowSamples: 0
    property int heightRevision: 0
    property real previousY: 0
    property double previousMs: 0
    property var heights: ({})
    property var order: []
    readonly property int preloadPixels: Math.ceil(view.height * 2)
    property Timer quiet: Timer { interval: 150; onTriggered: { policy.scrolling = false; policy.fast = false; policy.direction = 0; policy.speed = 0 } }
    property Connections changes: Connections {
        target: policy.view
        function onContentYChanged() {
            // 锚点补偿不属于用户手势，不能再次进入滚动态并推迟下一批排版发布。
            if (policy.adjustingLayout) { policy.previousY = policy.view.contentY; policy.previousMs = 0; return }
            const now = Date.now()
            const signedDelta = policy.view.contentY - policy.previousY
            const delta = Math.abs(signedDelta)
            const elapsed = now - policy.previousMs
            // 单次定位跳转不当成手势；快进阈值与慢出阈值分开，避免逐帧切换渲染模式。
            if (policy.previousMs > 0 && elapsed > 0 && elapsed < 100) {
                const speed = delta * 1000 / elapsed
                const direction = signedDelta > 0 ? 1 : signedDelta < 0 ? -1 : policy.direction
                if (delta >= 1) {
                    policy.speed = direction !== policy.direction ? speed : policy.speed * 0.65 + speed * 0.35
                    policy.direction = direction
                }
                if (speed >= 2200) { policy.fast = true; policy.slowSamples = 0 }
                else if (speed < 900 && policy.fast) {
                    if (++policy.slowSamples >= 3) { policy.fast = false; policy.slowSamples = 0 }
                } else policy.slowSamples = 0
            }
            // 间隔较长的首个滚轮事件仍立即确定方向，速度则等待连续样本，避免把跳页当作高速。
            if (delta >= 1 && policy.scrolling && (policy.previousMs <= 0 || elapsed >= 100)) {
                policy.direction = signedDelta > 0 ? 1 : -1
                policy.speed = 0
            }
            policy.previousY = policy.view.contentY
            policy.previousMs = now
            policy.beginScroll()
        }
    }
    function laneFor(item: Item): int {
        if (!item || !item.parent) return 2
        const top = item.mapToItem(view.contentItem, 0, 0).y - view.contentY
        const bottom = top + Math.max(24, item.height)
        // 上下各准备两屏；反向只交换排版优先级，避免重新扩大缓存。
        if (bottom >= 0 && top <= view.height) return 0
        if (bottom < -preloadPixels || top > view.height + preloadPixels) return 2
        const behind = direction < 0 ? top > view.height : direction > 0 && bottom < 0
        return behind ? 4 : 1
    }
    function priorityForLane(lane: int): int {
        // 沿用诊断编号：0可见、1前方或静止、2越界、3已回收、4后方。
        if (lane === 0) return 100
        if (lane === 1) return 20
        return lane === 4 ? 10 : -1
    }
    function beginScroll() { scrolling = true; quiet.restart() }
    function reset() { scrolling = false; fast = false; direction = 0; speed = 0; slowSamples = 0; previousMs = 0; quiet.stop(); heights = ({}); order = [] }
    function heightFor(key: string, width: real): real {
        const revision = heightRevision
        return heights[key + ':' + Math.round(width)] || 0
    }
    function remember(key: string, width: real, height: real) {
        if (!key || height <= 0) return
        const id = key + ':' + Math.round(width)
        if (heights[id] === height) return
        if (heights[id] === undefined) order.push(id)
        heights[id] = height
        if (order.length > 256) delete heights[order.shift()]
        ++heightRevision
    }
}
