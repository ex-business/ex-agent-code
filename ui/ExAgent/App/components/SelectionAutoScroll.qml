pragma ComponentBehavior: Bound
import QtQuick

// 正文和代码共用边缘拖选；使用场景坐标，视口滚动后鼠标不动也能继续扩选。
Item {
    id: root
    required property SelectionSurface surface
    property Flickable view: null
    property bool horizontal: false
    readonly property point pointer: view ? view.mapFromItem(null, surface.scenePointer.x, surface.scenePointer.y) : Qt.point(0, 0)
    readonly property bool dragging: surface.selectingDrag && (surface.pressedButtons & Qt.LeftButton) !== 0
    Timer {
        interval: 16
        repeat: true
        running: root.view !== null && root.dragging && (root.pointer.y < 18 || root.pointer.y > root.view.height - 18
                  || root.horizontal && (root.pointer.x < 18 || root.pointer.x > root.view.width - 18))
        onTriggered: {
            const view = root.view
            const distance = root.pointer.y < 18 ? root.pointer.y - 18 : root.pointer.y > view.height - 18 ? root.pointer.y - view.height + 18 : 0
            const step = Math.max(-32, Math.min(32, distance * 0.45))
            const minimum = view.originY - view.topMargin
            const maximum = Math.max(minimum, view.originY + view.contentHeight - view.height + view.bottomMargin)
            view.cancelFlick()
            view.contentY = Math.max(minimum, Math.min(maximum, view.contentY + step))
            if (root.horizontal) {
                const horizontalDistance = root.pointer.x < 18 ? root.pointer.x - 18 : root.pointer.x > view.width - 18 ? root.pointer.x - view.width + 18 : 0
                view.contentX = Math.max(0, Math.min(Math.max(0, view.contentWidth - view.width), view.contentX + Math.max(-32, Math.min(32, horizontalDistance * 0.45))))
            }
            const local = root.surface.mapFromItem(null, root.surface.scenePointer.x, root.surface.scenePointer.y)
            root.surface.extend(local.x, local.y)
        }
    }
}
