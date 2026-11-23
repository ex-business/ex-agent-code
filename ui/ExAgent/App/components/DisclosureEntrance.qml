pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 展开先完成布局，再在渲染线程淡入；不逐帧改变列表行高，也不播放回收项的初始入场。
OpacityAnimator {
    id: entrance
    required property Item surface
    property string contentKey: ""
    property bool expanded: true
    property bool contentReady: true
    property bool allowed: true
    property bool initialized: false
    property bool pending: false
    readonly property bool motionAllowed: allowed && Design.Theme.motionEnabled && Design.InteractionMotion.allowed
    property Design.MotionRegistration registration: Design.MotionRegistration { running: entrance.running }
    property Connections interruption: Connections {
        target: Design.InteractionMotion
        function onInterrupted() { entrance.cancel() }
    }
    target: surface
    from: 0.65
    to: 1
    duration: Design.Theme.disclosureDuration
    easing.type: Easing.OutCubic
    Component.onCompleted: initialized = true
    function cancel() {
        pending = false
        stop()
        if (surface) surface.opacity = 1
    }
    function request() {
        if (!initialized) return
        pending = expanded
        Qt.callLater(tryStart)
    }
    function tryStart() {
        if (!pending || !expanded || !contentReady) return
        pending = false
        if (motionAllowed) restart()
    }
    onContentKeyChanged: cancel()
    onExpandedChanged: { if (!expanded) cancel(); else request() }
    onContentReadyChanged: if (contentReady) Qt.callLater(tryStart)
    onMotionAllowedChanged: if (!motionAllowed) cancel()
}
