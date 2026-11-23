pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Item {
    id: root
    required property Flickable view
    property real stepSize: 72
    property int orientation: Qt.Vertical
    property var outerWheel: null
    property var linkedWheel: null
    property var selectionManager: outerWheel ? outerWheel.selectionManager : null
    readonly property bool kineticRunning: wheelMotion.running
    readonly property bool animating: view.moving || observer.active || wheelMotion.running
    signal scrollingStarted()
    signal scrollingSettled()

    function stop() { wheelMotion.stop(); view.cancelFlick(); if (linkedWheel) linkedWheel.stop() }
    function consumePixels(delta: real): real {
        if (delta === 0) return 0
        Design.InteractionMotion.interruptForInput()
        root.scrollingStarted(); settleTimer.restart()
        return -wheelMotion.enqueue(-delta)
    }
    function scrollBy(delta: real): bool { return wheelMotion.scrollBy(delta) }
    Design.WheelKinetics {
        id: wheelMotion
        view: root.view
        orientation: root.orientation
        onSettled: settleTimer.restart()
    }
    // 按下即停止自定义惯性；被动观察不抢走正文拖选和按钮点击。
    PointHandler {
        parent: root.view
        enabled: root.orientation === Qt.Vertical
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: if (active) { root.stop(); settleTimer.restart() }
    }
    // 触控板保留系统像素位移与惯性；仅离散鼠标滚轮进入连续速度模型。
    WheelHandler {
        id: observer
        parent: root.view
        target: null
        orientation: root.orientation
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: false
        onWheel: (event) => {
            Design.InteractionMotion.interruptForInput()
            root.scrollingStarted()
            settleTimer.restart()
            const angle = root.orientation === Qt.Vertical ? event.angleDelta.y : event.angleDelta.x
            if (Design.Theme.motionEnabled && event.phase === Qt.NoScrollPhase && event.pixelDelta.x === 0 && event.pixelDelta.y === 0 && angle !== 0) {
                event.accepted = root.scrollBy(-angle / 120 * root.stepSize)
            } else {
                wheelMotion.stop()
                event.accepted = false
            }
        }
        onActiveChanged: if (!active) settleTimer.restart()
    }
    Connections {
        target: root.view
        ignoreUnknownSignals: true
        function onPixelScrollStarted() { wheelMotion.stop(); Design.InteractionMotion.interruptForInput(); root.scrollingStarted(); settleTimer.restart() }
        function onMovingChanged() {
            root.view.pixelAligned = !root.view.moving
            if (root.view.moving) Design.InteractionMotion.interruptForInput()
            if (!root.view.moving) settleTimer.restart()
        }
    }
    Timer {
        id: settleTimer
        interval: 140
        onTriggered: {
            if (root.animating) restart()
            else {
                root.view.pixelAligned = true
                root.scrollingSettled()
            }
        }
    }
}
