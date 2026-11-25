pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

FocusScope {
    id: root
    property bool writable: false
    required property string folder
    signal revealRequested(string path)
    signal openRequested(string path)
    readonly property string selectedPath: git.selection.slice(git.selection.indexOf(":") + 1)
    readonly property var hunks: {
        const rows = []
        const lines = git.lines
        for (let i = 0; i < lines.length; ++i)
            if (/^@{2,} -/.test(lines[i])) rows.push(i)
        return rows
    }
    property int hunk: -1
    function showPath(path: string) {
        git.selectFile(path)
        patch.forceActiveFocus()
    }
    function jump(direction: int) {
        const count = hunks.length
        if (!count) return
        hunk = hunk < 0 ? (direction > 0 ? 0 : count - 1) : (hunk + direction + count) % count
        patch.positionViewAtIndex(hunks[hunk], ListView.Beginning)
        patch.forceActiveFocus()
    }
    Shortcut { sequence: "F7"; enabled: root.visible && root.activeFocus && root.hunks.length > 0; onActivated: root.jump(1) }
    Shortcut { sequence: "Shift+F7"; enabled: root.visible && root.activeFocus && root.hunks.length > 0; onActivated: root.jump(-1) }
    onHunksChanged: hunk = -1

    GitChanges {
        id: git
        folder: root.visible ? root.folder : ""
    }
    function translatedMessage(message: string): string {
        const end = message.indexOf("\n")
        return end < 0 ? Design.UiText.get(message) : Design.UiText.get(message.slice(0, end)) + message.slice(end)
    }
    function areaName(area: int): string {
        switch (area) {
        case GitChanges.Compared: return Design.UiText.get("版本差异")
        case GitChanges.Index: return Design.UiText.get("已暂存")
        case GitChanges.Untracked: return Design.UiText.get("未跟踪")
        case GitChanges.Conflict: return Design.UiText.get("冲突")
        default: return Design.UiText.get("未暂存")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Label {
                Layout.fillWidth: true
                text: Design.UiText.get("工作区变更") + "  ·  " + git.entries.length
                font.bold: true
                color: Design.Theme.text
            }
            Design.IconButton { iconName: "refresh"; tooltip: Design.UiText.get("刷新变更"); onClicked: git.refresh() }
        }
        Design.DiffScopeBar {
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8
            folder: root.folder
            changes: git
        }
        Label {
            visible: text.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            text: git.message ? root.translatedMessage(git.message)
                              : git.entries.length === 0 ? Design.UiText.get("工作区没有变更") : ""
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: Design.Theme.secondary
        }
        ListView {
            id: files
            Layout.fillWidth: true
            Layout.fillHeight: !git.selection
            Layout.preferredHeight: Math.min(contentHeight, root.height * 0.34)
            clip: true
            reuseItems: true
            model: git.entries
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                id: entry
                required property var modelData
                width: files.width
                height: 34
                hoverEnabled: true
                Accessible.name: root.areaName(modelData.area) + " " + modelData.path
                onClicked: { git.select(modelData.key); patch.forceActiveFocus() }
                background: Rectangle {
                    radius: 5
                    color: git.selection === entry.modelData.key ? Design.Theme.selected
                           : entry.hovered ? Design.Theme.hover : "transparent"
                    border.color: entry.visualFocus ? Design.Theme.accent : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 8
                    Label { Layout.leftMargin: 10; text: entry.modelData.code; font.family: Design.Theme.monoFont; color: Design.Theme.secondary }
                    Label {
                        Layout.fillWidth: true
                        text: entry.modelData.path
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: Design.Theme.text
                    }
                    Label { Layout.rightMargin: 4; text: root.areaName(entry.modelData.area); color: Design.Theme.secondary; font.pixelSize: 11 }
                    Design.IconButton {
                        readonly property bool staged: entry.modelData.area === GitChanges.Index
                        visible: actionBar.visible && git.branch.length > 0 && entry.modelData.area !== GitChanges.Conflict
                        enabled: !actionBar.busy
                        iconName: staged ? "minus" : "plus"
                        tooltip: Design.UiText.get(staged ? "撤销暂存" : "暂存文件") + " · " + entry.modelData.path
                        implicitWidth: 28; implicitHeight: 28; iconSize: 15
                        onClicked: actionBar.stageFile(entry.modelData.path, staged)
                    }
                }
            }
        }
        Rectangle { visible: !!git.selection; Layout.fillWidth: true; implicitHeight: 1; color: Design.Theme.border }
        RowLayout {
            visible: !!git.selection
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: root.selectedPath
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                font.bold: true
                color: Design.Theme.text
            }
            Label {
                visible: root.hunk >= 0
                text: (root.hunk + 1) + "/" + root.hunks.length
                color: Design.Theme.secondary
            }
            Design.IconButton { iconName: "back"; tooltip: Design.UiText.get("上一处差异") + " (Shift+F7)"; enabled: root.hunks.length > 0; onClicked: root.jump(-1) }
            Design.IconButton { iconName: "back"; rotation: 180; tooltip: Design.UiText.get("下一处差异") + " (F7)"; enabled: root.hunks.length > 0; onClicked: root.jump(1) }
            Design.IconButton { iconName: "folder"; tooltip: Design.UiText.get("在文件树中定位"); onClicked: root.revealRequested(root.selectedPath) }
            Design.IconButton { iconName: "file"; tooltip: Design.UiText.get("打开工作区文件"); onClicked: root.openRequested(root.selectedPath) }
        }
        ListView {
            id: patch
            visible: !!git.selection
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            model: git.lines
            ScrollBar.vertical: ScrollBar {}
            delegate: TextEdit {
                required property int index
                required property string modelData
                width: patch.width
                leftPadding: 10
                rightPadding: 10
                topPadding: 2
                bottomPadding: 2
                text: modelData
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                font.family: Design.Theme.monoFont
                font.bold: root.hunk >= 0 && root.hunks[root.hunk] === index
                font.pixelSize: 12
                color: modelData.startsWith("+++ ") || modelData.startsWith("--- ") || modelData.startsWith("diff ") || modelData.startsWith("index ") ? Design.Theme.secondary
                     : modelData.startsWith("+") ? Design.Theme.tokens.diffAdded
                     : modelData.startsWith("-") ? Design.Theme.danger
                     : modelData.startsWith("@@") ? Design.Theme.accent : Design.Theme.text
            }
        }
        Design.GitActionBar {
            id: actionBar
            Layout.fillWidth: true
            visible: root.writable && git.scope === GitChanges.Local
            folder: visible ? root.folder : ""
            changes: git
        }
    }
}
