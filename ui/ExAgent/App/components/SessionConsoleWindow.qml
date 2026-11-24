pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore as Core
import ExAgent.Chat

Window {
    id: root
    readonly property var sleepingWorkspaces: consoleModel.sleepingWorkspaces
    required property string workspace
    signal returnRequested(string conversation)
    required property AgentController agent
    title: "Agent 历史与终端"
    width: 960; height: 630
    minimumWidth: 610; minimumHeight: 390
    SessionConsole { id: consoleModel }
    AgentLauncher { id: launcher; backend: consoleModel; workspace: root.workspace }
    AgentActivityDialog { id: activityDialog; backend: consoleModel; workspace: root.workspace }
    Core.Settings {
        id: prefs
        category: "SessionConsoleWindow"
        property real historyWidth: 294
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
        if (!visible) return
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
    Timer { interval: 700; running: root.visible; repeat: true; onTriggered: consoleModel.poll(!surface.visible) }
    SplitView {
        id: split
        anchors.fill: parent
        ColumnLayout {
            id: leftPane
            SplitView.preferredWidth: prefs.historyWidth
            SplitView.minimumWidth: 150
            RowLayout {
                CheckBox { id: scope; text: "全部工作区"; onClicked: consoleModel.scan(checked ? "" : root.workspace) }
                Button { text: "刷新"; onClicked: consoleModel.scan(scope.checked ? "" : root.workspace) }
            }
            Button { text: "Agent 活动与仓库记忆"; enabled: !!root.workspace; onClicked: activityDialog.open() }
            CheckBox {
                text: "自动休眠后台 Pi 终端"
                checked: consoleModel.hibernationEnabled
                onToggled: consoleModel.configureHibernation(checked,consoleModel.idleMinutes)
            }
            RowLayout {
                visible: consoleModel.hibernationEnabled
                Label { text: "空闲分钟" }
                SpinBox {
                    from: 1; to: 1440; editable: true; value: consoleModel.idleMinutes
                    onValueModified: consoleModel.configureHibernation(true,value)
                }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; model: consoleModel.history
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    text: modelData.agent + " · " + modelData.id.slice(0,8) + "\n" + modelData.cwd
                    enabled: !!modelData.program
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.program ? "在原目录续接此会话" : "尚未安装该 CLI"
                    onClicked: consoleModel.resume(index)
                }
            }
        }
        ColumnLayout {
            SplitView.fillWidth: true
            RowLayout {
                ComboBox {
                    id: tabs
                    Layout.fillWidth: true
                    model: consoleModel.tabs
                    textRole: "title"
                    displayText: (consoleModel.current.title || "") + (consoleModel.current.state === SessionConsole.Sleeping ? " · 休眠" : "")
                    currentIndex: consoleModel.selected
                    onActivated: index => consoleModel.select(index)
                }
                Button { text: "启动 Agent"; onClicked: launcher.open() }
                Button {
                    text: "续接当前聊天"
                    enabled: { const active = root.agent.busy; const account = root.agent.account; return !active && account > 0 && !!root.agent.terminalBinding(root.workspace).threadId }
                    onClicked: consoleModel.continueCodex(root.agent.terminalBinding(root.workspace))
                    ToolTip.visible: hovered
                    ToolTip.text: "退出终端中的 Codex 后，即可回到原聊天继续发送"
                }
                Button {
                    visible: !!consoleModel.current.conversationId
                    enabled: consoleModel.current.state === 0
                    text: "返回原生聊天"
                    onClicked: root.returnRequested(consoleModel.current.conversationId)
                    ToolTip.visible: hovered
                    ToolTip.text: "在终端输入 /exit 退出后返回"
                }
                Button { text: "新终端"; onClicked: consoleModel.shell(root.workspace) }
            }
            Label {
                Layout.fillWidth: true; elide: Text.ElideMiddle
                text: consoleModel.selected < 0 ? "选择历史会话，或新建终端"
                    : (consoleModel.current.state === 1 ? "运行中 · PID " + consoleModel.current.pid
                    : consoleModel.current.state === SessionConsole.Sleeping ? "已休眠 · 已保存画面"
                    : consoleModel.current.state === 0 ? "后端不可用 · 已保存画面" : "正在核对后端") + "　" + consoleModel.current.cwd
            }
            RowLayout {
                CheckBox {
                    id: interactive
                    visible: surface.supported && consoleModel.ownsInput
                    enabled: consoleModel.current.state === 1
                    text: "交互终端"
                    onToggled: if (checked) Qt.callLater(() => surface.forceActiveFocus())
                }
                Label { visible: surface.visible; text: surface.error; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
            TerminalSurface {
                id: surface
                ToolTip { id: pasteNotice; timeout: 3000 }
                onPasteRejected: reason => pasteNotice.show(reason)
                Layout.fillWidth: true; Layout.fillHeight: true
                visible: root.visible && interactive.visible && interactive.checked && interactive.enabled
                target: visible ? consoleModel.current.id || "" : ""
            }
            ScrollView {
                visible: !surface.visible
                Layout.fillWidth: true; Layout.fillHeight: true
                TextArea {
                    id: output
                    text: consoleModel.current.text || ""
                    readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText
                    font.family: "Menlo"; font.pixelSize: 12
                    onTextChanged: cursorPosition = length
                }
            }
            Label { visible: !!text; text: consoleModel.error; Layout.fillWidth: true; wrapMode: Text.Wrap }
            TextField {
                id: input
                visible: !surface.visible
                readonly property string terminalId: consoleModel.current.id || ""
                onTerminalIdChanged: text = consoleModel.draft(terminalId)
                onTextEdited: consoleModel.setDraft(terminalId, text)
                Layout.fillWidth: true; enabled: consoleModel.current.state === 1
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
