pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.Chat
import ExAgent.App as Design

// 聊天内使用统一文档选区，其他场景保留原生编辑器选择。
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
    property Flickable selectionView: null
    property int dragAnchor: 0
    property var selectionManager: null
    property string messageId: ""
    property int blockIndex: 0
    property bool projectingSelection: false
    // 选词拖动只复用纯文本快照，不在每次鼠标移动时复制整个富文本。
    readonly property string selectionText: {
        const source = editor.text
        return source.length ? editor.getText(0, editor.length).replace(/[\u2028\u2029]/g, "\n") : ""
    }
    implicitWidth: editor.implicitWidth
    implicitHeight: editor.implicitHeight
    activeFocusOnTab: true
    signal linkActivated(string link)
    signal tapped()
    function clearSelection() { editor.deselect() }
    // 查询位置只用于工作区滚动映射，不改变选择或焦点。
    function ensureTextLayout() { editor.ensurePolished() }
    function positionAt(x, y) { return editor.positionAt(x,y) }
    function positionToRectangle(position) {
        // 显式依赖排版尺寸，分栏变宽或字体变化后重新计算表格装饰线的位置。
        if (editor.width <= 0 || editor.implicitHeight <= 0) return Qt.rect(0, 0, 0, 0)
        return editor.positionToRectangle(position)
    }
    function selectedContent() { return editor.selectedText }
    function syncKeyboardSelection() {
        if (!selectionManager || projectingSelection) return
        // 原生键盘处理完成后再同步，不能把跨消息投影误当成新的局部选择。
        const anchor = editor.cursorPosition === editor.selectionStart ? editor.selectionEnd : editor.selectionStart
        SelectionCoordinator.activate(selectionManager.document)
        selectionManager.document.select(messageId, blockIndex, anchor, messageId, blockIndex, editor.cursorPosition)
    }
    function hoverCursor(x, y) {
        if (editor.linkAt(x, y)) return Qt.PointingHandCursor
        // 悬停只决定光标，不缩小拖选区域；离开实际文字排版范围立即恢复箭头。
        return TextSelectionRules.containsText(editor.textDocument, x, y) ? Qt.IBeamCursor : Qt.ArrowCursor
    }
    onActiveFocusChanged: if (activeFocus && !root.selectionManager) SelectionCoordinator.activate(root)
    onSelectionLengthChanged: {
        if (root.selectionManager) return
        if (editor.activeFocus && selectionLength > 0) SelectionCoordinator.activate(root)
        SelectionCoordinator.changed(root)
    }
    TextEdit {
        id: editor
        width: parent.width
        readOnly: true
        cursorVisible: false
        // 聊天内左键交给跨消息手势层，聊天外仍由 TextEdit 原生拖选。
        selectByMouse: !root.selectionManager
        mouseSelectionMode: TextEdit.SelectCharacters
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
        onLinkActivated: (link) => root.linkActivated(link)
        Keys.onShortcutOverride: (event) => {
            if (root.selectionManager && (event.matches(StandardKey.Copy) || event.matches(StandardKey.SelectAll))) event.accepted = true
        }
        Keys.onPressed: (event) => {
            if (root.selectionManager && event.matches(StandardKey.Copy)) {
                root.selectionManager.copySelection()
                event.accepted = true
                return
            }
            if (root.selectionManager && event.matches(StandardKey.SelectAll)) {
                root.selectionManager.selectLoaded()
                event.accepted = true
                return
            }
            if (root.selectionManager && [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key))
                Qt.callLater(root.syncKeyboardSelection)
        }
        HoverHandler { cursorShape: root.hoverCursor(point.position.x, point.position.y) }
        PointHandler {
            id: pressTracker
            acceptedButtons: Qt.LeftButton
            enabled: !root.selectionManager
            target: null
            onActiveChanged: if (active) {
                editor.forceActiveFocus()
                SelectionCoordinator.activate(root)
                if (root.selectionView) root.selectionView.cancelFlick()
                // 等原生按下事件落定后读取锚点；观察器只被动跟踪，不抢走选区手势。
                Qt.callLater(() => {
                    if (pressTracker.active) root.dragAnchor = editor.cursorPosition === editor.selectionStart ? editor.selectionEnd : editor.selectionStart
                })
            }
        }
        TapHandler {
            enabled: !root.selectionManager
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.DragThreshold
            onDoubleTapped: (point) => {
                let position = editor.positionAt(point.position.x, point.position.y)
                const rect = editor.positionToRectangle(position)
                if (position > 0 && rect.x > point.position.x && Math.abs(rect.y - point.position.y) < rect.height) --position
                const range = TextSelectionRules.range(root.selectionText, position, 1)
                // 原生选择完成后补充共享的中文分词规则，避免与左键拖动竞争。
                Qt.callLater(() => editor.select(range.start, range.end))
            }
        }
    }
    Design.ChatSelectionPart {
        id: selectionPart
        manager: root.selectionManager
        target: editor
        messageId: root.messageId
        blockIndex: root.blockIndex
        textLength: editor.length
        positionFor: (x, y) => editor.positionAt(x, y)
        rangeFor: (position, unit) => TextSelectionRules.range(root.selectionText, position, unit)
        projectRange: (start, end) => {
            root.projectingSelection = true
            try { editor.select(start, end) }
            finally { root.projectingSelection = false }
        }
    }
    Design.SelectionSurface {
        anchors.fill: parent
        z: 2
        visible: root.selectionManager !== null
        documentPart: selectionPart
        positionFor: (x, y, nearest) => {
            let position = editor.positionAt(x, y)
            if (!nearest) {
                const rect = editor.positionToRectangle(position)
                if (position > 0 && rect.x > x && Math.abs(rect.y - y) < rect.height) --position
            }
            return position
        }
        rangeFor: (position, unit) => TextSelectionRules.range(root.selectionText, position, unit)
        applySelection: (start, end) => editor.select(start, end)
        cursorFor: (x, y) => root.hoverCursor(x, y)
        onFocusRequested: editor.forceActiveFocus()
        onContextRequested: (x, y) => textMenu.popup(root, x, y)
        onTapped: (x, y) => { const link = editor.linkAt(x, y); if (link) root.linkActivated(link); else root.tapped() }
    }
    MouseArea {
        visible: !root.selectionManager
        anchors.fill: parent
        z: 1
        acceptedButtons: Qt.RightButton
        onPressed: (mouse) => { SelectionCoordinator.activate(root); textMenu.popup(root, mouse.x, mouse.y) }
    }
    Design.SelectionAutoScroll {
        selectionItem: editor
        view: root.selectionManager ? null : root.selectionView
        scenePointer: pressTracker.point.scenePosition
        dragging: pressTracker.active && Math.abs(pressTracker.point.scenePosition.x - pressTracker.point.scenePressPosition.x)
                  + Math.abs(pressTracker.point.scenePosition.y - pressTracker.point.scenePressPosition.y) > TextSelectionRules.dragDistance
        onExtendRequested: (x, y) => editor.select(root.dragAnchor, editor.positionAt(x, y))
    }
    Design.ContextMenu {
        id: textMenu
        Design.ContextMenuItem { text: "复制"; iconName: "copy"; shortcutLabel: Qt.platform.os === "osx" ? "⌘C" : ""; enabled: root.selectionManager ? root.selectionManager.document.selectionLength > 0 : editor.selectionStart !== editor.selectionEnd; onTriggered: { if (root.selectionManager) root.selectionManager.copySelection(); else { SelectionCoordinator.activate(root); SelectionCoordinator.copy() } } }
        Design.ContextMenuItem { visible: root.selectionManager !== null; text: "全选已加载聊天"; iconName: "select"; shortcutLabel: Qt.platform.os === "osx" ? "⌘A" : ""; onTriggered: root.selectionManager.selectLoaded() }
        Design.ContextMenuItem { text: "全选当前段落"; iconName: "select"; onTriggered: { editor.forceActiveFocus(); if (root.selectionManager) { SelectionCoordinator.activate(root.selectionManager.document); root.selectionManager.document.select(root.messageId, root.blockIndex, 0, root.messageId, root.blockIndex, editor.length) } else editor.selectAll() } }
    }
}
