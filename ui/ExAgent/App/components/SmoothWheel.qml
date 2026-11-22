pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Item {
    id: root
    required property Flickable view
    property real stepSize: 72
    property int orientation: Qt.Vertical
    property var outerWheel: null
    property var selectionManager: outerWheel ? outerWheel.selectionManager : null
    readonly property bool animating: view.moving || observer.active || wheelMotion.running
    signal scrollingStarted()
    signal scrollingSettled()

    property real destination: 0
    property real lastStep: 0
    property double lastInput: 0
    function stop() { wheelMotion.stop(); view.cancelFlick() }
    function scrollBy(delta: real): bool {
        const now = Date.now()
        const current = root.orientation === Qt.Vertical ? view.contentY : view.contentX
        const start = root.orientation === Qt.Vertical ? view.originY - view.topMargin : view.originX - view.leftMargin
        const extent = root.orientation === Qt.Vertical ? view.contentHeight - view.height + view.topMargin + view.bottomMargin : view.contentWidth - view.width + view.leftMargin + view.rightMargin
        // 反向输入立即丢弃旧目标；连续滚动合并到一个有界目标，避免补间排队追赶。
        const continuing = wheelMotion.running && delta * lastStep > 0 && now - lastInput < 160
        const runway = Math.min(480, (root.orientation === Qt.Vertical ? view.height : view.width) * 0.75)
        destination = Math.max(start, Math.min(start + Math.max(0, extent), Math.max(current - runway, Math.min(current + runway, (continuing ? destination : current) + delta))))
        wheelMotion.stop()
        view.cancelFlick()
        if (Math.abs(destination - current) < 0.01) return false
        view.pixelAligned = false
        wheelMotion.from = current
        wheelMotion.to = destination
        wheelMotion.duration = continuing ? 70 : 110
        lastInput = now; lastStep = delta
        wheelMotion.start()
        return true
    }
    NumberAnimation {
        id: wheelMotion
        target: root.view
        property: root.orientation === Qt.Vertical ? "contentY" : "contentX"
        easing.type: Easing.OutCubic
        onFinished: settled.restart()
    }
    // 触控板像素事件及系统惯性原样交给Flickable；仅离散鼠标滚轮使用短补间。
    WheelHandler {
        id: observer
        parent: root.view
        target: null
        orientation: root.orientation
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: false
        onWheel: (event) => {
            root.scrollingStarted()
            settled.restart()
            const angle = root.orientation === Qt.Vertical ? event.angleDelta.y : event.angleDelta.x
            if (Design.Theme.motionEnabled && event.phase === Qt.NoScrollPhase && event.pixelDelta.x === 0 && event.pixelDelta.y === 0 && angle !== 0) {
                event.accepted = root.scrollBy(-angle / 120 * root.stepSize)
            } else {
                wheelMotion.stop()
                event.accepted = false
            }
        }
        onActiveChanged: if (!active) settled.restart()
    }
    Connections {
        target: root.view
        ignoreUnknownSignals: true
        function onPixelScrollStarted() { wheelMotion.stop(); root.scrollingStarted(); settled.restart() }
        function onMovingChanged() {
            root.view.pixelAligned = !root.view.moving
            if (!root.view.moving) settled.restart()
        }
    }
    Timer {
        id: settled
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
