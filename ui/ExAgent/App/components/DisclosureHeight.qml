pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

// 仅用户改变展开状态时启用；列表复用、异步内容增长和滚动布局不做高度补间。
Behavior {
    property bool allowed: true
    property bool ready: false
    property double previousChangeMs: 0
    property bool rapidChange: false
    // 连续反向时立即采用最新高度，不能继续逐帧排版或排队播放上一段收起。
    onTargetValueChanged: {
        const now = Date.now()
        rapidChange = previousChangeMs > 0 && now - previousChangeMs < Design.Theme.disclosureDuration + 40
        previousChangeMs = now
    }
    enabled: ready && allowed && !rapidChange && Design.Theme.motionEnabled && Design.InteractionMotion.allowed
    Component.onCompleted: ready = true
    readonly property bool running: heightMotion.running
    property Design.MotionRegistration registration: Design.MotionRegistration { running: heightMotion.running }
    property Connections interruption: Connections {
        target: Design.InteractionMotion
        function onInterrupted() { if (heightMotion.running) heightMotion.complete() }
    }
    NumberAnimation { id: heightMotion; duration: Design.Theme.disclosureDuration; easing.type: Easing.OutCubic }
}
