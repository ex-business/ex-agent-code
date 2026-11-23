pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 离散滚轮共享连续速度；触控板的系统惯性不进入此模型，避免重复减速。
Item {
    id: motion
    required property Flickable view
    property int orientation: Qt.Vertical
    readonly property bool running: frames.running
    property real destination: 0
    property real velocity: 0
    property real lastStep: 0
    property real lastPosition: 0
    readonly property real position: orientation === Qt.Vertical ? view.contentY : view.contentX
    readonly property real start: orientation === Qt.Vertical ? view.originY - view.topMargin : view.originX - view.leftMargin
    readonly property real end: start + Math.max(0, orientation === Qt.Vertical ? view.contentHeight - view.height + view.topMargin + view.bottomMargin : view.contentWidth - view.width + view.leftMargin + view.rightMargin)
    signal settled()
    function trace(phase: int, input: real) {
        if (view.objectName === "messageList") Design.PerformanceTrace.wheelMotion(phase, position, velocity, destination, input)
    }
    function stop() {
        const wasRunning = frames.running
        frames.stop(); velocity = 0; destination = position; lastPosition = position
        if (wasRunning) trace(3, 0)
    }
    function synchronizePosition() {
        // ListView回收可改变内容坐标原点，按实际外部位移平移目标，不把坐标修正当作反向输入。
        if (frames.running) destination += position - lastPosition
        lastPosition = position
    }
    function scrollBy(delta: real): bool { return Math.abs(enqueue(delta)) > 0.001 }
    function enqueue(delta: real): real {
        if (!Number.isFinite(delta) || delta === 0) return 0
        synchronizePosition()
        trace(0, delta)
        // 同方向只更新目标，哪怕两次输入间隔超过160ms也不能把尚未结束的速度突然归零。
        const continuing = frames.running && delta * lastStep > 0
        const origin = continuing ? Math.max(start, Math.min(end, destination)) : position
        const physicalTarget = Math.max(start, Math.min(end, origin + delta))
        const accepted = physicalTarget - origin
        // 反向清除旧方向动量，同向保留速度；累计位移只按物理边界截断，不能吞掉用户输入。
        if (!continuing) velocity = 0
        destination = physicalTarget
        if (Math.abs(destination - position) < 0.01) { stop(); return 0 }
        view.cancelFlick()
        view.pixelAligned = false
        lastStep = delta
        trace(1, accepted)
        if (!continuing) {
            // 起滚或反向先走一小步，目标仍是完整位移，后续动画只追赶剩余距离。
            const step = Math.sign(accepted) * Math.min(4, Math.abs(accepted) * 0.2)
            if (orientation === Qt.Vertical) view.contentY += step
            else view.contentX += step
            lastPosition = position
            trace(4, step)
        }
        frames.start()
        // 返回量与累计目标增量一致，只有真正超出内容边界的输入才交给外层。
        return accepted
    }
    function advance(seconds: real) {
        // 临界阻尼解析解保留加速度和减速度，不用固定16ms定时器，也不因输入重启动画。
        // 直接使用真实帧间隔，卡帧后不积欠大量小步；目标与位置均约束在当前内容边界。
        synchronizePosition()
        const dt = Math.max(0, seconds)
        const target = Math.max(start, Math.min(end, destination))
        const offset = position - target
        // 提高跟随响应，缩短拖尾；调整阻尼速度，不放大或丢弃滚轮位移。
        const omega = 42
        const coefficient = velocity + omega * offset
        const decay = Math.exp(-omega * dt)
        const projected = target + (offset + coefficient * dt) * decay
        const crossed = offset !== 0 && offset * (projected - target) <= 0
        const next = crossed ? target : Math.max(start, Math.min(end, projected))
        velocity = (velocity - omega * coefficient * dt) * decay
        if (orientation === Qt.Vertical) view.contentY = next
        else view.contentX = next
        lastPosition = position
        trace(2, 0)
        if (crossed || (next === target) || Math.abs(target - next) < 0.25 && Math.abs(velocity) < 4) {
            if (orientation === Qt.Vertical) view.contentY = target
            else view.contentX = target
            stop(); settled()
        }
    }
    FrameAnimation { id: frames; onTriggered: motion.advance(frameTime) }
}
