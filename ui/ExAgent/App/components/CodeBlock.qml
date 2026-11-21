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
    function clearSelection() { lines.select(lines.selectionCursor, lines.selectionCursor) }
    readonly property int selectionLength: lines.selectionLength
    function selectedContent() { return lines.selectedText }
    onActiveFocusChanged: if (activeFocus) SelectionCoordinator.activate(root)
    onSelectionLengthChanged: SelectionCoordinator.changed(root)
    function copySelection() { if (lines.selectionLength > 0) { controller.copyText(lines.selectedText); copyTimer.restart() } }
    Shortcut { sequence: StandardKey.SelectAll; enabled: root.activeFocus; onActivated: lines.selectAll() }
    Keys.onPressed: (event) => {
        if (![Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key)) return
        let key = event.key
        const command = Qt.platform.os === "osx" && (event.modifiers & Qt.ControlModifier)
        if (command && key === Qt.Key_Left) key = Qt.Key_Home
        if (command && key === Qt.Key_Right) key = Qt.Key_End
        lines.moveCursor(key, (event.modifiers & Qt.ShiftModifier) !== 0,
                         (event.modifiers & (Qt.platform.os === "osx" ? Qt.AltModifier : Qt.ControlModifier)) !== 0)
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
                onClicked: { root.forceActiveFocus(); lines.selectAll() }
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
            selectionAnchor: lines.selectionAnchor
            function hitPosition(x, y, nearestCursor) {
                const p = codeView.mapToItem(codeView.contentItem, x, Math.max(0, Math.min(codeView.height - 1, y)))
                const rowIndex = Math.max(0, Math.min(lines.lineCount - 1, Math.floor(p.y / root.lineHeight)))
                return lines.positionAtPixel(rowIndex, Math.max(0, p.x - 15), metrics.font, nearestCursor)
            }
            positionFor: (x, y, nearest) => hitPosition(x, y, nearest)
            rangeFor: (position, unit) => lines.rangeAt(position, unit)
            applySelection: (start, end) => { SelectionCoordinator.activate(root); lines.select(start, end) }
            onFocusRequested: { SelectionCoordinator.activate(root); root.forceActiveFocus() }
            onContextRequested: (x, y) => codeMenu.popup(selectionMouse, x, y)
            // 拖出视口时继续选择原始文本，回收行对象不会丢失跨行选区。
            Timer {
                interval: 30
                repeat: true
                running: (selectionMouse.pressedButtons & Qt.LeftButton) !== 0 && (selectionMouse.mouseY < 16 || selectionMouse.mouseY > codeView.height - 16)
                onTriggered: {
                    const direction = selectionMouse.mouseY < 16 ? -1 : 1
                    codeView.contentY = Math.max(codeView.originY - codeView.topMargin,
                        Math.min(codeView.originY + codeView.contentHeight - codeView.height + codeView.bottomMargin, codeView.contentY + direction * 12))
                    selectionMouse.extend(selectionMouse.mouseX, selectionMouse.mouseY)
                }
            }
        }
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
        Design.ContextMenuItem { text: "复制所选代码"; enabled: lines.selectionLength > 0; onTriggered: root.copySelection() }
        Design.ContextMenuItem { text: "全选代码"; onTriggered: { root.forceActiveFocus(); lines.selectAll() } }
        Design.ContextMenuItem { text: "复制完整代码"; onTriggered: root.controller.copyText(root.code) }
    }
}
