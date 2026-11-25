pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: root
    required property string folder

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
                onClicked: git.select(modelData.key)
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
                    Label { Layout.rightMargin: 12; text: root.areaName(entry.modelData.area); color: Design.Theme.secondary; font.pixelSize: 11 }
                }
            }
        }
        Rectangle { visible: !!git.selection; Layout.fillWidth: true; implicitHeight: 1; color: Design.Theme.border }
        Label {
            visible: !!git.selection
            Layout.fillWidth: true
            Layout.margins: 10
            text: git.selection.slice(git.selection.indexOf(":") + 1)
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            font.bold: true
            color: Design.Theme.text
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
                font.pixelSize: 12
                color: modelData.startsWith("+++ ") || modelData.startsWith("--- ") || modelData.startsWith("diff ") || modelData.startsWith("index ") ? Design.Theme.secondary
                     : modelData.startsWith("+") ? Design.Theme.tokens.diffAdded
                     : modelData.startsWith("-") ? Design.Theme.danger
                     : modelData.startsWith("@@") ? Design.Theme.accent : Design.Theme.text
            }
        }
    }
}
