pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore as Core
import ExAgent.App as Design
import ExAgent.Chat

Window {
    id: root
    readonly property var sleepingWorkspaces: consoleModel.sleepingWorkspaces
    readonly property var navigationTabs: consoleModel.tabHeaders
    readonly property var activityRows: consoleModel.terminalActivity
    function openActivityTab(id: string) {
        const i = consoleModel.tabHeaders.findIndex(tab => tab.id === id)
        if (i < 0) return
        selectTab(i)
        if (floating.visible && floatingId === id) { floating.raise(); floating.requestActivate(); return }
        showConsole()
        if (consoleModel.ownsInput && primary.supported) {
            interactive.checked = true
            Qt.callLater(() => primary.focusTerminal())
        }
    }
    required property string workspace
    signal returnRequested(string conversation)
    required property AgentController agent
    title: "Agent 历史与终端"
    color: "#11151b"
    width: 960; height: 630
    minimumWidth: 610; minimumHeight: 390
    property bool historyOpen: false
    SessionConsole { id: consoleModel }
    AgentLauncher { id: launcher; backend: consoleModel; workspace: root.workspace }
    AgentActivityDialog { id: activityDialog; backend: consoleModel; workspace: root.workspace }
    readonly property string selectedId: consoleModel.current.id || ""
    property string pinnedId: ""
    property string floatingId: ""
    function selectTab(index) {
        const id = consoleModel.tabHeaders[index].id
        if (id === pinnedId) pinnedId = selectedId
        consoleModel.select(index)
    }
    function splitTerminal() {
        const original = selectedId
        consoleModel.shell(consoleModel.current.cwd || root.workspace)
        if (selectedId !== original) {
            pinnedId = original
            interactive.checked = true
            Qt.callLater(() => primary.focusTerminal())
        }
    }
    Window {
        id: floating
        title: "浮动终端 · " + root.floatingId.slice(-8)
        width: 720; height: 440; minimumWidth: 260; minimumHeight: 160
        onClosing: root.floatingId = ""
        TerminalPane {
            anchors.fill: parent
            visible: floating.visible
            backend: consoleModel
            terminalId: root.floatingId
            caption: "关闭窗口可返回终端页签"
        }
    }
    Core.Settings {
        id: prefs
        category: "SessionConsoleWindow"
        property real historyWidth: 210
        property bool reopen: false
        property bool focusInput: true
    }
    function showConsole() {
        show()
        raise()
        requestActivate()
    }
    function restoreWindow() { if (prefs.reopen) show() }
    function saveLayout() {
        if (visible && leftPane.width > 0) prefs.historyWidth = leftPane.width
        prefs.focusInput = input.activeFocus
        prefs.reopen = visible
        prefs.sync()
    }
    onClosing: { saveLayout(); prefs.reopen = false; prefs.sync() }
    onVisibleChanged: {
        consoleModel.setConsoleVisible(visible)
        if (!visible) { floating.close(); return }
        prefs.reopen = true
        consoleModel.scan(scope.checked ? "" : workspace)
        consoleModel.poll()
        Qt.callLater(() => {
            leftPane.SplitView.preferredWidth = prefs.historyWidth
            if (prefs.focusInput) input.forceActiveFocus()
            else output.forceActiveFocus()
        })
    }
    onWorkspaceChanged: if (visible && !scope.checked) consoleModel.scan(workspace)
    Timer { interval: 700; running: root.visible; repeat: true; onTriggered: consoleModel.poll(!primary.visible) }
    SplitView {
        id: split
        anchors.fill: parent
        anchors.margins: 14
        handle: Rectangle { implicitWidth: 12; color: "transparent"; Rectangle { anchors.centerIn: parent; width: 1; height: parent.height; color: "#313a45" } }
        ColumnLayout {
            id: leftPane
            visible: root.historyOpen
            SplitView.preferredWidth: prefs.historyWidth
            SplitView.minimumWidth: 150
            spacing: 7
            RowLayout {
                Layout.fillWidth: true
                Label { text: "会话历史"; font.pixelSize: 16; font.bold: true; color: "#e8edf4"; Layout.fillWidth: true }
                Design.IconButton { iconName: "refresh"; tooltip: "刷新会话历史"; onClicked: consoleModel.scan(scope.checked ? "" : root.workspace) }
            }
            TerminalAction { darkChrome: true; id: scope; glyph: "folder"; checkable: true; text: checked ? "全部工作区" : "当前工作区"; onClicked: consoleModel.scan(checked ? "" : root.workspace) }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; model: consoleModel.history
                Label { visible: parent.count === 0; anchors.top: parent.top; anchors.topMargin: 16; text: "暂无可续接的会话"; color: "#8996a6"; font.pixelSize: 12 }
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    contentItem: Text { text: row.text; color: row.enabled ? "#d5deea" : "#8996a6"; elide: Text.ElideMiddle; font.pixelSize: 12 }
                    background: Rectangle { radius: 6; color: row.hovered ? "#29313c" : "transparent" }
                    text: modelData.agent + " · " + modelData.id.slice(0,8) + "\n" + modelData.cwd
                    enabled: !!modelData.program
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.program ? "在原目录续接此会话" : "尚未安装该 CLI"
                    onClicked: consoleModel.resume(index)
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#313a45" }
            TerminalAction { darkChrome: true; glyph: "activity"; text: "Agent 活动与仓库记忆"; enabled: !!root.workspace; onClicked: activityDialog.open() }
            TerminalAction { darkChrome: true; glyph: "clock"; checkable: true;
                text: "后台 Pi 自动休眠"
                checked: consoleModel.hibernationEnabled
                onToggled: consoleModel.configureHibernation(checked,consoleModel.idleMinutes)
            }
            RowLayout {
                visible: consoleModel.hibernationEnabled
                Label { text: "空闲分钟"; color: "#bec8d6" }
                SpinBox {
                    palette.text: "#e8edf4"
                    palette.buttonText: "#e8edf4"
                    palette.base: "#171c23"
                    palette.button: "#303843"
                    palette.mid: "#424c59"
                    background: Rectangle { radius: 6; color: "#171c23"; border.color: "#424c59" }
                    from: 1; to: 1440; editable: true; value: consoleModel.idleMinutes
                    onValueModified: consoleModel.configureHibernation(true,value)
                }
            }
        }
        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                TerminalAction { darkChrome: true; glyph: "sidebar"; text: "历史"; checked: root.historyOpen; onClicked: root.historyOpen = !root.historyOpen }
                Rectangle { implicitWidth: 1; implicitHeight: 16; color: "#313a45" }
                Label { text: "终端"; font.bold: true; font.pixelSize: 18; color: "#e8edf4"; Layout.fillWidth: true }
                TerminalAction { darkChrome: true; glyph: "activity"; text: "启动 Agent"; onClicked: launcher.open() }
                TerminalAction { darkChrome: true; glyph: "plus"; primary: true; text: Design.UiText.get("新终端"); onClicked: consoleModel.shell(root.workspace) }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#2b333f" }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
            ListView {
                id: tabs
                Layout.fillWidth: true; Layout.preferredHeight: 30
                orientation: ListView.Horizontal; spacing: 2; clip: true
                model: consoleModel.tabHeaders
                currentIndex: consoleModel.selected
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex,ListView.Contain)
                delegate: TerminalAction { darkChrome: true;
                    glyph: "terminal"
                    required property int index
                    required property var modelData
                    height: tabs.height
                    text: modelData.title + " · " + (index + 1)
                    checked: index === consoleModel.selected
                    onClicked: root.selectTab(index)
                }
            }
                TerminalAction { darkChrome: true; glyph: "chat"; visible: enabled
                    text: "续接当前聊天"
                    enabled: { const active = root.agent.busy; const account = root.agent.account; return !active && account > 0 && !!root.agent.terminalBinding(root.workspace).threadId }
                    onClicked: consoleModel.continueCodex(root.agent.terminalBinding(root.workspace))
                    ToolTip.visible: hovered
                    ToolTip.text: "退出终端中的 Codex 后，即可回到原聊天继续发送"
                }
                TerminalAction { darkChrome: true; glyph: "back"
                    visible: !!consoleModel.current.conversationId
                    enabled: consoleModel.current.state === 0
                    text: "返回原生聊天"
                    onClicked: root.returnRequested(consoleModel.current.conversationId)
                    ToolTip.visible: hovered
                    ToolTip.text: "在终端输入 /exit 退出后返回"
                }
                TerminalAction { darkChrome: true; glyph: "terminal"; checkable: true;
                    id: interactive
                    visible: primary.supported && consoleModel.ownsInput
                    enabled: consoleModel.current.state === 1
                    text: checked ? "交互" : "记录"
                    onToggled: if (checked) Qt.callLater(() => primary.focusTerminal())
                }
                TerminalAction { darkChrome: true; glyph: "sidebar"
                    text: root.pinnedId ? "合并视图" : "分屏"
                    visible: interactive.visible
                    enabled: root.pinnedId || (interactive.enabled && root.selectedId !== root.floatingId)
                    onClicked: { if (root.pinnedId) root.pinnedId = ""; else root.splitTerminal() }
                }
                TerminalAction { darkChrome: true; glyph: "expand"
                    text: root.floatingId ? "收回终端" : "浮动"
                    visible: interactive.visible
                    enabled: !!root.floatingId || interactive.enabled
                    onClicked: {
                        if (root.floatingId) { floating.close(); return }
                        interactive.checked = true
                        root.floatingId = root.selectedId
                        floating.show(); floating.raise(); floating.requestActivate()
                    }
                }
                TerminalAction { darkChrome: true; glyph: "search"; text: "搜索回滚"; visible: !primary.visible; enabled: !!root.selectedId; onClicked: savedHistory.open() }
            }
            TerminalHistoryDialog { id: savedHistory; backend: consoleModel; terminalId: root.selectedId; parent: Overlay.overlay }
            SplitView {
                Layout.fillWidth: true; Layout.fillHeight: true
                handle: Rectangle { implicitWidth: 8; color: "transparent" }
                id: paneSplit
                visible: root.visible && interactive.checked && interactive.visible && ((interactive.enabled && root.selectedId !== root.floatingId) || (!!root.pinnedId && root.pinnedId !== root.floatingId))
                TerminalPane {
                    id: primary
                    SplitView.fillWidth: true; SplitView.minimumWidth: 120
                    visible: parent && root.visible && interactive.checked && interactive.visible && interactive.enabled && root.selectedId !== root.floatingId
                    terminalId: root.selectedId; backend: consoleModel; caption: consoleModel.current.title || ""
                }
                TerminalPane {
                    SplitView.preferredWidth: Math.max(150, paneSplit.width * 0.48); SplitView.minimumWidth: 120
                    visible: !!root.pinnedId && root.pinnedId !== root.selectedId && root.pinnedId !== root.floatingId
                    terminalId: root.pinnedId; backend: consoleModel; caption: { const i = consoleModel.tabHeaders.findIndex(tab => tab.id === root.pinnedId); return i < 0 ? "" : consoleModel.tabHeaders[i].title + " · " + (i + 1) }
                }
            }
            ScrollView {
                visible: !paneSplit.visible
                Layout.fillWidth: true; Layout.fillHeight: true
                TextArea {
                    id: output
                    color: "#dbe4ef"; padding: 12; background: Rectangle { radius: 8; color: "#191d24" }
                    text: consoleModel.current.text || ""
                    readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText
                    font.family: "Menlo"; font.pixelSize: 12
                    onTextChanged: cursorPosition = length
                }
            }
            Label {
                Layout.fillWidth: true; elide: Text.ElideMiddle; color: "#9ba7b5"; font.pixelSize: 11
                text: consoleModel.selected < 0 ? "选择历史会话，或新建终端"
                    : (consoleModel.current.state === 1 ? "运行中 · PID " + consoleModel.current.pid
                    : consoleModel.current.state === SessionConsole.Sleeping ? "已休眠 · 已保存画面"
                    : consoleModel.current.state === 0 ? "后端不可用 · 已保存画面" : "正在核对后端") + "　" + consoleModel.current.cwd
            }
            Label { visible: !!text; text: consoleModel.error; Layout.fillWidth: true; wrapMode: Text.Wrap }
            TextField {
                id: input
                color: "#e4eaf2"; placeholderTextColor: "#8d9baa"
                background: Rectangle { radius: 7; color: "#202732"; border.color: input.activeFocus ? "#7c96ad" : "#313a45" }
                visible: !paneSplit.visible
                readonly property string terminalId: consoleModel.current.id || ""
                onTerminalIdChanged: text = consoleModel.draft(terminalId)
                onTextEdited: consoleModel.setDraft(terminalId, text)
                Layout.fillWidth: true; enabled: consoleModel.current.state === 1 && root.selectedId !== root.floatingId
                placeholderText: "输入命令或消息，按回车发送"
                Keys.onPressed: event => {
                    if (text.length === 0 && [Qt.Key_Up, Qt.Key_Down, Qt.Key_Escape].includes(event.key)) {
                        consoleModel.sendNavigationKey(event.key)
                        event.accepted = true
                    }
                }
                onAccepted: {
                    if (text.length === 0) consoleModel.sendNavigationKey(Qt.Key_Return)
                    else { consoleModel.send(text); consoleModel.setDraft(terminalId, ""); clear() }
                }
            }
        }
    }
}
