pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import ExAgent.Chat
import ExAgent.App as Design

WorkbenchDialog {
    id: root
    title: "终端设置"
    darkSurface: true
    readonly property color bodyColor: Design.Theme.dark ? Design.Theme.tokens.panelText : "#e3e8ef"
    readonly property color fieldColor: Design.Theme.dark ? Design.Theme.tokens.panelBase : "#171c23"
    readonly property color secondaryColor: Design.Theme.dark ? Design.Theme.tokens.panelPlaceholderText : "#a2abb8"
    modal: true
    width: Math.min(480, parent ? parent.width - 32 : 480)
    anchors.centerIn: parent
    standardButtons: Dialog.Cancel
    footer: Control {
        padding: 20; topPadding: 8; bottomPadding: 16
        background: Item {}
        contentItem: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            DialogAction { text: "取消"; onClicked: root.reject() }
            DialogAction {
                text: "保存"; primary: true
                onClicked: {
                    const error=TerminalPreferences.save(shell.text,args.text,commands.text)
                    if (error) feedback.text=error
                    else root.accept()
                }
            }
    }
    }
    onOpened: {
        shell.text=TerminalPreferences.values.shell
        args.text=TerminalPreferences.values.arguments
        commands.text=TerminalPreferences.values.commands
        feedback.text=""
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label { text: "Shell 可执行文件" }
        DialogInput { id: shell; Layout.fillWidth: true; placeholderText: "/bin/zsh" }
        Label { text: "启动参数 · 每行一个，应用于新终端" }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: 64
            TextArea { id: args; textFormat: TextEdit.PlainText; wrapMode: TextEdit.NoWrap; selectByMouse: true; font.family: Design.Theme.monoFont; font.pixelSize: 12; color: root.bodyColor; background: Rectangle { color: root.fieldColor; radius: 6 } }
        }
        Label { text: "快速命令 · 每行一条，选择后填入终端" }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: 100
            TextArea { id: commands; textFormat: TextEdit.PlainText; wrapMode: TextEdit.NoWrap; selectByMouse: true; font.family: Design.Theme.monoFont; font.pixelSize: 12; color: root.bodyColor; background: Rectangle { color: root.fieldColor; radius: 6 } }
        }
        RowLayout {
            DialogSelect { id: format; model: ["Ghostty", "Warp YAML"]; Layout.fillWidth: true }
            DialogAction { text: "导入主题"; onClicked: picker.open() }
            DialogAction { text: "恢复默认"; onClicked: TerminalPreferences.resetTheme() }
        }
        Label { id: feedback; Layout.fillWidth: true; visible: !!text; wrapMode: Text.Wrap; color: root.secondaryColor }
        Label { text: "⌘C / ⌘V 复制粘贴 · ⌘← / → 行首行尾 · ⌥← / → 按词移动"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.secondaryColor; font.pixelSize: 12 }
    }
    FileDialog {
        id: picker
        title: "导入终端主题"
        onAccepted: feedback.text=TerminalPreferences.importTheme(selectedFile,format.currentIndex) || "主题已应用"
    }
}
