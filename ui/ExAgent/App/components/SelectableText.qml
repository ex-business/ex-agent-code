pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design

// 选择层是编辑器的兄弟节点，避免 TextEdit 的原生事件过滤器吞掉自定义手势。
FocusScope {
    id: root
    property alias text: editor.text
    property alias textFormat: editor.textFormat
    property alias wrapMode: editor.wrapMode
    property alias color: editor.color
    property alias font: editor.font
    property alias selectionColor: editor.selectionColor
    property alias selectedTextColor: editor.selectedTextColor
    readonly property int length: editor.length
    readonly property int selectionStart: editor.selectionStart
    readonly property int selectionEnd: editor.selectionEnd
    readonly property int selectionLength: editor.selectionEnd - editor.selectionStart
    implicitHeight: editor.implicitHeight
    activeFocusOnTab: true
    signal linkActivated(string link)
    function clearSelection() { editor.deselect() }
    function positionToRectangle(position) {
        // 显式依赖排版尺寸，分栏变宽或字体变化后重新计算表格装饰线的位置。
        if (editor.width <= 0 || editor.implicitHeight <= 0) return Qt.rect(0, 0, 0, 0)
        return editor.positionToRectangle(position)
    }
    function selectedContent() { return editor.selectedText }
    function hoverCursor(x, y) {
        if (editor.width <= 0 || editor.length === 0) return Qt.ArrowCursor
        if (editor.linkAt(x, y)) return Qt.PointingHandCursor
        const position = editor.positionAt(x, y)
        const current = editor.positionToRectangle(position)
        const previous = editor.positionToRectangle(Math.max(0, position - 1))
        const next = editor.positionToRectangle(Math.min(editor.length, position + 1))
        const left = Math.abs(previous.y - current.y) < 1 ? Math.min(previous.x, current.x) : current.x
        const right = Math.abs(next.y - current.y) < 1 ? Math.max(next.x, current.x) : current.x
        return y >= current.y && y <= current.y + current.height && x >= left && x <= right ? Qt.IBeamCursor : Qt.ArrowCursor
    }
    onActiveFocusChanged: if (activeFocus) SelectionCoordinator.activate(root)
    onSelectionLengthChanged: SelectionCoordinator.changed(root)
    TextEdit {
        id: editor
        width: parent.width
        readOnly: true
        selectByMouse: false
        selectByKeyboard: true
        persistentSelection: true
        focus: true
        wrapMode: TextEdit.Wrap
        color: Design.Theme.text
        selectionColor: Design.Theme.dark ? "#36557b" : "#b4d7ff"
        selectedTextColor: Design.Theme.text
        font.family: Design.Theme.font
        font.pixelSize: 14
        Accessible.name: root.Accessible.name
    }
    Design.SelectionSurface {
        id: selectionSurface
        anchors.fill: parent
        z: 1
        cursorShape: root.hoverCursor(selectionSurface.mouseX, selectionSurface.mouseY)
        selectionAnchor: editor.cursorPosition === editor.selectionStart ? editor.selectionEnd : editor.selectionStart
        positionFor: (x, y, caret) => {
            const position = editor.positionAt(x, y)
            if (caret || position === 0) return position
            const rect = editor.positionToRectangle(position)
            return rect.x > x && Math.abs(rect.y - y) < rect.height ? position - 1 : position
        }
        rangeFor: (position, unit) => TextSelectionRules.range(editor.getText(0, editor.length).replace(/[\u2028\u2029]/g, "\n"), position, unit)
        applySelection: (start, end) => { SelectionCoordinator.activate(root); editor.select(start, end) }
        onFocusRequested: { SelectionCoordinator.activate(root); editor.forceActiveFocus() }
        onContextRequested: (x, y) => textMenu.popup(root, x, y)
        onTapped: (x, y) => {
            const link = editor.linkAt(x, y)
            if (editor.selectionStart === editor.selectionEnd && link) root.linkActivated(link)
        }
    }
    Design.ContextMenu {
        id: textMenu
        Design.ContextMenuItem { text: "复制"; enabled: editor.selectionStart !== editor.selectionEnd; onTriggered: { SelectionCoordinator.activate(root); SelectionCoordinator.copy() } }
        Design.ContextMenuItem { text: "全选"; onTriggered: { editor.forceActiveFocus(); editor.selectAll() } }
    }
}
