pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 离散滚轮共享连续速度；触控板的系统惯性不进入此模型，避免重复减速。
Item {
    id: motion
    required property Flickable view
    property int orientation: Qt.Vertical
    readonly property bool running: frames.running
    property real responseRate: 42
    property real settlingRate: responseRate
    property real maxFrameStep: Infinity
    property real maxSpeed: Infinity
    property bool paceFrames: false
    property real previousStepSeconds: 0
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
        if (!continuing) {
            velocity = 0
            previousStepSeconds = 0
        }
        destination = physicalTarget
        if (Math.abs(destination - position) < 0.01) { stop(); return 0 }
        if (view.flicking) view.cancelFlick()
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
        if (!frames.running) frames.start()
        // 返回量与累计目标增量一致，只有真正超出内容边界的输入才交给外层。
        return accepted
    }
    function advance(seconds: real) {
        // 临界阻尼解析解保留加速度和减速度，不用固定16ms定时器，也不因输入重启动画。
        // 正常帧按实时间隔推进；受限视口在长帧分摊剩余距离，目标与方向仍随输入立即更新。
        synchronizePosition()
        const sample = Math.min(maxFrameStep, Math.max(0, seconds))
        if (sample === 0) return
        // 调度产生一长一短两帧时，平分相邻间隔；匀速段不会因单个迟到帧突然跨过大段正文。
        const dt = paceFrames && previousStepSeconds > 0 ? (previousStepSeconds + sample) / 2 : sample
        if (paceFrames) previousStepSeconds = sample
        const target = Math.max(start, Math.min(end, destination))
        const offset = position - target
        // 接近目标时连续过渡到收尾速率，降低高速脉冲峰值的同时避免亚像素长尾。
        const proximity = Math.max(0, 1 - Math.abs(offset) / 72)
        const blend = proximity * proximity * (3 - 2 * proximity)
        const omega = responseRate + (settlingRate - responseRate) * blend
        const coefficient = velocity + omega * offset
        const decay = Math.exp(-omega * dt)
        const projected = target + (offset + coefficient * dt) * decay
        const crossed = offset !== 0 && offset * (projected - target) <= 0
        const desired = crossed ? target : Math.max(start, Math.min(end, projected))
        // 驱动可能把多格合成一个大事件。限速只作用于本帧，余量留在原目标，反向仍可立即打断。
        const budget = Number.isFinite(maxSpeed) ? maxSpeed * dt : Infinity
        const travel = desired - position
        const limited = Math.abs(travel) > budget
        const next = limited ? position + Math.sign(travel) * budget : desired
        const nextVelocity = (velocity - omega * coefficient * dt) * decay
        velocity = Math.max(-maxSpeed, Math.min(maxSpeed, nextVelocity))
        // 尾帧只写一次最终坐标，避免先移动小数像素再对齐时重复触发布局。
        // 聊天滚动只剩半个物理像素时直接到达目标，避免在同一字形边缘来回采样小数坐标。
        const arrived = paceFrames ? Math.abs(target - next) <= 0.5 / motion.Screen.devicePixelRatio
                                   : Math.abs(target - next) < 0.25 && Math.abs(velocity) < 4
        const finished = next === target || (!limited && arrived)
        const framePosition = finished ? target : next
        if (orientation === Qt.Vertical) view.contentY = framePosition
        else view.contentX = framePosition
        lastPosition = position
        trace(2, 0)
        if (finished) {
            stop(); settled()
        }
    }
    FrameAnimation { id: frames; onTriggered: motion.advance(frameTime) }
}
