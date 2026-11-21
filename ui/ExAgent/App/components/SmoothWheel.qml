pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    required property Flickable view
    property real stepSize: 72
    property int orientation: Qt.Vertical
    property var outerWheel: null
    property var selectionManager: outerWheel ? outerWheel.selectionManager : null
    readonly property bool animating: view.moving || observer.active
    signal scrollingStarted()
    signal scrollingSettled()

    function stop() { view.cancelFlick() }
    // 只观察输入，不修改 contentY。像素位移、滚轮加速及惯性统一交给原生 Flickable。
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
            event.accepted = false
        }
        onActiveChanged: if (!active) settled.restart()
    }
    Connections {
        target: root.view
        ignoreUnknownSignals: true
        function onPixelScrollStarted() { root.scrollingStarted(); settled.restart() }
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
