pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

ColumnLayout {
    id: bar
    required property GitChanges changes
    required property string folder
    readonly property bool busy: actions.busy
    spacing: 4
    function stageFile(path: string, staged: bool) {
        actions.stageFile(changes.branch, path, staged)
    }
    onFolderChanged: {
        if (dialog) dialog.close()
        if (summary) summary.clear()
    }
    GitActions {
        id: actions
        folder: bar.folder
        onRefreshed: bar.changes.refreshAfterWrite()
        onReviewReady: { dialog.open(); summary.forceActiveFocus() }
        onCommitted: { dialog.close(); summary.clear() }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Design.Theme.border }
    Label {
        Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
        visible: !!actions.notice
        text: Design.UiText.get(actions.notice)
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: actions.state === GitActions.Failed ? Design.Theme.danger : Design.Theme.secondary
    }
    ScrollView {
        visible: !!actions.detail
        Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
        Layout.preferredHeight: Math.min(84, resultText.implicitHeight + 3)
        clip: true
        TextArea {
            id: resultText
            text: actions.detail
            textFormat: TextEdit.PlainText
            readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
            font.family: Design.Theme.monoFont; font.pixelSize: 11
            color: Design.Theme.secondary
            background: null
        }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 8
        Label {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            text: bar.changes.branch ? "origin/" + bar.changes.branch : Design.UiText.get("无可推送分支")
            Accessible.name: Design.UiText.get("推送目标") + " " + text
            textFormat: Text.PlainText; elide: Text.ElideMiddle
            color: Design.Theme.secondary; font.pixelSize: 12
        }
        Design.DialogAction {
            text: Design.UiText.get("提交…")
            enabled: !!bar.changes.branch && !bar.busy
            onClicked: actions.reviewCommit(bar.changes.branch)
        }
        Design.DialogAction {
            text: Design.UiText.get("推送")
            enabled: !!bar.changes.branch && !bar.busy
            onClicked: actions.push(bar.changes.branch)
        }
    }

    Design.WorkbenchDialog {
        id: dialog
        parent: Overlay.overlay
        modal: true
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        title: Design.UiText.get("提交已暂存的变更")
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: bar.changes.branch + " · " + Design.UiText.get("全仓库已暂存") + " (" + actions.staged.length + ")"
                textFormat: Text.PlainText; elide: Text.ElideMiddle; Layout.fillWidth: true
                color: Design.Theme.secondary
            }
            ListView {
                id: stagedList
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(150, actions.staged.length * 25)
                clip: true; reuseItems: true
                model: actions.staged
                delegate: Label {
                    required property string modelData
                    width: stagedList.width; height: 25
                    text: modelData; textFormat: Text.PlainText; elide: Text.ElideMiddle
                    font.family: Design.Theme.monoFont
                }
                ScrollBar.vertical: ScrollBar {}
            }
            Design.DialogInput {
                id: summary
                Layout.fillWidth: true
                placeholderText: Design.UiText.get("提交说明")
                maximumLength: 4096
                enabled: !bar.busy
            }
            Label {
                visible: actions.state === GitActions.Failed
                Layout.fillWidth: true
                text: Design.UiText.get(actions.notice)
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: Design.Theme.danger
            }
            ScrollView {
                visible: actions.state === GitActions.Failed && !!actions.detail
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(92, errorText.implicitHeight + 3)
                clip: true
                TextArea {
                    id: errorText
                    text: actions.detail; textFormat: TextEdit.PlainText
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                    font.family: Design.Theme.monoFont; font.pixelSize: 11
                }
            }
        }
        footer: Design.WorkbenchDialogActions {
            primaryText: Design.UiText.get("提交")
            primaryEnabled: !bar.busy && !!summary.text.trim()
            onPrimaryRequested: actions.commit(summary.text)
            onCancelled: dialog.reject()
        }
        Shortcut {
            sequence: "Ctrl+Return"
            enabled: dialog.visible && !bar.busy && !!summary.text.trim()
            onActivated: actions.commit(summary.text)
        }
    }
}
