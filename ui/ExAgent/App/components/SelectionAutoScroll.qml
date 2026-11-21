pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat

// 正文和代码共用边缘拖选；使用场景坐标，视口滚动后鼠标不动也能继续扩选。
Item {
    id: root
    required property Item selectionItem
    property Flickable view: null
    property bool horizontal: false
    property point scenePointer: Qt.point(0, 0)
    property bool dragging: false
    signal extendRequested(real x, real y)
    readonly property point pointer: view ? view.mapFromItem(null, scenePointer.x, scenePointer.y) : Qt.point(0, 0)
    SelectionScrollClock {
        running: root.view !== null && root.dragging && (root.pointer.y < 18 || root.pointer.y > root.view.height - 18
                  || root.horizontal && (root.pointer.x < 18 || root.pointer.x > root.view.width - 18))
        onTick: (seconds) => {
            const view = root.view
            const distance = root.pointer.y < 18 ? root.pointer.y - 18 : root.pointer.y > view.height - 18 ? root.pointer.y - view.height + 18 : 0
            const step = Math.max(-2000, Math.min(2000, distance * 28.125)) * seconds
            const minimum = view.originY - view.topMargin
            const maximum = Math.max(minimum, view.originY + view.contentHeight - view.height + view.bottomMargin)
            view.cancelFlick()
            view.contentY = Math.max(minimum, Math.min(maximum, view.contentY + step))
            if (root.horizontal) {
                const horizontalDistance = root.pointer.x < 18 ? root.pointer.x - 18 : root.pointer.x > view.width - 18 ? root.pointer.x - view.width + 18 : 0
                view.contentX = Math.max(0, Math.min(Math.max(0, view.contentWidth - view.width), view.contentX + Math.max(-2000, Math.min(2000, horizontalDistance * 28.125)) * seconds))
            }
            const local = root.selectionItem.mapFromItem(null, root.scenePointer.x, root.scenePointer.y)
            root.extendRequested(local.x, local.y)
        }
    }
}
