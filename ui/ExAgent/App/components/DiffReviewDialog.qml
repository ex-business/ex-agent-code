pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Design.WorkbenchDialog {
    id: dialog
    required property ChatController chat
    property var anchor: ({})
    property var origin: ({})
    property string owner: ""
    property string error: ""
    property bool writing: false
    readonly property var notes: chat.reviewNotes
    function showLine(a: var) {
        if (!a.snapshot) return
        anchor = a
        origin = chat.reviewOrigin(a)
        begin(true)
    }
    function begin(edit: bool) {
        owner = chat.currentSessionId
        writing = edit
        error = ""
        input.clear()
        open()
        if (edit) input.forceActiveFocus()
    }
    function location(a: var): string {
        return a.path + "  " + (a.oldLine ? "−" + a.oldLine : "") + (a.oldLine && a.newLine ? " / " : "") + (a.newLine ? "+" + a.newLine : "")
    }
    parent: Overlay.overlay
    x: parent ? Math.round((parent.width-width)/2) : 0
    y: parent ? Math.round((parent.height-height)/2) : 0
    modal: true
    title: Design.UiText.get(writing ? "行级批注" : "待发送批注")
    Connections {
        target: dialog.chat
        function onWorkspaceChanged() {
            if (dialog.visible && (dialog.chat.previewMode || dialog.owner !== dialog.chat.currentSessionId)) dialog.close()
        }
    }
    contentItem: ColumnLayout {
        spacing: 7
        Label {
            visible: dialog.writing; Layout.fillWidth: true
            text: dialog.writing ? dialog.location(dialog.anchor) + "  ·  " + dialog.anchor.snapshot.slice(0,12) : ""
            textFormat: Text.PlainText; elide: Text.ElideMiddle; color: Design.Theme.secondary
        }
        Label {
            visible: dialog.writing; Layout.fillWidth: true
            text: dialog.writing ? dialog.anchor.source : ""
            textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Design.Theme.monoFont
        }
        Label {
            visible: dialog.writing && !!dialog.origin.messageId; Layout.fillWidth: true
            text: Design.UiText.get("匹配 Codex 文件改动") + " · " + (dialog.origin.messageId || "").slice(0,8)
            textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Design.Theme.secondary
        }
        Design.DialogInput {
            id: input
            visible: dialog.writing; Layout.fillWidth: true
            placeholderText: Design.UiText.get("输入批注"); maximumLength: 1500
            onAccepted: if (text.trim()) dialog.save()
        }
        ListView {
            id: list
            visible: !dialog.writing
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(200,contentHeight)
            clip: true; model: dialog.notes
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: note
                required property int index
                required property var modelData
                width: list.width; height: 48
                highlighted: ListView.isCurrentItem
                onClicked: list.currentIndex = index
                contentItem: Column {
                    Label { width: parent.width; text: dialog.location(note.modelData) + "  ·  " + note.modelData.snapshot.slice(0,12); textFormat: Text.PlainText; elide: Text.ElideMiddle; color: Design.Theme.secondary }
                    Label { width: parent.width; text: note.modelData.comment; textFormat: Text.PlainText; elide: Text.ElideRight }
                }
            }
        }
        Label {
            visible: !!text; Layout.fillWidth: true
            text: dialog.error || (!dialog.writing && !dialog.chat.agent.account ? Design.UiText.get("请先配置会话运行账号") : "")
            textFormat: Text.PlainText; wrapMode: Text.Wrap
        }
    }
    function save() {
        if (chat.saveReviewNote(owner,anchor,input.text)) close()
        else error = chat.errorMessage || Design.UiText.get("批注未保存，请检查工作区或批注数量。")
    }
    footer: Design.WorkbenchDialogActions {
        primaryText: Design.UiText.get(dialog.writing ? "保存批注" : "发送给 Codex")
        primaryEnabled: dialog.writing ? !!input.text.trim() : list.currentIndex >= 0 && list.currentIndex < dialog.notes.length && !!dialog.chat.agent.account && !dialog.chat.agent.busy && !dialog.chat.agent.authenticating
        onPrimaryRequested: {
            if (dialog.writing) dialog.save()
            else if (dialog.chat.sendReviewNote(dialog.notes[list.currentIndex].id)) dialog.close()
            else dialog.error = dialog.chat.errorMessage || Design.UiText.get("批注尚未派发")
        }
        onCancelled: dialog.reject()
    }
}
