pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat

// 手势与文本实现解耦：正文 TextEdit 和回收的代码行使用同一套点击/拖动规则。
MouseArea {
    id: root
    required property var positionFor
    required property var rangeFor
    required property var applySelection
    property var cursorFor: null
    property int selectionAnchor: 0
    property int gestureAnchor: 0
    property int unit: 0
    property int unitStart: 0
    property int unitEnd: 0
    property real pressX: 0
    property real pressY: 0
    property real lastPress: 0
    property int clickCount: 0
    property bool selectingDrag: false
    property point scenePointer: Qt.point(0, 0)
    signal focusRequested()
    signal contextRequested(real x, real y)
    signal tapped(real x, real y)
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    preventStealing: true
    hoverEnabled: true
    cursorShape: cursorFor ? cursorFor(mouseX, mouseY) : Qt.IBeamCursor
    // 悬停与按键手势独立，覆盖原生 TextEdit 的默认光标，但不抢占滚动或选择。
    HoverHandler {
        id: hover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        cursorShape: root.cursorFor ? root.cursorFor(point.position.x, point.position.y) : Qt.IBeamCursor
    }
    function extend(x, y) {
        const position = positionFor(x, y, unit === 0)
        if (unit === 0) applySelection(gestureAnchor, position)
        else {
            const range = rangeFor(position, unit)
            applySelection(position < unitStart ? unitEnd : unitStart,
                           position < unitStart ? range.start : range.end)
        }
    }
    onPressed: (mouse) => {
        focusRequested()
        if (mouse.button === Qt.RightButton) { contextRequested(mouse.x, mouse.y); return }
        selectingDrag = false
        scenePointer = mapToItem(null, mouse.x, mouse.y)
        const position = positionFor(mouse.x, mouse.y, false)
        const now = Date.now()
        const nearby = now - lastPress <= TextSelectionRules.doubleClickInterval
                    && Math.abs(mouse.x - pressX) < TextSelectionRules.dragDistance
                    && Math.abs(mouse.y - pressY) < TextSelectionRules.dragDistance
        clickCount = nearby ? clickCount % 3 + 1 : 1
        lastPress = now
        pressX = mouse.x; pressY = mouse.y
        unit = clickCount === 3 ? 2 : clickCount === 2 ? 1 : 0
        if (unit !== 0) {
            const range = rangeFor(position, unit)
            unitStart = range.start; unitEnd = range.end
            applySelection(unitStart, unitEnd)
        } else {
            // 锚点只在按下时确定，不能随选区绑定或焦点回调在拖动中漂移。
            const caret = positionFor(mouse.x, mouse.y, true)
            gestureAnchor = mouse.modifiers & Qt.ShiftModifier ? selectionAnchor : caret
            applySelection(gestureAnchor, caret)
        }
    }
    // 让第二次 press 进入统一计数，避免 doubleClicked 与 pressed 的顺序覆盖选区。
    onDoubleClicked: (mouse) => { mouse.accepted = false }
    onClicked: (mouse) => { if (mouse.button === Qt.LeftButton && unit === 0) tapped(mouse.x, mouse.y) }
    onPositionChanged: (mouse) => {
        scenePointer = mapToItem(null, mouse.x, mouse.y)
        if ((pressedButtons & Qt.LeftButton) && (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 2)) { selectingDrag = true; clickCount = 0; extend(mouse.x, mouse.y) }
    }
    onReleased: (mouse) => {
        // 高速移动可能合并 move 事件，释放位置必须再落一次选区终点。
        if (mouse.button === Qt.LeftButton && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 2) extend(mouse.x, mouse.y)
        selectingDrag = false
    }
    onCanceled: selectingDrag = false
}
