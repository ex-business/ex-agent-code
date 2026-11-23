pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.App as Design
import ExAgent.Chat

Rectangle {
    id: root
    required property var entry
    property bool selected
    property bool collapsed
    property bool writable
    signal opened()
    signal toggled()
    signal newChat()
    signal bindFolder()
    signal moveHere()
    property bool canMove
    readonly property bool group: entry.kind === ProjectNavigator.ProjectRow || entry.kind === ProjectNavigator.WorkspaceRow
    readonly property bool project: entry.kind === ProjectNavigator.ProjectRow
    radius: 7
    color: selected ? Design.Theme.selected : hover.hovered ? Design.Theme.hover : "transparent"
    HoverHandler { id: hover }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.project ? 2 : root.entry.workspace ? 17 : 10
        anchors.rightMargin: 4
        spacing: 1
        Design.IconButton {
            visible: root.group
            implicitWidth: 22; implicitHeight: 26; iconSize: 14
            iconName: root.collapsed ? "right" : "down"
            tooltip: (root.collapsed ? "展开 " : "收起 ") + root.entry.title
            onClicked: root.toggled()
        }
        AbstractButton {
            id: label
            Layout.fillWidth: true; Layout.fillHeight: true
            Accessible.name: root.entry.title
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            onClicked: root.opened()
            contentItem: RowLayout {
                spacing: 6
                Design.Icon {
                    implicitWidth: 15; implicitHeight: 15
                    name: root.project ? "grid" : root.group ? "folder" : "chat"
                }
                Text {
                    Layout.fillWidth: true; elide: Text.ElideRight
                    text: root.entry.title
                    font.pixelSize: 13; font.weight: root.project ? Font.DemiBold : Font.Normal
                    color: Design.Theme.text
                }
            }
        }
        Design.IconButton {
            id: actions
            visible: root.group && root.writable
            implicitWidth: 28; implicitHeight: 26
            tooltip: root.entry.title + "操作"; iconName: "dots"
            onClicked: menu.popup()
            Design.ContextMenu {
                id: menu
                Design.ContextMenuItem { text: "在此新建聊天"; iconName: "new"; onTriggered: root.newChat() }
                Design.ContextMenuItem { visible: root.project; height: visible ? implicitHeight : 0; text: "添加工作区…"; iconName: "folder"; onTriggered: root.bindFolder() }
                Design.ContextMenuItem { enabled: root.canMove; text: "将当前聊天移到这里"; onTriggered: root.moveHere() }
            }
        }
    }
}
