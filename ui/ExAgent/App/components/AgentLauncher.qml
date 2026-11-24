import QtQuick
import "AgentProfiles.js" as Catalog
import QtQuick.Controls
import QtQuick.Layouts
import QtCore as Core

WorkbenchDialog {
    darkSurface: true
    id: root
    required property var backend
    required property string workspace
    title: "启动终端 Agent"
    modal: true
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - 32, 480) : 480
    readonly property var profiles: Catalog.entries
    property string resolved
    property string feedback
    Core.Settings { id: prefs; category: "TerminalAgentLauncher"; property var commands: ({}); property var argumentsByAgent: ({}) }
    function loadProfile() {
        const profile = profiles[picker.currentIndex]
        executable.text = prefs.commands[profile[0]] ?? profile[1]
        args.text = prefs.argumentsByAgent[profile[0]] ?? profile[2]
        environment.clear()
        feedback = ""
        probe()
    }
    function probe() {
        resolved = backend.agentExecutable(executable.text)
        if (picker.currentText === "Claude Agent Teams" && !backend.agentExecutable("claude")) {
            resolved = ""
            feedback = "Claude Agent Teams 还需要 Claude CLI。"
        } else {
            feedback = ""
        }
    }
    onOpened: loadProfile()
    onClosed: environment.clear()
    contentItem: ColumnLayout {
        DialogSelect {
            id: picker
            Layout.fillWidth: true
            model: root.profiles.map(p => p[0])
            onActivated: root.loadProfile()
        }
        DialogInput {
            id: executable
            Layout.fillWidth: true
            placeholderText: "可执行文件名或绝对路径"
            onTextEdited: root.probe()
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            text: root.resolved || "未找到可执行文件"
        }
        TextArea { id: args; Layout.fillWidth: true; placeholderText: "启动参数，每行一个；路径可含空格" }
        TextArea { id: environment; Layout.fillWidth: true; placeholderText: "本次启动的环境变量，每行 NAME=value" }
        Label { visible: !!root.feedback; text: root.feedback; Layout.fillWidth: true; wrapMode: Text.Wrap }
        RowLayout {
            DialogAction { text: "重新探测"; onClicked: root.probe() }
            DialogAction {
                text: "启动"; enabled: !!root.resolved
                onClicked: {
                    const label = picker.currentText
                    root.feedback = root.backend.launchAgent(label, root.workspace, executable.text, args.text, environment.text)
                    if (root.feedback) return
                    prefs.commands = Object.assign({}, prefs.commands, { [label]: executable.text })
                    prefs.argumentsByAgent = Object.assign({}, prefs.argumentsByAgent, { [label]: args.text })
                    root.close()
                }
            }
        }
    }
}
