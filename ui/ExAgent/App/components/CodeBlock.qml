pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

Rectangle {
    id: root
    required property string code
    property string language: ""
    required property ChatController controller
    property var outerWheel: null
    property string messageId: ""
    property int blockIndex: 0
    readonly property var selectionManager: outerWheel ? outerWheel.selectionManager : null
    property int maximumCodeHeight: 330
    readonly property int lineHeight: 22
    readonly property int viewportHeight: Math.min(maximumCodeHeight, Math.max(lineHeight, lines.lineCount * lineHeight) + 24)
    readonly property int firstVisibleLine: Math.max(1, Math.floor(codeView.contentY / lineHeight) + 1)
    readonly property int lastVisibleLine: Math.min(lines.lineCount, firstVisibleLine + Math.ceil(codeView.height / lineHeight) - 1)
    implicitHeight: 38 + viewportHeight + (lines.lineCount > 15 ? 27 : 0)
    radius: 18
    color: Design.Theme.code
    border.color: Design.Theme.border
    clip: true
    activeFocusOnTab: true
    function selectCode() {
        if (selectionManager) {
            SelectionCoordinator.activate(selectionManager.document)
            selectionManager.document.select(messageId, blockIndex, 0, messageId, blockIndex, code.length)
        } else lines.selectAll()
    }
    function clearSelection() { lines.select(lines.selectionCursor, lines.selectionCursor) }
    readonly property int selectionLength: lines.selectionLength
    function selectedContent() { return lines.selectedText }
    readonly property var caret: {
        return lines.selectionCursor >= 0 ? lines.cursorGeometry(metrics.font) : ({row: 0, x: 0})
    }
    function revealCursor() {
        const y = caret.row * lineHeight
        if (y < codeView.contentY + 12) codeView.contentY = y - 12
        else if (y + lineHeight > codeView.contentY + codeView.height - 12) codeView.contentY = y + lineHeight - codeView.height + 12
        const x = caret.x + 15
        if (x < codeView.contentX + 15) codeView.contentX = Math.max(0, x - 15)
        else if (x > codeView.contentX + codeView.width - 20) codeView.contentX = x - codeView.width + 20
    }
    onActiveFocusChanged: if (activeFocus && !selectionManager) SelectionCoordinator.activate(root)
    onSelectionLengthChanged: if (!selectionManager) SelectionCoordinator.changed(root)
    function copySelection() { if (lines.selectionLength > 0) { controller.copyText(lines.selectedText); copyTimer.restart() } }
    Shortcut { sequence: StandardKey.SelectAll; enabled: root.activeFocus; onActivated: root.selectCode() }
    Keys.onShortcutOverride: (event) => { if (root.selectionManager && event.matches(StandardKey.Copy)) event.accepted = true }
    Keys.onPressed: (event) => {
        if (root.selectionManager && event.matches(StandardKey.Copy)) {
            root.selectionManager.copySelection(); event.accepted = true; return
        }
        if (![Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key)) return
        let key = event.key
        const command = Qt.platform.os === "osx" && (event.modifiers & Qt.ControlModifier)
        if (command && key === Qt.Key_Left) key = Qt.Key_Home
        if (command && key === Qt.Key_Right) key = Qt.Key_End
        lines.moveCursor(key, (event.modifiers & Qt.ShiftModifier) !== 0,
                         (event.modifiers & (Qt.platform.os === "osx" ? Qt.AltModifier : Qt.ControlModifier)) !== 0, metrics.font)
        if (root.selectionManager) {
            SelectionCoordinator.activate(root.selectionManager.document)
            root.selectionManager.document.select(root.messageId, root.blockIndex, lines.selectionAnchor,
                                                   root.messageId, root.blockIndex, lines.selectionCursor)
        }
        revealCursor()
        event.accepted = true
    }
    Accessible.role: Accessible.Pane
    Accessible.name: (language || "文本") + " 代码块，" + lines.lineCount + " 行"
    Accessible.description: "已格式化 " + lines.formattedLineCount + " 行，当前缓存 " + lines.cachedLineCount + " 行"

    CodeLineModel {
        id: lines
        language: root.language
        code: root.code
        dark: Design.Theme.dark
        onPreparingChanged: {
            if (root.selectionManager) {
                root.selectionManager.layoutChangeStarting()
                Qt.callLater(root.selectionManager.layoutChangeFinished)
            }
        }
    }
    Text {
        visible: lines.preparing
        x: 15; y: 46
        text: "正在准备代码…"
        color: Design.Theme.muted
        font.pixelSize: 12
    }
    Design.ChatSelectionPart {
        id: selectionPart
        manager: root.selectionManager
        target: selectionMouse
        scrollView: codeView
        messageId: root.messageId
        blockIndex: root.blockIndex
        textLength: root.code.length
        positionFor: (x, y) => selectionMouse.hitPosition(x, y, true)
        rangeFor: (position, unit) => lines.rangeAt(position, unit)
        projectRange: (start, end) => lines.select(start, end)
    }
    FontMetrics { id: metrics; font.family: Design.Theme.monoFont; font.pixelSize: 13 }
    Item {
        id: codeHeader
        width: parent.width
        height: 38
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 15
            anchors.rightMargin: 8
            spacing: 6
            Design.Icon { name: "code"; implicitWidth: 15; implicitHeight: 15 }
            Text { text: root.language === "text" || root.language === "" ? "纯文本" : root.language; color: Design.Theme.secondary; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Text { visible: lines.selectionLength > 0 || copyTimer.running; text: lines.selectionLength > 0 ? "已选 " + lines.selectionLength + " 字符" : "已复制"; color: Design.Theme.secondary; font.pixelSize: 11 }
            Design.IconButton {
                iconName: "select"
                tooltip: "全选代码"
                onClicked: { root.forceActiveFocus(); root.selectCode() }
            }
            Design.IconButton {
                visible: lines.selectionLength > 0
                iconName: "copy"
                tooltip: "复制所选代码"
                onClicked: root.copySelection()
            }
            Design.IconButton {
                iconName: copyTimer.running ? "check" : "copy"
                tooltip: "复制完整代码"
                onClicked: { root.controller.copyText(root.code); copyTimer.restart() }
            }
        }
    }
    ListView {
        id: codeView
        objectName: "virtualCodeLines"
        property bool pixelWheelEnabled: true
        signal pixelScrollStarted()
        anchors.top: codeHeader.bottom
        width: parent.width
        height: root.viewportHeight
        model: lines
        clip: true
        pixelAligned: true
        reuseItems: true
        cacheBuffer: root.lineHeight * 3
        topMargin: 12
        bottomMargin: 12
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Qt.NoButton
        flickableDirection: Flickable.AutoFlickIfNeeded
        contentWidth: Math.max(width, lines.maximumColumns * metrics.averageCharacterWidth + 34)
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6; z: 3 }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 6; z: 3 }
        Design.SmoothWheel { view: codeView; stepSize: root.lineHeight * 3; outerWheel: root.outerWheel }
        Design.SmoothWheel { view: codeView; orientation: Qt.Horizontal; stepSize: 66 }
        Design.SelectionSurface {
            id: selectionMouse
            parent: codeView
            anchors.fill: parent
            z: 2
            Accessible.role: Accessible.EditableText
            Accessible.name: "代码选择区域"
            Accessible.focusable: true
            Accessible.description: "已选择 " + lines.selectionLength + " 个字符；支持跨行选择、全选与复制"
            documentPart: selectionPart
            selectionAnchor: lines.selectionAnchor
            // 代码视口整体可拖选，空格与缩进处也保持选择光标。
            function hitPosition(x, y, nearestCursor) {
                const p = codeView.mapToItem(codeView.contentItem, x, Math.max(0, Math.min(codeView.height - 1, y)))
                const rowIndex = Math.max(0, Math.min(lines.lineCount - 1, Math.floor(p.y / root.lineHeight)))
                return lines.positionAtPixel(rowIndex, Math.max(0, p.x - 15), metrics.font, nearestCursor)
            }
            positionFor: (x, y, nearest) => hitPosition(x, y, nearest)
            rangeFor: (position, unit) => lines.rangeAt(position, unit)
            applySelection: (start, end) => { if (!root.selectionManager) SelectionCoordinator.activate(root); lines.select(start, end) }
            onFocusRequested: { codeView.cancelFlick(); if (!root.selectionManager) SelectionCoordinator.activate(root); root.forceActiveFocus() }
            onContextRequested: (x, y) => codeMenu.popup(selectionMouse, x, y)
        }
        Design.SelectionAutoScroll {
            selectionItem: selectionMouse
            view: root.selectionManager && (selectionMouse.scenePointer.y < selectionMouse.mapToItem(null, 0, -24).y
                  || selectionMouse.scenePointer.y > selectionMouse.mapToItem(null, 0, selectionMouse.height + 24).y) ? null : codeView
            horizontal: true
            scenePointer: selectionMouse.scenePointer
            dragging: selectionMouse.selectingDrag && (selectionMouse.pressedButtons & Qt.LeftButton) !== 0
            onExtendRequested: (x, y) => selectionMouse.extend(x, y)
        }
        // 只读代码仅展示选区，不绘制编辑器式插入竖线；键盘定位仍用于扩选和滚动。
        // 每行都是固定高度的可回收 delegate，超出视口的行不创建富文本对象。
        component CodeRow: Item {
            id: row
            required property string highlightedLine
            required property string plainLine
            required property int lineNumber
            required property int selectionFrom
            required property int selectionTo
            readonly property string displayLine: plainLine.slice(0, 24 * 1024).replace(/\t/g, "    ")
            width: codeView.contentWidth
            height: root.lineHeight
            Rectangle {
                // 选区是轻量背景，代码行不再为选择功能创建完整 QTextDocument。
                x: 15 + metrics.advanceWidth(row.displayLine.slice(0, row.selectionFrom))
                width: metrics.advanceWidth(row.displayLine.slice(row.selectionFrom, row.selectionTo))
                height: root.lineHeight
                visible: row.selectionTo > row.selectionFrom
                color: Design.Theme.dark ? "#36557b" : "#b4d7ff"
            }
            Text {
                x: 15
                width: parent.width - 30
                height: parent.height
                text: row.highlightedLine
                textFormat: Text.StyledText
                wrapMode: Text.NoWrap
                verticalAlignment: Text.AlignVCenter
                color: Design.Theme.text
                font.family: Design.Theme.monoFont
                font.pixelSize: 13
                Accessible.role: Accessible.StaticText
                Accessible.name: "第 " + row.lineNumber + " 行：" + row.plainLine
            }
        }
        delegate: CodeRow {}
    }
    RowLayout {
        visible: lines.lineCount > 15
        anchors.top: codeView.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 15
        anchors.rightMargin: 15
        height: 27
        Text { text: root.firstVisibleLine + "–" + root.lastVisibleLine + " / " + lines.lineCount + " 行"; color: Design.Theme.muted; font.pixelSize: 10 }
        Item { Layout.fillWidth: true }
        Text { text: "在代码区域内滚动"; color: Design.Theme.muted; font.pixelSize: 10 }
    }
    Timer { id: copyTimer; interval: 1800 }
    Design.ContextMenu {
        id: codeMenu
        Design.ContextMenuItem { text: "复制所选聊天内容"; visible: root.selectionManager !== null; enabled: root.selectionManager && root.selectionManager.document.selectionLength > 0; onTriggered: root.selectionManager.copySelection() }
        Design.ContextMenuItem { text: "复制所选代码"; enabled: lines.selectionLength > 0; onTriggered: root.copySelection() }
        Design.ContextMenuItem { text: "全选代码"; onTriggered: { root.forceActiveFocus(); root.selectCode() } }
        Design.ContextMenuItem { text: "复制完整代码"; onTriggered: root.controller.copyText(root.code) }
    }
}
