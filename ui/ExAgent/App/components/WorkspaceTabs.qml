pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ExAgent.Chat
import ExAgent.App as Design

ListView {
    id: root
    required property WorkspaceController workspace
    orientation: ListView.Horizontal
    clip: true
    spacing: 2
    model: workspace.tabs
    delegate: Rectangle {
        id: tab
        required property var modelData
        required property int index
        width: Math.min(190, label.implicitWidth + 62)
        height: root.height
        radius: 6
        color: root.workspace.activeTab === index ? Design.Theme.background : "transparent"
        MouseArea { anchors.fill: parent; onClicked: root.workspace.selectTab(tab.index) }
        Design.Icon { x: 8; anchors.verticalCenter: parent.verticalCenter; width: 13; height: 13; name: "file" }
        Text { id: label; x: 28; width: parent.width - 58; anchors.verticalCenter: parent.verticalCenter; text: tab.modelData.name; font.pixelSize: 12; color: Design.Theme.text; elide: Text.ElideMiddle }
        Design.IconButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; implicitWidth: 26; implicitHeight: 26; iconSize: 12; iconName: "close"; tooltip: "关闭 " + tab.modelData.name; onClicked: root.workspace.closeTab(tab.index) }
    }
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 3 }
}
