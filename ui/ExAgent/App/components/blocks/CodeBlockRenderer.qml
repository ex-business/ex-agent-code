pragma ComponentBehavior: Bound
import QtQuick
import "../ShellPresentation.js" as Shell
import QtQuick.Controls
import ExAgent.Chat
import ExAgent.App as Design

Design.BlockRendererContent {
    id: root
    readonly property string script: root.block.shell ? MarkdownFormatter.pythonPreview(root.block.command || "") : ""
    // 三种视图共用一份按行渲染的代码控件，切换会话时回到脚本页。
    property int page: 0
    onDetachedChanged: if (root.detached) page = Number(root.block.initialPage || 0)
    onMessageIdChanged: page = 0
    readonly property bool shellPanel: root.block.shell === true
    readonly property bool commandOnly: shellPanel && !script && !root.block.text
    readonly property bool outputPage: !shellPanel || (script ? page === 1 : page === 0 && !commandOnly)
    readonly property bool commandPage: shellPanel && !script && (page === 1 || commandOnly)
    HoverHandler { id: panelHover; blocking: false }
    implicitHeight: code.implicitHeight
    Design.CodeBlock {
        id: code
        sourceBlock: root.block; detached: root.detached; initialPage: root.page
        width: parent.width
        shellOutput: root.shellPanel && root.outputPage
        command: root.block.command || ""
        commandStatus: Number(root.block.messageStatus || 0)
        knownLineCount: root.outputPage ? root.block.codeLineCount ?? -1 : -1
        code: root.outputPage ? root.block.text || "" : root.commandPage ? Shell.readable(root.block.command || "") : root.page === 0 ? root.script : root.block.command
        language: root.script ? ["python", "terminal-output", "shell"][root.page]
                              : root.shellPanel ? (root.outputPage ? "terminal-output" : "shell")
                              : root.block.language || (root.block.data || {}).language || (root.block.rendererKind === 7 ? "diff" : "text")
        controller: root.controller
        messageId: root.messageId
        blockIndex: root.blockIndex
        outerWheel: root.outputPage ? root.outerWheel : null
        headerContent: root.shellPanel ? scriptHeader : null
    }
    Component {
        id: scriptHeader
        Item {
            id: toolbar
            readonly property bool compact: width < 320
            // 方向键只在标签间切换，Tab 按工具栏的视觉顺序移动。
            function selectTab(index: int) {
                const tab = tabs.itemAt((index + tabs.count) % tabs.count)
                if (tab) { root.page = tab.index; tab.forceActiveFocus(Qt.TabFocusReason) }
            }
            Row {
                id: tabRow
                x: 10; height: parent.height
                Repeater {
                    id: tabs
                    model: root.script ? ["Python", "输出", "原始命令"] : root.commandOnly ? ["命令"] : ["输出", "命令"]
                    delegate: ToolButton {
                        required property int index
                        required property string modelData
                        id: tab
                        height: parent.height
                        leftPadding: toolbar.compact ? 5 : 9; rightPadding: leftPadding
                        KeyNavigation.tab: index + 1 < tabs.count ? tabs.itemAt(index + 1) : copyPage
                        Keys.onLeftPressed: toolbar.selectTab(index - 1)
                        Keys.onRightPressed: toolbar.selectTab(index + 1)
                        text: modelData
                        Accessible.role: Accessible.PageTab
                        Accessible.selected: root.page === index
                        Accessible.onPressAction: root.page = index
                        onClicked: root.page = index
                        contentItem: Text {
                            text: tab.text; verticalAlignment: Text.AlignVCenter
                            font.family: Design.Theme.font; font.pixelSize: 12
                            font.weight: root.page === tab.index ? Font.DemiBold : Font.Normal
                            color: root.page === tab.index || tab.hovered ? Design.Theme.text : Design.Theme.secondary
                        }
                        background: Item {
                            Rectangle {
                                anchors.fill: parent; anchors.margins: 2
                                radius: 5; color: "transparent"
                                border.color: tab.visualFocus ? Design.Theme.secondary : "transparent"
                            }
                            Rectangle {
                                visible: root.page === tab.index
                                anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                                width: tab.contentItem.implicitWidth; height: 2; radius: 1
                                color: Design.Theme.text
                            }
                        }
                    }
                }
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter; spacing: 6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, Math.min(implicitWidth, toolbar.width - tabRow.x - tabRow.width - copyPage.width - 20))
                    elide: Text.ElideRight
                    text: ["✓ 成功", "执行中", "执行失败", "已取消"][code.commandStatus] || ""
                    color: code.commandStatus === 2 ? Design.Theme.danger : Design.Theme.secondary
                    font.pixelSize: 11
                }
                Design.IconButton {
                    // 窄栏隐藏时归还整段按钮宽度；保留 Tab 焦点，使键盘进入后恢复点击区域。
                    id: copyPage
                    KeyNavigation.tab: code.windowAction
                    KeyNavigation.backtab: tabs.itemAt(tabs.count - 1)
                    focusPolicy: Qt.StrongFocus
                    readonly property bool revealed: panelHover.hovered || activeFocus || copied.running
                    width: toolbar.compact && !revealed ? 0 : implicitWidth
                    opacity: revealed ? 1 : 0
                    iconName: copied.running ? "check" : "copy"
                    tooltip: root.commandPage ? "复制原始命令" : "复制当前页"
                    onClicked: { root.controller.copyText(root.commandPage ? root.block.command : code.code); copied.restart() }
                    Timer { id: copied; interval: 1200 }
                }
            }
        }
    }
}
